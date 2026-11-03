"""Display-only FC correction; preserve the emulator, save ABI and input path."""
from pathlib import Path
import hashlib, json, unittest

ROOT=Path(__file__).resolve().parents[2]
CONTRACT=ROOT/'tooling/firmware/fixtures/contracts/fc-rendering-20261022.json'
NATIVE=ROOT/'tooling/firmware/fixtures/contracts/fc-native-pixels-20261022.json'

def assert_timing_contract(test, data, historical_sha):
    timing=json.loads(CONTRACT.read_text(encoding='utf-8'))['files']['firmware/main/core/fc_timing.hpp']
    test.assertEqual(historical_sha,timing['before_sha256'])
    test.assertEqual(hashlib.sha256(data).hexdigest(),timing['sha256'])

class FcRenderingContracts(unittest.TestCase):
    def test_rendering_delta_and_unchanged_engine_save_input_paths(self):
        contract=json.loads(CONTRACT.read_text(encoding='utf-8'))
        native=json.loads(NATIVE.read_text(encoding='utf-8'))
        self.assertEqual(set(contract['changed']),{'firmware/main/core/fc_timing.hpp','firmware/main/core/fc_video.hpp'})
        self.assertEqual(set(native['files']),{'firmware/main/core/fc_video.hpp','firmware/main/apps/fc_player.cpp'})
        for name,record in contract['files'].items():
            self.assertEqual(record['sha256']!=record['before_sha256'],name in contract['changed'],name)
            expected=record['sha256']
            if name in native['files']:
                update=native['files'][name]
                self.assertEqual(update['before_sha256'],expected,name)
                self.assertNotEqual(update['sha256'],expected,name)
                expected=update['sha256']
            data=(ROOT/name).read_bytes()
            if name in ('firmware/sdkconfig','firmware/sdkconfig.defaults'):
                # Current configuration includes the separately verified Xiaozhi
                # components; all existing option values are frozen by its delta.
                from check_auto_power import assert_config
                assert_config(self,name.split('/',1)[1])
                continue
            if name=='firmware/main/core/fc_emulator.cpp':
                # The new foreground AI audio reservation adds only this include
                # and start guard. Removing precisely both must reproduce the
                # recorded engine/save/input implementation byte for byte.
                for delta in (b'#include "xiaozhi_service.hpp"\n',b'!core::xiaozhi::idle() || '):
                    self.assertEqual(data.count(delta),1,name)
                    data=data.replace(delta,b'',1)
            self.assertEqual(hashlib.sha256(data).hexdigest(),expected,name)

    def test_both_native_rates_remain_covered_by_target_compiler(self):
        model=(ROOT/'tooling/firmware/fc_performance_model_test.cpp').read_text(encoding='utf-8')
        for name in ('exactNativeRate(50)','exactNativeRate(60)','bothPhasesSurvive(50)','bothPhasesSurvive(60)','nativeViewportIsExact()'):
            self.assertIn('static_assert('+name+')',model)

    def test_replay_uses_production_ppu_timing_and_scaler(self):
        runner=(ROOT/'tooling/firmware/test_fc_rendering.py').read_text(encoding='utf-8')
        c=(ROOT/'tooling/firmware/native/fc_rendering_runtime.c').read_text(encoding='utf-8')
        cpp=(ROOT/'tooling/firmware/native/fc_rendering_schedule.cpp').read_text(encoding='utf-8')
        self.assertIn("engine/'CMakeLists.txt'",runner)
        self.assertIn('ppu_renderline(video, y, draw)',c)
        for token in ('core/fc_timing.hpp','core/fc_video.hpp','EXPECT_OLD','EXPECT_AREA','rotatingOam()','missingLines()','timing.presented(start)','rejectEvery','reference(','exactPixels()'):
            self.assertIn(token,cpp)

    def test_image_is_centered_and_live_and_restored_frames_share_conversion(self):
        player=(ROOT/'firmware/main/apps/fc_player.cpp').read_text(encoding='utf-8')
        setup=player.split('void FcPlayer::play(',1)[1].split('void FcPlayer::createMenu()',1)[0]
        self.assertIn('lv_obj_center(image)',setup)
        self.assertNotIn('lv_obj_set_pos(image',setup)
        self.assertNotIn('lv_image_set_scale',setup)
        for token in ('descriptor.header.w = core::fc::Width','descriptor.header.h = core::fc::Height','descriptor.header.stride = core::fc::Width * 2','descriptor.data_size = core::fc::Pixels * 2'):
            self.assertIn(token,setup)
        worker=(ROOT/'firmware/main/core/fc_emulator.cpp').read_text(encoding='utf-8')
        self.assertEqual(worker.count('scaleFrame(job.frame,video.data,NES_SCREEN_PITCH,NES_SCREEN_OVERDRAW,colors)'),2)

if __name__=='__main__':unittest.main(verbosity=2)
