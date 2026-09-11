#pragma once
// ============================================================
//  DashView.h — 仪表盘（No.03）
//  移植 WebApp vDash() 中可本地运行的部分：
//   - 六张统计卡：总学习时长 / 专注总时长 / 今日专注 /
//     平均完成率 / 最佳单日 / 当月满勤
//   - 最近 14 天表现条形
//   - GitHub 风格年度热力图（近 53 周，列主序，悬停看当日）
//   - 月度热力图（可翻月，悬停看当日）
//   - 专注时长趋势点线图（近 14 天，含动画揭示）
//   - 按科目累计时长（回应 Stub 的「表现曲线支持按科目拆分」）
//   - 作息环形图（近 30 天）：24 小时刻度盘，读 rhythm.json 的起床/就寝时刻
//   - 历史小结（每日复盘）：读 journal.json，并内置复盘编辑弹层（Win32 EDIT）
//  数据全部来自 CheckinStore（checkin.json / focus.json / rhythm.json / journal.json）。
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "app/Data.h"
#include "app/Store.h"
#include "core/Hwnd.h"
#include <windows.h>

namespace lj {

class DashView : public View
{
public:
    const wchar_t* Id() const override { return L"dash"; }
    const wchar_t* Title() const override { return L"仪 表 盘"; }

    void OnEnter() override;
    void OnLeave() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    struct CellData
    {
        std::wstring date;
        int pct = 0;        // 完成率
        int done = 0;       // 完成项
        int total = 0;      // 总项
        bool future = false;
        bool empty = false; // 日历补位空白格
    };

    // ---------------- 数据 ----------------
    void ComputeStats();
    void BuildYearData();
    void BuildMonthData();
    void BuildWeekReport();          // #41 F1 智能周报：近 7 天聚合 + 薄弱项
    void WriteWeakToJournal();       // #41 薄弱项 → 今日复盘「明日三要事」
    static int  WebDay(int z);                 // 0=周日..6=周六
    static int  DaysInMonth(int y, int m);
    static void AddMonth(Date& d, int delta);
    int  LvlOf(int pct) const;
    D2D1_COLOR_F LevelColor(const Palette& pal, int lvl) const;

    // ---------------- 绘制 ----------------
    void PaintTitle(Canvas& cv, float x0, float y, float w);
    void PaintStatCards(Canvas& cv, float x0, float y, float w);
    void PaintRecentBars(Canvas& cv, float x0, float y, float w);
    void PaintYearHeat(Canvas& cv, float x0, float y, float w);
    void PaintLowerColumns(Canvas& cv, float x0, float y, float w);
    void PaintRhythmJournal(Canvas& cv, float x0, float y, float w);
    void PaintWeekReport(Canvas& cv, float x0, float y, float w);   // #41
    void PaintTooltip(Canvas& cv);
    void DebugForceOpen() override;

    // ---------------- 作息 / 复盘 ----------------
    void BuildRhythm();          // 汇总近 30 天起床 / 就寝 / 睡眠时长
    void PaintJournalOverlay(Canvas& cv);
    void ComputeJournalRects();
    void OpenJournal();
    void CloseJournal(bool save);
    void EnsureJournalEditors();
    void DestroyJournalEditors();
    static std::wstring HHMM(int minuteOfDay);

    // ---------------- 统计结果 ----------------
    long long m_totalMin = 0;
    int m_trackedDays = 0, m_avg = 0, m_best = 0, m_perfectMonth = 0;
    int m_focusTotalMin = 0, m_focusCount = 0, m_todayFocusMin = 0;

    ChecklistBundle m_bundle;     // 实时打卡项（与打卡页同源，来自用户自定义的 items.json）

    std::vector<int>         m_recentPct;
    std::vector<std::wstring> m_recentLbl;
    std::vector<int>         m_focus14;
    std::vector<std::wstring> m_focusLbl;
    int m_focusMax = 60;
    std::vector<std::pair<std::wstring, int>> m_subject;
    int m_subjectMax = 1;

    // ---------------- 热力图数据（逻辑）----------------
    std::vector<CellData> m_yearCells;
    std::vector<D2D1_RECT_F> m_yearRects;
    std::vector<std::pair<float, std::wstring>> m_yearMonthLabels;
    std::vector<CellData> m_monthCells;
    std::vector<D2D1_RECT_F> m_monthRects;
    Date m_dispMonth{ 2026, 8, 1 };
    static const Date kHeatMin;                 // 数据起点 2026-07

    // ---------------- 布局锚点 / 缓存 ----------------
    float m_contentTop = 0.0f;
    float m_cardY = 0.0f;
    float m_barY = 0.0f;
    float m_yearY = 0.0f;
    float m_lowerY = 0.0f;
    float m_phY = 0.0f;
    float m_weekY = 0.0f;                    // #41 周报卡顶部
    std::vector<D2D1_RECT_F> m_cardRects;       // 6 张卡
    std::vector<D2D1_RECT_F> m_barRects;         // 14 根条
    float m_focusChartX = 0, m_focusChartY = 0, m_focusChartW = 0, m_focusChartH = 0;
    float m_subjY = 0.0f;
    std::vector<D2D1_RECT_F> m_subjRects;        // 6 根科目条
    D2D1_RECT_F m_monthCard{}, m_focusCard{}, m_subjCard{};

    // ---------------- 周报（#41 F1 智能周报）----------------
    struct WeekItem { std::wstring tag; int pct = 0; int n = 0; };
    std::vector<WeekItem> m_weekTags;        // 按 tag 近 7 天完成率（降序）
    int m_weekAvg = 0;                       // 近 7 天平均完成率
    int m_weekDays = 0;                      // 有记录天数
    int m_weekFocusMin = 0, m_weekFocusCnt = 0;
    int m_weekJournal = 0;                   // 近 7 天有复盘天数
    D2D1_RECT_F m_weekCard{};
    Button m_weekBtn;                        // 「写入明日三要事」
    bool m_weekWrote = false;                // 已写入提示闪烁
    float m_weekWriteT = 0.0f;

    // ---------------- 悬停 ----------------
    bool  m_hasHover = false;
    CellData m_hover;
    float m_hoverX = 0, m_hoverY = 0;

    // ---------------- 作息（rhythm.json）----------------
    int m_rhWakeAvg = -1;        // 近 30 天平均起床（分钟数）
    int m_rhSleepAvg = -1;       // 近 30 天平均就寝
    int m_rhDurAvg = -1;         // 近 30 天平均睡眠时长（分钟）
    int m_rhDays = 0;            // 有记录的天数
    DayRhythm m_rhToday{};

    // ---------------- 复盘（journal.json）----------------
    std::vector<std::pair<std::wstring, DayJournal>> m_journals;   // 日期升序
    D2D1_RECT_F m_jrEntry{};     // 「写今日复盘」按钮（内容坐标，含滚动）
    bool  m_jrOpen = false;
    DayJournal m_jrDraft;
    D2D1_RECT_F m_jrCard{}, m_jrBox1{}, m_jrBox2{}, m_jrSave{}, m_jrCancel{};
    HWND  m_ed1 = nullptr, m_ed2 = nullptr;
    HFONT m_edFont = nullptr;
    Smooth m_jrA{ 0.0f, 0.18f };

    // ---------------- 控件 ----------------
    Button m_prevBtn, m_nextBtn, m_backBtn;
    std::vector<Widget*> m_widgets;

    float m_t = 0.0f;
};

} // namespace lj
