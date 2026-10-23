#pragma once
#include <cstdint>
#include <mutex>

namespace core {
// One worker, task-context producers. State/queues are the source of truth;
// notifications only request a recheck. Never wait while holding this mutex.
class WorkerWake {
public:
    void attach(); // Worker only, before its first state check.
    void detach(); // Worker only, before publishing done/deleting the task.
    void notify(); // Publish command state before calling.
    void wait(uint32_t timeoutMs=250); // Bounded fallback for global power state.
private:
    std::mutex mutex_;
    void *task_=nullptr;
};
}
