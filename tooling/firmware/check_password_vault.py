"""Password-vault source contracts and crash model. No device execution.
Supersedes prior task-specific "only revision may change" freeze assertions.
"""
from pathlib import Path
import base64
import hashlib
import json
import re
import struct
import unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/password-vault-build-20260917-190010'
SLOT=0x80000;SECTOR=4096;MAX=262144

def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def compact(s):return re.sub(r'\s+','',s)
def framing(b):
    if len(b)<144 or len(b)>MAX or b[:8]!=b'JYVAULT2':return False
    rounds,size,revision=struct.unpack_from('<IIQ',b,8)
    return rounds==0 and size==len(b)-128 and 0<=revision<=0xffffffff and any(b[24:40]) and any(b[40:56])
def slot_header(generation,bundle,config):
    h=bytearray(96);h[:8]=b'JVSLOT1\0';struct.pack_into('<III',h,8,generation,len(bundle),len(config));h[32:64]=hashlib.sha256(bundle+config).digest();h[64:96]=hashlib.sha256(h[:64]).digest();return bytes(h)
def read_slot(flash,index):
    offset=index*SLOT;h=bytes(flash[offset:offset+96]);generation,b,c=struct.unpack_from('<III',h,8)
    if h[:8]!=b'JVSLOT1\0' or not generation or not 144<=b<=MAX or not 28<=c<=8192 or any(h[20:32]) or hashlib.sha256(h[:64]).digest()!=h[64:96]:return None
    data=bytes(flash[offset+SECTOR:offset+SECTOR+b+c])
    if hashlib.sha256(data).digest()!=h[32:64] or not framing(data[:b]):return None
    return generation,data[:b],data[b:]
def latest(flash):
    valid=[r for r in (read_slot(flash,0),read_slot(flash,1)) if r]
    return max(valid,key=lambda r:r[0]) if valid else None

def operations(target,generation,bundle,config):
    offset=target*SLOT
    for n in range(0,SLOT,SECTOR):yield offset+n,b'\xff'*SECTOR
    data=bundle+config
    for n in range(0,len(data),SECTOR):yield offset+SECTOR+n,data[n:n+SECTOR]
    yield offset,slot_header(generation,bundle,config)

class VaultContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.bundle=b'JYVAULT2'+struct.pack('<IIQ',0,16,1)+b'i'*16+b's'*16+b'c'*88
    def test_scope_all_preexisting_unrelated_sources_configs_unchanged(self):
        baseline=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files']
        allowed={'main/CMakeLists.txt','main/main.cpp','main/apps/settings_system.cpp','main/core/ble_hid.cpp','main/core/ble_hid.hpp','main/core/power_mgr.cpp','main/core/system_service.cpp','main/core/system_service.hpp','main/ui/lv_font_menu_14.c','partitions.csv','../tooling/firmware/check_battery_power.py','../tooling/firmware/check_firmware_refactor.py','../tooling/firmware/check_hid_ui.py','../tooling/firmware/check_keyboard_battery.py'}
        for name,info in baseline.items():
            if name in allowed or name.startswith(('../docs/firmware/','../docs/history/memory/')):continue
            self.assertTrue((ROOT/name).is_file(),name)
            self.assertEqual(hashlib.sha256((ROOT/name).read_bytes()).hexdigest(),info['sha256'],name)
    def test_partition_user_regions_preserved_without_model_partition(self):
        rows=[];offset=0
        for line in read('partitions.csv').splitlines():
            if not line.strip() or line.lstrip().startswith('#'):continue
            name,kind,sub,start,size,*_=map(str.strip,line.split(','));size=int(size,0)
            offset=int(start,0) if start else offset
            rows.append((name,offset,size));offset+=size
        self.assertEqual(rows,[('nvs',0x9000,0x4000),('otadata',0xd000,0x2000),('phy_init',0xf000,0x1000),('factory',0x20000,0x950000),('assets',0x970000,0x550000),('vault',0xEC0000,0x100000)])
        self.assertEqual(offset,0xFC0000)
    def test_new_app_lazy_and_no_new_background_boot_job(self):
        main=read('main/main.cpp');self.assertIn('std::make_unique<apps::PasswordVault>()',main)
        self.assertEqual(main.count('vault::'),0)
        marker=re.search(r'\[build\] ([A-Z][A-Z0-9-]+) running=',main)
        self.assertIsNotNone(marker)
        self.assertIn(marker.group(1),read('main/apps/settings_system.cpp'))
        self.assertNotIn('esp_http_server',read('main/CMakeLists.txt'))
        self.assertIn('vTaskDeleteWithCaps(nullptr)',read('main/core/password_vault.cpp'))
    def test_sensitive_cleanup_and_screen_lock_ownership(self):
        app=compact(read('main/apps/password_vault.cpp'));worker=compact(read('main/core/password_vault.cpp'))
        self.assertIn('vault::cancel(m_job);m_job.reset();m_session.reset();resetListIndex();clearListWidgets();wipeField();',app)
        self.assertIn('vault::clear(m_pin)',app)
        self.assertIn('lv_display_get_inactive_time(nullptr)>=60000',app)
        self.assertIn('ScreenOff',app);self.assertIn('clear(job->master);job->input.reset();',worker)
        self.assertIn('MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT',worker)
    def test_exception_safe_crypto_http_handles(self):
        crypto=compact(read('main/core/vault_crypto.cpp'));network=read('main/core/vault_sync.cpp');parser=read('main/core/vault_json.cpp')
        self.assertIn('~HmacSha256(){wipe(this,sizeof(*this));}',crypto)
        self.assertIn('HmacSha256hmac;',crypto)
        self.assertNotIn('psa_import_key(',crypto);self.assertNotIn('psa_mac_compute(',crypto)
        self.assertIn('ClientOwner',network);self.assertIn('~ClientOwner()',network)
        self.assertIn('TransferBuffer',network);self.assertNotIn('/sdcard/',parser);self.assertIn('printedOwner',parser)
    def test_https_strict_bounded_manual_download(self):
        source=compact(read('main/core/vault_sync.cpp'))
        for token in ('config.cert_pem=ca.empty()?nullptr:ca.c_str()','config.skip_cert_common_name_check=false','config.disable_auto_redirect=true','config.timeout_ms=10000','status!=200','output.size()+n>maximum','sys::WifiState::Connected','esp_http_client_is_complete_data_received'):
            self.assertIn(token,source)
        self.assertIn('if(https&&ca.empty())config.crt_bundle_attach=esp_crt_bundle_attach',source)
        self.assertIn('normalizeOrigin(origin,endpoint)',source)
        self.assertIn('https&&std::time(nullptr)<1704067200',source)
        self.assertNotRegex(source,r'https?://192\.')
    def test_sync_checks_identity_authentication_without_revision_gate(self):
        code=compact(read('main/core/password_vault.cpp'))
        for token in ('current.id.data()','current.salt.data()','bundle==disk.bundle&&result->url==current.url','decode(job,bundle,current.key)','configEncode(*result,config)','commit(disk,bundle,config'):
            self.assertIn(token,code)
        self.assertNotIn('current.revision',code)
        self.assertNotIn('同版本内容不同，拒绝同步',code)
    def test_builtin_keys_and_local_pin_without_provisioning(self):
        app=read('main/apps/password_vault.cpp');core=read('main/core/vault_json.cpp');worker=compact(read('main/core/password_vault.cpp'))
        self.assertNotIn('授权镜像',app);self.assertNotIn('/sdcard/',core)
        self.assertIn('configDecode(disk.config,*result)',worker)
        self.assertIn('Operation::Setup',worker);self.assertIn('pin_salt',core);self.assertIn('pin_hash',core)
        self.assertIn('builtin::Token',worker);self.assertIn('builtin::Key',worker)
        self.assertIn('pattern::valid(job.master.data(),job.master.size())',worker)
        self.assertIn('!job.blank',worker)
    def test_no_automatic_enter_tab_or_host_target_switch(self):
        s=compact(read('main/core/password_vault.cpp'));ble=compact(read('main/core/ble_hid.cpp'))
        self.assertIn('printable(text.data(),text.size())',s)
        self.assertIn('privateLinkGeneration()!=job.link',s)
        self.assertNotIn('i<30',s);self.assertNotIn('pdMS_TO_TICKS(100)',s)
        self.assertIn('if(i+1<text.size())vTaskDelay(pdMS_TO_TICKS(interaction::KeyGapMs))',s)
        self.assertIn('s.link==s_linkGeneration.load()',ble);self.assertIn('s.permit==s_privatePermit.load()',ble)
        self.assertIn('if(s.releasing)',ble);self.assertIn('ble_npl_time_ms_to_ticks32(40)',ble)
    def test_strong_pairing_scoped_and_send_authenticated_128_bit(self):
        ble=compact(read('main/core/ble_hid.cpp'))
        for token in ('desc.sec_state.authenticated','desc.sec_state.bonded','desc.sec_state.key_size==16','authenticated?BLE_SM_IO_CAP_DISP_YES_NO:BLE_SM_IO_CAP_NO_IO','ble_hs_cfg.sm_mitm=authenticated?1:0','privateStrokeEvent','nimble_port_get_dflt_eventq()'):
            self.assertIn(token,ble)
        self.assertIn('BluetoothSecureDiscover',read('main/core/system_service.cpp'))
    def test_power_waits_for_crypto_network_storage_and_key_release(self):
        code=read('main/core/power_mgr.cpp');worker=compact(read('main/core/password_vault.cpp'))
        self.assertIn('if(!core::storageIdle() || !vault::idle())',code)
        self.assertIn('>10000000',code);self.assertIn('password worker busy',code)
        self.assertLess(code.index('if(!on) BleHid::cancelPrivateInput()'),code.index('screen.store(on)'))
        self.assertIn('!active.load()&&sys::BleHid::privateInputIdle()',worker)
    def test_kdf_exact_iterations_and_yields(self):
        code=compact(read('main/core/vault_crypto.cpp'));header=compact(read('main/core/vault_format.hpp'))
        self.assertIn('PinRounds=10000',header);self.assertIn('first[19]=1',code)
        self.assertIn('uint32_ti=1;',code);self.assertIn('i<PinRounds',code)
        self.assertIn('(i&1023)==0',code);self.assertIn('vTaskDelay(1)',code)
        self.assertIn('(i&255)==0&&(cancel.load()||!sys::Power::isScreenOn()||sys::Power::sleepPending())',code)
        self.assertIn('hmac.compute(first,sizeof(first),u.bytes.data())',code)
        self.assertIn('hmac.compute(u.bytes.data(),u.bytes.size(),next.bytes.data())',code)
        self.assertIn('output.bytes[j]^=u.bytes[j]',code)
        self.assertIn('wipe(output.bytes.data(),32);returnfalse;',code)
        self.assertNotIn('mbedtls/gcm.h',code)
    def test_schema_sizes_and_parser_recursion(self):
        code=compact(read('main/core/vault_json.cpp'))
        for token in ('++depth>8','std::memchr(data,0,size)','core::isValidUtf8','t>96','u>128','p>128','n>MaxNoteBytes','std::strspn(id,"0123456789abcdef")!=32'):
            self.assertIn(token,code)
    def test_storage_only_validated_partition_and_header_last(self):
        code=compact(read('main/core/vault_storage.cpp'))
        for token in ('p->address==0xEC0000','p->size==0x100000','"vault"','previous.slot==0?1:0','previous.generation==UINT32_MAX','!previous.blank&&previous.slot<0'):
            self.assertIn(token,code)
        self.assertLess(code.index('io.write(p,offset+Sector+i'),code.index('io.write(p,offset,h.data()'))
        self.assertNotIn('esp_flash_erase',code)
    def test_flash_model_all_update_interruptions_keep_committed_copy(self):
        flash=bytearray(b'\xff'*(2*SLOT));config=b'n'*12+b'c'*32
        for at,data in operations(0,1,self.bundle,config):flash[at:at+len(data)]=data
        old=bytes(flash[:SLOT]);new_bundle=bytearray(self.bundle);struct.pack_into('<Q',new_bundle,16,2)
        for at,data in operations(1,2,bytes(new_bundle),config):
            self.assertEqual(latest(flash)[0],1)
            flash[at:at+len(data)]=data
            self.assertEqual(bytes(flash[:SLOT]),old)
        self.assertEqual(latest(flash)[0],2)
        # Torn header, corrupted data and a forged oversized length cannot win.
        for at in (SLOT,SLOT+12,SLOT+64,SLOT+SECTOR+5):
            damaged=bytearray(flash);damaged[at]^=0x40;self.assertEqual(latest(damaged)[0],1)
    def test_flash_model_partial_initial_commit_not_treated_as_existing_vault(self):
        config=b'n'*12+b'c'*32;flash=bytearray(b'\xff'*(2*SLOT));ops=list(operations(0,1,self.bundle,config))
        for at,data in ops[:-1]:flash[at:at+len(data)]=data;self.assertIsNone(latest(flash))
        self.assertTrue(any(b!=255 for b in flash)) # explicit initialization required
        at,data=ops[-1];flash[at:at+len(data)]=data;self.assertEqual(latest(flash)[0],1)
    def test_docker_disk_persistence_no_backup_feature(self):
        compose=read('../packaging/nas/compose.yaml');service=read('../server/src/main/java/com/joyvault/VaultStore.java')
        for token in ('type: bind','target: /data','create_host_path: false','read_only: true','no-new-privileges:true'):self.assertIn(token,compose)
        # Java 8 NAS deployment follows the graphical wizard's image-default user.
        self.assertNotRegex(compose,r'(?m)^\s*user:')
        self.assertIn('PRAGMA synchronous=FULL',service);self.assertIn('PRAGMA journal_mode=DELETE',service)
        self.assertNotRegex(read('../server/src/main/java/com/joyvault/VaultController.java'),r'@(?:Get|Post)Mapping\("/api/(?:backup|restore)')
        for p in (ROOT/'../server/src/main/resources/static').rglob('*.js'):
            self.assertNotRegex(p.read_text(encoding='utf-8'),r'(?:localStorage|sessionStorage)\.(?:setItem|\w+\s*=)')

if __name__=='__main__':unittest.main(verbosity=2)
