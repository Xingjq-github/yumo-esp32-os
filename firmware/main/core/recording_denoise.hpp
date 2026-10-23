#pragma once
#include "recorder_denoise.h"
#include "recording_latency.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
namespace core::recorder {
// Remove Speex's one-frame startup latency and drain its tail on pause/stop,
// so saved PCM contains the same number of samples as captured PCM.
class RecordingDenoise {
public:
  struct Output {bool ok;size_t frames;};
  RecordingDenoise()=default;
  RecordingDenoise(const RecordingDenoise &)=delete;
  RecordingDenoise &operator=(const RecordingDenoise &)=delete;
  ~RecordingDenoise() {recorder_denoise_destroy(context_);}
  bool open() {return context_||(context_=recorder_denoise_create());}
  bool reset() {latency_.reset();return recorder_denoise_reset(context_);}
  bool voice() const {return recorder_denoise_voice(context_);}
  Output process(int16_t *pcm,size_t frames) {
    if(!pcm||!frames||frames>512||frames%RECORDER_NS_FRAME)return {false,0};
    size_t written=0;
    for(size_t offset=0;offset<frames;offset+=RECORDER_NS_FRAME) {
      if(!recorder_denoise_run(context_,pcm+offset))return {false,0};
      if(!latency_.acceptFrame())continue;
      if(written!=offset)std::copy_n(pcm+offset,RECORDER_NS_FRAME,pcm+written);
      written+=RECORDER_NS_FRAME;
    }
    return {true,written};
  }
  Output flush(int16_t *pcm) {
    if(!latency_.flush())return {true,0};
    std::fill_n(pcm,RECORDER_NS_FRAME,int16_t(0));
    if(!recorder_denoise_run(context_,pcm))return {false,0};
    return {true,RECORDER_NS_FRAME};
  }
private:
  RecorderDenoise *context_=nullptr;
  RecordingLatency latency_;
};
} // namespace core::recorder
