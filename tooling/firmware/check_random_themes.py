"""Source-only theme contracts; native LVGL and browser tests run separately."""
from pathlib import Path
import re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path):return (ROOT/path).read_text(encoding='utf8')

class RandomThemes(unittest.TestCase):
    def test_random_is_a_real_independent_preview_scene(self):
        s=read('../desktop/ui-preview/unified.js')
        for token in ("random:'随机'","window.RandomPreview.mount","random?'random.css'","card.controller?.key?.(key)"):
            self.assertIn(token,s)
        self.assertIn('random-preview.js',read('../desktop/ui-preview/index.html'))
        self.assertIn("['随机','random','random']",read('../desktop/ui-preview/preview.js'))
        self.assertIn("if(target==='random') navigate(s,target)",read('../desktop/ui-preview/layouts.js'))

    def test_all_modes_and_pk_inputs_preserved(self):
        s=read('../desktop/ui-preview/random-preview.js')
        for word in ('数字','骰子','选项英文','选项数字','双色球','硬币','胜负','石头剪刀布'):
            self.assertIn(word,s)
        for token in ('below(3)','s.players.every(p=>p===2)','Math.min(9999','s.balls=red.sort','0x100000000','s.result<0','s.mode===7'):
            self.assertIn(token,s)
        self.assertNotRegex(s,r'fetch\(|XMLHttpRequest|setInterval|navigator\.bluetooth|localStorage')
        self.assertIn('document.removeEventListener',s)
        self.assertIn('if(!visible&&s.running)cancel()',s)

    def test_firmware_shares_logic_not_random_generators(self):
        s=read('main/apps/randomizer.cpp')
        for token in ('layout::number(ui::theme::current())','layout::cell(ui::theme::current()','layout::ball(ui::theme::current()',
                      'showNumber(final?m_result:model::integer','LV_COORD_MAX','set_style_outline_pad(obj,-1','m_timer=nullptr'):
            self.assertIn(token,s)
        self.assertEqual(s.count('lv_timer_create('),1)
        self.assertIn('rect.height<32?&lv_font_montserrat_18:&lv_font_montserrat_24',s)
        self.assertNotRegex(s,r'xTaskCreate|nvs_set|lv_anim_start')

    def test_terminal_clock_headers_and_live_calendar(self):
        s=read('main/apps/watchface.cpp')
        preview=read('../desktop/ui-preview/clock-engine.js')
        for old in ('YUMO / RTC','YUMO / ANALOG','RTC / HYBRID'):
            self.assertNotIn(old,s)
            self.assertNotIn(old,preview)
        self.assertEqual(s.count('caption(0,"YUMO",6,6)'),2)
        analog=s.split('// Keep one date/weekday row above the dial;',1)[1].split('    } else {',1)[0]
        for token in ('x=10;y=20;size=220','m_centerDate,LV_ALIGN_TOP_LEFT,6,5',
                      'setVisible(m_weekday,true)','m_weekday,LV_ALIGN_TOP_RIGHT,-6,5',
                      'm_weekday,&lv_font_montserrat_10'):
            self.assertIn(token,analog)
        live=s.split('    } else if(ui::theme::terminal()) {',1)[1].split('    } else {',1)[0]
        self.assertIn('setText(m_centerDate,dateBuf)',live)
        self.assertIn('setText(m_weekday,weekdays[value.tm_wday%7])',live)
        self.assertIn("case 'terminal':label(date,6,5,10,t.muted);label(n=>week[n.weekday],234,5,10,t.accent,'right')",preview)

    def test_warm_clock_matches_existing_preview(self):
        s=read('main/apps/watchface.cpp')
        preview=read('../desktop/ui-preview/clock-engine.js')
        warm=s.split('    int x=0,y=0,size=240;',1)[1].split('        } else if(t::focus()) {',1)[0]
        for token in ('x=3;y=43;size=152','caption(0,"Y U M O",12,13)',
                      'm_centerDate,LV_ALIGN_TOP_RIGHT,-11,14',
                      'm_centerTime,LV_ALIGN_TOP_MID,79,73','m_seconds,LV_ALIGN_TOP_MID,79,128',
                      'm_weekday,LV_ALIGN_TOP_MID,0,218','m_weekday,&lv_font_montserrat_14'):
            self.assertIn(token,warm)
        self.assertNotIn('HOURS / MIN',s)
        for token in ('const bool smallHybrid=hybrid&&(t::warm()||t::tiles())',
                      'setVisible(m_numerals[i],!hybrid||smallHybrid)',
                      'smallHybrid?&lv_font_montserrat_12','center-(smallHybrid?16:26)',
                      'lv_obj_set_style_outline_width(m_dial,warmHybrid?1:0,0)',
                      'box(0,82,160,76,27)','m_centerDate,LV_ALIGN_TOP_MID,0,167',
                      '"%s / %02d SEC",weekdays[value.tm_wday%7],value.tm_sec',
                      '"%s %02d",weekdays[value.tm_wday%7],value.tm_mday'):
            self.assertIn(token,s)
        self.assertIn("dial(79,119,76,{small:true})",preview)
        self.assertIn("week[n.weekday]+' / '+seconds(n),120,218,14",preview)
        self.assertIn("week[n.weekday]+' '+pad(n.day),120,167,12",preview)

    def test_mono_clock_matches_existing_preview(self):
        s=read('main/apps/watchface.cpp')
        preview=read('../desktop/ui-preview/clock-engine.js')
        for token in ('constexpr int xs[]={76,-41,-82,-34,0,34,82,79}',
                      'constexpr int ys[]={46,46,-13,-13,-13,-13,-13,89}',
                      'const Rect mono[]={{0,0,240,240},{0,37,240,1},{0,186,240,1}',
                      'caption(0,"TIME / 24H",9,10);caption(1,"SEC",9,204)',
                      'lv_obj_set_width(m_date,140)','lv_obj_set_width(m_weekday,68)',
                      'lv_obj_set_width(m_seconds,64)','lv_obj_set_width(m_colon,20)',
                      'm_weekday,LV_TEXT_ALIGN_RIGHT','m_seconds,LV_TEXT_ALIGN_RIGHT',
                      'm_seconds,&lv_font_montserrat_32','lv_obj_set_width(label,LV_SIZE_CONTENT)',
                      'm_centerDate,LV_ALIGN_TOP_MID,0,165'):
            self.assertIn(token,s)
        self.assertNotIn('Mono?"%02d SEC"',s)
        live=s.split('    } else if(ui::theme::current()==ui::theme::Id::Mono) {',1)[1].split('    } else if',1)[0]
        self.assertIn('"%02d.%02d",value.tm_mon+1,value.tm_mday',live)
        for token in ("label('TIME / 24H',9,10,14)","label(hm,120,77,84",
                      "label(date,9,158,14","label('SEC',9,204,14",
                      "pad(n.second),231,192,32","pad(n.month)+'.'+pad(n.day),120,165,12"):
            self.assertIn(token,preview)

    def test_tiles_clock_matches_existing_preview(self):
        s=read('main/apps/watchface.cpp')
        preview=read('../desktop/ui-preview/clock-engine.js')
        for token in ('constexpr int tileX[]={73,73,-74,-26,0,35,83,73}',
                      'constexpr int tileY[]={-16,13,-31,-31,0,80,80,-86}',
                      'caption(3,"0000.00",10,212)','lv_obj_set_width(m_captions[3],100)',
                      'lv_obj_set_width(caption,LV_SIZE_CONTENT)',
                      'm_date,&lv_font_montserrat_24','m_seconds,&lv_font_montserrat_40',
                      'setText(m_date,day)','"%04d.%02d",value.tm_year+1900,value.tm_mon+1',
                      'm_seconds,LV_ALIGN_TOP_MID,85,56','m_weekday,LV_ALIGN_TOP_MID,85,126',
                      'm_captions[0],LV_ALIGN_TOP_MID,85,14',
                      'm_centerTime,LV_ALIGN_TOP_LEFT,9,175','m_centerDate,LV_ALIGN_TOP_RIGHT,-9,178',
                      'm_captions[3],LV_ALIGN_TOP_RIGHT,-9,209',
                      'setText(m_captions[3],yearMonth)','setText(m_captions[3],year)',
                      'setVisible(m_numerals[i],!hybrid||smallHybrid)'):
            self.assertIn(token,s)
        for token in ("pad(n.hour),70,59,84","pad(n.second),193,12,40",
                      "week[n.weekday],193,94,18","pad(n.day),193,120,24",
                      "pad(n.minute),227,170,84","n.year+'.'+pad(n.month),10,212,12",
                      "dial(83,83,79,{small:true,rim:false})","pad(n.second),205,56,40",
                      "label(hm,9,175,40","pad(n.month)+'.'+pad(n.day),231,178,12",
                      "String(n.year),231,209,12"):
            self.assertIn(token,preview)

    def test_focus_clock_matches_existing_preview(self):
        s=read('main/apps/watchface.cpp')
        preview=read('../desktop/ui-preview/clock-engine.js')
        focus=s.split('    int x=0,y=0,size=240;',1)[1].split('        } else if(t::focus()) {',1)[1].split('        } else if(t::tiles()) {',1)[0]
        for token in ('x=92;y=64;size=144','for(unsigned i=0;i<2;++i)',
                      'setVisible(m_digits[i],true)',
                      'm_digits[i],LV_ALIGN_TOP_LEFT,8+i*48,4',
                      'm_centerTime,LV_ALIGN_TOP_LEFT,8,146','m_centerTime,t::accent()',
                      'm_centerDate,LV_ALIGN_TOP_RIGHT,-10,39',
                      'm_weekday,LV_ALIGN_TOP_RIGHT,-10,15','m_weekday,&lv_font_montserrat_14',
                      'm_seconds,LV_ALIGN_TOP_LEFT,8,202','m_seconds,&lv_font_montserrat_12',
                      'm_seconds,t::muted()','caption(3,"0000",8,226)'):
            self.assertIn(token,focus)
        self.assertNotIn('"MIN"',focus)
        self.assertNotIn('"HOURS"',focus)
        self.assertNotIn('transform_rotation',focus)
        live=s.split('void Watchface::refresh(void)',1)[1].split('    if (hybrid) {',1)[1]
        for token in ('if(ui::theme::tiles() || ui::theme::focus())',
                      '"%02d.%02d",value.tm_mon+1,value.tm_mday','setText(m_captions[3],year)',
                      'const int hours[]={value.tm_hour/10,value.tm_hour%10}',
                      'setText(m_digits[i],digit)','setText(m_centerTime,minute)',
                      '"%02d SEC",value.tm_sec','setText(m_seconds,seconds)',
                      'const bool focusHybrid=hybrid&&ui::theme::focus()',
                      'warmHybrid?17:tilesHybrid?16:focusHybrid?7:14',
                      'previewHybrid?secondLength*82/100','previewHybrid?secondLength*57/100'):
            self.assertIn(token,live)
        analog=s.split('\n    } else if(t::focus()) {',1)[1].split('\n    } else if(t::tiles()) {',1)[0]
        self.assertIn('m_centerDate,LV_ALIGN_TOP_MID,0,160',analog)
        date=live.split('    } else if(ui::theme::warm() || ui::theme::focus()) {',1)[1].split('    } else if',1)[0]
        self.assertIn('"%s %02d",weekdays[value.tm_wday%7],value.tm_mday',date)
        self.assertIn('t::warm()||t::focus()||(t::tiles()&&hybrid)',s)
        for token in ("pad(n.hour),8,4,84","week[n.weekday],230,15,14",
                      "pad(n.month)+'.'+pad(n.day),230,39,12",
                      "dial(164,136,72,{numbers:false,rim:false});hands(164,136,65)",
                      "pad(n.minute),8,146,48","label(seconds,8,202,12",
                      "String(n.year),8,226,10","week[n.weekday]+' '+pad(n.day),120,160,12"):
            self.assertIn(token,preview)

    def test_tiles_digital_fixed_cells_fit_screen(self):
        s=read('main/apps/watchface.cpp')
        xs=[int(v) for v in re.search(r'constexpr int tileX\[\]=\{([^}]+)',s).group(1).split(',')]
        ys=[int(v) for v in re.search(r'constexpr int tileY\[\]=\{([^}]+)',s).group(1).split(',')]
        # Weekday/day/hour digits/hidden colon/minute digits/seconds, then year-month.
        sizes=[(68,21),(64,27),(48,61),(48,61),(19,61),(48,61),(48,61),(64,44)]
        cells=list(zip(xs,ys,sizes))+[(-60,99,(100,15))]
        for x,y,(w,h) in cells:
            self.assertLessEqual(abs(x)+w/2,120)
            self.assertLessEqual(abs(y)+h/2,120)

    def test_display_rectangles_do_not_leak_old_alignment(self):
        s=read('main/apps/watchface.cpp')
        self.assertIn('lv_obj_align(m_dial,LV_ALIGN_TOP_LEFT,x,y)',s)
        self.assertIn('lv_obj_align(m_dial,LV_ALIGN_TOP_LEFT,0,0)',s)
        self.assertIn('lv_obj_align(m_captions[i],LV_ALIGN_TOP_LEFT,x,y)',s)
        self.assertIn('lv_obj_align(line,LV_ALIGN_TOP_LEFT,0,0)',s)
        self.assertNotIn('lv_obj_center(m_dial)',s)
        self.assertNotIn('lv_obj_set_pos(m_dial,',s)
        # Local LVGL confirms set_pos preserves the existing style alignment.
        lv=read('../dependencies/managed_components/lvgl__lvgl/src/core/lv_obj_pos.c')
        pos=lv.split('void lv_obj_set_pos(',1)[1].split('void lv_obj_set_x(',1)[0]
        self.assertNotIn('set_style_align',pos)
        align=lv.split('void lv_obj_align(',1)[1].split('void lv_obj_align_to(',1)[0]
        self.assertIn('lv_obj_set_style_align(obj, align, 0)',align)

if __name__=='__main__':unittest.main(verbosity=2)
