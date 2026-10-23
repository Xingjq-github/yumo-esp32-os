#pragma once
#include <cstddef>
#include <cstdint>
namespace vault::interaction {
struct PinLayout {
  static constexpr int Width = 240, BodyHeight = 240;
  static constexpr int HeaderHeight = 36, NormalBodyHeight = 203;
  // One shared line for prompt, masked digits and feedback. Keep the Chinese
  // font's full 21px line and the original 1px gap; give saved height to keys.
  static constexpr int LineHeight = 21;
  static constexpr int KeyWidth = 79, Gap = 1;
  static constexpr int KeyHeight = (BodyHeight - LineHeight - 4 * Gap) / 4;
  static constexpr int ContentHeight = LineHeight + 4 * KeyHeight + 4 * Gap;
};
struct InputLayout {
  static constexpr int Width = 240, RowHeight = 40, Gap = 1;
  static constexpr int TextWidth = Width * 3 / 4;
  static constexpr int EnterWidth = Width - TextWidth - Gap;
  static constexpr int Height = 2 * RowHeight + Gap;
};
enum class PinLine { Prompt, Masked, Message };
constexpr PinLine pinLine(size_t digits, bool hasMessage) {
  return hasMessage ? PinLine::Message : digits ? PinLine::Masked : PinLine::Prompt;
}
// The HID transport still holds each press for 40ms and sends a release first.
// This is only the additional pause between completed character strokes.
constexpr uint32_t KeyGapMs = 10;
struct PurposeLayout {
  static constexpr int NameHeight = 21, InputHeight = 36, InputWidth = 52;
  static constexpr int TextWidth = 240 - InputWidth - 1;
  static constexpr int RowHeight = NameHeight + 1 + InputHeight;
  static constexpr int Pitch = RowHeight + 1;
  static constexpr int ViewHeight = PinLayout::NormalBodyHeight - NameHeight - InputHeight - 2;
};
// No PIN or credential copies. A local grant belongs to exactly one account
// page; a global grant lives only in the unlocked app session.
class AccessState {
public:
  static constexpr size_t Global = size_t(-1), None = size_t(-2);
  constexpr bool super() const { return m_super; }
  constexpr bool visible(size_t page) const {
    return m_super || (page != None && page != Global && m_page == page);
  }
  constexpr void leavePage() { m_page = m_pending = None; }
  constexpr void lock() { m_super = false; leavePage(); }
  constexpr void request(size_t scope) { leavePage(); m_pending = scope; }
  constexpr bool complete(bool verified, size_t scope) {
    const bool accepted = verified && scope != None && m_pending == scope;
    m_pending = None;
    if (accepted) {
      if (scope == Global) m_super = true;
      else m_page = scope;
    }
    return accepted;
  }
private:
  bool m_super = false;
  size_t m_page = None, m_pending = None;
};
class RevealState {
public:
  static constexpr uint32_t DisplayMs = 30000;
  constexpr void hide() { m_pending = m_visible = false; }
  constexpr void request(size_t entry) {
    hide();
    m_entry = entry;
    m_pending = true;
  }
  // A result grants only its requested entry and can never be replayed.
  constexpr bool complete(bool verified, size_t entry, uint32_t now) {
    m_visible = m_pending && verified && entry == m_entry;
    m_pending = false;
    m_shownAt = now;
    return m_visible;
  }
  constexpr bool visible(size_t entry) const { return m_visible && m_entry == entry; }
  constexpr bool expire(uint32_t now) {
    if (!m_visible || uint32_t(now - m_shownAt) < DisplayMs)
      return false;
    hide();
    return true;
  }
private:
  bool m_pending = false, m_visible = false;
  size_t m_entry = 0;
  uint32_t m_shownAt = 0;
};
} // namespace vault::interaction
