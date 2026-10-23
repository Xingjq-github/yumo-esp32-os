#pragma once
#include <cstddef>
#include <cstdint>

namespace core::recorder {
// 16 kHz interleaved MIC1/MIC2 -> mono, in place, with no allocation.
// Select useful signal level, not "speech quality". There is no beamforming
// or acoustic echo cancellation. Only coherent, unclipped pairs are averaged;
// anti-phase or imbalanced channels stay on the stable selection path.
class DualMicInput {
public:
  static constexpr size_t Channels = 2, MaxFrames = 512, FadeFrames = 64;
  static constexpr unsigned HoldBlocks = 3; // 96 ms at a full block

  constexpr void reset() {
    dcQ8_[0] = dcQ8_[1] = 0;
    selected_ = pending_ = 0;
    initialized_ = false;
    mix_ = 0;
  }

  // Caller supplies frames * Channels samples. Only the first frames samples
  // are output PCM; return value is MONO frames, never input sample count.
  constexpr size_t process(int16_t *pcm, size_t frames) {
    if (!pcm || !frames || frames > MaxFrames)
      return 0;
    const bool first = !initialized_;
    if (first) {
      // Suppress a different DC startup step on each physical microphone.
      dcQ8_[0] = int32_t(pcm[0]) * 256;
      dcQ8_[1] = int32_t(pcm[1]) * 256;
      initialized_ = true;
    }
    Stats stats[Channels]{};
    for (size_t i = 0; i < frames; ++i) {
      for (size_t channel = 0; channel < Channels; ++channel) {
        const int32_t raw = pcm[i * Channels + channel];
        auto &stat = stats[channel];
        if (magnitude(raw) >= 32000)
          ++stat.clipped; // Detect ADC clipping BEFORE DC removal.
        const int32_t input = raw * 256;
        dcQ8_[channel] += (input - dcQ8_[channel]) / 128;
        const int32_t filtered = clamp((input - dcQ8_[channel]) / 256);
        pcm[i * Channels + channel] = static_cast<int16_t>(filtered);
        const auto amplitude = magnitude(filtered);
        stat.sum += amplitude;
        if (amplitude > stat.peak)
          stat.peak = amplitude;
      }
    }
    int64_t energy[Channels]{}, cross=0;
    for(size_t i=0;i<frames;++i) {
      const int32_t a=pcm[i*Channels]/32,b=pcm[i*Channels+1]/32;
      energy[0]+=a*a;energy[1]+=b*b;cross+=a*b;
    }
    for (auto &stat : stats)
      stat.mean = stat.sum / frames;
    // Average only strongly positively correlated, comparable, unclipped
    // signals; otherwise keep the established selection path. No microphone
    // spacing/calibration is assumed, so this is not a beamformer.
    const bool coherent=cross>0 && stats[0].active() && stats[1].active() &&
      !stats[0].clipped && !stats[1].clipped &&
      energy[0]<=2*energy[1] && energy[1]<=2*energy[0] &&
      cross*cross*16>=energy[0]*energy[1]*9;
    if(coherent) {if(mix_<256)mix_+=32;}
    else mix_=0; // Never keep blending opposite-phase or clipped input.
    const unsigned previous = selected_, other = selected_ ^ 1U;
    const auto &current = stats[selected_];
    const auto &candidate = stats[other];
    const bool missing = current.mean < 48 && candidate.active();
    const bool lessClipping = current.severe(frames) && candidate.active() &&
                              candidate.clipped * 4 < current.clipped;
    const bool stronger = candidate.active() && !candidate.severe(frames) &&
                          candidate.mean > current.mean * 2;
    if (missing || lessClipping || (first && stronger)) {
      selected_ = other;
      pending_ = 0;
    } else if (stronger) {
      if (++pending_ >= HoldBlocks) {
        selected_ = other;
        pending_ = 0;
      }
    } else {
      pending_ = 0;
    }

    // Short 4 ms crossfade only on an actual switch; hysteresis prevents
    // block-by-block chatter. A phase-dependent dip during this ramp is
    // possible, but there is no permanent cancellation of opposite inputs.
    const size_t fade = frames < FadeFrames ? frames : FadeFrames;
    for (size_t i = 0; i < frames; ++i) {
      const int32_t next = pcm[i * Channels + selected_];
      int32_t output = next;
      if (!first && previous != selected_ && i < fade) {
        const int32_t old = pcm[i * Channels + previous];
        output = (old * int32_t(fade - i - 1) + next * int32_t(i + 1)) /
                 int32_t(fade);
      }
      // All source samples for this frame were read before compacting. A
      // destination index never overwrites a future interleaved source frame.
      if(mix_) {
        const int32_t average=(int32_t(pcm[i*Channels])+pcm[i*Channels+1])/2;
        output=(output*(256-mix_)+average*mix_)/256;
      }
      pcm[i] = static_cast<int16_t>(output);
    }
    return frames;
  }

  constexpr unsigned selectedChannel() const { return selected_; }

private:
  struct Stats {
    uint32_t sum = 0, mean = 0, peak = 0, clipped = 0;
    constexpr bool active() const { return mean >= 96 && peak >= 384; }
    constexpr bool severe(size_t frames) const {
      return clipped >= 2 && clipped * 32 >= frames;
    }
  };
  static constexpr int32_t clamp(int32_t value) {
    return value < -32768 ? -32768 : value > 32767 ? 32767 : value;
  }
  static constexpr uint32_t magnitude(int32_t value) {
    return static_cast<uint32_t>(value < 0 ? -value : value);
  }
  int32_t dcQ8_[Channels]{};
  unsigned char selected_ = 0, pending_ = 0;
  bool initialized_ = false;
  uint16_t mix_ = 0;
};
static_assert(sizeof(DualMicInput) <= 16);
} // namespace core::recorder
