#include "worker_wake.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>

namespace core {
void WorkerWake::attach() {
    std::lock_guard<std::mutex> lock(mutex_);
    task_=xTaskGetCurrentTaskHandle();
}
void WorkerWake::detach() {
    std::lock_guard<std::mutex> lock(mutex_);
    task_=nullptr;
}
void WorkerWake::notify() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Clearing the handle under the same lock prevents notifying a dead task.
    if(task_)xTaskNotifyGive(static_cast<TaskHandle_t>(task_));
}
void WorkerWake::wait(uint32_t timeoutMs) {
    // A notification arriving between checking state and waiting stays pending.
    ulTaskNotifyTake(pdTRUE,std::max<TickType_t>(1,pdMS_TO_TICKS(timeoutMs)));
}
}
