#include "garden_model.hpp"
#include <algorithm>
#include <cstring>

namespace core::garden {
namespace {
void add(uint8_t &v,int n){v=uint8_t(std::max(0,std::min(100,int(v)+n)));}
void points(State &s,unsigned at,unsigned n){s.affinity[at]=uint16_t(std::min(999u,unsigned(s.affinity[at])+n));}
void fruit(State &s,unsigned n){s.fruit=uint16_t(std::min(999u,unsigned(s.fruit)+n));}
void stars(State &s,unsigned n){s.stars=uint16_t(std::min(999u,unsigned(s.stars)+n));}
void grow(State &s,int hunger,int mood,int energy,bool cropTick){
    // Settle the action and turn costs together, then clamp once. At 100,
    // feeding/resting must not lose their benefit before the turn is charged.
    add(s.hunger,hunger-HungerUse);add(s.mood,mood-MoodUse);add(s.energy,energy-EnergyUse);
    if(cropTick && s.crop!=None && s.crop<4)++s.crop;
    if(s.actions<1000000)++s.actions;
    if(s.turn<GrowthTurns){
        ++s.turn;s.careSum+=uint16_t((unsigned(s.hunger)+s.mood+s.energy)/3);
        if(s.turn==GrowthTurns){
            unsigned r=route(s);const bool rare=care(s)>=65 && (s.relics&(1u<<r));
            s.form=uint8_t(r*2+unsigned(rare));s.album|=uint16_t(1u<<s.form);stars(s,rare?8:5);
        }
    }
}
uint32_t checksum(const uint8_t *p,size_t n){
    uint32_t crc=0xffffffffu;
    for(size_t i=0;i<n;++i){crc^=p[i];for(unsigned b=0;b<8;++b)crc=(crc>>1)^(0xedb88320u&uint32_t(-int32_t(crc&1)));}
    return ~crc;
}
}
uint32_t random(State &s){uint32_t x=s.rng;x^=x<<13;x^=x>>17;x^=x<<5;s.rng=x?x:0x6d2b79f5;return s.rng;}
State create(uint32_t seed){
    State s;s.seed=s.rng=seed?seed:0x6d2b79f5;
    s.trait=uint8_t(random(s)%Routes);s.biome=uint8_t(random(s)%3);s.weather=uint8_t(random(s)%3);
    for(auto &v:s.affinity)v=uint16_t(random(s)%3);
    points(s,s.trait,4);return s;
}
unsigned stage(const State &s){return s.turn<6?0:s.turn<14?1:s.turn<GrowthTurns?2:3;}
unsigned route(const State &s){
    unsigned best=s.trait;
    for(unsigned i=0;i<Routes;++i)if(s.affinity[i]>s.affinity[best])best=i;
    return best;
}
unsigned care(const State &s){return s.turn?s.careSum/s.turn:(unsigned(s.hunger)+s.mood+s.energy)/3;}
bool valid(const State &s){
    if(!s.rng || !s.seed || !s.generation || s.turn>GrowthTurns || s.actions>1000000 || s.actions<s.turn ||
       s.fruit>999 || s.stars>999 || s.album>=1u<<Forms || s.eventsSeen>=1u<<EventCount ||
       s.hunger>100 || s.mood>100 || s.energy>100 || s.trait>=Routes || s.weather>=3 || s.biome>=3 ||
       (s.crop!=None && s.crop>4) || s.relics>=64 || s.decorations>=8 || s.decor>3 ||
       (s.decor && !(s.decorations&(1u<<(s.decor-1)))) || s.careSum>s.turn*100 ||
       (s.form!=None && s.form>=Forms) || (s.turn==GrowthTurns)!=(s.form!=None) ||
       (s.form!=None && !(s.album&(1u<<s.form))) || (s.event!=None && s.event>=EventCount) ||
       s.miniLength>6 || (s.miniLength && s.miniLength<3) ||
       (s.miniLength?s.miniPosition>=s.miniLength:s.miniPosition!=0) ||
       (s.event!=None && s.miniLength))return false;
    for(auto v:s.affinity)if(v>999)return false;
    for(auto v:s.melody)if(v>3)return false;
    return true;
}
Result apply(State &s,Action action,unsigned choice){
    if(!valid(s))return {false,"存档数据异常"};
    if(s.event!=None && action!=Action::Choose)return {false,"请先完成这次奇遇"};
    if(s.miniLength && action!=Action::Guess)return {false,"请先完成记忆游戏"};
    const bool adult=s.form!=None;
    bool turn=true;int hunger=0,mood=0,energy=0;const char *message="";
    switch(action){
    default:return {false,"未知操作"};
    case Action::Feed:
        if(!s.fruit)return {false,"果子不足，探索可以找到果子"};
        --s.fruit;hunger=35;mood=3;points(s,0,2);message="果子真甜，吃饱啦";break;
    case Action::Rest:
        energy=38;mood=3;message="小憩片刻，恢复精神";break;
    case Action::Clean:
        mood=22;points(s,0,5);message="洗香香，心情变好了";break;
    case Action::Cuddle:
        mood=20;points(s,5,10);message="贴贴，让彼此更亲密";break;
    case Action::Cook:
        if(s.fruit<2)return {false,"料理需要 2 个果子"};
        s.fruit-=2;hunger=55;energy=12;mood=10;points(s,2,3);message="暖暖料理，元气满满";break;
    case Action::Plant:
        if(s.crop!=None)return {false,"这块田已经种下了种子"};
        if(s.fruit<2)return {false,"播种需要 2 个果子"};
        s.fruit-=2;s.crop=0;points(s,0,10);message="种子种下了，4 回合可收获";break;
    case Action::Harvest:
        if(s.crop==None || s.crop<4)return {false,"果树还没有成熟"};
        s.crop=None;fruit(s,6);points(s,0,8);message="丰收啦，获得 6 个果子";break;
    case Action::Train:
        if(choice>=Routes)return {};
        if(s.energy<TrainingEnergy)return {false,"体力不足，先休息一下"};
        energy=EnergyUse-int(TrainingEnergy);points(s,choice,14);mood=choice==s.trait?8:3;message="练习完成，成长倾向提升";break;
    case Action::Explore:{
        if(choice>=3)return {};
        if(s.energy<ExploreEnergy)return {false,"体力不足，先休息一下"};
        s.biome=uint8_t(choice);const unsigned mask=15u<<(choice*4);
        if((s.eventsSeen&mask)==mask)s.eventsSeen&=uint16_t(~mask);
        unsigned id=choice*4+random(s)%4;
        while(s.eventsSeen&(1u<<id))id=choice*4+(id+1)%4;
        s.eventsSeen|=uint16_t(1u<<id);s.event=uint8_t(id);s.weather=uint8_t(random(s)%3);
        return {true,"遇见了新的朋友"};}
    case Action::Choose:{
        if(s.event==None || choice>1)return {};
        const auto &e=Encounters[s.event];const unsigned r=choice?e.rightRoute:e.leftRoute;
        points(s,r,12);points(s,3,3);energy=EnergyUse-int(ExploreEnergy);mood=10;fruit(s,2+random(s)%3);
        // A route's first encounter always grants its keepsake: rare forms are achievable, not a lottery.
        s.relics|=uint8_t(1u<<r);s.event=None;message="获得信物与果子，留下新回忆";break;}
    case Action::Play:
        if(s.energy<MemoryEnergy)return {false,"体力不足，先休息一下"};
        s.miniLength=uint8_t(3+stage(s));s.miniPosition=0;
        for(auto &v:s.melody)v=uint8_t(random(s)%4);
        return {true,"记住亮起的顺序，再依次点击"};
    case Action::Guess:
        if(!s.miniLength || choice>3)return {};
        if(choice!=s.melody[s.miniPosition]){
            s.miniLength=s.miniPosition=0;mood=6;points(s,4,3);message="差一点点，下次再试";
        }else if(++s.miniPosition<s.miniLength)return {true,"记得真准，继续"};
        else {s.miniLength=s.miniPosition=0;mood=24;points(s,4,14);fruit(s,3);stars(s,1);message="全对啦，获得果子和星星";}
        break;
    case Action::Decorate:
        if(choice>=3)return {};
        if(!(s.decorations&(1u<<choice))){if(s.stars<4)return {false,"布置需要 4 颗星星"};s.stars-=4;s.decorations|=uint8_t(1u<<choice);}
        s.decor=uint8_t(choice+1);turn=false;message="家园换上了新装";break;
    case Action::Rebirth:{
        if(s.form==None)return {false,"成年后才能传承新生"};
        const unsigned inherited=s.form/2;auto next=create(random(s));
        next.generation=s.generation==65535?65535:uint16_t(s.generation+1);next.album=s.album;
        next.stars=s.stars;next.decorations=s.decorations;next.decor=s.decor;
        points(next,inherited,5);s=next;return {true,"新旅程开始了，家园和图鉴会保留"};}
    }
    if(turn)grow(s,hunger,mood,energy,action!=Action::Plant);
    return {true,!adult && s.form!=None?"成年啦，新的形态已加入图鉴":message};
}
Record encode(const State &s,uint64_t revision){
    Record out{};if(!valid(s) || !revision)return out;
    std::memcpy(out.data(),"YMGARD01",8);size_t p=8;
    auto put=[&](uint64_t value,unsigned bytes){for(unsigned i=0;i<bytes;++i)out[p++]=uint8_t(value>>(8*i));};
    put(1,2);put(out.size(),2);put(revision,8);
    for(auto v:{s.rng,s.seed,s.actions})put(v,4);
    for(auto v:{s.generation,s.turn,s.fruit,s.stars,s.album,s.careSum})put(v,2);
    for(auto v:s.affinity)put(v,2);
    put(s.eventsSeen,2);
    for(auto v:{s.hunger,s.mood,s.energy,s.trait,s.weather,s.biome,s.crop,s.relics,s.decorations,s.decor,s.form,s.event,s.miniLength,s.miniPosition})put(v,1);
    for(auto v:s.melody)put(v,1);
    const auto crc=checksum(out.data(),out.size()-4);p=out.size()-4;put(crc,4);return out;
}
bool decode(const Record &in,State &out,uint64_t &revision){
    if(std::memcmp(in.data(),"YMGARD01",8))return false;
    size_t p=in.size()-4;auto get=[&](unsigned bytes){uint64_t v=0;for(unsigned i=0;i<bytes;++i)v|=uint64_t(in[p++])<<(8*i);return v;};
    if(get(4)!=checksum(in.data(),in.size()-4))return false;
    p=8;if(get(2)!=1 || get(2)!=in.size())return false;
    const uint64_t seq=get(8);if(!seq)return false;State s;
    s.rng=uint32_t(get(4));s.seed=uint32_t(get(4));s.actions=uint32_t(get(4));
    for(auto v:{&s.generation,&s.turn,&s.fruit,&s.stars,&s.album,&s.careSum})*v=uint16_t(get(2));
    for(auto &v:s.affinity)v=uint16_t(get(2));
    s.eventsSeen=uint16_t(get(2));
    for(auto v:{&s.hunger,&s.mood,&s.energy,&s.trait,&s.weather,&s.biome,&s.crop,&s.relics,&s.decorations,&s.decor,&s.form,&s.event,&s.miniLength,&s.miniPosition})*v=uint8_t(get(1));
    for(auto &v:s.melody)v=uint8_t(get(1));
    for(;p<in.size()-4;++p)if(in[p])return false;
    if(!valid(s))return false;
    out=s;revision=seq;return true;
}
}
