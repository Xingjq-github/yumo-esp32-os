#include "core/fc_tilt.hpp"
#include "core/qmi8658.hpp"
#include <limits>
namespace t=core::fc::tilt;
namespace c=core::fc::controls;

constexpr float rawG(int raw) {
    const auto bits=static_cast<uint16_t>(raw);
    return sys::Qmi8658::accelToG(uint8_t(bits),uint8_t(bits>>8));
}
static_assert(sys::Qmi8658::AccelConfig==0x25); // Datasheet: 8g, 250Hz, no self-test.
static_assert(rawG(4096)==1.0f && rawG(-4096)==-1.0f);
static_assert(rawG(0)==0.0f && rawG(-32768)==-8.0f);
constexpr bool rawSensorToDirections() {
    // Real sensor counts, using the exact conversion used by the driver.
    // A stationary device must pass validity checks in every orientation.
    constexpr int poses[][3]={{0,0,4096},{0,0,-4096},{4096,0,0},
        {-4096,0,0},{0,4096,0},{0,-4096,0}};
    for(const auto &pose:poses) {
        t::State state;
        if(!state.update(rawG(pose[0]),rawG(pose[1]),rawG(pose[2]))) return false;
    }
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        t::State state;
        if(!state.update(rawG(x*640),rawG(y*640),rawG(3995))) return false;
        const auto pad=(x<0?c::Left:x>0?c::Right:0)|(y<0?c::Up:y>0?c::Down:0);
        if(state.pad!=pad) return false;
        for(int i=0;i<4;++i) state.update(rawG(0),rawG(0),rawG(4096));
        if(state.pad) return false;
    }
    // Reproduce the old mismatch: 4g range produced 8192 counts at 1g, but
    // dividing by 4096 made the norm 2g and rejected otherwise normal data.
    t::State old;
    return !old.update(0,0,8192.0f/4096.0f);
}
static_assert(rawSensorToDirections());

constexpr bool horizontalAndEightDirections() {
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        t::State state;
        const auto expected=(x<0?c::Left:x>0?c::Right:0)|(y<0?c::Up:y>0?c::Down:0);
        if(!state.update(x*0.2f,y*0.2f,0.96f) || state.pad!=expected) return false;
        // Return to horizontal without calibration or leaving a held direction.
        for(int i=0;i<4;++i) state.update(0,0,1);
        if(state.pad) return false;
    }
    t::State state;
    for(int i=0;i<100;++i) {
        state.update(i%2?0.045f:-0.045f,i%3?0.025f:-0.025f,1);
        if(state.pad) return false;
    }
    return true;
}
constexpr bool hysteresisAndReversal() {
    if(t::axis(0.09f,0)!=0 || t::axis(t::Enter,0)!=1 || t::axis(-t::Enter,0)!=-1) return false;
    if(t::axis(0.07f,1)!=1 || t::axis(-0.07f,-1)!=-1) return false;
    if(t::axis(t::Release,1) || t::axis(-t::Release,-1)) return false;
    t::State state;
    state.update(0.2f,-0.2f,0.96f);
    if(state.pad!=(c::Right|c::Up)) return false;
    state.update(-0.3f,0.3f,0.90f);
    if(state.pad!=(c::Left|c::Down)) return false;
    state.clear();
    if(state.pad || state.seeded || state.x || state.y) return false;
    return state.update(0,0,1) && state.pad==0;
}
constexpr bool invalidSamplesFailNeutral() {
    constexpr float invalid[]={0.0f,0.1f,2.0f,
        std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()};
    for(float z:invalid) {
        t::State state;state.update(0.2f,0,1);
        if(state.pad!=c::Right) return false;
        if(state.update(0,0,z) || state.pad || state.seeded) return false;
        if(!state.update(0,0,1) || state.pad) return false;
    }
    t::State state;
    return !state.update(std::numeric_limits<float>::quiet_NaN(),0,1) && !state.pad;
}
constexpr bool staleClockAndWrap() {
    return t::fresh(1150,1000) && !t::fresh(1151,1000) &&
        t::fresh(49,UINT32_MAX-100) && !t::fresh(50,UINT32_MAX-100) &&
        !t::fresh(999,1000);
}
constexpr bool overridesAndIndependentButtons() {
    if(t::compose(true,c::Left|c::Up,c::Start,false,0)!=(c::Left|c::Up|c::Start)) return false;
    if(t::compose(true,c::Left|c::Up,0,true,0)!=0) return false; // Finger holds centre.
    if(t::compose(true,c::Left|c::Up,c::Down,true,0)!=c::Down) return false;
    if(t::compose(true,c::Left|c::Up,c::Select,false,c::Right)!=(c::Right|c::Up|c::Select)) return false;
    if(t::compose(true,c::Left|c::Up,0,false,c::Down|3)!=(c::Left|c::Down|3)) return false;
    for(unsigned gravity=0;gravity<256;gravity+=16) for(unsigned touch=0;touch<256;++touch) {
        if(t::compose(false,gravity,touch,false,c::Right)!=c::combine(c::Right,touch)) return false;
        if(t::compose(false,gravity,touch,true,c::Down)!=c::combine(c::Down,touch)) return false;
        const auto pad=t::compose(true,gravity,touch,false,3);
        if((pad&0x30)==0x30 || (pad&0xc0)==0xc0) return false;
        if((pad&0x0f)!=((touch|3)&0x0f)) return false;
    }
    return true;
}
static_assert(horizontalAndEightDirections());
static_assert(hysteresisAndReversal());
static_assert(invalidSamplesFailNeutral());
static_assert(staleClockAndWrap());
static_assert(overridesAndIndependentButtons());
