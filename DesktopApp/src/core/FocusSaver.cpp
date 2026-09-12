// ============================================================
//  FocusSaver.cpp — 专注屏保（独立全屏覆盖窗口，GDI 自绘）
//  详见 FocusSaver.h。视觉沿用档案室深色 + 朱砂 accent；
//  音频直接驱动 MusicPlayer 单例；计时状态由 RoomView 推送。
// ============================================================
#include "core/FocusSaver.h"
#include "core/Common.h"      // LogLine
#include "audio/MusicPlayer.h"
#include <windowsx.h>
#include <cmath>

namespace lj {

FocusSaver& FocusSaver::Instance()
{
    static FocusSaver s;
    return s;
}

void FocusSaver::Start(const std::wstring& item, int remainSec)
{
    m_item = item;
    m_remain = remainSec;
    m_paused = false;
    // 批次 C 修正：以顶部胶囊起步（温和）；鼠标静止满 5 秒才展开全屏封面。
    // 静止检测改为 GetCursorPos 轮询（屏幕坐标）——胶囊态窗口只有 480px 宽，
    // 鼠标不在其上时窗口收不到 WM_MOUSEMOVE，旧方案因此「一秒一次」来回跳：
    // 全屏形变本身还会触发一次假 mousemove，立刻又缩回胶囊。
    m_full = false;
    m_idle = 0.0f;
    m_lastPt = { -1, -1 };
    m_lastPaintSec = -1;
    if (!m_hwnd) CreateOverlay(GetModuleHandleW(nullptr));
    if (m_hwnd) ApplyShape();
}

void FocusSaver::Stop()
{
    Destroy();
}

void FocusSaver::PushState(int remainSec, bool paused)
{
    m_remain = remainSec;
    if (paused != m_paused) {
        m_paused = paused;
        if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);   // 播放/暂停图标切换
    }
}

namespace {
HFONT MakeFont(int px, bool bold)
{
    return CreateFontW(-px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
}
} // namespace

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

void FocusSaver::CreateOverlay(HINSTANCE hInst)
{
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &FocusSaver::WndProcStatic;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"FloriFocusSaver";
    RegisterClassExW(&wc);
    // 窗口尺寸在 ApplyShape 里按全屏/胶囊设置
    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"FloriFocusSaver", L"", WS_POPUP,
        -32000, -32000, 10, 10, nullptr, nullptr, hInst, this);
    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_timer = (UINT)SetTimer(m_hwnd, 1, 250, nullptr);   // 250ms：鼠标轮询 + 重绘节流
    RECT rc{}; GetWindowRect(m_hwnd, &rc);
    LogLine(L"[saver] overlay created %dx%d full=%d", rc.right - rc.left, rc.bottom - rc.top, (int)m_full);
}

void FocusSaver::Destroy()
{
    if (m_timer && m_hwnd) { KillTimer(m_hwnd, m_timer); m_timer = 0; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    // 字体缓存
    HFONT* fonts[] = { &m_fTag, &m_fBig, &m_fItem, &m_fMus, &m_fBtn, &m_fPill, &m_fVolS };
    for (auto* f : fonts) { if (*f) { DeleteObject(*f); *f = nullptr; } }
    m_lastPaintSec = -1;
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
    if (m_hwnd) InvalidateRect(m_hwnd, nullptr, TRUE);   // 形态切换立即重绘
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
    case WM_MOUSEMOVE: {
        // 静止检测不再依赖 mousemove（胶囊态收不到）；仅服务音量拖动
        if (m_volDrag) {
            POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            float t = (float)(pt.x - m_rVol.left) / (float)(m_rVol.right - m_rVol.left);
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            MusicPlayer::Instance().SetVolume(t * t);
        }
        return 0;
    }
    case WM_TIMER: {
        // —— 静止检测：直接用屏幕坐标比较（关键！）——
        // 上一版 ScreenToClient 后比较：窗口在胶囊↔全屏变形时同一屏幕点对应的
        // 客户坐标完全不同 → 封面一弹出就误判「鼠标动了」→ 秒退 → 无限闪烁。
        POINT spt{};
        GetCursorPos(&spt);
        if (m_lastPt.x < 0 ||
            std::fabs((float)spt.x - m_lastPt.x) > 1.0f ||
            std::fabs((float)spt.y - m_lastPt.y) > 1.0f) {
            m_lastPt = spt;
            m_idle = 0.0f;
            if (m_full) { m_full = false; ApplyShape(); }   // 移动 → 缩成胶囊
        } else {
            m_idle += 0.25f;
            if (!m_full && m_idle >= 5.0f) { m_full = true; ApplyShape(); }   // 静止 5s → 封面
        }
        if (m_volDrag) {
            POINT cpt{ spt.x, spt.y };
            ScreenToClient(m_hwnd, &cpt);   // 仅音量命中区需要客户坐标
            float t = (float)(cpt.x - m_rVol.left) / (float)(m_rVol.right - m_rVol.left);
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            MusicPlayer::Instance().SetVolume(t * t);
        }
        // —— 重绘节流：内容只在秒变化/状态变化时变，平时不重绘 ——
        // （主线程与软件本体共享；勿高频全屏重绘）
        int sec = m_remain;
        if (sec != m_lastPaintSec) {
            m_lastPaintSec = sec;
            if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);   // FALSE：不擦背景，Paint 自绘全幅
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
            if (Hit(m_rPause, lp)) { if (onPauseToggle) onPauseToggle(); return 0; }
            if (Hit(m_rEnd, lp))   { if (onEnd) onEnd(); return 0; }
        } else {
            if (Hit(m_rPillPause, lp)) { if (onPauseToggle) onPauseToggle(); return 0; }
            if (Hit(m_rPillEnd, lp))   { if (onEnd) onEnd(); return 0; }
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

// —— 小工具：居中画圆按钮（描边或实心）+ 简易矢量符号 ——
namespace {

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

} // namespace

void FocusSaver::Paint(HDC hdc)
{
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    if (m_full) PaintFull(hdc, rc.right, rc.bottom);
    else        PaintPill(hdc, rc.right, rc.bottom);
}

// —— 全屏专注封面 ——
void FocusSaver::PaintFull(HDC hdc, int W, int H)
{
    // 暖黑背景 + 底部朱砂 accent 线
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

    // 顶部小字
    SelectObject(hdc, m_fTag);
    SetTextColor(hdc, seal);
    RECT rt{ 0, H / 6, W, H / 6 + 30 };
    DrawTextW(hdc, L"专  注  中  ·  界  面  已  静  默", -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 大倒计时
    int remain = m_remain < 0 ? 0 : m_remain;
    int mm = remain / 60, ss = remain % 60;
    wchar_t tb[16]; swprintf_s(tb, L"%02d:%02d", mm, ss);
    SelectObject(hdc, m_fBig);
    SetTextColor(hdc, ink);
    RECT rd{ 0, H / 6 + 40, W, H / 6 + 200 };
    DrawTextW(hdc, tb, -1, &rd, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // 打卡项
    SelectObject(hdc, m_fItem);
    SetTextColor(hdc, ink);
    std::wstring itemS = L"当前专注 · " + (m_item.empty() ? std::wstring(L"自习") : m_item);
    RECT ri{ 0, H / 6 + 215, W, H / 6 + 255 };
    DrawTextW(hdc, itemS.c_str(), -1, &ri, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // —— 音乐控制行（62% 高度）——
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

        // ⏮
        m_rPrev = { cx - 22, cy - 22, cx + 22, cy + 22 };
        StrokeCircle(hdc, cx, cy, 20, ring, 1);
        Line(hdc, cx - 8, cy - 7, cx + 6, cy, ink3, 2);
        Line(hdc, cx + 6, cy, cx - 8, cy + 7, ink3, 2);
        Line(hdc, cx - 8, cy + 7, cx - 8, cy - 7, ink3, 2);
        Line(hdc, cx - 11, cy - 7, cx - 11, cy + 7, ink3, 2);
        // ⏯（播放/暂停）
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
        // ⏭
        cx += 62;
        m_rNext = { cx - 22, cy - 22, cx + 22, cy + 22 };
        StrokeCircle(hdc, cx, cy, 20, ring, 1);
        Line(hdc, cx + 8, cy - 7, cx - 6, cy, ink3, 2);
        Line(hdc, cx - 6, cy, cx + 8, cy + 7, ink3, 2);
        Line(hdc, cx + 8, cy + 7, cx + 8, cy - 7, ink3, 2);
        Line(hdc, cx + 11, cy - 7, cx + 11, cy + 7, ink3, 2);

        // 音量条
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

    // —— 底部双按钮 ——
    SelectObject(hdc, m_fBtn);
    int bw = 170, bh = 52, by = H - 110;
    int bx1 = W / 2 - bw - 14, bx2 = W / 2 + 14;
    // 暂停/继续（描边）
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
    DrawTextW(hdc, m_paused ? L"继 续 专 注" : L"暂 停", -1, &rp, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    // 结束（实心朱砂）
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

// —— 顶部胶囊（鼠标移动时）——
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
    int remain = m_remain < 0 ? 0 : m_remain;
    wchar_t tb[16]; swprintf_s(tb, L"%02d:%02d", remain / 60, remain % 60);
    EnsureFonts();
    SelectObject(hdc, m_fPill);
    SetTextColor(hdc, RGB(56, 48, 44));
    std::wstring s = std::wstring(tb) + L"  ·  " + (m_item.empty() ? std::wstring(L"自习") : m_item);
    RECT rt{ 16, 0, W - 100, H };
    DrawTextW(hdc, s.c_str(), -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const COLORREF ink = RGB(90, 82, 76);
    const COLORREF seal = RGB(178, 84, 92);
    int cy = H / 2;
    // 暂停/继续
    m_rPillPause = { W - 92, cy - 15, W - 54, cy + 15 };
    int px = (m_rPillPause.left + m_rPillPause.right) / 2;
    StrokeCircle(hdc, px, cy, 14, RGB(196, 188, 180), 1);
    if (m_paused) {
        Line(hdc, px - 4, cy - 6, px + 6, cy, ink, 2);
        Line(hdc, px + 6, cy, px - 4, cy + 6, ink, 2);
        Line(hdc, px - 4, cy + 6, px - 4, cy - 6, ink, 2);
    } else {
        Line(hdc, px - 3, cy - 5, px - 3, cy + 5, ink, 3);
        Line(hdc, px + 3, cy - 5, px + 3, cy + 5, ink, 3);
    }
    // 结束
    m_rPillEnd = { W - 48, cy - 15, W - 12, cy + 15 };
    int ex = (m_rPillEnd.left + m_rPillEnd.right) / 2;
    FillCircle(hdc, ex, cy, 14, seal);
    Line(hdc, ex - 5, cy - 5, ex + 5, cy + 5, RGB(250, 244, 236), 2);
    Line(hdc, ex + 5, cy - 5, ex - 5, cy + 5, RGB(250, 244, 236), 2);
}

} // namespace lj
