#include "ui/TopBar.h"
#include "app/Data.h"
#include "app/AccountStore.h"

namespace lj {

namespace {
bool Hit(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
} // namespace


// ---------------- NavTab ----------------
void NavTab::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float h = HoverAmt();
    auto col = active ? pal.seal : MixColor(pal.ink500, pal.ink900, h);

    TextStyle st;
    st.role = FontRole::Sans;
    st.size = 13.0f;
    st.weight = active ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    st.letterSpacing = 1.6f;
    st.hAlign = HAlign::Center;
    st.vAlign = VAlign::Middle;

    // 悬停微上浮
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -h * 1.0f));
    cv.Text(label, bounds, st, col);
    cv.PopTransform();

    if (!active && h > 0.01f) {
        float w = Width() * 0.42f * h;
        float cx = (bounds.left + bounds.right) * 0.5f;
        cv.Line(cx - w, bounds.bottom - 8.0f, cx + w, bounds.bottom - 8.0f,
                WithAlpha(pal.ink300, h * 0.8f), 1.0f);
    }
}

// ---------------- WinButton ----------------
void WinButton::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float h = HoverAmt();
    float cx = (bounds.left + bounds.right) * 0.5f;
    float cy = (bounds.top + bounds.bottom) * 0.5f;

    if (h > 0.005f) {
        auto bg = (kind == Kind::Close) ? WithAlpha(pal.vermilion, h * 0.85f)
                                        : WithAlpha(pal.ink300, h * 0.20f);
        cv.FillRect(bounds, bg);
    }
    auto ic = (kind == Kind::Close && h > 0.4f) ? pal.paperHi
                                                : MixColor(pal.ink500, pal.ink900, h);
    const float r = 5.0f;
    switch (kind) {
        case Kind::Minimize:
            cv.Line(cx - r, cy + 0.5f, cx + r, cy + 0.5f, ic, 1.5f);
            break;
        case Kind::Maximize:
            cv.StrokeRect({ cx - r, cy - r, cx + r, cy + r }, ic, 1.5f);
            break;
        case Kind::Restore:
            cv.StrokeRect({ cx - r, cy - r + 2.0f, cx + r - 2.0f, cy + r }, ic, 1.5f);
            cv.Line(cx - r + 2.0f, cy - r, cx + r, cy - r, ic, 1.5f);
            cv.Line(cx + r, cy - r, cx + r, cy + r - 2.0f, ic, 1.5f);
            break;
        case Kind::Close:
            cv.Line(cx - r, cy - r, cx + r, cy + r, ic, 1.5f);
            cv.Line(cx + r, cy - r, cx - r, cy + r, ic, 1.5f);
            break;
    }
}

// ---------------- TopBar ----------------
void TopBar::Init()
{
    struct Def { const wchar_t* label; const wchar_t* route; };
    const Def defs[] = {
        { L"首页",   L"home"      },
        { L"打卡",   L"checkin"   },
        { L"自习室", L"room"      },
        { L"专栏",   L"roadmap"   },
        { L"资料库", L"materials" },
        { L"知识库", L"knowledge" },
        { L"练考",   L"quiz"      },
        { L"复盘",   L"review"    },
    };
    m_tabs.clear();
    for (const auto& d : defs) {
        NavTab t;
        t.label = d.label;
        t.routeId = d.route;
        std::wstring route = d.route;
        t.onClick = [this, route] { if (onNavigate) onNavigate(route); };
        m_tabs.push_back(t);
    }

    // 个人中心入口：从顶栏收进账户卡片（页面不删只收）
    m_acctItems.clear();
    m_acctItems.push_back({ L"我的主页", L"profile" });
    m_acctItems.push_back({ L"成就",     L"achieve" });
    m_acctItems.push_back({ L"数据看板", L"dash" });
    m_acctItems.push_back({ L"管理",     L"manage"  });
    m_acctItems.push_back({ L"设置",     L"settings" });

    m_winButtons.clear();
    {
        WinButton b;
        b.kind = WinButton::Kind::Minimize;
        b.onClick = [this] { if (onMinimize) onMinimize(); };
        m_winButtons.push_back(b);
        b.kind = WinButton::Kind::Maximize;
        b.onClick = [this] { if (onMaximize) onMaximize(); };
        m_winButtons.push_back(b);
        b.kind = WinButton::Kind::Close;
        b.onClick = [this] { if (onClose) onClose(); };
        m_winButtons.push_back(b);
    }

    m_themeBtn.label = L"明/暗";
    m_themeBtn.fontSize = 11.5f;
    m_themeBtn.onClick = [this] { if (onToggleTheme) onToggleTheme(); };
}

void TopBar::Layout(float left, float top, float width, float height)
{
    m_left = left; m_top = top; m_width = width; m_height = height;

    // 右侧：窗口按钮
    const float bw = 44.0f;
    float x = left + width;
    for (int i = (int)m_winButtons.size() - 1; i >= 0; --i) {
        m_winButtons[i].bounds = { x - bw, top, x, top + height };
        x -= bw;
    }
    // 主题切换
    m_themeBtn.bounds = { x - 62.0f, top + 9.0f, x - 12.0f, top + height - 9.0f };
    // 账户胶囊（主题按钮左侧）
    float capW = 150.0f;
    m_accountRect = { x - 62.0f - 8.0f - capW, top + 9.0f, x - 62.0f - 8.0f, top + height - 9.0f };
    float navRight = m_accountRect.left - 14.0f;

    // 左侧品牌占 168，其后是导航
    float navLeft = left + 178.0f;
    float tabW = 78.0f;
    float total = tabW * m_tabs.size();
    if (navLeft + total > navRight) {
        tabW = (std::max)(52.0f, (navRight - navLeft) / (float)m_tabs.size());
    }
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        float tx = navLeft + i * tabW;
        m_tabs[i].bounds = { tx, top, tx + tabW, top + height };
    }

    m_all.clear();
    for (auto& t : m_tabs) m_all.push_back(&t);
    for (auto& b : m_winButtons) m_all.push_back(&b);
    m_all.push_back(&m_themeBtn);

    m_blocked.clear();
    for (auto* w : m_all) m_blocked.push_back(w->bounds);
    m_blocked.push_back(m_accountRect);

    m_blockedChrome.clear();
    for (auto& b : m_winButtons) m_blockedChrome.push_back(b.bounds);
    m_blockedChrome.push_back(m_themeBtn.bounds);
}

void TopBar::Update(float dt, const Input& in, const std::wstring& currentRoute, bool chromeOnly)
{
    // 最大化按钮外观跟随窗口状态
    for (auto& b : m_winButtons) {
        if (b.kind == WinButton::Kind::Maximize || b.kind == WinButton::Kind::Restore)
            b.kind = m_maximized ? WinButton::Kind::Restore : WinButton::Kind::Maximize;
    }

    m_hoverAny = false;
    if (!chromeOnly) {
        for (auto& t : m_tabs) {
            t.active = (t.routeId == currentRoute);
            t.Update(dt, in);
            if (t.Hovered()) m_hoverAny = true;
        }
    }
    for (auto& b : m_winButtons) {
        b.Update(dt, in);
        if (b.Hovered()) m_hoverAny = true;
    }
    m_themeBtn.Update(dt, in);
    if (m_themeBtn.Hovered()) m_hoverAny = true;

    // 账户胶囊 + 弹层
    m_accountHover = false;
    bool overAcct = false;
    if (!chromeOnly) {
        overAcct = (in.mouseX >= m_accountRect.left && in.mouseX <= m_accountRect.right &&
                    in.mouseY >= m_accountRect.top && in.mouseY <= m_accountRect.bottom);
        if (overAcct) { m_accountHover = true; m_hoverAny = true; }
    }

    // 弹层面板矩形（与胶囊同处顶栏本地坐标；绘制时随顶栏整体平移）
    {
        float pw = 264.0f;
        float px = m_accountRect.right - pw;
        float py = m_accountRect.bottom + 8.0f;
        // 面板高度按菜单项动态计算：头部 110 + 每项 30 + 间隔 10 + 退出按钮 36 + 底边距 14。
        // （旧版写死 286：5 项菜单排到 py+260，退出按钮却在 py+236 起 → 「设置」行被「退出登录」盖住）
        float ih = 30.0f;
        float ph = 110.0f + (float)m_acctItems.size() * ih + 10.0f + 36.0f + 14.0f;
        m_accountPanel = { px, py, px + pw, py + ph };
        float bb = 14.0f;
        m_logoutBtn = { px + bb, py + ph - 50.0f, px + pw - bb, py + ph - 14.0f };
        // 个人中心入口矩形（在分隔线之下、退出按钮之上）
        m_acctItemRects.clear();
        float iy = py + 110.0f;
        for (size_t i = 0; i < m_acctItems.size(); ++i)
            m_acctItemRects.push_back({ px + bb, iy + (float)i * ih, px + pw - bb, iy + (float)i * ih + ih });
    }
    // 个人中心入口 hover 高亮
    m_acctHoverIdx = -1;
    if (m_accountOpen) {
        for (size_t i = 0; i < m_acctItems.size(); ++i) {
            if (Hit(m_acctItemRects[i], in.mouseX, in.mouseY)) {
                m_acctHoverIdx = (int)i;
                m_hoverAny = true;
                break;
            }
        }
    }
    m_logoutHover = m_accountOpen && Hit(m_logoutBtn, in.mouseX, in.mouseY);

    // 开 / 关切换与面板交互（点击胶囊开↔关；弹层外点击关闭；ESC 关闭）
    // 未登录（无会话）：胶囊显示「登录」，点击直接跳登录页，不弹层
    bool hasSession = AccountStore::Instance().HasSession();
    if (in.clicked) {
        if (overAcct) {
            if (!hasSession) {
                if (onAccount) onAccount();   // 未登录 → 跳登录页
            } else {
                m_accountOpen = !m_accountOpen;
            }
        } else if (m_accountOpen) {
            bool handled = false;
            for (size_t i = 0; i < m_acctItems.size(); ++i) {
                if (Hit(m_acctItemRects[i], in.mouseX, in.mouseY)) {
                    m_accountOpen = false;
                    if (onNavigate) onNavigate(m_acctItems[i].second);
                    handled = true;
                    break;
                }
            }
            if (!handled) {
                if (Hit(m_logoutBtn, in.mouseX, in.mouseY)) {
                    m_accountOpen = false;
                    if (onLogout) onLogout();
                } else if (!Hit(m_accountPanel, in.mouseX, in.mouseY)) {
                    m_accountOpen = false;   // 点击弹层外：关闭
                }
            }
        }
    }
    if (m_accountOpen && in.keyDown[VK_ESCAPE]) m_accountOpen = false;

    // 缓动展开
    float targetA = m_accountOpen ? 1.0f : 0.0f;
    m_accountAnim += (targetA - m_accountAnim) * (1.0f - expf(-dt * 14.0f));
    if (m_accountAnim < 0.004f && !m_accountOpen) m_accountAnim = 0.0f;

    // 指示线跟随活动标签；不在任何导航页（登录 / 设置 / 个人 / 管理等）时收起，
    // 否则会残留在上一次的标签下方，看着像「下标停在别处」。
    bool anyActive = false;
    for (auto& t : m_tabs) {
        if (t.active) {
            float w = t.Width() * 0.42f;
            float cx = (t.bounds.left + t.bounds.right) * 0.5f;
            if (!m_indInit) { m_indX.Snap(cx); m_indW.Snap(0.0f); m_indInit = true; }
            m_indX.target = cx;
            m_indW.target = w;
            anyActive = true;
            break;
        }
    }
    if (!anyActive && m_indInit) m_indW.target = 0.0f;   // 宽度收拢到 0 → 指示线隐去
    m_indX.Update(dt);
    m_indW.Update(dt);
}

void TopBar::Paint(Canvas& cv, bool chromeOnly)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();
    D2D1_RECT_F r{ m_left, m_top, m_left + m_width, m_top + m_height };

    // 封面/加载页：只留窗口控件，浮在图纸之上，不画底条
    if (chromeOnly) {
        m_themeBtn.Paint(cv);
        for (auto& b : m_winButtons) b.Paint(cv);
        return;
    }

    // 档案条底：比纸面略深，底部一道实线
    cv.FillRect(r, MixColor(pal.paperHi, pal.paperLo, 0.55f));
    // top edge highlight: subtle 2px lighter line for depth
    cv.Line(r.left, r.top + 0.5f, r.right, r.top + 0.5f,
            WithAlpha(pal.paperHi, 0.35f), 2.0f);
    cv.Line(r.left, r.bottom - 0.5f, r.right, r.bottom - 0.5f, pal.rule, 1.0f);

    // ---- 品牌区 ----
    float bx = r.left + 22.0f;
    float cy = (r.top + r.bottom) * 0.5f;

    // 印记方块
    cv.FillRect({ bx, cy - 9.0f, bx + 18.0f, cy + 9.0f }, pal.seal);
    cv.FillRect({ bx + 5.5f, cy - 3.5f, bx + 12.5f, cy + 3.5f }, pal.paperHi);

    TextStyle bs;
    bs.role = FontRole::Serif; bs.size = 16.0f;
    bs.weight = DWRITE_FONT_WEIGHT_BLACK; bs.letterSpacing = 4.0f;
    bs.vAlign = VAlign::Middle;
    cv.Text(C.appName, { bx + 28.0f, r.top, bx + 96.0f, r.bottom }, bs, pal.ink900);

    TextStyle ls;
    ls.role = FontRole::Mono; ls.size = 9.5f;
    ls.letterSpacing = 2.0f; ls.vAlign = VAlign::Middle;
    cv.Text(L"Flori", { bx + 92.0f, r.top + 1.0f, bx + 150.0f, r.bottom }, ls,
            WithAlpha(pal.ink300, 0.95f));

    // ---- 导航 ----
    for (auto& t : m_tabs) t.Paint(cv);

    // 活动指示线（无活动标签时宽度为 0，不画）
    if (m_indInit && m_indW.value > 0.5f) {
        cv.Line(m_indX.value - m_indW.value, r.bottom - 8.0f,
                m_indX.value + m_indW.value, r.bottom - 8.0f, pal.seal, 2.0f);
    }

    // 账户胶囊
    {
        float cy = (m_accountRect.top + m_accountRect.bottom) * 0.5f;
        cv.FillRoundRect(m_accountRect, 13.0f,
                         m_accountHover ? WithAlpha(pal.seal, 0.14f) : WithAlpha(pal.rule, 0.10f));
        cv.StrokeRoundRect(m_accountRect, 13.0f, m_accountHover ? pal.seal : pal.rule, shape::kHair);
        // 印记小圆
        cv.FillCircle(m_accountRect.left + 16.0f, cy, 6.0f, pal.seal);
        TextStyle at; at.role = FontRole::Sans; at.size = 12.5f;
        at.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; at.vAlign = VAlign::Middle;
        at.hAlign = HAlign::Left;
        // 未登录：显示「登录」入口（点击直接跳登录页），不再是空胶囊
        bool hasSession = AccountStore::Instance().HasSession();
        std::wstring cap = hasSession ? AccountStore::Instance().CurrentName() : L"登录";
        cv.Text(cap,
                { m_accountRect.left + 30.0f, m_accountRect.top, m_accountRect.right - 10.0f, m_accountRect.bottom },
                at, hasSession ? pal.ink900 : pal.seal);
    }

    // ---- 账户弹层（仿网页端：头像 + 名号 + 角色 + 退出；仅已登录可开）----
    if (m_accountAnim > 0.004f) {
        float a = m_accountAnim;
        // slide in from above
        cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - a) * 24.0f));
        cv.PushOpacity(a);
        // 阴影
        D2D1_RECT_F sh{ m_accountPanel.left, m_accountPanel.top + 5.0f,
                        m_accountPanel.right, m_accountPanel.bottom + 5.0f };
        cv.FillRoundRect(sh, shape::kEdge, WithAlpha(pal.ink900, 0.20f * a));
        // 卡片
        cv.FillRoundRect(m_accountPanel, shape::kEdge, WithAlpha(pal.paperHi, 0.99f));
        cv.StrokeRoundRect(m_accountPanel, shape::kEdge, pal.rule, shape::kStroke);

        // 头像圆 + 首字
        float cy0 = m_accountPanel.top + 42.0f;
        float ax = m_accountPanel.left + 30.0f;
        cv.FillCircle(ax, cy0, 22.0f, pal.seal);
        std::wstring nm = AccountStore::Instance().CurrentName();
        wchar_t ini = nm.empty() ? L'？' : nm[0];
        TextStyle it; it.role = FontRole::Serif; it.size = 18.0f;
        it.weight = DWRITE_FONT_WEIGHT_BLACK; it.hAlign = HAlign::Center; it.vAlign = VAlign::Middle;
        cv.Text(std::wstring(1, ini), { ax - 22.0f, cy0 - 22.0f, ax + 22.0f, cy0 + 22.0f }, it, pal.paperHi);

        // 名号
        TextStyle nt; nt.role = FontRole::Sans; nt.size = 15.0f;
        nt.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; nt.vAlign = VAlign::Middle;
        cv.Text(nm, { ax + 40.0f, m_accountPanel.top + 24.0f, m_accountPanel.right - 16.0f, m_accountPanel.top + 46.0f },
                nt, pal.ink900);
        // 角色 / 编号
        std::wstring role = AccountStore::Instance().IsGuest() ? L"访客模式"
                            : (AccountStore::Instance().IsDemoCurrent() ? L"演示账户" : L"注册用户");
        std::wstring sub = AccountStore::Instance().UserId() + L" · " + role;
        TextStyle st; st.role = FontRole::Mono; st.size = 10.5f; st.vAlign = VAlign::Middle;
        cv.Text(sub, { ax + 40.0f, m_accountPanel.top + 48.0f, m_accountPanel.right - 16.0f, m_accountPanel.top + 66.0f },
                st, pal.ink500);

        // 分隔线
        cv.Line(m_accountPanel.left + 14.0f, m_accountPanel.top + 80.0f,
                m_accountPanel.right - 14.0f, m_accountPanel.top + 80.0f,
                pal.rule, shape::kHair);

        // 个人中心入口区（从顶栏收进账户卡片）
        {
            TextStyle ht; ht.role = FontRole::Sans; ht.size = 11.0f;
            ht.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ht.vAlign = VAlign::Middle;
            ht.letterSpacing = 1.0f;
            cv.Text(L"个人中心", { m_accountPanel.left + 14.0f, m_accountPanel.top + 88.0f,
                                   m_accountPanel.right - 14.0f, m_accountPanel.top + 104.0f },
                    ht, pal.ink500);
            for (size_t i = 0; i < m_acctItems.size(); ++i) {
                const D2D1_RECT_F& ir = m_acctItemRects[i];
                bool hov = (m_acctHoverIdx == (int)i);
                if (hov)
                    cv.FillRoundRect(ir, 8.0f, WithAlpha(pal.seal, 0.10f));
                TextStyle it; it.role = FontRole::Sans; it.size = 13.0f; it.vAlign = VAlign::Middle;
                cv.Text(m_acctItems[i].first,
                        { ir.left + 14.0f, ir.top, ir.right - 24.0f, ir.bottom }, it, pal.ink900);
                TextStyle ar; ar.role = FontRole::Sans; ar.size = 14.0f;
                ar.hAlign = HAlign::Right; ar.vAlign = VAlign::Middle;
                cv.Text(L"›", { ir.right - 24.0f, ir.top, ir.right - 6.0f, ir.bottom }, ar, pal.ink300);
            }
        }

        // 退出按钮
        cv.FillRoundRect(m_logoutBtn, shape::kEdgeSoft,
                         m_logoutHover ? WithAlpha(pal.vermilion, 0.16f) : WithAlpha(pal.rule, 0.06f));
        cv.StrokeRoundRect(m_logoutBtn, shape::kEdgeSoft, pal.vermilion, shape::kHair);
        TextStyle lo; lo.role = FontRole::Sans; lo.size = 12.5f;
        lo.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; lo.hAlign = HAlign::Center; lo.vAlign = VAlign::Middle;
        cv.Text(L"退出登录", m_logoutBtn, lo, pal.vermilion);

        cv.PopOpacity();
        cv.PopTransform();
    }

    m_themeBtn.Paint(cv);
    for (auto& b : m_winButtons) b.Paint(cv);
}

} // namespace lj
