#include "host_services.hpp"
#include "apps/speed_test.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include "ui/text_input.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <iostream>
using namespace core::speed;
sys::WifiSnapshot network;
bool screenOn=true;
std::shared_ptr<Job> currentJob;
std::shared_ptr<HistoryJob> currentHistory;
std::shared_ptr<DeleteJob> currentDelete;
unsigned deleteRequests=0;
unsigned homeCalls=0;
sys::WifiSnapshot sys::Connectivity::snapshot(){return network;}
bool sys::Power::isScreenOn(){return screenOn;}
namespace core::speed {
bool idle(){return (!currentJob || currentJob->done) && (!currentHistory || currentHistory->done) && (!currentDelete || currentDelete->done);}
std::shared_ptr<Job> start(){currentJob=std::make_shared<Job>();return currentJob;}
std::shared_ptr<HistoryJob> history(const std::string &before){currentHistory=std::make_shared<HistoryJob>();currentHistory->before=before;return currentHistory;}
std::shared_ptr<DeleteJob> removeHistory(const store::Entry &entry,uint64_t generation){
 ++deleteRequests;currentDelete=std::make_shared<DeleteJob>();currentDelete->entry=entry;currentDelete->cardGeneration=generation;return currentDelete;
}
}
namespace ui {
WindowManager &WindowManager::instance(){static WindowManager manager;return manager;}
void WindowManager::goHome(){++homeCalls;}
void TextInput::bindField(lv_obj_t *){assert(false);}
}
static unsigned char pixels[240*240*2],buffer[240*240*2];
static void flush(lv_display_t *d,const lv_area_t *a,uint8_t *p){
 for(int y=a->y1;y<=a->y2;++y)std::memcpy(pixels+(y*240+a->x1)*2,p+(y-a->y1)*(a->x2-a->x1+1)*2,(a->x2-a->x1+1)*2);lv_display_flush_ready(d);
}
static void capture(const char *name){
 lv_refr_now(nullptr);FILE *f=std::fopen(name,"wb");assert(f);std::fprintf(f,"P6\n240 240\n255\n");
 for(int i=0;i<240*240;++i){unsigned p=pixels[i*2]+256*pixels[i*2+1];unsigned char rgb[]={static_cast<unsigned char>(((p>>11)*255)/31),static_cast<unsigned char>((((p>>5)&63)*255)/63),static_cast<unsigned char>(((p&31)*255)/31)};std::fwrite(rgb,1,3,f);}std::fclose(f);
}
static void advance(){lv_tick_inc(260);lv_timer_handler();lv_obj_update_layout(lv_screen_active());}
static lv_obj_t *label(lv_obj_t *root,const char *text){
 if(lv_obj_has_flag(root,LV_OBJ_FLAG_HIDDEN))return nullptr;
 if(lv_obj_check_type(root,&lv_label_class) && !std::strcmp(lv_label_get_text(root),text))return root;
 for(unsigned i=0;i<lv_obj_get_child_count(root);++i)if(auto found=label(lv_obj_get_child(root,i),text))return found;
 return nullptr;
}
static lv_obj_t *find(const char *text){auto obj=label(lv_screen_active(),text);if(!obj)std::cerr<<"Missing: "<<text<<"\n";assert(obj);return obj;}
static void click(const char *text){auto button=lv_obj_get_parent(find(text));assert(!lv_obj_has_state(button,LV_STATE_DISABLED));lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);advance();}
static void contained(lv_obj_t *inside,lv_obj_t *outside){
 lv_area_t a,b;lv_obj_get_coords(inside,&a);lv_obj_get_coords(outside,&b);
 if(!(a.x1>=b.x1 && a.x2<=b.x2 && a.y1>=b.y1 && a.y2<=b.y2)){std::cerr<<"overflow "<<(lv_obj_check_type(inside,&lv_label_class)?lv_label_get_text(inside):"object")<<" child "<<a.x1<<","<<a.y1<<","<<a.x2<<","<<a.y2<<" parent "<<b.x1<<","<<b.y1<<","<<b.x2<<","<<b.y2<<"\n";std::abort();}
}
static lv_obj_t *chart(lv_obj_t *root) {
 if(lv_obj_is_hidden(root))return nullptr;
 if(lv_obj_check_type(root,&lv_chart_class))return root;
 for(unsigned i=0;i<lv_obj_get_child_count(root);++i)if(auto result=chart(lv_obj_get_child(root,i)))return result;
 return nullptr;
}
static void chartValues(lv_obj_t *root,unsigned count,const int32_t *values,unsigned used) {
 auto plot=chart(root);if(!plot || lv_chart_get_point_count(plot)!=count)std::fprintf(stderr,"chart expected=%u actual=%u\n",count,plot?lv_chart_get_point_count(plot):0);assert(plot && lv_chart_get_point_count(plot)==count);
 auto series=lv_chart_get_series_next(plot,nullptr);assert(series);auto data=lv_chart_get_series_y_array(plot,series);
 for(unsigned i=0;i<used;++i)assert(data[i]==values[i]);
 for(unsigned i=used;i<count;++i)assert(data[i]==LV_CHART_POINT_NONE);
 contained(plot,root);
}
static void verticalRates(lv_obj_t *root,const char *down,const char *up) {
 auto d=find("下载"),u=find("上传"),dv=find(down),uv=find(up),g=chart(root);
 lv_area_t da,ua,dva,uva,ga;lv_obj_get_coords(d,&da);lv_obj_get_coords(u,&ua);
 lv_obj_get_coords(dv,&dva);lv_obj_get_coords(uv,&uva);lv_obj_get_coords(g,&ga);
 assert(da.x1==ua.x1 && da.y2<ua.y1 && dva.x1>da.x2 && uva.x1>ua.x2);
 assert(dva.y1<=da.y2 && dva.y2>=da.y1 && uva.y1<=ua.y2 && uva.y2>=ua.y1);
 assert(dva.y2<uva.y1 && uva.y2<ga.y1 && lv_obj_get_height(g)==65);
 for(auto obj:{d,u,dv,uv})contained(obj,lv_obj_get_parent(obj));
 contained(g,root);
 for(auto row:{lv_obj_get_parent(d),lv_obj_get_parent(u)}) {
  auto unit=label(row,"Mbps");assert(unit);contained(unit,row);
  lv_area_t value,units;lv_obj_get_coords(row==lv_obj_get_parent(d)?dv:uv,&value);lv_obj_get_coords(unit,&units);
  assert(value.x2<units.x1 && units.x2<169);
 }
}
Record fixture(){
 Record r;std::strcpy(r.time,"2026-10-13 12:30:01 +0800");std::strcpy(r.ssid,"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456");std::strcpy(r.ip,"192.168.100.100");std::strcpy(r.bssid,"01:02:03:04:05:06");
 r.epoch=1791865801;r.rssiStart=-45;r.rssiEnd=-50;r.channel=6;r.clockSynced=true;r.durationUs=22000000;
 Sample s;s.requested=s.bytes=s.acknowledged=1000000;s.elapsedUs=1000000;s.headersUs=42100;s.httpStatus=200;s.complete=true;
 r.download.fill(s);r.upload.fill(s);r.downloadCount=r.uploadCount=4;
 const int64_t downTimes[]={500000,1500000,750000,1250000},upTimes[]={2000000,3000000,1500000,1500000};
 for(unsigned i=0;i<4;++i){r.download[i].elapsedUs=downTimes[i];r.upload[i].elapsedUs=upTimes[i];}
 s.requested=s.bytes=s.acknowledged=0;r.latency.fill(s);r.latencyCount=LatencyCount;
 for(unsigned i=0;i<LatencyCount;++i)r.latency[i].headersUs=30000+(i*7317)%60000;
 return r;
}
int main(){
 lv_init();auto display=lv_display_create(240,240);lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(display,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_FULL);lv_display_set_flush_cb(display,flush);
 for(int theme:{4,5,6,8,10}){
  ui::theme::init(theme);assert(int(ui::theme::current())==theme);currentJob.reset();currentHistory.reset();currentDelete.reset();deleteRequests=0;screenOn=true;network={};
  auto root=ui::compact::panel(lv_screen_active(),240,240);apps::SpeedTest app;app.setRoot(root);app.onCreate(root);app.onShow();advance();
  assert(lv_obj_has_state(lv_obj_get_parent(find("开始测速")),LV_STATE_DISABLED));
  click("历史");assert(currentHistory);currentHistory->done=true;advance();find("还没有测速记录");assert(app.onBack());
  network.state=sys::WifiState::Connected;network.ssid="家庭网络-ABCDEFGHIJKLMNOPQRSTUVWXYZ";network.ip="192.168.100.100";network.rssi=-55;advance();
  click("开始测速");assert(currentJob);assert(lv_obj_has_state(lv_obj_get_parent(find("历史")),LV_STATE_DISABLED));
  currentJob->published.phase=Phase::Download;currentJob->published.sample=2;currentJob->published.currentBytes=1250000;currentJob->published.liveCount=20;currentJob->published.liveValid=true;currentJob->published.currentMbps=3.85;
  for(unsigned i=0;i<20;++i)currentJob->published.liveKbps[i]=1000+(i*317)%3400;advance();
  chartValues(root,40,currentJob->published.liveKbps.data(),20);find("3.85");find("下载曲线");
  char path[60];std::snprintf(path,sizeof(path),"progress-%d.ppm",theme);capture(path);
  click("取消测速");assert(currentJob->cancelled);currentJob->published.record=fixture();currentJob->published.phase=Phase::Finished;currentJob->published.saved=true;currentJob->done=true;advance();
  auto result=find("测速完成 · 已保存 SD");contained(result,lv_obj_get_parent(result));
  auto button=lv_obj_get_parent(find("重新测速"));lv_area_t a,b;lv_obj_get_coords(result,&a);lv_obj_get_coords(button,&b);assert(a.y2<b.y1);
  verticalRates(root,"8.00","4.00");find("速率曲线");
  const int32_t downRates[]={16000,5333,10666,6400};chartValues(root,4,downRates,4);
  auto resultGraph=chart(root);auto firstSeries=lv_chart_get_series_next(resultGraph,nullptr);
  auto upValues=lv_chart_get_series_y_array(resultGraph,lv_chart_get_series_next(resultGraph,firstSeries));
  assert(upValues[0]==4000 && upValues[1]==2666 && upValues[2]==5333 && upValues[3]==5333);
  std::snprintf(path,sizeof(path),"main-%d.ppm",theme);capture(path);
  for(unsigned i=0;i<4;++i){currentJob->published.record.download[i].elapsedUs=800;currentJob->published.record.upload[i].elapsedUs=80000;}
  advance();verticalRates(root,"10000.00","100.00");
  std::snprintf(path,sizeof(path),"large-rate-%d.ppm",theme);capture(path);
  currentJob->published.record=fixture();advance();
  click("详情");find("下载 / 上传曲线");find("HTTP延迟曲线");
  assert(!label(root,"删除此记录"));
  std::snprintf(path,sizeof(path),"detail-%d.ppm",theme);capture(path);
  find("HTTP延迟 / 抖动");find("服务器确认 1000000 B");assert(app.onBack());advance();
  currentJob->published.record=fixture();auto &q=currentJob->published.record;q.qualityVersion=1;
  q.rssiMin=-68;q.rssiMax=-43;q.rssiCount=24;
  for(unsigned n=0;n<6;++n){auto &run=q.probes[n];std::strcpy(run.target,n%2?"1.1.1.1":"192.168.1.1");run.count=6;for(unsigned i=0;i<6;++i)run.samples[i]={i==2?-1:int32_t((n+1)*10000),-55};}
  advance();find("采样不足 · 结果供参考");find("外网 空闲20 / 下载40 / 上传60 ms");
  click("详情");find("网络延迟对比");find("路由器 ICMP");find("外网 ICMP");
  find("结果供参考：采样或成功率不足\n成功率 100.0% · 有效 4.0秒");
  std::snprintf(path,sizeof(path),"quality-detail-%d.ppm",theme);capture(path);assert(app.onBack());
  currentJob->published.record=fixture();advance();
  click("历史");assert(currentHistory);
  const char *selected="20261013-123001-0000000000000001.json";
  for(unsigned i=0;i<8;++i)currentHistory->page.entries.push_back({selected,fixture()});currentHistory->cardGeneration=7;currentHistory->page.more=true;currentHistory->page.cursor="cursor";currentHistory->done=true;advance();
  auto title=find("2026-10-13 12:30:01\nABCDEFGHIJKLMNOPQRSTUVWXYZ123456\n8.00 / 4.00 Mbps");contained(title,lv_obj_get_parent(title));
  std::snprintf(path,sizeof(path),"history-%d.ppm",theme);capture(path);
  click(lv_label_get_text(title));find("HTTP延迟 / 抖动");
  auto removeButton=lv_obj_get_parent(find("删除此记录")),body=lv_obj_get_parent(removeButton);
  assert(lv_obj_get_child(body,-1)==removeButton);
  lv_obj_scroll_to_view(removeButton,LV_ANIM_OFF);advance();contained(removeButton,body);
  std::snprintf(path,sizeof(path),"delete-bottom-%d.ppm",theme);capture(path);
  click("删除此记录");find("删除后无法恢复");assert(!currentDelete && deleteRequests==0);
  std::snprintf(path,sizeof(path),"delete-confirm-%d.ppm",theme);capture(path);
  click("取消");assert(!currentDelete && label(root,"删除此记录"));
  click("删除此记录");assert(app.onBack());advance();assert(!currentDelete && !label(root,"确认删除"));
  click("删除此记录");click("确认删除");assert(currentDelete && deleteRequests==1);
  assert(currentDelete->entry.filename==selected && currentDelete->cardGeneration==7);
  assert(lv_obj_has_state(removeButton,LV_STATE_DISABLED));assert(app.onBack());find("测速详情");
  currentDelete->error=EIO;currentDelete->done=true;advance();find("删除失败，请检查SD卡");
  assert(!lv_obj_has_state(removeButton,LV_STATE_DISABLED));
  click("删除此记录");click("确认删除");assert(deleteRequests==2);
  std::strcpy(currentJob->published.filename,selected);
  currentDelete->done=true;advance();assert(currentHistory && !currentHistory->done && !currentJob->published.saved);
  find("正在读取 SD 卡…");
  currentHistory->cardGeneration=7;currentHistory->page.entries.push_back({"20261013-123001-0000000000000002.json",fixture()});
  currentHistory->page.more=true;currentHistory->page.cursor="cursor";currentHistory->done=true;advance();find("测试记录");
  click("更早记录");assert(currentHistory->before=="cursor");
  currentHistory->page.error=EIO;currentHistory->done=true;advance();find("SD卡读写失败，请检查内存卡");assert(app.onBack());advance();click("说明");find("测速说明");assert(app.onBack());advance();
  currentJob->published.record.download[1].complete=false;currentJob->published.record.error=Error::Length;advance();
  const int32_t partial[]={16000,LV_CHART_POINT_NONE,10666,6400};chartValues(root,4,partial,4);
  click("重新测速");currentJob->published.record.error=Error::Connect;
  currentJob->published.record.warmupCount=1;currentJob->published.record.warmup[0].error=Error::Connect;
  currentJob->published.record.warmup[0].nativeError=28674;currentJob->done=true;currentJob->published.phase=Phase::Finished;advance();
  find("连接或证书校验失败 · 点详情");find("等待测速数据");chartValues(root,2,nullptr,0);
  std::snprintf(path,sizeof(path),"failure-%d.ppm",theme);capture(path);
  click("详情");find("请确认该 Wi-Fi 可以访问互联网，检查设备时间，再重新测速。");assert(app.onBack());advance();
  click("重新测速");screenOn=false;advance();assert(currentJob->cancelled);
  screenOn=true;currentJob->done=true;advance();click("历史");
  currentHistory->page.entries.push_back({selected,fixture()});currentHistory->cardGeneration=7;currentHistory->done=true;advance();
  click("2026-10-13 12:30:01\nABCDEFGHIJKLMNOPQRSTUVWXYZ123456\n8.00 / 4.00 Mbps");
  click("删除此记录");click("确认删除");screenOn=false;advance();assert(currentDelete->cancelled);
  app.onHide();app.onDestroy();lv_obj_delete(root);advance();assert(lv_obj_get_child_count(lv_screen_active())==0);
 }
 lv_display_delete(display);lv_deinit();std::cout<<"Production UI: five palettes, offline history, start/cancel, details, long SSID containment, pagination, SD error and screen-off/destroy lifecycle passed\n";
}
