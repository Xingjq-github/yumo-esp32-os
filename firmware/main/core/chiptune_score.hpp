#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace core::chip {
constexpr unsigned Rate=22050, Ticks=480, MaxEvents=2048, MaxFile=65536;
enum class Wave:uint8_t {Pulse,Triangle,Percussion};
struct Event {uint32_t begin=0,end=0;uint8_t note=0;};
static_assert(sizeof(Event)<=16);
struct Track {
    std::string id;
    Wave wave=Wave::Pulse;
    float duty=.5f,volume=.6f,gate=.85f;
    unsigned first=0,count=0;
};
struct Score {
    std::string title;
    unsigned tempo=120,count=0,ticks=0;
    bool loop=false;
    std::array<Track,4> tracks{};
    std::vector<Event> events;
};
// No device/UI dependencies. Bounded JSON and strict version-1 MML subset.
bool parse(const char *text,size_t bytes,Score &out,std::string &error);
uint64_t sampleAt(uint64_t tick,unsigned tempo);

class Synth {
public:
    explicit Synth(const Score &score):score_(score){reset();}
    void reset();
    // One sample-clock, including loop remainder. No allocation/file access.
    int16_t next(bool loop);
    bool ended()const{return ended_;}
    unsigned milliseconds()const;
    unsigned durationMs()const;
private:
    struct Voice {unsigned event=0;float phase=0,step=0;uint32_t noise=1;float filter=0;uint64_t begin=0,end=0;};
    const Score &score_;
    std::array<Voice,4> voices_{};
    uint64_t sample_=0,roundTick_=0,roundStart_=0,roundEnd_=0;
    bool ended_=false;
    void round();
    float voice(unsigned index);
};
} // namespace core::chip
