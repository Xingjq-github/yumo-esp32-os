// Protocol-compatible integration based on 78/xiaozhi-esp32 (MIT).
// Upstream notices: xiaozhi.LICENSE; pinned revision 8dc6d4588368a2091bf4f577e8b03a491c28fd71.
#include "xiaozhi_protocol.hpp"
#include "cJSON.h"
#include <cmath>
#include <cstring>
#include <memory>

namespace core::xiaozhi {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
bool stringValue(const cJSON* value, std::string& output, size_t limit) {
    if (!cJSON_IsString(value) || !value->valuestring) return false;
    const size_t size = std::strlen(value->valuestring);
    if (size > limit) return false;
    output.assign(value->valuestring, size);
    return true;
}
const cJSON* field(const cJSON* root, const char* name) {
    return cJSON_GetObjectItemCaseSensitive(root, name);
}
bool optionalString(const cJSON* root, const char* name, std::string& out, size_t limit) {
    const auto* item = field(root, name);
    return !item || stringValue(item, out, limit);
}
bool number(const cJSON* value, int& output) {
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        value->valuedouble < 0 || value->valuedouble > 65535 ||
        std::floor(value->valuedouble) != value->valuedouble) return false;
    output = static_cast<int>(value->valuedouble);
    return true;
}
bool validUtf8(const char* data, size_t size) {
    for (size_t i = 0; i < size;) {
        const auto ch = static_cast<unsigned char>(data[i++]);
        if (!ch) return false;
        if (ch < 0x80) continue;
        unsigned extra = ch >= 0xc2 && ch <= 0xdf ? 1 : ch >= 0xe0 && ch <= 0xef ? 2 :
                         ch >= 0xf0 && ch <= 0xf4 ? 3 : 4;
        if (extra == 4 || extra > size - i) return false;
        const auto next = static_cast<unsigned char>(data[i]);
        if ((ch == 0xe0 && next < 0xa0) || (ch == 0xed && next >= 0xa0) ||
            (ch == 0xf0 && next < 0x90) || (ch == 0xf4 && next >= 0x90)) return false;
        while (extra--) if ((static_cast<unsigned char>(data[i++]) & 0xc0) != 0x80) return false;
    }
    return true;
}
bool noDuplicateFields(const cJSON* root, unsigned depth = 0) {
    if (depth > 12) return false;
    for (auto* a = root->child; a; a = a->next) {
        if (cJSON_IsObject(root)) {
            if (!a->string) return false;
            for (auto* b = a->next; b; b = b->next)
                if (b->string && !std::strcmp(a->string, b->string)) return false;
        }
        if ((cJSON_IsArray(a) || cJSON_IsObject(a)) && !noDuplicateFields(a, depth + 1)) return false;
    }
    return true;
}
bool safeJsonBytes(const char* data, size_t size) {
    if (!data || !size || size > MaxJsonBytes || !validUtf8(data, size)) return false;
    // cJSON stores strings as C strings: forbid embedded NUL escapes explicitly.
    bool quoted = false;
    unsigned nesting = 0;
    for (size_t i = 0; i < size; ++i) {
        if (quoted && data[i] == '\\') {
            if (++i >= size) return false;
            if (data[i] == 'u' && size - i >= 5 && !std::memcmp(data + i + 1, "0000", 4)) return false;
        } else if (data[i] == '"') quoted = !quoted;
        else if (!quoted && (data[i] == '[' || data[i] == '{')) {
            if (++nesting > 12) return false;
        } else if (!quoted && (data[i] == ']' || data[i] == '}')) {
            if (!nesting) return false;
            --nesting;
        }
    }
    return !quoted && !nesting;
}
bool hex16(const cJSON* value, std::array<uint8_t, 16>& output) {
    std::string text;
    if (!stringValue(value, text, 32) || text.size() != 32) return false;
    auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < 16; ++i) {
        const int high = hex(text[i * 2]), low = hex(text[i * 2 + 1]);
        if (high < 0 || low < 0) return false;
        output[i] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}
bool serverHost(const std::string& host) {
    if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.') return false;
    for (unsigned char ch : host)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '-')) return false;
    return true;
}
std::string serialize(cJSON* root) {
    if (!root) return {};
    std::unique_ptr<char, decltype(&cJSON_free)> text(cJSON_PrintUnformatted(root), cJSON_free);
    return text ? std::string(text.get()) : std::string();
}
std::string command(const char* type, const std::string& session, const char* state = nullptr) {
    if (session.size() > 128 || (!session.empty() && !validUtf8(session.data(), session.size()))) return {};
    Json root(cJSON_CreateObject(), cJSON_Delete);
    if (!root || !cJSON_AddStringToObject(root.get(), "type", type) ||
        !cJSON_AddStringToObject(root.get(), "session_id", session.c_str())) return {};
    if (state && !cJSON_AddStringToObject(root.get(), "state", state)) return {};
    if (state && !std::strcmp(state, "start") && !cJSON_AddStringToObject(root.get(), "mode", "manual")) return {};
    return serialize(root.get());
}
uint16_t read16(const uint8_t* bytes) { return static_cast<uint16_t>((bytes[0] << 8) | bytes[1]); }
uint32_t read32(const uint8_t* bytes) {
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | bytes[3];
}
void write16(uint8_t* bytes, size_t value) { bytes[0] = uint8_t(value >> 8); bytes[1] = uint8_t(value); }
void write32(uint8_t* bytes, uint32_t value) {
    bytes[0] = uint8_t(value >> 24); bytes[1] = uint8_t(value >> 16);
    bytes[2] = uint8_t(value >> 8); bytes[3] = uint8_t(value);
}
}

namespace detail { bool validJsonInput(const char* data, size_t size) { return safeJsonBytes(data, size); } }

std::string makeHello(Transport transport, int version) {
    if (version < 1 || version > 3) return {};
    Json root(cJSON_CreateObject(), cJSON_Delete);
    if (!root || !cJSON_AddStringToObject(root.get(), "type", "hello") ||
        !cJSON_AddNumberToObject(root.get(), "version", transport == Transport::MqttUdp ? 3 : version) ||
        !cJSON_AddStringToObject(root.get(), "transport", transport == Transport::MqttUdp ? "udp" : "websocket")) return {};
    auto* audio = cJSON_AddObjectToObject(root.get(), "audio_params");
    if (!audio || !cJSON_AddStringToObject(audio, "format", "opus") ||
        !cJSON_AddNumberToObject(audio, "sample_rate", 16000) ||
        !cJSON_AddNumberToObject(audio, "channels", 1) ||
        !cJSON_AddNumberToObject(audio, "frame_duration", 60)) return {};
    return serialize(root.get());
}
std::string makeListen(const std::string& session, bool start) { return command("listen", session, start ? "start" : "stop"); }
std::string makeAbort(const std::string& session) { return command("abort", session); }
std::string makeGoodbye(const std::string& session) { return command("goodbye", session); }

bool parseServerEvent(const char* data, size_t size, Transport transport, ServerEvent& result, std::string& error) {
    result = ServerEvent{};
    error = "小智响应格式无效";
    if (!safeJsonBytes(data, size)) return false;
    const char* end = nullptr;
    Json root(cJSON_ParseWithLengthOpts(data, size, &end, false), cJSON_Delete);
    if (!root || !cJSON_IsObject(root.get()) || !noDuplicateFields(root.get()) || !end) return false;
    while (end < data + size && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
    if (end != data + size) return false;
    ServerEvent candidate;
    std::string type;
    if (!stringValue(field(root.get(), "type"), type, 32) ||
        !optionalString(root.get(), "session_id", candidate.sessionId, 128)) return false;
    if (type == "hello") {
        std::string mode;
        if (!stringValue(field(root.get(), "transport"), mode, 16) ||
            mode != (transport == Transport::MqttUdp ? "udp" : "websocket")) return false;
        const auto* params = field(root.get(), "audio_params");
        if (params) {
            if (!cJSON_IsObject(params)) return false;
            std::string format;
            int channels = 1;
            if (!optionalString(params, "format", format, 16) || (!format.empty() && format != "opus") ||
                (field(params, "channels") && (!number(field(params, "channels"), channels) || channels != 1)) ||
                (field(params, "sample_rate") && !number(field(params, "sample_rate"), candidate.sampleRate)) ||
                (field(params, "frame_duration") && !number(field(params, "frame_duration"), candidate.frameDuration))) return false;
            const int sr = candidate.sampleRate, ms = candidate.frameDuration;
            if ((sr != 8000 && sr != 12000 && sr != 16000 && sr != 24000 && sr != 48000) ||
                (ms != 20 && ms != 40 && ms != 60)) {
                error = "小智音频参数不支持";
                return false;
            }
        }
        if (transport == Transport::MqttUdp) {
            const auto* udp = field(root.get(), "udp");
            int port = 0;
            std::string encryption;
            if (!cJSON_IsObject(udp) || !stringValue(field(udp, "server"), candidate.udp.server, 253) ||
                !serverHost(candidate.udp.server) || !number(field(udp, "port"), port) || !port ||
                !hex16(field(udp, "key"), candidate.udp.key) || !hex16(field(udp, "nonce"), candidate.udp.nonce) ||
                !optionalString(udp, "encryption", encryption, 32) ||
                (!encryption.empty() && encryption != "aes-128-ctr")) return false;
            candidate.udp.port = static_cast<uint16_t>(port);
        }
        candidate.type = EventType::Hello;
    } else if (type == "stt") {
        if (!stringValue(field(root.get(), "text"), candidate.text, MaxTextBytes)) return false;
        candidate.type = EventType::Stt;
    } else if (type == "tts") {
        std::string state;
        if (!stringValue(field(root.get(), "state"), state, 32)) return false;
        if (state == "start") candidate.type = EventType::TtsStart;
        else if (state == "stop") candidate.type = EventType::TtsStop;
        else if (state == "sentence_start") {
            if (!stringValue(field(root.get(), "text"), candidate.text, MaxTextBytes)) return false;
            candidate.type = EventType::TtsSentence;
        }
    } else if (type == "llm") {
        if (!optionalString(root.get(), "emotion", candidate.emotion, 64)) return false;
        candidate.type = EventType::Emotion;
    } else if (type == "goodbye") candidate.type = EventType::Goodbye;
    else if (type == "alert" || type == "error") {
        if (!optionalString(root.get(), "message", candidate.text, MaxTextBytes)) return false;
        candidate.type = EventType::Alert;
    }
    result = std::move(candidate);
    error.clear();
    return true;
}

bool packWebsocketAudio(int version, const uint8_t* opus, size_t size, uint32_t timestamp,
                        std::vector<uint8_t>& output) {
    output.clear();
    if (version < 1 || version > 3 || !opus || !size || size > MaxOpusBytes) return false;
    const size_t header = version == 2 ? 16 : version == 3 ? 4 : 0;
    output.assign(header + size, 0);
    if (version == 2) {
        write16(output.data(), 2); write32(output.data() + 8, timestamp); write32(output.data() + 12, uint32_t(size));
    } else if (version == 3) write16(output.data() + 2, size);
    std::memcpy(output.data() + header, opus, size);
    return true;
}
bool unpackWebsocketAudio(int version, const uint8_t* data, size_t size, const uint8_t*& opus,
                          size_t& opusSize, uint32_t& timestamp) {
    opus = nullptr; opusSize = 0; timestamp = 0;
    if (version < 1 || version > 3 || !data) return false;
    const size_t header = version == 2 ? 16 : version == 3 ? 4 : 0;
    if (size <= header || size - header > MaxOpusBytes) return false;
    if (version == 2 && (read16(data) != 2 || read16(data + 2) != 0 || read32(data + 12) != size - header)) return false;
    if (version == 3 && (data[0] != 0 || read16(data + 2) != size - header)) return false;
    opus = data + header; opusSize = size - header;
    if (version == 2) timestamp = read32(data + 8);
    return true;
}
}
