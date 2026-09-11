#pragma once
// Widget.h - base widget class and basic controls
// Hover/press via Smooth, click produces ripple (ported from visual.js btn-ripple)
#include "ui/Canvas.h"
#include "ui/Motion.h"
#include "ui/Input.h"
#include <functional>

namespace lj {

class Widget
{
public:
    virtual ~Widget() = default;

    D2D1_RECT_F bounds{};
    bool enabled = true;
    bool visible = true;
    std::function<void()> onClick;

    // Entrance: parent view sets delay for staggered animation
    float enterDelay = 0.0f;
    Tween enter;

    void StartEnter(float delay)
    {
        enterDelay = delay;
        enter.Start(0.0f, 1.0f, 0.52f, ease::OutCubic, delay);
    }

    virtual void Update(float dt, const Input& in);
    virtual void Paint(Canvas& cv) = 0;

    bool Hovered() const { return m_hovered; }
    float HoverAmt() const { return m_hover.value; }
    float PressAmt() const { return m_press.value; }

    bool HitTest(float x, float y) const
    {
        return x >= bounds.left && x <= bounds.right && y >= bounds.top && y <= bounds.bottom;
    }

    float Width()  const { return bounds.right - bounds.left; }
    float Height() const { return bounds.bottom - bounds.top; }

protected:
    void PaintRipples(Canvas& cv, const D2D1_COLOR_F& color, float radius);

    struct Ripple { float x, y, t; };
    std::vector<Ripple> m_ripples;

    bool m_hovered = false;
    bool m_armed = false;
    Smooth m_hover{ 0.0f, 0.22f };
    Smooth m_press{ 0.0f, 0.34f };
};

// ---------------- Button: hard-edge + hover invert ----------------
class Button : public Widget
{
public:
    std::wstring label;
    std::wstring tag;
    bool primary = false;
    float fontSize = 14.0f;

    void Paint(Canvas& cv) override;
};

// ---------------- ModuleCard ----------------
class ModuleCard : public Widget
{
public:
    std::wstring index;
    std::wstring title;
    std::wstring subtitle;
    std::wstring meta;
    D2D1_COLOR_F accent{};
    bool accentSet = false;

    void Paint(Canvas& cv) override;
};

// ---------------- CountdownCard ----------------
class CountdownCard : public Widget
{
public:
    std::wstring label;
    std::wstring dateText;
    int days = 0;
    bool urgent = false;
    Tween countUp;

    void Start(float delay);
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;
};

} // namespace lj