#pragma once
#include "fc_save_store.h"
#include "sd_media.hpp"
#include <array>
#include <atomic>
extern "C" {
#include "../../components/nofrendo/nes/checkpoint.h"
}
namespace core::fc {
struct Job;
enum class SaveAction { Browse, Save, Load };
enum class SaveStage { Idle, Pending, Complete };
struct Saves {
  std::atomic<SaveStage> stage{SaveStage::Idle};
  // GUI writes a request only in Idle, worker owns it until Complete. GUI
  // copies the response before returning to Idle; no filesystem work on GUI.
  std::atomic<bool> cancelled{false};
  int64_t deadline=0,autoDeadline=0; // monotonic deadlines, published with requests
  SaveAction action=SaveAction::Browse;
  unsigned slot=0;
  fc_save_slot_t expected{};
  uint64_t generation=0;
  sd::Access access;
  std::array<fc_save_slot_t,FC_SAVE_SLOTS> slots{};
  bool success=false;
  char message[128]{};
  std::array<uint8_t,32> rom{}; // immutable after Job::ready release
  std::atomic<bool> autoFailed{false}; // worker publishes; GUI reads in pause menu
  std::atomic<bool> autoBusy{false};
  checkpoint_profile_t autoProfile{}; // cached immutable cartridge identity
  uint8_t *autoSnapshot=nullptr; // Job-owned PSRAM; writer retains shared ownership
  sd::Access autoAccess;
  uint64_t autoFrames=0,autoTimestamp=0;
};
bool requestSave(Job &,SaveAction,unsigned slot=0,const fc_save_slot_t *expected=nullptr,uint64_t generation=0);
// NES worker only; called while paused after a complete emulated frame.
bool processSave(Job &,uint64_t &playedFrames);
// A detached writer owns Job/snapshot until IO ends, including after app exit.
void prepareAutoSave(Job &);
bool drainAutoSave(Job &);
bool autoSavesIdle();
void releaseAutoSave(Job &);
// NES owner only copies a frame-boundary snapshot and starts the low-priority IO.
void processAutoSave(Job &,uint64_t playedFrames);
} // namespace core::fc
