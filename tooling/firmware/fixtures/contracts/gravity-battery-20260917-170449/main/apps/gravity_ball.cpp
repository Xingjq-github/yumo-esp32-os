#include "gravity_ball.hpp"

#include "core/qmi8658.hpp"
#include "core/settings_store.hpp"
#include "core/system_service.hpp"
#include "ui/compact_ui.hpp"
#include "theme.hpp"
#include "core/power_mgr.hpp"

#include <algorithm>
#include <cmath>

namespace apps {
namespace {

constexpr float kRadius = 15.0f;
constexpr float kMin = kRadius;
constexpr float kMax = 240.0f - kRadius;
constexpr float kAccelGain = 0.75f;
constexpr float kDamping = 0.94f;
constexpr float kBounce = 0.62f;
constexpr float kMaxSpeed = 9.0f;
constexpr float kDeadZone = 0.035f;

float deadZone(float value)
{
    return std::fabs(value) < kDeadZone ? 0.0f : value;
}

}// namespace

const char *GravityBall::icon(void) const
{
    return LV_SYMBOL_GPS;
}

void GravityBall::onCreate(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, ui::theme::bg(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    m_status = lv_label_create(parent);
    lv_obj_set_style_text_color(m_status, ui::theme::muted(), 0);
    lv_obj_align(m_status, LV_ALIGN_TOP_MID, 0, 10);

    m_ball = lv_obj_create(parent);
    lv_obj_set_size(m_ball, static_cast<int>(kRadius * 2.0f),
                    static_cast<int>(kRadius * 2.0f));
    lv_obj_set_style_radius(m_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(m_ball, ui::theme::accent(), 0);
    lv_obj_set_style_bg_opa(m_ball, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(m_ball, 0, 0);
    lv_obj_clear_flag(m_ball, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(m_ball, LV_OBJ_FLAG_CLICKABLE);

    m_enable=ui::compact::button(parent,"启用感应",[](lv_event_t *) {
        (void)sys::SystemService::request(sys::ServiceAction::Motion,true);
    },nullptr,116,38);
    lv_obj_align(m_enable,LV_ALIGN_BOTTOM_MID,0,-2);
    lv_label_set_text(m_status,"感应已关闭");
    placeBall();
    m_timer = lv_timer_create(tickCb, 25, this);
}

void GravityBall::onShow() { sys::Qmi8658::setInUse(true);if(m_timer) lv_timer_resume(m_timer); }
void GravityBall::onHide() { sys::Qmi8658::setInUse(false);if(m_timer) lv_timer_pause(m_timer); }

void GravityBall::onDestroy(void)
{
    if (m_timer) {
        lv_timer_delete(m_timer);
        m_timer = nullptr;
    }
}

void GravityBall::tickCb(lv_timer_t *timer)
{
    static_cast<GravityBall *>(lv_timer_get_user_data(timer))->tick();
}

void GravityBall::tick(void)
{
    if(!sys::Power::isScreenOn()) return;
    const bool ready=sys::Settings::motionSensorOn() && sys::Qmi8658::enabled();
    const bool busy=sys::SystemService::busy(sys::ServiceDomain::System);
    ui::compact::disabled(m_enable,busy);
    if(ready) lv_obj_add_flag(m_enable,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(m_enable,LV_OBJ_FLAG_HIDDEN);
    ui::compact::setText(m_status,ready?"倾斜设备":busy?"正在启用…":sys::Settings::motionSensorOn()?"感应不可用，请重试":"感应已关闭");
    m_sensorReady=ready;
    if(!m_sensorReady) return;

    sys::Acceleration acceleration = {};
    if (!sys::Qmi8658::readAcceleration(acceleration)) return;

    // Hardware-verified board/display mapping: +X moves right and +Y moves
    // down in LVGL screen coordinates. Keep axis conversion in the app layer.
    const float targetAx = deadZone(acceleration.x);
    const float targetAy = deadZone(acceleration.y);
    m_ax = m_ax * 0.78f + targetAx * 0.22f;
    m_ay = m_ay * 0.78f + targetAy * 0.22f;

    m_vx = std::clamp((m_vx + m_ax * kAccelGain) * kDamping,
                      -kMaxSpeed, kMaxSpeed);
    m_vy = std::clamp((m_vy + m_ay * kAccelGain) * kDamping,
                      -kMaxSpeed, kMaxSpeed);
    m_x += m_vx;
    m_y += m_vy;

    if (m_x < kMin) { m_x = kMin; m_vx = -m_vx * kBounce; }
    if (m_x > kMax) { m_x = kMax; m_vx = -m_vx * kBounce; }
    if (m_y < kMin) { m_y = kMin; m_vy = -m_vy * kBounce; }
    if (m_y > kMax) { m_y = kMax; m_vy = -m_vy * kBounce; }

    placeBall();
}

void GravityBall::placeBall(void)
{
    if (!m_ball) return;
    lv_obj_set_pos(m_ball,
                   static_cast<int>(m_x - kRadius),
                   static_cast<int>(m_y - kRadius));
}

}// namespace apps