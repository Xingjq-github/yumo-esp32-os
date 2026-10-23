#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Clockwise quarter turns from the board's normal orientation. The ST7789
 * has 240x320 GRAM; the visible 240x240 window starts at row zero at boot.
 * Mirrored rows need the other 80-row margin, also exchanged by MV.
 * Reference: Adafruit_ST7789::init/setRotation (240x240/1.54-inch panels).
 */
typedef struct {
    bool swap_xy, mirror_x, mirror_y;
    int x_gap, y_gap;
} bsp_orientation_panel_t;

static inline bsp_orientation_panel_t bsp_orientation_panel(unsigned turns)
{
    const bsp_orientation_panel_t modes[4] = {
        {false, false, false, 0, 0},
        {true,  true,  false, 0, 0},
        {false, true,  true,  0, 80},
        {true,  false, true, 80, 0},
    };
    return modes[turns & 3];
}

/* Raw physical touch -> logical UI. No LVGL rotation is applied afterwards. */
static inline void bsp_orientation_point(unsigned turns, uint16_t *x, uint16_t *y)
{
    const uint16_t px = *x, py = *y;
    switch (turns & 3) {
    case 1: *x = py; *y = 239 - px; break;
    case 2: *x = 239 - px; *y = 239 - py; break;
    case 3: *x = 239 - py; *y = px; break;
    default: break;
    }
}

/* Apply the same inverse rotation to a physical acceleration vector. */
static inline void bsp_orientation_vector(unsigned turns, float *x, float *y)
{
    const float px = *x, py = *y;
    switch (turns & 3) {
    case 1: *x = py; *y = -px; break;
    case 2: *x = -px; *y = -py; break;
    case 3: *x = -py; *y = px; break;
    default: break;
    }
}

/* GUI/LVGL lock held. Commands drain the SPI DMA queue before changing MADCTL.
 * This rotates panel addressing only; LVGL and captures stay logical 240x240.
 */
esp_err_t bsp_display_set_orientation(unsigned turns);
unsigned bsp_display_get_orientation(void);

#ifdef __cplusplus
}
#endif
