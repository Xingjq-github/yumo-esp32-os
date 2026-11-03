"""Knowledge package + source contracts. No compiler, device or SD writes.

Python record models check the intended disk protocol; they do not claim to
execute the ESP32 C++ implementation or validate actual LVGL/power behavior.
"""
from pathlib import Path
import copy,hashlib,re,struct,unittest,zlib
import build_knowledge as kb
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path):return (ROOT/path).read_text(encoding='utf-8')
def reseal(blob):
    ni=struct.unpack_from('<I',blob,20)[0]
    struct.pack_into('<I',blob,28,kb.crc(blob[64:64+ni]));struct.pack_into('<I',blob,32,kb.crc(blob[64+ni:]));struct.pack_into('<I',blob,60,kb.crc(blob[:60]))
    return blob
class KnowledgePackage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cards=kb.sources();cls.blob=kb.build(cls.cards);cls.decoded=kb.inspect(cls.blob)
    def test_all_categories_and_gof_families(self):
        self.assertEqual(len(self.cards),449)
        self.assertEqual([sum(c['category']==name for c in self.cards) for name in kb.CATEGORIES],[151,44,28,75,30,121])
        patterns=[c for c in self.cards if c['id'].startswith('java.pattern.')]
        self.assertEqual(len(patterns),23)
        self.assertEqual([sum(c['group'].endswith(n) for c in patterns) for n in ('创建型','结构型','行为型')],[5,7,11])
    def test_deterministic_package_matches_distribution(self):
        self.assertEqual(self.blob,(ROOT/'../resources/sdcard/programming/library.ykb').read_bytes())
        self.assertLess(self.decoded['index_bytes'],80*1024)
        self.assertLess(len(self.blob),512*1024)
        self.assertFalse((ROOT/'../resources/sdcard/programming/state.bin').exists())
    def test_every_original_section_roundtrips_without_reformatting(self):
        for card,decoded in zip(self.cards,self.decoded['entries']):
            self.assertEqual(decoded['id'],kb.sid(card['id']))
            self.assertEqual(decoded['title'],card['title'])
            self.assertEqual(decoded['version'],card['version'])
            for a,b in zip(card['sections'],decoded['sections']):
                self.assertEqual(a['text'],b['text']);self.assertEqual(kb.section_id(a['id']),b['id'])
    def test_stable_ids_ignore_order_and_revision(self):
        reordered=kb.inspect(kb.build(list(reversed(self.cards)),20260929))
        self.assertEqual({c['id'] for c in reordered['entries']},{c['id'] for c in self.decoded['entries']})
    def test_aliases_support_title_english_and_initials(self):
        self.assertEqual(kb.initials('工厂方法'),'gcff');self.assertEqual(kb.initials('冒泡排序'),'mppx')
        for needle in ('singleton','gcff','mppx','hashmap','bigdecimal','查询重复数据'):
            self.assertTrue(any(needle in c['aliases'] for c in self.decoded['entries']),needle)
        self.assertTrue(all(c['aliases']==c['aliases'].lower() for c in self.decoded['entries']))
    def test_truncation_trailing_data_and_crc_changes_rejected(self):
        for cut in (0,1,63,64,65,len(self.blob)//2,len(self.blob)-1):
            with self.assertRaises(ValueError):kb.inspect(self.blob[:cut])
        with self.assertRaises(ValueError):kb.inspect(self.blob+b'extra')
        for at in (0,8,16,28,60,64,1024,len(self.blob)-1):
            b=bytearray(self.blob);b[at]^=1
            with self.assertRaises(ValueError):kb.inspect(b)
    def test_valid_crcs_do_not_bypass_index_bounds(self):
        for at,value in ((16,1025),(20,0),(24,0)):
            b=bytearray(self.blob);struct.pack_into('<I',b,at,value);struct.pack_into('<I',b,60,kb.crc(b[:60]))
            with self.assertRaises(ValueError):kb.inspect(b)
        b=bytearray(self.blob);struct.pack_into('<I',b,64+8,0);reseal(b)
        with self.assertRaises(ValueError):kb.inspect(b)
    def test_duplicate_entry_and_invalid_section_are_rejected(self):
        b=bytearray(self.blob);b[64+48:64+56]=b[64:72];reseal(b)
        with self.assertRaises(ValueError):kb.inspect(b)
        b=bytearray(self.blob);base=64+self.decoded['index_bytes'];size=struct.unpack_from('<I',b,64+28)[0]
        struct.pack_into('<I',b,base+16+4,0)
        struct.pack_into('<I',b,64+32,kb.crc(b[base:base+size]));reseal(b)
        with self.assertRaises(ValueError):kb.inspect(b)
    def test_authoring_guards_reject_bad_content(self):
        for mutate in (
            lambda c:c[0].update(id=c[1]['id']),
            lambda c:c[0].update(related=['missing.reference']),
            lambda c:c[0]['sections'][0].update(text='x'*8193),
            lambda c:c[0]['sections'][0].update(text='x\n'*129),
            lambda c:c[0]['sections'][1].update(text='// 中文',kind='code'),
        ):
            cards=copy.deepcopy(self.cards);mutate(cards)
            with self.assertRaises(AssertionError):kb.build(cards)
    def test_complete_examples_have_class_and_main(self):
        examples=[s for c in self.cards for s in c['sections'] if s['title']=='完整示例']
        self.assertEqual(len(examples),33)
        for s in examples:
            self.assertIn('public class ',s['text']);self.assertIn('public static void main(String[] args)',s['text'])
        for c in self.cards:
            if c['group']=='排序':
                self.assertTrue(any(s['id']=='steps' for s in c['sections']))
                self.assertTrue(any(s['id']=='boundaries' for s in c['sections']))
    def test_all_metadata_and_source_links_present(self):
        self.assertTrue(all(c['source'].startswith('https://') and c['version'] for c in self.cards))
        self.assertTrue(all(any(s['id']=='reference' for s in c['sections']) for c in self.cards))
        self.assertLessEqual(max(len(s['text'].encode()) for c in self.cards for s in c['sections']),8192)
        self.assertLessEqual(max(sum(len(s['text'].encode()) for s in c['sections']) for c in self.cards),64*1024)
    def test_basic_operations_and_concrete_component_coverage(self):
        cards={c['id']:c for c in self.cards}
        for ident in ('mysql.query-basic','mysql.crud-demo','tool.linux.copy-move','tool.linux.network','tool.linux.service','tool.maven.modules','tool.maven.skip','java.jdbc-query','jvm.javap','algorithm.heap-ops'):
            self.assertTrue(any(s['kind']=='code' for s in cards[ident]['sections']),ident)
        from component_operations import OPERATIONS
        for name in OPERATIONS:
            self.assertTrue(any(s['kind']=='code' for s in cards['component.'+name]['sections']),name)
        self.assertFalse(any(s['kind']=='code' for s in cards['component.quorum']['sections']))
        example=next(s['text'] for s in cards['java.file-lines']['sections'] if s['kind']=='code')
        self.assertIn('"hello\\n"',example)
        self.assertNotIn('"hello\\\n',example)
    def test_windows_shells_are_separate_and_versioned(self):
        from windows_cards import cards
        windows=cards()
        self.assertEqual(len(windows),40)
        for group in ('CMD','PowerShell'):
            selected=[c for c in windows if c['group']==group]
            self.assertEqual(len(selected),20)
            for c in selected:
                self.assertTrue(c['source'].startswith('https://learn.microsoft.com/'))
                self.assertTrue(any(s['kind']=='code' for s in c['sections']))
                self.assertTrue(any(s['id']=='pitfall' and s['text'] for s in c['sections']))
        index={c['id']:c for c in windows}
        self.assertIn('8及以上',index['tool.cmd.robocopy']['sections'][-2]['text'])
        self.assertIn('5.1',index['tool.powershell.content']['sections'][-2]['text'])
class KnowledgeLifecycleContracts(unittest.TestCase):
    def test_on_demand_only_and_settings_last(self):
        main=read('main/main.cpp');names=re.findall(r'registerApp\(\s*"([^"]+)"',main)
        self.assertEqual(names.count('编程速查'),1);self.assertEqual(names[-1],'设置')
        self.assertIn('return std::make_unique<apps::Programming>()',main)
        self.assertNotIn('knowledge::open',main)
    def test_gui_never_does_sd_io_and_workers_never_access_ui(self):
        app=read('main/apps/programming.cpp');worker=read('main/core/knowledge_store.cpp')
        self.assertNotRegex(app,r'\b(?:fopen|fread|fwrite|fseek|fclose|fsync|opendir|bsp_sdcard)\s*\(')
        self.assertNotRegex(worker,r'\blv_\w+\s*\(|ui::|Programming\s*\*')
        self.assertIn('sys::Power::WorkGuard',worker);self.assertIn('MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',worker)
    def test_io_participates_in_sd_usb_and_sleep_fence(self):
        worker=read('main/core/knowledge_store.cpp');browser=read('main/core/storage_browser.cpp')
        self.assertIn('knowledge::idle()',browser)
        self.assertIn('sd::Session card(j->access,true,true)',worker)
        self.assertIn('card.generation()!=j->generation',worker)
        self.assertIn('lib->generation!=card.generation()',worker)
        self.assertIn('h!=lib->header',worker)
    def test_exit_cancels_reads_flushes_and_destroys_subscriptions(self):
        app=read('main/apps/programming.cpp');hide=app.split('void Programming::onHide()',1)[1].split('uint32_t Programming::powerHints',1)[0]
        for token in ('flush()','cancelled.store(true)','m_job.reset()','m_save.reset()','releasePageKeys','releaseView()','m_library.reset()','lv_timer_delete','EventBus::unsubscribe'):
            self.assertIn(token,hide)
        self.assertNotIn('vTaskDelete',app)
        self.assertEqual(read('main/core/knowledge_store.cpp').count('vTaskDeleteWithCaps(nullptr)'),2)
    def test_state_write_is_coalesced_and_synced_before_done(self):
        worker=read('main/core/knowledge_store.cpp')
        for token in ('pending=j','current.reset()','saveRunning=false','fsync(fileno(f.p))','f.close()','encodeStateInto(*b','slot==0?StateStride:0'):
            self.assertIn(token,worker)
        self.assertIn('m_saveAccess=core::sd::requestAccess()',read('main/apps/programming.cpp'))
        self.assertIn('m_changedAt)>=1200',read('main/apps/programming.cpp'))
    def test_small_viewport_touch_controls_and_keyboard_shared(self):
        app=read('main/apps/programming.cpp')
        for token in ('240,168','0,204','m_pool','networkField','TextInput::hide','m_state.wrap','LV_DIR_HOR','lv_obj_set_scroll_chain_ver','m_full?240:204'):
            self.assertIn(token,app)
        self.assertNotIn('LV_EVENT_GESTURE',app)
        self.assertIn('m_code?"描述":"代码"',app)
        self.assertIn('lv_line_set_points(shape,points,11)',app)
    def test_failed_request_cannot_reopen_old_block_with_new_ids(self):
        app=read('main/apps/programming.cpp')
        request=app.split('void Programming::request(',1)[1].split('void Programming::poll()',1)[0]
        self.assertLess(request.index('m_article={};m_blocks.clear();m_positions.clear();'),request.index('core::knowledge::read'))
        self.assertIn('if(m_blocks.size()!=m_article.sections.size())return',app)
        self.assertIn('std::min(ui::code::WrapColumns,end-at)',app)
    def test_font_and_bounds(self):
        from update_menu_font import read_font
        glyphs=read_font(read('main/ui/lv_font_code_13.c'))
        self.assertEqual(set(glyphs),set(range(32,127)))
        for g in glyphs.values():
            self.assertEqual(g.adv_w,128);self.assertLessEqual(g.box_h+g.ofs_y,13);self.assertGreaterEqual(g.ofs_y,-3)
        self.assertTrue((ROOT/'../resources/knowledge/licenses/DejaVu.txt').is_file())
        for token in ('MaxIndex=128*1024','MaxBlock=8192','MaxSections=32','MaxSourceLines=128','MaxRecent=30','StateStride=8192'):
            self.assertIn(token,read('main/core/knowledge_format.hpp'))
        for name in ('core/knowledge_store.cpp','apps/programming.cpp','ui/lv_font_code_13.c'):
            self.assertEqual(read('main/CMakeLists.txt').count('"'+name+'"'),1)
class BookmarkProtocolModel(unittest.TestCase):
    @staticmethod
    def record(seq,ident=1,section=2,offset=40):
        b=bytearray(8192);b[:4]=b'YKS1';struct.pack_into('<I',b,4,seq)
        struct.pack_into('<III',b,16,1,1,1);struct.pack_into('<Q',b,32,ident)
        struct.pack_into('<QII',b,32+512*8,ident,section,offset);struct.pack_into('<I',b,8188,kb.crc(b[:8188]));return b
    @staticmethod
    def valid(b):return len(b)==8192 and b[:4]==b'YKS1' and struct.unpack_from('<I',b,8188)[0]==kb.crc(b[:8188])
    @classmethod
    def latest(cls,a,b):
        if not cls.valid(a):return 1 if cls.valid(b) else -1
        if not cls.valid(b):return 0
        delta=(struct.unpack_from('<I',b,4)[0]-struct.unpack_from('<I',a,4)[0])&0xffffffff
        return 1 if 0<delta<0x80000000 else 0
    def test_interrupted_second_slot_never_loses_first(self):
        old=self.record(20);new=self.record(21,offset=100);blank=bytes(8192)
        for cut in range(8192):
            partial=new[:cut]+blank[cut:]
            self.assertEqual(self.latest(old,partial),0)
        self.assertEqual(self.latest(old,new),1)
    def test_wrap_corruption_and_two_bad_slots(self):
        self.assertEqual(self.latest(self.record(0xffffffff),self.record(0)),1)
        for at in (0,32,4096,8188):
            newer=self.record(2);newer[at]^=1
            self.assertEqual(self.latest(self.record(1),newer),0)
        self.assertEqual(self.latest(bytes(8192),bytes(8192)),-1)
if __name__=='__main__':unittest.main(verbosity=2)
