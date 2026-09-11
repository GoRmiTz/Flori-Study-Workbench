#include "views/CheckinView.h"
#include "ui/Layout.h"
#include "app/Store.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <windows.h>
#include <shellapi.h>

namespace lj {

// 标签配色（与网页 tagSpan 一致），自由函数以便 CheckRow 也能调用
namespace {
D2D1_COLOR_F TagColor(const Palette& pal, const std::wstring& t)
{
    if (t == L"主线") return pal.seal;
    if (t == L"英语" || t == L"手绘") return pal.jade;
    if (t == L"申论" || t == L"常识") return pal.brass;
    if (t == L"作息") return pal.ink500;
    return pal.ink500;
}
bool Hit(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
void OpenUrl(const std::wstring& url)
{
    if (url.empty()) return;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
} // namespace

// ============================================================
//  CheckRow — 单行打卡项（可勾选 + 详情入口）
// ============================================================
void CheckRow::Paint(Canvas& cv)
{
    if (!visible) return;
    const auto& pal = cv.Pal();
    float ev = enter.running ? enter.Value() : 1.0f;
    if (ev <= 0.001f) return;
    float h = m_hover.value;
    float p = m_press.value;

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - ev) * 12.0f));
    cv.PushOpacity(ev);

    D2D1_RECT_F r = bounds;
    r.top += p * 1.0f; r.bottom += p * 1.0f;

    // ---- 休息行 ----
    if (rest) {
        cv.FillRoundRect(r, shape::kEdgeSoft, WithAlpha(pal.brassWash, pal.dark ? 0.5f : 0.7f));
        cv.StrokeRoundRect(r, shape::kEdgeSoft, WithAlpha(pal.rule, 0.7f), shape::kHair);
        cv.PerforationH(r.left + 16.0f, r.right - 16.0f, r.top + 14.0f,
                        WithAlpha(pal.rule, 0.5f));
        TextStyle cs; cs.role = FontRole::Sans; cs.size = 13.0f; cs.vAlign = VAlign::Middle;
        cs.letterSpacing = 1.0f;
        cv.Text(L"☕ 休 息", { r.left + 18.0f, r.top, r.right - 120.0f, r.bottom }, cs, pal.brass);
        TextStyle ds; ds.role = FontRole::Mono; ds.size = 11.0f; ds.hAlign = HAlign::Right;
        ds.vAlign = VAlign::Middle;
        cv.Text(dur, { r.right - 120.0f, r.top, r.right - 18.0f, r.bottom }, ds, pal.ink500);
        cv.PopOpacity(); cv.PopTransform();
        return;
    }

    bool d = pDone && *pDone != 0;

    // 背景：常态给一层浅底（不再是透明，避免费眼），悬停加深
    D2D1_COLOR_F base = d ? WithAlpha(pal.rule, pal.dark ? 0.16f : 0.30f)
                          : WithAlpha(pal.rule, pal.dark ? 0.10f : 0.22f);
    D2D1_COLOR_F bg = MixColor(base, WithAlpha(pal.seal, 0.18f), h * 0.8f);
    cv.FillRoundRect(r, shape::kEdgeSoft, bg);
    cv.StrokeRoundRect(r, shape::kEdgeSoft,
                       MixColor(pal.rule, pal.seal, h * 0.5f), h > 0.02f ? shape::kStroke : shape::kHair);
    if (h > 0.01f)
        cv.CornerTicks({ r.left - 3.0f, r.top - 3.0f, r.right + 3.0f, r.bottom + 3.0f },
                       WithAlpha(pal.seal, h * 0.45f), 8.0f, 1.0f);

    // 左侧完成压条
    cv.FillRect({ r.left, r.top, r.left + 2.5f, r.bottom }, WithAlpha(d ? pal.jade : pal.seal, 0.85f));

    // 勾选框
    float bs = 24.0f;
    float boxCy = (r.top + r.bottom) * 0.5f;
    D2D1_RECT_F box{ r.left + 16.0f, boxCy - bs / 2.0f, r.left + 16.0f + bs, boxCy + bs / 2.0f };
    if (d) {
        cv.FillRect(box, pal.seal);
        cv.Line(box.left + 5.0f, box.top + 12.0f, box.left + 10.0f, box.top + 17.0f, pal.paperHi, 2.2f);
        cv.Line(box.left + 10.0f, box.top + 17.0f, box.left + 19.0f, box.top + 6.0f, pal.paperHi, 2.2f);
    } else {
        cv.StrokeRoundRect(box, 2.0f, MixColor(pal.ruleStrong, pal.seal, h), 1.4f);
    }

    // 时段
    float tx = box.right + 14.0f;
    TextStyle ts; ts.role = FontRole::Mono; ts.size = 12.0f; ts.tabularNums = true;
    ts.vAlign = VAlign::Middle;
    cv.Text(time, { tx, r.top, tx + 58.0f, r.bottom }, ts, d ? pal.ink300 : pal.ink500);

    // 标题（第一行）+ 标准（第二行）
    float titleX = tx + 64.0f;
    TextStyle ns; ns.role = FontRole::Sans; ns.size = 14.5f;
    ns.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ns.vAlign = VAlign::Top;
    cv.Text(title, { titleX, r.top + 9.0f, r.right - 90.0f, r.top + 32.0f }, ns,
            d ? pal.ink300 : pal.ink900);

    TextStyle ss; ss.role = FontRole::Sans; ss.size = 11.5f; ss.vAlign = VAlign::Top;
    cv.Text(standard, { titleX, r.top + 34.0f, r.right - 90.0f, r.bottom - 6.0f }, ss,
            WithAlpha(pal.ink500, 0.95f));

    // 分类标签（标题右侧，空间足够才画）
    if (!tag.empty()) {
        TextStyle tgs; tgs.role = FontRole::Mono; tgs.size = 10.0f; tgs.letterSpacing = 1.0f;
        tgs.vAlign = VAlign::Top;
        float tw = cv.MeasureWidth(tag, tgs);
        float tagX = titleX + cv.MeasureWidth(title, ns) + 10.0f;
        float tagRight = tagX + tw + 16.0f;
        if (tagRight < r.right - 150.0f) {    // 给「详情」按钮让位
            D2D1_RECT_F tr{ tagX, r.top + 11.0f, tagRight, r.top + 28.0f };
            cv.FillRoundRect(tr, 3.0f, WithAlpha(TagColor(pal, tag), pal.dark ? 0.28f : 0.16f));
            cv.StrokeRoundRect(tr, 3.0f, WithAlpha(TagColor(pal, tag), 0.7f), 1.0f);
            cv.Text(tag, { tagX + 8.0f, r.top + 11.0f, tagRight - 4.0f, r.top + 28.0f }, tgs,
                    TagColor(pal, tag));
        }
    }

    // 时长（右侧竖排居中）
    TextStyle ds; ds.role = FontRole::Mono; ds.size = 11.5f; ds.hAlign = HAlign::Right;
    ds.vAlign = VAlign::Middle; ds.tabularNums = true;
    cv.Text(dur, { r.right - 84.0f, r.top, r.right - 18.0f, r.bottom }, ds, pal.ink300);

    // 「详情」按钮（命中区由 Layout 写入 detailRect）
    {
        D2D1_RECT_F dr = detailRect;
        cv.FillRoundRect(dr, 4.0f, WithAlpha(pal.seal, pal.dark ? 0.20f : 0.12f));
        cv.StrokeRoundRect(dr, 4.0f, WithAlpha(pal.seal, 0.7f), 1.0f);
        TextStyle dts; dts.role = FontRole::Sans; dts.size = 11.5f;
        dts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; dts.hAlign = HAlign::Center;
        dts.vAlign = VAlign::Middle; dts.letterSpacing = 1.0f;
        cv.Text(L"详情", dr, dts, pal.seal);
    }

    cv.PopOpacity(); cv.PopTransform();
}

// ============================================================
//  CheckinView
// ============================================================
void CheckinView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_alert.active = false;
    m_card.active = false;

    const auto& C = Content::Get();
    auto bundle = CheckinStore::Instance().LoadItems();
    m_items = ItemsForDate(bundle, Today());

    std::wstring todayKey = FormatDate(Today());
    auto dayMap = CheckinStore::Instance().LoadDay(todayKey);
    m_done.assign(m_items.size(), 0);
    for (size_t i = 0; i < m_items.size(); ++i) {
        auto it = dayMap.find(m_items[i].Key());
        if (it != dayMap.end())
            m_done[i] = it->second ? 1 : 0;
        else
            m_done[i] = m_items[i].done ? 1 : 0;
    }

    Recompute();

    // 近 7 天出勤率：真实读取本地档案（今日取实时进度）
    auto days = CheckinStore::Instance().LastNDays(7);
    for (int i = 0; i < 7; ++i) {
        if (i == 6) { m_hist[6] = m_pct; continue; }
        auto hm = CheckinStore::Instance().LoadDay(days[i]);
        int tot = 0, dn = 0;
        for (size_t k = 0; k < m_items.size(); ++k) {
            if (m_items[k].tag == L"休息") continue;
            ++tot;
            auto f = hm.find(m_items[k].Key());
            if (f != hm.end() && f->second) ++dn;
        }
        m_hist[i] = tot ? (float)dn / (float)tot : 0.0f;
    }

    m_needRebuild = true;

    // ---- 截图/联调：环境变量强制弹层（生产无影响）----
    wchar_t env[8];
    if (GetEnvironmentVariableW(L"FLORI_TEST_FOCUS_ALERT", env, 8)) {
        CheckItem demo; demo.title = L"行测 · 言语"; demo.slot = L"09:00"; demo.tag = L"主线";
        ShowFocusAlert(demo);
    }
    if (GetEnvironmentVariableW(L"FLORI_TEST_TASK_CARD", env, 8)) {
        CheckItem demo; demo.title = L"英语单词"; demo.slot = L"07:10"; demo.minutes = 25;
        demo.tag = L"英语"; demo.standard = L"20-30 分钟，不断线维护";
        demo.link = L"https://www.xuexi.cn";
        openTaskCard(demo);
    }
}

void CheckinView::SaveToday()
{
    std::wstring key = FormatDate(Today());
    std::map<std::wstring, bool> dm;
    for (size_t i = 0; i < m_items.size(); ++i)
        dm[m_items[i].Key()] = m_done[i] != 0;
    CheckinStore::Instance().SaveDay(key, dm);
}

void CheckinView::Recompute()
{
    m_total = 0; m_doneCount = 0;
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].tag == L"休息") continue;   // 休息不计入统计
        ++m_total;
        if (m_done[i]) ++m_doneCount;
    }
    m_pct = m_total ? (float)m_doneCount / (float)m_total : 0.0f;
    m_prog.target = m_pct;
}

void CheckinView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);

    // 首次进入（导航前未触发 OnEnter 时的兜底）：先装载当日项
    if (!m_built) OnEnter();

    const auto& C = Content::Get();

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;

    // 纵向流式布局：区块只声明自身高度，位置由 flow 推进
    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);

    // ---- 标题区 ----
    m_contentTop = flow.cursorY;
    flow.block(96.0f);

    // ---- 进度区 ----
    m_progY = flow.block(70.0f + 20.0f).top;

    // ---- 任务列表 ----
    if (m_needRebuild || m_rows.size() != m_items.size()) {
        m_rows.clear();
        const float rowH = 62.0f, gap = 12.0f;
        for (size_t i = 0; i < m_items.size(); ++i) {
            CheckRow row;
            const auto& it = m_items[i];
            row.time = it.slot;
            row.title = it.title;
            row.standard = it.standard;
            row.tag = it.tag;
            row.dur = it.minutes > 0 ? std::to_wstring(it.minutes) + L" 分钟" : L"随时";
            row.rest = (it.tag == L"休息");
            row.pDone = &m_done[i];
            row.detailRect = { x0 + contentW - 150.0f, 0.0f, x0 + contentW - 92.0f, 0.0f };
            row.StartEnter(StaggerDelay((int)i, 0.05f) + 0.30f);
            m_rows.push_back(std::move(row));
        }
        m_needRebuild = false;
        m_built = true;
    }
    // 进度条文字右侧的「记专注」+「重置今日」
    m_focusBtn.label = L"＋ 记专注";
    m_focusBtn.tag = L"◷";
    m_focusBtn.fontSize = 12.0f;
    m_focusBtn.bounds = { x0 + contentW - 290.0f, m_progY + 18.0f,
                          x0 + contentW - 162.0f, m_progY + 52.0f };
    if (!m_focusBtnShown) { m_focusBtn.StartEnter(0.42f); m_focusBtnShown = true; }
    m_focusBtn.onClick = [this] { LogFocusNow(); ShowSuccess(L"25 分钟 · 自习室 · 已写入专注档案"); };

    m_resetBtn.label = L"重 置 今 日";
    m_resetBtn.tag = L"↺";
    m_resetBtn.fontSize = 12.0f;
    m_resetBtn.bounds = { x0 + contentW - 140.0f, m_progY + 18.0f,
                          x0 + contentW - 12.0f, m_progY + 52.0f };
    if (!m_resetBtnShown) { m_resetBtn.StartEnter(0.42f); m_resetBtnShown = true; }
    m_resetBtn.onClick = [this] {
        m_done.assign(m_done.size(), 0);
        Recompute();
        SaveToday();
    };

    m_listY = flow.cursorY;
    const float rowH = 58.0f, gap = 10.0f;
    float rowStart = m_listY + 30.0f;   // 让出「今日任务」小标题
    for (size_t i = 0; i < m_rows.size(); ++i) {
        float ry = rowStart + (float)i * (rowH + gap);
        m_rows[i].bounds = { x0, ry, x0 + contentW, ry + rowH };
        m_rows[i].detailRect = { x0 + contentW - 150.0f, ry + (rowH - 26.0f) / 2.0f,
                                 x0 + contentW - 92.0f, ry + (rowH - 26.0f) / 2.0f + 26.0f };
    }
    flow.block(30.0f + (float)m_rows.size() * (rowH + gap) + 16.0f);

    // ---- 近 7 天出勤率 ----
    m_histY = flow.block(34.0f + 78.0f + 18.0f).top;

    // ---- 三条执行纪律 ----
    m_discY = flow.block(26.0f + (float)C.disciplines.size() * 40.0f + 30.0f).top;

    // ---- 自定义打卡项入口 ----
    m_manageBtn.label = L"自定义打卡项 →";
    m_manageBtn.tag = L"✎";
    m_manageBtn.fontSize = 13.0f;
    {
        float top = flow.block(46.0f + 18.0f).top;
        m_manageBtn.bounds = { x0, top, x0 + 200.0f, top + 46.0f };
    }
    if (!m_manageBtnShown) { m_manageBtn.StartEnter(0.5f); m_manageBtnShown = true; }
    m_manageBtn.onClick = [this] { Go(L"manage"); };

    // ---- 返回首页 ----
    m_backBtn.label = L"返 回 首 页";
    m_backBtn.tag = L"00";
    m_backBtn.fontSize = 13.0f;
    {
        float top = flow.block(46.0f + 30.0f).top;
        m_backBtn.bounds = { x0, top, x0 + 150.0f, top + 46.0f };
    }
    if (!m_backBtnShown) { m_backBtn.StartEnter(0.5f); m_backBtnShown = true; }
    m_backBtn.onClick = [this] { Go(L"home"); };

    SetContentHeight(flow.cursorY - area.top);

    m_widgets.clear();
    m_widgets.push_back(&m_resetBtn);
    m_widgets.push_back(&m_focusBtn);
    m_widgets.push_back(&m_manageBtn);
    m_widgets.push_back(&m_backBtn);
    for (auto& r : m_rows) m_widgets.push_back(&r);
}

void CheckinView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    m_prog.Update(dt);
    m_overlayA.Update(dt);

    // 弹层激活：阻塞底层控件，仅处理弹层按钮
    if (m_alert.active || m_card.active) {
        Input blocked = in;
        blocked.clicked = false; blocked.released = false; blocked.pressed = false;
        blocked.inWindow = false;
        UpdateWidgets(m_widgets, dt, blocked);

        ComputeOverlayRects();
        if (in.clicked) {
            float mx = in.mouseX, my = in.mouseY;   // 屏幕坐标（与 m_area 同系）
            if (m_alert.active) {
                if (m_alert.mode == 0 && Hit(m_alert.btnLog, mx, my)) {
                    LogFocusNow(); m_alert.active = false;
                } else if (Hit(m_alert.btnOk, mx, my)) {
                    m_alert.active = false;
                }
            } else if (m_card.active) {
                if (!m_card.item.link.empty() && Hit(m_card.btnLink, mx, my))
                    OpenUrl(m_card.item.link);
                else if (!m_card.item.folder.empty() && Hit(m_card.btnFolder, mx, my))
                    OpenUrl(m_card.item.folder);
                else if (Hit(m_card.btnClose, mx, my))
                    m_card.active = false;
                else if (!Hit(m_card.card, mx, my))
                    m_card.active = false;   // 点卡片外关闭
            }
        }
        return;
    }

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);

    if (in.clicked) {
        float mx = in.mouseX;
        float my = in.mouseY + ScrollY();
        for (size_t i = 0; i < m_rows.size(); ++i) {
            auto& r = m_rows[i];
            if (!r.visible) continue;
            if (!r.rest && Hit(r.detailRect, mx, my)) { openTaskCard(m_items[i]); break; }
            if (Hit(r.bounds, mx, my)) { toggleRow((int)i); break; }
        }
    }
}

void CheckinView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    // ---- 标题区 ----
    {
        TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
        sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
        float ha = Clamp01(m_t / 0.5f);
        cv.PushOpacity(ha);
        cv.Text(L"SECTION · 每日执行档案", { x0, m_contentTop, x0 + 320.0f, m_contentTop + 16.0f },
                sec, pal.ink300);
        cv.PopOpacity();

        TextStyle h1; h1.role = FontRole::Serif; h1.size = 38.0f;
        h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
        cv.CharsReveal(L"每日打卡", x0, m_contentTop + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);

        float da = Clamp01((m_t - 0.25f) / 0.6f);
        if (da > 0.0f) {
            Date td = Today();
            TextStyle ds; ds.role = FontRole::Mono; ds.size = 11.0f; ds.letterSpacing = 1.6f;
            ds.hAlign = HAlign::Right;
            cv.PushOpacity(ease::OutCubic(da));
            cv.Text(WeekdayCN(td) + L" · " + FormatDate(td),
                    { x0 + contentW - 320.0f, m_contentTop + 2.0f, x0 + contentW, m_contentTop + 20.0f },
                    ds, pal.ink500);
            cv.PopOpacity();
        }
        cv.PerforationH(x0, x0 + contentW, m_contentTop + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));
    }

    PaintProgress(cv, x0, m_progY, contentW);

    // ---- 今日任务 小标题 ----
    {
        float ty = m_listY;
        TextStyle st; st.role = FontRole::Mono; st.size = 10.5f; st.letterSpacing = 2.4f;
        st.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"SECTION · 今日任务", { x0, ty, x0 + 400.0f, ty + 16.0f }, st, pal.ink300);
        cv.PerforationH(x0 + 150.0f, x0 + contentW, ty + 8.0f, WithAlpha(pal.ruleStrong, 0.5f));
    }
    for (auto& r : m_rows) r.Paint(cv);

    PaintHistory(cv, x0, m_histY, contentW);
    PaintDiscipline(cv, x0, m_discY, contentW);

    m_manageBtn.Paint(cv);
    m_backBtn.Paint(cv);

    cv.PopTransform();
    cv.PopClip();

    // ---- 弹层（屏幕坐标，不随滚动）----
    if (m_alert.active || m_card.active) PaintOverlay(cv);

    // 右侧滚动指示条
    if (MaxScroll() > 1.0f) {
        float trackTop = m_area.top + 8.0f;
        float trackH = (m_area.bottom - m_area.top) - 16.0f;
        float vh = m_area.bottom - m_area.top;
        float thumbH = (std::max)(40.0f, trackH * (vh / m_contentHeight));
        float pp = Clamp01(s / MaxScroll());
        float ty = trackTop + (trackH - thumbH) * pp;
        float bx = m_area.right - 6.0f;
        cv.FillRect({ bx, trackTop, bx + 2.0f, trackTop + trackH }, WithAlpha(pal.ink300, 0.16f));
        cv.FillRect({ bx, ty, bx + 2.0f, ty + thumbH }, WithAlpha(pal.seal, 0.55f));
    }
}

// ---------------- 进度条 + 计数 ----------------
void CheckinView::PaintProgress(Canvas& cv, float x, float y, float w)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.30f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 14.0f));
    cv.PushOpacity(e);

    float barH = 12.0f;
    D2D1_RECT_F track{ x, y, x + w, y + barH };
    cv.FillRoundRect(track, barH / 2.0f, WithAlpha(pal.ink300, 0.22f));
    float fw = (std::max)(0.0f, m_prog.value) * (w - 2.0f);
    if (fw > 1.0f) {
        D2D1_RECT_F fill{ x + 1.0f, y + 1.0f, x + 1.0f + fw, y + barH - 1.0f };
        cv.FillRoundRect(fill, (barH - 2.0f) / 2.0f, m_pct >= 0.999f ? pal.jade : pal.seal);
    }

    wchar_t buf[80];
    swprintf_s(buf, L"%d / %d 已完成（%d%%）%s", m_doneCount, m_total,
               (int)(m_pct * 100.0f + 0.5f), m_pct >= 0.999f ? L" · 今日已清空 ✓" : L"");
    TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.letterSpacing = 0.6f;
    cv.Text(buf, { x, y + barH + 12.0f, x + w - 300.0f, y + barH + 36.0f }, ts,
            m_pct >= 0.999f ? pal.jade : pal.ink700);

    if (m_pct >= 0.999f) {
        float sv = Clamp01((m_t - 0.6f) / 0.5f);
        if (sv > 0.01f)
            cv.SealStamp(x + w - 50.0f, y + 30.0f, 52.0f, L"已清空", -9.0f, 0.9f * ease::OutCubic(sv));
    }

    cv.PopOpacity();
    cv.PopTransform();
}

// ---------------- 近 7 天出勤率 ----------------
void CheckinView::PaintHistory(Canvas& cv, float x, float y, float w)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.45f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 14.0f));
    cv.PushOpacity(e);

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 近 7 天出勤率", { x, y, x + 400.0f, y + 16.0f }, sec, pal.ink300);
    cv.PerforationH(x + 170.0f, x + w, y + 8.0f, WithAlpha(pal.ruleStrong, 0.5f));

    static const wchar_t* kCN[] = { L"日", L"一", L"二", L"三", L"四", L"五", L"六" };
    auto weekChar = [](int serial) {
        int wd = ((serial + 4) % 7 + 7) % 7; return kCN[wd];
    };

    float maxH = 64.0f;
    float base = y + 34.0f + maxH;   // 基线在图表区底部，柱条向上生长不越界
    float colW = w / 7.0f;
    Date td = Today();
    int todaySerial = DaysFromCivil(td.y, td.m, td.d);

    for (int i = 0; i < 7; ++i) {
        float cx = x + colW * (i + 0.5f);
        float bw = (std::min)(colW * 0.5f, 40.0f);
        float bh = (std::max)(3.0f, m_hist[i] * maxH);
        D2D1_RECT_F bar{ cx - bw / 2.0f, base - bh, cx + bw / 2.0f, base };
        cv.FillRoundRect({ cx - bw / 2.0f, base - maxH, cx + bw / 2.0f, base }, bw / 2.0f,
                          WithAlpha(pal.ink300, 0.10f));
        cv.FillRoundRect(bar, bw / 2.0f, i == 6 ? pal.seal : MixColor(pal.seal, pal.brass, 0.4f));

        wchar_t p[8];
        swprintf_s(p, L"%d", (int)(m_hist[i] * 100.0f + 0.5f));
        TextStyle ps; ps.role = FontRole::Mono; ps.size = 10.0f; ps.hAlign = HAlign::Center;
        ps.tabularNums = true;
        cv.Text(p, { cx - bw / 2.0f, base - bh - 16.0f, cx + bw / 2.0f, base - bh - 2.0f },
                ps, pal.ink500);

        const wchar_t* label = (i == 6) ? L"今" : weekChar(todaySerial - (6 - i));
        TextStyle ls; ls.role = FontRole::Sans; ls.size = 11.0f; ls.hAlign = HAlign::Center;
        cv.Text(label, { cx - bw / 2.0f, base + 6.0f, cx + bw / 2.0f, base + 24.0f }, ls,
                i == 6 ? pal.seal : pal.ink500);
    }

    cv.PopOpacity();
    cv.PopTransform();
}

// ---------------- 三条执行纪律 ----------------
void CheckinView::PaintDiscipline(Canvas& cv, float x, float y, float w)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 执行纪律", { x, y, x + 400.0f, y + 16.0f }, sec, pal.ink300);
    cv.PerforationH(x + 150.0f, x + w, y + 8.0f, WithAlpha(pal.ruleStrong, 0.5f));

    float iy = y + 26.0f;
    for (size_t i = 0; i < C.disciplines.size(); ++i) {
        float a = Clamp01((m_t - 0.7f - (float)i * 0.08f) / 0.5f);
        if (a <= 0.004f) { iy += 40.0f; continue; }
        float er = ease::OutCubic(a);
        cv.PushOpacity(er);
        cv.PushTransform(D2D1::Matrix3x2F::Translation((1.0f - er) * 12.0f, 0.0f));

        D2D1_RECT_F row{ x, iy, x + w, iy + 32.0f };
        cv.FillRoundRect(row, shape::kEdgeSoft, WithAlpha(pal.vermWash, pal.dark ? 0.55f : 0.75f));
        cv.FillRect({ row.left, row.top, row.left + 2.5f, row.bottom }, WithAlpha(pal.vermilion, 0.8f));

        wchar_t num[8];
        swprintf_s(num, L"%02d", (int)i + 1);
        TextStyle ns; ns.role = FontRole::Mono; ns.size = 10.5f; ns.weight = DWRITE_FONT_WEIGHT_BOLD;
        ns.vAlign = VAlign::Middle;
        cv.Text(num, { row.left + 14.0f, row.top, row.left + 40.0f, row.bottom }, ns,
                WithAlpha(pal.vermilion, 0.95f));

        TextStyle ts; ts.role = FontRole::Sans; ts.size = 12.5f; ts.vAlign = VAlign::Middle;
        cv.Text(C.disciplines[i], { row.left + 42.0f, row.top, row.right - 14.0f, row.bottom },
                ts, pal.ink700);

        cv.PopTransform();
        cv.PopOpacity();
        iy += 40.0f;
    }
}

// ============================================================
//  弹层
// ============================================================
void CheckinView::ComputeOverlayRects()
{
    const float cx = (m_area.left + m_area.right) * 0.5f;
    const float aw = (std::min)(440.0f, m_area.right - m_area.left - 80.0f);
    const float ax = cx - aw / 2.0f;
    const float ay = m_area.top + 110.0f;
    const float ah = m_alert.mode == 0 ? 196.0f : 150.0f;
    m_alert.card = { ax, ay, ax + aw, ay + ah };

    const float bw = 150.0f, bh = 42.0f;
    if (m_alert.mode == 0) {
        const float gap = 18.0f;
        m_alert.btnLog = { ax + aw / 2.0f - bw - gap / 2.0f, ay + ah - 60.0f,
                           ax + aw / 2.0f - gap / 2.0f, ay + ah - 18.0f };
        m_alert.btnOk  = { ax + aw / 2.0f + gap / 2.0f, ay + ah - 60.0f,
                           ax + aw / 2.0f + gap / 2.0f + bw, ay + ah - 18.0f };
    } else {
        m_alert.btnOk = { cx - bw / 2.0f, ay + ah - 54.0f, cx + bw / 2.0f, ay + ah - 12.0f };
    }

    const bool hasLink   = !m_card.item.link.empty();
    const bool hasFolder = !m_card.item.folder.empty();

    const float tw = (std::min)(500.0f, m_area.right - m_area.left - 80.0f);
    const float tx = cx - tw / 2.0f;
    const float ty = m_area.top + 80.0f;
    const float th = 322.0f + (hasFolder ? 34.0f : 0.0f);
    m_card.card = { tx, ty, tx + tw, ty + th };

    // 底部按钮条：左侧动作按钮（外链 / 文件夹）依次排布，右侧固定「知道了」
    const float by0 = ty + th - 62.0f, by1 = ty + th - 20.0f;
    const float closeW = (hasLink || hasFolder) ? 104.0f : 200.0f;
    m_card.btnClose = { tx + tw - 20.0f - closeW, by0, tx + tw - 20.0f, by1 };

    float bx = tx + 20.0f;
    if (hasLink) {
        const float w = hasFolder ? 148.0f : 200.0f;
        m_card.btnLink = { bx, by0, bx + w, by1 };
        bx += w + 10.0f;
    } else {
        m_card.btnLink = { 0, 0, 0, 0 };
    }
    if (hasFolder) {
        const float w = hasLink ? 128.0f : 200.0f;
        m_card.btnFolder = { bx, by0, bx + w, by1 };
    } else {
        m_card.btnFolder = { 0, 0, 0, 0 };
    }
}

void CheckinView::PaintOverlay(Canvas& cv)
{
    const auto& pal = cv.Pal();
    ComputeOverlayRects();
    const float a = m_overlayA.value;

    // 背景遮罩
    cv.FillRect(m_area, WithAlpha(pal.paperDeep, 0.55f * a));
    cv.PushOpacity(a);

    auto DrawBtn = [&](const D2D1_RECT_F& r, const std::wstring& label, bool primary) {
        D2D1_COLOR_F bg = primary ? pal.seal : pal.paperLo;
        cv.FillRoundRect(r, 6.0f, bg);
        cv.StrokeRoundRect(r, 6.0f, primary ? pal.seal : pal.rule, shape::kHair);
        TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.0f; bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle; bs.letterSpacing = 1.0f;
        cv.Text(label, r, bs, primary ? pal.paperHi : pal.ink700);
    };

    if (m_alert.active) {
        cv.PaperCard(m_alert.card, 0.4f, shape::kEdge);
        cv.DoubleFrame(m_alert.card, WithAlpha(pal.seal, 0.8f));

        TextStyle ttl; ttl.role = FontRole::Serif; ttl.size = 22.0f;
        ttl.weight = DWRITE_FONT_WEIGHT_BOLD; ttl.letterSpacing = 2.0f;
        cv.Text(m_alert.title, { m_alert.card.left + 28.0f, m_alert.card.top + 22.0f,
                  m_alert.card.right - 28.0f, m_alert.card.top + 54.0f }, ttl, pal.seal);

        TextStyle tx1; tx1.role = FontRole::Sans; tx1.size = 13.5f; tx1.vAlign = VAlign::Top;
        cv.Text(m_alert.line1, { m_alert.card.left + 28.0f, m_alert.card.top + 70.0f,
                 m_alert.card.right - 28.0f, m_alert.card.top + 120.0f }, tx1, pal.ink700);
        TextStyle tx2; tx2.role = FontRole::Sans; tx2.size = 13.5f; tx2.vAlign = VAlign::Top;
        tx2.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(m_alert.line2, { m_alert.card.left + 28.0f, m_alert.card.top + 122.0f,
                 m_alert.card.right - 28.0f, m_alert.card.top + 170.0f }, tx2, pal.vermilion);

        if (m_alert.mode == 0) {
            DrawBtn(m_alert.btnLog, L"记一段专注", true);
            DrawBtn(m_alert.btnOk, L"知道了", false);
        } else {
            DrawBtn(m_alert.btnOk, L"知道了", true);
        }
    } else if (m_card.active) {
        const CheckItem& it = m_card.item;
        cv.PaperCard(m_card.card, 0.4f, shape::kEdge);
        cv.DoubleFrame(m_card.card, WithAlpha(pal.seal, 0.8f));

        // 头部：时段 + 标题 + 标签
        TextStyle ttl; ttl.role = FontRole::Serif; ttl.size = 21.0f;
        ttl.weight = DWRITE_FONT_WEIGHT_BOLD; ttl.letterSpacing = 1.0f;
        cv.Text(it.title, { m_card.card.left + 28.0f, m_card.card.top + 22.0f,
                 m_card.card.right - 28.0f, m_card.card.top + 56.0f }, ttl, pal.ink900);
        TextStyle meta; meta.role = FontRole::Mono; meta.size = 12.0f; meta.letterSpacing = 0.8f;
        std::wstring metaS = (it.slot.empty() ? L"随时" : it.slot);
        if (it.minutes > 0) metaS += L" · 约 " + std::to_wstring(it.minutes) + L" 分钟";
        if (!it.tag.empty()) metaS += L" · " + it.tag;
        cv.Text(metaS, { m_card.card.left + 28.0f, m_card.card.top + 58.0f,
                  m_card.card.right - 28.0f, m_card.card.top + 80.0f }, meta, pal.ink500);

        float yy = m_card.card.top + 98.0f;
        auto Section = [&](const std::wstring& head, const std::wstring& body, bool strong) {
            TextStyle hs; hs.role = FontRole::Mono; hs.size = 11.0f; hs.letterSpacing = 2.0f;
            hs.weight = DWRITE_FONT_WEIGHT_BOLD;
            cv.Text(head, { m_card.card.left + 28.0f, yy, m_card.card.right - 28.0f, yy + 16.0f },
                    hs, pal.seal);
            TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.5f; bs.vAlign = VAlign::Top;
            bs.weight = strong ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
            float hh = cv.MeasureHeight(body, bs, m_card.card.right - m_card.card.left - 56.0f);
            cv.Text(body, { m_card.card.left + 28.0f, yy + 20.0f,
                     m_card.card.right - 28.0f, yy + 20.0f + hh }, bs,
                    strong ? pal.ink900 : pal.ink700);
            yy += 20.0f + hh + 18.0f;
        };
        std::wstring what = L"在 " + (it.slot.empty() ? L"随时" : it.slot)
                          + (it.minutes > 0 ? (L"（约 " + std::to_wstring(it.minutes) + L" 分钟）") : L"")
                          + L" 完成：" + (it.title.empty() ? L"任务" : it.title);
        Section(L"做什么", what, true);
        Section(L"怎么学", it.standard.empty() ? L"（该账户此任务未设置学习方法说明）" : it.standard, false);

        std::wstring where;
        if (!it.link.empty())   where = it.link;
        if (!it.folder.empty()) {
            if (!where.empty()) where += L"\n";
            where += L"本地资料夹：" + it.folder;
        }
        if (where.empty()) where = L"本项未设置外部链接或本地资料夹，按完成标准直接执行即可。";
        Section(L"去哪学", where, false);

        if (!it.link.empty())   DrawBtn(m_card.btnLink,   L"↗ 打开学习资源", true);
        if (!it.folder.empty()) DrawBtn(m_card.btnFolder, L"▤ 打开资料夹",   it.link.empty());
        DrawBtn(m_card.btnClose, L"知道了", it.link.empty() && it.folder.empty());
    }

    cv.PopOpacity();
}

// ============================================================
//  交互逻辑
// ============================================================
void CheckinView::toggleRow(int idx)
{
    if (idx < 0 || idx >= (int)m_done.size()) return;
    m_done[idx] = m_done[idx] ? 0 : 1;
    Recompute();
    SaveToday();
    if (m_done[idx]) {
        markRhythm(m_items[idx]);
        checkFocusMonitor(m_items[idx]);
    }
}

// 勾选「作息」类任务时把真实时刻落到 rhythm.json，供仪表盘作息环形图使用。
// 判定：tag=作息/早起/睡觉，或标题里含 起床/早起/就寝/睡觉/入睡。
void CheckinView::markRhythm(const CheckItem& it)
{
    auto has = [&](const wchar_t* k) {
        return it.title.find(k) != std::wstring::npos;
    };
    const bool routineTag = (it.tag == L"作息" || it.tag == L"早起" || it.tag == L"睡觉");
    const bool wakeWord   = has(L"起床") || has(L"早起") || has(L"早　起");
    const bool sleepWord  = has(L"就寝") || has(L"睡觉") || has(L"入睡") || has(L"熄灯");
    if (!routineTag && !wakeWord && !sleepWord) return;
    if (!wakeWord && !sleepWord) return;          // 仅有 tag 无法判断早晚，跳过

    __time64_t now = _time64(nullptr);
    struct tm lt;
    _localtime64_s(&lt, &now);
    const int minuteOfDay = lt.tm_hour * 60 + lt.tm_min;

    // 就寝跨零点：0-4 点勾「睡觉」，仍算前一天的作息
    std::wstring key = FormatDate(Today());
    if (sleepWord && lt.tm_hour < 5) {
        Date td = Today();
        key = FormatDate(DateFromCivil(DaysFromCivil(td.y, td.m, td.d) - 1));
    }
    CheckinStore::Instance().MarkRhythm(key, sleepWord, minuteOfDay);
}

void CheckinView::checkFocusMonitor(const CheckItem& it)
{
    if (it.slot.empty()) return;
    bool routine = (it.tag == L"作息" || it.tag == L"早起" || it.tag == L"睡觉"
                    || it.title.find(L"睡觉") != std::wstring::npos);
    if (routine) return;

    // 解析 HH:MM（支持中文冒号）
    int hh = -1, mm = 0;
    for (size_t i = 0; i + 1 < it.slot.size(); ++i) {
        if (iswdigit(it.slot[i]) && iswdigit(it.slot[i + 1])) {
            bool leftOk = (i == 0) || !iswdigit(it.slot[i - 1]);
            wchar_t nxt = (i + 2 < it.slot.size()) ? it.slot[i + 2] : L'\0';
            if (leftOk && (nxt == L':' || nxt == L'：')) {
                hh = (it.slot[i] - L'0') * 10 + (it.slot[i + 1] - L'0');
                size_t j = i + 3;
                if (j + 1 < it.slot.size() && iswdigit(it.slot[j]) && iswdigit(it.slot[j + 1])) {
                    mm = (it.slot[j] - L'0') * 10 + (it.slot[j + 1] - L'0');
                }
                break;
            }
        }
    }
    if (hh < 0) return;
    int taskMin = hh * 60 + mm;

    auto focus = CheckinStore::Instance().LoadFocus();
    std::wstring todayKey = FormatDate(Today());
    bool has = false;
    for (auto& s : focus) {
        if (s.date != todayKey || s.start == 0) continue;
        __time64_t t = (__time64_t)s.start;
        struct tm tm2;
        _localtime64_s(&tm2, &t);
        int fmin = tm2.tm_hour * 60 + tm2.tm_min;
        if (std::abs(fmin - taskMin) <= 60) { has = true; break; }
    }
    if (!has) ShowFocusAlert(it);
}

void CheckinView::ShowFocusAlert(const CheckItem& it)
{
    m_alert.active = true;
    m_alert.mode = 0;
    m_alert.title = L"专注监控提醒";
    m_alert.line1 = L"「" + it.title + L"」安排在 " + (it.slot.empty() ? L"随时" : it.slot)
                  + L"，但该时段前后 1 小时内没有专注记录。";
    m_alert.line2 = L"虚假打卡是对自己的不负责——先去自习室专注一段再勾吧。";
    m_overlayA.target = 1.0f;
}

void CheckinView::LogFocusNow()
{
    FocusSession fs;
    fs.date = FormatDate(Today());
    fs.start = (long long)time(nullptr);
    fs.end = fs.start + 25 * 60;
    fs.min = 25;
    fs.tag = L"自习室";
    CheckinStore::Instance().AddFocus(fs);
}

void CheckinView::ShowSuccess(const std::wstring& msg)
{
    m_alert.active = true;
    m_alert.mode = 1;
    m_alert.title = L"已记录专注";
    m_alert.line1 = msg;
    m_alert.line2 = L"已写入专注档案（archive/focus.json）。";
    m_overlayA.target = 1.0f;
}

void CheckinView::openTaskCard(const CheckItem& it)
{
    m_card.active = true;
    m_card.item = it;
    m_overlayA.target = 1.0f;
}

} // namespace lj
