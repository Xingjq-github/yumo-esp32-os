"""Small 24px Chinese clock font. Uses the same local GDI rasterizer as menus."""
from pathlib import Path
from update_menu_font import new_glyphs
from ttf2lvgl import GlyphBitmap
from ttf2lvgl_emit import emit_c
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
CHARS='甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥时夜半鸡鸣平旦日出食隅中昳哺入黄昏人定正一二三四五六七八九十冬腊月初廿闰零黄历'
def main():
    glyphs=list(new_glyphs(sorted(set(map(ord,CHARS))),24))
    glyphs.append(GlyphBitmap(32,6*16,0,0,0,0,b''))
    emit_c(str(ROOT/'main/ui/lv_font_almanac_24.c'),'lv_font_almanac_24',24,2,sorted(glyphs,key=lambda g:g.cp),24,-6,30)
if __name__=='__main__':main()
