#pragma once
#include <cstdint>
namespace core::game_audio {
enum class Track {Runner,Maze,Morse};
enum Effect : unsigned {Jump=1,Bonus=2,Lost=4,Impact=8,Fall=16,Goal=32,Silence=64};
// Original miniature chiptune, generated at playback time. No sampled assets.
struct Synth {
    static constexpr unsigned Rate=16000;
    explicit constexpr Synth(Track selected=Track::Runner):track(selected){}
    const Track track;
    uint32_t musicSample=0,leadPhase=0,bassPhase=0,effectPhase=0,effectSample=0;
    uint32_t rollPhase=0,noise=0x6d2b79f5;
    int noiseFilter=0,rollGain=0;
    unsigned effect=0;
    static constexpr unsigned effectLength(unsigned e) {
        return e==Goal?12800:e==Fall?7200:e==Lost?6400:e==Bonus?3840:e==Impact?1280:1920;
    }
    static constexpr unsigned priority(unsigned e) {
        return e&Goal?Goal:e&Fall?Fall:e&Lost?Lost:e&Bonus?Bonus:e&Impact?Impact:e&Jump?unsigned(Jump):0U;
    }
    static constexpr unsigned movement(unsigned material,unsigned speed) {
        return ((material<5?material:0)<<8)|(speed>100?100:speed);
    }
    constexpr void silence(){effect=0;rollGain=noiseFilter=0;}
    constexpr void trigger(unsigned e) {effect=e;effectSample=effectPhase=0;}
    static constexpr int wave(uint32_t &phase,unsigned hz) {
        phase+=(uint64_t(hz)<<32)/Rate;
        const int p=int(phase>>20);
        return p<2048?p*2-2048:6143-p*2;
    }
    constexpr int16_t sample(bool music,unsigned motion=0) {
        int mixed=0;
        const unsigned material=(motion>>8)<5?motion>>8:0;
        if(music) {
          if(track==Track::Maze) {
            // Slower, softer arpeggio for the maze; independent from the runner.
            constexpr unsigned notes[32]={330,392,494,392,294,370,440,370,262,330,392,330,294,370,440,0,
                                           330,392,494,587,523,440,392,330,294,370,440,370,330,294,247,0};
            constexpr unsigned bass[4]={165,147,131,147};
            const unsigned note=musicSample/4800%32,pos=musicSample%4800;
            const unsigned envelope=pos<256?pos:(4800-pos)*256/4544;
            if(notes[note])mixed=wave(leadPhase,notes[note])*int(envelope)/512;
            mixed+=wave(bassPhase,bass[note/4%4])/8;
            ++musicSample;
          } else {
            constexpr unsigned notes[32]={523,659,784,659,587,698,880,698,659,784,988,784,587,698,784,0,
                                           523,659,784,1047,988,784,659,587,659,784,698,587,523,0,392,0};
            const unsigned note=musicSample/2560%32,pos=musicSample%2560;
            const unsigned envelope=pos<128?pos:pos>2304?(2560-pos)/2:128;
            if(notes[note])mixed=wave(leadPhase,notes[note])*int(envelope)/256;
            mixed+=wave(bassPhase,note<16?131:175)/5;
            ++musicSample;
          }
        }
        if(track==Track::Maze) {
            const int speed=music?int((motion&255)>100?100:motion&255):0;
            if(rollGain<speed)++rollGain;else if(rollGain>speed)--rollGain;
            if(rollGain) {
                // Filtered grain plus a quiet material resonance, never an asset stream.
                constexpr unsigned hz[5]={470,240,160,100,720};
                constexpr int grain[5]={120,200,240,100,80},ring[5]={96,48,28,20,120};
                noise=noise*1664525u+1013904223u;
                noiseFilter+=(int(noise>>20)-2048-noiseFilter)/(material==2?4:8);
                mixed+=(noiseFilter*grain[material]/512+wave(rollPhase,hz[material]+speed)*ring[material]/512)*rollGain/100;
            }
        }
        if(effect) {
            const unsigned length=effectLength(effect),left=length-effectSample;
            unsigned hz=effect==Lost?110+left*420/length:effect==Bonus?(effectSample<1280?880:effectSample<2560?1175:1568):350+effectSample*650/length;
            if(effect==Impact){constexpr unsigned taps[5]={880,420,230,150,1320};hz=taps[material];}
            else if(effect==Fall)hz=90+left*650/length;
            else if(effect==Goal){constexpr unsigned chord[4]={523,659,784,1047};hz=chord[effectSample/3200];}
            const unsigned attack=effectSample<80?effectSample:80;
            // Normalize first: longer fanfares must not overflow 32-bit products.
            const unsigned gain=left*attack*256/(length*80);
            mixed+=wave(effectPhase,hz)*int(gain)/(effect==Impact?192:128);
            if(++effectSample>=length)effect=0;
        }
        return int16_t(mixed<-32768?-32768:mixed>32767?32767:mixed);
    }
};
}
