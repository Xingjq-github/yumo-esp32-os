#pragma once
#include "app.hpp"
#include "core/speed_test.hpp"

namespace apps {
class SpeedTest : public ui::App {
public:
    const char *name()const override{return "网速测速";}
    const char *icon()const override{return LV_SYMBOL_UPLOAD;}
    void onCreate(lv_obj_t *parent)override;
    void onShow()override;
    void onHide()override;
    void onDestroy()override;
    bool onBack()override;
private:
    enum class Page { Main, History, Detail, Method };
    static void tick(lv_timer_t *timer);
    static void action(lv_event_t *event);
    static void historyRow(lv_event_t *event);
    void askDelete();
    void closeDelete();
    void showMain();
    void showHistory(const std::string &before={});
    void showDetail(const core::speed::Record &record,bool fromHistory,const std::string &filename={});
    void showMethod();
    void renderHistory();
    void refresh();
    void refreshGraph(bool busy);
    lv_obj_t *scrollPage(const char *title);
    lv_obj_t *paragraph(lv_obj_t *parent,const char *text,bool muted=false);
    Page page=Page::Main;
    bool detailFromHistory=false;
    lv_timer_t *timer=nullptr;
    lv_obj_t *mainPage=nullptr,*secondary=nullptr,*heading=nullptr,*historyButton=nullptr;
    lv_obj_t *wifi=nullptr,*download=nullptr,*upload=nullptr,*latency=nullptr,*state=nullptr;
    lv_obj_t *startButton=nullptr,*detailButton=nullptr,*methodButton=nullptr,*historyList=nullptr,*latestButton=nullptr,*olderButton=nullptr;
    lv_obj_t *graph=nullptr,*graphCaption=nullptr,*graphScale=nullptr,*emptyGraph=nullptr;
    lv_obj_t *deleteButton=nullptr,*deleteStatus=nullptr,*deleteDialog=nullptr;
    lv_chart_series_t *downSeries=nullptr,*upSeries=nullptr;
    std::shared_ptr<core::speed::Job> job;
    std::shared_ptr<core::speed::HistoryJob> historyJob;
    std::shared_ptr<core::speed::DeleteJob> deleting;
    core::speed::store::Entry detailEntry;
    uint64_t historyGeneration=0;
    core::speed::store::Page records;
    core::speed::Snapshot last;
    std::string notice;
};
} // namespace apps
