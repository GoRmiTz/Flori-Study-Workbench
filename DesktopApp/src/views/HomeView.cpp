#include "views/HomeView.h"
#include "app/Store.h"
#include "app/AccountStore.h"   // 空态引导要区分访客（只读）与新注册账户
#include "ui/Layout.h"          // S2-1：纵向流式布局原语

namespace lj {

void HomeView::RefreshToday()
{
    // 今日速览与打卡页（CheckinView）同源：读用户自定义的 items.json，
    // 按系统日期取当日生效列表，并合并今日实际完成记录。
    auto bundle = CheckinStore::Instance().LoadItems();
    m_todayItems = ItemsForDate(bundle, Today());
    std::wstring key = FormatDate(Today());
    auto dayMap = CheckinStore::Instance().LoadDay(key);
    m_todayDone.assign(m_todayItems.size(), 0);
    for (size_t i = 0; i < m_todayItems.size(); ++i) {
        auto it = dayMap.find(m_todayItems[i].Key());
        if (it != dayMap.end())      m_todayDone[i] = it->second ? 1 : 0;
        else                         m_todayDone[i] = m_todayItems[i].done ? 1 : 0;
    }
    m_totalCount = 0; m_doneCount = 0;
    for (size_t i = 0; i < m_todayItems.size(); ++i) {
        if (m_todayItems[i].tag == L"休息") continue;   // 休息不计入统计
        ++m_totalCount;
        if (m_todayDone[i]) ++m_doneCount;
    }
    m_todayRatio = m_totalCount ? (float)m_doneCount / (float)m_totalCount : 0.0f;
}

void HomeView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;

    RefreshToday();

    m_ring.Start(0.0f, m_todayRatio, 1.0f, ease::OutQuart, 0.35f);

    for (size_t i = 0; i < m_countdowns.size(); ++i)
        m_countdowns[i].Start(StaggerDelay((int)i, 0.09f) + 0.18f);
    for (size_t i = 0; i < m_cards.size(); ++i)
        m_cards[i].StartEnter(StaggerDelay((int)i, 0.08f) + 0.52f);
}

void HomeView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    const auto& C = Content::Get();

    // 首次布局时确保当日打卡项已装载（与打卡页同源），供面板高度计算
    if (m_todayItems.empty()) RefreshToday();

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;
    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);

    // ---- 标题区 ----
    m_contentTop = flow.cursorY;
    flow.block(96.0f);

    // ---- 倒计时四卡 ----
    if (m_countdowns.size() != C.milestones.size()) {
        m_countdowns.clear();
        for (const auto& ms : C.milestones) {
            CountdownCard c;
            c.label = ms.label;
            c.dateText = FormatDate(ms.date);
            c.days = (std::max)(0, DaysUntil(ms.date));
            c.urgent = ms.urgent;
            m_countdowns.push_back(c);
        }
        m_built = false;
    }

    const int cdCols = (contentW < 720.0f) ? 2 : 4;
    const float gap = 14.0f;
    float cdW = (contentW - gap * (cdCols - 1)) / cdCols;
    const float cdH = 148.0f;
    int cdRows = ((int)m_countdowns.size() + cdCols - 1) / cdCols;
    {
        D2D1_RECT_F cdArea = flow.block(cdRows * (cdH + gap) + 12.0f);
        for (size_t i = 0; i < m_countdowns.size(); ++i) {
            int r = (int)i / cdCols, c = (int)i % cdCols;
            float cx = x0 + c * (cdW + gap);
            float cy = cdArea.top + r * (cdH + gap);
            m_countdowns[i].bounds = { cx, cy, cx + cdW, cy + cdH };
        }
    }

    // ---- 管理倒计时入口 ----
    {
        D2D1_RECT_F btnArea = flow.block(40.0f + 18.0f);
        m_btnCountdown = { x0, btnArea.top, x0 + 220.0f, btnArea.top + 40.0f };
    }

    // ---- 今日速览面板（按当日打卡项数自适应高度，全部列出以便「何时做什么」一目了然）----
    m_panelY = flow.cursorY;
    m_panelH = (std::max)(168.0f, 56.0f + (float)m_todayItems.size() * 26.0f + 12.0f);

    // 空态：面板里换成引导文案 + 两个入口，按钮坐标随面板一起算好。
    // 访客是只读身份，给它「去配置」的按钮点了也白点，所以只留一个「登录/注册」。
    if (IsTodayEmpty()) {
        float bx = x0 + 186.0f;
        float by = m_panelY + 104.0f;
        if (AccountStore::Instance().IsGuest()) {
            m_btnEmptyPlan  = { bx, by, bx + 168.0f, by + 34.0f };   // → login
            m_btnEmptyItems = {};
        } else {
            m_btnEmptyPlan  = { bx, by, bx + 168.0f, by + 34.0f };
            m_btnEmptyItems = { bx + 180.0f, by, bx + 180.0f + 148.0f, by + 34.0f };
        }
    } else {
        m_btnEmptyPlan = m_btnEmptyItems = {};
    }
    flow.block(m_panelH + 36.0f);

    // ---- 模块入口卡片 ----
    if (m_cards.size() != C.modules.size()) {
        m_cards.clear();
        for (const auto& m : C.modules) {
            ModuleCard card;
            card.index = m.index;
            card.title = m.title;
            card.subtitle = m.subtitle;
            card.meta = m.meta;
            card.accentSet = true;
            m_cards.push_back(card);
        }
        m_built = false;
    }
    // 主题色需要 Canvas 的调色板，布局时补上
    for (size_t i = 0; i < m_cards.size(); ++i) {
        const auto& pal = cv.Pal();
        int a = C.modules[i].accent;
        m_cards[i].accent = (a == 1) ? pal.brass : (a == 2) ? pal.jade : pal.seal;
        std::wstring target = C.modules[i].id;
        m_cards[i].onClick = [this, target] { Go(target); };
    }

    const int mCols = (contentW < 760.0f) ? 1 : (contentW < 1000.0f ? 2 : 3);
    float mW = (contentW - gap * (mCols - 1)) / mCols;
    const float mH = 156.0f;
    int mRows = ((int)m_cards.size() + mCols - 1) / mCols;
    {
        D2D1_RECT_F modArea = flow.block(mRows * (mH + gap) + 20.0f);
        for (size_t i = 0; i < m_cards.size(); ++i) {
            int r = (int)i / mCols, c = (int)i % mCols;
            float cx = x0 + c * (mW + gap);
            float cy = modArea.top + r * (mH + gap);
            m_cards[i].bounds = { cx, cy, cx + mW, cy + mH };
        }
    }

    // ---- 纪律条 ----
    m_disciplineY = flow.cursorY;
    flow.block((float)C.disciplines.size() * 40.0f + 46.0f);

    SetContentHeight(flow.cursorY - area.top);

    m_widgets.clear();
    for (auto& c : m_countdowns) m_widgets.push_back(&c);
    for (auto& c : m_cards) m_widgets.push_back(&c);

    if (!m_built) {
        m_built = true;
        OnEnter();
    }
}

void HomeView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    m_ring.Update(dt);

    // 控件坐标处于文档空间，命中测试需要补上滚动量
    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);

    // 「管理倒计时」入口（其他账户可自定义首页倒计时）
    float mx = in.mouseX, my = in.mouseY + ScrollY();
    m_countdownHot = (mx >= m_btnCountdown.left && mx <= m_btnCountdown.right &&
                      my >= m_btnCountdown.top && my <= m_btnCountdown.bottom);
    if (in.clicked && m_countdownHot) { Go(L"manage"); return; }

    // 空态引导两个入口（仅当今日无打卡项时存在）
    auto hit = [&](const D2D1_RECT_F& b) {
        return b.right > b.left && mx >= b.left && mx <= b.right && my >= b.top && my <= b.bottom;
    };
    m_emptyPlanHot  = hit(m_btnEmptyPlan);
    m_emptyItemsHot = hit(m_btnEmptyItems);
    const bool guest = AccountStore::Instance().IsGuest();
    if (in.clicked && m_emptyPlanHot)  { Go(guest ? L"login" : L"checkin"); return; }
    if (in.clicked && m_emptyItemsHot) { Go(L"manage"); return; }
}

void HomeView::Paint(Canvas& cv)
{
    const auto& C = Content::Get();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    PaintHeader(cv, x0, m_contentTop, contentW);

    for (auto& c : m_countdowns) c.Paint(cv);

    // 管理倒计时入口（其他账户可在此自定义首页倒计时）
    {
        const auto& pal = cv.Pal();
        cv.FillRoundRect(m_btnCountdown, shape::kEdgeSoft,
                         m_countdownHot ? pal.seal : pal.paperLo);
        cv.StrokeRoundRect(m_btnCountdown, shape::kEdgeSoft,
                           m_countdownHot ? pal.seal : pal.rule, shape::kHair);
        TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.0f; bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle; bs.letterSpacing = 1.0f;
        cv.Text(L"＋ 管理倒计时", m_btnCountdown, bs, m_countdownHot ? pal.paperHi : pal.ink700);
    }

    PaintTodayPanel(cv, { x0, m_panelY, x0 + contentW, m_panelY + m_panelH });

    // 分节标题
    {
        const auto& pal = cv.Pal();
        float ty = m_panelY + m_panelH + 6.0f;
        TextStyle st;
        st.role = FontRole::Mono; st.size = 10.5f; st.letterSpacing = 2.4f;
        st.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"SECTION II · 功能模块", { x0, ty, x0 + 400.0f, ty + 16.0f }, st, pal.ink300);
        cv.PerforationH(x0 + 190.0f, x0 + contentW, ty + 8.0f, WithAlpha(pal.ruleStrong, 0.55f));
    }

    for (auto& c : m_cards) c.Paint(cv);

    PaintDiscipline(cv, x0, m_disciplineY, contentW);

    cv.PopTransform();
    cv.PopClip();

    // 右侧滚动指示条（不随内容移动）
    if (MaxScroll() > 1.0f) {
        const auto& pal = cv.Pal();
        float trackTop = m_area.top + 8.0f;
        float trackH = (m_area.bottom - m_area.top) - 16.0f;
        float vh = m_area.bottom - m_area.top;
        float thumbH = (std::max)(40.0f, trackH * (vh / m_contentHeight));
        float p = Clamp01(s / MaxScroll());
        float ty = trackTop + (trackH - thumbH) * p;
        float bx = m_area.right - 6.0f;
        cv.FillRect({ bx, trackTop, bx + 2.0f, trackTop + trackH }, WithAlpha(pal.ink300, 0.16f));
        cv.FillRect({ bx, ty, bx + 2.0f, ty + thumbH }, WithAlpha(pal.seal, 0.55f));
    }
}

void HomeView::PaintHeader(Canvas& cv, float x, float y, float w)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();

    // 分节编号
    TextStyle sec;
    sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    float ha = Clamp01(m_t / 0.5f);
    cv.PushOpacity(ha);
    cv.Text(L"SECTION I · 总览", { x, y, x + 300.0f, y + 16.0f }, sec, pal.ink300);
    cv.PopOpacity();

    // 主标题逐字揭示
    TextStyle h1;
    h1.role = FontRole::Serif; h1.size = 38.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(Title(), x, y + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);

    // 右上：今日日期 + 星期
    float da = Clamp01((m_t - 0.25f) / 0.6f);
    if (da > 0.0f) {
        Date td = Today();
        TextStyle ds;
        ds.role = FontRole::Mono; ds.size = 11.0f;
        ds.letterSpacing = 1.6f; ds.hAlign = HAlign::Right;
        cv.PushOpacity(ease::OutCubic(da));
        cv.Text(FormatDate(td) + L" · " + WeekdayCN(td),
                { x + w - 320.0f, y + 2.0f, x + w, y + 20.0f }, ds, pal.ink500);
        ds.size = 10.0f;
        cv.Text(C.dossierNo, { x + w - 320.0f, y + 22.0f, x + w, y + 38.0f }, ds, pal.ink300);
        cv.PopOpacity();
    }

    // 骑缝分隔
    cv.PerforationH(x, x + w, y + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));
}

void HomeView::PaintTodayPanel(Canvas& cv, const D2D1_RECT_F& r)
{
    const auto& pal = cv.Pal();

    float appear = Clamp01((m_t - 0.30f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 16.0f));
    cv.PushOpacity(e);

    cv.FillRoundRect(r, shape::kEdge, MixColor(pal.paperHi, pal.paperLo, 0.25f));
    cv.StrokeRoundRect(r, shape::kEdge, pal.rule, shape::kHair);
    cv.CornerTicks({ r.left + 7.0f, r.top + 7.0f, r.right - 7.0f, r.bottom - 7.0f },
                   WithAlpha(pal.ink300, 0.5f), 9.0f, 1.0f);

    // ---- 左：进度环 ----
    float ringCx = r.left + 96.0f;
    float ringCy = (r.top + r.bottom) * 0.5f;
    float ringR = 52.0f;
    float pv = m_ring.Value();
    cv.ProgressRing(ringCx, ringCy, ringR, 9.0f, pv,
                    WithAlpha(pal.ink300, 0.22f), pal.seal);

    wchar_t pct[16];
    swprintf_s(pct, L"%d%%", (int)(pv * 100.0f + 0.5f));
    TextStyle ps;
    ps.role = FontRole::Mono; ps.size = 25.0f;
    ps.weight = DWRITE_FONT_WEIGHT_BOLD;
    ps.hAlign = HAlign::Center; ps.vAlign = VAlign::Middle;
    ps.tabularNums = true;
    cv.Text(pct, { ringCx - ringR, ringCy - 20.0f, ringCx + ringR, ringCy + 8.0f }, ps, pal.ink900);

    TextStyle ls;
    ls.role = FontRole::Sans; ls.size = 10.5f;
    ls.hAlign = HAlign::Center; ls.letterSpacing = 1.4f;
    cv.Text(L"今日完成", { ringCx - ringR, ringCy + 10.0f, ringCx + ringR, ringCy + 26.0f },
            ls, pal.ink500);

    // ---- 右：今日打卡摘要 ----
    float tx = r.left + 186.0f;
    float ty = r.top + 22.0f;

    TextStyle title;
    title.role = FontRole::Serif; title.size = 17.0f;
    title.weight = DWRITE_FONT_WEIGHT_BOLD; title.letterSpacing = 1.6f;
    cv.Text(L"今日速览", { tx, ty, tx + 260.0f, ty + 24.0f }, title, pal.ink900);

    // ---- 空态：新账户还没有任何打卡项，别让它显示成「0 / 0 项 + PENDING」----
    // 那看着像出了 bug，实际只是还没配。这里直接告诉用户下一步点哪。
    if (IsTodayEmpty()) {
        const bool guest = AccountStore::Instance().IsGuest();

        TextStyle es;
        es.role = FontRole::Sans; es.size = 12.5f;
        cv.Text(guest ? L"访客模式 · 只读浏览" : L"还没有设定每日打卡项",
                { tx + 92.0f, ty + 5.0f, tx + 300.0f, ty + 23.0f }, es, pal.ink500);

        TextStyle hs;
        hs.role = FontRole::Sans; hs.size = 12.0f;
        if (guest) {
            cv.Text(L"访客可以随便逛，但打卡记录不会保存，也无法设定每日任务；",
                    { tx, ty + 34.0f, r.right - 32.0f, ty + 54.0f }, hs, pal.ink700);
            cv.Text(L"注册一个账号（本地即可，不必联网），进度才会一天天攒下来。",
                    { tx, ty + 54.0f, r.right - 32.0f, ty + 74.0f }, hs, pal.ink500);
        } else {
            cv.Text(L"先从每天要做的事列起，芙洛理会按时段帮你排好今天的安排；",
                    { tx, ty + 34.0f, r.right - 32.0f, ty + 54.0f }, hs, pal.ink700);
            cv.Text(L"设定之后，这里会按时段列出今天的安排，左边圆环显示完成度。",
                    { tx, ty + 54.0f, r.right - 32.0f, ty + 74.0f }, hs, pal.ink500);
        }

        auto btn = [&](const D2D1_RECT_F& b, const wchar_t* label, bool hot, bool primary) {
            if (b.right <= b.left) return;
            cv.FillRoundRect(b, shape::kEdgeSoft,
                             hot ? pal.seal : (primary ? pal.paperLo : pal.paperHi));
            cv.StrokeRoundRect(b, shape::kEdgeSoft,
                               hot ? pal.seal : (primary ? pal.ruleStrong : pal.rule), shape::kHair);
            TextStyle bs;
            bs.role = FontRole::Sans; bs.size = 12.5f; bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle; bs.letterSpacing = 0.8f;
            cv.Text(label, b, bs, hot ? pal.paperHi : pal.ink700);
        };
        btn(m_btnEmptyPlan,  guest ? L"登录 / 注册" : L"开始每日打卡", m_emptyPlanHot, true);
        btn(m_btnEmptyItems, L"自定义打卡项", m_emptyItemsHot, false);

        cv.PopOpacity();
        cv.PopTransform();
        return;
    }

    wchar_t cnt[64];
    swprintf_s(cnt, L"%d / %d 项", m_doneCount, m_totalCount);
    TextStyle cs;
    cs.role = FontRole::Mono; cs.size = 11.5f; cs.letterSpacing = 1.0f;
    cv.Text(cnt, { tx + 92.0f, ty + 6.0f, tx + 240.0f, ty + 22.0f }, cs, pal.ink500);

    // 打卡项条目（全部列出：时段 + 名称 + 完成标准）
    float iy = ty + 32.0f;
    int shown = 0;
    for (size_t i = 0; i < m_todayItems.size(); ++i) {
        const auto& item = m_todayItems[i];
        bool done = (m_todayDone[i] != 0);
        float rowA = Clamp01((m_t - 0.5f - shown * 0.07f) / 0.4f);
        if (rowA <= 0.004f) { ++shown; iy += 26.0f; continue; }
        float re = ease::OutCubic(rowA);

        cv.PushOpacity(re);
        cv.PushTransform(D2D1::Matrix3x2F::Translation((1.0f - re) * 10.0f, 0.0f));

        // 勾选框：完成则朱砂实心 + 对勾
        D2D1_RECT_F box{ tx, iy + 3.0f, tx + 14.0f, iy + 17.0f };
        if (done) {
            cv.FillRect(box, pal.seal);
            cv.Line(box.left + 3.0f, box.top + 7.0f, box.left + 6.0f, box.top + 10.5f, pal.paperHi, 1.6f);
            cv.Line(box.left + 6.0f, box.top + 10.5f, box.left + 11.0f, box.top + 4.0f, pal.paperHi, 1.6f);
        } else {
            cv.StrokeRect(box, pal.ruleStrong, 1.2f);
        }

        TextStyle slot;
        slot.role = FontRole::Mono; slot.size = 10.5f; slot.tabularNums = true;
        cv.Text(item.slot, { tx + 22.0f, iy + 3.0f, tx + 66.0f, iy + 19.0f }, slot, pal.ink300);

        TextStyle ns;
        ns.role = FontRole::Sans; ns.size = 12.5f;
        cv.Text(item.title, { tx + 70.0f, iy + 1.0f, tx + 230.0f, iy + 19.0f },
                ns, done ? pal.ink300 : pal.ink700);

        TextStyle sd;
        sd.role = FontRole::Sans; sd.size = 11.0f;
        cv.Text(item.standard, { tx + 232.0f, iy + 2.0f, r.right - 108.0f, iy + 19.0f },
                sd, WithAlpha(pal.ink300, 0.95f));

        cv.PopTransform();
        cv.PopOpacity();
        ++shown;
        iy += 26.0f;
    }

    // 右下：完成印章
    if (m_todayRatio >= 0.999f) {
        cv.SealStamp(r.right - 70.0f, r.bottom - 54.0f, 58.0f, L"已清空", -9.0f, 0.9f);
    } else {
        TextStyle hint;
        hint.role = FontRole::Mono; hint.size = 10.0f;
        hint.letterSpacing = 1.4f; hint.hAlign = HAlign::Right;
        cv.Text(L"PENDING", { r.right - 150.0f, r.bottom - 30.0f, r.right - 20.0f, r.bottom - 14.0f },
                hint, WithAlpha(pal.brass, 0.9f));
    }

    cv.PopOpacity();
    cv.PopTransform();
}

void HomeView::PaintDiscipline(Canvas& cv, float x, float y, float w)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();

    TextStyle sec;
    sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION III · 执行纪律", { x, y, x + 400.0f, y + 16.0f }, sec, pal.ink300);
    cv.PerforationH(x + 200.0f, x + w, y + 8.0f, WithAlpha(pal.ruleStrong, 0.55f));

    float iy = y + 26.0f;
    for (size_t i = 0; i < C.disciplines.size(); ++i) {
        float a = Clamp01((m_t - 0.8f - (float)i * 0.08f) / 0.5f);
        if (a <= 0.004f) { iy += 40.0f; continue; }
        float e = ease::OutCubic(a);
        cv.PushOpacity(e);
        cv.PushTransform(D2D1::Matrix3x2F::Translation((1.0f - e) * 12.0f, 0.0f));

        D2D1_RECT_F row{ x, iy, x + w, iy + 32.0f };
        cv.FillRoundRect(row, shape::kEdgeSoft, WithAlpha(pal.vermWash, pal.dark ? 0.55f : 0.75f));
        cv.FillRect({ row.left, row.top, row.left + 2.5f, row.bottom }, WithAlpha(pal.vermilion, 0.8f));

        wchar_t num[8];
        swprintf_s(num, L"%02d", (int)i + 1);
        TextStyle ns;
        ns.role = FontRole::Mono; ns.size = 10.5f;
        ns.weight = DWRITE_FONT_WEIGHT_BOLD; ns.vAlign = VAlign::Middle;
        cv.Text(num, { row.left + 14.0f, row.top, row.left + 40.0f, row.bottom }, ns,
                WithAlpha(pal.vermilion, 0.95f));

        TextStyle ts;
        ts.role = FontRole::Sans; ts.size = 12.5f; ts.vAlign = VAlign::Middle;
        cv.Text(C.disciplines[i], { row.left + 42.0f, row.top, row.right - 14.0f, row.bottom },
                ts, pal.ink700);

        cv.PopTransform();
        cv.PopOpacity();
        iy += 40.0f;
    }
}

} // namespace lj
