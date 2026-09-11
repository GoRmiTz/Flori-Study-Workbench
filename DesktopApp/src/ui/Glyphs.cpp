#include "ui/Glyphs.h"
#include <wrl/client.h>

namespace lj {
using Microsoft::WRL::ComPtr;

void PaintPlayGlyph(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& c)
{
    float w = r.right - r.left, h = r.bottom - r.top;
    float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    float tw = w * 0.30f, th = h * 0.36f;
    float ox = tw * 0.16f;                       // 重心右移，视觉居中
    D2D1_POINT_2F a  { cx - tw + ox, cy - th };
    D2D1_POINT_2F b  { cx - tw + ox, cy + th };
    D2D1_POINT_2F tip{ cx + tw + ox, cy };

    ComPtr<ID2D1Factory> factory;
    cv.DC()->GetFactory(&factory);
    ComPtr<ID2D1PathGeometry> path;
    if (factory && SUCCEEDED(factory->CreatePathGeometry(&path))) {
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(a, D2D1_FIGURE_BEGIN_FILLED);
            sink->AddLine(b);
            sink->AddLine(tip);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            cv.DC()->FillGeometry(path.Get(), cv.Brush(c));
        }
    }
}

void PaintPauseGlyph(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& c)
{
    float w = r.right - r.left, h = r.bottom - r.top;
    float cx = (r.left + r.right) * 0.5f, cy = (r.top + r.bottom) * 0.5f;
    float bh = h * 0.34f, bw = (w < 22.0f ? w * 0.13f : w * 0.10f), gap = w * 0.085f;
    float x1 = cx - gap - bw, x2 = cx + gap + bw;
    float rad = (w < 22.0f ? 1.0f : 1.6f);
    cv.FillRoundRect({ x1 - bw, cy - bh, x1 + bw, cy + bh }, rad, c);
    cv.FillRoundRect({ x2 - bw, cy - bh, x2 + bw, cy + bh }, rad, c);
}

void PaintPlayPauseButton(Canvas& cv, const D2D1_RECT_F& r, bool playing,
                          bool hover, bool press, bool showLabel)
{
    const auto& pal = cv.Pal();
    float sink = press ? 1.0f : 0.0f;
    D2D1_RECT_F rr = r; rr.top += sink; rr.bottom += sink;

    auto bg = MixColor(pal.seal, pal.sealLo, hover * 0.6f);
    cv.FillRoundRect(rr, shape::kEdgeSoft, bg);
    cv.StrokeRoundRect(rr, shape::kEdgeSoft, WithAlpha(pal.sealLo, 0.55f + hover * 0.45f), shape::kStroke);

    D2D1_COLOR_F ic = pal.paperHi;
    if (showLabel) {
        float ih = (rr.bottom - rr.top) * 0.5f;
        D2D1_RECT_F ib{ rr.left + 14.0f, (rr.top + rr.bottom) * 0.5f - ih * 0.5f,
                        rr.left + 14.0f + ih, (rr.top + rr.bottom) * 0.5f + ih * 0.5f };
        if (playing) PaintPauseGlyph(cv, ib, ic); else PaintPlayGlyph(cv, ib, ic);

        TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f;
        ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        ts.hAlign = HAlign::Left; ts.vAlign = VAlign::Middle; ts.letterSpacing = 1.0f;
        D2D1_RECT_F lb{ rr.left + 14.0f + ih + 8.0f, rr.top, rr.right - 12.0f, rr.bottom };
        cv.Text(playing ? L"暂停" : L"播放", lb, ts, ic);
    } else {
        float ih = (rr.bottom - rr.top) * 0.46f;
        D2D1_RECT_F ib{ (rr.left + rr.right) * 0.5f - ih * 0.5f, (rr.top + rr.bottom) * 0.5f - ih * 0.5f,
                        (rr.left + rr.right) * 0.5f + ih * 0.5f, (rr.top + rr.bottom) * 0.5f + ih * 0.5f };
        if (playing) PaintPauseGlyph(cv, ib, ic); else PaintPlayGlyph(cv, ib, ic);
    }
}

} // namespace lj
