#pragma once
// ============================================================
//  TopBar.h — 自定义标题栏 +「档案条」导航
//  无系统边框，整条自绘：品牌 / 导航 / 主题 / 窗口按钮
// ============================================================
#include "ui/Widget.h"
#include <functional>
#include <vector>
#include <utility>

namespace lj {

// 导航标签：下方指示线滑动
class NavTab : public Widget
{
public:
    std::wstring label;
    std::wstring routeId;
    bool active = false;
    void Paint(Canvas& cv) override;
};

// 窗口控制按钮
class WinButton : public Widget
{
public:
    enum class Kind { Minimize, Maximize, Restore, Close };
    Kind kind = Kind::Close;
    void Paint(Canvas& cv) override;
};

class TopBar
{
public:
    void Init();
    void Layout(float left, float top, float width, float height);
    // chromeOnly：封面/加载页不显示导航条，但窗口按钮必须始终可点
    void Update(float dt, const Input& in, const std::wstring& currentRoute, bool chromeOnly = false);
    void Paint(Canvas& cv, bool chromeOnly = false);

    bool  HoveringAny() const { return m_hoverAny; }
    // 供 WM_NCHITTEST：这些矩形不可拖拽窗口
    const std::vector<D2D1_RECT_F>& BlockedRects() const { return m_blocked; }
    const std::vector<D2D1_RECT_F>& ChromeRects() const { return m_blockedChrome; }

    std::function<void(const std::wstring&)> onNavigate;
    std::function<void()> onMinimize, onMaximize, onClose, onToggleTheme;
    std::function<void()> onAccount;     // 预留：账户胶囊（现改为弹层，不再直接跳转）
    std::function<void()> onLogout;      // 弹层内「退出登录」

    void SetMaximized(bool v) { m_maximized = v; }
    float Height() const { return m_height; }

    // 账户弹层
    bool AccountPopupOpen() const { return m_accountOpen; }
    void ForceAccountPopup() { m_accountOpen = true; m_accountAnim = 1.0f; }  // 仅截图自检用

private:
    std::vector<NavTab>   m_tabs;
    std::vector<WinButton> m_winButtons;
    Button                m_themeBtn;
    std::vector<Widget*>  m_all;
    std::vector<D2D1_RECT_F> m_blocked;
    std::vector<D2D1_RECT_F> m_blockedChrome;

    D2D1_RECT_F m_accountRect{};     // 账户胶囊
    bool m_accountHover = false;

    // 账户弹层（点击胶囊弹出，仿网页端）
    bool        m_accountOpen = false;
    float       m_accountAnim = 0.0f;        // 0→关闭 1→展开（缓动）
    bool        m_logoutHover = false;
    D2D1_RECT_F m_accountPanel{};            // 弹层卡片
    D2D1_RECT_F m_logoutBtn{};               // 退出按钮

    // 个人中心入口（从顶栏收进账户卡片：我的 / 成就 / 数据看板 / 管理）
    std::vector<std::pair<std::wstring, std::wstring>> m_acctItems;  // (label, route)
    std::vector<D2D1_RECT_F> m_acctItemRects;
    int         m_acctHoverIdx = -1;

    float m_left = 0, m_top = 0, m_width = 0, m_height = shape::kTitleBar;
    // 活动指示线：在标签之间滑动
    Smooth m_indX{ 0.0f, 0.15f }, m_indW{ 0.0f, 0.15f };
    bool m_indInit = false;
    bool m_hoverAny = false;
    bool m_maximized = false;
};

} // namespace lj
