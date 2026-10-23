#include "core/calendar_model.hpp"
using namespace calendar;
constexpr bool isLunar(Date d,int y,int m,int day,bool leap=false){const auto l=lunar(d);return l.year==y&&l.month==m&&l.day==day&&l.leap==leap;}
constexpr bool same(const char *a,const char *b){while(*a&&*a==*b){++a;++b;}return *a==*b;}
static_assert(isLunar({1901,1,1},1900,11,11));
static_assert(isLunar({2026,2,16},2025,12,29));
static_assert(isLunar({2026,2,17},2026,1,1));
static_assert(isLunar({2026,9,25},2026,8,15));
static_assert(isLunar({2023,3,22},2023,2,1,true));
static_assert(isLunar({2033,12,22},2033,11,1,true));
static_assert(isLunar({2034,2,19},2034,1,1));
static_assert(bool(lunar({2099,12,31}))&&!lunar({2100,1,1}));
static_assert(same(festival({2026,2,16}),"除夕")&&same(festival({2026,2,17}),"春节"));
static_assert(same(term({2026,12,22}),"冬至")&&same(term({2026,6,21}),"夏至"));
static_assert(weekday({2026,9,24})==4);
static_assert(valid({2000,2,29})&&!valid({1900,2,29})&&!valid({2100,2,29}));
static_assert(equal(moveMonth({2024,1,31},1),{2024,2,29}));
static_assert(equal(moveMonth({1901,1,1},-2),{1901,1,1}));
static_assert(equal(moveMonth({2099,12,31},2),{2099,12,31}));
// All table years have 12/13 months and valid Gregorian term days.
constexpr bool tableBounds(){
    for(int y=1900;y<=2099;++y){const auto t=data::years[y-1900];if(t.leap>12||t.newYearOffset<20||t.newYearOffset>50)return false;}
    for(int y=1901;y<=2099;++y)for(int i=0;i<24;++i)if(data::terms[y-1901][i]<1||data::terms[y-1901][i]>monthDays(y,i/2+1))return false;
    return true;
}
static_assert(tableBounds());

// Day-detail swipes cross leap days/months/years and clamp at table endpoints.
static_assert(equal(moveDay({2024,2,28},1),{2024,2,29}));
static_assert(equal(moveDay({2024,3,1},-1),{2024,2,29}));
static_assert(equal(moveDay({2025,3,1},-1),{2025,2,28}));
static_assert(equal(moveDay({2026,12,31},1),{2027,1,1}));
static_assert(equal(moveDay({2027,1,1},-1),{2026,12,31}));
static_assert(equal(moveDay({1901,1,1},-1),{1901,1,1}));
static_assert(equal(moveDay({2099,12,31},1),{2099,12,31}));
static_assert(equal(moveDay({2024,2,28},2),{2024,3,1}));
static_assert(equal(moveDay({2024,3,1},-2),{2024,2,28}));
static_assert(equal(moveDay({2026,9,30},0),{2026,9,30}));
