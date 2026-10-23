#pragma once
#include "fc_model.hpp"
#include "fc_saves.hpp"
#include "fc_video.hpp"
#include "sd_media.hpp"
#include "worker_wake.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
namespace core::fc {
struct Job : std::enable_shared_from_this<Job> {
  sd::Access sdAccess;
  Saves saves;
  uint32_t refreshRate=60; // immutable after ready
  std::atomic<bool> stop{false}, paused{false}, done{false}, ready{false},
      framePending{false}; // one display mailbox, not a queue
  std::atomic<uint8_t> buttons{0};
  WorkerWake wake;
  void requestStop() { stop.store(true);wake.notify(); }
  void setPaused(bool value) { if(paused.exchange(value)!=value)wake.notify(); }
  std::atomic<uint8_t> pressed{0}; // short keyboard/touch taps, consumed once per frame
  std::atomic<uint32_t> frames{0};
  uint32_t keySession=0; // immutable physical-key owner, set before task creation
  std::mutex frameMutex;
  uint16_t *frame = nullptr;
  std::string path, error, warning;
  Mapping mapping; // immutable after ready.store(release)
  ~Job();
};
std::shared_ptr<Job> start(const std::string &path,uint32_t keySession);
bool idle();
// Worker-only bounded configuration parser; key model has compile-time tests.
bool parseMapping(const std::string &json, Mapping &mapping,
                  std::string &error);
} // namespace core::fc
