// Production Apps + compact widgets on real LVGL. Hardware and I/O are fakes;
// the separate decoder/engine tests execute the real storage algorithms.
#include "lvgl.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "ui/theme.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/control_states.h"
#include "core/event_bus.hpp"
#include "core/button_mgr.hpp"
#include "core/screenshot.hpp"
#include "core/charset_conv.hpp"
#include "apps/text_paging.hpp"
#include "src/misc/lv_text_private.h"
#define private public
#include "apps/file_manager.hpp"
#include "apps/photo_viewer.hpp"
#include "apps/text_reader.hpp"
#undef private
static unsigned checks=0,home=0,sequence=0;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace fake {
bool screenshot=false,input=false;uint32_t pageKeys=0,viewExit=0,exitRequest=0;
std::map<uint32_t,std::pair<sys::EventType,sys::EventHandler>> handlers;
std::shared_ptr<core::files::Job> files;
std::shared_ptr<core::gif::Job> gif;
}
namespace sys {
struct Power {static bool isScreenOn(){return true;}static bool sleepPending(){return false;}};
uint32_t EventBus::subscribe(EventType t,EventHandler h){fake::handlers[++sequence]={t,h};return sequence;}
void EventBus::unsubscribe(uint32_t n){fake::handlers.erase(n);}
void EventBus::publish(const Event &event){if(event.type==EventType::HomeRequested)++home;for(auto &h:fake::handlers)if(h.second.first==event.type)h.second.second(event);}
uint32_t Buttons::claimPageKeys(){return fake::pageKeys=++sequence;}
void Buttons::releasePageKeys(uint32_t n){if(n==fake::pageKeys)fake::pageKeys=0;}
uint32_t Buttons::claimViewExit(){return fake::viewExit=++sequence;}
void Buttons::releaseViewExit(uint32_t n){if(n==fake::viewExit)fake::viewExit=0;}
bool Buttons::takeViewExit(uint32_t n){if(n && n==fake::exitRequest){fake::exitRequest=0;return true;}return false;}
void Screenshot::setViewBlocked(bool on){fake::screenshot=on;}
}
namespace ui {
void TextInput::hide(){fake::input=false;}void TextInput::show(lv_obj_t *){fake::input=true;}
bool TextInput::visible(){return fake::input;}void TextInput::bindField(lv_obj_t *){}
}
namespace core {
std::string ensureUtf8(const std::string &s){return s;}
std::string parentDirectory(const std::string &s){return s.substr(0,std::max(size_t(7),s.find_last_of('/')));}
const char *storageError(int){return "读取失败";}
std::shared_ptr<StorageJob> listDirectory(const std::string &,bool) {
    auto job=std::make_shared<StorageJob>();job->entries={{"/sdcard/one.gif","one.gif",false},{"/sdcard/not.jpg","not.jpg",false},{"/sdcard/two.GIF","two.GIF",false},{"/sdcard/folder","folder",true}};job->done=true;return job;
}
std::shared_ptr<StorageJob> readText(const std::string &path) {
 auto job=std::make_shared<StorageJob>();job->path=path;job->text=true;
 for(int i=0;i<50;++i)job->content+="fixture line\n";job->done=true;return job;
}
std::shared_ptr<StorageJob> saveTextProgress(const std::string &path,reading::Mark mark,uint64_t g) {
 auto job=std::make_shared<StorageJob>();job->path=path;job->progress=mark;job->cardGeneration=g;job->done=true;return job;
}
namespace photo {
Source::~Source(){std::free(data);}Job::~Job(){std::free(pixels);}
bool idle(){return true;}
std::shared_ptr<Job> start(const std::string &,std::shared_ptr<Source>,uint16_t){return {};}
}
namespace gif {
Job::~Job(){for(auto p:pixels)std::free(p);}
bool idle(){return true;}
bool isFile(const std::string &s){auto e=s.substr(s.size()>4?s.size()-4:0);return e==".gif"||e==".GIF";}
std::shared_ptr<Job> start(const std::string &path){auto job=std::make_shared<Job>();job->path=path;job->width=240;job->height=120;for(auto &p:job->pixels)p=static_cast<uint16_t *>(std::calloc(240*120,2));fake::gif=job;return job;}
}
namespace files {
bool idle(){return true;}
std::shared_ptr<Job> start(Request r,uint64_t g){auto job=std::make_shared<Job>();job->request=std::move(r);job->generation=g;fake::files=job;return job;}
const char *errorText(int n){return n?"失败":"操作完成";}
}
}
static void heap_caps_free(void *p){std::free(p);}
static tm *localtime_r(const time_t *n,tm *t){return localtime_s(t,n)==0?t:nullptr;}
#include "compact_ui.inc"
#include "file_manager.inc"
#include "photo_viewer.inc"
#include "text_reader.inc"
static uint8_t framebuffer[240*240*2];
static void advance(unsigned ms=20){lv_tick_inc(ms);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static lv_obj_t *find(lv_obj_t *p,const char *name){
 for(unsigned i=0;i<lv_obj_get_child_count(p);++i){auto c=lv_obj_get_child(p,i);if(lv_obj_check_type(c,&lv_label_class)&&!std::strcmp(lv_label_get_text(c),name))return p;if(auto r=find(c,name))return r;}return nullptr;
}
static void click(lv_obj_t *p){CHECK(p);CHECK(!lv_obj_has_state(p,LV_STATE_DISABLED));lv_obj_send_event(p,LV_EVENT_PRESSED,nullptr);lv_obj_send_event(p,LV_EVENT_RELEASED,nullptr);lv_obj_send_event(p,LV_EVENT_CLICKED,nullptr);advance();}
static unsigned timers(){unsigned n=0;for(auto t=lv_timer_get_next(nullptr);t;t=lv_timer_get_next(t))++n;return n;}
static size_t freeMemory(){lv_mem_monitor_t m{};lv_mem_monitor(&m);return m.free_size;}
static lv_obj_t *root(){auto r=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(r);lv_obj_set_pos(r,0,0);lv_obj_set_size(r,240,240);return r;}
static void finishList(apps::FileManager &app,uint64_t generation=7){
 auto job=app.m_job;CHECK(job&&job->request.operation==core::files::Operation::List);
 job->generation=generation;job->result.entries={{"recordings",true,0},{"notes.txt",false,256},{"测试目录",true,0}};
 job->done=true;app.poll();advance();CHECK(app.m_page==apps::FileManager::Page::List);
}
static void capture(const char *name){advance();lv_refr_now(nullptr);FILE *f=std::fopen(name,"wb");CHECK(f);std::fprintf(f,"P6\n240 240\n255\n");for(size_t i=0;i<sizeof(framebuffer);i+=2){unsigned c=framebuffer[i]|unsigned(framebuffer[i+1])<<8;unsigned char rgb[]={static_cast<unsigned char>((c>>11)*255/31),static_cast<unsigned char>(((c>>5)&63)*255/63),static_cast<unsigned char>((c&31)*255/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);}

static unsigned objectCount(lv_obj_t *p) {
 unsigned n=1;for(unsigned i=0;i<lv_obj_get_child_count(p);++i)n+=objectCount(lv_obj_get_child(p,i));return n;
}
template<class App> static void virtualList(bool photo) {
 auto r=root();App app;app.setRoot(r);app.onCreate(r);app.onShow();
 const unsigned widgets=objectCount(app.m_list);
 CHECK(widgets<=21);
 for(unsigned count:{0U,1U,10U,256U}) {
  app.scanFiles();auto loading=app.m_job;CHECK(loading);loading->entries.clear();
  for(unsigned i=0;i<count;++i) {
   char name[32];std::snprintf(name,sizeof(name),"file-%03u.%s",i,photo?"jpg":"txt");
   loading->entries.push_back({std::string("/sdcard/")+name,name,false});
  }
  loading->truncated=count==256;app.pollStorage();advance();
  CHECK(app.m_paths.size()==count);CHECK(objectCount(app.m_list)==widgets);
  CHECK(lv_obj_get_scroll_y(app.m_list)==0);
  if(!count)continue;
  const int pitch=ui::VirtualFileList::RowHeight+ui::theme::kGap;
  for(int offset=0;offset<int(count)*pitch;offset+=19) {
   lv_obj_scroll_to_y(app.m_list,offset,LV_ANIM_OFF);advance(1);
   const int scroll=lv_obj_get_scroll_y(app.m_list);
   CHECK(objectCount(app.m_list)==widgets);
   for(unsigned i=0;i<count;++i) {
    if(int(i)*pitch+38<=scroll || int(i)*pitch>=scroll+203)continue;
    bool found=false;
    for(auto &row:app.m_fileList.rows_)if(row.index==i) {
     CHECK(!lv_obj_is_hidden(row.button));
     CHECK(lv_obj_get_y(row.button)==int(i)*pitch);
     CHECK(app.m_names[i]==lv_label_get_text(row.label));found=true;
    }
    CHECK(found);
   }
  }
  lv_obj_scroll_to_y(app.m_list,count>10?pitch*100:0,LV_ANIM_OFF);advance();
  const int anchor=lv_obj_get_scroll_y(app.m_list);
  auto &row=app.m_fileList.rows_[count>10?1:0];
  const auto index=row.index;CHECK(index<count);
  click(row.button);CHECK(app.m_viewing);
  app.onBack();advance();CHECK(!app.m_viewing);
  CHECK(lv_obj_get_scroll_y(app.m_list)==anchor);
  if(count>10){
   auto button=app.m_fileList.rows_[1].button;
   lv_obj_send_event(button,LV_EVENT_PRESSED,nullptr);
   lv_obj_scroll_to_y(app.m_list,0,LV_ANIM_OFF);advance();
   lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);CHECK(!app.m_viewing);
   lv_obj_send_event(button,LV_EVENT_PRESSED,nullptr);app.scanFiles();
   lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);CHECK(!app.m_viewing);
  }
 }
 app.scanFiles();auto cancelled=app.m_job;app.onHide();CHECK(cancelled->cancelled);
 app.onDestroy();lv_obj_delete(r);advance();
}

int main(){
 lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
 lv_display_set_buffers(display,framebuffer,nullptr,sizeof(framebuffer),LV_DISPLAY_RENDER_MODE_FULL);
 lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *){lv_display_flush_ready(d);});
 ui::theme::init(10);advance();auto baseTimers=timers();size_t baseline=0;
 for(int theme:{4,5,6,8,10})for(int cycle=0;cycle<8;++cycle){
  ui::theme::init(theme);
  {
   auto r=root();apps::FileManager app;app.setRoot(r);app.onCreate(r);app.onShow();finishList(app);
   CHECK(app.m_generation==7);CHECK(!app.m_job);CHECK(app.allowsQuickPanel());
   if(theme==10&&!cycle)capture("files-list.ppm");
   app.select(1,false);advance();CHECK(app.m_page==apps::FileManager::Page::Actions);
   click(find(r,"属性"));CHECK(app.m_job && app.m_job->request.operation==core::files::Operation::Properties);
   CHECK(app.m_job->request.source=="/sdcard/notes.txt" && app.m_job->generation==7);
   app.m_job->result.properties={true,false,true,256,1,0,1790841600};app.m_job->done=true;app.poll();advance();
   CHECK(app.m_page==apps::FileManager::Page::Properties && !app.m_job && app.allowsQuickPanel());
   CHECK(find(r,"256 字节") && find(r,"/sdcard/notes.txt") && find(r,"修改时间"));
   if(theme==10&&!cycle)capture("files-properties.ppm");
   CHECK(app.onBack());advance();CHECK(app.m_page==apps::FileManager::Page::Actions);
   click(find(r,"属性"));auto reading=app.m_job;click(find(r,"取消"));CHECK(reading->cancelled);
   reading->result.error=ECANCELED;reading->done=true;app.poll();advance();CHECK(app.m_page==apps::FileManager::Page::Actions);
   app.action(apps::FileManager::Action::Cancel);app.select(0,true);click(find(r,"属性"));
   app.m_job->result.properties={true,true,false,512,2,1,0};app.m_job->result.error=E2BIG;
   app.m_job->done=true;app.poll();advance();CHECK(app.m_page==apps::FileManager::Page::Properties);
   CHECK(find(r,"已统计大小") && find(r,"2 个文件 / 1 个文件夹"));
   app.action(apps::FileManager::Action::Refresh);CHECK(app.m_job->request.operation==core::files::Operation::Properties);
   app.m_job->result.error=ENODEV;app.m_job->done=true;app.poll();advance();CHECK(app.m_page==apps::FileManager::Page::Actions);
   app.action(apps::FileManager::Action::Cancel);app.select(1,false);advance();
   click(find(r,"复制"));CHECK(app.m_clipboard=="/sdcard/notes.txt");
   app.select(0,false);finishList(app);CHECK(app.m_directory=="/sdcard/recordings");
   click(find(r,"粘贴"));CHECK(app.m_job->request.target=="/sdcard/recordings/notes.txt");CHECK(!app.allowsQuickPanel());
   app.m_job->done=true;app.poll();finishList(app);
   click(find(r,LV_SYMBOL_PLUS " 文件夹"));CHECK(app.m_input);CHECK(lv_obj_get_height(app.m_input)==36);
   lv_textarea_set_text(app.m_input,"new-folder");if(theme==10&&!cycle)capture("files-name.ppm");
   click(find(r,"确定"));CHECK(app.m_job->request.operation==core::files::Operation::Mkdir);CHECK(app.m_job->request.source=="/sdcard/recordings/new-folder");
   app.m_job->done=true;app.poll();finishList(app);
   app.select(1,false);click(find(r,"删除"));CHECK(!app.m_job);CHECK(app.m_page==apps::FileManager::Page::Delete);
   if(theme==10&&!cycle)capture("files-delete.ppm");click(find(r,"取消"));CHECK(!app.m_job);
   app.select(1,false);click(find(r,"删除"));click(find(r,"删除"));CHECK(app.m_job->request.operation==core::files::Operation::Remove);
   auto deleting=app.m_job;app.onHide();CHECK(deleting->cancelled);app.onDestroy();lv_obj_delete(r);fake::files.reset();advance();
  }
  {
   auto r=root();apps::FileManager app;app.setRoot(r);app.onCreate(r);app.onShow();finishList(app);
   app.select(0,true);click(find(r,"属性"));auto reading=app.m_job;app.onHide();CHECK(reading->cancelled);
   app.onDestroy();lv_obj_delete(r);reading->done=true;fake::files.reset();advance();
  }
  CHECK(timers()==baseTimers);CHECK(!fake::input);
  {
   auto r=root();apps::PhotoViewer app;app.setRoot(r);app.onCreate(r);app.onShow();advance(120);CHECK(app.m_paths.size()==4);
   app.openFile(0);app.pollGif();CHECK(app.m_gifMode&&app.m_viewing);CHECK(fake::screenshot&&fake::pageKeys&&fake::viewExit);CHECK(!app.allowsQuickPanel());
   CHECK(lv_obj_is_hidden(app.m_header));CHECK(lv_obj_is_hidden(app.m_list));
   auto first=app.m_gifJob;first->delayMs=20;first->ready=0;app.pollGif();advance(1);CHECK(first->displayed==0&&first->ready==-1);
   CHECK(app.m_imageDsc.data==reinterpret_cast<uint8_t *>(first->pixels[0]));CHECK(lv_obj_get_y(app.m_image)==60);CHECK(!app.m_draggable);
   first->delayMs=30;first->ready=1;app.pollGif();CHECK(first->displayed==0&&first->ready==1);
   advance(18);app.pollGif();CHECK(first->displayed==0&&first->ready==1);
   advance(1);app.pollGif();CHECK(first->displayed==1&&first->ready==-1);
   CHECK(app.m_imageDsc.data==reinterpret_cast<uint8_t *>(first->pixels[1]));
   first->delayMs=20;first->ready=0;advance(29);app.pollGif();CHECK(first->displayed==1&&first->ready==0);
   advance(1);app.pollGif();CHECK(first->displayed==0&&first->ready==-1);
   // Queued next frame is cancelled/detached when changing files or leaving.
   first->delayMs=100;first->ready=1;
   app.zoom(1);CHECK(!app.m_imageJob); // no static-image zoom path for GIFs
   sys::EventBus::publish({sys::EventType::NextPage,int32_t(app.m_keySession)});CHECK(first->cancelled&&app.m_gifIndex==2);app.pollGif();
   CHECK(app.m_gifJob->path=="/sdcard/two.GIF");
   sys::EventBus::publish({sys::EventType::PreviousPage,int32_t(app.m_keySession)});CHECK(app.m_gifIndex==0);app.pollGif();
   auto last=app.m_gifJob;fake::exitRequest=app.m_powerSession;advance(30);CHECK(!app.m_viewing&&!app.m_gifMode&&last->cancelled);
   CHECK(!fake::screenshot&&!fake::viewExit);CHECK(app.allowsQuickPanel());CHECK(!lv_obj_is_hidden(app.m_header));
   app.openFile(2);app.pollGif();auto closing=app.m_gifJob;app.onHide();CHECK(closing->cancelled);app.onDestroy();lv_obj_delete(r);fake::gif.reset();advance();
  }
  virtualList<apps::PhotoViewer>(true);virtualList<apps::TextReader>(false);
  CHECK(timers()==baseTimers);CHECK(fake::handlers.empty());CHECK(!fake::pageKeys&&!fake::viewExit&&!fake::screenshot);
  advance(500);lv_image_cache_drop(nullptr);
  if(cycle==1)baseline=freeMemory();
  if(cycle>1 && freeMemory()!=baseline)std::fprintf(stderr,"theme=%d cycle=%d heap=%llu base=%llu\n",theme,cycle,(unsigned long long)freeMemory(),(unsigned long long)baseline);
  if(cycle>1)CHECK(freeMemory()==baseline);
 }
 std::printf("{\"passed\":true,\"checks\":%u,\"theme_cycles\":40,\"timer_and_heap_restored\":true}\n",checks);
 lv_deinit();return 0;
}
