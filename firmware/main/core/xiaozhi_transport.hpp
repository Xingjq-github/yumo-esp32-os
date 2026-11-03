#pragma once
#include "xiaozhi_cloud.hpp"
#include <atomic>
#include <functional>
#include <string>

namespace core::xiaozhi {
using TextCallback = std::function<void(const char *, size_t)>;
using AudioCallback = std::function<void(const uint8_t *, size_t)>;

// A service worker owns all methods. SDK callbacks only deliver bounded data;
// callback receivers must copy it synchronously and never do GUI/audio I/O.
class TransportClient {
public:
    TransportClient(TextCallback onText, AudioCallback onAudio);
    ~TransportClient();
    TransportClient(const TransportClient &) = delete;
    TransportClient &operator=(const TransportClient &) = delete;

    bool connect(const CloudConfig &config, const CancelPredicate &cancelled,
                 std::string &error);
    bool sendText(const std::string &text);
    bool sendAudio(const uint8_t *data, size_t size, uint32_t timestamp);
    bool openAudio(const ServerEvent &hello, std::string &error);
    // Health check; a dedicated receiver drains UDP independently of I2S.
    // WebSocket mode is a no-op. False means cancellation/transport error.
    bool pollAudio();
    bool connected() const { return connected_.load(); }
    bool failed() const { return failure_.load()!=nullptr; }
    std::string error() const;
    // Worker-only: stops/joins SDK callbacks before releasing any state.
    void stop();

private:
    struct State;
    State *state_ = nullptr;
    TextCallback onText_;
    AudioCallback onAudio_;
    std::atomic<bool> connected_{false}, stopping_{false};
    std::atomic<const char *> failure_{nullptr};
    void fail(const char *message);
    bool cancelled() const;
    bool deliverText(const uint8_t *data,size_t size);
    bool deliverAudio(const uint8_t *data,size_t size);
    void websocketEvent(int32_t event,void *data);
    void mqttEvent(int32_t event,void *data);
    static void websocketCallback(void *arg,const char *base,int32_t event,void *data);
    static void mqttCallback(void *arg,const char *base,int32_t event,void *data);
    static void audioReceiver(void *arg);
    bool receiveAudio();
    void stopAudioReceiver();
};
} // namespace core::xiaozhi
