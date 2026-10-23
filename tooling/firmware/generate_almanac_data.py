"""Offline UTC+8 solar-term seconds from lunar-python 1.4.8 (MIT).

The firmware performs bounded lookups; Python and astronomy are build-time only.
Also emits an upstream oracle for all 199 years at every solar-term boundary.
"""
from pathlib import Path
from datetime import datetime,timedelta
import argparse,sys,json
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
ALL_TERMS=('小寒','大寒','立春','雨水','惊蛰','春分','清明','谷雨','立夏','小满','芒种','夏至',
           '小暑','大暑','立秋','处暑','白露','秋分','寒露','霜降','立冬','小雪','大雪','冬至')
def main():
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('--run-dir',type=Path,required=True);a=p.parse_args()
    sys.path.insert(0,str(a.source.resolve()))
    from lunar_python import Solar
    from lunar_python.util import LunarUtil
    out=['// Generated from 6tail/lunar-python 1.4.8 (MIT); third_party/lunar-LICENSE.',
         '// Seconds after January 1 00:00:00 China time, 1901..2099, XiaoHan..DongZhi.',
         '#pragma once','#include <cstdint>','namespace calendar::data {','inline constexpr uint32_t termSeconds[][24]={']
    oracle=['// Generated upstream oracle: lunar-python 1.4.8, MIT. Exact UTC+8 terms, sect 1.',
            '#include "core/almanac_model.hpp"','using namespace calendar;',
            'constexpr uint32_t mix(uint32_t h,unsigned v){return (h^v)*16777619u;}',
            'constexpr uint32_t sample(uint32_t h,Date d,int hour,int min,int sec){',
            ' const auto a=almanac(d,hour,min,sec);',
            ' const unsigned values[]={unsigned(a.lunarDate.year),unsigned(a.lunarDate.month),unsigned(a.lunarDate.day),unsigned(a.lunarDate.leap),',
            ' unsigned(a.year.cycle()),unsigned(a.month.cycle()),unsigned(a.day.cycle()),unsigned(a.time.cycle()),unsigned(a.timeGod)};',
            ' for(auto v:values){h=mix(h,v);}return h;}',
            'constexpr uint32_t boundaries(int year){uint32_t h=2166136261u;',
            ' for(int i=0;i<24;++i)for(int delta=-1;delta<=1;++delta){',
            ' int seconds=data::termSeconds[year-FirstYear][i]+delta;Date d{year,1,1};',
            ' int days=seconds/86400;while(days>=monthDays(year,d.month)){days-=monthDays(year,d.month);++d.month;}d.day+=days;',
            ' seconds%=86400;h=sample(h,d,seconds/3600,seconds/60%60,seconds%60);}',
            ' return h;}']
    count=0
    def sample(h,dt):
        nonlocal count
        l=Solar.fromYmdHms(dt.year,dt.month,dt.day,dt.hour,dt.minute,dt.second).getLunar()
        vals=[l.getYear(),abs(l.getMonth()),l.getDay(),int(l.getMonth()<0)]
        vals += [LunarUtil.JIA_ZI.index(x) for x in (l.getYearInGanZhiExact(),l.getMonthInGanZhiExact(),l.getDayInGanZhiExact(),l.getTimeInGanZhi())]
        vals += [LunarUtil.TIAN_SHEN.index(l.getTimeTianShen())-1]
        for v in vals:h=((h^v)*16777619)&0xffffffff
        count+=1;return h
    for year in range(1901,2100):
        table=Solar.fromYmd(year,6,1).getLunar().getJieQiTable();values=[];h=2166136261
        for name in ALL_TERMS:
            s=table['DONG_ZHI' if name=='冬至' else name]
            dt=datetime(s.getYear(),s.getMonth(),s.getDay(),s.getHour(),s.getMinute(),s.getSecond())
            assert dt.year==year
            values.append(int((dt-datetime(year,1,1)).total_seconds()))
            for delta in (-1,0,1):h=sample(h,dt+timedelta(seconds=delta))
        out.append('    {'+','.join(map(str,values))+f'}}, // {year}')
        oracle.append(f'static_assert(boundaries({year})=={h}u,"term boundary {year}");')
    out+=['};','} // namespace calendar::data','']
    # All days in the current year, including midnight, all double-hour entries
    # and the 23:00 day-pillar change. Group by month to bound constexpr cost.
    oracle+=['constexpr uint32_t days(int month){uint32_t h=2166136261u;',
             ' for(int d=1;d<=monthDays(2026,month);++d){for(int hour=0;hour<24;++hour){h=sample(h,{2026,month,d},hour,0,0);}}return h;}']
    for month in range(1,13):
        h=2166136261
        for day in range(1,32):
            try:dt=datetime(2026,month,day)
            except ValueError:break
            for hour in range(24):h=sample(h,dt.replace(hour=hour))
        oracle.append(f'static_assert(days({month})=={h}u,"all hours 2026/{month}");')
    (ROOT/'main/core/almanac_data.hpp').write_text('\n'.join(out),encoding='utf8')
    (ROOT/'../tooling/firmware/almanac_reference_test.cpp').write_text('\n'.join(oracle)+'\n',encoding='utf8')
    a.run_dir.mkdir(parents=True,exist_ok=True)
    result={'upstream':'6tail/lunar-python 1.4.8, MIT','term_years':199,'term_instants':199*24,'oracle_samples':count,'timezone':'UTC+8','bazi_day_boundary':'23:00 (sect 1)'}
    (a.run_dir/'reference-generation.json').write_text(json.dumps(result,indent=2),encoding='utf8');print(result)
if __name__=='__main__':main()
