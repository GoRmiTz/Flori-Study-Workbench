#pragma once
// ============================================================
//  Router.h — 视图路由与场景转场
//  对应网页的 hashchange：切换时旧页淡出上移、新页显影落位
//  转场缓动一律 inOut（场景级），控件级才用 out
// ============================================================
#include "ui/View.h"
#include <unordered_map>
#include <string>

namespace lj {

class Router
{
public:
    void Add(std::unique_ptr<View> view);
    void GoTo(const std::wstring& id, bool immediate = false);

    void Layout(const D2D1_RECT_F& area, Canvas& cv);
    void Update(float dt, const Input& in);
    void Paint(Canvas& cv);

    View* Current() const { return m_current; }
    const std::wstring& CurrentId() const { return m_currentId; }
    // F-D3：按 id 取已注册视图（用于全局热键触发特定视图动作）
    View* Find(const std::wstring& id) const {
        auto it = m_index.find(id);
        return it == m_index.end() ? nullptr : it->second;
    }
    bool Transitioning() const { return m_transitioning; }
    // 转场期间输入应被吞掉，避免误触
    bool InputLocked() const { return m_transitioning && m_t < 0.55f; }

private:
    std::vector<std::unique_ptr<View>> m_views;
    std::unordered_map<std::wstring, View*> m_index;

    View* m_current = nullptr;
    View* m_outgoing = nullptr;
    std::wstring m_currentId;
    std::wstring m_pendingId;

    bool  m_transitioning = false;
    float m_t = 0.0f;
    static constexpr float kDuration = 0.62f;

    D2D1_RECT_F m_area{};
    Canvas* m_lastCanvas = nullptr;
};

} // namespace lj
