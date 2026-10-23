#pragma once
#include <stdbool.h>
#include <stdint.h>

// No allocation, timers or flash lookups: also used inside the RTC wake stub.
typedef enum { WAKE_FIRST_DOWN, WAKE_FIRST_UP, WAKE_SECOND_DOWN, WAKE_SECOND_UP,
               WAKE_ACCEPT, WAKE_REJECT } wake_click_phase_t;
typedef struct { wake_click_phase_t phase; uint16_t age, stable; } wake_click_t;
static inline __attribute__((always_inline))
#ifdef __cplusplus
constexpr
#endif
void wake_click_sample(wake_click_t *s, bool down) {
    if(s->phase>=WAKE_ACCEPT)return;
    s->age += 2; // caller samples every 2 ms
    const bool expected = s->phase == WAKE_FIRST_DOWN || s->phase == WAKE_SECOND_DOWN;
    s->stable = down == expected ? s->stable + 2 : 0;
    if(s->phase == WAKE_FIRST_DOWN && !down) {s->phase=WAKE_REJECT;return;}
    const unsigned limit = s->phase == WAKE_SECOND_DOWN ? 400 : 350;
    if(s->age > limit) {s->phase=WAKE_REJECT;return;}
    if(s->stable >= 20) {
        s->phase = (wake_click_phase_t)(s->phase + 1);
        s->age=0;s->stable=0;
    }
}
