#include "settings.hpp"
#include "ui/compact_ui.hpp"
#include "theme.hpp"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include <array>
#include <cstdio>

namespace apps {
namespace c=ui::compact;
namespace {
constexpr int CardWidth=228, ContentWidth=212, BarWidth=212;
constexpr uint64_t InternalBytes=512*1024; // ESP32-S3 SRAM; RTC RAM excluded.
constexpr unsigned Counts[]={3,3,5,3};
constexpr unsigned Colors[][5]={{2,0,1},{2,0,1},{0,1,2,3,4},{0,1,4}};
lv_color_t storageColor(unsigned index) {
    // Same meaning in every theme, with contrast adjusted for light surfaces.
    constexpr uint32_t dark[]={0x65B5FF,0x50D6AC,0xBD9BFF,0xF4BE65,0x9DAABD};
    constexpr uint32_t light[]={0x236FC1,0x168060,0x8150B5,0xAD6508,0x677489};
    return lv_color_hex((ui::theme::light()?light:dark)[index]);
}
void capacity(char *text,size_t size,uint64_t bytes) {
    const uint64_t unit=bytes>=uint64_t(1024)*1024*1024?uint64_t(1024)*1024*1024:
                        bytes>=1024*1024?1024*1024:1024;
    const char *name=unit==uint64_t(1024)*1024*1024?"GiB":unit==1024*1024?"MiB":"KiB";
    // Truncate rather than round free space upward. No floating point needed.
    std::snprintf(text,size,"%llu.%02llu %s",static_cast<unsigned long long>(bytes/unit),
        static_cast<unsigned long long>(bytes%unit*100/unit),name);
}
lv_obj_t *smallLabel(lv_obj_t *parent,const char *text,int width) {
    auto label=c::label(parent,text,width);
    // Full font: descriptions must not depend on a sparse menu glyph subset.
    lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(label,2,0);
    return label;
}
} // namespace

void Settings::buildStorageOverview() {
    auto area=c::panel(v.body,240,LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(area,ui::theme::edge(),0);
    lv_obj_set_style_radius(area,10,0);
    lv_obj_set_style_pad_all(area,6,0);
    lv_obj_set_style_pad_row(area,6,0);
    lv_obj_set_flex_flow(area,LV_FLEX_FLOW_COLUMN);
    auto heading=c::panel(area,CardWidth,34);
    lv_obj_set_style_bg_opa(heading,LV_OPA_TRANSP,0);
    auto title=c::label(heading,LV_SYMBOL_SD_CARD " 存储空间",160);
    lv_obj_align(title,LV_ALIGN_LEFT_MID,4,0);
    v.aboutStorageRefresh=c::button(heading,LV_SYMBOL_REFRESH,storageRefreshCb,this,44,34);
    lv_obj_align(v.aboutStorageRefresh,LV_ALIGN_RIGHT_MID,0,0);
    const char *names[]={"内部 RAM","外部 PSRAM","内置 Flash","SD 存储卡"};
    const char *notes[]={
        "运行任务与驱动。固定部分含系统、缓存及内存管理开销。",
        "存放图像、应用及代码映射。退出应用仅释放动态分配的部分。",
        "余量指固件可增长空间。资源区与密码区专用，不能合并使用。",
        "文件保存在卡内；剩余指文件系统可写空间。点刷新重新统计。"};
    for(unsigned i=0;i<v.aboutStorage.size();++i) {
        auto &view=v.aboutStorage[i];
        auto card=c::panel(area,CardWidth,LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card,ui::theme::surface(),0);
        lv_obj_set_style_radius(card,8,0);
        lv_obj_set_style_pad_all(card,8,0);
        lv_obj_set_style_pad_row(card,5,0);
        lv_obj_set_flex_flow(card,LV_FLEX_FLOW_COLUMN);
        auto name=c::label(card,names[i],ContentWidth);
        lv_obj_set_style_text_color(name,ui::theme::fg(),0);
        auto metrics=c::panel(card,ContentWidth,44);
        lv_obj_set_style_bg_opa(metrics,LV_OPA_TRANSP,0);
        auto total=c::label(metrics,"总容量",102);lv_obj_set_pos(total,0,0);
        auto free=c::label(metrics,i==2?"固件剩余":"剩余可用",104);lv_obj_set_pos(free,108,0);
        lv_obj_set_style_text_color(total,ui::theme::muted(),0);
        lv_obj_set_style_text_color(free,ui::theme::muted(),0);
        view.total=c::label(metrics,"--",104);lv_obj_set_pos(view.total,0,23);
        view.free=c::label(metrics,"--",104);lv_obj_set_pos(view.free,108,23);
        lv_obj_set_style_text_font(view.total,&lv_font_montserrat_14,0);
        lv_obj_set_style_text_font(view.free,&lv_font_montserrat_14,0);
        lv_obj_set_style_text_color(view.free,storageColor(1),0);
        auto bar=c::panel(card,BarWidth,8);
        lv_obj_set_style_bg_color(bar,ui::theme::edge(),0);
        lv_obj_set_style_radius(bar,4,0);lv_obj_set_style_clip_corner(bar,true,0);
        for(unsigned n=0;n<Counts[i];++n) {
            auto segment=c::panel(bar,1,8);
            lv_obj_set_style_bg_color(segment,storageColor(Colors[i][n]),0);
            lv_obj_add_flag(segment,LV_OBJ_FLAG_HIDDEN);view.segments[n]=segment;
            auto row=c::panel(card,ContentWidth,LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);
            auto dot=c::panel(row,6,6);lv_obj_set_pos(dot,0,6);
            lv_obj_set_style_radius(dot,2,0);lv_obj_set_style_bg_color(dot,storageColor(Colors[i][n]),0);
            view.details[n]=smallLabel(row,"--",ContentWidth-12);lv_obj_set_pos(view.details[n],12,0);
        }
        view.note=smallLabel(card,notes[i],ContentWidth);
        lv_obj_set_style_text_color(view.note,ui::theme::muted(),0);
        if(i==0) {
            v.aboutMemoryDetail=smallLabel(card,"",ContentWidth);
            lv_obj_set_style_text_color(v.aboutMemoryDetail,ui::theme::muted(),0);
        }
    }
    aboutStorageRead=core::inspectStorage();
}

void Settings::storageRefreshCb(lv_event_t *event) {
    auto self=static_cast<Settings *>(lv_event_get_user_data(event));
    if(self->aboutStorageRead && !self->aboutStorageRead->done.load(std::memory_order_acquire))return;
    self->aboutStorageRead=core::inspectStorage();
    self->aboutUsage.sdValid=false;
    self->aboutRefreshed=0;
    self->refreshStorageOverview();
}

void Settings::refreshStorageOverview() {
    auto value=[](lv_obj_t *label,uint64_t bytes) {char text[32];capacity(text,sizeof(text),bytes);c::setText(label,text);};
    auto detail=[&](unsigned card,unsigned row,const char *name,uint64_t bytes) {
        char text[112],size[32];capacity(size,sizeof(size),bytes);
        std::snprintf(text,sizeof(text),"%s  %s",name,size);c::setText(v.aboutStorage[card].details[row],text);
    };
    auto bar=[&](unsigned card,uint64_t total,const std::array<uint64_t,5> &parts) {
        uint64_t sum=0;int left=0;
        for(unsigned n=0;n<Counts[card];++n) {
            auto segment=v.aboutStorage[card].segments[n];sum+=parts[n];
            const int right=core::storageEdge(sum,total,BarWidth);
            if(right<=left) lv_obj_add_flag(segment,LV_OBJ_FLAG_HIDDEN);
            else {lv_obj_remove_flag(segment,LV_OBJ_FLAG_HIDDEN);lv_obj_set_pos(segment,left,0);lv_obj_set_width(segment,right-left);}
            left=right;
        }
    };
    constexpr uint32_t caps[]={MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT};
    for(unsigned i=0;i<2;++i) {
        multi_heap_info_t heap{};heap_caps_get_info(&heap,caps[i]);
        const auto usage=core::memoryUsage(i?esp_psram_get_size():InternalBytes,
            heap.total_free_bytes+heap.total_allocated_bytes,heap.total_free_bytes);
        auto &view=v.aboutStorage[i];value(view.total,usage.total);value(view.free,usage.free);
        detail(i,0,i?"映射 / 固定":"系统 / 固定",usage.fixed);
        detail(i,1,"动态已用",usage.used);detail(i,2,"可分配剩余",usage.free);
        bar(i,usage.total,{usage.fixed,usage.used,usage.free,0,0});
        if(!usage.total) {c::setText(view.total,"--");c::setText(view.free,"--");c::setText(view.note,"存储器未初始化");}
    }
    const bool pending=aboutStorageRead && !aboutStorageRead->done.load(std::memory_order_acquire);
    if(pending) lv_obj_add_state(v.aboutStorageRefresh,LV_STATE_DISABLED);
    else lv_obj_remove_state(v.aboutStorageRefresh,LV_STATE_DISABLED);
    if(aboutStorageRead && !pending) {
        // Completion releases the worker result; no App pointer ever enters I/O.
        aboutUsage=aboutStorageRead->usage;aboutSdEpoch=aboutStorageRead->sdAccess.epoch;
        if(!aboutUsage.sdValid) c::setText(v.aboutStorage[3].note,
            aboutStorageRead->error?core::storageError(aboutStorageRead->error):"暂未取得容量，请刷新重试");
        aboutStorageRead.reset();
    }
    auto &flash=v.aboutStorage[2];
    const auto &u=aboutUsage;
    if(u.flash) {
        c::setText(flash.note,"余量指固件可增长空间。资源区与密码区专用，不能合并使用。");
        value(flash.total,u.flash);
        const auto free=u.imageValid?u.firmware-u.image:0;
        const auto allocated=u.firmware+u.assets+u.vault;
        const auto system=u.flash>allocated?u.flash-allocated:0;
        if(u.imageValid) {value(flash.free,free);detail(2,0,"固件已用",u.image);detail(2,1,"固件剩余",free);}
        else {c::setText(flash.free,"--");detail(2,0,"固件容量",u.firmware);c::setText(flash.details[1],"固件余量读取失败");}
        detail(2,2,"资源预留",u.assets);detail(2,3,"密码专用",u.vault);detail(2,4,"系统 / 保留",system);
        bar(2,u.flash,{u.imageValid?u.image:u.firmware,free,u.assets,u.vault,system});
    } else c::setText(flash.note,pending?"正在读取分区信息":"分区信息不可用，请刷新重试");
    auto &sd=v.aboutStorage[3];
    const auto state=core::sd::state();
    if(state!=core::sd::State::Available || core::sd::usbExclusive() || aboutSdEpoch!=core::sd::requestAccess().epoch)
        aboutUsage.sdValid=false;
    if(aboutUsage.sdValid) {
        value(sd.total,u.sdPhysical);value(sd.free,u.sdFree);
        detail(3,0,"文件已用",u.sdTotal-u.sdFree);detail(3,1,"可写剩余",u.sdFree);
        detail(3,2,"格式 / 保留",u.sdPhysical-u.sdTotal);
        bar(3,u.sdPhysical,{u.sdTotal-u.sdFree,u.sdFree,u.sdPhysical-u.sdTotal,0,0});
        c::setText(sd.note,"卡内文件系统余量；格式与分区占用已单列。点刷新重新统计。");
    } else {
        c::setText(sd.total,"--");c::setText(sd.free,"--");bar(3,0,{});
        for(unsigned n=0;n<Counts[3];++n)c::setText(sd.details[n],"--");
        if(core::sd::usbExclusive() || state==core::sd::State::Usb)c::setText(sd.note,"电脑正在使用 SD 卡，容量暂不可读。");
        else if(state==core::sd::State::Ejected)c::setText(sd.note,"SD 卡已安全弹出，请先在系统与电源中重新读取。");
        else if(state==core::sd::State::Ejecting)c::setText(sd.note,"正在安全弹出 SD 卡");
        else if(pending)c::setText(sd.note,"正在统计 SD 卡容量…");
    }
}
} // namespace apps
