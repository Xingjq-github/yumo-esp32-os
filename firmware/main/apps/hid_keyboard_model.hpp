#pragma once
#include <cstddef>
#include <cstdint>

namespace apps::keys {
struct Stroke { uint8_t code=0, modifiers=0; };
// HID modifier byte bits 0..3: Left Control, Shift, Alt/Option, GUI/Command.
// Apple 102650: Cmd+C/X/V/A/Z; lock = Ctrl+Cmd+Q. Never substitute Alt for Cmd.
constexpr uint8_t Ctrl=0x01, Shift=0x02, Alt=0x04, Gui=0x08;
// USB keyboard usages, US ANSI layout. Host layout/IME must be English (US).
constexpr Stroke ascii(char c) {
    if(c>='a' && c<='z') return {static_cast<uint8_t>(0x04+c-'a'),0};
    if(c>='A' && c<='Z') return {static_cast<uint8_t>(0x04+c-'A'),Shift};
    if(c>='1' && c<='9') return {static_cast<uint8_t>(0x1e + c-'1'),0};
    if(c=='0') return {0x27,0};
    if(c==' ') return {0x2c,0};
    if(c=='\n') return {0x28,0};
    if(c=='\t') return {0x2b,0};
    constexpr char plain[]="-=[]\\;'`,./";
    constexpr char upper[]="_+{}|:\"~<>?";
    constexpr uint8_t codes[]={0x2d,0x2e,0x2f,0x30,0x31,0x33,0x34,0x35,0x36,0x37,0x38};
    for(size_t i=0;i<sizeof(codes);++i) {
        if(c==plain[i]) return {codes[i],0};
        if(c==upper[i]) return {codes[i],Shift};
    }
    constexpr char shiftedDigits[]="!@#$%^&*()";
    for(uint8_t i=0;i<10;++i) if(c==shiftedDigits[i]) return {static_cast<uint8_t>(0x1e + i),Shift};
    return {};
}
constexpr bool coversAscii() {
    for(int c=32;c<=126;++c) if(!ascii(static_cast<char>(c)).code) return false;
    return true;
}
static_assert(coversAscii(),"All printable ASCII characters must have HID usages");
// Visual order is also the 3-column grid order. Arrows form an inverted T.
struct Shortcut { const char *label; Stroke win,mac; const char *winHint,*macHint; };
constexpr Shortcut shortcuts[]={
    {"复制",{0x06,Ctrl},{0x06,Gui},"Ctrl+C","Cmd+C"},
    {"剪切",{0x1b,Ctrl},{0x1b,Gui},"Ctrl+X","Cmd+X"},
    {"粘贴",{0x19,Ctrl},{0x19,Gui},"Ctrl+V","Cmd+V"},
    {"全选",{0x04,Ctrl},{0x04,Gui},"Ctrl+A","Cmd+A"},
    {"锁屏",{0x0f,Gui},{0x14,Ctrl|Gui},"Win+L","Ctrl+Cmd+Q"},
    {"回车",{0x28,0},{0x28,0},"Enter","Return"},
    {"退出",{0x29,0},{0x29,0},"Esc","Esc"},
    {"上",{0x52,0},{0x52,0},"Up","Up"},
    {"撤销",{0x1d,Ctrl},{0x1d,Gui},"Ctrl+Z","Cmd+Z"},
    {"左",{0x50,0},{0x50,0},"Left","Left"},
    {"下",{0x51,0},{0x51,0},"Down","Down"},
    {"右",{0x4f,0},{0x4f,0},"Right","Right"},
};
enum class Alphabet : uint8_t { Lower,Upper,Numbers,Symbols };
inline constexpr const char *lower[]={" 0\n\t",".,?!1","abc2","def3","ghi4","jkl5","mno6","pqrs7","tuv8","wxyz9"};
inline constexpr const char *upper[]={" 0\n\t",".,?!1","ABC2","DEF3","GHI4","JKL5","MNO6","PQRS7","TUV8","WXYZ9"};
inline constexpr const char *digits[]={"0","1","2","3","4","5","6","7","8","9"};
inline constexpr const char *symbols[]={" 0\n\t",".,?!","@#$_","+-=*/",":;\"'","()[]","{}<>","\\|~`","&%^","0123456789"};
constexpr const char *group(Alphabet mode,unsigned key) {
    if(key>9) return "";
    switch(mode) {
    case Alphabet::Lower:return lower[key];
    case Alphabet::Upper:return upper[key];
    case Alphabet::Numbers:return digits[key];
    case Alphabet::Symbols:return symbols[key];
    }
    return "";
}
// Local composition: no backspaces sent to the host when cycling a character.
// Commit after 700ms or on a different key. UI owns sending and error handling.
struct MultiTap {
    static constexpr uint32_t DelayMs=700;
    int key=-1; unsigned index=0; uint32_t at=0; char pending=0;
    constexpr void clear() { key=-1; index=0; pending=0; }
    constexpr bool due(uint32_t now) const { return pending && uint32_t(now-at)>=DelayMs; }
    constexpr void begin(Alphabet mode,unsigned next,uint32_t now) {
        const char *chars=group(mode,next);
        if(key==static_cast<int>(next) && pending) {
            ++index; if(!chars[index]) index=0;
        } else { key=static_cast<int>(next); index=0; }
        pending=chars[index]; at=now;
    }
};
} // namespace apps::keys
