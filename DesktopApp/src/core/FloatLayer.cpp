// ============================================================
//  FloatLayer.cpp — F-D2 常驻浮层（桌面护城河）
//  独立 topmost / no-activate 工具窗口，GDI 绘制，角落常驻。
// ============================================================
#include "core/FloatLayer.h"
#include "core/Common.h"
#include "core/TrayIcon.h"
#include "core/FocusTracker.h"
#include "app/Store.h"
#include "app/Data.h"
#include <algorithm>
#include <climits>
#include <ole2.h>     // F-D5 拖放：RegisterDragDrop / RevokeDragDrop
#include <oleidl.h>   // IDropTarget
#include <shlobj.h>   // CF_HDROP / DragQueryFileW
#include <ctime>      // time()

namespace lj {

int g_studyRoomOnline = 0;

namespace {
    // 逻辑尺寸（像素，乘 m_scale 后为实际像素）
    const int kW = 250;
    const int kH = 160;
    const int kPad = 14;
    const int kTitleH = 22;
    const int kRowH = 26;

    // 配色（酒红档案室基调，常驻 HUD 固定深色，跨主题一致可读）
    const COLORREF kBg      = RGB(38, 17, 23);
    const COLORREF kBorder  = RGB(107, 42, 53);   // 印章红
    const COLORREF kDivider = RGB(70, 40, 48);
    const COLORREF kTitle   = RGB(214, 158, 96);  // 黄铜
    const COLORREF kLabel   = RGB(196, 184, 176); // 暗浅
    const COLORREF kValue   = RGB(242, 230, 216); // 亮浅
    const COLORREF kAccent  = RGB(224, 150, 96);  // 数值黄铜

    static const wchar_t* kClassName = L"FloriFloatLayer";
}

// ---------------- F-D5：跨窗口拖入收集考点（OLE IDropTarget）----------------
namespace {
class DropTarget : public IDropTarget
{
public:
    DropTarget() : m_ref(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pdo, DWORD, POINTL, DWORD* pe) override
    {
        m_canDrop = Acceptable(pdo);
        *pe = m_canDrop ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* pe) override
    {
        *pe = m_canDrop ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        m_canDrop = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pdo, DWORD, POINTL, DWORD* pe) override
    {
        std::wstring text = GetText(pdo);
        if (text.empty()) text = GetFiles(pdo);
        m_canDrop = false;
        if (!text.empty()) {
            KCard c;
            c.id     = GenId();
            c.title  = FirstLine(text);
            c.body   = Trim(text);
            c.source = L"跨窗口拖入";
            c.ts     = (long long)time(nullptr);
            CheckinStore::Instance().AddKnowledge(c);
            TrayIcon::Instance().Balloon(L"已收集考点", c.title.c_str());
            *pe = DROPEFFECT_COPY;
        } else {
            *pe = DROPEFFECT_NONE;
        }
        return S_OK;
    }

private:
    ULONG m_ref = 1;
    bool  m_canDrop = false;

    static bool HasFormat(IDataObject* pdo, CLIPFORMAT fmt)
    {
        FORMATETC fe{ fmt, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        return pdo->QueryGetData(&fe) == S_OK;
    }
    bool Acceptable(IDataObject* pdo)
    {
        return HasFormat(pdo, CF_UNICODETEXT) || HasFormat(pdo, CF_HDROP);
    }
    static std::wstring GetText(IDataObject* pdo)
    {
        FORMATETC fe{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM sm{};
        if (SUCCEEDED(pdo->GetData(&fe, &sm)) && sm.tymed == TYMED_HGLOBAL && sm.hGlobal) {
            wchar_t* p = (wchar_t*)GlobalLock(sm.hGlobal);
            std::wstring s = p ? p : L"";
            GlobalUnlock(sm.hGlobal);
            ReleaseStgMedium(&sm);
            return s;
        }
        FORMATETC ft{ CF_TEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        if (SUCCEEDED(pdo->QueryGetData(&ft)) && SUCCEEDED(pdo->GetData(&ft, &sm)) &&
            sm.tymed == TYMED_HGLOBAL && sm.hGlobal) {
            char* p = (char*)GlobalLock(sm.hGlobal);
            int n = p ? (int)strlen(p) : 0;
            std::wstring s(n, L' ');
            for (int i = 0; i < n; ++i) s[i] = (wchar_t)(unsigned char)p[i];
            GlobalUnlock(sm.hGlobal);
            ReleaseStgMedium(&sm);
            return s;
        }
        return L"";
    }
    static std::wstring GetFiles(IDataObject* pdo)
    {
        FORMATETC fe{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM sm{};
        if (SUCCEEDED(pdo->QueryGetData(&fe)) && SUCCEEDED(pdo->GetData(&fe, &sm)) && sm.hGlobal) {
            HDROP h = (HDROP)sm.hGlobal;
            UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
            std::wstring out;
            for (UINT i = 0; i < n; ++i) {
                wchar_t buf[MAX_PATH + 1] = { 0 };
                if (DragQueryFileW(h, i, buf, MAX_PATH + 1) > 0) {
                    out += buf;
                    if (i + 1 < n) out += L"\n";
                }
            }
            ReleaseStgMedium(&sm);
            return out;
        }
        return L"";
    }
    static std::wstring GenId()
    {
        static long long s_seq = 0;
        return L"kc_" + std::to_wstring((long long)time(nullptr)) + L"_" + std::to_wstring(++s_seq);
    }
    static std::wstring FirstLine(const std::wstring& t)
    {
        size_t a = 0;
        while (a < t.size() && (t[a] == L'\r' || t[a] == L'\n' || t[a] == L' ' || t[a] == L'\t')) a++;
        size_t b = a;
        while (b < t.size() && t[b] != L'\r' && t[b] != L'\n') b++;
        std::wstring s = t.substr(a, b - a);
        if (s.size() > 48) s = s.substr(0, 48) + L"…";
        if (s.empty()) s = L"未命名考点";
        return s;
    }
    static std::wstring Trim(const std::wstring& t)
    {
        size_t a = 0, b = t.size();
        while (a < b && (t[a] == L'\r' || t[a] == L'\n' || t[a] == L' ' || t[a] == L'\t')) a++;
        while (b > a && (t[b - 1] == L'\r' || t[b - 1] == L'\n' || t[b - 1] == L' ' || t[b - 1] == L'\t')) b--;
        return t.substr(a, b - a);
    }
};
} // namespace

FloatLayer& FloatLayer::Instance()
{
    static FloatLayer s;
    return s;
}

bool FloatLayer::Create(HWND owner, HINSTANCE hInst, float scale)
{
    if (m_hwnd) return true;
    m_owner = owner;
    m_scale = (scale > 0.1f) ? scale : 1.0f;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &FloatLayer::WndProcStatic;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);   // 重复注册仅返回已有原子，安全

    int w = (int)(kW * m_scale + 0.5f);
    int h = (int)(kH * m_scale + 0.5f);
    // 注意：不能用 WS_EX_LAYERED —— 分层窗口下 AnimateWindow(AW_BLEND) 是 no-op（MSDN：
    // 分层窗口忽略 AW_BLEND）；且未调用 SetLayeredWindowAttributes/UpdateLayeredWindow 时
    // 分层窗口默认全透明，导致悬停浮层「永远不出现」。去掉分层样式后，普通 WS_POPUP +
    // AnimateWindow(AW_BLEND) 才能正常淡入/淡出，GDI 面板也正常可见。
    m_hwnd = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kClassName, L"", WS_POPUP,
        -1000, -1000, w, h, nullptr, nullptr, hInst, this);
    if (!m_hwnd) { LogLine(L"[float] CreateWindowEx 失败 GetLastError=%lu", GetLastError()); return false; }
    LogLine(L"[float] CreateWindowEx OK hwnd=%p", (void*)m_hwnd);

    PositionToCorner();
    m_visible = false;   // 默认隐藏：由看板娘悬停（ShowWithFade）驱动显示
    Recompute();
    ShowWindow(m_hwnd, SW_HIDE);
    m_timer = (UINT)SetTimer(m_hwnd, 1, 1000, nullptr);
    // F-D5：跨窗口拖入收集考点（OLE 拖放；主线程已 CoInitializeEx(APARTMENTTHREADED)）
    m_drop = new DropTarget();
    RegisterDragDrop(m_hwnd, m_drop);
    return true;
}

void FloatLayer::Destroy()
{
    if (m_drop) { RevokeDragDrop(m_hwnd); m_drop->Release(); m_drop = nullptr; }
    if (m_timer && m_hwnd) { KillTimer(m_hwnd, m_timer); m_timer = 0; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
    m_visible = false;
}

void FloatLayer::Show()
{
    if (!m_hwnd) return;
    m_visible = true;
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    PositionToCorner();
    Recompute();
}

void FloatLayer::Hide()
{
    if (!m_hwnd) return;
    m_visible = false;
    ShowWindow(m_hwnd, SW_HIDE);
}

void FloatLayer::Toggle()
{
    if (m_visible) Hide(); else Show();
}

void FloatLayer::ShowWithFade()
{
    if (!m_hwnd) return;
    PositionToCorner();
    Recompute();
    ShowWindow(m_hwnd, SW_HIDE);            // 先隐藏，确保 AW_BLEND 从 0 alpha 渐入
    // 真·alpha 浮现（约 180ms）；失败则退化为直接显示
    if (!AnimateWindow(m_hwnd, 180, AW_BLEND))
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    m_visible = true;
}

void FloatLayer::HideWithFade()
{
    if (!m_hwnd) return;
    // 真·alpha 淡出（约 160ms）后隐藏；失败则直接隐藏
    if (!AnimateWindow(m_hwnd, 160, AW_BLEND | AW_HIDE))
        ShowWindow(m_hwnd, SW_HIDE);
    m_visible = false;
}

void FloatLayer::PositionToCorner()
{
    if (!m_hwnd) return;
    MONITORINFO mi{}; mi.cbSize = sizeof(mi);
    HMONITOR mon = m_owner ? MonitorFromWindow(m_owner, MONITOR_DEFAULTTONEAREST)
                           : MonitorFromWindow(nullptr, MONITOR_DEFAULTTOPRIMARY);
    RECT work = { 0, 0, 0, 0 };
    if (mon && GetMonitorInfoW(mon, &mi)) work = mi.rcWork;
    else if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int w = (int)(kW * m_scale + 0.5f);
    int h = (int)(kH * m_scale + 0.5f);
    int m = (int)(16 * m_scale + 0.5f);
    int x = work.right  - w - m;
    int y = work.bottom - h - m;
    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK FloatLayer::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    FloatLayer* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<FloatLayer*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;   // 须在 WM_NCCREATE 内记录 hwnd，否则随后 WndProc 落到
                               // DefWindowProcW(m_hwnd=nullptr) 令 CreateWindowExW 返回 1400
    } else {
        self = reinterpret_cast<FloatLayer*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT FloatLayer::WndProc(UINT msg, WPARAM wp, LPARAM lp)
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
        Recompute();
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEACTIVATE:
        // 无论如何都不抢焦点：点击落在浮层上，前台窗口保持不变
        return MA_NOACTIVATE;
    case WM_LBUTTONUP:
        // 显式用户动作：恢复/显示主窗口（RestoreFromTray 内部会 SetForegroundWindow）
        TrayIcon::Instance().RestoreFromTray();
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

void FloatLayer::Recompute()
{
    // 每秒重算「当前连续」（内存读取，廉价）；每 5 秒重算文件类数据（今日专注 / 倒计时）
    bool heavy = (m_tick % 5 == 0);
    m_tick++;

    // ---- 当前连续专注（实时）----
    {
        auto st = FocusTracker::Instance().Snapshot();
        if (st.studying && st.continuousSec > 0) {
            if (st.continuousSec >= 60)
                m_contLine = std::to_wstring(st.continuousSec / 60) + L" 分钟";
            else
                m_contLine = std::to_wstring(st.continuousSec) + L" 秒";
        } else {
            m_contLine = L"休息中";
        }
    }

    if (heavy) {
        // ---- 今日专注（求和当日学习分钟）----
        auto focus = CheckinStore::Instance().LoadFocus();
        std::wstring todayKey = FormatDate(Today());
        int total = 0;
        for (auto& f : focus) {
            bool study = (f.category == 0 || f.category == 1);
            if (study && f.date == todayKey) total += f.min;
        }
        if (total >= 60)
            m_focusLine = std::to_wstring(total / 60) + L"h " + std::to_wstring(total % 60) + L"m";
        else
            m_focusLine = std::to_wstring(total) + L" 分钟";

        // ---- 目标倒计时（优先最近「未来」里程碑；全过期则取最近过去）----
        auto ms = CheckinStore::Instance().LoadMilestones();
        int bestDays = INT_MAX;
        std::wstring bestLabel;
        bool found = false;
        for (auto& m : ms) {
            int d = DaysUntil(m.date);
            if (d < 0) continue;             // 跳过已过期
            if (d < bestDays) { bestDays = d; bestLabel = m.label; found = true; }
        }
        if (!found && !ms.empty()) {         // 全部过期 → 取最接近今天的（最大仍为负）
            int bestPast = INT_MIN;
            for (auto& m : ms) {
                int d = DaysUntil(m.date);
                if (d > bestPast) { bestPast = d; bestLabel = m.label; bestDays = d; }
            }
        }
        if (bestLabel.empty()) {
            m_countLine = L"无目标";
        } else {
            if (bestLabel.size() > 6) bestLabel = bestLabel.substr(0, 6);
            m_countLine = bestLabel + L" " + std::to_wstring(bestDays) + L" 天";
        }
    }

    // ---- 自习室在线（来自 RoomView 全局）----
    if (g_studyRoomOnline > 0)
        m_roomLine = L"在线 " + std::to_wstring(g_studyRoomOnline) + L" 人";
    else
        m_roomLine = L"未进入";
}

void FloatLayer::Paint(HDC hdc)
{
    RECT rc{}; GetClientRect(m_hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    // 背景圆角面板
    HBRUSH hBg = CreateSolidBrush(kBg);
    HPEN   hPen = CreatePen(PS_SOLID, (int)(1 * m_scale + 0.5f), kBorder);
    HBRUSH hOld = (HBRUSH)SelectObject(hdc, hBg);
    HPEN   pOld = (HPEN)SelectObject(hdc, hPen);
    FillRect(hdc, &rc, hBg);   // 非分层窗口：先铺满底色，圆角外的四角才不会透出默认背景
    int r = (int)(14 * m_scale + 0.5f);
    RoundRect(hdc, 0, 0, W - 1, H - 1, r, r);
    SelectObject(hdc, hOld); DeleteObject(hBg);
    // 分隔线
    HPEN hDiv = CreatePen(PS_SOLID, 1, kDivider);
    SelectObject(hdc, hDiv);
    int yDiv = (int)((kPad + kTitleH) * m_scale + 0.5f);
    MoveToEx(hdc, (int)(kPad * m_scale + 0.5f), yDiv, nullptr);
    LineTo(hdc, W - (int)(kPad * m_scale + 0.5f), yDiv);
    SelectObject(hdc, pOld); DeleteObject(hPen); DeleteObject(hDiv);

    // 字体
    int titleSz = (int)(12 * m_scale + 0.5f);
    int rowSz   = (int)(13 * m_scale + 0.5f);
    HFONT hTitle = CreateFontW(-titleSz, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Microsoft YaHei UI");
    HFONT hRow = CreateFontW(-rowSz, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH, L"Microsoft YaHei UI");

    SetBkMode(hdc, TRANSPARENT);

    // 标题
    SelectObject(hdc, hTitle);
    SetTextColor(hdc, kTitle);
    RECT tRc{ (int)(kPad*m_scale+0.5f), (int)(kPad*m_scale+0.5f), W, yDiv };
    DrawTextW(hdc, L"芙洛理 · 专注陪伴", -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // 四行
    const wchar_t* labels[4] = { L"今日专注", L"当前连续", L"目标倒计时", L"自习室" };
    std::wstring  values[4]  = { m_focusLine, m_contLine, m_countLine, m_roomLine };
    int y0 = (int)((kPad + kTitleH + 8) * m_scale + 0.5f);
    for (int i = 0; i < 4; ++i) {
        RECT lr{ (int)(kPad*m_scale+0.5f), y0 + i*kRowH*(int)m_scale,
                 (int)(W*0.42f), y0 + (i+1)*kRowH*(int)m_scale };
        RECT vr{ (int)(W*0.40f), y0 + i*kRowH*(int)m_scale,
                 W - (int)(kPad*m_scale+0.5f), y0 + (i+1)*kRowH*(int)m_scale };
        SelectObject(hdc, hRow);
        SetTextColor(hdc, kLabel);
        DrawTextW(hdc, labels[i], -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hdc, (i == 3 && g_studyRoomOnline == 0) ? kLabel : kValue);
        DrawTextW(hdc, values[i].c_str(), -1, &vr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    DeleteObject(hTitle); DeleteObject(hRow);
}

} // namespace lj
