/*
 * SysInfoOverlay implementation. Runs entirely inside the LVGL task via
 * lv_timer, so no explicit locking is required here.
 */
#include "sysinfo_overlay.hpp"
#include "theme.hpp"
#include "window_mgr.hpp"
#include "compact_ui.hpp"
#include "core/power_mgr.hpp"
#include "core/connectivity.hpp"
#include "core/system_service.hpp"
#include "core/event_bus.hpp"
#include "core/lan_hotspot.hpp"

#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include <cstdarg>
#include <cstdio>
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
lv_obj_t     *s_lblApp      = nullptr;
lv_obj_t     *s_lblFps      = nullptr;
lv_obj_t     *s_lblCpu      = nullptr;
lv_obj_t     *s_lblRadio    = nullptr;
lv_obj_t     *s_lblService  = nullptr;
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

void text(lv_obj_t *label,const char *format,...)
{
    char buffer[128];va_list args;va_start(args,format);
    std::vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
    compact::setText(label,buffer); // unchanged values do not invalidate the display
}

void tick(lv_timer_t *)
{
    const bool active = s_visible && sys::Power::isScreenOn();
    setInstrumentationActive(active);
    if(!active) return;
    // Keep internal byte-addressable RAM separate from external PSRAM. A zero
    // free value remains visible. These totals are allocatable heaps, not chip sizes.
    constexpr uint32_t internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    constexpr uint32_t external=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
    text(s_lblApp,"前台 %s",WindowManager::instance().activeName());
    text(s_lblMem,"内存余 %u/%u KiB",unsigned(heap_caps_get_free_size(internal)/1024),unsigned(heap_caps_get_total_size(internal)/1024));
    text(s_lblMin,"最低 %uK · 连续 %uK",unsigned(heap_caps_get_minimum_free_size(internal)/1024),unsigned(heap_caps_get_largest_free_block(internal)/1024));
    text(s_lblPsram,"PS余 %u/%u KiB",unsigned(heap_caps_get_free_size(external)/1024),unsigned(heap_caps_get_total_size(external)/1024));
    text(s_lblCpu,"CPU %uMHz · %u任务",unsigned(esp_clk_cpu_freq()/1000000),unsigned(uxTaskGetNumberOfTasks()));

    // LVGL refresh rate is not CPU load or game simulation speed. On static
    // pages it naturally falls; do not label a low FPS as a system failure.
    uint32_t fps = 0;
    const bool fpsValid=s_disp&&esp_lv_adapter_get_fps(s_disp,&fps)==ESP_OK;
    char temperature[40]="";
#ifdef SYSINFO_HAS_TEMP
    float tc = 0.0f;
    if (s_tempSensor && s_tempEnabled && temperature_sensor_get_celsius(s_tempSensor, &tc) == ESP_OK) {
        std::snprintf(temperature,sizeof(temperature)," · 芯片%.0fC",double(tc));
    }
#endif
    if(fpsValid)text(s_lblFps,"刷新 %uFPS%s",unsigned(fps),temperature);
    else text(s_lblFps,"刷新 --%s",temperature);

    // Cached snapshots only: no scans, driver queries, file I/O or task walks.
    const auto wifi=sys::Connectivity::snapshot();
    const auto ble=sys::SystemService::bluetooth();
    const char *w="关",*b="关";
    if(sys::Connectivity::hotspotReserved()){
        const auto ap=sys::LanHotspot::snapshot();
        w=ap.phase==sys::LanPhase::On?"热点":ap.phase==sys::LanPhase::Failed?"异常":"切换";
    }else if(wifi.transitioning)w="切换";
    else if(wifi.enabled){
        switch(wifi.state){
        case sys::WifiState::Connected:w="已连";break;
        case sys::WifiState::Searching:case sys::WifiState::Connecting:w="连接中";break;
        case sys::WifiState::Failed:w="异常";break;
        default:w=wifi.scan==sys::WifiScanState::Scanning?"搜索":"未连";break;
        }
    }
    if(ble.hasError())b="异常";
    else if(ble.peer.connected)b="已连";
    else if(ble.state==sys::BleHidState::Starting)b="启动";
    else if(ble.state==sys::BleHidState::Connecting)b="连接中";
    else if(ble.enabled)b=ble.waitingForPeer||ble.state==sys::BleHidState::Advertising?"待连接":ble.scan==sys::BleScanState::Scanning?"搜索":"未连";
    text(s_lblRadio,"WiFi %s · 蓝牙%s",w,b);
    const bool busy=sys::SystemService::busy(sys::ServiceDomain::Wifi)||sys::SystemService::busy(sys::ServiceDomain::Bluetooth)||sys::SystemService::busy(sys::ServiceDomain::System);
    text(s_lblService,"服务%s · 累计丢弃%lu",!sys::SystemService::running()?"未就绪":busy?"忙":"空闲",static_cast<unsigned long>(sys::EventBus::dropped()));
    const uint64_t upSec=esp_timer_get_time()/1000000ULL;
    text(s_lblUp,"开机 %llu天 %02u:%02u:%02u",static_cast<unsigned long long>(upSec/86400),unsigned(upSec/3600%24),unsigned(upSec/60%60),unsigned(upSec%60));
}

lv_obj_t *makeLabel(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, ui::theme::fontMicro(), 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    // Diagnostic semantic colors remain readable over both light UI and media.
    lv_obj_set_style_bg_color(lbl,lv_color_black(),0);
    lv_obj_set_style_bg_opa(lbl,LV_OPA_80,0);
    lv_obj_set_style_pad_all(lbl, 0, 0);
    lv_obj_set_style_max_width(lbl,236,0);
    lv_label_set_long_mode(lbl,LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_clickable(lbl,false);
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
    lv_obj_set_clickable(s_root,false);
    lv_obj_set_scrollable(s_root,false);
    lv_obj_set_ignore_layout(s_root,true);

    s_lblApp   = makeLabel(s_root, lv_color_hex(0xFFFFFF)); // foreground app
    s_lblCpu   = makeLabel(s_root, lv_color_hex(0xB388FF)); // actual frequency
    s_lblMem   = makeLabel(s_root, lv_color_hex(0x00E676)); // green
    s_lblMin   = makeLabel(s_root, lv_color_hex(0x69F0AE)); // light green
    s_lblPsram = makeLabel(s_root, lv_color_hex(0x00E5FF)); // cyan
    s_lblFps   = makeLabel(s_root, lv_color_hex(0xFFAB40)); // orange
    s_lblRadio = makeLabel(s_root, lv_color_hex(0x82B1FF)); // blue
    s_lblService=makeLabel(s_root,lv_color_hex(0xFFEB3B));
    s_lblUp    = makeLabel(s_root, lv_color_hex(0xFF80AB)); // pink

    for(auto label:{s_lblApp,s_lblCpu,s_lblMem,s_lblMin,s_lblPsram,s_lblFps,s_lblRadio,s_lblService,s_lblUp})
        lv_label_set_text(label,"");

    s_timer = lv_timer_create(tick, 1000, nullptr);
    lv_timer_pause(s_timer); // no hidden diagnostic polling at boot

    // Start hidden; caller decides visibility.
    lv_obj_set_hidden(s_root,true);
    s_visible = false;
}

void SysInfoOverlay::setVisible(bool on)
{
    s_visible = on;
    if (!s_root) return;
    setInstrumentationActive(on && sys::Power::isScreenOn());
    if (on) {
        lv_obj_set_hidden(s_root,false);
        lv_obj_move_foreground(s_root);
        if (s_timer) { lv_timer_resume(s_timer); lv_timer_ready(s_timer); }
    } else {
        lv_obj_set_hidden(s_root,true);
        if (s_timer) lv_timer_pause(s_timer);
    }
}

bool SysInfoOverlay::visible(void)
{
    return s_visible;
}

}// namespace ui
