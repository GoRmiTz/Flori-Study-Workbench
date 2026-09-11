#include "ui/FieldEdit.h"
#include "core/Hwnd.h"
#include <imm.h>

namespace lj {

FieldEdit::~FieldEdit()
{
    if (m_edit && IsWindow(m_edit)) DestroyWindow(m_edit);
    m_edit = nullptr;
}

// ============================================================
//  代理 WndProc —— 全视图共享一份键位 / IME / 光标逻辑
// ============================================================
LRESULT CALLBACK FieldEdit::Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    FieldEdit* self = (FieldEdit*)GetWindowLongPtrW(w, GWLP_USERDATA);
    if (self) {
        switch (msg) {
        case WM_KEYDOWN:
            // 组合中回车/ESC 属于输入法（选字），不能当成快捷键
            if (self->m_imeComp.empty()) {
                if (wp == VK_RETURN) { if (self->onEnter) self->onEnter(); return 0; }
                if (wp == VK_ESCAPE) { if (self->onEsc)   self->onEsc();   return 0; }
            }
            break;
        case WM_SETFOCUS: {
            int len = GetWindowTextLengthW(w);
            SendMessageW(w, EM_SETSEL, (WPARAM)len, (LPARAM)len);   // 焦点时光标置文末
            break;
        }
        case WM_KILLFOCUS:
            if (self->m_active && self->onKillFocus) self->onKillFocus();
            break;
        case WM_IME_STARTCOMPOSITION:
            self->m_imeComp.clear(); self->m_imeCompCaret = 0;
            break;
        case WM_IME_COMPOSITION:
            if (!ImeReadComposition(w, self->m_imeComp, self->m_imeCompCaret)) {
                self->m_imeComp.clear(); self->m_imeCompCaret = 0;
            }
            break;
        case WM_IME_ENDCOMPOSITION:
            self->m_imeComp.clear(); self->m_imeCompCaret = 0;
            break;
        }
    }
    WNDPROC old = self ? self->m_old : nullptr;
    LRESULT r = old ? CallWindowProcW(old, w, msg, wp, lp) : DefWindowProcW(w, msg, wp, lp);
    // 压掉代理的原生光标（我们用 D3D 自绘光标）
    if (msg == WM_SETFOCUS || msg == WM_KEYDOWN || msg == WM_CHAR ||
        msg == WM_IME_STARTCOMPOSITION || msg == WM_IME_COMPOSITION || msg == WM_IME_CHAR)
        HideCaret(w);
    return r;
}

// ============================================================
//  生命周期
// ============================================================
void FieldEdit::Ensure()
{
    if (m_edit) return;
    HWND parent = AppHwnd();
    if (!parent) return;
    // 1×1 + WS_EX_TRANSPARENT：鼠标命中直接穿透到主窗口，绝不遮挡字段；
    // EDIT 只作为「键盘 + IME 的焦点载体」存在，白块被物理消除。
    m_edit = CreateWindowExW(WS_EX_TRANSPARENT, L"EDIT", L"",
                             WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
                             0, 0, 1, 1, parent, nullptr,
                             (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
    if (!m_edit) return;
    m_old = (WNDPROC)SetWindowLongPtrW(m_edit, GWLP_WNDPROC, (LONG_PTR)Proc);
    SetWindowLongPtrW(m_edit, GWLP_USERDATA, (LONG_PTR)this);
    ShowWindow(m_edit, SW_HIDE);
}

void FieldEdit::Begin(const std::wstring& text, bool password, float fontDip)
{
    Ensure();
    if (!m_edit) return;
    m_password = password;
    if (fontDip > 0.0f) m_fontDip = fontDip;
    SetWindowTextW(m_edit, text.c_str());
    SendMessageW(m_edit, EM_SETPASSWORDCHAR, password ? (WPARAM)L'\u2022' : 0, 0);
    m_imeComp.clear(); m_imeCompCaret = 0;
    m_dragAnchor = -1; m_dragging = false;
    m_active = true;
    m_placedX = m_placedY = -1;    // 强制重摆一次
    SendMessageW(m_edit, WM_SETREDRAW, FALSE, 0);
    ShowWindow(m_edit, SW_SHOW);
    SetFocus(m_edit);
    int len = GetWindowTextLengthW(m_edit);
    SendMessageW(m_edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);   // 光标置于文末
    // IME 组合字号跟随界面（候选窗/组合窗不脱节）
    if (HIMC himc = ImmGetContext(m_edit)) {
        LOGFONTW lf{};
        lf.lfHeight = -(LONG)(m_fontDip * (float)AppDpi() / 96.0f);
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei UI");
        ImmSetCompositionFontW(himc, &lf);
        ImmReleaseContext(m_edit, himc);
    }
}

void FieldEdit::End(bool commit, std::wstring& out)
{
    out.clear();
    if (!m_active || !m_edit) { m_active = false; return; }
    if (commit) out = ReadEditBuffer(m_edit);
    ImeCancelComposition(m_edit);
    m_imeComp.clear(); m_imeCompCaret = 0;
    m_active = false;   // 先置否：隐藏引发的 KILLFOCUS 不再回调（防递归）
    SendMessageW(m_edit, WM_SETREDRAW, TRUE, 0);
    ShowWindow(m_edit, SW_HIDE);
}

void FieldEdit::Cancel()
{
    std::wstring dummy;
    End(false, dummy);
}

std::wstring FieldEdit::Text() const
{
    return ReadEditBuffer(m_edit);
}

// ============================================================
//  每帧：绘制 + 代理摆放
// ============================================================
void FieldEdit::PlaceProxy(float xDip, float yDip, float scrollY)
{
    if (!m_edit) return;
    float s = (float)AppDpi() / 96.0f;
    int px = (int)(xDip * s);
    int py = (int)((yDip - scrollY) * s);
    if (px == m_placedX && py == m_placedY) return;
    SetWindowPos(m_edit, nullptr, px, py, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE);
    m_placedX = px; m_placedY = py;
}

void FieldEdit::Paint(Canvas& cv, const D2D1_RECT_F& box, const TextStyle& st,
                      const D2D1_COLOR_F& color, const std::wstring& placeholder,
                      const D2D1_COLOR_F& phColor, float padLeft, float scrollY)
{
    if (!m_active) return;
    std::wstring buf = ReadEditBuffer(m_edit);
    std::wstring shown = m_password ? std::wstring(buf.size(), L'\u2022') : buf;
    int caret = EditCaretPos(m_edit);
    if (caret < 0 || caret > (int)shown.size()) caret = (int)shown.size();

    if (!m_imeComp.empty()) {
        // 组合串画在插入点（淡底 + 虚线下划线），代理/候选窗锚定组合串起点
        float compX = PaintFieldEditIme(cv, box, st, shown, caret,
                                        m_imeComp, m_imeCompCaret, color, padLeft);
        PlaceProxy(compX, (box.top + box.bottom) * 0.5f, scrollY);
        ImeSetCandidatePos(m_edit, 0.0f, box.bottom - box.top, AppDpi());
        return;
    }

    int sa = -1, sb = -1;
    EditSelRange(m_edit, sa, sb);
    if (shown.empty() && !placeholder.empty()) {
        // 空缓冲：画占位文本，光标停在起点
        PaintFieldEdit(cv, box, st, placeholder, phColor, caret, padLeft, -1, -1);
    } else {
        PaintFieldEdit(cv, box, st, shown, color, caret, padLeft, sa, sb);
    }
    float cx = box.left + padLeft +
        (shown.empty() ? 0.0f : cv.MeasureWidth(shown.substr(0, (size_t)caret), st));
    PlaceProxy(cx, (box.top + box.bottom) * 0.5f, scrollY);
}

// ============================================================
//  每帧：鼠标 → 光标定位 / 拖拽框选
// ============================================================
bool FieldEdit::HandleMouse(const Input& in, Canvas& cv, const D2D1_RECT_F& box,
                            const TextStyle& st, float scrollY, float padLeft)
{
    if (!m_active) return false;
    float mx = in.mouseX;
    float my = in.mouseY + scrollY;    // 客户区 → 内容坐标
    bool inBox = mx >= box.left && mx <= box.right && my >= box.top && my <= box.bottom;
    if (!inBox && !m_dragging) return false;

    std::wstring buf = ReadEditBuffer(m_edit);
    std::wstring shown = m_password ? std::wstring(buf.size(), L'\u2022') : buf;

    if (in.pressed) {
        if (!inBox) { m_dragging = false; return false; }
        int i = CharIndexAtX(cv, shown, st, box.left + padLeft, mx);
        m_dragAnchor = i;
        m_dragging = true;
        SendMessageW(m_edit, EM_SETSEL, (WPARAM)i, (LPARAM)i);
        return true;
    }
    if (m_dragging && in.down) {
        // 拖出框外时夹取到边界，选择继续延伸到首/尾
        float cx = (std::min)((std::max)(mx, box.left), box.right);
        int i = CharIndexAtX(cv, shown, st, box.left + padLeft, cx);
        int a = (std::min)(i, m_dragAnchor);
        int b = (std::max)(i, m_dragAnchor);
        SendMessageW(m_edit, EM_SETSEL, (WPARAM)a, (LPARAM)b);
        return inBox;
    }
    if (in.released) m_dragging = false;
    return inBox;
}

} // namespace lj
