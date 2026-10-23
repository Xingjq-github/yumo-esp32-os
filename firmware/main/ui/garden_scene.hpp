#pragma once
#include "core/garden_model.hpp"
#include <array>
namespace ui::garden_scene {
constexpr unsigned Width=120,Height=120;
using Pixels=std::array<uint16_t,Width*Height>;
struct View {
    int preview=-1,biome=-1;
    unsigned petSize=50,reaction=0,variant=0;
    int petX=60,ground=80;
    bool pet=true;
};
void render(Pixels &pixels,const core::garden::State &state,unsigned frame,const View &view={});
}
