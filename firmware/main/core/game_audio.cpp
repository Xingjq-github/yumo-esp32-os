#include "mp3_player.hpp"
#include "xiaozhi_service.hpp"
#include "chiptune_player.hpp"
#include "game_audio.hpp"
#include "game_synth.hpp"
#include "morse_signal.hpp"
#include "audio_mgr.hpp"
#include "audio_recorder.hpp"
#include "fc_emulator.hpp"
#include "power_mgr.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <new>
namespace core::game_audio {
namespace {
std::atomic<bool> active{false},audible{false};
void telegraph(Session &job) {
    morse_audio::Signal signal;int16_t pcm[128];bool opened=false;
    uint32_t epoch=job.morseReset.load();
    while(!job.stop.load()&&!sys::Power::sleepPending()) {
        if(!sys::Power::audioMayRun())job.pause(false);
        const auto current=job.morseReset.load();
        if(current!=epoch){epoch=current;signal.clear();}
        if(!signal.active()) {
            Session::MorseCommand command;
            while(job.popMorse(command))if(command.epoch==epoch){signal.play(command.code);break;}
        }
        const bool key=job.morseKey.load(),ready=job.morseReady.load();
        if(!ready&&!key&&!signal.active()) {
            if(opened){sys::Audio::closeStream();opened=false;}
            audible.store(false);job.wake.wait();continue;
        }
        sys::Power::WorkGuard work;
        if(!opened){opened=sys::Audio::openStream(false,morse_audio::Signal::Rate);if(!opened){job.failed.store(true);break;}}
        audible.store(key||signal.active());
        for(auto &sample:pcm)sample=signal.sample(key);
        if(!sys::Audio::writeStream(pcm,sizeof(pcm))){job.failed.store(true);break;}
    }
    if(opened)sys::Audio::closeStream();
}
void worker(void *arg) {
    {
        auto holder=static_cast<std::shared_ptr<Session> *>(arg);
        auto job=*holder;delete holder;
        job->wake.attach();
        if(job->track==Track::Morse)telegraph(*job);
        else {
        Synth synth(job->track);int16_t pcm[256];bool opened=false;
        while(!job->stop.load()&&!sys::Power::sleepPending()) {
            if(!sys::Power::audioMayRun())job->pause(false);
            const unsigned event=job->effects.exchange(0);
            if(event&Silence)synth.silence();
            if(const auto effect=Synth::priority(event))synth.trigger(effect);
            const bool music=job->music.load();
            const unsigned motion=job->motion.load();
            if(!music&&!synth.effect) {
                if(opened) {sys::Audio::closeStream();opened=false;}
                audible.store(false);
                job->wake.wait();continue;
            }
            sys::Power::WorkGuard work;
            audible.store(true);
            if(!opened) {
                opened=sys::Audio::openStream(false,Synth::Rate);
                if(!opened){job->failed.store(true);ESP_LOGW("game_audio","Speaker unavailable");break;}
            }
            for(auto &sample:pcm)sample=synth.sample(music,motion);
            if(!sys::Audio::writeStream(pcm,sizeof(pcm))){job->failed.store(true);break;}
        }
        if(opened)sys::Audio::closeStream();
        }
        job->wake.detach();
    }
    audible.store(false);active.store(false);
    vTaskDeleteWithCaps(nullptr);
}
}
std::shared_ptr<Session> start(Track track) {
    if(sys::Power::sleepPending()||!mp3::idle()||!chip::idle()||!recorder::idle()||!fc::idle()||!core::xiaozhi::idle() || active.exchange(true))return {};
    std::shared_ptr<Session> job;std::shared_ptr<Session> *arg=nullptr;
    try {job=std::make_shared<Session>(track);arg=new std::shared_ptr<Session>(job);}
    catch(const std::bad_alloc &) {active.store(false);return {};}
    if(xTaskCreateWithCaps(worker,"game_audio",4096,arg,2,nullptr,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)!=pdPASS) {
        delete arg;active.store(false);return {};
    }
    return job;
}
bool idle(){return !active.load();}
bool busy(){return audible.load();}
}
