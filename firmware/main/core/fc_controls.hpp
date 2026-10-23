#pragma once
#include <cstdint>

namespace core::fc::controls {
constexpr int Screen=240, ButtonSize=80, ButtonY=Screen-ButtonSize, Quadrant=120;
constexpr int StickX=33, StickY=33, Radius=33, ThumbRadius=22;
constexpr uint8_t Select=0x04,Start=0x08,Up=0x10,Down=0x20,Left=0x40,Right=0x80;
enum class Region { None, Select, Menu, Start, Stick };
constexpr Region region(int x,int y) {
    if(x<0 || x>=Screen || y<0 || y>=Screen) return Region::None;
    if(y>=ButtonY) return x<ButtonSize?Region::Select:x<2*ButtonSize?Region::Menu:Region::Start;
    return x<Quadrant && y<Quadrant?Region::Stick:Region::None;
}
constexpr uint8_t neutralOpposites(uint8_t pad) {
    if((pad&(Up|Down))==(Up|Down)) pad&=uint8_t(~(Up|Down));
    if((pad&(Left|Right))==(Left|Right)) pad&=uint8_t(~(Left|Right));
    return pad;
}
// Keyboard direction takes priority on its own axis; local A/B/Select/Start
// remain available. A disconnected keyboard contributes zero immediately.
constexpr uint8_t combine(uint8_t keyboard,uint8_t local) {
    keyboard=neutralOpposites(keyboard); local=neutralOpposites(local);
    if(keyboard&(Up|Down)) local&=uint8_t(~(Up|Down));
    if(keyboard&(Left|Right)) local&=uint8_t(~(Left|Right));
    return keyboard|local;
}
// Four-way input chooses the dominant axis; ties consistently prefer horizontal.
// Keep A/B/Select/Start intact, and never quantize a keyboard's own directions.
constexpr uint8_t fourDirections(uint8_t pad,float x,float y) {
    const float ax=x<0?-x:x,ay=y<0?-y:y;
    if((pad&(Left|Right)) && (pad&(Up|Down)))
        pad&=uint8_t(~(ax>=ay?(Up|Down):(Left|Right)));
    return pad;
}
constexpr uint8_t direction(int dx,int dy,bool fourWay=false) {
    if(dx==0 && dy==0) return 0;
    const int ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
    uint8_t pad=0;
    if(ax*2>=ay) pad|=dx<0?Left:Right;
    if(ay*2>=ax) pad|=dy<0?Up:Down;
    return fourWay?fourDirections(pad,float(dx),float(dy)):pad;
}
struct Touch {
    Region active=Region::None;
    uint8_t pad=0;
    int dx=0,dy=0;
    bool fourWay=false;
    constexpr void clear() { active=Region::None; pad=0; dx=dy=0; }
    constexpr void begin(int x,int y) { clear(); active=region(x,y); move(x,y); }
    constexpr void move(int x,int y) {
        pad=0;
        if(active==Region::Stick) { dx=x-StickX; dy=y-StickY; pad=direction(dx,dy,fourWay); }
        else if(region(x,y)==active) {
            if(active==Region::Select) pad=Select;
            if(active==Region::Start) pad=Start;
        }
    }
};
}
