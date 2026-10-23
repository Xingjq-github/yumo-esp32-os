#include "core/pattern_play.hpp"
#include <cstdlib>
#include <string>
#include <set>
using core::patterns::play::Game;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#include "contract.inc"
static void actions(Game &g,std::initializer_list<unsigned> list){for(auto a:list)g.act(a);}
static void recover(Game g){
    std::set<core::patterns::play::State> seen;
    for(unsigned step=0;step<40&&!g.won;++step){
        int a=g.hint();if(a<0)return; // Irreversible excess needs explicit undo.
        if(!seen.insert(g.v).second){std::fprintf(stderr,"hint loop id=%u stage=%u action=%d\n",g.id,g.stage,a);CHECK(false);}
        CHECK(a<=4&&(a==4||g.editable(unsigned(a))));g.act(unsigned(a));CHECK(!g.error);
    }
    CHECK(g.won);
}
static void bugs(){
    Game g;char text[128];
    // Restoring before editing must never validate a later hand-edited value.
    g.reset(20,1);actions(g,{1,2,0,0,0,4});CHECK(!g.won&&!g.v[2]);
    g.reset(20,2);actions(g,{0,1,2,0,0,0,4});CHECK(!g.won&&!g.v[2]);
    g.reset(20,1);actions(g,{1,0,2,0,0,0,4});CHECK(!g.won&&!g.v[2]);
    g.reset(20,2);actions(g,{0,1,0,2,4});CHECK(g.won&&g.v[0]==1&&g.v[1]==1);
    // Selecting before connection is not an event delivered through Dialog.
    g.reset(19,1);actions(g,{1,2,0,4});CHECK(!g.won&&g.v[3]==0);recover(g);
    g.reset(19,2);actions(g,{2,1,0,4});CHECK(!g.won&&g.v[5]==0);recover(g);
    g.reset(19,1);actions(g,{0,2,1,4});CHECK(!g.won);actions(g,{2,4});CHECK(g.won);
    // Selecting another factory leaves existing products and delivery intact.
    g.reset(4,2);g.value(2,text,sizeof(text));CHECK(std::string(text)=="尚未创建");
    actions(g,{0,4});CHECK(g.v[3]==1&&g.v[2]==0);g.act(1);
    CHECK(g.v[3]==1&&g.v[2]==0&&g.v[1]==1&&g.v[4]==1);
    g.value(2,text,sizeof(text));CHECK(std::string(text)=="Mail #1");
    actions(g,{0,4});CHECK(g.v[3]==2&&g.v[4]==1&&g.v[5]==1&&!g.won);
    // Build steps and abstract factory product creation may be reordered.
    for(unsigned stage=0;stage<3;++stage){
        g.reset(6,stage);for(unsigned j=0;j<stage;++j)g.act(0);actions(g,{2,1,4});CHECK(g.won);
        g.reset(5,stage);for(unsigned j=0;j<stage;++j)g.act(0);g.act(2);CHECK(g.hint()==1);actions(g,{1,4});CHECK(g.won);
    }
    g.reset(11,2);actions(g,{1,2});g.value(0,text,sizeof(text));CHECK(std::string(text)=="Coffee = 10");
    g.value(3,text,sizeof(text));CHECK(std::string(text)=="外层总价 13");g.act(4);CHECK(g.won);
    // Draft editing allocates nothing; a print job builds a new local pool.
    g.reset(13,0);actions(g,{1,0,1,0,0,1,2,3});CHECK(!g.v[4]);g.act(4);
    CHECK(g.won&&g.v[4]==1&&g.v[5]==2);
    g.reset(13,2);actions(g,{1,2,3,4});CHECK(!g.won&&g.v[4]==1);
    actions(g,{0,2,0,3,4});CHECK(g.won&&g.v[4]==3&&g.v[5]==0);
    g.reset(17,0);g.act(3);CHECK(g.hint()==1);actions(g,{2,1,4});CHECK(g.won&&g.v[4]==3);
    g.value(3,text,sizeof(text));CHECK(std::string(text)=="x+2 = 3");
    // Prepared structures, not an empty board labelled "keep the tree".
    for(unsigned s=1;s<3;++s){
        g.reset(17,s);CHECK(g.v[1]&&g.v[2]&&g.v[3]&&g.v[0]==int(s-1));actions(g,{0,4});CHECK(g.won);
        g.reset(25,s);CHECK(g.v[1]&&g.v[2]);actions(g,{0,4});CHECK(g.won);
        g.reset(12,s);CHECK(g.v[0]==(1<<s)-1);actions(g,{s+1,4});CHECK(g.won);
    }
    g.reset(9,2);CHECK(g.v[1]==1&&!g.v[0]);actions(g,{0,4});CHECK(g.won&&g.v[1]==1);
    g.reset(21,2);CHECK(g.v[3]==7&&g.v[4]==7);actions(g,{1,0,4});CHECK(g.won&&g.v[3]==7);
    g.reset(22,2);actions(g,{0,4,0});CHECK(g.error&&g.v[0]==2&&g.v[1]==1);g.act(4);CHECK(g.won&&g.v[1]==1);
}
static void explore(Game g,unsigned depth){
    recover(g);if(!depth||g.won)return;
    for(unsigned a=0;a<5;++a){if(a<4&&!g.editable(a))continue;Game next=g;next.act(a);explore(next,depth-1);}
}
int main(){contracts();bugs();for(unsigned id=0;id<26;++id)for(unsigned stage=0;stage<3;++stage){Game g;g.reset(id,stage);explore(g,5);}
    std::printf("%u semantic checks passed: 78 independent contracts, negative paths, legal reorderings, prepared workbenches and hint recovery from all prefixes up to 5 actions.\n",checks);
}
