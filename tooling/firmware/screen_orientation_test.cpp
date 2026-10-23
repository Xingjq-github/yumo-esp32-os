#include "core/orientation_model.hpp"
#include "bsp/orientation.h"
static_assert(sys::orientation::FreshMs < sys::orientation::StableMs);
static_assert(sys::orientation::StableMs >= 500 && sys::orientation::StableMs <= 1000);
static_assert(sys::orientation::CooldownMs >= sys::orientation::StableMs);
static_assert(sizeof(sys::orientation::Filter) <= 48);

// Compile the production C-compatible board mapping with the target compiler.
bool screenOrientationMapping() {
    for(unsigned turn=0;turn<4;++turn) {
        uint16_t x=0,y=239;
        bsp_orientation_point(turn,&x,&y);
        if(x>239 || y>239)return false;
        const auto mode=bsp_orientation_panel(turn);
        if(mode.x_gap+240>(mode.swap_xy?320:240) ||
           mode.y_gap+240>(mode.swap_xy?240:320))return false;
    }
    return true;
}
