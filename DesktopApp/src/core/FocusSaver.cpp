// ============================================================
//  FocusSaver.cpp — 专注屏保 v3（独立线程 + 独立全屏覆盖窗口，GDI 自绘）
//  详见 FocusSaver.h。视觉沿用档案室深色 + 朱砂 accent。
// ============================================================
#include "core/FocusSaver.h"
#include "core/Common.h"      // LogLine
#include "audio/MusicPlayer.h"
#include <windowsx.h>
#include <cmath>

namespace lj {

namespace {
HFONT MakeFont(int px, bool bold)
{
    return CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}
void FillCircle(HDC hdc, int cx, int cy, int r, COLORREF c)
{
    HBRUSH b = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    auto ob = (HBRUSH)SelectObject(hdc, b);
    auto op = (HPEN)SelectObject(hdc, pen);
    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(b); DeleteObject(pen);
}
void StrokeCircle(HDC hdc, int cx, int cy, int r, COLORREF c, int w)
{
    HPEN pen = CreatePen(PS_SOLID, w, c);
    HBRUSH b = (HBRUSH)GetStockObject(NULL_BRUSH);
    auto op = (HPEN)SelectObject(hdc, pen);
    auto ob = (HBRUSH)SelectObject(hdc, b);
    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(pen);
}
void Line(HDC hdc, int x1, int y1, int x2, int y2, COLORREF c, int w)
{
    HPEN pen = CreatePen(PS_SOLID, w, c);
    auto op = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y1, nullptr);
    LineTo(hdc, x2, y2);
    SelectObject(hdc, op);
    DeleteObject(pen);
}
const UINT WM_FS_STOP = WM_APP + 0xF0A;   // 主线程 → 屏保线程：退出消息循环
} // namespace

FocusSaver& FocusSaver::Instance()
{
    static FocusSaver s;
    return s;
}

void FocusSaver::Start(const std::wstring& item, int remainSec)
{
    if (m_running.load(std::memory_order_acquire)) return;
    {
        std::lock_guard<std::mutex> lk(m_itemMu);
        m_item = item;
    }
    m_remain.store(remainSec, std::memory_order_release);
    m_paused.store(false, std::memory_order_release);
    m_actPause.store(0, std::memory_order_release);
    m_actEnd.store(0, std::memory_order_release);
    m_stopReq.store(false, std::memory_order_release);
    m_running.store(true, std::memory_order_release);
    m_thread = std::thread([this] { ThreadProc(); });
}

void FocusSaver::Stop()
{
    if (!m_running.load(std::memory_order_acquire)) return;
    m_stopReq.store(true, std::memory_order_release);
    DWORD tid = m_threadId.load(std::memory_order_acquire);
    if (tid) PostThreadMessageW(tid, WM_FS_STOP, 0, 0);
    if (m_thread.joinable()) m_thread.join();
    m_running.store(false, std::memory_order_release);
}

void FocusSaver::PushState(int remainSec, bool paused)
{
    m_remain.store(remainSec, std::memory_order_release);
    m_paused.store(paused, std::memory_order_release);
}

bool FocusSaver::ConsumeActions(bool& pauseToggle, bool& endFocus)
{
    int p = m_actPause.exchange(0, std::memory_order_acq_rel);
    int e = m_actEnd.exchange(0, std::memory_order_acq_rel);
    pauseToggle = (p > 0);
    endFocus    = (e > 0);
    return pauseToggle || endFocus;
}

// —— 屏保线程主体 ——
void FocusSaver::ThreadProc()
{
    m_threadId.store(GetCurrentThreadId(), std::memory_order_release);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &FocusSaver::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"FloriFocusSaver";
    RegisterClassExW(&wc);   // 重复注册失败无妨（进程级已注册）

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"FloriFocusSaver", L"", WS_POPUP,
        -32000, -32000, 10, 10, nullptr, nullptr, wc.hInstance, this);
    if (!m_hwnd) { m_running.store(false); return; }
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_timer = (UINT)SetTimer(m_hwnd, 1, 250, nullptr);
    ApplyShape();
    {
        RECT rc{}; GetWindowRect(m_hwnd, &rc);
        LogLine(L"[saver] thread overlay %dx%d full=%d", rc.right - rc.left, rc.bottom - rc.top, (int)m_full);
    }

    // 消息循环（专属线程，不占主线程）
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_FS_STOP || msg.message == WM_QUIT) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (m_stopReq.load(std::memory_order_acquire)) break;
    }

    if (m_timer && m_hwnd) { KillTimer(m_hwnd, m_timer); m_timer = 0; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    ReleaseFonts();
    m_running.store(false, std::memory_order_release);
}

void FocusSaver::ApplyShape()
{
    if (!m_hwnd) return;
    if (m_full) {
        int W = GetSystemMetrics(SM_CXSCREEN);
        int H = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, W, H,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        int scale = 96;
        if (HDC hdc = GetDC(nullptr)) { scale = GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(nullptr, hdc); }
        float s = scale / 96.0f;
        int pw = (int)(480.0f * s + 0.5f), ph = (int)(48.0f * s + 0.5f);
        int W = GetSystemMetrics(SM_CXSCREEN);
        SetWindowPos(m_hwnd, HWND_TOPMOST, (W - pw) / 2, (int)(14.0f * s), pw, ph,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);   // 形态切换立即重绘
}

bool FocusSaver::Hit(const RECT& r, LPARAM lp) const
{
    POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
    return pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom;
}

LRESULT CALLBACK FocusSaver::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    FocusSaver* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<FocusSaver*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<FocusSaver*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT FocusSaver::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER: {
        // —— 息屏判定：GetLastInputInfo 系统级空闲（鼠标 + 键盘都算）——
        // 打字/点按同样打断息屏；与窗口形态、鼠标位置完全无关。
        LASTINPUTINFO lii{ sizeof(LASTINPUTINFO) };
        DWORD idleMs = 0xFFFFFFFF;
        if (GetLastInputInfo(&lii)) idleMs = GetTickCount() - lii.dwTime;
        float idleSec = (float)(idleMs / 1000u);
        if (m_full && idleSec < 0.25f) { m_full = false; ApplyShape(); }          // 有输入 → 缩回胶囊
        else if (!m_full && idleSec >= 5.0f) { m_full = true; ApplyShape(); }     // 静止 5s → 封面

        if (m_volDrag && m_hwnd) {
            POINT spt{}; GetCursorPos(&spt);
            POINT cpt{ spt.x, spt.y };
            ScreenToClient(m_hwnd, &cpt);
            float t = (float)(cpt.x - m_rVol.left) / (float)(m_rVol.right - m_rVol.left);
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            MusicPlayer::Instance().SetVolume(t * t);
        }
        // —— 重绘节流：只在秒变化时重绘（形态/暂停切换由各自路径 Invalidate）——
        int sec = m_remain.load(std::memory_order_acquire);
        if (sec != m_lastPaintSec) {
            m_lastPaintSec = sec;
            if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (m_full) {
            if (Hit(m_rPrev, lp)) { MusicPlayer::Instance().Prev(); return 0; }
            if (Hit(m_rNext, lp)) { MusicPlayer::Instance().Next(); return 0; }
            if (Hit(m_rPlay, lp)) { MusicPlayer::Instance().Toggle(); return 0; }
            if (Hit(m_rVol, lp))  {
                m_volDrag = true; SetCapture(m_hwnd);
                float t = (float)(GET_X_LPARAM(lp) - m_rVol.left) / (float)(m_rVol.right - m_rVol.left);
                t = t < 0 ? 0 : (t > 1 ? 1 : t);
                MusicPlayer::Instance().SetVolume(t * t);
                return 0;
            }
            if (Hit(m_rPause, lp)) { m_actPause.fetch_add(1); return 0; }   // 主线程执行
            if (Hit(m_rEnd, lp))   { m_actEnd.fetch_add(1); return 0; }
        } else {
            if (Hit(m_rPillPause, lp)) { m_actPause.fetch_add(1); return 0; }
            if (Hit(m_rPillEnd, lp))   { m_actEnd.fetch_add(1); return 0; }
        }
        return 0;
    case WM_LBUTTONUP:
        if (m_volDrag) {
            m_volDrag = false;
            if (GetCapture() == m_hwnd) ReleaseCapture();
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(m_hwnd, &ps);
        Paint(hdc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void FocusSaver::EnsureFonts()
{
    if (m_fBig) return;
    m_fTag  = MakeFont(15, true);
    m_fBig  = MakeFont(120, true);
    m_fItem = MakeFont(26, true);
    m_fMus  = MakeFont(15, false);
    m_fBtn  = MakeFont(17, true);
    m_fPill = MakeFont(17, true);
    m_fVolS = MakeFont(12, false);
}

void FocusSaver::ReleaseFonts()
{
    HFONT* fonts[] = { &m_fTag, &m_fBig, &m_fItem, &m_fMus, &m_fBtn, &m_fPill, &m_fVolS };
    for (auto* f : fonts) { if (*f) { DeleteObject(*f); *f = nullptr; } }
    m_lastPaintSec = -1;
}

void FocusSaver::Paint(HDC hdc)
{
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    if (m_full) PaintFull(hdc, rc.right, rc.bottom);
    else        PaintPill(hdc, rc.right, rc.bottom);
}

// —— 全屏专注封面 ——
void FocusSaver::PaintFull(HDC hdc, int W, int H)
{
    HBRUSH hBg = CreateSolidBrush(RGB(31, 27, 25));
    HBRUSH hOld = (HBRUSH)SelectObject(hdc, hBg);
    PatBlt(hdc, 0, 0, W, H, PATCOPY);
    HBRUSH hAcc = CreateSolidBrush(RGB(107, 42, 53));
    SelectObject(hdc, hAcc);
    PatBlt(hdc, 0, H - 4, W, 4, PATCOPY);
    SelectObject(hdc, hOld);
    DeleteObject(hBg); DeleteObject(hAcc);

    SetBkMode(hdc, TRANSPARENT);
    const COLORREF ink   = RGB(242, 230, 216);
    const COLORREF ink3  = RGB(196, 184, 176);
    const COLORREF seal  = RGB(178, 84, 92);
    EnsureFonts();

    SelectObject(hdc, m_fTag);
    SetTextColor(hdc, seal);
    RECT rt{ 0, H / 6, W, H / 6 + 30 };
    DrawTextW(hdc, L"专  注  中  ·  屏  保  模  式", -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int remain = m_remain.load(std::memory_order_acquire);
    if (remain < 0) remain = 0;
    wchar_t tb[16]; swprintf_s(tb, L"%02d:%02d", remain / 60, remain % 60);
    SelectObject(hdc, m_fBig);
    SetTextColor(hdc, ink);
    RECT rd{ 0, H / 6 + 40, W, H / 6 + 200 };
    DrawTextW(hdc, tb, -1, &rd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    std::wstring item;
    { std::lock_guard<std::mutex> lk(m_itemMu); item = m_item; }
    SelectObject(hdc, m_fItem);
    SetTextColor(hdc, ink);
    std::wstring itemS = L"当前专注 · " + (item.empty() ? std::wstring(L"自习") : item);
    RECT ri{ 0, H / 6 + 215, W, H / 6 + 255 };
    DrawTextW(hdc, itemS.c_str(), -1, &ri, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int my = (int)(H * 0.62);
    auto ps = MusicPlayer::Instance().GetState();
    bool hasMusic = (ps != MusicPlayer::State::Idle) || MusicPlayer::Instance().Count() > 0;
    if (hasMusic) {
        SelectObject(hdc, m_fMus);
        SetTextColor(hdc, ink3);
        std::wstring t = MusicPlayer::Instance().GetTitle();
        size_t n = MusicPlayer::Instance().Count(), i = MusicPlayer::Instance().Index();
        if (!t.empty()) {
            std::wstring cnt;
            if (n > 1) cnt = L"  ·  " + std::to_wstring(i + 1) + L"/" + std::to_wstring(n);
            RECT rm{ 0, my - 46, W, my - 18 };
            DrawTextW(hdc, (L"♪ " + t + cnt).c_str(), -1, &rm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const COLORREF ring = RGB(120, 110, 104);
        int cy = my + 40;
        int cx = W / 2 - 90;

        m_rPrev = { cx - 22, cy - 22, cx + 22, cy + 22 };
        StrokeCircle(hdc, cx, cy, 20, ring, 1);
        Line(hdc, cx - 8, cy - 7, cx + 6, cy, ink3, 2);
        Line(hdc, cx + 6, cy, cx - 8, cy + 7, ink3, 2);
        Line(hdc, cx - 8, cy + 7, cx - 8, cy - 7, ink3, 2);
        Line(hdc, cx - 11, cy - 7, cx - 11, cy + 7, ink3, 2);

        cx += 62;
        m_rPlay = { cx - 24, cy - 24, cx + 24, cy + 24 };
        FillCircle(hdc, cx, cy, 21, seal);
        if (ps == MusicPlayer::State::Playing) {
            Line(hdc, cx - 5, cy - 8, cx - 5, cy + 8, RGB(250, 244, 236), 4);
            Line(hdc, cx + 5, cy - 8, cx + 5, cy + 8, RGB(250, 244, 236), 4);
        } else {
            Line(hdc, cx - 5, cy - 8, cx + 8, cy, RGB(250, 244, 236), 3);
            Line(hdc, cx + 8, cy, cx - 5, cy + 8, RGB(250, 244, 236), 3);
            Line(hdc, cx - 5, cy + 8, cx - 5, cy - 8, RGB(250, 244, 236), 3);
        }

        cx += 62;
        m_rNext = { cx - 22, cy - 22, cx + 22, cy + 22 };
        StrokeCircle(hdc, cx, cy, 20, ring, 1);
        Line(hdc, cx + 8, cy - 7, cx - 6, cy, ink3, 2);
        Line(hdc, cx - 6, cy, cx + 8, cy + 7, ink3, 2);
        Line(hdc, cx + 8, cy + 7, cx + 8, cy - 7, ink3, 2);
        Line(hdc, cx + 11, cy - 7, cx + 11, cy + 7, ink3, 2);

        int vx0 = cx + 40, vx1 = vx0 + 190;
        m_rVol = { vx0, cy - 12, vx1, cy + 12 };
        float vol = MusicPlayer::Instance().GetVolume();
        float vp = vol > 0 ? std::sqrt(vol) : 0.0f;
        Line(hdc, vx0, cy, vx1, cy, RGB(80, 72, 68), 4);
        int fx = vx0 + (int)((vx1 - vx0) * vp);
        if (fx > vx0 + 2) Line(hdc, vx0, cy, fx, cy, RGB(176, 138, 84), 4);
        FillCircle(hdc, fx, cy, 6, RGB(196, 184, 176));
        SelectObject(hdc, m_fVolS);
        SetTextColor(hdc, ink3);
        RECT rv{ vx1 + 12, cy - 12, vx1 + 90, cy + 12 };
        wchar_t vb[16]; swprintf_s(vb, L"音量 %d%%", (int)(vol * 100 + 0.5f));
        DrawTextW(hdc, vb, -1, &rv, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    else {
        SelectObject(hdc, m_fMus);
        SetTextColor(hdc, RGB(120, 110, 104));
        RECT rm{ 0, my, W, my + 26 };
        DrawTextW(hdc, L"未在播放背景音乐 · 可回自习室选择曲目", -1, &rm, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(hdc, m_fBtn);
    int bw = 170, bh = 52, by = H - 110;
    int bx1 = W / 2 - bw - 14, bx2 = W / 2 + 14;
    m_rPause = { bx1, by, bx1 + bw, by + bh };
    HPEN penS = CreatePen(PS_SOLID, 1, RGB(120, 110, 104));
    HBRUSH bNull = (HBRUSH)GetStockObject(NULL_BRUSH);
    auto op = (HPEN)SelectObject(hdc, penS);
    auto ob = (HBRUSH)SelectObject(hdc, bNull);
    RoundRect(hdc, bx1, by, bx1 + bw, by + bh, 10, 10);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(penS);
    SetTextColor(hdc, ink3);
    RECT rp{ bx1, by, bx1 + bw, by + bh };
    DrawTextW(hdc, m_paused.load(std::memory_order_acquire) ? L"继 续 专 注" : L"暂 停",
              -1, &rp, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    m_rEnd = { bx2, by, bx2 + bw, by + bh };
    HBRUSH bSeal = CreateSolidBrush(seal);
    ob = (HBRUSH)SelectObject(hdc, bSeal);
    RoundRect(hdc, bx2, by, bx2 + bw, by + bh, 10, 10);
    SelectObject(hdc, ob);
    DeleteObject(bSeal);
    SetTextColor(hdc, RGB(250, 244, 236));
    RECT re{ bx2, by, bx2 + bw, by + bh };
    DrawTextW(hdc, L"结 束 专 注", -1, &re, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// —— 顶部胶囊 ——
void FocusSaver::PaintPill(HDC hdc, int W, int H)
{
    int rad = H / 2;
    HBRUSH hBg = CreateSolidBrush(RGB(247, 243, 236));
    HBRUSH hOld = (HBRUSH)SelectObject(hdc, hBg);
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(196, 188, 180));
    auto op = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, 0, 0, W, H, rad, rad);
    SelectObject(hdc, hOld); SelectObject(hdc, op);
    DeleteObject(hBg); DeleteObject(pen);

    SetBkMode(hdc, TRANSPARENT);
    int remain = m_remain.load(std::memory_order_acquire);
    if (remain < 0) remain = 0;
    wchar_t tb[16]; swprintf_s(tb, L"%02d:%02d", remain / 60, remain % 60);
    EnsureFonts();
    SelectObject(hdc, m_fPill);
    SetTextColor(hdc, RGB(56, 48, 44));
    std::wstring item;
    { std::lock_guard<std::mutex> lk(m_itemMu); item = m_item; }
    std::wstring s = std::wstring(tb) + L"  ·  " + (item.empty() ? std::wstring(L"自习") : item);
    RECT rt{ 16, 0, W - 100, H };
    DrawTextW(hdc, s.c_str(), -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const COLORREF ink = RGB(90, 82, 76);
    const COLORREF seal = RGB(178, 84, 92);
    int cy = H / 2;
    m_rPillPause = { W - 92, cy - 15, W - 54, cy + 15 };
    int px = (m_rPillPause.left + m_rPillPause.right) / 2;
    StrokeCircle(hdc, px, cy, 14, RGB(196, 188, 180), 1);
    if (m_paused.load(std::memory_order_acquire)) {
        Line(hdc, px - 4, cy - 6, px + 6, cy, ink, 2);
        Line(hdc, px + 6, cy, px - 4, cy + 6, ink, 2);
        Line(hdc, px - 4, cy + 6, px - 4, cy - 6, ink, 2);
    } else {
        Line(hdc, px - 3, cy - 5, px - 3, cy + 5, ink, 3);
        Line(hdc, px + 3, cy - 5, px + 3, cy + 5, ink, 3);
    }
    m_rPillEnd = { W - 48, cy - 15, W - 12, cy + 15 };
    int ex = (m_rPillEnd.left + m_rPillEnd.right) / 2;
    FillCircle(hdc, ex, cy, 14, seal);
    Line(hdc, ex - 5, cy - 5, ex + 5, cy + 5, RGB(250, 244, 236), 2);
    Line(hdc, ex + 5, cy - 5, ex - 5, cy + 5, RGB(250, 244, 236), 2);
}

} // namespace lj
