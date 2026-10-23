#include "mp3_player.hpp"
#include "chiptune_player.hpp"
#include "photo_loader.hpp"
#include "gif_player.hpp"
#include "file_manager.hpp"
#include "knowledge_store.hpp"
#include "pattern_store.hpp"
#include "garden_service.hpp"
#include "storage_browser.hpp"
#include "sd_media.hpp"
#include "charset_conv.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "esp_flash.h"
#include "esp_image_format.h"
#include "esp_ota_ops.h"
#include "esp_vfs_fat.h"
#include "bsp/esp-bsp.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <new>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
namespace core {
namespace {
std::atomic<unsigned> active{0};
constexpr size_t kMaxEntries=256, kMaxTextBytes=64*1024;
std::mutex progressMutex,progressIoMutex;
std::vector<std::shared_ptr<StorageJob>> progressQueue;
std::shared_ptr<StorageJob> progressCurrent;
bool progressRunning=false;

std::string progressPath(const std::string &path) {
    const auto slash=path.find_last_of('/');
    const auto name=path.substr(slash+1);
    char suffix[48];
    snprintf(suffix,sizeof(suffix),"/.yumo-read-%016llx.bin",
             static_cast<unsigned long long>(reading::fingerprint(name.data(),name.size())));
    return path.substr(0,slash)+suffix;
}
void readRecords(FILE *file,reading::Record (&records)[2]) {
    for(unsigned slot=0;slot<2;++slot) {
        auto &record=records[slot];
        if(fseek(file,slot*reading::RecordStride,SEEK_SET)!=0 ||
           fread(record.data(),1,record.size(),file)!=record.size()) record={};
    }
}
reading::Mark readProgress(const std::string &path,uint64_t fingerprint,uint64_t cardGeneration) {
    {
        std::lock_guard<std::mutex> lock(progressMutex);
        // A rapid close/reopen sees the most recent queued position immediately.
        for(auto it=progressQueue.rbegin();it!=progressQueue.rend();++it)
            if((*it)->path==path && (*it)->cardGeneration==cardGeneration) return (*it)->progress;
        if(progressCurrent && progressCurrent->path==path && progressCurrent->cardGeneration==cardGeneration) return progressCurrent->progress;
    }
    std::lock_guard<std::mutex> lock(progressIoMutex);
    FILE *file=fopen(progressPath(path).c_str(),"rb");
    if(!file) return {fingerprint,1,0};
    reading::Record records[2]{}; readRecords(file,records); fclose(file);
    const int index=reading::latest(records[0],records[1]);
    return index<0?reading::Mark{fingerprint,1,0}:reading::decode(records[index]);
}
void writeProgress(StorageJob &job) {
    std::lock_guard<std::mutex> lock(progressIoMutex);
    const auto path=progressPath(job.path);
    FILE *file=fopen(path.c_str(),"r+b");
    if(!file && errno==ENOENT) {
        file=fopen(path.c_str(),"w+b");
        if(file) {
            // Allocate both sectors once; subsequent turns do not extend the file.
            const std::array<uint8_t,reading::RecordStride> empty{};
            if(fwrite(empty.data(),1,empty.size(),file)!=empty.size() ||
               fwrite(empty.data(),1,empty.size(),file)!=empty.size() || fflush(file)!=0) {
                job.error=EIO;fclose(file);return;
            }
        }
    }
    if(!file) { job.error=errno;return; }
    reading::Record records[2]{}; readRecords(file,records);
    if(ferror(file)) {job.error=EIO;fclose(file);return;}
    const int current=reading::latest(records[0],records[1]);
    const uint32_t sequence=current<0?1:static_cast<uint32_t>(reading::get(records[current],4,4))+1;
    const auto next=reading::encode(job.progress,sequence);
    clearerr(file);
    // Do not overwrite the last valid record's logical sector.
    if(fseek(file,current==0?reading::RecordStride:0,SEEK_SET)!=0 ||
       fwrite(next.data(),1,next.size(),file)!=next.size() || fflush(file)!=0 || fsync(fileno(file))!=0)
        job.error=EIO;
    if(fclose(file)!=0) job.error=EIO;
}
void progressWorker(void *) {
    {
        sys::Power::WorkGuard storageWork;
        while(true) {
            std::shared_ptr<StorageJob> job;
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                if(progressQueue.empty()) {
                    progressRunning=false;
                    std::vector<std::shared_ptr<StorageJob>>().swap(progressQueue);
                    break;
                }
                job=progressQueue.front(); progressQueue.erase(progressQueue.begin());
                progressCurrent=job;
            }
            try {
                sd::Session card(job->sdAccess,true,true);
                if(!card) job->error=card.error();
                else if(!job->cardGeneration || job->cardGeneration!=card.generation()) job->error=ENODEV;
                else { writeProgress(*job); card.reportError(job->error); }
            }
            catch(const std::bad_alloc &) {job->error=ENOMEM;}
            job->done.store(true,std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                progressCurrent.reset();
            }
        }
    }
    --active;
    vTaskDeleteWithCaps(nullptr);
}
bool accepted(const char *name,bool photos) {
    const char *ext=strrchr(name,'.'); if(!ext) return false;
    if(photos) return !strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg") || !strcasecmp(ext,".png") || !strcasecmp(ext,".gif");
    return !strcasecmp(ext,".txt") || !strcasecmp(ext,".md") ||
        !strcasecmp(ext,".log") || !strcasecmp(ext,".csv");
}
void inspectFlash(StorageUsage &usage) {
    uint32_t flash=0;
    if(esp_flash_get_size(nullptr,&flash)==ESP_OK) usage.flash=flash;
    const auto app=esp_ota_get_running_partition();
    if(app) {
        usage.firmware=app->size;
        esp_image_metadata_t image{};
        const esp_partition_pos_t pos{app->address,app->size};
        // Metadata only: no full image hashing, allocation or persistent cache.
        if(esp_image_get_metadata(&pos,&image)==ESP_OK && image.image_len && image.image_len<=app->size) {
            usage.image=image.image_len;usage.imageValid=true;
        }
    }
    const auto assets=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"assets");
    const auto vault=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"vault");
    if(assets) usage.assets=assets->size;
    if(vault) usage.vault=vault->size;
}
void execute(StorageJob &job) {
    if(job.usageOnly) {
        // The worker holds an SD Session: USB/eject cannot unmount this VFS.
        const auto card=bsp_sdcard_get_handle();
        if(!card) {job.error=ENODEV;return;}
        auto &usage=job.usage;
        usage.sdPhysical=uint64_t(card->csd.capacity)*card->csd.sector_size;
        if(esp_vfs_fat_info(sd::Root,&usage.sdTotal,&usage.sdFree)!=ESP_OK) {job.error=EIO;return;}
        usage.sdValid=usage.sdTotal>0 && usage.sdFree<=usage.sdTotal && usage.sdTotal<=usage.sdPhysical;
        if(!usage.sdValid) job.error=EIO;
        return;
    }
    if(job.path!="/sdcard" && job.path.rfind("/sdcard/",0)!=0) { job.error=EINVAL; return; }
    if(job.path.find("/../")!=std::string::npos || job.path.compare(job.path.size()-3,3,"/..")==0 || job.path.find('\0')!=std::string::npos) { job.error=EINVAL; return; }
    if(job.removeRecording) {
        if(!sd::child(job.path,sd::Recordings,".wav")) {job.error=EINVAL;return;}
        struct stat st{};
        if(stat(job.path.c_str(),&st)!=0) {job.error=errno;return;}
        if(!S_ISREG(st.st_mode)) {job.error=EINVAL;return;}
        if(!job.deleteIdentity.valid || st.st_size<0 ||
           job.deleteIdentity.size!=uint64_t(st.st_size) ||
           job.deleteIdentity.modified!=int64_t(st.st_mtime)) {job.error=ESTALE;return;}
        if(job.cancelled.load() || sys::Power::sleepPending()) {job.error=ECANCELED;return;}
        if(std::remove(job.path.c_str())!=0) job.error=errno;
        return;
    }
    if(job.text) {
        std::unique_ptr<FILE,decltype(&fclose)> owner(fopen(job.path.c_str(),"rb"),&fclose);
        FILE *file=owner.get();
        if(!file) { job.error=errno; return; }
        std::string bytes;
        size_t count=0;
        while(count<kMaxTextBytes && !job.cancelled.load()) {
            const auto chunk=std::min<size_t>(2048,kMaxTextBytes-count);
            bytes.resize(count+chunk);
            const auto read=fread(bytes.data()+count,1,chunk,file);
            count+=read;bytes.resize(count);
            if(ferror(file)) { job.error=EIO;break; }
            if(read<chunk) break;
        }
        if(!job.cancelled.load() && !job.error)
            job.truncated=count==kMaxTextBytes && fgetc(file)!=EOF;
        if(ferror(file)) job.error=EIO;
        owner.reset();
        if(!job.cancelled.load() && !job.error) {
            job.content=decodeText(bytes,job.truncated);
            const auto hash=reading::fingerprint(job.content.data(),job.content.size());
            job.progress=readProgress(job.path,hash,job.cardGeneration);
            if(job.progress.fingerprint!=hash) job.progress={hash,1,0};
        }
        return;
    }
    if(job.media && !sd::ensure(job.path.c_str())) { job.error=ENODEV;return; }
    std::unique_ptr<DIR,decltype(&closedir)> owner(opendir(job.path.c_str()),&closedir);
    DIR *dir=owner.get();
    if(!dir) { job.error=errno; return; }
    unsigned examined=0;
    while(!job.cancelled.load()) {
        errno=0; auto entry=readdir(dir);
        if(!entry) { if(errno) job.error=errno; break; }
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        if(++examined>4096 || job.entries.size()>=kMaxEntries) { job.truncated=true; break; }
        if(examined%8==0) vTaskDelay(pdMS_TO_TICKS(1));
        std::string path=job.path+"/"+entry->d_name;
        struct stat st{};
        if(stat(path.c_str(),&st)!=0) {
            if(errno==EIO || errno==ENODEV) {job.error=errno;break;}
            continue; // DT_UNKNOWN is common on VFS; skip vanished entries.
        }
        const bool directory=S_ISDIR(st.st_mode);
        if(job.media && directory) continue;
        if(!directory && (!S_ISREG(st.st_mode) || !(job.media ? sd::child(path,job.path.c_str(),job.media==2?".nes":".wav") : accepted(entry->d_name,job.photos)))) continue;
        auto name=ensureUtf8(entry->d_name);
        for(auto &c:name) if(static_cast<unsigned char>(c)<32) c=' ';
        job.entries.push_back({std::move(path),std::move(name),directory,
                               {st.st_size<0?0:uint64_t(st.st_size),int64_t(st.st_mtime),!directory&&st.st_size>=0}});
    }
    owner.reset();
    if(job.error) { job.entries.clear();return; }
    if(job.cancelled.load()) return;
    std::sort(job.entries.begin(),job.entries.end(),[](const FileEntry &a,const FileEntry &b) {
        if(a.directory!=b.directory) return a.directory>b.directory;
        return strcasecmp(a.name.c_str(),b.name.c_str())<0;
    });
}
void worker(void *arg) {
    {
        sys::Power::WorkGuard storageWork; // destructor runs before vTaskDeleteWithCaps
        auto holder=static_cast<std::shared_ptr<StorageJob> *>(arg);
        auto job=*holder; delete holder;
        try {
        if(!job->cancelled.load()) {
            if(job->usageOnly) inspectFlash(job->usage);
            if(!job->cancelled.load()) {
                sd::Session card(job->sdAccess);
                if(!card) job->error=card.error();
                else if(job->removeRecording && (!job->cardGeneration || job->cardGeneration!=card.generation()))
                    job->error=ENODEV; // A stale row must never delete a file on newly mounted media.
                else if(job->removeRecording && !card.claimMutation())
                    job->error=EBUSY; // No other SD session can change/open the target between stat and remove.
                else {
                    job->cardGeneration=card.generation();
                    if(!job->cancelled.load()) execute(*job);
                    card.reportError(job->error);
                }
            }
        }
    } catch(const std::bad_alloc &) {job->error=ENOMEM;}
        job->done.store(true,std::memory_order_release);
    } // destroy shared result before deleting the task; no leaked C++ locals
    --active;
    vTaskDeleteWithCaps(nullptr);
}
std::shared_ptr<StorageJob> start(const std::string &path,bool photos,bool text,unsigned media=0,bool removeRecording=false,uint64_t cardGeneration=0,FileIdentity identity={},bool usageOnly=false) {
    try {
    auto job=std::make_shared<StorageJob>(); job->path=path; job->photos=photos; job->text=text; job->media=media; job->removeRecording=removeRecording;
    job->cardGeneration=cardGeneration;
    job->deleteIdentity=identity; // Publish the complete request before the worker can run.
    job->usageOnly=usageOnly;
    job->sdAccess=sd::requestAccess(!removeRecording && !usageOnly);
    if(active.fetch_add(1)>=2) { --active; job->error=EBUSY; job->done.store(true); return job; }
    auto holder=new(std::nothrow) std::shared_ptr<StorageJob>(job);
    if(!holder || xTaskCreateWithCaps(worker,"sd_browser",6144,holder,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        delete holder; --active; job->error=ENOMEM; job->done.store(true);
    }
    return job;
    } catch(const std::bad_alloc &) { return {}; }
}
}
std::shared_ptr<StorageJob> listDirectory(const std::string &path,bool photos) { return start(path,photos,false); }
std::shared_ptr<StorageJob> readText(const std::string &path) { return start(path,false,true); }
std::shared_ptr<StorageJob> inspectStorage() { return start(sd::Root,false,false,0,false,0,{},true); }
std::shared_ptr<StorageJob> saveTextProgress(const std::string &path,reading::Mark mark,uint64_t cardGeneration) {
    std::shared_ptr<StorageJob> job;
    try {
        job=std::make_shared<StorageJob>(); job->path=path;job->progress=mark;job->cardGeneration=cardGeneration;
        job->sdAccess=sd::requestAccess();
        if(path.rfind("/sdcard/",0)!=0 || path.find("/../")!=std::string::npos ||
           path.find('\0')!=std::string::npos || path.back()=='/' || !mark.page) {
            job->error=EINVAL;job->done.store(true);return job;
        }
        std::lock_guard<std::mutex> lock(progressMutex);
        for(auto &pending:progressQueue) {
            if(pending->path!=path) continue;
            pending->cancelled.store(true);pending->done.store(true,std::memory_order_release);
            pending=job;return job;
        }
        if(progressQueue.size()>=8) {job->error=EBUSY;job->done.store(true);return job;}
        progressQueue.push_back(job);
        if(!progressRunning) {
            progressRunning=true;++active;
            if(xTaskCreateWithCaps(progressWorker,"text_progress",6144,nullptr,2,nullptr,
                                   MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
                --active;progressRunning=false;
                std::vector<std::shared_ptr<StorageJob>>().swap(progressQueue);
                job->error=ENOMEM;job->done.store(true,std::memory_order_release);
            }
        }
    } catch(const std::bad_alloc &) {
        if(job) {job->error=ENOMEM;job->done.store(true,std::memory_order_release);}
    }
    return job;
}
std::shared_ptr<StorageJob> listMedia(bool games) { return start(games?sd::Games:sd::Recordings,false,false,games?2:1); }
std::shared_ptr<StorageJob> deleteRecording(const std::string &path,uint64_t cardGeneration,FileIdentity identity) {
    if(sys::Power::sleepPending()) return {};
    try { return start(path,false,false,0,true,cardGeneration,identity); }
    catch(const std::bad_alloc &) {return {};}
}
std::string parentDirectory(const std::string &path) {
    if(path=="/sdcard") return path;
    const auto slash=path.find_last_of('/');
    return slash<7?"/sdcard":path.substr(0,slash);
}
bool storageIdle() { return active.load()==0 && chip::storageIdle() && mp3::storageIdle() && photo::idle() && gif::idle() && files::idle() && knowledge::idle() && patterns::idle() && garden::idle(); }
const char *storageError(int error) {
    switch(error) {
    case ENOENT: case ENODEV: case ENOTDIR: return "SD卡或目录不可用，检查后刷新";
    case ENOMEM: return "内存不足，请退出后重试";
    case EBUSY: return "存储任务忙，请稍后刷新";
    default: return "SD 卡读取失败，请检查存储卡后刷新";
    }
}
} // namespace core
