// Official discovery/activation v1; upstream MIT notice is in xiaozhi.LICENSE.
#include "xiaozhi_cloud.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#ifndef XIAOZHI_CLOUD_HOST_TEST
#include "connectivity.hpp"
#include "power_mgr.hpp"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <ctime>
#endif

namespace core::xiaozhi {
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
const cJSON* item(const cJSON* root, const char* name) { return cJSON_GetObjectItemCaseSensitive(root, name); }
bool text(const cJSON* value, std::string& result, size_t maximum, bool allowEmpty = true) {
    if (!cJSON_IsString(value) || !value->valuestring) return false;
    const size_t size = std::strlen(value->valuestring);
    if (size > maximum || (!allowEmpty && !size)) return false;
    result.assign(value->valuestring, size);
    return true;
}
bool optional(const cJSON* root, const char* name, std::string& result, size_t maximum) {
    const auto* value = item(root, name);
    return !value || text(value, result, maximum);
}
bool cleanHeader(const std::string& value) {
    for (unsigned char ch : value) if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}
bool host(const std::string& value) {
    if (value.empty() || value.size() > 253 || value.front() == '.' || value.back() == '.') return false;
    for (unsigned char ch : value)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '-')) return false;
    return true;
}
bool authority(const std::string& value) {
    const auto colon = value.find(':');
    if (colon == std::string::npos) return host(value);
    if (!host(value.substr(0, colon))) return false;
    const std::string port = value.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned number = 0;
    for (char ch : port) {
        if (ch < '0' || ch > '9') return false;
        number = number * 10 + unsigned(ch - '0');
    }
    return number > 0 && number <= 65535;
}
bool websocketUrl(const std::string& value) {
    if (value.rfind("wss://", 0) != 0 || value.size() > 1024 || !cleanHeader(value) ||
        value.find_first_of(" @#\\") != std::string::npos) return false;
    auto end = value.find_first_of("/?", 6);
    return authority(value.substr(6, end == std::string::npos ? end : end - 6));
}
bool integer(const cJSON* value, unsigned minimum, unsigned maximum, unsigned& result) {
    if (!cJSON_IsNumber(value) || !std::isfinite(value->valuedouble) ||
        value->valuedouble < minimum || value->valuedouble > maximum ||
        std::floor(value->valuedouble) != value->valuedouble) return false;
    result = static_cast<unsigned>(value->valuedouble);
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
bool mqttConfig(const cJSON* root, CloudConfig& config) {
    if (!cJSON_IsObject(root) || !text(item(root, "endpoint"), config.mqttEndpoint, 259, false) ||
        !authority(config.mqttEndpoint) || !text(item(root, "client_id"), config.mqttClientId, 256, false) ||
        !text(item(root, "username"), config.mqttUsername, 512, false) ||
        !text(item(root, "password"), config.mqttPassword, 2048, false) ||
        !text(item(root, "publish_topic"), config.mqttPublishTopic, 256, false) ||
        !optional(root, "subscribe_topic", config.mqttSubscribeTopic, 256)) return false;
    if (!cleanHeader(config.mqttClientId) || !cleanHeader(config.mqttUsername) ||
        !cleanHeader(config.mqttPassword) || !cleanHeader(config.mqttPublishTopic) ||
        !cleanHeader(config.mqttSubscribeTopic) || config.mqttPublishTopic.find_first_of("#+") != std::string::npos ||
        config.mqttSubscribeTopic.find_first_of("#+") != std::string::npos) return false;
    if (item(root, "keepalive") && !integer(item(root, "keepalive"), 15, 3600, config.mqttKeepalive)) return false;
    config.transport = Transport::MqttUdp;
    return true;
}
bool wsConfig(const cJSON* root, CloudConfig& config) {
    if (!cJSON_IsObject(root) || !text(item(root, "url"), config.wsUrl, 1024, false) ||
        !websocketUrl(config.wsUrl) || !optional(root, "token", config.token, 2048) || !cleanHeader(config.token)) return false;
    unsigned version = 1;
    if (item(root, "version") && !integer(item(root, "version"), 1, 3, version)) return false;
    config.protocolVersion = static_cast<int>(version);
    config.transport = Transport::WebSocket;
    return true;
}
}

bool parseDiscoveryResponse(const char* data, size_t size, CloudConfig& config,
                            ActivationInfo& activation, std::string& error) {
    error = "小智连接配置无效";
    activation = ActivationInfo{};
    if (!detail::validJsonInput(data, size)) return false;
    const char* end = nullptr;
    Json root(cJSON_ParseWithLengthOpts(data, size, &end, false), cJSON_Delete);
    if (!root || !cJSON_IsObject(root.get()) || !end || !noDuplicateFields(root.get())) return false;
    while (end < data + size && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t')) ++end;
    if (end != data + size) return false;
    ActivationInfo pending;
    const auto* info = item(root.get(), "activation");
    if (info) {
        std::string challenge;
        if (!cJSON_IsObject(info) || !optional(info, "code", pending.code, 64) ||
            !optional(info, "message", pending.message, 1024) ||
            !optional(info, "challenge", challenge, 4096) || !cleanHeader(pending.code)) return false;
        pending.challenge = !challenge.empty();
        if (item(info, "timeout_ms") && !integer(item(info, "timeout_ms"), 1, 86400000, pending.timeoutMs)) return false;
        pending.timeoutMs = std::max(1000u, std::min(300000u, pending.timeoutMs));
    }
    CloudConfig candidate;
    candidate.clientId = config.clientId;
    candidate.deviceId = config.deviceId;
    const bool mqtt = mqttConfig(item(root.get(), "mqtt"), candidate);
    if (!mqtt) {
        candidate = CloudConfig{};
        candidate.clientId = config.clientId;
        candidate.deviceId = config.deviceId;
    }
    const bool ready = mqtt || wsConfig(item(root.get(), "websocket"), candidate);
    if (!ready && pending.code.empty() && !pending.challenge) return false;
    if (ready) config = std::move(candidate);
    activation = std::move(pending);
    error.clear();
    return true;
}

#ifndef XIAOZHI_CLOUD_HOST_TEST
namespace {
constexpr char DiscoveryUrl[] = "https://api.tenclass.net/xiaozhi/ota/";
struct HttpOwner {
    esp_http_client_handle_t client = nullptr;
    ~HttpOwner() { if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); } }
};
bool proceeding(const CancelPredicate& cancelled, std::string& error) {
    if ((cancelled && cancelled()) || !sys::Power::isScreenOn() || sys::Power::sleepPending()) {
        error = "已退出小智";
        return false;
    }
    if (sys::Connectivity::wifiState() != sys::WifiState::Connected) {
        error = "请先连接Wi-Fi";
        return false;
    }
    return true;
}
bool uuidValid(const char* value) {
    if (std::strlen(value) != 36) return false;
    for (unsigned i = 0; i < 36; ++i) {
        const char ch = value[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (ch != '-') return false; }
        else if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return true;
}
bool identity(CloudConfig& config, std::string& error) {
    nvs_handle_t store = 0;
    if (nvs_open("yumo_xiaozhi", NVS_READWRITE, &store) != ESP_OK) {
        error = "小智身份存储不可用";
        return false;
    }
    char uuid[37]{};
    size_t size = sizeof(uuid);
    const auto status = nvs_get_str(store, "client_id", uuid, &size);
    bool ok = status == ESP_OK && size == sizeof(uuid) && uuidValid(uuid);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        uint8_t bytes[16];
        esp_fill_random(bytes, sizeof(bytes));
        bytes[6] = (bytes[6] & 0x0f) | 0x40;
        bytes[8] = (bytes[8] & 0x3f) | 0x80;
        std::snprintf(uuid, sizeof(uuid), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                      bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],
                      bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
        ok = nvs_set_str(store, "client_id", uuid) == ESP_OK && nvs_commit(store) == ESP_OK;
    }
    nvs_close(store);
    if (!ok) { error = "小智身份读取失败"; return false; }
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) { error = "小智设备信息不可用"; return false; }
    char address[18];
    std::snprintf(address, sizeof(address), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    config.clientId = uuid;
    config.deviceId = address;
    return true;
}
std::string descriptor(const CloudConfig& config) {
    Json root(cJSON_CreateObject(), cJSON_Delete);
    const auto* description = esp_app_get_description();
    if (!root || !cJSON_AddNumberToObject(root.get(), "version", 2) ||
        !cJSON_AddStringToObject(root.get(), "language", "zh-CN") ||
        !cJSON_AddStringToObject(root.get(), "mac_address", config.deviceId.c_str()) ||
        !cJSON_AddStringToObject(root.get(), "uuid", config.clientId.c_str()) ||
        !cJSON_AddStringToObject(root.get(), "chip_model_name", "esp32s3")) return {};
    auto* app = cJSON_AddObjectToObject(root.get(), "application");
    auto* board = cJSON_AddObjectToObject(root.get(), "board");
    if (!app || !board || !cJSON_AddStringToObject(app, "name", "yumo-os") ||
        !cJSON_AddStringToObject(app, "version", description->version) ||
        !cJSON_AddStringToObject(app, "idf_version", description->idf_ver) ||
        !cJSON_AddStringToObject(board, "type", "esp32-s3-touch-lcd-1.54") ||
        !cJSON_AddStringToObject(board, "name", "esp32-s3-touch-lcd-1.54") ||
        !cJSON_AddStringToObject(board, "manufacturer", "waveshare") ||
        !cJSON_AddStringToObject(board, "mac", config.deviceId.c_str())) return {};
    std::unique_ptr<char, decltype(&cJSON_free)> raw(cJSON_PrintUnformatted(root.get()), cJSON_free);
    return raw ? std::string(raw.get()) : std::string();
}
bool request(const CloudConfig& config, bool activate, const std::string& body,
             const CancelPredicate& cancelled, std::string& response, int& status, std::string& error) {
    response.clear(); status = 0;
    if (!proceeding(cancelled, error)) return false;
    if (std::time(nullptr) < 1704067200) { error = "请先联网校准时间"; return false; }
    const std::string url = std::string(DiscoveryUrl) + (activate ? "activate" : "");
    esp_http_client_config_t options{};
    options.url = url.c_str();
    options.method = HTTP_METHOD_POST;
    options.crt_bundle_attach = esp_crt_bundle_attach;
    options.timeout_ms = 5000;
    options.disable_auto_redirect = true;
    options.skip_cert_common_name_check = false;
    options.buffer_size = 2048;
    options.buffer_size_tx = 1024;
    HttpOwner owner{esp_http_client_init(&options)};
    if (!owner.client) { error = "小智网络内存不足"; return false; }
    auto client = owner.client;
    if (esp_http_client_set_header(client, "Activation-Version", "1") != ESP_OK ||
        esp_http_client_set_header(client, "Device-Id", config.deviceId.c_str()) != ESP_OK ||
        esp_http_client_set_header(client, "Client-Id", config.clientId.c_str()) != ESP_OK ||
        esp_http_client_set_header(client, "User-Agent", "yumo-esp32-s3-touch-lcd-1.54/2.0") != ESP_OK ||
        esp_http_client_set_header(client, "Accept-Language", "zh-CN") != ESP_OK ||
        esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK ||
        esp_http_client_set_header(client, "Accept", "application/json") != ESP_OK ||
        esp_http_client_set_header(client, "Accept-Encoding", "identity") != ESP_OK) {
        error = "小智请求初始化失败";
        return false;
    }
    const int64_t deadline = esp_timer_get_time() + 30000000;
    auto keepGoing = [&]() {
        if (!proceeding(cancelled, error)) return false;
        if (esp_timer_get_time() >= deadline) { error = "小智连接超时"; return false; }
        return true;
    };
    if (!keepGoing()) return false;
    const auto opened = esp_http_client_open(client, static_cast<int>(body.size()));
    if (!keepGoing()) return false;
    if (opened != ESP_OK) { error = "小智HTTPS连接失败"; return false; }
    size_t sent = 0;
    while (sent < body.size()) {
        if (!keepGoing()) return false;
        const int count = esp_http_client_write(client, body.data() + sent, static_cast<int>(body.size() - sent));
        if (!keepGoing()) return false;
        if (count <= 0) { error = "小智请求发送失败"; return false; }
        sent += static_cast<size_t>(count);
    }
    const int64_t length = esp_http_client_fetch_headers(client);
    if (!keepGoing()) return false;
    status = esp_http_client_get_status_code(client);
    if (length < 0 || length > static_cast<int64_t>(MaxJsonBytes)) { error = "小智响应长度无效"; return false; }
    if (status != 200 && !(activate && status == 202)) {
        error = status >= 300 && status < 400 ? "小智服务地址发生跳转" :
                status == 429 ? "小智请求频繁，请稍后重试" : "小智服务暂不可用";
        return false;
    }
    char chunk[1024];
    for (;;) {
        if (!keepGoing()) return false;
        const int count = esp_http_client_read(client, chunk, sizeof(chunk));
        if (!keepGoing()) return false;
        if (count < 0) { error = "小智响应读取失败"; return false; }
        if (!count) {
            if (!esp_http_client_is_complete_data_received(client)) { error = "小智响应不完整"; return false; }
            return true;
        }
        if (static_cast<size_t>(count) > MaxJsonBytes - response.size()) { error = "小智响应超过上限"; return false; }
        response.append(chunk, static_cast<size_t>(count));
        vTaskDelay(1);
    }
}
bool waitCancellable(unsigned milliseconds, const CancelPredicate& cancelled, std::string& error) {
    const int64_t until = esp_timer_get_time() + int64_t(milliseconds) * 1000;
    while (esp_timer_get_time() < until) {
        if (!proceeding(cancelled, error)) return false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return proceeding(cancelled, error);
}
}

bool discover(CloudConfig& config, const CancelPredicate& cancelled,
              const ActivationCallback& activation, std::string& error) {
    config = CloudConfig{};
    error.clear();
    if (!proceeding(cancelled, error) || !identity(config, error)) return false;
    const std::string body = descriptor(config);
    if (body.empty()) { error = "小智设备信息内存不足"; return false; }
    const int64_t end = esp_timer_get_time() + 300000000;
    while (esp_timer_get_time() < end) {
        std::string response;
        int status = 0;
        if (!request(config, false, body, cancelled, response, status, error)) return false;
        ActivationInfo info;
        if (!parseDiscoveryResponse(response.data(), response.size(), config, info, error)) return false;
        if (info.code.empty() && !info.challenge)
            return proceeding(cancelled, error);
        if (activation) activation(info.code, info.message.empty() ? "在xiaozhi.me添加设备" : info.message);
        // A successful activation must be followed by discovery for fresh transport credentials.
        if (info.challenge) {
            const int64_t refresh = std::min(end, esp_timer_get_time() + int64_t(std::min(info.timeoutMs, 15000u)) * 1000);
            while (esp_timer_get_time() < refresh) {
                if (!request(config, true, "{}", cancelled, response, status, error)) return false;
                if (status == 200) break;
                if (!waitCancellable(3000, cancelled, error)) return false;
            }
        } else if (!waitCancellable(3000, cancelled, error)) return false;
    }
    error = "绑定等待超时，请重新打开";
    return false;
}
#endif
}
