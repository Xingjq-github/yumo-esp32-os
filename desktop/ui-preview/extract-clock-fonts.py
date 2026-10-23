"""Read existing uncompressed LVGL glyphs for the offline clock mockups.

No compiler, font conversion tool, firmware build, or font source modification.
The preview uses original glyph bitmaps and advances, with kerning disabled.
"""
from pathlib import Path
import base64
import hashlib
import json
import math
import re

ROOT = Path(__file__).resolve().parent.parent
OUT = Path(__file__).resolve().parent / 'clock-fonts.js'


def array(source, name):
    body = re.search(r'\b' + name + r'\[\]\s*=\s*\{(.*?)\};', source, re.S).group(1)
    return re.sub(r'/\*.*?\*/', '', body, flags=re.S)


def extract(path, chars, direct_comments=False):
    source = path.read_text(encoding='utf-8')
    assert re.search(r'\.bitmap_format\s*=\s*0', source)
    bpp = int(re.search(r'\.bpp\s*=\s*(\d+)', source).group(1))
    assert bpp in (2, 4)
    data = bytes(int(n, 16) for n in re.findall(r'0x([0-9a-fA-F]+)', array(source, 'glyph_bitmap')))
    desc = []
    raw = re.search(r'\bglyph_dsc\[\]\s*=\s*\{(.*?)\};', source, re.S).group(1)
    for fields, comment in re.findall(r'\{([^{}]+)\},?([^\n]*)', raw):
        d = {k: int(v) for k, v in re.findall(r'\.(\w+)\s*=\s*(-?\d+)', fields)}
        code = re.search(r'U\+([0-9A-Fa-f]+)', comment)
        desc.append((d, int(code.group(1), 16) if code else None))
    code_to_desc = {code:d for d,code in desc if code is not None} if direct_comments else {cp:desc[cp-31][0] for cp in range(32, 127)}
    glyphs = {}
    for char in chars:
        assert ord(char) in code_to_desc, (path.name, char)
        d = code_to_desc[ord(char)]
        length = math.ceil(d['box_w'] * d['box_h'] * bpp / 8)
        start = d['bitmap_index']
        assert start + length <= len(data)
        glyphs[char] = [d['adv_w'],d['box_w'],d['box_h'],d['ofs_x'],d['ofs_y'],base64.b64encode(data[start:start+length]).decode('ascii')]
    return {'bpp':bpp,'height':int(re.search(r'\.line_height\s*=\s*(\d+)',source).group(1)),
            'baseline':int(re.search(r'\.base_line\s*=\s*(\d+)',source).group(1)),
            'glyphs':glyphs,'source':path.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    fonts = {}
    # All these sizes are already enabled by the current sdkconfig.
    config = (ROOT / 'sdkconfig').read_text(encoding='utf-8')
    chars = ''.join(chr(i) for i in range(32,127))
    for size in (10,12,14,16,18,24,32,40,48):
        assert f'CONFIG_LV_FONT_MONTSERRAT_{size}=y' in config
        path = ROOT / f'managed_components/lvgl__lvgl/src/font/lv_font_montserrat_{size}.c'
        fonts[str(size)] = extract(path, chars)
    fonts['84'] = extract(ROOT / 'main/ui/lv_font_clock_84.c', '0123456789:', True)
    fonts['cjk16'] = extract(ROOT / 'main/ui/lv_font_cjk_16.c', '雨墨年月日星期一二三四五六时分秒', True)
    OUT.write_text('/* Extracted from existing LVGL bitmap fonts; do not hand-edit. */\nwindow.ClockFonts = '+json.dumps(fonts,ensure_ascii=False,separators=(',',':'))+';\n',encoding='utf-8')
    print(json.dumps({'fonts':len(fonts),'glyphs':sum(len(f['glyphs']) for f in fonts.values()),'bytes':OUT.stat().st_size,'output':str(OUT)},ensure_ascii=False))


if __name__ == '__main__':
    main()
