#pragma once
// ============================================================
//  CoverView.h — 封面
//  复刻网页 cover-hero：旋转测量环 + 三档景深视差 + 逐字揭示
//  滚轮向下推进三幕，末幕进入主应用
// ============================================================
#include "ui/View.h"

namespace lj {

class CoverView : public View
{
public:
    const wchar_t* Id() const override { return L"cover"; }
    bool FullBleed() const override { return true; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    void GoEnter();                 // 进入：有会话→home，无会话→login
    void PaintHero(Canvas& cv, float scroll);
    void PaintIntro(Canvas& cv, float scroll);
    void PaintEnter(Canvas& cv, float scroll);

    Button m_enterBtn;
    std::vector<Widget*> m_widgets;

    float m_t = 0.0f;
    float m_rotorAngle = 0.0f;
    // 三档景深：分别以不同 lerp 系数跟随滚动
    Smooth m_parNear{ 0.0f, 0.20f };
    Smooth m_parMid{ 0.0f, 0.10f };
    Smooth m_parFar{ 0.0f, 0.075f };
    float m_pageH = 720.0f;
};

} // namespace lj
