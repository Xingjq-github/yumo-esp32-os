"""LAN source contracts and protocol/lifecycle specification models. No compiler/radio."""
from pathlib import Path
import re
import unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path): return (ROOT/path).read_text(encoding='utf-8')
def compact(text): return re.sub(r'\s+','',text)

def valid(name,password):
    if not name or len(name.encode('utf-8'))>32 or any(c in name for c in '\x00\r\n\t') or not name.strip(' '): return False
    return 8<=len(password.encode('utf-8'))<=63 and all(32<=ord(c)<=126 for c in password)

def packet(mac,bssid,seq=1,retry=False,qos=None,size=128):
    data=bytearray(size); fc=0x0108 | (0x0800 if retry else 0) | (0x0080 if qos is not None else 0)
    data[:2]=fc.to_bytes(2,'little'); data[4:10]=bssid; data[10:16]=mac; data[22:24]=seq.to_bytes(2,'little')
    if qos is not None:data[24]=qos
    return data

class MeterSpec:
    """Independent specification, not a substitute for running the C++ model."""
    def __init__(self,mac,bssid):self.mac=mac;self.bssid=bssid;self.sequences={};self.up=0;self.down=0
    def receive(self,data):
        if len(data)<28:return
        fc=int.from_bytes(data[:2],'little')
        if fc&15!=8 or fc&0x300!=0x100 or fc&0x40 or data[4:10]!=self.bssid or data[10:16]!=self.mac:return
        qos=bool(fc&0x80); header=24+(2 if qos else 0)+(4 if qos and fc&0x8000 else 0)
        if len(data)<header+4:return
        tid=data[24]&15 if qos else 16;seq=int.from_bytes(data[22:24],'little')
        if fc&0x800 and self.sequences.get(tid)==seq:return
        self.sequences[tid]=seq;self.up+=len(data)
    def send(self,data,success=True):
        if not success or len(data)<14:return
        target=data[:6]
        if target!=self.mac and len(data)>=24:
            fc=int.from_bytes(data[:2],'little')
            if fc&15==8 and fc&0x300==0x200 and not fc&0x40 and data[10:16]==self.bssid:target=data[4:10]
        if target==self.mac:self.down+=len(data)

class LeaseSpec:
    def __init__(self):self.generation=0;self.wanted=False;self.opened=False;self.phase='off';self.active=0
    def open(self):self.generation+=1;self.wanted=False;self.opened=True;return self.generation
    def close(self,generation):
        if generation==self.generation:self.wanted=False;self.opened=False
    def enable(self,generation):
        if generation!=self.generation or not self.opened or self.phase not in ('off','failed'):return False
        self.wanted=True;return True
    def tick(self,sta_stopped=False,ap_started=False,ap_stopped=False,fail=False):
        if self.phase in ('off','failed') and self.wanted:self.phase='preparing';self.active=self.generation
        if self.phase not in ('off','failed') and (not self.wanted or self.active!=self.generation):self.phase='stopping'
        if fail:self.wanted=False;self.phase='stopping'
        if self.phase=='preparing' and sta_stopped:self.phase='starting'
        if self.phase=='starting' and ap_started:self.phase='on'
        if self.phase=='stopping' and ap_stopped:self.phase='off'

class LanSourceContracts(unittest.TestCase):
    def test_registered_once_and_created_on_demand(self):
        main=read('main/main.cpp'); cmake=read('main/CMakeLists.txt')
        self.assertEqual(main.count('std::make_unique<apps::Lan>()'),1)
        for source in ('core/lan_hotspot.cpp','apps/lan.cpp'):self.assertEqual(cmake.count('"'+source+'"'),1)
        self.assertNotIn('LanHotspot::enable',main)
    def test_ap_only_four_peers_and_no_file_service(self):
        source=read('main/core/lan_hotspot.cpp'); model=read('main/core/lan_hotspot_model.hpp')
        self.assertIn('MaxClients=4',model); self.assertIn('wifi.ap.max_connection=lan::MaxClients',source)
        self.assertIn('esp_wifi_set_mode(borrowed?WIFI_MODE_APSTA:WIFI_MODE_AP)',source)
        self.assertIn('esp_netif_napt_enable(ap)',source)
        self.assertIn('if(!up.online)',source)
        for token in ('httpd_start','bsp_sdcard','esp_wifi_connect','xTaskCreate','lv_'):
            self.assertNotIn(token,source)
        self.assertIn('esp_http_server',read('main/CMakeLists.txt'))
        self.assertIn('lanweb::stop();',source)
        self.assertIn('if(!lanweb::idle())return false;',source)
    def test_wpa2_simple_defaults_and_ram_radio_storage(self):
        source=read('main/core/lan_hotspot.cpp'); model=read('main/core/lan_hotspot_model.hpp')
        self.assertIn('DefaultSsid[]="Yumo-LAN"',model); self.assertIn('DefaultPassword[]="12345678"',model)
        self.assertIn('wifi.ap.authmode=WIFI_AUTH_WPA2_PSK',source)
        self.assertIn('esp_wifi_set_storage(WIFI_STORAGE_RAM)',source)
        self.assertIn('wifi.ap.channel=6',source);self.assertIn('esp_wifi_set_bandwidth(WIFI_IF_AP,WIFI_BW20)',source)
        self.assertNotIn('WIFI_BW_HT20',source)
    def test_settings_only_persist_credentials_not_enabled_lease(self):
        source=read('main/core/settings_store.cpp')
        for key in ('lan_ssid','lan_pass'):self.assertIn('nvs_set_str(h,"'+key+'"',source)
        self.assertNotIn('lan_on',source)
        self.assertIn('lan::validate(lanName,lanPass)',source)
        self.assertNotIn('nvs_',read('main/apps/lan.cpp'))
    def test_sta_stop_fence_and_scan_resources_are_released(self):
        code=read('main/core/connectivity.cpp')
        source=code.split('esp_err_t Connectivity::acquireForHotspot()',1)[1]
        self.assertLess(source.index('hotspotOwner.exchange(true)'),source.index('setWifiEnabled(false)'))
        self.assertLess(source.index('if(awaitingStop)'),source.index('releaseStoppedLocked()'))
        cleanup=code.split('esp_err_t releaseStoppedLocked()',1)[1].split('void setState(',1)[0]
        self.assertLess(cleanup.index('if(awaitingStop'),cleanup.index('esp_wifi_deinit'))
        for token in ('esp_netif_destroy_default_wifi(station)','heap_caps_free(networks)','heap_caps_free(viewNetworks)','xQueueReset(events)'):
            self.assertIn(token,cleanup)
        self.assertIn('if(hotspotOwner.load()) return HotspotBusy',read('main/core/connectivity.cpp'))
        self.assertIn('hotspotShared=true;hotspotOwner=true;return ESP_OK',source)
        self.assertIn('if(hotspotShared)return ESP_ERR_NOT_FINISHED',cleanup)
    def test_sta_handoff_keeps_revisions_monotonic(self):
        source=read('main/core/connectivity.cpp').split('esp_err_t releaseStoppedLocked()',1)[1].split('void setState(',1)[0]
        self.assertIn('revision=state.revision+1,scanRevision=state.scanRevision+1',source)
        self.assertIn('state.revision=revision; state.scanRevision=scanRevision',source)
    def test_defaults_have_one_source_and_layout_is_shared(self):
        settings=read('main/core/settings_store.hpp')
        self.assertIn('lanSsid = lan::DefaultSsid',settings)
        self.assertIn('lanPassword = lan::DefaultPassword',settings)
        self.assertNotIn('InputHeight=',read('main/core/lan_hotspot_model.hpp'))
    def test_no_auto_restore_and_power_waits_for_cleanup(self):
        source=read('main/core/lan_hotspot.cpp')
        self.assertNotIn('setWifiEnabled(true)',source);self.assertNotIn('connectWifi(',source)
        power=read('main/core/power_mgr.cpp')
        self.assertLess(power.index('if(!LanHotspot::idle())'),power.index('bsp_sdcard_unmount'))
        self.assertIn('if(Power::sleepPending()) { request.wanted=false',source)
    def test_queue_independent_exit_is_generation_checked(self):
        source=read('main/core/lan_hotspot.cpp'); ui=read('main/apps/lan.cpp')
        close=source.split('void LanHotspot::close',1)[1].split('esp_err_t LanHotspot::enable',1)[0]
        self.assertIn('request.session!=session',close);self.assertIn('request.wanted=false',close)
        self.assertNotIn('xQueueSend',source)
        self.assertIn('sys::LanHotspot::close(session); session=0',ui)
        self.assertIn('lv_timer_delete(timer); timer=nullptr',ui)
        self.assertIn('intent.session!=activeSession',source)
    def test_default_ap_assert_helper_not_used(self):
        source=read('main/core/lan_hotspot.cpp')
        self.assertNotIn('esp_netif_create_default_wifi_ap(',source)
        self.assertIn('if(!ap) return ESP_ERR_NO_MEM',source)
        self.assertIn('if(err!=ESP_OK) return err',source)
        self.assertIn('if(startError!=ESP_OK) fail(startError)',source)
    def test_cleanup_fences_before_netif_destroy(self):
        source=read('main/core/lan_hotspot.cpp').split('bool cleanup()',1)[1].split('void sample(',1)[0]
        self.assertLess(source.index('if(radio && !stopped.load())'),source.index('esp_wifi_deinit()'))
        self.assertLess(source.index('esp_wifi_deinit()'),source.index('esp_netif_destroy_default_wifi(ap)'))
        self.assertLess(source.index('esp_netif_destroy_default_wifi(ap)'),source.index('Connectivity::releaseFromHotspot()'))
        self.assertIn('if(err!=ESP_OK && err!=ESP_ERR_WIFI_NOT_INIT)',source)
        self.assertLess(source.index('if(!fenceReached.load()) return false'),source.index('esp_netif_destroy_default_wifi(ap)'))
    def test_callbacks_only_keep_static_counters(self):
        source=read('main/core/lan_hotspot.cpp')
        callbacks=source.split('void receive(',1)[1].split('void stopMeter()',1)[0]
        for token in ('new ','malloc','printf','lv_','Settings::','xQueueSend','esp_wifi_'):self.assertNotIn(token,callbacks)
        self.assertIn('if(iface!=WIFI_IF_AP || !success',callbacks)
        self.assertIn('if(meterEnabled)',callbacks)
        self.assertIn('portENTER_CRITICAL(&meterMux)',callbacks)
    def test_stop_disables_both_observers(self):
        source=read('main/core/lan_hotspot.cpp')
        stop=source.split('void stopMeter()',1)[1].split('void startMeter()',1)[0]
        for token in ('meterEnabled=false','esp_wifi_set_promiscuous(false)','esp_wifi_set_promiscuous_rx_cb(nullptr)','esp_wifi_set_tx_done_cb(nullptr)'):self.assertIn(token,stop)
    def test_real_time_sampling_and_missing_statistics_are_honest(self):
        source=read('main/core/lan_hotspot.cpp'); ui=read('main/apps/lan.cpp')
        self.assertIn('now-lastSample',source);self.assertIn('esp_wifi_ap_get_sta_list(&list)',source)
        self.assertIn('esp_wifi_ap_get_sta_list_with_ip',source)
        self.assertIn('if(!available) std::snprintf(out,size,"--")',ui)
        self.assertIn('浏览器：SD文件',ui);self.assertIn('peer.ip[0]?peer.ip:',ui)
        self.assertNotIn('150 Mbps',ui)
    def test_shared_keyboard_and_wifi_height(self):
        ui=read('main/apps/lan.cpp')
        self.assertIn('c::networkField(row,title,value.c_str(),maxLength)',ui)
        self.assertIn('ui::TextInputLayout::NetworkFieldHeight',ui)
        self.assertIn('c::panel(parent,LV_PCT(100),height)',ui)
        self.assertIn('TextInput::bindField(obj)',read('main/ui/compact_ui.cpp'))
        self.assertNotIn('lv_keyboard_create',ui);self.assertNotIn('lv_dropdown_create',ui)
    def test_single_scroll_page_and_large_touch_controls(self):
        ui=read('main/apps/lan.cpp')
        for token in ('c::panel(parent,240,240,true)','LV_DIR_VER','LV_FLEX_FLOW_COLUMN','c::toggle(body,','c::disabled(ssid,fieldsLocked)','c::disabled(password,fieldsLocked)'):self.assertIn(token,ui)
        self.assertNotIn('WindowManager::openApp',ui)
        self.assertIn('if(sys::Power::isScreenOn()) self->refresh()',ui)
    def test_retained_configuration_and_new_source_presence(self):
        sdk=read('sdkconfig')
        self.assertIn('CONFIG_ESP_WIFI_SOFTAP_SUPPORT=y',sdk)
        self.assertIn('CONFIG_LWIP_IP_FORWARD=y',sdk)
        self.assertIn('CONFIG_LWIP_IPV4_NAPT=y',sdk)
        for source in ('main/core/lan_hotspot.hpp','main/core/lan_hotspot_model.hpp','main/apps/lan.hpp'):self.assertTrue((ROOT/source).is_file())

class LanConfigurationModels(unittest.TestCase):
    def test_defaults(self):self.assertTrue(valid('Yumo-LAN','12345678'))
    def test_ssid_utf8_byte_limit(self):
        self.assertTrue(valid('网'*10,'12345678'));self.assertFalse(valid('网'*11,'12345678'))
    def test_empty_controls_and_all_space_names(self):
        for name in ('','   ','x\x00y','x\ny','x\ty'):self.assertFalse(valid(name,'12345678'))
    def test_password_boundaries_and_symbols(self):
        self.assertFalse(valid('x','1234567'));self.assertTrue(valid('x','a'*63));self.assertFalse(valid('x','a'*64))
        self.assertTrue(valid('x','Ab!@#$%8'));self.assertFalse(valid('x','密码12345678'))

class LanTrafficModels(unittest.TestCase):
    def setUp(self):self.mac=bytes.fromhex('aabbccddeeff');self.bssid=bytes.fromhex('102030405060');self.m=MeterSpec(self.mac,self.bssid)
    def test_observed_uplink_counts_bytes(self):self.m.receive(packet(self.mac,self.bssid,size=160));self.assertEqual(self.m.up,160)
    def test_neighbour_and_unassociated_stations_ignored(self):
        self.m.receive(packet(self.mac,b'123456'));self.m.receive(packet(b'123456',self.bssid));self.assertEqual(self.m.up,0)
    def test_retry_deduplicated_per_tid(self):
        self.m.receive(packet(self.mac,self.bssid,qos=1));self.m.receive(packet(self.mac,self.bssid,qos=1,retry=True));self.m.receive(packet(self.mac,self.bssid,qos=2,retry=True));self.assertEqual(self.m.up,256)
    def test_null_and_truncated_frames_ignored(self):
        p=packet(self.mac,self.bssid);p[0]|=0x40;self.m.receive(p)
        self.m.receive(packet(self.mac,self.bssid)[:20]);self.assertEqual(self.m.up,0)
    def test_successful_ethernet_download_only(self):
        p=bytearray(100);p[:6]=self.mac;self.m.send(p,False);self.assertEqual(self.m.down,0)
        self.m.send(p);self.assertEqual(self.m.down,100)
    def test_fromds_download_and_bssid_filter(self):
        p=packet(self.mac,self.bssid);p[:2]=(0x0208).to_bytes(2,'little');p[4:10]=self.mac;p[10:16]=self.bssid
        self.m.send(p);self.assertEqual(self.m.down,128)
        p[10:16]=b'123456';self.m.send(p);self.assertEqual(self.m.down,128)
    def test_rate_uses_elapsed_time_not_fixed_timer_ticks(self):
        self.assertEqual(1500*1000000//1500000,1000)
        source=read('main/core/lan_hotspot_model.hpp');self.assertIn('if(elapsedUs<=0) return 0',source)
        self.assertIn('std::numeric_limits<uint64_t>::max()/1000000ULL',source)

class LanLifetimeModels(unittest.TestCase):
    def test_open_does_not_start_hotspot(self):
        l=LeaseSpec();l.open();l.tick();self.assertEqual(l.phase,'off')
    def test_close_before_enable_processing(self):
        l=LeaseSpec();g=l.open();l.enable(g);l.close(g);l.tick();self.assertEqual(l.phase,'off')
    def test_sta_fence_precedes_ap_start(self):
        l=LeaseSpec();g=l.open();l.enable(g);l.tick();self.assertEqual(l.phase,'preparing')
        l.tick(sta_stopped=True);self.assertEqual(l.phase,'starting')
        l.tick(ap_started=True);self.assertEqual(l.phase,'on')
    def test_exit_during_start_waits_for_stop(self):
        l=LeaseSpec();g=l.open();l.enable(g);l.tick(sta_stopped=True);l.close(g);l.tick(ap_started=True)
        self.assertEqual(l.phase,'stopping');l.tick(ap_stopped=True);self.assertEqual(l.phase,'off')
    def test_old_session_cannot_start_or_close_new_owner(self):
        l=LeaseSpec();a=l.open();b=l.open();l.close(a);self.assertTrue(l.opened);self.assertFalse(l.enable(a));self.assertTrue(l.enable(b))
    def test_failure_closes_and_never_auto_restarts(self):
        l=LeaseSpec();g=l.open();l.enable(g);l.tick(sta_stopped=True);l.tick(fail=True);l.tick(ap_stopped=True);l.tick()
        self.assertEqual(l.phase,'off');self.assertFalse(l.wanted)
    def test_reopen_waits_for_previous_driver_cleanup(self):
        l=LeaseSpec();a=l.open();l.enable(a);l.tick(sta_stopped=True,ap_started=True);b=l.open();l.tick()
        self.assertFalse(l.enable(b));self.assertEqual(l.phase,'stopping');l.tick(ap_stopped=True);self.assertTrue(l.enable(b))

if __name__=='__main__':unittest.main()
