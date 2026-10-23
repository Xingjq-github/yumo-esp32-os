#include "screen_orientation.hpp"
#include "orientation_model.hpp"
#include "settings_store.hpp"
#include "power_mgr.hpp"
#include "screenshot.hpp"
#include "bsp/orientation.h"
#include "bsp/power_port.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace sys {
namespace {
lv_display_t *display=nullptr;
lv_indev_t *touch=nullptr;
orientation::Filter filter;
}
void ScreenOrientation::init(lv_display_t *disp,lv_indev_t *input) {
    display=disp;touch=input;filter=orientation::Filter{};
    Qmi8658::setInUse(false);
    ESP_LOGI("orientation","Ready: panel=0, logical=240x240, four-way hardware rotation");
}
void ScreenOrientation::poll(bool appLocked) {
    const bool enabled=Settings::motionSensorOn();
    const bool awake=Power::isScreenOn() && !Power::sleepPending() && !Power::slowIdle();
    const bool wanted=display && awake && !appLocked;
    Qmi8658::setInUse(wanted); // Sensor I/O remains in the system worker.
    if(!wanted || !enabled) {filter.reset();return;}
    const bool touching=touch && (lv_indev_get_state(touch)==LV_INDEV_STATE_PRESSED ||
        lv_indev_get_scroll_obj(touch)!=nullptr || bsp_power_touch_pending());
    // Do not change coordinates during a press, scroll or screenshot capture.
    if(touching || !Screenshot::idle()) {filter.reset();return;}
    AccelerationSample sample;
    if(!Qmi8658::cachedAcceleration(sample)) {filter.reset();return;}
    const uint32_t now=static_cast<uint32_t>(esp_timer_get_time()/1000);
    const int target=filter.update(sample.value.x,sample.value.y,sample.value.z,sample.timeMs,now,true);
    if(target<0)return;
    const auto error=bsp_display_set_orientation(unsigned(target));
    if(error!=ESP_OK) {
        filter.failed(now);
        ESP_LOGW("orientation","Panel change failed: %s",esp_err_to_name(error));
        return;
    }
    filter.committed(unsigned(target),now);
    if(touch)lv_indev_reset(touch,nullptr);
    bsp_power_touch_reset();
    lv_obj_invalidate(lv_display_get_screen_active(display));
    lv_obj_invalidate(lv_display_get_layer_top(display));
    lv_obj_invalidate(lv_display_get_layer_sys(display));
    ESP_LOGI("orientation","Panel=%d degrees, logical=240x240",target*90);
}
void ScreenOrientation::mapAcceleration(Acceleration &value) {
    bsp_orientation_vector(bsp_display_get_orientation(),&value.x,&value.y);
}
}
