#include "mp3_player.hpp"
#include "xiaozhi_service.hpp"
#include "chiptune_player.hpp"
#include "fc_emulator.hpp"
#include "fc_autosave.hpp"
#include "fc_timing.hpp"
#include "fc_controls.hpp"
#include "button_mgr.hpp"
#include "audio_mgr.hpp"
#include "audio_recorder.hpp"
#include "game_audio.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "power_mgr.hpp"
#include "sd_media.hpp"
#include "vault_json.hpp"
#include "vault_sha256.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <new>
#include <sys/stat.h>
extern "C" {
#include "fc_port.h"
#include "nes/nes.h"
}
// Nofrendo's C-only utility macros must not alter C++ standard headers/code.
#undef malloc
#undef calloc
#undef MIN
#undef MAX
namespace core::fc {
namespace {
std::atomic<bool> active{false};
constexpr const char *KeysPath = "/sdcard/config/nes-keys.json";
struct Buffer {
  uint8_t *data = nullptr;
  ~Buffer() { heap_caps_free(data); }
  bool allocate(size_t size) {
    data = static_cast<uint8_t *>(
        heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return data;
  }
};
struct Engine {
  bool initialized = false, audio = false, audioTried = false;
  ~Engine() {
    if (audio)
      sys::Audio::closeStream();
    if (initialized)
      nes_shutdown();
  }
};
void loadMapping(Job &job, sd::Session &card) {
  FILE *file = std::fopen(KeysPath, "rb");
  if (!file) {
    card.reportError(errno);
    return; // Missing optional config uses documented defaults.
  }
  char bytes[2049];
  const size_t length = std::fread(bytes, 1, sizeof(bytes), file);
  const bool error = std::ferror(file);
  std::fclose(file);
  if (error ||
      !parseMapping(std::string(bytes, length), job.mapping, job.warning)) {
    if (error) card.reportError(EIO);
    job.mapping = Mapping{};
    job.warning = "按键配置无效，已使用默认按键";
  }
}
std::string romError(ines_error_t error,const ines_info_t &info) {
  char message[80];
  if(error==INES_MAPPER) {
    std::snprintf(message,sizeof(message),"不支持Mapper %u",info.mapper);return message;
  }
  if(error==INES_SUBMAPPER) {
    std::snprintf(message,sizeof(message),"不支持Mapper %u.%u",info.mapper,info.submapper);return message;
  }
  switch(error) {
  case INES_SIZE:return "游戏数据超过2MB";
  case INES_TRUNCATED:return "ROM不完整，请重新复制";
  case INES_CONSOLE:return "不支持街机版ROM";
  case INES_RAM:return "不支持此ROM的扩展内存";
  case INES_TIMING:return "不支持此ROM的制式";
  case INES_LAYOUT:return "不支持此NES2扩展格式";
  default:return "ROM头无效，需原始NES文件";
  }
}
void execute(Job &job) {
  if(job.stop.load() || sys::Power::sleepPending()) return;
  if (!sd::child(job.path, sd::Games, ".nes")) {
    job.error = "游戏路径无效";
    return;
  }
  Buffer rom, video;
  sd::Session card(job.sdAccess);
  if (!card) { job.error = "SD卡不可用，请插卡后刷新"; return; }
  if(job.stop.load() || sys::Power::sleepPending()) return;
  std::unique_ptr<FILE, decltype(&std::fclose)> owner(
      std::fopen(job.path.c_str(), "rb"), &std::fclose);
  FILE *file = owner.get();
  struct stat st{};
  uint8_t header[16]{};
  if (!file) {
    card.reportError(errno);
    job.error = "无法打开游戏，请检查SD卡";
    return;
  }
  if(fstat(fileno(file),&st)!=0) {
    card.reportError(errno);job.error="ROM文件读取失败";return;
  }
  if(std::fread(header,1,16,file)!=16) {
    if (std::ferror(file)) card.reportError(EIO);
    job.error="ROM头读取失败";return;
  }
  ines_info_t romInfo{};
  const auto format=ines_parse(header,size_t(st.st_size),&romInfo);
  if(format!=INES_OK) {job.error=romError(format,romInfo);return;}
  if(!rom.allocate(st.st_size)) {
    job.error="游戏内存不足";
    return;
  }
  std::memcpy(rom.data, header, 16);
  size_t position = 16;
  while (position < size_t(st.st_size) && !job.stop.load() &&
         !sys::Power::sleepPending()) {
    const size_t count = std::min<size_t>(4096, st.st_size - position);
    if (std::fread(rom.data + position, 1, count, file) != count) {
      card.reportError(EIO);
      break;
    }
    position += count;
    vTaskDelay(1);
  }
  owner.reset();
  if (position != size_t(st.st_size)) {
    job.error = job.stop.load() ? "已取消" : "游戏读取中断";
    return;
  }
  // Stable SHA-256 identity, including on a PSRAM task stack. Hash the original
  // bytes before the emulator sees them, without accelerator/DMA constraints.
  vault_sha256_state hash{};vault_sha256_init(&hash);
  bool hashed=true;
  for(size_t offset=0;offset<size_t(st.st_size) && hashed;) {
    if(job.stop.load() || sys::Power::sleepPending())return;
    const size_t count=std::min<size_t>(4096,size_t(st.st_size)-offset);
    hashed=vault_sha256_process(&hash,rom.data+offset,count)==0;
    offset+=count;vTaskDelay(1);
  }
  if(!hashed || vault_sha256_done(&hash,job.saves.rom.data())!=0) {
    job.error="游戏校验失败，请重试";return;
  }
  loadMapping(job, card);
  card.release(); // ROM and key mapping are in RAM; gameplay owns no SD handle.
  if (job.stop.load() || sys::Power::sleepPending())
    return;
  if (!video.allocate(NES_SCREEN_PITCH * NES_SCREEN_HEIGHT)) {
    job.error = "画面内存不足";
    return;
  }
  job.frame = static_cast<uint16_t *>(
      heap_caps_calloc(Pixels, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!job.frame) {
    job.error = "显示内存不足";
    return;
  }
  Engine engine;
  nes_t *nes = nes_init(SYS_DETECT, 22050, false, nullptr);
  if (!nes) {
    job.error = "模拟器初始化失败";
    return;
  }
  engine.initialized = true;
  auto cart = rom_loadmem(rom.data, st.st_size);
  if (!cart || nes_insertcart(cart) != 0) {
    job.error = "游戏加载失败";
    return;
  }
  nes_setvidbuf(video.data);
  input_connect(0, NES_JOYPAD);
  uint16_t colors[256];
  for (unsigned i = 0; i < 256; ++i)
    colors[i] = fc_color(i);
  job.refreshRate=nes->refresh_rate;
  struct AutoRelease {Job &job;~AutoRelease(){releaseAutoSave(job);}} autoRelease{job};
  prepareAutoSave(job);
  uint64_t playedFrames=0;
  AutoSaveClock autoSave;
  bool restoredFrame=false;
  job.ready.store(true, std::memory_order_release);
  FrameTiming timing(nes->refresh_rate);
  timing.reset(esp_timer_get_time());
  while (!job.stop.load() && !sys::Power::sleepPending()) {
    if (!sys::Power::isScreenOn())
      job.paused.store(true);
    autoSave.update(esp_timer_get_time(),!job.paused.load());
    if (job.paused.load()) {
      sys::Buttons::gameRunning(job.keySession,false);
      job.buttons.store(0);
      job.pressed.store(0);
      if (engine.audio) {
        sys::Audio::closeStream();
        engine.audio = false;
      }
      engine.audioTried = false;
      const bool loaded=processSave(job,playedFrames);
      if(loaded)autoSave.reset(esp_timer_get_time());
      restoredFrame=loaded || restoredFrame;
      if(restoredFrame) {
        const std::unique_lock<std::mutex> lock(job.frameMutex,std::try_to_lock);
        if(lock.owns_lock()) {
          scaleFrame(job.frame,video.data,NES_SCREEN_PITCH,NES_SCREEN_OVERDRAW,colors);
          job.framePending.store(true,std::memory_order_release);
          restoredFrame=false;
        }
      }
      // Retry a contended restored-frame mailbox promptly; otherwise sleep
      // until a menu command, with a bounded global-power fallback.
      job.wake.wait(restoredFrame?20:250);
      timing.reset(esp_timer_get_time());
      continue;
    }
    sys::Power::WorkGuard work;
    if (!engine.audioTried) {
      engine.audio = sys::Audio::openStream(false, 22050);
      engine.audioTried = true;
      // Codec initialization/resume is outside the emulated frame deadline.
      timing.reset(esp_timer_get_time());
    }
    sys::Buttons::gameRunning(job.keySession,true);
    const uint8_t pad=job.buttons.load() | job.pressed.exchange(0) |
                      sys::Buttons::takeGameKeys(job.keySession);
    input_update(0, controls::neutralOpposites(pad));
    // Keep 50/60Hz CPU/APU/input. Avoid drawing while the GUI still owns a
    // pending frame or when late; the fallback refresh prevents a frozen view.
    const bool draw = timing.draw(esp_timer_get_time(),
                                 !job.framePending.load(std::memory_order_acquire));
    nes_emulate(draw);
    restoredFrame=false; // live frames supersede the paused preview
    if (draw) {
      const std::unique_lock<std::mutex> lock(job.frameMutex, std::try_to_lock);
      if (lock.owns_lock()) {
        scaleFrame(job.frame,video.data,NES_SCREEN_PITCH,NES_SCREEN_OVERDRAW,colors);
        job.framePending.store(true,std::memory_order_release);
        timing.presented(esp_timer_get_time());
      }
    }
    if (engine.audio &&
        !sys::Audio::writeStream(nes->apu->buffer,
                                 nes->apu->samples_per_frame * 2)) {
      sys::Audio::closeStream();
      engine.audio = false;
    }
    ++job.frames;
    ++playedFrames;
    autoSave.update(esp_timer_get_time(),true);
    if(autoSave.due() && !job.paused.load() && !job.stop.load() &&
       job.saves.stage.load(std::memory_order_acquire)==SaveStage::Idle &&
       sys::Power::isScreenOn() && !sys::Power::sleepPending()) {
      // Only copy the stable frame. CRC/SD work runs on the detached writer;
      // keep I2S alive and keep the original absolute frame deadline.
      processAutoSave(job,playedFrames);
      autoSave.reset(esp_timer_get_time());
    }
    const int64_t now = esp_timer_get_time();
    const auto ticks=timing.waitTicks(now,timing.finish(now),1000000/configTICK_RATE_HZ);
    if(ticks) vTaskDelay(ticks);
    else taskYIELD(); // no extra 1ms on EVERY frame already paced by I2S

  }
  job.buttons.store(0);
  job.pressed.store(0);
  sys::Buttons::gameRunning(job.keySession,false);
}
void worker(void *arg) {
  {
    auto holder = static_cast<std::shared_ptr<Job> *>(arg);
    auto job = *holder;
    delete holder;
    job->wake.attach();
    try {
      execute(*job);
    } catch (const std::bad_alloc &) {
      job->error = "游戏内存不足";
    }
    job->wake.detach();
    job->done.store(true, std::memory_order_release);
  }
  active.store(false);
  vTaskDeleteWithCaps(nullptr);
}
} // namespace
Job::~Job() { heap_caps_free(saves.autoSnapshot);heap_caps_free(frame); }
bool parseMapping(const std::string &json, Mapping &mapping,
                  std::string &error) {
  if (json.empty() || json.size() > 2048) {
    error = "配置长度错误";
    return false;
  }
  const size_t bom = json.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
  vault::Json doc(reinterpret_cast<const uint8_t *>(json.data() + bom),
                  json.size() - bom);
  if (!doc.value || !cJSON_IsObject(doc.value))
    return false;
  const auto schema = cJSON_GetObjectItemCaseSensitive(doc.value, "schema");
  const auto keys = cJSON_GetObjectItemCaseSensitive(doc.value, "keys");
  if (cJSON_GetArraySize(doc.value) != 2 || !cJSON_IsNumber(schema) ||
      schema->valuedouble != 1 || !cJSON_IsObject(keys))
    return false;
  static const char *names[KeyCount] = {"a",     "b",    "select", "start",
                                        "up",    "down", "left",   "right",
                                        "pause", "exit"};
  Mapping result;
  std::array<bool, KeyCount> seen{};
  for (auto entry = keys->child; entry; entry = entry->next) {
    unsigned i = 0;
    for (; i < KeyCount; ++i)
      if (entry->string && !std::strcmp(names[i], entry->string))
        break;
    if (i == KeyCount || seen[i] || !cJSON_IsString(entry) ||
        !entry->valuestring)
      return false;
    const auto key = keyUsage(entry->valuestring);
    if (!key)
      return false;
    seen[i] = true;
    result.usage[i] = key;
  }
  for (unsigned i = 0; i < KeyCount; ++i)
    for (unsigned j = 0; j < i; ++j)
      if (result.usage[i] == result.usage[j])
        return false;
  mapping = result;
  return true;
}
std::shared_ptr<Job> start(const std::string &path,uint32_t keySession) {
  if (sys::Power::sleepPending() || !core::mp3::idle() || !core::chip::idle() || !core::game_audio::idle() || !core::recorder::idle() ||
      !autoSavesIdle() || !core::xiaozhi::idle() || active.exchange(true))
    return {};
  std::shared_ptr<Job> job;
  std::shared_ptr<Job> *holder = nullptr;
  try {
    job = std::make_shared<Job>();
    job->path = path;
    job->sdAccess=sd::requestAccess(true);
    job->keySession = keySession;
    holder = new std::shared_ptr<Job>(job);
  } catch (const std::bad_alloc &) {
    active.store(false);
    return {};
  }
  if (xTaskCreateWithCaps(worker, "nes", 12288, holder, 2, nullptr,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    delete holder;
    active.store(false);
    return {};
  }
  return job;
}
bool idle() { return !active.load() && autoSavesIdle(); }
} // namespace core::fc
