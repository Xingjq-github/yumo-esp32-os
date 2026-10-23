#pragma once
#include "app.hpp"
#include "random_model.hpp"
#include "rps_match.hpp"
#include <array>
#include <string>

namespace apps {
class Randomizer : public ui::App {
public:
    const char *name() const override { return "Random"; }
    const char *icon() const override { return LV_SYMBOL_SHUFFLE; }
    void onCreate(lv_obj_t *parent) override;
    void onShow() override;
    void onHide() override;
    void onDestroy() override;
    bool onBack() override;
private:
    enum class Mode { Number, Dice, Letters, Choices, Lottery, Coin, Outcome, RockPaperScissors };
    void buildMode();
    void buildChoices();
    void buildLottery();
    void buildPk();
    void startPk(unsigned sides);
    void tickPk();
    void paintPk();
    void start();
    void tick();
    void finish();
    void cancelAnimation();
    void lockControls(bool locked);
    void paintCell(unsigned index,bool selected,bool final=false);
    void showNumber(int value);
    unsigned choiceCount() const;
    static void modeCb(lv_event_t *event);
    static void countCb(lv_event_t *event);

    Mode m_mode=Mode::Number;
    bool m_visible=false,m_running=false;
    bool m_pkMode=false;
    rps::Match m_match{};
    unsigned m_pkStep[2]={},m_pkTotal[2]={};
    uint32_t m_pkDue[2]={};
    int m_pkCursor[2]={-1,-1};
    lv_obj_t *m_pkButton=nullptr,*m_pkResult=nullptr,*m_pkScore[2]={};
    lv_obj_t *m_pkCells[2][3]={};
    unsigned m_letters=4,m_choices=4,m_step=0,m_totalSteps=0;
    int m_low=1,m_high=100,m_result=0,m_cursor=-1;
    std::string m_lowText="1",m_highText="100";
    random_model::Lottery m_lottery{};
    uint32_t m_screenOffSub=0;
    uint32_t m_keySession=0,m_previousSub=0,m_nextSub=0;
    lv_timer_t *m_timer=nullptr;
    lv_obj_t *m_modeSelect=nullptr,*m_body=nullptr,*m_board=nullptr,*m_start=nullptr;
    lv_obj_t *m_countSelect=nullptr,*m_lowField=nullptr,*m_highField=nullptr;
    lv_obj_t *m_number=nullptr,*m_hint=nullptr;
    std::array<lv_obj_t *,random_model::MaxChoices> m_cells{};
    std::array<lv_obj_t *,7> m_ballLabels{};
};
} // namespace apps
