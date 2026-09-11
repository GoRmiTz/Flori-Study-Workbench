#include "views/LoginView.h"
#include "core/Hwnd.h"
#include "ui/Layout.h"

namespace lj {

namespace {
bool Hit(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
} // namespace

// ============================================================
void LoginView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_errLogin.clear();
    m_errReg.clear();
    // 有账户时默认显示列表；无账户时直接进登录页
    auto list = AccountStore::Instance().List();
    m_page = list.empty() ? PG_LOGIN : PG_LIST;
    if (m_editingOn) CancelEdit();
}

void LoginView::OnLeave()
{
    if (m_editingOn) CommitEdit();
}

void LoginView::BeginEdit(const Field& fld)
{
    std::wstring cur;
    switch (fld.f) {
        case F_LOGIN_USER:   cur = m_loginUser; break;
        case F_LOGIN_PASS:   cur = m_loginPass; break;
        case F_REG_USER:     cur = m_regUser; break;
        case F_REG_PASS:     cur = m_regPass; break;
        case F_REG_CONFIRM:  cur = m_regConfirm; break;
    }
    CommitEdit();                        // 收尾上一个字段
    m_editing = fld;
    m_editingOn = true;
    // v2 统一输入框：1×1 透明代理只收键盘 + IME，字段上没有任何
    // GDI 子窗口 —— 文字/光标/选区/组合串全由 D3D 绘制（无白块）。
    m_edit.onEnter     = [this] { CommitEdit(); };
    m_edit.onEsc       = [this] { CancelEdit(); };
    m_edit.onKillFocus = [this] { CommitEdit(); };
    // 字段初值按当前显隐状态进入编辑（密码 + 未显示 → 掩码开）
    m_edit.Begin(cur, fld.pass && !m_reveal[(int)fld.f], 14.0f);
}

void LoginView::DebugForceOpen()
{
    // 截图自检：强制进登录页并打开登录用户编辑，验证 D3D 自绘编辑态
    m_page = PG_LOGIN;
    Field f{}; f.f = F_LOGIN_USER; f.pass = false;
    BeginEdit(f);
}

void LoginView::CommitEdit()
{
    if (!m_editingOn) { m_editingOn = false; return; }
    std::wstring txt;
    m_edit.End(true, txt);
    switch (m_editing.f) {
        case F_LOGIN_USER:   m_loginUser = txt; break;
        case F_LOGIN_PASS:   m_loginPass = txt; break;
        case F_REG_USER:     m_regUser = txt; break;
        case F_REG_PASS:     m_regPass = txt; break;
        case F_REG_CONFIRM:  m_regConfirm = txt; break;
    }
    m_editingOn = false;
}

void LoginView::CancelEdit()
{
    m_edit.Cancel();
    m_editingOn = false;
}

// ============================================================
void LoginView::DoLogin()
{
    std::wstring err;
    if (AccountStore::Instance().Login(m_loginUser, m_loginPass, err))
        EnterHome();
    else
        m_errLogin = err;
}

void LoginView::DoRegister()
{
    if (m_regPass != m_regConfirm) { m_errReg = L"两次输入的密码不一致"; return; }
    std::wstring err;
    if (AccountStore::Instance().Register(m_regUser, m_regPass, err))
        EnterHome();
    else
        m_errReg = err;
}

void LoginView::DoQuick(const std::wstring& name)
{
    AccountStore::Instance().QuickLogin(name);
    EnterHome();
}

void LoginView::DoGuest()
{
    AccountStore::Instance().Guest();
    EnterHome();
}

void LoginView::DoLogout()
{
    if (m_editingOn) CancelEdit();
    AccountStore::Instance().Logout();
    m_page = PG_LOGIN;                  // 回到登录页（语义贴合「回到登录」）
    Layout(m_areaCached, *m_cvCached);   // 重新布局
}

void LoginView::EnterHome()
{
    if (m_editingOn) CommitEdit();
    Content::ApplyCurrentAccount();
    CheckinStore::Instance().Reload();
    AppSyncTheme(false);
    // 登录/注册成功直接进主应用：开屏仪式已由 loader→cover 保证
    // （loader 播完一律先封面，封面进入时无会话才落到本页），不再二进封面。
    Go(L"home");
}

// ============================================================
void LoginView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    m_areaCached = area;
    m_cvCached = &cv;
    m_built = false;

    float availW = area.right - area.left;
    float contentW = (std::min)(520.0f, availW - 72.0f);   // 单列居中，比原来更宽
    float x0 = area.left + (availW - contentW) * 0.5f;

    auto& acc = AccountStore::Instance();
    auto list = acc.List();
    bool hasAccounts = !list.empty();

    // 三条流（账户列表 / 登录页 / 注册页）以 bodyTop 为起点构建。
    // 跑两遍：第一遍测量内容高度，第二遍把整块 UI 在视口内垂直居中。
    auto BuildFlows = [&](float bodyTop) -> float {
        m_fields.clear();
        m_accounts.clear();
        for (auto& e : m_eyeRect) e = D2D1_RECT_F{};

        m_yHead = bodyTop;
        bodyTop += 124.0f;   // 标题 + 副标题预留

        auto addField = [&](lj::ui::VLayout& flow, float h, F id, bool pwd) {
            float top = flow.block(h).top;
            m_fields.push_back({ { x0, top, x0 + contentW, top + h }, id, pwd });
            if (pwd)   // 密码框：右侧留出眼睛图标区
                m_eyeRect[(int)id] = { x0 + contentW - 40.0f, top, x0 + contentW - 12.0f, top + h };
        };

        // ---- 账户列表 ----
        {
            lj::ui::VLayout listFlow(x0, bodyTop, contentW, 0.0f);
            m_yListHead = listFlow.cursorY;
            listFlow.block(34.0f);   // "已有账户" 标题

            for (const auto& a : list) {
                float top = listFlow.block(64.0f + 10.0f).top;
                m_accounts.push_back({ { x0, top, x0 + contentW, top + 64.0f }, a.username });
            }

            {
                float top = listFlow.block(56.0f + 8.0f).top;
                m_guestRow = { x0, top, x0 + contentW, top + 56.0f };
            }

            if (acc.HasSession() && !acc.IsGuest()) {
                float top = listFlow.block(40.0f + 16.0f).top;
                m_logoutRow = { x0, top, x0 + contentW, top + 40.0f };
            } else {
                m_logoutRow = { 0, 0, 0, 0 };
            }

            if (hasAccounts) {
                float top = listFlow.block(42.0f + 24.0f).top;
                m_otherBtn = { x0, top, x0 + contentW, top + 42.0f };
            } else {
                m_otherBtn = { 0, 0, 0, 0 };
            }
            m_listBottom = listFlow.cursorY;
        }

        // ---- 登录页：只有登录表单（注册切到独立页）----
        {
            lj::ui::VLayout flow(x0, bodyTop, contentW, 0.0f);
            m_yLogHead = flow.cursorY;
            flow.block(34.0f);   // 「登录」标题

            addField(flow, 46.0f, F_LOGIN_USER, false);
            flow.block(12.0f);
            addField(flow, 46.0f, F_LOGIN_PASS, true);
            flow.block(18.0f);
            {
                float top = flow.block(48.0f).top;
                m_btnLogin = { x0, top, x0 + contentW, top + 48.0f };
                m_loginErrR = m_btnLogin;
            }
            flow.block(14.0f);

            // 访客入口：本地模式，不进云端
            {
                float top = flow.block(46.0f).top;
                m_guestLoginRow = { x0, top, x0 + contentW, top + 46.0f };
            }
            flow.block(16.0f);

            // 「注册新账户 →」链接：切到独立注册页
            {
                float top = flow.cursorY;
                m_linkReg = { x0, top, x0 + contentW, top + 26.0f };
                flow.block(26.0f);
            }

            if (hasAccounts) {
                flow.block(6.0f);
                float top = flow.cursorY;
                m_backToList = { x0, top, x0 + contentW, top + 24.0f };
                flow.block(24.0f + 20.0f);
            } else {
                m_backToList = { 0, 0, 0, 0 };
                flow.block(20.0f);
            }
            m_logBottom = flow.cursorY;
        }

        // ---- 注册页：独立表单 ----
        {
            lj::ui::VLayout flow(x0, bodyTop, contentW, 0.0f);
            m_yRegHead = flow.cursorY;
            flow.block(34.0f);   // 「注册新账户」标题

            addField(flow, 46.0f, F_REG_USER, false);
            flow.block(12.0f);
            addField(flow, 46.0f, F_REG_PASS, true);
            flow.block(12.0f);
            addField(flow, 46.0f, F_REG_CONFIRM, true);
            flow.block(18.0f);
            {
                float top = flow.block(48.0f).top;
                m_btnReg = { x0, top, x0 + contentW, top + 48.0f };
                m_regErrR = m_btnReg;
            }
            flow.block(22.0f);

            // 「← 返回登录」
            {
                float top = flow.cursorY;
                m_backToLogin = { x0, top, x0 + contentW, top + 24.0f };
                flow.block(24.0f + 20.0f);
            }
            m_regBottom = flow.cursorY;
        }

        return m_page == PG_LIST ? m_listBottom
             : m_page == PG_REG  ? m_regBottom
                                 : m_logBottom;
    };

    // 第一遍：测量
    float probeTop = area.top + m_topPad;
    float probeBottom = BuildFlows(probeTop);
    float contentH = probeBottom - probeTop;
    float availH = area.bottom - area.top;

    // 第二遍：内容放得下就整块垂直居中（放不下保持顶部起点，交给滚动）
    float startTop = probeTop;
    if (contentH + m_topPad < availH) {
        float centered = area.top + (availH - contentH) * 0.5f;
        if (centered > startTop) startTop = centered;
    }
    float bottom = (startTop != probeTop) ? BuildFlows(startTop) : probeBottom;

    SetContentHeight(bottom - area.top + 40.0f);
    m_built = true;
}

void LoginView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;

    // 「使用其他账户登录」点击动效：按下瞬间脉冲，随后缓动回落
    m_otherAnim += (0.0f - m_otherAnim) * (1.0f - expf(-dt * 8.0f));
    if (m_otherAnim < 0.004f) m_otherAnim = 0.0f;

    m_hot = { 0, 0, 0, 0 };
    auto hit = [&](const D2D1_RECT_F& r) {
        if (r.right > r.left && Hit(r, in.mouseX, in.mouseY)) m_hot = r;
    };

    // ---- 眼睛图标（密码显隐）：先于字段命中处理，点眼睛只切换显隐 ----
    m_eyeHot = (m_page == PG_LOGIN && Hit(m_eyeRect[1], in.mouseX, in.mouseY))
            || (m_page == PG_REG && (Hit(m_eyeRect[3], in.mouseX, in.mouseY) ||
                                     Hit(m_eyeRect[4], in.mouseX, in.mouseY)));
    int eyeHit = -1;
    if (in.clicked && m_eyeHot) {
        if (m_page == PG_LOGIN && Hit(m_eyeRect[1], in.mouseX, in.mouseY)) eyeHit = 1;
        else if (m_page == PG_REG) {
            if (Hit(m_eyeRect[3], in.mouseX, in.mouseY)) eyeHit = 3;
            else if (Hit(m_eyeRect[4], in.mouseX, in.mouseY)) eyeHit = 4;
        }
    }
    if (eyeHit >= 0) {
        m_reveal[eyeHit] = !m_reveal[eyeHit];
        // 正在编辑该字段：实时切换掩码（不动缓冲）
        if (m_editingOn && m_editing.f == (F)eyeHit) m_edit.SetPassword(!m_reveal[eyeHit]);
    }

    if (m_page == PG_LIST) {
        // 账户列表页
        for (auto& a : m_accounts) hit(a.r);
        hit(m_guestRow);
        hit(m_logoutRow);
        hit(m_otherBtn);
    } else if (m_page == PG_LOGIN && m_fields.size() >= 5) {
        // 登录页
        hit(m_fields[0].r); hit(m_fields[1].r);
        hit(m_btnLogin); hit(m_guestLoginRow); hit(m_linkReg); hit(m_backToList);
    } else if (m_page == PG_REG && m_fields.size() >= 5) {
        // 注册页
        hit(m_fields[2].r); hit(m_fields[3].r); hit(m_fields[4].r);
        hit(m_btnReg); hit(m_backToLogin);
    }

    // 编辑态：鼠标先交给输入框（点击定位光标 / 拖拽框选），
    // 按下点落在字段外才收尾（点眼睛的本帧跳过，防光标乱跳）；
    bool eyeOnEditing = (eyeHit >= 0 && m_editingOn && m_editing.f == (F)eyeHit);
    if (m_editingOn && m_cvCached && !eyeOnEditing) {
        TextStyle tx; tx.role = FontRole::Sans; tx.size = 14.0f;
        tx.vAlign = VAlign::Middle; tx.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bool inside = m_edit.HandleMouse(in, *m_cvCached, m_editing.r, tx);
        if (!inside && in.pressed) CommitEdit();
    }

    // Tab / Shift+Tab：在当前页的字段间跳转
    if (m_editingOn && !eyeOnEditing && in.keyDown[VK_TAB] && m_page != PG_LIST) {
        int n    = (m_page == PG_LOGIN) ? 2 : 3;
        int base = (m_page == PG_LOGIN) ? 0 : 2;
        int cur  = (int)m_editing.f - base;
        if (cur >= 0 && cur < n) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int nxt = shift ? (cur + n - 1) % n : (cur + 1) % n;
            CommitEdit();
            BeginEdit(m_fields[base + nxt]);
        }
    }

    if (in.clicked) {
        if (m_page == PG_LIST) {
            // 列表页的点击
            for (auto& a : m_accounts)
                if (Hit(a.r, in.mouseX, in.mouseY)) { DoQuick(a.name); return; }
            if (Hit(m_guestRow, in.mouseX, in.mouseY)) { DoGuest(); return; }
            if (m_logoutRow.right > m_logoutRow.left &&
                Hit(m_logoutRow, in.mouseX, in.mouseY)) { DoLogout(); return; }
            if (m_otherBtn.right > m_otherBtn.left &&
                Hit(m_otherBtn, in.mouseX, in.mouseY)) {
                m_otherAnim = 1.0f;
                m_page = PG_LOGIN;
                Layout(m_areaCached, *m_cvCached);
                return;
            }
        } else if (m_page == PG_LOGIN) {
            // 登录页的点击
            if (Hit(m_guestLoginRow, in.mouseX, in.mouseY)) { DoGuest(); return; }
            if (eyeHit >= 0) return;                       // 点眼睛：本帧已消费
            if (m_linkReg.right > m_linkReg.left &&
                Hit(m_linkReg, in.mouseX, in.mouseY)) {
                m_page = PG_REG;                  // 切到独立注册页
                Layout(m_areaCached, *m_cvCached);
                return;
            }
            if (m_backToList.right > m_backToList.left &&
                Hit(m_backToList, in.mouseX, in.mouseY)) {
                m_page = PG_LIST;
                Layout(m_areaCached, *m_cvCached);
                return;
            }
            if (Hit(m_btnLogin, in.mouseX, in.mouseY)) { DoLogin(); return; }
            for (int i = 0; i <= 1; ++i)
                if (Hit(m_fields[i].r, in.mouseX, in.mouseY)) { BeginEdit(m_fields[i]); return; }
        } else {
            // 注册页的点击
            if (eyeHit >= 0) return;                       // 点眼睛：本帧已消费
            if (m_backToLogin.right > m_backToLogin.left &&
                Hit(m_backToLogin, in.mouseX, in.mouseY)) {
                m_page = PG_LOGIN;                // 返回登录
                Layout(m_areaCached, *m_cvCached);
                return;
            }
            if (Hit(m_btnReg, in.mouseX, in.mouseY)) { DoRegister(); return; }
            for (int i = 2; i <= 4; ++i)
                if (Hit(m_fields[i].r, in.mouseX, in.mouseY)) { BeginEdit(m_fields[i]); return; }
        }
    }
}

void LoginView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(520.0f, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;

    cv.PushClip(m_area);

    // ---- 头部（两种模式共用）----
    float a = Clamp01(m_t / 0.5f);
    cv.PushOpacity(a);
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"ACCOUNT · 账户档案", { x0, m_yHead, x0 + 360.0f, m_yHead + 16.0f }, sec, pal.ink300);
    TextStyle h1; h1.role = FontRole::Serif; h1.size = 34.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"登入你的芙洛理", x0, m_yHead + 20.0f, h1, pal.ink900, m_t * 1.1f, 0.04f, 16.0f);
    TextStyle lead; lead.role = FontRole::Sans; lead.size = 12.5f;
    const wchar_t* sub = m_page == PG_LIST ? L"选择已有账户快速进入，或使用其他账户登录。"
                       : m_page == PG_REG  ? L"创建一个全新的芙洛理档案，注册后自动登入。"
                                           : L"输入账户信息登录；也可以访客身份本地试用，或注册新档案。";
    cv.Text(sub, { x0, m_yHead + 92.0f, x0 + contentW, m_yHead + 112.0f }, lead, pal.ink500);
    cv.PopOpacity();

    // ---- 眼睛图标（自绘，纸面档案风）：圆环 + 瞳孔，显示中加斜杠 ----
    auto DrawEye = [&](const D2D1_RECT_F& box, bool revealed) {
        float cx = (box.left + box.right) * 0.5f;
        float cy = (box.top + box.bottom) * 0.5f;
        auto col = m_eyeHot ? pal.seal : WithAlpha(pal.ink500, 0.9f);
        cv.StrokeCircle(cx, cy, 6.2f, col, 1.2f);
        cv.FillCircle(cx, cy, 2.1f, col);
        if (revealed)   // 明文显示中 → 斜杠提示「点击隐藏」
            cv.Line(cx - 7.6f, cy + 7.6f, cx + 7.6f, cy - 7.6f, col, 1.4f);
    };

    // ---- 字段 / 按钮 绘制助手（登录页与注册页共用）----
    auto DrawField = [&](const Field& f, const std::wstring& placeholder) {
        bool hot = (f.r.left == m_hot.left && f.r.top == m_hot.top);
        bool editing = (m_editingOn && m_editing.f == f.f);
        int idx = (int)f.f;
        cv.FillRoundRect(f.r, shape::kEdgeSoft, pal.paperHi);
        cv.StrokeRoundRect(f.r, shape::kEdgeSoft, hot ? pal.seal : pal.rule, hot ? shape::kStroke : shape::kHair);
        TextStyle tx; tx.role = FontRole::Sans; tx.size = 14.0f;
        tx.vAlign = VAlign::Middle; tx.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        // 密码框：文字区右侧避开眼睛图标
        D2D1_RECT_F tbox{ f.r.left + 14.0f, f.r.top, f.r.right - (f.pass ? 42.0f : 8.0f), f.r.bottom };
        if (editing) {
            // v2 统一输入框：文字/光标/选区/IME 组合串全由 D3D 绘制，
            // 1×1 透明代理只收键盘 + IME，字段上无任何 GDI 子窗口（无白块）
            m_edit.Paint(cv, tbox, tx, pal.ink900, placeholder, pal.ink300, 0.0f);
        } else {
            std::wstring disp;
            switch (f.f) {
                case F_LOGIN_USER:   disp = m_loginUser; break;
                case F_LOGIN_PASS:   disp = m_loginPass; break;
                case F_REG_USER:     disp = m_regUser; break;
                case F_REG_PASS:     disp = m_regPass; break;
                case F_REG_CONFIRM:  disp = m_regConfirm; break;
            }
            if (f.pass && !m_reveal[idx]) disp = std::wstring(disp.size(), L'•');
            bool empty = disp.empty();
            lj::PaintFieldEdit(cv, tbox, tx,
                              empty ? placeholder : disp,
                              empty ? pal.ink300 : pal.ink900, -1, 0.0f, -1, -1);
        }
        if (f.pass) DrawEye(m_eyeRect[idx], m_reveal[idx]);
    };

    auto DrawButton = [&](const D2D1_RECT_F& r, const std::wstring& label, bool hot) {
        cv.FillRoundRect(r, shape::kEdgeSoft, pal.seal);
        cv.StrokeRoundRect(r, shape::kEdgeSoft, pal.seal, shape::kHair);
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.5f;
        ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ts.hAlign = HAlign::Center;
        ts.vAlign = VAlign::Middle; ts.letterSpacing = 1.0f;
        cv.Text(label, r, ts, pal.paperHi);
    };

    if (m_page == PG_LIST) {
        // ================================================================
        //  账户列表
        // ================================================================

        // 卡片背景
        D2D1_RECT_F card{ x0, m_yListHead - 6.0f, x0 + contentW, m_listBottom + 4.0f };
        cv.PaperCard(card, 0.2f, shape::kEdge);
        cv.StrokeRoundRect(card, shape::kEdge, pal.rule, shape::kHair);

        TextStyle gh; gh.role = FontRole::Mono; gh.size = 12.5f;
        gh.weight = DWRITE_FONT_WEIGHT_BOLD; gh.letterSpacing = 1.0f;
        cv.Text(L"已有账户", { x0 + 18.0f, m_yListHead, x0 + contentW - 18.0f, m_yListHead + 22.0f }, gh, pal.seal);

        for (auto& row : m_accounts) {
            bool hot = (row.r.left == m_hot.left && row.r.top == m_hot.top);
            float lift = hot ? -1.0f : 0.0f;
            D2D1_RECT_F r = row.r; r.top += lift; r.bottom += lift;
            cv.FillRoundRect(r, shape::kEdgeSoft, hot ? WithAlpha(pal.seal, 0.10f) : WithAlpha(pal.rule, 0.07f));
            cv.StrokeRoundRect(r, shape::kEdgeSoft, hot ? pal.seal : pal.rule, hot ? shape::kStroke : shape::kHair);
            float cy = (r.top + r.bottom) * 0.5f;
            // 圆形头像 + 首字
            float ar = 18.0f;
            float ax = r.left + 18.0f + ar;
            cv.FillCircle(ax, cy, ar, pal.seal);
            wchar_t ini = row.name.empty() ? L'？' : row.name[0];
            TextStyle it; it.role = FontRole::Serif; it.size = 15.0f;
            it.weight = DWRITE_FONT_WEIGHT_BLACK; it.hAlign = HAlign::Center; it.vAlign = VAlign::Middle;
            cv.Text(std::wstring(1, ini), { ax - ar, cy - ar, ax + ar, cy + ar }, it, pal.paperHi);
            // 名号
            TextStyle un; un.role = FontRole::Sans; un.size = 15.0f;
            un.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; un.vAlign = VAlign::Middle;
            cv.Text(row.name, { ax + ar + 14.0f, r.top, r.right - 44.0f, r.bottom }, un, pal.ink900);
            // 角色副标题
            TextStyle tip; tip.role = FontRole::Mono; tip.size = 10.0f; tip.vAlign = VAlign::Middle;
            cv.Text(L"点击快速进入", { ax + ar + 14.0f, r.bottom - 21.0f, r.right - 44.0f, r.bottom - 5.0f },
                    tip, pal.ink300);
            // 右侧进入箭头
            TextStyle ch; ch.role = FontRole::Sans; ch.size = 17.0f;
            ch.weight = DWRITE_FONT_WEIGHT_BOLD; ch.hAlign = HAlign::Right; ch.vAlign = VAlign::Middle;
            cv.Text(L"›", { r.right - 40.0f, r.top, r.right - 16.0f, r.bottom }, ch, hot ? pal.seal : pal.ink300);
        }

        // 访客
        {
            bool hot = (m_guestRow.left == m_hot.left && m_guestRow.top == m_hot.top);
            cv.FillRoundRect(m_guestRow, shape::kEdgeSoft, hot ? WithAlpha(pal.jade, 0.16f) : WithAlpha(pal.rule, 0.08f));
            cv.StrokeRoundRect(m_guestRow, shape::kEdgeSoft, hot ? pal.jade : pal.rule, shape::kHair);
            TextStyle gt; gt.role = FontRole::Sans; gt.size = 14.0f;
            gt.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; gt.vAlign = VAlign::Middle;
            cv.Text(L"访客模式 · 免登录试用", { m_guestRow.left + 18.0f, m_guestRow.top, m_guestRow.right - 12.0f, m_guestRow.bottom }, gt, pal.jade);
        }

        // 退出当前账户（按钮化样式，非纯文本；点击回到登录表单）
        if (m_logoutRow.right > m_logoutRow.left) {
            bool hot = (m_logoutRow.left == m_hot.left && m_logoutRow.top == m_hot.top);
            cv.FillRoundRect(m_logoutRow, shape::kEdgeSoft,
                             hot ? WithAlpha(pal.vermilion, 0.12f) : WithAlpha(pal.rule, 0.06f));
            cv.StrokeRoundRect(m_logoutRow, shape::kEdgeSoft, hot ? pal.vermilion : pal.rule, shape::kHair);
            // 电源图标
            float cyc = (m_logoutRow.top + m_logoutRow.bottom) * 0.5f;
            float ix = m_logoutRow.left + 22.0f;
            cv.StrokeCircle(ix, cyc, 7.0f, hot ? pal.vermilion : pal.ink500, 1.4f);
            cv.Line(ix, cyc - 7.0f, ix, cyc - 2.0f, hot ? pal.vermilion : pal.ink500, 1.4f);
            TextStyle lo; lo.role = FontRole::Sans; lo.size = 13.0f;
            lo.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; lo.vAlign = VAlign::Middle; lo.hAlign = HAlign::Center;
            cv.Text(L"退出当前账户", m_logoutRow, lo, hot ? pal.vermilion : pal.ink700);
        }

        // 「使用其他账户登录」按钮（含点击动效：按下脉冲 + 轻微下沉）
        if (m_otherBtn.right > m_otherBtn.left) {
            bool hot = (m_otherBtn.left == m_hot.left && m_otherBtn.top == m_hot.top);
            float press = m_otherAnim;                       // 1→0 脉冲
            float dy = (1.0f - press) * 1.6f;                // 下沉量
            D2D1_RECT_F r = m_otherBtn; r.top += dy; r.bottom += dy;
            // 基础描边 + 悬停/按下填充
            float fillAmt = hot ? 0.4f : 0.0f;
            if (press > fillAmt) fillAmt = press;
            cv.FillRoundRect(r, shape::kEdgeSoft,
                             MixColor(WithAlpha(pal.rule, 0.04f),
                                      WithAlpha(pal.seal, 0.22f), fillAmt));
            cv.StrokeRoundRect(r, shape::kEdgeSoft, hot ? pal.seal : pal.ink500,
                               hot ? shape::kStroke : shape::kHair);
            TextStyle ob; ob.role = FontRole::Sans; ob.size = 13.0f;
            ob.weight = DWRITE_FONT_WEIGHT_MEDIUM; ob.hAlign = HAlign::Center; ob.vAlign = VAlign::Middle;
            cv.Text(L"+ 使用其他账户登录", r, ob, hot ? pal.seal : pal.ink700);
        }

    } else if (m_page == PG_LOGIN) {
        // ================================================================
        //  登录页（注册已切到独立页）
        // ================================================================

        // 登录区
        TextStyle rh; rh.role = FontRole::Mono; rh.size = 12.5f;
        rh.weight = DWRITE_FONT_WEIGHT_BOLD; rh.letterSpacing = 1.0f;
        cv.Text(L"登录", { x0, m_yLogHead, x0 + contentW, m_yLogHead + 22.0f }, rh, pal.seal);
        DrawField(m_fields[0], L"账户名");
        DrawField(m_fields[1], L"密码");
        bool lhot = (m_btnLogin.left == m_hot.left && m_btnLogin.top == m_hot.top);
        DrawButton(m_btnLogin, L"登 录", lhot);
        if (!m_errLogin.empty()) {
            TextStyle er; er.role = FontRole::Sans; er.size = 12.5f; er.vAlign = VAlign::Middle;
            cv.Text(m_errLogin, m_loginErrR, er, pal.vermilion);
        }

        // 登录按钮下：访客入口（本地模式，不进云端）
        if (m_guestLoginRow.right > m_guestLoginRow.left) {
            bool hot = (m_guestLoginRow.left == m_hot.left && m_guestLoginRow.top == m_hot.top);
            cv.FillRoundRect(m_guestLoginRow, shape::kEdgeSoft,
                             hot ? WithAlpha(pal.jade, 0.16f) : WithAlpha(pal.rule, 0.08f));
            cv.StrokeRoundRect(m_guestLoginRow, shape::kEdgeSoft, hot ? pal.jade : pal.rule, shape::kHair);
            TextStyle gt; gt.role = FontRole::Sans; gt.size = 13.5f;
            gt.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; gt.vAlign = VAlign::Middle;
            cv.Text(L"访客模式 · 本地试用，不进云端",
                    { m_guestLoginRow.left + 18.0f, m_guestLoginRow.top,
                      m_guestLoginRow.right - 12.0f, m_guestLoginRow.bottom },
                    gt, hot ? pal.jade : pal.ink700);
        }

        // 「注册新账户 →」链接：切到独立注册页
        if (m_linkReg.right > m_linkReg.left) {
            bool hot = (m_linkReg.left == m_hot.left && m_linkReg.top == m_hot.top);
            TextStyle lr; lr.role = FontRole::Sans; lr.size = 12.5f;
            lr.hAlign = HAlign::Center; lr.vAlign = VAlign::Middle;
            cv.Text(L"还没有账户？注册新账户 →", m_linkReg, lr, hot ? pal.seal : pal.ink500);
        }

        // 返回账户列表
        if (m_backToList.right > m_backToList.left) {
            bool hot = (m_backToList.left == m_hot.left && m_backToList.top == m_hot.top);
            TextStyle bl; bl.role = FontRole::Sans; bl.size = 12.5f;
            bl.hAlign = HAlign::Center; bl.vAlign = VAlign::Middle;
            cv.Text(L"← 返回已有账户列表", m_backToList, bl, hot ? pal.seal : pal.ink500);
        }

    } else if (m_page == PG_REG) {
        // ================================================================
        //  注册页（独立）
        // ================================================================

        TextStyle rh; rh.role = FontRole::Mono; rh.size = 12.5f;
        rh.weight = DWRITE_FONT_WEIGHT_BOLD; rh.letterSpacing = 1.0f;
        cv.Text(L"注册新账户", { x0, m_yRegHead, x0 + contentW, m_yRegHead + 22.0f }, rh, pal.seal);
        DrawField(m_fields[2], L"账户名");
        DrawField(m_fields[3], L"密码");
        DrawField(m_fields[4], L"确认密码");
        bool rhot = (m_btnReg.left == m_hot.left && m_btnReg.top == m_hot.top);
        DrawButton(m_btnReg, L"注 册 并 登 入", rhot);
        if (!m_errReg.empty()) {
            TextStyle er; er.role = FontRole::Sans; er.size = 12.5f; er.vAlign = VAlign::Middle;
            cv.Text(m_errReg, m_regErrR, er, pal.vermilion);
        }

        // 「← 返回登录」
        if (m_backToLogin.right > m_backToLogin.left) {
            bool hot = (m_backToLogin.left == m_hot.left && m_backToLogin.top == m_hot.top);
            TextStyle bl; bl.role = FontRole::Sans; bl.size = 12.5f;
            bl.hAlign = HAlign::Center; bl.vAlign = VAlign::Middle;
            cv.Text(L"← 返回登录", m_backToLogin, bl, hot ? pal.seal : pal.ink500);
        }
    }

    cv.PopClip();
}

} // namespace lj
