// These assertions execute the real UI state model at compile time.
#include "core/vault_interaction.hpp"
using vault::interaction::RevealState;
using L = vault::interaction::PinLayout;
constexpr bool initiallyHidden() {
  RevealState s;
  return !s.visible(0) && !s.expire(60000) && !s.complete(true, 0, 0);
}
constexpr bool requestNeedsVerification() {
  RevealState s;
  s.request(3);
  return !s.visible(3) && !s.complete(false, 3, 0) && !s.visible(3);
}
constexpr bool onlyRequestedEntry() {
  RevealState s;
  s.request(3);
  return s.complete(true, 3, 10) && s.visible(3) && !s.visible(2);
}
constexpr bool wrongEntryConsumesGrant() {
  RevealState s;
  s.request(3);
  return !s.complete(true, 2, 0) && !s.visible(3) && !s.complete(true, 3, 1);
}
constexpr bool cancelRejectsLateResult() {
  RevealState s;
  s.request(3);
  s.hide();
  return !s.complete(true, 3, 1) && !s.visible(3);
}
constexpr bool hideRequiresFreshPin() {
  RevealState s;
  s.request(3);
  if (!s.complete(true, 3, 0)) return false;
  s.hide();
  if (s.visible(3) || s.complete(true, 3, 1)) return false;
  s.request(3);
  return !s.visible(3) && s.complete(true, 3, 2);
}
constexpr bool errorThenRetry() {
  RevealState s;
  s.request(3);
  if (s.complete(false, 3, 0) || s.complete(true, 3, 1)) return false;
  s.request(3);
  return s.complete(true, 3, 2);
}
constexpr bool newRequestHidesPrevious() {
  RevealState s;
  s.request(3);
  if (!s.complete(true, 3, 0)) return false;
  s.request(4);
  return !s.visible(3) && !s.visible(4) && s.complete(true, 4, 1);
}
constexpr bool completionCannotReplay() {
  RevealState s;
  s.request(3);
  return s.complete(true, 3, 0) && !s.complete(true, 3, 1) && !s.visible(3);
}
constexpr bool expiresAtDeadline() {
  RevealState s;
  s.request(3);
  if (!s.complete(true, 3, 100)) return false;
  return !s.expire(30099) && s.visible(3) && s.expire(30100) &&
         !s.visible(3) && !s.expire(30101) && !s.complete(true, 3, 30102);
}
constexpr bool expirySurvivesTickWrap() {
  constexpr uint32_t start = UINT32_MAX - 10000;
  RevealState s;
  s.request(3);
  if (!s.complete(true, 3, start)) return false;
  return !s.expire(uint32_t(start + RevealState::DisplayMs - 1)) &&
         s.expire(uint32_t(start + RevealState::DisplayMs)) && !s.visible(3);
}
static_assert(initiallyHidden());
static_assert(requestNeedsVerification());
static_assert(onlyRequestedEntry());
static_assert(wrongEntryConsumesGrant());
static_assert(cancelRejectsLateResult());
static_assert(hideRequiresFreshPin());
static_assert(errorThenRetry());
static_assert(newRequestHidesPrevious());
static_assert(completionCannotReplay());
static_assert(expiresAtDeadline());
static_assert(expirySurvivesTickWrap());
static_assert(RevealState::DisplayMs == 30000);
static_assert(vault::interaction::KeyGapMs == 10);
using vault::interaction::PinLine;
using vault::interaction::pinLine;
constexpr bool pinLineStates() {
  if (pinLine(0, false) != PinLine::Prompt) return false;
  // Typing replaces a prompt; message wins over any retained invalid PIN.
  for (size_t digits = 0; digits <= 12; ++digits) {
    if (pinLine(digits, true) != PinLine::Message) return false;
    if (digits && pinLine(digits, false) != PinLine::Masked) return false;
  }
  // Editing clears the message; deleting the last digit restores the prompt.
  return pinLine(4, true) == PinLine::Message &&
         pinLine(5, false) == PinLine::Masked &&
         pinLine(0, false) == PinLine::Prompt;
}
static_assert(pinLineStates());
static_assert(L::LineHeight == 21);
static_assert(L::KeyHeight == 53 && L::KeyHeight > 44);
static_assert(L::KeyWidth == 79 && L::Gap == 1);
static_assert(3 * L::KeyWidth + 2 * L::Gap <= L::Width);
static_assert(L::ContentHeight == 237 && L::ContentHeight <= L::BodyHeight);
static_assert(L::HeaderHeight + L::Gap + L::NormalBodyHeight == 240);
static_assert(L::BodyHeight == 240 && L::Width == 240);
using I = vault::interaction::InputLayout;
static_assert(I::TextWidth == 180 && I::EnterWidth == 59);
static_assert(I::TextWidth + I::Gap + I::EnterWidth == I::Width);
static_assert(I::TextWidth * 4 == I::Width * 3);
static_assert(I::RowHeight == 40 && I::Height == 81);
static_assert(2 * I::RowHeight + I::Gap == I::Height);
