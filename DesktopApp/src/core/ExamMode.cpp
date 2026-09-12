// ============================================================
//  ExamMode.cpp — F-D6 考场模式（模考环境模拟）
//  全屏顶部 HUD（topmost + 点击穿透），真实倒计时；复用 F-D1 前台进程感知
//  做中断检测（切到非学习进程标记一次中断）。到点自动出报告；可中途退出。
//  不锁系统、不强制关进程，只提示 + 标记中断。
// ============================================================
#include "core/ExamMode.h"
#include "core/FocusTracker.h"
#include "core/TrayIcon.h"
#include <windowsx.h>
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

    // 批次 F：大横条改为「顶部小圆角框」（自习室屏保同款）——显示时间 + 中断，
    // 右侧直接提供「结束」交互（两步确认），不必再去托盘。
    // 穿透策略：WM_NCHITTEST 里仅按钮区收点击（HTCLIENT），其余 HTTRANSPARENT
    // 穿透到下层模考应用——既可点按钮，又不挡正常使用。
    int scale = 96;
    if (HDC hdc = GetDC(nullptr)) { scale = GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(nullptr, hdc); }
    float s = scale / 96.0f;
    int W = GetSystemMetrics(SM_CXSCREEN);
    int pw = (int)(560.0f * s + 0.5f), ph = (int)(52.0f * s + 0.5f);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"FloriExamHud", L"", WS_POPUP,
        (W - pw) / 2, (int)(16.0f * s), pw, ph, nullptr, nullptr, hInst, this);
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_timer = (UINT)SetTimer(m_hwnd, 1, 1000, nullptr);
    m_confirmUntil = 0;
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void ExamMode::Destroy()
{
    if (m_timer && m_hwnd) { KillTimer(m_hwnd, m_timer); m_timer = 0; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    m_confirmUntil = 0;
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
    case WM_NCHITTEST: {
        // 仅「结束」按钮可点，其余全部穿透到下层模考应用
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(m_hwnd, &pt);
        if (pt.x >= m_rEnd.left && pt.x <= m_rEnd.right &&
            pt.y >= m_rEnd.top && pt.y <= m_rEnd.bottom)
            return HTCLIENT;
        return HTTRANSPARENT;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (pt.x >= m_rEnd.left && pt.x <= m_rEnd.right &&
            pt.y >= m_rEnd.top && pt.y <= m_rEnd.bottom) {
            long long now = (long long)time(nullptr);
            if (m_confirmUntil != 0 && now <= m_confirmUntil) {
                m_confirmUntil = 0;
                Finish();                       // 二次点击：确认结束并出报告
            } else {
                m_confirmUntil = now + 3;       // 首次点击：进入 3 秒确认态
                InvalidateRect(m_hwnd, nullptr, TRUE);
            }
            return 0;
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(m_hwnd, &ps);
        Paint(hdc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        Tick();
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

// —— GDI 小工具（胶囊绘制用）——
namespace {
HFONT ExMakeFont(int px, bool bold)
{
    return CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}
void ExRoundRect(HDC hdc, int l, int t, int r, int b, int rad, COLORREF fill, COLORREF edge, int edgeW)
{
    HBRUSH hb = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, edgeW, edge);
    auto ob = (HBRUSH)SelectObject(hdc, hb);
    auto op = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, l, t, r, b, rad, rad);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(hb); DeleteObject(pen);
}
} // namespace

void ExamMode::Paint(HDC hdc)
{
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    const COLORREF paper = RGB(247, 243, 236);
    const COLORREF ink   = RGB(56, 48, 44);
    const COLORREF ink3  = RGB(150, 140, 132);
    const COLORREF seal  = RGB(178, 84, 92);
    const COLORREF warn  = RGB(196, 96, 72);

    // 胶囊本体（米白 + 朱砂细边）
    ExRoundRect(hdc, 0, 0, W, H, H / 2, paper, seal, 1);

    SetBkMode(hdc, TRANSPARENT);

    // 左：倒计时（等宽加粗）
    HFONT hBig = ExMakeFont(24, true);
    SelectObject(hdc, hBig);
    SetTextColor(hdc, ink);
    RECT rc1{ 24, 0, 150, H };
    DrawTextW(hdc, CountdownText().c_str(), -1, &rc1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 中：状态（考试中 / 中断进行中红显）+ 中断次数
    bool inInterrupt = (m_interruptStart != 0);
    HFONT hMid = ExMakeFont(13, false);
    SelectObject(hdc, hMid);
    SetTextColor(hdc, inInterrupt ? warn : ink3);
    std::wstring status = inInterrupt
        ? (L"⚠ 中断：" + (m_lastProc.empty() ? L"其它应用" : m_lastProc))
        : L"考试中";
    RECT rc2{ 170, 0, W / 2 + 40, H };
    DrawTextW(hdc, status.c_str(), -1, &rc2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, ink3);
    wchar_t ib[32];
    swprintf_s(ib, L"中断 %d 次", m_report.interrupts);
    RECT rc3{ W / 2 + 50, 0, W - 150, H };
    DrawTextW(hdc, ib, -1, &rc3, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 右：结束按钮（两步确认）
    m_rEnd = { W - 128, 8, W - 14, H - 8 };
    long long now = (long long)time(nullptr);
    bool confirming = (m_confirmUntil != 0 && now <= m_confirmUntil);
    if (confirming) {
        ExRoundRect(hdc, m_rEnd.left, m_rEnd.top, m_rEnd.right, m_rEnd.bottom,
                    (m_rEnd.bottom - m_rEnd.top) / 2, warn, warn, 0);
        SetTextColor(hdc, RGB(255, 246, 238));
    } else {
        ExRoundRect(hdc, m_rEnd.left, m_rEnd.top, m_rEnd.right, m_rEnd.bottom,
                    (m_rEnd.bottom - m_rEnd.top) / 2, seal, seal, 0);
        SetTextColor(hdc, RGB(255, 246, 238));
    }
    HFONT hBtn = ExMakeFont(13, true);
    SelectObject(hdc, hBtn);
    RECT rb{ m_rEnd.left, m_rEnd.top, m_rEnd.right, m_rEnd.bottom };
    DrawTextW(hdc, confirming ? L"确认结束?" : L"✕ 结束", -1, &rb,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    DeleteObject(hBig); DeleteObject(hMid); DeleteObject(hBtn);
}

} // namespace lj
