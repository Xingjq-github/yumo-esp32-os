#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include "worker_wake.hpp"

namespace core::xiaozhi {
enum class State { Connecting, Activating, Ready, Listening, Thinking, Speaking, Failed, Stopped };
struct Snapshot {
    State state = State::Connecting;
    uint32_t revision = 0;
    std::string notice, userText, assistantText, activationCode;
    unsigned level = 0;
};
struct Session;
struct Job {
    std::atomic<bool> cancelled{false}, done{false};
    Snapshot snapshot();
private:
    friend struct Session;
    friend std::shared_ptr<Job> start();
    friend bool toggleTalk(const std::shared_ptr<Job>&);
    friend void cancel(const std::shared_ptr<Job>&);
    std::mutex mutex_;
    Snapshot published_;
    enum class Command { None, Start, Stop, Abort };
    Command command_ = Command::None;
    WorkerWake wake_;
};
// Foreground-only sessions. All public operations return without network/audio I/O.
std::shared_ptr<Job> start();
bool toggleTalk(const std::shared_ptr<Job>& job);
void cancel(const std::shared_ptr<Job>& job);
bool idle();
}
