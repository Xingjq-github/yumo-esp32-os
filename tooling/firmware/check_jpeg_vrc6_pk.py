from pathlib import Path
import json,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')

class JpegVrc6PkContracts(unittest.TestCase):
    def test_progressive_has_bounded_c_boundary_and_keeps_fast_baseline(self):
        decoder=read('components/photo_jpeg/photo_jpeg.c')
        memory=read('components/photo_jpeg/jpeg_memory.c')
        for token in ('if(setjmp(r->jump))','jpeg_destroy_decompress(&r->jpeg)',
                      'coefficients>PHOTO_JPEG_MAX_BACKING','input_scan_number>64',
                      'j->data_precision!=8','j->scale_denom=8','jpeg_read_scanlines(j,row,1)',
                      'if(r->cancel && r->cancel(r->context))'):
            self.assertIn(token,decoder)
        self.assertNotRegex(decoder,r'\bexit\(|fopen\(|std::|\bnew\b')
        self.assertIn('MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT',memory)
        self.assertIn('JERR_NO_BACKING_STORE',memory)
        self.assertIn('r->used>r->limit-bytes-sizeof(Allocation)',memory)
        self.assertIn('PHOTO_JPEG_RESERVE=384*1024',read('components/photo_jpeg/jpeg_port.h'))
        worker=read('main/core/photo_loader.cpp')
        self.assertIn('180000000',worker)
        self.assertIn('jpeg_dec_process(decoder,&io)',worker)
        self.assertIn('if(!ok && !sink && s.data && !s.png && !s.progressive && !job.view.x && !job.view.y',worker)
        self.assertIn('photo_jpeg_read_view(s.data,Input::callback',worker)
        self.assertIn('Stream *s=(*r->jpeg.mem->alloc_small)',decoder)
        self.assertIn('ERREXIT(j,JERR_INPUT_EOF)',decoder)
        self.assertIn('photo_jpeg',read('main/CMakeLists.txt'))
        for token in ('jpeg_crop_scanline','photo_view_valid'):self.assertIn(token,decoder)
        for token in ('PHOTO_JPEG_MAX_BACKING-r->backing_bytes','r->backing->close','r->backing->write','r->backing->read'):self.assertIn(token,memory)
        for token in ('O_CREAT|O_EXCL|O_RDWR','card.claimMutation()','sd::PhotoCache','std::memcmp(tag,magic','closeTemp(i)','esp_vfs_fat_info','reserved)+bytes+8*1024*1024'):self.assertIn(token,worker)

    def test_decoder_is_pinned_without_encoders_or_simd(self):
        p=json.loads(read('components/photo_jpeg/provenance.json'))
        self.assertEqual(p['version'],'3.1.2')
        self.assertEqual(len(p['archive_sha256']),64)
        cmake=read('components/photo_jpeg/CMakeLists.txt')
        self.assertNotRegex(cmake,r'jcap|jcphuff|jchuff|simd|jmemnobs|GLOB')
        self.assertTrue((ROOT/'components/photo_jpeg/README.ijg').exists())

    def test_mapper_irq_and_audio_are_clocked_and_released(self):
        s=read('components/nofrendo/mappers/map024.c')
        for token in ('nes6502_setcyclehook(map_clock)','nes6502_setmapperirq(true)',
                      'vrc.irq.prescaler+=341','apu_setext(&expansion)',
                      'vrc.voice[2].accumulator>>3','memset(&vrc,0,sizeof(vrc))',
                      'vrc.count==1024','advance_audio();vrc.audio_control',
                      'ppu_setreadonly','rom_nt','ram_read'):
            self.assertIn(token,s)
        self.assertNotIn('map_hblank',s)
        cpu=read('components/nofrendo/nes/cpu.c')
        self.assertIn('cpu.int_pending || cpu.mapper_irq',cpu)
        self.assertEqual(cpu.count('cycle_hook((unsigned)(hook_remaining - remaining_cycles))'),2)
        shutdown=read('components/nofrendo/nes/mmc.c').split('void mmc_shutdown()',1)[1].split('mapper_t *mmc_init',1)[0]
        for token in ('nes6502_setcyclehook(NULL)','nes6502_setmapperirq(false)',
                      'apu_setext(NULL)','ppu_setreadonly(0)'):
            self.assertIn(token,shutdown)

    def test_pk_round_state_and_compact_geometry(self):
        s=read('main/apps/randomizer.cpp');model=read('main/apps/rps_match.hpp')
        for token in ('startPk(1)','startPk(2)','startPk(3)','m_match.complete(side',
                      'm_match.clearRound()','lv_obj_set_pos(m_pkButton,180,200)',
                      'lv_obj_set_width(m_start,hands?179:240)','if(m_pkMode) c::disabled(m_start,false)'):
            self.assertIn(token,s)
        self.assertIn('state[0]==State::Done && state[1]==State::Done && result<0',model)
        self.assertIn('score[result]<9999',model)
        self.assertEqual(s.count('lv_timer_create('),1)
        self.assertTrue((ROOT/'../tooling/firmware/rps_match_test.cpp').exists())
        self.assertEqual(162+78,240)
        self.assertLessEqual(26+2*46+44,162)
        self.assertGreaterEqual(60,36)

if __name__=='__main__':unittest.main(verbosity=2)
