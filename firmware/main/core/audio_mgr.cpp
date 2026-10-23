/*
 * Shared ES8311 speaker-volume controller.
 */
#include "audio_mgr.hpp"

#include "settings_store.hpp"
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <mutex>

namespace sys {
namespace {

const char *TAG = "audio";
esp_codec_dev_handle_t s_speaker = nullptr;
bool s_suspended = false;
std::recursive_mutex codecMutex;
esp_codec_dev_handle_t s_microphone=nullptr, s_stream=nullptr;
bool s_recording=false;

int clampVolume(int value)
{
    if (value < 0) return 0;
    if (value > 100) return 100;
    return value;
}

// Called only under codecMutex, after a stream owner has stopped I/O.
void releaseAudioLocked()
{
    if(s_stream) (void)esp_codec_dev_close(s_stream);
    s_stream=nullptr;
    s_recording=false;
    const auto err=bsp_audio_deinit();
    s_speaker=s_microphone=nullptr;
    if(err!=ESP_OK) ESP_LOGW(TAG,"Audio resource cleanup: %s",esp_err_to_name(err));
}

} // namespace

bool Audio::init(void)
{
    const std::lock_guard<std::recursive_mutex> lock(codecMutex);
    if (s_speaker) return true;
    s_speaker = bsp_audio_codec_speaker_init();
    if (!s_speaker) {
        releaseAudioLocked();
        ESP_LOGE(TAG, "ES8311 speaker initialization failed");
        return false;
    }

    const int volume = Settings::volume();
    const int result = esp_codec_dev_set_out_vol(s_speaker, volume);
    if(result!=0) releaseAudioLocked();
    return result == 0;
}

bool Audio::applyVolume(int value)
{
    const std::lock_guard<std::recursive_mutex> lock(codecMutex);
    const int volume = clampVolume(value);
    // Changing the preference must not allocate idle I2S/DMA buffers.
    // Settings is updated by the caller; the next playback applies it.
    if (!s_speaker || s_suspended) return true;

    const int result = esp_codec_dev_set_out_vol(s_speaker, volume);
    if(result!=0) ESP_LOGW(TAG, "Cannot set speaker volume: %d", result);
    return result == 0;
}

void Audio::suspendForStandby()
{
    const std::lock_guard<std::recursive_mutex> lock(codecMutex);
    if(s_suspended || s_stream) return;
    s_suspended=true;
    releaseAudioLocked();
}

bool Audio::resumeFromStandby()
{
    const std::lock_guard<std::recursive_mutex> lock(codecMutex);
    if(!s_suspended) return true;
    // Do not open an idle codec/I2S stream or force PA on. Playback opens it later.
    s_suspended=false;
    return true;
}


bool Audio::openStream(bool record,int rate) {
    const std::lock_guard<std::recursive_mutex> lock(codecMutex);
    if(s_stream || rate<8000 || rate>48000 || !resumeFromStandby()) return false;
    if(record) {
        if(!s_microphone) s_microphone=bsp_audio_codec_microphone_init();
        if(!s_microphone) { releaseAudioLocked();return false; }
        s_stream=s_microphone;
    } else {
        if(!init()) return false;
        s_stream=s_speaker;
    }
    esp_codec_dev_sample_info_t format{};
    format.sample_rate=rate;format.bits_per_sample=16;
    // Both physical microphones must reach the recorder, in I2S slot order.
    constexpr uint16_t micChannels=ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0) |
                                   ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
    format.channel=record?2:1;
    format.channel_mask=record?micChannels:0;
    if(esp_codec_dev_open(s_stream,&format)!=0) {
        releaseAudioLocked();return false;
    }
    s_recording=record;
    if(record) {
        // ES7210 MIC1/MIC2: 0x43/0x44 low nibble 13 is 36 dB.
        // The channel-gain wrapper discards lower-level I2C errors. Verify
        // BOTH registers once on open/resume, never in the capture loop.
        constexpr int micGainFirstRegister=0x43, micGainCode=13;
        bool gainOk=esp_codec_dev_set_in_channel_gain(
            s_stream,micChannels,36.0f)==ESP_CODEC_DEV_OK;
        for(int channel=0;channel<2;++channel) {
            int gainRegister=-1;
            const bool readOk=esp_codec_dev_read_reg(
                s_stream,micGainFirstRegister+channel,&gainRegister)==ESP_CODEC_DEV_OK;
            gainOk=gainOk && readOk && (gainRegister&0x0f)==micGainCode;
        }
        if(!gainOk) {
            ESP_LOGE(TAG,"Dual microphone gain setup/readback failed");
            releaseAudioLocked();
            return false;
        }
    } else (void)esp_codec_dev_set_out_vol(s_stream,Settings::volume());
    return true;
}
bool Audio::readStream(void *data,int bytes) {
    const std::lock_guard<std::recursive_mutex> lock(codecMutex);
    return s_stream && s_recording && esp_codec_dev_read(s_stream,data,bytes)==0;
}
bool Audio::writeStream(void *data,int bytes) {
    const std::lock_guard<std::recursive_mutex> lock(codecMutex);
    return s_stream && !s_recording && esp_codec_dev_write(s_stream,data,bytes)==0;
}
void Audio::closeStream() {
    const std::lock_guard<std::recursive_mutex> lock(codecMutex);
    releaseAudioLocked();
}
bool Audio::releaseIdle() {
    // System worker must never wait for an active recorder/player read/write.
    const std::unique_lock<std::recursive_mutex> lock(codecMutex,std::try_to_lock);
    if(!lock.owns_lock() || s_stream) return false;
    releaseAudioLocked();
    return true;
}
} // namespace sys
