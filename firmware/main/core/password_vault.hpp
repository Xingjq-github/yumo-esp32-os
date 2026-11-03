#pragma once
#include "ble_hid.hpp"
#include "vault_crypto.hpp"
#include "vault_pattern.hpp"
#include "vault_load_state.hpp"
#include "sd_media.hpp"
#include <atomic>
#include <memory>
namespace vault {
struct Entry {
  std::string id, title, username, password, note, category, target;
  bool favorite = false;
  ~Entry() {
    clear(username);
    clear(password);
    clear(note);
    clear(title);
    clear(target);
  }
};
struct Session {
  Key key;
  std::array<uint8_t, 16> id{}, salt{};
  uint32_t revision = 0;
  std::vector<Entry> entries;
  std::string url, ca, token, pinSalt, pinHash;
  bool patternAuth = false; // absent in legacy schema-2 config; never infer from digits
  ~Session() { clear(token); }
};
enum class Operation { Load, Setup, Unlock, Sync, SaveConfig, Type, Enter, VerifyPin, VerifyChangePin, ChangePin, VerifyAccess, WebUnlock };
struct Job {
  core::sd::Access sdAccess;
  explicit Job(Operation kind) : operation(kind) {}
  Operation operation;
  std::atomic<bool> cancelled{false}, done{false};
  std::atomic<unsigned> progress{0}, syncStage{0}; // 0=local, 1=SD notice, 2=SD attempt
  bool sdConfigUsed = false;
  bool ok = false, exists = false, blank = false, configWriteStarted = false;
  bool needsPatternSetup = false;
  std::string error, master, replacement, url;
  std::shared_ptr<const Session> input;
  std::shared_ptr<Session> output;
  size_t entry = 0;
  bool username = false;
  uintptr_t link = 0;
  uint32_t permit = 0;
  ~Job() { clear(master); clear(replacement); }
};
// On-demand single worker. No persistent tasks, LVGL pointers or app callbacks.
StartResult start(const std::shared_ptr<Job> &job);
bool idle();
void cancel(const std::shared_ptr<Job> &job);
} // namespace vault
