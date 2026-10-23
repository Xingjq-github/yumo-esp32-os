#pragma once
#include "recorder_transport.hpp"
#include "esp_ae_sonic.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>

namespace core::recorder {
// One worker owns the file cursor and this bounded block renderer. Output stays
// at 16 kHz. Sonic owns a small buffer only while pitch-preserving 2x is active.
class Playback {
public:
  enum class Status { Audio, End, ReadError, EffectError };
  struct Block { Status status; unsigned frames; };
  explicit Playback(uint32_t length) : length_(length & ~1U) {}
  ~Playback() { close(); }
  Playback(const Playback &) = delete;
  Playback &operator=(const Playback &) = delete;
  uint32_t position() const { return position_; }
  int16_t *samples() { return direction_>0?fastOutput_.get():input_; }
  void seek(uint32_t position) {
    close();position_=std::min(position & ~1U,length_);direction_=2;
    pending_=offset_=padding_=0;reverse_.reset();
  }

  template<class Read> Block next(int direction,Read read) {
    direction=direction<0?-1:direction>0?1:0;
    if(direction!=direction_) {
      close();direction_=direction;origin_=feed_=position_;emitted_=0;
      pending_=offset_=padding_=0;reverse_.reset();
    }
    if(direction<=0) {
      const uint32_t bytes=direction<0?rewindBlockBytes(position_):
        std::min<uint32_t>(OutputFrames*2,length_-position_);
      if(!bytes)return {Status::End,0};
      const uint32_t start=direction<0?position_-bytes:position_;
      if(!read(start,input_,bytes))return {Status::ReadError,0};
      unsigned frames=bytes/2;
      if(direction<0) {
        std::reverse(input_,input_+frames);
        frames=reverse_.process(input_,frames);
      }
      position_=direction<0?start:position_+bytes;
      return {Status::Audio,frames};
    }

    // Round one odd source sample up, and never play the zero padding itself.
    const uint32_t target=(length_-origin_+3U)/4U;
    if(emitted_>=target)return {Status::End,0};
    if(!open())return {Status::EffectError,0};
    if(!pending_) {
      offset_=0;
      if(feed_<length_) {
        const uint32_t bytes=std::min<uint32_t>(InputFrames*2,length_-feed_);
        if(!read(feed_,input_,bytes))return {Status::ReadError,0};
        pending_=bytes/2;feed_+=bytes;isPadding_=false;
      } else {
        // The SDK exposes no flush call. Feed bounded silence to release its
        // final analysis window, trimming output to the real source duration.
        if(padding_>=MaxPadding)return {Status::EffectError,0};
        pending_=std::min<unsigned>(InputFrames,MaxPadding-padding_);
        std::fill_n(input_,pending_,int16_t(0));isPadding_=true;
      }
    }
    esp_ae_sonic_in_data_t in{input_+offset_,pending_,0};
    esp_ae_sonic_out_data_t out{fastOutput_.get(),std::min<uint32_t>(OutputFrames,target-emitted_),0};
    if(esp_ae_sonic_process(sonic_,&in,&out)!=ESP_AE_ERR_OK ||
       in.consume_num>pending_ || out.out_num>out.needed_num ||
       (!in.consume_num&&!out.out_num))return {Status::EffectError,0};
    pending_-=in.consume_num;offset_+=in.consume_num;
    if(isPadding_)padding_+=in.consume_num;
    emitted_+=out.out_num;
    position_=uint32_t(std::min<uint64_t>(length_,uint64_t(origin_)+uint64_t(emitted_)*4));
    return {Status::Audio,out.out_num};
  }
private:
  static constexpr unsigned InputFrames=1024,OutputFrames=512,MaxPadding=8192;
  uint32_t length_=0,position_=0,origin_=0,feed_=0,emitted_=0;
  unsigned pending_=0,offset_=0,padding_=0;
  int direction_=2;
  bool isPadding_=false;
  // Normal/reverse playback transforms this same 2-KiB buffer in place.
  // Only Sonic needs a separate output buffer; release it when leaving 2x.
  int16_t input_[InputFrames]{};
  std::unique_ptr<int16_t[]> fastOutput_;
  RewindSpeed reverse_;
  esp_ae_sonic_handle_t sonic_=nullptr;
  void close() { if(sonic_)esp_ae_sonic_close(sonic_);sonic_=nullptr;fastOutput_.reset(); }
  bool open() {
    if(sonic_)return true;
    fastOutput_.reset(new(std::nothrow) int16_t[OutputFrames]);
    if(!fastOutput_)return false;
    esp_ae_sonic_cfg_t cfg{wav::Rate,1,16};
    if(esp_ae_sonic_open(&cfg,&sonic_)!=ESP_AE_ERR_OK || !sonic_) {close();return false;}
    if(esp_ae_sonic_set_speed(sonic_,2.0f)!=ESP_AE_ERR_OK ||
       esp_ae_sonic_set_pitch(sonic_,1.0f)!=ESP_AE_ERR_OK) {close();return false;}
    return true;
  }
};
} // namespace core::recorder
