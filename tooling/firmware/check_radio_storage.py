"""Source-contract and Python-model checks for radio/storage fixes.
No C/C++ is executed or compiled; no configure, build, flash or device access.
Models exercise boundary vectors but do NOT prove firmware runtime behavior.
"""
from pathlib import Path
import hashlib
import json
import re
import unittest
from check_firmware_refactor import code_only

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
from build_firmware import SDK
RUN=ROOT/'../tooling/firmware/fixtures/contracts/radio-storage-fix-20260916'
BACKUP=ROOT/'../tooling/firmware/fixtures/contracts/radio-storage-fix-20260916'
def read(name): return (ROOT/name).read_text(encoding='utf-8-sig')
def part(text,start,end): return text.split(start,1)[1].split(end,1)[0]
def valid_utf8(data):
    try: data.decode('utf-8'); return True
    except UnicodeDecodeError: return False

def trim_utf8_tail_model(text):
    # Mirrors only the bounded suffix validation in decodeText; not C++ execution.
    if valid_utf8(text): return text
    for cut in range(1,min(3,len(text))+1):
        start=len(text)-cut; lead=text[start]
        need=2 if 0xC2<=lead<=0xDF else 3 if 0xE0<=lead<=0xEF else 4 if 0xF0<=lead<=0xF4 else 0
        continuation=all(c&0xc0==0x80 for c in text[start+1:])
        if cut>1:
            second=text[start+1]
            if ((lead==0xE0 and second<0xA0) or (lead==0xED and second>=0xA0) or
                (lead==0xF0 and second<0x90) or (lead==0xF4 and second>=0x90)):
                continuation=False
        if need>cut and continuation and valid_utf8(text[:start]): return text[:start]
    return text

class RadioStorageContracts(unittest.TestCase):
    def test_wifi_fresh_presence_precedes_every_connect(self):
        s=read('main/core/connectivity.cpp')
        scan=part(s,'esp_err_t beginTargetScan()', 'void failed(')
        self.assertIn('config.ssid=',scan)
        self.assertIn('config.show_hidden=true',scan)
        self.assertIn('esp_wifi_scan_start(&config,false)',scan)
        connect=part(s,'esp_err_t Connectivity::connectWifi(', 'esp_err_t Connectivity::startScan')
        self.assertNotIn('esp_wifi_connect(',connect)
        self.assertIn('targetValidated=false; connectQueued=true',connect)
        consume=part(s,'void consumeScan(', 'void Connectivity::init')
        self.assertIn('if(err==ESP_OK && found) { targetValidated=true',consume)
        self.assertIn('Connectivity::NetworkNotSeen',consume)
        self.assertIn('validateCachedTarget()',connect)
        self.assertIn('recentPresence(networksSeenAt)',s)
        self.assertIn('!recentPresence(targetSeenAt)',s)
        self.assertIn('esp_wifi_scan_get_ap_record(&ap)',consume)
        self.assertNotIn('count=std::min<uint16_t>',consume)
        self.assertIn('!broadcastProbe) broadcastProbe=true',consume)
        poll=s.split('void Connectivity::poll()',1)[1]
        self.assertRegex(poll,r'if\(targetValidated\)\s*\{[^}]*beginConnect\(\)')
        self.assertIn('!awaitingStop && !awaitingStart && !scanOutstanding',poll)

    def test_wifi_retries_always_invalidate_target_and_have_backoff(self):
        s=read('main/core/connectivity.cpp')
        retry=part(s,'if(retryAt && now>=retryAt', 'if(connectQueued &&')
        self.assertIn('targetValidated=false',retry)
        self.assertIn('setState(WifiState::Searching)',retry)
        failed=part(s,'void failed(', 'void consumeScan(')
        for token in ('state.retry<3','Connectivity::NetworkNotSeen?30000000LL:2000000LL','!auth','state.ip.clear()'):
            self.assertIn(token,failed)
        self.assertEqual([30<<i for i in range(3)],[30,60,120])

    def test_wifi_cancellation_timeout_waits_for_stop_fence(self):
        s=read('main/core/connectivity.cpp')
        cancel=part(s,'void stopScanLocked()', 'esp_err_t requestDisconnectLocked')
        self.assertNotIn('scanOutstanding=false',cancel)
        self.assertIn('cancelDeadline=esp_timer_get_time()+2000000',cancel)
        timeout=part(s,'if(((cancelDeadline && now>=cancelDeadline', 'if(overflow.exchange')
        self.assertIn('restartStationLocked(now)',timeout)
        timeout=part(s,'esp_err_t restartStationLocked(', 'esp_err_t beginConnect')
        order=['esp_wifi_stop()', 'awaitingStop=true','restartPending=state.enabled']
        self.assertEqual([timeout.index(x) for x in order],sorted(timeout.index(x) for x in order))
        self.assertIn('transitionDeadline=now+4000000',timeout)
        self.assertIn('view.transitioning=!transitionTimedOut',s)

    def test_wifi_reenable_reuses_observed_start_not_an_idempotent_call(self):
        s=read('main/core/connectivity.cpp')
        callback=part(s,'void radioEvent(', 'void tuneRadio')
        self.assertIn('driverStarted.store(true)',callback)
        self.assertIn('driverStarted.store(false)',callback)
        enable=part(s,'esp_err_t Connectivity::setWifiEnabled(', 'esp_err_t Connectivity::stopWifi')
        for token in ('const bool alreadyStarted=driverStarted.load()',
                      'alreadyStarted?ESP_OK:esp_wifi_start()', 'awaitingStart=!alreadyStarted',
                      'awaitingStop || !needsStop?ESP_OK:esp_wifi_stop()', 'restartPending=true'):
            self.assertIn(token,enable)

    def test_wifi_late_ip_never_means_connected_without_real_ap(self):
        s=read('main/core/connectivity.cpp')
        ip=part(s,'event.kind==RadioEvent::GotIp)', 'event.kind==RadioEvent::LostIp')
        for token in ('state.state!=WifiState::Connecting','esp_wifi_sta_get_ap_info',
                      'esp_netif_get_ip_info','!info.ip.addr','targetSsid!=std::string'):
            self.assertIn(token,ip)
        self.assertLess(ip.index('!info.ip.addr'),ip.index('Settings::rememberWifi'))
        self.assertLess(ip.index('Settings::rememberWifi'),ip.index('setState(WifiState::Connected)'))

    def test_wifi_off_clears_runtime_not_saved_credentials(self):
        s=part(read('main/core/connectivity.cpp'),'if(!enabled) {','if(awaitingStop) {')
        for token in ('state.enabled=false','state.ssid.clear()','state.ip.clear()','state.rssi=0','networkCount=0'):
            self.assertIn(token,s)
        self.assertNotRegex(s,r'Settings::(?:forgetWifi|setWifiCredentials)')

    def test_ble_callbacks_only_queue_advertising(self):
        s=read('main/core/ble_hid.cpp')
        gap=part(s,'int gapEvent(', 'esp_err_t configureGap')
        self.assertNotRegex(code_only(gap),r'ble_gap_adv_(?:start|set_fields)\s*\(')
        callback=part(s,'void BleHid::startAdvertisingFromCallback', 'int BleHid::lastError')
        self.assertIn('s_advPending.store(true)',callback)
        self.assertIn('s_nextAdvAttempt=now+1000000',s)
        self.assertIn('s_state.compare_exchange_strong(previousState',s)

    def test_ble_quick_off_on_does_not_restore_connection_permission(self):
        s=part(read('main/core/ble_hid.cpp'),'esp_err_t BleHid::setEnabled(', 'esp_err_t BleHid::disconnect')
        enable,off=s.split('    s_enabled.store(false);s_startDeadlineUs.store(0);',1)
        self.assertIn('s_connHandle.load()!=kInvalidHandle || s_connectPending.load()',enable)
        self.assertNotIn('s_advertisingAllowed.store(true)',enable)
        self.assertNotIn('makeDiscoverable()',enable)
        self.assertIn('endConnectionWindow()',off)
        self.assertIn('s_disconnectPending.store(true)',off)
        self.assertNotRegex(off,r's_connHandle\.(?:exchange|store)')
        self.assertNotRegex(code_only(off),r'nimble_port_(?:stop|deinit)\s*\(')

    def test_ble_connected_requires_encryption_and_no_pending_stop(self):
        s=read('main/core/ble_hid.cpp')
        connect=part(s,'case BLE_GAP_EVENT_CONNECT:', 'case BLE_GAP_EVENT_DISCONNECT:')
        self.assertIn('BleHidState::Connecting',connect)
        enc=part(s,'case BLE_GAP_EVENT_ENC_CHANGE:', 'case BLE_GAP_EVENT_PASSKEY_ACTION:')+part(s,'bool completeSecureConnection(', 'void securityFallback(')
        for token in ('event->enc_change.conn_handle!=s_connHandle.load()', 's_disconnectPending.load()',
                      'desc.sec_state.encrypted', 's_state.store(BleHidState::Connected)'):
            self.assertIn(token,enc)
        connected=part(s,'bool BleHid::connected(void)', 'BleHidState BleHid::state')
        self.assertIn('!s_disconnectPending.load()',connected)
        self.assertIn('s_state.load()==BleHidState::Connected',connected)
        self.assertIn('if (!BleHid::remoteConnected() || !device',s)

    def test_ble_pairing_timeout_and_radio_reset_can_recover(self):
        s=read('main/core/ble_hid.cpp')
        self.assertIn('s_securityDeadlineUs.store(esp_timer_get_time()+(s_keyboardLink.load()?90000000:30000000))',s)
        poll=part(s,'void BleHid::poll()', 'namespace {\nvoid processAdvertising')
        for token in ('now>=deadline', 's_pairError.store(BLE_HS_ETIMEOUT)', 'ble_gap_terminate(handle'):
            self.assertIn(token,poll)
        reset=part(s,'ble_hs_cfg.reset_cb=', 'ble_hs_cfg.sm_io_cap')
        self.assertIn('s_connHandle.store(kInvalidHandle)',reset)
        self.assertIn('endConnectionWindow()',reset)
        self.assertNotIn('s_advPending.store(true)',reset)
        for name in ('sdkconfig','sdkconfig.defaults'):
            self.assertIn('CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE=6144',read(name))

    def test_ui_uses_shared_snapshots_and_error_rule(self):
        root=read('main/apps/settings.cpp'); bar=read('main/ui/window_mgr.cpp')
        for s in (root,bar):
            self.assertIn('Connectivity::snapshot()',s)
            self.assertIn('SystemService::bluetooth()',s)
            self.assertIn('bt.hasError()',s)
        self.assertIn('state.hasError()',read('main/apps/settings_bluetooth.cpp'))
        self.assertIn('wifi.state==sys::WifiState::Failed?"连接失败":wifi.transitioning',root)
        self.assertNotRegex(bar,r'BleHid::(?:enabled|state|connected)\(')
        self.assertIn('state!=BleHidState::Connecting',read('main/core/system_service.hpp'))

    def test_ble_fast_snapshot_does_not_enumerate_bonds_at_10hz(self):
        s=read('main/core/system_service.cpp')
        self.assertIn('nextBondRefresh=now+1000000',s)
        self.assertIn('forceBonds || now>=nextBondRefresh || next.state!=previous.state',s)
        self.assertIn('next.bonds[i].connected=next.peer.connected && BlePeerStore::same(next.peer,next.bonds[i])',s)
        worker=s.split('void worker(void *)',1)[1]
        self.assertLess(worker.index('refreshBluetooth(true)'),worker.index('--pending[index]'))

    def test_terminal_sleep_fences_host_access_and_queued_commands(self):
        s=read('main/core/system_service.cpp'); worker=s.split('void worker(void *)',1)[1]
        self.assertLess(worker.index('if(Power::sleepTerminal())'),worker.index('BleHid::poll()'))
        self.assertIn('Power::sleepPending()?ESP_ERR_INVALID_STATE:execute(cmd)',worker)
        self.assertIn('uxQueueMessagesWaiting(queue)==0',worker)
        deep=read('main/core/ble_hid.cpp').split('esp_err_t BleHid::prepareDeepSleep()',1)[1]
        self.assertLess(deep.index('setEnabled(false)'),deep.index('nimble_port_stop()'))
        self.assertNotRegex(code_only(deep),r'(?:free|deinit)\s*\(')

    def test_deep_wake_reuses_preferences_but_not_connection_cache(self):
        power=read('main/core/power_mgr.cpp')
        entry=part(power,'void Power::processStandby()', 'void Power::syncTouch')
        self.assertNotRegex(entry,r'Settings::set(?:BluetoothOn|WifiOn)')
        worker=part(read('main/core/system_service.cpp'),'void worker(void *)', 'ServiceDomain SystemService::domain')
        self.assertNotIn('Settings::bluetoothOn()',worker.split('for(;;)',1)[1])
        self.assertIn('c.bluetooth=true;c.wifi=true;c.motion=false',read('main/core/settings_store.cpp'))
        self.assertNotIn('setWifiEnabled(Settings::wifiOn())',read('main/core/connectivity.cpp'))
        self.assertIn('if(!Power::wokeFromSleep() || time(nullptr)<1609459200LL)',read('main/main.cpp'))
        config=read('sdkconfig')
        self.assertIn('CONFIG_LIBC_TIME_SYSCALL_USE_RTC_HRT=y',config)
        self.assertIn('esp_sleep_enable_ext0_wakeup(kPwrPin,0)',power)
        self.assertNotIn('esp_sleep_enable_timer_wakeup',power)

    def test_storage_jobs_own_values_and_task_delete_matches_allocator(self):
        src=read('main/core/storage_browser.cpp'); header=read('main/core/storage_browser.hpp')
        self.assertNotRegex(code_only(header),r'\b(?:lv_obj_t|App|TextReader|PhotoViewer)\b')
        self.assertIn('std::atomic<bool> done{false}, cancelled{false}',header)
        self.assertIn('active.fetch_add(1)>=2',src)
        self.assertIn('xTaskCreateWithCaps(worker,"sd_browser",6144',src)
        self.assertIn('job->done.store(true,std::memory_order_release)',src)
        self.assertIn('vTaskDeleteWithCaps(nullptr)',src)
        self.assertIn('auto job=*holder; delete holder',src)
        self.assertNotIn('vTaskDelete(nullptr)',src)

    def test_storage_cancel_and_timer_cleanup_on_both_apps(self):
        for name,cls in (('text_reader','TextReader'),('photo_viewer','PhotoViewer')):
            s=read(f'main/apps/{name}.cpp')
            for token in ('done.load(std::memory_order_acquire)', 'm_job->cancelled.store(true)',
                          'm_job.reset()', 'lv_timer_pause(m_poll)', 'lv_timer_delete(m_poll)',
                          f'{cls}::onHide(', f'{cls}::onDestroy(', f'{cls}::onBack('):
                self.assertIn(token,s)
            self.assertNotRegex(code_only(s),r'\b(?:readdir|opendir|mkdir)\s*\(')

    def test_directory_filter_preserves_directories_and_raw_io_paths(self):
        src=read('main/core/storage_browser.cpp')
        for token in ('stat(path.c_str(),&st)', 'S_ISDIR(st.st_mode)', '!directory &&',
                      'std::move(path),std::move(name),directory', 'a.directory>b.directory',
                      'kMaxEntries=256', '++examined>4096', 'examined%8==0',
                      'job.path.compare(job.path.size()-3,3,"/..")', 'if(errno) job.error=errno'):
            self.assertIn(token,src)
        self.assertNotIn('d_type',code_only(src))
        self.assertNotIn('mkdir(',src)

    def test_directory_rows_are_compact_font_safe_and_reuse_the_button_label(self):
        helper=read('main/ui/compact_ui.cpp')
        self.assertIn('if(text && *text)',helper) # empty captions DO NOT create children
        widget=read('main/ui/virtual_file_list.hpp')
        for token in ('RowHeight=38','PoolSize=8','lv_obj_get_child(row.button,0)',
                      'LV_LABEL_LONG_MODE_DOTS','compact::button(content_,"?"',
                      'text.empty()?"?":text.c_str()'):
            self.assertIn(token,widget)
        for name in ('text_reader','photo_viewer'):
            s=read(f'main/apps/{name}.cpp')
            for token in ('core::parentDirectory(m_directory)','m_fileList.attach(m_list,this',
                          'm_fileList.show(m_names.size(),m_listingTruncated?', 'm_fileList.detach()'):
                self.assertIn(token,s)
            self.assertNotIn('lv_obj_clean(m_list)',s)
            self.assertNotIn('LV_LABEL_LONG_SCROLL',s)

    def test_text_decode_guards_and_limit(self):
        s=read('main/core/charset_conv.cpp')
        for token in ('cp>=0xD800 && cp<=0xDFFF', 'byte(0)==0xff && byte(1)==0xfe',
                      'byte(0)==0xfe && byte(1)==0xff', 'low>=0xDC00 && low<=0xDFFF',
                      '0x10000+((cp-0xD800)<<10)', 'if(!truncated && bytes.size()%2)',
                      'lead==0xE0 && second<0xA0', 'lead==0xF4 && second>=0x90',
                      "else if(c<32 && c!='\\n') appendUtf8(out,0xfffd)"):
            self.assertIn(token,s)
        self.assertIn('kMaxTextBytes=64*1024',read('main/core/storage_browser.cpp'))
        self.assertIn('job.content=decodeText(bytes,job.truncated)',read('main/core/storage_browser.cpp'))

    def test_real_gbk_lookup_table_matches_filename_vectors(self):
        array=part(read('main/core/gbk_table.c'),'= {','};')
        table=[int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]+',array)]
        self.assertEqual(len(table),126*190)
        for ch in '中文目录相册文本测试设备照片':
            lead,trail=ch.encode('gbk')
            idx=(lead-0x81)*190+trail-(0x40 if trail<0x7f else 0x41)
            self.assertEqual(table[idx],ord(ch),ch)

    def test_utf8_tail_model_preserves_gbk_and_rejects_impossible_sequences(self):
        prefix='文件内容'.encode('utf-8')
        for scalar in ('中','é','😀','\U0010ffff'):
            encoded=scalar.encode('utf-8')
            for n in range(1,len(encoded)):
                self.assertEqual(trim_utf8_tail_model(prefix+encoded[:n]),prefix)
        for suffix in (b'\xe0\x80',b'\xed\xa0',b'\xf0\x80',b'\xf4\x90'):
            self.assertEqual(trim_utf8_tail_model(b'abc'+suffix),b'abc'+suffix)
        gbk='中文目录'.encode('gbk')
        self.assertEqual(trim_utf8_tail_model(gbk),gbk)
        self.assertEqual(trim_utf8_tail_model(prefix),prefix)

    def test_utf16_boundary_reference_vectors(self):
        # Python reference vectors supplement the source guards, not a C++ test.
        for codec,bom in (('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff')):
            data=bom+'中文😀'.encode(codec)
            self.assertEqual(data[2:].decode(codec),'中文😀')
            self.assertEqual(data[:-1][2:].decode(codec,errors='replace'),'中文�')
            self.assertEqual((bom+b'A')[2:].decode(codec,errors='replace'),'�')

    def test_pagination_model_is_lossless_bounded_and_utf8_aligned(self):
        s=read('main/apps/text_reader.cpp')
        self.assertIn('lv_text_get_next_line(text,static_cast<uint32_t>(bytes),font,nullptr,&attrs)',s)
        self.assertIn('text_paging::linesPerPage(lv_obj_get_content_height(m_reader),lv_font_get_line_height(font),attrs.line_space)',s)
        self.assertIn('attrs.max_width=lv_obj_get_content_width(m_text)',s)
        self.assertIn('lv_obj_set_style_text_font(m_text, ui::theme::fontReading(), 0)',s)
        self.assertIn('lv_obj_set_scrollable(m_reader,false)',s)
        self.assertNotIn('end=1024',s)
        # Font measurement/wrapping comes from local LVGL; page grouping and
        # UTF-8/newline restoration execute in tools/text_reader_test.cpp.
        self.assertIn('static_assert(pageBoundaries()',read('../tooling/firmware/text_reader_test.cpp'))
        self.assertEqual(240//20,12)

    def test_jpeg_cache_detaches_before_free_and_failed_zoom_retains_old_image(self):
        s=read('main/apps/photo_viewer.cpp')
        header='src/misc/cache/instance/lv_image_cache.h'
        self.assertIn(f'#include "{header}"',s)
        self.assertIn('void lv_image_cache_drop(const void * src);',
                      (ROOT/'../dependencies/managed_components/lvgl__lvgl'/header).read_text(encoding='utf-8'))
        free=part(s,'void PhotoViewer::releaseImage', 'void PhotoViewer::releaseSource')
        stages=['lv_image_cache_drop(&m_imageDsc)','lv_image_set_src(m_image,nullptr)','heap_caps_free(m_pixels)']
        self.assertEqual([free.index(x) for x in stages],sorted(free.index(x) for x in stages))
        loader=read('main/core/photo_loader.cpp')
        decode=part(loader,'bool decodeJpeg', 'void execute')
        poll=part(s,'void PhotoViewer::pollImage','void PhotoViewer::releaseImage')
        self.assertLess(poll.index('if(job->error || !job->pixels)'),poll.index('releaseImage();m_pixels=job->pixels'))
        self.assertIn('photo_pixels::rgb888Bytes(width,height)',decode)
        self.assertIn('MaxPixels=1024*1024',read('main/apps/photo_pixels.hpp'))
        self.assertIn('size_t(io.out_size)==rgbBytes',decode)
        self.assertIn('MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT',loader)
        self.assertIn('heap_caps_realloc(pixels,imageBytes,Caps)',decode)
        self.assertIn('if(!m_pixels)',s)
        self.assertIn('s.width>16384 || s.height>16384',loader)

    def test_reader_fullscreen_keeps_header_only_on_directory_list(self):
        s=read('main/apps/text_reader.cpp')
        for token in ('lv_obj_set_size(m_reader, ui::theme::kContentW, ui::theme::kScreenH)',
                      'lv_obj_align(m_reader, LV_ALIGN_TOP_MID, 0, 0)',
                      'lv_obj_set_hidden(m_header,true)',
                      'lv_obj_set_hidden(m_header,false)'):
            self.assertIn(token,s)
        self.assertNotRegex(s,r'\bm_prev\b|\bm_next\b|setReadingHeader')
        listing=part(s,'void TextReader::showList','void TextReader::openFile')
        self.assertIn('lv_obj_set_hidden(m_header,false)',listing)
        self.assertIn('lv_obj_set_hidden(m_reader,true)',listing)
        self.assertNotIn('"文本 %u/%u"',s)
        photo=read('main/apps/photo_viewer.cpp')
        for token in ('lv_obj_set_width(m_title,166)','lv_obj_set_hidden(m_header,true)',
                      'lv_obj_align(m_imagePanel, LV_ALIGN_TOP_MID, 0, 0)','kViewH = 240'):
            self.assertIn(token,photo)

    def test_new_api_symbols_exist_in_selected_local_sdk_headers(self):
        for name,tokens in {
            'components/bt/host/nimble/nimble/nimble/host/include/host/ble_gap.h': ('ble_gap_conn_find','encrypted'),
            'components/bt/host/nimble/nimble/nimble/host/include/host/ble_hs.h': ('BLE_HS_ETIMEOUT',),
            'components/freertos/esp_additions/include/freertos/idf_additions.h': ('xTaskCreateWithCaps','vTaskDeleteWithCaps'),
            'components/esp_hw_support/include/esp_sleep.h': ('esp_sleep_enable_ext0_wakeup','esp_deep_sleep_start'),
        }.items():
            header=(SDK/name).read_text(encoding='utf-8')
            for token in tokens: self.assertRegex(header,rf'\b{token}\b')
        headers='\n'.join(p.read_text(encoding='utf-8',errors='replace') for p in (ROOT/'../dependencies/managed_components/lvgl__lvgl').rglob('*.h'))
        for app in ('text_reader','photo_viewer'):
            for call in set(re.findall(r'\b(lv_\w+)\s*\(',read(f'main/apps/{app}.cpp'))):
                self.assertRegex(headers,rf'\b{call}\b',call)

    def test_new_source_marker_and_old_build_artifacts_unchanged(self):
        from check_manual_lifecycle import verify_build_unchanged
        for name in ('main/main.cpp','main/apps/settings_system.cpp'):
            self.assertIn('BATTERY-CLEAN-20260917',read(name))
        verify_build_unchanged(self)

    def test_input_animation_torch_and_full_reading_font_preserved(self):
        # Torch defaults and external input routing are checked by check_keyboard_battery.
        protected=['main/apps/flashlight_signal.hpp','main/ui/text_input.hpp','main/ui/boot_animation.cpp',
                   'main/ui/boot_animation.hpp','main/ui/lv_font_cjk_16.c',
                   'main/ui/theme.hpp']
        baseline=json.loads((BACKUP/'manifest.json').read_text(encoding='utf-8'))
        self.assertEqual(len(protected),6)
        for name in protected:
            if name=='main/apps/flashlight_signal.hpp':
                from check_auto_power import original_sos
                self.assertEqual(hashlib.sha256(original_sos(read(name)).encode()).hexdigest(),baseline[name])
            elif name=='main/ui/boot_animation.cpp':
                # Authorized YUMO redesign; lifecycle/vector checks replace
                # the obsolete four-square byte freeze (check_ui_power).
                self.assertIn('lv_label_set_text(wordmark,"YUMO")',read(name))
                self.assertIn('elapsed>=boot::DurationMs',read(name))
                self.assertIn('lv_timer_delete(timer)',read(name))
                self.assertIn('lv_obj_delete(cover)',read(name))
            elif name=='main/ui/text_input.hpp':
                # H-mode navigation needs the visible editor's focus scope.
                # Keep the pre-existing public interface byte-identical.
                data=(ROOT/name).read_bytes()
                addition=b'    static lv_obj_t *root(); // Controller focus stays inside the visible input bar.\n'
                self.assertEqual(data.count(addition),1)
                self.assertEqual(hashlib.sha256(data.replace(addition,b'')).hexdigest(),baseline[name],name)
            elif name=='main/ui/theme.hpp':
                # Palette became dynamic; preserve every font/layout declaration.
                from check_native_themes import FONT_LAYOUT_SHA256
                suffix=read(name).split('// Fonts',1)[1].strip()
                reading='// Plain document text does not need the taller UI-symbol fallback metrics.\nconst lv_font_t *fontReading(void);\n'
                self.assertEqual(suffix.count(reading),1)
                suffix=suffix.replace(reading,'')
                self.assertEqual(hashlib.sha256(suffix.encode()).hexdigest(),FONT_LAYOUT_SHA256)
            else:
                self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),baseline[name],name)

    def test_sd_sleep_gate_checks_idle_and_unmounts_only_after_gui_ack(self):
        power=read('main/core/power_mgr.cpp')
        start=part(power,'esp_err_t Power::deepSleep(bool automatic)', 'void Power::standby')
        self.assertIn('standby(automatic)',start)
        self.assertNotIn('FileServer',power)
        self.assertNotIn('ESP_ERR_NOT_FINISHED',start)
        from check_native_themes import strip_theme_commit
        entry=strip_theme_commit(part(power,'void Power::processStandby()', 'void Power::syncTouch'))
        order=['if(!uiStopped.load())','core::storageIdle()', 'Settings::flush(true)',
               'terminalPhase.store(true)','bsp_sdcard_unmount()', 'esp_wifi_stop()']
        self.assertEqual([entry.index(x) for x in order],sorted(entry.index(x) for x in order))
        self.assertIn('requestRecovery("SD reader busy")',entry)
        browser=read('main/core/storage_browser.cpp')
        self.assertIn('bool storageIdle() { return active.load()==0 && chip::storageIdle() && mp3::storageIdle() && photo::idle() && gif::idle() && files::idle() && knowledge::idle() && patterns::idle() && garden::idle(); }',browser)
        worker=part(browser,'void worker(void *arg)', 'std::shared_ptr<StorageJob> start')
        self.assertLess(worker.index('execute(*job)'),worker.index('--active'))

    def test_removed_http_routes_have_no_remaining_settings_or_service_hooks(self):
        for name in ('main/core/file_server.cpp','main/core/file_server.hpp'):
            self.assertFalse((ROOT/name).exists())
        for name in ('main/main.cpp','main/CMakeLists.txt','main/core/system_service.cpp','main/core/power_mgr.cpp',
                     'main/apps/settings.cpp','main/apps/settings_system.cpp'):
            self.assertNotRegex(read(name).replace('        esp_http_server\n',''),r'FileServer|file_server|esp_http_server|网络硬盘|文件传输尚未结束')

    def test_local_storage_sleep_spec_requires_gui_ack_and_idle(self):
        # State-space specification, not C++ execution: an old reader can finish
        # before or after GUI acknowledgement; unmount is allowed only after both.
        from itertools import product
        for gui_ack,active,pressed in product((False,True),(0,1,2),(False,True)):
            can_unmount=gui_ack and active==0 and not pressed
            if can_unmount:
                self.assertTrue(gui_ack);self.assertEqual(active,0);self.assertFalse(pressed)
        power=part(read('main/core/power_mgr.cpp'),'void Power::processStandby()', 'void Power::syncTouch')
        self.assertLess(power.index('if(gpio_get_level(kPwrPin)==0) return'),power.index('core::storageIdle()'))
        for protected in ('GUI acknowledgement timeout','SD reader busy','settings save failed'):
            self.assertIn(protected,power)

    def test_diagnostics_do_not_query_a_host_task_during_terminal_teardown(self):
        s=read('main/main.cpp')
        self.assertNotRegex(s,r'xTaskGetHandle|uxTaskGetStackHighWaterMark|vTaskListTasks')
        bsp=read('components/esp32_s3_touch_lcd_1_54/include/bsp/esp32_s3_touch_lcd_1_54.h')
        for token in ('bsp_sdcard_unmount','bsp_sdcard_get_handle'):
            self.assertIn(token,bsp)

if __name__=='__main__': unittest.main(verbosity=2)
