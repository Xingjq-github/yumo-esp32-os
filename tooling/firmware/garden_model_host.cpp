#include "core/garden_model.hpp"
#include "core/garden_store.hpp"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <cstring>
namespace g=core::garden;
static void act(g::State &s,g::Action a,unsigned c=0){assert(g::apply(s,a,c).changed);assert(g::valid(s));}
static bool same(const g::State &a,const g::State &b){return g::encode(a,1)==g::encode(b,1);}
static g::State play(unsigned seed,unsigned route,bool rare){
    auto s=g::create(seed);
    for(unsigned steps=0;s.turn<g::GrowthTurns && steps<100;++steps){
        if(s.event!=g::None){auto &e=g::Encounters[s.event];act(s,g::Action::Choose,e.rightRoute==route?1:0);}
        else if(s.energy<40)act(s,g::Action::Rest);
        else if(s.hunger<65 && s.fruit>=2)act(s,g::Action::Cook);
        else if(rare && !(s.relics&(1<<route)))act(s,g::Action::Explore,route==4?1:0);
        else if(s.fruit<2)act(s,g::Action::Explore,0);
        else if(s.mood<65)act(s,g::Action::Clean);
        else act(s,g::Action::Train,route);
    }
    assert(s.turn==24 && s.form!=g::None);return s;
}
static void write(const std::string &path,const g::Record &r,size_t size=160){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char *>(r.data()),size);assert(f.good());}
static void numbers(){
    // Independent expected net values: the turn costs are hunger 4, mood 2, energy 3.
    struct Case {g::Action action;unsigned hunger,mood,energy,fruit;};
    const Case cases[]={{g::Action::Feed,71,41,37,7},{g::Action::Rest,36,41,75,8},
        {g::Action::Clean,36,60,37,8},{g::Action::Cuddle,36,58,37,8},{g::Action::Cook,91,48,49,6}};
    for(const auto &c:cases){
        auto s=g::create(73);s.hunger=s.mood=s.energy=40;act(s,c.action);
        assert(s.hunger==c.hunger && s.mood==c.mood && s.energy==c.energy && s.fruit==c.fruit);
        assert(s.turn==1 && s.actions==1 && s.careSum==(c.hunger+c.mood+c.energy)/3);
        s=g::create(73);s.hunger=s.mood=s.energy=100;act(s,c.action);
        if(c.action==g::Action::Feed || c.action==g::Action::Cook)assert(s.hunger==100);
        if(c.action==g::Action::Rest || c.action==g::Action::Cook)assert(s.energy==100);
        assert(s.mood==100); // Every care action benefits mood, including at the upper bound.
    }
    auto s=g::create(73);s.energy=12;auto before=s;assert(!g::apply(s,g::Action::Train,0).changed && same(s,before));
    s.energy=13;act(s,g::Action::Train,0);assert(s.energy==0);
    s=g::create(73);s.energy=7;before=s;assert(!g::apply(s,g::Action::Explore,0).changed && same(s,before));
    s.energy=8;const auto fruit=s.fruit;act(s,g::Action::Explore,0);assert(s.energy==8 && s.turn==0);
    act(s,g::Action::Choose,0);assert(s.energy==0 && s.turn==1 && s.fruit>=fruit+2 && s.fruit<=fruit+4);
    s=g::create(73);s.energy=2;before=s;assert(!g::apply(s,g::Action::Play).changed && same(s,before));
    s.energy=3;act(s,g::Action::Play);const unsigned count=s.miniLength;
    for(unsigned i=0;i<count;++i){act(s,g::Action::Guess,s.melody[s.miniPosition]);if(i+1<count)assert(s.energy==3 && s.turn==0 && s.stars==0);}
    assert(s.energy==0 && s.turn==1 && s.fruit==11 && s.stars==1);
    before=s;assert(!g::apply(s,g::Action::Guess,0).changed && same(s,before)); // Reward cannot repeat.
    s=g::create(73);act(s,g::Action::Play);act(s,g::Action::Guess,(s.melody[0]+1)%4);
    assert(s.hunger==76 && s.mood==84 && s.energy==77 && s.fruit==8 && !s.stars && s.turn==1);
    s=g::create(73);s.fruit=1;before=s;assert(!g::apply(s,g::Action::Cook).changed && same(s,before));
    assert(!g::apply(s,g::Action::Plant).changed && same(s,before));s.fruit=0;before=s;
    assert(!g::apply(s,g::Action::Feed).changed && same(s,before));
    s=g::create(73);s.fruit=s.stars=999;act(s,g::Action::Play);
    while(s.miniLength)act(s,g::Action::Guess,s.melody[s.miniPosition]);assert(s.fruit==999 && s.stars==999);
    s=g::create(73);while(s.turn<23)act(s,g::Action::Cuddle);const auto reward=s.stars;
    auto result=g::apply(s,g::Action::Rest);assert(result.changed && std::strstr(result.message,"成年"));
    assert(s.stars==reward+5);result=g::apply(s,g::Action::Decorate,0);
    assert(result.changed && !std::strstr(result.message,"成年") && s.stars==reward+1);
    before=s;act(s,g::Action::Decorate,0);assert(same(s,before)); // Owned decor costs no stars/turn.
    std::cout<<"PASS: independent action deltas, full-stat care, exact energy gates, rejected-action invariance, single settlement/reward, resource caps, owned decor and one-time adulthood.\n";
}
int main(){
    numbers();
    std::set<unsigned> forms,traits;std::set<uint32_t> seeds;
    for(unsigned seed=1;seed<=600;++seed){
        for(unsigned route=0;route<6;++route)for(bool rare:{false,true}){
            auto s=play(seed,route,rare);auto again=play(seed,route,rare);assert(same(s,again));
            forms.insert(s.form);traits.insert(s.trait);seeds.insert(s.seed);
            auto record=g::encode(s,seed);g::State decoded;uint64_t revision=0;
            assert(g::decode(record,decoded,revision) && revision==seed && same(decoded,s));
            auto album=s.album;auto stars=s.stars;auto inherited=s.form/2;auto oldAffinity=s.affinity;
            act(s,g::Action::Rebirth);assert(s.generation==2 && s.turn==0 && s.album==album && s.stars==stars && s.affinity[inherited]>=5);
            assert(s.affinity!=oldAffinity);
        }
    }
    assert(forms.size()==12 && traits.size()==6 && seeds.size()==600);
    for(unsigned r=0;r<6;++r){
        bool normal=false,rare=false;
        for(unsigned seed=1;seed<601;++seed){normal|=play(seed,r,false).form==r*2;rare|=play(seed,r,true).form==r*2+1;}
        assert(normal && rare);
    }
    auto s=g::create(100);auto original=s;auto encoded=g::encode(s,10);g::State decoded;uint64_t revision=0;
    for(unsigned i=0;i<encoded.size();++i){auto bad=encoded;bad[i]^=1;assert(!g::decode(bad,decoded,revision));}
    assert(!g::apply(s,g::Action(255)).changed && same(original,s));
    s.miniLength=6;s.miniPosition=6;assert(!g::valid(s));s=original;
    for(unsigned biome=0;biome<3;++biome){
        std::set<unsigned> seen;
        for(unsigned i=0;i<4;++i){s.energy=100;act(s,g::Action::Explore,biome);assert(seen.insert(s.event).second);
            auto waiting=s;assert(!g::apply(s,g::Action::Explore,biome).changed && same(s,waiting));
            auto record=g::encode(s,2);assert(g::decode(record,decoded,revision) && decoded.event==s.event);act(s,g::Action::Choose,i%2);}
    }
    s=g::create(99);act(s,g::Action::Play);const auto length=s.miniLength;
    for(unsigned i=0;i<length;++i){assert(s.turn==0);auto record=g::encode(s,3);assert(g::decode(record,decoded,revision));s=decoded;act(s,g::Action::Guess,s.melody[s.miniPosition]);}
    assert(s.turn==1 && !s.miniLength && s.stars==1);
    act(s,g::Action::Play);act(s,g::Action::Guess,(s.melody[0]+1)%4);assert(s.turn==2 && !s.miniLength);
    s=g::create(99);act(s,g::Action::Plant);assert(s.crop==0);assert(!g::apply(s,g::Action::Harvest).changed);
    for(unsigned i=1;i<=4;++i){act(s,g::Action::Rest);assert(s.crop==i);if(i<4)assert(!g::apply(s,g::Action::Harvest).changed);}
    act(s,g::Action::Harvest);assert(s.fruit==12);auto harvested=s;assert(!g::apply(s,g::Action::Harvest).changed && same(s,harvested));
    s.stars=4;act(s,g::Action::Decorate,1);assert(!s.stars && s.decorations==2 && s.decor==2);act(s,g::Action::Decorate,1);
    s=g::create(7);s.hunger=s.mood=s.energy=0;s.fruit=0;act(s,g::Action::Rest);act(s,g::Action::Explore);act(s,g::Action::Choose);act(s,g::Action::Feed);assert(s.hunger>0);
    // Roundtrip at every operation across adversarial and random action choices.
    for(unsigned seed=1;seed<=200;++seed){s=g::create(seed);uint32_t rng=seed;
        for(unsigned i=0;i<400;++i){rng=rng*1664525+1013904223;auto before=s;auto result=g::apply(s,g::Action(rng%16),(rng>>8)%7);
            assert(g::valid(s));if(!result.changed)assert(same(s,before));
            assert(g::decode(g::encode(s,1),decoded,revision) && same(s,decoded));}}
    std::filesystem::create_directory("store");g::store::Snapshot snapshot;
    assert(!g::store::load("store",snapshot) && !snapshot.exists);
    s=g::create(12);assert(!g::store::save("store",s,0,snapshot) && snapshot.revision==1);
    auto first=s;act(s,g::Action::Rest);assert(!g::store::save("store",s,1,snapshot) && snapshot.revision==2);
    assert(!g::store::save("store",s,1,snapshot) && snapshot.revision==2); // retry an unacknowledged write
    auto different=s;act(different,g::Action::Cuddle);assert(g::store::save("store",different,1,snapshot)==ESTALE);
    write("store/garden-b.sav",g::encode(s,2),40);assert(!g::store::load("store",snapshot) && snapshot.recovered && same(snapshot.state,first));
    assert(!g::store::save("store",s,1,snapshot) && snapshot.revision==2);
    {std::ofstream f("store/garden-b.sav",std::ios::binary|std::ios::app);f.put('X');}
    assert(!g::store::load("store",snapshot) && snapshot.recovered && snapshot.revision==1);
    auto newer=g::encode(s,3);newer[8]=2;write("store/garden-b.sav",newer);
    assert(g::store::load("store",snapshot)==ENOTSUP && g::store::save("store",s,1,snapshot)==ENOTSUP);
    write("store/garden-b.sav",g::encode(different,1));assert(g::store::load("store",snapshot)==EBADMSG);
    write("store/garden-b.sav",g::encode(first,1));assert(!g::store::load("store",snapshot));
    write("store/garden-a.sav",g::Record{},30);write("store/garden-b.sav",g::Record{},20);
    assert(g::store::load("store",snapshot)==EBADMSG && g::store::save("store",s,0,snapshot)==EBADMSG);
    // Failed target open leaves the valid peer unchanged.
    std::filesystem::create_directory("failed");write("failed/garden-a.sav",g::encode(first,1));std::filesystem::create_directory("failed/garden-b.sav");
    assert(g::store::save("failed",s,1,snapshot)!=0);
    std::ifstream bank("failed/garden-a.sav",std::ios::binary);g::Record intact;bank.read(reinterpret_cast<char *>(intact.data()),160);assert(intact==g::encode(first,1));
    std::cout<<"PASS: 7200 deterministic growth runs, all 12 forms and six directed routes, 80000 random operations, resumable encounters/memory, resources, inheritance, corrupt/truncated/oversize/future/ambiguous banks, stale revision, idempotent retry and failed-write preservation.\n";
}
