// Production Settings cache/flush, isolated NVS adapter; fresh process readback.
#include "core/settings_store.hpp"
#include "core/mp3_model.hpp"
#include "esp_err.h"
#include <algorithm>
#include <mutex>
#include <new>
#include <utility>
#include <cstring>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define ESP_LOGW(...)
using nvs_handle_t=int;constexpr int NVS_READONLY=0,NVS_READWRITE=1;
using namespace sys::app_order;
static Encoded disk{},pending{};
static std::string diskTrack,pendingTrack;
static bool changeMusicDuringCommit=false;
static bool haveDisk=false,failOpen=false,failWrite=false,failCommit=false,changeDuringCommit=false;
static unsigned writes=0,commits=0;static int64_t now=10000000;
constexpr Order initial{{key("clock"),key("vault"),key("recorder"),Settings},4};
constexpr Order latest{{key("recorder"),key("clock"),key("vault"),Settings},4};
static int64_t esp_timer_get_time(){return now;}
static int nvs_open(const char*,int,int *h){if(failOpen)return -1;*h=1;pendingTrack=diskTrack;return 0;}
static void nvs_close(int){}
static int nvs_get_str(int,const char*k,char*data,size_t*size){
 if(std::strcmp(k,"mp3_track")||diskTrack.empty())return -1;
 if(data){CHECK(*size>=diskTrack.size()+1);std::memcpy(data,diskTrack.c_str(),diskTrack.size()+1);}
 *size=diskTrack.size()+1;return 0;
}
static int nvs_get_i32(int,const char*,int32_t*){return -1;}
static int nvs_get_i64(int,const char*,int64_t*){return -1;}
static int nvs_get_u32(int,const char*,uint32_t*){return -1;}
static int nvs_get_blob(int,const char *name,void *buffer,size_t *size){
    if(std::strcmp(name,"app_order_v1")||!haveDisk)return -1;
    CHECK(*size>=disk.size());std::memcpy(buffer,disk.data(),disk.size());*size=disk.size();return 0;
}
static int nvs_set_i32(int,const char*,int32_t){CHECK(false);return -1;}
static int nvs_set_i64(int,const char*,int64_t){CHECK(false);return -1;}
static int nvs_set_u32(int,const char*,uint32_t){CHECK(false);return -1;}
static int nvs_set_str(int,const char*k,const char*v){CHECK(!std::strcmp(k,"mp3_track"));++writes;if(failWrite)return -1;pendingTrack=v;return 0;}
static int nvs_set_blob(int,const char *name,const void *buffer,size_t size){
    CHECK(std::strcmp(name,"app_order_v1")==0&&size==pending.size());++writes;
    if(failWrite)return -1;std::memcpy(pending.data(),buffer,size);return 0;
}
static int nvs_commit(int){
    ++commits;
    if(changeDuringCommit){changeDuringCommit=false;CHECK(sys::Settings::setAppOrder(latest));}
    if(changeMusicDuringCommit){changeMusicDuringCommit=false;sys::Settings::rememberMp3Track("/sdcard/MP3/夜色.mp3");}
    if(failCommit)return -1;disk=pending;diskTrack=pendingTrack;haveDisk=true;return 0;
}
struct FakeTimeval{int64_t tv_sec,tv_usec;};static int fakeSetTime(FakeTimeval*,void*){return 0;}
#include "settings.inc"
int main(int argc,char **argv){
    CHECK(argc==3);const bool write=std::strcmp(argv[1],"write")==0;
    if(!write){
        FILE *file=std::fopen(argv[2],"rb");CHECK(file);CHECK(std::fread(disk.data(),1,disk.size(),file)==disk.size());std::fclose(file);haveDisk=true;
        file=std::fopen((std::string(argv[2])+".mp3-track").c_str(),"rb");CHECK(file);char text[1024]{};auto n=std::fread(text,1,sizeof(text)-1,file);std::fclose(file);diskTrack.assign(text,n);
        if(std::strcmp(argv[1],"corrupt")==0){disk[19]^=1;diskTrack="/sdcard/elsewhere.mp3";}
    }
    using sys::Settings;Settings::load();
    if(!write){
        if(std::strcmp(argv[1],"corrupt")==0)CHECK(Settings::appOrder().count==0&&Settings::mp3Track().empty());
        else CHECK(Settings::appOrder()==latest&&Settings::mp3Track()=="/sdcard/MP3/夜色.mp3");
        CHECK(Settings::appOrderSaveState()==SaveState::Saved&&writes==0&&commits==0);
    }else{
        CHECK(Settings::appOrder().count==0);CHECK(Settings::setAppOrder(initial));
        CHECK(Settings::appOrderSaveState()==SaveState::Pending);CHECK(Settings::flush()&&writes==0);
        now+=600000;CHECK(Settings::flush()&&writes==1&&commits==1&&decode(disk)==initial);
        CHECK(Settings::appOrderSaveState()==SaveState::Saved);
        CHECK(Settings::setAppOrder(initial)&&Settings::flush(true)&&writes==1);
        for(int failure=0;failure<3;++failure){
            CHECK(Settings::setAppOrder(latest));
            failOpen=failure==0;failWrite=failure==1;failCommit=failure==2;
            CHECK(!Settings::flush(true));CHECK(Settings::appOrderSaveState()==SaveState::Failed);
            CHECK(decode(disk)==initial);failOpen=failWrite=failCommit=false;
            const auto count=writes;now+=100000;CHECK(Settings::flush()&&writes==count);
            now+=2100000;CHECK(Settings::flush());CHECK(Settings::appOrderSaveState()==SaveState::Saved&&decode(disk)==latest);
            CHECK(Settings::setAppOrder(initial)&&Settings::flush(true));
        }
        // A newer drop while the worker is committing must not be reported saved
        // by the old transaction; it must remain pending until its own commit.
        Order interim=initial;CHECK(move(interim,0,1));CHECK(Settings::setAppOrder(interim));
        changeDuringCommit=true;CHECK(Settings::flush(true));CHECK(Settings::appOrderSaveState()==SaveState::Pending);
        CHECK(decode(disk)==interim&&Settings::appOrder()==latest);
        now+=600000;CHECK(Settings::flush()&&decode(disk)==latest&&Settings::appOrderSaveState()==SaveState::Saved);
        FILE *file=std::fopen(argv[2],"wb");CHECK(file);CHECK(std::fwrite(disk.data(),1,disk.size(),file)==disk.size());std::fclose(file);
        const auto count=writes;const std::string first="/sdcard/MP3/初夏.mp3";
        Settings::rememberMp3Track(first);CHECK(Settings::mp3Track()==first&&writes==count);CHECK(Settings::flush()&&writes==count);
        now+=600000;CHECK(Settings::flush()&&diskTrack==first&&writes==count+1);
        Settings::rememberMp3Track(first);CHECK(Settings::flush(true)&&writes==count+1);
        for(const std::string &bad:std::vector<std::string>{"/sdcard/other.mp3","/sdcard/MP3/sub/track.mp3","/sdcard/MP3/a.wav",std::string(1024,'x')})Settings::rememberMp3Track(bad);
        CHECK(Settings::mp3Track()==first&&Settings::flush(true)&&writes==count+1);
        Settings::rememberMp3Track("/sdcard/MP3/午后.mp3");failCommit=true;CHECK(!Settings::flush(true)&&diskTrack==first);failCommit=false;
        changeMusicDuringCommit=true;CHECK(Settings::flush(true));CHECK(diskTrack=="/sdcard/MP3/午后.mp3"&&Settings::mp3Track()=="/sdcard/MP3/夜色.mp3");
        now+=2100000;CHECK(Settings::flush()&&diskTrack=="/sdcard/MP3/夜色.mp3");
        file=std::fopen((std::string(argv[2])+".mp3-track").c_str(),"wb");CHECK(file);CHECK(std::fwrite(diskTrack.data(),1,diskTrack.size(),file)==diskTrack.size());std::fclose(file);
    }
    std::printf("%u order storage checks (%s): production cache/flush, batched writes, retry, in-flight replacement, fresh process load\n",checks,argv[1]);
}
