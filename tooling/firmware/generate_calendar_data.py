"""Generate tiny offline Chinese-calendar tables from lunar-python 1.4.8 (MIT).

Pass an extracted upstream source directory; no package install or firmware runtime
dependency. Civil dates use UTC+8 lunar/solar-term days, not approximate term formulae.
"""
from pathlib import Path
from datetime import date
import argparse, sys

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
TERMS=('小寒','大寒','立春','雨水','惊蛰','春分','清明','谷雨','立夏','小满','芒种','夏至',
       '小暑','大暑','立秋','处暑','白露','秋分','寒露','霜降','立冬','小雪','大雪','冬至')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    args=parser.parse_args();sys.path.insert(0,str(args.source.resolve()))
    from lunar_python import LunarYear, Solar
    out=['// Generated from 6tail/lunar-python v1.4.8, MIT; see third_party/lunar-LICENSE.',
         '// Lunar year 1900..2099 plus solar terms 1901..2099. UTC+8 calendar dates.',
         '#pragma once','#include <cstdint>','namespace calendar::data {',
         'struct Year { uint16_t longMonths; uint8_t leap, newYearOffset; };',
         'inline constexpr Year years[]={']
    for year in range(1900,2100):
        months=[m for m in LunarYear.fromYear(year).getMonths() if m.getYear()==year]
        assert len(months) in (12,13) and months[0].getMonth()==1
        start=Solar.fromJulianDay(months[0].getFirstJulianDay())
        offset=(date(start.getYear(),start.getMonth(),start.getDay())-date(year,1,1)).days
        mask=sum((m.getDayCount()-29)<<i for i,m in enumerate(months))
        leap=next((-m.getMonth() for m in months if m.getMonth()<0),0)
        out.append(f'    {{0x{mask:04x},{leap},{offset}}}, // {year}')
    out+=['};','inline constexpr uint8_t terms[][24]={']
    for year in range(1901,2100):
        table=Solar.fromYmd(year,6,1).getLunar().getJieQiTable()
        values=[table['DONG_ZHI' if t=='冬至' else t] for t in TERMS]
        assert all(s.getYear()==year and s.getMonth()==i//2+1 for i,s in enumerate(values))
        out.append('    {'+','.join(str(s.getDay()) for s in values)+f'}}, // {year}')
    out+=['};','} // namespace calendar::data','']
    (ROOT/'main/core/calendar_data.hpp').write_text('\n'.join(out),encoding='utf8')
    license_dir=ROOT/'main/third_party';license_dir.mkdir(exist_ok=True)
    (license_dir/'lunar-LICENSE').write_bytes((args.source/'LICENSE').read_bytes())
    print('Generated 200 lunar years and 199 years of 24 solar terms.')

if __name__=='__main__':main()
