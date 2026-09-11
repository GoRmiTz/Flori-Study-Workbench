#include "ui/FieldText.h"
#include <windows.h>

namespace lj {

void PaintFieldEdit(Canvas& cv,
                    const D2D1_RECT_F& textBox,
                    const TextStyle& st,
                    const std::wstring& text,
                    const D2D1_COLOR_F& color,
                    int caret,
                    float padLeft,
                    int selStart,
                    int selEnd)
{
    // ---- 选区高亮（隐藏代理的 EM_GETSEL 在 1x1 上看不见，D3D 层补画）----
    if (selEnd > selStart && selStart >= 0 && (size_t)selEnd <= text.size()) {
        std::wstring pre = text.substr(0, (size_t)selStart);
        std::wstring sel = text.substr((size_t)selStart, (size_t)(selEnd - selStart));
        std::wstring post = text.substr((size_t)selEnd);
        D2D1_COLOR_F inv{ 1.0f - color.r, 1.0f - color.g, 1.0f - color.b, 1.0f };
        D2D1_COLOR_F selBg = WithAlpha(color, 0.30f);
        float x = textBox.left + padLeft;
        float top = textBox.top, bot = textBox.bottom;

        if (!pre.empty()) {
            cv.Text(pre, { x, top, textBox.right, bot }, st, color);
            x += cv.MeasureWidth(pre, st);
        }
        if (!sel.empty()) {
            float selW = cv.MeasureWidth(sel, st);
            cv.FillRect({ x, top, x + selW, bot }, selBg);
            cv.Text(sel, { x, top, x + selW, bot }, st, inv);
            x += selW;
        }
        if (!post.empty()) {
            cv.Text(post, { x, top, textBox.right, bot }, st, color);
            x += cv.MeasureWidth(post, st);
        }
        // 光标：只在选区尾之后（caret==selEnd 常见于拖拽到一半）
        if (caret >= 0 && caret >= selEnd) {
            float cx = textBox.left + padLeft + cv.MeasureWidth(text.substr(0, (size_t)caret), st);
            float h = bot - top;
            if (((GetTickCount() / 530) % 2) != 0) return;   // 闪烁
            cv.FillRect({ cx - 1.0f, top + h * 0.20f, cx + 1.0f, bot - h * 0.20f }, color);
        }
        return;
    }

    // ---- 无选区：文字 + 光标（原逻辑）----
    cv.Text(text, textBox, st, color);

    if (caret < 0) return;
    if (((GetTickCount() / 530) % 2) != 0) return;

    float x = textBox.left + padLeft + cv.MeasureWidth(text.substr(0, (size_t)caret), st);
    float h = textBox.bottom - textBox.top;
    float top = textBox.top + h * 0.20f;
    float bot = textBox.bottom - h * 0.20f;
    cv.FillRect({ x - 1.0f, top, x + 1.0f, bot }, color);
}

float PaintFieldEditIme(Canvas& cv,
                        const D2D1_RECT_F& textBox,
                        const TextStyle& st,
                        const std::wstring& text,
                        int caret,
                        const std::wstring& comp,
                        int compCaret,
                        const D2D1_COLOR_F& color,
                        float padLeft)
{
    int n = (int)text.size();
    if (caret < 0 || caret > n) caret = n;
    std::wstring pre = text.substr(0, (size_t)caret);
    std::wstring post = text.substr((size_t)caret);

    float top = textBox.top, bot = textBox.bottom, h = bot - top;
    float x = textBox.left + padLeft;

    if (!pre.empty()) {
        cv.Text(pre, { x, top, textBox.right, bot }, st, color);
        x += cv.MeasureWidth(pre, st);
    }

    float compX = x;
    if (!comp.empty()) {
        float cw = cv.MeasureWidth(comp, st);
        // 组合串：淡底 + 虚线下划线（Windows 输入法的通行视觉约定：未提交）
        cv.FillRect({ x, top + h * 0.16f, x + cw, bot - h * 0.16f }, WithAlpha(color, 0.08f));
        cv.Text(comp, { x, top, textBox.right, bot }, st, color);
        float uy = bot - h * 0.20f;
        for (float sx = x; sx < x + cw; sx += 6.0f) {
            float ex = sx + 3.0f; if (ex > x + cw) ex = x + cw;
            cv.FillRect({ sx, uy, ex, uy + 1.2f }, WithAlpha(color, 0.75f));
        }
        x += cw;
    }
    if (!post.empty()) {
        cv.Text(post, { x, top, textBox.right, bot }, st, color);
    }

    // 光标：有组合串时落在组合串内 compCaret 处，否则落在插入点
    float caretX = compX;
    if (!comp.empty()) {
        int cc = compCaret;
        if (cc < 0) cc = 0;
        if (cc > (int)comp.size()) cc = (int)comp.size();
        caretX = compX + cv.MeasureWidth(comp.substr(0, (size_t)cc), st);
    }
    if (((GetTickCount() / 530) % 2) == 0) {
        cv.FillRect({ caretX - 1.0f, top + h * 0.20f, caretX + 1.0f, bot - h * 0.20f }, color);
    }
    return compX;
}

bool ImeReadComposition(HWND h, std::wstring& comp, int& compCaret)
{
    comp.clear(); compCaret = 0;
    HIMC himc = ImmGetContext(h);
    if (!himc) return false;
    bool active = false;
    LONG bytes = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
    if (bytes > 0) {
        comp.resize((size_t)bytes / sizeof(wchar_t));
        ImmGetCompositionStringW(himc, GCS_COMPSTR, &comp[0], (DWORD)bytes);
        LONG cp = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
        compCaret = (cp >= 0) ? (int)cp : (int)comp.size();
        active = !comp.empty();
    }
    ImmReleaseContext(h, himc);
    return active;
}

void ImeSetCandidatePos(HWND h, float compX_client, float boxH, UINT dpi)
{
    if (!h) return;
    HIMC himc = ImmGetContext(h);
    if (!himc) return;
    float s = (float)dpi / 96.0f;
    int x = (int)(compX_client * s);
    if (x < 0) x = 0;
    RECT rc{}; GetClientRect(h, &rc);
    if (x > rc.right) x = rc.right;

    COMPOSITIONFORM cf{};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos = { x, 0 };
    ImmSetCompositionWindow(himc, &cf);

    CANDIDATEFORM caf{};
    caf.dwIndex = 0;
    caf.dwStyle = CFS_EXCLUDE;
    caf.ptCurrentPos = { x, rc.bottom };
    caf.rcArea = rc;
    ImmSetCandidateWindow(himc, &caf);
    ImmReleaseContext(h, himc);
}

void ImeCancelComposition(HWND h)
{
    if (!h) return;
    HIMC himc = ImmGetContext(h);
    if (!himc) return;
    ImmNotifyIME(himc, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
    ImmReleaseContext(h, himc);
}

std::wstring ReadEditBuffer(HWND h)
{
    if (!h) return L"";
    int n = GetWindowTextLengthW(h);
    std::wstring t; t.resize((size_t)n + 1);
    GetWindowTextW(h, &t[0], n + 1);
    t.resize((size_t)n);
    return t;
}

int EditCaretPos(HWND h)
{
    DWORD a = 0, b = 0;
    SendMessageW(h, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
    return (int)a;
}

void EditSelRange(HWND h, int& a, int& b)
{
    DWORD s = 0, e = 0;
    if (h) SendMessageW(h, EM_GETSEL, (WPARAM)&s, (LPARAM)&e);
    a = (int)s; b = (int)e;
}

// 由 x 坐标换算字符位置：二分逼近「MeasureWidth(前缀) <= x - textLeft」的最大前缀长度
int CharIndexAtX(Canvas& cv, const std::wstring& text, const TextStyle& st,
                 float textLeft, float x)
{
    int n = (int)text.size();
    if (n <= 0) return 0;
    float target = x - textLeft;
    if (target <= 0.0f) return 0;
    if (cv.MeasureWidth(text, st) <= target) return n;
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (cv.MeasureWidth(text.substr(0, (size_t)mid), st) <= target) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

} // namespace lj
