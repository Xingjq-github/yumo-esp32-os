#include "password_vault.hpp"
#include "core/connectivity.hpp"
#include "core/event_bus.hpp"
#include "core/power_mgr.hpp"
#include "core/settings_store.hpp"
#include "core/system_service.hpp"
#include "core/vault_text.hpp"
#include "ui/vault_qr.hpp"
#include "ui/compact_ui.hpp"
#include "ui/text_input.hpp"
#include "ui/theme.hpp"
#include "ui/window_mgr.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
namespace apps {
namespace c = ui::compact;
namespace {
constexpr uint32_t IdlePollMs = 100;
constexpr uint32_t JobPollMs = 20;
const char *categoryKeys[] = {"work", "app", "web", "other"};
const char *categoryNames[] = {"工作", "应用", "网页", "其他"};
void wrap(lv_obj_t *p, const char *text) {
  auto label = c::label(p, text, 238);
  lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
}
void peerLabel(lv_obj_t *p, const sys::BleBond &peer) {
  wrap(p, peer.name[0] ? peer.name : "未命名设备");
  char address[18];
  sys::BleHid::formatAddr(peer, address, sizeof(address));
  auto label = c::label(p, address, 238);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, ui::theme::muted(), 0);
}
} // namespace
void PasswordVault::wipeField() {
  if (!m_field)
    return;
  const char *text = lv_textarea_get_text(m_field);
  if (text)
    vault::wipe(const_cast<char *>(text), std::strlen(text));
  lv_textarea_set_text(m_field, "");
  ui::TextInput::hide();
  m_field = nullptr;
}
void PasswordVault::message(const char *text) {
  m_message = text ? text : "";
  m_redraw = true;
}
bool PasswordVault::changingPin() const {
  return m_page == Page::ChangeOld || m_page == Page::ChangeNew ||
         m_page == Page::ChangeConfirm;
}
void PasswordVault::finishPinChange(const char *text) {
  vault::clear(m_pin);
  vault::clear(m_firstPin);
  vault::clear(m_oldPin);
  m_reveal.hide();
  // Never let a cancelled/detached result reopen the vault after screen-off.
  if (!changingPin())
    return;
  if (!m_session) {
    lock();
    return;
  }
  m_page = Page::Categories;
  message(text);
}
void PasswordVault::submitPinChange() {
  if (!changingPin() || !m_session)
    return;
  if (!vault::pattern::valid(m_pin.data(), m_pin.size())) {
    finishPinChange("请至少连接 4 个点");
    return;
  }
  try {
    if (m_page == Page::ChangeNew) {
      m_firstPin = m_pin;
      vault::clear(m_pin);
      m_page = Page::ChangeConfirm;
      m_message.clear();
      m_redraw = true;
      return;
    }
    const bool old = m_page == Page::ChangeOld;
    if (!old && m_pin != m_firstPin) {
      finishPinChange("修改失败：两次图形不同");
      return;
    }
    auto job = std::make_shared<vault::Job>(old ? vault::Operation::VerifyChangePin
                                                : vault::Operation::ChangePin);
    job->input = m_session;
    if (old) {
      m_oldPin = m_pin;
      job->master = m_pin;
    } else {
      job->master = m_oldPin;
      job->replacement = m_pin;
      vault::clear(m_oldPin);
      vault::clear(m_firstPin);
    }
    vault::clear(m_pin);
    if (launch(job) != vault::StartResult::Started)
      finishPinChange("修改失败：任务未启动，请稍后重试");
  } catch (const std::bad_alloc &) {
    finishPinChange("修改失败：内存不足，请稍后重试");
  }
}
void PasswordVault::rejectReveal(const char *text) {
  m_qrPending = false;
  m_reveal.hide();
  m_redraw = true;
  // A late result must not reopen a locked or different page.
  if (m_page != Page::VerifyReveal)
    return;
  vault::clear(m_pin);
  vault::clear(m_firstPin);
  if (!m_session || m_selected >= m_session->entries.size()) {
    lock();
    return;
  }
  m_page = Page::Detail;
  // A failed launch already supplied its specific error message.
  if (text)
    message(text);
}
void PasswordVault::finishAccess(bool verified, const char *text) {
  vault::clear(m_pin);
  if (m_page != Page::VerifyAccess || !m_session) {
    m_access.leavePage();
    return;
  }
  const bool global = m_accessScope == vault::interaction::AccessState::Global;
  const bool current = global || m_accessScope == m_webAccount;
  const bool accepted = m_access.complete(verified && current, m_accessScope);
  m_page = global ? Page::Categories : Page::List;
  m_accessScope = vault::interaction::AccessState::None;
  if (accepted) {
    m_message.clear();
    lv_display_trigger_activity(nullptr);
  } else {
    m_message = text ? text : "验证失败，未展示密码";
  }
  m_redraw = true;
}
lv_obj_t *PasswordVault::button(lv_obj_t *parent, const char *text,
                                Action action, size_t index, int width) {
  auto result = c::button(parent, text, clicked, this, width, 40);
  lv_obj_set_user_data(
      result, reinterpret_cast<void *>((static_cast<uintptr_t>(action) << 16) |
                                       (index & 0xffff)));
  if (auto label = lv_obj_get_child(result, 0)) {
    lv_obj_set_width(label, width - 10);
    lv_obj_set_height(label, ui::theme::fontSmall()->line_height);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  }
  return result;
}
void PasswordVault::onCreate(lv_obj_t *parent) { setRoot(parent); }
void PasswordVault::onShow() {
  m_visible = true;
  m_screenOff = sys::EventBus::subscribe(
      sys::EventType::ScreenOff, [this](const sys::Event &) { lock(); });
  m_timer = lv_timer_create(timer, IdlePollMs, this);
  m_load.reset(lv_tick_get());
  load();
  draw();
}
void PasswordVault::lock() {
  m_qrPending = false;
  vault::cancel(m_job);
  m_job.reset();
  m_session.reset();
  resetListIndex();
  clearListWidgets();
  wipeField();
  vault::clear(m_oldPin);
  m_load.reset(lv_tick_get());
  m_exists = m_blank = m_needsPatternSetup = false;
  vault::clear(m_pin);
  vault::clear(m_firstPin);
  m_reveal.hide();
  m_pattern.detach();
  m_patternPending = false;
  m_access.lock();
  m_accessScope = vault::interaction::AccessState::None;
  if (m_pairStarted)
    sys::SystemService::cancelBluetoothConnection();
  m_pairStarted = false;
  m_btPending = false;
  m_page = Page::Locked;
  m_message = "已锁定";
  m_redraw = true;
  // Remove decrypted account/title labels even while the panel is dark.
  m_status = m_peerName = m_peerAddress = nullptr;
  m_bleButton = m_pairButton = m_disconnectButton = nullptr;
  m_bondButtons.clear();
  if (root())
    lv_obj_clean(root());
  m_body = nullptr;
}
void PasswordVault::onHide() {
  m_visible = false;
  lock();
  // lock() already cancels this app's pending pairing/reconnect invitation.
  if (m_timer) {
    lv_timer_delete(m_timer);
    m_timer = nullptr;
  }
  if (m_screenOff) {
    sys::EventBus::unsubscribe(m_screenOff);
    m_screenOff = 0;
  }
}
void PasswordVault::onDestroy() {
  onHide();
  m_body = nullptr;
  m_status = nullptr;
}
bool PasswordVault::onBack() {
  m_patternPending = false;
  m_qrPending = false;
  if (m_page == Page::Qr) {
    m_reveal.hide();
    m_page = Page::Detail;
    m_redraw = true;
    return true;
  }
  if (m_job && m_job->operation == vault::Operation::ChangePin) {
    message("正在保存密码，请稍候");
    return true;
  }
  if (m_job) {
    vault::cancel(m_job);
    m_reveal.hide();
    message("正在取消…");
    return true;
  }
  if (changingPin()) {
    finishPinChange("修改失败：操作已取消");
    return true;
  }
  if (m_page == Page::VerifyAccess) {
    finishAccess(false, "验证已取消");
    return true;
  }
  m_reveal.hide();
  vault::clear(m_pin);
  vault::clear(m_firstPin);
  if (m_page == Page::Detail || m_page == Page::VerifyReveal) {
    m_page = m_page == Page::Detail ? Page::List : Page::Detail;
    m_message.clear();
    m_redraw = true;
    return true;
  }
  if (m_page == Page::Address) {
    m_page = Page::Network;
    m_redraw = true;
    return true;
  }
  if (m_page == Page::List && targetCategory() && !targetGroups()) {
    m_access.leavePage();
    m_targetGroup = vault::interaction::TargetIndex::None;
    selectList();
    m_message.clear();
    m_redraw = true;
    return true;
  }
  if (m_page == Page::List && m_category == 2 && !webAccounts()) {
    m_access.leavePage();
    m_webAccount = vault::interaction::WebIndex::None;
    selectList();
    m_message.clear();
    m_redraw = true;
    return true;
  }
  if (m_page != Page::Locked && m_page != Page::Categories) {
    m_page = m_session ? Page::Categories : Page::Locked;
    m_message.clear();
    m_redraw = true;
    return true;
  }
  return false;
}
vault::StartResult PasswordVault::launch(const std::shared_ptr<vault::Job> &job) {
  const auto result = m_job ? vault::StartResult::Busy : vault::start(job);
  if (result != vault::StartResult::Started) {
    message(result == vault::StartResult::Busy
                ? "上一任务未结束，请稍后重试"
            : result == vault::StartResult::NoMemory
                ? "内存不足，请稍后重试"
                : "系统正在休眠，请唤醒后重试");
    return result;
  }
  wipeField();
  m_job = job;
  // A completed KDF should not sit behind the idle UI's 100ms poll window.
  // The worker stays asynchronous; only its completion check runs faster.
  if (m_timer)
    lv_timer_set_period(m_timer, JobPollMs);
  m_message = "";
  m_redraw = true;
  return result;
}
void PasswordVault::load() {
  const auto now = lv_tick_get();
  if (m_job || !m_load.due(now))
    return;
  if (m_load.expired(now)) {
    m_load.finish(false);
    message("上一任务未结束，请稍后重试");
    return;
  }
  if (!vault::idle()) {
    const auto previous = m_load.phase();
    m_load.attempted(vault::StartResult::Busy, now);
    // Do not allocate jobs or rebuild LVGL widgets on each waiting tick.
    if (previous != vault::LoadState::Phase::Waiting)
      message("正在等待上一任务结束…");
    return;
  }
  try {
    m_load.attempted(
        launch(std::make_shared<vault::Job>(vault::Operation::Load)), now);
  } catch (const std::bad_alloc &) {
    m_load.finish(false);
    message("内存不足，请稍后重试");
  }
}
void PasswordVault::startTyping(bool username, uintptr_t link, bool enter) {
  m_reveal.hide();
  m_typePage = purposePage() ? Page::List : Page::Detail;
  m_page = m_typePage;
  m_redraw = true;
  if (!m_session || m_selected >= m_session->entries.size())
    return;
  if (!link || link != sys::BleHid::privateLinkGeneration()) {
    message("请先连接目标蓝牙设备并选中输入框");
    return;
  }
  try {
    auto job = std::make_shared<vault::Job>(enter ? vault::Operation::Enter
                                                : vault::Operation::Type);
    job->input = m_session;
    job->entry = m_selected;
    job->username = username;
    job->link = link;
    job->permit = sys::BleHid::beginPrivateInput();
    if (launch(job) != vault::StartResult::Started)
      sys::BleHid::cancelPrivateInput();
  } catch (const std::bad_alloc &) {
    sys::BleHid::cancelPrivateInput();
    message("内存不足，请稍后重试");
  }
}
void PasswordVault::requestBluetooth(Action action, size_t index) {
  if (m_page != Page::Network)
    return;
  if (m_btPending || sys::SystemService::busy(sys::ServiceDomain::Bluetooth)) {
    message("蓝牙正在处理，请稍候");
    return;
  }
  const auto state = sys::SystemService::bluetooth();
  const bool discovering = state.discovering();
  const bool canConnect = state.enabled &&
      state.state != sys::BleHidState::Starting;
  if ((action == Action::Bond || (action == Action::SecurePair && !discovering)) &&
      !canConnect) {
    message("请先打开蓝牙，等待就绪");
    return;
  }
  // ESP_OK from enqueue means accepted, not connected or even executed.
  // Track the service's completion separately from the actual GAP snapshot.
  const auto revision = sys::SystemService::result(sys::ServiceDomain::Bluetooth).revision;
  auto service = sys::ServiceAction::BluetoothEnable;
  esp_err_t result = ESP_ERR_INVALID_ARG;
  switch (action) {
  case Action::Ble:
    result = sys::SystemService::request(service, !state.enabled);
    break;
  case Action::SecurePair:
    service = sys::ServiceAction::BluetoothDiscover;
    result = sys::SystemService::request(service, !discovering);
    break;
  case Action::Bond:
    if (index >= m_bonds.size())
      return;
    service = sys::ServiceAction::BluetoothReconnect;
    // The system revalidates this identity against real bonds; never connect
    // using a stale list index, name or a newly scanned random address.
    result = sys::SystemService::reconnectBond(m_bonds[index]);
    break;
  case Action::Disconnect:
    service = sys::ServiceAction::BluetoothDisconnect;
    result = sys::SystemService::request(service);
    break;
  default:
    return;
  }
  if (result != ESP_OK) {
    message(sys::SystemService::errorText(result) ? sys::SystemService::errorText(result) :
            "蓝牙请求未接受，请稍后重试");
    return;
  }
  m_btRevision = revision;
  m_btAction = service;
  m_btPending = true;
  if (action == Action::SecurePair)
    m_pairStarted = !discovering;
  else if (action == Action::Bond)
    m_pairStarted = true; // own this bounded invitation until exit/lock
  else if (action == Action::Disconnect || (action == Action::Ble && state.enabled))
    m_pairStarted = false;
  message("蓝牙请求已提交，正在处理");
}
bool PasswordVault::patternPage() const {
  return m_page == Page::Locked || m_page == Page::VerifyReveal ||
         m_page == Page::VerifyAccess || changingPin();
}
const char *PasswordVault::patternPrompt() const {
  if (!m_message.empty()) return m_message.c_str();
  if (m_job) return m_job->operation == vault::Operation::Load ? "正在读取" : "正在验证";
  if (m_page == Page::ChangeOld) return "绘制原图形";
  if (m_page == Page::ChangeNew) return "设置新图形，至少 4 个点";
  if (m_page == Page::ChangeConfirm) return "再次绘制新图形";
  if (m_page == Page::Locked && m_needsPatternSetup)
    return m_firstPin.empty() ? "设置图形，至少 4 个点" : "再次绘制图形";
  return "绘制图形密码";
}
void PasswordVault::patternSubmitted(void *context, const char *value, size_t size) {
  auto self = static_cast<PasswordVault *>(context);
  if (!self->m_visible || !self->patternPage() || self->m_job || self->m_patternPending) return;
  self->m_pin.assign(value, size);
  self->m_patternPending = true; // submit from poll, never destroy the active touch object
  // Ready schedules the next GUI timer pass; it does not run the job or
  // replace the touch object while its RELEASED callback is still executing.
  if (self->m_timer)
    lv_timer_ready(self->m_timer);
}
void PasswordVault::drawPin(Action submit) {
  m_patternAction = submit;
  const bool ready = !m_job && (m_page != Page::Locked || (m_load.ready() && (m_exists || m_blank)));
  m_pattern.create(m_body, patternPrompt(), ready, patternSubmitted, this);
}
void PasswordVault::timer(lv_timer_t *t) {
  static_cast<PasswordVault *>(lv_timer_get_user_data(t))->poll();
}
void PasswordVault::clicked(lv_event_t *e) {
  auto self = static_cast<PasswordVault *>(lv_event_get_user_data(e));
  const auto packed = reinterpret_cast<uintptr_t>(
      lv_obj_get_user_data(lv_event_get_target_obj(e)));
  if (static_cast<Action>(packed >> 16) == Action::Detail ||
      static_cast<Action>(packed >> 16) == Action::Account ||
      static_cast<Action>(packed >> 16) == Action::TargetGroup ||
      static_cast<Action>(packed >> 16) == Action::PurposePassword ||
      static_cast<Action>(packed >> 16) == Action::PurposeDetail) {
    const auto pressed = self->m_pressedListEntry;
    self->m_pressedListEntry = vault::interaction::ListWindow::None;
    if (self->m_redraw || self->m_page != Page::List ||
        pressed != (packed & 0xffff))
      return;
  }
  if (sys::Power::isScreenOn() && !sys::Power::sleepPending())
    self->action(static_cast<Action>(packed >> 16), packed & 0xffff);
}
void PasswordVault::action(Action a, size_t index) {
  if (a == Action::Cancel) {
    if (m_job && m_job->operation == vault::Operation::ChangePin)
      return; // Save is short; screen-off/exit still cancels and locks.
    vault::cancel(m_job);
    m_reveal.hide();
    message("正在取消…");
    return;
  }
  if (a == Action::Back) {
    if (!onBack())
      ui::WindowManager::instance().goHome();
    return;
  }
  if (m_job)
    return;
  switch (a) {
  case Action::Super:
  case Action::PageReveal:
    if (!m_session || (a == Action::Super ? m_page != Page::Categories : !purposePage()))
      return;
    if (a == Action::Super && m_access.super()) {
      m_access.lock();
    } else if (a == Action::PageReveal && m_access.super()) {
      return;
    } else if (a == Action::PageReveal && m_access.visible(m_webAccount)) {
      m_access.leavePage();
    } else {
      m_accessScope = a == Action::Super ? vault::interaction::AccessState::Global : m_webAccount;
      m_access.request(m_accessScope);
      vault::clear(m_pin);
      m_page = Page::VerifyAccess;
    }
    m_message.clear();
    m_redraw = true;
    break;
  case Action::VerifyAccess:
    if (m_page != Page::VerifyAccess || !m_session)
      return;
    if (!vault::pattern::valid(m_pin.data(), m_pin.size())) {
      finishAccess(false, "请至少连接 4 个点");
      return;
    }
    try {
      auto job = std::make_shared<vault::Job>(vault::Operation::VerifyAccess);
      job->input = m_session;
      job->master = m_pin;
      job->entry = m_accessScope;
      vault::clear(m_pin);
      if (launch(job) != vault::StartResult::Started)
        finishAccess(false, "验证未启动，请稍后重试");
    } catch (const std::bad_alloc &) {
      finishAccess(false, "内存不足，请稍后重试");
    }
    break;
  case Action::ChangePassword:
    if (m_session && m_page == Page::Categories) {
      wipeField();
      vault::clear(m_pin);
      vault::clear(m_firstPin);
      vault::clear(m_oldPin);
      m_reveal.hide();
      m_page = Page::ChangeOld;
      m_message.clear();
      m_redraw = true;
    }
    break;
  case Action::SubmitChange:
    submitPinChange();
    break;
  case Action::RetryLoad:
    if (m_page == Page::Locked) {
      m_exists = m_blank = false;
      m_load.reset(lv_tick_get());
      load();
    }
    break;
  case Action::Unlock: {
    if (!m_load.ready() || (!m_exists && !m_blank))
      return;
    if (!vault::pattern::valid(m_pin.data(), m_pin.size())) {
      message("请至少连接 4 个点");
      return;
    }
    if (m_needsPatternSetup && m_firstPin.empty()) {
      m_firstPin = m_pin;
      vault::clear(m_pin);
      message("再次绘制图形");
      return;
    }
    if (m_needsPatternSetup && m_firstPin != m_pin) {
      vault::clear(m_pin);
      vault::clear(m_firstPin);
      message("两次不同，请重新设置");
      return;
    }
    auto job = std::make_shared<vault::Job>(m_needsPatternSetup ? vault::Operation::Setup
                                                                : vault::Operation::Unlock);
    job->master = m_pin;
    vault::clear(m_pin);
    vault::clear(m_firstPin);
    launch(job);
    break;
  }
  case Action::Category:
    if (index < 4 && m_session) {
      m_access.leavePage();
      m_reveal.hide();
      m_category = index;
      m_webAccount = vault::interaction::WebIndex::None;
      m_targetGroup = vault::interaction::TargetIndex::None;
      if (targetCategory())
        m_targetIndex.build(m_session->entries, categoryKeys[m_category]);
      else
        m_targetIndex.clear();
      selectList();
      m_page = Page::List;
      m_message.clear();
      m_redraw = true;
    }
    break;
  case Action::Address:
    m_page = Page::Address;
    m_message.clear();
    m_redraw = true;
    break;
  case Action::SaveAddress:
    if (m_field && m_session) {
      auto job = std::make_shared<vault::Job>(vault::Operation::SaveConfig);
      job->input = m_session;
      job->url = lv_textarea_get_text(m_field);
      launch(job);
    }
    break;
  case Action::Qr:
    if (m_page != Page::Detail || !m_session || m_selected >= m_session->entries.size()) return;
    if (m_session->entries[m_selected].category == "other" || m_access.super() || m_reveal.visible(m_selected)) {
      m_page = Page::Qr;
      m_qrSince = lv_tick_get();
      m_message.clear();
      m_redraw = true;
    } else {
      m_qrPending = true;
      action(Action::Reveal, 0);
    }
    break;
  case Action::Reveal:
    if (m_access.super() || m_page != Page::Detail || !m_session ||
        m_selected >= m_session->entries.size() ||
        m_session->entries[m_selected].category == "other")
      return;
    if (m_reveal.visible(m_selected)) {
      m_reveal.hide();
    } else {
      m_reveal.hide();
      vault::clear(m_pin);
      vault::clear(m_firstPin);
      m_page = Page::VerifyReveal;
    }
    m_message.clear();
    m_redraw = true;
    break;
  case Action::VerifyReveal:
    if (m_page != Page::VerifyReveal || !m_session ||
        m_selected >= m_session->entries.size())
      return;
    if (!vault::pattern::valid(m_pin.data(), m_pin.size())) {
      rejectReveal("请至少连接 4 个点");
      return;
    }
    try {
      auto job = std::make_shared<vault::Job>(vault::Operation::VerifyPin);
      job->input = m_session;
      job->entry = m_selected;
      job->master = m_pin;
      vault::clear(m_pin);
      m_reveal.request(m_selected);
      if (launch(job) != vault::StartResult::Started)
        rejectReveal(nullptr);
    } catch (const std::bad_alloc &) {
      rejectReveal("内存不足，请稍后重试");
    }
    break;
  case Action::Sync: {
    auto job = std::make_shared<vault::Job>(vault::Operation::Sync);
    job->input = m_session;
    launch(job);
    break;
  }
  case Action::Network:
    m_page = Page::Network;
    m_message.clear();
    m_redraw = true;
    break;
  case Action::Wifi: {
    auto state = sys::Connectivity::snapshot();
    auto err = sys::SystemService::request(sys::ServiceAction::WifiEnable,
                                           !state.enabled);
    message(err == ESP_OK ? "已请求切换Wi-Fi" :
        sys::SystemService::errorText(err) ? sys::SystemService::errorText(err) : "Wi-Fi请求失败");
    break;
  }
  case Action::Ble:
    requestBluetooth(a, index);
    break;
  case Action::WifiSaved:
    if (index < m_wifi.size()) {
      auto password = sys::Settings::wifiPasswordFor(m_wifi[index]);
      auto err = sys::SystemService::connectWifi(m_wifi[index], password);
      vault::clear(password);
      message(err == ESP_OK ? "检查附近网络后连接" :
          sys::SystemService::errorText(err) ? sys::SystemService::errorText(err) : "连接请求未接受");
    }
    break;
  case Action::SecurePair:
  case Action::Disconnect:
  case Action::Bond:
    requestBluetooth(a, index);
    break;
  case Action::TargetGroup:
    if (m_page == Page::List && targetGroups() && m_session &&
        index < m_session->entries.size() &&
        m_targetIndex.groupFor(index) != vault::interaction::TargetIndex::None) {
      m_targetGroup = m_targetIndex.groupFor(index);
      selectList();
      m_access.leavePage();
      m_reveal.hide();
      m_message.clear();
      m_redraw = true;
    }
    break;
  case Action::Account:
    if (m_page == Page::List && webAccounts() && m_session &&
        m_webIndex.groupFor(index) != vault::interaction::WebIndex::None) {
      m_webAccount = m_webIndex.groupFor(index);
      selectList();
      m_access.leavePage();
      m_selected = index;
      m_message.clear();
      m_redraw = true;
    }
    break;
  case Action::PurposeUser:
  case Action::PurposePassword:
    if (!purposePage() || !m_session)
      return;
    if (a == Action::PurposeUser)
      index = m_webIndex.entry(m_webAccount);
    if (index >= m_session->entries.size() || m_webIndex.groupFor(index) != m_webAccount)
      return;
    m_selected = index;
    startTyping(a == Action::PurposeUser, sys::BleHid::privateLinkGeneration());
    break;
  case Action::Detail:
    if (m_page == Page::List && !webAccounts() && !purposePage() &&
        !targetGroups() && m_session && index < m_session->entries.size() &&
        m_session->entries[index].category == categoryKeys[m_category] &&
        (!targetCategory() || m_targetIndex.groupFor(index) == m_targetGroup)) {
      m_selected = index;
      m_reveal.hide();
      m_page = Page::Detail;
      m_message.clear();
      m_redraw = true;
    }
    break;
  case Action::PurposeDetail:
    if (purposePage() && m_session && index < m_session->entries.size() &&
        m_webIndex.groupFor(index) == m_webAccount) {
      m_selected = index;
      m_reveal.hide();
      m_page = Page::Detail;
      m_message.clear();
      m_redraw = true;
    }
    break;
  case Action::User:
    if (m_page == Page::Detail && m_session && m_selected < m_session->entries.size() &&
        m_session->entries[m_selected].category != "other")
      startTyping(true, sys::BleHid::privateLinkGeneration());
    break;
  case Action::Password:
    if (m_page != Page::Detail || !m_session ||
        m_selected >= m_session->entries.size())
      return;
    startTyping(false, sys::BleHid::privateLinkGeneration());
    break;
  case Action::Enter:
    if (m_page == Page::Detail || purposePage())
      startTyping(false, sys::BleHid::privateLinkGeneration(), true);
    break;
  case Action::Settings: {
    auto &wm = ui::WindowManager::instance();
    const auto &apps = wm.apps();
    for (size_t i = 0; i < apps.size(); ++i)
      if (!std::strcmp(apps[i].name, "设置")) {
        wm.openApp(i);
        break;
      }
    break;
  }
  default:
    break;
  }
}
void PasswordVault::poll() {
  if (!m_visible)
    return;
  if (!sys::Power::isScreenOn() || sys::Power::sleepPending()) {
    if (m_session || m_field)
      lock();
    return;
  }
  m_pattern.poll();
  if (m_patternPending) {
    m_patternPending = false;
    action(m_patternAction, 0);
  }
  if (m_session && !m_job && lv_display_get_inactive_time(nullptr) >= 60000)
    lock();
  if (m_reveal.expire(lv_tick_get()))
    m_redraw = true;
  if (m_page == Page::Qr && (lv_tick_elaps(m_qrSince) >= 30000 ||
      (!m_access.super() && m_session && m_session->entries[m_selected].category != "other" && !m_reveal.visible(m_selected)))) {
    m_reveal.hide();
    m_page = Page::Detail;
    m_redraw = true;
  }
  if (m_job && m_job->done.load()) {
    const auto kind = m_job->operation;
    bool success = m_job->ok && !m_job->cancelled.load();
    if (success) {
      switch (kind) {
      case vault::Operation::WebUnlock:
        lock(); // Browser-only jobs are never owned by the device UI.
        return;
      case vault::Operation::Load:
        m_load.finish(true);
        m_exists = m_job->exists;
        m_blank = m_job->blank;
        m_needsPatternSetup = m_job->needsPatternSetup;
        break;
      case vault::Operation::Setup:
      case vault::Operation::SaveConfig:
      case vault::Operation::Unlock:
      case vault::Operation::Sync:
        m_session = std::move(m_job->output);
        resetListIndex();
        // Start the idle-lock window after a user-requested KDF/sync,
        // not while that operation was still computing.
        lv_display_trigger_activity(nullptr);
        m_exists = true;
        m_blank = m_needsPatternSetup = false;
        m_page = kind == vault::Operation::SaveConfig ? Page::Network
                                                      : Page::Categories;
        m_message = kind == vault::Operation::Sync
            ? (m_job->sdConfigUsed ? "同步成功，已保存SD配置地址" : "同步成功") : "";
        break;
      case vault::Operation::VerifyPin:
        if (m_reveal.complete(m_page == Page::VerifyReveal && m_session &&
                                  m_selected == m_job->entry,
                              m_job->entry, lv_tick_get())) {
          m_page = Page::Detail;
          m_message.clear();
          lv_display_trigger_activity(nullptr);
          if (m_qrPending) {
            m_qrPending = false;
            m_page = Page::Qr;
            m_qrSince = lv_tick_get();
          }
        } else {
          rejectReveal("查看已取消");
        }
        break;
      case vault::Operation::VerifyAccess:
        // The worker wipes/releases input before publishing done. The owned
        // job, current verification page and pending scope gate the result;
        // lock() detaches the job before destroying/replacing the session.
        finishAccess(m_job->entry == m_accessScope,
                     "验证已取消");
        break;
      case vault::Operation::VerifyChangePin:
        if (m_page == Page::ChangeOld && m_session) {
          m_page = Page::ChangeNew;
          m_message.clear();
          lv_display_trigger_activity(nullptr);
        } else {
          finishPinChange("修改失败：验证已取消");
        }
        break;
      case vault::Operation::ChangePin:
        if (m_page == Page::ChangeConfirm && m_session && m_job->output) {
          m_session = std::move(m_job->output);
          resetListIndex();
          lv_display_trigger_activity(nullptr);
          finishPinChange("密码修改成功");
        } else {
          lock();
        }
        break;
      case vault::Operation::Type:
        m_page = m_typePage;
        message("已发送，请核对");
        break;
      case vault::Operation::Enter:
        m_page = m_typePage;
        message("回车已发送");
        break;
      }
    } else {
      if ((kind == vault::Operation::ChangePin || kind == vault::Operation::Setup) && m_job->configWriteStarted) {
        // A late cancel can race the atomic header write. Reload the actual PIN
        // from disk before any sync/config save; never overwrite it via old RAM.
        lock();
        message("保存结果未确认，请重新解锁核对");
      } else if (kind == vault::Operation::VerifyChangePin ||
                 kind == vault::Operation::ChangePin) {
        finishPinChange("修改失败：原图形错误或操作未完成");
      } else if (kind == vault::Operation::VerifyAccess)
        finishAccess(false, m_job->error.empty() ? "操作已取消" : m_job->error.c_str());
      else if (kind == vault::Operation::VerifyPin)
        rejectReveal(m_job->error.empty() ? "操作已取消" : m_job->error.c_str());
      else
        message(m_job->error.empty() ? "操作已取消" : m_job->error.c_str());
      if (kind == vault::Operation::Load) {
        m_load.finish(false);
        m_exists = m_blank = false;
      }
      if (kind == vault::Operation::Type || kind == vault::Operation::Enter)
        m_page = m_session ? m_typePage : Page::Locked;
    }
    m_job.reset();
    if (m_timer)
      lv_timer_set_period(m_timer, IdlePollMs);
    m_redraw = true;
  }
  if (!m_job && m_page == Page::Locked)
    load();
  if (m_page == Page::Network && !m_job) {
    const auto state = sys::SystemService::bluetooth();
    if (state.bondRevision != m_bondRevision)
      m_redraw = true;
    if (m_btPending && !sys::SystemService::busy(sys::ServiceDomain::Bluetooth)) {
      const auto result = sys::SystemService::result(sys::ServiceDomain::Bluetooth);
      if (result.revision != m_btRevision) {
        m_btPending = false;
        m_message.clear();
        if (result.action == m_btAction && result.error != ESP_OK) {
          m_message = std::string("蓝牙请求失败：") + esp_err_to_name(result.error);
          if (m_btAction == sys::ServiceAction::BluetoothDiscover ||
              m_btAction == sys::ServiceAction::BluetoothReconnect)
            m_pairStarted = false;
        }
        m_redraw = true;
      }
    }
  }
  if (m_redraw && !m_pattern.drawing())
    draw();
  if (m_job && m_status) {
    char text[96];
    if (m_job->operation == vault::Operation::Type)
      std::snprintf(text, sizeof(text), "正在输入\n已发送 %u 个字符",
                    m_job->progress.load());
    else if (m_job->operation == vault::Operation::Sync)
      std::snprintf(text, sizeof(text), "%s", m_job->syncStage.load() == 0
          ? "正在连接密码服务" : m_job->syncStage.load() == 1
          ? "同步失败，尝试从SD卡获取配置后再同步" : "正在使用SD配置同步");
    else if (m_job->operation == vault::Operation::Enter)
      std::snprintf(text, sizeof(text), "正在发送回车");
    else if (m_job->operation == vault::Operation::VerifyPin ||
             m_job->operation == vault::Operation::VerifyAccess)
      std::snprintf(text, sizeof(text), "正在验证管理员密码");
    else
      std::snprintf(text, sizeof(text), "正在处理，请稍候");
    c::setText(m_status, text);
  }
  if (m_page == Page::Network && !m_job && m_status) {
    const auto w = sys::Connectivity::snapshot();
    const auto b = sys::SystemService::bluetooth();
    char address[18];
    const auto shownPeer=b.connecting()?b.target:b.peer;
    sys::BleHid::formatAddr(shownPeer, address, sizeof(address));
    std::string s = w.state == sys::WifiState::Failed
                        ? (w.error==ESP_ERR_NO_MEM ? "Wi-Fi: 内存不足，请重试" : "Wi-Fi: 操作失败，代码 "+std::to_string(w.error))
                    : w.transitioning ? "Wi-Fi: 切换中"
                    : w.state == sys::WifiState::Connected ? "Wi-Fi: " + w.ssid
                    : w.enabled                          ? "Wi-Fi: 未连接"
                                                         : "Wi-Fi: 已关闭";
    s += "\n";
    const bool connected = b.state == sys::BleHidState::Connected;
    if (connected)
      s += sys::BleHid::privateLinkGeneration() ? "蓝牙：可输入"
                                                : "蓝牙：不可输入密码";
    else if (b.state == sys::BleHidState::Starting)
      s += "蓝牙：启动中";
    else if (b.switching)
      s += "蓝牙：正在结束旧连接";
    else if (b.connecting())
      s += "蓝牙：等待目标设备连接";
    else if (b.discovering())
      s += "蓝牙：等待配对（电脑选择Brookesia-s3）";
    else if (b.state == sys::BleHidState::Connecting)
      s += "蓝牙：连接中";
    else if (b.hasError())
      s += "蓝牙：连接失败，请到设置检查";
    else
      s += b.enabled ? "蓝牙：未连接" : "蓝牙：已关闭";
    const bool showPeer = connected || b.connecting();
    if (m_peerName && m_peerAddress) {
      c::setText(m_peerName, showPeer
                                 ? (shownPeer.name[0] ? shownPeer.name : "未命名设备")
                                 : "");
      c::setText(m_peerAddress, showPeer ? address : "");
      if (showPeer) {
        lv_obj_set_hidden(m_peerName,false);
        lv_obj_set_hidden(m_peerAddress,false);
      } else {
        lv_obj_set_hidden(m_peerName,true);
        lv_obj_set_hidden(m_peerAddress,true);
      }
    }
    const bool busy = m_btPending || sys::SystemService::busy(sys::ServiceDomain::Bluetooth);
    const bool ready = b.enabled && !busy && b.state != sys::BleHidState::Starting;
    if (m_bleButton) {
      c::setText(lv_obj_get_child(m_bleButton, 0), b.enabled ? "关闭蓝牙" : "打开蓝牙");
      c::disabled(m_bleButton, busy);
    }
    if (m_pairButton) {
      c::setText(lv_obj_get_child(m_pairButton, 0), b.discovering() ? "停止配对" : "配对新设备");
      c::disabled(m_pairButton, !ready);
    }
    if (m_disconnectButton) {
      const bool canCancel = b.cancellable();
      c::setText(lv_obj_get_child(m_disconnectButton, 0), canCancel ? "取消连接" : "断开蓝牙");
      c::disabled(m_disconnectButton, busy || (!connected && !canCancel));
    }
    for (auto row : m_bondButtons)
      c::disabled(row, !ready);
    if (s != m_lastNetwork) {
      m_lastNetwork = s;
      c::setText(m_status, s.c_str());
    }
  }
}
void PasswordVault::resetListIndex() {
  m_access.leavePage();
  m_listIndex.clear();
  m_categoryCounts.fill(0);
  m_listOffsets.fill(0);
  m_webOffsets.fill(0);
  m_webAccount = vault::interaction::WebIndex::None;
  m_webIndex.clear();
  m_targetOffsets.fill(0);
  m_targetGroup = vault::interaction::TargetIndex::None;
  m_targetIndex.clear();
  m_pressedListEntry = vault::interaction::ListWindow::None;
  if (m_session) {
    m_webIndex.build(m_session->entries);
    for (const auto &entry : m_session->entries)
      for (size_t k = 0; k < m_categoryCounts.size(); ++k)
        if (entry.category == categoryKeys[k]) {
          ++m_categoryCounts[k];
          break;
        }
  }
}
bool PasswordVault::webAccounts() const {
  return m_category == 2 && m_webAccount == vault::interaction::WebIndex::None;
}
bool PasswordVault::targetCategory() const {
  return m_category == 0 || m_category == 1;
}
bool PasswordVault::targetGroups() const {
  return targetCategory() && m_targetGroup == vault::interaction::TargetIndex::None;
}
const char *PasswordVault::targetName(size_t index) const {
  if (!m_session || index >= m_session->entries.size() || m_session->entries[index].target.empty())
    return "未填写";
  return m_session->entries[index].target.c_str();
}
bool PasswordVault::purposePage() const {
  return m_page == Page::List && m_category == 2 && m_webAccount < m_webIndex.groups();
}
int PasswordVault::listPitch() const {
  return purposePage() ? vault::interaction::PurposeLayout::Pitch : vault::interaction::ListWindow::Pitch;
}
const char *PasswordVault::listTitle() const {
  if (targetCategory())
    return targetGroups() ? (m_category == 0 ? "单位 / 系统" : "应用名称 / 平台")
                          : targetName(m_targetIndex.entry(m_targetGroup));
  if (m_category != 2) return categoryNames[m_category];
  return webAccounts() ? "网页账号" : "用途列表";
}
int32_t &PasswordVault::listOffset() {
  if (targetCategory() && m_targetGroup < m_targetIndex.groups())
    return m_targetOffsets[m_targetIndex.entry(m_targetGroup)];
  return m_category == 2 && m_webAccount < m_webIndex.groups()
             ? m_webOffsets[m_webAccount] : m_listOffsets[m_category];
}
void PasswordVault::selectList() {
  m_listIndex.clear();
  if (targetCategory()) {
    if (targetGroups()) {
      for (size_t g = 0; g < m_targetIndex.groups(); ++g)
        m_listIndex.append(m_targetIndex.entry(g));
    } else {
      for (size_t i = 0; i < m_targetIndex.count(m_targetGroup); ++i)
        m_listIndex.append(m_targetIndex.entry(m_targetGroup, i));
    }
    return;
  }
  if (m_category == 2) {
    if (webAccounts()) {
      for (size_t g = 0; g < m_webIndex.groups(); ++g)
        m_listIndex.append(m_webIndex.entry(g));
    } else {
      for (size_t i = 0; i < m_webIndex.count(m_webAccount); ++i)
        m_listIndex.append(m_webIndex.entry(m_webAccount, i));
    }
    return;
  }
  if (m_session)
    for (size_t i = 0; i < m_session->entries.size(); ++i)
      if (m_session->entries[i].category == categoryKeys[m_category])
        m_listIndex.append(i);
}
void PasswordVault::clearListWidgets() {
  // Detach before deleting LVGL objects: deletion can send scroll events.
  m_listViewport = nullptr;
  m_listRows.fill(ListRow{});
  m_pressedListEntry = vault::interaction::ListWindow::None;
  m_listUpdating = false;
}
void PasswordVault::listPressed(lv_event_t *e) {
  auto self = static_cast<PasswordVault *>(lv_event_get_user_data(e));
  self->m_pressedListEntry = reinterpret_cast<uintptr_t>(
      lv_obj_get_user_data(lv_event_get_target_obj(e))) & 0xffff;
}
void PasswordVault::listScrolled(lv_event_t *e) {
  auto self = static_cast<PasswordVault *>(lv_event_get_user_data(e));
  if (!self->m_listViewport || self->m_listUpdating || self->m_redraw ||
      self->m_page != Page::List || self->m_job ||
      lv_event_get_target_obj(e) != self->m_listViewport)
    return;
  // A drag must never activate the row that is recycled under the finger.
  self->m_pressedListEntry = vault::interaction::ListWindow::None;
  self->listOffset() = self->m_listIndex.clamp(
      lv_obj_get_scroll_y(self->m_listViewport),
      lv_obj_get_height(self->m_listViewport), self->listPitch());
  self->updateListRows();
}
void PasswordVault::updateListRows() {
  if (!m_listViewport || !m_session || m_listUpdating)
    return;
  using W = vault::interaction::ListWindow;
  m_listUpdating = true;
  const size_t first = m_listIndex.first(lv_obj_get_scroll_y(m_listViewport),
                                        lv_obj_get_height(m_listViewport), listPitch());
  for (size_t i = first; i < first + m_listIndex.slots(); ++i) {
    auto &row = m_listRows[W::slot(i)];
    const size_t index = m_listIndex.entry(i);
    if (row.entry == index)
      continue;
    const auto &entry = m_session->entries[index];
    row.entry = index;
    const auto action = targetGroups() ? Action::TargetGroup :
        purposePage() ? Action::PurposePassword : webAccounts() ? Action::Account : Action::Detail;
    lv_obj_set_user_data(row.input ? row.input : row.button, reinterpret_cast<void *>(
        (static_cast<uintptr_t>(action) << 16) | index));
    if (row.input)
      lv_obj_set_user_data(row.button, reinterpret_cast<void *>(
          (static_cast<uintptr_t>(Action::PurposeDetail) << 16) | index));
    // With modulo slots a one-row scroll rebinds only one off-screen row.
    if (targetGroups()) {
      char count[16];
      std::snprintf(count, sizeof(count), "%u  >",
                    unsigned(m_targetIndex.count(m_targetIndex.groupFor(index))));
      c::setText(row.title, targetName(index));
      c::setText(row.account, count);
    } else if (m_category == 2) {
      char subtitle[48];
      if (webAccounts()) {
        std::snprintf(subtitle, sizeof(subtitle), "%u  >",
                      unsigned(m_webIndex.count(m_webIndex.groupFor(index))));
        c::setText(row.title, entry.username.empty() ? "未填写账号" : entry.username.c_str());
      } else {
        c::setText(row.title, entry.target.empty() ? entry.title.c_str() : entry.target.c_str());
      }
      c::setText(row.account, webAccounts() ? subtitle :
          m_access.visible(m_webAccount) ? entry.password.c_str() : "********");
    } else {
      c::setText(row.title, entry.title.c_str());
      c::setText(row.account, entry.category == "other" ? entry.password.c_str() : entry.username.c_str());
    }
    lv_obj_set_y(row.button, int(i) * listPitch());
  }
  m_listUpdating = false;
}
void PasswordVault::styleCategory(lv_obj_t *row,lv_obj_t *name,lv_obj_t *detail,size_t index) {
  if (ui::theme::focus()) {
    // Four existing category actions, not decrypted entries or preloaded apps.
    // LVGL scrolling cancels click dispatch and snaps one category at a time.
    // The new row is the final body child. The preceding child is the
    // viewport made on the first iteration, even when a message precedes it.
    auto viewport=index?lv_obj_get_child(m_body,-2):c::panel(m_body,240,160,true);
    if(index==0) {
      lv_obj_set_scroll_dir(viewport,LV_DIR_HOR);
      lv_obj_set_scroll_snap_x(viewport,LV_SCROLL_SNAP_CENTER);
      lv_obj_set_scroll_one(viewport,true);
      lv_obj_set_gesture_bubble(viewport,false);
      lv_obj_set_scroll_chain_hor(viewport,false);
      lv_obj_set_scrollbar_mode(viewport,LV_SCROLLBAR_MODE_OFF);
      lv_obj_set_flex_flow(viewport,LV_FLEX_FLOW_ROW);
      lv_obj_set_style_pad_column(viewport,0,0);
    }
    lv_obj_set_parent(row,viewport);lv_obj_set_size(row,240,158);
    lv_obj_set_snappable(row,true);
    lv_obj_set_width(name,216);lv_obj_align(name,LV_ALIGN_TOP_LEFT,10,8);
    lv_obj_set_width(detail,154);lv_obj_align(detail,LV_ALIGN_TOP_LEFT,10,46);
    lv_obj_set_style_text_align(detail,LV_TEXT_ALIGN_LEFT,0);
    lv_obj_set_style_text_font(detail,&lv_font_montserrat_48,0);
    lv_obj_set_style_text_color(detail,ui::theme::accent(),0);
    auto icon=c::label(row,LV_SYMBOL_EYE_CLOSE);
    lv_obj_set_style_text_font(icon,&lv_font_montserrat_32,0);lv_obj_set_pos(icon,185,57);
    auto hint=c::label(row,"左右滑动选择分类",216);lv_obj_set_pos(hint,10,124);
    lv_obj_set_style_text_color(hint,ui::theme::muted(),0);
    if(index==3)lv_obj_scroll_to_x(viewport,int(m_category)*240,LV_ANIM_OFF);
  } else if (ui::theme::tiles()) {
    lv_obj_set_flex_flow(m_body,LV_FLEX_FLOW_ROW_WRAP);
    // 44px toolbar + 79px/78px category rows + two 1px gaps = 203px body.
    lv_obj_set_size(row,119,index < 2 ? 79 : 78);
    lv_obj_set_width(name,107);lv_obj_align(name,LV_ALIGN_TOP_LEFT,5,5);
    lv_obj_align(detail,LV_ALIGN_BOTTOM_RIGHT,-5,-5);
    lv_obj_set_style_text_font(detail,&lv_font_montserrat_24,0);
    lv_obj_set_style_text_color(detail,ui::theme::accent(),0);
  } else if (ui::theme::terminal()) {
    const auto title=std::to_string(index+1)+". "+categoryNames[index];
    c::setText(name,title.c_str());
  }
}
void PasswordVault::drawList() {
  using W = vault::interaction::ListWindow;
  static_assert(W::Pitch == W::RowHeight + c::kGap);
  static_assert(W::ViewHeight == vault::interaction::PinLayout::NormalBodyHeight);
  // The outer body can contain a message. Only the remaining list area scrolls.
  lv_obj_set_scrollable(m_body,false);
  using P = vault::interaction::PurposeLayout;
  if (purposePage()) {
    const auto &entry = m_session->entries[m_webIndex.entry(m_webAccount)];
    auto name = c::label(m_body, ("名称：" + entry.title).c_str(), 240);
    lv_obj_set_height(name, P::NameHeight);
    auto account = c::panel(m_body, 240, P::InputHeight);
    auto text = c::label(account, ("账号：" + entry.username).c_str(), P::TextWidth);
    lv_obj_set_height(text, P::NameHeight);
    lv_obj_align(text, LV_ALIGN_LEFT_MID, 0, 0);
    auto input = button(account, "输入", Action::PurposeUser, 0, P::InputWidth);
    lv_obj_set_height(input, P::InputHeight);
    lv_obj_align(input, LV_ALIGN_RIGHT_MID, 0, 0);
    c::disabled(input, entry.username.empty());
  }
  m_listViewport = c::panel(m_body, 240, 0, true);
  lv_obj_set_flex_grow(m_listViewport, 1);
  auto content = c::panel(m_listViewport, 240, m_listIndex.height(listPitch()));
  lv_obj_set_clickable(content,false);
  for (size_t i = 0; i < m_listIndex.slots(); ++i) {
    auto &row = m_listRows[i];
    if (purposePage()) {
      row.button = c::panel(content, 240, P::RowHeight);
      lv_obj_set_clickable(row.button,true);
      lv_obj_add_event_cb(row.button, clicked, LV_EVENT_CLICKED, this);
      lv_obj_add_event_cb(row.button, listPressed, LV_EVENT_PRESSED, this);
      row.title = c::label(row.button, "", 240);
      lv_obj_set_height(row.title, P::NameHeight);
      lv_obj_set_style_text_color(row.title, ui::theme::muted(), 0);
      row.account = c::label(row.button, "", P::TextWidth);
      lv_obj_set_style_text_font(row.account, &lv_font_montserrat_14, 0);
      lv_obj_set_height(row.account, 16);
      lv_obj_set_pos(row.account, 0, P::NameHeight + 1 + (P::InputHeight - 16) / 2);
      row.input = button(row.button, "输入", Action::PurposePassword, 0, P::InputWidth);
      lv_obj_set_height(row.input, P::InputHeight);
      lv_obj_align(row.input, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
      lv_obj_set_scroll_on_focus(row.input,false);
      lv_obj_add_event_cb(row.input, listPressed, LV_EVENT_PRESSED, this);
      continue;
    }
    row.button = button(content, "", Action::Detail);
    lv_obj_set_height(row.button, W::RowHeight);
    // Recycled rows must not move the viewport through implicit focus scrolling.
    lv_obj_set_scroll_on_focus(row.button,false);
    lv_obj_add_event_cb(row.button, listPressed, LV_EVENT_PRESSED, this);
    row.title = c::label(row.button, "", 229);
    lv_obj_align(row.title, LV_ALIGN_TOP_LEFT, 4, 0);
    row.account = c::label(row.button, "", 229);
    lv_obj_set_style_text_font(row.account, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(row.account, ui::theme::muted(), 0);
    lv_obj_align(row.account, LV_ALIGN_BOTTOM_LEFT, 4, -1);
    if (webAccounts() || targetGroups()) {
      lv_obj_set_size(row.title, 172, 21);
      lv_obj_align(row.title, LV_ALIGN_LEFT_MID, 4, 0);
      lv_obj_set_size(row.account, 52, 16);
      lv_obj_set_style_text_font(row.account, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_align(row.account, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_align(row.account, LV_ALIGN_RIGHT_MID, -4, 0);
    }
  }
  lv_obj_update_layout(m_body);
  const int offset = m_listIndex.clamp(listOffset(),
                                       lv_obj_get_height(m_listViewport), listPitch());
  lv_obj_scroll_to_y(m_listViewport, offset, LV_ANIM_OFF);
  listOffset() = offset;
  updateListRows();
  lv_obj_add_event_cb(m_listViewport, listScrolled, LV_EVENT_SCROLL, this);
  lv_obj_add_event_cb(m_listViewport, listScrolled, LV_EVENT_SCROLL_BEGIN, this);
}
void PasswordVault::draw() {
  m_redraw = false;
  wipeField();
  m_body = nullptr;
  m_status = nullptr;
  m_peerName = nullptr;
  m_peerAddress = nullptr;
  m_pattern.detach();
  m_patternPending = false;
  m_bleButton = m_pairButton = m_disconnectButton = nullptr;
  m_bondButtons.clear();
  clearListWidgets();
  lv_obj_clean(root());
  lv_obj_set_style_pad_all(root(), 0, 0);
  lv_obj_set_flex_flow(root(), LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(root(), 1, 0);
  if (m_page == Page::Qr && m_session && m_selected < m_session->entries.size()) {
    m_body = c::panel(root(), 240, 240);
    lv_obj_add_flag(m_body, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(m_body, clicked, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(m_body, reinterpret_cast<void *>((static_cast<uintptr_t>(Action::Back) << 16)));
    auto text = vault::plainText(m_session->entries[m_selected]);
    if (!ui::vaultQr(m_body, text)) {
      wrap(m_body, "二维码生成失败，请通过局域网查看完整信息");
      button(m_body, "返回", Action::Back);
    }
    vault::clear(text);
    return;
  }
  using L = vault::interaction::PinLayout;
  const bool pinPage = m_page == Page::Locked || m_page == Page::VerifyReveal || m_page == Page::VerifyAccess || changingPin();
  if (!pinPage) {
    auto header = c::panel(root(), L::Width, L::HeaderHeight);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    auto back = button(header, LV_SYMBOL_LEFT, Action::Back, 0, 36);
    lv_obj_set_height(back, L::HeaderHeight);
    auto title = c::label(header, purposePage() && !m_message.empty() ? m_message.c_str() : m_page == Page::List ? listTitle()
                                                      : m_page == Page::Detail && m_session && m_selected < m_session->entries.size() ? m_session->entries[m_selected].title.c_str()
                                                      : "密码本", purposePage() ? 97 : m_page == Page::Categories ? 130 : 203);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(title, 8, 0);
    lv_obj_set_height(title, L::HeaderHeight);
    if (m_session && m_page == Page::Categories) {
      auto changeButton = button(header, "修改密码", Action::ChangePassword, 0, 72);
      lv_obj_set_height(changeButton, L::HeaderHeight);
    } else if (purposePage()) {
      auto eye = button(header, m_access.visible(m_webAccount) ? LV_SYMBOL_EYE_OPEN : LV_SYMBOL_EYE_CLOSE,
                        Action::PageReveal, 0, 40);
      lv_obj_set_height(eye, L::HeaderHeight);
      if (m_access.visible(m_webAccount))
        c::buttonBackground(eye, ui::theme::accent());
      c::disabled(eye, m_access.super());
      auto enter = button(header, "回车", Action::Enter, 0, 64);
      lv_obj_set_height(enter, L::HeaderHeight);
    }
  }
  m_body = c::panel(root(), L::Width,
                    pinPage ? L::BodyHeight : L::NormalBodyHeight, !pinPage);
  lv_obj_set_flex_flow(m_body, LV_FLEX_FLOW_COLUMN);
  if (m_job) {
    if (pinPage) { drawPin(Action::Unlock); return; }
    m_status = c::label(m_body, "正在处理…", 238);
    lv_label_set_long_mode(m_status, LV_LABEL_LONG_MODE_WRAP);
    if (m_job->operation != vault::Operation::ChangePin)
      button(m_body, "取消", Action::Cancel);
    return;
  }
  if (!m_message.empty() && !pinPage && !purposePage() && m_page != Page::Detail)
    wrap(m_body, m_message.c_str());
  switch (m_page) {
  case Page::Locked:
    if (m_load.ready() && (m_exists || m_blank)) {
      drawPin(Action::Unlock);
    } else {
      if (m_load.ready())
        wrap(m_body, "密码区校验失败，原数据已保留。请重新读取。");
      else
        wrap(m_body, m_message.empty() ? "正在读取密码区" : m_message.c_str());
      if (m_load.ready() || m_load.phase() == vault::LoadState::Phase::Failed)
        button(m_body, "重新读取", Action::RetryLoad);
    }
    break;
  case Page::ChangeOld:
  case Page::ChangeNew:
  case Page::ChangeConfirm:
    if (m_session)
      drawPin(Action::SubmitChange);
    else
      lock();
    break;
  case Page::VerifyReveal:
    if (m_session && m_selected < m_session->entries.size())
      drawPin(Action::VerifyReveal);
    else
      lock();
    break;
  case Page::VerifyAccess:
    if (m_session)
      drawPin(Action::VerifyAccess);
    else
      lock();
    break;
  case Page::Categories: {
    if (!m_session) {
      m_page = Page::Locked;
      m_redraw = true;
      break;
    }
    const int toolbarHeight = ui::theme::tiles() ? 44 : 36;
    auto toolbar = c::panel(m_body, 240, toolbarHeight);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_height(button(toolbar, "同步", Action::Sync, 0, 79), toolbarHeight);
    lv_obj_set_height(button(toolbar, "连接", Action::Network, 0, 79), toolbarHeight);
    auto super = button(toolbar, "超管", Action::Super, 0, 80);
    lv_obj_set_height(super, toolbarHeight);
    if (m_access.super())
      c::buttonBackground(super, ui::theme::accent());
    for (size_t k = 0; k < 4; ++k) {
      auto row = button(m_body, "", Action::Category, k);
      const size_t count = m_categoryCounts[k];
      auto name = c::label(row, categoryNames[k], 170);
      lv_obj_align(name, LV_ALIGN_LEFT_MID, 5, 0);
      auto detail = c::label(row, (std::to_string(count) + "  >").c_str(), 55);
      lv_obj_set_style_text_font(detail, &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(detail, ui::theme::muted(), 0);
      lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_align(detail, LV_ALIGN_RIGHT_MID, -5, 0);
      styleCategory(row, name, detail, k);
    }
    break;
  }
  case Page::List: {
    if (!m_session) {
      m_page = Page::Locked;
      m_redraw = true;
      break;
    }
    if (m_listIndex.count())
      drawList();
    else
      wrap(m_body, "暂无密码，请在服务端添加后同步");
    break;
  }
  case Page::Address:
    wrap(m_body, "密码服务地址");
    m_field = c::networkField(m_body, "https://example.com",
                              m_session ? m_session->url.c_str() : "", 240,
                              &lv_font_montserrat_14);
    button(m_body, "保存地址", Action::SaveAddress);
    wrap(m_body, "支持公网域名或局域网IP，标准端口可省略。");
    break;
  case Page::Detail:
    if (!m_session || m_selected >= m_session->entries.size()) {
      m_page = Page::Locked;
      m_redraw = true;
      break;
    }
    {
      const auto &entry = m_session->entries[m_selected];
      if (entry.category == "other") {
        auto actions = c::panel(m_body, 240, 40);
        lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
        button(actions, entry.password.c_str(), Action::Password, 0, 177);
        button(actions, "回车", Action::Enter, 0, 62);
        button(m_body, "二维码", Action::Qr);
      } else {
        using I = vault::interaction::InputLayout;
        auto actions = c::panel(m_body, I::Width, I::Height);
        lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
        auto inputs = c::panel(actions, I::TextWidth, I::Height);
        lv_obj_set_flex_flow(inputs, LV_FLEX_FLOW_COLUMN);
        auto account = button(inputs, entry.username.empty() ? "未设置账号" : entry.username.c_str(), Action::User, 0, I::TextWidth);
        lv_obj_set_height(account, I::RowHeight);
        c::disabled(account, entry.username.empty());
        auto password = button(inputs, (m_access.super() || m_reveal.visible(m_selected)) ? entry.password.c_str() : "********", Action::Password, 0, I::TextWidth);
        lv_obj_set_height(password, I::RowHeight);
        auto enter = button(actions, "回车", Action::Enter, 0, I::EnterWidth);
        lv_obj_set_height(enter, I::Height);
        auto controls = c::panel(m_body, 240, 40);
        lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
        auto reveal = button(controls, m_reveal.visible(m_selected) ? "隐藏密码" : "展示密码", Action::Reveal, 0, 119);
        c::disabled(reveal, m_access.super());
        button(controls, "二维码", Action::Qr, 0, 120);
      }
      wrap(m_body, (std::string("分类：") + vault::categoryLabel(entry.category)).c_str());
      if (!entry.target.empty())
        wrap(m_body, (std::string(vault::targetLabel(entry.category)) + "：" + entry.target).c_str());
      if (!entry.note.empty())
        wrap(m_body, ("备注：" + entry.note).c_str());
      if (!m_message.empty())
        wrap(m_body, m_message.c_str());
      auto hint = c::label(m_body, "先选输入框，US英文、关大写", 238);
      lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
      lv_obj_set_style_text_color(hint, ui::theme::muted(), 0);
    }
    break;
  case Page::Qr:
    lock();
    break;
  case Page::Network:
    m_status = c::label(m_body, "", 238);
    lv_label_set_long_mode(m_status, LV_LABEL_LONG_MODE_WRAP);
    m_peerName = c::label(m_body, "", 238);
    m_peerAddress = c::label(m_body, "", 238);
    lv_obj_set_style_text_font(m_peerAddress, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(m_peerAddress, ui::theme::muted(), 0);
    m_lastNetwork.clear();
    {
      auto bar = c::panel(m_body, 240, 40);
      lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
      button(bar, "Wi-Fi开关", Action::Wifi, 0, 119);
      m_bleButton = button(bar, "蓝牙开关", Action::Ble, 0, 120);
    }
    m_pairButton = button(m_body, "配对新设备", Action::SecurePair);
    if (m_session)
      button(m_body, "服务地址", Action::Address);
    m_disconnectButton = button(m_body, "断开蓝牙", Action::Disconnect);
    wrap(m_body, "已保存Wi-Fi（先打开开关）");
    m_wifi.clear();
    for (auto &wifi : sys::Settings::wifiHistory()) {
      size_t i = m_wifi.size();
      m_wifi.push_back(wifi.ssid);
      button(m_body, wifi.ssid.c_str(), Action::WifiSaved, i);
      vault::clear(wifi.password);
    }
    wrap(m_body, "已配对设备（点击连接）");
    m_bonds.clear();
    {
      const auto state = sys::SystemService::bluetooth();
      m_bondRevision = state.bondRevision;
      for (int i = 0; i < state.bondCount; ++i) {
        if (state.bonds[i].keyboard)
          continue;
        size_t index = m_bonds.size();
        m_bonds.push_back(state.bonds[i]);
        auto row = button(m_body, "", Action::Bond, index);
        m_bondButtons.push_back(row);
        lv_obj_set_height(row, 52);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        peerLabel(row, state.bonds[i]);
      }
    }
    button(m_body, "更多网络设置", Action::Settings);
    wrap(m_body, "开机尝试最近连接；手动断开蓝牙后不再自动连接。");
    break;
  }
}
} // namespace apps
