#pragma once
#include "sd_media.hpp"
#include "worker_wake.hpp"
#include <atomic>
#include <memory>
#include <string>
namespace core::recorder {
enum class State { Opening, Recording, Playing, Paused, Saving, Finished };
struct Job {
  const bool recording;
  sd::Access sdAccess;
  explicit Job(bool record) : recording(record) {}
  std::atomic<bool> stop{false}, pause{false}, done{false};
  WorkerWake wake;
  void requestStop() { stop.store(true);wake.notify(); }
  void setPause(bool value) { pause.store(value);wake.notify(); }
  void setScrub(int value) { scrub.store(value);wake.notify(); }
  std::atomic<State> state{State::Opening};
  std::atomic<uint32_t> bytes{0};
  std::atomic<unsigned> peak{0};
  std::atomic<int> scrub{0}; // -1 audible reverse 2x, +1 pitch-preserving 2x, 0 normal
  std::atomic<uint32_t> length{0}; // playback source bytes; publish after WAV validation
  std::string path, error; // read on GUI only AFTER done(acquire)
  bool ok = false;
};
std::shared_ptr<Job> start(bool record, const std::string &path = {});
bool idle();
} // namespace core::recorder
