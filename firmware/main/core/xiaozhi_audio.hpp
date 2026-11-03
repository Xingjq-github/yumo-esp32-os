#pragma once
#include <cstddef>
#include <cstdint>

namespace core::xiaozhi {

// A single service worker owns this bridge. No codec or I2S work runs on the
// GUI or the WebSocket event task. Capture and playback are mutually exclusive.
class AudioBridge {
public:
    static constexpr int CaptureRate = 16000;
    static constexpr size_t CaptureChunkFrames = 320; // 20 ms, two physical MICs
    static constexpr size_t CapturePacketFrames = 960; // 60 ms, mono Opus
    static constexpr size_t PacketCapacity = 4096; // explicit transport budget

    AudioBridge() = default;
    ~AudioBridge();
    AudioBridge(const AudioBridge &) = delete;
    AudioBridge &operator=(const AudioBridge &) = delete;

    bool beginCapture();
    // 0: collecting a packet; >0: encoded byte count; -1: stopped/error.
    // level is the current input level in 0..100, updated every 20 ms.
    int capture(uint8_t *output, size_t capacity, unsigned &level);
    // Send this optional final packet BEFORE the protocol's listen/stop.
    // Pads only the last incomplete 60-ms frame; cancellation discards it.
    int finishCapture(uint8_t *output, size_t capacity);
    void endCapture();

    bool beginPlayback(int sampleRate);
    bool play(const uint8_t *packet, size_t bytes);
    void stop();

private:
    struct State;
    State *state_ = nullptr;
    bool allocate(size_t pcmFrames);
    int encode(uint8_t *output, size_t capacity);
};
} // namespace core::xiaozhi
