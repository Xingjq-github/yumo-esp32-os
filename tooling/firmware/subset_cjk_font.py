#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CJK font subsetter for the ESP32 firmware.

Problem
-------
A full Chinese TTF (e.g. Source Han Sans / Microsoft YaHei) can be ~10 MB.
The firmware loads the whole file into PSRAM via lv_tiny_ttf_create_data(),
so a 9.75 MB font triggers OOM and the UI falls back to the built-in CJK
font (which lacks many glyphs -> boxes / garbled text).

Solution
--------
Subset the font down to just the characters we actually need:
  * ASCII (0x20..0x7E)
  * Common CJK punctuation / fullwidth forms
  * All 6763 GB2312 hanzi (covers everyday Chinese)
This typically shrinks a 10 MB font to ~2-3 MB, which fits in PSRAM.

Usage
-----
  pip install fonttools
  python tools/subset_cjk_font.py <input_font> <output_font>

Example
-------
  python tools/subset_cjk_font.py path/to/licensed-font.ttf cjk.ttf
  python tools/subset_cjk_font.py source-han-sans.otf cjk.ttf

Then copy the produced cjk.ttf to the SD card at /sdcard/fonts/cjk.ttf.

Notes
-----
  * .ttc (font collection) input is supported; the first face (index 0)
    is used by default. Override with --font-number.
  * Output is written as a plain TTF/OTF (no flavor / not woff).
"""

import argparse
import sys


def build_charset() -> set:
    """Return the set of unicode code points to keep in the subset."""
    chars = set()

    # 1) Basic ASCII (printable).
    for cp in range(0x20, 0x7F):
        chars.add(cp)

    # 2) Common CJK punctuation and fullwidth forms.
    #    General punctuation, CJK symbols/punctuation, fullwidth forms.
    ranges = [
        (0x00A0, 0x00FF),   # Latin-1 supplement (degree sign, etc.)
        (0x2000, 0x206F),   # General punctuation (…, —, “ ” ‘ ’, etc.)
        (0x2100, 0x214F),   # Letterlike symbols (№, etc.)
        (0x2460, 0x24FF),   # Enclosed alphanumerics (①②③)
        (0x2600, 0x26FF),   # Misc symbols (a few common ones)
        (0x3000, 0x303F),   # CJK symbols and punctuation (。、《》「」etc.)
        (0x3040, 0x30FF),   # Hiragana + Katakana (some texts mix these)
        (0xFF00, 0xFFEF),   # Fullwidth forms (！？，．：；（）etc.)
    ]
    for lo, hi in ranges:
        for cp in range(lo, hi + 1):
            chars.add(cp)

    # 3) All GB2312 hanzi (6763 chars) via codec round-trip.
    #    GB2312 hanzi live in two byte ranges; decode every valid pair.
    for hi in range(0xB0, 0xF8):          # first byte 0xA1..0xF7 (hanzi: 0xB0..0xF7)
        for lo in range(0xA1, 0xFF):      # second byte 0xA1..0xFE
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except (UnicodeDecodeError, ValueError):
                continue
            chars.add(ord(ch))

    # 4) Also fold in GBK-only common chars would enlarge output; skip to
    #    keep size small. GB2312 already covers everyday Chinese text.

    return chars


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Subset a CJK font down to common Chinese characters.")
    parser.add_argument("input", help="Input font path (.ttf/.otf/.ttc)")
    parser.add_argument("output", help="Output font path (.ttf/.otf)")
    parser.add_argument("--font-number", type=int, default=0,
                        help="Face index for .ttc collections (default 0)")
    parser.add_argument("--extra-chars", default="",
                        help="Extra literal characters to force-include")
    parser.add_argument("--extra-file", default="",
                        help="UTF-8 text file; all its chars are included")
    args = parser.parse_args()

    try:
        from fontTools import subset
        from fontTools.ttLib import TTFont
    except ImportError:
        sys.stderr.write(
            "ERROR: fonttools not installed. Run:  pip install fonttools\n")
        return 2

    charset = build_charset()

    # Optional extra characters from CLI.
    for ch in args.extra_chars:
        charset.add(ord(ch))

    # Optional extra characters from a text file (e.g. your actual docs).
    if args.extra_file:
        try:
            with open(args.extra_file, "r", encoding="utf-8") as fp:
                for ch in fp.read():
                    if ch not in ("\r", "\n", "\t"):
                        charset.add(ord(ch))
        except OSError as exc:
            sys.stderr.write("WARN: cannot read extra-file: %s\n" % exc)

    sys.stdout.write("Keeping %d code points.\n" % len(charset))

    # Load the font (handle .ttc collections).
    try:
        font = TTFont(args.input, fontNumber=args.font_number)
    except Exception as exc:  # noqa: BLE001
        sys.stderr.write("ERROR: cannot open input font: %s\n" % exc)
        return 3

    options = subset.Options()
    # Keep it lean; drop hinting and layout tables we don't need on-device.
    options.desubroutinize = True
    options.drop_tables += ["GSUB", "GPOS", "GDEF", "kern", "morx",
                            "hdmx", "VDMX", "LTSH"]
    options.name_IDs = ["*"]
    options.name_legacy = True
    options.name_languages = ["*"]
    options.notdef_outline = True
    options.recalc_bounds = True
    options.recalc_timestamp = False
    options.glyph_names = False        # smaller output
    options.legacy_kern = False
    options.layout_features = []       # we dropped GSUB/GPOS anyway
    options.ignore_missing_glyphs = True
    options.ignore_missing_unicodes = True

    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=sorted(charset))
    try:
        subsetter.subset(font)
    except Exception as exc:  # noqa: BLE001
        sys.stderr.write("ERROR: subsetting failed: %s\n" % exc)
        return 4

    try:
        font.flavor = None  # plain TTF/OTF, not woff
        font.save(args.output)
    except Exception as exc:  # noqa: BLE001
        sys.stderr.write("ERROR: cannot save output font: %s\n" % exc)
        return 5

    # Report resulting size.
    import os
    size = os.path.getsize(args.output)
    sys.stdout.write("Wrote %s (%.2f MB).\n" % (args.output, size / 1048576.0))
    if size > 4 * 1048576:
        sys.stdout.write(
            "WARNING: output is still >4 MB; consider trimming the charset "
            "(GB2312-only already; the base font may just be heavy).\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())