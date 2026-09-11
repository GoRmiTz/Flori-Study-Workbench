#pragma once
// ============================================================
//  CheckinView.h — 每日打卡（首个真实内容视图）
//  移植 WebApp vCheckin()：日期头 + 进度条 + 可勾选任务行
//  + 近 7 天出勤率 + 三条执行纪律。
//  本版在网页能力上补齐三项未移植交互：
//   · 专注监控（±60min）：勾选带时段的非作息任务时，若该时段
//     前后 1 小时无专注记录，弹提醒（虚假打卡是对自己的不负责）。
//   · 任务详情卡：每行「详情」按钮弹出「做什么/怎么学/去哪学」。
//   · 自定义打卡项入口：跳转 #/manage。
//  勾选即记（当日落盘 archive/checkin.json；项模板见 items.json）。
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "app/Data.h"
#include "app/Store.h"

namespace lj {

// ---------------- 打卡项行：可勾选 + 详情入口 ----------------
class CheckRow : public Widget
{
public:
    std::wstring time;       // 时段
    std::wstring title;
    std::wstring standard;   // 完成标准
    std::wstring tag;        // 分类标签
    std::wstring dur;        // 时长
    bool  rest = false;      // 休息行（不计入统计）
    const uint8_t* pDone = nullptr;   // 指向视图的完成态

    D2D1_RECT_F detailRect{};  // 「详情」按钮命中区（Layout 赋值，Paint 同位置绘制）

    void Paint(Canvas& cv) override;
};

// ---------------- 专注提醒 / 成功 弹层 ----------------
struct AlertOverlay
{
    bool   active = false;
    int    mode = 0;          // 0=专注警告（双按钮） 1=成功（单按钮）
    std::wstring title;
    std::wstring line1;
    std::wstring line2;
    D2D1_RECT_F btnOk{};
    D2D1_RECT_F btnLog{};     // 仅 mode=0 用（记一段专注）
    D2D1_RECT_F card{};
};

// ---------------- 任务详情卡 ----------------
struct TaskCardOverlay
{
    bool   active = false;
    CheckItem item;
    D2D1_RECT_F btnLink{};
    D2D1_RECT_F btnFolder{};   // 本地资料夹（item.folder 非空时可见）
    D2D1_RECT_F btnClose{};
    D2D1_RECT_F card{};
};

// ---------------- 视图 ----------------
class CheckinView : public View
{
public:
    const wchar_t* Id() const override { return L"checkin"; }
    const wchar_t* Title() const override { return L"每日打卡"; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    void Recompute();
    void SaveToday();   // 把当前 m_done 写入本地档案
    void PaintProgress(Canvas& cv, float x, float y, float w);
    void PaintHistory(Canvas& cv, float x, float y, float w);
    void PaintDiscipline(Canvas& cv, float x, float y, float w);
    void PaintOverlay(Canvas& cv);
    void ComputeOverlayRects();

    // 交互
    void toggleRow(int idx);
    void checkFocusMonitor(const CheckItem& it);
    void markRhythm(const CheckItem& it);   // 作息类任务落时刻（rhythm.json）
    void ShowFocusAlert(const CheckItem& it);
    void LogFocusNow();
    void ShowSuccess(const std::wstring& msg);
    void openTaskCard(const CheckItem& it);

    std::vector<CheckRow>   m_rows;
    std::vector<CheckItem>  m_items;     // 当日生效列表（与 m_rows 平行）
    std::vector<Widget*>    m_widgets;
    Button m_resetBtn;
    Button m_backBtn;
    Button m_manageBtn;
    Button m_focusBtn;

    std::vector<uint8_t> m_done;     // 与 m_rows 平行的完成态（内存）
    int   m_doneCount = 0, m_total = 0;
    float m_pct = 0.0f;
    Smooth m_prog{ 0.0f, 0.12f }; // 进度条平滑填充

    float m_t = 0.0f;
    float m_contentTop = 0.0f;
    float m_progY = 0.0f;
    float m_listY = 0.0f;
    float m_histY = 0.0f;
    float m_discY = 0.0f;
    bool  m_built = false;
    bool  m_needRebuild = true;   // 列表需按 m_items 重建
    float m_hist[7]{};            // 近 7 天出勤率（索引 6 = 今日）
    bool  m_resetBtnShown = false;
    bool  m_backBtnShown = false;
    bool  m_manageBtnShown = false;
    bool  m_focusBtnShown = false;

    // 弹层
    AlertOverlay    m_alert;
    TaskCardOverlay m_card;
    Smooth          m_overlayA{ 0.0f, 0.18f };
};

} // namespace lj
