#include "mp3_player.hpp"
#include "xiaozhi_service.hpp"
#include "chiptune_player.hpp"
#include "audio_mgr.hpp"
#include "audio_recorder.hpp"
#include "game_audio.hpp"
#include "fc_emulator.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
namespace core::chip {
namespace {
std::atomic<bool> active{false},audible{false},scanning{false};
std::atomic<unsigned> readers{0};
struct Reading {Reading(){readers.fetch_add(1);}~Reading(){readers.fetch_sub(1);}};
bool suffix(const char *name){auto n=std::strlen(name);return n>10&&!strcasecmp(name+n-10,".chip.json");}
bool load(const std::string &path,sd::Access access,std::atomic<bool> &cancel,Score &score,std::string &error){
    Reading reading;sd::Session session(access);
    if(!session){error=sd::usbExclusive()?"SD卡正在由电脑使用":"SD卡不可读，请插卡后刷新";return false;}
    struct stat info{};
    if(stat(path.c_str(),&info)||!S_ISREG(info.st_mode)){error="找不到乐谱文件";return false;}
    if(info.st_size<=0||info.st_size>MaxFile){error="乐谱为空或超过64KiB";return false;}
    std::unique_ptr<FILE,decltype(&std::fclose)> file(std::fopen(path.c_str(),"rb"),std::fclose);
    if(!file){session.reportError(errno);error="无法打开乐谱";return false;}
    std::string data(size_t(info.st_size),'\0');size_t offset=0;
    while(offset<data.size()&&!cancel.load()&&!sys::Power::sleepPending()){
        const size_t want=std::min(size_t(4096),data.size()-offset);
        if(std::fread(&data[offset],1,want,file.get())!=want){session.reportError(errno?errno:EIO);error="SD卡读取失败";return false;}
        offset+=want;vTaskDelay(1);
    }
    file.reset();session.release();
    if(cancel.load()||sys::Power::sleepPending())return false;
    return parse(data.data(),data.size(),score,error);
}
void scanWorker(void *arg){
    {
        auto holder=static_cast<std::shared_ptr<Listing> *>(arg);auto job=*holder;delete holder;
        try {
            if(!job->metadataOnly) {
                job->entries.reserve(201);
                sd::Session session(job->access);
                if(!session)job->error=sd::usbExclusive()?"SD卡正在由电脑使用":"SD卡不可读，请插卡后刷新";
                else {
                    std::unique_ptr<DIR,decltype(&closedir)> dir(opendir(Directory),closedir);
                    if(!dir)job->error="请在SD卡根目录放入chiptune文件夹";
                    else {
                        unsigned examined=0;
                        while(!job->cancel.load()&&!sys::Power::sleepPending()){
                            errno=0;auto item=readdir(dir.get());
                            if(!item){if(errno){session.reportError(errno);job->error="读取目录失败";}break;}
                            if(++examined>4096){job->truncated=true;break;}
                            if(examined%8==0)vTaskDelay(1);
                            if(!suffix(item->d_name))continue;
                            const std::string path=std::string(Directory)+"/"+item->d_name;struct stat st{};
                            if(stat(path.c_str(),&st)||!S_ISREG(st.st_mode))continue;
                            Entry entry{path,item->d_name,item->d_name,{}};
                            // Keep the first 200 in lexical order, independent of FAT directory order.
                            auto at=std::lower_bound(job->entries.begin(),job->entries.end(),entry,[](const Entry&a,const Entry&b){return a.name<b.name;});
                            if(job->entries.size()<200)job->entries.insert(at,std::move(entry));
                            else {job->truncated=true;if(at!=job->entries.end()){job->entries.insert(at,std::move(entry));job->entries.pop_back();}}
                            vTaskDelay(1);
                        }
                    }
                }
            }
            for(size_t i=0;i<std::min(size_t(4),job->entries.size());++i){
                if(job->cancel.load()||sys::Power::sleepPending())break;
                auto &entry=job->entries[i];
                if(entry.inspected)continue;
                Score score;if(load(entry.path,job->access,job->cancel,score,entry.error)&&!score.title.empty())entry.title=std::move(score.title);
                entry.inspected=true;
                vTaskDelay(1);
            }
        }catch(const std::bad_alloc &){job->error="内存不足，请退出后重试";job->entries.clear();}
        job->done.store(true,std::memory_order_release);
    }
    readers.fetch_sub(1);scanning.store(false);vTaskDeleteWithCaps(nullptr);
}
void playerWorker(void *arg){
    {
        auto holder=static_cast<std::shared_ptr<Player> *>(arg);auto job=*holder;delete holder;
        {std::lock_guard<std::mutex> lock(job->wakeMutex);job->worker=xTaskGetCurrentTaskHandle();}
        bool opened=false;
        try {
            Score score;
            if(load(job->path,job->access,job->cancel,score,job->error)&&!job->cancel.load()){
                job->title=score.title;job->tempo.store(score.tempo);job->loop.store(score.loop);
                Synth synth(score);job->duration.store(synth.durationMs());
                int16_t pcm[256];float gain=0;bool rewinding=false;
                job->state.store(State::Playing);
                while(true){
                    const bool quit=job->cancel.load()||sys::Power::sleepPending();
                    if(job->rewind.exchange(false)){rewinding=true;job->play.store(false);}
                    const bool play=job->play.load()&&!rewinding&&!quit;
                    if(!play&&gain<=0){
                        if(opened){sys::Audio::closeStream();opened=false;}
                        audible.store(false);
                        if(quit)break;
                        if(rewinding){synth.reset();job->position.store(0);rewinding=false;job->state.store(State::Stopped);}
                        else if(job->state.load()!=State::Stopped)job->state.store(State::Paused);
                        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(250));continue;
                    }
                    sys::Power::WorkGuard work;audible.store(true);
                    if(!opened){opened=sys::Audio::openStream(false,Rate);if(!opened){job->error="扬声器忙，请稍后重试";job->state.store(State::Failed);break;}}
                    if(play)job->state.store(State::Playing);
                    unsigned count=0;
                    while(count<256){
                        const auto sample=synth.next(job->loop.load());
                        if(synth.ended())break;
                        gain=play?std::min(1.f,gain+1.f/110):std::max(0.f,gain-1.f/110);
                        pcm[count++]=int16_t(sample*gain);
                        if(!play&&gain<=0)break;
                    }
                    if(count&&!sys::Audio::writeStream(pcm,count*sizeof(int16_t))){job->error="音频输出失败";job->state.store(State::Failed);break;}
                    job->position.store(synth.milliseconds());
                    if(synth.ended()){job->play.store(false);rewinding=true;gain=0;}
                }
            }else if(!job->cancel.load())job->state.store(State::Failed);
        }catch(const std::bad_alloc &){job->error="播放内存不足";job->state.store(State::Failed);}
        if(opened)sys::Audio::closeStream();
        {std::lock_guard<std::mutex> lock(job->wakeMutex);job->worker=nullptr;}
        audible.store(false);job->done.store(true,std::memory_order_release);
    }
    active.store(false);vTaskDeleteWithCaps(nullptr);
}
}
void Player::wake(){
    std::lock_guard<std::mutex> lock(wakeMutex);
    if(worker)xTaskNotifyGive(static_cast<TaskHandle_t>(worker));
}
std::shared_ptr<Listing> titles(const std::vector<Entry> &entries,size_t offset){
    if(offset>=entries.size()||sys::Power::sleepPending()||scanning.exchange(true))return {};
    std::shared_ptr<Listing> job;std::shared_ptr<Listing> *arg=nullptr;
    try {
        job=std::make_shared<Listing>();job->access=sd::requestAccess();job->metadataOnly=true;job->offset=offset;
        const auto end=std::min(entries.size(),offset+4);
        job->entries.assign(entries.begin()+offset,entries.begin()+end);
        arg=new std::shared_ptr<Listing>(job);
    }catch(const std::bad_alloc &){scanning.store(false);return {};}
    readers.fetch_add(1);
    if(xTaskCreateWithCaps(scanWorker,"chip_titles",6144,arg,1,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){delete arg;readers.fetch_sub(1);scanning.store(false);return {};}
    return job;
}
std::shared_ptr<Listing> scan(){
    if(sys::Power::sleepPending()||scanning.exchange(true))return {};
    std::shared_ptr<Listing> job;std::shared_ptr<Listing> *arg=nullptr;
    try{job=std::make_shared<Listing>();job->access=sd::requestAccess(true);arg=new std::shared_ptr<Listing>(job);}catch(const std::bad_alloc &){scanning.store(false);return {};}
    readers.fetch_add(1);
    if(xTaskCreateWithCaps(scanWorker,"chip_scan",6144,arg,1,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){delete arg;readers.fetch_sub(1);scanning.store(false);return {};}
    return job;
}
std::shared_ptr<Player> start(const std::string &path){
    if(sys::Power::sleepPending()||!mp3::idle()||!game_audio::idle()||!recorder::idle()||!fc::idle()||!core::xiaozhi::idle() || active.exchange(true))return {};
    std::shared_ptr<Player> job;std::shared_ptr<Player> *arg=nullptr;
    try{job=std::make_shared<Player>();job->path=path;job->access=sd::requestAccess();arg=new std::shared_ptr<Player>(job);}catch(const std::bad_alloc &){active.store(false);return {};}
    if(xTaskCreateWithCaps(playerWorker,"chip_audio",6144,arg,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS){delete arg;active.store(false);return {};}
    return job;
}
bool idle(){return !active.load();}
bool busy(){return audible.load()||readers.load()!=0;}
bool storageIdle(){return readers.load()==0;}
}
