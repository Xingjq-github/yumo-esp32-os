#pragma once
#include <cstdint>
namespace core::fc {
// Absolute, fractional deadlines. Audio writes consume this SAME frame budget.
// Catch up by omitting video, never by omitting CPU/APU/input emulation.
class FrameTiming {
public:
  explicit constexpr FrameTiming(unsigned hz):hz_(hz==50?50:60) {}
  constexpr void reset(int64_t now) {
    deadline_=now;lastDraw_=now-100000;lastYield_=now;fraction_=sequence_=0;
    credits_=1;havePhase_=false;phase_=lastPhase_=0;
    dither_=0x9E3779B9u;deferrals_=0;
  }
  constexpr bool draw(int64_t now,bool available) {
    phase_=(sequence_++ & 1u);
    if(credits_<4)++credits_;
    // A fixed 0,2,4... sample permanently loses alternate-frame sprites.
    // Retain the half-rate budget, but select the opposite phase after each
    // successful mailbox delivery. Busy GUI / failed try_lock must not advance
    // this phase. At most two saved render credits allow bounded catch-up.
    const bool stale=now-lastDraw_>=100000;
    const bool ready=available && credits_>=2 &&
           (!havePhase_ || phase_!=lastPhase_) &&
           (now-deadline_<1000000/hz_ || stale);
    // Also break synchronization with 3/4/... frame OAM rotations and a slow
    // GUI. Rare, bounded phase dithering is local to display scheduling; it
    // never changes NES timing or the game's own random state.
    if(ready && !stale && deferrals_) {--deferrals_;return false;}
    if(ready)credits_-=2; // Charge PPU work even if its later try_lock fails.
    return ready;
  }
  constexpr void presented(int64_t now) {
    lastDraw_=now;lastPhase_=phase_;havePhase_=true;
    dither_^=dither_<<13;dither_^=dither_>>17;dither_^=dither_<<5;
    deferrals_=(dither_&15u)?0:1+((dither_>>4)&1u);
  }
  constexpr int64_t finish(int64_t now) {
    // A long scheduling/audio stall must not create an unbounded catch-up loop.
    if(now-deadline_>100000) {deadline_=now;fraction_=0;}
    deadline_+=1000000/hz_;
    fraction_+=1000000%hz_;
    if(fraction_>=hz_) {++deadline_;fraction_-=hz_;}
    return deadline_>now?deadline_-now:0;
  }
  constexpr unsigned waitTicks(int64_t now,int64_t remaining,unsigned tickUs) {
    const unsigned ticks=remaining>0?static_cast<unsigned>(remaining/tickUs):0;
    // Allow idle/watchdog tasks to run even if continuously behind schedule.
    if(ticks || now-lastYield_>=50000) {lastYield_=now;return ticks?ticks:1;}
    return 0;
  }
private:
  unsigned hz_,fraction_=0,sequence_=0;
  unsigned credits_=1,phase_=0,lastPhase_=0;
  uint32_t dither_=0x9E3779B9u;
  unsigned deferrals_=0;
  bool havePhase_=false;
  int64_t deadline_=0,lastDraw_=0,lastYield_=0;
};
} // namespace core::fc
