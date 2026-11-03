#include "core/xiaozhi_cloud.hpp"
#include "core/xiaozhi_protocol.hpp"
#include "cJSON.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace core::xiaozhi;
namespace {
unsigned checks = 0;
void check(bool value, const char* label) { ++checks; if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); } }
bool parse(const std::string& input, ServerEvent& event, Transport transport = Transport::WebSocket) {
    std::string error;
    return parseServerEvent(input.data(), input.size(), transport, event, error);
}
bool discovery(const std::string& input, CloudConfig& config, ActivationInfo& info) {
    std::string error;
    return parseDiscoveryResponse(input.data(), input.size(), config, info, error);
}
void invalid(const std::string& input) { ServerEvent event; check(!parse(input, event), "reject malformed protocol JSON"); }
}
int main() {
    ServerEvent event;
    const std::string hello = R"({"type":"hello","transport":"websocket","session_id":"synthetic-session","audio_params":{"format":"opus","sample_rate":24000,"channels":1,"frame_duration":60}})";
    check(parse(hello, event) && event.type == EventType::Hello && event.sampleRate == 24000 && event.frameDuration == 60, "valid server hello");
    for (size_t n = 0; n < hello.size(); ++n) invalid(hello.substr(0, n));
    invalid(hello + "{}");
    invalid("{\"type\":\"hello\",\"transport\":\"websocket\",\"audio_params\":null}");
    invalid("{\"type\":\"hello\",\"transport\":17}");
    invalid("{\"type\":\"hello\",\"transport\":\"udp\"}");
    invalid("{\"type\":\"hello\",\"transport\":\"websocket\",\"audio_params\":{\"sample_rate\":24000.5}}");
    invalid("{\"type\":\"hello\",\"transport\":\"websocket\",\"audio_params\":{\"sample_rate\":true}}");
    invalid("{\"type\":\"hello\",\"transport\":\"websocket\",\"audio_params\":{\"channels\":2}}");
    invalid("{\"type\":\"hello\",\"transport\":\"websocket\",\"audio_params\":{\"format\":\"pcm\"}}");
    invalid("{\"type\":\"hello\",\"transport\":\"websocket\",\"audio_params\":{\"frame_duration\":120}}");
    invalid("{\"type\":\"stt\",\"type\":\"tts\",\"text\":\"x\"}");
    invalid("{\"type\":\"stt\",\"text\":\"a\\u0000hidden\"}");
    invalid(std::string("{\"type\":\"stt\",\"text\":\"") + char(0xff) + "\"}");
    invalid("{\"type\":\"stt\",\"text\":\"" + std::string(MaxTextBytes + 1, 'a') + "\"}");
    invalid(std::string(MaxJsonBytes + 1, ' '));
    invalid("{\"type\":\"mcp\",\"payload\":" + std::string(40, '[') + "0" + std::string(40, ']') + "}");
    check(parse(hello + "\r\n\t ", event), "allow trailing JSON whitespace");
    check(parse("{\"type\":\"hello\",\"transport\":\"websocket\"}", event) && event.sampleRate == 24000, "official absent audio params defaults");
    check(parse("{\"type\":\"stt\",\"text\":\"你好 \\u4e16\\u754c\"}", event) && event.type == EventType::Stt, "UTF8 transcript");
    check(parse("{\"type\":\"tts\",\"state\":\"sentence_start\",\"text\":\"答复\"}", event) && event.type == EventType::TtsSentence && event.text == "答复", "reply subtitles");
    check(parse("{\"type\":\"llm\",\"emotion\":\"happy\",\"text\":\"emoji\"}", event) && event.type == EventType::Emotion && event.text.empty(), "LLM emoji not answer text");
    check(parse("{\"type\":\"tts\",\"state\":\"start\"}", event) && event.type == EventType::TtsStart, "tts starts");
    check(parse("{\"type\":\"tts\",\"state\":\"stop\"}", event) && event.type == EventType::TtsStop, "tts stops");
    check(parse("{\"type\":\"system\",\"command\":\"reboot\"}", event) && event.type == EventType::Ignored, "remote reboot ignored");
    check(parse("{\"type\":\"mcp\",\"payload\":{\"method\":\"tools/call\"}}", event) && event.type == EventType::Ignored, "MCP tools ignored");
    check(parse("{\"type\":\"tts\",\"state\":\"future_state\"}", event) && event.type == EventType::Ignored, "unknown state ignored");
    const std::string session = "synthetic\"\\\n";
    for (auto command : {makeListen(session, true), makeListen(session, false), makeAbort(session), makeGoodbye(session)}) {
        auto* root = cJSON_Parse(command.c_str());
        check(root && std::string(cJSON_GetObjectItemCaseSensitive(root, "session_id")->valuestring) == session, "builders escape session strings");
        cJSON_Delete(root);
    }
    check(makeAbort(std::string(129, 'x')).empty(), "reject oversized outgoing session");
    for (int version = 1; version <= 3; ++version) {
        const auto message = makeHello(Transport::WebSocket, version);
        auto* root = cJSON_Parse(message.c_str());
        check(root && cJSON_GetObjectItemCaseSensitive(root, "version")->valueint == version, "hello negotiated version");
        check(!cJSON_GetObjectItemCaseSensitive(root, "features"), "do not advertise MCP/AEC");
        cJSON_Delete(root);
        std::vector<uint8_t> original(133), wire;
        for (size_t i = 0; i < original.size(); ++i) original[i] = uint8_t(i);
        check(packWebsocketAudio(version, original.data(), original.size(), 0x12345678, wire), "pack complete audio");
        const uint8_t* opus; size_t size; uint32_t timestamp;
        check(unpackWebsocketAudio(version, wire.data(), wire.size(), opus, size, timestamp) &&
              size == original.size() && !std::memcmp(opus, original.data(), size), "audio protocol roundtrip");
        check(timestamp == (version == 2 ? 0x12345678u : 0), "network order timestamp");
        if (version > 1) {
            for (size_t n = 0; n < wire.size(); ++n)
                check(!unpackWebsocketAudio(version, wire.data(), n, opus, size, timestamp), "every framed audio truncation rejected");
            auto bad = wire; bad[version == 2 ? 3 : 0] = 1;
            check(!unpackWebsocketAudio(version, bad.data(), bad.size(), opus, size, timestamp), "wrong audio type rejected");
            bad = wire; bad[version == 2 ? 15 : 3] ^= 1;
            check(!unpackWebsocketAudio(version, bad.data(), bad.size(), opus, size, timestamp), "length mismatch rejected");
        }
        for (size_t count : {size_t(1275), size_t(1276), size_t(4096)}) {
            original.resize(count);
            for (size_t i = 0; i < count; ++i) original[i] = uint8_t(i * 31 + 7);
            check(packWebsocketAudio(version, original.data(), count, 0x87654321, wire), "packet frame-cap and application-cap boundaries accepted");
            check(unpackWebsocketAudio(version, wire.data(), wire.size(), opus, size, timestamp) &&
                  size == count && !std::memcmp(opus, original.data(), count), "accepted packet boundary preserves all audio bytes");
            check(timestamp == (version == 2 ? 0x87654321u : 0), "boundary packet preserves timestamp");
        }
        // A matching header with 4097 payload bytes must fail the packet limit,
        // not merely a length mismatch. Each protocol version receives it too.
        auto oversizedWire = wire;
        oversizedWire.push_back(0);
        if (version == 2) {oversizedWire[12] = 0; oversizedWire[13] = 0; oversizedWire[14] = 0x10; oversizedWire[15] = 1;}
        if (version == 3) {oversizedWire[2] = 0x10; oversizedWire[3] = 1;}
        check(!unpackWebsocketAudio(version, oversizedWire.data(), oversizedWire.size(), opus, size, timestamp), "4097-byte incoming packet rejected");
        check(!opus && !size && !timestamp, "rejected packet exposes no partial audio view");
        original.resize(4097);
        check(!packWebsocketAudio(version, original.data(), original.size(), 0, wire) && wire.empty(), "4097-byte outgoing packet rejected with no partial wire data");
        check(!packWebsocketAudio(version, nullptr, 0, 0, wire), "empty packet rejected");
    }
    const std::string udpHello = R"({"type":"hello","transport":"udp","session_id":"synthetic","udp":{"server":"voice.example.test","port":12345,"key":"00112233445566778899aabbccddeeff","nonce":"01000000000000000000000000000000"}})";
    check(parse(udpHello, event, Transport::MqttUdp) && event.udp.port == 12345 && event.udp.key[1] == 0x11, "MQTT UDP parameters hex decoding");
    check(!parse(udpHello, event, Transport::WebSocket), "reject mixed transport hello");
    for (auto target : {std::string("voice.example.test"), std::string("00112233445566778899aabbccddeeff"), std::string("12345")}) {
        auto bad = udpHello;
        const auto pos = bad.find(target);
        bad.replace(pos, target.size(), target == "12345" ? "0" : "invalid!\r\n");
        check(!parse(bad, event, Transport::MqttUdp), "malformed UDP host/key/port rejected");
    }
    CloudConfig config; ActivationInfo info;
    config.clientId = "synthetic-client"; config.deviceId = "00:00:00:00:00:00";
    const std::string ws = R"({"websocket":{"url":"wss://voice.example.test/v1","token":"synthetic-token","version":3}})";
    check(discovery(ws, config, info) && config.transport == Transport::WebSocket && config.protocolVersion == 3, "websocket discovery");
    check(config.clientId == "synthetic-client", "discovery preserves separate client identity");
    for (size_t n = 0; n < ws.size(); ++n) check(!discovery(ws.substr(0, n), config, info), "truncated discovery rejected");
    for (const auto& bad : {std::string("ws://voice.example.test/v1"), std::string("wss://user@voice.example.test/"), std::string("wss://voice.example.test:0/"), std::string("wss://voice.example.test:65536/"), std::string("wss://voice.example.test/#evil")}) {
        auto input = ws; input.replace(input.find("wss://voice.example.test/v1"), std::strlen("wss://voice.example.test/v1"), bad);
        check(!discovery(input, config, info), "unsafe discovered URL rejected");
    }
    auto badToken = ws; badToken.replace(badToken.find("synthetic-token"), 15, "token\\r\\nInjected:1");
    check(!discovery(badToken, config, info), "authorization injection rejected");
    check(config.token == "synthetic-token", "invalid response commits no partial credentials");
    const std::string mqtt = R"({"mqtt":{"endpoint":"mqtt.example.test:8883","client_id":"synthetic-mqtt","username":"synthetic-user","password":"synthetic-password","publish_topic":"device/synthetic","keepalive":240}})";
    check(discovery(mqtt, config, info) && config.transport == Transport::MqttUdp && config.mqttKeepalive == 240, "official MQTT discovery");
    check(config.mqttSubscribeTopic.empty(), "no invented MQTT subscribe topic");
    check(discovery(mqtt.substr(0, mqtt.size() - 1) + "," + ws.substr(1), config, info) && config.transport == Transport::MqttUdp, "prefer official MQTT when both available");
    check(discovery(R"({"activation":{"code":"123456","challenge":"synthetic-challenge","timeout_ms":600000}})", config, info) && info.challenge && info.timeoutMs == 300000, "activation pending with bounded lifetime");
    check(!discovery(R"({"firmware":{"force":1,"url":"https://example.test/firmware.bin"}})", config, info), "firmware command is not transport configuration");
    check(!discovery(R"({"websocket":{"url":"wss://voice.example.test","url":"wss://other.example.test"}})", config, info), "duplicate discovery fields rejected");
    std::printf("{\"passed\":true,\"checks\":%u}\n", checks);
}
