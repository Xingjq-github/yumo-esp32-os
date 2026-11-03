#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core::xiaozhi {
enum class Transport { WebSocket, MqttUdp };
constexpr size_t MaxJsonBytes = 16384;
constexpr size_t MaxTextBytes = 4096;
// RFC 6716 section 3.2.1 limits a coded frame to 1275 bytes, not a packet.
// Packets also contain framing and may bundle multiple frames. This bounded
// application limit matches AudioBridge::PacketCapacity without depending on it.
constexpr size_t MaxOpusBytes = 4096;
namespace detail { bool validJsonInput(const char* data, size_t size); }
struct UdpParameters {
    std::string server;
    uint16_t port = 0;
    std::array<uint8_t, 16> key{}, nonce{};
};
enum class EventType { Ignored, Hello, Stt, TtsStart, TtsStop, TtsSentence, Emotion, Goodbye, Alert };
struct ServerEvent {
    EventType type = EventType::Ignored;
    std::string sessionId, text, emotion;
    int sampleRate = 24000, frameDuration = 60;
    UdpParameters udp;
};
// Protocol builders never interpolate unescaped server strings.
std::string makeHello(Transport transport, int version = 1);
std::string makeListen(const std::string& sessionId, bool start);
std::string makeAbort(const std::string& sessionId);
std::string makeGoodbye(const std::string& sessionId);
// Unknown/MCP/system events are ignored and never interpreted as device commands.
bool parseServerEvent(const char* data, size_t size, Transport transport,
                      ServerEvent& result, std::string& error);
bool packWebsocketAudio(int version, const uint8_t* opus, size_t size,
                        uint32_t timestamp, std::vector<uint8_t>& output);
// Returned view points into data and must not outlive it.
bool unpackWebsocketAudio(int version, const uint8_t* data, size_t size,
                          const uint8_t*& opus, size_t& opusSize, uint32_t& timestamp);
}
