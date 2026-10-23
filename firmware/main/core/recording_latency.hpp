#pragma once
namespace core::recorder {
class RecordingLatency {
public:
  constexpr void reset() {ready_=pending_=false;}
  constexpr bool acceptFrame() {const bool output=ready_;ready_=pending_=true;return output;}
  constexpr bool flush() {const bool output=pending_;pending_=false;return output;}
private:
  bool ready_=false,pending_=false;
};
} // namespace core::recorder
