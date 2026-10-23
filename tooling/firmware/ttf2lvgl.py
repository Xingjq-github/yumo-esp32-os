#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate an LVGL v9 built-in font C source from a TTF/TTC.

Why
---
The firmware previously loaded a full Chinese TTF from the SD card at runtime
(lv_tiny_ttf). If the SD card / font file was missing the UI fell back to the
built-in Source Han Sans SC 16 CJK font, which only covers ~1000 glyphs, so
many characters rendered as boxes / garbled text.

This script bakes a full GB2312 (6763 hanzi) + ASCII + CJK punctuation font
straight into the firmware as a static `lv_font_t` C array. It never depends
on the SD card and never fails to load.

Output format is byte-compatible with lv_font_conv's `--format lvgl` output:
  * bpp = 2 (4-level anti-aliasing, small enough to stay < 1 MB bitmap)
  * cmap = SPARSE_TINY (GB2312 code points are not contiguous)
  * no kerning, no compression

Usage
-----
  pip install fonttools freetype-py
  python tools/ttf2lvgl.py path/to/licensed-font.ttf main/ui/lv_font_cjk_16.c \
      --size 16 --bpp 2 --name lv_font_cjk_16 --face 0

The generated file guards itself with `#if LV_FONT_CJK_16` (default 1).
"""

import argparse
import sys


def build_charset(extra: str = "") -> list:
    """Return sorted unique list of code points to bake into the font."""
    chars = set()

    # 1) Printable ASCII.
    for cp in range(0x20, 0x7F):
        chars.add(cp)

    # 2) Common punctuation / fullwidth / symbols used by CJK text and the UI.
    ranges = [
        (0x00A0, 0x00FF),   # Latin-1 supplement (deg sign, etc.)
        (0x2000, 0x206F),   # General punctuation ...  em dash  quotes
        (0x2100, 0x214F),   # Letterlike symbols
        (0x2460, 0x24FF),   # Enclosed alphanumerics
        (0x25A0, 0x25FF),   # Geometric shapes (bullets)
        (0x2600, 0x26FF),   # Misc symbols
        (0x3000, 0x303F),   # CJK symbols and punctuation
        (0xFF00, 0xFFEF),   # Fullwidth forms
    ]
    for lo, hi in ranges:
        for cp in range(lo, hi + 1):
            chars.add(cp)

    # 3) All GB2312 hanzi (6763) via codec round-trip.
    for hi in range(0xB0, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except (UnicodeDecodeError, ValueError):
                continue
            chars.add(ord(ch))

    # 4) Extra literal characters forced in.
    for ch in extra:
        if ch not in ("\r", "\n", "\t"):
            chars.add(ord(ch))

    return sorted(chars)


def quantize(value: int, bpp: int) -> int:
    """Map an 8-bit coverage value to a `bpp`-bit value (LVGL style)."""
    maxv = (1 << bpp) - 1
    # Round to nearest of maxv+1 levels.
    return (value * maxv + 127) // 255


class GlyphBitmap:
    __slots__ = ("cp", "adv_w", "box_w", "box_h", "ofs_x", "ofs_y", "data", "_id")

    def __init__(self, cp, adv_w, box_w, box_h, ofs_x, ofs_y, data):
        self.cp = cp
        self.adv_w = adv_w      # 8.4 fixed (real * 16)
        self.box_w = box_w
        self.box_h = box_h
        self.ofs_x = ofs_x
        self.ofs_y = ofs_y
        self.data = data        # packed bytes, one row after another


def render_glyphs(font_path, face_index, px_size, bpp, charset):
    """Render every code point to a packed `bpp`-per-pixel bitmap."""
    import freetype

    face = freetype.Face(font_path, index=face_index)
    face.set_pixel_sizes(0, px_size)

    ascender = face.size.ascender >> 6      # 26.6 -> px
    descender = face.size.descender >> 6    # negative
    line_height = (face.size.height >> 6)

    glyphs = []
    for cp in charset:
        face.load_char(cp, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        g = face.glyph
        bmp = g.bitmap
        box_w = bmp.width
        box_h = bmp.rows
        ofs_x = g.bitmap_left
        # LVGL ofs_y = distance from baseline to the BOTTOM ofthe bbox.
        ofs_y = g.bitmap_top - box_h
        adv_w = int(round((g.advance.x / 64.0) * 16))   # 8.4 fixed

        # Pack pixels to `bpp` bits per pixel, MSB first, as ONE continuous
        # bit stream for the whole glyph (stride = 0, no per-row byte align).
        # This matches lv_font_conv's default LVGL output that the runtime
        # (lv_font_fmt_txt_dsc_t.stride == 0) expects.
        src = bmp.buffer
        pitch = bmp.pitch
        packed = bytearray()
        acc = 0
        nbits = 0
        for y in range(box_h):
            for x in range(box_w):
                v = quantize(src[y * pitch + x], bpp)
                acc = (acc << bpp) | v
                nbits += bpp
                if nbits == 8:
                    packed.append(acc & 0xFF)
                    acc = 0
                    nbits = 0
        if nbits:
            acc <<= (8 - nbits)
            packed.append(acc & 0xFF)
        glyphs.append(GlyphBitmap(cp, adv_w, box_w, box_h, ofs_x, ofs_y, bytes(packed)))

    return glyphs, ascender, descender, line_height


def main() -> int:
    ap = argparse.ArgumentParser(description="TTF/TTC -> LVGL v9 built-in font C source")
    ap.add_argument("input", help="Input font (.ttf/.otf/.ttc)")
    ap.add_argument("output", help="Output .c path")
    ap.add_argument("--size", type=int, default=16, help="Pixel size (default 16)")
    ap.add_argument("--bpp", type=int, default=2, choices=[1, 2, 4], help="Bits per pixel")
    ap.add_argument("--face", type=int, default=0, help="Face index for .ttc")
    ap.add_argument("--name", default="lv_font_cjk_16", help="C symbol / guard name")
    ap.add_argument("--chars-only", default=None, help="Bake only these literal characters (small UI subset)")
    ap.add_argument("--extra", default="", help="Extra literal chars to include")
    args = ap.parse_args()

    try:
        import freetype  # noqa: F401
    except ImportError:
        sys.stderr.write("ERROR: freetype-py not installed. pip install freetype-py\n")
        return 2

    charset = sorted(set(map(ord,args.chars_only))) if args.chars_only is not None else build_charset(args.extra)
    sys.stdout.write("Charset: %d code points, size=%dpx bpp=%d\n"
                     % (len(charset), args.size, args.bpp))

    glyphs, ascender, descender, line_height = render_glyphs(
        args.input, args.face, args.size, args.bpp, charset)

    from ttf2lvgl_emit import emit_c
    emit_c(args.output, args.name, args.size, args.bpp,
           glyphs, ascender, descender, line_height)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())