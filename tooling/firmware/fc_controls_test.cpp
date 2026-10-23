#include "core/fc_controls.hpp"
#include "core/game_key_state.hpp"
namespace c=core::fc::controls;
using Keys=sys::GameKeyState;
static_assert(c::Radius*2==66 && c::ThumbRadius*2==44);
static_assert(c::StickX==33 && c::StickY==33 && c::Quadrant==120);

constexpr bool screenRegionsAndJoystick() {
    unsigned select=0,menu=0,start=0,stick=0;
    for(int y=0;y<240;++y) for(int x=0;x<240;++x) {
        const auto zone=c::region(x,y);
        select+=zone==c::Region::Select; menu+=zone==c::Region::Menu;
        start+=zone==c::Region::Start; stick+=zone==c::Region::Stick;
    }
    if(select!=6400 || menu!=6400 || start!=6400 || stick!=14400) return false;
    if(c::region(-1,20)!=c::Region::None || c::region(240,239)!=c::Region::None) return false;
    c::Touch touch;
    touch.begin(c::StickX,c::StickY); if(touch.pad) return false;
    touch.move(c::StickX+1,c::StickY); if(touch.pad!=c::Right) return false;
    touch.move(c::StickX-1,c::StickY); if(touch.pad!=c::Left) return false;
    touch.move(c::StickX,c::StickY-1); if(touch.pad!=c::Up) return false;
    touch.move(c::StickX,c::StickY+1); if(touch.pad!=c::Down) return false;
    touch.move(c::StickX+1,c::StickY-1); if(touch.pad!=(c::Right|c::Up)) return false;
    touch.move(c::StickX,c::StickY); if(touch.pad) return false;
    touch.move(c::StickX+30,c::StickY); if(touch.pad!=c::Right) return false;
    touch.move(c::StickX+30,c::StickY-30); if(touch.pad!=(c::Right|c::Up)) return false;
    touch.move(c::StickX,c::StickY+40); if(touch.pad!=c::Down) return false;
    // Pointer stays owned by its initial zone; a drag cannot open the menu.
    touch.move(120,210); if(touch.active!=c::Region::Stick) return false;
    touch.clear(); if(touch.pad || touch.dx || touch.dy) return false;
    touch.begin(10,210); if(touch.pad!=c::Select) return false;
    touch.move(200,210); if(touch.pad || touch.active!=c::Region::Select) return false;
    touch.begin(200,210); if(touch.pad!=c::Start) return false;
    touch.begin(200,150); touch.move(c::StickX,c::StickY); if(touch.pad) return false;
    return true;
}
constexpr bool directionPriorityAndBounds() {
    for(unsigned keyboard=0;keyboard<256;++keyboard) for(unsigned local=0;local<256;++local) {
        const auto pad=c::combine(keyboard,local),key=c::neutralOpposites(keyboard);
        if((pad&0x30)==0x30 || (pad&0xc0)==0xc0) return false;
        if((key&0x30) && (pad&0x30)!=(key&0x30)) return false;
        if((key&0xc0) && (pad&0xc0)!=(key&0xc0)) return false;
        if((pad&0x0f)!=((keyboard|local)&0x0f)) return false;
    }
    return c::combine(0,c::Left|Keys::Key)==(c::Left|Keys::Key) &&
           c::direction(-40,0)==c::Left && c::direction(0,-40)==c::Up;
}
constexpr bool rawKeysHoldTapAndChord() {
    Keys state; const auto session=state.claim();
    if(!state.consumeClick(Keys::Key) || state.blockHome()) return false;
    state.down(Keys::Key); if(state.take(session)) return false; // List never emits A.
    state.mode(session,true); state.running(session,true);
    if(!state.blockHome() || state.take(session)) return false; // Held before game.
    state.up(Keys::Key); state.end(Keys::Key); state.down(Keys::Key);
    if(state.take(session)!=1 || state.take(session)!=1) return false;
    state.down(Keys::Boot); if(state.take(session)!=3) return false;
    state.up(Keys::Key); state.up(Keys::Boot); if(state.take(session)) return false;
    // Down/up between emulated frames is not lost or repeated forever.
    state.down(Keys::Boot); state.up(Keys::Boot);
    if(state.take(session)!=2 || state.take(session)) return false;
    state.down(Keys::Boot); if(state.take(session)!=2) return false;
    state.up(Keys::Boot); if(state.take(session)) return false;
    return true;
}
constexpr bool pauseSleepAndOwnerLifetime() {
    Keys state;const auto first=state.claim();state.mode(first,true);state.running(first,true);
    state.down(Keys::Boot);state.take(first);
    state.running(first,false);if(state.take(first)) return false;
    state.down(Keys::Key);if(state.take(first)) return false;
    state.running(first,true);if(state.take(first)) return false; // Release then repress.
    state.up(Keys::Boot);state.up(Keys::Key);state.down(Keys::Key);
    if(state.take(first)!=1) return false;
    state.clear(first);if(state.take(first)) return false;
    state.release(first);
    if(state.take(first) || !state.consumeClick(Keys::Key) || !state.blockHome()) return false;
    state.up(Keys::Key);state.end(Keys::Key);state.end(Keys::Boot);
    if(state.consumeClick(Keys::Key) || state.blockHome()) return false;
    const auto second=state.claim();state.mode(second,true);state.running(second,true);
    state.release(first);state.mode(first,false);state.running(first,false);
    state.down(Keys::Key);
    return state.take(first)==0 && state.take(second)==1 && state.blockHome();
}
constexpr bool doubleClickFenceEndsWithGesture() {
    Keys state;auto session=state.claim();state.mode(session,true);state.running(session,true);
    state.down(Keys::Boot);state.up(Keys::Boot);state.down(Keys::Boot);state.up(Keys::Boot);
    state.mode(session,false); // Game has stopped, but its delayed double-click is still pending.
    if(!state.blockHome() || !state.consumeClick(Keys::Boot)) return false;
    state.end(Keys::Boot);
    if(state.blockHome()) return false; // BOOT Home works again on list/Bluetooth pages.
    state.release(session);
    state.down(Keys::Boot);state.up(Keys::Boot);
    return !state.consumeClick(Keys::Boot) && !state.blockHome();
}
static_assert(screenRegionsAndJoystick());
static_assert(directionPriorityAndBounds());
static_assert(rawKeysHoldTapAndChord());
static_assert(pauseSleepAndOwnerLifetime());
static_assert(doubleClickFenceEndsWithGesture());
static_assert(c::StickX-c::Radius==0 && c::StickX+c::Radius<=c::Quadrant);
static_assert(c::StickY-c::Radius==0 && c::StickY+c::Radius<=c::Quadrant);
constexpr bool fourWayMode() {
    c::Touch touch;
    if(touch.fourWay)return false;
    touch.fourWay=true;
    for(int y=-120;y<=120;++y)for(int x=-120;x<=120;++x) {
        const auto pad=c::direction(x,y,true);
        if(pad && (pad&(pad-1)))return false; // Exactly one cardinal direction.
        const int ax=x<0?-x:x,ay=y<0?-y:y;
        const auto expected=x==0&&y==0?0:ax>=ay?(x<0?c::Left:c::Right):(y<0?c::Up:c::Down);
        if(pad!=expected)return false;
    }
    touch.begin(c::StickX+1,c::StickY+1);if(touch.pad!=c::Right)return false;
    touch.move(c::StickX+1,c::StickY+2);if(touch.pad!=c::Down)return false;
    touch.clear();if(!touch.fourWay || touch.pad)return false;
    touch.begin(20,200);if(touch.pad!=c::Select)return false;
    touch.begin(220,200);if(touch.pad!=c::Start)return false;
    touch.fourWay=false;touch.begin(c::StickX+1,c::StickY+1);
    if(touch.pad!=(c::Right|c::Down))return false;
    const auto diagonal=c::Right|c::Up|3;
    return c::fourDirections(diagonal,.2f,-.3f)==(c::Up|3) &&
           c::fourDirections(diagonal,.3f,-.2f)==(c::Right|3) &&
           c::combine(c::Right|c::Up,c::Left)==(c::Right|c::Up);
}
static_assert(fourWayMode());
