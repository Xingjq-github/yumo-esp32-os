#include "vault_qr.hpp"
#include "core/vault_crypto.hpp"
#include "src/libs/qrcode/qrcodegen.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <vector>
#include <cstring>
namespace ui {
bool vaultQr(lv_obj_t *parent, const std::string &text) {
  constexpr int MaxVersion = 23, Pixels = 240;
  constexpr size_t Capacity = qrcodegen_BUFFER_LEN_FOR_VERSION(MaxVersion);
  vault::SecretBytes scratch, qr, bytes;
  scratch.value.resize(Capacity); qr.value.resize(Capacity);
  bytes.value.resize(text.size());
  uint8_t eci[3]{};
  const qrcodegen_Segment segments[] = {
    qrcodegen_makeEci(26, eci),
    qrcodegen_makeBytes(reinterpret_cast<const uint8_t *>(text.data()), text.size(), bytes.value.data())
  };
  if (!qrcodegen_encodeSegmentsAdvanced(segments, 2, qrcodegen_Ecc_LOW, 1, MaxVersion,
        qrcodegen_Mask_AUTO, true, scratch.value.data(), qr.value.data())) return false;
  const int modules = qrcodegen_getSize(qr.value.data()), scale = Pixels / (modules + 8);
  if (scale < 2) return false;
  auto buffer = lv_draw_buf_create(Pixels, Pixels, LV_COLOR_FORMAT_I1, LV_STRIDE_AUTO);
  if (!buffer) return false;
  std::memset(buffer->data, 0, buffer->data_size);
  const int origin = (Pixels - modules * scale) / 2;
  // Palette index 0 is white; index 1 is black. Integer scaling only.
  for (int y = 0; y < modules; ++y) for (int x = 0; x < modules; ++x) {
    if (!qrcodegen_getModule(qr.value.data(), x, y)) continue;
    for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx) {
      const int px = origin + x * scale + dx, py = origin + y * scale + dy;
      buffer->data[8 + py * buffer->header.stride + px / 8] |= 1u << (7 - px % 8);
    }
  }
  auto canvas = lv_canvas_create(parent);
  lv_canvas_set_draw_buf(canvas, buffer);
  lv_canvas_set_palette(canvas, 0, lv_color_to_32(lv_color_white(), LV_OPA_COVER));
  lv_canvas_set_palette(canvas, 1, lv_color_to_32(lv_color_black(), LV_OPA_COVER));
  lv_obj_add_event_cb(canvas, [](lv_event_t *event) {
    auto buf = static_cast<lv_draw_buf_t *>(lv_event_get_user_data(event));
    lv_image_cache_drop(buf);
    vault::wipe(buf->data, buf->data_size);
    lv_draw_buf_destroy(buf);
  }, LV_EVENT_DELETE, buffer);
  lv_obj_set_clickable(canvas, false);
  lv_obj_set_scrollable(canvas, false);
  return true;
}
}
