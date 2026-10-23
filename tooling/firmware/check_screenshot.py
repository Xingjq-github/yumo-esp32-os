"""Screenshot wiring/lifetime contracts; native replay exercises pixels and PNG."""
from pathlib import Path
import unittest
from check_battery_power import body
from update_menu_font import read_font
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf8')
class ScreenshotContracts(unittest.TestCase):
    def test_key_double_only_and_existing_navigation_preserved(self):
        source=read('main/core/button_mgr.cpp')
        key=body(source,'static void keyCb(');pwr=body(source,'static void pwrCb(')
        self.assertIn('ev==BUTTON_DOUBLE_CLICK',key)
        self.assertIn('Screenshot::request()',key)
        self.assertIn('routeGameKey(ev,GameKeyState::Key)',key)
        self.assertIn('routePageKey(EventType::NextPage)',key)
        self.assertNotIn('Screenshot',pwr);self.assertNotIn('BUTTON_DOUBLE_CLICK',pwr)
        for behavior in ('s_viewExit.store','Power::toggleScreen()','Power::screenOn()','Power::powerOff()'):
            self.assertIn(behavior,pwr)
        driver=read('../dependencies/managed_components/espressif__button/iot_button.c')
        self.assertRegex(driver,r'if \(btn->repeat == 1\).*?')
        self.assertIn('else if (btn->repeat == 2)',driver)
    def test_button_requests_are_bounded_and_no_io_or_gui(self):
        source=read('main/core/screenshot.cpp');request=body(source,'void Screenshot::request(')
        self.assertIn('!active.load',request);self.assertIn('requested.store(true',request)
        for token in ('lv_','fopen','::open(','Session','xTaskCreate'):self.assertNotIn(token,request)
        self.assertIn('requested.exchange(false',source)
        self.assertIn('!active.load',body(source,'bool Screenshot::idle('))
        self.assertIn('!requested.load',body(source,'bool Screenshot::idle('))
    def test_sleep_and_card_admission_fences(self):
        source=read('main/core/screenshot.cpp')
        self.assertIn('job->access=core::sd::requestAccess();',source)
        self.assertNotIn('requestAccess(true)',source)
        self.assertIn('core::sd::Session card(j.access)',source)
        self.assertIn('core::sd::State::Available',source)
        power=body(read('main/core/power_mgr.cpp'),'void Power::processStandby(')
        self.assertLess(power.index('Screenshot::idle()'),power.index('bsp_sdcard_unmount()'))
        main=body(read('main/main.cpp'),'static void ui_pump_cb(')
        self.assertLess(main.index('Screenshot::poll()'),main.index('Power::acknowledgeSleepUi()'))
    def test_transaction_closes_file_before_publish_and_card_release(self):
        save=read('main/core/screenshot.cpp').split('void save(Job &j) {',1)[1].split('void worker(',1)[0]
        for token in ('O_EXCL','%Y-%m-%d_%H-%M-%S','%03ld_%02u.png','.part','fsync(fileno(file))'):
            self.assertIn(token,save)
        self.assertLess(save.index('fsync(fileno(file))'),save.index('std::fclose(file)'))
        self.assertLess(save.index('std::fclose(file)'),save.index('std::rename(temporary,path)'))
        self.assertLess(save.index('std::rename(temporary,path)'),save.index('j.result=Notice::Saved'))
        self.assertNotIn('remove(path)',save)
        worker=body(read('main/core/screenshot.cpp'),'void worker(')
        self.assertLess(worker.index('save(*j);'),worker.index('done.store'))
        self.assertIn('vTaskDeleteWithCaps',worker)
    def test_full_frame_pre_swap_and_worker_owns_no_live_lvgl(self):
        s=read('main/core/screenshot.cpp');capture=body(s,'void capture(')
        self.assertIn('LV_EVENT_FLUSH_START',s)
        self.assertIn('job->captured || job->invalid || job->writing',capture)
        self.assertIn('job->rowCount==YMCAST_HEIGHT',capture)
        self.assertIn('ymcast_copy_rect',capture)
        for sig in ('void worker(','void save('):
            self.assertNotIn('lv_',body(s,sig))
        self.assertIn('std::memset(pixels,0,YMCAST_BYTES)',s)
        codec=read('main/core/screenshot_png.c')
        for token in ('png_destroy_write_struct','192*1024','png_set_compression_level(png,1)',
                      'png_set_compression_window_bits(png,12)','memset(w,0,sizeof(*w))'):
            self.assertIn(token,codec)
    def test_album_accepts_screenshot_and_not_partial(self):
        accepted=read('main/core/storage_browser.cpp').split('bool accepted(',1)[1].split('void execute(',1)[0]
        self.assertIn('strcasecmp(ext,".png")',accepted)
        self.assertNotIn('.part',accepted)
        self.assertIn('"/sdcard"',read('main/apps/photo_viewer.hpp'))
        self.assertIn('"/sdcard/screenshots"',read('main/core/sd_media.hpp'))
        self.assertIn('".png"',read('main/core/photo_loader.cpp'))
    def test_notice_does_not_intercept_touch_and_glyphs_exist(self):
        s=body(read('main/main.cpp'),'static void screenshot_feedback(')
        self.assertIn('lv_layer_sys()',s)
        self.assertIn('lv_obj_set_clickable(screenshotNotice,false)',s)
        self.assertIn('clear_screenshot_notice()',s)
        font=read_font(read('main/ui/lv_font_cjk_16.c'))
        for char in '已保存失败卡不可用空间足内请重试：':self.assertIn(ord(char),font,char)
    def test_fc_gameplay_and_photo_view_disable_even_pending_capture(self):
        s=read('main/core/screenshot.cpp')
        for token in ('!viewBlocked.load','!Buttons::blockHomeShortcut()'):
            self.assertIn(token,body(s,'bool eligible('))
        for sig in ('void Screenshot::request(','Screenshot::Notice Screenshot::poll(','void capture('):
            self.assertIn('eligible()',body(s,sig))
        self.assertIn('requested.store(false',body(s,'void Screenshot::setViewBlocked('))
        photo=read('main/apps/photo_viewer.cpp')
        self.assertIn('setViewBlocked(true)',body(photo,'void PhotoViewer::openFile('))
        for sig in ('void PhotoViewer::showList(','void PhotoViewer::onHide('):
            self.assertIn('setViewBlocked(false)',body(photo,sig))
if __name__=='__main__':unittest.main(verbosity=2)
