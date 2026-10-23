#pragma once
#include "core/audio_recorder.hpp"
#include "core/storage_browser.hpp"
#include "ui/app.hpp"
namespace apps {
class Recorder final : public ui::App {
public:
  const char *name() const override { return "录音机"; }
  const char *icon() const override { return LV_SYMBOL_AUDIO; }
  void onCreate(lv_obj_t *) override;
  void onHide() override;
  void onDestroy() override;
  bool onBack() override;
private:
  void poll();
  void refresh();
  void play(size_t index);
  void askDelete(size_t index);
  void closeDelete();
  void updateMeter();
  void rebuildList();
  void clearTransportPress();
  static void tick(lv_timer_t *);
  static void click(lv_event_t *);
  static void seek(lv_event_t *);
  uint32_t noticeUntil=0;
  bool recordAfterPlayback=false;
  lv_timer_t *timer=nullptr;
  lv_obj_t *root=nullptr,*back=nullptr,*refreshButton=nullptr,*elapsed=nullptr,
           *status=nullptr,*meter=nullptr,*record=nullptr,*stop=nullptr,
           *rewind=nullptr,*pause=nullptr,*forward=nullptr,*list=nullptr,*dialog=nullptr,
           *recordDot=nullptr,*recordPause=nullptr;
  std::string deletePath,playPath,pendingDeletePath;
  core::FileIdentity deleteIdentity,pendingDeleteIdentity;
  uint64_t filesGeneration=0,pendingDeleteGeneration=0;
  std::shared_ptr<core::recorder::Job> job;
  std::shared_ptr<core::StorageJob> listing,deleting;
  std::vector<core::FileEntry> files;
};
} // namespace apps
