#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace core::garden {
constexpr unsigned GrowthTurns=24,Routes=6,Forms=12,EventCount=12;
constexpr int HungerUse=4,MoodUse=2,EnergyUse=3;
constexpr unsigned TrainingEnergy=13,ExploreEnergy=8,MemoryEnergy=3;
constexpr uint8_t None=255;
enum class Action : uint8_t { Feed,Rest,Clean,Cuddle,Cook,Plant,Harvest,Train,Explore,Choose,Play,Guess,Decorate,Rebirth };
struct State {
    uint32_t rng=1,seed=1,actions=0;
    uint16_t generation=1,turn=0,fruit=8,stars=0,album=0,careSum=0;
    std::array<uint16_t,Routes> affinity{};
    uint16_t eventsSeen=0;
    uint8_t hunger=80,mood=80,energy=80,trait=0,weather=0,biome=0;
    uint8_t crop=None,relics=0,decorations=0,decor=0,form=None;
    uint8_t event=None,miniLength=0,miniPosition=0;
    std::array<uint8_t,6> melody{};
};
struct Route {const char *name,*practice,*normal,*rare,*hint;uint32_t color;};
inline constexpr Route RouteData[Routes]={
    {"花匠","园艺","绒叶兔","花冠兔","园艺与种植，让自然倾向成长。",0x74B878},
    {"守卫","体能","岩角熊","琥珀熊","体能训练，守护探索中的伙伴。",0xD8A15A},
    {"学者","阅读","书羽鸮","星图鸮","阅读与研究，积累智慧。",0x9596D4},
    {"游侠","寻宝","风尾狐","极光狐","寻宝与探索，拥抱未知。",0xEFAB83},
    {"乐师","乐理","音符雀","虹羽雀","乐理与记忆游戏，练习节奏。",0xEC96B1},
    {"星灵","冥想","月绒灵","流光灵","冥想与陪伴，建立亲密。",0x78C7CC}
};
inline constexpr const char *Traits[]={"温柔","勇敢","聪慧","好奇","活泼","宁静"};
inline constexpr const char *Biomes[]={"林间","海湾","云丘"};
inline constexpr const char *Weather[]={"晴日","细雨","星夜"};
struct Encounter {const char *title,*story,*left,*right;uint8_t leftRoute,rightRoute;};
inline constexpr Encounter Encounters[EventCount]={
    {"迷路的萤火虫","小灯在苔藓间打转。它想回家，也想听一个故事。","护送回家","讲个故事",1,5},
    {"古树的信","树洞藏着一封旧信。旁边的藤蔓正等待照料。","读旧信","整理藤蔓",2,0},
    {"蘑菇舞会","雨后的蘑菇邀请你加入舞会，或寻找迷失的鼓点。","一起合奏","寻找鼓点",4,3},
    {"种子的约定","一颗会发光的种子落在掌心。你想怎样照顾它？","栽进花园","静静陪伴",0,5},
    {"贝壳电台","海风从贝壳里送来旋律，也带来一道星空谜题。","接续旋律","破解谜题",4,2},
    {"搁浅的小船","浪花托着一艘小船。它的主人站在礁石的另一边。","搬动小船","寻找小路",1,3},
    {"潮汐花园","潮水退去，露出柔软的小花和一只孤单的水母。","照料小花","陪伴水母",0,5},
    {"漂流瓶","瓶中是一张不完整的地图。背面还画着有趣的符号。","顺图寻宝","研究符号",3,2},
    {"云上邮差","邮差的包裹散落在云间。他需要勇气，也需要智慧。","跨过云桥","设计滑索",1,2},
    {"风铃山坡","轻风推着铃声滚向远方。花朵也随之醒来。","和风歌唱","照料花朵",4,0},
    {"小小流星","一颗流星想知道，哪里才是这片天空的尽头。","一起冒险","许下心愿",3,5},
    {"守夜的约定","云上的朋友们睡着了。你可以守护他们，也可以送一首晚安曲。","安静守护","弹奏晚安",1,4}
};
struct Result {bool changed=false;const char *message="";};
uint32_t random(State &state);
State create(uint32_t seed);
unsigned stage(const State &state);
unsigned route(const State &state);
unsigned care(const State &state);
bool valid(const State &state);
Result apply(State &state,Action action,unsigned choice=0);
// Fixed-size, little-endian, checksummed records; no struct padding on disk.
using Record=std::array<uint8_t,160>;
Record encode(const State &state,uint64_t revision);
bool decode(const Record &record,State &state,uint64_t &revision);
}
