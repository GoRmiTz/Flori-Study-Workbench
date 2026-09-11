#pragma once
// ============================================================
//  HomeView.h — 首页（仪表盘式）
//  倒计时四卡 + 今日完成进度环 + 模块入口 + 执行纪律
// ============================================================
#include "ui/View.h"
#include "app/Data.h"

namespace lj {

class HomeView : public View
{
public:
    const wchar_t* Id() const override { return L"home"; }
    const wchar_t* Title() const override { return L"档案首页"; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    void PaintHeader(Canvas& cv, float x, float y, float w);
    void PaintTodayPanel(Canvas& cv, const D2D1_RECT_F& r);
    void PaintDiscipline(Canvas& cv, float x, float y, float w);

    std::vector<CountdownCard> m_countdowns;
    std::vector<ModuleCard>    m_cards;
    std::vector<Widget*>       m_widgets;
    D2D1_RECT_F m_btnCountdown{};   // 「管理倒计时」入口（其他账户自定义倒计时）
    bool  m_countdownHot = false;

    // S2-5 空态引导：新注册账户 items.json 为空，今日速览会是一片「0 / 0 项」，
    // 看不出下一步该干嘛。空态时改画引导文案 + 这两个入口。
    D2D1_RECT_F m_btnEmptyPlan{};   // → plan：套用现成备考方案（推荐）
    D2D1_RECT_F m_btnEmptyItems{};  // → manage：自己定打卡项
    bool  m_emptyPlanHot = false, m_emptyItemsHot = false;
    bool  IsTodayEmpty() const { return m_todayItems.empty(); }

    Tween m_ring;              // 进度环填充
    float m_t = 0.0f;
    float m_todayRatio = 0.0f;
    int   m_doneCount = 0, m_totalCount = 0;

    float m_contentTop = 0.0f;
    float m_panelY = 0.0f, m_panelH = 0.0f;
    float m_disciplineY = 0.0f;
    bool  m_built = false;

    // 今日速览：实时打卡项（与打卡页 CheckinView 同源，来自用户自定义的 items.json）
    std::vector<CheckItem> m_todayItems;
    std::vector<int>       m_todayDone;     // 对应完成标记（0/1）
    void RefreshToday();                     // 从 CheckinStore 取当日打卡项 + 当日完成态
};

} // namespace lj
