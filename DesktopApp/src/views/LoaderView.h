#pragma once
// ============================================================
//  LoaderView.h — 加载屏
//  对应网页 #pageLoader：进度走完后自动进入封面
// ============================================================
#include "ui/View.h"

namespace lj {

class LoaderView : public View
{
public:
    const wchar_t* Id() const override { return L"loader"; }
    bool FullBleed() const override { return true; }

    void OnEnter() override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

    // 背景着色器的全局显影进度
    float RevealAmount() const { return m_reveal; }

private:
    float m_t = 0.0f;
    float m_reveal = 0.0f;
    bool  m_done = false;
    bool  m_navigated = false;   // 跳转只触发一次
    Tween m_barTween;
    Tween m_fade;
};

} // namespace lj
