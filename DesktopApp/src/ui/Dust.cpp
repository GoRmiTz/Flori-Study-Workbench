#include "ui/Dust.h"
#include <random>

namespace lj {

void DustField::Init(int seed)
{
    std::mt19937 rng(seed);
    auto rnd = [&rng](float a, float b) {
        return a + (b - a) * (float)(rng() % 100000) / 100000.0f;
    };

    struct LayerDef { int count; float sizeMin, sizeMax, opMin, opMax, spMin, spMax; };
    // 远 / 中 / 近，与网页三档一致
    const LayerDef defs[3] = {
        { 9, 3.0f,  5.0f,  0.05f, 0.09f, 0.010f, 0.020f },
        { 6, 5.0f,  8.0f,  0.07f, 0.12f, 0.018f, 0.030f },
        { 4, 7.0f, 11.0f,  0.08f, 0.15f, 0.026f, 0.042f },
    };

    m_motes.clear();
    for (int L = 0; L < 3; ++L) {
        const auto& d = defs[L];
        for (int i = 0; i < d.count; ++i) {
            Mote m{};
            m.x = rnd(0.02f, 0.98f);
            m.y = rnd(0.05f, 0.98f);
            m.size = rnd(d.sizeMin, d.sizeMax);
            m.alpha = rnd(d.opMin, d.opMax);
            m.phase = rnd(0.0f, 6.283f);
            m.speed = rnd(d.spMin, d.spMax);
            m.drift = rnd(-0.5f, 0.5f);
            m.layer = L;
            m_motes.push_back(m);
        }
    }
}

void DustField::Update(float dt, float width, float height, float scroll)
{
    m_w = width; m_h = height;
    m_t += dt;
    m_scroll = scroll;

    for (auto& m : m_motes) {
        m.y -= m.speed * dt * 0.06f;          // 极缓上浮
        if (m.y < -0.05f) {
            m.y = 1.05f;
            m.x = std::fmod(m.x + 0.37f, 1.0f);
        }
    }
}

void DustField::Paint(Canvas& cv)
{
    if (m_w <= 0.0f || m_h <= 0.0f) return;
    const auto& pal = cv.Pal();
    // 三档视差系数：与 visual.js 的 .075 / .1 / .2 呼应
    const float par[3] = { 0.04f, 0.09f, 0.16f };

    for (const auto& m : m_motes) {
        float sway = std::sin(m_t * 0.35f + m.phase) * 6.0f * (0.4f + m.layer * 0.3f);
        float x = m.x * m_w + sway + m.drift * 4.0f;
        float y = m.y * m_h + m_scroll * par[m.layer];
        if (y < -20.0f || y > m_h + 20.0f) continue;

        float breathe = 0.75f + 0.25f * std::sin(m_t * 0.8f + m.phase * 1.7f);
        float s = m.size * 0.5f;
        // 方形墨点，不是圆球 —— 图纸上的污渍
        D2D1_RECT_F r{ x - s, y - s, x + s, y + s };
        cv.FillRect(r, WithAlpha(pal.ink300, m.alpha * breathe));
    }
}

} // namespace lj
