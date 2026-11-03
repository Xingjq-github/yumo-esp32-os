#pragma once
#include "lvgl.h"
#include "core/vault_pattern.hpp"

namespace ui {
// On-demand LVGL view. Touch events allocate nothing and never recreate widgets.
class VaultPattern {
public:
  using Submit = void (*)(void *, const char *, size_t);
  void create(lv_obj_t *parent, const char *prompt, bool enabled, Submit submit, void *context);
  void detach(); // call before the parent is cleaned/deleted
  void poll();
  void hint(const char *text);
  bool drawing() const { return stroke.down; }
private:
  static void event(lv_event_t *e);
  void paint(lv_layer_t *layer);
  void invalidateMotion(size_t previousCount, int previousX, int previousY);
  lv_obj_t *board = nullptr, *toast = nullptr, *label = nullptr;
  vault::pattern::Stroke stroke;
  Submit submit = nullptr;
  void *context = nullptr;
  bool enabled = false, error = false;
  uint32_t hintAt = 0, releasedAt = 0;
};
} // namespace ui
