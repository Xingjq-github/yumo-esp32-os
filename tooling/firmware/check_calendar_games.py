"""Current calendar/game UI, persistence and lifecycle source contracts.

Physics and lunar calculations are verified separately by compiled constexpr models.
"""
from pathlib import Path
import re,unittest
from check_battery_power import body
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf-8')

class CalendarGames(unittest.TestCase):
    def test_retired_apps_removed_and_settings_always_last(self):
        s=read('main/main.cpp');cmake=read('main/CMakeLists.txt')
        names=re.findall(r'registerApp\(\s*"([^"]+)"',s)
        self.assertEqual(names[-7:],['局域网','网速测速','FC游戏','小恐龙','重力迷宫','小智AI','设置'])
        self.assertEqual(len(names),25)
        for app in ('gravity_ball','touch_scope','deep_sleep'):
            for ext in ('cpp','hpp'):self.assertFalse((ROOT/f'main/apps/{app}.{ext}').exists())
            self.assertNotIn('apps/'+app+'.',s+cmake)
        self.assertIn('sys::ServiceAction::DeepSleep',read('main/apps/settings.cpp'))
        self.assertIn('sys::ServiceAction::DeepSleep',read('main/ui/quick_panel.cpp'))
    def test_no_new_app_gui_network_disk_or_hardware_io(self):
        for app in ('calendar','dino','gravity_maze'):
            s=read(f'main/apps/{app}.cpp')
            for token in ('nvs_', 'fopen(', 'esp_http_', 'i2c_', 'readAcceleration(', 'vTaskDelay', 'std::thread'):
                self.assertNotIn(token,s,(app,token))
    def test_all_new_app_timers_and_subscriptions_are_released(self):
        for app,cls in (('calendar','Calendar'),('dino','Dino'),('gravity_maze','GravityMaze')):
            s=read(f'main/apps/{app}.cpp')
            self.assertIn('lv_timer_pause(timer)',body(s,f'void {cls}::onHide('))
            teardown=body(s,f'void {cls}::onDestroy(')
            for t in ('onHide()', 'lv_timer_delete(timer)', 'EventBus::unsubscribe('):self.assertIn(t,teardown)
    def test_sensor_demand_is_scoped_and_sample_is_fresh(self):
        s=read('main/apps/gravity_maze.cpp')
        self.assertNotIn('Settings::setMotion',s)
        self.assertIn('claimTemporaryUse()',s);self.assertIn('releaseTemporaryUse(lease)',s)
        self.assertIn('releaseSensor()',body(s,'void GravityMaze::pause('))
        self.assertIn('releaseSensor()',body(s,'void GravityMaze::onHide('))
        self.assertIn('now-lastSample)>=250',s)
        self.assertIn('ScreenOff',s)
    def test_score_is_monotonic_and_writes_on_system_worker(self):
        s=read('main/core/settings_store.cpp')
        setter=body(s,'void Settings::recordDinoScore(')
        self.assertIn('score<=cache().dinoBest',setter)
        self.assertIn('mark(DinoScore)',setter)
        self.assertNotIn('nvs_',setter)
        self.assertIn('nvs_get_u32(h,"dino_best",&c.dinoBest)',s)
        self.assertIn('nvs_set_u32(h,"dino_best",snapshot.dinoBest)',s)
        game=read('main/apps/dino.cpp')
        self.assertIn('now-checkpoint>=5000',game)
        self.assertIn('saveScore()',body(game,'void Dino::onHide('))
        self.assertIn('saveScore()',body(game,'void Dino::pause('))
    def test_games_are_touch_operated_and_fullscreen(self):
        for app in ('dino','gravity_maze'):
            s=read(f'main/apps/{app}.cpp')
            self.assertIn('panel(parent,240,240)',s)
            self.assertIn('LV_EVENT_',s)
            self.assertIn('claimPageKeys()',s);self.assertIn('releasePageKeys(keySession)',s)
            self.assertNotIn('status_bar',s)
        self.assertIn('game.jump()',read('main/apps/dino.cpp'))
        self.assertNotIn('score',read('main/apps/gravity_maze.cpp'))
    def test_steady_frames_reuse_objects(self):
        s=read('main/apps/gravity_maze.cpp')
        self.assertNotIn('lv_obj_create',body(s,'void GravityMaze::tick('))
        self.assertNotIn('new ',body(s,'void GravityMaze::tick('))
        s=read('main/apps/dino.cpp')
        for sig in ('void Dino::paint(', 'void Dino::tick('):
            for t in ('lv_obj_create','lv_image_create','new '):self.assertNotIn(t,body(s,sig))
        self.assertIn('obstacleKinds[i]!=o.kind',s)
        self.assertIn('lv_timer_set_period(timer,80)',s)
    def test_calendar_all_cells_fit_and_compact_font_has_characters(self):
        for i in range(42):
            x,y=i%7*34+1,54+i//7*31
            self.assertLessEqual(x+33,240);self.assertLessEqual(y+30,240)
        from update_menu_font import read_font
        glyphs=read_font(read('main/ui/lv_font_menu_14.c'))
        words=re.findall(r'"([^"\n]+)"',read('main/core/calendar_model.hpp'))
        for cp in set(map(ord,''.join(words))):self.assertIn(cp,glyphs)
        self.assertIn('LV_EVENT_GESTURE',read('main/apps/calendar.cpp'))
    def test_about_reads_actual_hardware_and_build_status(self):
        s=read('main/apps/settings_system.cpp')
        sources=s+read('main/apps/settings_storage.cpp')+read('main/core/storage_browser.cpp')
        for call in ('esp_chip_info(', 'esp_flash_get_size(', 'esp_app_get_description()', 'esp_ota_get_running_partition()',
                     'heap_caps_get_info(', 'heap_caps_get_minimum_free_size(', 'esp_reset_reason()', 'Battery::snapshot()', 'esp_read_mac('):self.assertIn(call,sources)
        refresh=body(s,'void Settings::refreshAbout(')
        for t in ('fopen(', 'bsp_sdcard_mount(', 'esp_http_', 'readAcceleration('):self.assertNotIn(t,refresh)
        self.assertIn('无法自动检测插卡',s)
        self.assertIn('余量指固件可增长空间',sources)
    def test_upstream_licenses_kept(self):
        self.assertIn('Redistribution and use',read('main/third_party/chromium-LICENSE'))
        self.assertIn('MIT License',read('main/third_party/lunar-LICENSE'))
        self.assertIn('static RGB565',read('main/apps/dino_sprites.hpp'))
if __name__=='__main__':unittest.main(verbosity=2)
