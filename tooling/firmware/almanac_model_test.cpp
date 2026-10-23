#include "core/almanac_model.hpp"
using namespace calendar;
constexpr bool sameText(const char *a,const char *b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
constexpr auto today=dateAlmanac({2026,9,24});
static_assert(today.lunarDate.month==8&&today.lunarDate.day==14);
static_assert(today.year.cycle()==42&&today.month.cycle()==33&&today.day.cycle()==37); // 丙午 丁酉 辛丑
static_assert(sameText(constellation({2026,9,24}),"天秤座"));
static_assert(sameText(constellation({2026,9,22}),"处女座")&&sameText(constellation({2026,9,23}),"天秤座"));
static_assert(sameText(constellation({2026,10,23}),"天秤座")&&sameText(constellation({2026,10,24}),"天蝎座"));
static_assert(sameText(constellation({2026,12,22}),"摩羯座")&&sameText(constellation({2026,1,19}),"摩羯座"));
static_assert(!almanac({1900,12,31},12)&&!almanac({2100,1,1},0));
static_assert(!almanac({2026,2,30},0)&&!almanac({2026,9,24},24)&&!almanac({2026,9,24},0,60));
static_assert(bool(almanac({1901,1,1},0))&&bool(almanac({2099,12,31},23,59,59)));
static_assert(almanac({2026,1,1},0).solarTerm==23);
static_assert(almanac({2026,9,24},22,59,59).day.cycle()==37);
static_assert(almanac({2026,9,24},23).day.cycle()==38);
static_assert(almanac({2026,9,25},0).day.cycle()==38);
static_assert(almanac({2026,9,24},23).time.cycle()==almanac({2026,9,25},0).time.cycle());
static_assert(almanac({2026,9,24},23).lunarDate.day==14&&almanac({2026,9,25},0).lunarDate.day==15);
static_assert(dateAlmanac({2026,9,24}).day.cycle()==37);
constexpr auto afternoon=almanac({2026,9,24},15,36);
static_assert(sameText(StemElements[afternoon.time.stem],"阳火"));
static_assert(sameText(BranchElements[afternoon.time.branch],"阳金"));
static_assert(sameText(NaYin[afternoon.time.cycle()/2],"山下火"));
constexpr auto evening=almanac({2026,9,24},17);
static_assert(sameText(StemElements[evening.time.stem],"阴火"));
static_assert(sameText(BranchElements[evening.time.branch],"阴金"));
static_assert(sameText(NaYin[evening.time.cycle()/2],"山下火"));
constexpr auto midnight=almanac({2026,9,24},23);
static_assert(sameText(StemElements[midnight.time.stem],"阳金"));
static_assert(sameText(BranchElements[midnight.time.branch],"阳水"));
static_assert(sameText(NaYin[midnight.time.cycle()/2],"壁上土"));
static_assert(sameText(BranchElements[5],"阴火")&&sameText(BranchElements[6],"阳火"));
static_assert(sameText(BranchElements[1],"阴土")&&sameText(BranchElements[10],"阳土"));
constexpr bool tableConsistency(){
    for(int y=FirstYear;y<=LastYear;++y)for(int i=0;i<24;++i){
        const int sec=data::termSeconds[y-FirstYear][i];
        const Date d{y,i/2+1,data::terms[y-FirstYear][i]};
        if(sec<yearSecond(d)||sec>=yearSecond(d)+86400)return false;
        if(i&&sec<=int(data::termSeconds[y-FirstYear][i-1]))return false;
    }return true;
}
static_assert(tableConsistency());
