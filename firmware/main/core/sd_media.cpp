#include "sd_media.hpp"
#include "bsp/esp-bsp.h"
#include "sdmmc_cmd.h"
#include "power_mgr.hpp"
#include "storage_browser.hpp"
#include "audio_recorder.hpp"
#include "esp_timer.h"
#include <atomic>
#include <cerrno>
#include <cassert>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <strings.h>
#include <sys/stat.h>
namespace core::sd {
namespace {
std::mutex cardMutex;
unsigned users = 0;
bool mutation = false;
bool damaged = false;
bool teardownFailed = false; // Partial VFS teardown requires a reboot, not remount.
uint64_t cardGeneration = 1;
// Publish state and admission epoch together. Tasks queued before an eject can
// never remount the card after the UI has already said it can be removed.
constexpr uint32_t StateMask=7;
std::atomic<uint32_t> control{8}; // epoch 1, Available
enum class Failure { None, Timeout, Write, Card, Unmount, Pins };
std::atomic<Failure> failure{Failure::None};
std::atomic<bool> usbFence{false};
State stateOf(uint32_t value) { return static_cast<State>(value&StateMask); }
uint32_t withState(uint32_t value,State state) { return (value&~StateMask)|uint32_t(state); }
void failEject(uint32_t expected,Failure why) {
  failure.store(why);
  control.compare_exchange_strong(expected,withState(expected,State::Failed));
}

bool directory(const char *path) {
  struct stat st{};
  if (stat(path, &st) == 0)
    return S_ISDIR(st.st_mode);
  if (errno != ENOENT || mkdir(path, 0775) != 0)
    return false;
  return true;
}
} // namespace
State state() { return stateOf(control.load()); }
bool busy() { const auto s=state();return usbFence.load() || s==State::Ejecting || s==State::Reading; }
bool usbExclusive(){return usbFence.load();}
bool requestUsb() {
  if(usbFence.exchange(true))return false;
  if(requestEject())return true;
  usbFence.store(false);return false;
}
bool claimUsb() {
  const std::lock_guard<std::mutex> lock(cardMutex);
  auto value=control.load();
  if(!usbFence.load()||users||stateOf(value)!=State::Ejected||teardownFailed||damaged)return false;
  return control.compare_exchange_strong(value,withState(value,State::Usb));
}
void cancelUsbRequest() {
  // Called only when no raw owner was created. State/epoch still fence old
  // jobs, including an eject in progress or its original failure reason.
  if(state()!=State::Usb)usbFence.store(false);
}
void releaseUsb(bool clean) {
  const std::lock_guard<std::mutex> lock(cardMutex);
  auto value=control.load();uint32_t next=(value&~StateMask)+8;if(!next)next=8;
  ++cardGeneration;
  if(!clean){teardownFailed=true;failure.store(Failure::Unmount);}
  control.store(next|uint32_t(clean?State::Ejected:State::Failed));
  usbFence.store(!clean); // A failed raw host teardown cannot remount until reboot.
}
Access requestAccess(bool resume) {
  if(usbFence.load())return {};
  const auto value=control.load();
  if(stateOf(value)==State::Ejecting || stateOf(value)==State::Reading) return {};
  return {value&~StateMask,resume};
}
bool requestEject() {
  if(sys::Power::sleepPending()) return false;
  auto value=control.load();
  if(stateOf(value)==State::Usb)return false;
  if(stateOf(value)==State::Ejected || stateOf(value)==State::Ejecting) return true;
  if(stateOf(value)==State::Reading) return false;
  // Zero is reserved for requests refused while ejecting. Wrap safely.
  uint32_t next=(value&~StateMask)+8;
  if(!next) next=8;
  return control.compare_exchange_strong(value,next|uint32_t(State::Ejecting));
}
const char *ejectError() {
  switch(failure.load()) {
  case Failure::Timeout: return "SD任务未结束，请勿拔卡，稍后重试";
  case Failure::Write: return "SD保存失败，请勿拔卡，关机后检查";
  case Failure::Card: return "SD卡未就绪，请勿拔卡，检查后重试";
  case Failure::Unmount: return "SD卸载失败，请勿拔卡，关机后检查";
  case Failure::Pins: return "SD信号关闭失败，请关机后拔卡";
  default: return "SD弹出失败，请勿拔卡";
  }
}
void pollEject() {
  const auto value=control.load();
  if(stateOf(value)!=State::Ejecting) return;
  static uint32_t pending=0;
  static int64_t since=0;
  if(pending!=value) {pending=value;since=esp_timer_get_time();}
  if(esp_timer_get_time()-since>10000000) {failEject(value,Failure::Timeout);return;}
  std::unique_lock<std::mutex> lock(cardMutex,std::try_to_lock);
  if(!lock.owns_lock() || users || !core::storageIdle() || !core::recorder::idle()) return;
  if(control.load()!=value || sys::Power::sleepTerminal()) return;
  // A failed VFS teardown may have freed only part of its state. Do not turn
  // a retry with a null BSP handle into a false successful eject or remount.
  if(teardownFailed) {failEject(value,Failure::Unmount);return;}
  // Writers have flushed, fsynced, closed and reported errors before idle.
  if(damaged) {failEject(value,Failure::Write);return;}
  sys::Power::WorkGuard work;
  if(bsp_sdcard_get_handle()) {
    const auto ready=bsp_sdcard_ready();
    if(ready==ESP_ERR_NOT_FINISHED) return; // wait across polls, not a busy loop
    if(ready!=ESP_OK) {failEject(value,Failure::Card);return;}
    ++cardGeneration;
    if(bsp_sdcard_unmount()!=ESP_OK) {teardownFailed=true;failEject(value,Failure::Unmount);return;}
  }
  if(bsp_sdcard_quiesce()!=ESP_OK) {failEject(value,Failure::Pins);return;}
  failure.store(Failure::None);
  auto expected=value;
  control.compare_exchange_strong(expected,withState(value,State::Ejected));
}
Session::Session(Access access,bool recover,bool drain) {
  std::unique_lock<std::mutex> lock(cardMutex, std::defer_lock);
  if (recover) lock.lock();
  else if (!lock.try_lock()) { error_ = EBUSY; return; }
  // Deep-sleep teardown owns the BSP after all App workers have finished.
  if (sys::Power::sleepTerminal()) { error_ = ECANCELED; return; }
  if(teardownFailed) {error_=EIO;return;}
  if(mutation) {error_=EBUSY;return;}
  const auto value=control.load();
  const auto current=stateOf(value);
  const auto epoch=value&~StateMask;
  // Only already-submitted bookmark writes may drain after admission closes.
  const bool finishing=drain && current==State::Ejecting && access.epoch &&
      ((access.epoch+8==epoch) || (access.epoch==0xfffffff8u && epoch==8));
  if((usbFence.load()&&!finishing)||current==State::Usb){error_=EBUSY;return;}
  if(!access.epoch || (access.epoch!=epoch && !finishing) ||
     (current==State::Ejecting && !finishing) || current==State::Reading) {
    error_=EBUSY;return;
  }
  const bool resuming=current==State::Ejected || current==State::Failed;
  if(resuming && (!access.resume || !recover || users)) {error_=EBUSY;return;}
  if(resuming) {
    auto expected=value;
    if(!control.compare_exchange_strong(expected,withState(value,State::Reading))) {error_=EBUSY;return;}
  }
  // Restore Ejected/Failed on mount/read failure; publish Available only after
  // a real directory read succeeds. No GUI lock is held during this work.
  struct ResumeGuard {
    bool active;
    uint32_t original;
    ~ResumeGuard() {
      if(!active) return;
      if(stateOf(original)==State::Ejected && bsp_sdcard_quiesce()!=ESP_OK) {
        original=withState(original,State::Failed);failure.store(Failure::Pins);
      }
      control.store(original);
    }
  } resumeGuard{resuming,value};
  auto card = bsp_sdcard_get_handle();
  if(finishing) {
    if(!card || damaged) {error_=EIO;return;}
  } else if (users) {
    // Do not send raw SD commands or unmount beneath an open FILE/DIR.
    if (damaged || !card) { error_ = EBUSY; return; }
  } else if (recover) {
    sys::Power::WorkGuard work;
    if (card && (damaged || sdmmc_get_status(card) != ESP_OK)) {
      damaged = true;
      ++cardGeneration; // Invalidate queued bookmarks before changing media.
      const auto result = bsp_sdcard_unmount();
      if (result != ESP_OK) {
        error_=EIO;teardownFailed=true;failure.store(Failure::Unmount);
        resumeGuard.original=withState(value,State::Failed);
        auto expected=value;
        if(!resuming) control.compare_exchange_strong(expected,withState(value,State::Failed));
        return;
      }
      card = nullptr;
    }
    if (!card) {
      // One attempt per user operation; missing cards can retry next refresh.
      const auto result = bsp_sdcard_mount();
      if (result != ESP_OK) {
        error_ = result == ESP_ERR_NO_MEM ? ENOMEM : ENODEV;
        return;
      }
      ++cardGeneration;
    }
    damaged = false;
  } else if (!card || damaged) {
    error_ = ENODEV;
    return;
  }
  if(resuming) {
    // Even an empty card must successfully read and close its root directory.
    DIR *root=opendir(Root);
    int readError=0;
    if(!root) readError=errno?errno:EIO;
    else {
      errno=0;(void)readdir(root);readError=errno;
      if(closedir(root)!=0 && !readError) readError=errno?errno:EIO;
    }
    if(readError) {
      error_=readError;
      const auto unmounted=bsp_sdcard_unmount();
      if(unmounted!=ESP_OK) teardownFailed=true;
      const auto quiet=unmounted==ESP_OK?bsp_sdcard_quiesce():unmounted;
      if(quiet!=ESP_OK) {
        resumeGuard.original=withState(value,State::Failed);
        failure.store(teardownFailed?Failure::Unmount:Failure::Pins);
      }
      return;
    }
    failure.store(Failure::None);
    control.store(withState(value,State::Available));
    resumeGuard.active=false;
  }
  ++users;
  generation_ = cardGeneration;
  held_ = true;
}
Session::~Session() { release(); }
RestartState prepareRestart() {
  if(!sys::Power::sleepPending() || !sys::Power::sleepUiReady()) return RestartState::Failed;
  std::unique_lock<std::mutex> lock(cardMutex,std::try_to_lock);
  if(!lock.owns_lock() || users || busy()) return RestartState::Busy;
  const auto value=control.load();
  if(teardownFailed) {failEject(value,Failure::Unmount);return RestartState::Failed;}
  if(damaged) {failEject(value,Failure::Write);return RestartState::Failed;}
  if(!bsp_sdcard_get_handle()) return RestartState::Ready;
  const auto ready=bsp_sdcard_ready();
  if(ready==ESP_ERR_NOT_FINISHED) return RestartState::Busy;
  if(ready!=ESP_OK) {failEject(value,Failure::Card);return RestartState::Failed;}
  ++cardGeneration;
  if(bsp_sdcard_unmount()!=ESP_OK) {
    teardownFailed=true;failEject(value,Failure::Unmount);return RestartState::Failed;
  }
  return RestartState::Ready; // A cancelled restart remounts on the next explicit read.
}
void Session::release() {
  if (!held_) return;
  std::lock_guard<std::mutex> lock(cardMutex);
  assert(users);
  if(mutation_) {mutation=false;mutation_=false;}
  --users;
  held_ = false;
}
bool Session::claimMutation() {
  std::lock_guard<std::mutex> lock(cardMutex);
  if(!held_ || users!=1 || mutation || state()!=State::Available || usbFence.load())return false;
  mutation=mutation_=true;return true;
}
void Session::reportError(int error) {
  if (!held_ || (error != EIO && error != ENODEV && error != ENXIO &&
                 error != ETIMEDOUT)) return;
  std::lock_guard<std::mutex> lock(cardMutex);
  damaged = true; // Next worker recovers after the last handle closes.
}
bool ensure(const char *path) {
  if (!managedDirectory(path)) { errno = EINVAL; return false; }
  struct stat st{};
  return stat(Root, &st) == 0 && S_ISDIR(st.st_mode) && directory(path);
}
bool child(const std::string &path, const char *directory,
           const char *extension) {
  const std::string prefix = std::string(directory) + "/";
  if (path.rfind(prefix, 0) != 0 || path.size() > 255 ||
      path.find('\0') != std::string::npos)
    return false;
  const auto name = path.substr(prefix.size());
  if (name.empty() || name == "." || name == ".." ||
      name.find_first_of("/\\") != std::string::npos)
    return false;
  const size_t len = std::strlen(extension);
  return name.size() > len &&
         strcasecmp(name.c_str() + name.size() - len, extension) == 0;
}
} // namespace core::sd
