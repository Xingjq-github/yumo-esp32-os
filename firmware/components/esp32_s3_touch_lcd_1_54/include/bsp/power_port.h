#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
void bsp_power_set_quiet(bool quiet,bool lit); // LVGL lock held
void bsp_power_set_pump(lv_timer_t *pump,void (*activity)(void));
void bsp_power_wake(void);
void bsp_power_touch_irq(void); // ISR only
void bsp_power_touch_wakeup(void); // PM exit callback, no blocking work
bool bsp_power_take_touch_wakeup(void);
void bsp_power_touch_active(bool active); // GUI task, paired with LVGL input enable
void bsp_power_touch_sample(bool pressed); // GUI task, only after a valid hardware read
bool bsp_power_touch_enabled(void);
void bsp_power_touch_gate(bool active); // GUI task: mask IRQ and reset driver cache
bool bsp_power_touch_pending(void); // GUI: masked level IRQ still needs draining
void bsp_power_touch_rearm(void); // GUI: after a bounded hardware read
void bsp_power_touch_reset(void); // GUI: reset adapter caches, no bus I/O
esp_err_t bsp_power_pause(int32_t timeout_ms); // service worker, wakes idle LVGL
#ifdef __cplusplus
}
#endif
