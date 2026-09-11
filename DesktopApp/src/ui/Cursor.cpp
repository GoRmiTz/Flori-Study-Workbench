#include "ui/Cursor.h"

namespace lj {

void DraftingCursor::Update(float dt, const Input& in, bool overInteractive)
{
    if (!enabled) return;

    if (in.inWindow) {
        m_mx = in.mouseX; m_my = in.mouseY;
        if (!m_seen) {
            m_seen = true;
            m_rx.Snap(m_mx); m_ry.Snap(m_my);
            m_cx.Snap(m_mx); m_cy.Snap(m_my);
        }
    }
    m_alpha.target = in.inWindow ? 1.0f : 0.0f;

    m_rx.target = m_mx; m_ry.target = m_my;
    m_cx.target = m_mx; m_cy.target = m_my;
    m_grow.target = overInteractive ? 1.0f : 0.0f;
    m_click.target = in.down ? 1.0f : 0.0f;

    m_rx.Update(dt); m_ry.Update(dt);
    m_cx.Update(dt); m_cy.Update(dt);
    m_grow.Update(dt);
    m_alpha.Update(dt);
    m_click.Update(dt);
}

void DraftingCursor::Paint(Canvas& cv)
{
    if (!enabled || m_alpha.value <= 0.01f) return;
    const auto& pal = cv.Pal();
    float a = m_alpha.value;
    float g = m_grow.value;
    float ck = m_click.value;

    // ---- 取景框：四个直角，慢速跟随，悬停时张开 ----
    {
        float s = 0.70f + g * 0.30f;
        float half = (16.0f + g * 8.0f) * s;
        float len = 5.0f + g * 3.0f;
        float x = m_cx.value, y = m_cy.value;
        auto c = WithAlpha(pal.seal, a * (0.16f + g * 0.44f));
        float w = 1.2f;
        cv.Line(x - half, y - half, x - half + len, y - half, c, w);
        cv.Line(x - half, y - half, x - half, y - half + len, c, w);
        cv.Line(x + half - len, y - half, x + half, y - half, c, w);
        cv.Line(x + half, y - half, x + half, y - half + len, c, w);
        cv.Line(x - half, y + half - len, x - half, y + half, c, w);
        cv.Line(x - half, y + half, x - half + len, y + half, c, w);
        cv.Line(x + half - len, y + half, x + half, y + half, c, w);
        cv.Line(x + half, y + half - len, x + half, y + half, c, w);
    }

    // ---- 准星：十字 + 细环 ----
    {
        float x = m_rx.value, y = m_ry.value;
        float r = 9.0f + g * 5.0f - ck * 2.0f;
        auto c = WithAlpha(pal.ink700, a * (0.30f + g * 0.35f));
        cv.StrokeCircle(x, y, r, c, 1.0f);
        float arm = r + 4.0f;
        auto c2 = WithAlpha(pal.ink700, a * 0.22f);
        cv.Line(x - arm, y, x - r - 1.0f, y, c2, 1.0f);
        cv.Line(x + r + 1.0f, y, x + arm, y, c2, 1.0f);
        cv.Line(x, y - arm, x, y - r - 1.0f, c2, 1.0f);
        cv.Line(x, y + r + 1.0f, x, y + arm, c2, 1.0f);
    }

    // ---- 中心点：零延迟吸附鼠标真实位置 ----
    {
        float rr = 2.0f + ck * 1.2f;
        cv.FillCircle(m_mx, m_my, rr, WithAlpha(pal.seal, a * 0.95f));
    }
}

} // namespace lj
