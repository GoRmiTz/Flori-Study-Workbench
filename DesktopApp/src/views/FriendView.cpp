#include "views/FriendView.h"
#include "app/AccountStore.h"   // UserId()：私聊 mine 判定
#include "core/Hwnd.h"
#include "ui/Layout.h"
#include <windows.h>
#include <ctime>

namespace lj {

namespace {
bool Hit(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
std::wstring DmTime(long long ts)
{
    if (ts <= 0) return L"";
    std::tm tmv{};
    time_t tt = (time_t)(ts / 1000);
    localtime_s(&tmv, &tt);
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return buf;
}
} // namespace

// ============================================================
//  输入（v2 统一输入框 FieldEdit：1×1 透明代理 + D3D 自绘，无白块）
// ============================================================
void FriendView::BeginAddEdit()
{
    m_addEdit.onEnter     = [this] { EndAddEdit(true); };
    m_addEdit.onEsc       = [this] { EndAddEdit(false); };
    m_addEdit.onKillFocus = [this] { EndAddEdit(true); };
    m_addEdit.Begin(m_addText, false, 13.0f);
    m_addOn = true;
}

void FriendView::EndAddEdit(bool commit)
{
    std::wstring txt;
    m_addEdit.End(commit, txt);
    if (commit) m_addText = txt;
    m_addOn = false;
    if (commit) {
        std::wstring name = m_addText;
        size_t a = name.find_first_not_of(L" \t");
        size_t b = name.find_last_not_of(L" \t");
        name = (a == std::wstring::npos) ? L"" : name.substr(a, b - a + 1);
        m_addText.clear();
        if (name.empty()) { SetHint(L"请输入用户名"); return; }
        Cloud::RunAsync([this, name]() {
            std::wstring err;
            CloudResult cr = Cloud::Instance().AddFriend(name, err);
            if (cr == CloudResult::Ok) { SetHint(L"已添加：" + name); FetchFriends(); }
            else { SetHint(err.empty() ? L"添加失败" : err); }
        });
    }
}

void FriendView::BeginDmEdit()
{
    m_dmEdit.onEnter     = [this] { EndDmEdit(true); };
    m_dmEdit.onEsc       = [this] { CloseDm(); };
    m_dmEdit.onKillFocus = [this] { EndDmEdit(false); };
    m_dmEdit.Begin(m_dmText, false, 12.5f);
    m_dmOn = true;
}

void FriendView::EndDmEdit(bool send)
{
    std::wstring txt;
    m_dmEdit.End(send, txt);
    if (send) m_dmText = txt;
    m_dmOn = false;
    if (send && !m_dmText.empty()) SendDmText(m_dmText);
    m_dmText.clear();
}

// ============================================================
//  数据
// ============================================================
void FriendView::FetchFriends()
{
    Cloud::RunAsync([this] {
        auto r = Cloud::Instance().GetFriends();
        if (!r.Ok()) return;
        lj::json::Parser pp(r.body.data(), r.body.size());
        lj::json::JVal root = pp.parse();
        if (!root.IsObj()) return;
        std::vector<FriendItem> items;
        const auto* arr = lj::json::JGet(root, "friends");
        if (arr && arr->IsArr()) {
            for (const auto& v : arr->arr) {
                FriendItem it;
                it.uid = net::FromUtf8(lj::json::JStr(v, "uid"));
                it.username = net::FromUtf8(lj::json::JStr(v, "username"));
                const auto* ol = lj::json::JGet(v, "online");
                if (ol) it.online = ol->bval;
                const auto* dd = lj::json::JGet(v, "days");
                if (dd) it.days = (long long)dd->num;
                const auto* fm = lj::json::JGet(v, "focusMinutes");
                if (fm) it.focusMin = (long long)fm->num;
                if (!it.uid.empty()) items.push_back(std::move(it));
            }
        }
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_src = std::move(items);
            m_fetched = true;
        }
        m_dirty.store(true);
    });
}

void FriendView::Snapshot()
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_friends = m_src;
    }
    if (!m_fetched) return;
    // 重建卡片矩形（Layout 会再算，这里先保证数量一致）
    m_cardRects.assign(m_friends.size(), { 0,0,0,0 });
    m_delBtns.assign(m_friends.size(), { 0,0,0,0 });
    m_dmBtns.assign(m_friends.size(), { 0,0,0,0 });
}

void FriendView::SetHint(const std::wstring& s)
{
    std::lock_guard<std::mutex> lk(m_mu);
    m_hint = s;
    m_hintT = (long long)time(nullptr) + 4;
}

// ============================================================
//  私聊
// ============================================================
void FriendView::OpenDm(const FriendItem& f)
{
    m_dmPeer = f;
    m_dmOpen = true;
    m_dmScroll = 0;
    {
        std::lock_guard<std::mutex> lk(m_dmMu);
        m_dmSrc.clear();
        m_dmMsgs.clear();
    }
    // 拉历史
    Realtime::Instance().RequestDmHistory(net::ToUtf8(f.uid));
    // 编辑态：进入浮层后等一帧（等 m_dmInput 布局），Update 里 BeginDmEdit
}

void FriendView::CloseDm()
{
    EndDmEdit(false);
    m_dmOpen = false;
}

void FriendView::SendDmText(const std::wstring& t)
{
    if (t.empty() || m_dmPeer.uid.empty()) return;
    Realtime::Instance().SendDm(net::ToUtf8(m_dmPeer.uid), net::ToUtf8(t));
}

void FriendView::OnNetDm(const lj::json::JVal& m)
{
    lj::json::JVal msg = lj::json::JGet(m, "msg") ? *lj::json::JGet(m, "msg") : lj::json::JVal{};
    DmMsg dm;
    const auto* from = lj::json::JGet(m, "from");
    dm.mine = false;
    if (from) dm.mine = (net::FromUtf8(from->str) == AccountStore::Instance().UserId());
    dm.text = net::FromUtf8(lj::json::JStr(msg, "text"));
    const auto* ts = lj::json::JGet(msg, "ts");
    if (ts) dm.ts = (long long)ts->num;
    {
        std::lock_guard<std::mutex> lk(m_dmMu);
        m_dmSrc.push_back(dm);
        if (m_dmSrc.size() > 300) m_dmSrc.erase(m_dmSrc.begin());
    }
    m_dmDirty.store(true);
}

void FriendView::OnNetDmHistory(const lj::json::JVal& m)
{
    std::wstring with = net::FromUtf8(lj::json::JStr(m, "with"));
    if (with != m_dmPeer.uid) return;   // 非当前会话的历史忽略
    std::vector<DmMsg> items;
    const auto* arr = lj::json::JGet(m, "list");
    if (arr && arr->IsArr()) {
        for (const auto& v : arr->arr) {
            DmMsg dm;
            const auto* from = lj::json::JGet(v, "from");
            if (from) dm.mine = (net::FromUtf8(from->str) == AccountStore::Instance().UserId());
            dm.text = net::FromUtf8(lj::json::JStr(v, "text"));
            const auto* ts = lj::json::JGet(v, "ts");
            if (ts) dm.ts = (long long)ts->num;
            items.push_back(std::move(dm));
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_dmMu);
        m_dmSrc = std::move(items);
    }
    m_dmDirty.store(true);
    m_dmScroll = 1e9f;   // 拉历史后滚到底
}

// ============================================================
//  生命周期
// ============================================================
void FriendView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_fetched = false;
    auto rt = &Realtime::Instance();
    rt->On("chat:dm",      [this](const lj::json::JVal& m) { OnNetDm(m); });
    rt->On("dm:history",   [this](const lj::json::JVal& m) { OnNetDmHistory(m); });
    FetchFriends();
}

void FriendView::OnLeave()
{
    EndAddEdit(false);
    EndDmEdit(false);
    m_dmOpen = false;
    auto rt = &Realtime::Instance();
    rt->Off("chat:dm");
    rt->Off("dm:history");
}

void FriendView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    m_cvCached = &cv;   // FieldEdit::HandleMouse 测量宽度需要
    const float availW = area.right - area.left;
    const float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    const float x0 = area.left + (availW - contentW) * 0.5f;

    // 纵向流式布局：区块只声明自身高度，位置由 flow 推进
    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);

    // 标题区
    m_contentTop = flow.cursorY;
    flow.block(96.0f);

    // 添加区
    {
        float top = flow.block(40.0f + 8.0f).top;
        m_addBox = { x0, top, x0 + contentW - 130.0f, top + 40.0f };
        m_addBtn = { x0 + contentW - 118.0f, top, x0 + contentW, top + 40.0f };
    }

    // 好友列表
    m_listY = flow.cursorY;
    m_cardRects.clear(); m_delBtns.clear(); m_dmBtns.clear();
    for (size_t i = 0; i < m_friends.size(); ++i) {
        float top = flow.block(58.0f + 8.0f).top;
        D2D1_RECT_F r{ x0 + 14.0f, top, x0 + contentW - 14.0f, top + 58.0f };
        m_cardRects.push_back(r);
        m_dmBtns.push_back({ r.right - 148.0f, r.top + 13.0f, r.right - 80.0f, r.top + 45.0f });
        m_delBtns.push_back({ r.right - 70.0f, r.top + 13.0f, r.right - 14.0f, r.top + 45.0f });
    }
    flow.block(20.0f);

    // 返回
    m_backBtn.label = L"返 回 首 页";
    m_backBtn.tag = L"00";
    m_backBtn.fontSize = 13.0f;
    {
        float top = flow.block(46.0f + 30.0f).top;
        m_backBtn.bounds = { x0, top, x0 + 150.0f, top + 46.0f };
    }
    m_backBtn.onClick = [this] { Go(L"home"); };

    SetContentHeight(flow.cursorY - area.top);

    // 私聊浮层布局
    const float dw = (std::min)(560.0f, area.right - area.left - 80.0f);
    const float dh = (std::min)(460.0f, area.bottom - area.top - 80.0f);
    const float cx = (area.left + area.right) * 0.5f;
    const float cy = (area.top + area.bottom) * 0.5f;
    m_dmCard = { cx - dw * 0.5f, cy - dh * 0.5f, cx + dw * 0.5f, cy + dh * 0.5f };
    m_dmList = { m_dmCard.left + 16.0f, m_dmCard.top + 52.0f, m_dmCard.right - 16.0f, m_dmCard.bottom - 66.0f };
    m_dmInput = { m_dmCard.left + 16.0f, m_dmCard.bottom - 52.0f, m_dmCard.right - 96.0f, m_dmCard.bottom - 14.0f };
    m_dmSend = { m_dmCard.right - 84.0f, m_dmCard.bottom - 52.0f, m_dmCard.right - 16.0f, m_dmCard.bottom - 14.0f };
    m_dmClose = { m_dmCard.right - 42.0f, m_dmCard.top + 12.0f, m_dmCard.right - 12.0f, m_dmCard.top + 42.0f };

    m_widgets.clear();
    m_widgets.push_back(&m_backBtn);
}

void FriendView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);
    float mx = in.mouseX, my = shifted.mouseY;

    // 提示过期
    if (m_hintT > 0 && (long long)time(nullptr) > m_hintT) { m_hint.clear(); m_hintT = 0; }

    // 好友快照消费
    if (m_dirty.exchange(false)) Snapshot();

    // 私聊快照消费 + 自动滚底
    if (m_dmDirty.exchange(false)) {
        std::lock_guard<std::mutex> lk(m_dmMu);
        m_dmMsgs = m_dmSrc;
        m_dmScroll = 1e9f;
    }
    if (m_dmOpen && !m_dmOn && in.clicked) {
        // 打开浮层后首次点击输入区进入编辑态（浮层为屏幕固定坐标，用未滚动鼠标）
        if (Hit(m_dmInput, in.mouseX, in.mouseY)) { BeginDmEdit(); }
    }

    if (!m_dmOpen) {
        // ---- 好友页交互 ----
        // 编辑态：鼠标先交给输入框（点击定位光标 / 拖拽框选），按下点在字段外才收起
        if (m_addOn && m_cvCached) {
            TextStyle as; as.role = FontRole::Sans; as.size = 13.0f; as.vAlign = VAlign::Middle;
            bool inside = m_addEdit.HandleMouse(in, *m_cvCached, m_addBox, as, ScrollY());
            if (!inside && in.pressed) EndAddEdit(false);
        }
        if (in.clicked) {
            if (Hit(m_addBox, mx, my)) { if (!m_addOn) BeginAddEdit(); return; }
            if (Hit(m_addBtn, mx, my)) { if (m_addOn) EndAddEdit(true); else SetHint(L"先点输入框输入用户名"); return; }
            for (size_t i = 0; i < m_friends.size() && i < m_cardRects.size(); ++i) {
                if (!Hit(m_cardRects[i], mx, my)) continue;
                if (Hit(m_delBtns[i], mx, my)) {
                    std::string uid = net::ToUtf8(m_friends[i].uid);
                    Cloud::RunAsync([this, uid] {
                        Cloud::Instance().DeleteFriend(uid);
                        FetchFriends();
                    });
                    return;
                }
                if (Hit(m_dmBtns[i], mx, my)) { OpenDm(m_friends[i]); return; }
                return;
            }
            if (m_addOn) EndAddEdit(false);
        }
        if (m_addOn) m_addText = m_addEdit.Text();
    } else {
        // ---- 私聊浮层交互（屏幕固定坐标，用未滚动鼠标）----
        if (m_dmOn && m_cvCached) {
            TextStyle is; is.role = FontRole::Sans; is.size = 12.5f; is.vAlign = VAlign::Middle;
            bool inside = m_dmEdit.HandleMouse(in, *m_cvCached, m_dmInput, is);
            if (!inside && in.pressed) EndDmEdit(false);
        }
        if (m_dmOn) m_dmText = m_dmEdit.Text();
        // 滚轮
        if (in.wheel != 0.0f) {
            m_dmScroll -= in.wheel * 48.0f;   // wheel 正=向下 → 减小滚动
            m_dmScroll = (std::max)(0.0f, m_dmScroll);
        }
        if (in.clicked) {
            if (Hit(m_dmClose, in.mouseX, in.mouseY)) { CloseDm(); return; }
            if (Hit(m_dmSend, in.mouseX, in.mouseY))  { if (m_dmOn) EndDmEdit(true); return; }
            if (Hit(m_dmInput, in.mouseX, in.mouseY)) { if (!m_dmOn) BeginDmEdit(); return; }
            if (m_dmOn) EndDmEdit(false);
        }
    }
}

void FriendView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    // 标题
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 好友 · 私聊与进度", { x0, m_contentTop, x0 + 420.0f, m_contentTop + 16.0f }, sec, pal.ink300);
    TextStyle h1; h1.role = FontRole::Serif; h1.size = 38.0f; h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"好　友", x0, m_contentTop + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);
    cv.PerforationH(x0, x0 + contentW, m_contentTop + 78.0f, WithAlpha(pal.ruleStrong, 0.6f));

    // 添加区
    D2D1_RECT_F addr{ m_addBox.left - 6.0f, m_addBox.top - 6.0f, m_addBox.right + 6.0f, m_addBox.bottom + 6.0f };
    cv.FillRoundRect(addr, 8.0f, WithAlpha(pal.paperHi, 0.8f));
    cv.StrokeRoundRect(addr, 8.0f, WithAlpha(m_addOn ? pal.jade : pal.rule, m_addOn ? 0.9f : 0.5f), 1.0f);
    TextStyle as; as.role = FontRole::Sans; as.size = 13.0f; as.vAlign = VAlign::Middle;
    if (m_addOn) {
        // v2 统一输入框：D3D 自绘编辑态（1×1 代理不遮字段）
        m_addEdit.Paint(cv, { m_addBox.left + 10.0f, m_addBox.top, m_addBox.right - 8.0f, m_addBox.bottom },
                        as, pal.ink900, L"输入用户名添加好友…", pal.ink300, 0.0f, ScrollY());
    } else {
        lj::PaintFieldEdit(cv, { m_addBox.left + 10.0f, m_addBox.top, m_addBox.right - 8.0f, m_addBox.bottom },
                           as, m_addText.empty() ? L"输入用户名添加好友…" : m_addText,
                           m_addText.empty() ? pal.ink300 : pal.ink900, -1, 0.0f, -1, -1);
    }
    cv.FillRoundRect(m_addBtn, 6.0f, pal.seal);
    TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.0f; bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle; bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    cv.Text(L"添加", m_addBtn, bs, pal.paperHi);
    if (!m_hint.empty()) {
        TextStyle hs; hs.role = FontRole::Sans; hs.size = 11.5f; hs.vAlign = VAlign::Middle; hs.hAlign = HAlign::Right;
        cv.Text(m_hint, { m_addBox.right + 10.0f, m_addBox.top, m_addBox.right + 200.0f, m_addBox.bottom }, hs, pal.vermilion);
    }

    // 好友列表
    if (m_friends.empty()) {
        TextStyle et; et.role = FontRole::Sans; et.size = 12.5f; et.vAlign = VAlign::Middle;
        cv.Text(m_fetched ? L"还没有好友 —— 输入用户名添加一位吧（对方会出现在你的列表，你也会出现在对方的列表）。"
                          : L"加载好友列表…",
                { x0 + 14.0f, m_listY, x0 + contentW - 14.0f, m_listY + 44.0f }, et, pal.ink500);
    }
    for (size_t i = 0; i < m_friends.size() && i < m_cardRects.size(); ++i) {
        const auto& f = m_friends[i];
        const auto& r = m_cardRects[i];
        bool hov = (m_hover == (int)i);
        cv.FillRoundRect(r, shape::kEdgeSoft, WithAlpha(hov ? pal.jade : pal.sealWash, hov ? 0.18f : (pal.dark ? 0.18f : 0.26f)));
        cv.StrokeRoundRect(r, shape::kEdgeSoft, WithAlpha(hov ? pal.jade : pal.rule, hov ? 0.9f : 0.5f), 1.0f);
        // 名字 + uid + 在线
        TextStyle ns; ns.role = FontRole::Sans; ns.size = 14.0f; ns.weight = DWRITE_FONT_WEIGHT_BOLD; ns.vAlign = VAlign::Middle;
        cv.Text(f.username, { r.left + 16.0f, r.top + 6.0f, r.right - 200.0f, r.top + 30.0f }, ns, pal.ink900);
        TextStyle us; us.role = FontRole::Mono; us.size = 10.0f;
        cv.Text(f.uid, { r.left + 16.0f, r.top + 30.0f, r.right - 200.0f, r.top + 46.0f }, us, pal.ink300);
        // 在线 + 进度
        TextStyle ss; ss.role = FontRole::Mono; ss.size = 11.0f; ss.vAlign = VAlign::Middle;
        std::wstring stat = (f.online ? L"● 在线" : L"○ 离线") + std::wstring(L"  ·  打卡 ") +
                            std::to_wstring(f.days) + L" 天  ·  专注 " + std::to_wstring(f.focusMin) + L" 分钟";
        cv.Text(stat, { r.left + 190.0f, r.top, r.right - 160.0f, r.bottom }, ss, f.online ? pal.jade : pal.ink500);
        // 私聊 / 删除
        cv.FillRoundRect(m_dmBtns[i], 5.0f, WithAlpha(pal.seal, 0.14f));
        cv.StrokeRoundRect(m_dmBtns[i], 5.0f, WithAlpha(pal.seal, 0.6f), shape::kHair);
        TextStyle xs; xs.role = FontRole::Sans; xs.size = 11.0f; xs.hAlign = HAlign::Center; xs.vAlign = VAlign::Middle;
        cv.Text(L"私聊", m_dmBtns[i], xs, pal.seal);
        cv.FillRoundRect(m_delBtns[i], 5.0f, WithAlpha(pal.vermilion, 0.10f));
        cv.StrokeRoundRect(m_delBtns[i], 5.0f, WithAlpha(pal.vermilion, 0.5f), shape::kHair);
        cv.Text(L"删除", m_delBtns[i], xs, pal.vermilion);
    }

    m_backBtn.Paint(cv);

    cv.PopTransform();
    cv.PopClip();

    // 私聊浮层（最上层）
    if (m_dmOpen) {
        cv.FillRect(m_area, D2D1::ColorF(0.06f, 0.05f, 0.04f, 0.72f));
        cv.FillRoundRect(m_dmCard, 10.0f, pal.dark ? D2D1::ColorF(0.10f, 0.09f, 0.08f, 0.98f) : D2D1::ColorF(0.97f, 0.96f, 0.94f, 0.98f));
        cv.StrokeRoundRect(m_dmCard, 10.0f, WithAlpha(pal.seal, 0.7f), 1.0f);

        TextStyle ts; ts.role = FontRole::Serif; ts.size = 15.0f; ts.weight = DWRITE_FONT_WEIGHT_BOLD; ts.vAlign = VAlign::Middle;
        cv.Text(L"与 " + m_dmPeer.username + L" 私聊", { m_dmCard.left + 18.0f, m_dmCard.top + 8.0f, m_dmCard.right - 50.0f, m_dmCard.top + 46.0f }, ts, pal.ink900);
        cv.FillRoundRect(m_dmClose, 5.0f, WithAlpha(pal.vermilion, 0.16f));
        TextStyle cs; cs.role = FontRole::Sans; cs.size = 15.0f; cs.hAlign = HAlign::Center; cs.vAlign = VAlign::Middle;
        cv.Text(L"×", m_dmClose, cs, pal.vermilion);

        // 消息列表（裁剪 + 滚动）
        cv.PushClip(m_dmList);
        float totalH = 0.0f;
        std::vector<float> hh(m_dmMsgs.size(), 0.0f);
        TextStyle mt; mt.role = FontRole::Sans; mt.size = 12.5f; mt.vAlign = VAlign::Top;
        for (size_t i = 0; i < m_dmMsgs.size(); ++i) {
            std::wstring line = (m_dmMsgs[i].mine ? L"我： " : m_dmPeer.username + L"： ") + m_dmMsgs[i].text;
            hh[i] = cv.MeasureHeight(line, mt, m_dmList.right - m_dmList.left - 20.0f) + 10.0f;
            totalH += hh[i];
        }
        float maxScroll = (std::max)(0.0f, totalH - (m_dmList.bottom - m_dmList.top));
        if (m_dmScroll > maxScroll + 1.0f) m_dmScroll = maxScroll;
        float yy = m_dmList.top - m_dmScroll;
        for (size_t i = 0; i < m_dmMsgs.size(); ++i) {
            std::wstring line = (m_dmMsgs[i].mine ? L"我： " : m_dmPeer.username + L"： ") + m_dmMsgs[i].text;
            cv.Text(line, { m_dmList.left + 10.0f, yy, m_dmList.right - 10.0f, yy + hh[i] }, mt,
                    m_dmMsgs[i].mine ? pal.ink900 : pal.ink700);
            yy += hh[i];
        }
        if (m_dmMsgs.empty()) {
            TextStyle es; es.role = FontRole::Sans; es.size = 12.0f; es.hAlign = HAlign::Center; es.vAlign = VAlign::Middle;
            cv.Text(L"还没有私聊记录，说点什么吧。", m_dmList, es, pal.ink300);
        }
        cv.PopClip();

        // 输入 + 发送
        cv.FillRoundRect(m_dmInput, 6.0f, WithAlpha(pal.paperHi, 0.9f));
        cv.StrokeRoundRect(m_dmInput, 6.0f, WithAlpha(m_dmOn ? pal.jade : pal.rule, m_dmOn ? 0.9f : 0.5f), 1.0f);
        TextStyle is; is.role = FontRole::Sans; is.size = 12.5f; is.vAlign = VAlign::Middle;
        if (m_dmOn) {
            // v2 统一输入框：D3D 自绘编辑态（浮层屏幕固定坐标，scrollY=0）
            m_dmEdit.Paint(cv, { m_dmInput.left + 10.0f, m_dmInput.top, m_dmInput.right - 8.0f, m_dmInput.bottom },
                           is, pal.ink900, L"输入消息…", pal.ink300, 0.0f);
        } else {
            lj::PaintFieldEdit(cv, { m_dmInput.left + 10.0f, m_dmInput.top, m_dmInput.right - 8.0f, m_dmInput.bottom },
                               is, m_dmText.empty() ? L"输入消息…" : m_dmText,
                               m_dmText.empty() ? pal.ink300 : pal.ink900, -1, 0.0f, -1, -1);
        }
        cv.FillRoundRect(m_dmSend, 6.0f, pal.seal);
        TextStyle xs; xs.role = FontRole::Sans; xs.size = 12.5f; xs.hAlign = HAlign::Center; xs.vAlign = VAlign::Middle; xs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(L"发送", m_dmSend, xs, pal.paperHi);
    }

    if (MaxScroll() > 1.0f) {
        float trackTop = m_area.top + 8.0f;
        float trackH = (m_area.bottom - m_area.top) - 16.0f;
        float thumbH = (std::max)(40.0f, trackH * ((m_area.bottom - m_area.top) / m_contentHeight));
        float pp = Clamp01(s / MaxScroll());
        float ty = trackTop + (trackH - thumbH) * pp;
        float bx = m_area.right - 6.0f;
        cv.FillRect({ bx, trackTop, bx + 2.0f, trackTop + trackH }, WithAlpha(pal.ink300, 0.16f));
        cv.FillRect({ bx, ty, bx + 2.0f, ty + thumbH }, WithAlpha(pal.seal, 0.55f));
    }
}

} // namespace lj
