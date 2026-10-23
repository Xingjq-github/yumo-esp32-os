#include "mp3_player.hpp"
#include "audio_mgr.hpp"
#include "audio_recorder.hpp"
#include "chiptune_player.hpp"
#include "game_audio.hpp"
#include "fc_emulator.hpp"
#include "power_mgr.hpp"
#include "settings_store.hpp"
#include "charset_conv.hpp"
#include "minimp3_ex.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <climits>
#include <cmath>
#include <array>
#include <dirent.h>
#include <sys/stat.h>

namespace core::mp3 {
namespace {
std::atomic<bool> active{false},scanning{false};
using File=std::unique_ptr<FILE,decltype(&std::fclose)>;
bool stopping(const std::atomic<bool>&cancel) {
    const auto s=sd::state();
    return cancel.load()||sys::Power::sleepPending()||s==sd::State::Ejecting||s==sd::State::Ejected||s==sd::State::Failed||s==sd::State::Usb;
}
void scanWorker(void *arg) {
    {
        auto holder=static_cast<std::shared_ptr<Listing>*>(arg);auto job=*holder;delete holder;
        try {
            sd::Session session(job->access);
            if(!session)job->error="SD卡不可用";
            else {
                std::unique_ptr<DIR,decltype(&closedir)> dir(opendir(Directory),closedir);
                if(!dir)job->error="请将音乐放入 SD /MP3";
                else {
                    unsigned count=0;
                    while(!stopping(job->cancel)) {
                        errno=0;auto item=readdir(dir.get());
                        if(!item){if(errno){session.reportError(errno);job->error="目录读取失败";}break;}
                        if(++count>8192){job->truncated=true;break;}
                        if(count%16==0)vTaskDelay(1);
                        if(!extension(item->d_name,".mp3"))continue;
                        std::string path=std::string(Directory)+"/"+item->d_name;
                        struct stat st{};if(stat(path.c_str(),&st)||!S_ISREG(st.st_mode))continue;
                        Entry entry{path,shortText(ensureUtf8(item->d_name))};
                        if(extension(entry.title,".mp3"))entry.title.resize(entry.title.size()-4);
                        auto at=std::lower_bound(job->entries.begin(),job->entries.end(),entry,[](const Entry&a,const Entry&b){return a.path<b.path;});
                        if(job->entries.size()<MaxTracks)job->entries.insert(at,std::move(entry));
                        else {job->truncated=true;if(at!=job->entries.end()){job->entries.insert(at,std::move(entry));job->entries.pop_back();}}
                    }
                    if(job->entries.empty()&&job->error.empty())job->error="SD /MP3 中没有音乐";
                }
            }
        }catch(const std::bad_alloc&){job->entries.clear();job->error="内存不足";}
        job->done.store(true,std::memory_order_release);
    }
    scanning=false;vTaskDeleteWithCaps(nullptr);
}
struct Input {
    FILE *file;Player &job;sd::Session &session;uint64_t size;bool error=false;
    static size_t read(void *data,size_t size,void *arg) {
        auto &s=*static_cast<Input*>(arg);size_t total=0;
        while(total<size&&!stopping(s.job.cancel)) {
            size_t want=std::min(size-total,size_t(4096));
            const auto n=fread(static_cast<uint8_t*>(data)+total,1,want,s.file);total+=n;
            if(n<want) {
                if(ferror(s.file)){s.error=true;s.session.reportError(errno?errno:EIO);}
                break;
            }
            // Keep cancellation bounded without stretching every 128 KiB refill
            // by 32 ticks. The independent PCM sink supplies audio meanwhile.
            if(total%16384==0)vTaskDelay(1);
        }
        return total;
    }
    static int seek(uint64_t pos,void *arg) {
        auto &s=*static_cast<Input*>(arg);
        if(stopping(s.job.cancel)||pos>s.size||pos>LONG_MAX)return -1;
        if(fseek(s.file,long(pos),SEEK_SET)) {s.error=true;s.session.reportError(errno?errno:EIO);return -1;}
        return 0;
    }
};
struct Decoder {mp3dec_ex_t value{};~Decoder(){mp3dec_ex_close(&value);}};
struct Output {
    bool opened=false;int channels=0,rate=0;
    float gain=0;
    ~Output(){close();}
    void close() {
        if(opened){sys::Audio::closeStream();opened=false;}
        gain=0;
    }
    bool open(int hz,int ch) {
        close();rate=hz;channels=ch;
        return opened=sys::Audio::openStream(false,rate);
    }
    bool write(int16_t *pcm,unsigned frames,bool fadeOut,uint32_t &level) {
        int16_t out[576];uint64_t energy=0;
        for(unsigned i=0;i<frames;++i) {
            // The board codec controls volume; preserve the 12 ms edge fade.
            gain=fadeOut?std::max(0.f,gain-1.f/(rate*.012f)):std::min(1.f,gain+1.f/(rate*.012f));
            const int16_t left=pcm[i*channels],right=channels==2?pcm[i*channels+1]:left;
            const int16_t m=mono(left,right);energy+=int64_t(m)*m;
            out[i]=int16_t(m*gain);
        }
        level=uint32_t(std::min(100.f,std::sqrt(float(energy)/std::max(1u,frames))/120.f));
        return sys::Audio::writeStream(out,frames*2);
    }
};
// Single producer / single consumer. Decode and SD refills never run on the
// paced I2S task. 32 blocks hold 384 ms at 48 kHz, in PSRAM; start with
// 288 ms ready so a normal SD refill cannot empty the hardware DMA buffer.
struct OutputQueue {
    static constexpr unsigned Capacity=32,Prebuffer=24;
    struct Block {int16_t pcm[576*2];unsigned samples=0;uint64_t end=0;};
    Player &job;const unsigned rate,channels;const uint64_t total;
    std::array<Block,Capacity> blocks;
    std::atomic<unsigned> read{0},written{0};
    std::atomic<bool> hold{false},held{false},stop{false},eof{false},done{false};
    const char *error=nullptr; // Published by done, read only after joining.
    bool started=false;
    OutputQueue(Player &p,unsigned r,unsigned c,uint64_t n):job(p),rate(r),channels(c),total(n){}
    ~OutputQueue(){finish();}
    bool halted()const{return stop.load()||stopping(job.cancel);}
    unsigned size()const{return written.load(std::memory_order_acquire)-read.load(std::memory_order_acquire);}
    void finish() {
        if(!started)return;
        stop=true;
        while(!done.load(std::memory_order_acquire))vTaskDelay(1);
    }
    bool suspend() {
        hold=true;
        while(!held.load(std::memory_order_acquire)&&!done.load()&&!halted())vTaskDelay(1);
        return held.load()&&!done.load()&&!halted();
    }
    void reset() {
        // Only the producer touches decoder state; the sink has acknowledged
        // that the old stream is closed and no old block remains in use.
        read=0;written=0;eof=false;hold=false;
        // Complete both halves of the handshake before another rapid seek.
        while(held.load(std::memory_order_acquire)&&!done.load()&&!halted())vTaskDelay(1);
    }
    bool start() {
        started=xTaskCreatePinnedToCoreWithCaps(worker,"mp3_sink",12288,this,5,nullptr,0,
            MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS;
        return started;
    }
    bool send(Output &output,bool fade) {
        const auto r=read.load(std::memory_order_relaxed);
        auto &b=blocks[r%Capacity];uint32_t level=0;
        if(!output.write(b.pcm,b.samples/channels,fade||b.end>=total,level)) {
            error="扬声器输出失败";return false;
        }
        job.position=uint32_t(b.end*1000/(rate*channels));job.level=level;
        read.store(r+1,std::memory_order_release);return true;
    }
    void run() {
        Output output;std::unique_ptr<sys::Power::WorkGuard> work;
        bool buffering=true,remembered=false;
        while(!halted()) {
            if(hold.load()) {
                output.close();work.reset();buffering=true;job.level=0;
                held.store(true,std::memory_order_release);
                while(hold.load()&&!halted())vTaskDelay(1);
                held.store(false,std::memory_order_release);
                continue;
            }
            if(!job.play.load()) {
                if(output.opened) {
                    if(size()&&!send(output,true))break;
                    output.close();
                }
                work.reset();buffering=true;job.level=0;job.state=State::Paused;
                vTaskDelay(pdMS_TO_TICKS(5));continue;
            }
            const auto available=size();
            if(!available&&eof.load(std::memory_order_acquire)&&!size()) {
                job.position=job.duration.load();job.state=State::Ended;break;
            }
            if(!work)work=std::make_unique<sys::Power::WorkGuard>();
            if(buffering&&available<Prebuffer&&!eof.load()) {
                vTaskDelay(1);continue;
            }
            if(!available) {
                // Count starvation once per rebuffer, never silently skip PCM.
                ++job.underruns;buffering=true;job.level=0;job.state=State::Loading;
                vTaskDelay(1);continue;
            }
            if(!output.opened&&!output.open(rate,channels)) {
                error="扬声器输出不可用";break;
            }
            buffering=false;job.outputRate=rate;
            if(!send(output,false))break;
            job.state=State::Playing;
            if(!remembered) {
                // RAM-only preference change; SystemService batches NVS writes.
                sys::Settings::rememberMp3Track(job.path);remembered=true;
            }
            // FreeRTOS taskYIELD only admits equal-priority peers. Sleep one
            // tick with the codec mutex released so the lower-priority system
            // worker can apply volume and process radios between audio writes.
            // Hardware DMA buffering continues during this scheduling gap.
            vTaskDelay(1);
        }
        
    }
    static void worker(void *arg) {
        auto &q=*static_cast<OutputQueue*>(arg);
        try{q.run();}catch(const std::bad_alloc&){q.error="播放内存不足";}
        q.done.store(true,std::memory_order_release);
        vTaskDeleteWithCaps(nullptr);
    }
};
void lyrics(Player &job) {
    const auto base=job.path.substr(0,job.path.size()-4);
    File file(fopen((base+".lrc").c_str(),"rb"),fclose);
    if(!file)file.reset(fopen((base+".LRC").c_str(),"rb"));
    if(!file)return;
    std::string text;char buf[2048];
    while(!stopping(job.cancel)) {
        auto n=fread(buf,1,sizeof(buf),file.get());if(!n)break;
        if(text.size()+n>MaxLrcBytes)return;
        text.append(buf,n);vTaskDelay(1);
    }
    if(!stopping(job.cancel)&&!ferror(file.get()))job.lyrics=parseLrc(decodeText(text));
}
void fail(Player &job,const char *message) {
    // Once published, the GUI can read this string concurrently.
    if(job.state.load()==State::Failed)return;
    job.error=message;job.state.store(State::Failed,std::memory_order_release);
}
void playFile(Player &job) {
    auto work=std::make_unique<sys::Power::WorkGuard>();
    sd::Session session(job.access);
    if(!session){fail(job,"SD卡不可用");return;}
    struct stat st{};
    if(stat(job.path.c_str(),&st)||st.st_size<=0||st.st_size>512*1024*1024) {fail(job,"文件无效或超过512MiB");return;}
    File file(fopen(job.path.c_str(),"rb"),fclose);
    if(!file){session.reportError(errno);fail(job,"无法打开音乐");return;}
    lyrics(job);
    job.metadataReady.store(true,std::memory_order_release);
    auto decoder=std::make_unique<Decoder>();auto &d=decoder->value;
    Input input{file.get(),job,session,uint64_t(st.st_size)};
    mp3dec_io_t io{Input::read,&input,Input::seek,&input};
    int rc=mp3dec_ex_open_cb(&d,&io,MP3D_SEEK_TO_SAMPLE);
    if(stopping(job.cancel))return;
    if(rc||d.info.layer!=3||d.info.hz<8000||d.info.hz>48000||d.info.channels<1||d.info.channels>2||!d.samples) {
        fail(job,rc==MP3D_E_MEMORY?"歌曲索引超过内存预算":"MP3格式无效或文件已损坏");return;
    }
    job.duration=uint32_t(d.samples*1000/(d.info.hz*d.info.channels));
    job.rate=d.info.hz;job.bitrate=d.info.bitrate_kbps;
    uint64_t decoded=0;const char *decodeError=nullptr;
    auto queue=std::make_unique<OutputQueue>(job,d.info.hz,d.info.channels,d.samples);
    if(!queue->start()){fail(job,"音频缓冲任务启动失败");return;}
    while(!stopping(job.cancel)) {
        if(queue->done.load(std::memory_order_acquire))break;
        const auto seek=job.seekMs.exchange(UINT32_MAX);
        if(seek!=UINT32_MAX) {
            if(!queue->suspend())break;
            job.state=State::Seeking;
            if(!work)work=std::make_unique<sys::Power::WorkGuard>();
            decoded=std::min(d.samples,uint64_t(seek)*d.info.hz/1000*d.info.channels);
            if(mp3dec_ex_seek(&d,decoded)) {if(!stopping(job.cancel))decodeError="定位失败，歌曲索引过大或已损坏";break;}
            job.position=uint32_t(decoded*1000/(d.info.hz*d.info.channels));
            queue->reset();
        }
        if(!job.play.load()) {
            work.reset();vTaskDelay(pdMS_TO_TICKS(5));continue;
        }
        if(queue->size()>=OutputQueue::Capacity||queue->eof.load()) {
            vTaskDelay(1);continue;
        }
        // The validated index/tag is the audio boundary. Reading beyond it can
        // interpret trailing tags/padding as a broken frame and discard the tail.
        if(decoded>=d.samples){queue->eof.store(true,std::memory_order_release);continue;}
        if(!work)work=std::make_unique<sys::Power::WorkGuard>();
        const auto w=queue->written.load(std::memory_order_relaxed);
        auto &block=queue->blocks[w%OutputQueue::Capacity];
        const auto n=mp3dec_ex_read(&d,block.pcm,size_t(std::min<uint64_t>(576*d.info.channels,d.samples-decoded)));
        if(stopping(job.cancel))break;
        if(!n) {
            if(d.last_error||input.error) {decodeError="音乐读取或解码失败";break;}
            queue->eof.store(true,std::memory_order_release);continue;
        }
        decoded+=n;block.samples=n;block.end=decoded;
        queue->written.store(w+1,std::memory_order_release);
        // The producer is bounded by the ring. Let idle/system work run even
        // while initially decoding ahead; never wait in the LVGL thread.
        vTaskDelay(1);
    }
    queue->finish();
    if(decodeError)fail(job,decodeError);
    if(queue->error)fail(job,queue->error);
    if(!job.cancel.load()&&!sys::Power::sleepPending()&&stopping(job.cancel))fail(job,"SD卡已断开或弹出");
}
void playerWorker(void *arg) {
    {
        auto holder=static_cast<std::shared_ptr<Player>*>(arg);auto job=*holder;delete holder;
        try {playFile(*job);}catch(const std::bad_alloc&){fail(*job,"播放内存不足");}
        job->done.store(true,std::memory_order_release);
    }
    active=false;vTaskDeleteWithCaps(nullptr);
}
}
std::shared_ptr<Listing> scan() {
    if(sys::Power::sleepPending()||scanning.exchange(true))return {};
    try {
        auto job=std::make_shared<Listing>();job->access=sd::requestAccess(true);
        auto arg=new std::shared_ptr<Listing>(job);
        if(xTaskCreateWithCaps(scanWorker,"mp3_scan",6144,arg,1,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return job;
        delete arg;
    }catch(const std::bad_alloc&){}
    scanning=false;return {};
}
std::shared_ptr<Player> start(const std::string &path) {
    if(!trackPath(path)||sys::Power::sleepPending()||!recorder::idle()||!chip::idle()||!game_audio::idle()||!fc::idle()||active.exchange(true))return {};
    try {
        auto job=std::make_shared<Player>();job->path=path;job->access=sd::requestAccess();
        auto arg=new std::shared_ptr<Player>(job);
        if(xTaskCreatePinnedToCoreWithCaps(playerWorker,"mp3_decode",40960,arg,5,nullptr,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return job;
        delete arg;
    }catch(const std::bad_alloc&){}
    active=false;return {};
}
bool idle(){return !active.load();}
bool storageIdle(){return !scanning.load();}
}
