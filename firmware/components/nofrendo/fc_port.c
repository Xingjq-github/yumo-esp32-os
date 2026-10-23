#include "fc_port.h"
#include <stdlib.h>
#include "palettes.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
void *fc_alloc(size_t size) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}
void *fc_calloc(size_t count,size_t size) {
#ifdef ESP_PLATFORM
    return heap_caps_calloc(count,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return calloc(count,size);
#endif
}
void *fc_calloc_hot(size_t count,size_t size) {
    if(size && count>SIZE_MAX/size) return NULL;
    const size_t bytes=count*size;
#ifdef ESP_PLATFORM
    // Best effort, not an internal heap reservation: other tasks can allocate.
    // Avoid large/DMA allocations and fall back safely if internal RAM is busy.
    const unsigned caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    if(bytes && bytes<=4096 && heap_caps_get_free_size(caps)>=96*1024+bytes) {
        void *p=heap_caps_calloc(count,size,caps);
        if(p) return p;
    }
#else
    (void)bytes;
#endif
    return fc_calloc(count,size);
}
uint16_t fc_color(unsigned index) {
    const uint8_t *rgb=index<192?nes_palettes[0]+(index&63)*3:gui_pal+(index&7)*3;
    return ((rgb[0]>>3)<<11)|((rgb[1]>>2)<<5)|(rgb[2]>>3);
}
