# -*- coding: utf-8 -*-
"""Emit an LVGL v9 `lv_font_t` C source from rendered glyphs.

Layout produced (matches lv_font_conv / lv_font_montserrat_*.c):

    glyph_bitmap[]      packed bpp bitmaps, glyph 0 is the "not found" glyph
    glyph_dsc[]         lv_font_fmt_txt_glyph_dsc_t, index 0 is a dummy
    cmaps[]             one lv_font_fmt_txt_cmap_t per contiguous range,
                        SPARSE_TINY when the range has holes, FORMAT0_TINY
                        when it is fully contiguous
    font_dsc            lv_font_fmt_txt_dsc_t (no kerning, no compression)
    <name>              const lv_font_t

The glyph_dsc index space is 1-based: cmap entries map a code point to a
"glyph id" which is the index into glyph_dsc. Id 0 means "not found".
"""


def _wrap_hex(data, per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append("    " + ", ".join("0x%02x" % b for b in chunk) + ",")
    return "\n".join(lines) if lines else "    /* empty */"


def _split_ranges(glyphs):
    """Split the (sorted) glyphs into contiguous-ish cmap ranges.

    We start a new range when the gap to the previous code point is large,
    to keep SPARSE glyph-id lists small while avoiding thousands of ranges.
    """
    ranges = []
    cur = []
    GAP = 64
    for g in glyphs:
        if cur and g.cp - cur[-1].cp > GAP:
            ranges.append(cur)
            cur = []
        cur.append(g)
    if cur:
        ranges.append(cur)
    return ranges


def emit_c(out_path, name, px_size, bpp, glyphs, ascender, descender, line_height, *, deduplicate_bitmaps=False):
    # glyph id 0 is reserved (not-found). Assign ids 1..N in charset order.
    for i, g in enumerate(glyphs):
        g._id = i + 1  # type: ignore[attr-defined]

    # ---- glyph_bitmap ------------------------------------------------------
    bitmap = bytearray()
    bmp_index = [0]  # id 0 dummy points at offset 0
    shared = {}
    for g in glyphs:
        # Identical byte streams can share a flash address. Glyph dimensions
        # and advances remain independent; no runtime decompression is needed.
        if deduplicate_bitmaps and g.data in shared:
            bmp_index.append(shared[g.data])
        else:
            bmp_index.append(len(bitmap))
            if deduplicate_bitmaps:
                shared[g.data] = len(bitmap)
            bitmap += g.data

    # ---- glyph_dsc ---------------------------------------------------------
    dsc_lines = []
    # index 0 : dummy / not-found
    dsc_lines.append(
        "    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, "
        ".ofs_x = 0, .ofs_y = 0}, /* id = 0 reserved */")
    for g in glyphs:
        dsc_lines.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, "
            ".ofs_x = %d, .ofs_y = %d}, /* U+%04X */"
            % (bmp_index[g._id], g.adv_w, g.box_w, g.box_h,
               g.ofs_x, g.ofs_y, g.cp))

    # ---- cmaps -------------------------------------------------------------
    ranges = _split_ranges(glyphs)
    cmap_blobs = []       # extra arrays (unicode list / glyph id list)
    cmap_entries = []     # lv_font_fmt_txt_cmap_t initializers
    for ri, rg in enumerate(ranges):
        start = rg[0].cp
        last = rg[-1].cp
        span = last - start + 1
        contiguous = (span == len(rg))
        if contiguous:
            # FORMAT0_TINY: glyph id = range_start_glyph_id + (cp - range_start)
            cmap_entries.append(
                "    {\n"
                "        .range_start = %d, .range_length = %d,\n"
                "        .glyph_id_start = %d,\n"
                "        .unicode_list = NULL, .glyph_id_ofs_list = NULL,\n"
                "        .list_length = 0,\n"
                "        .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,\n"
                "    }," % (start, span, rg[0]._id))
        else:
            # SPARSE_TINY: explicit unicode list (offsets from range_start),
            # glyph id = glyph_id_start + index_in_list.
            uni_name = "unicode_list_%d" % ri
            uni_vals = [g.cp - start for g in rg]
            cmap_blobs.append(
                "static const uint16_t %s[] = {\n%s\n};\n"
                % (uni_name, _wrap_u16(uni_vals)))
            cmap_entries.append(
                "    {\n"
                "        .range_start = %d, .range_length = %d,\n"
                "        .glyph_id_start = %d,\n"
                "        .unicode_list = %s, .glyph_id_ofs_list = NULL,\n"
                "        .list_length = %d,\n"
                "        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,\n"
                "    }," % (start, last - start + 1, rg[0]._id,
                           uni_name, len(rg)))

    base_line = -descender if descender < 0 else 0
    if line_height <= 0:
        line_height = ascender - descender

    guard = name.upper()

    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* Auto-generated by tools/ttf2lvgl.py. DO NOT EDIT. */\n")
        f.write("/* Full GB2312 CJK font baked into firmware (no SD card). */\n\n")
        f.write("#ifdef LV_LVGL_H_INCLUDE_SIMPLE\n")
        f.write("    #include \"lvgl.h\"\n")
        f.write("#else\n")
        f.write("    #include \"lvgl.h\"\n")
        f.write("#endif\n\n")
        f.write("#ifndef %s\n#define %s 1\n#endif\n\n" % (guard, guard))
        f.write("#if %s\n\n" % guard)

        f.write("/* Glyph bitmaps (%d bytes, bpp=%d). */\n" % (len(bitmap), bpp))
        f.write("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {\n")
        f.write(_wrap_hex(bitmap))
        f.write("\n};\n\n")

        f.write("/* Glyph descriptors. */\n")
        f.write("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {\n")
        f.write("\n".join(dsc_lines))
        f.write("\n};\n\n")

        f.write("/* Character map lists. */\n")
        for blob in cmap_blobs:
            f.write(blob)
            f.write("\n")

        f.write("static const lv_font_fmt_txt_cmap_t cmaps[] = {\n")
        f.write("\n".join(cmap_entries))
        f.write("\n};\n\n")

        f.write("static const lv_font_fmt_txt_dsc_t font_dsc = {\n")
        f.write("    .glyph_bitmap = glyph_bitmap,\n")
        f.write("    .glyph_dsc = glyph_dsc,\n")
        f.write("    .cmaps = cmaps,\n")
        f.write("    .kern_dsc = NULL,\n")
        f.write("    .kern_scale = 0,\n")
        f.write("    .cmap_num = %d,\n" % len(cmap_entries))
        f.write("    .bpp = %d,\n" % bpp)
        f.write("    .kern_classes = 0,\n")
        f.write("    .bitmap_format = 0,\n")
        f.write("    .stride = 0,\n")
        f.write("};\n\n")

        f.write("const lv_font_t %s = {\n" % name)
        f.write("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,\n")
        f.write("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,\n")
        f.write("    .line_height = %d,\n" % line_height)
        f.write("    .base_line = %d,\n" % base_line)
        f.write("    .subpx = LV_FONT_SUBPX_NONE,\n")
        f.write("    .underline_position = -1,\n")
        f.write("    .underline_thickness = 1,\n")
        f.write("    .dsc = &font_dsc,\n")
        f.write("    .fallback = NULL,\n")
        f.write("    .user_data = NULL,\n")
        f.write("};\n\n")

        f.write("#endif /* %s */\n" % guard)

    import sys
    sys.stdout.write("Wrote %s : %d glyphs, %d cmap ranges, bitmap %d bytes\n"
                     % (out_path, len(glyphs), len(cmap_entries), len(bitmap)))


def _wrap_u16(vals, per_line=12):
    lines = []
    for i in range(0, len(vals), per_line):
        chunk = vals[i:i + per_line]
        lines.append("    " + ", ".join("0x%04x" % v for v in chunk) + ",")
    return "\n".join(lines) if lines else "    0"
