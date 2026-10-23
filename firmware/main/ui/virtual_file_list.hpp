#pragma once
#include "compact_ui.hpp"
#include "theme.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ui {
// GUI-thread only. Data stays with the App; the widget owns at most eight rows.
class VirtualFileList {
public:
    using Caption = std::string (*)(void *, size_t);
    using Activate = void (*)(void *, size_t);
    static constexpr size_t PoolSize=8;
    static constexpr int RowHeight=38;
    void attach(lv_obj_t *viewport, void *context, Caption caption, Activate activate) {
        viewport_=viewport;context_=context;caption_=caption;activate_=activate;
        lv_obj_set_layout(viewport_,0);
        content_=lv_obj_create(viewport_);lv_obj_remove_style_all(content_);
        lv_obj_set_width(content_,LV_PCT(100));
        lv_obj_set_scrollable(content_,false);
        for(auto &row:rows_) {
            row.owner=this;
            row.button=compact::button(content_,"?",nullptr,nullptr,LV_PCT(100),RowHeight);
            row.label=lv_obj_get_child(row.button,0);
            lv_obj_set_width(row.label,234);
            lv_label_set_long_mode(row.label,LV_LABEL_LONG_MODE_DOTS);
            lv_obj_center(row.label);
            lv_obj_add_event_cb(row.button,rowEvent,LV_EVENT_ALL,&row);
            lv_obj_set_hidden(row.button,true);
        }
        footer_=makeMessage(content_);
        message_=makeMessage(viewport_);
        lv_obj_set_floating(message_,true);
        lv_obj_add_event_cb(viewport_,scrollEvent,LV_EVENT_SCROLL,this);
        lv_obj_set_hidden(content_,true);
    }
    void detach() {
        if(viewport_)lv_obj_remove_event_cb_with_user_data(viewport_,scrollEvent,this);
        for(auto &row:rows_)if(row.button)lv_obj_remove_event_cb_with_user_data(row.button,rowEvent,&row);
        viewport_=content_=footer_=message_=nullptr;
        for(auto &row:rows_)row={};
    }
    // Call before replacing directory data, including a failed/cancelled scan.
    void reset() {
        savedY_=0;count_=0;++revision_;invalidatePress();
        if(viewport_) {
            updating_=true;
            lv_obj_set_hidden(content_,true);
            lv_obj_scroll_to_y(viewport_,0,LV_ANIM_OFF);
            updating_=false;
        }
    }
    void show(size_t count, const char *footer=nullptr) {
        if(!viewport_)return;
        updating_=true;++revision_;invalidatePress();count_=count;
        const int pitch=RowHeight+theme::kGap;
        const int rowsHeight=count?static_cast<int>(count)*pitch-theme::kGap:0;
        lv_obj_set_hidden(message_,true);
        lv_obj_set_hidden(content_,false);
        int height=rowsHeight;
        if(footer && *footer) {
            lv_label_set_text(footer_,footer);lv_obj_set_pos(footer_,0,rowsHeight+theme::kGap);
            lv_obj_set_hidden(footer_,false);lv_obj_update_layout(footer_);
            height+=theme::kGap+lv_obj_get_height(footer_);
        } else lv_obj_set_hidden(footer_,true);
        lv_obj_set_height(content_,std::max(height,1));
        for(auto &row:rows_)row.index=Invalid;
        lv_obj_update_layout(viewport_);
        lv_obj_scroll_to_y(viewport_,std::min<int32_t>(savedY_,std::max<int32_t>(0,height-lv_obj_get_content_height(viewport_))),LV_ANIM_OFF);
        updating_=false;render();
    }
    void message(const char *text) {
        if(!viewport_)return;
        if(!lv_obj_is_hidden(content_))savedY_=std::max<int32_t>(0,lv_obj_get_scroll_y(viewport_));
        updating_=true;++revision_;invalidatePress();
        lv_obj_set_hidden(content_,true);
        lv_label_set_text(message_,text);lv_obj_set_hidden(message_,false);
        lv_obj_scroll_to_y(viewport_,0,LV_ANIM_OFF);updating_=false;
    }
private:
    static constexpr size_t Invalid=static_cast<size_t>(-1);
    struct Row {
        VirtualFileList *owner=nullptr;
        lv_obj_t *button=nullptr,*label=nullptr;
        size_t index=Invalid,pressed=Invalid;
        uint32_t revision=0;
    };
    std::array<Row,PoolSize> rows_{};
    lv_obj_t *viewport_=nullptr,*content_=nullptr,*footer_=nullptr,*message_=nullptr;
    void *context_=nullptr;Caption caption_=nullptr;Activate activate_=nullptr;
    size_t count_=0;uint32_t revision_=0;int savedY_=0;bool updating_=false;
    static lv_obj_t *makeMessage(lv_obj_t *parent) {
        auto label=lv_label_create(parent);lv_obj_set_width(label,LV_PCT(100));
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_color(label,theme::muted(),0);
        lv_obj_set_style_text_font(label,theme::fontCjkFull(),0);
        lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_hidden(label,true);return label;
    }
    void invalidatePress() { for(auto &row:rows_)row.pressed=Invalid; }
    void render() {
        if(updating_ || lv_obj_is_hidden(content_))return;
        updating_=true;
        savedY_=std::max<int32_t>(0,lv_obj_get_scroll_y(viewport_));
        const int pitch=RowHeight+theme::kGap;
        const size_t visible=static_cast<size_t>(savedY_/pitch);
        const size_t start=std::min(visible?visible-1:0,count_>PoolSize?count_-PoolSize:0);
        for(size_t i=0;i<rows_.size();++i) {
            auto &row=rows_[i];const size_t index=start+i;
            if(index>=count_) {row.index=Invalid;lv_obj_set_hidden(row.button,true);continue;}
            if(row.index!=index) {
                row.pressed=Invalid;row.index=index;
                lv_obj_remove_state(row.button,static_cast<lv_state_t>(LV_STATE_PRESSED|LV_STATE_CHECKED|LV_STATE_FOCUSED));
                const auto text=caption_(context_,index);
                lv_label_set_text(row.label,text.empty()?"?":text.c_str());
                lv_obj_set_pos(row.button,0,static_cast<int>(index)*pitch);
            }
            lv_obj_set_hidden(row.button,false);
        }
        updating_=false;
    }
    static void scrollEvent(lv_event_t *event) {
        auto self=static_cast<VirtualFileList *>(lv_event_get_user_data(event));
        if(!self->updating_){self->invalidatePress();self->render();}
    }
    static void rowEvent(lv_event_t *event) {
        auto row=static_cast<Row *>(lv_event_get_user_data(event));auto self=row->owner;
        const auto code=lv_event_get_code(event);
        if(code==LV_EVENT_PRESSED){row->pressed=row->index;row->revision=self->revision_;}
        else if(code==LV_EVENT_PRESS_LOST)row->pressed=Invalid;
        else if(code==LV_EVENT_CLICKED) {
            const size_t index=row->pressed;row->pressed=Invalid;
            if(index==row->index && index<self->count_ && row->revision==self->revision_)
                self->activate_(self->context_,index);
        }
    }
};
} // namespace ui
