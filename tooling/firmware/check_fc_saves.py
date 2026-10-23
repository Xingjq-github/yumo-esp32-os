"""Checkpoint lifecycle/UI integration; machine/file behavior runs in native C."""
from pathlib import Path
import re,unittest
from check_menu_layout import font_metrics
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(p):return (ROOT/p).read_text(encoding='utf-8')
class FcSaves(unittest.TestCase):
    def test_worker_owns_io_and_releases_before_completion(self):
        s=read('main/core/fc_saves.cpp');ui=read('main/apps/fc_player_saves.cpp')
        self.assertNotRegex(ui,r'fopen|fwrite|checkpoint_load|xTaskCreate|sd::Session')
        self.assertIn('sd::Session card(s.access)',s)
        self.assertIn('card.generation()!=s.generation',s)
        self.assertIn('s.access=sd::requestAccess(true)',s)
        self.assertLess(s.index('if(!job.ready.load'),s.index('s.stage.store(SaveStage::Pending'))
        self.assertLess(s.index('// No file handles'),s.rindex('s.stage.store(SaveStage::Complete'))
        self.assertIn('heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)',s)
        worker=read('main/core/fc_emulator.cpp')
        paused=worker.split('if (job.paused.load()) {',1)[1].split('sys::Power::WorkGuard work;',1)[0]
        self.assertLess(paused.index('sys::Audio::closeStream()'),paused.index('processSave('))
        self.assertEqual(worker.count('xTaskCreateWithCaps'),1)
        self.assertIn('++playedFrames',worker)
        self.assertIn('playedFrames=result.frames',s)

    def test_loading_returns_to_menu_and_blocks_shortcuts_during_subpage(self):
        ui=read('main/apps/fc_player_saves.cpp');base=read('main/apps/fc_player.cpp')
        self.assertIn('if(loaded) { closeSaves();showMenu(); }',ui)
        self.assertNotIn('resumeGame()',ui)
        self.assertIn('if(!job || savePanel || saveBusy ||',base)
        self.assertIn('if(self->savePanel) self->closeSaves();else self->stopGame()',base)
        self.assertIn('if(saveBusy && job)job->saves.cancelled.store(true);',ui)
        self.assertIn('c::disabled(saveBack,false)',ui)
        self.assertIn('if(!savePanel) {',ui)
        self.assertIn('if(savePanel) { closeSaves();return true; }',base)
        self.assertIn('if(self->saveSlots[self->saveSelection].exists) self->confirmSave()',ui)
        self.assertIn('!saveSlots[saveSelection].valid',ui)
        self.assertIn('else saveListReady=false;',ui)

    def test_three_manual_and_independent_auto_slots_fit_240(self):
        ui=read('main/apps/fc_player_saves.cpp')
        for value in ('FC_SAVE_MANUAL_SLOTS=3','FC_SAVE_AUTO_SLOT=3','FC_SAVE_SLOTS=4'):
            self.assertIn(value,read('main/core/fc_save_store.h'))
        self.assertIn('order==0?unsigned(FC_SAVE_AUTO_SLOT):order-1',ui)
        self.assertIn('saveSelection>=FC_SAVE_MANUAL_SLOTS',ui)
        self.assertIn('action==SaveAction::Save && slot>=FC_SAVE_MANUAL_SLOTS',read('main/core/fc_saves.cpp'))
        self.assertIn('240,158,true',ui)
        self.assertIn('this,236,38',ui)
        self.assertEqual(4*38+3*2,158)
        for x,y,w,h in [(0,0,36,36),(204,0,36,36),(0,38,240,158),(0,200,118,40),(122,200,118,40)]:
            self.assertLessEqual(x+w,240);self.assertLessEqual(y+h,240)
        glyphs=font_metrics('main/ui/lv_font_cjk_16.c')
        for p in ['main/apps/fc_player_saves.cpp','main/core/fc_saves.cpp']:
            for ch in {ch for ch in read(p) if '\u4e00'<=ch<='\u9fff'}:self.assertIn(ord(ch),glyphs,ch)

    def test_two_generation_sync_and_changed_selection_checked_before_capture(self):
        s=read('main/core/fc_save_store.c')
        write=s.split('bool fc_save_write(',1)[1]
        self.assertLess(write.index('fc_save_same(&found,expected)'),write.index('s->capture('))
        self.assertLess(write.index('s->capture('),write.index('fopen(path,"wb")'))
        self.assertIn('expected->bank^1',write)
        self.assertLess(write.index('fflush(file)'),write.index('fsync(fileno(file))'))
        self.assertLess(write.index('close_file(s,file,true)'),write.index('!inspect(s,slot,bank,result)'))
        self.assertIn('memcmp(h+56,s->rom,32)',s)
        self.assertIn('get(h+92,4)!=checkpoint_crc(h,92)',s)

    def test_all_stateful_mappers_have_complete_checkpoint_and_no_raw_pointer_dumps(self):
        files=re.findall(r'"(mappers/[^\"]+\.c)"',read('components/nofrendo/CMakeLists.txt'))
        stateful={'001','004','009','010','016','018','021','023','024','032','064','065','070','073','074','075','078','206'}
        for f in files:
            if Path(f).stem[3:] in stateful:self.assertIn('.checkpoint = map_checkpoint',read('components/nofrendo/'+f))
        vrc=read('components/nofrendo/mappers/map024.c').split('static void map_checkpoint',1)[1]
        self.assertNotIn('CHECKPOINT_FIELD(io, vrc.cart)',vrc)
        codec=read('components/nofrendo/nes/checkpoint.c')
        self.assertIn('page.offset>r[page.region].length-size',codec)
        self.assertIn('if (!checkpoint_valid(n,data,length)) return false;',codec)
        self.assertNotRegex(codec,r'memcpy\([^;]*sizeof\((nes_t|nes6502_t|ppu_t|apu_t|mem_t)\)')
        self.assertFalse((ROOT/'components/nofrendo/nes/state.h').exists())
if __name__=='__main__':unittest.main(verbosity=2)
