#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace apps::random_model {
constexpr int MinInteger=-999999, MaxInteger=999999;
constexpr unsigned MaxChoices=16, RedCount=6, RedMax=33, BlueMax=16;
constexpr int Width=240, BodyHeight=162, ParameterHeight=36, Gap=1;

inline bool parseInteger(const char *text,int &result)
{
    if(!text || !*text) return false;
    const bool negative=*text=='-';
    if(negative) ++text;
    if(!*text) return false;
    int value=0;
    for(;*text;++text) {
        if(*text<'0' || *text>'9') return false;
        const int digit=*text-'0';
        if(value>(MaxInteger-digit)/10) return false;
        value=value*10+digit;
    }
    result=negative?-value:value;
    return true;
}

// Rejection sampling removes the modulo bias. No radio/ADC ownership needed.
// The caller provides esp_random; this is a leisure App, not a key generator.
template<class Rng> uint32_t below(uint32_t count,Rng &rng)
{
    if(count<=1) return 0;
    const uint32_t threshold=(uint32_t{0}-count)%count;
    uint32_t value;
    do { value=rng(); } while(value<threshold);
    return value%count;
}
template<class Rng> int integer(int low,int high,Rng &rng)
{
    return low+static_cast<int>(below(static_cast<uint32_t>(high-low+1),rng));
}
struct Lottery {
    std::array<uint8_t,RedCount> red{};
    uint8_t blue=0;
};
template<class Rng> Lottery lottery(Rng &rng)
{
    std::array<uint8_t,RedMax> pool{};
    for(unsigned i=0;i<RedMax;++i) pool[i]=i+1;
    Lottery result;
    for(unsigned i=0;i<RedCount;++i) {
        const unsigned next=i+below(RedMax-i,rng);
        std::swap(pool[i],pool[next]); result.red[i]=pool[i];
    }
    std::sort(result.red.begin(),result.red.end());
    result.blue=1+below(BlueMax,rng);
    return result;
}
struct Cell { int x,y,width,height; };
// Fill every row, including a short final row, with equal tracks (within 1px).
constexpr Cell cell(unsigned count,unsigned index,int height)
{
    if(!count || count>MaxChoices || index>=count) return {0,0,0,0};
    const unsigned columns=count<=3?count:count==4?2:count<=9?3:4;
    const unsigned rows=(count+columns-1)/columns, row=index/columns;
    const unsigned remaining=count-row*columns;
    const unsigned inRow=remaining<columns?remaining:columns, col=index%columns;
    const int usableW=Width-int(inRow-1)*Gap, usableH=height-int(rows-1)*Gap;
    const int x=usableW*int(col)/int(inRow)+int(col)*Gap;
    const int y=usableH*int(row)/int(rows)+int(row)*Gap;
    return {x,y,usableW*int(col+1)/int(inRow)+int(col)*Gap-x,
                usableH*int(row+1)/int(rows)+int(row)*Gap-y};
}
constexpr std::array<uint16_t,6> DicePips={0x010,0x101,0x111,0x145,0x155,0x16D};
} // namespace apps::random_model
