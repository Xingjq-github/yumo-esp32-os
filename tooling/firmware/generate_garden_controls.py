"""Original inventory art; static ARGB8888 supports LVGL scaling with transparency."""
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
NAMES='Back Leaf Heart Star Fruit Bed Bubbles Pot Music Sprout Shield Book Compass Crystal Map House Album Forest Sea Cloud Lamp Desk Seed Flower Rain Wind Replay Check Save Alert Egg'.split()
PALETTE=[0,0x405848,0xFFF5D6,0xBBD69D,0x709B76,0xC99866,0xF1CF7A,0xD99E70,0xE899A2,0xBD6980,0xA7DADC,0x679FAD,0xBCB5DE,0x817BBA,0xF9E7AE,0x8C7864]

def artwork(name):
    p=[[0]*24 for _ in range(24)]
    def box(x,y,w,h,c):
        for yy in range(max(0,y),min(24,y+h)):
            for xx in range(max(0,x),min(24,x+w)):p[yy][xx]=c
    def line(x,y,ex,ey,c=1,w=1):
        dx=abs(ex-x);sx=1 if x<ex else -1;dy=-abs(ey-y);sy=1 if y<ey else -1;err=dx+dy
        while True:
            box(x,y,w,w,c)
            if x==ex and y==ey:break
            e=2*err
            if e>=dy:err+=dy;x+=sx
            if e<=dx:err+=dx;y+=sy
    def oval(cx,cy,rx,ry,c):
        for y in range(24):
            for x in range(24):
                if (x-cx)**2*ry*ry+(y-cy)**2*rx*rx<=rx*rx*ry*ry:p[y][x]=c
    def disk(x,y,r,c):oval(x,y,r,r,c)
    def star(x=12,y=12,c=6):
        for dx,dy,w,h in [(-2,-8,5,17),(-8,-2,17,5),(-5,-5,11,11)]:box(x+dx,y+dy,w,h,1)
        for dx,dy,w,h in [(-1,-6,3,13),(-6,-1,13,3),(-3,-3,7,7)]:box(x+dx,y+dy,w,h,c)
        box(x-1,y-4,2,3,2)
    def leaf(x=12,y=10):
        oval(x,y,8,5,1);oval(x,y-1,6,3,3);line(x-6,y+3,x+5,y-3,4)
    def book():
        box(2,4,20,16,1);box(3,5,8,13,2);box(13,5,8,13,14);box(11,6,2,14,5)
        for y in (8,11,14):box(5,y,4,1,5);box(15,y,4,1,5)
    if name=='Back':
        for i in range(7):box(3+i,11-i,3,3,1);box(3+i,11+i,3,3,1)
        box(6,10,15,5,1);box(8,11,12,2,2)
    elif name=='Leaf':leaf();line(8,14,6,20,1,2)
    elif name=='Heart':
        disk(7,8,5,1);disk(16,8,5,1)
        for y in range(9,21):box(3+(y-9)//2,y,18-(y-9),1,1)
        disk(7,8,3,8);disk(16,8,3,8)
        for y in range(9,19):box(5+(y-9)//2,y,14-(y-9),1,8)
        box(6,6,3,2,2)
    elif name in ('Star','Crystal'):
        if name=='Star':star()
        else:
            for y in range(3,22):
                half=1+(y-3)//2 if y<11 else (22-y)//2
                box(12-half,y,half*2+1,1,1);box(13-half,y,max(1,half*2-1),1,12 if y<12 else 13)
            line(12,5,12,18,2)
    elif name=='Fruit':
        line(12,3,11,8,1,2);oval(16,4,4,2,4);oval(11,14,8,8,1);oval(11,13,6,6,8);oval(8,11,2,3,2);box(12,18,3,1,9)
    elif name=='Bed':
        box(2,8,2,14,1);box(21,10,2,12,1);box(4,12,17,8,1);box(4,12,6,5,2);box(11,11,10,7,12);box(4,19,17,2,5)
    elif name in ('Bubbles','Rain','Sea'):
        if name=='Bubbles':
            for x,y,r in [(7,15,6),(16,8,5),(17,18,3)]:disk(x,y,r,1);disk(x,y,r-1,10);box(x-2,y-2,2,2,2)
        elif name=='Rain':
            oval(12,7,9,4,1);oval(12,6,8,3,10)
            for x in (5,12,19):line(x,13,x-2,19,11,2)
        else:
            for y in (7,13,19):
                for x in range(2,22):box(x,y+(x%8)//4,1,2,11);box(x,y-1+(x%8)//4,1,1,2)
    elif name=='Pot':
        box(3,10,18,10,1);box(5,12,14,6,7);box(1,12,3,3,1);box(21,12,3,3,1);box(2,8,20,2,1);box(10,6,4,2,1)
        for x in (7,15):line(x,2,x-1,5,2)
    elif name=='Music':
        box(9,3,2,15,1);box(10,3,11,5,1);box(19,5,2,12,1);box(11,4,8,2,12);oval(6,18,4,3,9);oval(16,17,4,3,9)
    elif name in ('Sprout','Seed','Flower'):
        line(12,7,12,21,1,2);oval(7,12,5,3,4);oval(17,9,5,3,3)
        if name=='Seed':box(4,19,16,3,5);box(6,20,12,1,7)
        if name=='Flower':
            for x,y in ((8,5),(16,5),(12,2),(12,8)):disk(x,y,3,8)
            disk(12,5,2,6)
    elif name=='Shield':
        box(4,3,17,10,1);box(6,5,13,9,6)
        for y in range(13,22):box(4+(y-13),y,17-2*(y-13),1,1)
        for y in range(13,20):box(7+(y-13),y,11-2*(y-13),1,7)
        box(11,7,3,10,2);box(8,10,9,3,2)
    elif name in ('Book','Album'):
        book()
        if name=='Album':disk(7,10,3,4);box(5,14,5,2,4);star(17,10,6)
    elif name=='Compass':
        disk(12,13,10,1);disk(12,13,8,14);disk(12,13,6,2)
        line(8,18,16,7,9,2);line(8,18,12,13,11,2);disk(12,13,1,1)
    elif name=='Map':
        box(2,4,20,17,1);box(3,5,18,15,14);box(9,5,1,15,5);box(16,5,1,15,5)
        line(5,16,13,9,7,2);line(13,9,19,12,7,2);disk(18,10,3,9);disk(18,10,1,2)
    elif name=='House':
        for y in range(2,12):box(12-y,y,2*y+1,1,1)
        for y in range(4,11):box(14-y,y,2*y-3,1,9)
        box(4,11,17,11,1);box(6,12,13,8,14);box(9,14,5,8,5);box(16,13,2,4,10)
    elif name=='Forest':
        for x,y in ((7,10),(17,13)):
            box(x-1,y,3,12,5)
            for yy in range(2,12):box(x-yy//2,y-8+yy,yy+1,1,1 if yy in (2,11) else 4 if x==7 else 3)
    elif name=='Cloud':
        oval(12,15,10,5,1);oval(12,14,8,4,2);disk(10,10,5,1);disk(10,10,4,2);disk(17,12,4,14)
    elif name=='Lamp':
        box(11,4,3,17,1);box(6,20,13,2,1);box(5,6,16,10,1);box(7,8,12,6,6);box(10,8,6,5,2);box(7,4,12,2,5)
    elif name=='Desk':
        box(2,11,20,4,1);box(3,12,18,2,7);box(4,15,3,8,5);box(18,15,3,8,5);box(6,5,12,6,1);box(7,6,10,4,14);box(11,6,2,4,5)
    elif name=='Wind':
        for y in (6,12,18):box(2,y,16,2,11);oval(18,y-1,3,3,11);oval(18,y-1,1,1,0)
    elif name=='Replay':
        disk(12,12,9,1);disk(12,12,7,6);disk(12,12,5,0);box(1,3,12,7,0);line(4,3,4,10,1,2);line(4,9,11,9,1,2)
    elif name=='Check':line(3,12,9,18,4,3);line(9,18,20,5,4,3)
    elif name=='Save':
        box(3,2,18,21,1);box(5,3,14,18,4);box(7,3,9,6,2);box(7,13,10,8,14);box(9,15,6,1,5);box(9,18,6,1,5)
    elif name=='Alert':
        for y in range(2,22):box(12-y//2,y,y+1,1,6)
        box(11,8,3,7,1);box(11,18,3,2,1)
    else:
        oval(12,14,8,10,1);oval(12,13,6,8,14);oval(9,10,2,3,2);box(8,17,2,2,4);box(15,13,2,2,8)
    return p

def indexed(p,palette):
    colors=[bytes([c&255,(c>>8)&255,c>>16,255 if i else 0]) for i,c in enumerate(palette)]
    return b''.join(colors[v] for row in p for v in row)

def main():
    images=[(24,artwork(name),PALETTE) for name in NAMES]
    source=(ROOT/'firmware/main/ui/garden_art.hpp').read_text(encoding='utf8')
    rows=re.findall(r'"([0-9a-f]{32})"',source)
    palettes=[[int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{6})',line)] for line in source.splitlines() if line.startswith('    {0x')]
    for i in range(14):images.append((32,[[int(v,16) for v in row] for row in rows[i*32:(i+1)*32]],palettes[i]))
    out=['// Generated by tooling/firmware/generate_garden_controls.py; original pixel artwork.','#pragma once','#include "lvgl.h"','#include <array>','namespace ui::garden_controls {',
         'enum class Art { '+','.join(NAMES)+',Count };']
    for i,(size,p,palette) in enumerate(images):
        data=indexed(p,palette);out.append(f'alignas(4) inline constexpr uint8_t Pixels{i}[]={{')
        out+=['    '+','.join(f'0x{v:02x}' for v in data[n:n+24])+',' for n in range(0,len(data),24)];out.append('};')
    out+=['inline constexpr lv_image_dsc_t descriptor(unsigned size,const uint8_t *data,unsigned bytes){',
          '    lv_image_dsc_t d{};d.header.magic=LV_IMAGE_HEADER_MAGIC;d.header.cf=LV_COLOR_FORMAT_ARGB8888;',
          '    d.header.w=size;d.header.h=size;d.header.stride=size*4;d.data_size=bytes;d.data=data;return d;','}',
          f'inline constexpr std::array<lv_image_dsc_t,{len(images)}> Images={{']
    out += [f'    descriptor({size},Pixels{i},sizeof(Pixels{i})),' for i,(size,_,_) in enumerate(images)]
    out+=['};','inline const lv_image_dsc_t *icon(Art art){return &Images[static_cast<unsigned>(art)];}',
          'inline const lv_image_dsc_t *pet(unsigned form){return &Images[static_cast<unsigned>(Art::Count)+(form<14?form:12)];}','}']
    (ROOT/'firmware/main/ui/garden_controls.hpp').write_text('\n'.join(out)+'\n',encoding='utf8')
    print(f'Generated {len(NAMES)} inventory icons and 14 pet portraits, {sum(len(indexed(p,c)) for _,p,c in images)} static image bytes.')

if __name__=='__main__':main()
