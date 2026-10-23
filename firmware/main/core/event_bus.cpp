#include "event_bus.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <array>
#include <atomic>
#include <utility>

namespace sys {
namespace {
constexpr size_t kQueueSize = 24;
constexpr size_t kSubscriptions = 32;
StaticQueue_t queueControl;
uint8_t queueStorage[kQueueSize * sizeof(Event)];
QueueHandle_t queue = nullptr;
struct Subscription { uint32_t id = 0; EventType type = EventType::None; EventHandler handler; };
std::array<Subscription, kSubscriptions> subscriptions;
uint32_t nextId = 0;
std::atomic<uint32_t> lost{0};
std::atomic<bool> homeRequested{false};
bool dispatching = false;
}
bool EventBus::init()
{
    if (!queue) queue = xQueueCreateStatic(kQueueSize, sizeof(Event), queueStorage, &queueControl);
    return queue != nullptr;
}
uint32_t EventBus::subscribe(EventType type, EventHandler handler)
{
    for (auto &slot : subscriptions) {
        if (!slot.id) {
            if (++nextId == 0) ++nextId;
            slot.id = nextId; slot.type = type; slot.handler = std::move(handler);
            return slot.id;
        }
    }
    return 0;
}
void EventBus::unsubscribe(uint32_t id)
{
    for (auto &slot : subscriptions) if (slot.id == id) { slot = {}; return; }
}
void EventBus::publish(const Event &event)
{
    // Home is a coalesced escape path and cannot be lost to a full queue.
    if (event.type == EventType::HomeRequested) { homeRequested.store(true); return; }
    if (!queue || xQueueSend(queue, &event, 0) != pdTRUE) ++lost;
}
void EventBus::dispatch(unsigned budget,bool (*allow)(const Event &))
{
    if (!queue || dispatching) return;
    dispatching = true;
    Event event;
    const bool home = homeRequested.exchange(false);
    for (unsigned n = 0; n < budget; ++n) {
        if (n == 0 && home) event = {EventType::HomeRequested};
        else if (xQueueReceive(queue, &event, 0) != pdTRUE) break;
        if(allow && !allow(event))continue;
        // Snapshot IDs, not callbacks holding potentially destroyed app pointers.
        std::array<uint32_t, kSubscriptions> ids{};
        size_t count = 0;
        for (const auto &slot : subscriptions)
            if (slot.id && slot.type == event.type) ids[count++] = slot.id;
        for (size_t i = 0; i < count; ++i) {
            for (auto &slot : subscriptions) {
                if (slot.id == ids[i] && slot.handler) {
                    auto handler = slot.handler; // keep this invocation alive if it unsubscribes
                    handler(event);
                    break;
                }
            }
        }
    }
    dispatching = false;
}
uint32_t EventBus::dropped() { return lost.load(); }
} // namespace sys
