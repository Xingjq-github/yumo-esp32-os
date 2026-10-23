// Small compile-time adapter over the pinned upstream source. Keep upstream
// management/pause/cleanup intact; only its clock and idle wait are replaced.
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "bsp/power_port.h"
extern lv_indev_t *bsp_display_get_input_dev(void);
static esp_err_t power_tick_start(esp_timer_handle_t timer,uint64_t period);
static void power_delay(TickType_t ticks);
static uint32_t power_handler(void);
#define esp_timer_start_periodic power_tick_start
#define vTaskDelay power_delay
#define lv_timer_handler power_handler
#include YUMO_LV_ADAPTER_SOURCE
#undef esp_timer_start_periodic
#undef vTaskDelay
#undef lv_timer_handler

static atomic_bool quiet,dark,requested,force_touch;
static atomic_bool touch_active=true;
static bool touch_pressed; // GUI task only
static lv_timer_t *pump_timer;
static void (*activity_cb)(void);
esp_err_t bsp_power_pause(int32_t timeout_ms) {
    if(!s_ctx.inited)return ESP_ERR_INVALID_STATE;
    if(s_ctx.paused && s_ctx.pause_ack)return ESP_OK;
    // Clear the old acknowledgement BEFORE publishing a new request.
    if(s_ctx.pause_done_sem)xSemaphoreTake(s_ctx.pause_done_sem,0);
    s_ctx.pause_ack=false;
    s_ctx.paused=true;
    if(!s_ctx.task || xTaskGetCurrentTaskHandle()==s_ctx.task || !s_ctx.pause_done_sem) {
        s_ctx.pause_ack=true;
        return ESP_OK;
    }
    xTaskNotifyGive(s_ctx.task); // break the one-second dark-screen idle wait
    if(xSemaphoreTake(s_ctx.pause_done_sem,timeout_ms<0?portMAX_DELAY:pdMS_TO_TICKS(timeout_ms))==pdTRUE)
        return ESP_OK;
    s_ctx.paused=false;
    // A late worker acknowledgement may have entered its notification wait.
    // Publish rollback first, then wake it even when the BSP never suspended.
    xTaskNotifyGive(s_ctx.task);
    return ESP_ERR_TIMEOUT;
}
static uint32_t monotonic_ms(void) {return (uint32_t)(esp_timer_get_time()/1000);}
static esp_err_t power_tick_start(esp_timer_handle_t timer,uint64_t period) {
    (void)timer;(void)period;
    lv_tick_set_cb(monotonic_ms); // no periodic tick wakes; includes sleep time
    return ESP_OK;
}
static void power_delay(TickType_t ticks) {
    // Upstream also delays callers during resume/deinit. Do not consume their
    // notifications or stretch those waits with the GUI's idle policy.
    if(xTaskGetCurrentTaskHandle()!=s_ctx.task) {vTaskDelay(ticks);return;}
    if(!atomic_load(&quiet) && ticks>pdMS_TO_TICKS(8))ticks=pdMS_TO_TICKS(8);
    if(atomic_load(&quiet) && atomic_load(&dark))ticks=pdMS_TO_TICKS(1000);
    ulTaskNotifyTake(pdTRUE,ticks?ticks:1);
}
void bsp_power_set_pump(lv_timer_t *timer,void (*cb)(void)) {pump_timer=timer;activity_cb=cb;}
void bsp_power_wake(void) {
    atomic_store(&requested,true);
    if(s_ctx.task)xTaskNotifyGive(s_ctx.task);
}
void bsp_power_touch_irq(void) {
    if(!atomic_load(&touch_active))return;
    atomic_store(&requested,true); // an IRQ requests a read; it is not user input
    if(s_ctx.task) {
        BaseType_t woken=pdFALSE;vTaskNotifyGiveFromISR(s_ctx.task,&woken);
        if(woken)portYIELD_FROM_ISR();
    }
}
void bsp_power_touch_wakeup(void) {
    if(!atomic_load(&touch_active))return;
    atomic_store(&force_touch,true);bsp_power_wake();
}
bool bsp_power_take_touch_wakeup(void) {return atomic_exchange(&force_touch,false);}
bool bsp_power_touch_enabled(void) {return atomic_load(&touch_active);}
void bsp_power_touch_active(bool active) {
    atomic_store(&touch_active,active);touch_pressed=false;
    atomic_store(&force_touch,active); // discard stale packet state on screen-on
    bsp_power_touch_gate(active);
    if(active)bsp_power_wake();
}
void bsp_power_touch_sample(bool pressed) {
    if(!atomic_load(&touch_active))return;
    const bool activity=pressed || touch_pressed;
    touch_pressed=pressed;
    if(activity && activity_cb)activity_cb(); // press/move/release, never empty IRQs
}
void bsp_power_set_quiet(bool enabled,bool lit) {
    atomic_store(&dark,!lit);
    if(atomic_exchange(&quiet,enabled)==enabled)return;
    lv_display_t *display=lv_display_get_default();
    lv_indev_t *input=bsp_display_get_input_dev();
    if(display)lv_timer_set_period(lv_display_get_refr_timer(display),enabled?1000:LV_DEF_REFR_PERIOD);
    if(input)lv_timer_set_period(lv_indev_get_read_timer(input),enabled?1000:LV_DEF_REFR_PERIOD);
}
static uint32_t power_handler(void) {
    if(atomic_exchange(&requested,false)) {
        if(pump_timer)lv_timer_ready(pump_timer);
        lv_indev_t *input=bsp_display_get_input_dev();
        if(input) {
            // Validate the packet BEFORE the due idle-policy timer. Only prime
            // the driver's cached state here; LVGL dispatches UI events later.
            // A real touch on the blackout deadline must win over that timeout.
            lv_indev_read_cb_t read=lv_indev_get_read_cb(input);
            if(atomic_load(&touch_active) && read) {
                lv_indev_data_t data={0};read(input,&data);
            }
            lv_timer_ready(lv_indev_get_read_timer(input));
        }
    }
    const uint32_t next=lv_timer_handler();
    // A due clock/SOS timer paints now, without waiting for the 1-second idle
    // refresh period. LVGL does no transfer when there is no dirty rectangle.
    if(atomic_load(&quiet))lv_refr_now(NULL);
    return next;
}
