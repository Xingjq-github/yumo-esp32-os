"""Connection UI/worker contracts, supplemented by native SDK and flow replay."""
from pathlib import Path
import unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf8')
def part(s,a,b):return s.split(a,1)[1].split(b,1)[0]
class ConnectionFlowContracts(unittest.TestCase):
    def test_every_manual_entry_uses_one_worker_flow(self):
        s=read('main/core/system_service.cpp')
        for op in ('Saved,cmd.bond','Keyboard,cmd.scanned.identity','Discover,{}'):
            self.assertIn('bluetoothFlow.begin(BluetoothOperation::'+op,s)
        self.assertIn('bluetoothFlow.poll(esp_timer_get_time(),Power::sleepPending())',s)
        for p in ('settings_bluetooth.cpp','password_vault.cpp','fc_player.cpp'):
            self.assertIn('SystemService::reconnectBond',read('main/apps/'+p))
    def test_saved_target_is_revalidated_before_and_after_teardown(self):
        s=read('main/core/ble_connection_flow.hpp')
        self.assertLess(s.index('if(!found) return ESP_ERR_NOT_FOUND'),s.index('BleHid::disconnect()'))
        poll=part(s,'void poll(', 'private:')
        self.assertLess(poll.index('!BleHid::connectionIdle()'),poll.index('BleHid::reconnectBond(target)'))
        self.assertIn('deadline_=now+10000000',s)
        self.assertIn('if(stopping || !BleHid::enabled()) {cancel();return;}',s)
        self.assertNotRegex(s,r'new |malloc|removeBond|removeAllBonds')
    def test_discovery_buttons_do_not_follow_short_link_phases(self):
        s=read('main/apps/settings_bluetooth.cpp')
        disable=next(line for line in s.splitlines() if 'c::disabled(v.discoverButton' in line)
        self.assertNotIn('Connecting',disable)
        self.assertIn('state.discovering()',s)
        self.assertIn('state.cancellable()?"取消连接"',s)
        vault=read('main/apps/password_vault.cpp')
        self.assertIn('b.discovering() ? "停止配对"',vault)
        self.assertIn('const bool canCancel = b.cancellable()',vault)
    def test_selected_host_rows_allow_switching_and_scope_errors(self):
        s=part(read('main/apps/settings_bluetooth.cpp'),'} else if(page==Page::BluetoothDevice)', '} else if(page==Page::BluetoothScan)')
        self.assertIn('sameBond(state.target,selectedBond)',s)
        self.assertIn('"切换至此设备"',s)
        disabled=next(line for line in s.splitlines() if 'c::disabled(v.connect' in line)
        self.assertNotIn('state.peer.connected',disabled)
        self.assertNotIn('state.state==sys::BleHidState::Connecting',disabled)
    def test_security_completion_is_shared_and_idempotent(self):
        s=read('main/core/ble_hid.cpp')
        for handle in ('event->connect.conn_handle','event->enc_change.conn_handle','s_securityHandle'):
            self.assertIn('completeSecureConnection('+handle+')',s)
        done=part(s,'bool completeSecureConnection(', 'void securityFallback(')
        self.assertIn('s_secureCompletedLink==s_linkGeneration.load()',done)
        self.assertLess(done.index('!matchesLinkExpected(peer)'),done.index('s_secureCompletedLink=s_linkGeneration.load()'))
        self.assertLess(done.index('s_secureCompletedLink=s_linkGeneration.load()'),done.index('BleKeyboardClient::start'))
    def test_controller_to_callback_gap_is_not_a_free_slot(self):
        s=read('main/core/ble_hid.cpp')
        idle=part(s,'bool BleHid::connectionIdle()', 'bool BleHid::keyboardConnected()')
        self.assertIn('!s_advAwaitingResult.load()',idle)
        self.assertIn('s_advAwaitingResult.store(true)',s)
        self.assertIn('reinterpret_cast<uintptr_t>(arg)!=s_advGeneration.load()',s)
        stop=part(s,'int stopAdvertising()', '// One report map')
        self.assertIn('if(rc==0) s_advAwaitingResult.store(false)',stop)
if __name__=='__main__':unittest.main(verbosity=2)
