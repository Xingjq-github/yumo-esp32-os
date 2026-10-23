#include "core/photo_loader.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <string>
#include <set>
#include <vector>
static unsigned checks=0,frames=0,starts=0;
#define CHECK(c) do{++checks;if(!(c)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#c);std::exit(1);}}while(0)
static uint32_t tick=0;
static bool screen=true,sleeping=false,active=false,failStart=false;
static std::set<void *> allocations;
static uint8_t *allocate(size_t n){auto p=static_cast<uint8_t *>(std::malloc(n));CHECK(p);allocations.insert(p);return p;}
static void heap_caps_free(void *p){if(p){CHECK(allocations.erase(p)==1);std::free(p);}}
struct lv_point_t {int32_t x=0,y=0;};
struct lv_image_dsc_t {struct{unsigned magic=0,cf=0,w=0,h=0,stride=0;}header;size_t data_size=0;const uint8_t *data=nullptr;};
struct lv_obj_t {int x=0,y=0;bool hidden=false;std::string text;const lv_image_dsc_t *image=nullptr;};
struct lv_timer_t{};
struct lv_event_t {void *user;};
static lv_point_t pointer;
static lv_point_t *lv_indev_active(){return &pointer;}
static void lv_indev_get_point(lv_point_t *p,lv_point_t *out){*out=*p;}
static void *lv_event_get_user_data(lv_event_t *e){return e->user;}
static uint32_t lv_tick_get(){return tick;}
static void lv_obj_set_hidden(lv_obj_t *p,bool hidden){p->hidden=hidden;}
static void lv_obj_center(lv_obj_t *){}
static void lv_label_set_text(lv_obj_t *p,const char *s){p->text=s;}
static void lv_obj_set_pos(lv_obj_t *p,int x,int y){p->x=x;p->y=y;}
static void lv_image_cache_drop(lv_image_dsc_t *){}
static void lv_image_set_src(lv_obj_t *p,const lv_image_dsc_t *d){
    p->image=d;if(d){CHECK(d->header.w<=240&&d->header.h<=240);CHECK(d->data_size<=240*240*2);++frames;}
}
#define LV_IMAGE_HEADER_MAGIC 1
#define LV_COLOR_FORMAT_RGB565 2
#define LV_SYMBOL_IMAGE "image"
namespace sys {namespace power {enum{StayLit=1};}struct Power{static bool isScreenOn(){return screen;}static bool sleepPending(){return sleeping;}};}
namespace ui {struct VirtualFileList{};}
namespace core {struct StorageJob{};namespace gif {struct Job{};}}
static std::shared_ptr<core::photo::Job> worker;
namespace core::photo {
Source::~Source(){heap_caps_free(data);}
Job::~Job(){heap_caps_free(pixels);}
bool idle(){return !active;}
std::shared_ptr<Job> start(const std::string &,std::shared_ptr<Source> source,uint16_t width,int32_t x,int32_t y){
    CHECK(!active);if(failStart)return {};active=true;++starts;
    worker=std::make_shared<Job>();worker->source=source;worker->targetWidth=width;worker->panX=x;worker->panY=y;return worker;
}
}
namespace apps {constexpr int kViewW=240,kViewH=240,kScaleStepNum=3,kScaleStepDen=2;}
#include "view_header.inc"
#include "view_methods.inc"
namespace apps {void PhotoViewer::showMessage(const char *s){m_notice->text=s;}}
static void finish(bool fail=false){
    CHECK(active&&worker);auto job=std::move(worker);active=false;
    job->view=photo_view_make(job->source->width,job->source->height,job->targetWidth,job->panX,job->panY);
    job->width=job->view.width;job->height=job->view.height;job->fitWidth=photo_fit_width(job->source->width,job->source->height);
    if(fail)job->error="synthetic decode failure";else job->pixels=allocate(size_t(job->width)*job->height*2);
    job->done.store(true,std::memory_order_release);
}
int main(){
    apps::PhotoViewer v;lv_obj_t image,notice,header,list,panel;
    v.m_image=&image;v.m_notice=&notice;v.m_header=&header;v.m_list=&list;v.m_imagePanel=&panel;
    v.m_viewing=true;v.m_keySession=1;v.m_source=std::make_shared<core::photo::Source>();
    v.m_source->width=v.m_origW=6000;v.m_source->height=v.m_origH=4000;
    v.m_requestedW=v.m_dispW=v.m_fitW=240;v.m_imgW=240;v.m_imgH=160;v.m_pixels=allocate(240*160*2);
    auto old=v.m_pixels;
    // Rapid taps coalesce; no worker starts until debounce expires.
    for(int i=0;i<40;++i)v.zoom(1);
    CHECK(v.m_requestedW==6000&&v.m_pendingView);v.pollImage();CHECK(!active&&starts==0);
    tick+=130;v.pollImage();CHECK(active&&starts==1&&v.m_pixels==old);
    // A newer request cancels the old job; even a successful late result is ignored.
    v.zoom(-1);CHECK(worker->cancelled&&v.m_pendingView);finish();tick+=130;v.pollImage();
    CHECK(active&&starts==2&&v.m_pixels==old);finish();v.pollImage();
    CHECK(v.m_dispW==4099&&v.m_imgW==240&&v.m_imgH==240&&v.m_draggable);
    // Integer zoom steps must be reversible, so existing cache keys are reused.
    std::vector<unsigned> scales{v.m_dispW};
    while(v.m_requestedW>v.m_fitW){v.zoom(-1);scales.push_back(v.m_requestedW);}
    for(size_t i=scales.size()-1;i>0;--i){v.zoom(1);CHECK(v.m_requestedW==scales[i-1]);}
    tick+=130;v.pollImage();CHECK(active);finish();v.pollImage();
    CHECK(v.m_dispW==scales.front()&&!v.m_pendingView);
    // Stable panel hit target; drag requests are merged and wait for last position.
    lv_event_t event{&v};pointer={100,100};apps::PhotoViewer::imagePressCb(&event);CHECK(v.m_dragging);
    const auto originX=v.m_offsetX,originY=v.m_offsetY;
    pointer={60,40};apps::PhotoViewer::imagePressingCb(&event);CHECK(v.m_offsetX==originX+40&&v.m_offsetY==originY+60);
    CHECK(image.x==-40&&image.y==-60&&v.m_pendingView);
    apps::PhotoViewer::imageReleaseCb(&event);CHECK(!v.m_dragging);v.pollImage();CHECK(active);
    finish();v.pollImage();CHECK(image.x==0&&image.y==0&&v.m_renderX==originX+40);
    // Continuous motion never postpones the first deadline or cancels the
    // same-scale worker. Intermediate results preserve the finger's position.
    pointer={150,150};apps::PhotoViewer::imagePressCb(&event);
    pointer={140,140};apps::PhotoViewer::imagePressingCb(&event);
    const auto deadline=v.m_requestAt;
    for(int i=0;i<4;++i){tick+=10;pointer.x-=4;pointer.y-=3;apps::PhotoViewer::imagePressingCb(&event);if(!active)CHECK(v.m_requestAt==deadline);v.pollImage();}
    CHECK(active&&!worker->cancelled&&notice.hidden);
    const auto submittedX=worker->panX,submittedY=worker->panY;
    for(int i=0;i<8;++i){tick+=10;pointer.x-=4;pointer.y-=3;apps::PhotoViewer::imagePressingCb(&event);v.pollImage();CHECK(active&&!worker->cancelled);}
    const auto requestedX=v.m_offsetX,requestedY=v.m_offsetY;
    const auto beforePan=frames;finish();v.pollImage();
    CHECK(frames==beforePan+1&&active&&!worker->cancelled);
    CHECK(v.m_renderX==submittedX&&v.m_renderY==submittedY);
    CHECK(v.m_offsetX==requestedX&&v.m_offsetY==requestedY);
    CHECK(image.x==submittedX-requestedX&&image.y==submittedY-requestedY);
    CHECK(worker->panX==requestedX&&worker->panY==requestedY&&notice.hidden);
    apps::PhotoViewer::imageReleaseCb(&event);finish();v.pollImage();
    CHECK(!active&&!v.m_pendingView&&image.x==0&&image.y==0);
    // Error keeps the current pixels and restores the last committed pan/scale.
    old=v.m_pixels;v.zoom(1);tick+=130;v.pollImage();finish(true);v.pollImage();
    CHECK(v.m_pixels==old&&v.m_requestedW==v.m_dispW&&!notice.hidden&&notice.text=="synthetic decode failure");
    // Failure to start is bounded and permits a later explicit retry.
    failStart=true;v.zoom(1);tick+=130;v.pollImage();CHECK(!v.m_pendingView&&!active&&v.m_pixels==old);failStart=false;
    // Screen/sleep gates hold the request without starting I/O.
    v.zoom(1);tick+=130;screen=false;v.pollImage();CHECK(!active&&v.m_pendingView);
    screen=true;sleeping=true;v.pollImage();CHECK(!active);sleeping=false;v.pollImage();CHECK(active);
    // Exit releases request/source ownership; late completion cannot repaint.
    auto pending=worker;v.releaseSource();CHECK(pending->cancelled&&!v.m_pendingView&&!v.m_source);
    v.m_viewing=false;v.m_keySession=0;unsigned before=frames;finish();v.pollImage();CHECK(frames==before);
    pending.reset();v.releaseImage();CHECK(allocations.empty());
    // Geometry spans corners, tiny axes, invalid dimensions and every crop bound.
    for(auto size:std::vector<std::pair<unsigned,unsigned>>{{1,16000},{16000,1},{6000,4000},{17,301},{16384,4096}}){
        for(unsigned w:std::vector<unsigned>{0,1,240,500,6000,65535})for(int x:std::vector<int>{-100,0,50,20000})for(int y:std::vector<int>{-10,0,500,20000}){
            auto q=photo_view_make(size.first,size.second,w,x,y);CHECK(photo_view_valid(&q,size.first,size.second));
            CHECK(q.width<=240&&q.height<=240);
            CHECK(photo_sample(q.x+q.width-1,size.first,q.scaled_width)<size.first);
            CHECK(photo_sample(q.y+q.height-1,size.second,q.scaled_height)<size.second);
        }
    }
    auto invalid=photo_view_make(0,0,0,0,0);CHECK(!photo_view_valid(&invalid,0,0));
    std::printf("{\"passed\":true,\"assertions\":%u,\"frames\":%u,\"started_jobs\":%u,\"leaks\":0}\n",checks,frames,starts);
}
