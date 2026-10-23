#pragma once
#include "compact_ui.hpp"
#include "theme.hpp"
#include <algorithm>
#include <string>
#include <string_view>

LV_FONT_DECLARE(lv_font_code_13);

namespace ui::code {
// Shared by the paged reference reader and the freely scrolling lesson view.
// No timers, persistent buffers or App callbacks: the parent owns everything.
constexpr int Gutter=32, Inset=4, Top=2, RowHeight=18, Scrollbar=4;
constexpr size_t WrapColumns=(240-Gutter-2*Inset)/8;

inline unsigned commonIndent(std::string_view source) {
    size_t indent=source.size();
    bool found=false;
    for(size_t start=0;start<source.size();) {
        auto end=source.find('\n',start);
        if(end==std::string_view::npos)end=source.size();
        size_t first=start;
        while(first<end && source[first]==' ')++first;
        if(first<end){indent=std::min(indent,first-start);found=true;}
        start=end+1;
    }
    return found?unsigned(indent):0;
}

inline std::string unindent(std::string_view source,unsigned indent) {
    std::string result;result.reserve(source.size());
    for(size_t start=0;start<source.size();) {
        auto end=source.find('\n',start);
        if(end==std::string_view::npos)end=source.size();
        size_t first=start;
        while(first<end && first-start<indent && source[first]==' ')++first;
        result.append(source.data()+first,end-first);
        if(end<source.size())result+='\n';
        start=end+1;
    }
    return result;
}

inline std::string lineNumbers(std::string_view source,unsigned first=1) {
    std::string result=std::to_string(first);
    for(size_t i=0;i<source.size();++i)if(source[i]=='\n')result+='\n'+std::to_string(++first);
    return result;
}

inline lv_obj_t *text(lv_obj_t *parent,const std::string &source,bool numbers=false) {
    auto label=lv_label_create(parent);lv_obj_remove_style_all(label);
    lv_obj_set_style_text_font(label,&lv_font_code_13,0);
    lv_obj_set_style_text_color(label,numbers?theme::muted():theme::fg(),0);
    lv_obj_set_style_text_letter_space(label,0,0);
    lv_obj_set_style_text_line_space(label,RowHeight-lv_font_code_13.line_height,0);
    lv_obj_set_style_base_dir(label,LV_BASE_DIR_LTR,0);
    lv_label_set_text(label,source.c_str());
    lv_obj_set_size(label,LV_SIZE_CONTENT,LV_SIZE_CONTENT);
    lv_obj_set_pos(label,numbers?0:Inset,Top);
    lv_obj_set_gesture_bubble(label,true);
    if(numbers) {
        lv_obj_set_width(label,Gutter-Inset);
        lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_RIGHT,0);
    }
    return label;
}

struct View {lv_obj_t *viewport,*gutter,*source,*numbers;};
inline View create(lv_obj_t *parent,int width,int height,const std::string &source,
                   const std::string &numbers,lv_dir_t direction=LV_DIR_ALL) {
    // Create the gutter last, so viewport deletion cannot leave its callback
    // pointing to an already deleted line-number label during parent cleanup.
    auto viewport=compact::panel(parent,width-Gutter,height,direction!=LV_DIR_NONE);
    lv_obj_set_pos(viewport,Gutter,0);lv_obj_set_scroll_dir(viewport,direction);
    lv_obj_set_scroll_elastic(viewport,false);
    lv_obj_set_scroll_chain(viewport,false);
    lv_obj_set_gesture_bubble(viewport,true);
    lv_obj_set_style_pad_right(viewport,Inset,0);
    lv_obj_set_style_pad_bottom(viewport,Scrollbar,0);
    lv_obj_set_style_width(viewport,2,LV_PART_SCROLLBAR);
    lv_obj_set_style_height(viewport,2,LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(viewport,theme::accent(),LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(viewport,LV_OPA_60,LV_PART_SCROLLBAR);
    auto content=text(viewport,source);
    auto gutter=compact::panel(parent,Gutter,height);
    lv_obj_set_style_bg_color(gutter,theme::surface(),0);
    lv_obj_set_style_border_width(gutter,1,0);
    lv_obj_set_style_border_side(gutter,LV_BORDER_SIDE_RIGHT,0);
    lv_obj_set_style_border_color(gutter,theme::edge(),0);
    lv_obj_set_clickable(gutter,false);
    auto numberLabel=text(gutter,numbers,true);
    lv_obj_add_event_cb(viewport,[](lv_event_t *event) {
        auto numbers=static_cast<lv_obj_t *>(lv_event_get_user_data(event));
        lv_obj_set_y(numbers,Top-lv_obj_get_scroll_y(lv_event_get_target_obj(event)));
    },LV_EVENT_SCROLL,numberLabel);
    return {viewport,gutter,content,numberLabel};
}
}
