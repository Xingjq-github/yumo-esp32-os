#pragma once
#include "app.hpp"
#include "core/password_vault.hpp"
#include "core/vault_interaction.hpp"
#include "core/vault_list.hpp"
#include "core/vault_web_index.hpp"
#include "core/system_service.hpp"
#include "ui/vault_pattern.hpp"
#include <memory>
#include <string>
#include <vector>
namespace apps {
class PasswordVault : public ui::App {
public:
  const char *name() const override { return "密码本"; }
  const char *icon() const override { return LV_SYMBOL_EYE_CLOSE; }
  void onCreate(lv_obj_t *parent) override;
  void onShow() override;
  void onHide() override;
  void onDestroy() override;
  bool onBack() override;
  bool allowsQuickPanel() const override { return !patternPage(); }
  bool locksScreenOrientation() const override { return patternPage(); }
  bool allowsScreenProtection() const override {
    return m_visible && patternPage() && !m_job && !m_patternPending && !m_pattern.drawing();
  }

private:
  enum class Page {
    Locked,
    Categories,
    List,
    Detail,
    Qr,
    VerifyReveal,
    VerifyAccess,
    ChangeOld,
    ChangeNew,
    ChangeConfirm,
    Network,
    Address
  };
  enum class Action {
    Back,
    Category,
    Address,
    SaveAddress,
    Reveal,
    Qr,
    VerifyReveal,
    Unlock,
    Sync,
    ChangePassword,
    SubmitChange,
    Network,
    Wifi,
    Ble,
    SecurePair,
    WifiSaved,
    Bond,
    Disconnect,
    Detail,
    User,
    Password,
    Enter,
    Cancel,
    Settings,
    RetryLoad,
    Account,
    Super,
    PageReveal,
    VerifyAccess,
    PurposeUser,
    PurposePassword,
    PurposeDetail,
    TargetGroup
  };
  static void timer(lv_timer_t *t);
  static void clicked(lv_event_t *e);
  static void listScrolled(lv_event_t *e);
  static void listPressed(lv_event_t *e);
  void resetListIndex();
  void selectList();
  void drawList();
  void updateListRows();
  void clearListWidgets();
  bool webAccounts() const;
  bool targetCategory() const;
  bool targetGroups() const;
  const char *targetName(size_t entry) const;
  bool purposePage() const;
  int listPitch() const;
  void finishAccess(bool verified, const char *text);
  const char *listTitle() const;
  int32_t &listOffset();
  void action(Action a, size_t index);
  void draw();
  bool m_qrPending = false;
  uint32_t m_qrSince = 0;
  void styleCategory(lv_obj_t *row,lv_obj_t *name,lv_obj_t *detail,size_t index);
  void drawPin(Action submit);
  bool patternPage() const;
  const char *patternPrompt() const;
  static void patternSubmitted(void *context, const char *value, size_t size);
  void startTyping(bool username, uintptr_t link, bool enter = false);
  void requestBluetooth(Action action, size_t index);
  void poll();
  void load();
  void lock();
  void wipeField();
  vault::StartResult launch(const std::shared_ptr<vault::Job> &job);
  lv_obj_t *button(lv_obj_t *parent, const char *text, Action action,
                   size_t index = 0, int width = 240);
  void message(const char *text);
  void rejectReveal(const char *text);
  bool changingPin() const;
  void finishPinChange(const char *text);
  void submitPinChange();
  Page m_page = Page::Locked;
  Page m_typePage = Page::Detail;
  size_t m_accessScope = vault::interaction::AccessState::None;
  lv_obj_t *m_body = nullptr, *m_field = nullptr, *m_status = nullptr;
  lv_obj_t *m_peerName = nullptr, *m_peerAddress = nullptr;
  ui::VaultPattern m_pattern;
  Action m_patternAction = Action::Unlock;
  bool m_patternPending = false, m_needsPatternSetup = false;
  lv_obj_t *m_bleButton = nullptr, *m_pairButton = nullptr,
           *m_disconnectButton = nullptr;
  std::vector<lv_obj_t *> m_bondButtons;
  uint32_t m_bondRevision = UINT32_MAX, m_btRevision = 0;
  sys::ServiceAction m_btAction = sys::ServiceAction::BluetoothEnable;
  bool m_btPending = false;
  lv_timer_t *m_timer = nullptr;
  uint32_t m_screenOff = 0;
  vault::LoadState m_load;
  bool m_visible = false, m_redraw = true, m_exists = false,
       m_blank = false, m_pairStarted = false;
  size_t m_selected = 0, m_category = 0;
  struct ListRow {
    lv_obj_t *button = nullptr, *title = nullptr, *account = nullptr, *input = nullptr;
    size_t entry = vault::interaction::ListWindow::None;
  };
  vault::interaction::ListWindow m_listIndex;
  std::array<ListRow, vault::interaction::ListWindow::Slots> m_listRows{};
  std::array<size_t, 4> m_categoryCounts{};
  std::array<int32_t, 4> m_listOffsets{};
  vault::interaction::WebIndex m_webIndex;
  size_t m_webAccount = vault::interaction::WebIndex::None;
  std::array<int32_t, vault::MaxEntries> m_webOffsets{};
  vault::interaction::TargetIndex m_targetIndex;
  size_t m_targetGroup = vault::interaction::TargetIndex::None;
  // Keyed by the group's representative entry, not its category-local ordinal.
  std::array<int32_t, vault::MaxEntries> m_targetOffsets{};
  lv_obj_t *m_listViewport = nullptr;
  size_t m_pressedListEntry = vault::interaction::ListWindow::None;
  bool m_listUpdating = false;
  vault::interaction::RevealState m_reveal;
  vault::interaction::AccessState m_access;
  std::string m_message, m_lastNetwork, m_pin, m_firstPin, m_oldPin;
  std::vector<std::string> m_wifi;
  std::vector<sys::BleBond> m_bonds;
  std::shared_ptr<vault::Job> m_job;
  std::shared_ptr<vault::Session> m_session;
};
} // namespace apps
