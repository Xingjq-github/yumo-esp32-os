#pragma once

#include "app.hpp"
#include "virtual_file_list.hpp"
#include "storage_browser.hpp"
#include "photo_loader.hpp"
#include "gif_player.hpp"
#include "lvgl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace apps {

class PhotoViewer : public ui::App {
public:
    const char *name(void) const override { return "Photo"; }
    const char *icon(void) const override { return LV_SYMBOL_IMAGE; }

    void onCreate(lv_obj_t *parent) override;
    void onShow(void) override;
    bool onBack() override;

    void onHide(void) override;
    void onDestroy(void) override;
    bool handlesHorizontalGesture() const override { return true; }
    bool allowsQuickPanel() const override {return !m_viewing;}
    uint32_t powerHints() const override {return m_gifMode?uint32_t(sys::power::StayLit):0;}

private:
    ui::VirtualFileList m_fileList;
    static void backCb(lv_event_t *e);
    static void refreshCb(lv_event_t *e);
    static void imagePressCb(lv_event_t *e);
    static void imagePressingCb(lv_event_t *e);
    static void imageReleaseCb(lv_event_t *e);

    void scanFiles(void);
    void pollStorage();
    void pollImage();
    void pollGif();
    void switchGif(int direction);
    void openGif(size_t index);
    void releaseGif();
    void failGif(const char *error);
    std::shared_ptr<core::gif::Job> m_gifJob;
    bool m_gifMode=false,m_gifPending=false,m_gifFailed=false;
    size_t m_gifIndex=0;
    uint32_t m_gifWait=0;
    std::shared_ptr<core::photo::Job> m_imageJob;
    std::shared_ptr<core::photo::Source> m_source;
    uint32_t m_powerSession=0,m_noticeUntil=0;
    std::string m_directory="/sdcard", m_scanError;
    std::vector<bool> m_directories;
    std::shared_ptr<core::StorageJob> m_job;
    lv_timer_t *m_poll=nullptr;
    bool m_listingTruncated=false, m_viewing=false;

    void showList(void);
    void openFile(size_t index);
    void releaseImage(void);
    void releaseSource(void);
    void showMessage(const char *message);
    void applyScale(uint16_t dispW);
    void queueView(bool pan=false);
    void submitView();
    bool m_pendingView=false,m_dragging=false;
    uint16_t m_requestedW=0;
    uint32_t m_requestAt=0;
    int32_t m_renderX=0,m_renderY=0;
    void zoom(int direction);
    uint32_t m_keySession=0,m_previousSub=0,m_nextSub=0;
    void placeImage(void);             // position the small viewport frame/drag preview
    void clampOffset(void);

    lv_obj_t *m_header = nullptr;
    lv_obj_t *m_title = nullptr;
    lv_obj_t *m_back = nullptr;
    lv_obj_t *m_refresh = nullptr;
    lv_obj_t *m_notice = nullptr;
    lv_obj_t *m_list = nullptr;
    lv_obj_t *m_imagePanel = nullptr;
    lv_obj_t *m_image = nullptr;

    std::vector<std::string> m_paths;
    std::vector<std::string> m_names;
    uint8_t *m_pixels = nullptr;
    lv_image_dsc_t m_imageDsc = {};

    uint16_t m_origW = 0;
    uint16_t m_origH = 0;
    uint16_t m_imgW = 0;             // currently decoded pixel width
    uint16_t m_imgH = 0;             // currently decoded pixel height
    uint16_t m_fitW = 0;            // display width that fits the view (min scale)
    uint16_t m_dispW = 0;           // current target display width
    bool m_draggable = false;       // true only when decoded image is larger than view
    int32_t m_offsetX = 0;          // pan offset (px, image space)
    int32_t m_offsetY = 0;
    lv_point_t m_pressPoint = {0, 0};
    int32_t m_pressOffsetX = 0;
    int32_t m_pressOffsetY = 0;
};

} // namespace apps
