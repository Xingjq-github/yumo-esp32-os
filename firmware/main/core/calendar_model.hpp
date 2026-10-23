#pragma once
#include "calendar_data.hpp"
#include <cstdint>

namespace calendar {
constexpr int FirstYear=1901, LastYear=2099;
struct Date { int year,month,day; };
struct Lunar { int year=0,month=0,day=0; bool leap=false; explicit constexpr operator bool() const {return year!=0;} };
constexpr bool leapYear(int y) {return y%4==0&&(y%100!=0||y%400==0);}
constexpr int monthDays(int y,int m) {
    constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return m<1||m>12?0:days[m-1]+(m==2&&leapYear(y));
}
constexpr bool valid(Date d) {return d.year>=FirstYear&&d.year<=LastYear&&d.day>=1&&d.day<=monthDays(d.year,d.month);}
// Proleptic Gregorian day number; 0001-01-01 is Monday.
constexpr int serial(Date d) {
    const int y=d.year-1;int n=365*y+y/4-y/100+y/400+d.day;
    for(int m=1;m<d.month;++m)n+=monthDays(d.year,m);
    return n;
}
constexpr int weekday(Date d) {return serial(d)%7;} // Sun=0
constexpr bool equal(Date a,Date b) {return a.year==b.year&&a.month==b.month&&a.day==b.day;}
constexpr Date next(Date d) {
    if(++d.day>monthDays(d.year,d.month)){d.day=1;if(++d.month>12){d.month=1;++d.year;}}
    return d;
}
// Clamp at the supported table endpoints; never index an unsupported lunar year.
constexpr Date moveDay(Date d,int delta) {
    if(!valid(d))return d;
    while(delta>0) {
        const auto candidate=next(d);
        if(!valid(candidate))break;
        d=candidate;--delta;
    }
    while(delta<0) {
        auto candidate=d;
        if(--candidate.day==0) {
            if(--candidate.month==0){candidate.month=12;--candidate.year;}
            candidate.day=monthDays(candidate.year,candidate.month);
        }
        if(!valid(candidate))break;
        d=candidate;++delta;
    }
    return d;
}
constexpr Date moveMonth(Date d,int delta) {
    int month=d.year*12+d.month-1+delta;
    if(month<FirstYear*12)month=FirstYear*12;
    if(month>LastYear*12+11)month=LastYear*12+11;
    d.year=month/12;d.month=month%12+1;
    if(d.day>monthDays(d.year,d.month))d.day=monthDays(d.year,d.month);
    return d;
}
constexpr Lunar lunar(Date d) {
    if(!valid(d))return {};
    int year=d.year;
    if(serial(d)<serial({year,1,1})+data::years[year-1900].newYearOffset)--year;
    const auto info=data::years[year-1900];
    int offset=serial(d)-serial({year,1,1})-info.newYearOffset,slot=0;
    for(int m=1;m<=12;++m) {
        for(int pass=0;pass<(m==info.leap?2:1);++pass) {
            const int days=29+((info.longMonths>>slot++)&1);
            if(offset<days)return {year,m,offset+1,pass!=0};
            offset-=days;
        }
    }
    return {};
}
inline constexpr const char *Terms[]={"小寒","大寒","立春","雨水","惊蛰","春分","清明","谷雨","立夏","小满","芒种","夏至",
    "小暑","大暑","立秋","处暑","白露","秋分","寒露","霜降","立冬","小雪","大雪","冬至"};
inline constexpr const char *Months[]={"正月","二月","三月","四月","五月","六月","七月","八月","九月","十月","冬月","腊月"};
inline constexpr const char *Days[]={"初一","初二","初三","初四","初五","初六","初七","初八","初九","初十","十一","十二","十三","十四","十五",
    "十六","十七","十八","十九","二十","廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九","三十"};
inline constexpr const char *Stems[]={"甲","乙","丙","丁","戊","己","庚","辛","壬","癸"};
inline constexpr const char *Branches[]={"子","丑","寅","卯","辰","巳","午","未","申","酉","戌","亥"};
inline constexpr const char *Animals[]={"鼠","牛","虎","兔","龙","蛇","马","羊","猴","鸡","狗","猪"};
constexpr const char *term(Date d) {
    if(!valid(d))return "";
    for(int i=(d.month-1)*2;i<d.month*2;++i)
        if(data::terms[d.year-FirstYear][i]==d.day)return Terms[i];
    return "";
}
constexpr const char *festival(Date d) {
    const auto l=lunar(d);if(!l)return "";
    if(!l.leap) {
        const int key=l.month*100+l.day;
        switch(key) {case 101:return "春节";case 115:return "元宵节";case 505:return "端午节";
        case 707:return "七夕";case 815:return "中秋节";case 909:return "重阳节";case 1208:return "腊八节";}
        const auto tomorrow=lunar(next(d));
        if(l.month==12&&tomorrow&&tomorrow.month==1&&tomorrow.day==1)return "除夕";
    }
    switch(d.month*100+d.day) {case 101:return "元旦";case 308:return "妇女节";case 312:return "植树节";
    case 501:return "劳动节";case 504:return "青年节";case 601:return "儿童节";case 801:return "建军节";
    case 910:return "教师节";case 1001:return "国庆节";}
    return "";
}
} // namespace calendar
