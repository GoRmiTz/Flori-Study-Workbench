#include "ui/Canvas.h"
#include "ui/Motion.h"
#include <d2d1_1helper.h>

namespace lj {

static uint32_t ColorKey(const D2D1_COLOR_F& c)
{
    auto q = [](float v) { return (uint32_t)(Clamp01(v) * 255.0f + 0.5f); };
    return (q(c.a) << 24) | (q(c.r) << 16) | (q(c.g) << 8) | q(c.b);
}

bool Canvas::Init(Graphics& gfx)
{
    m_gfx = &gfx;
    m_dc = gfx.D2D();
    if (!m_dc) return false;

    gfx.DWrite()->GetSystemFontCollection(&m_sysFonts, FALSE);

    auto pick = [&](const wchar_t* const* list, size_t n, const wchar_t* fallback) -> std::wstring {
        for (size_t i = 0; i < n; ++i) if (FamilyExists(list[i])) return list[i];
        return fallback;
    };
    m_serif = pick(font::kSerifCandidates, _countof(font::kSerifCandidates), L"SimSun");
    m_sans  = pick(font::kSansCandidates,  _countof(font::kSansCandidates),  L"Segoe UI");
    m_mono  = pick(font::kMonoCandidates,  _countof(font::kMonoCandidates),  L"Consolas");
    LogLine(L"[font] serif=%s sans=%s mono=%s", m_serif.c_str(), m_sans.c_str(), m_mono.c_str());

    ID2D1Factory* f = nullptr;
    m_dc->GetFactory(&f);
    if (f) {
        float dashes[] = { 3.0f, 3.0f };
        auto props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
            D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_CUSTOM, 0.0f);
        f->CreateStrokeStyle(props, dashes, _countof(dashes), &m_dashStyle);

        float perf[] = { 1.0f, 4.0f };   // 骑缝孔
        auto props2 = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
            D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_CUSTOM, 0.0f);
        f->CreateStrokeStyle(props2, perf, _countof(perf), &m_perfStyle);
        f->Release();
    }
    return true;
}

void Canvas::Shutdown()
{
    m_brushes.clear();
    m_formats.clear();
    m_dashStyle.Reset();
    m_perfStyle.Reset();
    m_sysFonts.Reset();
    m_dc = nullptr;
}

bool Canvas::FamilyExists(const wchar_t* name)
{
    if (!m_sysFonts) return false;
    UINT32 index = 0; BOOL exists = FALSE;
    if (FAILED(m_sysFonts->FindFamilyName(name, &index, &exists))) return false;
    return exists != FALSE;
}

const wchar_t* Canvas::FamilyFor(FontRole r) const
{
    switch (r) {
        case FontRole::Serif: return m_serif.c_str();
        case FontRole::Mono:  return m_mono.c_str();
        default:              return m_sans.c_str();
    }
}

ID2D1SolidColorBrush* Canvas::Brush(const D2D1_COLOR_F& c)
{
    uint32_t key = ColorKey(c);
    auto it = m_brushes.find(key);
    if (it != m_brushes.end()) return it->second.Get();
    ComPtr<ID2D1SolidColorBrush> b;
    if (FAILED(m_dc->CreateSolidColorBrush(c, &b))) return nullptr;
    auto* raw = b.Get();
    m_brushes.emplace(key, std::move(b));
    return raw;
}

IDWriteTextFormat* Canvas::Format(const TextStyle& st)
{
    FormatKey key{ (int)st.role, (int)(st.size * 10.0f + 0.5f), (int)st.weight, st.tabularNums ? 1 : 0 };
    auto it = m_formats.find(key);
    IDWriteTextFormat* fmt = nullptr;
    if (it != m_formats.end()) {
        fmt = it->second.Get();
    } else {
        ComPtr<IDWriteTextFormat> created;
        HRESULT hr = m_gfx->DWrite()->CreateTextFormat(
            FamilyFor(st.role), nullptr, st.weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            st.size, L"zh-CN", &created);
        if (FAILED(hr) || !created) return nullptr;
        created->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
        fmt = created.Get();
        m_formats.emplace(key, std::move(created));
    }
    // 对齐每次设置（缓存的是字体本身）
    fmt->SetTextAlignment(st.hAlign == HAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                        : st.hAlign == HAlign::Right  ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                      : DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(st.vAlign == VAlign::Middle ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                             : st.vAlign == VAlign::Bottom ? DWRITE_PARAGRAPH_ALIGNMENT_FAR
                                                           : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    return fmt;
}

// ---------------- 基本形 ----------------
void Canvas::FillRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c)
{
    if (auto* b = Brush(c)) m_dc->FillRectangle(r, b);
}

void Canvas::FillRoundRect(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c)
{
    if (radius <= 0.01f) { FillRect(r, c); return; }
    if (auto* b = Brush(c)) m_dc->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
}

void Canvas::StrokeRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float w)
{
    // 半像素对齐，避免发丝线糊成两像素
    float h = w * 0.5f;
    D2D1_RECT_F rr{ r.left + h, r.top + h, r.right - h, r.bottom - h };
    if (auto* b = Brush(c)) m_dc->DrawRectangle(rr, b, w);
}

void Canvas::StrokeRoundRect(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c, float w)
{
    float h = w * 0.5f;
    D2D1_RECT_F rr{ r.left + h, r.top + h, r.right - h, r.bottom - h };
    if (auto* b = Brush(c)) m_dc->DrawRoundedRectangle(D2D1::RoundedRect(rr, radius, radius), b, w);
}

void Canvas::Line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float w)
{
    if (auto* b = Brush(c))
        m_dc->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), b, w);
}

void Canvas::DashedLine(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float w)
{
    if (auto* b = Brush(c))
        m_dc->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), b, w, m_dashStyle.Get());
}

void Canvas::FillCircle(float cx, float cy, float r, const D2D1_COLOR_F& c)
{
    if (auto* b = Brush(c)) m_dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b);
}

void Canvas::StrokeCircle(float cx, float cy, float r, const D2D1_COLOR_F& c, float w)
{
    if (auto* b = Brush(c)) m_dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), b, w);
}

// ---------------- DOSSIER 母题 ----------------
void Canvas::DoubleFrame(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float gap, float w)
{
    StrokeRect(r, c, w);
    D2D1_RECT_F inner{ r.left + gap, r.top + gap, r.right - gap, r.bottom - gap };
    if (inner.right > inner.left && inner.bottom > inner.top)
        StrokeRect(inner, WithAlpha(c, c.a * 0.55f), w);
}

void Canvas::CornerTicks(const D2D1_RECT_F& r, const D2D1_COLOR_F& c, float len, float w, float inset)
{
    D2D1_RECT_F q{ r.left + inset, r.top + inset, r.right - inset, r.bottom - inset };
    auto* b = Brush(c);
    if (!b) return;
    auto seg = [&](float ax, float ay, float bx, float by) {
        m_dc->DrawLine(D2D1::Point2F(ax, ay), D2D1::Point2F(bx, by), b, w);
    };
    // 左上
    seg(q.left, q.top, q.left + len, q.top);
    seg(q.left, q.top, q.left, q.top + len);
    // 右上
    seg(q.right - len, q.top, q.right, q.top);
    seg(q.right, q.top, q.right, q.top + len);
    // 左下
    seg(q.left, q.bottom - len, q.left, q.bottom);
    seg(q.left, q.bottom, q.left + len, q.bottom);
    // 右下
    seg(q.right - len, q.bottom, q.right, q.bottom);
    seg(q.right, q.bottom - len, q.right, q.bottom);
}

void Canvas::PerforationH(float x1, float x2, float y, const D2D1_COLOR_F& c)
{
    if (auto* b = Brush(c))
        m_dc->DrawLine(D2D1::Point2F(x1, y), D2D1::Point2F(x2, y), b, 1.4f, m_perfStyle.Get());
}

void Canvas::NumberTag(const D2D1_RECT_F& r, const wchar_t* text,
                       const D2D1_COLOR_F& bg, const D2D1_COLOR_F& fg, float fontSize)
{
    FillRoundRect(r, shape::kEdgeSoft, bg);
    TextStyle st;
    st.role = FontRole::Mono;
    st.size = fontSize;
    st.weight = DWRITE_FONT_WEIGHT_BOLD;
    st.hAlign = HAlign::Center;
    st.vAlign = VAlign::Middle;
    st.letterSpacing = 0.6f;
    Text(text, r, st, fg);
}

void Canvas::SealStamp(float cx, float cy, float size, const wchar_t* text, float angleDeg, float alpha)
{
    if (alpha <= 0.003f) return;
    const auto& pal = Pal();
    auto col = WithAlpha(pal.seal, alpha);

    PushTransform(D2D1::Matrix3x2F::Rotation(angleDeg, D2D1::Point2F(cx, cy)));

    D2D1_RECT_F box{ cx - size * 0.5f, cy - size * 0.5f, cx + size * 0.5f, cy + size * 0.5f };
    StrokeRect(box, col, 2.2f);
    D2D1_RECT_F inner{ box.left + 3.5f, box.top + 3.5f, box.right - 3.5f, box.bottom - 3.5f };
    StrokeRect(inner, WithAlpha(pal.seal, alpha * 0.5f), 1.0f);

    TextStyle st;
    st.role = FontRole::Serif;
    st.size = size * 0.30f;
    st.weight = DWRITE_FONT_WEIGHT_BLACK;
    st.hAlign = HAlign::Center;
    st.vAlign = VAlign::Middle;
    st.letterSpacing = 1.0f;
    Text(text, box, st, col);

    PopTransform();
}

void Canvas::ProgressRing(float cx, float cy, float radius, float thickness, float progress,
                          const D2D1_COLOR_F& track, const D2D1_COLOR_F& fill)
{
    StrokeCircle(cx, cy, radius, track, thickness);
    progress = Clamp01(progress);
    if (progress <= 0.0001f) return;

    ID2D1Factory* f = nullptr;
    m_dc->GetFactory(&f);
    if (!f) return;

    ComPtr<ID2D1PathGeometry> geo;
    if (SUCCEEDED(f->CreatePathGeometry(&geo))) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(geo->Open(&sink))) {
            const float start = -3.14159265f * 0.5f;             // 12 点方向
            const float sweep = 6.28318531f * progress;
            D2D1_POINT_2F p0{ cx + radius * std::cos(start), cy + radius * std::sin(start) };
            float endA = start + sweep;
            D2D1_POINT_2F p1{ cx + radius * std::cos(endA), cy + radius * std::sin(endA) };

            sink->BeginFigure(p0, D2D1_FIGURE_BEGIN_HOLLOW);
            D2D1_ARC_SEGMENT arc{};
            arc.point = p1;
            arc.size = D2D1::SizeF(radius, radius);
            arc.rotationAngle = 0.0f;
            arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
            arc.arcSize = (progress > 0.5f) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
            sink->AddArc(arc);
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
        }
        if (auto* b = Brush(fill)) {
            ComPtr<ID2D1StrokeStyle> caps;
            auto props = D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
            f->CreateStrokeStyle(props, nullptr, 0, &caps);
            m_dc->DrawGeometry(geo.Get(), b, thickness, caps.Get());
        }
    }
    f->Release();
}

void Canvas::PaperCard(const D2D1_RECT_F& r, float lift, float radius)
{
    const auto& pal = Pal();
    // lift 0..1：抬起时底色更亮、描边更实，模拟纸从桌面被拈起
    auto face = MixColor(pal.paperHi, pal.dark ? pal.paper : D2D1::ColorF(1, 1, 1, 1), lift * 0.55f);
    FillRoundRect(r, radius, face);
    auto edge = MixColor(pal.rule, pal.ruleStrong, lift);
    StrokeRoundRect(r, radius, edge, shape::kHair);
    // 底部重线：档案卡的厚度
    float a = 0.10f + lift * 0.14f;
    Line(r.left + radius, r.bottom, r.right - radius, r.bottom,
         WithAlpha(pal.ink900, a), 1.2f);
}

// ---------------- 文字 ----------------
void Canvas::Text(const std::wstring& s, const D2D1_RECT_F& box, const TextStyle& st, const D2D1_COLOR_F& c)
{
    if (s.empty()) return;
    auto* fmt = Format(st);
    auto* b = Brush(c);
    if (!fmt || !b) return;

    if (st.letterSpacing != 0.0f) {
        // 字距需要 TextLayout1
        ComPtr<IDWriteTextLayout> layout;
        if (SUCCEEDED(m_gfx->DWrite()->CreateTextLayout(
                s.c_str(), (UINT32)s.size(), fmt,
                (std::max)(box.right - box.left, 1.0f),
                (std::max)(box.bottom - box.top, 1.0f), &layout))) {
            ComPtr<IDWriteTextLayout1> l1;
            if (SUCCEEDED(layout.As(&l1))) {
                DWRITE_TEXT_RANGE all{ 0, (UINT32)s.size() };
                l1->SetCharacterSpacing(0.0f, st.letterSpacing, 0.0f, all);
            }
            m_dc->DrawTextLayout(D2D1::Point2F(box.left, box.top), layout.Get(), b,
                                 D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
                                 | D2D1_DRAW_TEXT_OPTIONS_CLIP);
            return;
        }
    }
    m_dc->DrawText(s.c_str(), (UINT32)s.size(), fmt, box, b,
                   D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT
                   | D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Canvas::MeasureWidth(const std::wstring& s, const TextStyle& st)
{
    if (s.empty()) return 0.0f;
    auto* fmt = Format(st);
    if (!fmt) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_gfx->DWrite()->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt,
                                                 100000.0f, 10000.0f, &layout)))
        return 0.0f;
    if (st.letterSpacing != 0.0f) {
        ComPtr<IDWriteTextLayout1> l1;
        if (SUCCEEDED(layout.As(&l1))) {
            DWRITE_TEXT_RANGE all{ 0, (UINT32)s.size() };
            l1->SetCharacterSpacing(0.0f, st.letterSpacing, 0.0f, all);
        }
    }
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float Canvas::MeasureHeight(const std::wstring& s, const TextStyle& st, float maxWidth)
{
    if (s.empty()) return 0.0f;
    auto* fmt = Format(st);
    if (!fmt) return 0.0f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_gfx->DWrite()->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt,
                                                 (std::max)(maxWidth, 1.0f), 100000.0f, &layout)))
        return 0.0f;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.height;
}

void Canvas::CharsReveal(const std::wstring& s, float x, float top, const TextStyle& st,
                         const D2D1_COLOR_F& c, float progress, float perChar, float riseDip)
{
    if (s.empty()) return;
    TextStyle one = st;
    one.hAlign = HAlign::Left;
    one.vAlign = VAlign::Top;

    float cursor = x;
    for (size_t i = 0; i < s.size(); ++i) {
        std::wstring ch(1, s[i]);
        float w = MeasureWidth(ch, one) + one.letterSpacing;

        if (s[i] != L' ') {
            // 每字错开：本字的局部进度
            float t = (progress - (float)i * perChar) / 0.42f;
            t = Clamp01(t);
            if (t > 0.0f) {
                float e = ease::OutCubic(t);
                float dy = (1.0f - e) * riseDip;
                float alpha = e;
                // 翻转感：纵向压缩到展开
                float scaleY = 0.55f + 0.45f * e;

                float cx = cursor + w * 0.5f;
                float cy = top + st.size * 0.6f;
                auto m = D2D1::Matrix3x2F::Scale(1.0f, scaleY, D2D1::Point2F(cx, cy))
                       * D2D1::Matrix3x2F::Translation(0.0f, dy);
                PushTransform(m);
                D2D1_RECT_F box{ cursor, top, cursor + w + 2.0f, top + st.size * 2.0f };
                Text(ch, box, one, WithAlpha(c, c.a * alpha));
                PopTransform();
            }
        }
        cursor += w;
    }
}

// ---------------- 变换 / 裁剪 / 透明 ----------------
void Canvas::PushClip(const D2D1_RECT_F& r)
{
    m_dc->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED);
    ++m_clipDepth;
}

void Canvas::PopClip()
{
    if (m_clipDepth > 0) { m_dc->PopAxisAlignedClip(); --m_clipDepth; }
}

void Canvas::PushOpacity(float alpha)
{
    auto params = D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                        D2D1::IdentityMatrix(), Clamp01(alpha));
    m_dc->PushLayer(params, nullptr);
    ++m_layerDepth;
}

void Canvas::PopOpacity()
{
    if (m_layerDepth > 0) { m_dc->PopLayer(); --m_layerDepth; }
}

void Canvas::PushTransform(const D2D1_MATRIX_3X2_F& m)
{
    D2D1_MATRIX_3X2_F cur{};
    m_dc->GetTransform(&cur);
    m_xformStack.push_back(cur);
    // D2D1::Matrix3x2F 没有从基类型的转换构造，只能按引用重解释
    const D2D1::Matrix3x2F& a = *D2D1::Matrix3x2F::ReinterpretBaseType(&m);
    const D2D1::Matrix3x2F& b = *D2D1::Matrix3x2F::ReinterpretBaseType(&cur);
    m_dc->SetTransform(a * b);
}

void Canvas::PopTransform()
{
    if (m_xformStack.empty()) return;
    m_dc->SetTransform(m_xformStack.back());
    m_xformStack.pop_back();
}

} // namespace lj
