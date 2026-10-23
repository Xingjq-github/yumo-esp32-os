#pragma once
#include "ui/app_icons.hpp"
#include "ui/app.hpp"
#include "core/knowledge_store.hpp"
namespace apps {
class Programming : public ui::App {
public:
    const char *name()const override{return "编程速查";}
    const char *icon()const override{return ui::icons::Programming;}
    void onCreate(lv_obj_t *)override;
    void onShow()override;
    void onHide()override;
    void onDestroy()override;
    bool onBack()override;
    bool handlesHorizontalGesture()const override{return true;}
    uint32_t powerHints()const override;
    void onPowerSaving(bool)override;
private:
    enum class Page{Home,Groups,List,Search,Reader,Related,Options,Info,Busy,Error};
    enum class Mode{Group,Search,Favorites,Recent};
    enum class Action:unsigned{Back=1,Search,Menu,Category,Group,Entry,Home,Favorites,Recent,Resume,
        Submit,Favorite,Related,Wrap,Size,Full,Reload,Info,Toggle};
    struct Position{unsigned section;int y,content,height;};
    static void click(lv_event_t *);
    void act(Action,unsigned=0);
    lv_obj_t *button(lv_obj_t *,const char *,Action,unsigned,int,int,int,int);
    lv_obj_t *label(lv_obj_t *,const std::string &,int,int,int,bool=false);
    void draw();void drawReader();void updateRows();void poll();void load();void request(unsigned,unsigned=0,size_t=0,uint32_t=0);
    void groups();void filter();void turn(int);void mark();void flush();void dirty();void releaseView();
    size_t count()const;
    lv_obj_t *m_parent=nullptr,*m_header=nullptr,*m_body=nullptr,*m_footer=nullptr,*m_input=nullptr;
    lv_obj_t *m_codeViewport=nullptr;
    lv_timer_t *m_timer=nullptr;
    std::shared_ptr<core::knowledge::Library> m_library;
    std::shared_ptr<core::knowledge::Job> m_job;
    std::shared_ptr<core::knowledge::Save> m_save;
    core::knowledge::State m_state;
    core::knowledge::Article m_article;
    core::sd::Access m_saveAccess;
    std::vector<unsigned> m_matches;
    std::vector<std::string> m_groups;
    std::vector<std::string> m_blocks;
    std::vector<Position> m_positions;
    std::array<lv_obj_t *,7> m_pool{};
    lv_point_t m_press{};
    std::string m_query,m_group,m_message;
    Page m_page=Page::Home,m_return=Page::Home,m_origin=Page::List;
    Mode m_mode=Mode::Group;
    unsigned m_category=0,m_entry=0,m_section=0,m_offset=0,m_listOffset=0;
    int32_t m_codeX=0,m_scroll[2]={0,0};
    size_t m_restore=0;uint32_t m_restoreSection=0;
    uint32_t m_keys=0,m_power=0,m_prevSub=0,m_nextSub=0,m_changedAt=0,m_waitAt=0;
    bool m_dirty=false,m_hidden=true,m_full=false,m_waitOpen=false,m_code=false,m_hasCode=false,m_drawing=false,m_pendingRestore=false;
};
}
