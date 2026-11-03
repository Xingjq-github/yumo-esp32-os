#include "mp3_player.hpp"
#include "xiaozhi_service.hpp"
#include "chiptune_player.hpp"
#include "audio_recorder.hpp"
#include "audio_mgr.hpp"
#include "game_audio.hpp"
#include "dual_mic_input.hpp"
#include "esp_heap_caps.h"
#include "fc_emulator.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "power_mgr.hpp"
#include "recording_level.hpp"
#include "recording_denoise.hpp"
#include "recorder_playback.hpp"
#include "sd_media.hpp"
#include "wav_format.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <new>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
namespace core::recorder {
namespace {
std::atomic<bool> active{false};
struct File {
  sd::Session &card;
  explicit File(sd::Session &session) : card(session) {}
  FILE *p = nullptr;
  ~File() {
    if (p) {
      const bool failed = std::ferror(p);
      const int closed = std::fclose(p);
      if (failed || closed != 0) card.reportError(EIO);
    }
  }
};
struct Stream {
  bool opened = false;
  bool open(bool record) {
    if (opened)
      return true;
    return opened = sys::Audio::openStream(record, wav::Rate);
  }
  void close() {
    if (opened)
      sys::Audio::closeStream();
    opened = false;
  }
  ~Stream() { close(); }
};
bool checkpoint(FILE *file, uint32_t bytes) {
  const auto h = wav::header(bytes);
  return std::fseek(file, 0, SEEK_SET) == 0 &&
         std::fwrite(h.data(), 1, h.size(), file) == h.size() &&
         std::fflush(file) == 0 && fsync(fileno(file)) == 0 &&
         std::fseek(file, 0, SEEK_END) == 0;
}
bool create(Job &job, File &file, std::string &temporary) {
  if (!sd::ensure(sd::Recordings)) {
    file.card.reportError(errno);
    job.error = "SD卡不可写，请检查存储卡";
    return false;
  }
  timeval tv{};
  gettimeofday(&tv, nullptr);
  tm now{};
  localtime_r(&tv.tv_sec, &now);
  if (now.tm_year + 1900 < 2024) {
    job.error = "请先在设置中校准日期时间";
    return false;
  }
  char timestamp[40];
  std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H-%M-%S", &now);
  for (unsigned i = 0; i < 100; ++i) {
    char name[80];
    std::snprintf(name, sizeof(name), "%s_%03ld_%02u.wav", timestamp,
                  long(tv.tv_usec / 1000), i);
    job.path = std::string(sd::Recordings) + "/" + name;
    struct stat st{};
    if (stat(job.path.c_str(), &st) == 0)
      continue;
    if (errno != ENOENT) {
      // A failed read is not proof that a name is unused. Leave all existing
      // recordings untouched and stop before creating a temporary file.
      file.card.reportError(errno);
      break;
    }
    temporary = job.path + ".part";
    int fd = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_RDWR, 0664);
    if (fd < 0) {
      if (errno == EEXIST)
        continue;
      file.card.reportError(errno);
      break;
    }
    file.p = fdopen(fd, "w+b");
    if (!file.p) {
      ::close(fd);
      break;
    }
    if (checkpoint(file.p, 0))
      return true;
    file.card.reportError(EIO);
    break;
  }
  job.error = "创建录音失败，请检查SD空间和写保护";
  return false;
}
void play(Job &job,File &file,Stream &stream,uint32_t limit) {
  // Keep block buffers off the 8-KiB worker stack; destroy them on worker exit.
  // The Sonic handle is released on pause and on every playback mode change.
  std::unique_ptr<Playback> player(new(std::nothrow) Playback(limit));
  if(!player) {job.error="播放内存不足";return;}
  job.length.store(limit);
  uint32_t total=0;
  bool wasPaused=false;
  auto read=[&](uint32_t position,int16_t *samples,uint32_t bytes) {
    if(position>uint32_t(LONG_MAX-44))return false;
    if(std::fseek(file.p,44L+static_cast<long>(position),SEEK_SET)!=0 ||
       std::fread(samples,1,bytes,file.p)!=bytes) {file.card.reportError(EIO);return false;}
    return true;
  };
  while(!job.stop.load()&&!sys::Power::sleepPending()) {
    if(!sys::Power::audioMayRun()) {job.pause.store(true);job.scrub.store(0);}
    const int direction=job.scrub.load();
    if(job.pause.load()&&direction==0) {
      if(!wasPaused){stream.close();player->seek(total);job.peak.store(0);}
      wasPaused=true;
      job.state.store(State::Paused);job.wake.wait();continue;
    }
    wasPaused=false;
    sys::Power::WorkGuard work;
    if(!stream.open(false)) {job.error="音频设备忙或启动失败";break;}
    job.state.store(State::Playing);
    const auto block=player->next(direction,read);
    // A command arriving during SD/DSP work invalidates that block, including
    // buffered forward audio. Resume from the last position actually played.
    if(job.stop.load()||job.scrub.load()!=direction||(job.pause.load()&&direction==0)||
       !sys::Power::audioMayRun()||sys::Power::sleepPending()) {
      player->seek(total);continue;
    }
    if(block.status==Playback::Status::End)break;
    if(block.status!=Playback::Status::Audio) {
      job.error=block.status==Playback::Status::ReadError?"录音读取失败":"倍速音频处理失败";
      break;
    }
    auto samples=player->samples();
    if(block.frames&&!sys::Audio::writeStream(samples,block.frames*sizeof(int16_t))) {
      job.error="录音播放失败";break;
    }
    unsigned peak=0;
    for(unsigned i=0;i<block.frames;++i)
      peak=std::max(peak,unsigned(samples[i]<0?-int(samples[i]):int(samples[i])));
    total=player->position();job.bytes.store(total);job.peak.store(peak);
    // Blocking I2S writes supply playback pacing. Yield only while Sonic is
    // collecting its initial analysis window, avoiding an extra tick/block.
    if(!block.frames)vTaskDelay(1);
  }
  stream.close();job.peak.store(0);job.scrub.store(0);
  job.ok=job.error.empty();
}
void execute(Job &job) {
  // onHide cancels queued starts; only an already-open recording needs saving.
  if(job.stop.load() || sys::Power::sleepPending()) return;
  sd::Session card(job.sdAccess);
  if (!card) { job.error = "SD卡不可用，请插卡后重试"; return; }
  if(job.stop.load() || sys::Power::sleepPending()) return;
  File file(card); // Closed before Session is released, including pause/exit.
  Stream stream;
  RecordingDenoise denoise;
  std::string temporary;
  uint32_t limit = wav::MaxBytes;
  if (job.recording) {
    if(!denoise.open()) {job.error="录音降噪内存不足";return;}
    // A codec initialization failure must not create an empty saved recording.
    if (!stream.open(true)) {
      job.error = "音频设备忙或启动失败";
      return;
    }
    if(job.stop.load() || sys::Power::sleepPending()) return;
    if (!create(job, file, temporary))
      return;
  } else {
    if (!sd::child(job.path, sd::Recordings, ".wav")) {
      job.error = "录音路径无效";
      return;
    }
    if(job.stop.load() || sys::Power::sleepPending()) return;
    file.p = std::fopen(job.path.c_str(), "rb");
    if (!file.p) card.reportError(errno);
    uint8_t h[44];
    struct stat st{};
    if (!file.p || fstat(fileno(file.p), &st) != 0 ||
        std::fread(h, 1, 44, file.p) != 44 ||
        !wav::parse(h, st.st_size, limit)) {
      job.error = "录音损坏或不是16kHz单声道PCM WAV";
      return;
    }
    play(job,file,stream,limit);
    return;
  }
  static_assert(DualMicInput::MaxFrames == RecordingLevel::MaxSamples);
  int16_t samples[DualMicInput::MaxFrames * DualMicInput::Channels];
  constexpr uint32_t MonoBlockBytes = RecordingLevel::MaxSamples * sizeof(int16_t);
  DualMicInput microphones;
  RecordingLevel level;
  uint32_t total = 0, flushed = 0;
  job.length.store(0);
  bool wasPaused = false;
  auto flushTail = [&]() {
    const auto tail=denoise.flush(samples);
    if(!tail.ok) {job.error="录音降噪处理失败";return false;}
    const size_t count=std::min<size_t>(tail.frames,(limit-total)/sizeof(int16_t));
    if(count) {
      level.processDcBlocked(samples,count,denoise.voice());
      const size_t bytes=count*sizeof(int16_t);
      if(std::fwrite(samples,1,bytes,file.p)!=bytes) {card.reportError(EIO);job.error="SD写入失败";return false;}
      total+=bytes;job.bytes.store(total);
    }
    return true;
  };
  while (!job.stop.load() && !sys::Power::sleepPending()) {
    if (!sys::Power::audioMayRun()) {
      job.pause.store(true);
      job.scrub.store(0);
    }
    if (job.pause.load()) {
      stream.close();
      if (!wasPaused) {
        if(!flushTail())break;
        microphones.reset();
        level.reset();
        if(!denoise.reset()) {job.error="录音降噪重置失败";break;}
      }
      wasPaused=true;
      job.peak.store(0);
      job.state.store(State::Paused);
      if (flushed != total) {
        if (!checkpoint(file.p, total)) {
          card.reportError(EIO);
          job.error = "SD写入失败，临时录音已保留";
          break;
        }
        flushed = total;
      }
      job.wake.wait();
      continue;
    }
    wasPaused=false;
    sys::Power::WorkGuard work;
    if (!stream.open(true)) {
      job.error = "音频设备忙或启动失败";
      break;
    }
    job.state.store(State::Recording);
    // File size/time use MONO bytes; capture alone reads two I2S samples/frame.
    const uint32_t size = std::min<uint32_t>(MonoBlockBytes, limit - total);
    if (!size)
      break;
    uint32_t outputBytes=size;
    {
      if (!sys::Audio::readStream(samples, size * DualMicInput::Channels)) {
        job.error = "麦克风读取失败";
        break;
      }
      if (job.stop.load() || job.pause.load() || !sys::Power::audioMayRun())
        continue;
      const size_t frames = size / sizeof(samples[0]);
      microphones.process(samples, frames);
      const auto clean=denoise.process(samples,frames);
      if(!clean.ok) {job.error="录音降噪处理失败";break;}
      level.processDcBlocked(samples,clean.frames,denoise.voice());
      outputBytes=clean.frames*sizeof(int16_t);
      if (std::fwrite(samples, 1, outputBytes, file.p) != outputBytes) {
        card.reportError(EIO);
        job.error = "SD空间不足或写入失败";
        break;
      }
    }
    unsigned peak = 0;
    for (unsigned i = 0; i < outputBytes / 2; ++i)
      peak = std::max(
          peak, unsigned(samples[i] < 0 ? -int(samples[i]) : int(samples[i])));
    job.peak.store(peak);
    total += outputBytes;
    job.bytes.store(total);
    if (total - flushed >= wav::ByteRate) {
      if (!checkpoint(file.p, total)) {
        card.reportError(EIO);
        job.error = "SD写入失败，临时录音已保留";
        break;
      }
      flushed = total;
    }
    vTaskDelay(1);
  }
  stream.close();
  job.state.store(State::Saving);
  job.peak.store(0);
  if (job.recording) {
    if(job.error.empty())(void)flushTail();
    const bool saved = checkpoint(file.p, total);
    const int closed = std::fclose(file.p);
    file.p = nullptr;
    if (!saved || closed != 0) {
      card.reportError(EIO);
      job.error = "保存失败，.part文件已保留";
      return;
    }
    // ESP-IDF FatFS rename rejects an existing destination (FR_EXIST); keep
    // the .part on failure. Never unlink an existing WAV to make room.
    if (std::rename(temporary.c_str(), job.path.c_str()) != 0) {
      card.reportError(errno);
      job.error = "完成文件重命名失败，.part已保留";
      return;
    }
  }
  job.ok = job.error.empty();
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
      job->error = "录音内存不足";
    }
    job->wake.detach();
    job->scrub.store(0);
    job->state.store(State::Finished);
    job->done.store(true, std::memory_order_release);
  }
  active.store(false);
  vTaskDeleteWithCaps(nullptr);
}
} // namespace
std::shared_ptr<Job> start(bool record, const std::string &path) {
  if (sys::Power::sleepPending() || !core::mp3::idle() || !core::chip::idle() || !core::game_audio::idle() || !core::fc::idle() || !core::xiaozhi::idle() || active.exchange(true))
    return {};
  std::shared_ptr<Job> job;
  std::shared_ptr<Job> *holder = nullptr;
  try {
    job = std::make_shared<Job>(record);
    job->sdAccess=sd::requestAccess(!record);
    job->path = path;
    holder = new std::shared_ptr<Job>(job);
  } catch (const std::bad_alloc &) {
    active.store(false);
    return {};
  }
  if (xTaskCreateWithCaps(worker, "recorder", 8192, holder, 3, nullptr,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    delete holder;
    active.store(false);
    return {};
  }
  return job;
}
bool idle() { return !active.load(); }
} // namespace core::recorder
