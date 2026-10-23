// Actual GIF worker and frame clock. RTOS, SD, decode cost and GUI are controlled
// test doubles; exact decoder pixels are checked separately against Pillow.
#include "core/gif_player.hpp"
#include "photo_gif.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <strings.h>
#include <sys/stat.h>
#include <vector>
static unsigned checks=0,cases=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
namespace fake {
std::shared_ptr<core::gif::Job> owner;
std::weak_ptr<core::gif::Job> weak;
void (*task)(void *)=nullptr;void *arg=nullptr;
uint64_t now=0,base=0,nextGui=0;
unsigned cost=4,target=30,polls=0,decoded=0,restarts=0,decoders=0;
unsigned sessions=0,files=0,allocs=0,leases=0,frameNo=0;
unsigned failDecode=0,cancelDecode=0,failedAllocation=0;
bool sdBusy=false,sleep=false,openFail=false,taskFail=false,sessionFail=false;
bool jitter=false,dropOwner=false,stopWhenQueued=false;
uint16_t *shown=nullptr;uint16_t shownValue=0;
std::vector<unsigned> delays{20};
std::vector<uint64_t> times;
std::vector<unsigned> shownDelays;
std::function<void()> hook;
void inspect() {if(shown)for(unsigned i=0;i<16;++i)CHECK(shown[i]==shownValue);}
void gui() {
    if(!owner)return;
    auto &j=*owner;const auto frame=j.readyFrame(uint32_t(now));
    if(frame>=0) {
        inspect();shown=j.pixels[frame];shownValue=shown[0];
        CHECK(shownValue==times.size()+1);inspect();
        times.push_back(now);shownDelays.push_back(j.delayMs);j.present(frame,uint32_t(now));
        if(times.size()>=target) {shown=nullptr;j.cancelled=true;if(dropOwner)owner.reset();}
    }
}
void advance(unsigned ms) {
    while(ms--) {
        ++now;CHECK(now-base<30000);inspect();
        if(hook)hook();
        if(owner && stopWhenQueued && owner->ready>=0 && owner->displayed>=0) {
            shown=nullptr;owner->cancelled=true;if(dropOwner)owner.reset();
        }
        if(now>=nextGui) {
            gui();static const unsigned intervals[]={7,13,11,9};
            nextGui=now+(jitter?intervals[polls++%4]:10);
        }
    }
}
}
static int64_t esp_timer_get_time(){return int64_t(fake::now*1000);}
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
static void vTaskDelay(unsigned ticks){CHECK(ticks>0);fake::advance(ticks);}
static void vTaskDeleteWithCaps(void *){}
static int xTaskCreateWithCaps(void (*fn)(void *),const char *,unsigned,void *arg,unsigned,void *,unsigned) {
    if(fake::taskFail)return 0;
    fake::task=fn;fake::arg=arg;return pdPASS;
}
static void *heap_caps_malloc(size_t n,unsigned) {
    if(fake::failedAllocation && --fake::failedAllocation==0)return nullptr;
    void *p=std::malloc(n);if(p)++fake::allocs;return p;
}
static void heap_caps_free(void *p){if(p){CHECK(fake::allocs>0);--fake::allocs;std::free(p);}}
static int fakeStat(const char *,struct stat *s){s->st_mode=S_IFREG;s->st_size=1024;return 0;}
static FILE *fakeFopen(const char *,const char *) {
    CHECK(fake::sessions==1);if(fake::openFail){errno=EIO;return nullptr;}
    FILE *f=std::tmpfile();CHECK(f);++fake::files;return f;
}
static int fakeFclose(FILE *f){CHECK(fake::sessions==1&&fake::files==1);--fake::files;return std::fclose(f);}
namespace sys {
struct Power {
    static bool sleepPending(){return fake::sleep;}
    struct WorkGuard {WorkGuard(){++fake::leases;}~WorkGuard(){CHECK(fake::leases==1);--fake::leases;}};
};
}
namespace core::sd {
Access requestAccess(bool){return {};}
bool busy(){return fake::sdBusy;}
Session::Session(Access,bool,bool){if(!fake::sessionFail){held_=true;++fake::sessions;}}
Session::~Session(){if(held_){CHECK(fake::files==0&&fake::decoders==0);--fake::sessions;}}
void Session::reportError(int){}
}
struct photo_gif {void *ctx;photo_gif_cancel_cb cancel;unsigned position=0;};
extern "C" {
photo_gif *photo_gif_open(void *ctx,photo_gif_read_cb,photo_gif_seek_cb,photo_gif_cancel_cb cb) {
    CHECK(fake::sessions==1&&fake::files==1);++fake::decoders;return new photo_gif{ctx,cb};
}
void photo_gif_close(photo_gif *g){if(g){CHECK(fake::decoders==1);--fake::decoders;delete g;}}
unsigned photo_gif_width(const photo_gif *){return 4;}
unsigned photo_gif_height(const photo_gif *){return 4;}
int photo_gif_restart(photo_gif *g){++fake::restarts;g->position=0;return 1;}
int photo_gif_next(photo_gif *g,uint16_t *out,size_t pixels,unsigned *delay) {
    CHECK(pixels==16&&out!=fake::shown);++fake::decoded;
    if(g->position==fake::delays.size())return 0;
    for(unsigned i=0;i<fake::cost;++i) {
        fake::advance(1);
        if(fake::cancelDecode==fake::decoded && i==1 && fake::owner) {
            fake::shown=nullptr;fake::owner->cancelled=true;if(fake::dropOwner)fake::owner.reset();
        }
        if(g->cancel(g->ctx))return -2;
    }
    if(fake::failDecode==fake::decoded)return -1;
    CHECK(out!=fake::shown);std::fill(out,out+pixels,uint16_t(++fake::frameNo));
    *delay=fake::delays[g->position++];return 1;
}
}
#define stat(path,out) fakeStat(path,out)
#define fopen fakeFopen
#define fclose fakeFclose
#include "gif_player.inc"
#undef fclose
#undef fopen
#undef stat
static void reset(uint64_t start=1000) {
    CHECK(core::gif::idle());CHECK(!fake::owner&&fake::weak.expired());
    CHECK(!fake::files&&!fake::sessions&&!fake::decoders&&!fake::allocs&&!fake::leases);
    fake::base=fake::now=start;fake::nextGui=start+10;fake::cost=4;fake::target=30;
    fake::polls=fake::decoded=fake::restarts=fake::frameNo=0;
    fake::failDecode=fake::cancelDecode=fake::failedAllocation=0;
    fake::sdBusy=fake::sleep=fake::openFail=fake::taskFail=fake::sessionFail=false;
    fake::jitter=fake::dropOwner=fake::stopWhenQueued=false;fake::shown=nullptr;
    fake::delays={20};fake::times.clear();fake::shownDelays.clear();fake::hook={};
    fake::task=nullptr;fake::arg=nullptr;++cases;
}
static void run(bool error=false) {
    fake::owner=core::gif::start("/sdcard/synthetic.gif");CHECK(fake::owner);
    fake::weak=fake::owner;CHECK(!core::gif::idle());CHECK(!core::gif::start("/sdcard/other.gif"));
    CHECK(fake::task);fake::task(fake::arg);
    CHECK(core::gif::idle());CHECK(!fake::sessions&&!fake::files&&!fake::decoders&&!fake::leases);
    if(fake::owner) {CHECK(fake::owner->done);CHECK((fake::owner->error!=nullptr)==error);}
    fake::shown=nullptr;fake::owner.reset();CHECK(fake::weak.expired());CHECK(fake::allocs==0);
}
static void cadence(unsigned tolerance=0) {
    CHECK(fake::times.size()==fake::target);
    uint64_t expected=fake::times[0];
    for(size_t i=1;i<fake::times.size();++i) {
        expected+=fake::shownDelays[i-1];
        CHECK(fake::times[i]+tolerance>=expected&&fake::times[i]<=expected+tolerance);
    }
}
int main() {
    reset();run();cadence();CHECK(fake::restarts>20);
    reset();fake::delays={20,30,50,70,100,200};fake::target=60;run();cadence();
    reset(0xffffffe0ULL);fake::target=50;run();cadence();CHECK(fake::times.back()>0x100000000ULL);
    reset();fake::jitter=true;fake::target=100;run();cadence(13);
    // A slow frame/GUI pause rebases once instead of trying to flash stale frames.
    reset();fake::cost=60;run();CHECK(fake::times.size()==fake::target);
    for(size_t i=1;i<fake::times.size();++i)CHECK(fake::times[i]-fake::times[i-1]>=20);
    reset();fake::hook=[](){if(fake::now==fake::base+51)fake::nextGui=fake::now+250;};run();
    bool paused=false;for(size_t i=1;i<fake::times.size();++i) {
        auto dt=fake::times[i]-fake::times[i-1];CHECK(dt>=20);if(dt>=250)paused=true;
    }CHECK(paused);
    reset();fake::dropOwner=true;run();
    reset();fake::dropOwner=true;fake::cancelDecode=3;run();
    reset();fake::dropOwner=true;fake::stopWhenQueued=true;run();CHECK(fake::times.size()==1);
    reset();fake::hook=[](){if(fake::now-fake::base>60)fake::sdBusy=true;};run();
    reset();fake::hook=[](){if(fake::now-fake::base>60)fake::sleep=true;};run();
    reset();fake::failDecode=3;run(true);
    reset();fake::cost=5000;run(true);CHECK(fake::times.empty());
    reset();fake::openFail=true;run(true);
    reset();fake::sessionFail=true;run(true);
    reset();fake::failedAllocation=1;run(true);
    reset();fake::failedAllocation=2;run(true);
    reset();fake::taskFail=true;CHECK(!core::gif::start("/sdcard/synthetic.gif"));CHECK(core::gif::idle());
    reset();CHECK(!core::gif::start("/other/a.gif"));CHECK(!core::gif::start("/sdcard/../a.gif"));CHECK(core::gif::idle());
    reset();
    std::printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"buffers_and_SD_released\":true}\n",cases-1,checks);
}
