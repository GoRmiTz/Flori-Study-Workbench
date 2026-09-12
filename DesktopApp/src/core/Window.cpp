#include "core/Window.h"
#include <dwmapi.h>
#include <windowsx.h>
#include <shellapi.h>   // APPBARDATA / SHAppBarMessage：最大化时避让自动隐藏任务栏
#include "core/Hwnd.h"  // AppPalette()：主题化 EDIT 背景/文字
#include "ui/Theme.h"   // Palette / RGB 取值

#pragma comment(lib, "dwmapi.lib")

namespace lj {

static const wchar_t* kClassName = L"FloriDossierWindow";

// 与 main.cpp 约定的「第二个实例请求置前」消息（跨进程 SetForegroundWindow 会被限流，故由本进程自行恢复）
static UINT g_wmBringFront = RegisterWindowMessageW(L"Flori.Desktop.BringFront");

// 主题化 EDIT 背景画刷缓存：仅在背景色变化时重建，避免每次 WM_CTLCOLOREDIT 泄漏 GDI 对象
static HBRUSH g_editBrush = nullptr;
static COLORREF g_editBrushColor = 0xFFFFFFFF;

// D2D 浮点色 → COLORREF（0..255），定义见 App.cpp（共享给各视图）

bool Window::Create(HINSTANCE hInst, const wchar_t* title, int widthDip, int heightDip)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::WndProcStatic;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;               // 自绘，不要系统擦除
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&wc)) {
        DWORD e = GetLastError();
        if (e != ERROR_CLASS_ALREADY_EXISTS) { LogLine(L"[win] RegisterClass 失败 %lu", e); return false; }
    }

    m_dpi = 96;
    {
        // 先用主显示器 DPI 估算初始尺寸
        HDC dc = GetDC(nullptr);
        if (dc) { m_dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); }
        if (m_dpi == 0) m_dpi = 96;
    }
    int w = MulDiv(widthDip, m_dpi, 96);
    int h = MulDiv(heightDip, m_dpi, 96);

    int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    // 保留 THICKFRAME/CAPTION 以获得系统阴影、贴边与动画；标题栏在 NCCALCSIZE 中吃掉
    // WS_CLIPCHILDREN：blit 模型的 swap chain 每帧 Present 会把 D3D 后台缓冲
    // bitblt 到整个客户区；加上该样式后 bitblt 会裁剪掉子窗口（EDIT 等 GDI 控件）
    // 所在区域，从而不再每帧覆盖输入框，配合 DWM 把 EDIT 合成在最上层 → 彻底消除频闪。
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    m_hwnd = CreateWindowExW(0, kClassName, title, style,
                             sx, sy, w, h, nullptr, nullptr, hInst, this);
    if (!m_hwnd) { LogLine(L"[win] CreateWindow 失败 %lu", GetLastError()); return false; }

    m_dpi = GetDpiForWindow(m_hwnd);
    if (m_dpi == 0) m_dpi = 96;

    // 让 DWM 画出投影（1px 顶部边即可换来完整阴影）
    MARGINS margins{ 0, 0, 1, 0 };
    DwmExtendFrameIntoClientArea(m_hwnd, &margins);

    // 圆角（Win11 有效，旧系统忽略）
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    // 触发一次 NCCALCSIZE，让新边框规则生效
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

void Window::Show(int nCmdShow)
{
    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
}

void Window::Destroy()
{
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

bool Window::Maximized() const
{
    WINDOWPLACEMENT wp{ sizeof(wp) };
    if (!m_hwnd || !GetWindowPlacement(m_hwnd, &wp)) return false;
    return wp.showCmd == SW_SHOWMAXIMIZED;
}

void Window::Minimize() { ShowWindow(m_hwnd, SW_MINIMIZE); }

void Window::ToggleMaximize()
{
    ShowWindow(m_hwnd, Maximized() ? SW_RESTORE : SW_MAXIMIZE);
}

void Window::Close() { PostMessageW(m_hwnd, WM_CLOSE, 0, 0); }

void Window::ForceClose() { PostMessageW(m_hwnd, WM_CLOSE, 0, 1); }

LRESULT CALLBACK Window::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Window* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Window::HitTest(POINT screenPt)
{
    RECT wr{};
    GetWindowRect(m_hwnd, &wr);

    const int border = (int)(8 * Scale());
    bool maxed = Maximized();

    if (!maxed) {
        bool left   = screenPt.x < wr.left + border;
        bool right  = screenPt.x >= wr.right - border;
        bool top    = screenPt.y < wr.top + border;
        bool bottom = screenPt.y >= wr.bottom - border;

        if (top && left)     return HTTOPLEFT;
        if (top && right)    return HTTOPRIGHT;
        if (bottom && left)  return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left)   return HTLEFT;
        if (right)  return HTRIGHT;
        if (top)    return HTTOP;
        if (bottom) return HTBOTTOM;
    }

    POINT cp = screenPt;
    ScreenToClient(m_hwnd, &cp);
    float dipX = cp.x / Scale();
    float dipY = cp.y / Scale();

    if (dipY < captionHeight) {
        if (isBlockedPoint && isBlockedPoint(dipX, dipY)) return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT Window::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_NCCALCSIZE:
        if (wp == TRUE) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            RECT& rc = params->rgrc[0];
            if (Maximized()) {
                // 最大化时必须手动缩进，否则内容会溢出屏幕边缘
                int fx = GetSystemMetricsForDpi(SM_CXFRAME, m_dpi)
                       + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
                int fy = GetSystemMetricsForDpi(SM_CYFRAME, m_dpi)
                       + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, m_dpi);
                rc.left += fx; rc.right -= fx;
                rc.top  += fy; rc.bottom -= fy;

                // 自动隐藏任务栏：留 1px 否则无法唤出
                APPBARDATA abd{ sizeof(abd) };
                if (SHAppBarMessage(ABM_GETSTATE, &abd) & ABS_AUTOHIDE) rc.bottom -= 1;
            }
            return 0;   // 客户区 = 整个窗口
        }
        break;

    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        return HitTest(pt);
    }

    case WM_SIZE: {
        if (wp == SIZE_MINIMIZED) break;
        UINT w = LOWORD(lp), h = HIWORD(lp);
        if (onResize) onResize(w, h, m_dpi);
        break;
    }

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wp);
        RECT* suggested = reinterpret_cast<RECT*>(lp);
        SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        RECT rc{};
        GetClientRect(m_hwnd, &rc);
        if (onResize) onResize(rc.right - rc.left, rc.bottom - rc.top, m_dpi);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = MulDiv(880, m_dpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(560, m_dpi, 96);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (!m_tracking) {
            TRACKMOUSEEVENT tme{ sizeof(tme) };
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = m_hwnd;
            TrackMouseEvent(&tme);
            m_tracking = true;
        }
        if (onInput) onInput(msg, wp, lp);
        return 0;

    case WM_MOUSELEAVE:
        m_tracking = false;
        if (onInput) onInput(msg, wp, lp);
        return 0;

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
        if (onInput) onInput(msg, wp, lp);
        if (msg == WM_SYSKEYDOWN && wp == VK_F4) break;   // 交回系统关窗
        return 0;

    case WM_NCLBUTTONDBLCLK:
        // 双击标题栏最大化
        if (wp == HTCAPTION) { ToggleMaximize(); return 0; }
        break;

    case WM_HOTKEY:
        // F-D3 全局热键：转发给 App 处理（RegisterHotKey 注册的 hwnd 收到，不会抢前台焦点）
        if (onInput) onInput(msg, wp, lp);
        return 0;

    case WM_SETCURSOR: {
        // 客户区内隐藏系统光标，改由制图仪光标接管；
        // 但所有子窗口（EDIT 标题框 / RichEdit 正文框 / 未来其它控件）需保留自身光标
        // （如正文框的 I-beam 文本光标）——否则鼠标移入编辑框后既无制图仪光标、又无 I-beam，
        // 表现为「光标消失」。这里统一放行全部子窗口，交由其 DefWindowProc 决定。
        HWND hw = (HWND)wp;
        if (hw != m_hwnd && IsChild(m_hwnd, hw))
            return DefWindowProcW(hw, msg, wp, lp);   // 子窗口自行显示 I-beam / 箭头
        if (LOWORD(lp) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
        break;
    }

    case WM_ERASEBKGND:
        return 1;   // 全部自绘，避免闪白

    case WM_CTLCOLOREDIT: {
        // 单行编辑框现在只是「隐藏的输入法 / 键盘捕获代理」（1x1），背景色无关紧要；
        // 多行日记框仍可见，用与背板一致的面色（由视图 SetEditBackdrop 设入）画实心背景，
        // 既无盖住边框的块、也避免透明背景导致的残影。
        HDC hdc = (HDC)wp;
        const D2D1_COLOR_F& bg = lj::EditBackdrop();
        COLORREF cr = RGB((BYTE)(bg.r * 255.0f), (BYTE)(bg.g * 255.0f), (BYTE)(bg.b * 255.0f));
        if (g_editBrush == nullptr || g_editBrushColor != cr) {
            if (g_editBrush) DeleteObject(g_editBrush);
            g_editBrush = CreateSolidBrush(cr);
            g_editBrushColor = cr;
        }
        SetTextColor(hdc, RgbOf(lj::AppPalette().ink900));
        SetBkColor(hdc, cr);
        return (LRESULT)g_editBrush;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        if (onIdlePaint) onIdlePaint();
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        // lp==1 为强制关闭（App 弹层「退出」/ 托盘菜单「退出」）；
        // 否则交给 App 决定：直接退 / 最小化到托盘 / 弹确认层（按设置）。
        if (lp == 0 && onCloseRequest) { onCloseRequest(); return 0; }
        m_closed = true;
        DestroyWindow(m_hwnd);
        return 0;

    default:
        // 单实例：第二个实例通过此消息请求把已运行的主窗口恢复并置前
        if (msg == g_wmBringFront) {
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        m_closed = true;
        m_hwnd = nullptr;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

} // namespace lj
