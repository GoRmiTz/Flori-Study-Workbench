// ============================================================
//  TrayIcon.cpp — 系统托盘常驻
// ============================================================
#include "core/TrayIcon.h"
#include "core/Common.h"
#include <cmath>

namespace lj {

#define WM_TRAY_CALLBACK (WM_APP + 43)

static const wchar_t* kTrayClassName = L"FloriTrayIconWindow";

TrayIcon& TrayIcon::Instance()
{
    static TrayIcon s;
    return s;
}

HICON TrayIcon::CreateSealIcon()
{
    const int sz = 32;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = sz;
    bi.bV5Height = -sz;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask   = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask  = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hbm = CreateDIBSection(hdc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hbm || !bits) return nullptr;

    DWORD* px = static_cast<DWORD*>(bits);
    float cx = sz * 0.5f, cy = sz * 0.5f, r = sz * 0.45f;
    for (int y = 0; y < sz; ++y) {
        for (int x = 0; x < sz; ++x) {
            float dx = (float)x - cx, dy = (float)y - cy;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d >= r) { px[y * sz + x] = 0; continue; }
            BYTE a = (d < r - 2.0f) ? 255 : (BYTE)(255 * (r - d) / 2.0f);
            BYTE R = 0x6B, G = 0x2A, B = 0x35;
            px[y * sz + x] = (a << 24) | (R << 16) | (G << 8) | B;
        }
    }

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = hbm;
    ii.hbmColor = hbm;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(hbm);
    return icon;
}

LRESULT CALLBACK TrayIcon::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    TrayIcon* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<TrayIcon*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TRAY_CALLBACK:
        if (lp == WM_LBUTTONUP) {
            RestoreFromTray();
            return 0;
        }
        if (lp == WM_RBUTTONUP) {
            ShowContextMenu();
            return 0;
        }
        if (lp == NIN_BALLOONUSERCLICK) {   // F-D7 增强：点气泡 → 跳转复盘
            if (m_onBalloonClick) m_onBalloonClick();
            return 0;
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == 1) RestoreFromTray();
        else if (LOWORD(wp) == 2) {
            if (m_owner) PostMessageW(m_owner, WM_CLOSE, 0, 0);
        }
        else if (LOWORD(wp) >= 100 && m_onMenuExtra) {
            m_onMenuExtra((int)LOWORD(wp));
        }
        return 0;

    case WM_DESTROY:
        // 不在此重复调用 Destroy()：Destroy() 由显式调用或析构驱动，
        // DestroyWindow 已会同步触发 WM_DESTROY，重入会二次销毁同一窗口。
        return 0;
    }
    // 注意：必须用消息携带的 hwnd——WM_NCCREATE 等创建期消息到达时
    // m_hwnd 成员尚未赋值，传 nullptr 会导致 DefWindowProc 返回失败，
    // 进而 CreateWindowExW 报 err=1400（托盘窗口永远建不出来）。
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool TrayIcon::Create(HWND owner, HINSTANCE hInst, const wchar_t* tip)
{
    LogLine(L"[tray] Create begin (hwnd existed: %d)", m_hwnd ? 1 : 0);
    if (m_hwnd) return true;
    m_owner = owner;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayIcon::WndProcStatic;
    wc.hInstance = hInst;
    wc.lpszClassName = kTrayClassName;
    if (!RegisterClassExW(&wc)) {
        DWORD e = GetLastError();
        if (e != ERROR_CLASS_ALREADY_EXISTS)
            LogLine(L"[tray] RegisterClassExW 失败 err=%lu", e);
    }

    m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW,
                             kTrayClassName, L"FloriTray", WS_POPUP,
                             -32000, -32000, 1, 1, nullptr, nullptr, hInst, this);
    if (!m_hwnd) {
        LogLine(L"[tray] 窗口创建失败 err=%lu", GetLastError());
        return false;
    }
    // 常驻隐藏：不显示窗口本体（收托盘消息 + 气泡由 NIF_ICON 承载）
    ShowWindow(m_hwnd, SW_HIDE);

    m_icon = CreateSealIcon();
    m_nid = new NOTIFYICONDATAW{};

    m_nid->cbSize = sizeof(*m_nid);
    m_nid->hWnd = m_hwnd;
    m_nid->uID = 1;
    m_nid->uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid->uCallbackMessage = WM_TRAY_CALLBACK;
    m_nid->hIcon = m_icon ? m_icon : LoadIconW(nullptr, IDI_APPLICATION);
    if (tip) wcscpy_s(m_nid->szTip, tip);

    m_visible = Shell_NotifyIconW(NIM_ADD, m_nid);
    if (!m_visible) {
        LogLine(L"[tray] Shell_NotifyIcon(NIM_ADD) 失败");
        Destroy();
        return false;
    }
    // 启用 V4 行为（气泡提示 NIF_INFO、Aero Peek 等需要）
    m_nid->uVersion = 4;
    Shell_NotifyIconW(NIM_SETVERSION, m_nid);
    LogLine(L"[tray] Shell_NotifyIcon OK（托盘图标已常驻；Win11 默认收在任务栏角溢出 ^ 内）");
    return true;
}

void TrayIcon::Destroy()
{
    if (m_visible && m_nid) {
        Shell_NotifyIconW(NIM_DELETE, m_nid);
        m_visible = false;
    }
    if (m_icon) { DestroyIcon(m_icon); m_icon = nullptr; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    delete m_nid; m_nid = nullptr;
}

void TrayIcon::MinimizeToTray()
{
    if (!m_owner) return;
    ShowWindow(m_owner, SW_HIDE);
    if (!m_visible && m_hwnd && m_nid) {
        m_nid->hWnd = m_hwnd;
        m_visible = Shell_NotifyIconW(NIM_ADD, m_nid);
    }
}

void TrayIcon::RestoreFromTray()
{
    if (!m_owner) return;
    ShowWindow(m_owner, SW_SHOW);
    ShowWindow(m_owner, SW_RESTORE);
    SetForegroundWindow(m_owner);
}

void TrayIcon::AddMenuItem(int id, const std::wstring& label)
{
    if (id < 100) return;            // 内置项占用 1/2，扩展项须 >= 100
    std::lock_guard<std::mutex> lk(m_menuMu);
    for (auto& it : m_menuExtras)
        if (it.first == id) { it.second = label; return; }
    m_menuExtras.push_back({ id, label });
}

void TrayIcon::Balloon(const wchar_t* title, const wchar_t* text, DWORD flags)
{
    if (!m_visible || !m_nid) return;
    m_nid->uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    m_nid->dwInfoFlags = flags;
    m_nid->uTimeout = 12000;
    wcscpy_s(m_nid->szInfoTitle, title ? title : L"");
    wcscpy_s(m_nid->szInfo, text ? text : L"");
    Shell_NotifyIconW(NIM_MODIFY, m_nid);
    // 复位 info 标志与文本，避免下次 NIM_MODIFY 残留旧气泡
    m_nid->uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid->szInfo[0] = L'\0';
}

void TrayIcon::ShowContextMenu()
{
    if (!m_owner) return;
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"显示主窗口");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 2, L"退出");
    // P2-3 扩展项（自动更新等）
    {
        std::lock_guard<std::mutex> lk(m_menuMu);
        if (!m_menuExtras.empty()) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            for (auto& it : m_menuExtras)
                AppendMenuW(menu, MF_STRING, it.first, it.second.c_str());
        }
    }

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);

    if (cmd == 1) RestoreFromTray();
    else if (cmd == 2 && m_owner) PostMessageW(m_owner, WM_CLOSE, 0, 1);  // lp=1 强制退出，不再弹确认
    else if (cmd >= 100 && m_onMenuExtra) m_onMenuExtra(cmd);
}

} // namespace lj
