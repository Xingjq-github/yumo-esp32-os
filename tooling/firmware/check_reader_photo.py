"""Reader/photo lifecycle contracts; host specifications, not a touch/device test."""
from pathlib import Path
import re
import unittest
from check_menu_layout import font_metrics
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def part(s,a,b):return s.split(a,1)[1].split(b,1)[0]

class ReaderPhotoContracts(unittest.TestCase):
    def test_reading_uses_twelve_complete_rows_and_keeps_character_coverage(self):
        source=read('main/apps/text_reader.cpp')
        reader=part(source,'m_reader = lv_obj_create(parent);','m_pageNotice=')
        self.assertIn('lv_obj_remove_style_all(m_reader)',reader)
        self.assertIn('ui::theme::kContentW, ui::theme::kScreenH',reader)
        self.assertIn('ui::theme::fontReading()',reader)
        self.assertIn('LV_ALIGN_TOP_LEFT, 0, 0',reader)
        paging=part(source,'void TextReader::paginate()','void TextReader::saveProgress()')
        self.assertIn('lv_obj_get_content_width(m_text)',paging)
        self.assertIn('lv_obj_get_content_height(m_reader)',paging)
        self.assertIn('lv_obj_get_style_text_font(m_text,LV_PART_MAIN)',paging)
        self.assertIn('lv_text_get_next_line',paging)
        font=read('main/ui/lv_font_reading_15.c')
        height=int(re.search(r'\.line_height\s*=\s*(\d+)',font)[1])
        baseline=int(re.search(r'\.base_line\s*=\s*(\d+)',font)[1])
        self.assertEqual(height,20)
        self.assertEqual(240//height,12)
        glyphs=font_metrics('main/ui/lv_font_reading_15.c')
        self.assertEqual(set(glyphs),set(font_metrics('main/ui/lv_font_cjk_16.c')))
        for cp,glyph in glyphs.items():
            if glyph['box_h']==0: continue
            top=height-baseline-glyph['box_h']-glyph['ofs_y']
            self.assertGreaterEqual(top,0,hex(cp))
            self.assertLessEqual(top+glyph['box_h'],height,hex(cp))
        self.assertIn('fontReading(void) { return &lv_font_reading_15; }',read('main/ui/theme.cpp'))
        self.assertIn('"ui/lv_font_reading_15.c"',read('main/CMakeLists.txt'))

    def test_swipe_is_local_and_does_not_trigger_global_back_while_reading(self):
        header=read('main/apps/text_reader.hpp');source=read('main/apps/text_reader.cpp')
        self.assertIn('handlesHorizontalGesture() const override { return true; }',header)
        self.assertIn('lv_indev_wait_release(indev)',source)
        gesture=part(source,'lv_obj_add_event_cb(parent,','},LV_EVENT_GESTURE,this)')
        self.assertIn('dir==LV_DIR_LEFT || dir==LV_DIR_TOP',gesture)
        self.assertIn('dir==LV_DIR_RIGHT || dir==LV_DIR_BOTTOM',gesture)
        self.assertIn('self->turnPage(next?1:-1)',gesture)
        self.assertIn('else if(dir==LV_DIR_LEFT || dir==LV_DIR_RIGHT)',gesture)
        self.assertIn('sys::EventBus::publish({sys::EventType::BackRequested})',gesture)
        self.assertIn('lv_obj_set_gesture_bubble(parent,false)',source)
        self.assertNotIn('LV_DIR_VER',part(source,'m_reader = lv_obj_create','m_text ='))

    def test_reader_power_exit_is_scoped_to_document_and_keeps_idle_mailbox_alive(self):
        source=read('main/apps/text_reader.cpp')
        opening=part(source,'void TextReader::openFile','void TextReader::turnPage')
        self.assertLess(opening.index('if(m_directories[index])'),opening.index('claimViewExit()'))
        self.assertIn('sys::Buttons::takeViewExit(self->m_powerSession)',source)
        self.assertEqual(source.count('releaseViewExit(m_powerSession);m_powerSession=0'),2)
        for start,end in (('void TextReader::showList','void TextReader::openFile'),
                          ('void TextReader::onHide','void TextReader::onDestroy')):
            body=part(source,start,end)
            self.assertIn('releaseViewExit(m_powerSession)',body)
            self.assertIn('hidePageNotice()',body)
        poll=part(source,'void TextReader::pollStorage','bool TextReader::onBack')
        self.assertIn('if(!m_viewing && !m_saveJob && m_poll) lv_timer_pause(m_poll)',poll)
        self.assertIn('if(job->error) { showList();',poll)
        self.assertNotIn('Power::toggleScreen()',source)

    def test_reader_notice_is_transient_and_reuses_the_storage_poll_timer(self):
        source=read('main/apps/text_reader.cpp')
        self.assertEqual(source.count('lv_timer_create('),1)
        self.assertIn('kNoticeHoldMs=1000, kNoticeFadeMs=500',source)
        notice=part(source,'void TextReader::showPageNotice','void TextReader::onHide')
        self.assertIn('m_noticeSince=lv_tick_get();m_noticeVisible=true',notice)
        self.assertIn('lv_tick_elaps(m_noticeSince)',notice)
        self.assertIn('elapsed>=kNoticeHoldMs+kNoticeFadeMs',notice)
        self.assertIn('!sys::Power::isScreenOn() || sys::Power::sleepPending()',notice)
        self.assertIn('lv_timer_set_period(m_poll,kPollMs)',notice)
        self.assertIn('lv_obj_set_style_text_color(m_pageNotice,lv_color_white(),0)',source)
        self.assertIn('lv_obj_set_style_bg_color(m_pageNotice,lv_color_black(),0)',source)
        self.assertIn('lv_obj_set_clickable(m_pageNotice,false)',source)
        self.assertIn('lv_obj_set_scrollable(m_pageNotice,false)',source)
        self.assertIn('lv_timer_delete(m_poll)',source)

    def test_saving_keeps_no_ui_pointer_and_idle_includes_final_flush(self):
        storage=read('main/core/storage_browser.cpp')
        writer=part(storage,'void progressWorker','bool accepted')
        for token in ('sys::Power::WorkGuard storageWork','writeProgress(*job)',
                      'job->done.store(true,std::memory_order_release)','progressCurrent.reset()',
                      '--active','vTaskDeleteWithCaps(nullptr)'):
            self.assertIn(token,writer)
        self.assertLess(writer.index('writeProgress(*job)'),writer.index('--active'))
        self.assertNotRegex(writer,r'TextReader|lv_\w+|void \*arg')
        hide=part(read('main/apps/text_reader.cpp'),'void TextReader::onHide','void TextReader::onDestroy')
        self.assertIn('m_saveJob.reset()',hide)
        self.assertNotIn('m_saveJob->cancelled',hide)
        self.assertIn('core::storageIdle()',read('main/core/power_mgr.cpp'))

    def test_updates_coalesce_before_bounded_queue_and_read_pending_first(self):
        s=read('main/core/storage_browser.cpp')
        save=part(s,'std::shared_ptr<StorageJob> saveTextProgress','std::shared_ptr<StorageJob> listMedia')
        self.assertLess(save.index('pending=job;return job'),save.index('progressQueue.size()>=8'))
        self.assertLess(save.index('progressRunning=true;++active'),save.index('xTaskCreateWithCaps'))
        self.assertIn('std::lock_guard<std::mutex> lock(progressMutex)',save)
        restore=part(s,'reading::Mark readProgress','void writeProgress')
        self.assertLess(restore.index('progressQueue.rbegin()'),restore.index('progressCurrent->path==path'))
        self.assertLess(restore.index('progressCurrent->path==path'),restore.index('fopen('))
        self.assertIn('if(job.progress.fingerprint!=hash) job.progress={hash,1,0}',s)

    def test_bookmark_is_sibling_checked_two_slot_file_not_program_flash(self):
        s=read('main/core/storage_browser.cpp')
        path=part(s,'std::string progressPath','void readRecords')
        self.assertIn('path.substr(0,slash)+suffix',path)
        self.assertIn('/.yumo-read-%016llx.bin',path)
        write=part(s,'void writeProgress','void progressWorker')
        for token in ('"r+b"','errno==ENOENT','"w+b"','reading::latest',
                      'fseek(file,current==0?reading::RecordStride:0,SEEK_SET)','fflush(file)','fsync(fileno(file))','fclose(file)'):
            self.assertIn(token,write)
        self.assertNotRegex(write,r'nvs_|esp_partition_|remove\(|rename\(')
        self.assertIn('static_assert(checkedRecords()',read('../tooling/firmware/text_reader_test.cpp'))
        self.assertIn('RecordStride=512',read('main/core/text_progress.hpp'))

    def test_photo_keys_use_session_and_cannot_change_volume_or_decode_when_closed(self):
        s=read('main/apps/photo_viewer.cpp')
        self.assertEqual(s.count('event.i32==static_cast<int32_t>(m_keySession)'),2)
        for token in ('claimPageKeys()','releasePageKeys(m_keySession);m_keySession=0',
                      'EventBus::unsubscribe(m_previousSub)','EventBus::unsubscribe(m_nextSub)',
                      'lv_timer_pause(m_poll)','lv_timer_delete(m_poll)'):
            self.assertIn(token,s)
        zoom=part(s,'void PhotoViewer::zoom(int direction)','void PhotoViewer::imagePressCb')
        for token in ('!m_keySession','!m_viewing','m_job','!m_pixels','!m_source',
                      '!sys::Power::isScreenOn()','sys::Power::sleepPending()',
                      'if(next!=m_requestedW)applyScale'):
            self.assertIn(token,zoom)
        submit=part(s,'void PhotoViewer::submitView()','void PhotoViewer::applyScale')
        for token in ('m_imageJob','!core::photo::idle()','!m_pendingView','sys::Power::sleepPending()'):self.assertIn(token,submit)
        self.assertNotRegex(s,r'SystemService::setVolume|Settings::volume|lv_button_create')
        self.assertIn('if(!indev || self->m_viewing) return',s)

    def test_eight_modes_and_requested_chinese_choice_order(self):
        s=read('main/apps/randomizer.cpp')
        self.assertIn('Lottery, Coin, Outcome',read('main/apps/randomizer.hpp'))
        self.assertIn('m_mode==Mode::Coin?2:m_mode==Mode::Outcome?5',s)
        self.assertIn('coin[]={"正","反"}',s)
        self.assertIn('outcome[]={"胜","平","负","让平","让负"}',s)
        self.assertIn('Outcome, RockPaperScissors',read('main/apps/randomizer.hpp'))
        self.assertIn('m_mode==Mode::RockPaperScissors?3',s)
        self.assertIn('hands[]={"石头","剪刀","布"}',s)
        self.assertIn('胜负\\n石头剪刀布',s)
        self.assertIn('const bool adjustable=m_mode==Mode::Letters || m_mode==Mode::Choices',s)
        self.assertIn('lv_obj_set_style_text_font(label,ui::theme::fontCjkFull(),0)',s)
        self.assertIn('m_totalSteps=count*3+model::below(count,rng)+1',s)

    def test_dense_pagination_and_chinese_controls_fit_real_fonts(self):
        small=font_metrics('../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_12.c')
        width=lambda text:sum((small[ord(c)]['adv_w']+8)//16 for c in text)
        self.assertLessEqual(width('9999/9999'),70)
        cjk=font_metrics('main/ui/lv_font_cjk_16.c')
        for label in ('缩小','放大','上一页','下一页','让平','让负','硬币','胜负','石头','剪刀','布'):
            self.assertLessEqual(sum((cjk[ord(c)]['adv_w']+8)//16 for c in label),64)
        reader=read('main/apps/text_reader.cpp')
        self.assertIn('m_pages.size()>=1000?&lv_font_montserrat_12',reader)
        self.assertIn("if(end>begin && m_content[end-1]=='\\n') --end",reader)

if __name__=='__main__':unittest.main(verbosity=2)
