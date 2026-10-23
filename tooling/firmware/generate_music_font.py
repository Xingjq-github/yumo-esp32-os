"""22px music lyrics, full GB2312, bounded 28px lines, no runtime SD font IO."""
from pathlib import Path
import argparse,os
from ttf2lvgl import build_charset,render_glyphs
from ttf2lvgl_emit import emit_c

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--font',required=True,help='Licensed TTF/TTC font file')
 parser.add_argument('--output',type=Path,default=Path(__file__).resolve().parents[2]/'firmware/main/ui/lv_font_music_22.c')
 args=parser.parse_args()
 glyphs,_,_,_=render_glyphs(args.font,0,22,2,build_charset())
 for glyph in glyphs:
  if not glyph.box_h:continue
  assert glyph.box_h<=28,f'U+{glyph.cp:04X}'
  glyph.ofs_y=max(-5,min(glyph.ofs_y,23-glyph.box_h))
  assert 0<=23-glyph.box_h-glyph.ofs_y<=28-glyph.box_h
 emit_c(str(args.output),'lv_font_music_22',22,2,glyphs,23,-5,28,deduplicate_bitmaps=True)
 print(f'{len(glyphs)} glyphs; 28px line height; all glyphs fit without clipping')

if __name__=='__main__':main()
