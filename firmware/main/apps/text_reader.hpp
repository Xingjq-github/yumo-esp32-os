#pragma once

#include "app.hpp"
#include "virtual_file_list.hpp"
#include "storage_browser.hpp"
#include "lvgl.h"

#include <string>
#include <vector>

namespace apps {

class TextReader : public ui::App {
public:
    const char *name(void) const override { return "Text"; }
    const char *icon(void) const override { return LV_SYMBOL_FILE; }

    void onCreate(lv_obj_t *parent) override;
    void onShow(void) override;
    bool onBack() override;
    void onHide() override;
    void onDestroy() override;
    bool handlesHorizontalGesture() const override { return true; }
    uint32_t powerHints() const override {return m_viewing?sys::power::StayLit|sys::power::LowLoad|sys::power::Dim:sys::power::None;}
    void onPowerSaving(bool enabled) override {if(m_poll)lv_timer_set_period(m_poll,enabled?1000:100);}


private:
    ui::VirtualFileList m_fileList;
    static void backCb(lv_event_t *e);
    static void refreshCb(lv_event_t *e);

    void scanFiles(void);
    void pollStorage();
    std::string m_directory="/sdcard", m_scanError;
    std::vector<bool> m_directories;
    std::shared_ptr<core::StorageJob> m_job;
    lv_timer_t *m_poll=nullptr;
    bool m_listingTruncated=false, m_viewing=false;
    std::string m_content;
    std::vector<size_t> m_pages;
    size_t m_page=0;
    void showTextPage();
    void turnPage(int delta);
    void showPageNotice(bool saveFailed=false);
    void hidePageNotice();
    void pollPageNotice();
    lv_obj_t *m_pageNotice=nullptr;
    uint32_t m_noticeSince=0, m_powerSession=0;
    bool m_noticeVisible=false;
    void paginate();
    void saveProgress();
    std::string m_openPath;
    core::reading::Mark m_progress;
    uint64_t m_cardGeneration=0;
    std::shared_ptr<core::StorageJob> m_saveJob;
    uint32_t m_keySession=0, m_previousSub=0, m_nextSub=0;

    void showList(void);
    void openFile(size_t index);
    void showMessage(const char *message);

    lv_obj_t *m_parent = nullptr;
    lv_obj_t *m_header = nullptr;
    lv_obj_t *m_title = nullptr;
    lv_obj_t *m_back = nullptr;
    lv_obj_t *m_refresh = nullptr;
    lv_obj_t *m_list = nullptr;
    lv_obj_t *m_reader = nullptr;
    lv_obj_t *m_text = nullptr;

    std::vector<std::string> m_paths;
    std::vector<std::string> m_names;
};

} // namespace apps
