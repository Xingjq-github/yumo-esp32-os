#pragma once
#include <cstddef>
#include <cstdint>

namespace core::recorder {
// Bounded, allocation-free mono PCM conditioning for 16 kHz recording only.
// A 512-sample block is 32 ms. Playback never processes/rewrites old files.
class RecordingLevel {
public:
  static constexpr size_t MaxSamples = 512;
  static constexpr int32_t One = 4096;
  static constexpr int32_t BaseGain = 2 * One, MaxGain = 8 * One;
  static constexpr int32_t TargetPeak = 12000, Ceiling = 30000;

  constexpr void reset() {
    dcQ8_ = 0;
    gain_ = BaseGain;
    applied_ = 0; // short ramp on start/resume, no inherited DC/gain transient
  }

  constexpr unsigned process(int16_t *samples, size_t count) {
    return processImpl(samples, count, true, true);
  }

  // DualMicInput has already removed each channel's DC before selection.
  constexpr unsigned processDcBlocked(int16_t *samples, size_t count, bool allowBoost = true) {
    return processImpl(samples, count, false, allowBoost);
  }

  constexpr int32_t gainQ12() const { return gain_; }

private:
  constexpr unsigned processImpl(int16_t *samples, size_t count, bool removeDc, bool allowBoost) {
    if (!samples || !count || count > MaxSamples)
      return 0;
    uint32_t peak = 0, absoluteSum = 0;
    for (size_t i = 0; i < count; ++i) {
      // Remove DC / very low-frequency bias (~20 Hz corner at 16 kHz).
      if (removeDc) {
        const int32_t input = int32_t(samples[i]) * 256;
        dcQ8_ += (input - dcQ8_) / 128;
        const int32_t filtered = clamp((input - dcQ8_) / 256, -32768, 32767);
        samples[i] = static_cast<int16_t>(filtered);
      }
      const uint32_t amplitude = magnitude(samples[i]);
      if (amplitude > peak)
        peak = amplitude;
      absoluteSum += amplitude;
    }

    // Do not chase silence to maximum gain. Quiet frames relax toward 2x;
    // voice-like levels rise slowly, loud input reduces the gain immediately.
    const bool active = allowBoost && peak >= 384 && absoluteSum / count >= 96;
    const int32_t desired =
        active ? clamp(TargetPeak * One / int32_t(peak), One, MaxGain)
               : BaseGain;
    if (active && desired < gain_)
      gain_ = desired;
    else if (desired > gain_)
      gain_ += (desired - gain_ + 15) / 16;
    else if (desired < gain_)
      gain_ -= (gain_ - desired + 31) / 32;

    // One block of look-ahead already exists in the SD writer. Cap BOTH ends
    // of the gain ramp so a sudden loud sample cannot wrap or clip to int16.
    const int32_t headroom = peak ? Ceiling * One / int32_t(peak) : MaxGain;
    const int32_t from = applied_ < headroom ? applied_ : headroom;
    const int32_t to = gain_ < headroom ? gain_ : headroom;
    unsigned outputPeak = 0;
    for (size_t i = 0; i < count; ++i) {
      const int32_t gain = from + (to - from) * int32_t(i + 1) / int32_t(count);
      const int32_t output =
          clamp(int32_t(samples[i]) * gain / One, -Ceiling, Ceiling);
      samples[i] = static_cast<int16_t>(output);
      const auto amplitude = magnitude(output);
      if (amplitude > outputPeak)
        outputPeak = amplitude;
    }
    applied_ = to;
    return outputPeak;
  }

  static constexpr int32_t clamp(int32_t value, int32_t low, int32_t high) {
    return value < low ? low : value > high ? high : value;
  }
  static constexpr uint32_t magnitude(int32_t value) {
    return static_cast<uint32_t>(value < 0 ? -value : value);
  }
  int32_t dcQ8_ = 0, gain_ = BaseGain, applied_ = 0;
};
static_assert(sizeof(RecordingLevel) <= 16);
} // namespace core::recorder
