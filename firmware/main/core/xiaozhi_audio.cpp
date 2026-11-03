#include "xiaozhi_audio.hpp"
#include "audio_mgr.hpp"
#include "dual_mic_input.hpp"
#include "recording_level.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_opus_enc.h"
#include "esp_opus_dec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <climits>
#include <new>

namespace core::xiaozhi {
namespace {
constexpr char Tag[]="xiaozhi-audio";
void traceStage(const char *stage) {
    // One log per transition/first codec frame, never per 20-ms capture block.
    // No cloud credentials, transcripts or microphone data enter diagnostics.
    ESP_LOGI(Tag,"%s: stack free_min=%u",stage,
             unsigned(uxTaskGetStackHighWaterMark(nullptr)));
}
bool mayRun() {
    return !sys::Power::sleepPending() && sys::Power::audioMayRun();
}
bool opusRate(int value) {
    return value==8000 || value==12000 || value==16000 ||
           value==24000 || value==48000;
}
constexpr unsigned MemoryCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
}

struct AudioBridge::State {
    void *encoder=nullptr, *decoder=nullptr;
    int16_t *pcm=nullptr;
    size_t capacity=0, used=0;
    int outputBytes=0, rate=0;
    bool opened=false, recording=false, codecTraced=false;
    recorder::DualMicInput microphones;
    recorder::RecordingLevel level;
    int16_t interleaved[CaptureChunkFrames * recorder::DualMicInput::Channels]{};
};

AudioBridge::~AudioBridge() { stop(); }

bool AudioBridge::allocate(size_t frames) {
    auto memory=heap_caps_malloc(sizeof(State),MemoryCaps);
    if(!memory) return false;
    state_=new(memory) State;
    state_->pcm=static_cast<int16_t *>(heap_caps_calloc(frames,sizeof(int16_t),MemoryCaps));
    if(!state_->pcm) {stop();return false;}
    state_->capacity=frames;
    return true;
}

bool AudioBridge::beginCapture() {
    if(state_ && state_->recording && state_->opened) {
        if(mayRun()) return true;
        stop();return false;
    }
    stop();
    if(!mayRun() || !allocate(CapturePacketFrames)) return false;
    auto &s=*state_;
    s.recording=true;
    s.rate=CaptureRate;
    traceStage("capture encoder open");
    esp_opus_enc_config_t config{};
    config.sample_rate=CaptureRate;
    config.channel=ESP_AUDIO_MONO;
    config.bits_per_sample=ESP_AUDIO_BIT16;
    config.bitrate=24000;
    config.frame_duration=ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    config.application_mode=ESP_OPUS_ENC_APPLICATION_VOIP;
    config.complexity=0;
    config.enable_fec=false;
    config.enable_dtx=true;
    config.enable_vbr=true;
    int inputBytes=0;
    if(esp_opus_enc_open(&config,sizeof(config),&s.encoder)!=ESP_AUDIO_ERR_OK ||
       !s.encoder ||
       esp_opus_enc_get_frame_size(s.encoder,&inputBytes,&s.outputBytes)!=ESP_AUDIO_ERR_OK ||
       inputBytes!=int(CapturePacketFrames*sizeof(int16_t)) ||
       s.outputBytes<=0 || size_t(s.outputBytes)>PacketCapacity || !mayRun()) {
        stop();return false;
    }
    s.opened=sys::Audio::openStream(true,CaptureRate);
    if(!s.opened) {stop();return false;}
    traceStage("capture ready");
    return true;
}

int AudioBridge::encode(uint8_t *output,size_t capacity) {
    if(!state_ || !state_->encoder || !output ||
       capacity<size_t(state_->outputBytes) || !mayRun()) {stop();return -1;}
    auto &s=*state_;
    esp_audio_enc_in_frame_t input{};
    input.buffer=reinterpret_cast<uint8_t *>(s.pcm);
    input.len=CapturePacketFrames*sizeof(int16_t);
    esp_audio_enc_out_frame_t encoded{};
    encoded.buffer=output;
    encoded.len=static_cast<uint32_t>(std::min(capacity,PacketCapacity));
    if(!s.codecTraced) traceStage("first encode begin");
    const auto result=esp_opus_enc_process(s.encoder,&input,&encoded);
    if(!s.codecTraced) {traceStage("first encode end");s.codecTraced=true;}
    if(result!=ESP_AUDIO_ERR_OK || !encoded.encoded_bytes ||
       encoded.encoded_bytes>encoded.len || encoded.encoded_bytes>INT_MAX || !mayRun()) {
        stop();return -1;
    }
    s.used=0;
    return static_cast<int>(encoded.encoded_bytes);
}

int AudioBridge::capture(uint8_t *output,size_t capacity,unsigned &level) {
    level=0;
    if(!state_ || !state_->recording || !state_->opened || !output ||
       capacity<size_t(state_->outputBytes) || !mayRun()) {stop();return -1;}
    sys::Power::WorkGuard work;
    auto &s=*state_;
    if(!sys::Audio::readStream(s.interleaved,sizeof(s.interleaved)) || !mayRun()) {
        stop();return -1;
    }
    static_assert(CaptureChunkFrames<=recorder::DualMicInput::MaxFrames);
    s.microphones.process(s.interleaved,CaptureChunkFrames);
    const unsigned peak=s.level.processDcBlocked(s.interleaved,CaptureChunkFrames);
    level=std::min(100U,peak*100U/unsigned(recorder::RecordingLevel::Ceiling));
    std::copy_n(s.interleaved,CaptureChunkFrames,s.pcm+s.used);
    s.used+=CaptureChunkFrames;
    return s.used==CapturePacketFrames?encode(output,capacity):0;
}

int AudioBridge::finishCapture(uint8_t *output,size_t capacity) {
    if(!state_ || !state_->recording || !state_->opened) return 0;
    if(!mayRun()) {stop();return -1;}
    sys::Power::WorkGuard work;
    if(!state_->used) return 0;
    std::fill(state_->pcm+state_->used,state_->pcm+CapturePacketFrames,int16_t(0));
    return encode(output,capacity);
}

void AudioBridge::endCapture() {
    if(state_ && state_->recording) stop();
}

bool AudioBridge::beginPlayback(int rate) {
    if(state_ && !state_->recording && state_->opened && state_->rate==rate) {
        if(mayRun()) return true;
        stop();return false;
    }
    stop();
    // A received Opus packet may contain up to 120 ms. Allocate a bounded
    // buffer once, never resize it based on an untrusted decoded length.
    if(!opusRate(rate) || !mayRun() || !allocate(size_t(rate)*120/1000)) return false;
    auto &s=*state_;
    s.rate=rate;
    traceStage("playback decoder open");
    esp_opus_dec_cfg_t config{};
    config.sample_rate=rate;
    config.channel=ESP_AUDIO_MONO;
    config.frame_duration=ESP_OPUS_DEC_FRAME_DURATION_120_MS;
    config.self_delimited=false;
    if(esp_opus_dec_open(&config,sizeof(config),&s.decoder)!=ESP_AUDIO_ERR_OK ||
       !s.decoder || !mayRun()) {stop();return false;}
    s.opened=sys::Audio::openStream(false,rate);
    if(!s.opened) {stop();return false;}
    traceStage("playback ready");
    return true;
}

bool AudioBridge::play(const uint8_t *packet,size_t bytes) {
    if(!state_ || state_->recording || !state_->opened || !state_->decoder ||
       !packet || !bytes || bytes>PacketCapacity || !mayRun()) {stop();return false;}
    sys::Power::WorkGuard work;
    auto &s=*state_;
    esp_audio_dec_in_raw_t raw{};
    raw.buffer=const_cast<uint8_t *>(packet);
    raw.len=static_cast<uint32_t>(bytes);
    raw.frame_recover=ESP_AUDIO_DEC_RECOVERY_NONE;
    esp_audio_dec_out_frame_t frame{};
    frame.buffer=reinterpret_cast<uint8_t *>(s.pcm);
    frame.len=s.capacity*sizeof(int16_t);
    esp_audio_dec_info_t info{};
    if(!s.codecTraced) traceStage("first decode begin");
    const auto result=esp_opus_dec_decode(s.decoder,&raw,&frame,&info);
    if(!s.codecTraced) {traceStage("first decode end");s.codecTraced=true;}
    if(result!=ESP_AUDIO_ERR_OK ||
       raw.consumed!=bytes || !frame.decoded_size || frame.decoded_size>frame.len ||
       frame.decoded_size%sizeof(int16_t) || info.sample_rate!=uint32_t(s.rate) ||
       info.channel!=ESP_AUDIO_MONO || info.bits_per_sample!=ESP_AUDIO_BIT16) {
        stop();return false;
    }
    const size_t frames=frame.decoded_size/sizeof(int16_t);
    const size_t block=size_t(s.rate)*20/1000;
    for(size_t offset=0;offset<frames;offset+=block) {
        if(!mayRun() || !sys::Audio::writeStream(s.pcm+offset,
                int(std::min(block,frames-offset)*sizeof(int16_t)))) {
            stop();return false;
        }
    }
    return true;
}

void AudioBridge::stop() {
    if(!state_) return;
    auto s=state_;
    state_=nullptr;
    // An open failure may mean another App owns the codec. Only the worker
    // that successfully acquired the stream may close it.
    if(s->opened) sys::Audio::closeStream();
    if(s->encoder) esp_opus_enc_close(s->encoder);
    if(s->decoder) (void)esp_opus_dec_close(s->decoder);
    heap_caps_free(s->pcm);
    s->~State();
    heap_caps_free(s);
}
} // namespace core::xiaozhi
