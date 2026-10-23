"""SNTP lifecycle/UI integration contracts; transport behavior is in the native replay."""
from pathlib import Path
import unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path):return (ROOT/path).read_text(encoding='utf-8')

class NetworkTimeContracts(unittest.TestCase):
    def test_bounded_async_session_before_economy(self):
        s=read('main/core/system_service.cpp')
        self.assertLess(s.index('pollTimeSync(); // acquire'),s.index('Power::processEconomy();'))
        self.assertIn('SystemService::timeSyncBusy()',read('main/core/power_runtime.cpp'))
        self.assertIn('esp_netif_sntp_sync_wait(0)',s)
        self.assertIn('if(action==ServiceAction::SyncTime && error!=ESP_OK) timeSyncQueued.store(false);',s)
        self.assertIn('if(cmd.action==ServiceAction::SyncTime) timeSyncQueued.store(false);',s)
        self.assertLess(s.index('if(Power::sleepPending() && timeSyncActive.load())'),s.index('Power::processStandby();'))

    def test_real_completion_refreshes_editor_only_on_revision(self):
        s=read('main/apps/settings_system.cpp')
        self.assertIn('if(!force && timeRevision==state.revision) return;',s)
        self.assertIn('lv_dropdown_set_selected(v.date[i],values[i])',s)
        self.assertIn('if(page==Page::Time) refreshTime(force);',read('main/apps/settings.cpp'))
        self.assertIn('校时超时',s)
        self.assertNotIn('已请求网络校时',s)

    def test_manual_time_and_sntp_share_worker(self):
        s=read('main/apps/settings_system.cpp')
        self.assertNotIn('settimeofday(',s)
        self.assertIn('sys::SystemService::setClock(epoch)',s)
        service=read('main/core/system_service.cpp')
        self.assertIn('case ServiceAction::SetTime: return applyClock(cmd.epoch);',service)
        self.assertIn('timeSyncActive.load() || timeSyncQueued.exchange(true)',service)

if __name__=='__main__':unittest.main()
