"""Native 04/05/06/08/10 contracts and geometry/color models. No build or device I/O.

Models read production constants/font metrics; they are not LVGL runtime tests.
"""
from pathlib import Path
import itertools
import math
import re
import unittest
from check_menu_layout import font_metrics

FONT_LAYOUT_SHA256="b25345fad4ade987271f47d87c51f0a25ac1c5b530edbc64fe71fbb007784680"
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(name): return (ROOT/name).read_text(encoding='utf-8-sig')
def part(text,start,end): return text.split(start,1)[1].split(end,1)[0]
def strip_theme_commit(text):
    """Keep old deep-sleep contracts independent from the new sibling branch."""
    marker='    if(shutdownRequested) {'
    if marker in text:
        begin=text.index(marker);depth=0
        for i in range(text.index('{',begin),len(text)):
            if text[i]=='{':depth+=1
            if text[i]=='}':
                depth-=1
                if depth==0:text=text[:begin]+text[i+1:];break
    start=text.index('    if(restartTheme) {',text.index('Settings::flush(true)'))
    depth=0
    for i in range(text.index('{',start),len(text)):
        if text[i]=='{': depth+=1
        if text[i]=='}':
            depth-=1
            if depth==0: return text[:start]+text[i+1:]
    raise AssertionError('Unclosed theme commit branch')

def palettes():
    rows=re.findall(r'return \{([^}]+)\};',read('main/core/theme_model.hpp'))
    return [[int(v.strip(),0) for v in row.split(',')] for row in rows]
def luminance(rgb):
    # RGB565 quantization used by this display, not ideal 24-bit mockup colors.
    channels=[(rgb>>16)&255,(rgb>>8)&255,rgb&255]
    channels=[round((v>>(3 if i!=1 else 2))*255/(31 if i!=1 else 63))/255 for i,v in enumerate(channels)]
    linear=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in channels]
    return sum(v*k for v,k in zip(linear,[.2126,.7152,.0722]))
def contrast(a,b):
    lo,hi=sorted([luminance(a),luminance(b)])
    return (hi+.05)/(lo+.05)
def font(size):
    return font_metrics(f'../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_{size}.c')
def width(text,size):
    glyphs=font(size)
    return sum((glyphs[ord(c)]['adv_w']+8)//16 for c in text)

class NativeThemes(unittest.TestCase):
    def test_stable_ids_and_default_terminal(self):
        s=read('main/core/theme_model.hpp')
        self.assertIn('Warm=4, Mono=5, Tiles=6, Focus=8, Terminal=10',s)
        self.assertIn('Default=Id::Terminal',s)
        self.assertIn('valid(id)?static_cast<Id>(id):Default',s)
        allowed=set(map(int,re.findall(r'id==(\d+)',s)))
        self.assertEqual(allowed,{4,5,6,8,10})
        for value in range(-128,256):
            normalized=value if value in allowed else 10
            self.assertIn(normalized,allowed)
        self.assertIn('themes::Id  theme      = themes::Default',read('main/core/settings_store.hpp'))
        self.assertIn('c.theme=themes::normalize(value)',read('main/core/settings_store.cpp'))

    def test_theme_is_loaded_once_before_any_gui(self):
        s=read('main/main.cpp')
        order=['Settings::load();','ESP_ERROR_CHECK(bsp_display_lock(-1));','ui::theme::init(','WindowManager::instance().init()','bsp_display_unlock();']
        self.assertEqual([s.index(x) for x in order],sorted(s.index(x) for x in order))
        # This BSP returns esp_err_t, and zero timeout is a nonblocking take.
        # A failed take must not allow app_main to race the running LVGL task.
        adapter=read('../dependencies/managed_components/espressif__esp_lvgl_adapter/src/adapter/esp_lv_adapter.c')
        self.assertIn('(timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)',adapter)
        self.assertNotIn('bsp_display_lock(0)',s)
        all_gui='\n'.join(p.read_text(encoding='utf8') for p in (ROOT/'main/apps').glob('*.cpp'))
        self.assertNotIn('theme::init(',all_gui)
        self.assertNotIn('nvs_',all_gui)

    def test_theme_save_has_no_deferred_dirty_retry(self):
        s=part(read('main/core/settings_store.cpp'),'bool Settings::saveTheme(', 'void Settings::saveClock')
        for token in ('themes::valid','nvs_set_i32(h,"theme"','nvs_commit(h)','if(err!=ESP_OK) return false;','cache().theme=id'):
            self.assertIn(token,s)
        self.assertLess(s.index('nvs_commit(h)'),s.index('cache().theme=id'))
        self.assertNotRegex(s,r'\b(?:mark|dirty|retryAt)\b|nvs_erase|nvs_flash_init')
        self.assertEqual(re.findall(r'nvs_set_\w+\(h,"([^"]+)"',s),['theme'])

    def test_settings_confirm_cancel_current_and_duplicate(self):
        s=read('main/apps/settings.cpp')
        self.assertIn('menuRow("主题",Page::Theme)',s)
        self.assertIn('case Page::Theme: buildTheme()',s)
        choices=part(s,'void Settings::buildTheme()', 'void Settings::markListTouch')
        self.assertIn('if(self->themeSubmitted) return',choices)
        self.assertIn('id==static_cast<uint8_t>(ui::theme::current())',choices)
        self.assertIn('self->ask(Dialog::Theme',choices)
        self.assertNotIn('switchTheme(',choices)
        # Leave room for both CJK lines plus the selected row's 2px border;
        # all five choices scroll in the 203px body, with unchanged touch height.
        self.assertIn('this,240,54)',choices)
        self.assertIn('set_pos(detail,5,25)',choices)
        self.assertLessEqual(2+1+25+22,54-2)
        self.assertGreater(24+5*54+5,240-37)
        self.assertIn("sizeof(ids)/sizeof(ids[0])",choices)
        self.assertIn("c::kHeight-bodyTop,true",s)
        submit=part(s,'void Settings::processDialog()', 'void Settings::refresh')
        self.assertLess(submit.index('if(yes)'),submit.index('switchTheme(selectedTheme)'))
        self.assertIn('themeSubmitted=err==ESP_OK',submit)
        self.assertEqual(s.count('switchTheme('),1)
        self.assertIn('themeSubmitted=false; report',s)

    def test_worker_values_and_gui_close_ack(self):
        s=read('main/core/system_service.cpp')
        self.assertIn('uint8_t theme=0',s)
        self.assertIn('cmd.theme=id; return enqueue(cmd)',s)
        self.assertIn('Power::restartForTheme(static_cast<themes::Id>(cmd.theme))',s)
        self.assertIn('Power::sleepPending()?ESP_ERR_INVALID_STATE:execute(cmd)',s)
        pump=part(read('main/main.cpp'),'static void ui_pump_cb', '// Registered apps')
        self.assertLess(pump.index('goHome()'),pump.index('acknowledgeSleepUi()'))
        close=part(read('main/ui/window_mgr.cpp'),'void WindowManager::closeActive()', 'void WindowManager::goBack')
        self.assertLess(close.index('onHide()'),close.index('onDestroy()'))
        self.assertLess(close.index('onDestroy()'),close.index('m_active.reset()'))

    def test_restart_only_after_all_resource_fences(self):
        s=part(read('main/core/power_mgr.cpp'),'void Power::processStandby()', 'void Power::syncTouch')
        for fence in ('uiStopped.load()','DisplayCast::idle()','BleHid::keysIdle()','LanHotspot::idle()',
                      'core::sd::busy()','core::storageIdle()','vault::idle()','core::recorder::idle()','core::fc::idle()'):
            self.assertLess(s.index(fence),s.index('Settings::saveTheme('),fence)
        order=['Settings::saveClock(time(nullptr))','Settings::flush(true)','core::sd::prepareRestart()',
               'Settings::saveTheme(','themeRestartReady.store(true)','xTaskNotifyGive(sleepTask)']
        self.assertEqual([s.index(x) for x in order],sorted(s.index(x) for x in order))
        for error in ('theme restart SD unmount failed','theme save failed','shutdown/restart drain timeout'):
            self.assertIn('requestRecovery("'+error+'")',s)

    def test_sd_partial_unmount_cannot_be_retried_as_success(self):
        s=part(read('main/core/sd_media.cpp'),'RestartState prepareRestart()', 'void Session::release()')
        self.assertIn('!sys::Power::sleepUiReady()',s)
        for guard in ('lock.owns_lock()','users','busy()','teardownFailed','damaged','bsp_sdcard_ready()'):
            self.assertLess(s.index(guard),s.index('bsp_sdcard_unmount()'))
        self.assertIn('ready==ESP_ERR_NOT_FINISHED',s)
        self.assertIn('teardownFailed=true;failEject(value,Failure::Unmount)',s)
        self.assertLess(s.index('teardownFailed)'),s.index('!bsp_sdcard_get_handle()'))

    def test_restart_has_no_forced_sleep_fallback(self):
        s=read('main/core/power_mgr.cpp')
        entry=part(s,'void sleepEntry(', 'void setScreenLocked(')
        self.assertIn('if(themeRestartReady.load() && !shuttingDown.load()) esp_restart()',entry)
        self.assertLess(entry.index('esp_restart()'),entry.index('pdMS_TO_TICKS(8000)'))
        theme=part(s,'    if(restartTheme) {\n        if(wakeRequested.load())', '// A failed suspend')
        self.assertNotIn('sleepReady.store',theme)
        self.assertNotIn('esp_wifi_stop',theme)
        self.assertNotIn('holdPerformance(false)',theme)
        self.assertIn('restartTheme=0; themeRestartFailed.store(true)',s)
        self.assertIn('takeThemeRestartError()',read('main/ui/window_mgr.cpp'))
        self.assertIn('if(shuttingDown.load() || sleepRequested.load()) return ESP_ERR_INVALID_STATE',s)

    def test_color_contrast_survives_rgb565(self):
        rows=palettes();self.assertEqual(len(rows),5)
        for i,p in enumerate(rows):
            bg,surface,fg,accent,muted,edge,on_accent,*_=p
            for text,background in [(fg,bg),(fg,surface),(muted,bg),(muted,surface),(on_accent,accent)]:
                self.assertGreaterEqual(contrast(text,background),4.5,(i,hex(text),hex(background)))

    def test_button_ink_inherits_and_follows_state_without_null_transition(self):
        s=read('main/ui/compact_ui.cpp')
        label=part(s,'lv_obj_t *label(', 'lv_obj_t *button(')
        self.assertNotIn('set_style_text_color',label)
        for token in ('ui_control_ink(obj,theme::fg(),theme::onAccent())','ui_control_transitions(obj)',
                      'void buttonBackground(', 'theme::onAccent()'):
            self.assertIn(token,s)
        self.assertNotIn('buttonInk',s)
        self.assertNotIn('lv_obj_get_state(obj)',s)
        policy=read('main/ui/control_states.h')
        for state in ('DEFAULT','PRESSED','CHECKED','DISABLED'):
            self.assertIn('lv_obj_set_style_text_color(obj, '+('selected' if state in ('PRESSED','CHECKED') else 'normal')+', LV_STATE_'+state+')',policy)
        # Cover every property merged from the installed LVGL default theme.
        upstream=part(read('../dependencies/managed_components/lvgl__lvgl/src/themes/default/lv_theme_default.c'),'trans_props[] = {','};')
        for prop in re.findall(r'LV_STYLE_\w+',upstream):self.assertIn(prop,policy)
        self.assertIn('{props, NULL, lv_anim_path_linear, 0, 0}',policy)
        self.assertNotIn('set_style_transition(obj,nullptr',s)
        self.assertNotIn('lv_timer_create',s)

    def test_shared_keyboard_and_network_fields_remain_shared(self):
        s=read('main/ui/compact_ui.cpp')
        self.assertIn('TextInputLayout::NetworkFieldHeight',s)
        self.assertIn('TextInput::bindField(obj)',s)
        self.assertIn('theme::onAccent(),selected',s)
        self.assertIn('TextInputLayout',read('main/ui/text_input.cpp'))
        self.assertIn('c::networkField',read('main/apps/password_vault.cpp'))
        self.assertEqual(read('main/ui/text_input.cpp').count('lv_timer_create'),0)

    def test_hid_dropdown_and_pk_foregrounds_fit_themes(self):
        hid=read('main/ui/hid_controls.hpp')
        for state in ('PRESSED','CHECKED'):
            self.assertIn('theme::onAccent(),LV_STATE_'+state,hid)
        self.assertIn('theme::fg(),LV_STATE_DISABLED',hid)
        dropdown=part(read('main/ui/compact_ui.cpp'),'lv_obj_t *dropdown(', 'void setText(')
        self.assertIn('ui_control_transitions(obj)',dropdown)
        self.assertIn('set_style_recolor_opa(obj, LV_OPA_TRANSP, LV_STATE_PRESSED)',read('main/ui/control_states.h'))
        random=read('main/apps/randomizer.cpp')
        self.assertIn('m_pkScore[side],teamInk(side)',random)
        self.assertIn('m_match.result==0?teamInk(0)',random)
        colors=tuple(int(v,16) for v in re.findall(r'0x([\dA-F]+)',part(random,'lv_color_t teamInk(', 'void centered(')))
        self.assertEqual(len(colors),4)
        for i,palette in enumerate(palettes()):
            for text in (colors[0 if i in (0,2,3) else 1],colors[2 if i in (0,2,3) else 3]):
                self.assertGreaterEqual(contrast(text,palette[0]),4.5)

    def test_every_app_reachable_without_factory_preload(self):
        main=read('main/main.cpp');home=read('main/ui/home_screen.cpp')
        self.assertEqual(len(re.findall(r'registerApp\(',main)),25)
        self.assertIn('if(theme::tiles()) buildTiles(parent)',home)
        self.assertIn('else if(theme::terminal()) buildTerminal(parent)',home)
        self.assertIn('else buildAppList(parent)',home)
        self.assertIn('buildAppList(allApps)',home)
        self.assertIn('terminalRows.reserve(apps.size())',home)
        self.assertNotRegex(home,r'factory\(|make_unique|new apps::')
        self.assertIn('WindowManager::instance().openApp(registeredAt(position))',home)
        self.assertIn('setCursor(cursor+1)',home)

    def test_home_tiles_fit_and_have_minimum_hit_size(self):
        s=read('main/ui/home_screen.cpp')
        rects=[tuple(map(int,r)) for r in re.findall(r'tile\("[^"]+",(\d+),(\d+),(\d+),(\d+)\)',s)]
        self.assertEqual(len(rects),5)
        for x,y,w,h in rects:
            self.assertGreaterEqual(min(w,h),36);self.assertLessEqual(x+w,240);self.assertLessEqual(y+h,179)
        for a,b in itertools.combinations(rects,2):
            self.assertTrue(a[0]+a[2]<=b[0] or b[0]+b[2]<=a[0] or a[1]+a[3]<=b[1] or b[1]+b[3]<=a[1])
        self.assertIn('240,179,true',s);self.assertIn('240,44',s)
        self.assertIn('dock,0,180',s);self.assertIn('120,36',s)
        self.assertIn('!WindowManager::instance().atHome()',s)
        self.assertIn('lv_obj_is_hidden(desktop)',s)

    def test_small_subset_font_is_not_used_for_new_chinese_copy(self):
        for name in ('main/apps/settings.cpp','main/apps/watchface.cpp'):
            self.assertNotIn('fontMenu()',read(name))
        home=read('main/ui/home_screen.cpp')
        self.assertEqual(home.count('fontMenu()'),3) # grid, drag preview and the existing HID app name
        cjk=font_metrics('main/ui/lv_font_cjk_16.c')
        for text in ('砂岩','一页','墨线','拼贴','游标','主题切换未完成','编号列表','信息磁贴','网络关闭','左右滑动选择分类'):
            for char in text:self.assertIn(ord(char),cjk)

    def test_five_fixed_digital_layouts_fit_screen(self):
        s=read('main/apps/watchface.cpp')
        def arr(n):return list(map(int,re.search(r'constexpr int '+n+r'\[\]=\{([^}]+)',s)[1].split(',')))
        weekday24=max(width(w,24) for w in ('SUN','MON','TUE','WED','THU','FRI','SAT'))
        weekday18=max(width(w,18) for w in ('SUN','MON','TUE','WED','THU','FRI','SAT'))
        date=max(width(f'{y:04}.{m:02}.{d:02}',14) for y in (2026,2088) for m in (1,12) for d in (1,28,31))
        # Mono and Tiles now use fixed live-text cells, not the previous
        # content-sized date/seconds labels. Check both text fit and cell bounds.
        self.assertLessEqual(date,140)
        self.assertLessEqual(max(width(w,14) for w in ('SUN','MON','TUE','WED','THU','FRI','SAT')),68)
        self.assertLessEqual(width('88',32),64)
        self.assertLessEqual(weekday18,68)
        self.assertLessEqual(width('88',24),64)
        self.assertLessEqual(width('88',40),64)
        for xs,ys,sizes in [
            ('warmX','warmY',[(weekday18,22),(date,17),(48,61),(48,61),(0,0),(48,61),(48,61),(width('88',24),29)]),
            ('focusX','focusY',[(weekday18,22),(date,17),(48,61),(48,61),(0,0),(48,61),(48,61),(width('88',24),29)]),
            ('xs','ys',[(68,16),(140,16),(48,61),(48,61),(20,61),(48,61),(48,61),(64,35)]),
            ('tileX','tileY',[(68,21),(64,27),(48,61),(48,61),(0,0),(48,61),(48,61),(64,44)]),
            ('terminalX','terminalY',[(weekday24,29),(date,17),(48,61),(48,61),(19,61),(48,61),(48,61),(width('88',24),29)])]:
            for x,y,(w,h) in zip(arr(xs),arr(ys),sizes):
                self.assertLessEqual(abs(x)+w/2,120,(xs,x,y,w,h))
                self.assertLessEqual(abs(y)+h/2,120,(ys,x,y,w,h))

    def test_digital_blocks_and_year_month_keep_fixed_positions(self):
        s=part(read('main/apps/watchface.cpp'),'void Watchface::layoutDigitalFace', 'void Watchface::changeFace')
        for theme in ('mono','tiles','terminal','warm','focus'):
            block=re.search(r'const Rect '+theme+r'\[\]=\{(.*?)\};',s)[1]
            for row in re.findall(r'\{([^}]+)\}',block):
                x,y,w,h=map(int,row.split(','))
                self.assertGreaterEqual(min(x,y),0);self.assertLessEqual(x+w,240);self.assertLessEqual(y+h,240)
        self.assertNotIn('transform_rotation',s)
        self.assertIn('lv_obj_set_pos(m_blocks[i],r.x,r.y)',s)
        self.assertIn('lv_obj_set_size(m_blocks[i],r.w,r.h)',s)
        self.assertIn('lv_obj_align(m_captions[3],LV_ALIGN_CENTER,-60,99)',s)
        layout=part(read('main/apps/watchface.cpp'),'void Watchface::layoutFace()', 'void Watchface::refresh')
        self.assertIn('caption(3,"0000.00",10,212)',layout)
        self.assertIn('lv_obj_set_width(m_captions[3],100)',layout)
        self.assertLessEqual(width('2088.12',12),100)
        x,y,w,h=-60,99,100,15
        self.assertLessEqual(abs(x)+w/2,120);self.assertLessEqual(abs(y)+h/2,120)

    def test_focus_seconds_caption_does_not_overlap_digits(self):
        s=read('main/apps/watchface.cpp')
        self.assertIn('caption(1,"SEC",8,153)',s)
        self.assertLessEqual(153+12+3,120+63-29/2)

    def test_analog_ticks_hands_share_resized_center(self):
        s=read('main/apps/watchface.cpp');h=read('main/apps/watchface.hpp')
        self.assertIn('m_tickLines[60]',h)
        self.assertIn('m_tickLines[i]=createHand',s)
        self.assertIn('lv_line_set_points(m_tickLines[i],m_tickPoints[i],2)',s)
        self.assertIn('const int center=m_dialPixels/2',s)
        for size in (144,152,158,198,200,226,236,238):
            center=size//2
            for i in range(60):
                a=math.radians(i*6-90)
                for value in (center+math.cos(a)*(center-2),center+math.sin(a)*(center-2)):
                    self.assertGreaterEqual(value,0);self.assertLess(value,size)
        refresh=s.split('void Watchface::refresh',1)[1]
        self.assertNotRegex(refresh,r'lv_\w+_create|\bnew\b|malloc')
        self.assertIn('if(now==m_lastRendered) return',refresh)
        self.assertNotIn('Qmi8658',s)
        self.assertIn('lv_timer_delete(m_timer)',s)

    def test_analog_hybrid_panels_and_text_fit(self):
        s=part(read('main/apps/watchface.cpp'),'void Watchface::layoutFace()', 'void Watchface::refresh')
        for row in re.findall(r'box\(\d+,(\d+),(\d+),(\d+),(\d+)',s):
            x,y,w,h=map(int,row);self.assertLessEqual(x+w,240);self.assertLessEqual(y+h,240)
        for x,y,size in ((3,43,152),(94,70,144),(2,2,236),(1,1,238),(4,4,158),(41,77,158),(21,5,198),(20,21,200),(7,7,226)):
            self.assertLessEqual(x+size,240);self.assertLessEqual(y+size,240)
        self.assertLessEqual(width('23:59',40)+8,190)
        self.assertLessEqual(width('WED 2088.12.31',14),236)
        self.assertNotIn('c::button',s) # clocks remain swipe-only full-screen

    def test_vault_theme_helper_has_no_sensitive_operations(self):
        s=read('main/apps/password_vault.cpp')
        helper=part(s,'void PasswordVault::styleCategory(', 'void PasswordVault::drawList()')
        self.assertNotRegex(helper,r'vault::|launch\(|Action::|startTyping|m_access|m_session')
        self.assertIn('lv_obj_set_size(row,119,index < 2 ? 79 : 78)',helper)
        self.assertEqual(s.count('styleCategory(row, name, detail, k);'),1)
        self.assertIn('m_categoryCounts[k]',s)
        self.assertIn('m_listIndex',s)

    def test_semantic_content_colors_are_not_recolored(self):
        random=read('main/apps/randomizer.cpp')
        self.assertIn('0xC52D44:0x2469B7',random)
        self.assertIn('selected?lv_color_white()',random)
        self.assertIn('lit?lv_color_white():lv_color_black()',read('main/apps/flashlight.cpp'))
        self.assertIn('0xC52D44',read('main/apps/flashlight.cpp'))
        self.assertIn('LV_COLOR_FORMAT_RGB565',read('main/apps/photo_viewer.cpp'))
        self.assertIn('LV_COLOR_FORMAT_RGB565',read('main/apps/fc_player.cpp'))
        self.assertIn('lv_color_white()',read('main/apps/dino.cpp'))
        self.assertFalse((ROOT/'main/apps/touch_scope.cpp').exists())

    def test_recording_layout_and_resource_lifetime_preserved(self):
        s=read('main/apps/recorder.cpp')
        for token in ('place(record,layout::Record)','place(stop,layout::Stop)','place(pause,layout::Pause)',
                      'place(forward,layout::Forward)','place(rewind,layout::Rewind)','job->peak.load()',
                      'job->requestStop()','listing->cancelled.store(true)','files.clear()'):
            self.assertIn(token,s)

    def test_focus_card_is_bounded_and_swipe_does_not_open_app(self):
        s=read('main/ui/focus_pager.hpp')
        for token in ('#include "theme.hpp"','LV_EVENT_PRESS_LOST','LV_EVENT_CLICKED && !p->dragged_',
                      'std::abs(dx)>28','std::abs(dx)>std::abs(dy)','47,36','144,36','70,70'):
            self.assertIn(token,s)
        self.assertNotRegex(s,r'lv_timer_create|lv_anim_start|malloc|make_unique')
        self.assertIn('focusApps.create(parent,216',read('main/ui/home_screen.cpp'))
        self.assertIn('focusMenu.create(v.body,240,7',read('main/apps/settings.cpp'))
        for count in (7,14):
            reached=set();index=0
            for _ in range(count):reached.add(index);index=(index+1)%count
            self.assertEqual(reached,set(range(count)))
            self.assertEqual(index,0)
            self.assertEqual(index-1 if index else count-1,count-1)
        for height in (216,240):
            self.assertLessEqual(24+(height-69),height-42)
            self.assertLessEqual(193+47,240)
            self.assertLessEqual(128+12,height-69-4)

    def test_focus_vault_preserves_actions_and_bounded_lists(self):
        helper=part(read('main/apps/password_vault.cpp'),'void PasswordVault::styleCategory(', 'void PasswordVault::drawList()')
        for token in ('LV_SCROLL_SNAP_CENTER','lv_obj_set_scroll_one','lv_obj_set_snappable',
                      'lv_obj_set_parent(row,viewport)','lv_obj_set_gesture_bubble','240,158',
                      'lv_obj_align(name,LV_ALIGN_TOP_LEFT,10,8)',
                      'lv_obj_align(detail,LV_ALIGN_TOP_LEFT,10,46)'):
            self.assertIn(token,helper)
        code=re.sub(r'//[^\n]*','',helper)
        self.assertNotRegex(code,r'lv_timer_create|lv_anim_start|entries|password|username')
        self.assertLessEqual(37+160,203)
        self.assertLessEqual(124+22+4,158)
        self.assertNotIn('m_categoryViewport',read('main/apps/password_vault.hpp'))

if __name__=='__main__':unittest.main(verbosity=2)
