"""Photo/NES source contracts and specification models; no firmware compilation or ROM execution."""
from pathlib import Path
import re, unittest

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path):return (ROOT/path).read_text(encoding='utf-8-sig')
def part(s,a,b):return s.split(a,1)[1].split(b,1)[0]

class PhotoFcContracts(unittest.TestCase):
    def test_png_filter_and_decoder_are_linked_without_global_lvgl_decoder(self):
        browser=read('main/core/storage_browser.cpp')
        accepted=part(browser,'bool accepted','void execute')
        for ext in ('.jpg','.jpeg','.png'):self.assertIn('!strcasecmp(ext,"'+ext+'")',accepted)
        cmake=read('main/CMakeLists.txt')
        for value in ('"core/photo_loader.cpp"','"core/photo_png.c"','espressif__libpng'):
            self.assertIn(value,cmake)
        self.assertNotIn('fopen(',read('main/apps/photo_viewer.cpp'))
        self.assertIn('# CONFIG_LV_USE_LIBPNG is not set',read('sdkconfig'))

    def test_png_transforms_alpha_palette_gray_16bit_and_interlace_are_bounded(self):
        s=read('main/core/photo_png.c')
        for name in ('png_set_scale_16','png_set_palette_to_rgb','png_set_expand_gray_1_2_4_to_8',
                     'png_set_tRNS_to_alpha','png_set_gray_to_rgb','png_set_background',
                     'png_set_gamma','png_set_interlace_handling','png_read_update_info'):
            self.assertIn(name,s)
        for value in ('MaxDimension=16384','MaxSourcePixels=64*1024*1024',
                      'MaxCodecBytes=512*1024','png_set_chunk_malloc_max(png,64*1024)',
                      'png_get_rowbytes(png,info)!=stride','bytes>r->length-r->offset',
                      'heap_caps_free(r->row)','PNG_PASS_START_COL(pass)','PNG_PASS_START_ROW(pass)',
                      'PNG_PASS_COL_OFFSET(pass)','PNG_PASS_ROW_OFFSET(pass)'):
            self.assertIn(value,s)
        self.assertNotRegex(s,r'\bnew\b|std::|fopen\(')
        # Error state lives on the heap; setjmp never unwinds C++ destructors.
        self.assertLess(s.index('if(setjmp(png_jmpbuf(png)))'),s.index('png_read_info'))
        self.assertIn('png_destroy_read_struct(&png,&info,NULL)',s)
        self.assertIn('if(cancelled && cancelled(context))',s)
        self.assertIn('photo_view_valid(view,w,h)',s)

    def test_photo_task_cancels_before_mount_and_closes_stream_before_completion(self):
        s=read('main/core/photo_loader.cpp');ui=read('main/apps/photo_viewer.cpp')
        self.assertNotRegex(s,r'\blv_\w+|PhotoViewer|WindowManager|void \*app')
        self.assertIn('job.cancelled.load() || sys::Power::sleepPending()',s)
        self.assertIn('active.compare_exchange_strong(expected,true)',s)
        self.assertIn('std::min<size_t>(4096',s)
        self.assertIn('job->sdAccess=sd::requestAccess()',s)
        self.assertIn('Source::~Source() {heap_caps_free(data);}',s)
        self.assertIn('Job::~Job() {heap_caps_free(pixels);}',s)
        self.assertIn('photo::idle()',read('main/core/storage_browser.cpp'))
        self.assertLess(s.index('job->done.store(true'),s.index('vTaskDeleteWithCaps'))
        self.assertIn('stopping(job) || !readSource(job,context,input)',s)
        close=part(s,'bool close()','bool failure(')
        self.assertLess(close.index('std::fclose(file)'),close.index('card.release()'))
        for token in ('MaxFileBytes=16*1024*1024','CacheBytes=256*1024',
                      'photo_png_read_view','photo_jpeg_read_view','input->finish()',
                      'input->close()','hash!=s.fingerprint','generation!=card.generation()',
                      'heap_caps_get_largest_free_block','ReserveBytes+CodecRoom'):
            self.assertIn(token,s)
        hide=part(ui,'void PhotoViewer::onHide','void PhotoViewer::showList')
        self.assertIn('releaseSource()',hide)
        self.assertIn('m_imageJob->cancelled.store(true);m_imageJob.reset()',ui)

    def test_fullscreen_view_and_failed_zoom_keep_pixels_until_success(self):
        ui=read('main/apps/photo_viewer.cpp')
        self.assertIn('kViewH = 240',ui)
        self.assertIn('lv_obj_align(m_imagePanel, LV_ALIGN_TOP_MID, 0, 0)',ui)
        self.assertIn('lv_obj_set_hidden(m_header,true)',ui)
        self.assertNotRegex(ui,r'm_zoomIn|m_zoomOut|loadJpeg|decodeAt')
        self.assertIn('lv_obj_set_hidden(m_header,false)',part(ui,'void PhotoViewer::showList','void PhotoViewer::openFile'))
        poll=part(ui,'void PhotoViewer::pollImage','void PhotoViewer::releaseImage')
        self.assertLess(poll.index('if(job->error || !job->pixels)'),poll.index('releaseImage();m_pixels=job->pixels'))
        self.assertIn('m_imageJob->done.load(std::memory_order_acquire)',poll)
        self.assertIn('job->pixels=nullptr',poll)
        self.assertLess(poll.index('if(m_pendingView &&'),poll.index('releaseImage();m_pixels=job->pixels'))
        self.assertIn('job->view.scaled_width!=m_requestedW',poll)
        self.assertIn('if(!m_pendingView){m_requestedW=m_dispW;',poll)
        self.assertIn('m_imageJob->cancelled.store(true)',ui)
        self.assertIn('LV_EVENT_RELEASED',ui)
        self.assertIn('LV_EVENT_PRESS_LOST',ui)

    def test_pwr_override_is_queue_independent_and_latches_press_owner(self):
        s=read('main/core/button_mgr.cpp');ui=read('main/apps/photo_viewer.cpp')
        route=part(s,'static void pwrCb','// Emergency PWR')
        self.assertIn('case BUTTON_PRESS_DOWN:',route)
        self.assertIn('s_pressedView=Power::isScreenOn()?s_viewSession.load',route)
        self.assertIn('if(!Power::isScreenOn()) Power::screenOn()',route)
        self.assertLess(route.index('if(Power::sleepPending()) Power::toggleScreen()'),
                        route.index('if(!Power::isScreenOn()) Power::screenOn()'))
        self.assertIn('else if(s_pressedView) s_viewExit.store(s_pressedView',route)
        self.assertIn('else if(!s_viewSession.load(std::memory_order_acquire)) Power::toggleScreen()',route)
        self.assertIn('case BUTTON_LONG_PRESS_START:',route)
        self.assertIn('Power::powerOff()',route)
        self.assertIn('s_systemPanel.load(std::memory_order_acquire)) EventBus::publish({EventType::BackRequested})',route)
        self.assertIn('const auto exit=s_viewExit.exchange(0,std::memory_order_acq_rel)',s)
        self.assertIn('!s_systemPanel.load(std::memory_order_acquire) && exit==session',s)
        self.assertIn('sys::Buttons::takeViewExit(self->m_powerSession)',ui)
        self.assertEqual(ui.count('releaseViewExit(m_powerSession);m_powerSession=0'),2)

    def test_rom_header_has_one_c_cpp_parser_and_specific_failures(self):
        app=read('main/core/fc_emulator.cpp');engine=read('components/nofrendo/nes/rom.c')
        model=read('main/core/fc_model.hpp');parser=read('components/nofrendo/nes/ines.h')
        for s in (app,engine,model):self.assertIn('ines_parse(',s)
        self.assertNotIn('(data[7]&0xf0)',engine)
        self.assertIn('rom.prg_rom_banks=prg/ROM_PRG_BANK_SIZE',engine)
        self.assertIn('rom.chr_rom=chr?data+offset+prg:NULL',engine)
        for name in ('INES_MAPPER','INES_SUBMAPPER','INES_CONSOLE','INES_RAM','INES_TIMING','INES_TRUNCATED','INES_SIZE'):
            self.assertIn(name,app);self.assertIn(name,parser)
        self.assertIn('if(info.submapper) return INES_SUBMAPPER',parser)
        self.assertIn('info.offset+info.prg+info.chr>size',parser)
        self.assertLess(app.index('ines_parse('),app.index('std::memcpy(rom.data'))
        ui=read('main/apps/fc_player.cpp')
        self.assertIn('else if(!loadError.empty())',ui)
        self.assertIn('loadError.clear()',part(ui,'void FcPlayer::refresh','void FcPlayer::play'))
        self.assertIn('loadError=error',part(ui,'if (job->done.load','if (!job->ready.load'))

    def test_every_enabled_mapper_links_once_and_new_mutable_state_resets(self):
        registry=read('components/nofrendo/mappers/mappers.h')
        ids=list(map(int,re.findall(r'&map(\d+)_intf',registry)))
        cmake=read('components/nofrendo/CMakeLists.txt')
        files=re.findall(r'"(mappers/[^\"]+\.c)"',cmake)
        implementations=[]
        for name in files:
            s=read('components/nofrendo/'+name)
            self.assertIn('GNU Library General',s)
            implementations.extend(map(int,re.findall(r'mapintf_t map(\d+)_intf\s*=',s)))
            # A symbol in the registry is insufficient if its actual board
            # number is wrong (the old map78 interface accidentally said 70).
            for symbol,number in re.findall(r'mapintf_t map(\d+)_intf\s*=\s*\{[^}]*?\.number\s*=\s*(\d+)',s):
                self.assertEqual(symbol,number,name)
        self.assertEqual(len(ids),35)
        for mapper in ids:self.assertEqual(implementations.count(mapper),1)
        # Unregistered MMC3 aliases and their unsafe ROM/RAM workaround are gone.
        self.assertEqual(set(implementations)-set(ids),set())
        for name,tokens in {
            'map018.c':('memset(&irq,0,sizeof(irq))','memset(lowprgnybbles','memset(highprgnybbles'),
            'map021.c':('select_c000=false','memset(lownybbles','memset(highnybbles'),
            'map023.c':('memset(lownybbles','memset(highnybbles'),
            'map032.c':('select_c000=0',),
            'map064.c':('command=0;vrombase=0',),
            'map075.c':('memset(latch,0,sizeof(latch));hibits=0',),
        }.items():
            s=read('components/nofrendo/mappers/'+name)
            init=part(s,'static void map_init','mapintf_t ')
            for token in tokens:self.assertIn(token,init)
            self.assertNotRegex(s,r'\.init\s*=\s*NULL')
        s=read('components/nofrendo/mappers/map206.c')
        self.assertIn('mmc_bankchr(2, 0x0800, (value & 0x3E) >> 1, CHR_ROM)',s)

    def test_joystick_scales_visuals_without_shrinking_hit_zone_or_deadzone(self):
        s=read('main/core/fc_controls.hpp')
        self.assertIn('StickX=33, StickY=33, Radius=33, ThumbRadius=22',s)
        self.assertIn('Quadrant=120',s)
        self.assertIn('if(dx==0 && dy==0) return 0',s)
        self.assertIn('if(ax*2>=ay)',s);self.assertIn('if(ay*2>=ax)',s)
        self.assertLessEqual(abs(66-100*2/3),1)
        self.assertEqual(44,66*2/3)

class PhotoSpecifications(unittest.TestCase):
    def test_adam7_sampling_covers_each_output_pixel_exactly_once(self):
        starts_x=(0,4,0,2,0,1,0);starts_y=(0,0,4,0,2,0,1)
        steps_x=(8,8,4,4,2,2,1);steps_y=(8,8,8,4,4,2,2)
        png=read('main/core/photo_png.c')
        self.assertIn('png_read_row(png,r->row,NULL)',png)
        self.assertNotRegex(png,r'heap_caps_\w+\(h,stride')
        for width,height in ((1,1),(1,17),(19,1),(3,5),(7,8),(8,7),(9,9),(31,17),(65,33)):
            for out_w,out_h in ((width,height),(1,1),(min(width,7),min(height,5))):
                seen=[[0]*out_w for _ in range(out_h)]
                pixels=[[None]*out_w for _ in range(out_h)]
                for pass_no in range(7):
                    for oy in range(out_h):
                        sy=(2*oy+1)*height//(2*out_h)
                        if sy<starts_y[pass_no] or (sy-starts_y[pass_no])%steps_y[pass_no]:continue
                        for ox in range(out_w):
                            sx=(2*ox+1)*width//(2*out_w)
                            if sx<starts_x[pass_no] or (sx-starts_x[pass_no])%steps_x[pass_no]:continue
                            seen[oy][ox]+=1;pixels[oy][ox]=(sx,sy)
                for oy in range(out_h):
                    for ox in range(out_w):
                        self.assertEqual(seen[oy][ox],1)
                        self.assertEqual(pixels[oy][ox],((2*ox+1)*width//(2*out_w),(2*oy+1)*height//(2*out_h)))

    def test_fit_and_row_sampling_never_overflow_or_read_beyond_image(self):
        for w,h in ((1,8192),(8192,1),(1,1),(17,19),(240,240),(4000,3000),(8192,8192)):
            fit=max(1,min(240,w,w*240//h));height=min(240,max(1,fit*h//w))
            self.assertLessEqual(fit,240);self.assertLessEqual(height,240)
            self.assertLessEqual(height,h);self.assertLessEqual(fit,w)
            rows=[((2*y+1)*h)//(2*height) for y in range(height)]
            columns=[((2*x+1)*w)//(2*fit) for x in range(fit)]
            self.assertEqual(len(rows),len(set(rows)))
            self.assertTrue(all(0<=i<h for i in rows))
            self.assertTrue(all(0<=i<w for i in columns))

    def test_cancelled_old_result_never_replaces_new_view_and_pwr_never_toggles_old_owner(self):
        # Exhaust the owner/press/screen state combinations independently of C++.
        for current in (0,1,2):
            for pressed in (0,1,2):
                for screen_on in (False,True):
                    action='wake' if not screen_on else 'exit' if pressed else 'off' if not current else 'consume'
                    if current:self.assertNotEqual(action,'off')
                    if pressed and screen_on:self.assertEqual(action,'exit')
                    deliver=action=='exit' and current==pressed and current!=0
                    if pressed!=current:self.assertFalse(deliver)
        ui=read('main/apps/photo_viewer.cpp')
        self.assertIn('if(!m_viewing || !m_keySession || job->cancelled.load()) return',ui)

if __name__=='__main__':unittest.main(verbosity=2)
