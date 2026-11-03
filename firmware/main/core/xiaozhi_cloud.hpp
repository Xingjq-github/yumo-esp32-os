#pragma once
#include "xiaozhi_protocol.hpp"
#include <functional>
#include <string>

namespace core::xiaozhi {
struct CloudConfig {
    Transport transport = Transport::WebSocket;
    std::string wsUrl, token, clientId, deviceId;
    int protocolVersion = 1;
    std::string mqttEndpoint, mqttClientId, mqttUsername, mqttPassword;
    std::string mqttPublishTopic, mqttSubscribeTopic;
    unsigned mqttKeepalive = 240;
};
struct ActivationInfo {
    std::string code, message;
    bool challenge = false;
    unsigned timeoutMs = 300000;
};
// Pure response parser; credentials commit only after complete validation.
bool parseDiscoveryResponse(const char* data, size_t size, CloudConfig& config,
                            ActivationInfo& activation, std::string& error);
using CancelPredicate = std::function<bool()>;
using ActivationCallback = std::function<void(const std::string& code, const std::string& message)>;
// Worker-only; I/O timeout5s, per-request30s, activation bounded to5min.
// Also cancels when screen is off, sleep pending or Wi-Fi is disconnected.
// Dedicated NVS UUID only. No official OTA, OS clock writes, vault data or token persistence.
bool discover(CloudConfig& config, const CancelPredicate& cancelled,
              const ActivationCallback& activation, std::string& error);
}
