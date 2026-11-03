"""Integration contracts complement the production parser/worker/UI replays."""
from pathlib import Path
import json,re,tempfile,unittest,zipfile
from package_sd_resources import write_chiptune_archive
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(n):return (ROOT/n).read_text(encoding='utf-8')
class ChiptuneIntegration(unittest.TestCase):
    def test_on_demand_registration_and_sources(self):
        main=read('main/main.cpp');cmake=read('main/CMakeLists.txt')
        self.assertRegex(main,r'registerApp\("8Bit音乐", LV_SYMBOL_PLAY,\s*\[\] \{ return std::make_unique<apps::Chiptune>\(\); \},"chiptune"\)')
        for n in ('apps/chiptune.cpp','core/chiptune_player.cpp','core/chiptune_score.cpp'):self.assertEqual(cmake.count('"'+n+'"'),1)
        self.assertNotIn('chip::start(',main)
    def test_existing_audio_owners_respect_player(self):
        for n in ('game_audio','audio_recorder','fc_emulator'):
            self.assertRegex(read('main/core/'+n+'.cpp'),r'!((?:core::)?chip)::idle\(\)')
        player=read('main/core/chiptune_player.cpp')
        for token in ('!game_audio::idle()','!recorder::idle()','!fc::idle()'):self.assertIn(token,player)
    def test_sd_and_sleep_drain_include_new_jobs(self):
        self.assertIn('chip::storageIdle()',read('main/core/storage_browser.cpp'))
        self.assertEqual(read('main/core/power_mgr.cpp').count('!core::chip::idle()'),2)
        self.assertIn('core::chip::busy()',read('main/core/power_runtime.cpp'))
    def test_exit_cancels_and_frees_ui(self):
        source=read('main/apps/chiptune.cpp')
        close=source.split('void Chiptune::onHide()',1)[1].split('void Chiptune::onDestroy',1)[0]
        for token in ('listing->cancel.store(true)','player->cancel.store(true)','lv_timer_delete(timer)','listing.reset()','player.reset()','swap(entries)'):self.assertIn(token,close)
    def test_resources_match_archive_and_chiptune_directory(self):
        scores=sorted((ROOT/'../resources/sdcard/chiptune').glob('*.chip.json'));self.assertEqual(len(scores),7)
        sd=ROOT/'../resources/sdcard'
        with tempfile.TemporaryDirectory(prefix='yumo-chiptune-') as temporary:
            archive=Path(temporary)/'chiptune-samples.zip'
            write_chiptune_archive(sd,archive)
            with zipfile.ZipFile(archive) as z:
                paths=sorted(path for path in (sd/'chiptune').iterdir() if path.is_file())
                self.assertIsNone(z.testzip())
                self.assertEqual(sorted(z.namelist()),sorted('chiptune/'+path.name for path in paths))
                for path in paths:self.assertEqual(z.read('chiptune/'+path.name),path.read_bytes())
        for p in scores:
            value=json.loads(p.read_text(encoding='utf-8'));self.assertEqual(value['format'],'chip-score');self.assertEqual(value['version'],1);self.assertLessEqual(p.stat().st_size,65536)
        self.assertIn('"/sdcard/chiptune"',read('main/core/chiptune_player.hpp'))
        self.assertNotIn('/scores',read('main/core/chiptune_player.cpp'))
if __name__=='__main__':unittest.main()
