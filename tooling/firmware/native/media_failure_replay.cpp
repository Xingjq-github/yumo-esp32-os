// Replay the production error transitions; hardware, widgets and jobs are fakes.
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "core/storage_browser.hpp"
#include "core/photo_loader.hpp"
#include "core/gif_player.hpp"
#include "apps/text_paging.hpp"
static unsigned checks=0;
#define CHECK(c) do{++checks;if(!(c)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
struct lv_point_t {int32_t x=0,y=0;};
struct lv_image_dsc_t {struct{unsigned magic=0,cf=0,w=0,h=0,stride=0;}header;size_t data_size=0;const uint8_t *data=nullptr;};
struct lv_obj_t {bool hidden=false;unsigned writes=0;std::string text;};
struct lv_timer_t {unsigned period=100;bool paused=true;};
struct lv_event_t {};
namespace fake {
uint32_t tick=1,sequence=0,viewExit=0;
unsigned claims=0,releases=0,listStarts=0,textStarts=0,gifStarts=0,decodedText=0;
bool nullList=false,nullText=false,nullGif=false,gifBusy=false,screenshot=false;
const uint8_t *displayed=nullptr;
std::set<const void *> pixels;
}
static void lv_timer_set_period(lv_timer_t *p,unsigned n){p->period=n;}
static void lv_timer_pause(lv_timer_t *p){p->paused=true;}
static void lv_timer_resume(lv_timer_t *p){p->paused=false;}
static uint32_t lv_tick_get(){return fake::tick;}
static void lv_obj_set_hidden(lv_obj_t *p,bool hidden){p->hidden=hidden;}
static void lv_obj_center(lv_obj_t *){}
static void lv_obj_invalidate(lv_obj_t *){}
static void lv_obj_set_width(lv_obj_t *,int){}
static void lv_obj_set_style_text_font(lv_obj_t *,int,int){}
static void lv_obj_set_style_text_color(lv_obj_t *,int,int){}
static void lv_label_set_text(lv_obj_t *p,const char *s){p->text=s;++p->writes;}
static void lv_image_cache_drop(lv_image_dsc_t *){}
static void lv_image_set_src(lv_obj_t *,const lv_image_dsc_t *d){fake::displayed=d?d->data:nullptr;}
static void heap_caps_free(void *p){std::free(p);}
#define LV_IMAGE_HEADER_MAGIC 1
#define LV_COLOR_FORMAT_RGB565 2
#define LV_SYMBOL_IMAGE "image"
#define LV_SYMBOL_FILE "file"
namespace ui {
namespace theme {static int fontSmall(){return 0;}static int fg(){return 0;}}
struct VirtualFileList {
    std::string notice;
    size_t count=0;
    unsigned writes=0;
    void reset(){notice.clear();count=0;}
    void show(size_t n,const char *){count=n;notice.clear();}
    void message(const char *s){notice=s;count=0;++writes;}
};
}
namespace sys {
namespace power {enum{None=0,StayLit=1,LowLoad=2,Dim=4};}
struct Buttons {
    static uint32_t claimViewExit(){CHECK(!fake::viewExit);++fake::claims;return fake::viewExit=++fake::sequence;}
    static void releaseViewExit(uint32_t n){if(n){CHECK(fake::viewExit==n);fake::viewExit=0;++fake::releases;}}
};
struct Screenshot {static void setViewBlocked(bool b){fake::screenshot=b;}};
}
namespace core {
std::string ensureUtf8(const std::string &s){return s;}
std::string parentDirectory(const std::string &s){return s.substr(0,std::max(size_t(7),s.find_last_of('/')));}
const char *storageError(int error){CHECK(error==ENOMEM);return "memory allocation failed";}
std::shared_ptr<StorageJob> listDirectory(const std::string &,bool photo){
    ++fake::listStarts;if(fake::nullList)return {};
    auto p=std::make_shared<StorageJob>();p->photos=photo;
    p->entries=photo?std::vector<FileEntry>{{"/sdcard/first.gif","first.gif",false},{"/sdcard/second.gif","second.gif",false}}:
        std::vector<FileEntry>{{"/sdcard/read.txt","read.txt",false}};
    p->done=true;return p;
}
std::shared_ptr<StorageJob> readText(const std::string &path){
    ++fake::textStarts;if(fake::nullText)return {};
    auto p=std::make_shared<StorageJob>();p->path=path;p->text=true;p->content="test text";p->done=true;return p;
}
namespace photo {
Source::~Source(){std::free(data);}Job::~Job(){std::free(pixels);}
bool idle(){return true;}
}
namespace gif {
Job::~Job(){for(auto p:pixels)if(p){CHECK(reinterpret_cast<const uint8_t *>(p)!=fake::displayed);CHECK(fake::pixels.erase(p)==1);std::free(p);}}
bool idle(){return !fake::gifBusy;}
bool isFile(const std::string &s){return s.size()>=4 && s.substr(s.size()-4)==".gif";}
std::shared_ptr<Job> start(const std::string &path){
    ++fake::gifStarts;if(fake::nullGif)return {};
    auto p=std::make_shared<Job>();p->path=path;p->width=p->height=2;
    for(auto &pixels:p->pixels){pixels=static_cast<uint16_t *>(std::calloc(4,2));CHECK(pixels);fake::pixels.insert(pixels);}
    return p;
}
}
}
namespace apps {constexpr uint32_t kPollMs=100;}
#include "photo_viewer_header.inc"
#include "text_reader_header.inc"
#include "photo_viewer_methods.inc"
#include "text_reader_methods.inc"
// Paging and rendering are outside these failure-path tests.
namespace apps {
void TextReader::paginate(){m_pages={0};}
void TextReader::showTextPage(){++fake::decodedText;}
void TextReader::showPageNotice(bool){}
}
struct Widgets {
    lv_obj_t header,title,back,refresh,notice,list,panel,image,text;
    lv_timer_t timer;
    void attach(apps::PhotoViewer &app){
        app.m_header=&header;app.m_title=&title;app.m_back=&back;app.m_refresh=&refresh;
        app.m_notice=&notice;app.m_list=&list;app.m_imagePanel=&panel;app.m_image=&image;app.m_poll=&timer;
    }
    void attach(apps::TextReader &app){
        app.m_header=&header;app.m_title=&title;app.m_back=&back;app.m_refresh=&refresh;
        app.m_pageNotice=&notice;app.m_list=&list;app.m_reader=&panel;app.m_text=&text;app.m_poll=&timer;
    }
};
template<class App> static void failedList(bool photo){
    App app;Widgets ui;ui.attach(app);
    // Cover replacement/cancellation as well as the first null submission.
    app.scanFiles();auto previous=app.m_job;CHECK(previous);
    fake::nullList=true;app.scanFiles();CHECK(previous->cancelled);
    CHECK(!app.m_job&&!app.m_viewing&&!fake::viewExit);
    CHECK(app.m_scanError=="memory allocation failed");CHECK(app.m_fileList.notice==app.m_scanError);
    CHECK(ui.timer.paused&&!ui.header.hidden&&!ui.refresh.hidden&&!ui.list.hidden&&ui.panel.hidden);
    const auto writes=app.m_fileList.writes,starts=fake::listStarts;
    for(int i=0;i<1000;++i)app.pollStorage();
    CHECK(fake::listStarts==starts&&app.m_fileList.writes==writes);
    // Refresh and directory back both remain usable.
    fake::nullList=false;app.scanFiles();CHECK(app.m_job&&!ui.timer.paused);app.pollStorage();
    CHECK(app.m_scanError.empty()&&app.m_paths.size()==(photo?2:1));CHECK(ui.timer.paused);
    app.m_directory="/sdcard/folder";fake::nullList=true;app.scanFiles();CHECK(app.onBack());
    CHECK(app.m_directory=="/sdcard"&&!app.m_job);CHECK(!app.onBack());
    fake::nullList=false;app.scanFiles();app.pollStorage();CHECK(!app.m_paths.empty());
    app.showList();CHECK(!fake::viewExit&&ui.timer.paused);
}
static void failedText(){
    apps::TextReader app;Widgets ui;ui.attach(app);app.scanFiles();app.pollStorage();
    for(int cycle=0;cycle<40;++cycle){
        const auto claims=fake::claims;fake::nullText=true;app.openFile(0);
        CHECK(!app.m_job&&!app.m_viewing&&!app.m_powerSession&&!fake::viewExit);
        CHECK(fake::claims==claims&&ui.timer.paused);
        CHECK(!ui.header.hidden&&!ui.refresh.hidden&&!ui.list.hidden&&ui.panel.hidden);
        CHECK(app.m_fileList.notice=="memory allocation failed");
        const auto writes=app.m_fileList.writes,starts=fake::textStarts;
        for(int i=0;i<1000;++i)app.pollStorage();
        CHECK(app.m_fileList.writes==writes&&fake::textStarts==starts);
        // A normal request after memory recovery retains the original behavior.
        fake::nullText=false;app.scanFiles();app.pollStorage();app.openFile(0);
        CHECK(app.m_job&&app.m_viewing&&app.m_powerSession&&fake::viewExit);
        CHECK(!ui.timer.paused&&ui.header.hidden&&ui.list.hidden&&!ui.panel.hidden);
        app.pollStorage();CHECK(!app.m_job&&app.m_viewing&&fake::decodedText==unsigned(cycle+1));
        CHECK(app.onBack());CHECK(!app.m_viewing&&!fake::viewExit&&ui.timer.paused);
    }
    CHECK(fake::claims==fake::releases);
}
static void failedGif(){
    apps::PhotoViewer app;Widgets ui;ui.attach(app);app.scanFiles();app.pollStorage();
    for(int cycle=0;cycle<40;++cycle){
        app.openGif(0);CHECK(ui.timer.period==10&&!ui.timer.paused);app.pollGif();
        CHECK(app.m_gifJob);app.m_gifJob->ready=0;app.pollGif();
        std::weak_ptr<core::gif::Job> ownership=app.m_gifJob;
        const auto displayed=fake::displayed;
        CHECK(displayed&&fake::pixels.count(displayed));
        app.m_gifJob->error="decode failed";app.m_gifJob->done=true;
        const auto before=ui.notice.writes;app.pollGif();CHECK(ui.notice.writes==before+1);
        CHECK(ui.notice.text=="decode failed"&&!ui.notice.hidden);
        // Repeated callbacks do not allocate or mutate labels after failure.
        for(int i=0;i<1000;++i){fake::tick+=100;app.pollGif();}
        CHECK(ui.notice.writes==before+1);CHECK(ui.timer.period==100&&!ui.timer.paused);
        CHECK(!ownership.expired()&&fake::displayed==displayed&&fake::pixels.count(displayed));
        CHECK(app.m_viewing&&fake::viewExit&&fake::screenshot);
        // Next/previous use production switching and detach before task destruction.
        app.switchGif(1);CHECK(ownership.expired()&&!fake::displayed);
        CHECK(app.m_gifIndex==1&&ui.timer.period==10);app.pollGif();CHECK(app.m_gifJob);
        app.switchGif(-1);CHECK(app.m_gifIndex==0);app.pollGif();CHECK(app.m_gifJob);
        CHECK(app.onBack());CHECK(!app.m_gifMode&&!app.m_viewing&&!fake::viewExit&&!fake::screenshot);
        CHECK(ui.timer.paused&&fake::pixels.empty());
    }
    // Null worker creation is also a terminal, low-frequency error state.
    fake::nullGif=true;app.openGif(0);app.pollGif();
    CHECK(!app.m_gifJob&&ui.timer.period==100&&!ui.timer.paused&&!ui.notice.hidden);
    auto writes=ui.notice.writes,starts=fake::gifStarts;
    for(int i=0;i<1000;++i)app.pollGif();
    CHECK(ui.notice.writes==writes&&fake::gifStarts==starts);
    fake::nullGif=false;app.switchGif(1);app.pollGif();CHECK(app.m_gifJob);CHECK(app.onBack());
    // Busy timeout stops retrying, retains return polling, and a fresh open works.
    fake::gifBusy=true;app.openGif(0);fake::tick+=5001;app.pollGif();
    CHECK(!app.m_gifPending&&!app.m_gifJob&&ui.timer.period==100&&!ui.timer.paused);
    writes=ui.notice.writes;starts=fake::gifStarts;
    for(int i=0;i<1000;++i)app.pollGif();
    CHECK(ui.notice.writes==writes&&fake::gifStarts==starts);
    fake::gifBusy=false;CHECK(app.onBack());app.openGif(0);app.pollGif();CHECK(app.m_gifJob);
    CHECK(app.onBack());CHECK(fake::pixels.empty()&&!fake::displayed&&fake::claims==fake::releases);
}
int main(int argc,char **argv){
    CHECK(argc==2);const std::string scenario=argv[1];
    if(scenario=="gif")failedGif();
    else if(scenario=="photo-list")failedList<apps::PhotoViewer>(true);
    else if(scenario=="text-list")failedList<apps::TextReader>(false);
    else if(scenario=="text-open")failedText();
    else CHECK(false);
    std::printf("{\"passed\":true,\"scenario\":\"%s\",\"checks\":%u}\n",scenario.c_str(),checks);
    return 0;
}
