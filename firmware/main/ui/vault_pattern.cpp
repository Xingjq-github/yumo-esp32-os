#include "vault_pattern.hpp"
#include "compact_ui.hpp"
#include "theme.hpp"
#include "core/power_mgr.hpp"
#include "core/vault_format.hpp"
#include <algorithm>

namespace ui {
namespace p = vault::pattern;
namespace {
constexpr int DotDiameter = 15;
constexpr int RingDiameter = 4 * DotDiameter;
constexpr int RingBefore = RingDiameter / 2;
constexpr int RingAfter = RingDiameter - RingBefore - 1;
constexpr int LineWidth = 3;
constexpr int LinePadding = LineWidth + 1; // round caps and antialiasing
static_assert(p::HitRadius <= RingBefore && RingDiameter < p::Pitch);
}
void VaultPattern::detach() {
  vault::wipe(&stroke, sizeof(stroke));
  board = toast = label = nullptr; submit = nullptr; context = nullptr;
  enabled = error = false;
}
void VaultPattern::create(lv_obj_t *parent, const char *prompt, bool active, Submit callback, void *user) {
  detach(); enabled = active; submit = callback; context = user;
  board = lv_obj_create(parent);
  lv_obj_remove_style_all(board);
  // Halos and the moving stroke blend only with this page's own backdrop.
  lv_obj_set_style_bg_color(board, theme::bg(), 0);
  lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
  lv_obj_set_size(board, p::Side, p::Side);
  lv_obj_set_pos(board, 0, 0);
  lv_obj_set_scrollable(board, false);
  lv_obj_set_clickable(board, true);
  lv_obj_set_press_lock(board, true);
  lv_obj_set_gesture_bubble(board, false);
  lv_obj_set_event_bubble(board, false);
  lv_obj_add_event_cb(board, event, LV_EVENT_ALL, this);
  toast = lv_obj_create(board);
  lv_obj_remove_style_all(toast);
  lv_obj_set_size(toast, 208, 52);
  lv_obj_center(toast);
  lv_obj_set_style_radius(toast, 14, 0);
  lv_obj_set_style_bg_color(toast, lv_color_hex(0x142423), 0);
  lv_obj_set_style_bg_opa(toast, LV_OPA_80, 0);
  lv_obj_set_clickable(toast, false); lv_obj_set_scrollable(toast, false);
  label = compact::label(toast, "", 196);
  lv_obj_set_clickable(label, false);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_center(label);
  hint(prompt);
}
void VaultPattern::hint(const char *text) {
  if (!toast) return;
  compact::setText(label, text ? text : ""); lv_obj_center(label);
  hintAt = lv_tick_get();
  lv_obj_set_hidden(toast, !text || !*text);
}
void VaultPattern::poll() {
  if (!board) return;
  if (lv_tick_elaps(hintAt) >= 1800) lv_obj_set_hidden(toast, true);
  if (!stroke.down && stroke.count && lv_tick_elaps(releasedAt) >= 450) {
    stroke.clear(); error = false; lv_obj_invalidate(board);
  }
}
void VaultPattern::invalidateMotion(size_t previousCount, int previousX, int previousY) {
  lv_area_t origin; lv_obj_get_coords(board, &origin);
  auto invalidate = [&](int x1, int y1, int x2, int y2) {
    lv_area_t dirty = {origin.x1 + std::max(0, x1), origin.y1 + std::max(0, y1),
                       origin.x1 + std::min(p::Side - 1, x2), origin.y1 + std::min(p::Side - 1, y2)};
    lv_obj_invalidate_area(board, &dirty);
  };
  auto line = [&](int ax, int ay, int bx, int by) {
    invalidate(std::min(ax, bx) - LinePadding, std::min(ay, by) - LinePadding,
               std::max(ax, bx) + LinePadding, std::max(ay, by) + LinePadding);
  };
  // A move only appends cells: the fixed prefix stays on screen. Erase the old
  // finger tail and draw the new one without invalidating the whole 240px board.
  if (previousCount) {
    const unsigned last = stroke.cells[previousCount - 1];
    line(p::x(last), p::y(last), previousX, previousY);
  }
  if (stroke.count) {
    const unsigned last = stroke.cells[stroke.count - 1];
    line(p::x(last), p::y(last), stroke.px, stroke.py);
  }
  for (size_t i = previousCount; i < stroke.count; ++i) {
    const unsigned cell = stroke.cells[i];
    const int x = p::x(cell), y = p::y(cell);
    invalidate(x - RingBefore, y - RingBefore, x + RingAfter, y + RingAfter);
    // Include every segment added by a fast swipe or automatic midpoint.
    if (i) line(p::x(stroke.cells[i - 1]), p::y(stroke.cells[i - 1]), x, y);
  }
}
void VaultPattern::event(lv_event_t *e) {
  auto self = static_cast<VaultPattern *>(lv_event_get_user_data(e));
  const auto code = lv_event_get_code(e);
  if (code == LV_EVENT_DRAW_MAIN) { self->paint(lv_event_get_layer(e)); return; }
  if (code == LV_EVENT_GESTURE) { lv_event_stop_bubbling(e); return; }
  if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
    self->stroke.clear(); self->error = false; lv_obj_invalidate(self->board); return;
  }
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED) return;
  lv_event_stop_bubbling(e);
  auto input = lv_indev_active();
  if (!self->enabled || !input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER ||
      !sys::Power::isScreenOn() || sys::Power::sleepPending()) return;
  lv_point_t point; lv_indev_get_point(input, &point);
  lv_area_t area; lv_obj_get_coords(self->board, &area);
  const int x=point.x-area.x1, y=point.y-area.y1;
  if (code == LV_EVENT_PRESSED) {
    self->stroke.begin(x, y); self->error=false; lv_obj_set_hidden(self->toast, true);
  } else if (!self->stroke.down) return;
  else if (code == LV_EVENT_PRESSING) {
    if (self->stroke.px == x && self->stroke.py == y) return;
    const size_t previousCount = self->stroke.count;
    const int previousX = self->stroke.px, previousY = self->stroke.py;
    self->stroke.move(x, y);
    self->invalidateMotion(previousCount, previousX, previousY);
    return;
  } else {
    self->stroke.end(x, y); self->releasedAt=lv_tick_get();
    if (!self->stroke.acceptable()) {
      self->error = true; self->hint("至少连接 4 个点");
    } else if (self->submit) {
      char encoded[p::Maximum]{};
      for (size_t i=0;i<self->stroke.count;++i) encoded[i]=char('1'+self->stroke.cells[i]);
      self->enabled=false; // exactly one submission until the app presents its next state
      self->submit(self->context, encoded, self->stroke.count);
      vault::wipe(encoded, sizeof(encoded));
    }
  }
  lv_obj_invalidate(self->board);
}
void VaultPattern::paint(lv_layer_t *layer) {
  lv_area_t origin; lv_obj_get_coords(board, &origin);
  const auto accent=error?lv_color_hex(0xF07579):theme::accent();
  lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
  line.width=LineWidth; line.color=accent; line.opa=LV_OPA_80; line.round_start=line.round_end=1;
  for (size_t i=1;i<stroke.count;++i) {
    line.p1={lv_value_precise_t(origin.x1+p::x(stroke.cells[i-1])),lv_value_precise_t(origin.y1+p::y(stroke.cells[i-1]))};
    line.p2={lv_value_precise_t(origin.x1+p::x(stroke.cells[i])),lv_value_precise_t(origin.y1+p::y(stroke.cells[i]))};
    lv_draw_line(layer,&line);
  }
  if (stroke.down && stroke.count) {
    line.p1={lv_value_precise_t(origin.x1+p::x(stroke.cells[stroke.count-1])),lv_value_precise_t(origin.y1+p::y(stroke.cells[stroke.count-1]))};
    line.p2={lv_value_precise_t(origin.x1+stroke.px),lv_value_precise_t(origin.y1+stroke.py)};
    line.opa=LV_OPA_50; lv_draw_line(layer,&line);
  }
  for (unsigned cell=0;cell<p::Maximum;++cell) {
    const int x=origin.x1+p::x(cell), y=origin.y1+p::y(cell);
    const bool selected=stroke.selected(cell);
    lv_draw_rect_dsc_t disc; lv_draw_rect_dsc_init(&disc); disc.radius=LV_RADIUS_CIRCLE;
    // Stable sizes avoid a visual jump when a point is acquired. LVGL areas
    // include both endpoints: 15px center, exactly 60px translucent outer ring.
    disc.bg_color=selected?accent:theme::muted();disc.bg_opa=selected?LV_OPA_20:LV_OPA_10;
    disc.border_color=disc.bg_color;disc.border_width=selected?2:1;disc.border_opa=selected?LV_OPA_70:LV_OPA_30;
    lv_area_t ring={x-RingBefore,y-RingBefore,x+RingAfter,y+RingAfter};lv_draw_rect(layer,&disc,&ring);
    const int r=DotDiameter/2;
    disc.bg_color=selected?accent:theme::muted();disc.bg_opa=enabled?LV_OPA_COVER:LV_OPA_50;disc.border_width=0;
    lv_area_t dot={x-r,y-r,x+r,y+r};lv_draw_rect(layer,&disc,&dot);
  }
}
} // namespace ui
