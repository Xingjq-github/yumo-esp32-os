"""Append missing App-name glyphs using Windows GDI; preserve every existing glyph.

No pip packages, font downloads, compiler or full CJK font regeneration needed.
Run with -B. Uses the installed Microsoft YaHei font, 14px, 2bpp.
"""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import re
from ttf2lvgl import GlyphBitmap
from ttf2lvgl_emit import emit_c
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
FONT=ROOT/'main/ui/lv_font_menu_14.c'

def read_font(text):
    bitmap=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',re.search(r'uint8_t glyph_bitmap\[\] = \{(.*?)\};',text,re.S)[1]))
    rows=[]
    for row in re.findall(r'\{([^{}]*\.bitmap_index[^{}]*)\}',text):
        rows.append({key:int(value) for key,value in re.findall(r'\.(\w+) = (-?\d+)',row)})
    offsets={name:[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',body)] for name,body in re.findall(r'uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};',text,re.S)}
    glyphs={}
    for cmap in re.findall(r'\{([^{}]*\.range_start[^{}]*)\}',text):
        start=int(re.search(r'\.range_start = (\d+)',cmap)[1]); first=int(re.search(r'\.glyph_id_start = (\d+)',cmap)[1])
        name=re.search(r'\.unicode_list = (\w+)',cmap)[1]
        chars=offsets[name] if name!='NULL' else range(int(re.search(r'\.range_length = (\d+)',cmap)[1]))
        for index,offset in enumerate(chars):
            d=rows[first+index]; size=(d['box_w']*d['box_h']*2+7)//8
            glyphs[start+offset]=GlyphBitmap(start+offset,d['adv_w'],d['box_w'],d['box_h'],d['ofs_x'],d['ofs_y'],bitmap[d['bitmap_index']:d['bitmap_index']+size])
    return glyphs

def signature(g): return (g.adv_w,g.box_w,g.box_h,g.ofs_x,g.ofs_y,bytes(g.data))

def new_glyphs(codepoints,pixels=14,face='Microsoft YaHei'):
    class FIXED(C.Structure): _fields_=[('fract',W.WORD),('value',C.c_short)]
    class MAT2(C.Structure): _fields_=[('a',FIXED),('b',FIXED),('c',FIXED),('d',FIXED)]
    class POINT(C.Structure): _fields_=[('x',W.LONG),('y',W.LONG)]
    class METRICS(C.Structure): _fields_=[('width',W.UINT),('height',W.UINT),('origin',POINT),('advance_x',C.c_short),('advance_y',C.c_short)]
    gdi=C.WinDLL('gdi32',use_last_error=True)
    gdi.CreateCompatibleDC.argtypes=[W.HDC]; gdi.CreateCompatibleDC.restype=W.HDC
    gdi.CreateFontW.argtypes=[C.c_int]*5+[W.DWORD]*8+[W.LPCWSTR]; gdi.CreateFontW.restype=W.HANDLE
    gdi.SelectObject.argtypes=[W.HDC,W.HANDLE]; gdi.SelectObject.restype=W.HANDLE
    gdi.GetGlyphOutlineW.argtypes=[W.HDC,W.UINT,W.UINT,C.POINTER(METRICS),W.DWORD,C.c_void_p,C.POINTER(MAT2)]; gdi.GetGlyphOutlineW.restype=W.DWORD
    gdi.DeleteObject.argtypes=[W.HANDLE]; gdi.DeleteDC.argtypes=[W.HDC]
    dc=gdi.CreateCompatibleDC(None)
    font=gdi.CreateFontW(-pixels,0,0,0,400,0,0,0,1,0,0,4,0,face)
    if not dc or not font: raise RuntimeError('GDI font creation failed')
    old=gdi.SelectObject(dc,font)
    matrix=MAT2(FIXED(0,1),FIXED(0,0),FIXED(0,0),FIXED(0,1))
    try:
        for cp in codepoints:
            m=METRICS(); size=gdi.GetGlyphOutlineW(dc,cp,6,C.byref(m),0,None,C.byref(matrix))
            if size==0xffffffff: raise RuntimeError(f'Cannot render U+{cp:04X}')
            if size==0:
                if cp!=32 or m.advance_x<=0: raise RuntimeError(f'Cannot render U+{cp:04X}')
                yield GlyphBitmap(cp,int(m.advance_x)*16,0,0,0,0,b'')
                continue
            buffer=(C.c_ubyte*size)()
            if gdi.GetGlyphOutlineW(dc,cp,6,C.byref(m),size,buffer,C.byref(matrix))==0xffffffff: raise RuntimeError('Glyph rasterization failed')
            stride=(m.width+3)&~3; packed=bytearray((m.width*m.height*2+7)//8)
            for y in range(m.height):
                for x in range(m.width):
                    q=min(3,(int(buffer[y*stride+x])*3+32)//64)
                    pixel=y*m.width+x; packed[pixel//4]|=q<<(6-2*(pixel%4))
            glyph=GlyphBitmap(cp,int(m.advance_x)*16,m.width,m.height,m.origin.x,m.origin.y-int(m.height),bytes(packed))
            if glyph.adv_w>pixels*16 or glyph.ofs_y < -max(4,pixels//4) or glyph.box_h+glyph.ofs_y>pixels: raise RuntimeError('Glyph exceeds font metrics')
            yield glyph
    finally:
        gdi.SelectObject(dc,old); gdi.DeleteObject(font); gdi.DeleteDC(dc)

def main():
    text=FONT.read_text(encoding='utf-8'); glyphs=read_font(text)
    old={cp:signature(g) for cp,g in glyphs.items()}
    names=re.findall(r'registerApp\(\s*"([^"]+)"',(ROOT/'main/main.cpp').read_text(encoding='utf-8'))
    # The calendar uses the same compact 14px CJK face for its two-line day cells.
    names+=re.findall(r'"([^"\n]+)"',(ROOT/'main/core/calendar_model.hpp').read_text(encoding='utf-8'))
    # The almanac's three-part element line uses this compact font as well.
    almanac=(ROOT/'main/core/almanac_model.hpp').read_text(encoding='utf-8')
    for block in re.findall(r'(?:NaYin|StemElements|BranchElements)\[\]=\{(.*?)\};',almanac,re.S):
        names+=re.findall(r'"([^"\n]+)"',block)
    names.append('天干: 地支: 纳音:')
    names.append('0123456789') # Dynamic practice positions use the compact font.
    # Both in-scene game menus share the compact 14px subset.
    for app in ('gravity_maze','dino','morse','design_patterns','speed_test','timer','garden'):
        names+=re.findall(r'"([^"\n]+)"',(ROOT/f'main/apps/{app}.cpp').read_text(encoding='utf-8'))
    # Learning copy includes all roles, conditions and trade-offs, not only titles.
    names.append((ROOT/'../resources/knowledge/pattern_game.py').read_text(encoding='utf-8'))
    names.append((ROOT/'main/core/pattern_play.hpp').read_text(encoding='utf-8'))
    for suffix in ('hpp','cpp'):
        names+=re.findall(r'"([^"\n]+)"',(ROOT/f'main/core/garden_model.{suffix}').read_text(encoding='utf-8'))
    missing=sorted(set(map(ord,''.join(names)))-set(glyphs))
    if not missing: print('All registered App names are already covered.'); return
    for glyph in new_glyphs(missing): glyphs[glyph.cp]=glyph
    emit_c(str(FONT),'lv_font_menu_14',14,2,[glyphs[cp] for cp in sorted(glyphs)],14,-4,18)
    updated=FONT.read_text(encoding='utf-8').replace('/* Auto-generated by tools/ttf2lvgl.py. DO NOT EDIT. */','/* Generated by ttf2lvgl; missing menu glyphs appended by tools/update_menu_font.py. */',1)
    FONT.write_text(updated,encoding='utf-8',newline='\n')
    checked=read_font(updated)
    assert all(signature(checked[cp])==value for cp,value in old.items()),'Existing menu glyph changed'
    print(f'Added {len(missing)} glyphs; {len(old)} existing glyphs preserved byte-for-byte.')
if __name__=='__main__': main()
