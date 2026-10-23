#pragma once
#include "calendar_model.hpp"
#include "almanac_data.hpp"

// Traditional calendar convention: China civil time (UTC+8), exact solar-term
// instants for BaZi years/months, 23:00 starts the next BaZi day (sect 1).
// Civil/lunar dates still change at midnight. No fortune/prediction data.
namespace calendar {
constexpr int positiveMod(int n,int d){return (n%d+d)%d;}
struct Pillar {
    int stem=0,branch=0;
    constexpr int cycle() const {for(int i=0;i<60;++i)if(i%10==stem&&i%12==branch)return i;return 0;}
};
constexpr Pillar cyclePillar(int n){return {positiveMod(n,10),positiveMod(n,12)};}
inline constexpr const char *HourNames[]={"夜半","鸡鸣","平旦","日出","食时","隅中","日中","日昳","哺时","日入","黄昏","人定"};
inline constexpr const char *StarGods[]={"青龙","明堂","天刑","朱雀","金匮","天德","白虎","玉堂","天牢","玄武","司命","勾陈"};
inline constexpr const char *NaYin[]={"海中金","炉中火","大林木","路旁土","剑锋金","山头火","涧下水","城头土","白蜡金","杨柳木",
    "泉中水","屋上土","霹雳火","松柏木","长流水","沙中金","山下火","平地木","壁上土","金箔金",
    "覆灯火","天河水","大驿土","钗钏金","桑柘木","大溪水","沙中土","天上火","石榴木","大海水"};
// Stem/branch polarity alternates Yang/Yin in the same zero-based order as
// Stems/Branches. Earth branches keep their own element, not the NaYin one.
inline constexpr const char *StemElements[]={"阳木","阴木","阳火","阴火","阳土","阴土","阳金","阴金","阳水","阴水"};
inline constexpr const char *BranchElements[]={"阳水","阴土","阳木","阴木","阳土","阴火","阳火","阴土","阳金","阴金","阳土","阴水"};
inline constexpr const char *ChineseDigits[]={"零","一","二","三","四","五","六","七","八","九"};
constexpr const char *constellation(Date d) {
    if(!valid(d))return "";
    const int n=d.month*100+d.day;
    return n>=321&&n<=419?"白羊座":n>=420&&n<=520?"金牛座":n>=521&&n<=621?"双子座":
           n>=622&&n<=722?"巨蟹座":n>=723&&n<=822?"狮子座":n>=823&&n<=922?"处女座":
           n>=923&&n<=1023?"天秤座":n>=1024&&n<=1122?"天蝎座":n>=1123&&n<=1221?"射手座":
           n>=1222||n<=119?"摩羯座":n<=218?"水瓶座":"双鱼座";
}
constexpr int yearSecond(Date d,int hour=0,int minute=0,int second=0) {
    return (serial(d)-serial({d.year,1,1}))*86400+hour*3600+minute*60+second;
}
struct Almanac {
    Lunar lunarDate{};
    Pillar year{},month{},day{},time{};
    int solarTerm=23,timeGod=0;
    explicit constexpr operator bool() const {return bool(lunarDate);}
};
constexpr Almanac almanac(Date d,int hour,int minute=0,int second=0) {
    if(!valid(d)||hour<0||hour>23||minute<0||minute>59||second<0||second>59)return {};
    Almanac a{};a.lunarDate=lunar(d);
    const auto &terms=data::termSeconds[d.year-FirstYear];
    const int now=yearSecond(d,hour,minute,second);
    const int springYear=d.year-(now<int(terms[2]));
    a.year=cyclePillar(springYear-4);
    // January before XiaoHan is Zi (10), afterwards Chou (11). Tiger=0.
    int month=10;
    for(int i=0;i<24;++i)if(now>=int(terms[i])) {
        a.solarTerm=i;
        if(i%2==0)month=positiveMod(i/2-1,12);
    }
    a.month={(a.year.stem%5*2+2+month)%10,(month+2)%12};
    // Julian noon = serial + 1721425; upstream cycle offset is -11.
    a.day=cyclePillar(serial(d)+14+(hour==23));
    a.time={(a.day.stem%5*2+(hour+1)/2%12)%10,(hour+1)/2%12};
    constexpr int offsets[]={4,2,0,10,8,6,4,2,0,10,8,6};
    a.timeGod=(a.time.branch+offsets[a.day.branch])%12;
    return a;
}
// Date-only calendar view follows the whole civil day, with LiChun/Jie changing
// on their published dates. It never borrows the next day's late-Zi day pillar.
constexpr Almanac dateAlmanac(Date d) {
    auto a=almanac(d,0);if(!a)return a;
    const int end=yearSecond(d,23,59,59);
    const auto &terms=data::termSeconds[d.year-FirstYear];
    a.year=cyclePillar(d.year-(end<int(terms[2]))-4);
    int month=10;
    for(int i=0;i<24;++i)if(end>=int(terms[i])){a.solarTerm=i;if(i%2==0)month=positiveMod(i/2-1,12);}
    a.month={(a.year.stem%5*2+2+month)%10,(month+2)%12};
    return a;
}
} // namespace calendar
