#pragma once
// ============================================================
//  Dust.h — 图纸上的墨点尘埃
//  对应 visual.js 的 physicsLayer：三档景深、单色方形、极淡
//  桌面端加了极缓的自由漂移与呼吸，长时间盯着不会觉得静止
// ============================================================
#include "ui/Canvas.h"
#include "ui/Motion.h"

namespace lj {

class DustField
{
public:
    void Init(int seed = 20260803);
    void Update(float dt, float width, float height, float scroll);
    void Paint(Canvas& cv);

private:
    struct Mote
    {
        float x, y;          // 归一化 0..1
        float size;
        float alpha;
        float phase;
        float speed;
        float drift;
        int   layer;         // 0 远 1 中 2 近
    };
    std::vector<Mote> m_motes;
    float m_w = 0.0f, m_h = 0.0f;
    float m_t = 0.0f;
    float m_scroll = 0.0f;
};

} // namespace lj
