"""Calendar/clock layout, fonts and lifecycle contracts; arithmetic has C++ oracles."""
from pathlib import Path
import re,unittest
from update_menu_font import read_font
from check_battery_power import body
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf8')
def words(source,name):return re.findall(r'"([^"]+)"',re.search(r'\b'+name+r'\[\]\s*=\s*\{(.*?)\};',source,re.S)[1])

class AlmanacContracts(unittest.TestCase):
    def test_theme_changes_colors_only_and_rows_fit(self):
        s=read('main/ui/almanac_clock.cpp');rects=re.findall(r'rows\[(\d)\]\.create\(area,(\d+),(\d+),(\d+),(\d+),',s)
        self.assertEqual(len(rects),7);last=0
        for i,x,y,w,h in rects:
            x,y,w,h=map(int,(x,y,w,h));self.assertGreaterEqual(y,last);last=y+h
            self.assertLessEqual(x+w,240);self.assertLessEqual(y+h,240)
        self.assertIn('ui_rolling_clock_create(&clock,area,4,216,232,24,theme::fontSmall(),theme::fg())',s)
        for branch in ('theme::current','theme::warm','theme::tiles','theme::focus','theme::terminal','theme::radius'):self.assertNotIn(branch,s)
    def test_big_chinese_font_covers_every_lunar_and_hour_value(self):
        font=read_font(read('main/ui/lv_font_almanac_24.c'));cal=read('main/core/calendar_model.hpp');model=read('main/core/almanac_model.hpp')
        chars=''.join(words(cal,'Months')+words(cal,'Days')+words(cal,'Stems')+words(cal,'Branches')+words(model,'HourNames'))+'闰时 黄历'
        self.assertTrue(all(ord(c) in font for c in chars))
        def width(text):return sum(font[ord(c)].adv_w for c in text)/16
        for month in words(cal,'Months'):
            for day in words(cal,'Days'):self.assertLessEqual(width('闰'+month+day),232)
        for branch in words(cal,'Branches'):
            for name in words(model,'HourNames'):self.assertLessEqual(width(branch+'时  '+name),232)
        self.assertLessEqual(width('癸亥 癸亥 癸亥 癸亥'),232)
    def test_detail_and_clock_small_glyphs_and_worst_time_fit(self):
        font=read_font(read('main/ui/lv_font_cjk_16.c'));model=read('main/core/almanac_model.hpp')
        chars=''.join(words(model,'NaYin')+words(model,'StarGods')+words(model,'ChineseDigits'))+'农历生肖时星神时柱五行节气年月日分秒请先设置设备日期天秤白羊金牛双子巨蟹狮子处女天蝎射手摩羯水瓶双鱼座'
        for c in chars:self.assertIn(ord(c),font,c)
        def number(n):return '零一二三四五六七八九'[n//10]+'零一二三四五六七八九'[n%10]
        for h in range(24):
            for m in range(60):
                text=number(h)+'时'+number(m)+'分五九秒'
                self.assertLessEqual(sum(font[ord(c)].adv_w for c in text)/16,232)
    def test_clock_lazy_creation_hidden_draws_and_animation_cleanup(self):
        s=read('main/apps/watchface.cpp');ui=read('main/ui/almanac_clock.cpp')
        self.assertIn('kFaceCount = 4',s);self.assertIn('if(m_face==3)',body(s,'void Watchface::applyFace('))
        self.assertIn('enableGestureBubbleRecursive(m_faceArea)',body(s,'void Watchface::applyFace('))
        for name in ('onHide','onDestroy'):self.assertIn('m_almanac.stop()',body(s,'void Watchface::'+name+'('))
        self.assertIn('if(!sys::Power::isScreenOn()) {self->m_almanac.stop();return;}',s)
        self.assertIn('if(!area)create(parent)',ui)
        for sig,source in (('void Watchface::refresh(',s),('void AlmanacClock::refresh(',ui)):
            method=body(source,sig)
            for token in ('lv_obj_create','lv_label_create','fopen','esp_http','nvs_'):self.assertNotIn(token,method)
    def test_three_elements_fit_one_line_for_every_pillar(self):
        font=read_font(read('main/ui/lv_font_menu_14.c'));model=read('main/core/almanac_model.hpp')
        stems=words(model,'StemElements');branches=words(model,'BranchElements');nayin=words(model,'NaYin')
        self.assertEqual(len(stems),10);self.assertEqual(len(branches),12);self.assertEqual(len(nayin),30)
        for i in range(60):
            text=f'天干:{stems[i%10]} 地支:{branches[i%12]} 纳音:{nayin[i//2]}'
            for c in text:self.assertIn(ord(c),font,c)
            self.assertLessEqual(sum(font[ord(c)].adv_w for c in text)/16,232,text)
        s=read('main/ui/almanac_clock.cpp')
        self.assertIn('rows[6].create(area,4,192,232,22,theme::fontMenu()',s)
        self.assertIn('"天干:%s 地支:%s 纳音:%s",cal::StemElements[a.time.stem],cal::BranchElements[a.time.branch],cal::NaYin[a.time.cycle()/2]',s)
    def test_calendar_sheet_colors_come_from_active_palette(self):
        s=body(read('main/apps/calendar.cpp'),'void Calendar::showDetail(')
        for color in ('fg','accent','onAccent','edge'):self.assertIn(f'ui::theme::{color}()',s)
        # The persistent sheet/page inherit their paper color from compact::panel.
        self.assertIn('dialog=c::panel(root(),240,240)',s)
        self.assertIn('detailPage=c::panel(dialog,240,204)',s)
        panel=body(read('main/ui/compact_ui.cpp'),'lv_obj_t *panel(')
        self.assertIn('lv_obj_set_style_bg_color(obj,theme::bg(),0)',panel)
        self.assertNotIn('lv_color_hex(',s)
        self.assertIn('ui_control_ink(back,headingInk,headingInk)',s)
        self.assertIn('lv_color_mix(highlight,ink,220)',s)
    def test_tear_off_detail_has_fields_and_no_old_note(self):
        s=read('main/apps/calendar.cpp');detail=body(s,'void Calendar::showDetail(')
        update=body(s,'void Calendar::updateDetail(')
        self.assertIn('updateDetail()',detail)
        for field in ('cal::constellation(d)','cal::dateAlmanac(d)','cal::Animals','cal::Months','cal::Days'):self.assertIn(field,update)
        for token in ('lv_obj_create','lv_obj_delete','lv_label_create','c::panel'):self.assertNotIn(token,update)
        self.assertNotIn('按中国时区',s);self.assertNotIn('lv_textarea',detail)
        self.assertIn('40,36',detail);self.assertIn('closePending=true',detail)
    def test_animation_commits_pending_value_and_deletes_callback(self):
        s=read('main/ui/rolling_text.h')
        self.assertIn('lv_anim_delete(r,ui_rolling_animate)',s);self.assertIn('if(r->active)ui_rolling_commit(r)',s)
        self.assertIn('lv_obj_set_overflow_visible',s);self.assertIn('lv_anim_set_duration(&a,280)',s)
        update=body(s,'static inline void ui_rolling_set(')
        self.assertNotIn('lv_obj_create',update);self.assertNotIn('lv_label_create',update)
        self.assertLess(update.index('strcmp('),update.index('lv_label_set_text('))
if __name__=='__main__':unittest.main()
