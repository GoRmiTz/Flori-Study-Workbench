#pragma once
// ============================================================
//  Cursor.h — 制图仪光标
//  移植自 visual.js：三段式跟随造景深
//    中心点  零延迟，直接吸附
//    准星环  lerp k=0.24
//    取景框  lerp k=0.11 + 悬停放大
// ============================================================
#include "ui/Canvas.h"
#include "ui/Motion.h"
#include "ui/Input.h"

namespace lj {

class DraftingCursor
{
public:
    void Update(float dt, const Input& in, bool overInteractive);
    void Paint(Canvas& cv);

    bool enabled = true;

private:
    float m_mx = 0.0f, m_my = 0.0f;
    Smooth m_rx{ 0.0f, 0.24f }, m_ry{ 0.0f, 0.24f };   // 准星
    Smooth m_cx{ 0.0f, 0.11f }, m_cy{ 0.0f, 0.11f };   // 取景框
    Smooth m_grow{ 0.0f, 0.18f };
    Smooth m_alpha{ 0.0f, 0.20f };
    Smooth m_click{ 0.0f, 0.30f };
    bool m_seen = false;
};

} // namespace lj
