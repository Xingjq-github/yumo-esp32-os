/*
 * Central theme: colors and font sizes shared across the UI.
 *
 * Kept as small helpers instead of a full lv_theme so the look stays simple
 * and predictable on the 240x240 round-ish panel.
 */
#pragma once

#include "lvgl.h"
#include "core/theme_model.hpp"

namespace ui {

namespace theme {

using Id=sys::themes::Id;
// Boot-only selection: all UI objects see one immutable palette until reboot.
void init(int savedId);
Id current();
const sys::themes::Palette &palette();
inline bool tiles() { return current()==Id::Tiles; }
inline bool terminal() { return current()==Id::Terminal; }
inline bool warm() { return current()==Id::Warm; }
inline bool focus() { return current()==Id::Focus; }
inline bool light() { return sys::themes::light(current()); }
inline lv_color_t bg() { return lv_color_hex(palette().background); }
inline lv_color_t surface() { return lv_color_hex(palette().surface); }
inline lv_color_t fg() { return lv_color_hex(palette().foreground); }
inline lv_color_t accent() { return lv_color_hex(palette().accent); }
inline lv_color_t onAccent() { return lv_color_hex(palette().onAccent); }
inline lv_color_t muted() { return lv_color_hex(palette().muted); }
inline lv_color_t edge() { return lv_color_hex(palette().edge); }
inline lv_color_t danger() { return lv_color_hex(palette().danger); }
inline lv_color_t success() { return lv_color_hex(palette().success); }
inline lv_color_t warning() { return lv_color_hex(palette().warning); }
inline int radius() { return palette().radius; }

// Fonts (see sdkconfig.defaults for enabled sizes).
// Chinese UI text is baked into the firmware; theme.cpp attaches the symbol
// fallback once. SD-card TTF loading is no longer part of the runtime path.
const lv_font_t *fontCjkFull(void);
// Plain document text does not need the taller UI-symbol fallback metrics.
const lv_font_t *fontReading(void);
// Small 14px menu subset; the full reading font remains unchanged.
const lv_font_t *fontMenu(void);

inline const lv_font_t *fontCjk(void)    { return fontCjkFull(); }
inline const lv_font_t *fontMicro(void)  { return fontCjkFull(); }
inline const lv_font_t *fontTiny(void)   { return fontCjkFull(); }
inline const lv_font_t *fontSmall(void)  { return fontCjkFull(); }
inline const lv_font_t *fontNormal(void) { return fontCjkFull(); }
inline const lv_font_t *fontLarge(void)  { return &lv_font_montserrat_24; }
inline const lv_font_t *fontHuge(void)   { return &lv_font_montserrat_48; }

// Screen geometry.
static constexpr int kScreenW = 240;
static constexpr int kScreenH = 240;

// ---------------------------------------------------------------------------
// Layout metrics (shared by every screen so the look stays consistent).
//
// Edge-aligned containers and 1px sibling gaps maximize the 240x240 area.
// Touch target sizes are independent of these visual spacing values.
static constexpr int kEdgePad = 0;
// Gap between sibling widgets (grid columns/rows, flex items, list entries).
// Per the project UI convention every inner element gap is exactly 1px (tight
// layout). evenSpan() below still keeps equal-width tracks pixel-exact for any
// gap value, so a 1px gap does not reintroduce the left-heavy column issue.
static constexpr int kGap = 1;
// Status bar aligns with the screen edge.
static constexpr int kStatusPad = 0;

// Usable content width once both edge margins are removed.
static constexpr int kContentW = kScreenW - 2 * kEdgePad; // 240

// Largest span <= `available` that splits into `count` equal integer tracks
// separated by kGap. LVGL distributes the remainder of an FR division to the
// leading tracks, so a non-divisible span makes the first column visibly wider
// than the last one (this is what made the BT keyboard/music pads look
// left-heavy). Sizing the container to an exact multiple removes the remainder
// entirely, so every element in a row ends up the same width.
constexpr int evenSpan(int count, int available)
{
    return ((available - (count - 1) * kGap) / count) * count + (count - 1) * kGap;
}

// Convenience: a grid that spans the full content width with `cols` equal
// columns (grid pad_all must be 0 and pad_column must be kGap).
constexpr int gridWidth(int cols) { return evenSpan(cols, kContentW); }

}// namespace theme

}// namespace ui
