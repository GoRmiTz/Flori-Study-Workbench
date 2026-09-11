#include "ui/View.h"

namespace lj {

namespace {
// 每格滚轮的滚动距离（DIP）。1.0 太小会"爬行"，40~60 是常规手感。
constexpr float kScrollStep = 48.0f;
}

void View::Update(float dt, const Input& in)
{
    m_entered += dt;

    if (in.wheel != 0.0f && in.inWindow) {
        m_scroll.target = (std::max)(0.0f, (std::min)(MaxScroll(), m_scroll.target + in.wheel * kScrollStep));
    }
    // 内容变短时收回
    m_scroll.target = (std::max)(0.0f, (std::min)(MaxScroll(), m_scroll.target));
    m_scroll.Update(dt);
}

void View::UpdateWidgets(std::vector<Widget*>& list, float dt, const Input& in)
{
    m_overInteractive = false;
    for (auto* w : list) {
        if (!w) continue;
        w->Update(dt, in);
        if (w->Hovered()) m_overInteractive = true;
    }
}

} // namespace lj
