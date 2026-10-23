#include "display_cast.hpp"
#include "usb_storage.hpp"
#include "display_cast_codec.h"
#include "power_mgr.hpp"
#include "settings_store.hpp"
#include "driver/usb_serial_jtag.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <new>

void displayCastReserveConsole(bool on);
namespace sys {
namespace {
constexpr int64_t PeerTimeout=3000000,FrameTimeout=1500000,FrameInterval=50000;
struct Session {
    uint32_t generation=0;
    std::atomic<bool> stop{false},done{false},ready{false},presented{false},wantFrame{false};
    std::atomic<esp_err_t> error{ESP_OK};
    uint8_t *canvas=nullptr,*mailbox=nullptr,*encoded=nullptr;
    std::mutex mailboxMutex;
    bool pending=false; // mutex protected
    // GUI-owned capture state. The worker never reads the live canvas.
    bool captureStarted=false,changed=false;
    uint32_t revision=0,published=0;
    ~Session() {
        // May contain password pixels; do not retain them in a reusable heap.
        for(auto p:{canvas,mailbox,encoded})if(p) {std::memset(p,0,YMCAST_BYTES);heap_caps_free(p);}
    }
};
Session *session=nullptr; // only GUI touches ownership
lv_display_t *display=nullptr;
std::atomic<bool> running{false};
std::atomic<uint32_t> cancellation{0};
esp_err_t lastError=ESP_OK;
bool usable(const Session &s) {
    return s.generation==cancellation.load() && !s.stop.load() && Power::isScreenOn() && !Power::sleepPending();
}
bool sendBytes(Session &s,const uint8_t *data,size_t size,int64_t deadline,bool ending=false) {
    size_t offset=0;
    while(offset<size && (ending || usable(s)) && esp_timer_get_time()<deadline) {
        const size_t chunk=std::min<size_t>(2048,size-offset);
        const int n=usb_serial_jtag_write_bytes(data+offset,chunk,pdMS_TO_TICKS(10));
        if(n>0)offset+=static_cast<size_t>(n);
        else vTaskDelay(1);
    }
    return offset==size;
}
bool sendPacket(Session &s,const ymcast_packet &p,const uint8_t *payload=nullptr,bool ending=false) {
    uint8_t header[YMCAST_HEADER];ymcast_header(header,&p);
    const int64_t deadline=esp_timer_get_time()+(ending?20000:FrameTimeout);
    return sendBytes(s,header,sizeof(header),deadline,ending) &&
        (!p.length || sendBytes(s,payload,p.length,deadline));
}
void execute(Session &s) {
    const uint32_t id=esp_random()|1u;
    uint32_t sequence=0,acknowledged=0;
    int64_t lastRx=0,lastTx=0,sentAt=0,nextFrame=0;
    bool hello=false,peer=false,outstanding=false;
    uint8_t input[YMCAST_HEADER]{};size_t used=0;
    s.ready.store(true,std::memory_order_release);
    while(usable(s)) {
        uint8_t bytes[256];
        const int n=usb_serial_jtag_read_bytes(bytes,sizeof(bytes),0);
        for(int i=0;i<n && usable(s);++i) {
            input[used++]=bytes[i];
            if(used<YMCAST_HEADER)continue;
            ymcast_packet p{};
            if(!ymcast_parse(input,&p) || p.kind<YMCAST_HELLO || p.length) {
                std::memmove(input,input+1,--used);continue;
            }
            used=0;
            const auto now=esp_timer_get_time();
            if(p.kind==YMCAST_HELLO && !peer) {
                hello=true;lastRx=now;
                if(!sendPacket(s,{YMCAST_STATUS,0,1,id,sequence,0,0})) {
                    if(usable(s))s.error.store(ESP_ERR_TIMEOUT);
                    return;
                }
                lastTx=esp_timer_get_time();
            } else if(p.session==id && hello) {
                if(p.kind==YMCAST_STOP) {s.stop.store(true);break;}
                if(p.kind==YMCAST_READY && !peer) {
                    peer=true;lastRx=now;s.wantFrame.store(true);
                } else if(p.kind==YMCAST_ACK && peer) {
                    if(outstanding && p.sequence==sequence) {
                        outstanding=false;acknowledged=sequence;lastRx=now;
                        // Only a complete frame actually painted by the PC can darken LCD.
                        s.presented.store(true);s.wantFrame.store(true);
                    } else if(p.sequence==acknowledged)lastRx=now;
                }
            }
        }
        if(!usable(s))break;
        auto now=esp_timer_get_time();
        if((hello && now-lastRx>PeerTimeout) || (outstanding && now-sentAt>PeerTimeout)) {
            s.error.store(ESP_ERR_TIMEOUT);break;
        }
        if(peer && !outstanding && now>=nextFrame) {
            size_t length=0;uint8_t format=YMCAST_RAW;
            {
                const std::lock_guard<std::mutex> lock(s.mailboxMutex);
                if(s.pending) {
                    s.pending=false;
                    length=ymcast_encode(s.mailbox,YMCAST_PIXELS,s.encoded,YMCAST_BYTES);
                    if(length)format=YMCAST_RLE;
                    else {length=YMCAST_BYTES;std::memcpy(s.encoded,s.mailbox,length);}
                }
            }
            if(length) {
                if(++sequence==0)++sequence;
                const ymcast_packet packet{YMCAST_FRAME,format,1,id,sequence,
                    static_cast<uint32_t>(length),ymcast_crc32(s.encoded,length)};
                if(!sendPacket(s,packet,s.encoded)) {
                    if(usable(s))s.error.store(ESP_ERR_TIMEOUT);
                    break;
                }
                sentAt=lastTx=esp_timer_get_time();nextFrame=now+FrameInterval;outstanding=true;
            }
        }
        now=esp_timer_get_time();
        if(hello && now-lastTx>=500000) {
            if(!sendPacket(s,{YMCAST_STATUS,0,1,id,sequence,0,0})) {
                if(usable(s))s.error.store(ESP_ERR_TIMEOUT);
                break;
            }
            lastTx=esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    // Best effort clearing marker; the receiver also clears on its own deadline.
    (void)sendPacket(s,{YMCAST_END,0,0,id,sequence,0,0},nullptr,true);
}
void worker(void *arg) {
    auto s=static_cast<Session *>(arg);
    bool installed=false,reserved=false;
    if(usable(*s)) {
        s->canvas=static_cast<uint8_t *>(heap_caps_calloc(1,YMCAST_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        s->mailbox=static_cast<uint8_t *>(heap_caps_malloc(YMCAST_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        s->encoded=static_cast<uint8_t *>(heap_caps_malloc(YMCAST_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        if(!s->canvas || !s->mailbox || !s->encoded)s->error.store(ESP_ERR_NO_MEM);
        else if(usb_serial_jtag_is_driver_installed())s->error.store(ESP_ERR_INVALID_STATE);
        else {
            displayCastReserveConsole(true);reserved=true;
            usb_serial_jtag_driver_config_t config{};config.tx_buffer_size=8192;config.rx_buffer_size=512;
            const auto error=usb_serial_jtag_driver_install(&config);
            if(error==ESP_OK) {installed=true;execute(*s);}
            else s->error.store(error);
        }
    }
    // Install and uninstall run on the same pinned core, per IDF's ISR contract.
    if(installed) {
        (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(20));
        (void)usb_serial_jtag_driver_uninstall();
    }
    if(reserved)displayCastReserveConsole(false);
    s->presented.store(false);
    running.store(false,std::memory_order_release);
    s->done.store(true,std::memory_order_release); // no Session access after this line
    vTaskDelete(nullptr);
}
void publishFrame(Session &s) {
    if(!s.revision || s.revision==s.published || !s.wantFrame.load())return;
    const std::unique_lock<std::mutex> lock(s.mailboxMutex,std::try_to_lock);
    if(!lock.owns_lock())return; // No GUI wait for encoding or USB.
    std::memcpy(s.mailbox,s.canvas,YMCAST_BYTES);
    s.pending=true;s.published=s.revision;s.wantFrame.store(false);
}
void capture(lv_event_t *event) {
    auto s=session;
    if(!s || !s->captureStarted || !usable(*s) || s->done.load())return;
    const auto area=static_cast<const lv_area_t *>(lv_event_get_param(event));
    auto buffer=lv_display_get_buf_active(display);
    if(!area || !buffer || !ymcast_copy_rect(s->canvas,area->x1,area->y1,area->x2,area->y2,
                           buffer->data,buffer->header.stride,buffer->data_size)) {
        s->error.store(ESP_ERR_NOT_SUPPORTED);s->stop.store(true);return;
    }
    s->changed=true;
    if(lv_display_flush_is_last(display)) {
        if(++s->revision==0)++s->revision;
        s->changed=false;publishFrame(*s);
    }
}
}
void DisplayCast::init(lv_display_t *disp) {
    if(display)return;
    display=disp;
    if(display)lv_display_add_event_cb(display,capture,LV_EVENT_FLUSH_START,nullptr);
}
esp_err_t DisplayCast::setEnabled(bool on) {
    if(!on) {
        if(session)session->stop.store(true);
        Power::setDisplayCast(false);return ESP_OK;
    }
    poll();
    if(!UsbStorage::idle())return ESP_ERR_INVALID_STATE;
    if(session)return session->stop.load()?ESP_ERR_INVALID_STATE:ESP_OK;
    const auto generation=cancellation.load();
    if(!display || !Power::isScreenOn() || Power::sleepPending())return ESP_ERR_INVALID_STATE;
    // Hardware orientation does not rotate these logical flush strips. The PC
    // receives the same upright 240x240 UI at all four physical panel angles.
    if(lv_display_get_color_format(display)!=LV_COLOR_FORMAT_RGB565 ||
       lv_display_get_render_mode(display)!=LV_DISPLAY_RENDER_MODE_PARTIAL ||
       lv_display_get_rotation(display)!=LV_DISPLAY_ROTATION_0 ||
       lv_display_get_horizontal_resolution(display)!=YMCAST_WIDTH ||
       lv_display_get_vertical_resolution(display)!=YMCAST_HEIGHT)return ESP_ERR_NOT_SUPPORTED;
    session=new(std::nothrow) Session;
    if(!session)return ESP_ERR_NO_MEM;
    session->generation=generation;
    lastError=ESP_OK;running.store(true);
    if(xTaskCreatePinnedToCore(worker,"display_cast",4096,session,2,nullptr,0)!=pdPASS) {
        running.store(false);delete session;session=nullptr;return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
void DisplayCast::poll() {
    if(!session)return;
    if(!usable(*session))session->stop.store(true);
    if(session->done.load(std::memory_order_acquire)) {
        lastError=session->error.load();Power::setDisplayCast(false);
        delete session;session=nullptr;return;
    }
    if(session->ready.load(std::memory_order_acquire) && !session->captureStarted && !session->stop.load()) {
        session->captureStarted=true;lv_obj_invalidate(lv_screen_active());
    }
    if(session->captureStarted && !session->changed && usable(*session))publishFrame(*session);
    Power::setDisplayCast(session->presented.load() && usable(*session) && Settings::castScreenOff());
}
bool DisplayCast::enabled() {return session && !session->stop.load() && !session->done.load();}
bool DisplayCast::idle() {return !running.load(std::memory_order_acquire);}
void DisplayCast::cancel() {cancellation.fetch_add(1);}
const char *DisplayCast::statusText() {
    if(session) {
        if(session->stop.load() || session->done.load())return "正在关闭投放…";
        if(session->presented.load())return Settings::castScreenOff()?"投屏中 · 本机熄屏":"投屏中 · 双屏显示";
        if(!session->ready.load())return "正在开启 USB 投放…";
        return "等待电脑连接 · 本机亮屏";
    }
    if(lastError==ESP_ERR_NO_MEM)return "内存不足，请关闭应用后重试";
    if(lastError==ESP_ERR_TIMEOUT)return "电脑连接已中断 · 请重新开启";
    if(lastError!=ESP_OK)return "投放开启失败，请关闭后重试";
    return "投屏已关闭 · 手动开启";
}
}
