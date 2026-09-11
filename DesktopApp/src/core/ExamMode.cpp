// ============================================================
//  ExamMode.cpp — F-D6 考场模式（模考环境模拟）
//  全屏顶部 HUD（topmost + 点击穿透），真实倒计时；复用 F-D1 前台进程感知
//  做中断检测（切到非学习进程标记一次中断）。到点自动出报告；可中途退出。
//  不锁系统、不强制关进程，只提示 + 标记中断。
// ============================================================
#include "core/ExamMode.h"
#include "core/FocusTracker.h"
#include "core/TrayIcon.h"
#include <ctime>

namespace lj {

ExamMode& ExamMode::Instance()
{
    static ExamMode s;
    return s;
}

int ExamMode::RemainingSec() const
{
    if (!m_running.load()) return 0;
    long long now = (long long)time(nullptr);
    int r = (int)(m_endAt - now);
    return r < 0 ? 0 : r;
}

std::wstring ExamMode::CountdownText() const
{
    int r = RemainingSec();
    int hh = r / 3600, mm = (r % 3600) / 60, ss = r % 60;
    wchar_t buf[16];
    if (hh > 0) swprintf_s(buf, 16, L"%02d:%02d:%02d", hh, mm, ss);
    else        swprintf_s(buf, 16, L"%02d:%02d", mm, ss);
    return buf;
}

std::wstring ExamMode::StatusText() const
{
    if (!m_running.load()) return L"";
    if (m_interruptStart != 0)
        return L"⚠ 中断：" + (m_lastProc.empty() ? L"其它应用" : m_lastProc);
    return L"专注中";
}

void ExamMode::Start(int minutes)
{
    if (m_running.load()) return;
    m_planned = (minutes > 0) ? minutes : 120;
    long long now = (long long)time(nullptr);
    m_startAt = now;
    m_endAt = now + m_planned * 60;
    m_report = ExamReport{};
    m_report.startTime = now;
    m_report.plannedMin = m_planned;
    m_wasFocus = true;
    m_graceUntil = now + 3;          // 起手 3 秒宽限，避免「点击开始」瞬间的误判
    m_interruptStart = 0;
    m_totalInterruptSec = 0;
    m_lastProc.clear();

    // 进入考场：主窗口收起，仅留顶部 HUD + 用户自己的模考应用
    TrayIcon::Instance().MinimizeToTray();
    CreateOverlay(GetModuleHandleW(nullptr));
    m_running = true;
}

void ExamMode::Stop()
{
    if (!m_running.load()) return;
    m_report.abandoned = true;       // 中途退出 = 放弃
    Finish();
}

void ExamMode::Tick()
{
    long long now = (long long)time(nullptr);
    if ((int)(m_endAt - now) <= 0) { Finish(); return; }

    auto fs = FocusTracker::Instance().Snapshot();
    bool focus = fs.studying;        // studying 已排除 idle
    if (now < m_graceUntil) focus = true;

    if (focus) {
        if (m_interruptStart != 0) { // 一段中断结束，累计其时长
            m_totalInterruptSec += (int)(now - m_interruptStart);
            m_interruptStart = 0;
        }
        m_wasFocus = true;
        m_lastProc.clear();
    } else {
        if (m_wasFocus) {            // 新中断段开始
            m_interruptStart = now;
            m_report.interrupts++;
            m_lastProc = fs.process;
            std::wstring msg = L"切到了 " + (fs.process.empty() ? L"其它应用" : fs.process)
                             + L"，已记录一次考场中断。";
            TrayIcon::Instance().Balloon(L"⚠ 考场中断", msg.c_str());
        }
        m_wasFocus = false;
    }
}

void ExamMode::Finish()
{
    long long now = (long long)time(nullptr);
    if (m_interruptStart != 0) {
        m_totalInterruptSec += (int)(now - m_interruptStart);
        m_interruptStart = 0;
    }
    m_report.actualSec    = (int)(now - m_startAt);
    m_report.effectiveSec = (std::max)(0, m_report.actualSec - m_totalInterruptSec);

    CheckinStore::Instance().AddExamReport(m_report);   // 落盘 + 上推

    Destroy();
    m_running = false;
    if (m_onFinish) m_onFinish();
}

void ExamMode::CreateOverlay(HINSTANCE hInst)
{
    if (m_hwnd) return;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &ExamMode::WndProcStatic;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"FloriExamHud";
    RegisterClassExW(&wc);

    int scale = 96;
    if (HDC hdc = GetDC(nullptr)) { scale = GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(nullptr, hdc); }
    float s = scale / 96.0f;
    int W = GetSystemMetrics(SM_CXSCREEN);
    int H = (int)(72.0f * s + 0.5f);   // 顶部横幅高度（DPI 自适应）

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        L"FloriExamHud", L"", WS_POPUP,
        0, 0, W, H, nullptr, nullptr, hInst, this);
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_timer = (UINT)SetTimer(m_hwnd, 1, 1000, nullptr);
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ExamMode::Destroy()
{
    if (m_timer && m_hwnd) { KillTimer(m_hwnd, m_timer); m_timer = 0; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

LRESULT CALLBACK ExamMode::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    ExamMode* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<ExamMode*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;   // 须在 WM_NCCREATE 内记录 hwnd，否则随后 WndProc 落到
                               // DefWindowProcW(m_hwnd=nullptr) 令 CreateWindowExW 返回 1400
    } else {
        self = reinterpret_cast<ExamMode*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ExamMode::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(m_hwnd, &ps);
        Paint(hdc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        Tick();
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void ExamMode::Paint(HDC hdc)
{
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // 背景：酒红档案室深色横幅
    HBRUSH hBg = CreateSolidBrush(RGB(38, 17, 23));
    HBRUSH hOld = (HBRUSH)SelectObject(hdc, hBg);
    PatBlt(hdc, 0, 0, W, H, PATCOPY);
    // 底部朱砂 accent 线
    HBRUSH hAcc = CreateSolidBrush(RGB(107, 42, 53));
    SelectObject(hdc, hAcc);
    PatBlt(hdc, 0, H - 3, W, 3, PATCOPY);
    SelectObject(hdc, hOld);
    DeleteObject(hBg); DeleteObject(hAcc);

    SetBkMode(hdc, TRANSPARENT);

    // 大号倒计时（居中）
    HFONT hBig = CreateFontW(-32, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Microsoft YaHei UI");
    HFONT hSmall = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Microsoft YaHei UI");
    HFONT hOldF = (HFONT)SelectObject(hdc, hBig);

    SetTextColor(hdc, RGB(242, 230, 216));
    RECT c{ 0, 4, W, H - 4 };
    DrawTextW(hdc, CountdownText().c_str(), -1, &c, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hSmall);
    // 左：状态（中断时高亮朱砂）
    SetTextColor(hdc, m_interruptStart != 0 ? RGB(224, 120, 96) : RGB(196, 184, 176));
    RECT l{ 16, 4, W / 2 - 60, H - 4 };
    DrawTextW(hdc, StatusText().c_str(), -1, &l, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    // 右：中断次数 + 退出提示
    SetTextColor(hdc, RGB(196, 184, 176));
    std::wstring right = L"中断 " + std::to_wstring(m_report.interrupts) + L" 次 · 右键托盘退出";
    RECT r{ W / 2 + 60, 4, W - 16, H - 4 };
    DrawTextW(hdc, right.c_str(), -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hOldF);
    DeleteObject(hBig); DeleteObject(hSmall);
}

} // namespace lj
