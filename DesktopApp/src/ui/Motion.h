#pragma once
// ============================================================
//  Motion.h — 动效数学
//  移植自 WebApp/assets/js/visual.js 的核心结论：
//   1) lerp 系数必须帧率无关：f = 1 - (1-k)^(dt*60)
//   2) 缓动分野：界面控件 out / 场景转场 inOut / 滚动驱动 linear
// ============================================================
#include <cmath>
#include <algorithm>

namespace lj {

// ---------- 帧率无关插值系数 ----------
// k = 「每 60 帧应完成的比例」，144Hz 屏上不再飞快
inline float LerpFactor(float k, float dt)
{
    dt = (std::min)(dt, 0.1f);
    return 1.0f - std::pow(1.0f - k, dt * 60.0f);
}

// ---------- 缓动函数库 ----------
namespace ease {

inline float Linear(float t) { return t; }

// 界面控件：一律 out（快起慢收，响应感强）
inline float OutCubic(float t) { float u = 1.0f - t; return 1.0f - u * u * u; }
inline float OutQuart(float t) { float u = 1.0f - t; return 1.0f - u * u * u * u; }
inline float OutExpo(float t)  { return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
inline float OutBack(float t)
{
    constexpr float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

// 场景转场：inOut（有起有落，像镜头运动）
inline float InOutCubic(float t)
{
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}
inline float InOutQuint(float t)
{
    return t < 0.5f ? 16.0f * t * t * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
}

// 弹性收尾，用于印章落下、卡片弹入
inline float OutElastic(float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    constexpr float c4 = 6.283185307f / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

} // namespace ease

// ---------- 平滑跟随量：给定目标，自己慢慢追上去 ----------
struct Smooth
{
    float value = 0.0f;
    float target = 0.0f;
    float k = 0.20f;     // 每 60 帧完成比例

    Smooth() = default;
    explicit Smooth(float initial, float kk = 0.20f)
        : value(initial), target(initial), k(kk) {}

    void Snap(float v) { value = target = v; }
    void Update(float dt) { value += (target - value) * LerpFactor(k, dt); }
    bool Settled(float eps = 0.001f) const { return std::fabs(target - value) < eps; }
    operator float() const { return value; }
};

// ---------- 一次性补间 ----------
struct Tween
{
    float from = 0.0f, to = 1.0f;
    float duration = 0.3f;
    float elapsed = 0.0f;
    float delay = 0.0f;
    float (*fn)(float) = ease::OutCubic;
    bool  running = false;

    void Start(float f, float t, float d, float (*e)(float), float delaySec = 0.0f)
    {
        from = f; to = t; duration = (std::max)(d, 0.0001f);
        fn = e; delay = delaySec; elapsed = 0.0f; running = true;
    }
    void Update(float dt) { if (running) elapsed += dt; }
    bool Done() const { return running && elapsed >= delay + duration; }
    float Value() const
    {
        if (!running) return from;
        float t = (elapsed - delay) / duration;
        if (t <= 0.0f) return from;
        if (t >= 1.0f) return to;
        return from + (to - from) * fn(t);
    }
};

// ---------- 弹簧：用于有物理感的位移（卡片拖拽回弹等） ----------
struct Spring
{
    float value = 0.0f, target = 0.0f, velocity = 0.0f;
    float stiffness = 170.0f, damping = 22.0f;

    void Snap(float v) { value = target = v; velocity = 0.0f; }
    void Update(float dt)
    {
        dt = (std::min)(dt, 0.05f);
        float a = stiffness * (target - value) - damping * velocity;
        velocity += a * dt;
        value += velocity * dt;
    }
    operator float() const { return value; }
};

// ---------- 交错延迟：第 i 个元素的入场延迟 ----------
inline float StaggerDelay(int index, float step = 0.045f, float maxDelay = 0.6f)
{
    return (std::min)(index * step, maxDelay);
}

// ---------- 时长语义常量（映射到 Smooth/Tween duration 参数） ----------
namespace dur {
    constexpr float Instant = 0.10f;
    constexpr float Micro   = 0.18f;
    constexpr float Quick   = 0.30f;
    constexpr float Normal  = 0.50f;
    constexpr float Slow    = 0.80f;
    constexpr float Page    = 0.62f;
}

// ---------- 缓动语义别名（映射到既有 ease:: 函数） ----------
namespace se {
    inline float Hover(float t)   { return ease::OutCubic(t);   }
    inline float Press(float t)   { return ease::OutQuart(t);   }
    inline float Enter(float t)   { return ease::OutBack(t);    }
    inline float Scene(float t)   { return ease::InOutCubic(t); }
    inline float Elastic(float t) { return ease::OutElastic(t); }
    inline float Reveal(float t)  { return ease::OutCubic(t);   }
}

} // namespace lj
