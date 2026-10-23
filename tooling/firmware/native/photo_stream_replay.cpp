// Production loader + real PNG/JPEG codecs; only RTOS, SD admission and the
// architecture-specific small-JPEG fast path are replaced on the host.
#include "core/photo_loader.hpp"
#include "core/photo_frame_cache.hpp"
#include "apps/photo_pixels.hpp"
#include "photo_png.h"
#include "photo_jpeg.h"
#include "esp_jpeg_dec.h"
#include "esp_heap_caps.h"
#include "probe.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sys/stat.h>
#include <string>
#include <memory>
#include <limits>
#include <filesystem>
#include <set>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <io.h>
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
namespace fake {
std::string path,mode,cache,tileCache;
std::set<FILE *> tileFiles;
std::set<int> tileFds;
bool tiles=false,mutate=false;
uint64_t tileReadBytes=0,tileWriteBytes=0;
std::set<FILE *> tempFiles;
unsigned rawFiles=0,swapOpens=0,swapReads=0,swapWrites=0;
uint64_t swapBytes=0;
std::shared_ptr<core::photo::Job> job;
void (*task)(void *)=nullptr;void *argument=nullptr;
uint64_t now=0,cardGeneration=1;
size_t reads=0,total=0,maxRead=0;
unsigned files=0,sessions=0,guards=0,opens=0,fastAttempts=0;
bool sleep=false,busy=false,zoom=false;
void hook() {
    if(reads>=3) {
        if(mode=="cancel" && job)job->cancelled=true;
        if(mode=="sleep")sleep=true;
        if(mode=="eject")busy=true;
        if(mode=="timeout")now=91000000;
    }
}
}
static int64_t esp_timer_get_time(){fake::now+=100;return fake::now;}
static void vTaskDelay(unsigned){fake::now+=1000;fake::hook();}
static void vTaskDeleteWithCaps(void *){}
#define pdPASS 1
static int xTaskCreateWithCaps(void (*fn)(void *),const char *,unsigned,void *arg,unsigned,void *,unsigned) {
    if(fake::mode=="task-fail")return 0;
    fake::task=fn;fake::argument=arg;return pdPASS;
}
namespace sys {
struct Power {
    static bool sleepPending(){return fake::sleep;}
    struct WorkGuard {WorkGuard(){++fake::guards;}~WorkGuard(){CHECK(fake::guards==1);--fake::guards;}};
};
}
namespace core::sd {
Access requestAccess(bool){return {8,false};}
bool busy(){return fake::busy;}
bool child(const std::string &p,const char *dir,const char *ext) {
    auto start=std::string(dir)+"/";if(p.rfind(start,0)!=0 || p.find('/',start.size())!=std::string::npos)return false;
    auto tail=p.substr(p.find_last_of('.'));std::transform(tail.begin(),tail.end(),tail.begin(),[](char c){return char(std::tolower((unsigned char)c));});return tail==ext;
}
Session::Session(Access,bool,bool){if(fake::mode!="session-fail"){held_=true;generation_=fake::cardGeneration;++fake::sessions;}}
Session::~Session(){release();}
void Session::release(){if(held_){CHECK(fake::files==0);CHECK(fake::sessions==1);held_=false;--fake::sessions;}}
void Session::reportError(int){}
bool Session::claimMutation(){return held_ && fake::mode!="swap-busy" && fake::mode!="tiles-busy";}
bool ensure(const char *p){
    if(std::string(p)==PhotoFrames){if(!fake::tiles || fake::mode=="tiles-directory")return false;std::filesystem::create_directories(fake::tileCache);return true;}
    CHECK(std::string(p)==PhotoCache);if(fake::mode=="swap-directory")return false;std::filesystem::create_directories(fake::cache);return true;
}
}
static bool tilePath(const char *p){return std::string(p).rfind(core::sd::PhotoFrames,0)==0;}
static std::string mapTemp(const char *path) {
    if(tilePath(path)) {
        auto name=std::filesystem::path(path).filename().string();
        CHECK(name=="building.tmp" || (name.size()==20 && name.substr(16)==".ytc"));
        return fake::tileCache+"/"+name;
    }
    CHECK(std::string(path).rfind(core::sd::PhotoCache,0)==0);
    auto name=std::filesystem::path(path).filename().string();
    CHECK(name=="coeff-0.tmp"||name=="coeff-1.tmp"||name=="coeff-2.tmp");
    return fake::cache+"/"+name;
}
static bool isTemp(FILE *f){return fake::tempFiles.count(f)!=0;}
static FILE *fakeFopen(const char *path,const char *mode) {
    CHECK(fake::sessions==1);CHECK(std::strcmp(mode,"rb")==0);
    if(tilePath(path)) {
        if(!fake::tiles){errno=ENOENT;return nullptr;}
        FILE *f=std::fopen(mapTemp(path).c_str(),mode);if(f){++fake::files;fake::tileFiles.insert(f);}return f;
    }
    const bool temp=std::string(path).rfind(core::sd::PhotoCache,0)==0;
    if(!temp && fake::mode=="open-fail"){errno=EIO;return nullptr;}
    FILE *f=std::fopen(temp?mapTemp(path).c_str():fake::path.c_str(),mode);
    if(f){++fake::files;if(temp)fake::tempFiles.insert(f);else ++fake::opens;}return f;
}
static int fakeFclose(FILE *f) {
    if(fake::tileFiles.erase(f)) {
        CHECK(fake::sessions==1&&fake::files);--fake::files;int rc=std::fclose(f);
        return fake::mode=="tiles-close"?-1:rc;
    }
    CHECK(fake::sessions==1&&fake::files>=1);bool temp=isTemp(f);--fake::files;fake::tempFiles.erase(f);
    int rc=std::fclose(f);
    if((!temp && fake::mode=="close-fail") || (temp && fake::mode=="swap-close")){errno=EIO;return -1;}return rc;
}
static int fakeFstat(int fd,struct stat *s) {
    if(fake::mode=="stat-fail"){errno=EIO;return -1;}
    int rc=fstat(fd,s);
    if(fake::zoom && fake::mode=="size-change")++s->st_size;
    if(fake::zoom && fake::mode=="time-change")s->st_mtime+=2;
    return rc;
}
static void swapHook(){
    if(fake::swapWrites>3){
        if(fake::mode=="swap-cancel"&&fake::job)fake::job->cancelled=true;
        if(fake::mode=="swap-sleep")fake::sleep=true;
        if(fake::mode=="swap-eject")fake::busy=true;
        if(fake::mode=="swap-timeout")fake::now=181000000;
    }
}
static size_t fakeFread(void *out,size_t size,size_t n,FILE *f) {
    CHECK(fake::sessions==1&&fake::files>=1);CHECK(size==1&&n<=4096);
    if(fake::tileFiles.count(f)) {
        fake::tileReadBytes+=n;
        if(fake::mode=="tiles-read"){errno=EIO;return 0;}
        return std::fread(out,size,n,f);
    }
    if(isTemp(f)) {
        ++fake::swapReads;fake::swapBytes+=n;swapHook();
        if(fake::mode=="swap-read"){errno=EIO;return 0;}
        return std::fread(out,size,n,f);
    }
    ++fake::reads;fake::total+=n;fake::maxRead=std::max(fake::maxRead,n);fake::hook();
    if(fake::mode=="read-fail"&&fake::reads==3){errno=EIO;return 0;}
    const long position=std::ftell(f);size_t got=std::fread(out,size,n,f);
    if((fake::mutate || (fake::zoom && fake::mode=="content-change")) && position<=10 && position+long(got)>10)
        static_cast<uint8_t *>(out)[10-position]^=1;
    return got;
}
static int fakeFseek(FILE *f,long offset,int where) {
    CHECK(fake::sessions==1&&fake::files>=1);
    if(fake::tileFiles.count(f) && fake::mode=="tiles-seek")return -1;
    if((isTemp(f)&&fake::mode=="swap-seek") || (!isTemp(f)&&fake::mode=="seek-fail")){errno=EIO;return -1;}
    return std::fseek(f,offset,where);
}
static int fakeOpen(const char *p,int flags,int permission) {
    CHECK(fake::sessions==1);CHECK(flags==(O_CREAT|O_EXCL|O_RDWR));
    if(tilePath(p)) {
        if(fake::mode=="tiles-create")return -1;
        int fd=::open(mapTemp(p).c_str(),flags|O_BINARY,permission);
        if(fd>=0){++fake::files;++fake::rawFiles;fake::tileFds.insert(fd);}return fd;
    }
    if(fake::mode=="swap-create"){errno=EROFS;return -1;}
    int fd=::open(mapTemp(p).c_str(),flags|O_BINARY,permission);
    if(fd>=0){++fake::files;++fake::rawFiles;++fake::swapOpens;}return fd;
}
static FILE *fakeFdopen(int fd,const char *mode) {
    if(fake::tileFds.count(fd)) {
        if(fake::mode=="tiles-fdopen")return nullptr;
        FILE *f=::fdopen(fd,mode);if(f){--fake::rawFiles;fake::tileFds.erase(fd);fake::tileFiles.insert(f);}return f;
    }
    if(fake::mode=="swap-fdopen")return nullptr;
    FILE *f=::fdopen(fd,mode);if(f){--fake::rawFiles;fake::tempFiles.insert(f);}return f;
}
static int fakeCloseFd(int fd){CHECK(fake::rawFiles);fake::tileFds.erase(fd);--fake::rawFiles;--fake::files;return ::close(fd);}
static int fakeRemove(const char *p) {
    CHECK(fake::sessions==1);
    if(tilePath(p))return fake::mode=="tiles-remove"?-1:std::remove(mapTemp(p).c_str());
    if(fake::mode=="swap-remove"){errno=EIO;return -1;}return std::remove(mapTemp(p).c_str());
}
static size_t fakeFwrite(const void *p,size_t size,size_t n,FILE *f) {
    if(fake::tileFiles.count(f)) {
        CHECK(fake::sessions==1&&size==1&&n<=4096);fake::tileWriteBytes+=n;
        if(fake::mode=="tiles-write"){errno=ENOSPC;return 0;}
        if(fake::mode=="tiles-cancel" && fake::tileWriteBytes>4096 && fake::job)fake::job->cancelled=true;
        if(fake::mode=="tiles-sleep" && fake::tileWriteBytes>4096)fake::sleep=true;
        if(fake::mode=="tiles-eject" && fake::tileWriteBytes>4096)fake::busy=true;
        return std::fwrite(p,size,n,f);
    }
    CHECK(isTemp(f)&&fake::sessions==1&&size==1&&n<=4096);
    ++fake::swapWrites;fake::swapBytes+=n;swapHook();
    if(fake::mode=="swap-write"&&fake::swapWrites>3){errno=ENOSPC;return 0;}
    if(fake::mode=="swap-header"&&fake::swapWrites==1){errno=ENOSPC;return 0;}
    return std::fwrite(p,size,n,f);
}
static int fakeFflush(FILE *f){if(fake::mode=="swap-flush" || (fake::tileFiles.count(f)&&fake::mode=="tiles-flush")){errno=EIO;return -1;}return std::fflush(f);}
static int fakeFsync(int fd){return fake::mode=="tiles-sync"?-1: ::_commit(fd);}
static int fakeRename(const char *from,const char *to){CHECK(fake::sessions==1);return fake::mode=="tiles-rename"?-1:std::rename(mapTemp(from).c_str(),mapTemp(to).c_str());}
static DIR *fakeOpendir(const char *p){CHECK(fake::sessions==1&&std::string(p)==core::sd::PhotoFrames);return ::opendir(fake::tileCache.c_str());}
static dirent *fakeReaddir(DIR *d){CHECK(fake::sessions==1);return ::readdir(d);}
static int fakeClosedir(DIR *d){CHECK(fake::sessions==1);return ::closedir(d);}
#define ESP_OK 0
static int esp_vfs_fat_info(const char *,uint64_t *total,uint64_t *free) {
    *total=4ull*1024*1024*1024;*free=(fake::mode=="swap-full"||fake::mode=="tiles-full")?4*1024*1024:*total;
    return fake::mode=="swap-info"?-1:0;
}
extern "C" {
jpeg_error_t jpeg_dec_open(jpeg_dec_config_t *,jpeg_dec_handle_t *){++fake::fastAttempts;return static_cast<jpeg_error_t>(-1);}
jpeg_error_t jpeg_dec_parse_header(jpeg_dec_handle_t,jpeg_dec_io_t *,jpeg_dec_header_info_t *){return static_cast<jpeg_error_t>(-1);}
jpeg_error_t jpeg_dec_get_outbuf_len(jpeg_dec_handle_t,int *){return static_cast<jpeg_error_t>(-1);}
jpeg_error_t jpeg_dec_process(jpeg_dec_handle_t,jpeg_dec_io_t *){return static_cast<jpeg_error_t>(-1);}
jpeg_error_t jpeg_dec_close(jpeg_dec_handle_t){return JPEG_ERR_OK;}
}
#include "photo_frame_cache.inc"
#include "photo_loader.inc"
static std::shared_ptr<core::photo::Job> run(std::shared_ptr<core::photo::Source> source={},unsigned width=0,int x=0,int y=0) {
    CHECK(core::photo::idle());
    auto suffix=fake::path.substr(fake::path.find_last_of('.'));
    fake::job=core::photo::start("/sdcard/synthetic"+suffix,source,width,x,y);
    if(fake::mode=="task-fail"){CHECK(!fake::job&&core::photo::idle());return {};}
    CHECK(fake::job&&!core::photo::idle());CHECK(!core::photo::start("/sdcard/second.png"));
    auto weak=std::weak_ptr<core::photo::Job>(fake::job);
    auto task=fake::task;auto arg=fake::argument;fake::task=nullptr;fake::argument=nullptr;task(arg);
    CHECK(core::photo::idle()&&!fake::files&&!fake::sessions&&!fake::guards);
    CHECK(fake::job->done.load(std::memory_order_acquire));
    auto job=std::move(fake::job);CHECK(!weak.expired());return job;
}
static size_t entries() {
    size_t count=0;for(auto &p:std::filesystem::directory_iterator(fake::tileCache))if(p.path().extension()==".ytc")++count;
    return count;
}
static void seedLevel(unsigned n) {
    core::photo::Source source;source.width=source.height=16;source.length=123;source.fingerprint=n+1;
    source.path="/sdcard/level-"+std::to_string(n)+".png";source.png=true;
    auto view=photo_view_make(16,16,16,0,0);
    core::sd::Session card(core::sd::requestAccess());
    core::photo::frames::Access access{card,&card,[](void *p){return static_cast<core::sd::Session *>(p)->claimMutation();},nullptr,[](void *){return 0;}};
    core::photo::frames::Writer writer(source,view,access);auto sink=writer.sink();CHECK(sink);
    sink->passes=1;std::memset(sink->row,n,16*3);
    for(unsigned y=0;y<16;++y)sink->write(sink->context,y,0);
    CHECK(writer.commit(source));
}
int main(int argc,char **argv) {
    if(argc!=5 && argc!=7)return 2;
    fake::path=argv[1];fake::mode=argv[3];unsigned width=std::strtoul(argv[4],nullptr,10);
    fake::cache=std::string(argv[2])+".cache";
    fake::tileCache=std::string(argv[2])+".tiles";fake::tiles=fake::mode.rfind("tiles-",0)==0;
    if(fake::mode=="tiles-foreign") {
        std::filesystem::create_directories(fake::tileCache);
        FILE *f=std::fopen((fake::tileCache+"/building.tmp").c_str(),"wb");CHECK(f);
        CHECK(std::fwrite("USER DATA",1,9,f)==9);CHECK(std::fclose(f)==0);
    }
    std::filesystem::create_directories(fake::cache);
    if(fake::mode=="swap-stale" || fake::mode=="swap-foreign") {
        FILE *old=std::fopen((fake::cache+"/coeff-0.tmp").c_str(),"wb");CHECK(old);
        const char magic[32]="YUMO_PHOTO_CACHE_V1\r\n";const char foreign[32]="DO NOT DELETE USER DATA";
        std::fwrite(fake::mode=="swap-stale"?magic:foreign,1,32,old);std::fclose(old);
    }
    // Leave enough for the ordinary viewport but not the optional tile band.
    size_t capacity=fake::mode=="tiles-memory"?992*1024:fake::mode=="low-memory"?800*1024:2800*1024;
    long fail=fake::mode.rfind("allocation-",0)==0?std::strtol(fake::mode.c_str()+11,nullptr,10):-1;
    probe_config(capacity,fake::mode=="fragmented"?64*1024:0,fail);
    auto first=run();auto result=first;
    if(width && first && first->pixels) {
        fake::zoom=true;
        if(fake::mode=="generation-change")++fake::cardGeneration;
        if(fake::mode=="cached-zoom")fake::mode="session-fail";
        result=run(first->source,width,argc==7?std::atoi(argv[5]):0,argc==7?std::atoi(argv[6]):0);
        CHECK(first->pixels); // Old displayed frame remains owned on all failures.
        if(fake::mode=="session-fail"&&first->source->data)CHECK(result&&result->pixels);
    }
    if(fake::mode=="tiles-roundtrip") {
        CHECK(result&&result->pixels&&!result->error);
        auto initial=result;
        auto reads=fake::reads;
        auto writes=fake::tileWriteBytes;
        auto swapWrites=fake::swapWrites;
        auto sx=result->view.scaled_width,sy=result->view.scaled_height;
        for(auto point:std::vector<std::pair<int,int>>{{0,0},{127,127},{128,128},{255,191},{int(sx),int(sy)}}) {
            auto readBytes=fake::tileReadBytes;
            result=run(initial->source,width,point.first,point.second);
            CHECK(result&&result->cacheHit&&result->pixels&&!result->error);
            CHECK(fake::tileReadBytes-readBytes<=9*32776+512);
            CHECK(fake::reads==reads&&fake::tileWriteBytes==writes&&fake::swapWrites==swapWrites);
            // Compare every tile crossing/edge against a separate uncached
            // production ROI decode, including alpha and Adam7 transforms.
            fake::tiles=false;
            auto direct=run(initial->source,width,point.first,point.second);
            fake::tiles=true;
            CHECK(direct&&direct->pixels&&!direct->error&&direct->width==result->width&&direct->height==result->height);
            for(size_t i=0;i<size_t(result->width)*result->height*2;i+=2) {
                uint16_t a=result->pixels[i]|uint16_t(result->pixels[i+1])<<8;
                uint16_t b=direct->pixels[i]|uint16_t(direct->pixels[i+1])<<8;
                const int tolerance=initial->source->png?0:1;
                CHECK(std::abs(int(a>>11)-int(b>>11))<=tolerance);
                CHECK(std::abs(int((a>>5)&63)-int((b>>5)&63))<=tolerance);
                CHECK(std::abs(int(a&31)-int(b&31))<=tolerance);
            }
            reads=fake::reads;swapWrites=fake::swapWrites;
        }
        result=run(initial->source,width,initial->view.x,initial->view.y);
        CHECK(result&&result->cacheHit&&result->pixels);
        CHECK(result->width==initial->width&&result->height==initial->height);
        CHECK(!std::memcmp(result->pixels,initial->pixels,size_t(result->width)*result->height*2));
        auto smaller=run(initial->source,std::max(1u,width*2/3));
        CHECK(smaller&&smaller->pixels&&!smaller->error);
        writes=fake::tileWriteBytes;swapWrites=fake::swapWrites;
        result=run(initial->source,width,initial->view.x,initial->view.y);
        CHECK(result&&result->cacheHit&&result->pixels&&fake::tileWriteBytes==writes&&fake::swapWrites==swapWrites);
        smaller.reset();
        // Dispose all RAM ownership and simulate a fresh app/session with persisted files.
        first.reset();initial.reset();result.reset();fake::zoom=false;fake::now=0;
        result=run({},width,argc==7?std::atoi(argv[5]):0,argc==7?std::atoi(argv[6]):0);
        CHECK(result&&result->cacheHit&&result->pixels&&!result->error);
        CHECK(fake::tileWriteBytes==writes&&fake::swapWrites==swapWrites);
    }
    if(fake::mode=="tiles-replace") {
        CHECK(result&&result->pixels);
        const auto fingerprint=result->source->fingerprint;
        first.reset();result.reset();fake::zoom=false;fake::mutate=true;fake::now=0;
        result=run();CHECK(result&&result->pixels&&!result->cacheHit&&!result->error);
        CHECK(result->source->fingerprint!=fingerprint);
        auto changed=result;result=run(changed->source);
        CHECK(result&&result->pixels&&result->cacheHit&&!result->error);
    }
    if(fake::mode=="tiles-policy") {
        CHECK(result&&result->pixels&&entries()==1);
        auto original=std::filesystem::directory_iterator(fake::tileCache)->path();
        std::filesystem::last_write_time(original,std::filesystem::file_time_type::clock::now()-std::chrono::hours(24));
        for(unsigned n=0;n<96;++n)seedLevel(n);
        CHECK(entries()==96&&!std::filesystem::exists(original));
        auto old=result;result=run(old->source);CHECK(result&&result->pixels&&!result->cacheHit&&entries()==96);
        old=result;result=run(old->source);CHECK(result&&result->pixels&&result->cacheHit);
    }
    if(fake::mode=="tiles-stale" || fake::mode=="tiles-foreign-final") {
        CHECK(result&&result->pixels&&entries()==1);
        auto original=std::filesystem::directory_iterator(fake::tileCache)->path();
        if(fake::mode=="tiles-stale") {
            std::filesystem::copy_file(original,fake::tileCache+"/building.tmp");
            CHECK(std::filesystem::remove(original)); // Interrupted generation has no final file.
            auto old=result;result=run(old->source,150);CHECK(result&&result->pixels);
            old=result;result=run(old->source,150);CHECK(result&&result->pixels&&result->cacheHit);
        } else {
            FILE *f=std::fopen(original.string().c_str(),"wb");CHECK(f);CHECK(std::fwrite("USER DATA",1,9,f)==9);std::fclose(f);
            auto old=result;result=run(old->source);CHECK(result&&result->pixels&&!result->cacheHit);
            CHECK(std::filesystem::file_size(original)==9);
            f=std::fopen(original.string().c_str(),"rb");CHECK(f);char content[10]{};CHECK(std::fread(content,1,9,f)==9);std::fclose(f);CHECK(std::strcmp(content,"USER DATA")==0);
        }
    }
    if(fake::mode=="tiles-corrupt" || fake::mode=="tiles-truncate") {
        CHECK(result&&result->pixels);
        for(auto &entry:std::filesystem::directory_iterator(fake::tileCache))if(entry.path().extension()==".ytc") {
            if(fake::mode=="tiles-truncate")std::filesystem::resize_file(entry.path(),160);
            else {FILE *f=std::fopen(entry.path().string().c_str(),"r+b");CHECK(f);CHECK(std::fseek(f,136,SEEK_SET)==0);int c=std::fgetc(f);CHECK(c!=EOF);CHECK(std::fseek(f,136,SEEK_SET)==0);CHECK(std::fputc(c^1,f)!=EOF);std::fclose(f);}
        }
        auto old=result;result=run(old->source,width);
        CHECK(result&&result->pixels&&!result->cacheHit&&!result->error);
        CHECK(!std::memcmp(result->pixels,old->pixels,size_t(result->width)*result->height*2));
        old=result;result=run(old->source,width);CHECK(result&&result->cacheHit&&result->pixels);
    }
    bool ok=result&&result->pixels&&!result->error;
    if(ok) {
        FILE *f=std::fopen(argv[2],"wb");CHECK(f);
        CHECK(std::fwrite(result->pixels,2,size_t(result->width)*result->height,f)==size_t(result->width)*result->height);
        CHECK(std::fclose(f)==0);
    }
    std::printf("{\"ok\":%s,\"cancelled\":%s,\"width\":%u,\"height\":%u,\"cached\":%s,\"opens\":%u,\"reads\":%zu,\"max_read\":%zu,\"total_read\":%zu,\"peak\":%zu,\"allocations\":%zu,\"fast_attempts\":%u,\"error\":\"%s\",",
        ok?"true":"false",result&&result->cancelled?"true":"false",result?result->width:0,result?result->height:0,
        result&&result->source&&result->source->data?"true":"false",fake::opens,fake::reads,fake::maxRead,fake::total,probe_peak(),probe_attempts(),fake::fastAttempts,result&&result->error?result->error:"");
    std::printf("\"cache_hit\":%s,\"tile_read_bytes\":%llu,\"tile_write_bytes\":%llu,",result&&result->cacheHit?"true":"false",(unsigned long long)fake::tileReadBytes,(unsigned long long)fake::tileWriteBytes);
    std::printf("\"scaled_width\":%u,\"scaled_height\":%u,\"x\":%u,\"y\":%u,\"swap_opens\":%u,\"swap_reads\":%u,\"swap_writes\":%u,\"swap_bytes\":%llu,",
        result?result->view.scaled_width:0,result?result->view.scaled_height:0,result?result->view.x:0,result?result->view.y:0,
        fake::swapOpens,fake::swapReads,fake::swapWrites,(unsigned long long)fake::swapBytes);
    if(fake::mode=="swap-foreign") {
        CHECK(std::filesystem::file_size(fake::cache+"/coeff-0.tmp")==32);
        FILE *f=std::fopen((fake::cache+"/coeff-0.tmp").c_str(),"rb");CHECK(f);
        char tag[32]{};CHECK(std::fread(tag,1,32,f)==32);std::fclose(f);
        CHECK(std::strcmp(tag,"DO NOT DELETE USER DATA")==0);
    } else if(fake::mode!="swap-remove")CHECK(std::filesystem::is_empty(fake::cache));
    if(fake::mode=="tiles-foreign")CHECK(std::filesystem::file_size(fake::tileCache+"/building.tmp")==9);
    else if(fake::tiles && fake::mode!="tiles-remove")CHECK(!std::filesystem::exists(fake::tileCache+"/building.tmp"));
    CHECK(!fake::rawFiles&&fake::tempFiles.empty()&&fake::tileFiles.empty()&&fake::tileFds.empty());
    result.reset();first.reset();CHECK(!probe_live()&&!probe_used());CHECK(core::photo::idle());
    CHECK(!fake::files&&!fake::sessions&&!fake::guards);CHECK(probe_peak()<=capacity);
    std::printf("\"checks\":%u,\"released\":true}\n",checks);
}
