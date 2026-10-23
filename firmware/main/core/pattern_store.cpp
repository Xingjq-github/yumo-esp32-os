#include "pattern_store.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <mutex>
#include <new>
#include <sys/stat.h>
#include <unistd.h>
namespace core::patterns {
namespace {
std::mutex gate;
std::atomic<bool> running{false};
std::shared_ptr<Job> queuedRead,queuedSave;
struct File {
    FILE *p=nullptr;explicit File(const char *name,const char *mode):p(fopen(name,mode)){}
    ~File(){if(p)fclose(p);}bool close(){if(!p)return true;auto f=p;p=nullptr;return fclose(f)==0;}
};
bool fetch(FILE *f,size_t offset,void *data,size_t bytes){return fseek(f,long(offset),SEEK_SET)==0&&fread(data,1,bytes,f)==bytes;}
bool records(FILE *f,Record &a,Record &b){
    if(fseek(f,0,SEEK_SET)!=0)return false;
    if(fread(a.data(),1,a.size(),f)!=a.size()){a={};if(ferror(f))return false;}
    clearerr(f); // A short/torn slot is recoverable; an I/O error is not.
    if(fseek(f,512,SEEK_SET)!=0)return false;
    if(fread(b.data(),1,b.size(),f)!=b.size()){b={};if(ferror(f))return false;}
    return true;
}
void loadProgress(Job &j){
    File file(ProgressPath,"rb");if(!file.p){if(errno!=ENOENT)j.error=errno;return;}
    Record a{},b{};if(!records(file.p,a,b)){j.error=EIO;return;}
    const int slot=latest(a,b);if(slot<0)j.damaged=true;else j.progress=decode(slot==0?a:b);
}
void writeProgress(Job &j){
    File f(ProgressPath,"r+b");
    if(!f.p&&errno==ENOENT){f.p=fopen(ProgressPath,"w+b");if(f.p){std::array<uint8_t,512> zeros{};for(int i=0;i<2;++i)if(fwrite(zeros.data(),1,zeros.size(),f.p)!=zeros.size()){j.error=EIO;return;}}}
    if(!f.p){j.error=errno;return;}
    Record a{},b{};if(!records(f.p,a,b)){j.error=EIO;return;}const int slot=latest(a,b);
    const uint32_t sequence=slot<0?1:get32((slot==0?a:b).data()+8)+1;auto data=encode(j.progress,sequence);clearerr(f.p);
    if(fseek(f.p,slot==0?512:0,SEEK_SET)!=0||fwrite(data.data(),1,data.size(),f.p)!=data.size()||fflush(f.p)!=0||fsync(fileno(f.p))!=0)j.error=EIO;
    if(!f.close())j.error=EIO;
}
// Provision only a missing package. Existing user files are never replaced.
// A stale/incompatible package uses the built-in course without blocking play.
void provision(Job &j){
    struct stat info{};if(stat(LessonPath,&info)==0)return;if(errno!=ENOENT){j.fallback=true;return;}
    constexpr const char *temp="/sdcard/patterns/.lessons-install.tmp";
    File f(temp,"wb");if(!f.p){j.fallback=true;return;}bool ok=true;
    for(size_t offset=0;offset<PackageBytes;){
        if(j.cancelled.load()||sys::Power::sleepPending()){ok=false;break;}
        auto count=std::min<size_t>(2048,PackageBytes-offset);
        if(fwrite(Package+offset,1,count,f.p)!=count){ok=false;break;}
        offset+=count;vTaskDelay(1);
    }
    if(ok&&(fflush(f.p)!=0||fsync(fileno(f.p))!=0))ok=false;
    if(!f.close())ok=false;
    if(ok&&stat(LessonPath,&info)!=0&&errno==ENOENT)ok=rename(temp,LessonPath)==0;
    else ok=false;
    if(!ok){unlink(temp);j.fallback=true;}
}
void readLesson(Job &j,sd::Session *card){
    const auto row=Package+16+j.index*16;const size_t offset=get32(row),bytes=get32(row+4);
    if(bytes>MaxLessonBytes||offset>PackageBytes||bytes>PackageBytes-offset){j.error=EBADMSG;return;}
    std::unique_ptr<char[]> data(new char[bytes+1]);bool fromCard=false;
    if(card){File f(LessonPath,"rb");if(f.p){std::array<uint8_t,IndexBytes> header{};struct stat info{};
        fromCard=fstat(fileno(f.p),&info)==0&&uint64_t(info.st_size)==PackageBytes&&fetch(f.p,0,header.data(),header.size())&&
            !memcmp(header.data(),Package,header.size())&&fetch(f.p,offset,data.get(),bytes)&&crc32(reinterpret_cast<uint8_t *>(data.get()),bytes)==get32(row+8);
        if(ferror(f.p))card->reportError(EIO);
    }}
    if(!fromCard){std::memcpy(data.get(),Package+offset,bytes);j.fallback=true;}data[bytes]=0;
    j.lesson=std::make_unique<Lesson>();if(!parseLesson(data.get(),bytes,j.index,*j.lesson)){j.lesson.reset();j.error=EBADMSG;}
}
void execute(Job &j){
    if(j.operation!=Operation::Save&&(j.cancelled.load()||sys::Power::sleepPending())){j.error=ECANCELED;return;}
    sys::Power::WorkGuard work;
    sd::Session card(j.access,true,j.operation==Operation::Save);
    if(j.operation==Operation::Read){
        if(j.cancelled.load()){j.error=ECANCELED;return;}
        readLesson(j,card&&(!j.generation||card.generation()==j.generation)?&card:nullptr);return;
    }
    if(!card){j.error=card.error();j.fallback=true;return;}
    if(j.operation==Operation::Save){
        if(card.generation()!=j.generation){j.error=ESTALE;return;}writeProgress(j);card.reportError(j.error);return;
    }
    j.generation=card.generation();
    if(!sd::ensure(Directory)){j.error=errno?errno:EIO;j.generation=0;card.reportError(j.error);return;}
    loadProgress(j);if(!j.error)provision(j);card.reportError(j.error);
}
void worker(void *){
    while(true){std::shared_ptr<Job> job;
        {std::lock_guard<std::mutex> lock(gate);if(queuedSave)job=std::move(queuedSave);else if(queuedRead)job=std::move(queuedRead);else {running.store(false,std::memory_order_release);break;}}
        try{execute(*job);}catch(const std::bad_alloc &){job->error=ENOMEM;}
        if(job->cancelled.load()&&job->operation!=Operation::Save){job->lesson.reset();job->error=ECANCELED;}
        job->done.store(true,std::memory_order_release);
    }
    vTaskDeleteWithCaps(nullptr);
}
std::shared_ptr<Job> enqueue(std::shared_ptr<Job> job){
    std::lock_guard<std::mutex> lock(gate);auto &queue=job->operation==Operation::Save?queuedSave:queuedRead;
    if(queue){queue->error=ECANCELED;queue->cancelled.store(true);queue->done.store(true,std::memory_order_release);}queue=job;
    if(running.load())return job;
    running.store(true);
    if(xTaskCreateWithCaps(worker,"pattern_store",12288,nullptr,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return job;
    running.store(false);queue.reset();return {};
}
}
bool idle(){return !running.load(std::memory_order_acquire);}
std::shared_ptr<Job> open(){try{auto j=std::make_shared<Job>();j->access=sd::requestAccess(true);return enqueue(j);}catch(const std::bad_alloc &){return {};}}
std::shared_ptr<Job> read(unsigned index,uint64_t generation){if(index>=LessonCount)return {};try{auto j=std::make_shared<Job>();j->operation=Operation::Read;j->index=index;j->generation=generation;j->access=sd::requestAccess();return enqueue(j);}catch(const std::bad_alloc &){return {};}}
std::shared_ptr<Job> save(const Progress &p,uint64_t generation,sd::Access access){if(!generation||!validProgress(p))return {};try{auto j=std::make_shared<Job>();j->operation=Operation::Save;j->progress=p;j->generation=generation;j->access=access;return enqueue(j);}catch(const std::bad_alloc &){return {};}}
}
