#include "speed_test.hpp"
#include "core/connectivity.hpp"
#include "core/power_mgr.hpp"
#include "ui/compact_ui.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace apps {
namespace c=ui::compact;
namespace speed=core::speed;
namespace {
const char *storageText(int error) {
    if(error==ENOSPC)return "SD卡空间不足，未保存";
    if(error==EBUSY)return "SD卡已弹出或正忙，未保存";
    if(error==ENOMEM)return "保存内存不足，未保存";
    if(error==ECANCELED)return "保存已取消";
    return "SD卡读写失败，请检查内存卡";
}
void buttonText(lv_obj_t *button,const char *text){c::setText(lv_obj_get_child(button,0),text);}
void rate(char *out,size_t size,const speed::Throughput &value) {
    if(value.valid)std::snprintf(out,size,"%.2f",value.average);else std::snprintf(out,size,"--");
}
lv_obj_t *small(lv_obj_t *parent,const char *text,int width) {
    auto label=c::label(parent,text,width);lv_obj_set_style_text_font(label,ui::theme::fontMenu(),0);return label;
}
lv_obj_t *plot(lv_obj_t *parent,int width,int height,unsigned count) {
    auto chart=lv_chart_create(parent);lv_obj_set_size(chart,width,height);
    lv_obj_set_scrollable(chart,false);lv_obj_set_clickable(chart,false);
    lv_chart_set_type(chart,LV_CHART_TYPE_LINE);lv_chart_set_point_count(chart,count);
    lv_chart_set_div_line_count(chart,3,4);
    lv_obj_set_style_bg_opa(chart,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(chart,0,0);
    lv_obj_set_style_pad_all(chart,3,0);lv_obj_set_style_radius(chart,0,0);
    lv_obj_set_style_line_color(chart,ui::theme::edge(),LV_PART_MAIN);
    lv_obj_set_style_line_width(chart,1,LV_PART_MAIN);lv_obj_set_style_line_opa(chart,LV_OPA_50,LV_PART_MAIN);
    lv_obj_set_style_line_width(chart,2,LV_PART_ITEMS);lv_obj_set_style_size(chart,4,4,LV_PART_INDICATOR);
    return chart;
}
int32_t graphValue(const speed::Sample &sample,bool latency=false) {
    if(!sample.complete || sample.error!=speed::Error::None || sample.httpStatus!=200 || sample.elapsedUs<=0 || sample.bytes!=sample.requested || (latency?sample.headersUs<=0:!sample.bytes))return LV_CHART_POINT_NONE;
    const double value=latency?double(sample.headersUs)/100.0:speed::mbps(sample.bytes,sample.elapsedUs)*1000.0;
    return static_cast<int32_t>(std::min(value,1000000000.0));
}
int32_t scale(int32_t maximum,bool latency=false) {
    const int32_t step=latency?500:maximum>10000?5000:maximum>1000?1000:250;
    return std::max(step,((maximum+step-1)/step)*step);
}
void resultPlot(lv_obj_t *parent,const speed::Record &record,bool latency) {
    const unsigned count=latency?speed::LatencyCount:std::max(2u,std::max(record.downloadCount,record.uploadCount));
    auto graph=plot(parent,224,76,count);
    auto first=lv_chart_add_series(graph,ui::theme::accent(),LV_CHART_AXIS_PRIMARY_Y);
    auto second=latency?nullptr:lv_chart_add_series(graph,ui::theme::success(),LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_values(graph,first,LV_CHART_POINT_NONE);
    if(second)lv_chart_set_all_values(graph,second,LV_CHART_POINT_NONE);
    int32_t maximum=0;
    for(unsigned i=0;i<count;++i) {
        const int32_t a=latency?(i<record.latencyCount?graphValue(record.latency[i],true):LV_CHART_POINT_NONE):
            (i<record.downloadCount?graphValue(record.download[i]):LV_CHART_POINT_NONE);
        const int32_t b=!latency && i<record.uploadCount?graphValue(record.upload[i]):LV_CHART_POINT_NONE;
        lv_chart_set_series_value_by_id(graph,first,i,a);
        if(second)lv_chart_set_series_value_by_id(graph,second,i,b);
        if(a!=LV_CHART_POINT_NONE)maximum=std::max(maximum,a);
        if(b!=LV_CHART_POINT_NONE)maximum=std::max(maximum,b);
    }
    const auto top=scale(maximum,latency);lv_chart_set_axis_range(graph,LV_CHART_AXIS_PRIMARY_Y,0,top);
    char caption[96];std::snprintf(caption,sizeof(caption),latency?"0~%.0f ms / #1~%u":"0~%.2f Mbps / #1~%u",double(top)/(latency?10:1000),count);
    auto label=small(parent,caption,224);lv_obj_set_style_text_color(label,ui::theme::muted(),0);
}

}
void SpeedTest::onCreate(lv_obj_t *parent) {
    lv_obj_set_scrollable(parent,false);
    auto header=c::panel(parent,240,36);
    c::button(header,LV_SYMBOL_LEFT,[](lv_event_t *e){auto self=static_cast<SpeedTest *>(lv_event_get_user_data(e));if(!self->onBack())ui::WindowManager::instance().goHome();},this,36,36);
    heading=c::label(header,"网速测速",140);lv_obj_set_pos(heading,40,7);
    historyButton=c::button(header,"历史",action,this,58,36);lv_obj_set_pos(historyButton,182,0);
    mainPage=c::panel(parent,240,204);lv_obj_set_pos(mainPage,0,36);
    wifi=c::label(mainPage,"请先连接 Wi-Fi",232);lv_obj_set_style_text_font(wifi,ui::theme::fontReading(),0);lv_obj_set_pos(wifi,4,0);
    lv_label_set_long_mode(wifi,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    for(bool up:{false,true}) {
        auto card=c::panel(mainPage,234,22);lv_obj_set_pos(card,3,up?42:20);
        lv_obj_set_style_bg_color(card,ui::theme::surface(),0);lv_obj_set_style_radius(card,4,0);
        auto title=small(card,up?"上传":"下载",32);lv_obj_align(title,LV_ALIGN_LEFT_MID,6,0);
        lv_obj_set_style_text_color(title,up?ui::theme::success():ui::theme::accent(),0);
        auto value=c::label(card,"--",76);lv_obj_set_style_text_font(value,&lv_font_montserrat_18,0);
        lv_obj_align(value,LV_ALIGN_LEFT_MID,44,0);
        auto unit=c::label(card,"Mbps",40);lv_obj_set_style_text_font(unit,&lv_font_montserrat_12,0);
        lv_obj_align(unit,LV_ALIGN_LEFT_MID,124,0);lv_obj_set_style_text_color(unit,ui::theme::muted(),0);
        if(up)upload=value;else download=value;
    }
    graphCaption=small(mainPage,"速率曲线",66);lv_obj_set_pos(graphCaption,169,22);
    lv_obj_set_style_text_align(graphCaption,LV_TEXT_ALIGN_RIGHT,0);
    graphScale=c::label(mainPage,"0~0.25",66);lv_obj_set_pos(graphScale,169,47);
    lv_obj_set_style_text_font(graphScale,&lv_font_montserrat_12,0);lv_obj_set_style_text_align(graphScale,LV_TEXT_ALIGN_RIGHT,0);
    graph=plot(mainPage,234,65,speed::TransferCount);lv_obj_set_pos(graph,3,64);
    downSeries=lv_chart_add_series(graph,ui::theme::accent(),LV_CHART_AXIS_PRIMARY_Y);
    upSeries=lv_chart_add_series(graph,ui::theme::success(),LV_CHART_AXIS_PRIMARY_Y);
    emptyGraph=small(mainPage,"等待测速数据",180);lv_obj_set_pos(emptyGraph,30,88);
    lv_obj_set_style_text_align(emptyGraph,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(emptyGraph,ui::theme::muted(),0);
    latency=small(mainPage,"HTTP延迟 -- ms",232);lv_obj_set_pos(latency,4,130);
    lv_label_set_long_mode(latency,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    state=small(mainPage,"约40~120秒，最多32 MB",232);lv_obj_set_pos(state,4,149);
    lv_label_set_long_mode(state,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);lv_obj_set_style_text_color(state,ui::theme::muted(),0);
    startButton=c::button(mainPage,"开始测速",action,this,116,36);lv_obj_set_pos(startButton,2,168);
    c::buttonBackground(startButton,ui::theme::accent());
    detailButton=c::button(mainPage,"详情",action,this,59,36);lv_obj_set_pos(detailButton,119,168);
    methodButton=c::button(mainPage,"说明",action,this,59,36);lv_obj_set_pos(methodButton,179,168);
}
void SpeedTest::onShow(){if(!timer)timer=lv_timer_create(tick,250,this);refresh();}
void SpeedTest::onHide() {
    if(job)job->cancelled=true;
    if(historyJob)historyJob->cancelled=true;
    if(deleting)deleting->cancelled=true;
    closeDelete();
    if(timer){lv_timer_delete(timer);timer=nullptr;}
}
void SpeedTest::onDestroy(){onHide();job.reset();historyJob.reset();deleting.reset();records={};detailEntry={};}
bool SpeedTest::onBack() {
    if(deleteDialog){closeDelete();return true;}
    if(deleting)return true;
    if(page==Page::Main)return false;
    if(page==Page::Detail && detailFromHistory){page=Page::History;renderHistory();return true;}
    showMain();return true;
}
void SpeedTest::tick(lv_timer_t *timer) {
    auto self=static_cast<SpeedTest *>(lv_timer_get_user_data(timer));
    if(sys::Power::isScreenOn())self->refresh();
    else {
        if(self->job)self->job->cancelled=true;
        if(self->deleting)self->deleting->cancelled=true;
        self->closeDelete();
    }
}
void SpeedTest::action(lv_event_t *event) {
    auto self=static_cast<SpeedTest *>(lv_event_get_user_data(event));auto target=lv_event_get_current_target(event);
    if(target==self->startButton) {
        if(self->job && !self->job->done.load(std::memory_order_acquire)) {self->job->cancelled=true;self->notice="正在取消并释放连接";}
        else {
            self->job=speed::start();self->last={};
            self->notice=self->job?"":"未连接Wi-Fi或任务尚未结束";
        }
        self->refresh();
    }else if(target==self->historyButton || target==self->latestButton)self->showHistory();
    else if(target==self->olderButton)self->showHistory(self->records.cursor);
    else if(target==self->detailButton)self->showDetail(self->last.record,false);
    else if(target==self->methodButton)self->showMethod();
    else if(target==self->deleteButton)self->askDelete();
}
void SpeedTest::closeDelete() {
    if(deleteDialog){lv_obj_delete(deleteDialog);deleteDialog=nullptr;}
}
void SpeedTest::askDelete() {
    if(!detailFromHistory || detailEntry.filename.empty() || !historyGeneration || deleting || deleteDialog)return;
    deleteDialog=c::panel(root(),240,240);lv_obj_set_gesture_bubble(deleteDialog,false);
    auto title=c::label(deleteDialog,"删除测速记录",230);lv_obj_set_pos(title,5,24);
    char time[24];std::snprintf(time,sizeof(time),"%.19s",detailEntry.record.time);
    auto stamp=c::label(deleteDialog,time,230);lv_obj_set_pos(stamp,5,60);
    auto ssid=c::label(deleteDialog,detailEntry.record.ssid,230);lv_obj_set_pos(ssid,5,86);
    lv_obj_set_style_text_font(ssid,ui::theme::fontReading(),0);lv_label_set_long_mode(ssid,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    auto warning=c::label(deleteDialog,"删除后无法恢复",230);lv_obj_set_pos(warning,5,120);
    auto confirm=c::button(deleteDialog,"确认删除",[](lv_event_t *e) {
        auto self=static_cast<SpeedTest *>(lv_event_get_user_data(e));
        if(!self->deleteDialog || self->deleting)return;
        self->deleting=speed::removeHistory(self->detailEntry,self->historyGeneration);
        self->closeDelete();
        c::setText(self->deleteStatus,self->deleting?"正在删除记录…":"任务正忙，请稍后重试");
        c::disabled(self->deleteButton,bool(self->deleting));
    },this,118,44);lv_obj_set_pos(confirm,0,156);c::buttonBackground(confirm,ui::theme::danger());
    auto cancel=c::button(deleteDialog,"取消",[](lv_event_t *e) {
        static_cast<SpeedTest *>(lv_event_get_user_data(e))->closeDelete();
    },this,118,44);lv_obj_set_pos(cancel,122,156);
}
void SpeedTest::showMain() {
    if(historyJob){historyJob->cancelled=true;historyJob.reset();}
    if(secondary){lv_obj_delete(secondary);secondary=nullptr;}
    historyList=latestButton=olderButton=nullptr;
    closeDelete();deleteButton=deleteStatus=nullptr;detailEntry={};
    page=Page::Main;c::setText(heading,"网速测速");lv_obj_set_hidden(mainPage,false);lv_obj_set_hidden(historyButton,false);refresh();
}
lv_obj_t *SpeedTest::scrollPage(const char *title) {
    closeDelete();deleteButton=deleteStatus=nullptr;
    if(secondary)lv_obj_delete(secondary);
    historyList=latestButton=olderButton=nullptr;
    lv_obj_set_hidden(mainPage,true);lv_obj_set_hidden(historyButton,true);c::setText(heading,title);
    secondary=c::panel(root(),240,204,true);lv_obj_set_pos(secondary,0,36);
    lv_obj_set_flex_flow(secondary,LV_FLEX_FLOW_COLUMN);lv_obj_set_scroll_dir(secondary,LV_DIR_VER);
    lv_obj_set_style_pad_all(secondary,4,0);lv_obj_set_style_pad_row(secondary,6,0);
    return secondary;
}
lv_obj_t *SpeedTest::paragraph(lv_obj_t *parent,const char *text,bool muted) {
    auto label=c::label(parent,text,LV_PCT(100));lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);
    if(muted)lv_obj_set_style_text_color(label,ui::theme::muted(),0);
    return label;
}
void SpeedTest::showMethod() {
    page=Page::Method;auto body=scrollPage("测速说明");
    paragraph(body,"设备到 Cloudflare 的单连接 HTTPS 实测。约40~120秒，最多32 MB测试数据；实际网络流量还包含协议开销和重传。结果代表这台设备当前的表现，受设备性能和服务器距离影响。");
    paragraph(body,"下载和上传分别预热，各以12秒有效传输为目标，最多16次请求；每个方向最多25秒。速率=成功传输总字节×8÷这些请求的总耗时；Mbps为每秒100万比特，MB为100万字节。失败样本不计入网速，保留原始原因。");
    paragraph(body,"计时不含连接建立，包含请求、服务器处理和响应。上传收到服务器确认后才有效。预热和失败样本不计入速率，但计入总数据量。");
    paragraph(body,"延迟为10次独立HTTP请求到完整响应头的时间，包含服务器处理，不是ICMP ping。主页显示中位数；P95使用最近秩，抖动是相邻成功样本的延迟差绝对值平均值。");
    paragraph(body,"路由器和外网1.1.1.1同时进行独立ICMP探测；空闲20次，下载和上传中各最多48次。每500毫秒发送，800毫秒超时，32字节负载。超时率只表示探测未获回复，不是网络丢包率；全部无回复时无法判断质量。");
    paragraph(body,"网速采样充分要求至少3次成功、10秒有效传输且成功率不少于90%。次数或时长不足显示供参考；仍保留有效速度。忙时延迟反映传输中卡顿，需与同一目标空闲延迟比较。",true);
    paragraph(body,"历史存于 SD 卡 /speedtest，每条记录含原始样本、时间与Wi-Fi名称。时间沿用设备时钟，请先校准。无SD卡或写入失败会显示未保存。");
    paragraph(body,"测速中的曲线为分段传输速度，上传尚待服务器确认；结束后显示各次成功请求的有效速率。横轴为采样或请求序号，断点代表无有效数据。历史曲线使用保存的原始样本。",true);
    paragraph(body,"退出、手动关屏或断网会中止测试。历史可离线查看；已取消的测试保留在当前页面，不写入历史。",true);
}
void SpeedTest::showHistory(const std::string &before) {
    const std::string cursor=before; // The argument may alias records.cursor.
    if(job && !job->done.load(std::memory_order_acquire))return;
    if(historyJob && !historyJob->done.load(std::memory_order_acquire))return;
    if(deleting)return;
    page=Page::History;records={};historyGeneration=0;historyJob=speed::history(cursor);
    if(!historyJob)records.error=EBUSY;
    renderHistory();
}
void SpeedTest::renderHistory() {
    scrollPage("测试记录");lv_obj_set_scrollable(secondary,false);lv_obj_set_flex_flow(secondary,LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(secondary,0,0);lv_obj_set_style_layout(secondary,LV_LAYOUT_NONE,0);
    historyList=c::panel(secondary,240,164,true);lv_obj_set_pos(historyList,0,0);
    lv_obj_set_flex_flow(historyList,LV_FLEX_FLOW_COLUMN);lv_obj_set_scroll_dir(historyList,LV_DIR_VER);
    if(historyJob)paragraph(historyList,"正在读取 SD 卡…");
    else if(records.error)paragraph(historyList,storageText(records.error));
    else if(records.entries.empty())paragraph(historyList,records.skipped?"本页记录损坏，可继续查看更早记录":"还没有测速记录");
    for(size_t i=0;i<records.entries.size();++i) {
        const auto &r=records.entries[i].record;char text[220],down[24],up[24];
        rate(down,sizeof(down),speed::throughput(r.download,r.downloadCount));rate(up,sizeof(up),speed::throughput(r.upload,r.uploadCount));
        std::snprintf(text,sizeof(text),"%.19s\n%s\n%s / %s Mbps%s",r.time,r.ssid,down,up,speed::complete(r)?"":" · 部分");
        auto button=c::button(historyList,text,historyRow,this,236,96);
        lv_obj_set_user_data(button,reinterpret_cast<void *>(i));
        auto label=lv_obj_get_child(button,0);lv_obj_set_width(label,224);lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_LEFT,0);
    }
    if(records.skipped && !records.entries.empty())paragraph(historyList,"已跳过损坏的记录",true);
    latestButton=c::button(secondary,"最新记录",action,this,119,38);lv_obj_set_pos(latestButton,0,166);
    olderButton=c::button(secondary,"更早记录",action,this,120,38);lv_obj_set_pos(olderButton,120,166);
    c::disabled(latestButton,bool(historyJob));c::disabled(olderButton,bool(historyJob)||!records.more);
}
void SpeedTest::historyRow(lv_event_t *event) {
    auto self=static_cast<SpeedTest *>(lv_event_get_user_data(event));
    const auto index=reinterpret_cast<uintptr_t>(lv_obj_get_user_data(static_cast<lv_obj_t *>(lv_event_get_current_target(event))));
    if(index<self->records.entries.size())self->showDetail(self->records.entries[index].record,true,self->records.entries[index].filename);
}
void SpeedTest::showDetail(const speed::Record &r,bool fromHistory,const std::string &filename) {
    page=Page::Detail;detailFromHistory=fromHistory;auto body=scrollPage("测速详情");char text[360];
    detailEntry.filename=filename;detailEntry.record=r;
    std::snprintf(text,sizeof(text),"%.19s",r.time);small(body,text,224);
    auto ssid=paragraph(body,r.ssid,true);lv_obj_set_style_text_font(ssid,ui::theme::fontReading(),0);
    lv_label_set_long_mode(ssid,LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    std::snprintf(text,sizeof(text),"%s%s",speed::complete(r)?"测试完成":r.qualityVersion && r.error==speed::Error::None?"采样不足，结果供参考":speed::errorText(r.error),fromHistory?"":last.saved?" · 已保存 SD":" · 未保存");small(body,text,224);
    if(!fromHistory && last.storageError)paragraph(body,storageText(last.storageError),true);
    c::section(body,"下载 / 上传曲线");
    resultPlot(body,r,false);
    paragraph(body,"按请求序号绘图；下载与上传分别对应主页两种颜色。失败样本留空。",true);
    c::section(body,"HTTP延迟曲线");resultPlot(body,r,true);
    if(r.qualityVersion) {
        c::section(body,"网络延迟对比");
        paragraph(body,"同一目标：空闲 / 下载中 / 上传中。曲线断点表示未获回复。",true);
        for(unsigned target=0;target<2;++target) {
            c::section(body,target?"外网 ICMP":"路由器 ICMP");
            paragraph(body,r.probes[target].target,true);
            auto chart=plot(body,224,76,speed::ProbeCapacity);
            const lv_color_t colors[]={ui::theme::accent(),ui::theme::success(),ui::theme::warning()};
            int32_t maximum=0;
            for(unsigned phase=0;phase<3;++phase) {
                const auto &run=r.probes[phase*2+target];const auto stats=speed::probeStats(run);
                auto series=lv_chart_add_series(chart,colors[phase],LV_CHART_AXIS_PRIMARY_Y);lv_chart_set_all_values(chart,series,LV_CHART_POINT_NONE);
                for(unsigned i=0;i<run.count;++i)if(run.samples[i].elapsedUs>=0) {
                    const auto value=run.samples[i].elapsedUs/100;maximum=std::max(maximum,value);lv_chart_set_series_value_by_id(chart,series,i,value);
                }
                const char *title=phase==0?"空闲":phase==1?"下载中":"上传中";
                if(stats.valid)std::snprintf(text,sizeof(text),"%s：中位 %.1f ms\nP95 %.1f · 抖动 %.1f ms\n回复 %u/%u · 超时 %.1f%%%s",title,stats.median,stats.p95,stats.jitter,stats.valid,stats.attempted,100.0*(stats.attempted-stats.valid)/stats.attempted,stats.valid<5?" · 样本少":"");
                else std::snprintf(text,sizeof(text),"%s：无法测量\n回复 0/%u · 原始错误 %d",title,stats.attempted,run.error);
                paragraph(body,text,true);
            }
            maximum=scale(maximum,true);lv_chart_set_axis_range(chart,LV_CHART_AXIS_PRIMARY_Y,0,maximum);lv_chart_refresh(chart);
            std::snprintf(text,sizeof(text),"纵轴 0~%.1f ms · 横轴探测序号",maximum/10.0);paragraph(body,text,true);
        }
        paragraph(body,"ICMP可能被目标限制；全部无回复不代表断网。探测超时率不是网络丢包率。",true);
        if(r.rssiCount) {
            std::snprintf(text,sizeof(text),"Wi-Fi信号：%d ~ %d dBm\n采样 %u 次",r.rssiMin,r.rssiMax,r.rssiCount);paragraph(body,text);
        }
    }
    if(r.warmupCount && !r.warmup[0].complete) {
        const auto &w=r.warmup[0];
        std::snprintf(text,sizeof(text),"预热失败：%s\nHTTP %d · 原始错误 %d\n%llu/%lu B · %.2f s",speed::errorText(w.error),w.httpStatus,w.nativeError,static_cast<unsigned long long>(w.bytes),static_cast<unsigned long>(w.requested),double(w.elapsedUs+w.connectUs)/1000000);
        paragraph(body,text);
        paragraph(body,"请确认该 Wi-Fi 可以访问互联网，检查设备时间，再重新测速。",true);
    }
    for(bool up:{false,true}) {
        const auto &samples=up?r.upload:r.download;const auto count=up?r.uploadCount:r.downloadCount;const auto value=speed::throughput(samples,count);
        c::section(body,up?"上传数据":"下载数据");
        if(value.valid)std::snprintf(text,sizeof(text),"有效速率 %.3f Mbps\n最低 %.3f / 最高 %.3f\n有效 %u/%u 次\n%llu 字节 / %.3f 秒",value.average,value.worst,value.best,value.valid,value.attempted,static_cast<unsigned long long>(value.bytes),double(value.elapsedUs)/1000000);
        else std::snprintf(text,sizeof(text),"无有效结果 · 已尝试 %u 次",value.attempted);
        paragraph(body,text);
        if(r.qualityVersion) {
            const auto confidence=speed::confidence(value);
            std::snprintf(text,sizeof(text),"%s\n成功率 %.1f%% · 有效 %.1f秒%s",confidence==speed::Confidence::Good?"采样充分":confidence==speed::Confidence::Limited?"结果供参考：采样或成功率不足":"无有效测速样本",value.attempted?100.0*value.valid/value.attempted:0.0,double(value.elapsedUs)/1000000,r.trafficLimited[up?1:0]?"\n达到测试流量上限":"");
            paragraph(body,text,true);
        }
        for(unsigned i=0;i<count;++i) {
            const auto &s=samples[i];
            std::snprintf(text,sizeof(text),"#%u %s · HTTP %d\n%llu/%lu B · %.3f s\n连接 %.1f ms · %s\n原始错误 %d",i+1,speed::errorText(s.error),s.httpStatus,static_cast<unsigned long long>(s.bytes),static_cast<unsigned long>(s.requested),double(s.elapsedUs)/1000000,double(s.connectUs)/1000,s.reused?"复用连接":"新连接",s.nativeError);
            paragraph(body,text,true);
            if(up){std::snprintf(text,sizeof(text),"服务器确认 %lu B",static_cast<unsigned long>(s.acknowledged));paragraph(body,text,true);}
        }
    }
    c::section(body,"HTTP延迟 / 抖动");const auto v=speed::latency(r);
    if(v.valid) {
        std::snprintf(text,sizeof(text),"最小 %.2f ms\n平均 %.2f ms\n中位 %.2f ms\nP95 %.2f ms\n最大 %.2f ms\n成功 %u/%u 次",v.minimum,v.mean,v.median,v.p95,v.maximum,v.valid,v.attempted);paragraph(body,text);
        if(v.jitterPairs)std::snprintf(text,sizeof(text),"抖动 %.2f ms (%u对)",v.jitter,v.jitterPairs);else std::snprintf(text,sizeof(text),"抖动 -- (相邻样本不足)");paragraph(body,text);
    }else paragraph(body,"没有有效延迟样本");
    if(v.attempted){std::snprintf(text,sizeof(text),"请求失败率 %.1f%%\n不是网络丢包率",100.0*(v.attempted-v.valid)/v.attempted);paragraph(body,text,true);}
    for(unsigned i=0;i<r.latencyCount;++i) {
        const auto &s=r.latency[i];
        if(s.complete)std::snprintf(text,sizeof(text),"#%u %.2f ms · 连接 %.1f ms",i+1,double(s.headersUs)/1000,double(s.connectUs)/1000);
        else std::snprintf(text,sizeof(text),"#%u %s · HTTP %d",i+1,speed::errorText(s.error),s.httpStatus);
        paragraph(body,text,true);
    }
    c::section(body,"环境 / 统计口径");
    std::snprintf(text,sizeof(text),"服务器 %s\n节点 %s\nIP %s\nBSSID %s\n信号 %d → %d dBm\n信道 %d\n总耗时 %.2f 秒\n总传输数据 %.3f MB\n时钟 %s",speed::Host,r.serverTrace[0]?r.serverTrace:"--",r.ip,r.bssid,r.rssiStart,r.rssiEnd,r.channel,double(r.durationUs)/1000000,double(r.transferredBytes)/1000000,r.clockSynced?"网络校时成功":"设备时钟，请核对");paragraph(body,text);
    paragraph(body,"HTTPS单连接有效吞吐；计时包含响应确认，排除连接建立。失败样本不计入速率。延迟包含HTTP与服务器处理。详细规则见主页说明。",true);
    if(fromHistory && !detailEntry.filename.empty()) {
        deleteStatus=paragraph(body,"",true);
        deleteButton=c::button(body,"删除此记录",action,this,LV_PCT(100),40);
        c::buttonBackground(deleteButton,ui::theme::danger());c::disabled(deleteButton,!historyGeneration);
    }
}
void SpeedTest::refreshGraph(bool busy) {
    const auto &r=last.record;const bool live=busy && (last.phase==speed::Phase::Download || last.phase==speed::Phase::Upload);
    const bool ping=busy && last.phase==speed::Phase::Latency;
    const unsigned count=live?40:ping?speed::LatencyCount:std::max(2u,std::max(r.downloadCount,r.uploadCount));
    lv_chart_set_point_count(graph,count);lv_chart_set_all_values(graph,downSeries,LV_CHART_POINT_NONE);lv_chart_set_all_values(graph,upSeries,LV_CHART_POINT_NONE);
    int32_t maximum=0;bool any=false;
    for(unsigned i=0;i<count;++i) {
        int32_t a=LV_CHART_POINT_NONE,b=LV_CHART_POINT_NONE;
        if(live) {
            if(i<last.liveCount && last.liveKbps[i]>=0) {
                if(last.phase==speed::Phase::Upload)b=last.liveKbps[i];else a=last.liveKbps[i];
            }
        }else if(ping){if(i<r.latencyCount)a=graphValue(r.latency[i],true);}
        else {if(i<r.downloadCount)a=graphValue(r.download[i]);if(i<r.uploadCount)b=graphValue(r.upload[i]);}
        lv_chart_set_series_value_by_id(graph,downSeries,i,a);lv_chart_set_series_value_by_id(graph,upSeries,i,b);
        for(auto value:{a,b})if(value!=LV_CHART_POINT_NONE){maximum=std::max(maximum,value);any=true;}
    }
    const auto top=scale(maximum,ping);lv_chart_set_axis_range(graph,LV_CHART_AXIS_PRIMARY_Y,0,top);
    char text[64];std::snprintf(text,sizeof(text),ping?"0~%.0f ms":top>=1000000?"0~%.0f":"0~%.2f",double(top)/(ping?10:1000));c::setText(graphScale,text);
    std::snprintf(text,sizeof(text),"%s",live?(last.phase==speed::Phase::Upload?"上传曲线":"下载曲线"):
        ping?"延迟曲线":"速率曲线");
    c::setText(graphCaption,text);lv_obj_set_hidden(emptyGraph,any);lv_chart_refresh(graph);
}
void SpeedTest::refresh() {
    if(job)job->snapshot(last);
    if(deleting && deleting->done.load(std::memory_order_acquire)) {
        const int error=deleting->error;
        if((!error || error==ENOENT) && job && job->done.load(std::memory_order_acquire)) {
            const std::lock_guard<std::mutex> lock(job->mutex);
            if(deleting->entry.filename==job->published.filename) {
                job->published.saved=false;job->published.filename[0]='\0';last=job->published;
            }
        }
        deleting.reset();
        if(!error || error==ENOENT){showHistory();return;}
        if(deleteStatus)c::setText(deleteStatus,error==ESTALE?"记录或SD卡已变化，请刷新历史":
            error==EBUSY?"SD卡正忙，请稍后重试":error==ECANCELED?"删除已取消":"删除失败，请检查SD卡");
        if(deleteButton)c::disabled(deleteButton,false);
    }
    if(historyJob && historyJob->done.load(std::memory_order_acquire)) {
        historyGeneration=historyJob->cardGeneration;records=std::move(historyJob->page);historyJob.reset();if(page==Page::History)renderHistory();
    }
    if(page!=Page::Main)return;
    const auto net=sys::Connectivity::snapshot();const bool busy=job && !job->done.load(std::memory_order_acquire);
    char text[180],down[24],up[24];
    std::snprintf(text,sizeof(text),"Wi-Fi: %s",net.state==sys::WifiState::Connected?net.ssid.c_str():"未连接");
    if(net.state==sys::WifiState::Connected)std::snprintf(text,sizeof(text),"Wi-Fi: %s · %d dBm",net.ssid.c_str(),net.rssi);
    c::setText(wifi,text);
    rate(down,sizeof(down),speed::throughput(last.record.download,last.record.downloadCount));rate(up,sizeof(up),speed::throughput(last.record.upload,last.record.uploadCount));
    if(busy && last.liveValid && (last.phase==speed::Phase::Download || last.phase==speed::Phase::Upload))
        std::snprintf(last.phase==speed::Phase::Upload?up:down,sizeof(down),"%.2f",last.currentMbps);
    c::setText(download,down);c::setText(upload,up);
    lv_obj_set_style_text_font(download,std::strlen(down)>6?&lv_font_montserrat_14:&lv_font_montserrat_18,0);
    lv_obj_set_style_text_font(upload,std::strlen(up)>6?&lv_font_montserrat_14:&lv_font_montserrat_18,0);
    lv_obj_align(download,LV_ALIGN_LEFT_MID,44,0);lv_obj_align(upload,LV_ALIGN_LEFT_MID,44,0);
    const auto l=speed::latency(last.record);
    if(l.valid && l.jitterPairs)std::snprintf(text,sizeof(text),"HTTP延迟 %.0f ms · 抖动 %.0f ms",l.median,l.jitter);
    else if(l.valid)std::snprintf(text,sizeof(text),"HTTP延迟 %.0f ms · 抖动 --",l.median);
    else std::snprintf(text,sizeof(text),"HTTP延迟 -- ms · 抖动 --");
    if(last.record.qualityVersion) {
        const auto idle=speed::probeStats(last.record.probes[1]);
        const auto downPing=speed::probeStats(last.record.probes[3]);const auto upPing=speed::probeStats(last.record.probes[5]);
        char a[20],b[20],d[20];
        auto number=[](char *out,const speed::Latency &v){if(v.valid)std::snprintf(out,20,"%.0f",v.median);else std::strcpy(out,"--");};
        number(a,idle);number(b,downPing);number(d,upPing);
        std::snprintf(text,sizeof(text),"外网 空闲%s / 下载%s / 上传%s ms",a,b,d);
    }
    c::setText(latency,text);
    if(busy) {
        if(job->cancelled.load())std::snprintf(text,sizeof(text),"正在取消并释放连接");
        else if(last.phase==speed::Phase::Latency)std::snprintf(text,sizeof(text),"测延迟 %u/%u",last.sample,speed::LatencyCount);
        else if(last.phase==speed::Phase::Download || last.phase==speed::Phase::Upload)std::snprintf(text,sizeof(text),"%s #%u · %.0f/12 s",last.phase==speed::Phase::Upload?"测上传":"测下载",last.sample,double(last.directionElapsedUs)/1000000);
        else std::snprintf(text,sizeof(text),"%s",last.phase==speed::Phase::Saving?"保存到 SD 卡":"连接服务器 / 预热");
    }else if(job) {
        if(speed::complete(last.record))std::snprintf(text,sizeof(text),"测速完成 · %s",last.saved?"已保存 SD":last.storageError?"保存失败，点详情":"未保存");
        else if(last.record.qualityVersion && last.record.error==speed::Error::None)std::snprintf(text,sizeof(text),"采样不足 · 结果供参考");
        else std::snprintf(text,sizeof(text),"%s · 点详情",speed::errorText(last.record.error));
    }else std::snprintf(text,sizeof(text),"%s",notice.empty()?"约40~120秒，最多32 MB":notice.c_str());
    c::setText(state,text);buttonText(startButton,busy?"取消测速":job?"重新测速":"开始测速");refreshGraph(busy);
    lv_obj_set_style_text_color(state,job && !busy && !speed::complete(last.record)?ui::theme::warning():ui::theme::muted(),0);
    c::disabled(startButton,!busy && (net.state!=sys::WifiState::Connected || !speed::idle()));
    c::disabled(historyButton,busy);c::disabled(detailButton,busy || !job);c::disabled(methodButton,busy);
}
} // namespace apps
