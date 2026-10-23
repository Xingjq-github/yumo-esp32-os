// Compiler-executed constant-expression tests of the actual firmware key model.
// No link/run/flash is required. Compile -std=c++17 -fsyntax-only -I main.
#include "apps/hid_keyboard_model.hpp"
using namespace apps::keys;
constexpr bool same(Stroke a,Stroke b) { return a.code==b.code && a.modifiers==b.modifiers; }
constexpr bool asciiUnique() {
    for(int a=32;a<127;++a) for(int b=a+1;b<127;++b)
        if(same(ascii(a),ascii(b))) return false;
    return true;
}
constexpr bool allModesCoverAscii() {
    for(int ch=32;ch<127;++ch) {
        bool found=false;
        for(unsigned mode=0;mode<4;++mode) for(unsigned key=0;key<10;++key) {
            const char *g=group(static_cast<Alphabet>(mode),key);
            for(unsigned i=0;g[i];++i) if(g[i]==ch) found=true;
        }
        if(!found) return false;
    }
    return true;
}
constexpr bool cycleAndCancel() {
    MultiTap m;
    m.begin(Alphabet::Lower,2,100);
    if(m.pending!='a' || m.due(799) || !m.due(800)) return false;
    m.begin(Alphabet::Lower,2,200);if(m.pending!='b' || m.due(899)) return false;
    m.begin(Alphabet::Lower,2,300);if(m.pending!='c') return false;
    m.begin(Alphabet::Lower,2,400);if(m.pending!='2') return false;
    m.begin(Alphabet::Lower,2,500);if(m.pending!='a') return false;
    m.clear();if(m.pending || m.key!=-1 || m.due(9000)) return false;
    m.begin(Alphabet::Upper,2,1000);if(m.pending!='A') return false;
    m.clear();m.begin(Alphabet::Numbers,7,1000);if(m.pending!='7') return false;
    m.clear();m.begin(Alphabet::Lower,0,0);if(m.pending!=' ') return false;
    m.begin(Alphabet::Lower,0,10);if(m.pending!='0') return false;
    m.begin(Alphabet::Lower,0,20);if(m.pending!='\n') return false;
    m.begin(Alphabet::Lower,0,30);return m.pending=='\t';
}
constexpr bool wraps() {
    MultiTap m;m.begin(Alphabet::Lower,3,0xffffff00u);
    return !m.due(443u) && m.due(444u);
}
struct Harness {
    MultiTap compose; char output[16]{};unsigned n=0;
    constexpr void commit() { if(compose.pending) output[n++]=compose.pending;compose.clear(); }
    constexpr void tap(Alphabet mode,unsigned k,uint32_t t) {
        if(compose.key!=static_cast<int>(k) || compose.due(t)) commit();
        compose.begin(mode,k,t);if(mode==Alphabet::Numbers) commit();
    }
    constexpr void tick(uint32_t t) { if(compose.due(t)) commit(); }
};
constexpr bool sequence() {
    Harness h;h.tap(Alphabet::Lower,2,0);h.tap(Alphabet::Lower,2,100); // b
    h.tap(Alphabet::Lower,3,200); // commit b, pending d
    if(h.n!=1 || h.output[0]!='b') return false;
    h.tick(899);if(h.n!=1) return false;
    h.tick(900);if(h.n!=2 || h.output[1]!='d') return false;
    h.tap(Alphabet::Lower,2,1000);h.tap(Alphabet::Lower,2,1700); // timeout: aa, not b
    h.tick(2400);if(h.n!=4 || h.output[2]!='a' || h.output[3]!='a') return false;
    h.tap(Alphabet::Lower,3,2500);h.compose.clear();h.tick(5000); // cancel no host backspace
    h.tap(Alphabet::Numbers,9,5000);return h.n==5 && h.output[4]=='9' && !h.compose.pending;
}
static_assert(coversAscii() && asciiUnique() && allModesCoverAscii());
static_assert(same(ascii('A'),{0x04,Shift}) && same(ascii('0'),{0x27,0}));
static_assert(same(ascii('!'),{0x1e,Shift}) && same(ascii(')'),{0x27,Shift}));
static_assert(same(ascii('\n'),{0x28,0}) && same(ascii('\t'),{0x2b,0}));
static_assert(!ascii('\x01').code && !ascii('\x7f').code);
static_assert(sizeof(shortcuts)/sizeof(shortcuts[0])==12);
static_assert(same(shortcuts[0].win,{0x06,Ctrl}) && same(shortcuts[0].mac,{0x06,Gui}));
static_assert(same(shortcuts[1].win,{0x1b,Ctrl}) && same(shortcuts[1].mac,{0x1b,Gui}));
static_assert(same(shortcuts[2].win,{0x19,Ctrl}) && same(shortcuts[2].mac,{0x19,Gui}));
static_assert(same(shortcuts[3].win,{0x04,Ctrl}) && same(shortcuts[3].mac,{0x04,Gui}));
static_assert(same(shortcuts[4].win,{0x0f,Gui}) && same(shortcuts[4].mac,{0x14,Ctrl|Gui}));
static_assert(same(shortcuts[5].win,{0x28,0}) && same(shortcuts[5].mac,{0x28,0}));
static_assert(same(shortcuts[6].win,{0x29,0}) && same(shortcuts[6].mac,{0x29,0}));
static_assert(same(shortcuts[7].win,{0x52,0}) && same(shortcuts[7].mac,{0x52,0}));
static_assert(same(shortcuts[8].win,{0x1d,Ctrl}) && same(shortcuts[8].mac,{0x1d,Gui}));
static_assert(same(shortcuts[9].win,{0x50,0}) && same(shortcuts[9].mac,{0x50,0}));
static_assert(same(shortcuts[10].win,{0x51,0}) && same(shortcuts[10].mac,{0x51,0}));
static_assert(same(shortcuts[11].win,{0x4f,0}) && same(shortcuts[11].mac,{0x4f,0}));
static_assert(cycleAndCancel() && wraps() && sequence());

// Complete expected HID code + modifier table, indexed by ASCII 32..126.
// Coverage alone would not detect swapping punctuation or losing Shift.
constexpr uint8_t printableCodes[] = {
  0x2c,0x1e,0x34,0x20,0x21,0x22,0x24,0x34,0x26,0x27,0x25,0x2e,0x36,0x2d,0x37,0x38,
  0x27,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x33,0x33,0x36,0x2e,0x37,0x38,
  0x1f,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,
  0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x2f,0x31,0x30,0x23,0x2d,
  0x35,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,
  0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x2f,0x31,0x30,0x35
};
constexpr bool exactPrintableAscii() {
  constexpr char shifted[] = R"hid(ABCDEFGHIJKLMNOPQRSTUVWXYZ!"#$%&()*+:<>?@^_{|}~)hid";
  for (int c=32;c<=126;++c) {
    bool shift=false;
    for (char s : shifted) if (s==c) shift=true;
    auto key=ascii(static_cast<char>(c));
    if (key.code!=printableCodes[c-32] || key.modifiers!=(shift?Shift:0)) return false;
  }
  return true;
}
static_assert(sizeof(printableCodes)==95 && exactPrintableAscii());
