"""Mac HID source contracts; actual queue/report cases compile in C++ separately.
No hardware or successful macOS shortcut execution is implied.
"""
from pathlib import Path
import unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(name): return (ROOT/name).read_text(encoding='utf-8-sig')
def part(s,a,b): return s.split(a,1)[1].split(b,1)[0]
class MacHidContracts(unittest.TestCase):
    def test_gui_sender_queues_without_delays_or_direct_reports(self):
        s=part(read('main/core/ble_hid.cpp'),'esp_err_t BleHid::sendKey(','void BleHid::cancelKeys')
        for t in ('s.queue.push(key)','s.running.exchange(true)','nimble_port_get_dflt_eventq()','Power::sleepPending()','s_privateStroke.busy.load()'):
            self.assertIn(t,s)
        for t in ('vTaskDelay','sendReport(','xTaskCreate'):self.assertNotIn(t,s)
    def test_keyboard_wire_descriptor_has_gui_bit_and_eight_byte_report(self):
        s=part(read('main/core/ble_hid.cpp'),'const uint8_t kReportMap[]','esp_hid_raw_report_map_t')
        for t in ('0x19, 0xE0','0x29, 0xE7','0x95, 0x08','0x95, 0x06','0x85, kKeyboardReportId'):self.assertIn(t,s)
        self.assertIn('std::array<uint8_t,8>',read('main/core/hid_key_sequence.hpp'))
    def test_host_owns_reports_and_callout_holds_each_phase(self):
        s=part(read('main/core/ble_hid.cpp'),'void keyStrokeEvent(','std::atomic<int64_t> s_connectDeadline')
        for t in ('s.sequence.report()','s.sequence.delayMs()','s.sequence.sent(err==ESP_OK)','ble_npl_time_ms_to_ticks32(delay)'):self.assertIn(t,s)
        for t in ('lv_','vTaskDelay','malloc','new '):self.assertNotIn(t,s)
    def test_pending_keys_are_bound_to_link_and_ui_epoch(self):
        s=part(read('main/core/ble_hid.cpp'),'void keyStrokeEvent(','std::atomic<int64_t> s_connectDeadline')
        for t in ('key.link!=s_linkGeneration.load()','key.epoch!=s.epoch.load()','!currentKeyLink()','s.current.epoch!=s.epoch.load()','s.sequence.cancel()'):self.assertIn(t,s)
        self.assertIn('void BleHid::cancelKeys() { ++s_keys.epoch; }',read('main/core/ble_hid.cpp'))
    def test_failures_drop_pending_presses_and_retry_only_all_up(self):
        s=part(read('main/core/ble_hid.cpp'),'void keyStrokeEvent(','std::atomic<int64_t> s_connectDeadline')
        for t in ('s.error.store(err);++s.epoch','Phase::Abort','abortKeyLink()','uint8_t zero[8]{}'):self.assertIn(t,s)
        model=read('main/core/hid_key_sequence.hpp')
        self.assertIn('MaxFailures=3',model);self.assertIn('Phase::Abort:Phase::Release',model)
    def test_failed_release_blocks_same_connection_even_if_terminate_fails(self):
        s=read('main/core/ble_hid.cpp')
        block=part(s,'void abortKeyLink()','void keyStrokeEvent')
        self.assertIn('s_keys.blockedLink.store(s_keys.current.link)',block)
        self.assertIn('if(currentKeyLink())',block)
        self.assertIn('s.blockedLink.load()==key.link',s)
        self.assertIn('s_keys.blockedLink.load()==link',s)
    def test_private_channel_is_exclusive_and_encrypted_bonded(self):
        s=read('main/core/ble_hid.cpp')
        private=part(s,'esp_err_t BleHid::sendPrivateKey(','esp_err_t BleHid::sendKey(')
        for t in ('std::unique_lock<std::mutex> channel(s_keySubmitMutex)','s_keys.running.load()','link!=privateLinkGeneration()','permit!=s_privatePermit.load()'):self.assertIn(t,private)
        self.assertLess(private.index('channel.unlock()'),private.index('while(!s.done.load()'))
        self.assertIn('s_privateEligible.load()',part(s,'void privateStrokeEvent(','// Ordinary keyboard output'))
        self.assertIn('s_privateEligible.store(desc.sec_state.encrypted && desc.sec_state.bonded && desc.sec_state.key_size==16)',s)
    def test_app_exit_mode_switch_and_screen_off_revoke_queue(self):
        s=read('main/apps/bt_keyboard_mouse.cpp')
        for a,b in [('void BtKeyboardMouse::onHide','void BtKeyboardMouse::onDestroy'),('void BtKeyboardMouse::showKeyboard','void BtKeyboardMouse::switchCb'),('void BtKeyboardMouse::selectTab','void BtKeyboardMouse::tabCb')]:
            self.assertIn('BleHid::cancelKeys()',part(s,a,b))
        self.assertIn('BleHid::cancelKeys()',part(s,'m_screenOffSub=','m_statusTimer=lv_timer_create'))
    def test_power_revokes_before_screen_change_and_waits_for_release(self):
        s=read('main/core/power_mgr.cpp')
        self.assertLess(s.index('if(!on) BleHid::cancelKeys()'),s.index('screen.store(on)'))
        self.assertLess(s.index('if(!BleHid::keysIdle())'),s.index('BleHid::prepareDeepSleep()'))
        self.assertIn('requestRecovery("keyboard release busy")',s)
    def test_errors_use_existing_rail_not_per_key_logs(self):
        self.assertIn('BleHid::takeKeyError()!=ESP_OK',read('main/apps/bt_keyboard_mouse.cpp'))
        s=part(read('main/core/ble_hid.cpp'),'// Ordinary keyboard output','std::atomic<int64_t> s_connectDeadline')
        self.assertNotIn('ESP_LOG',s);self.assertNotIn('BtKeyboardMouse',s)
    def test_compact_touch_grid_and_platform_hints_retained(self):
        s=read('main/apps/bt_keyboard_mouse.cpp')
        for t in ('(i%3)*80,(i/3)*49,79,48','lv_font_montserrat_10','m_tab==1?keys::shortcuts[i].macHint:keys::shortcuts[i].winHint','selectTab(0)'):self.assertIn(t,s)
    def test_queue_error_is_reported_instead_of_overwriting_strokes(self):
        s=part(read('main/core/ble_hid.cpp'),'esp_err_t BleHid::sendKey(','void BleHid::cancelKeys')
        self.assertIn('if(!s.queue.push(key)) { portEXIT_CRITICAL(&s_keyQueueLock);return ESP_ERR_NO_MEM; }',s)
        self.assertIn('s.queue.clear();s.running.store(false)',s)
if __name__=='__main__': unittest.main()
