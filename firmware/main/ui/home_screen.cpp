/* Watch-style home screen implementation. */
#include "home_screen.hpp"
#include "window_mgr.hpp"
#include "theme.hpp"
#include "compact_ui.hpp"
#include "focus_pager.hpp"
#include "app_icons.hpp"
#include "core/power_mgr.hpp"
#include "core/settings_store.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

namespace ui {

static lv_obj_t *s_app_list = nullptr;
namespace {
lv_obj_t *desktop=nullptr,*allApps=nullptr,*homeTime=nullptr,*homeDate=nullptr;
lv_obj_t *desktopTab=nullptr,*appsTab=nullptr;
std::vector<lv_obj_t *> terminalRows;
size_t cursor=0;
FocusPager focusApps;
namespace orderModel=sys::app_order;
orderModel::Order menuOrder,dragBefore;
struct MenuRow {lv_obj_t *object;size_t app;lv_obj_t *number=nullptr;};
std::vector<MenuRow> menuRows;
lv_obj_t *homeParent=nullptr,*menuList=nullptr,*arrangePage=nullptr,*ghost=nullptr,*noticeBox=nullptr,*noticeLabel=nullptr;
lv_obj_t *pressRow=nullptr;
lv_indev_t *dragInput=nullptr;
lv_timer_t *tileTimer=nullptr;
lv_point_t pressPoint{},dragPoint{},grabOffset{};
size_t draggedApp=0;
unsigned menuColumns=2;
int menuRowHeight=53;
bool dragging=false,pressMoved=false,suppressClick=false,arrangePending=false,closeArrangePending=false;
bool waitingSave=false;
uint32_t nextDragTick=0,noticeUntil=0;
uint32_t focusSelectedId=0;
orderModel::SaveState shownSave=orderModel::SaveState::Saved;

size_t registeredAt(size_t position){
    const auto &apps=WindowManager::instance().apps();
    if(position<menuOrder.count)for(size_t i=0;i<apps.size();++i)if(apps[i].id==menuOrder.ids[position])return i;
    return apps.size();
}
size_t positionOf(size_t app){
    const auto &apps=WindowManager::instance().apps();
    return app<apps.size()?menuOrder.find(apps[app].id):menuOrder.count;
}
void openPosition(size_t position){if(!dragging&&!suppressClick)WindowManager::instance().openApp(registeredAt(position));}
void clearNotice(){if(noticeBox)lv_obj_delete(noticeBox);noticeBox=noticeLabel=nullptr;noticeUntil=0;}
void notice(const char *text,uint32_t duration=1500){
    if(!homeParent)return;
    if(!noticeBox){
        noticeBox=compact::panel(homeParent,228,26);if(!noticeBox)return;
        lv_obj_set_pos(noticeBox,6,0);lv_obj_set_clickable(noticeBox,false);
        lv_obj_set_style_bg_color(noticeBox,theme::surface(),0);
        lv_obj_set_style_border_width(noticeBox,1,0);lv_obj_set_style_border_color(noticeBox,theme::accent(),0);
        noticeLabel=compact::label(noticeBox,"",220);lv_obj_center(noticeLabel);
        lv_obj_set_style_text_align(noticeLabel,LV_TEXT_ALIGN_CENTER,0);
    }
    compact::setText(noticeLabel,text);lv_obj_move_foreground(noticeBox);
    noticeUntil=duration?lv_tick_get()+duration:0;
}
void translateX(void *obj,int32_t value){lv_obj_set_style_translate_x(static_cast<lv_obj_t *>(obj),value,0);}
void translateY(void *obj,int32_t value){lv_obj_set_style_translate_y(static_cast<lv_obj_t *>(obj),value,0);}
void stopShift(lv_obj_t *obj){
    lv_anim_delete(obj,translateX);lv_anim_delete(obj,translateY);
    translateX(obj,0);translateY(obj,0);
}
void shift(lv_obj_t *obj,int x,int y){
    auto animate=[&](lv_anim_exec_xcb_t cb,int start){
        if(!start)return;
        cb(obj,start);lv_anim_t anim;lv_anim_init(&anim);lv_anim_set_var(&anim,obj);
        lv_anim_set_exec_cb(&anim,cb);lv_anim_set_values(&anim,start,0);lv_anim_set_duration(&anim,110);
        lv_anim_set_path_cb(&anim,lv_anim_path_ease_out);lv_anim_start(&anim);
    };
    animate(translateX,x);animate(translateY,y);
}
void refreshRows(){
    if(!theme::terminal()||arrangePage)return;
    terminalRows.clear();
    for(size_t i=0;i<menuRows.size();++i){
        terminalRows.push_back(menuRows[i].object);
        if(menuRows[i].number)lv_label_set_text_fmt(menuRows[i].number,"%02u",unsigned(i+1));
    }
}
void insertRow(size_t from,size_t to,bool animate){
    if(from==to||from>=menuRows.size()||to>=menuRows.size())return;
    std::array<lv_point_t,orderModel::Capacity> old{};
    for(size_t i=0;i<menuRows.size();++i){
        auto obj=menuRows[i].object;stopShift(obj);lv_area_t a;lv_obj_get_coords(obj,&a);old[menuRows[i].app]={a.x1,a.y1};
    }
    auto row=menuRows[from];
    if(from<to)std::rotate(menuRows.begin()+from,menuRows.begin()+from+1,menuRows.begin()+to+1);
    else std::rotate(menuRows.begin()+to,menuRows.begin()+from,menuRows.begin()+from+1);
    lv_obj_move_to_index(row.object,int(to));lv_obj_update_layout(menuList);
    if(animate)for(const auto &item:menuRows){
        if(item.object==pressRow)continue;
        lv_area_t a;lv_obj_get_coords(item.object,&a);
        shift(item.object,old[item.app].x-a.x1,old[item.app].y-a.y1);
    }
    refreshRows();
}
void finishDrag(bool commit){
    if(!dragging)return;
    const auto from=menuOrder.find(WindowManager::instance().apps()[draggedApp].id);
    if(!commit){
        const auto to=dragBefore.find(WindowManager::instance().apps()[draggedApp].id);
        insertRow(from,to,false);menuOrder=dragBefore;
    }
    for(const auto &row:menuRows)stopShift(row.object);
    if(pressRow){lv_obj_set_style_opa(pressRow,LV_OPA_COVER,0);lv_obj_remove_state(pressRow,LV_STATE_PRESSED);}
    if(ghost)lv_obj_delete(ghost);
    ghost=nullptr;
    lv_obj_set_scrollable(menuList,true);dragging=false;dragInput=nullptr;pressRow=nullptr;suppressClick=true;
    if(theme::terminal()&&!arrangePage){
        cursor=positionOf(draggedApp);
        for(size_t i=0;i<terminalRows.size();++i)compact::checked(terminalRows[i],i==cursor);
    }
    if(commit&&!(menuOrder==dragBefore)){
        if(sys::Settings::setAppOrder(menuOrder)){
            waitingSave=true;shownSave=orderModel::SaveState::Pending;notice("正在保存顺序",0);
        }else notice("无法保存顺序");
    }else clearNotice();
}
void dragTick(){
    if(!dragging||!menuList)return;
    lv_indev_get_point(dragInput,&dragPoint);
    lv_area_t area;lv_obj_get_coords(menuList,&area);
    lv_area_t parentArea;lv_obj_get_coords(homeParent,&parentArea);
    const int width=lv_obj_get_width(ghost),height=lv_obj_get_height(ghost);
    lv_obj_set_pos(ghost,std::clamp(int(dragPoint.x-grabOffset.x-parentArea.x1),0,240-width),
        std::clamp(int(dragPoint.y-grabOffset.y-parentArea.y1),0,int(lv_obj_get_height(homeParent))-height));
    const auto now=lv_tick_get();if(int32_t(now-nextDragTick)<0)return;nextDragTick=now+30;
    const int edge=24;
    int step=0;
    if(dragPoint.y<area.y1+edge)step=-std::clamp(int((area.y1+edge-dragPoint.y)/3),1,8);
    else if(dragPoint.y>area.y2-edge)step=std::clamp(int((dragPoint.y-area.y2+edge)/3),1,8);
    const int total=int((menuRows.size()+menuColumns-1)/menuColumns)*(menuRowHeight+theme::kGap)-theme::kGap;
    const int maxScroll=std::max(0,total-int(lv_obj_get_height(menuList)));
    if(step)lv_obj_scroll_to_y(menuList,std::clamp(int(lv_obj_get_scroll_y(menuList))+step,0,maxScroll),LV_ANIM_OFF);
    if(dragPoint.x<area.x1||dragPoint.x>area.x2)return;
    const int y=std::clamp(int(dragPoint.y),int(area.y1),int(area.y2))-area.y1+lv_obj_get_scroll_y(menuList);
    const int col=menuColumns==2&&dragPoint.x>=area.x1+120?1:0;
    size_t to=std::min(size_t(std::max(0,y)/(menuRowHeight+theme::kGap)*int(menuColumns)+col),menuRows.size()-1);
    if(menuOrder.ids[to]==orderModel::Settings){if(!to)return;--to;}
    const auto from=positionOf(draggedApp);
    if(orderModel::move(menuOrder,from,to))insertRow(from,to,true);
}
void beginDrag(lv_obj_t *obj,lv_indev_t *input){
    if(!input||lv_indev_get_type(input)!=LV_INDEV_TYPE_POINTER||pressMoved||lv_indev_get_scroll_obj(input)||menuRows.size()<2)return;
    auto found=std::find_if(menuRows.begin(),menuRows.end(),[&](const MenuRow &row){return row.object==obj;});
    if(found==menuRows.end())return;
    suppressClick=true;
    const auto &app=WindowManager::instance().apps()[found->app];
    if(app.id==orderModel::Settings){notice("设置固定在最后");return;}
    ghost=compact::panel(homeParent,lv_obj_get_width(obj),lv_obj_get_height(obj));if(!ghost)return;
    lv_obj_set_clickable(ghost,false);lv_obj_set_style_radius(ghost,theme::radius(),0);
    lv_obj_set_style_bg_color(ghost,theme::surface(),0);
    lv_obj_set_style_border_width(ghost,2,0);lv_obj_set_style_border_color(ghost,theme::accent(),0);
    auto icon=compact::label(ghost,app.icon,32);lv_obj_set_style_text_font(icon,icons::font24(),0);lv_obj_align(icon,LV_ALIGN_LEFT_MID,5,0);
    auto name=compact::label(ghost,app.name,lv_obj_get_width(obj)-48);lv_obj_set_style_text_font(name,theme::fontMenu(),0);lv_obj_align(name,LV_ALIGN_LEFT_MID,40,0);
    lv_indev_get_point(input,&dragPoint);lv_area_t a;lv_obj_get_coords(obj,&a);
    grabOffset={dragPoint.x-a.x1,dragPoint.y-a.y1};dragInput=input;pressRow=obj;draggedApp=found->app;
    dragBefore=menuOrder;dragging=true;nextDragTick=0;
    lv_obj_set_scrollable(menuList,false);lv_obj_set_style_opa(obj,LV_OPA_30,0);
    notice("拖动调整 · 松手保存",0);lv_obj_move_foreground(ghost);dragTick();
}
void menuEvent(lv_event_t *event){
    auto obj=lv_event_get_target_obj(event);const auto code=lv_event_get_code(event);auto input=lv_indev_active();
    if(code==LV_EVENT_PRESSED){
        suppressClick=false;pressMoved=false;pressRow=obj;
        if(input)lv_indev_get_point(input,&pressPoint);
    }
    if(code==LV_EVENT_PRESSING&&input&&!dragging){
        lv_point_t point;lv_indev_get_point(input,&point);
        if(std::abs(point.x-pressPoint.x)>8||std::abs(point.y-pressPoint.y)>8)pressMoved=true;
    }
    if(code==LV_EVENT_LONG_PRESSED&&!dragging)beginDrag(obj,input);
    if(code==LV_EVENT_RELEASED&&dragging&&obj==pressRow)finishDrag(true);
    if(code==LV_EVENT_PRESS_LOST){pressMoved=suppressClick=true;if(dragging)finishDrag(false);}
}
void bindRow(lv_obj_t *obj,size_t app,lv_obj_t *number=nullptr){
    menuRows.push_back({obj,app,number});lv_obj_set_press_lock(obj,true);lv_obj_set_gesture_bubble(obj,false);
    lv_obj_add_event_cb(obj,menuEvent,LV_EVENT_ALL,nullptr);
}
size_t appIndex(const char *name) {
    const auto &apps=WindowManager::instance().apps();
    for(size_t i=0;i<apps.size();++i) if(std::strcmp(apps[i].name,name)==0) return i;
    return apps.size();
}
void openNamed(lv_event_t *event) {
    WindowManager::instance().openApp(appIndex(static_cast<const char *>(lv_event_get_user_data(event))));
}
void setCursor(size_t index) {
    if(terminalRows.empty()) return;
    cursor=index%terminalRows.size();
    for(size_t i=0;i<terminalRows.size();++i) compact::checked(terminalRows[i],i==cursor);
    lv_obj_scroll_to_view(terminalRows[cursor],LV_ANIM_OFF);
}
void terminalClick(lv_event_t *e) {
    if(suppressClick||dragging||pressMoved)return;
    const auto index=positionOf(static_cast<size_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
    if(index==cursor) openPosition(index);
    else setCursor(index);
}
void buildTerminal(lv_obj_t *parent) {
    auto list=compact::panel(parent,240,179,true);
    menuList=list;menuColumns=1;menuRowHeight=44;
    lv_obj_set_flex_flow(list,LV_FLEX_FLOW_COLUMN);
    const auto &apps=WindowManager::instance().apps();
    terminalRows.reserve(apps.size());
    for(size_t position=0;position<menuOrder.count;++position) {
        const auto i=registeredAt(position);
        auto row=compact::button(list,"",terminalClick,reinterpret_cast<void *>(i),240,44);
        terminalRows.push_back(row);
        char number[12];std::snprintf(number,sizeof(number),"%02u",static_cast<unsigned>(position+1));
        auto num=compact::label(row,number,28);lv_obj_align(num,LV_ALIGN_LEFT_MID,5,0);
        auto icon=compact::label(row,apps[i].icon,32);lv_obj_set_style_text_font(icon,icons::font24(),0);
        lv_obj_align(icon,LV_ALIGN_LEFT_MID,38,0);
        auto title=compact::label(row,apps[i].name,137);lv_obj_align(title,LV_ALIGN_LEFT_MID,78,0);
        auto arrow=compact::label(row,LV_SYMBOL_RIGHT);lv_obj_align(arrow,LV_ALIGN_RIGHT_MID,-5,0);
        bindRow(row,i,num);
    }
    auto dock=compact::panel(parent,240,36);lv_obj_set_pos(dock,0,180);
    auto prev=compact::button(dock,LV_SYMBOL_UP,[](lv_event_t *) {
        if(!terminalRows.empty())setCursor((cursor+terminalRows.size()-1)%terminalRows.size());
    },nullptr,59,36);
    auto enter=compact::button(dock,"打开",[](lv_event_t *) { if(!dragging)WindowManager::instance().openApp(registeredAt(cursor)); },nullptr,120,36);
    auto next=compact::button(dock,LV_SYMBOL_DOWN,[](lv_event_t *) {setCursor(cursor+1);},nullptr,59,36);
    lv_obj_set_pos(prev,0,0);lv_obj_set_pos(enter,60,0);lv_obj_set_pos(next,181,0);
    setCursor(0);
}
void updateTiles(lv_timer_t *) {
    if(!sys::Power::isScreenOn() || !WindowManager::instance().atHome() || lv_obj_is_hidden(desktop)) return;
    const auto now=time(nullptr);tm local{};localtime_r(&now,&local);
    char text[32];std::snprintf(text,sizeof(text),"%02d:%02d",local.tm_hour,local.tm_min);compact::setText(homeTime,text);
    std::snprintf(text,sizeof(text),"%04d.%02d.%02d",local.tm_year+1900,local.tm_mon+1,local.tm_mday);compact::setText(homeDate,text);
}
void tilePage(bool apps) {
    compact::checked(desktopTab,!apps);compact::checked(appsTab,apps);
    if(apps) {lv_obj_set_hidden(desktop,true);lv_obj_set_hidden(allApps,false);}
    else {lv_obj_set_hidden(allApps,true);lv_obj_set_hidden(desktop,false);updateTiles(nullptr);}
}
}

static void appItemCb(lv_event_t *e)
{
    if(suppressClick||dragging||pressMoved||arrangePage)return;
    const size_t idx = (size_t)(intptr_t)lv_event_get_user_data(e);
    WindowManager::instance().openApp(idx);
}

static void buildAppList(lv_obj_t *parent)
{
    s_app_list = lv_obj_create(parent);
    menuList=s_app_list;menuColumns=2;menuRowHeight=53;
    lv_obj_set_size(s_app_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_app_list, theme::bg(), 0);
    lv_obj_set_style_bg_opa(s_app_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_app_list, 0, 0);
    // Compact edge-aligned layout with a 1px gap between touch targets.
    lv_obj_set_style_pad_hor(s_app_list, theme::kEdgePad, 0);
    lv_obj_set_style_pad_ver(s_app_list, theme::kEdgePad, 0);
    lv_obj_set_style_pad_row(s_app_list, theme::kGap, 0);
    lv_obj_set_style_pad_column(s_app_list, theme::kGap, 0);
    lv_obj_set_flex_flow(s_app_list, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_scroll_dir(s_app_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_app_list, LV_SCROLLBAR_MODE_OFF); // no scrollbar eating tile width

    // Two equal tiles per row. Computing the width explicitly (instead of
    // LV_PCT(50)) keeps both tiles exactly the same size once the column gap
    // is taken out, which LV_PCT(50) would otherwise overflow.
    constexpr int kTileW = (theme::kContentW - theme::kGap) / 2;
    constexpr int kTilePad = 8;
    constexpr int kIconSlot = 32; // covers even the wide 24px keyboard symbol
    constexpr int kIconGap = 6;
    constexpr int kNameOffset = kIconSlot + kIconGap;
    constexpr int kNameWidth = kTileW - 2*kTilePad - kNameOffset;
    static_assert(kNameWidth >= 4*14, "Four menu CJK glyphs must remain visible");

    // 68px -> 53px: remove one third of the vertical blank around the
    // ~22px label (46px -> 31px); retain 1px gutters and comfortable targets.
    const auto &apps = WindowManager::instance().apps();
    for (size_t position = 0; position < menuOrder.count; ++position) {
        const auto i=registeredAt(position);
        lv_obj_t *btn = compact::button(s_app_list,"",appItemCb,
            reinterpret_cast<void *>(static_cast<intptr_t>(i)),kTileW,53);
        lv_obj_set_flex_grow(btn,0);
        // Internal padding is independent of the compact 1px inter-tile gap.
        lv_obj_set_style_pad_hor(btn,kTilePad,0);
        // Keep the measured 65px label width: visual outlines draw inward
        // without reducing the already tight four-Hanzi content box.
        lv_obj_set_style_border_width(btn,0,0);
        lv_obj_set_style_outline_width(btn,1,0);lv_obj_set_style_outline_pad(btn,-1,0);
        lv_obj_set_style_outline_color(btn,theme::edge(),0);

        // Center every symbol inside the same inset slot, not on the border.
        lv_obj_t *icon = lv_label_create(btn);
        lv_label_set_text(icon, apps[i].icon ? apps[i].icon : "");
        lv_obj_set_style_text_font(icon, icons::font24(), 0);
        lv_obj_set_width(icon,kIconSlot);
        lv_obj_set_style_text_align(icon,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

        // Actual 14px menu font: four CJK glyphs fit on one line after the icon.
        lv_obj_t *name = lv_label_create(btn);
        lv_label_set_text(name, apps[i].name);
        lv_obj_set_style_text_font(name, theme::fontMenu(), 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(name, kNameWidth);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, kNameOffset, 0);
        bindRow(btn,i);
    }
}

static void buildArrange(){
    arrangePending=false;
    if(arrangePage)return;
    focusSelectedId=menuOrder.ids[focusApps.selected()];
    arrangePage=compact::panel(homeParent,240,216);
    auto title=compact::label(arrangePage,"长按拖动排序",172);lv_obj_set_pos(title,5,7);
    auto done=compact::button(arrangePage,"完成",[](lv_event_t *){closeArrangePending=true;},nullptr,58,34);
    lv_obj_set_pos(done,181,0);
    auto content=compact::panel(arrangePage,240,180);lv_obj_set_pos(content,0,36);
    buildAppList(content);lv_obj_update_layout(content);
    if(focusApps.selected()<menuRows.size())lv_obj_scroll_to_view(menuRows[focusApps.selected()].object,LV_ANIM_OFF);
}

static void buildTiles(lv_obj_t *parent) {
    desktop=compact::panel(parent,240,179);
    allApps=compact::panel(parent,240,179);buildAppList(allApps);
    auto tile=[&](const char *name,int x,int y,int w,int h) {
        auto obj=compact::button(desktop,"",openNamed,const_cast<char *>(name),w,h);lv_obj_set_pos(obj,x,y);return obj;
    };
    auto clock=tile("时钟",0,0,143,87);
    compact::buttonBackground(clock,theme::accent());
    homeTime=compact::label(clock,"00:00",137);lv_obj_set_style_text_font(homeTime,&lv_font_montserrat_32,0);
    lv_obj_set_style_text_align(homeTime,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_pos(homeTime,2,14);
    homeDate=compact::label(clock,"",137);lv_obj_set_style_text_font(homeDate,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_align(homeDate,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_pos(homeDate,2,59);
    auto rec=tile("录音机",144,0,96,43);auto recText=compact::label(rec,"录音机");lv_obj_center(recText);
    auto random=tile("随机",144,44,96,43);auto randomText=compact::label(random,"随机");lv_obj_center(randomText);
    auto vault=tile("密码本",0,88,143,90);
    auto key=compact::label(vault,LV_SYMBOL_EYE_CLOSE);lv_obj_set_style_text_font(key,&lv_font_montserrat_24,0);lv_obj_align(key,LV_ALIGN_TOP_MID,0,10);
    auto vaultText=compact::label(vault,"密码本");lv_obj_align(vaultText,LV_ALIGN_BOTTOM_MID,0,-10);
    auto hid=tile("蓝牙键鼠",144,88,96,90);
    auto keyboardIcon=compact::label(hid,LV_SYMBOL_KEYBOARD);lv_obj_set_style_text_font(keyboardIcon,icons::font24(),0);lv_obj_align(keyboardIcon,LV_ALIGN_TOP_MID,0,10);
    auto hidText=compact::label(hid,"蓝牙键鼠",90);
    lv_obj_set_style_text_font(hidText,theme::fontMenu(),0);
    lv_obj_set_style_text_align(hidText,LV_TEXT_ALIGN_CENTER,0);lv_obj_align(hidText,LV_ALIGN_BOTTOM_MID,0,-10);
    auto dock=compact::panel(parent,240,36);lv_obj_set_pos(dock,0,180);
    auto home=compact::button(dock,"桌面",[](lv_event_t *){tilePage(false);},nullptr,79,36);
    auto apps=compact::button(dock,"应用",[](lv_event_t *){tilePage(true);},nullptr,79,36);
    auto settings=compact::button(dock,"设置",openNamed,const_cast<char *>("设置"),80,36);
    lv_obj_set_pos(home,0,0);lv_obj_set_pos(apps,80,0);lv_obj_set_pos(settings,160,0);
    desktopTab=home;appsTab=apps;
    tilePage(false);
    // Home UI only, not a preloaded clock App. One cheap, visibility-gated tick.
    tileTimer=lv_timer_create(updateTiles,1000,nullptr);
}

void HomeScreen::build(lv_obj_t *parent)
{
    homeParent=parent;
    orderModel::Order registered;
    for(const auto &app:WindowManager::instance().apps())if(registered.count<orderModel::Capacity)registered.ids[registered.count++]=app.id;
    menuOrder=orderModel::normalize(sys::Settings::appOrder(),registered);
    menuRows.reserve(menuOrder.count);
    lv_obj_add_event_cb(parent,[](lv_event_t *){
        if(tileTimer)lv_timer_delete(tileTimer);
        tileTimer=nullptr;
        dragging=arrangePending=closeArrangePending=waitingSave=false;
        homeParent=menuList=s_app_list=arrangePage=ghost=noticeBox=noticeLabel=pressRow=nullptr;dragInput=nullptr;
        desktop=allApps=homeTime=homeDate=desktopTab=appsTab=nullptr;
        std::vector<MenuRow>().swap(menuRows);std::vector<lv_obj_t *>().swap(terminalRows);
        focusApps.reset();
    },LV_EVENT_DELETE,nullptr);
    lv_obj_set_style_bg_color(parent, theme::bg(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    if(theme::focus()) {
        focusApps.create(parent,216,WindowManager::instance().apps().size(),0,nullptr,
            [](void *,size_t i) {const auto &app=WindowManager::instance().apps()[registeredAt(i)];focusApps.content(app.name,app.icon,"左右选择 · 长按排序");},
            [](void *,size_t i) {WindowManager::instance().openApp(registeredAt(i));});
        focusApps.onLongPress([](void *,size_t){arrangePending=true;if(auto input=lv_indev_active())lv_indev_wait_release(input);});
    }
    else if(theme::tiles()) buildTiles(parent);
    else if(theme::terminal()) buildTerminal(parent);
    else buildAppList(parent);
}

bool HomeScreen::arranging(){return dragging||arrangePending||arrangePage;}
bool HomeScreen::cancelArrange(){
    const bool active=arranging();arrangePending=closeArrangePending=false;
    if(dragging)finishDrag(false);
    if(arrangePage){
        const auto selected=menuOrder.find(focusSelectedId);
        lv_obj_delete(arrangePage);arrangePage=nullptr;menuList=s_app_list=pressRow=nullptr;menuRows.clear();
        focusApps.select(selected);
    }
    clearNotice();return active;
}
void HomeScreen::poll(){
    if(!homeParent)return;
    if(!sys::Power::isScreenOn()||sys::Power::sleepPending()||!WindowManager::instance().atHome()){
        cancelArrange();return;
    }
    if(closeArrangePending)cancelArrange();
    if(arrangePending)buildArrange();
    if(dragging){
        if(!dragInput||lv_indev_get_state(dragInput)!=LV_INDEV_STATE_PRESSED||lv_indev_search_obj(lv_layer_top(),&dragPoint))finishDrag(false);
        else dragTick();
    }
    if(waitingSave&&!dragging){
        const auto state=sys::Settings::appOrderSaveState();
        if(state!=shownSave){
            shownSave=state;
            if(state==orderModel::SaveState::Saved){notice("顺序已保存");waitingSave=false;}
            else if(state==orderModel::SaveState::Failed)notice("保存失败，正在重试",4000);
        }
    }
    if(noticeUntil&&int32_t(lv_tick_get()-noticeUntil)>=0)clearNotice();
}

}// namespace ui
