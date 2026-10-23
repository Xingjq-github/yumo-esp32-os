"""Speed-test integration contracts; numeric/storage behavior runs in native C++."""
from pathlib import Path
import re, unittest
ROOT=Path(__file__).resolve().parents[2]/'firmware'
def read(name):return (ROOT/name).read_text(encoding='utf8')
class SpeedTestContracts(unittest.TestCase):
    def test_registration_and_build(self):
        main=read('main/main.cpp');cmake=read('main/CMakeLists.txt')
        self.assertEqual(main.count('std::make_unique<apps::SpeedTest>()'),1)
        for name in ('core/speed_test.cpp','core/speed_test_store.cpp','apps/speed_test.cpp'):
            self.assertEqual(cmake.count('"'+name+'"'),1)
        registrations=re.findall(r'registerApp\(\s*"([^"]+)"\s*,\s*([^,]+),',main)
        icon=next(icon.strip() for name,icon in registrations if name=='网速测速')
        self.assertEqual(sum(value.strip()==icon for _,value in registrations),1)
        self.assertEqual(registrations[-1][0],'设置')
    def test_transport_validates_full_payload_and_server_ack(self):
        s=read('main/core/speed_test.cpp')
        for token in ('esp_crt_bundle_attach','skip_cert_common_name_check=false','disable_auto_redirect=true',
                      '"Accept-Encoding","identity"','"Cache-Control","no-cache, no-store"',
                      'cf-meta-upload-bytes','result.acknowledged!=bytes','result.bytes!=bytes',
                      'esp_http_client_is_complete_data_received','HTTP_EVENT_ON_HEADERS_COMPLETE',
                      'esp_http_client_is_persistent_connection'):
            self.assertIn(token,s)
        self.assertIn('CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y',read('sdkconfig.defaults'))
        self.assertIn('CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y',read('sdkconfig'))
        for token in ('setWifiEnabled','setWifiPs','esp_wifi_set_ps','nvs_','vTaskDelete('):self.assertNotIn(token,s)
    def test_workers_and_lifecycle_are_fenced(self):
        worker=read('main/core/speed_test.cpp');app=read('main/apps/speed_test.cpp')
        self.assertNotIn('lv_',worker)
        self.assertIn('job->cancelled=true',app);self.assertIn('historyJob->cancelled=true',app)
        self.assertIn('memory_order_release',worker);self.assertIn('memory_order_acquire',app)
        self.assertIn('!core::speed::idle()',read('main/core/power_runtime.cpp'))
        self.assertIn('speed test shutdown busy',read('main/core/power_mgr.cpp'))
        for token in ('sd::Session media(job.sdAccess)','media.claimMutation()','media.reportError',
                      'job->sdAccess=sd::requestAccess()','sameNetwork(state.record)','sys::Power::isScreenOn()'):
            self.assertIn(token,worker)
    def test_sd_history_and_ui_have_no_fake_packet_loss(self):
        ui=read('main/apps/speed_test.cpp');store=read('main/core/speed_test_store.cpp')
        self.assertIn('不是网络丢包率',ui);self.assertIn('HTTP延迟',ui)
        self.assertIn('%.19s\\n%s',ui);self.assertIn('SD 卡 /speedtest',ui)
        for token in ('fopen','opendir','esp_http_client_'):self.assertNotIn(token,ui)
        for token in ('O_CREAT|O_EXCL|O_WRONLY','syncFile(file.get())','std::rename','MaxRecordBytes','names.size()>PageSize'):
            self.assertIn(token,store)
        self.assertIn('SpeedTests = "/sdcard/speedtest"',read('main/core/sd_media.hpp'))
    def test_history_delete_uses_worker_and_stable_target(self):
        worker=read('main/core/speed_test.cpp');ui=read('main/apps/speed_test.cpp')
        self.assertIn('media.generation()!=job->cardGeneration',worker)
        self.assertIn('store::erase(sd::SpeedTests,job->entry,{job.get(),deleteCancelled})',worker)
        self.assertIn('xTaskCreateWithCaps(deleteWorker',worker)
        self.assertIn('removeHistory(self->detailEntry,self->historyGeneration)',ui)
        self.assertIn('"删除此记录"',ui);self.assertIn('"确认删除"',ui)
        self.assertNotIn('unlink(',ui)
    def test_quality_sampling_is_bounded_and_independent(self):
        worker=read('main/core/speed_test.cpp');model=read('main/core/speed_test_model.hpp')
        for token in ('TransferTargetUs=12000000','TrafficLimitBytes=32000000','DirectionLimitUs=25000000','ProbeCapacity=48'):
            self.assertIn(token,model)
        for token in ('xTaskCreateWithCaps(worker,"speed_ping"','SO_SNDTIMEO','MSG_DONTWAIT','probes.stop();probes.snapshot(r)',
                      't.address.sin_addr.s_addr','checksum(echo,n-header)','std::memcmp(echo+8,t.payload','transferSatisfied(samples,count)'):
            self.assertIn(token,worker)
    def test_quality_history_and_confidence_are_explicit(self):
        ui=read('main/apps/speed_test.cpp');store=read('main/core/speed_test_store.cpp')
        for token in ('quality_version','rtt_us','download_traffic_limited'):
            self.assertIn(token,store)
        for token in ('probeStats','Confidence::Good','lv_chart_set_series_value_by_id','job->snapshot(last)'):
            self.assertIn(token,ui)
if __name__=='__main__':unittest.main()
