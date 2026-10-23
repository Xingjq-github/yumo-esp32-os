// Real block renderer and worker playback loop. The SDK Sonic ABI and hardware
// are fakes here: this tests buffering/control/lifetime, not vendor DSP quality.
#include "core/recorder_playback.hpp"
#include "core/audio_recorder.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <climits>
#include <cerrno>

static unsigned checks=0,opens=0,closes=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct FakeSonic {
  std::deque<int16_t> in,out;
  unsigned phase=0;
  float speed=1,pitch=1;
};
static unsigned consumeLimit=37,outputLimit=53,analysisFrames=256;
static bool failOpen=false,failSetting=false,failProcess=false,noOutput=false,noProgress=false;
extern "C" esp_ae_err_t esp_ae_sonic_open(esp_ae_sonic_cfg_t *cfg,esp_ae_sonic_handle_t *handle) {
  CHECK(cfg->sample_rate==16000&&cfg->channel==1&&cfg->bits_per_sample==16);
  *handle=nullptr;if(failOpen)return static_cast<esp_ae_err_t>(-1);
  *handle=new FakeSonic;++opens;return ESP_AE_ERR_OK;
}
extern "C" void esp_ae_sonic_close(esp_ae_sonic_handle_t handle) {CHECK(handle);delete static_cast<FakeSonic *>(handle);++closes;}
extern "C" esp_ae_err_t esp_ae_sonic_set_speed(esp_ae_sonic_handle_t handle,float value) {
  CHECK(value==2.0f);static_cast<FakeSonic *>(handle)->speed=value;
  return failSetting?static_cast<esp_ae_err_t>(-1):ESP_AE_ERR_OK;
}
extern "C" esp_ae_err_t esp_ae_sonic_set_pitch(esp_ae_sonic_handle_t handle,float value) {
  CHECK(value==1.0f);static_cast<FakeSonic *>(handle)->pitch=value;return ESP_AE_ERR_OK;
}
extern "C" esp_ae_err_t esp_ae_sonic_process(esp_ae_sonic_handle_t handle,esp_ae_sonic_in_data_t *in,esp_ae_sonic_out_data_t *out) {
  auto &s=*static_cast<FakeSonic *>(handle);CHECK(s.speed==2&&s.pitch==1);
  CHECK(in->samples&&out->samples&&in->num&&out->needed_num);
  in->consume_num=out->out_num=0;
  if(failProcess)return static_cast<esp_ae_err_t>(-1);
  if(noProgress)return ESP_AE_ERR_OK;
  if(s.out.empty()) {
    in->consume_num=std::min(in->num,consumeLimit);
    auto pcm=static_cast<const int16_t *>(in->samples);
    for(unsigned i=0;i<in->consume_num;++i)s.in.push_back(pcm[i]);
    // Deliberate input latency and partial consumption, like the documented
    // streaming ABI. Sample selection is a fake, not a pitch algorithm.
    while(s.in.size()>=analysisFrames) {
      for(unsigned i=0;i<analysisFrames;++i) {
        const auto value=s.in.front();s.in.pop_front();
        if((s.phase++&1)==0&&!noOutput)s.out.push_back(value);
      }
    }
  }
  auto dest=static_cast<int16_t *>(out->samples);
  while(!s.out.empty()&&out->out_num<std::min(out->needed_num,outputLimit)) {
    dest[out->out_num++]=s.out.front();s.out.pop_front();
  }
  return ESP_AE_ERR_OK;
}

using core::recorder::Playback;
static std::vector<int16_t> signal(unsigned frames) {
  std::vector<int16_t> pcm(frames);
  for(unsigned i=0;i<frames;++i)pcm[i]=int16_t(12000*std::sin(i*2*3.141592653589793*500/16000));
  return pcm;
}
static auto reader(const std::vector<int16_t> &pcm) {
  return [&pcm](uint32_t offset,int16_t *out,uint32_t bytes) {
    CHECK(!(offset&1)&&!(bytes&1)&&uint64_t(offset)+bytes<=pcm.size()*2);
    std::copy_n(pcm.data()+offset/2,bytes/2,out);return true;
  };
}
static std::vector<int16_t> render(Playback &player,int direction,const std::vector<int16_t> &pcm) {
  std::vector<int16_t> output;unsigned iterations=0;uint32_t old=player.position();
  for(;;) {
    CHECK(++iterations<100000);auto result=player.next(direction,reader(pcm));
    if(result.status==Playback::Status::End)break;
    CHECK(result.status==Playback::Status::Audio&&result.frames<=512);
    CHECK(direction<0?player.position()<=old:player.position()>=old);old=player.position();
    output.insert(output.end(),player.samples(),player.samples()+result.frames);
  }
  CHECK(player.position()==(direction<0?0:pcm.size()*2));return output;
}

namespace fake {
bool audio=true,sleep=false,opening=true,writing=true;
unsigned streamOpens=0,streamCloses=0,writes=0,delays=0;
std::vector<int16_t> output;
std::function<void()> onWrite,onDelay;
}
namespace sys {
struct Power {
  struct WorkGuard {};
  static bool sleepPending(){return fake::sleep;}
  static bool audioMayRun(){return fake::audio;}
};
struct Audio {static bool writeStream(void *data,int bytes) {
  CHECK(bytes>0&&bytes<=1024&&!(bytes&1));if(!fake::writing)return false;
  auto pcm=static_cast<int16_t *>(data);fake::output.insert(fake::output.end(),pcm,pcm+bytes/2);
  ++fake::writes;if(fake::onWrite)fake::onWrite();return true;
}};
}
#define pdMS_TO_TICKS(x) (x)
static void vTaskDelay(unsigned) {CHECK(++fake::delays<10000);if(fake::onDelay)fake::onDelay();}
namespace core {
void WorkerWake::notify() {}
void WorkerWake::wait(uint32_t ms) {CHECK(ms==250);vTaskDelay(ms);}
}
namespace core::recorder {
struct File {FILE *p=nullptr;struct Card {void reportError(int){}} card;};
struct Stream {
  bool opened=false;
  bool open(bool record) {CHECK(!record);if(!fake::opening)return false;if(!opened){opened=true;++fake::streamOpens;}return true;}
  void close(){if(opened){opened=false;++fake::streamCloses;}}
};
#include "audio_play.inc"
}

static void worker(const std::vector<int16_t> &pcm,core::recorder::Job &job) {
  fake::output.clear();fake::writes=fake::delays=0;
  auto fp=std::tmpfile();CHECK(fp);
  const auto header=core::wav::header(pcm.size()*2);
  CHECK(std::fwrite(header.data(),1,header.size(),fp)==header.size());
  CHECK(std::fwrite(pcm.data(),2,pcm.size(),fp)==pcm.size());std::fflush(fp);
  core::recorder::File file{fp};core::recorder::Stream stream;
  core::recorder::play(job,file,stream,pcm.size()*2);
  CHECK(!stream.opened);std::fclose(fp);CHECK(opens==closes);
  fake::onWrite={};fake::onDelay={};fake::audio=fake::opening=fake::writing=true;fake::sleep=false;
}

int main() {
  CHECK(sizeof(Playback)<2304);
  for(unsigned count:{0U,1U,2U,3U,63U,511U,512U,513U,1023U,1024U,1025U,4097U,16000U}) {
    const auto pcm=signal(count);
    {Playback p(count*2);CHECK(render(p,0,pcm)==pcm);}
    {Playback p(count*2);p.seek(count*2);const auto output=render(p,-1,pcm);CHECK(output.size()==count/2);
      // Independent chronological FIR reference, including cross-block history.
      constexpr int weights[]={-1024,0,9216,16384,9216,0,-1024};
      for(unsigned i=0;i<output.size();++i) {
        const int last=int(i*2+1);int32_t mixed=0;
        for(int k=0;k<7;++k){const int at=last-6+k;if(at>=0)mixed+=int32_t(pcm[count-1-at])*weights[k];}
        CHECK(output[i]==std::clamp(mixed/32768,-32768,32767));
      }
    }
    for(unsigned chunk:{1U,37U,512U,1024U}) {
      consumeLimit=chunk;Playback p(count*2);const auto out=render(p,1,pcm);
      CHECK(out.size()==(count+1)/2);for(unsigned i=0;i<out.size();++i)CHECK(out[i]==pcm[i*2]);
    }
    CHECK(opens==closes);
  }
  consumeLimit=37;const auto pcm=signal(16000);
  for(unsigned i=0;i<300;++i) {
    Playback p(pcm.size()*2);p.seek(10000);
    for(unsigned n=0;n<20;++n)CHECK(p.next(1,reader(pcm)).status==Playback::Status::Audio);
    const auto at=p.position();CHECK(opens==closes+1);
    CHECK(p.next(-1,reader(pcm)).status==Playback::Status::Audio);CHECK(opens==closes&&p.position()<at);
    const auto normal=p.position();auto r=p.next(0,reader(pcm));
    CHECK(r.frames==512&&p.samples()[0]==pcm[normal/2]);
    CHECK(p.next(1,reader(pcm)).status==Playback::Status::Audio);
    p.seek(normal);CHECK(opens==closes&&p.position()==normal);
  }
  for(int failure=0;failure<5;++failure) {
    failOpen=failure==0;failSetting=failure==1;failProcess=failure==2;noProgress=failure==3;noOutput=failure==4;
    Playback p(200);unsigned loops=0;
    for(;;) {CHECK(++loops<500);const auto b=p.next(1,reader(pcm));if(b.status==Playback::Status::EffectError)break;CHECK(b.status==Playback::Status::Audio);}
  }
  failOpen=failSetting=failProcess=noProgress=noOutput=false;CHECK(opens==closes);
  for(int direction:{-1,0,1}) {Playback p(100);if(direction<0)p.seek(100);CHECK(p.next(direction,[](uint32_t,int16_t *,uint32_t){return false;}).status==Playback::Status::ReadError);}

  {core::recorder::Job job(false);worker(pcm,job);CHECK(job.ok&&fake::output==pcm&&job.bytes==pcm.size()*2);}
  {core::recorder::Job job(false);job.scrub=1;worker(pcm,job);CHECK(job.ok&&fake::output.size()==pcm.size()/2&&job.scrub==0);}
  // Forward preview while paused, then release to pause, then resume normal.
  {core::recorder::Job job(false);job.pause=true;job.scrub=1;unsigned pauses=0;
    fake::onWrite=[&]{if(fake::writes==4)job.scrub=0;if(fake::writes==8)job.stop=true;};
    fake::onDelay=[&]{if(job.state==core::recorder::State::Paused){++pauses;job.pause=false;}};
    worker(pcm,job);CHECK(job.ok&&pauses==1&&fake::writes==8);}
  // Reverse from the current audible position; reaching the start finishes.
  {core::recorder::Job job(false);uint32_t reverseAt=0;
    fake::onWrite=[&]{if(fake::writes==5){job.scrub=-1;reverseAt=job.bytes+1024;}};
    worker(pcm,job);CHECK(job.ok&&job.bytes==0&&job.scrub==0&&fake::writes>5&&reverseAt>0);}
  // Stop, screen-off and sleep cancel Sonic and leave no retained stream.
  for(int mode=0;mode<3;++mode){core::recorder::Job job(false);job.scrub=1;
    fake::onWrite=[&]{if(mode==0)job.stop=true;else if(mode==1)fake::sleep=true;else fake::audio=false;};
    fake::onDelay=[&]{if(job.state==core::recorder::State::Paused)job.stop=true;};
    worker(pcm,job);CHECK(job.scrub==0);}
  {core::recorder::Job job(false);fake::opening=false;worker(pcm,job);CHECK(!job.ok&&!job.error.empty());}
  {core::recorder::Job job(false);fake::writing=false;worker(pcm,job);CHECK(!job.ok&&!job.error.empty());}
  CHECK(opens==closes&&fake::streamOpens==fake::streamCloses);
  std::printf("%u checks; real Playback/worker with fake Sonic ABI and hardware; all buffers/streams released. Vendor DSP sound quality not measured.\n",checks);
}
