#pragma once
// ============================================================
//  View.h — 视图基类
//  自带惯性滚动（对应网页的 scrollY 与三档视差驱动）
// ============================================================
#include "ui/Canvas.h"
#include "ui/Widget.h"
#include "ui/Input.h"
#include "ui/Motion.h"
#include <functional>

namespace lj {

class View
{
public:
    virtual ~View() = default;

    virtual const wchar_t* Id() const = 0;
    virtual const wchar_t* Title() const { return L""; }

    // 由 App 注入：视图内部触发路由跳转
    std::function<void(const std::wstring&)> navigate;
    void Go(const std::wstring& id) { if (navigate) navigate(id); }

    // 进入视图时重置动画
    virtual void OnEnter() { m_scroll.Snap(0.0f); m_entered = 0.0f; }
    // 离开视图（路由切换时）由 Router 调用，用于收起浮层/编辑器
    virtual void OnLeave() {}
    // area 为可用内容区（DIP）
    virtual void Layout(const D2D1_RECT_F& area, Canvas& cv) { m_area = area; }
    virtual void Update(float dt, const Input& in);
    virtual void Paint(Canvas& cv) {}

    // 本视图是否需要覆盖全屏（封面/加载屏不显示顶栏）
    virtual bool FullBleed() const { return false; }
    // 截图自检：强制打开本视图的编辑器浮层（默认无操作）
    virtual void DebugForceOpen() {}
    // 截图自检：强制打开本视图的只读预览浮层（默认无操作）
    virtual void DebugForcePreview() {}
    // 光标是否处于可交互元素上
    virtual bool OverInteractive() const { return m_overInteractive; }

    float ScrollY() const { return m_scroll.value; }
    float MaxScroll() const
    {
        float vh = m_area.bottom - m_area.top;
        return (std::max)(0.0f, m_contentHeight - vh);
    }
    void SetScroll(float v) { m_scroll.Snap(v); }   // 截图自检用：瞬间定位

protected:
    // 子类在 Layout 末尾设置内容总高，用于限制滚动
    void SetContentHeight(float h) { m_contentHeight = h; }
    void UpdateWidgets(std::vector<Widget*>& list, float dt, const Input& in);

    D2D1_RECT_F m_area{};
    Smooth m_scroll{ 0.0f, 0.20f };   // 平滑滚动
    float  m_contentHeight = 0.0f;
    float  m_entered = 0.0f;          // 进入后经过的秒数
    bool   m_overInteractive = false;
};

} // namespace lj
