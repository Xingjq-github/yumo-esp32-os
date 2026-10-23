"""Lifecycle, state restoration and honest lightweight runtime telemetry."""
from pathlib import Path
import re,unittest
from check_battery_power import body
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf-8')
class GamesOverlay(unittest.TestCase):
    def test_runner_sprites_have_real_alpha_over_scenery(self):
        s=read('main/apps/dino_sprites.hpp')
        self.assertIn('LV_COLOR_FORMAT_RGB565A8',s)
        self.assertIn('CONFIG_LV_DRAW_SW_SUPPORT_RGB565A8=y',read('sdkconfig'))
        sizes={**{f'Rex{i}':(31,33) for i in range(4)},'Cactus0':(13,25),'Cactus1':(13,35),'Cactus2':(27,25)}
        for name,(w,h) in sizes.items():
            raw=re.search(name+r'Pixels\[\]=\{([^}]+)\}',s).group(1)
            pixels=bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',raw))
            self.assertEqual(len(pixels),w*h*3)
            alpha=pixels[w*h*2:]
            self.assertIn(0,alpha);self.assertIn(255,alpha)
            self.assertEqual(set(alpha),{0,255})
    def test_decorations_and_bonuses_are_fixed_pools(self):
        src=read('main/apps/dino.cpp');header=read('main/apps/dino.hpp')
        self.assertIn('*clouds[3]{},*mountains[3]{},*ridges[2]{},*stones[9]{},*bonuses[3]{}',header)
        for fn in ('void Dino::paint(', 'void Dino::moveScenery(', 'void Dino::tick('):
            self.assertNotRegex(body(src,fn),r'lv_\w+_create\(|\bnew\b')
        model=read('main/apps/dino_model.hpp')
        for name in ('cloud','mountain','stone'):self.assertNotIn(name,model)
        self.assertIn('spawnBonus(entropy)',model);self.assertIn('safeGap(speed)',model)
    def test_maze_primary_and_select_level_actions_differ(self):
        s=read('main/apps/gravity_maze.cpp')
        self.assertIn('button("切换关卡",Action::Next',s)
        self.assertNotIn('button("下一关",Action::Next',s)
        self.assertIn('hasPlayed=false',body(s,'void GravityMaze::buildBoard('))
        self.assertIn('hasPlayed=true',body(s,'void GravityMaze::play('))
        next_action=s.split('case Action::Next:',1)[1].split('break;',1)[0]
        self.assertIn('pause()',next_action);self.assertNotIn('play()',next_action)
    def test_compact_game_menu_text_has_baked_glyphs(self):
        from update_menu_font import read_font
        glyphs=read_font(read('main/ui/lv_font_menu_14.c'))
        maze=read('main/apps/gravity_maze.cpp')
        material=re.search(r'const char \*materialName\([^\n]*\)\{.*?\n\}',maze,re.S).group(0)
        vision=re.search(r'const char \*visionName\([^\n]*\)\{.*?\n\}',maze,re.S).group(0)
        # The dynamic material description explicitly uses the full CJK font;
        # every title/button that still uses the small font must remain baked.
        self.assertIn('lv_obj_set_style_text_font(hint,ui::theme::fontCjkFull(),0)',maze)
        for char in ''.join(re.findall(r'"([^"\n]*)"',material+vision)):
            if '\u4e00'<=char<='\u9fff':self.assertTrue(char.encode('gb2312'),char)
        text=''.join(re.findall(r'"([^"\n]*)"',maze.replace(material,'').replace(vision,'')+read('main/apps/dino.cpp')))
        for cp in set(map(ord,text)):
            if cp>127:self.assertTrue(cp in glyphs,chr(cp))
    def test_runtime_metrics_are_correctly_scoped(self):
        s=read('main/ui/sysinfo_overlay.cpp');tick=body(s,'void tick(')
        for token in ('MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT','MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',
                      'heap_caps_get_total_size','heap_caps_get_minimum_free_size',
                      'heap_caps_get_largest_free_block','esp_clk_cpu_freq()',
                      'activeName()', 'Connectivity::snapshot()', 'SystemService::bluetooth()', 'EventBus::dropped()'):
            self.assertIn(token,s)
        for wrong in ('esp_get_free_heap_size','CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ','esp_ota_get_running_partition','esp_flash_get_size'):
            self.assertNotIn(wrong,s)
        self.assertNotIn('psramFree > 0',s)
        for token in ('heap_caps_get_free_size','esp_clk_cpu_freq','Connectivity::snapshot'):
            self.assertLess(tick.index('if(!active) return'),tick.index(token))
        self.assertNotRegex(tick,r'fopen|nvs_|readAcceleration|startScan|uxTaskGetSystemState|esp_wifi_')
    def test_overlay_remains_bounded_and_does_not_capture_touches(self):
        s=read('main/ui/sysinfo_overlay.cpp')
        self.assertEqual(len(re.findall(r'=\s*makeLabel\(s_root',s)),9)
        self.assertIn('lv_obj_set_scrollable(s_root,false)',s)
        self.assertIn('lv_obj_set_clickable(s_root,false)',s)
        self.assertIn('lv_obj_set_style_max_width(lbl,236,0)',s)
        self.assertIn('compact::setText(label,buffer)',s)
        self.assertIn('lv_timer_create(tick, 1000, nullptr)',s)
    def test_semantic_button_background_updates_are_explicit(self):
        for path,token in (('main/ui/home_screen.cpp','compact::buttonBackground(clock'),
                           ('main/ui/focus_pager.hpp','compact::buttonBackground(enter'),
                           ('main/apps/randomizer.cpp','c::buttonBackground(m_pkButton'),
                           ('main/apps/password_vault.cpp','c::buttonBackground(super'),
                           ('main/apps/settings_wifi.cpp','c::buttonBackground(v.connect')):
            self.assertIn(token,read(path))
if __name__=='__main__':unittest.main(verbosity=2)
