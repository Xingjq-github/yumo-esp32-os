#pragma once
#include "app.hpp"
#include "core/file_manager.hpp"
namespace apps {
class FileManager : public ui::App {
public:
    const char *name() const override {return "文件管理";}
    const char *icon() const override {return LV_SYMBOL_DIRECTORY;}
    void onCreate(lv_obj_t *) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    bool onBack() override;
    bool allowsQuickPanel() const override {return !m_job;}
    uint32_t powerHints() const override {return m_job?uint32_t(sys::power::StayLit|sys::power::KeepSession):0;}
private:
    enum class Page {List,Actions,Name,Delete,Busy,Properties};
    enum class Action {Back,Refresh,New,Previous,Next,Paste,Clear,Copy,Move,Rename,Remove,Confirm,Cancel,Properties};
    Page m_page=Page::List;
    lv_obj_t *m_body=nullptr,*m_title=nullptr,*m_input=nullptr,*m_progress=nullptr;
    lv_timer_t *m_timer=nullptr;
    std::string m_directory="/sdcard",m_selected,m_clipboard,m_message;
    bool m_move=false,m_new=false,m_more=false,m_hidden=false;
    uint64_t m_generation=0,m_clipGeneration=0;
    size_t m_offset=0;
    std::vector<core::files::Entry> m_entries;
    std::shared_ptr<core::files::Job> m_job;
    core::files::Properties m_properties;
    void draw();
    void scan(const core::files::Entry *anchor=nullptr,bool before=false);
    void poll();
    void action(Action);
    void select(size_t,bool);
    void launch(core::files::Request);
    lv_obj_t *button(lv_obj_t *,const char *,Action,int,int);
    static void click(lv_event_t *);
};
}
