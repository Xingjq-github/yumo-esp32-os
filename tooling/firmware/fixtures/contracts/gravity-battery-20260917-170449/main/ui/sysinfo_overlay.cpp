/*
 * SysInfoOverlay implementation. Runs entirely inside the LVGL task via
 * lv_timer, so no explicit locking is required here.
 */
#include "sysinfo_overlay.hpp"
#include "theme.hpp"
#include "core/power_mgr.hpp"

#include "sdkconfig.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if __has_include("driver/temperature_sensor.h")
#include "driver/temperature_sensor.h"
#define SYSINFO_HAS_TEMP 1
#endif

namespace ui {

namespace {

lv_display_t *s_disp        = nullptr;
lv_obj_t     *s_root        = nullptr;
lv_obj_t     *s_lblMem      = nullptr;
lv_obj_t     *s_lblMin      = nullptr;
lv_obj_t     *s_lblPsram    = nullptr;
lv_obj_t     *s_lblFlash    = nullptr;
lv_obj_t     *s_lblFps      = nullptr;
lv_obj_t     *s_lblCpu      = nullptr;
lv_obj_t     *s_lblTask     = nullptr;
lv_obj_t     *s_lblTemp     = nullptr;
lv_obj_t     *s_lblUp       = nullptr;
lv_timer_t   *s_timer       = nullptr;
bool          s_visible     = false;
bool          s_instrumentationActive = false;

#ifdef SYSINFO_HAS_TEMP
temperature_sensor_handle_t s_tempSensor = nullptr;
bool s_tempEnabled = false;
#endif

// Optional diagnostics run only while visible and the display is on.
// Retain an installed (disabled) handle across toggles, without background sensing.
void setInstrumentationActive(bool active)
{
    if(s_instrumentationActive == active) return;
    s_instrumentationActive = active;
    if(s_disp) esp_lv_adapter_fps_stats_enable(s_disp, active);
#ifdef SYSINFO_HAS_TEMP
    if(active) {
        if(!s_tempSensor) {
            temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
            if(temperature_sensor_install(&config, &s_tempSensor) != ESP_OK) s_tempSensor = nullptr;
        }
        if(s_tempSensor && !s_tempEnabled)
            s_tempEnabled = temperature_sensor_enable(s_tempSensor) == ESP_OK;
    } else if(s_tempSensor && s_tempEnabled) {
        if(temperature_sensor_disable(s_tempSensor) == ESP_OK) s_tempEnabled = false;
    }
#endif
}

void tick(lv_timer_t *)
{
    const bool active = s_visible && sys::Power::isScreenOn();
    setInstrumentationActive(active);
    if(!active) return;
    // Internal SRAM heap free.
    size_t heapFree = esp_get_free_heap_size();
    lv_label_set_text_fmt(s_lblMem, "MEM %uK", (unsigned)(heapFree / 1024));

    // PSRAM heap free (may be zero if not available).
    size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psramFree > 0) {
        lv_label_set_text_fmt(s_lblPsram, "PSRAM %uK", (unsigned)(psramFree / 1024));
        lv_obj_clear_flag(s_lblPsram, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lblPsram, LV_OBJ_FLAG_HIDDEN);
    }

    // Flash chip total size (running partition size shown as used app size).
    static uint32_t flashKB    = 0;
    static uint32_t appKB      = 0;
    if (flashKB == 0) {
        uint32_t sz = 0;
        if (esp_flash_get_size(nullptr, &sz) == ESP_OK) flashKB = sz / 1024;
        const esp_partition_t *run = esp_ota_get_running_partition();
        if (run) appKB = run->size / 1024;
    }
    lv_label_set_text_fmt(s_lblFlash, "FLASH %u/%uK", (unsigned)appKB, (unsigned)flashKB);

    // Display refresh rate from the LVGL adapter.
    uint32_t fps = 0;
    if (s_disp) esp_lv_adapter_get_fps(s_disp, &fps);
    lv_label_set_text_fmt(s_lblFps, "FPS %u", (unsigned)fps);

    // Historical minimum free heap (low-water mark).
    size_t minFree = esp_get_minimum_free_heap_size();
    lv_label_set_text_fmt(s_lblMin, "MIN %uK", (unsigned)(minFree / 1024));

    // CPU core count and frequency (cached once).
    static char cpuStr[24] = {0};
    if (cpuStr[0] == 0) {
        esp_chip_info_t ci;
        esp_chip_info(&ci);
        snprintf(cpuStr, sizeof(cpuStr), "CPU %dx@%dM", ci.cores,
                 (int)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    }
    lv_label_set_text(s_lblCpu, cpuStr);

    // FreeRTOS task count.
    lv_label_set_text_fmt(s_lblTask, "TASK %u",
                          (unsigned)uxTaskGetNumberOfTasks());

    // Chip temperature (if the sensor is available).
#ifdef SYSINFO_HAS_TEMP
    float tc = 0.0f;
    if (s_tempSensor && s_tempEnabled && temperature_sensor_get_celsius(s_tempSensor, &tc) == ESP_OK) {
        lv_label_set_text_fmt(s_lblTemp, "TEMP %d.%01dC", (int)tc,
                              (int)((tc - (int)tc) * 10));
        lv_obj_clear_flag(s_lblTemp, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_lblTemp, LV_OBJ_FLAG_HIDDEN);
    }
#endif

    // Uptime in minutes and seconds.
    uint64_t upSec = (uint64_t)(esp_timer_get_time() / 1000000ULL);
    lv_label_set_text_fmt(s_lblUp, "UP %um%02us",
                          (unsigned)(upSec / 60), (unsigned)(upSec % 60));
}

lv_obj_t *makeLabel(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, ui::theme::fontMicro(), 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_pad_all(lbl, 0, 0);
    return lbl;
}

}// namespace

void SysInfoOverlay::init(lv_display_t *disp)
{
    if (s_root) {
        s_disp = disp;
        return;
    }
    s_disp = disp;

    lv_obj_t *top = lv_layer_top();
    s_root = lv_obj_create(top);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_root, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_pad_row(s_root, 0, 0);
    lv_obj_set_flex_flow(s_root, LV_FLEX_FLOW_COLUMN);
    // Do not swallow touches so apps below stay interactive.
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_IGNORE_LAYOUT);

    s_lblMem   = makeLabel(s_root, lv_color_hex(0x00E676)); // green
    s_lblMin   = makeLabel(s_root, lv_color_hex(0x69F0AE)); // light green
    s_lblPsram = makeLabel(s_root, lv_color_hex(0x00E5FF)); // cyan
    s_lblFlash = makeLabel(s_root, lv_color_hex(0x82B1FF)); // blue
    s_lblFps   = makeLabel(s_root, lv_color_hex(0xFFAB40)); // orange
    s_lblCpu   = makeLabel(s_root, lv_color_hex(0xB388FF)); // purple
    s_lblTask  = makeLabel(s_root, lv_color_hex(0xFFEB3B)); // yellow
    s_lblTemp  = makeLabel(s_root, lv_color_hex(0xFF5252)); // red
    s_lblUp    = makeLabel(s_root, lv_color_hex(0xFF80AB)); // pink

    lv_label_set_text(s_lblMem,   "MEM --");
    lv_label_set_text(s_lblMin,   "MIN --");
    lv_label_set_text(s_lblPsram, "PSRAM --");
    lv_label_set_text(s_lblFlash, "FLASH --");
    lv_label_set_text(s_lblFps,   "FPS --");
    lv_label_set_text(s_lblCpu,   "CPU --");
    lv_label_set_text(s_lblTask,  "TASK --");
    lv_label_set_text(s_lblTemp,  "TEMP --");
    lv_label_set_text(s_lblUp,    "UP --");

    s_timer = lv_timer_create(tick, 1000, nullptr);
    lv_timer_pause(s_timer); // no hidden diagnostic polling at boot

    // Start hidden; caller decides visibility.
    lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
    s_visible = false;
}

void SysInfoOverlay::setVisible(bool on)
{
    s_visible = on;
    if (!s_root) return;
    setInstrumentationActive(on && sys::Power::isScreenOn());
    if (on) {
        lv_obj_clear_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_root);
        if (s_timer) { lv_timer_resume(s_timer); lv_timer_ready(s_timer); }
    } else {
        lv_obj_add_flag(s_root, LV_OBJ_FLAG_HIDDEN);
        if (s_timer) lv_timer_pause(s_timer);
    }
}

bool SysInfoOverlay::visible(void)
{
    return s_visible;
}

}// namespace ui