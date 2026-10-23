#include "ui/boot_animation_model.hpp"
using namespace ui::boot;
constexpr bool framesStayBounded() {
    auto previous=frame(0);
    for(uint32_t elapsed=0;elapsed<=DurationMs;++elapsed) {
        const auto f=frame(elapsed);
        if(f.branches>255 || f.stem>255 || f.textOpacity>255 || f.dotOpacity>160 ||
           f.textY<145 || f.textY>153 || f.textY+35>196 ||
           f.radius<30 || f.radius>48 || 86-f.radius-3<0 ||
           f.barWidth<0 || f.barWidth>120 || f.angle<0 || f.angle>450) return false;
        if(f.branches<previous.branches || f.stem<previous.stem ||
           f.textOpacity<previous.textOpacity || f.dotOpacity>previous.dotOpacity ||
           f.textY>previous.textY || f.barWidth<previous.barWidth) return false;
        previous=f;
    }
    return true;
}
static_assert(DurationMs==600 && FrameMs==15);
static_assert(frame(0).textOpacity==0 && frame(0).branches==0);
static_assert(frame(240).branches==255 && frame(240).stem==255);
static_assert(frame(325).textOpacity==255 && frame(325).textY==145);
static_assert(frame(450).dotOpacity==0);
static_assert(frame(550).barWidth==120);
static_assert(frame(UINT32_MAX).textOpacity==255);
static_assert(framesStayBounded());
