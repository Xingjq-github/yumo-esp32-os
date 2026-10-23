#include "core/wake_click.h"
constexpr wake_click_t feed(wake_click_t s,bool down,unsigned ms) {
    for(unsigned i=0;i<ms;i+=2)wake_click_sample(&s,down);
    return s;
}
constexpr wake_click_t start{WAKE_FIRST_DOWN,0,0};
constexpr bool validClicks(unsigned press,unsigned gap) {
    auto s=feed(start,true,press);s=feed(s,false,gap);s=feed(s,true,press);s=feed(s,false,30);
    return s.phase==WAKE_ACCEPT;
}
static_assert(validClicks(40,40));static_assert(validClicks(120,140));static_assert(validClicks(300,300));
static_assert(feed(feed(start,true,80),false,600).phase==WAKE_REJECT,"single click stays asleep");
static_assert(feed(start,true,1000).phase==WAKE_REJECT,"held key does not boot");
static_assert(feed(feed(start,true,10),false,20).phase==WAKE_REJECT,"short noise is rejected");
static_assert(!validClicks(60,500),"slow second press is not a double click");
constexpr bool bounceDoesNotBoot(){
    auto s=feed(start,true,40);
    for(int i=0;i<20;++i){s=feed(s,false,4);s=feed(s,true,4);}
    if(s.phase!=WAKE_FIRST_UP)return false;
    s=feed(s,false,40);
    for(int i=0;i<20;++i){s=feed(s,true,4);s=feed(s,false,4);}
    return feed(s,false,600).phase==WAKE_REJECT;
}
static_assert(bounceDoesNotBoot());
constexpr bool secondMustRelease(){
    auto s=feed(feed(start,true,60),false,60);
    return feed(s,true,1000).phase==WAKE_REJECT;
}
static_assert(secondMustRelease());
