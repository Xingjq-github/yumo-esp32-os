"""Guard the learning package, renderer bounds and app integration without I/O on a device."""
from pathlib import Path
import json,re,struct,sys,unittest,zlib
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
sys.path.insert(0,str(ROOT/'../resources/knowledge'))
from pattern_game import LESSONS
class PatternGame(unittest.TestCase):
    def test_course_coverage_and_authored_metadata(self):
        self.assertEqual(len(LESSONS),26)
        self.assertEqual([sum(x['group']==i for x in LESSONS) for i in range(4)],[3,5,7,11])
        self.assertEqual(len({x['id'] for x in LESSONS}),26)
        packed=json.loads((ROOT/'../artifacts/resources/patterns/lessons.json').read_text(encoding='utf-8'))
        for source,lesson in zip(LESSONS,packed):
            for key,value in source.items():self.assertEqual(lesson[key],value)
            self.assertEqual(len(lesson['runs']),6)
            self.assertEqual(len({(r['output'],tuple(r['trace'])) for r in lesson['runs'][:3]}),3)
            for run in lesson['runs']:
                self.assertLessEqual(len(run['trace']),12)
                for call in run['trace']:self.assertLessEqual(len(call.encode()),160)
    def test_package_offsets_crc_and_embedded_copy(self):
        raw=(ROOT/'../resources/sdcard/patterns/lessons.ypg').read_bytes()
        self.assertEqual(raw[:8],b'YMPAT01\0')
        count,crc=struct.unpack_from('<II',raw,8);self.assertEqual(count,26)
        self.assertEqual(zlib.crc32(raw[16:432]),crc)
        expected=json.loads((ROOT/'../artifacts/resources/patterns/lessons.json').read_text(encoding='utf-8'))
        end=432
        for i in range(count):
            off,size,crc,index=struct.unpack_from('<IIII',raw,16+i*16)
            self.assertEqual(off,end);self.assertEqual(index,i);self.assertLessEqual(size,24576)
            data=raw[off:off+size];self.assertEqual(zlib.crc32(data),crc)
            self.assertEqual(json.loads(data),expected[i]);end=off+size
        self.assertEqual(end,len(raw))
        cpp=(ROOT/'main/core/pattern_data.cpp').read_text(encoding='utf-8')
        self.assertEqual(bytes(int(v,16) for v in re.findall(r'0x([0-9a-f]{2})',cpp)),raw)
    def test_java_snippets_match_delivered_source(self):
        source=(ROOT/'../resources/knowledge/PatternExamples.java').read_bytes()
        self.assertFalse((ROOT/'../resources/sdcard/patterns/PatternExamples.java').exists())
        snippets=dict(re.findall(r'// @(\S+)\n(.*?)(?=    // @)',source.decode(),re.S))
        for x in json.loads((ROOT/'../artifacts/resources/patterns/lessons.json').read_text(encoding='utf-8')):
            self.assertEqual(x['code'],snippets[x['id']].strip())
    def test_app_registered_once_and_settings_last(self):
        source=(ROOT/'main/main.cpp').read_text(encoding='utf-8')
        names=re.findall(r'registerApp\(\s*"([^"]+)"',source)
        self.assertEqual(names.count('设计模式'),1);self.assertEqual(names[-1],'设置')
        self.assertEqual(names[names.index('编程速查')+1],'设计模式')
        cmake=(ROOT/'main/CMakeLists.txt').read_text()
        for file in ['apps/design_patterns.cpp','core/pattern_format.cpp','core/pattern_store.cpp','core/pattern_data.cpp']:
            self.assertEqual(cmake.count('"'+file+'"'),1)
        self.assertIn('patterns::idle()', (ROOT/'main/core/storage_browser.cpp').read_text())
    def test_compact_font_covers_course_text(self):
        from update_menu_font import read_font
        glyphs=read_font((ROOT/'main/ui/lv_font_menu_14.c').read_text(encoding='utf-8'))
        def strings(x):
            if isinstance(x,str):yield x
            elif isinstance(x,list):
                for v in x:yield from strings(v)
            elif isinstance(x,dict):
                for v in x.values():yield from strings(v)
        for text in strings(LESSONS):
            for c in text:
                if ord(c)>127 and not c.isspace():self.assertIn(ord(c),glyphs,c)
if __name__=='__main__':unittest.main()
