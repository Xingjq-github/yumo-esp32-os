// Production wait adapter, game audio worker and FC pause branch on fake RTOS.
// This checks command/exit races, not I2S latency or real-device power draw.
#include "core/worker_wake.hpp"
#include "core/game_audio.hpp"
#include "core/morse_signal.hpp"
#include "core/fc_emulator.hpp"
#include "core/fc_autosave.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>
static std::atomic<unsigned> checks{0};
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<__LINE__<<": "<<#x<<"\n";std::exit(1);}}while(0)
namespace fake {
std::atomic<bool> sleep{false},mayRun{true},failTask{false},failOpen{false},failWrite{false};
std::atomic<unsigned> waits{0},timeouts{0},writes{0},sounds{0},opens{0},closes{0},work{0};
std::atomic<bool> opened{false};std::vector<std::thread> tasks;
void until(std::function<bool()> condition) {
    for(unsigned i=0;i<3000&&!condition();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(condition());
}
void join(){for(auto &t:tasks)t.join();tasks.clear();CHECK(work==0&&!opened&&opens==closes);}
}
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define pdPASS 1
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGW(...)
using TickType_t=uint32_t;
struct Notification {std::mutex mutex;std::condition_variable cv;unsigned count=0;};
using TaskHandle_t=Notification *;
static thread_local Notification mailbox;
static TaskHandle_t xTaskGetCurrentTaskHandle(){return &mailbox;}
static void xTaskNotifyGive(TaskHandle_t task){std::lock_guard<std::mutex> lock(task->mutex);++task->count;task->cv.notify_one();}
static unsigned ulTaskNotifyTake(int clear,unsigned ms) {
    CHECK(clear==pdTRUE&&ms>0&&ms<=250);++fake::waits;CHECK(fake::work==0);
    std::unique_lock<std::mutex> lock(mailbox.mutex);
    if(!mailbox.cv.wait_for(lock,std::chrono::milliseconds(ms),[]{return mailbox.count!=0;}))++fake::timeouts;
    const unsigned n=mailbox.count;mailbox.count=0;return n;
}
static int xTaskCreateWithCaps(void (*fn)(void *),const char *,unsigned,void *arg,unsigned,void *,unsigned) {
    if(fake::failTask.exchange(false))return 0;fake::tasks.emplace_back(fn,arg);return pdPASS;
}
static void vTaskDeleteWithCaps(void *){}
namespace sys {
struct Power {
    static bool sleepPending(){return fake::sleep;}
    static bool audioMayRun(){return fake::mayRun;}
    struct WorkGuard {WorkGuard(){++fake::work;}~WorkGuard(){--fake::work;}};
};
struct Audio {
    static bool openStream(bool record,int rate){CHECK(!record&&(rate==22050||rate==16000));if(fake::failOpen)return false;CHECK(!fake::opened.exchange(true));++fake::opens;return true;}
    static void closeStream(){CHECK(fake::opened.exchange(false));++fake::closes;}
    static bool writeStream(void *data,int bytes){CHECK(fake::opened&&bytes>0);++fake::writes;auto p=static_cast<int16_t *>(data);for(int i=0;i<bytes/2;++i)if(p[i]){++fake::sounds;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));return !fake::failWrite;}
};
struct Buttons {static void gameRunning(uint32_t,bool running){CHECK(!running);}};
}
namespace core::chip {bool idle(){return true;}}
namespace core::recorder {bool idle(){return true;}}
namespace core::fc {bool idle(){return true;} Job::~Job() {}}
#include "wake.inc"
#include "game_audio.inc"

namespace fc_test {
static std::atomic<unsigned> saved{0},scaled{0};
}
namespace core::fc {
bool processSave(Job &job,uint64_t &) {
    if(job.saves.stage.exchange(SaveStage::Idle)==SaveStage::Pending){++fc_test::saved;return true;}
    return false;
}
}
namespace fc_test {
using core::fc::Job;
static void scaleFrame(uint16_t *,void *,int,int,uint16_t *){++scaled;}
static int64_t esp_timer_get_time(){return std::chrono::steady_clock::now().time_since_epoch().count();}
constexpr int NES_SCREEN_PITCH=256,NES_SCREEN_OVERDRAW=0;
static void paused(Job &job) {
    struct {bool audio=false,audioTried=false;} engine;
    struct {void *data=nullptr;} video;
    struct {void reset(int64_t){}} timing;
    uint16_t colors[256]{};uint64_t playedFrames=0;bool restoredFrame=false;
    core::fc::AutoSaveClock autoSave;
    job.wake.attach();
    while(!job.stop.load()&&!fake::sleep) {
        if(!job.paused.load())break;
        #include "fc_pause.inc"
    }
    job.wake.detach();job.done=true;
}
}
int main() {
    // A command before registration remains in state; a command before wait
    // stays pending; detach makes late notifications harmless.
    core::WorkerWake wake;wake.notify();wake.attach();wake.notify();
    const auto before=fake::timeouts.load();wake.wait();CHECK(fake::timeouts==before);wake.detach();wake.notify();
    for(unsigned cycle=0;cycle<100;++cycle) {
        std::atomic<bool> ready{false},go{false},done{false};
        std::thread worker([&]{wake.attach();ready=true;while(!go)wake.wait();wake.detach();done=true;});
        fake::until([&]{return ready.load();});go=true;wake.notify();
        fake::until([&]{return done.load();});wake.notify();worker.join();
    }
    using namespace core::game_audio;
    fake::failTask=true;CHECK(!start(Track::Runner)&&idle());
    for(unsigned cycle=0;cycle<20;++cycle) {
        auto audio=start(Track::Runner);CHECK(audio);fake::until([&]{return fake::waits>0;});
        const auto writes=fake::writes.load();audio->trigger(Jump);fake::until([&]{return fake::writes>writes;});
        audio->pause();fake::until([]{return !busy()&&!fake::opened;});
        // Idle has no 10/20ms loop, and no performance lock is held.
        const auto waits=fake::waits.load();std::this_thread::sleep_for(std::chrono::milliseconds(70));
        CHECK(fake::waits<=waits+1&&fake::work==0);
        audio->setMusic(true);fake::until([]{return busy();});
        audio->requestStop();fake::until([]{return idle();});fake::join();audio->requestStop();
    }
    for(unsigned cycle=0;cycle<12;++cycle) {
        auto audio=start(Track::Morse);CHECK(audio);
        audio->setMorseReady(true);fake::until([]{return fake::opened.load();});
        const auto sounds=fake::sounds.load();audio->setMorseKey(true);fake::until([&]{return fake::sounds>sounds;});
        audio->setMorseKey(false);audio->pause();fake::until([]{return !fake::opened;});
        CHECK(audio->pushMorse(core::morse_audio::encode(".-")));fake::until([]{return fake::opened.load();});
        audio->requestStop();fake::until([]{return idle();});fake::join();audio->setMorseKey(true);
    }
    {auto audio=start(Track::Runner);CHECK(audio);fake::sleep=true;fake::until([]{return idle();});fake::join();fake::sleep=false;}
    {fake::failOpen=true;auto audio=start(Track::Runner);CHECK(audio);audio->setMusic(true);fake::until([]{return idle();});CHECK(audio->failed);fake::join();fake::failOpen=false;}
    for(unsigned i=0;i<20;++i) {
        core::fc::Job job;job.paused=true;
        const auto scaled=fc_test::scaled.load();
        std::unique_lock<std::mutex> held(job.frameMutex);
        std::thread worker([&]{fc_test::paused(job);});
        job.saves.stage=core::fc::SaveStage::Pending;job.wake.notify();
        fake::until([&]{return job.saves.stage==core::fc::SaveStage::Idle;});
        CHECK(fc_test::scaled==scaled);held.unlock();
        fake::until([&]{return job.framePending.load();});CHECK(fc_test::scaled==scaled+1);
        if(i&1)job.setPaused(false);else job.requestStop();
        fake::until([&]{return job.done.load();});worker.join();job.requestStop();
    }
    CHECK(fake::work==0&&!fake::opened);
    std::cout<<checks<<" checks: notification races, silent game/Morse waits, stop/sleep, and FC paused save/preview retry passed; synthetic RTOS/audio only.\n";
}
