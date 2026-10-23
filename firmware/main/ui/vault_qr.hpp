#pragma once
#include "lvgl.h"
#include <string>
namespace ui {
// Full 240px display, four-module quiet zone, UTF-8 ECI, >=2px per module.
// Owns only a bitmap; no retained plaintext. Canvas is wiped on deletion.
bool vaultQr(lv_obj_t *parent, const std::string &text);
}
