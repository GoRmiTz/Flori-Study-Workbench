#pragma once
// ============================================================
//  Canvas.h — DOSSIER 绘图原语
//  把「硬边直角 / 双线框 / 四角裁切标记 / 骑缝虚线 / 朱砂印」
//  这些视觉母题封装成可复用调用，视图层只管排版不管画法。
//  坐标一律使用 DIP（逻辑像素），由 D2D 的 DPI 设置负责缩放。
// ============================================================
#include "gfx/Graphics.h"
#include "ui/Theme.h"
#include <unordered_map>

namespace lj {

enum class FontRole { Serif, Sans, Mono };
enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

struct TextStyle
{
    FontRole role = FontRole::Sans;
    float size = 14.0f;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    float letterSpacing = 0.0f;   // 额外字距（DIP）
    HAlign hAlign = HAlign::Left;
    VAlign vAlign = VAlign::Top;
    bool   tabularNums = false;   // 等宽数字，防止倒计时跳动
};

class Canvas
{
public:
    bool Init(Graphics& gfx);
    void Shutdown();

    void BeginFrame(const Palette& pal) { m_pal = &pal; }
    const Palette& Pal() const { return *m_pal; }

    ID2D1DeviceContext* DC() const { return m_dc; }
    Graphics& Gfx() const { return *m_gfx; }

    // ---------- 资源缓存 ----------
    ID2D1SolidColorBrush* Brush(const D2D1_COLOR_F& c);
    IDWriteTextFormat*    Format(const TextStyle& st);

    // ---------- 基本形 ----------
    void FillRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    void FillRoundRect(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c);
    void StrokeRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float w = shape::kHair);
    void StrokeRoundRect(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c, float w = shape::kHair);
    void Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float w = shape::kHair);
    void DashedLine(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float w = shape::kHair);
    void FillCircle(float cx, float cy, float r, const D2D1_COLOR_F& c);
    void StrokeCircle(float cx, float cy, float r, const D2D1_COLOR_F& c, float w = shape::kHair);

    // ---------- DOSSIER 母题 ----------
    // 双线框：外框 + 内框，档案封套感
    void DoubleFrame(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float gap = 3.0f, float w = shape::kHair);
    // 四角裁切标记：只画四个角，悬停时显现
    void CornerTicks(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float len = 10.0f,
                     float w = shape::kStroke, float inset = 0.0f);
    // 骑缝虚线（水平）
    void PerforationH(float x1, float x2, float y, const D2D1_COLOR_F& c);
    // 编号签：小色块 + 等宽数字
    void NumberTag(const D2D1_RECT_F& r, const wchar_t* text,
                   const D2D1_COLOR_F& bg, const D2D1_COLOR_F& fg, float fontSize = 11.0f);
    // 朱砂印章：方框 + 文字 + 轻微旋转
    void SealStamp(float cx, float cy, float size, const wchar_t* text, float angleDeg, float alpha);
    // 进度环
    void ProgressRing(float cx, float cy, float radius, float thickness, float progress,
                      const D2D1_COLOR_F& track, const D2D1_COLOR_F& fill);
    // 卡片底：纸面 + 细描边（+ 可选阴影感的底部重线）
    void PaperCard(const D2D1_RECT_F& r, float lift, float radius = shape::kEdge);

    // ---------- 文字 ----------
    void Text(const std::wstring& s, const D2D1_RECT_F& box, const TextStyle& st, const D2D1_COLOR_F& c);
    float MeasureWidth(const std::wstring& s, const TextStyle& st);
    float MeasureHeight(const std::wstring& s, const TextStyle& st, float maxWidth);
    // 逐字揭示：progress 为整体进度，每字按索引错开；用于大标题入场
    void CharsReveal(const std::wstring& s, float x, float baselineTop, const TextStyle& st,
                     const D2D1_COLOR_F& c, float progress, float perChar = 0.045f,
                     float riseDip = 14.0f);

    // ---------- 变换 / 裁剪 / 透明 ----------
    void PushClip(const D2D1_RECT_F& r);
    void PopClip();
    void PushOpacity(float alpha);
    void PopOpacity();
    void PushTransform(const D2D1_MATRIX_3X2_F& m);   // 相对叠加
    void PopTransform();

private:
    struct FormatKey
    {
        int role; int size10; int weight; int tabular;
        bool operator==(const FormatKey& o) const
        { return role == o.role && size10 == o.size10 && weight == o.weight && tabular == o.tabular; }
    };
    struct FormatKeyHash
    {
        size_t operator()(const FormatKey& k) const
        { return (size_t)k.role * 73856093u ^ (size_t)k.size10 * 19349663u
               ^ (size_t)k.weight * 83492791u ^ (size_t)k.tabular * 2654435761u; }
    };

    const wchar_t* FamilyFor(FontRole r) const;
    bool FamilyExists(const wchar_t* name);

    Graphics* m_gfx = nullptr;
    ID2D1DeviceContext* m_dc = nullptr;
    const Palette* m_pal = nullptr;

    std::unordered_map<uint32_t, ComPtr<ID2D1SolidColorBrush>> m_brushes;
    std::unordered_map<FormatKey, ComPtr<IDWriteTextFormat>, FormatKeyHash> m_formats;
    ComPtr<ID2D1StrokeStyle> m_dashStyle;
    ComPtr<ID2D1StrokeStyle> m_perfStyle;
    ComPtr<IDWriteFontCollection> m_sysFonts;

    std::wstring m_serif, m_sans, m_mono;
    std::vector<D2D1_MATRIX_3X2_F> m_xformStack;
    int m_clipDepth = 0;
    int m_layerDepth = 0;
};

} // namespace lj
