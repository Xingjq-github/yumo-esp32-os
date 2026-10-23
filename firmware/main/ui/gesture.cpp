/*
 * Global gesture layer implementation.
 */
#include "gesture.hpp"
#include "../core/event_bus.hpp"

namespace ui {

static void gestureCb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);

    switch (dir) {
    case LV_DIR_RIGHT:
    case LV_DIR_LEFT:
        // Preserve horizontal navigation while vertical gestures remain local
        // to the home screen overlays.
        sys::EventBus::publish({sys::EventType::BackRequested});
        break;
    default:
        break;
    }
    (void)e;
}

void Gesture::hook(lv_obj_t *obj)
{
    lv_obj_add_event_cb(obj, gestureCb, LV_EVENT_GESTURE, nullptr);
}

}// namespace ui