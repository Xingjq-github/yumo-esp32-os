#include "xiaozhi_audio.hpp"
#include "audio_mgr.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "esp_opus_enc.h"
#include "esp_opus_dec.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>

namespace fake {
int checks=0,scenarios=0;
void check(bool ok,const char *reason) {
    ++checks;
    if(!ok) {std::cerr<<"check failed: "<<reason<<"\n";std::exit(1);}
}
bool sleep=false,audio=true,busy=false,recording=false;
int rate=0,opens=0,closes=0,reads=0,writes=0,guards=0,allocs=0,failAlloc=0;
bool failOpen=false,failRead=false,failWrite=false,sleepAfterRead=false;
int stopAfterWrite=0,encOpen=0,encClose=0,decOpen=0,decClose=0;
int encMode=0,decMode=0,signalMode=0,decodedMs=60;
esp_opus_enc_config_t encoderConfig{};
esp_opus_dec_cfg_t decoderConfig{};
std::set<void*> memory;
std::vector<std::vector<int16_t>> encoded;
std::vector<int16_t> played;
std::vector<std::string> logs;
unsigned sequence=0;
void clean() {
    check(memory.empty(),"all PSRAM allocations released");
    check(encOpen==encClose,"all encoder handles released");
    check(decOpen==decClose,"all decoder handles released");
    check(!guards,"all CPU leases released");
}
void reset() {
    clean();++scenarios;
    sleep=false;audio=true;busy=false;recording=false;rate=0;
    opens=closes=reads=writes=guards=allocs=failAlloc=0;
    failOpen=failRead=failWrite=sleepAfterRead=false;
    stopAfterWrite=encOpen=encClose=decOpen=decClose=0;
    encMode=decMode=signalMode=0;decodedMs=60;sequence=0;
    encoded.clear();played.clear();logs.clear();
}
}
void xiaozhi_host_log(const char *tag,const char *format,...) {
    fake::check(std::strcmp(tag,"xiaozhi-audio")==0,"audio diagnostic tag");
    char message[128];
    va_list args;va_start(args,format);
    const int length=std::vsnprintf(message,sizeof(message),format,args);
    va_end(args);
    fake::check(length>0&&size_t(length)<sizeof(message),"diagnostics remain short");
    fake::logs.emplace_back(message);
}
void *heap_caps_malloc(size_t n,unsigned caps) {
    using namespace fake;
    check(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),"work buffers use PSRAM");
    if(++allocs==failAlloc) return nullptr;
    auto p=std::malloc(n);if(p)memory.insert(p);return p;
}
void *heap_caps_calloc(size_t n,size_t size,unsigned caps) {
    auto p=heap_caps_malloc(n*size,caps);if(p)std::memset(p,0,n*size);return p;
}
void heap_caps_free(void *p) {
    if(p) {fake::check(fake::memory.erase(p)==1,"free only owned allocation");std::free(p);}
}
namespace sys {
bool Power::sleepPending(){return fake::sleep;}
bool Power::audioMayRun(){return fake::audio;}
Power::WorkGuard::WorkGuard(){++fake::guards;}
Power::WorkGuard::~WorkGuard(){--fake::guards;}
bool Audio::openStream(bool record,int value) {
    using namespace fake;
    ++opens;if(failOpen||busy)return false;
    busy=true;recording=record;rate=value;return true;
}
void Audio::closeStream() {
    fake::check(fake::busy,"close only acquired hardware");
    fake::busy=false;++fake::closes;
}
bool Audio::readStream(void *data,int bytes) {
    using namespace fake;
    check(busy&&recording&&rate==16000,"capture format stereo 16 kHz");
    check(bytes==1280,"read exactly 20 ms of stereo PCM");++reads;
    if(failRead)return false;
    auto p=static_cast<int16_t*>(data);
    for(int i=0;i<320;++i,++sequence) {
        const int sample=signalMode==1?30000:signalMode==2?0:
            int(std::sin(double(sequence)*0.1)*18000.0);
        p[i*2]=int16_t(sample);
        p[i*2+1]=int16_t(signalMode==3?-sample:sample);
    }
    if(sleepAfterRead)sleep=true;
    return true;
}
bool Audio::writeStream(void *data,int bytes) {
    using namespace fake;
    check(busy&&!recording,"playback owns output stream");
    check(bytes>0&&bytes<=rate*20/1000*2&&bytes%2==0,"20 ms bounded output slice");
    ++writes;if(failWrite)return false;
    auto p=static_cast<int16_t*>(data);played.insert(played.end(),p,p+bytes/2);
    if(writes==stopAfterWrite)audio=false;
    return true;
}
}
extern "C" {
esp_audio_err_t esp_opus_enc_open(void *cfg,uint32_t size,void **out) {
    using namespace fake;
    check(size==sizeof(encoderConfig),"encoder config ABI");
    encoderConfig=*static_cast<esp_opus_enc_config_t*>(cfg);
    if(encMode==1){*out=nullptr;return ESP_AUDIO_ERR_MEM_LACK;}
    *out=new int(1);++encOpen;return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_enc_get_frame_size(void*,int *in,int *out) {
    *in=fake::encMode==2?640:1920;*out=fake::encMode==3?8192:128;
    return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_enc_process(void*,esp_audio_enc_in_frame_t *in,esp_audio_enc_out_frame_t *out) {
    using namespace fake;
    check(in->len==1920,"one 60 ms mono frame per packet");
    check(out->len>=128&&out->len<=core::xiaozhi::AudioBridge::PacketCapacity,"bounded encoder output");
    if(encMode==4)return ESP_AUDIO_ERR_FAIL;
    const auto p=reinterpret_cast<int16_t*>(in->buffer);
    encoded.emplace_back(p,p+960);
    out->encoded_bytes=encMode==5?out->len+1:encMode==6?0:encMode==7?1:8;
    if(out->encoded_bytes<=out->len)std::fill_n(out->buffer,out->encoded_bytes,uint8_t(0x7a));
    return ESP_AUDIO_ERR_OK;
}
void esp_opus_enc_close(void *p) {delete static_cast<int*>(p);++fake::encClose;}
esp_audio_err_t esp_opus_dec_open(void *cfg,uint32_t size,void **out) {
    using namespace fake;
    check(size==sizeof(decoderConfig),"decoder config ABI");
    decoderConfig=*static_cast<esp_opus_dec_cfg_t*>(cfg);
    if(decMode==1){*out=nullptr;return ESP_AUDIO_ERR_MEM_LACK;}
    *out=new int(2);++decOpen;return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_dec_decode(void*,esp_audio_dec_in_raw_t *raw,esp_audio_dec_out_frame_t *out,esp_audio_dec_info_t *info) {
    using namespace fake;
    check(raw->frame_recover==ESP_AUDIO_DEC_RECOVERY_NONE,"no unsolicited PLC");
    check(out->len==decoderConfig.sample_rate*120/1000*2,"decoder allocation capped at 120 ms");
    if(decMode==2)return ESP_AUDIO_ERR_FAIL;
    raw->consumed=decMode==3?raw->len-1:raw->len;
    out->decoded_size=decoderConfig.sample_rate*decodedMs/1000*2;
    if(decMode==4)out->decoded_size=out->len+2;
    if(decMode==5)out->decoded_size=3;
    if(decMode==6)out->decoded_size=0;
    info->sample_rate=decMode==7?44100:decoderConfig.sample_rate;
    info->channel=decMode==8?2:1;
    info->bits_per_sample=decMode==9?8:16;
    if(out->decoded_size<=out->len) {
        auto p=reinterpret_cast<int16_t*>(out->buffer);
        for(unsigned i=0;i<out->decoded_size/2;++i)p[i]=int16_t(int(i%1800)-900);
    }
    return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_dec_close(void *p) {delete static_cast<int*>(p);++fake::decClose;return ESP_AUDIO_ERR_OK;}
}

int main() {
    using core::xiaozhi::AudioBridge;
    using namespace fake;
    std::array<uint8_t,AudioBridge::PacketCapacity> packet{};
    unsigned level=0;
    reset();
    {
        AudioBridge b;check(b.beginCapture(),"capture begins");
        check(encoderConfig.sample_rate==16000&&encoderConfig.channel==1&&encoderConfig.bits_per_sample==16,
              "official capture format");
        check(encoderConfig.bitrate==24000&&encoderConfig.frame_duration==ESP_OPUS_ENC_FRAME_DURATION_60_MS&&
              encoderConfig.complexity==0&&encoderConfig.enable_dtx&&encoderConfig.enable_vbr,"bounded speech configuration");
        for(int i=0;i<300;++i) {
            const auto n=b.capture(packet.data(),packet.size(),level);
            check(n==(i%3==2?8:0),"20 ms chunks form 60 ms packets");
            check(level<=100,"input meter bounded");
        }
        check(encoded.size()==100,"one hundred packets, no skipped samples");
        check(logs.size()==4,"long recording logs opening and first encode only");
        check(logs[2].find("first encode begin")==0&&logs[3].find("first encode end")==0,
              "first encoder stage pair locates a crash");
        for(const auto& frame:encoded)for(auto sample:frame)check(sample>=-30000&&sample<=30000,"conditioner clips safely");
        check(b.finishCapture(packet.data(),packet.size())==0,"exact boundary has no added packet");
        b.endCapture();b.stop();check(closes==1,"idempotent stop");
    }
    clean();
    for(int chunks=1;chunks<=2;++chunks) {
        reset();AudioBridge b;check(b.beginCapture(),"tail begins");
        for(int i=0;i<chunks;++i)check(b.capture(packet.data(),packet.size(),level)==0,"tail pending");
        check(b.finishCapture(packet.data(),packet.size())==8,"tail encoded");
        check(std::all_of(encoded.back().begin()+320*chunks,encoded.back().end(),[](auto v){return v==0;}),"tail padded only after captured samples");
        check(b.finishCapture(packet.data(),packet.size())==0,"tail sent once");
        b.endCapture();clean();
    }
    for(int mode=1;mode<=3;++mode) {
        reset();signalMode=mode;AudioBridge b;check(b.beginCapture(),"conditioner input begins");
        for(int i=0;i<3;++i)b.capture(packet.data(),packet.size(),level);
        if(mode==1||mode==2)check(std::all_of(encoded[0].begin(),encoded[0].end(),[](auto v){return v==0;}),"DC/silence removed");
        if(mode==3)check(std::any_of(encoded[0].begin(),encoded[0].end(),[](auto v){return v>1000;}),"antiphase microphones do not cancel");
        b.stop();clean();
    }
    for(int mode=1;mode<=7;++mode) {
        reset();encMode=mode;AudioBridge b;
        const bool started=b.beginCapture();
        if(mode<=3)check(!started,"encoder allocation/format error rejected");
        else {check(started,"encoding failure setup");b.capture(packet.data(),packet.size(),level);b.capture(packet.data(),packet.size(),level);
              check(b.capture(packet.data(),packet.size(),level)==(mode==7?1:-1),"encoder result bounds/DTX");}
        b.stop();clean();
    }
    for(int allocation=1;allocation<=2;++allocation) {
        reset();failAlloc=allocation;AudioBridge b;check(!b.beginCapture(),"allocation failure handled");b.stop();clean();
    }
    reset();{AudioBridge b;busy=true;check(!b.beginCapture(),"busy hardware rejected");check(closes==0&&busy,"failed ownership never closes another app");busy=false;clean();}
    reset();{AudioBridge b;check(b.beginCapture(),"cancel tail begins");b.capture(packet.data(),packet.size(),level);b.endCapture();check(encoded.empty(),"cancel discards audio");clean();}
    reset();{AudioBridge b;check(b.beginCapture(),"I2S failure setup");failRead=true;check(b.capture(packet.data(),packet.size(),level)==-1,"I2S read failure closes");clean();}
    reset();{AudioBridge b;check(b.beginCapture(),"sleep during read setup");sleepAfterRead=true;check(b.capture(packet.data(),packet.size(),level)==-1,"sleep never transmits trailing read");check(encoded.empty(),"no encode after sleep");clean();}
    reset();{AudioBridge b;check(b.beginCapture(),"small output setup");check(b.capture(packet.data(),1,level)==-1,"small output rejected before read");check(!reads,"no read on small output");clean();}
    reset();{AudioBridge b;sleep=true;check(!b.beginCapture()&&!opens&&!allocs,"sleep blocks lazy start");}
    reset();{AudioBridge b;check(b.beginCapture(),"power check reuse setup");audio=false;check(!b.beginCapture(),"power stops already opened capture");clean();}
    for(int sampleRate:{8000,12000,16000,24000,48000})for(int duration:{20,60,120}) {
        reset();decodedMs=duration;AudioBridge b;check(b.beginPlayback(sampleRate),"legal playback rate");
        check(b.beginPlayback(sampleRate)&&opens==1,"same stream reused");
        check(b.play(packet.data(),8),"valid decoded packet");
        check(played.size()==size_t(sampleRate*duration/1000)&&writes==duration/20,"exact sample count and slices");
        for(size_t i=0;i<played.size();++i)check(played[i]==int16_t(int(i%1800)-900),"PCM unchanged on playback");
        check(logs.size()==4,"decoder open/reuse and first-frame diagnostics bounded");
        check(b.play(packet.data(),8),"second decoded packet");
        check(logs.size()==4,"no repeated logs on later playback packets");
        b.endCapture();check(busy,"capture cleanup cannot close playback");b.stop();clean();
    }
    for(int sampleRate:{0,7999,11025,22050,44100,48001,-1}) {
        reset();AudioBridge b;check(!b.beginPlayback(sampleRate)&&!opens&&!allocs,"non-Opus rates rejected before allocation");
    }
    for(int mode=1;mode<=9;++mode) {
        reset();decMode=mode;AudioBridge b;
        if(mode==1)check(!b.beginPlayback(24000),"decoder allocation failure");
        else {check(b.beginPlayback(24000),"decoder error setup");check(!b.play(packet.data(),8),"truncated/invalid/oversize metadata rejected");check(!writes,"no invalid PCM sent to hardware");}
        clean();
    }
    reset();{AudioBridge b;check(b.beginPlayback(24000),"oversized input setup");check(!b.play(packet.data(),packet.size()+1),"input packet capped");clean();}
    reset();{AudioBridge b;check(b.beginPlayback(24000),"null input setup");check(!b.play(nullptr,8),"null input rejected");clean();}
    reset();{AudioBridge b;check(b.beginPlayback(24000),"empty input setup");check(!b.play(packet.data(),0),"empty input rejected");clean();}
    reset();{AudioBridge b;check(b.beginPlayback(24000),"output fault setup");failWrite=true;check(!b.play(packet.data(),8),"output fault closes stream");clean();}
    reset();{AudioBridge b;check(b.beginPlayback(24000),"power output setup");stopAfterWrite=1;check(!b.play(packet.data(),8)&&writes==1,"power loss fences next 20 ms slice");clean();}
    reset();{AudioBridge b;check(b.beginPlayback(16000),"rate switch setup");check(b.beginPlayback(24000)&&opens==2&&closes==1,"rate switch releases old codec");check(b.beginCapture()&&opens==3&&closes==2,"record/play switch exclusive");b.stop();clean();}
    reset();{AudioBridge b;check(b.beginPlayback(24000),"reuse power setup");audio=false;check(!b.beginPlayback(24000),"power stops existing output");clean();}
    reset();{AudioBridge b;check(b.beginCapture(),"destructor setup");}
    clean();check(closes==1,"destructor releases hardware");
    std::cout<<scenarios<<" scenarios / "<<checks<<" checks passed\n";
}
