#include <thread>
#include <condition_variable>
#include "shutdown_baseline.inc"

static std::thread canceller;
static std::mutex rendezvous;
static std::condition_variable rendezvousSignal;
static bool probed=false,held=false,finished=false;

// Called after the actual final wakeRequested.load captured its return value.
// The old code allowed PWR to complete here; the fixed code owns displayMutex.
static void finalCheck() {
    using namespace sys;
    wakeLoadHook=nullptr;
    canceller=std::thread([] {
        const bool acquired=displayMutex.try_lock();
        if(acquired)displayMutex.unlock();
        {
            std::lock_guard<std::mutex> guard(rendezvous);
            held=!acquired;probed=true;rendezvousSignal.notify_one();
        }
        Power::toggleScreen();
        {
            std::lock_guard<std::mutex> guard(rendezvous);
            finished=true;rendezvousSignal.notify_one();
        }
    });
    std::unique_lock<std::mutex> lock(rendezvous);
    rendezvousSignal.wait(lock,[]{return probed && (held || finished);});
}

int main() {
    using namespace sys;
    shutdownBaselineMain();
    for(int cycle=0;cycle<100;++cycle) {
        // Cancellation gets the shared lock first, just after peripheral suspend.
        reset();Power::deepSleep(false);uiStopped=true;
        suspendHook=[] {std::thread button([] {Power::toggleScreen();});button.join();};
        Power::processStandby();suspendHook=nullptr;
        CHECK(wakeRequested && !terminalPhase && notifications==0);
        Power::processStandby();
        CHECK(!sleepRequested && !terminalPhase && lit);
        // Commit gets the lock first. PWR overlaps the last load/terminal stores.
        reset();Power::deepSleep(false);uiStopped=true;
        probed=held=finished=false;wakeLoadHook=finalCheck;
        Power::processStandby();canceller.join();
        CHECK(finished && terminalPhase && sleepReady && notifications==2);
#ifdef EXPECT_OLD
        CHECK(!held && wakeRequested); // Regression proof: accepted cancellation was lost.
#else
        CHECK(held && !wakeRequested); // Cancellation sees committed state and is rejected.
#endif
        // A committed entry remains committed; no fake recovery after radio shutdown.
        const auto before=notifications;Power::processStandby();CHECK(notifications==before);
    }
#ifdef EXPECT_OLD
    std::printf("%u checks; 100 deterministic lost-cancellation reproductions in old source\n",checks);
#else
    std::printf("%u checks; 100 cancellation-first and 100 commit-first real-thread interleavings\n",checks);
#endif
}
