#include "ui/Router.h"
#include "ui/ImageViewer.h"   // 切走时立即关闭软件内图片查看全屏遮罩，避免转场前半段残留黑屏

namespace lj {

void Router::Add(std::unique_ptr<View> view)
{
    View* raw = view.get();
    m_index[raw->Id()] = raw;
    m_views.push_back(std::move(view));
    if (!m_current) {
        m_current = raw;
        m_currentId = raw->Id();
        raw->OnEnter();
    }
}

void Router::GoTo(const std::wstring& id, bool immediate)
{
    auto it = m_index.find(id);
    if (it == m_index.end()) { LogLine(L"[router] 未知视图: %s", id.c_str()); return; }
    // 已经站在目标页
    if (it->second == m_current && !m_transitioning) return;
    // 正在转场去同一个目标：绝不能重置 m_t，否则进度被反复清零、转场永远走不完
    if (m_transitioning && m_pendingId == id) return;

    // 发起切换即收起全局图片查看遮罩（资源/视频页可能开着）。否则它只在转场中点
    // 由原视图 OnLeave 关闭，导致切走前半段全屏暗色遮罩残留 → 用户看到「黑屏」。
    ImageViewer::Instance().Close();

    if (immediate) {
        if (m_current) m_current->OnLeave();
        m_current = it->second;
        m_currentId = id;
        m_outgoing = nullptr;
        m_transitioning = false;
        m_current->OnEnter();
        if (m_lastCanvas) m_current->Layout(m_area, *m_lastCanvas);
        return;
    }
    m_pendingId = id;
    m_outgoing = m_current;
    m_transitioning = true;
    m_t = 0.0f;
}

void Router::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    m_area = area;
    m_lastCanvas = &cv;
    if (m_current) m_current->Layout(area, cv);
    if (m_outgoing) m_outgoing->Layout(area, cv);
}

void Router::Update(float dt, const Input& in)
{
    if (m_transitioning) {
        m_t += dt / kDuration;
        // 中点：真正换页
        if (m_t >= 0.5f && !m_pendingId.empty()) {
            auto it = m_index.find(m_pendingId);
            if (it != m_index.end()) {
                if (m_outgoing) m_outgoing->OnLeave();
                m_current = it->second;
                m_currentId = m_pendingId;
                m_current->OnEnter();
                if (m_lastCanvas) m_current->Layout(m_area, *m_lastCanvas);
                LogLine(L"[router] -> %s", m_currentId.c_str());
            }
            m_pendingId.clear();
        }
        if (m_t >= 1.0f) {
            m_t = 1.0f;
            m_transitioning = false;
            m_outgoing = nullptr;
        }
    }

    Input local = in;
    if (InputLocked()) {                 // 转场前半段吞掉点击
        local.pressed = local.released = local.clicked = false;
        local.down = false;
        local.wheel = 0.0f;
    }
    if (m_current) m_current->Update(dt, local);
}

void Router::Paint(Canvas& cv)
{
    if (!m_transitioning) {
        if (m_current) m_current->Paint(cv);
        return;
    }

    // 转场：旧页上移淡出 / 新页落位淡入（纯位移 + 不透明绘制）。
    // 关键约束：交换链 D2D target 带 CANNOT_DRAW，整页 PushLayer(PushOpacity)
    // 合成时无法回读底层 → 透明区落黑/白/纯色；离屏位图合成在部分机器会崩。
    // 故转场只用位移、页面始终不透明、卡片间隙透出 D3D 纸纹，既无黑屏/纯色/闪白，
    // 也绝不触碰离屏缓冲。顶栏在 App 帧循环里于 router 之后绘制，转场永远盖不到导航栏。
    if (m_t < 0.5f) {
        if (!m_outgoing) return;
        cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -m_t * 26.0f));
        m_outgoing->Paint(cv);
        cv.PopTransform();
    } else {
        if (!m_current) return;
        float t = (m_t - 0.5f) / 0.5f;
        cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - t) * 22.0f));
        m_current->Paint(cv);
        cv.PopTransform();
    }
}

} // namespace lj
