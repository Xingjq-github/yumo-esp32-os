#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "bsp/power_port.h"
#include "esp_lcd_touch.h"
#include "esp_timer.h"
extern lv_indev_t *bsp_display_get_input_dev(void);
static esp_err_t touch_read_result=ESP_OK; // single GUI reader
static bool touch_retry=false,touch_held=false,touch_force=false;
static int64_t touch_next_read=0;
static esp_lcd_touch_handle_t last_touch_handle;
static esp_err_t power_touch_read(esp_lcd_touch_handle_t handle) {
    last_touch_handle=handle;
    touch_read_result=esp_lcd_touch_read_data(handle);
    touch_retry=touch_read_result!=ESP_OK;
    // Back off on bus faults; periodically read a held touch to recover a lost
    // release IRQ. No new timer/task or allocation is needed.
    touch_next_read=esp_timer_get_time()+(touch_retry?100000:20000);
    bsp_power_touch_rearm();
    return touch_read_result;
}
static esp_err_t power_touch_get(esp_lcd_touch_handle_t handle,esp_lcd_touch_point_data_t *points,
                                uint8_t *count,uint8_t maximum) {
    const esp_err_t result=esp_lcd_touch_get_data(handle,points,count,maximum);
    if(touch_read_result!=ESP_OK) { *count=0;touch_held=false;return touch_read_result; }
    touch_held=result==ESP_OK && *count>0;
    if(result==ESP_OK)bsp_power_touch_sample(*count>0);
    return result;
}
static void power_touch_clear_sem(SemaphoreHandle_t semaphore) {
    // Preserve the FreeRTOS macro before replacing it for the upstream reader.
    (void)xSemaphoreTake(semaphore,0);
}
static BaseType_t power_touch_take(SemaphoreHandle_t semaphore,TickType_t wait) {
    const BaseType_t available=xSemaphoreTake(semaphore,wait);
    // A light-sleep GPIO edge may be consumed by wake logic before the GPIO
    // ISR runs. Read one fresh packet, then retain the upstream state machine.
    if(!bsp_power_touch_enabled())return pdFALSE;
    const bool wake=bsp_power_take_touch_wakeup();
    touch_force=touch_force || wake || available==pdTRUE;
    const bool due=esp_timer_get_time()>=touch_next_read;
    if(touch_retry && !due)return pdFALSE;
    if(touch_force || (due && (touch_retry || touch_held || bsp_power_touch_pending()))) {
        touch_force=false;return pdTRUE;
    }
    return pdFALSE;
}
#undef xSemaphoreTake
#define xSemaphoreTake power_touch_take
#define esp_lcd_touch_read_data power_touch_read
#define esp_lcd_touch_get_data power_touch_get
#include YUMO_LV_TOUCH_SOURCE
#undef esp_lcd_touch_get_data
#undef esp_lcd_touch_read_data
#undef xSemaphoreTake

void bsp_power_touch_reset(void) {
    touch_retry=touch_held=touch_force=false;touch_next_read=0;touch_read_result=ESP_OK;
    lv_indev_t *input=bsp_display_get_input_dev();
    if(!input)return;
    esp_lv_adapter_touch_ctx_t *ctx=lv_indev_get_driver_data(input);
    if(!ctx)return;
    if(ctx->touch_sem)power_touch_clear_sem(ctx->touch_sem);
    ctx->last_state=LV_INDEV_STATE_RELEASED;ctx->last_point=(lv_point_t){0};
    // Discard the controller driver's buffered point without touching I2C.
    if(last_touch_handle) {
        esp_lcd_touch_point_data_t point;uint8_t count=0;
        (void)esp_lcd_touch_get_data(last_touch_handle,&point,&count,1);
    }
}
