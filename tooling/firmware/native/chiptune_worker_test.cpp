// Real worker bodies and parser; fake RTOS/audio/SD admission, real fixture I/O.
#include "core/chiptune_player.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <chrono>
#include <thread>
#include <mutex>
#include <functional>
#include <condition_variable>
#include <iostream>
static std::atomic<unsigned> checks{0};
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": "<<#x<<"\n";std::exit(1);}}while(0)
namespace fake {
std::atomic<bool> sleep{false},mounted{true},taskFail{false},audioFail{false},writeFail{false},otherAudio{false};
std::atomic<int> sessions{0},files{0},dirs{0},opens{0},closes{0},writes{0},work{0};
std::string root;std::vector<std::thread> tasks;std::mutex taskMutex,audioMutex;bool opened=false;std::thread::id owner;
std::string path(const char *s){const std::string p=s;CHECK(p.rfind("/sdcard/chiptune",0)==0);return root+p.substr(7);}
FILE *open(const char *s,const char *mode){auto f=std::fopen(path(s).c_str(),mode);if(f)++files;return f;}
int close(FILE *f){--files;return std::fclose(f);}
DIR *directory(const char *s){auto d=opendir(path(s).c_str());if(d)++dirs;return d;}
int closeDirectory(DIR *d){--dirs;return closedir(d);}
int info(const char *s,struct stat *st){return stat(path(s).c_str(),st);}
void wait(std::function<bool()> predicate){for(unsigned i=0;i<5000&&!predicate();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(predicate());}
void join(){std::lock_guard<std::mutex> lock(taskMutex);for(auto &t:tasks)if(t.joinable())t.join();tasks.clear();CHECK(files==0&&dirs==0&&sessions==0&&work==0);}
}
namespace sys {
struct Power {static bool sleepPending(){return fake::sleep;}struct WorkGuard {WorkGuard(){++fake::work;}~WorkGuard(){--fake::work;}};};
struct Audio {
 static bool openStream(bool record,int rate){CHECK(!record&&rate==22050);std::lock_guard<std::mutex> lock(fake::audioMutex);if(fake::audioFail)return false;CHECK(!fake::opened);fake::opened=true;fake::owner=std::this_thread::get_id();++fake::opens;return true;}
 static bool writeStream(void *data,int bytes){CHECK(data&&bytes>0&&bytes<=512&&bytes%2==0);CHECK(fake::opened&&fake::owner==std::this_thread::get_id());++fake::writes;std::this_thread::sleep_for(std::chrono::milliseconds(1));return !fake::writeFail;}
 static void closeStream(){std::lock_guard<std::mutex> lock(fake::audioMutex);CHECK(fake::opened&&fake::owner==std::this_thread::get_id());fake::opened=false;++fake::closes;}
};
}
namespace core::sd {
Access requestAccess(bool resume){return {1,resume};}
bool usbExclusive(){return false;}
Session::Session(Access,bool,bool){held_=fake::mounted;if(held_)++fake::sessions;else error_=EIO;}
Session::~Session(){release();}
void Session::release(){if(held_){held_=false;--fake::sessions;}}
void Session::reportError(int){}
}
namespace core::recorder {bool idle(){return !fake::otherAudio;}}
namespace core::mp3 {bool idle(){return true;}}
namespace core::fc {bool idle(){return true;}}
namespace core::game_audio {bool idle(){return true;}}
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
struct Notification {std::mutex mutex;std::condition_variable cv;bool pending=false;};
using TaskHandle_t=Notification *;
static thread_local Notification notification;
static TaskHandle_t xTaskGetCurrentTaskHandle(){return &notification;}
static void xTaskNotifyGive(TaskHandle_t task){std::lock_guard<std::mutex> lock(task->mutex);task->pending=true;task->cv.notify_one();}
static unsigned ulTaskNotifyTake(int,unsigned ms){std::unique_lock<std::mutex> lock(notification.mutex);notification.cv.wait_for(lock,std::chrono::milliseconds(ms),[]{return notification.pending;});notification.pending=false;return 1;}
static int xTaskCreateWithCaps(void (*fn)(void *),const char *,unsigned,void *arg,unsigned,void *,unsigned){
 if(fake::taskFail.exchange(false))return 0;
 std::lock_guard<std::mutex> lock(fake::taskMutex);fake::tasks.emplace_back(fn,arg);return pdPASS;
}
static void vTaskDelay(unsigned){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
static void vTaskDeleteWithCaps(void *){}
#include "worker.inc"
using namespace core::chip;
static void stop(std::shared_ptr<Player> &p){p->cancel=true;p->wake();fake::wait([&]{return p->done&&idle();});p.reset();fake::join();CHECK(!busy()&&storageIdle());CHECK(fake::opens==fake::closes);}
int main(int argc,char **argv){
 CHECK(argc==2);fake::root=argv[1];
 fake::taskFail=true;CHECK(!scan()&&storageIdle());fake::taskFail=true;CHECK(!start("/sdcard/chiptune/000.chip.json")&&idle());
 fake::otherAudio=true;CHECK(!start("/sdcard/chiptune/000.chip.json"));fake::otherAudio=false;
 fake::sleep=true;CHECK(!scan()&&!start("/sdcard/chiptune/000.chip.json"));fake::sleep=false;
 auto list=scan();CHECK(list);CHECK(!scan());fake::wait([&]{return list->done.load();});CHECK(list->error.empty()&&list->truncated&&list->entries.size()==200);
 CHECK(list->entries.front().name=="000.chip.json"&&list->entries.back().name=="199.chip.json");CHECK(!list->entries[1].error.empty());
 CHECK(list->entries[3].inspected&&!list->entries[4].inspected);
 auto catalog=list->entries;list.reset();fake::join();
 list=titles(catalog,4);CHECK(list&&list->metadataOnly);fake::wait([&]{return list->done.load();});
 CHECK(list->entries.size()==4&&list->offset==4&&list->entries.front().title=="fixture");
 CHECK(list->entries.back().inspected);list.reset();fake::join();
 fake::mounted=false;list=scan();fake::wait([&]{return list->done.load();});CHECK(!list->error.empty()&&list->entries.empty());list.reset();fake::join();fake::mounted=true;
 list=scan();list->cancel=true;fake::wait([&]{return list->done.load();});list.reset();fake::join();
 auto p=start("/sdcard/chiptune/001.chip.json");CHECK(p);fake::wait([&]{return p->done.load();});CHECK(p->state==State::Failed&&!p->error.empty());stop(p);
 for(unsigned cycle=0;cycle<12;++cycle){
  p=start("/sdcard/chiptune/000.chip.json");CHECK(p);CHECK(!start("/sdcard/chiptune/002.chip.json"));
  fake::wait([&]{return p->position>100&&p->state==State::Playing;});CHECK(p->tempo==120&&p->duration==4000&&p->title=="fixture");
  CHECK(storageIdle()&&busy());fake::mounted=false;const auto before=p->position.load();fake::wait([&]{return p->position>before+100;});CHECK(p->state==State::Playing);
  p->play=false;fake::wait([&]{return p->state==State::Paused;});const auto paused=p->position.load();std::this_thread::sleep_for(std::chrono::milliseconds(12));CHECK(p->position==paused&&!busy()&&fake::opens==fake::closes);
  p->play=true;p->wake();fake::wait([&]{return p->position>paused+30;});fake::mounted=true;
  p->rewind=true;fake::wait([&]{return p->state==State::Stopped;});CHECK(p->position==0&&!busy());
  p->play=true;fake::wait([&]{return p->position>50;});stop(p);
 }
 p=start("/sdcard/chiptune/000.chip.json");p->cancel=true;stop(p);
 fake::audioFail=true;p=start("/sdcard/chiptune/000.chip.json");fake::wait([&]{return p->done.load();});CHECK(p->state==State::Failed&&!p->error.empty());stop(p);fake::audioFail=false;
 fake::writeFail=true;p=start("/sdcard/chiptune/000.chip.json");fake::wait([&]{return p->done.load();});CHECK(p->state==State::Failed&&!p->error.empty());stop(p);fake::writeFail=false;
 p=start("/sdcard/chiptune/000.chip.json");fake::wait([&]{return p->position>10;});p->loop=false;fake::wait([&]{return p->state==State::Stopped;});CHECK(p->position==0);stop(p);
 p=start("/sdcard/chiptune/000.chip.json");fake::wait([&]{return p->position>50;});fake::sleep=true;fake::wait([&]{return p->done.load();});stop(p);fake::sleep=false;
 CHECK(fake::opens==fake::closes&&!fake::opened);std::cout<<checks<<" worker checks; 12 pause/resume/stop cycles, scan limit/order/errors, task failure, unmount during playback, cancellation, EOF and deep-sleep release passed\n";
}
