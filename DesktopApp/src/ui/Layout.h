#pragma once
// ============================================================
//  Layout.h — 轻量流式 / 约束布局辅助（P1-9 基础设施）
// ------------------------------------------------------------
//  背景：之前每个视图的 Layout() 与 Paint() 各自手算高度/坐标，
//  公式一改就错位，重叠 bug 反复复发。本文件提供一组零依赖的
//  流式布局原语，让「卡片高度」「行内排布」只在一处计算、多处复用，
//  从根本上消除「改内容必重算 + Layout/Paint 不一致」。
//
//  全为头文件内联实现，不新增 .cpp，不改动 CMake。
//  坐标单位均为 DIP（逻辑像素）。
// ============================================================
#include <d2d1_1.h>
#include <vector>
#include <algorithm>

namespace lj {
namespace ui {

// ---------------- 矩形工具 ----------------
inline float RectW(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float RectH(const D2D1_RECT_F& r) { return r.bottom - r.top; }

inline D2D1_RECT_F MakeRect(float l, float t, float w, float h)
{
    return D2D1_RECT_F{ l, t, l + w, t + h };
}

inline D2D1_RECT_F InsetRect(const D2D1_RECT_F& r, float dx, float dy)
{
    return D2D1_RECT_F{ r.left + dx, r.top + dy, r.right - dx, r.bottom - dy };
}

inline bool RectsOverlap(const D2D1_RECT_F& a, const D2D1_RECT_F& b)
{
    return a.left < b.right && b.left < a.right &&
           a.top  < b.bottom && b.top  < a.bottom;
}

// ---------------- 纵向流式布局 ----------------
//  从 y 起向下堆叠定高块，自动累计光标并给出总高。
//  用法：VLayout v(x, y, width, gap); auto r = v.block(h);
struct VLayout
{
    float x = 0.0f;
    float cursorY = 0.0f;
    float width = 0.0f;
    float gap = 12.0f;
    float startY = 0.0f;

    VLayout() = default;
    VLayout(float x_, float y_, float w_, float gap_ = 12.0f)
        : x(x_), cursorY(y_), width(w_), gap(gap_), startY(y_) {}

    // 预定一块高度 h 的区域，返回其 rect，光标下移
    D2D1_RECT_F block(float h)
    {
        D2D1_RECT_F r{ x, cursorY, x + width, cursorY + h };
        cursorY += h + gap;
        return r;
    }
    // 最后一块的底边（去掉末尾 gap）
    float bottom() const { return cursorY - gap; }
    // 自 startY 起累计的内容高度
    float total() const { return (cursorY - gap) - startY; }
};

// ---------------- 横向流式布局（左→右，可换行）----------------
//  在一行内从左到右排定宽块；若超出 maxRight 且已排过至少一块，则换行。
//  用法：HLayout h(x, y, lineH, gap); h.start(x); auto r = h.pack(w, maxRight);
struct HLayout
{
    float x = 0.0f;
    float y = 0.0f;
    float height = 0.0f;
    float gap = 8.0f;
    float startX = 0.0f;

    HLayout() = default;
    HLayout(float x_, float y_, float h_, float gap_ = 8.0f)
        : x(x_), y(y_), height(h_), gap(gap_), startX(x_) {}

    void start(float x_) { x = x_; startX = x_; }

    D2D1_RECT_F pack(float w, float maxRight)
    {
        if (x + w > maxRight && x > startX) {     // 换行
            x = startX;
            y += height + gap;
        }
        D2D1_RECT_F r{ x, y, x + w, y + height };
        x += w + gap;
        return r;
    }
    // 当前光标到右边界（maxRight）的剩余可用宽度
    float remaining(float maxRight) const { return (std::max)(0.0f, maxRight - x); }
};

// ---------------- 右对齐打包（行尾动作簇）----------------
//  从右边界向左逐个排定宽块，返回 rect 并把光标左移（含 gap）。
//  用法：float cr = right; auto r = PackRight(cr, y, h, w, gap);
inline D2D1_RECT_F PackRight(float& cursorRight, float y, float h, float w, float gap)
{
    cursorRight -= w;
    D2D1_RECT_F r{ cursorRight, y, cursorRight + w, y + h };
    cursorRight -= gap;
    return r;
}

} // namespace ui
} // namespace lj
