#include "photo_viewer.hpp"

#include "charset_conv.hpp"
#include "theme.hpp"
#include "compact_ui.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"

#include "esp_jpeg_dec.h"
#include "esp_jpeg_common.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace apps {
namespace {

constexpr const char *TAG = "photo_viewer";
constexpr size_t kMaxJpegBytes = 4 * 1024 * 1024;
constexpr int32_t kViewW = 240;      // image panel width  (fills screen edge-to-edge)
constexpr int32_t kViewH = 201;      // image panel height (header bottom + 1px gap -> screen bottom)
constexpr int32_t kScaleStepNum = 3; // zoom step ratio = 3/2 (+50% per tap)
constexpr int32_t kScaleStepDen = 2;


} // namespace

void PhotoViewer::onCreate(lv_obj_t *parent)
{
    lv_obj_set_style_text_font(parent,ui::theme::fontCjkFull(),0);
    m_poll=lv_timer_create([](lv_timer_t *t) {
        static_cast<PhotoViewer *>(lv_timer_get_user_data(t))->pollStorage();
    },100,this);
    lv_obj_set_style_bg_color(parent, ui::theme::bg(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    m_header = lv_obj_create(parent);
    lv_obj_set_size(m_header, ui::theme::kContentW, 34);
    lv_obj_align(m_header, LV_ALIGN_TOP_MID, 0, ui::theme::kGap);
    lv_obj_set_style_bg_opa(m_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_header, 0, 0);
    lv_obj_set_style_pad_all(m_header, 0, 0);
    lv_obj_clear_flag(m_header, LV_OBJ_FLAG_SCROLLABLE);

    m_back = lv_button_create(m_header);
    lv_obj_set_size(m_back, 34, 30);
    lv_obj_align(m_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(m_back, backCb, LV_EVENT_CLICKED, this);
    lv_obj_t *backLabel = lv_label_create(m_back);
    lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
    lv_obj_center(backLabel);
    lv_obj_add_flag(m_back, LV_OBJ_FLAG_HIDDEN);

    m_title = lv_label_create(m_header);
    lv_obj_set_width(m_title, 122);
    lv_label_set_long_mode(m_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(m_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(m_title, ui::theme::fontSmall(), 0);
    lv_obj_set_style_text_color(m_title, ui::theme::fg(), 0);
    lv_obj_align(m_title,LV_ALIGN_LEFT_MID,36,0);

    m_refresh = lv_button_create(m_header);
    lv_obj_set_size(m_refresh, 34, 30);
    lv_obj_align(m_refresh, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(m_refresh, refreshCb, LV_EVENT_CLICKED, this);
    lv_obj_t *refreshLabel = lv_label_create(m_refresh);
    lv_label_set_text(refreshLabel, LV_SYMBOL_REFRESH);
    lv_obj_center(refreshLabel);

    m_zoomIn = lv_button_create(m_header);
    lv_obj_set_size(m_zoomIn, 34, 30);
    lv_obj_align(m_zoomIn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(m_zoomIn, zoomInCb, LV_EVENT_CLICKED, this);
    lv_obj_t *zoomInLabel = lv_label_create(m_zoomIn);
    lv_label_set_text(zoomInLabel, LV_SYMBOL_PLUS);
    lv_obj_center(zoomInLabel);
    lv_obj_add_flag(m_zoomIn, LV_OBJ_FLAG_HIDDEN);

    m_zoomOut = lv_button_create(m_header);
    lv_obj_set_size(m_zoomOut, 34, 30);
    lv_obj_align(m_zoomOut, LV_ALIGN_RIGHT_MID, -37, 0);
    lv_obj_add_event_cb(m_zoomOut, zoomOutCb, LV_EVENT_CLICKED, this);
    lv_obj_t *zoomOutLabel = lv_label_create(m_zoomOut);
    lv_label_set_text(zoomOutLabel, LV_SYMBOL_MINUS);
    lv_obj_center(zoomOutLabel);
    lv_obj_add_flag(m_zoomOut, LV_OBJ_FLAG_HIDDEN);

    m_list = lv_obj_create(parent);
    lv_obj_set_size(m_list, ui::theme::kContentW, 195);
    lv_obj_align(m_list, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_bg_color(m_list, ui::theme::bg(), 0);
    lv_obj_set_style_bg_opa(m_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_list, 0, 0);
    lv_obj_set_style_pad_all(m_list, 0, 0);
    lv_obj_set_style_pad_row(m_list, ui::theme::kGap, 0);
    lv_obj_set_flex_flow(m_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(m_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(m_list, LV_SCROLLBAR_MODE_OFF);

    m_imagePanel = lv_obj_create(parent);
    lv_obj_set_size(m_imagePanel, 240, 201);
    lv_obj_align(m_imagePanel, LV_ALIGN_TOP_MID, 0, 39);
    lv_obj_set_style_pad_all(m_imagePanel, 0, 0);
    lv_obj_set_style_border_width(m_imagePanel, 0, 0);
    lv_obj_set_style_radius(m_imagePanel, 0, 0);
    lv_obj_set_style_bg_color(m_imagePanel, lv_color_black(), 0);
    lv_obj_clear_flag(m_imagePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(m_imagePanel, LV_OBJ_FLAG_HIDDEN);

    m_image = lv_image_create(m_imagePanel);
    lv_obj_add_flag(m_image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m_image, imagePressCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(m_image, imagePressingCb, LV_EVENT_PRESSING, this);
    lv_obj_set_pos(m_image, 0, 0);
}

void PhotoViewer::onShow(void)
{
    if(m_poll) lv_timer_resume(m_poll);
    scanFiles();
}
void PhotoViewer::scanFiles(void)
{
    if(m_job) m_job->cancelled.store(true);
    m_paths.clear(); m_names.clear(); m_directories.clear(); m_scanError.clear();
    m_listingTruncated=false; m_viewing=false;
    m_job=core::listDirectory(m_directory,true);
    showList(); lv_obj_clean(m_list); showMessage("正在读取目录…");
}
void PhotoViewer::pollStorage()
{
    if(!m_job || !m_job->done.load(std::memory_order_acquire)) return;
    auto job=std::move(m_job);
    if(job->cancelled.load()) return;
    if(job->error) m_scanError=core::storageError(job->error);
    m_listingTruncated=job->truncated;
    for(auto &entry:job->entries) {
        m_paths.push_back(std::move(entry.path)); m_names.push_back(std::move(entry.name));
        m_directories.push_back(entry.directory);
    }
    showList();
}
bool PhotoViewer::onBack()
{
    if(m_viewing) { showList(); return true; }
    if(m_directory!="/sdcard") { m_directory=core::parentDirectory(m_directory); scanFiles(); return true; }
    return false;
}
void PhotoViewer::onDestroy(void)
{
    if(m_job) { m_job->cancelled.store(true); m_job.reset(); }
    if(m_poll) { lv_timer_delete(m_poll); m_poll=nullptr; }
    releaseImage();
    releaseJpeg();
}
void PhotoViewer::onHide(void)
{
    if(m_job) { m_job->cancelled.store(true); m_job.reset(); }
    if(m_poll) lv_timer_pause(m_poll);
}
void PhotoViewer::showList(void)
{
    releaseImage();
    releaseJpeg();
    m_viewing=false;
    lv_obj_clean(m_list);
    lv_obj_remove_flag(m_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_imagePanel, LV_OBJ_FLAG_HIDDEN);
    if(m_directory=="/sdcard") lv_obj_add_flag(m_back,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(m_back,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_zoomIn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_zoomOut, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(m_refresh, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(m_title,core::ensureUtf8(m_directory.substr(7).empty()?"SD 卡":m_directory.substr(7)).c_str());
    if(!m_scanError.empty()) { showMessage(m_scanError.c_str()); return; }
    if (m_names.empty()) {
        showMessage("此目录没有子目录或 JPEG 图片");
        return;
    }
    for (size_t i = 0; i < m_names.size(); ++i) {
        const auto caption=(m_directories[i]?std::string("[目录] "):std::string())+m_names[i];
        lv_obj_t *btn = ui::compact::button(m_list,caption.empty()?"?":caption.c_str(),fileCb,this,LV_PCT(100),38);
        lv_obj_set_user_data(btn, reinterpret_cast<void *>(i));
        // Nonempty button text creates the label that we reuse for the filename.
        lv_obj_t *label = lv_obj_get_child(btn,0);
        lv_obj_set_width(label, 234);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_font(label, ui::theme::fontSmall(), 0);
        lv_obj_center(label);
    }
    if(m_listingTruncated) showMessage("目录较大，仅展示前 256 项，请进入子目录");
}

void PhotoViewer::openFile(size_t index)
{
    if (index >= m_paths.size()) return;
    if(m_directories[index]) { m_directory=m_paths[index]; scanFiles(); return; }
    m_viewing=true;
    if (!loadJpeg(m_paths[index])) {
        lv_obj_add_flag(m_imagePanel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(m_back, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_refresh, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_zoomIn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(m_zoomOut, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(m_title, m_names[index].c_str());
        lv_obj_clean(m_list); showMessage("图片无法读取：请使用完整的 JPG/JPEG 文件");
        return;
    }
    lv_obj_add_flag(m_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(m_imagePanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(m_back, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(m_refresh, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(m_zoomIn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(m_zoomOut, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(m_title, m_names[index].c_str());

    // fit width = display width that makes the whole image fit the view; this is
    // the minimum scale (the "shrink to fit screen" limit the user asked for).
    uint32_t fitByW = kViewW;
    uint32_t fitByH = m_origH ? (static_cast<uint32_t>(m_origW) * kViewH) / m_origH : m_origW;
    uint32_t fitW = std::min(fitByW, fitByH);
    if (fitW > m_origW) fitW = m_origW;
    if (fitW < 8) fitW = 8;
    m_fitW = static_cast<uint16_t>(fitW);
    applyScale(m_fitW);
}

// Read the whole JPEG into PSRAM once and parse its header. Every zoom level then
// re-decodes from this cached buffer at a fresh resolution (crisp), instead of
// transform-scaling a bitmap that was already downsampled once.
bool PhotoViewer::loadJpeg(const std::string &path)
{
    releaseImage();
    releaseJpeg();
    FILE *file = fopen(path.c_str(), "rb");
    if (!file) {
        ESP_LOGE(TAG, "fopen failed: %s (errno=%d)", path.c_str(), errno);
        return false;
    }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length <= 0 || length > static_cast<long>(kMaxJpegBytes)) {
        ESP_LOGE(TAG, "bad file length=%ld", length);
        fclose(file);
        return false;
    }
    uint8_t *input = static_cast<uint8_t *>(
        heap_caps_malloc(static_cast<size_t>(length), MALLOC_CAP_SPIRAM));
    if (!input) {
        input = static_cast<uint8_t *>(heap_caps_malloc(static_cast<size_t>(length), MALLOC_CAP_8BIT));
    }
    if (!input) {
        ESP_LOGE(TAG, "input alloc failed for %ld bytes", length);
        fclose(file);
        return false;
    }
    size_t readBytes = fread(input, 1, static_cast<size_t>(length), file);
    fclose(file);
    if (readBytes != static_cast<size_t>(length)) {
        ESP_LOGE(TAG, "fread short: %u/%ld", static_cast<unsigned>(readBytes), length);
        heap_caps_free(input);
        return false;
    }
    m_jpeg = input;
    m_jpegLen = static_cast<size_t>(length);
    ESP_LOGI(TAG, "load %s len=%ld head=%02X%02X", path.c_str(), length,
             m_jpegLen > 0 ? m_jpeg[0] : 0, m_jpegLen > 1 ? m_jpeg[1] : 0);

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t decoder = nullptr;
    if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg_dec_open (header) failed");
        releaseJpeg();
        return false;
    }
    jpeg_dec_io_t io = {};
    jpeg_dec_header_info_t info = {};
    io.inbuf = m_jpeg;
    io.inbuf_len = static_cast<int>(m_jpegLen);
    jpeg_error_t err = jpeg_dec_parse_header(decoder, &io, &info);
    jpeg_dec_close(decoder);
    if (err != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "parse_header failed err=%d (progressive not supported)", err);
        releaseJpeg();
        return false;
    }
    m_origW = info.width;
    m_origH = info.height;
    ESP_LOGI(TAG, "orig %ux%u", m_origW, m_origH);
    if(m_origW<8 || m_origH<8 || m_origW>8192 || m_origH>8192) { releaseJpeg(); return false; }
    return true;
}

// Decode the cached JPEG so its output resolution is close to (targetW x targetH)
// while honoring esp_new_jpeg constraints: scale is a TARGET resolution with
// width/height multiples of 8; max downscale is 1/8; upscale is not supported.
bool PhotoViewer::decodeAt(uint16_t targetW, uint16_t targetH)
{
    if (!m_jpeg || !m_origW || !m_origH) return false;

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;

    auto alignDim = [](uint16_t target, uint16_t orig) -> uint16_t {
        uint32_t floor8 = (static_cast<uint32_t>(target) / 8) * 8;
        uint32_t min8 = ((orig + 63) / 64) * 8;        // smallest mult-of-8 >= orig/8
        if (floor8 < min8) floor8 = min8;
        uint32_t origFloor8 = (orig / 8) * 8;
        if (floor8 > origFloor8) floor8 = origFloor8;
        if (floor8 < 8) floor8 = 8;
        return static_cast<uint16_t>(floor8);
    };

    bool needScale = (targetW < m_origW) || (targetH < m_origH);
    if (needScale) {
        config.scale.width = alignDim(targetW, m_origW);
        config.scale.height = alignDim(targetH, m_origH);
    }

    jpeg_dec_handle_t decoder = nullptr;
    jpeg_error_t err = jpeg_dec_open(&config, &decoder);
    if (err != JPEG_ERR_OK) { ESP_LOGE(TAG, "jpeg_dec_open failed err=%d", err); return false; }
    jpeg_dec_io_t io = {};
    jpeg_dec_header_info_t info = {};
    io.inbuf = m_jpeg;
    io.inbuf_len = static_cast<int>(m_jpegLen);
    err = jpeg_dec_parse_header(decoder, &io, &info);
    if (err != JPEG_ERR_OK) { ESP_LOGE(TAG, "parse_header failed err=%d", err); jpeg_dec_close(decoder); return false; }
    uint16_t width = config.scale.width ? config.scale.width : info.width;
    uint16_t height = config.scale.height ? config.scale.height : info.height;
    ESP_LOGI(TAG, "decodeAt target=%ux%u -> %ux%u", targetW, targetH, width, height);

    int outLength = 0;
    if (jpeg_dec_get_outbuf_len(decoder, &outLength) != JPEG_ERR_OK || outLength <= 0 || outLength > 2*1024*1024) {
        ESP_LOGE(TAG, "get_outbuf_len failed outLen=%d", outLength);
        jpeg_dec_close(decoder);
        return false;
    }
    auto *pixels = static_cast<uint8_t *>(jpeg_calloc_align(outLength, 16));
    if (!pixels) { ESP_LOGE(TAG, "calloc_align failed len=%d", outLength); jpeg_dec_close(decoder); return false; }
    io.outbuf = pixels;
    io.out_size = outLength;
    int processCount = 1;
    jpeg_dec_get_process_count(decoder, &processCount);
    bool ok = true;
    for (int i = 0; i < processCount && ok; ++i) {
        if (jpeg_dec_process(decoder, &io) != JPEG_ERR_OK) ok = false;
    }
    jpeg_dec_close(decoder);
    if (!ok) { ESP_LOGE(TAG, "decode process failed"); jpeg_free_align(pixels); return false; }

    uint32_t rowBytes = static_cast<uint32_t>(width) * 2;
    if (rowBytes && static_cast<uint32_t>(outLength) % rowBytes == 0) {
        height = static_cast<uint16_t>(outLength / rowBytes);
    }
    releaseImage(); m_pixels=pixels;
    m_imageDsc = {};
    m_imageDsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    m_imageDsc.header.cf = LV_COLOR_FORMAT_RGB565;
    m_imageDsc.header.w = width;
    m_imageDsc.header.h = height;
    m_imageDsc.header.stride = width * 2;
    m_imageDsc.data_size = outLength;
    m_imageDsc.data = m_pixels;
    m_imgW = width;
    m_imgH = height;
    return true;
}

void PhotoViewer::releaseImage(void)
{
    if(m_pixels) {
        lv_image_cache_drop(&m_imageDsc);
        if(m_image) lv_image_set_src(m_image,nullptr);
        jpeg_free_align(m_pixels); m_pixels=nullptr;
    }
    m_imageDsc = {};
}

void PhotoViewer::releaseJpeg(void)
{
    if (m_jpeg) { heap_caps_free(m_jpeg); m_jpeg = nullptr; }
    m_jpegLen = 0;
    m_origW = m_origH = 0;
}

void PhotoViewer::showMessage(const char *message)
{
    lv_obj_t *label = lv_label_create(m_list);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, ui::theme::muted(), 0);
    lv_obj_set_style_text_font(label,ui::theme::fontCjkFull(),0);
    lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(label, message);
}

void PhotoViewer::fileCb(lv_event_t *e) { auto *s = static_cast<PhotoViewer *>(lv_event_get_user_data(e)); s->openFile(reinterpret_cast<size_t>(lv_obj_get_user_data(lv_event_get_target_obj(e)))); }
void PhotoViewer::backCb(lv_event_t *e) {static_cast<PhotoViewer *>(lv_event_get_user_data(e))->onBack(); }
void PhotoViewer::refreshCb(lv_event_t *e) { auto *s = static_cast<PhotoViewer *>(lv_event_get_user_data(e)); s->scanFiles(); }

// Re-decode the cached JPEG so the displayed width equals dispW (clamped to
// [m_fitW, m_origW]). This yields a real resolution change (crisp pixels) rather
// than transform-scaling a bitmap that was already downsampled once.
void PhotoViewer::applyScale(uint16_t dispW)
{
    if (dispW < m_fitW) dispW = m_fitW;
    if (dispW > m_origW) dispW = m_origW;
    uint32_t dispH = m_origW ? (static_cast<uint32_t>(dispW) * m_origH) / m_origW : m_origH;
    if (dispH < 8) dispH = 8;
    if (!decodeAt(dispW, static_cast<uint16_t>(dispH))) {
        if(!m_pixels) {
            lv_obj_add_flag(m_imagePanel,LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(m_list,LV_OBJ_FLAG_HIDDEN); lv_obj_clean(m_list);
            showMessage("图片解码失败或过大，请返回选择较小的 JPEG 图片");
        } else lv_label_set_text(m_title,"缩放受限");
        return;
    }
    m_dispW = dispW;
    lv_image_set_src(m_image, &m_imageDsc);
    placeImage();
}

// Center the image if it fits the view; otherwise clamp the pan offset. Dragging
// is only enabled while the decoded image is larger than the view (i.e. zoomed in).
void PhotoViewer::placeImage(void)
{
    int32_t w = m_imgW, h = m_imgH;
    m_draggable = (w > kViewW) || (h > kViewH);
    if (w <= kViewW) m_offsetX = -(kViewW - w) / 2;
    if (h <= kViewH) m_offsetY = -(kViewH - h) / 2;
    clampOffset();
    lv_obj_set_pos(m_image, -m_offsetX, -m_offsetY);
}

// Keep the pan offset within the decoded image bounds so the view never shows
// empty area beyond the picture edges (only relevant when larger than the view).
void PhotoViewer::clampOffset(void)
{
    int32_t w = m_imgW, h = m_imgH;
    if (w > kViewW) {
        if (m_offsetX < 0) m_offsetX = 0;
        if (m_offsetX > w - kViewW) m_offsetX = w - kViewW;
    }
    if (h > kViewH) {
        if (m_offsetY < 0) m_offsetY = 0;
        if (m_offsetY > h - kViewH) m_offsetY = h - kViewH;
    }
}

void PhotoViewer::zoomInCb(lv_event_t *e)
{
    auto *s = static_cast<PhotoViewer *>(lv_event_get_user_data(e));
    uint32_t next = (static_cast<uint32_t>(s->m_dispW) * kScaleStepNum) / kScaleStepDen;
    if (next <= s->m_dispW) next = s->m_dispW + 8;   // guarantee progress
    s->applyScale(static_cast<uint16_t>(std::min<uint32_t>(next, s->m_origW)));
}

void PhotoViewer::zoomOutCb(lv_event_t *e)
{
    auto *s = static_cast<PhotoViewer *>(lv_event_get_user_data(e));
    uint32_t next = (static_cast<uint32_t>(s->m_dispW) * kScaleStepDen) / kScaleStepNum;
    if (next >= s->m_dispW && s->m_dispW > 8) next = s->m_dispW - 8;
    s->applyScale(static_cast<uint16_t>(std::max<uint32_t>(next, s->m_fitW)));
}

void PhotoViewer::imagePressCb(lv_event_t *e)
{
    auto *s = static_cast<PhotoViewer *>(lv_event_get_user_data(e));
    if (!s->m_draggable) return;   // no panning at fit (minimum) scale
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_indev_get_point(indev, &s->m_pressPoint);
    s->m_pressOffsetX = s->m_offsetX;
    s->m_pressOffsetY = s->m_offsetY;
}

void PhotoViewer::imagePressingCb(lv_event_t *e)
{
    auto *s = static_cast<PhotoViewer *>(lv_event_get_user_data(e));
    if (!s->m_draggable) return;   // panning only allowed when zoomed in
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s->m_offsetX = s->m_pressOffsetX - (p.x - s->m_pressPoint.x);
    s->m_offsetY = s->m_pressOffsetY - (p.y - s->m_pressPoint.y);
    s->clampOffset();
    lv_obj_set_pos(s->m_image, -s->m_offsetX, -s->m_offsetY);
}

} // namespace apps