#pragma once
#include <cstdint>

namespace ui::boot {
constexpr uint32_t DurationMs = 600, FrameMs = 15;
constexpr unsigned ramp(uint32_t elapsed, uint32_t begin, uint32_t end) {
    return elapsed <= begin ? 0 : elapsed >= end ? 255 :
        unsigned((elapsed - begin) * 255 / (end - begin));
}
struct Frame {
    unsigned branches, stem, textOpacity, dotOpacity;
    int textY, radius, angle, barWidth;
};
constexpr Frame frame(uint32_t elapsed) {
    const unsigned text = ramp(elapsed, 50, 325);
    const unsigned settle = ramp(elapsed, 225, 450);
    return {ramp(elapsed, 0, 150), ramp(elapsed, 110, 240), text,
            (255 - settle) * 160 / 255,
            145 + int((255 - text) * 8 / 255), 48 - int(settle * 18 / 255),
            int((elapsed % DurationMs) * 450 / DurationMs),
            int(ramp(elapsed, 300, 550) * 120 / 255)};
}
} // namespace ui::boot
