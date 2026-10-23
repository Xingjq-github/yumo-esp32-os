#pragma once
#include <cstdint>

// Persist only these stable preview IDs. Never persist an array position.
namespace sys::themes {
enum class Id : uint8_t { Warm=4, Mono=5, Tiles=6, Focus=8, Terminal=10 };
constexpr Id Default=Id::Terminal;
constexpr bool valid(int id) { return id==4 || id==5 || id==6 || id==8 || id==10; }
constexpr Id normalize(int id) { return valid(id)?static_cast<Id>(id):Default; }
constexpr bool light(Id id) { return id==Id::Warm || id==Id::Mono || id==Id::Focus; }
struct Palette {
    uint32_t background,surface,foreground,accent,muted,edge,onAccent;
    uint32_t danger,success,warning;
    int radius;
};
// Muted text is slightly stronger than the HTML mockup for this small panel.
constexpr Palette palette(Id id) {
    switch(id) {
    case Id::Mono: return {0xF1F2EE,0xF9FAF6,0x24362D,0x344F3D,0x56624F,0xB9C7B3,0xFFFFFF,0xA92733,0x23673E,0x795000,2};
    case Id::Tiles: return {0x0E1B26,0x203443,0xF2F7FB,0xC6F26D,0xB0C1CE,0x3E5260,0x0E1B26,0xF18087,0x83DCAC,0xF0C66F,9};
    case Id::Warm: return {0xF6F0E7,0xFFFCF6,0x594737,0xB95435,0x75604D,0xD2BFA8,0xFFFFFF,0xA92733,0x23673E,0x795000,10};
    case Id::Focus: return {0xF1EFEB,0xE6E2D9,0x282723,0xB63B17,0x665E51,0xC6C1B6,0xFFFFFF,0xA92733,0x23673E,0x795000,7};
    default: return {0x101E1B,0x203B2B,0xB8D6BD,0xD5EF8D,0xA0B7A2,0x405B42,0x101E1B,0xF18087,0x98DBA6,0xE9C577,0};
    }
}
constexpr const char *name(Id id) {
    switch(id) { case Id::Warm:return "04 砂岩"; case Id::Mono:return "05 墨线"; case Id::Tiles:return "06 拼贴"; case Id::Focus:return "08 一页"; default:return "10 游标"; }
}
} // namespace sys::themes
