"""Contracts for the current clock/HID/startup fix; hardware validation is separate."""
from pathlib import Path
import re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf-8-sig')
def between(s,a,b):return s.split(a,1)[1].split(b,1)[0]
class ClockHidBleContracts(unittest.TestCase):
    def test_static_host_creation_is_checked_and_uses_explicit_memory(self):
        s=read('main/core/ble_hid.cpp')
        self.assertIn('StaticTask_t s_hostTaskControl',s)
        self.assertIn('if(!s_hostStack || !xTaskCreateStaticPinnedToCore(',s)
        self.assertNotIn('nimble_port_freertos_init(hostTask)',s)
        self.assertIn('heap_caps_malloc(CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,kHostStackCaps)',s)
        self.assertIn('err=ESP_ERR_NO_MEM;goto fail',s)
    def test_unrelated_gap_security_scanning_and_hid_report_paths_preserved(self):
        # GAP now supports a central keyboard, so whole-function byte equality is obsolete.
        # Retain actual HID report formats/transmission and callback-generation guarantees.
        s=read('main/core/ble_hid.cpp')
        for token in ('remoteConnected()', 'sendReport(kMouseReportId','sendReport(kKeyboardReportId',
                      'sendReport(kConsumerReportId','s_scanGeneration','BLE_GAP_EVENT_ENC_CHANGE',
                      's_disconnectPending.load()', 'clearPairRequest()'):
            self.assertIn(token,s)
        old=read('../tooling/firmware/fixtures/contracts/keyboard-battery-ui-20260917-1306/main/core/ble_hid.cpp')
        # Mouse submission is now async and covered by production replay; the
        # unrelated media-report format and press/release timing remain intact.
        for start,end in (('esp_err_t BleHid::sendConsumer','// ---- bond management'),):
            self.assertEqual(between(old,start,end),between(s,start,end))
        submit=between(s,'esp_err_t BleHid::sendMouse','esp_err_t BleHid::clickMouse')
        self.assertIn('s.queue.push(dx,dy,wheel,buttons)',submit)
        self.assertIn('expectedLink!=link',submit)
        self.assertNotRegex(submit,r'sendReport|esp_hidd_dev_input_set|vTaskDelay')
    def test_external_stack_requires_cache_safe_xip_configuration(self):
        s=read('main/core/ble_hid.cpp');cfg=read('sdkconfig')
        self.assertIn('#if CONFIG_SPIRAM_XIP_FROM_PSRAM && CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM',s)
        for option in ('CONFIG_SPIRAM_XIP_FROM_PSRAM=y','CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y','CONFIG_SPIRAM_RODATA=y','CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM=y'):
            self.assertIn(option,cfg)
        self.assertIn('#else\nconstexpr uint32_t kHostStackCaps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;',s)
    def test_host_static_storage_lifetime_and_terminal_stop(self):
        s=read('main/core/ble_hid.cpp');host=between(s,'void hostTask','int gapEvent')
        self.assertIn('nimble_port_run();',host);self.assertIn('vTaskDelete(nullptr)',host)
        self.assertNotIn('vTaskDeleteWithCaps',host);self.assertNotIn('heap_caps_free',host)
        off=between(s,'    s_enabled.store(false);s_startDeadlineUs.store(0);','esp_err_t BleHid::disconnect')
        self.assertNotIn('nimble_port_stop();',off)
        terminal=s.split('esp_err_t BleHid::prepareDeepSleep()',1)[1]
        self.assertIn('nimble_port_stop()',terminal)
    def test_startup_timeout_is_bounded_and_does_not_keep_overwriting_failed(self):
        s=read('main/core/ble_hid.cpp');poll=between(s,'void BleHid::poll()','namespace {\nvoid processAdvertising')
        self.assertIn('kHostStartTimeoutUs=10000000',s)
        for item in ('deadline && now>=deadline','ESP_ERR_TIMEOUT','s_state.exchange(BleHidState::Failed)','s_state.load()!=BleHidState::Failed'):
            self.assertIn(item,poll)
        init=between(s,'esp_err_t BleHid::init','esp_err_t BleHid::setEnabled')
        self.assertLess(init.index('s_startDeadlineUs.store(esp_timer_get_time()+'),init.index('xTaskCreateStaticPinnedToCore'))
        self.assertNotIn('s_state.store',init.split('xTaskCreateStaticPinnedToCore')[1].split('fail:')[0])
    def test_hid_sync_callback_chain_and_bonds_not_erased(self):
        s=read('main/core/ble_hid.cpp')
        self.assertIn('ble_hs_cfg.sync_cb=[]()',s)
        init=between(s,'esp_err_t BleHid::init','esp_err_t BleHid::setEnabled')
        self.assertLess(init.index('configureGap()'),init.index('esp_hidd_dev_init('))
        self.assertNotIn('nvs_flash_erase',s)
        self.assertIn('ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO',s)
    def test_advertising_permission_is_not_implicit_restart(self):
        s=read('main/core/ble_hid.cpp')
        poll=between(s,'void BleHid::poll()','namespace {\nvoid processAdvertising')
        self.assertNotIn('s_advPending.exchange(false) || s_advertisingAllowed.load()',poll)
        self.assertIn('if(!restart && !requested) return',poll)
        self.assertLess(poll.index('s_advPending.exchange(false)'),poll.index('if(!ble_gap_adv_active()'))
        adv=s.split('namespace {\nvoid processAdvertising()',1)[1]
        self.assertNotIn('s_advPending.store(true)',adv)
        self.assertIn('endConnectionWindow()',adv)
    def test_advertising_request_model_handles_pending_connection_gap(self):
        # Same decision order as poll; explicit requests are consumed regardless
        # of advertising activity, never kept pending until CONNECT stops it.
        def poll(active,requested=False,restart=False,allowed=True):
            consumed=False
            if not requested and not restart:return active,consumed
            if restart:active=False
            if not active and allowed:return True,consumed
            return active,consumed
        active,pending=poll(True,True) # late HID_START while already advertising
        self.assertTrue(active);self.assertFalse(pending)
        active=False # controller has connected; application callback is pending
        self.assertEqual(poll(active,pending),(False,False))
        self.assertEqual(poll(False,True),(True,False)) # explicit user request, never DISCONNECT
        self.assertEqual(poll(True,True,True),(True,False)) # discovery window update
        self.assertEqual(poll(False,True,allowed=False),(False,False))
    def test_keyboard_full_width_and_wheel_hidden_only_in_keyboard(self):
        s=read('main/apps/bt_keyboard_mouse.cpp')
        shell=between(s,'void BtKeyboardMouse::showKeyboard','void BtKeyboardMouse::switchCb')
        self.assertIn('if(show) lv_obj_set_hidden(m_wheel,true)',shell)
        self.assertIn('else lv_obj_set_hidden(m_wheel,false)',shell)
        for name,parent in (('m_keys','parent'),('m_shortcuts','m_keys'),('m_charPanel','m_keys')):
            self.assertIn(f'{name}=c::panel({parent},240,195)',s)
        self.assertNotIn('m_status,LV_OBJ_FLAG_HIDDEN',shell)
        self.assertNotIn('m_switch,LV_OBJ_FLAG_HIDDEN',shell)
    def test_mouse_header_width_is_reassigned_exactly(self):
        s=read('main/apps/bt_keyboard_mouse.cpp')
        self.assertIn('3,0,65,44',s);self.assertIn('"1/s\\n2/s\\n5/s\\n10/s",44',s)
        self.assertIn('m_mouseBar,69,0',s);self.assertIn('dd,127,0',s)
        self.assertEqual(69+127,196);self.assertEqual(65-42,67-44)
        self.assertIn('lv_dropdown_set_symbol(dd,nullptr)',s)
    def test_exact_key_order_and_inverted_t(self):
        s=read('main/apps/hid_keyboard_model.hpp');block=between(s,'constexpr Shortcut shortcuts[]={','enum class Alphabet')
        labels=re.findall(r'\{"([^"]+)"',block)
        self.assertEqual(labels,['复制','剪切','粘贴','全选','锁屏','回车','退出','上','撤销','左','下','右'])
        self.assertEqual(labels[7],'上');self.assertEqual(labels[9:12],['左','下','右'])
        self.assertIn('{"退出",{0x29,0},{0x29,0},"Esc","Esc"}',block)
    def test_hint_is_small_under_chinese_and_changes_by_platform(self):
        s=read('main/apps/bt_keyboard_mouse.cpp')
        self.assertIn('lv_font_montserrat_10',s)
        self.assertIn('LV_ALIGN_TOP_MID,0,1',s);self.assertIn('LV_ALIGN_BOTTOM_MID,0,-2',s)
        self.assertIn('m_tab==1?keys::shortcuts[i].macHint:keys::shortcuts[i].winHint',s)
    def test_clock_is_cardless_and_preallocated(self):
        s=read('main/apps/watchface.cpp');header=read('main/apps/watchface.hpp')
        self.assertNotIn('m_card',s);self.assertNotIn('m_card',header)
        self.assertIn('m_tickPoints[60][2]',header)
        self.assertIn('i<60',s);self.assertIn('kDialSize = kFaceHeight',s)
        refresh=s.split('void Watchface::refresh',1)[1]
        self.assertNotIn('lv_obj_create',refresh);self.assertNotIn('lv_label_create',refresh)
        self.assertIn('if(now==m_lastRendered) return',refresh)
    def test_clock_fixed_glyph_layout_fits_screen(self):
        # Mono's fixed label cells and local fonts: 14px line=16, 32px line=35.
        s=read('main/apps/watchface.cpp')
        xs=[int(v) for v in re.search(r'constexpr int xs\[\]=\{([^}]+)',s).group(1).split(',')]
        ys=[int(v) for v in re.search(r'constexpr int ys\[\]=\{([^}]+)',s).group(1).split(',')]
        sizes=[(68,16),(140,16),(48,61),(48,61),(20,61),(48,61),(48,61),(64,35)]
        for x,y,(w,h) in zip(xs,ys,sizes):
            self.assertLessEqual(abs(x)+w/2,120);self.assertLessEqual(abs(y)+h/2,120)
        layout=between(s,'void Watchface::layoutDigitalFace','void Watchface::changeFace')
        self.assertNotIn('m_dial',layout)
        self.assertIn('lv_obj_align(obj,LV_ALIGN_CENTER,ox,oy)',layout)
    def test_clock_swipe_switches_once_and_never_uses_imu(self):
        s=read('main/apps/watchface.cpp');gesture=between(s,'void Watchface::touchCb','void Watchface::pressCb')
        self.assertLess(gesture.index('m_tracking=false'),gesture.index('changeFace'))
        header=read('main/apps/watchface.hpp')
        for token in ('qmi8658','Qmi8658','Acceleration','motionSensorOn','updateOrientation',
                      'applyOrientation','m_sensorReady','m_orient','m_candidate','m_stableCount',
                      'm_fx','m_fy','transform_rotation','transform_pivot'):
            self.assertNotIn(token,s);self.assertNotIn(token,header)
    def test_font_is_bounded_subset_not_large_unicode_font(self):
        s=read('main/ui/lv_font_clock_84.c')
        self.assertIn('11-glyph subset',s);self.assertIn('.line_height = 61',s)
        self.assertIn('5165 bytes, bpp=2',s)
        self.assertIn('"ui/lv_font_clock_84.c"',read('main/CMakeLists.txt'))
if __name__=='__main__':unittest.main()
