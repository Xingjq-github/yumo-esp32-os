"""Source contracts, actual font metrics and layout/probability models.

These checks do not execute C++ or replace compilation/device acceptance.
"""
from collections import Counter
from pathlib import Path
import itertools
import re
import unittest
from check_menu_layout import font_metrics

ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(path): return (ROOT/path).read_text(encoding='utf-8-sig')
def part(text,start,end): return text.split(start,1)[1].split(end,1)[0]

class TextRandomContracts(unittest.TestCase):
    def test_button_task_only_queues_and_keeps_volume_fallback_and_home(self):
        source=read('main/core/button_mgr.cpp')
        self.assertNotRegex(source,r'\blv_\w+\s*\(|TextReader|WindowManager')
        self.assertIn('std::atomic<uint32_t> s_pageSession{0}',source)
        for name,delta in [('PreviousPage',-5),('NextPage',5)]:
            self.assertIn(f'if(!routePageKey(EventType::{name})) changeVolume({delta})',source)
        route=part(source,'static bool routePageKey','static void changeVolume')
        self.assertLess(route.index('if(!session) return false'),route.index('EventBus::publish'))
        self.assertIn('return true;',route) # queue-full never becomes a volume click
        self.assertIn('EventBus::publish({EventType::HomeRequested})',source)
        pwr=part(source,'static void pwrCb','static esp_err_t createButton')
        for token in ('Power::toggleScreen()', 'Power::powerOff()', 'kHoldMs   = 3000'):
            self.assertIn(token,pwr)

    def test_reader_events_cannot_reach_a_closed_or_new_session(self):
        source=read('main/apps/text_reader.cpp')
        self.assertEqual(source.count('event.i32==static_cast<int32_t>(m_keySession)'),2)
        hide=part(source,'void TextReader::onHide','void TextReader::onDestroy')
        self.assertLess(hide.index('releasePageKeys(m_keySession); m_keySession=0'),hide.index('m_job->cancelled'))
        for name in ('m_previousSub','m_nextSub'):
            self.assertIn(f'EventBus::unsubscribe({name}); {name}=0',source)
        button=read('main/core/button_mgr.cpp')
        self.assertIn('generation>=INT32_MAX?1:generation+1',button)
        self.assertIn('compare_exchange_strong(session,0',button)
        guard=part(source,'void TextReader::turnPage','void TextReader::showTextPage')
        for token in ('!m_keySession','!m_viewing','m_job','m_pages.empty()',
                      '!sys::Power::isScreenOn()','sys::Power::sleepPending()',
                      'm_page>0','m_page+1<m_pages.size()'):
            self.assertIn(token,guard)

    def test_reader_idle_poll_stops_and_closed_document_frees_buffers(self):
        source=read('main/apps/text_reader.cpp')
        poll=part(source,'void TextReader::pollStorage','bool TextReader::onBack')
        self.assertIn('if(!m_viewing && !m_saveJob && m_poll) lv_timer_pause(m_poll)',poll)
        self.assertIn('m_saveJob->done.load(std::memory_order_acquire)',poll)
        listing=part(source,'void TextReader::showList','void TextReader::openFile')
        for token in ('std::string().swap(m_content)','std::vector<size_t>().swap(m_pages)',
                      'lv_label_set_text(m_text,"")','if(!m_job && !m_saveJob && m_poll) lv_timer_pause(m_poll)'):
            self.assertIn(token,listing)

    def test_random_app_is_lazy_and_only_animation_owns_a_timer(self):
        source=read('main/apps/randomizer.cpp')
        self.assertIn('std::make_unique<apps::Randomizer>()',read('main/main.cpp'))
        self.assertIn('"apps/randomizer.cpp"',read('main/CMakeLists.txt'))
        self.assertEqual(source.count('lv_timer_create('),1)
        self.assertEqual(source.count('lv_timer_resume(m_timer)'),2) # Same timer: normal draw or independent PK sides.
        self.assertNotRegex(source,r'xTaskCreate|esp_wifi_|bootloader_random_enable|Settings::|nvs_|fopen')
        self.assertIn('EventType::ScreenOff',source)
        self.assertIn('!m_visible || !sys::Power::isScreenOn() || sys::Power::sleepPending()',source)
        for name,stop in [('finish','lv_timer_pause(m_timer)'),('onHide','lv_timer_pause(m_timer)'),
                          ('onDestroy','lv_timer_delete(m_timer)')]:
            body=source.split(f'void Randomizer::{name}()',1)[1].split('\n}',1)[0]
            self.assertIn(stop,body)
        self.assertIn('EventBus::unsubscribe(m_screenOffSub)',source)
        self.assertIn('if(!m_running) return;',source)
        self.assertIn('m_keySession=sys::Buttons::claimPageKeys()',source)
        self.assertEqual(source.count('event.i32==static_cast<int32_t>(m_keySession)'),2)
        self.assertIn('if(m_pkMode) startPk(1);else start()',source)
        self.assertIn('if(m_pkMode) startPk(2);else start()',source)
        hide=part(source,'void Randomizer::onHide','void Randomizer::onDestroy')
        self.assertLess(hide.index('releasePageKeys(m_keySession); m_keySession=0'),hide.index('cancelAnimation()'))
        for name in ('m_previousSub','m_nextSub'):
            self.assertIn(f'EventBus::unsubscribe({name}); {name}=0',source)
        start=part(source,'void Randomizer::start','void Randomizer::tick')
        self.assertIn('!m_visible || m_running || !sys::Power::isScreenOn() || sys::Power::sleepPending()',start)

    def test_input_uses_shared_keyboard_and_checks_range_before_start(self):
        source=read('main/apps/randomizer.cpp')
        self.assertIn('m_body=c::panel(parent,model::Width,model::BodyHeight,true)',source)
        for field in ('m_lowField','m_highField'):
            self.assertRegex(source,rf'{field}=c::field\([^;]+,7\)')
        self.assertIn('lv_textarea_set_accepted_chars(field,"0123456789-")',source)
        start=part(source,'void Randomizer::start','void Randomizer::tick')
        self.assertLess(start.index('model::parseInteger'),start.index('m_running=true'))
        self.assertLess(start.index('if(m_low>m_high)'),start.index('model::integer'))
        self.assertIn('ui::TextInput::hide()',start)
        self.assertIn('lockControls(true)',start)
        parse=part(read('main/apps/random_model.hpp'),'inline bool parseInteger','// Rejection')
        self.assertIn("if(*text<'0' || *text>'9') return false",parse)
        self.assertIn('if(value>(MaxInteger-digit)/10) return false',parse)
        self.assertIn('if(!*text) return false',parse)

    def test_grid_geometry_for_every_candidate_count(self):
        model=read('main/apps/random_model.hpp')
        self.assertIn('columns=count<=3?count:count==4?2:count<=9?3:4',model)
        self.assertIn('inRow=remaining<columns?remaining:columns',model)
        # Independent specification: all tracks partition their row, no overlap.
        for count in range(2,17):
            cols=count if count<=3 else 2 if count==4 else 3 if count<=9 else 4
            rows=(count+cols-1)//cols
            height=162 if count==6 else 125
            rects=[]
            for row in range(rows):
                n=min(cols,count-row*cols); usable_w=240-(n-1); usable_h=height-(rows-1)
                y=usable_h*row//rows+row; h=usable_h*(row+1)//rows+row-y
                current=[]
                for col in range(n):
                    x=usable_w*col//n+col; w=usable_w*(col+1)//n+col-x
                    self.assertGreaterEqual(w,59); self.assertGreaterEqual(h,30)
                    current.append((x,y,w,h))
                self.assertEqual(current[0][0],0)
                self.assertEqual(current[-1][0]+current[-1][2],240)
                self.assertLessEqual(max(c[2] for c in current)-min(c[2] for c in current),1)
                rects+=current
            self.assertEqual(len(rects),count)
            self.assertEqual(rects[-1][1]+rects[-1][3],height)
            for a,b in itertools.combinations(rects,2):
                self.assertTrue(a[0]+a[2]<=b[0] or b[0]+b[2]<=a[0] or a[1]+a[3]<=b[1] or b[1]+b[3]<=a[1])

    def test_dice_masks_and_chase_final_positions(self):
        model=read('main/apps/random_model.hpp')
        masks=[int(x,16) for x in re.search(r'DicePips=\{([^}]+)\}',model)[1].split(',')]
        self.assertEqual([mask.bit_count() for mask in masks],list(range(1,7)))
        self.assertTrue(all(mask<1<<9 for mask in masks))
        source=read('main/apps/randomizer.cpp')
        self.assertIn('m_totalSteps=count*3+model::below(count,rng)+1',source)
        self.assertIn('m_cursor=(m_step-1)%choiceCount()',source)
        for count in range(2,17):
            for chosen in range(count):
                ticks=count*3+chosen+1
                self.assertEqual((ticks-1)%count,chosen)
        self.assertIn('final?ui::theme::accent()',source)

    def test_rejection_sampling_has_equal_buckets(self):
        model=read('main/apps/random_model.hpp')
        self.assertIn('threshold=(uint32_t{0}-count)%count',model)
        self.assertIn('while(value<threshold)',model)
        # Exhaust a smaller word size: the accepted domain has equal buckets.
        for count in range(2,34):
            threshold=(1<<12)%count
            buckets=Counter(value%count for value in range(threshold,1<<12))
            self.assertEqual(len(buckets),count)
            self.assertEqual(len(set(buckets.values())),1)
            self.assertEqual(((1<<32)-((1<<32)%count))%count,0)

    def test_lottery_without_replacement_sort_and_sequential_reveal(self):
        model=read('main/apps/random_model.hpp')
        for token in ('RedCount=6, RedMax=33, BlueMax=16','i+below(RedMax-i,rng)',
                      'std::swap(pool[i],pool[next])','std::sort(result.red.begin(),result.red.end())',
                      'result.blue=1+below(BlueMax,rng)'):
            self.assertIn(token,model)
        source=read('main/apps/randomizer.cpp')
        self.assertIn('m_totalSteps=7',source)
        self.assertIn('const unsigned index=m_step-1',source)
        self.assertIn('index<model::RedCount?0xC52D44:0x2469B7',source) # semantic red/blue, independent of theme
        cancel=part(source,'void Randomizer::cancelAnimation','void Randomizer::modeCb')
        self.assertIn('lv_label_set_text(m_ballLabels[i],"--")',cancel)
        # Every legal sequence of six swaps removes a distinct entry. Exercise
        # both ends at each step rather than relying on a probabilistic sample.
        for ends in itertools.product((False,True),repeat=6):
            pool=list(range(1,34)); reds=[]
            for i,last in enumerate(ends):
                j=32 if last else i
                pool[i],pool[j]=pool[j],pool[i]; reds.append(pool[i])
            self.assertEqual(len(set(reds)),6)
            self.assertTrue(all(1<=n<=33 for n in reds))

    def test_real_fonts_cover_new_chinese_and_largest_number(self):
        cjk=font_metrics('main/ui/lv_font_cjk_16.c')
        source=read('main/apps/randomizer.cpp')+read('main/apps/text_reader.cpp')
        for ch in set(re.findall('[\u4e00-\u9fff]',source)):
            self.assertIn(ord(ch),cjk,ch)
        digits=font_metrics('../dependencies/managed_components/lvgl__lvgl/src/font/lv_font_montserrat_48.c')
        width=lambda ch:(digits[ord(ch)]['adv_w']+8)//16
        self.assertLessEqual(width('-')+6*max(width(ch) for ch in '0123456789'),240)
        self.assertLessEqual(max(digits[ord(ch)]['box_h'] for ch in '-0123456789'),77)

    def test_new_lvgl_calls_exist_in_local_headers(self):
        headers='\n'.join(p.read_text(encoding='utf-8',errors='replace') for p in
                          (ROOT/'../dependencies/managed_components/lvgl__lvgl').rglob('*.h'))
        for call in set(re.findall(r'\b(lv_\w+)\s*\(',read('main/apps/randomizer.cpp'))):
            self.assertRegex(headers,rf'\b{call}\b',call)

if __name__=='__main__': unittest.main(verbosity=2)
