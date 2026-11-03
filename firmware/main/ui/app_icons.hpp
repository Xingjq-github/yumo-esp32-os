#pragma once
#include "lvgl.h"

LV_FONT_DECLARE(lv_font_app_icons_24);
LV_FONT_DECLARE(lv_font_app_icons_48);

namespace ui::icons {
inline constexpr char Xiaozhi[]="\xEE\x80\x88";
inline constexpr char Music[]="\xEE\x80\x87";
inline constexpr char Timer[]="\xEE\x80\x86";
inline constexpr char Garden[]="\xEE\x80\x85";
inline constexpr char Patterns[]="\xEE\x80\x84";
// Original monochrome artwork. Private-use glyphs never depend on SD fonts.
inline constexpr char Fc[] = "\xEE\x80\x80";
inline constexpr char Programming[] = "\xEE\x80\x81";
inline constexpr char Dino[] = "\xEE\x80\x82";
inline constexpr char Maze[] = "\xEE\x80\x83";
inline const lv_font_t *font24() { return &lv_font_app_icons_24; }
inline const lv_font_t *font48() { return &lv_font_app_icons_48; }
}
