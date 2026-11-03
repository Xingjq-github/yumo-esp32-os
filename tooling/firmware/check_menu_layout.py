"""Real font metrics + source contracts for the 240px two-column menu."""
from pathlib import Path
import re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf-8')
def font_metrics(path,text=None):
    if text is None:text=read(path)
    rows=[{k:int(v) for k,v in re.findall(r'\.(\w+)\s*=\s*(-?\d+)',row)} for row in re.findall(r'\{([^{}]*\.bitmap_index[^{}]*)\}',text)]
    offsets={name:[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',body)] for name,body in re.findall(r'uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};',text,re.S)}
    glyphs={}
    for cmap in re.findall(r'\{([^{}]*\.range_start[^{}]*)\}',text):
        start=int(re.search(r'\.range_start\s*=\s*(\d+)',cmap)[1]);first=int(re.search(r'\.glyph_id_start\s*=\s*(\d+)',cmap)[1])
        name=re.search(r'\.unicode_list\s*=\s*(\w+)',cmap)[1]
        values=offsets[name] if name!='NULL' else range(int(re.search(r'\.range_length\s*=\s*(\d+)',cmap)[1]))
        for i,v in enumerate(values):glyphs[start+v]=rows[first+i]
    return glyphs

def icon_definitions():
    definitions=read('../dependencies/managed_components/lvgl__lvgl/include/lvgl/font/lv_symbol_def.h')
    symbols=dict(re.findall(r'#define\s+(LV_SYMBOL_\w+)\s+"((?:\\x[0-9A-Fa-f]{2})+)"',definitions))
    symbols.update({'ui::icons::'+name:value for name,value in re.findall(
        r'inline constexpr char (\w+)\[\]\s*=\s*"([^"]+)"',read('main/ui/app_icons.hpp'))})
    return {name:bytes(int(h,16) for h in re.findall(r'\\x([0-9A-Fa-f]{2})',value)).decode('utf-8') for name,value in symbols.items()}

def icon_text(expression):
    definitions=icon_definitions()
    tokens=re.findall(r'LV_SYMBOL_\w+|ui::icons::\w+',expression)
    assert tokens and not re.sub(r'LV_SYMBOL_\w+|ui::icons::\w+','',expression).strip(),expression
    return ''.join(definitions[t] for t in tokens)

def registrations():
    return re.findall(r'registerApp\(\s*"([^"]+)",\s*([^,]+),\s*\[\]\s*\{\s*return std::make_unique<apps::(\w+)>',read('main/main.cpp'))

def app_metrics(size):
    text=read('main/ui/lv_font_app_icons.c')
    text=text.split(f'static LV_ATTRIBUTE_LARGE_CONST const uint8_t bitmap_{size}[]',1)[1]
    text=text.split('static LV_ATTRIBUTE_LARGE_CONST',1)[0]
    metrics=font_metrics(f'../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_{size}.c')
    metrics.update(font_metrics('',text=text))
    return metrics
class MenuLayoutContracts(unittest.TestCase):
    def test_order_keys_are_stable_unique_and_independent_of_labels(self):
        main=read('main/main.cpp')
        keys=re.findall(r'return std::make_unique<apps::\w+>\(\);\s*\},"([a-z0-9_]+)"\)',main)
        self.assertEqual(len(keys),len(registrations()))
        self.assertEqual(len(set(keys)),len(keys))
        self.assertEqual(keys[-1],'settings')
        self.assertEqual(keys,'vault clock timer text hid photos recorder random calendar programming patterns bt_music mp3 chiptune flashlight morse files garden lan speedtest fc dino maze xiaozhi settings'.split())
        self.assertIn('order.ids[from]==Settings||order.ids[to]==Settings',read('main/core/app_order_model.hpp'))
    def test_grid_and_touch_targets_preserved(self):
        s=read('main/ui/home_screen.cpp')
        for token in ('LV_FLEX_FLOW_ROW_WRAP','LV_DIR_VER','kTileW,53','(theme::kContentW - theme::kGap) / 2','static_assert(kNameWidth >= 4*14'):
            self.assertIn(token,s)
        self.assertEqual((240-1)//2,119)
    def test_inset_icon_slot_and_text_geometry(self):
        s=read('main/ui/home_screen.cpp')
        for token in ('kTilePad = 8','kIconSlot = 32','kIconGap = 6','kNameOffset = kIconSlot + kIconGap','kNameWidth = kTileW - 2*kTilePad - kNameOffset','lv_obj_set_style_pad_hor(btn,kTilePad,0)','lv_obj_set_width(icon,kIconSlot)','lv_obj_set_style_text_align(icon,LV_TEXT_ALIGN_CENTER,0)','lv_obj_set_width(name, kNameWidth)','lv_obj_align(name, LV_ALIGN_LEFT_MID, kNameOffset, 0)'):
            self.assertIn(token,s)
        self.assertEqual(119-2*8-32-6,65)
    def test_icon_fonts_keep_existing_symbol_fallbacks(self):
        self.assertIn('icons::font24()',read('main/ui/home_screen.cpp'))
        self.assertIn('icons::font48()',read('main/ui/focus_pager.hpp'))
        for size in (24,48):self.assertIn(f'.fallback = &lv_font_montserrat_{size}',read('main/ui/lv_font_app_icons.c'))
        self.assertIn('CONFIG_LV_FONT_MONTSERRAT_24=y',read('sdkconfig'))
        self.assertNotIn('lv_font_montserrat_18',read('main/ui/home_screen.cpp'))
    def test_all_real_registered_icon_glyphs_fit_inset_slot(self):
        metrics=app_metrics(24);icons=registrations()
        self.assertEqual(len(icons),25)
        for name,expr,_ in icons:
            glyphs=[metrics[ord(c)] for c in icon_text(expr)]
            advance=sum((g['adv_w']+8)//16 for g in glyphs)
            self.assertLessEqual(advance,32,name);pen=8+(32-advance)//2
            for g in glyphs:
                left=pen+g['ofs_x'];self.assertGreaterEqual(left,8,name)
                self.assertLessEqual(left+g['box_w'],8+32,name)
                top=(53-27)//2+27-5-g['box_h']-g['ofs_y']
                self.assertGreaterEqual(top,8,name);self.assertLessEqual(top+g['box_h'],53-8,name)
                pen+=(g['adv_w']+8)//16
    def test_every_app_has_a_unique_consistent_icon(self):
        icons=registrations();texts=[icon_text(expr) for _,expr,_ in icons]
        self.assertEqual(len(set(texts)),len(icons),'App icons must not repeat')
        headers=list((ROOT/'main/apps').glob('*.hpp'))
        for name,expr,cls in icons:
            header=next(p for p in headers if re.search(r'\bclass '+cls+r'(?:\s+final)?\s*:',p.read_text(encoding='utf-8')))
            text=header.read_text(encoding='utf-8')
            match=re.search(r'\bicon\([^)]*\)[^{;]*\{\s*return ([^;]+);',text)
            if not match:
                text=header.with_suffix('.cpp').read_text(encoding='utf-8')
                match=re.search(cls+r'::icon\([^)]*\)[^{;]*\{\s*return ([^;]+);',text)
            self.assertIsNotNone(match,name)
            self.assertEqual(icon_text(match[1]),icon_text(expr),name)
    def test_custom_artwork_is_unique_static_and_fits_focus_badge(self):
        from generate_app_icons import artwork,packed,source
        shapes=[packed(p) for p in artwork()]
        self.assertEqual(len(set(shapes)),9)
        self.assertTrue(all(any(p) and not all(b==255 for b in p) for p in shapes))
        self.assertEqual(read('main/ui/lv_font_app_icons.c'),source())
        self.assertEqual(sum(len(p) for p in shapes)*5,3240)
        for name,expr,_ in registrations():
            glyphs=[app_metrics(48)[ord(c)] for c in icon_text(expr)]
            self.assertLessEqual(sum((g['adv_w']+8)//16 for g in glyphs),70,name)
            for g in glyphs:
                top=(70-56)//2+56-10-g['box_h']-g['ofs_y']
                self.assertGreaterEqual(top,0,name);self.assertLessEqual(top+g['box_h'],70,name)
    def test_tiles_shortcut_opens_hid_without_wifi_polling(self):
        s=read('main/ui/home_screen.cpp')
        self.assertIn('tile("蓝牙键鼠",144,88,96,90)',s)
        self.assertIn('openNamed,const_cast<char *>(name)',s)
        self.assertNotIn('Connectivity::snapshot',s)
        self.assertNotIn('网络设置',s)
        self.assertIn('const_cast<char *>("设置")',s)
    def test_all_app_names_fit_and_four_hanzi_still_fit(self):
        glyphs=font_metrics('main/ui/lv_font_menu_14.c')
        for name in re.findall(r'registerApp\(\s*"([^"]+)"',read('main/main.cpp')):
            self.assertLessEqual(sum((glyphs[ord(c)]['adv_w']+8)//16 for c in name),65,name)
        self.assertLessEqual(4*max((x['adv_w']+8)//16 for x in glyphs.values()),65)
    def test_naming_is_documentation_only(self):
        for path in ('README.md','../docs/firmware/project_naming.md','../docs/firmware/lan_hotspot_20260920.md','../docs/server/README.md','../packaging/nas/README.md'):
            self.assertIn('雨墨',read(path),path)
        self.assertIn('DefaultSsid[]="Yumo-LAN"',read('main/core/lan_hotspot_model.hpp'))
        self.assertIn('project(01_factory)',read('CMakeLists.txt'))
if __name__=='__main__':unittest.main(verbosity=2)
