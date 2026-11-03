"""New AP-only browser and runtime QR contracts. Native tests exercise production handlers."""
from pathlib import Path
import unittest,re
ROOT=Path(__file__).resolve().parents[2]/'firmware'
def read(name):return (ROOT/'main'/name).read_text(encoding='utf-8')
class LanWebContracts(unittest.TestCase):
    def test_service_drains_before_radio_and_sd_sleep(self):
        source=read('core/lan_hotspot.cpp');cleanup=source.split('bool cleanup()',1)[1]
        self.assertLess(cleanup.index('lanweb::stop()'),cleanup.index('stopMeter()'))
        self.assertLess(cleanup.index('!lanweb::idle()'),cleanup.index('esp_wifi_stop()'))
        self.assertIn('lanweb::enable(apAddress)',source)
        web=read('core/lan_web.cpp');self.assertIn('while (httpd_stop(server) != ESP_OK)',web)
        self.assertIn('wanted.load() && !Power::sleepPending()',web)
    def test_interface_host_origin_and_write_guards(self):
        web=read('core/lan_web.cpp')
        for token in ('local != ip','!socketIpv4(fd, false, local)','!socketIpv4(fd, true, peer)',
                      '(ntohl(peer) & 0xffffff00u) != (ntohl(ip) & 0xffffff00u)',
                      'header(req, "Host")','header(req, "Origin")','header(req, "X-Yumo-Request") != "1"','if (!admitted(req))'):
            self.assertIn(token,web)
        self.assertNotIn('Access-Control-Allow-Origin',web)
    def test_socket_normalization_keeps_full_address_and_checks_mapping(self):
        body=read('core/lan_web.cpp').split('bool socketIpv4(',1)[1].split('bool admitted(',1)[0]
        for token in ('sockaddr_storage endpoint{}','size = sizeof(endpoint)','getpeername','getsockname',
                      'size >= sizeof(sockaddr_in)','size >= sizeof(sockaddr_in6)',
                      'endpoint.ss_family == AF_INET','endpoint.ss_family == AF_INET6',
                      'std::memcmp(bytes, mappedPrefix, sizeof(mappedPrefix))',
                      'std::memcpy(&ip, bytes + sizeof(mappedPrefix), sizeof(ip))'):
            self.assertIn(token,body)
    def test_streaming_upload_is_exclusive_complete_and_non_overwriting(self):
        upload=read('core/lan_web.cpp').split('esp_err_t upload(',1)[1].split('esp_err_t login(',1)[0]
        for token in ('sd.claimMutation()','req->content_len > MaxUpload','O_WRONLY | O_CREAT | O_EXCL','received != req->content_len','fsync(fd)','close(fd)','unlink(temporary.c_str())','return sdError(req, EEXIST)'):
            self.assertIn(token,upload)
        self.assertLess(upload.index('fsync(fd)'),upload.index('rename(temporary'))
        self.assertLess(upload.index('close(fd)'),upload.index('rename(temporary'))
        self.assertIn('char buffer[4096]',upload)
    def test_web_unlock_reuses_pin_hash_cooldown_and_read_only_disk_path(self):
        worker=read('core/password_vault.cpp')
        self.assertIn('job.operation != Operation::WebUnlock',worker)
        unlock=worker.split('} else if (job.operation == Operation::Unlock || job.operation == Operation::WebUnlock)',1)[1].split('} else if (job.operation == Operation::ChangePin)',1)[0]
        for token in ('configDecode(disk.config, *result)','verifyPin(job, *result)','job.output = std::move(result)','return;'):self.assertIn(token,unlock)
        for token in ('commit(', 'encryptEmpty(', 'Setup'):self.assertNotIn(token,unlock)
        self.assertIn('job.operation == Operation::Unlock || job.operation == Operation::WebUnlock ||',worker)
        self.assertIn('if (esp_timer_get_time() < nextUnlock)',worker)
    def test_browser_has_no_persistent_password_cache_and_clears_late_results(self):
        web=read('core/lan_web.html')
        for token in ('records=[]','own!==generation','visibilitychange','pagehide','pageshow','SameSite'): # Cookie comes from HTTP implementation.
            self.assertIn(token,web+read('core/lan_web.cpp'))
        for token in ('localStorage','sessionStorage','innerHTML','document.write'):self.assertNotIn(token,web)
        self.assertIn('document.execCommand(\'copy\')',web)
        self.assertIn('request.responseText).bytes!==file.size',web)
    def test_detail_has_title_header_values_on_input_and_labeled_metadata(self):
        app=read('apps/password_vault.cpp');detail=app.split('  case Page::Detail:',1)[1].split('  case Page::Qr:',1)[0]
        self.assertIn('m_session->entries[m_selected].title.c_str()',app.split('void PasswordVault::draw()',1)[1])
        for token in ('"输入账号"','"输入密码"','wrap(m_body, entry.title.c_str())','auto secret'):self.assertNotIn(token,detail)
        for token in ('entry.username.c_str(), Action::User','entry.password.c_str() : "********", Action::Password','vault::targetLabel(entry.category)','"备注："','Action::Qr'):self.assertIn(token,detail)
    def test_qr_requires_reveal_and_expires_and_has_integer_scaling(self):
        app=read('apps/password_vault.cpp');qr=read('ui/vault_qr.cpp')
        for token in ('m_qrPending = true','action(Action::Reveal, 0)','lv_tick_elaps(m_qrSince) >= 30000','vault::clear(text)','lv_obj_clean(root())'):self.assertIn(token,app)
        for token in ('qrcodegen_makeEci(26','qrcodegen_encodeSegmentsAdvanced','Pixels / (modules + 8)','if (scale < 2)','vault::SecretBytes','vault::wipe(buf->data, buf->data_size)','LV_EVENT_DELETE'):self.assertIn(token,qr)
    def test_hint_removal_preserves_visible_validation_errors(self):
        ui=read('apps/lan.cpp')
        self.assertNotIn('修改后打开开关即可连接',ui);self.assertNotIn('关闭后可改名称和密码',ui)
        self.assertIn('lv_obj_set_hidden(hint,false)',ui);self.assertIn('"http://"',ui)
if __name__=='__main__':unittest.main()
