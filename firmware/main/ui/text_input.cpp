#include "text_input.hpp"
#include "compact_ui.hpp"
#include "core/power_mgr.hpp"
#include "core/ble_hid.hpp"
#include "core/ble_keyboard_client.hpp"
#include <algorithm>
#include <cstring>
#include <string>
namespace ui {
namespace {
namespace c=compact;
lv_obj_t *bar=nullptr,*type=nullptr,*character=nullptr,*target=nullptr,*viewport=nullptr;
int32_t viewportHeight=0;
bool initialized=false,shown=false;
uint32_t selectedType=0,selectedCharacter=0;
bool within(lv_obj_t *obj,lv_obj_t *ancestor) {
    if(!ancestor) return false;
    for(;obj;obj=lv_obj_get_parent(obj)) if(obj==ancestor) return true;
    return false;
}
void deleted(lv_event_t *e) {
    auto obj=lv_event_get_target_obj(e);
    if(obj==viewport) viewport=nullptr; // parent DELETE precedes child DELETE
    if(obj==target) { target=nullptr; TextInput::hide(); }
}
void populate(lv_event_t *) {
    static const char *sets[]={"0123456789"," !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~","ABCDEFGHIJKLMNOPQRSTUVWXYZ","abcdefghijklmnopqrstuvwxyz"};
    auto selected=std::min<uint32_t>(lv_dropdown_get_selected(type),3);
    std::string options;
    for(const char *p=sets[selected];*p;++p) {
        if(!options.empty()) options+='\n';
        options+=*p==' '?std::string("空格"):std::string(1,*p);
    }
    lv_dropdown_set_options(character,options.c_str());
}
void edit(lv_event_t *e) {
    if(!target || !shown || !sys::Power::isScreenOn()) return;
    if(lv_event_get_user_data(e)) lv_textarea_delete_char(target);
    else {
        char text[16]; lv_dropdown_get_selected_str(character,text,sizeof(text));
        lv_textarea_add_text(target,std::strcmp(text,"空格")==0?" ":text);
    }
}
void pointerClick(lv_event_t *e) {
    auto obj=static_cast<lv_obj_t *>(lv_event_get_param(e));
    if(!obj) return;
    // Dropdown lists live on the top layer, not beneath the bar.
    if(bar && (within(obj,bar) || within(obj,lv_dropdown_get_list(type)) || within(obj,lv_dropdown_get_list(character)))) return;
    for(auto field=obj;field;field=lv_obj_get_parent(field)) {
        if(lv_obj_check_type(field,&lv_textarea_class)) { TextInput::show(field); return; }
    }
    TextInput::hide();
}
void createBar() {
    using L=TextInputLayout;
    static_assert(c::kGap==L::Gap && c::kInput==L::Height);
    bar=c::panel(lv_layer_top(),L::Width,L::Height);
    lv_obj_align(bar,LV_ALIGN_BOTTOM_MID,0,0); lv_obj_set_flex_flow(bar,LV_FLEX_FLOW_COLUMN);
    auto selectors=c::panel(bar,L::Width,L::RowHeight);
    lv_obj_set_flex_flow(selectors,LV_FLEX_FLOW_ROW);
    type=c::dropdown(selectors,"123\n!@#\nABC\nabc",L::LeftWidth);
    character=c::dropdown(selectors,"0",L::RightWidth);
    // Centre text in the enlarged hit areas; bound popups above the bar.
    for(auto control : {type,character}) {
        lv_obj_set_height(control,L::RowHeight);
        const auto font=lv_obj_get_style_text_font(control,LV_PART_MAIN);
        const int pad=std::max(0,(L::RowHeight-int(font->line_height))/2);
        lv_obj_set_style_pad_top(control,pad,0); lv_obj_set_style_pad_bottom(control,pad,0);
        lv_obj_set_style_max_height(lv_dropdown_get_list(control),L::Top-L::Gap,0);
    }
    lv_dropdown_set_symbol(character,nullptr);
    lv_obj_add_event_cb(type,populate,LV_EVENT_VALUE_CHANGED,nullptr);
    auto actions=c::panel(bar,L::Width,L::RowHeight);
    lv_obj_set_flex_flow(actions,LV_FLEX_FLOW_ROW);
    c::button(actions,"删除",edit,reinterpret_cast<void *>(1),L::LeftWidth,L::RowHeight);
    c::button(actions,"输入",edit,nullptr,L::RightWidth,L::RowHeight);
    lv_dropdown_set_selected(type,selectedType);
    populate(nullptr);
    lv_dropdown_set_selected(character,selectedCharacter);
    lv_obj_set_hidden(bar,true);
}
}
void TextInput::init(lv_indev_t *pointer) {
    if(initialized || !pointer) return;
    // Only the pointer hook lives for the system lifetime. Widgets are built
    // on first input and disposed when dismissed or when an App leaves.
    lv_indev_add_event_cb(pointer,pointerClick,LV_EVENT_CLICKED,nullptr);
    initialized=true;
}
void TextInput::bindField(lv_obj_t *field) {
    lv_obj_add_event_cb(field,[](lv_event_t *e) {
        // H-mode focus only selects a field; A opens its on-screen editor.
        if(!sys::BleKeyboardClient::iineKeyboard()) TextInput::show(lv_event_get_target_obj(e));
    },LV_EVENT_FOCUSED,nullptr);
}
void TextInput::show(lv_obj_t *field) {
    if(sys::BleHid::keyboardConnected() && !sys::BleKeyboardClient::iineKeyboard()) { hide();return; }
    if(!initialized || !field || !sys::Power::isScreenOn() || lv_obj_has_state(field,LV_STATE_DISABLED)) return;
    if(field==target && shown) return;
    hide(); createBar(); target=field; shown=true;
    lv_obj_add_event_cb(target,deleted,LV_EVENT_DELETE,nullptr);
    lv_obj_add_state(target,LV_STATE_FOCUSED);
    lv_obj_set_hidden(bar,false); lv_obj_move_foreground(bar);
    // Reserve the bar's space in the nearest scrolling content area. Never
    // resize a textarea, app root or the persistent system display itself.
    for(auto parent=lv_obj_get_parent(field);parent && parent!=lv_screen_active();parent=lv_obj_get_parent(parent)) {
        if(lv_obj_is_scrollable(parent)) { viewport=parent; break; }
    }
    if(viewport) {
        lv_obj_update_layout(viewport);
        viewportHeight=lv_obj_get_style_height(viewport,LV_PART_MAIN);
        lv_area_t area; lv_obj_get_coords(viewport,&area);
        lv_obj_set_height(viewport,TextInputLayout::viewport(lv_obj_get_height(viewport),area.y1));
        lv_obj_add_event_cb(viewport,deleted,LV_EVENT_DELETE,nullptr);
        lv_obj_update_layout(viewport); lv_obj_scroll_to_view(field,LV_ANIM_OFF);
    }
}
void TextInput::hide() {
    if(!bar) return;
    selectedType=lv_dropdown_get_selected(type);
    selectedCharacter=lv_dropdown_get_selected(character);
    if(type) lv_dropdown_close(type);
    if(character) lv_dropdown_close(character);
    shown=false;
    if(target) { lv_obj_remove_event_cb(target,deleted); lv_obj_remove_state(target,LV_STATE_FOCUSED); target=nullptr; }
    if(viewport) { lv_obj_remove_event_cb(viewport,deleted); lv_obj_set_height(viewport,viewportHeight); viewport=nullptr; }
    auto oldBar=bar;
    bar=type=character=nullptr;
    lv_obj_delete(oldBar); // Includes dropdown popups, labels and their buffers.
}
bool TextInput::visible() { return shown; }
lv_obj_t *TextInput::root() { return shown?bar:nullptr; }
} // namespace ui
