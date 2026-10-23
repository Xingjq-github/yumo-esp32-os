// Actual worker + decoder. All files are in the supplied synthetic SD tree.
#include "core/mp3_player.hpp"
#include "minimp3_ex.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <climits>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <chrono>
#include <thread>
#include <mutex>
#include <functional>
#include <iostream>
#include <array>
static std::atomic<unsigned> checks{0};
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": "<<#x<<"\n";std::exit(1);}}while(0)
namespace fake {
std::atomic<bool> sleep{false},mounted{true},taskFail{false},sinkFail{false},audioFail{false},writeFail{false},otherAudio{false},eject{false};
std::atomic<int> sessions{0},files{0},dirs{0},opens{0},closes{0},writes{0},work{0};
std::atomic<bool> audio{false};std::string root,last;std::vector<std::thread> tasks;std::mutex taskMutex,lastMutex;
bool capture=false,paced=false,jitter=false;unsigned hz=44100,reads=0,delays=0,progressStalls=0;std::vector<int16_t> pcm;
std::chrono::steady_clock::time_point lastWrite;double maxGap=0;
std::string path(const char *s){std::string p=s;CHECK(p.rfind("/sdcard/MP3",0)==0);return root+p.substr(7);}
FILE *open(const char *s,const char *m){auto f=fopen(path(s).c_str(),m);if(f)++files;return f;}
int close(FILE *f){--files;return fclose(f);}
size_t read(void*d,size_t s,size_t n,FILE*f){
 if(jitter&&writes>0&&++reads%16==0){
  ++delays;const auto before=writes.load();std::this_thread::sleep_for(std::chrono::milliseconds(100));
  if(writes>before)++progressStalls;
 }
 return fread(d,s,n,f);
}
void output(int16_t*p,size_t samples,unsigned channels){
 if(capture)pcm.insert(pcm.end(),p,p+samples);
 if(paced){
  auto now=std::chrono::steady_clock::now();
  if(writes>1)maxGap=std::max(maxGap,std::chrono::duration<double,std::milli>(now-lastWrite).count());
  std::this_thread::sleep_for(std::chrono::microseconds(samples*1000000ull/(hz*channels)));
  lastWrite=std::chrono::steady_clock::now();
 }else std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
int info(const char*s,struct stat *st){return stat(path(s).c_str(),st);}
DIR *directory(const char*s){auto d=opendir(path(s).c_str());if(d)++dirs;return d;}
int closeDir(DIR*d){--dirs;return closedir(d);}
void wait(std::function<bool()> pred){for(int i=0;i<6000&&!pred();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(pred());}
void join(){for(;;){std::vector<std::thread> batch;{std::lock_guard<std::mutex> l(taskMutex);batch.swap(tasks);}if(batch.empty())break;for(auto&t:batch)t.join();}CHECK(files==0&&dirs==0&&sessions==0&&work==0&&!audio);CHECK(opens==closes);}
}
namespace sys {
struct Power {static bool sleepPending(){return fake::sleep;}struct WorkGuard{WorkGuard(){++fake::work;}~WorkGuard(){--fake::work;}};};
struct Settings {static int volume(){return 50;}static void rememberMp3Track(const std::string&p){std::lock_guard<std::mutex>l(fake::lastMutex);fake::last=p;}};
struct Audio {
 static bool openStream(bool rec,int hz){CHECK(!rec&&hz>=8000&&hz<=48000);if(fake::audioFail)return false;CHECK(!fake::audio);fake::audio=true;fake::hz=hz;++fake::opens;return true;}
 static bool writeStream(void*p,int n){CHECK(fake::audio&&p&&n>0&&n<=1152&&n%2==0);++fake::writes;fake::output(static_cast<int16_t*>(p),n/2,1);return !fake::writeFail;}
 static void closeStream(){CHECK(fake::audio);fake::audio=false;++fake::closes;}
};
}
namespace core {
std::string ensureUtf8(const char*p){return p;}
std::string decodeText(const std::string&p){return p;}
namespace sd {
Access requestAccess(bool resume){return {1,resume};}
State state(){return fake::eject?State::Ejecting:State::Available;}
Session::Session(Access,bool,bool){held_=fake::mounted;if(held_)++fake::sessions;}
Session::~Session(){release();}
void Session::release(){if(held_){held_=false;--fake::sessions;}}
void Session::reportError(int){}
}
namespace recorder{bool idle(){return !fake::otherAudio;}}
namespace chip{bool idle(){return true;}}
namespace fc{bool idle(){return true;}}
namespace game_audio{bool idle(){return true;}}
}
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
static void vTaskDelay(unsigned){std::this_thread::sleep_for(std::chrono::milliseconds(1));}
static void vTaskDeleteWithCaps(void*){}
static int xTaskCreateWithCaps(void(*fn)(void*),const char*name,unsigned,void*arg,unsigned,void*,unsigned){if(fake::taskFail.exchange(false)||(!std::strcmp(name,"mp3_sink")&&fake::sinkFail.exchange(false)))return 0;std::lock_guard<std::mutex>l(fake::taskMutex);fake::tasks.emplace_back(fn,arg);return pdPASS;}
static int xTaskCreatePinnedToCoreWithCaps(void(*fn)(void*),const char*n,unsigned s,void*a,unsigned p,void*h,unsigned,unsigned caps){return xTaskCreateWithCaps(fn,n,s,a,p,h,caps);}
#include "worker.inc"
using namespace core::mp3;
static void stop(std::shared_ptr<Player>&p){p->cancel=true;fake::wait([&]{return p->done&&idle();});p.reset();fake::join();}
int main(int argc,char**argv){
 CHECK(argc==2);fake::root=argv[1];
 CHECK(!start("/sdcard/elsewhere.mp3"));fake::taskFail=true;CHECK(!scan()&&storageIdle());fake::taskFail=true;CHECK(!start("/sdcard/MP3/000.mp3")&&idle());
 fake::otherAudio=true;CHECK(!start("/sdcard/MP3/000.mp3"));fake::otherAudio=false;
 auto list=scan();CHECK(list);CHECK(!scan());fake::wait([&]{return list->done.load();});CHECK(list->entries.size()==512&&list->truncated&&list->entries.front().path=="/sdcard/MP3/000.mp3");list.reset();fake::join();
 for(int cycle=0;cycle<8;++cycle){
  auto p=start("/sdcard/MP3/000.mp3");CHECK(p);CHECK(!start("/sdcard/MP3/000.mp3"));
  fake::wait([&]{return p->state==State::Playing&&p->position>100;});CHECK(p->metadataReady&&p->lyrics.size()==2&&p->duration>1000);
  p->play=false;fake::wait([&]{return p->state==State::Paused;});std::this_thread::sleep_for(std::chrono::milliseconds(8));auto pos=p->position.load();std::this_thread::sleep_for(std::chrono::milliseconds(8));CHECK(pos==p->position&&!fake::audio);
  // Back-to-back seeks while paused must complete the sink acknowledgement,
  // including a new request before the previous hold has been released.
  for(unsigned target:{500u,1500u,200u,1800u,1000u}){p->seekMs=target;fake::wait([&]{return p->position==target;});}
  p->play=true;fake::wait([&]{return p->state==State::Playing&&p->position>1100;});stop(p);
 }
 auto p=start("/sdcard/MP3/bad.mp3");fake::wait([&]{return p->done.load();});CHECK(p->state==State::Failed);stop(p);
 fake::sinkFail=true;p=start("/sdcard/MP3/000.mp3");fake::wait([&]{return p->done.load();});CHECK(p->state==State::Failed);stop(p);
 fake::audioFail=true;p=start("/sdcard/MP3/000.mp3");fake::wait([&]{return p->done.load();});CHECK(p->state==State::Failed);stop(p);fake::audioFail=false;
 fake::writeFail=true;p=start("/sdcard/MP3/000.mp3");fake::wait([&]{return p->done.load();});CHECK(p->state==State::Failed);stop(p);fake::writeFail=false;

 p=start("/sdcard/MP3/000.mp3");fake::wait([&]{return p->position>30;});fake::eject=true;fake::wait([&]{return p->done.load();});CHECK(p->state==State::Failed);stop(p);fake::eject=false;
 p=start("/sdcard/MP3/000.mp3");fake::wait([&]{return p->position>30;});fake::sleep=true;fake::wait([&]{return p->done.load();});stop(p);fake::sleep=false;
 CHECK(fake::last=="/sdcard/MP3/000.mp3");
 // Decode a full track twice, then inject 100 ms SD stalls while the paced
 // output continues. Every PCM sample (including the tail) must be identical.
 {
  std::vector<int16_t> reference;
  for(int pass=0;pass<2;++pass){
   fake::capture=true;fake::paced=true;fake::jitter=pass;fake::reads=fake::delays=fake::progressStalls=0;fake::writes=0;fake::maxGap=0;fake::pcm.clear();
   p=start("/sdcard/MP3/000.mp3");CHECK(p);
   fake::wait([&]{return p->done.load();});
   std::cout<<"pass="<<pass<<" state="<<int(p->state.load())<<" position="<<p->position<<" duration="<<p->duration<<" underruns="<<p->underruns<<" stalls="<<fake::delays<<" gap="<<fake::maxGap<<" error="<<p->error<<"\n";
   CHECK(p->state==State::Ended&&p->position==p->duration&&p->underruns==0);stop(p);
   if(!pass)reference=fake::pcm;
   else {
    CHECK(fake::delays>0&&fake::pcm==reference);
    // Test independence directly: a synchronous reader cannot output ANY PCM
    // during its own read stall. Windows scheduling gaps are diagnostic only,
    // not a claim about the ESP32 DMA deadline or physical audible underruns.
    CHECK(fake::progressStalls==fake::delays);
    std::cout<<"speaker"<<" SD stalls="<<fake::delays<<", continued output during="<<fake::progressStalls<<", max host scheduling gap="<<fake::maxGap<<" ms, exact PCM samples="<<fake::pcm.size()<<"\n";
   }
  }
 }
 std::cout<<checks<<" checks; actual worker/decoder scan, pause/seek/resume, task errors, audio failure, eject and sleep release passed\n";
}
