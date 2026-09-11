// ============================================================
//  ReviewNudge.cpp — F-D7 增强：复盘每日自动 nudge
//  独立 message-only 窗口承载 60s 定时器，到点汇总当日复盘并弹托盘气泡。
//  纯本地规则（复用 ReviewEngine），不联网、不调用模型、不读窗内文本。
// ============================================================
#include "core/ReviewNudge.h"
#include "core/ReviewEngine.h"
#include "core/TrayIcon.h"
#include "core/ExamMode.h"
#include "app/Store.h"
#include "core/App.h"   // App::NavigateTo
#include <ctime>

namespace lj {

static const wchar_t* kNudgeClass = L"FloriReviewNudgeWindow";

ReviewNudge& ReviewNudge::Instance()
{
    static ReviewNudge s;
    return s;
}

void ReviewNudge::Init(App* app)
{
    if (m_inited) return;
    m_app = app;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &ReviewNudge::WndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kNudgeClass;
    RegisterClassExW(&wc);   // 重入注册失败无害（类名唯一）

    m_hwnd = CreateWindowExW(0, kNudgeClass, L"FloriReviewNudge", 0,
                             0, 0, 0, 0, HWND_MESSAGE, nullptr,
                             GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return;

    m_timer = (UINT)SetTimer(m_hwnd, 1, 60000, nullptr);  // 每 60s 检查一次是否到提醒时刻
    m_inited = true;
}

void ReviewNudge::Kick()
{
    if (m_inited) MaybeNudge();
}

void ReviewNudge::Shutdown()
{
    if (m_timer && m_hwnd) KillTimer(m_hwnd, m_timer);
    m_timer = 0;
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    m_inited = false;
}

LRESULT CALLBACK ReviewNudge::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ReviewNudge* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ReviewNudge*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<ReviewNudge*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ReviewNudge::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER) { Tick(); return 0; }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void ReviewNudge::Tick()
{
    MaybeNudge();
}

void ReviewNudge::MaybeNudge()
{
    if (!m_inited || !m_hwnd) return;
    if (ExamMode::Instance().Running()) return;   // 模考进行中不打扰

    auto s = CheckinStore::Instance().LoadSettings();
    if (!s.reviewNudge) return;

    std::wstring key = ReviewEngine::TodayKey();
    if (key == s.reviewNudgeLast) return;          // 当日已提醒（含跨重启去重）

    time_t t = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &t);
    if (tm.tm_hour < s.reviewNudgeHour) return;    // 还没到提醒时刻，继续等

    DayReview r = ReviewEngine::Generate(key);

    // 组装气泡文案（托盘气泡 ~256 字上限，保持精简）
    std::wstring title = L"今日复盘提醒";
    std::wstring text;
    text += L"打卡 " + std::to_wstring(r.done) + L"/" + std::to_wstring(r.total)
          + L" · 专注 " + std::to_wstring(r.focusMin) + L" 分钟";
    if (!r.weak.empty())
        text += L"\n薄弱：「" + r.weak[0].tag + L"」" + std::to_wstring(r.weak[0].pct)
              + L"% (目标 " + std::to_wstring(r.weak[0].target) + L"%)";
    if (r.interrupts > 0)
        text += L"\n模考中断 " + std::to_wstring(r.interrupts) + L" 次";
    text += L"\n点此查看 / 写今日复盘";

    TrayIcon::Instance().Balloon(title.c_str(), text.c_str());

    // 点击气泡 → 回到复盘页（先恢复主窗口再跳转）
    TrayIcon::Instance().SetBalloonClickHandler([this]() {
        TrayIcon::Instance().RestoreFromTray();
        if (m_app) m_app->NavigateTo(L"review");
    });

    // 持久化今日已提醒，跨重启当天不再重复弹
    s.reviewNudgeLast = key;
    CheckinStore::Instance().SaveSettings(s);
}

} // namespace lj
