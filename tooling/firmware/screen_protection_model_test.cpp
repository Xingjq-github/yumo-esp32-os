#include "ui/screen_protection_model.hpp"
using namespace ui::protection;
constexpr bool idleDeadline() {
    Idle idle;
    if(idle.step(10,1,true) || idle.step(120009,1,true))return false;
    if(!idle.step(120010,1,true))return false;
    if(idle.step(120010,2,true) || idle.step(240009,2,true))return false;
    if(!idle.step(240010,2,true))return false;
    if(idle.step(240011,2,false) || idle.step(240012,2,true))return false;
    idle.reset();if(idle.step(0xfffffff0u,0xffffffffu,true))return false;
    if(idle.step(0xfffffff0u+IdleMs-1,0xffffffffu,true))return false;
    if(!idle.step(0xfffffff0u+IdleMs,0xffffffffu,true))return false;
    return !idle.step(0xfffffff0u+IdleMs,0,true);
}
constexpr bool wakeStroke() {
    TouchGate gate;
    if(gate.filter(false,false))return false;
    if(!gate.filter(true,true))return false;
    if(!gate.filter(false,true) || !gate.filter(false,false))return false;
    if(gate.filter(false,true) || gate.filter(false,false))return false;
    return !gate.filter(true,false);
}
constexpr bool positionsFit() {
    for(unsigned i=0;i<8;++i) {
        const auto p=position(i*MoveMs);
        if(p.x<0 || p.y<0 || p.x+96>240 || p.y+36>240)return false;
        for(unsigned j=0;j<i;++j) {
            const auto q=position(j*MoveMs);
            if(p.x==q.x && p.y==q.y)return false;
        }
        const auto same=position(i*MoveMs+MoveMs-1);
        if(p.x!=same.x || p.y!=same.y)return false;
    }
    return position(8*MoveMs).x==position(0).x && position(8*MoveMs).y==position(0).y;
}
static_assert(idleDeadline(),"120-second input deadline and tick/activity wrap");
static_assert(wakeStroke(),"first touch stays consumed through release after cover deletion");
static_assert(positionsFit(),"all 8 positions are distinct and fit the 240x240 display");
