// Adapt the pinned LVGL FreeRTOS port without changing managed sources. The
// two 40 KiB software-render stacks must not consume the radio's internal RAM.
// Keep the upstream thread entry, synchronization, priority and stack sizes.
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"

#if !CONFIG_SPIRAM_XIP_FROM_PSRAM || !CONFIG_SPIRAM_FETCH_INSTRUCTIONS || \
    !CONFIG_SPIRAM_RODATA || !CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM
#error "LVGL PSRAM stacks require XiP and external task stack support"
#endif

_Static_assert(sizeof(StackType_t) == 1, "ESP-IDF task stacks are byte-counted");

static BaseType_t yumo_lv_thread_create(TaskFunction_t entry, const char *name,
                                      configSTACK_DEPTH_TYPE bytes, void *argument,
                                      UBaseType_t priority, TaskHandle_t *task)
{
    // WithCaps keeps the TCB internal. Do not silently fall back to the scarce
    // internal heap; propagate failure through LVGL's existing error result.
    return xTaskCreateWithCaps(entry, name, bytes, argument, priority, task,
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// WithCaps tasks require the matching delete API for both the stack and TCB.
// Scope these replacements to this one upstream translation unit only.
#define xTaskCreate yumo_lv_thread_create
#define vTaskDelete vTaskDeleteWithCaps
#include YUMO_LV_FREERTOS_SOURCE
#undef vTaskDelete
#undef xTaskCreate
