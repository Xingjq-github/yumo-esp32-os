#include "bt_mouse.hpp"

#include "core/ble_hid.hpp"
#include "theme.hpp"

#include <cstdint>
#include <cstdlib>

#include <cmath>

namespace apps {
namespace {

// 触摸区拖动灵敏度基准(放大手指位移 -> 光标位移); x2 时翻倍
constexpr float kMoveGainBase = 1.6f;
// 单击判定阈值:总位移小于该值视为点击而非拖动
constexpr int32_t kClickThreshold = 12;

// 指针加速(pointer acceleration):参考成熟触控板方案,增益随手指瞬时
// 速度非线性增大 —— 慢速滑动低增益(高精度微调),快速滑动高增益(快速跨屏)。
//   有效增益 = kMoveGainBase * (1 + kAccelFactor * speed)
//   speed 为本次事件的曼哈顿位移(|dx|+|dy|),再按 kAccelCap 限制上限。
constexpr float kAccelFactor = 0.06f; // 速度对增益的放大系数
constexpr float kAccelCap = 3.5f;     // 有效增益相对基准的最大倍数(防止过冲)

// HID 鼠标单份报告的位移范围为 int8(-127..127);超出部分需拆分为多份报告发送。
constexpr int32_t kReportMax = 127;

// 滚轮触摸条:每移动这么多像素发送 1 格滚轮
constexpr int32_t kWheelStep = 14;

// tap-and-drag(点击后拖拽)判定参数:
//   kTapMaxMs      : 一次按下-抬起在此时间内且位移小,才算作有效"轻点"(tap)
//   kDragWindowMs  : tap 抬起后,在此窗口内再次按下则进入拖拽锁定
//   kDragMoveStart : 拖拽待命时,二次按下后需移动超过此值才真正开始拖拽,
//                    防止把"想双击"误判成拖拽
constexpr uint32_t kTapMaxMs = 260;
constexpr uint32_t kDragWindowMs = 320;
constexpr int32_t kDragMoveStart = 4;

// 连击频率下拉选项(每秒点击次数)
constexpr int32_t kRateOptions[] = {1, 2, 5, 10};

int8_t clampInt8(int32_t v)
{
    if (v > kReportMax) return static_cast<int8_t>(kReportMax);
    if (v < -kReportMax) return static_cast<int8_t>(-kReportMax);
    return static_cast<int8_t>(v);
}

} // namespace

void BtMouse::onCreate(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, ui::theme::bg(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_border_width(parent, 0, 0);

    // 布局常量(240x240,部件之间无缝隙)
    constexpr int kBarH = 44;   // 顶部按钮排高度
    constexpr int kWheelW = 44; // 右侧滚轮条宽度

    // 顶部一排:L / R / 速度切换 / 连击开关 / 连击频率下拉
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, ui::theme::kScreenW, kBarH);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 1, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // 创建一个占满高度、可增长的文本按钮
    auto makeBtn = [&](const char *text, lv_event_cb_t cb) -> lv_obj_t * {
        lv_obj_t *button = lv_button_create(bar);
        lv_obj_set_height(button, LV_PCT(100));
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_radius(button, 4, 0);
        lv_obj_set_style_pad_all(button, 1, 0);
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, this);
        lv_obj_t *label = lv_label_create(button);
        lv_obj_set_style_text_font(label, ui::theme::fontSmall(), 0);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        return label;
    };

    makeBtn("L", clickLeftCb);
    makeBtn("R", clickRightCb);
    m_speedLabel = makeBtn("x1", speedCb);
    m_autoLabel = makeBtn("C:OFF", autoClickCb);

    // 连击频率下拉(1/2/5/10 次每秒)
    lv_obj_t *dd = lv_dropdown_create(bar);
    lv_dropdown_set_options(dd, "1\n2\n5\n10");
    lv_obj_set_height(dd, LV_PCT(100));
    lv_obj_set_flex_grow(dd, 1);
    lv_obj_set_style_pad_all(dd, 1, 0);
    lv_obj_set_style_text_font(dd, ui::theme::fontSmall(), 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dd), ui::theme::fontSmall(), 0);
    // 展开下拉列表:增大每个选项的行高(行间距 + 上下内边距)
    lv_obj_set_style_text_line_space(lv_dropdown_get_list(dd), 14, LV_PART_MAIN);
    lv_obj_set_style_pad_top(lv_dropdown_get_list(dd), 8, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(lv_dropdown_get_list(dd), 8, LV_PART_MAIN);
    lv_obj_add_event_cb(dd, rateCb, LV_EVENT_VALUE_CHANGED, this);

    // 右侧竖向滚轮触摸条,紧贴右侧,占据按钮排以下的全部高度
    lv_obj_t *wheel = lv_obj_create(parent);
    lv_obj_set_size(wheel, kWheelW, ui::theme::kScreenH - kBarH);
    lv_obj_align(wheel, LV_ALIGN_TOP_RIGHT, 0, kBarH);
    lv_obj_set_style_bg_color(wheel, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_bg_opa(wheel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(wheel, 0, 0);
    lv_obj_set_style_radius(wheel, 0, 0);
    lv_obj_set_style_pad_all(wheel, 0, 0);
    lv_obj_clear_flag(wheel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(wheel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(wheel, wheelCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(wheel, wheelCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(wheel, wheelCb, LV_EVENT_RELEASED, this);

    lv_obj_t *wheelHint = lv_label_create(wheel);
    lv_obj_set_style_text_font(wheelHint, ui::theme::fontSmall(), 0);
    lv_obj_set_style_text_color(wheelHint, ui::theme::muted(), 0);
    lv_label_set_text(wheelHint, LV_SYMBOL_SHUFFLE);
    lv_obj_center(wheelHint);

    // 中间触摸区(拖动移光标 / 单击左键),占据左下剩余全部空间
    lv_obj_t *pad = lv_obj_create(parent);
    lv_obj_set_size(pad, ui::theme::kScreenW - kWheelW, ui::theme::kScreenH - kBarH);
    lv_obj_align(pad, LV_ALIGN_TOP_LEFT, 0, kBarH);
    lv_obj_set_style_bg_color(pad, lv_color_hex(0x181818), 0);
    lv_obj_set_style_bg_opa(pad, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pad, 0, 0);
    lv_obj_set_style_radius(pad, 0, 0);
    lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pad, touchpadCb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(pad, touchpadCb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(pad, touchpadCb, LV_EVENT_RELEASED, this);

    lv_obj_t *padHint = lv_label_create(pad);
    lv_obj_set_style_text_font(padHint, ui::theme::fontSmall(), 0);
    lv_obj_set_style_text_color(padHint, ui::theme::muted(), 0);
    lv_label_set_text(padHint, "点按+拖动");
    lv_obj_center(padHint);

    // 左上角 2px 竖条连接状态条,高度与顶部按钮排一致(最后创建,置于最上层)
    m_status = lv_obj_create(parent);
    lv_obj_set_size(m_status, 2, kBarH);
    lv_obj_align(m_status, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(m_status, 0, 0);
    lv_obj_set_style_border_width(m_status, 0, 0);
    lv_obj_set_style_pad_all(m_status, 0, 0);
    lv_obj_clear_flag(m_status, LV_OBJ_FLAG_SCROLLABLE);

    m_statusTimer = lv_timer_create(statusTimerCb, 500, this);
    refreshStatus();
}

void BtMouse::onDestroy(void)
{
    // 若离开页面时仍处于拖拽(左键按住)状态,补发一次左键释放,
    // 防止主机端左键卡在按下状态。
    if (m_dragging) {
        sys::BleHid::sendMouse(0, 0, 0, 0x00);
        m_dragging = false;
    }
    m_dragArmed = false;
    m_touching = false;

    if (m_statusTimer) {
        lv_timer_delete(m_statusTimer);
        m_statusTimer = nullptr;
    }
    if (m_autoTimer) {
        lv_timer_delete(m_autoTimer);
        m_autoTimer = nullptr;
    }
}

void BtMouse::clickLeftCb(lv_event_t *e)
{
    (void)e;
    sys::BleHid::clickMouse(0x01);
}

void BtMouse::clickRightCb(lv_event_t *e)
{
    (void)e;
    sys::BleHid::clickMouse(0x02);
}

void BtMouse::speedCb(lv_event_t *e)
{
    auto *self = static_cast<BtMouse *>(lv_event_get_user_data(e));
    self->m_speedX2 = !self->m_speedX2;
    self->updateSpeedLabel();
}

void BtMouse::autoClickCb(lv_event_t *e)
{
    auto *self = static_cast<BtMouse *>(lv_event_get_user_data(e));
    self->m_autoClick = !self->m_autoClick;
    self->updateAutoLabel();
    self->restartAutoClick();
}

void BtMouse::rateCb(lv_event_t *e)
{
    auto *self = static_cast<BtMouse *>(lv_event_get_user_data(e));
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    if (sel >= (sizeof(kRateOptions) / sizeof(kRateOptions[0]))) sel = 0;
    self->m_autoRate = kRateOptions[sel];
    if (self->m_autoClick) self->restartAutoClick();
}

void BtMouse::autoClickTimerCb(lv_timer_t *timer)
{
    (void)timer;
    sys::BleHid::clickMouse(0x01);
}

void BtMouse::updateSpeedLabel(void)
{
    if (m_speedLabel) lv_label_set_text(m_speedLabel, m_speedX2 ? "x2" : "x1");
}

void BtMouse::updateAutoLabel(void)
{
    if (m_autoLabel) lv_label_set_text(m_autoLabel, m_autoClick ? "C:ON" : "C:OFF");
}

void BtMouse::restartAutoClick(void)
{
    if (m_autoTimer) {
        lv_timer_delete(m_autoTimer);
        m_autoTimer = nullptr;
    }
    if (m_autoClick && m_autoRate > 0) {
        uint32_t period = 1000 / m_autoRate;
        if (period < 1) period = 1;
        m_autoTimer = lv_timer_create(autoClickTimerCb, period, this);
    }
}

void BtMouse::emitMove(int32_t dx, int32_t dy, uint8_t buttons)
{
    // 1) 指针加速:按本次事件的瞬时速度(曼哈顿位移)非线性放大增益。
    //    慢速时接近基准增益(高精度);快速时增益增大(快速跨屏),并限制上限。
    const float speed = static_cast<float>(std::abs(dx) + std::abs(dy));
    float accel = 1.0f + kAccelFactor * speed;
    if (accel > kAccelCap) accel = kAccelCap;
    const float gain = kMoveGainBase * (m_speedX2 ? 2.0f : 1.0f) * accel;

    // 2) 小数累积:放大后的位移带小数,只取整数部分发送,余量留到下次,
    //    避免慢速滑动因整数截断而"移不动/发涩"。
    m_accX += static_cast<float>(dx) * gain;
    m_accY += static_cast<float>(dy) * gain;

    int32_t moveX = static_cast<int32_t>(m_accX);
    int32_t moveY = static_cast<int32_t>(m_accY);
    m_accX -= static_cast<float>(moveX);
    m_accY -= static_cast<float>(moveY);

    if (moveX == 0 && moveY == 0) return;

    // 3) 大位移拆分:单份 HID 报告位移范围为 int8(-127..127),
    //    超出时拆成多份逐一发送,防止快速滑动被饱和截断而"追不上手指"。
    while (moveX != 0 || moveY != 0) {
        const int8_t stepX = clampInt8(moveX);
        const int8_t stepY = clampInt8(moveY);

        sys::BleHid::sendMouse(stepX, stepY, 0, buttons);
        moveX -= stepX;
        moveY -= stepY;
    }
}

void BtMouse::touchpadCb(lv_event_t *e)
{
    auto *self = static_cast<BtMouse *>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    const uint32_t now = lv_tick_get();

    if (code == LV_EVENT_PRESSED) {
        self->m_lastPoint = p;
        self->m_totalMove = 0;
        self->m_touching = true;
        self->m_pressMs = now;
        // 新手势开始,清空小数累积余量,避免上次残留造成起始跳动
        self->m_accX = 0.0f;
        self->m_accY = 0.0f;

        // 若上一次是有效轻点(tap),且本次按下落在拖拽窗口内 -> 进入拖拽待命。
        // 待命状态下,真正开始拖拽还需二次按下后有一定移动(kDragMoveStart),
        // 从而与"想再点一下"区分开,防止误触发拖拽。
        if (self->m_dragArmed &&
            (now - self->m_lastReleaseMs) <= kDragWindowMs) {
            // 保持待命,等待 PRESSING 判断是否真的开始拖动
            self->m_dragging = false;
        } else {
            self->m_dragArmed = false;
            self->m_dragging = false;
        }
    } else if (code == LV_EVENT_PRESSING && self->m_touching) {
        int32_t dx = p.x - self->m_lastPoint.x;
        int32_t dy = p.y - self->m_lastPoint.y;
        self->m_lastPoint = p;
        self->m_totalMove += std::abs(dx) + std::abs(dy);

        // 处于拖拽待命且移动量已达阈值 -> 正式进入按住左键拖拽
        if (self->m_dragArmed && !self->m_dragging &&
            self->m_totalMove >= kDragMoveStart) {
            self->m_dragging = true;
        }

        if (dx != 0 || dy != 0) {
            // 拖拽中带上左键按下位(0x01),否则普通移动(button=0);
            // 加速/小数累积/大位移拆分由 emitMove 统一处理。
            uint8_t buttons = self->m_dragging ? 0x01 : 0x00;
            self->emitMove(dx, dy, buttons);
        }
    } else if (code == LV_EVENT_RELEASED && self->m_touching) {
        self->m_touching = false;
        const bool wasDragging = self->m_dragging;
        const uint32_t heldMs = now - self->m_pressMs;

        if (wasDragging) {
            // 拖拽结束:释放左键
            sys::BleHid::sendMouse(0, 0, 0, 0x00);
            self->m_dragging = false;
            self->m_dragArmed = false;
        } else if (self->m_totalMove < kClickThreshold && heldMs <= kTapMaxMs) {
            // 一次有效轻点(位移小且时间短)
            if (self->m_dragArmed &&
                (now - self->m_lastReleaseMs) <= kDragWindowMs) {
                // 处于待命窗口内的第二次轻点却没有拖动 -> 视为双击,执行一次单击,
                // 并退出待命(不进入拖拽)
                sys::BleHid::clickMouse(0x01);
                self->m_dragArmed = false;
            } else {
                // 首次轻点:发送单击,并进入拖拽待命窗口
                sys::BleHid::clickMouse(0x01);
                self->m_dragArmed = true;
            }
        } else {
            // 位移大或按住时间长 -> 只是普通移动光标,退出待命
            self->m_dragArmed = false;
        }
        self->m_lastReleaseMs = now;
    }
}

void BtMouse::wheelCb(lv_event_t *e)
{
    auto *self = static_cast<BtMouse *>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        self->m_wheelLastY = p.y;
        self->m_wheeling = true;
    } else if (code == LV_EVENT_PRESSING && self->m_wheeling) {
        int32_t dy = p.y - self->m_wheelLastY;
        while (dy >= kWheelStep) {
            sys::BleHid::sendMouse(0, 0, -1); // 手指下滑 -> 向下滚
            self->m_wheelLastY += kWheelStep;
            dy -= kWheelStep;
        }
        while (dy <= -kWheelStep) {
            sys::BleHid::sendMouse(0, 0, 1); // 手指上滑 -> 向上滚
            self->m_wheelLastY -= kWheelStep;
            dy += kWheelStep;
        }
    } else if (code == LV_EVENT_RELEASED) {
        self->m_wheeling = false;
    }
}

void BtMouse::statusTimerCb(lv_timer_t *timer)
{
    static_cast<BtMouse *>(lv_timer_get_user_data(timer))->refreshStatus();
}

void BtMouse::refreshStatus(void)
{
    if (!m_status) return;
    lv_obj_set_style_bg_color(m_status,
        sys::BleHid::connected() ? ui::theme::success() : ui::theme::danger(), 0);
    lv_obj_set_style_bg_opa(m_status, LV_OPA_COVER, 0);
}

} // namespace apps