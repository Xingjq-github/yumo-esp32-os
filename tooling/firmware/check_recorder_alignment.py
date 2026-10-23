"""Recorder size/compact timer update; no hardware or real data access."""
from pathlib import Path
import hashlib,json,re,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
RUN=ROOT/'../tooling/firmware/fixtures/contracts/recorder-alignment-20260919-1918'
ALLOWED={'main/apps/settings_system.cpp','main/main.cpp','main/apps/recorder.cpp','main/ui/recorder_layout.hpp'}
def compact(path):return re.sub(r'\s+','',(ROOT/path).read_text(encoding='utf-8-sig'))
class RecorderAlignmentContracts(unittest.TestCase):
 def test_exact_scope_preserves_nes_recording_dsp_and_other_apps(self):
  before=json.loads((RUN/'before.json').read_text(encoding='utf-8'))['files'];changed=set()
  for path,old in before.items():
   self.assertTrue((ROOT/path).is_file(),path)
   if hashlib.sha256((ROOT/path).read_bytes()).hexdigest()!=old['sha256']:changed.add(path)
  self.assertEqual(changed,ALLOWED)
 def test_record_and_list_take_size_from_the_layout(self):
  s=compact('main/apps/recorder.cpp')
  self.assertIn('record=c::button(parent,"",click,this,layout::Record.w,layout::Record.h)',s)
  self.assertIn('list=c::panel(parent,layout::List.w,layout::List.h,true)',s)
 def test_timer_is_smaller_but_icons_and_meter_gap_are_preserved(self):
  s=compact('main/apps/recorder.cpp')
  for token in ('lv_obj_set_style_text_font(elapsed,&lv_font_montserrat_18,0)',
                'font=&lv_font_montserrat_24', 'lv_obj_set_size(recordDot,28,28)',
                'lv_obj_get_x(elapsed)+lv_obj_get_width(elapsed)+layout::MeterGap',
                'lv_obj_get_y(elapsed)+(lv_obj_get_height(elapsed)-12)/2'):
   self.assertIn(token,s)
  font=(ROOT/'../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_18.c').read_text(encoding='utf-8')
  height=int(re.search(r'\.line_height\s*=\s*(\d+)',font)[1])
  self.assertEqual(height,21)
  self.assertLessEqual(39+height+4,64)
 def test_both_edges_align_and_recovered_height_goes_to_the_list(self):
  s=compact('main/ui/recorder_layout.hpp')
  for token in ('Record{0,64,118,77}', 'Stop{119,64,121,40}', 'Rewind{119,105,39,36}',
                'Pause{159,105,40,36}', 'Forward{200,105,40,36}', 'List{0,142,240,98}',
                'Record.y==Stop.y','Record.y+Record.h==Forward.y+Forward.h',
                'List.y==Forward.y+Forward.h+1'):
   self.assertIn(token,s)
if __name__=='__main__':unittest.main(verbosity=2)
