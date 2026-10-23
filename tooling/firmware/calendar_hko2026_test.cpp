// Independent published daily reference: Hong Kong Observatory T2026c.txt.
// Source https://www.hko.gov.hk/tc/gts/time/calendar/text/files/T2026c.txt
// SHA256 7b0831acb16a5a72a1ab553205e938b2f658c3837e06440f22c084e1a39832dd
// Expected FNV-1a covers lunar year/month/day/leap, weekday and term index,
// in Gregorian day order for all 365 days. Oracle is independent of lunar-python.
#include "core/calendar_model.hpp"
constexpr uint32_t calendar2026Digest(){
    using namespace calendar;uint32_t h=2166136261u;Date d{2026,1,1};
    while(d.year==2026){const auto l=lunar(d);unsigned ti=0;
        for(unsigned k=0;k<24;++k)if(int(k/2)+1==d.month&&data::terms[2026-FirstYear][k]==d.day)ti=k+1;
        const unsigned values[]={unsigned(l.year),unsigned(l.month),unsigned(l.day),unsigned(l.leap),unsigned(weekday(d)),ti};
        for(auto v:values){h=(h^v)*16777619u;}
        d=next(d);
    }return h;
}
static_assert(calendar2026Digest()==3458105169u);
