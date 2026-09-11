#include "ui/Widget.h"

namespace lj {

void Widget::Update(float dt, const Input& in)
{
    enter.Update(dt);

    bool hit = enabled && visible && in.inWindow && HitTest(in.mouseX, in.mouseY);
    m_hovered = hit;
    m_hover.target = hit ? 1.0f : 0.0f;

    if (hit && in.pressed) {
        m_armed = true;
        m_ripples.push_back({ in.mouseX - bounds.left, in.mouseY - bounds.top, 0.0f });
    }
    if (in.released) {
        if (m_armed && hit && onClick) onClick();
        m_armed = false;
    }
    m_press.target = (m_armed && hit) ? 1.0f : 0.0f;

    m_hover.Update(dt);
    m_press.Update(dt);

    for (auto& r : m_ripples) r.t += dt;
    m_ripples.erase(std::remove_if(m_ripples.begin(), m_ripples.end(),
                                   [](const Ripple& r) { return r.t > 0.62f; }),
                    m_ripples.end());
}

void Widget::PaintRipples(Canvas& cv, const D2D1_COLOR_F& color, float radius)
{
    for (const auto& r : m_ripples) {
        float t = Clamp01(r.t / 0.62f);
        float e = ease::OutCubic(t);
        float rad = radius * e;
        float alpha = (1.0f - t) * 0.22f;
        cv.FillCircle(bounds.left + r.x, bounds.top + r.y, rad, WithAlpha(color, alpha));
    }
}

// ---------------- Button ----------------
void Button::Paint(Canvas& cv)
{
    if (!visible) return;
    const auto& pal = cv.Pal();
    float ev = enter.running ? enter.Value() : 1.0f;
    if (ev <= 0.001f) return;

    float h = m_hover.value;
    float p = m_press.value;

    // 入场：上浮 + 淡入
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - ev) * 10.0f));
    cv.PushOpacity(ev);

    // 按下时整体下沉 1px
    D2D1_RECT_F r = bounds;
    r.top += p * 1.0f; r.bottom += p * 1.0f;

    if (primary) {
        auto bg = MixColor(pal.seal, pal.sealLo, h * 0.6f);
        cv.FillRoundRect(r, shape::kEdgeSoft, bg);
        cv.PushClip(r);
        PaintRipples(cv, pal.paperHi, Width() * 1.2f);
        cv.PopClip();
        TextStyle st;
        st.role = FontRole::Sans; st.size = fontSize;
        st.weight = DWRITE_FONT_WEIGHT_BOLD;
        st.hAlign = HAlign::Center; st.vAlign = VAlign::Middle;
        st.letterSpacing = 1.2f;
        cv.Text(label, r, st, pal.paperHi);
    } else {
        auto bg = MixColor(WithAlpha(pal.paperHi, 0.0f), pal.paperLo, h);
        cv.FillRoundRect(r, shape::kEdgeSoft, bg);
        auto edge = MixColor(pal.rule, pal.seal, h);
        cv.StrokeRoundRect(r, shape::kEdgeSoft, edge, h > 0.02f ? shape::kStroke : shape::kHair);
        cv.PushClip(r);
        PaintRipples(cv, pal.seal, Width() * 1.2f);
        cv.PopClip();

        TextStyle st;
        st.role = FontRole::Sans; st.size = fontSize;
        st.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        st.hAlign = HAlign::Center; st.vAlign = VAlign::Middle;
        st.letterSpacing = 1.2f;
        cv.Text(label, r, st, MixColor(pal.ink700, pal.seal, h));
    }

    // 悬停时四角裁切标记显现
    if (h > 0.01f) {
        D2D1_RECT_F o{ r.left - 4.0f, r.top - 4.0f, r.right + 4.0f, r.bottom + 4.0f };
        cv.CornerTicks(o, WithAlpha(pal.seal, h * 0.55f), 6.0f, 1.2f);
    }

    cv.PopOpacity();
    cv.PopTransform();
}

// ---------------- ModuleCard ----------------
void ModuleCard::Paint(Canvas& cv)
{
    if (!visible) return;
    const auto& pal = cv.Pal();
    float ev = enter.running ? enter.Value() : 1.0f;
    if (ev <= 0.001f) return;

    float h = m_hover.value;
    float p = m_press.value;
    auto ac = accentSet ? accent : pal.seal;

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - ev) * 18.0f));
    cv.PushOpacity(ev);

    // 悬停抬起 3dip，按下回落
    float lift = h * 3.0f - p * 2.0f;
    D2D1_RECT_F r = bounds;
    r.top -= lift; r.bottom -= lift;

    cv.PaperCard(r, h);

    // 顶部色条：档案分类标签
    D2D1_RECT_F bar{ r.left, r.top, r.left + 3.0f + h * 2.0f, r.bottom };
    cv.FillRect(bar, WithAlpha(ac, 0.55f + h * 0.45f));

    cv.PushClip(r);
    PaintRipples(cv, ac, Width());
    cv.PopClip();

    const float padL = 18.0f;
    // 编号
    TextStyle idx;
    idx.role = FontRole::Mono; idx.size = 10.5f;
    idx.weight = DWRITE_FONT_WEIGHT_BOLD; idx.letterSpacing = 1.4f;
    cv.Text(index, { r.left + padL, r.top + 13.0f, r.right - 14.0f, r.top + 30.0f },
            idx, WithAlpha(pal.ink300, 0.95f));

    // 标题
    TextStyle ts;
    ts.role = FontRole::Serif; ts.size = 20.0f;
    ts.weight = DWRITE_FONT_WEIGHT_BOLD; ts.letterSpacing = 1.5f;
    cv.Text(title, { r.left + padL, r.top + 30.0f, r.right - 14.0f, r.top + 62.0f },
            ts, MixColor(pal.ink900, ac, h * 0.8f));

    // 副标题
    TextStyle ss;
    ss.role = FontRole::Sans; ss.size = 12.5f;
    cv.Text(subtitle, { r.left + padL, r.top + 62.0f, r.right - 16.0f, r.bottom - 26.0f },
            ss, pal.ink500);

    // 底部骑缝线 + 状态
    float by = r.bottom - 22.0f;
    cv.PerforationH(r.left + padL, r.right - padL, by, WithAlpha(pal.ruleStrong, 0.6f));
    TextStyle ms;
    ms.role = FontRole::Mono; ms.size = 10.5f; ms.letterSpacing = 0.8f;
    cv.Text(meta, { r.left + padL, by + 3.0f, r.right - padL, r.bottom - 2.0f },
            ms, WithAlpha(pal.ink300, 0.9f));

    // 悬停：右下角箭头推入 + 四角标记
    if (h > 0.01f) {
        TextStyle ar;
        ar.role = FontRole::Sans; ar.size = 15.0f;
        ar.weight = DWRITE_FONT_WEIGHT_BOLD;
        ar.hAlign = HAlign::Right; ar.vAlign = VAlign::Middle;
        float dx = ease::OutCubic(h) * 4.0f;
        cv.Text(L"→", { r.right - 40.0f + dx, by - 2.0f, r.right - padL + dx, by + 16.0f },
                ar, WithAlpha(ac, h));
        D2D1_RECT_F o{ r.left - 5.0f, r.top - 5.0f, r.right + 5.0f, r.bottom + 5.0f };
        cv.CornerTicks(o, WithAlpha(ac, h * 0.5f), 11.0f, 1.3f);
    }

    cv.PopOpacity();
    cv.PopTransform();
}

// ---------------- CountdownCard ----------------
void CountdownCard::Start(float delay)
{
    StartEnter(delay);
    countUp.Start(0.0f, (float)days, 0.9f, ease::OutQuart, delay + 0.1f);
}

void CountdownCard::Update(float dt, const Input& in)
{
    Widget::Update(dt, in);
    countUp.Update(dt);
}

void CountdownCard::Paint(Canvas& cv)
{
    if (!visible) return;
    const auto& pal = cv.Pal();
    float ev = enter.running ? enter.Value() : 1.0f;
    if (ev <= 0.001f) return;

    float h = m_hover.value;
    auto ac = urgent ? pal.vermilion : pal.seal;

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - ev) * 14.0f));
    cv.PushOpacity(ev);

    D2D1_RECT_F r = bounds;
    cv.FillRoundRect(r, shape::kEdge, MixColor(pal.paperHi, pal.paperLo, 0.35f - h * 0.35f));
    cv.StrokeRoundRect(r, shape::kEdge, MixColor(pal.rule, ac, h * 0.7f), shape::kHair);

    // 左上编号签
    D2D1_RECT_F tag{ r.left + 12.0f, r.top + 11.0f, r.left + 12.0f + 46.0f, r.top + 27.0f };
    cv.NumberTag(tag, dateText.substr(0, 4).c_str(), WithAlpha(ac, 0.10f), ac, 10.0f);

    // 大数字
    int shown = (int)(countUp.running ? countUp.Value() + 0.5f : (float)days);
    wchar_t buf[32];
    swprintf_s(buf, L"%d", shown);
    TextStyle ns;
    ns.role = FontRole::Mono; ns.size = 40.0f;
    ns.weight = DWRITE_FONT_WEIGHT_BOLD;
    ns.hAlign = HAlign::Left; ns.vAlign = VAlign::Top;
    ns.tabularNums = true;
    float numW = cv.MeasureWidth(buf, ns);
    cv.Text(buf, { r.left + 14.0f, r.top + 30.0f, r.right - 8.0f, r.top + 84.0f }, ns, ac);

    TextStyle us;
    us.role = FontRole::Sans; us.size = 12.0f;
    cv.Text(L"天", { r.left + 18.0f + numW, r.top + 54.0f, r.right - 8.0f, r.top + 76.0f },
            us, pal.ink500);

    // 标签
    TextStyle ls;
    ls.role = FontRole::Sans; ls.size = 13.0f;
    ls.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ls.letterSpacing = 0.8f;
    cv.Text(label, { r.left + 14.0f, r.bottom - 42.0f, r.right - 12.0f, r.bottom - 24.0f },
            ls, pal.ink700);

    TextStyle ds;
    ds.role = FontRole::Mono; ds.size = 10.5f; ds.letterSpacing = 0.6f;
    cv.Text(dateText, { r.left + 14.0f, r.bottom - 24.0f, r.right - 12.0f, r.bottom - 8.0f },
            ds, pal.ink300);

    if (h > 0.01f)
        cv.CornerTicks({ r.left - 4.0f, r.top - 4.0f, r.right + 4.0f, r.bottom + 4.0f },
                       WithAlpha(ac, h * 0.5f), 9.0f, 1.2f);

    cv.PopOpacity();
    cv.PopTransform();
}

} // namespace lj
