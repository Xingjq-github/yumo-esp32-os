#pragma once
#include <cstdint>
#include <cstring>
#include <string>
namespace core::sd {
inline constexpr const char *Root = "/sdcard";
inline constexpr const char *Recordings = "/sdcard/recordings";
inline constexpr const char *Games = "/sdcard/nes";
inline constexpr const char *Screenshots = "/sdcard/screenshots";
inline constexpr const char *Patterns = "/sdcard/patterns";
inline constexpr const char *SpeedTests = "/sdcard/speedtest";
inline constexpr const char *Garden = "/sdcard/garden";
inline constexpr const char *PhotoCache = "/sdcard/.yumo-photo-cache-v1";
inline constexpr const char *PhotoFrames = "/sdcard/.yumo-photo-tiles-v1";
// Shared by the real SD service and host adapters; never accept arbitrary paths.
inline bool managedDirectory(const char *path) {
  return path && (!std::strcmp(path, Recordings) || !std::strcmp(path, Games) ||
                 !std::strcmp(path, Screenshots) || !std::strcmp(path, Patterns) ||
                 !std::strcmp(path, SpeedTests) || !std::strcmp(path, Garden) ||
                 !std::strcmp(path, PhotoCache) || !std::strcmp(path, PhotoFrames));
}
enum class State : uint8_t { Available, Ejecting, Ejected, Failed, Reading, Usb };
bool requestUsb(); // Close admission immediately; then use the normal eject fence.
bool claimUsb(); // Worker only, after successful eject. No VFS mount remains.
void cancelUsbRequest(); // No raw owner yet; leave a pending eject to finish normally.
void releaseUsb(bool clean); // Only after raw SD host and USB I/O have stopped.
bool usbExclusive();
// Captured at task submission, not when a delayed worker finally starts.
struct Access { uint32_t epoch=0; bool resume=false; };
Access requestAccess(bool resume=false);
State state();
bool requestEject(); // GUI-safe, immediate admission fence; never waits for I/O.
void pollEject(); // System worker only; no work unless eject is pending.
bool busy();
const char *ejectError();
enum class RestartState : uint8_t { Ready, Busy, Failed };
// Worker-only, after GUI/app drain. Preserve teardown errors if restart cancels.
RestartState prepareRestart();
// Every SD FILE/DIR must be opened and closed inside a Session. Recovery runs
// only in workers, before any handles are opened. GUI callers use recover=false:
// it never waits for a mount or sends a probe; refresh prepares the card instead.
class Session {
public:
  explicit Session(Access access, bool recover = true, bool drain = false);
  ~Session();
  Session(const Session &) = delete;
  Session &operator=(const Session &) = delete;
  explicit operator bool() const { return held_; }
  int error() const { return error_; }
  uint64_t generation() const { return generation_; }
  void reportError(int error);
  void release(); // Only after ALL associated files/directories are closed.
  bool claimMutation(); // Worker only. Exclusive rename/delete/copy session.
private:
  bool held_ = false;
  bool mutation_ = false;
  int error_ = 0;
  uint64_t generation_ = 0;
};
// Worker context with an active Session. Only creates the requested media dir.
bool ensure(const char *directory);
bool child(const std::string &path, const char *directory,
           const char *extension);
} // namespace core::sd
