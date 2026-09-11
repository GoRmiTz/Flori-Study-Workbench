// ============================================================
//  ReviewView.cpp — F-D7 本地复盘搭子（规则版）
//  基于 ReviewEngine 的今日复盘结论，渲染为一张报告卡 + 操作按钮。
// ============================================================
#include "views/ReviewView.h"
#include "app/Store.h"
#include "ui/Layout.h"

namespace lj {

bool ReviewView::InRect(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

void ReviewView::OnEnter()
{
    View::OnEnter();
    LoadNudge();
    Regenerate();
}

void ReviewView::LoadNudge()
{
    auto s = CheckinStore::Instance().LoadSettings();
    m_nudgeOn = s.reviewNudge;
    m_nudgeHour = s.reviewNudgeHour;
}

void ReviewView::SaveNudge()
{
    auto s = CheckinStore::Instance().LoadSettings();
    s.reviewNudge = m_nudgeOn;
    s.reviewNudgeHour = m_nudgeHour;
    CheckinStore::Instance().SaveSettings(s);
}

void ReviewView::Regenerate()
{
    m_r = ReviewEngine::Generate(FormatDate(Today()));
    if (m_cv) Layout(m_area, *m_cv);
}

void ReviewView::Toast(const std::wstring& msg)
{
    m_toastMsg = msg;
    m_toast = true;
    m_toastT = 0.0f;
}

void ReviewView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    m_area = area;
    m_cv = &cv;

    float pad = 28.0f;
    float x0 = area.left + pad;
    float contentW = (area.right - area.left) - 2.0f * pad;
    float x1 = area.left + 28.0f;
    float cw = contentW - 56.0f;

    ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);

    // 标题区
    flow.block(96.0f);

    // ---- 复盘提醒设置卡 ----
    float setH = 146.0f;
    D2D1_RECT_F setCard = flow.block(setH);
    m_setCard = { x1, setCard.top, x1 + cw, setCard.bottom };
    float slx = m_setCard.left + 24.0f;
    float srx = m_setCard.right - 24.0f;
    float yTitle = m_setCard.top + 16.0f;
    float yToggle = yTitle + 22.0f + 12.0f;
    m_toggleRow = { slx, yToggle, srx, yToggle + 36.0f };
    float yHour = yToggle + 36.0f + 12.0f;
    float hh = 28.0f, minusW = 32.0f, hourW = 56.0f, plusW = 32.0f;
    float ctrlW = minusW + 8.0f + hourW + 8.0f + plusW;
    float ctrlX = srx - ctrlW;
    m_minusBtn = { ctrlX, yHour + (36.0f - hh) / 2.0f, ctrlX + minusW, yHour + (36.0f - hh) / 2.0f + hh };
    m_hourRect = { ctrlX + minusW + 8.0f, yHour + (36.0f - hh) / 2.0f,
                   ctrlX + minusW + 8.0f + hourW, yHour + (36.0f - hh) / 2.0f + hh };
    m_plusBtn  = { ctrlX + minusW + 8.0f + hourW + 8.0f, yHour + (36.0f - hh) / 2.0f,
                   ctrlX + ctrlW, yHour + (36.0f - hh) / 2.0f + hh };

    // 报告卡：标题(34) + 行(每行 30) + 薄弱项块(若有) + 提示块 + 按钮(46) + 间距
    int lineN = (int)m_r.lines.size();
    float linesH = (float)lineN * 30.0f;
    float weakH = 0.0f;
    if (!m_r.weak.empty())
        weakH = 30.0f /*小标题*/ + 22.0f * (float)m_r.weak.size() + 14.0f;
    float hintH = 56.0f;     // 一句提示块
    float btnH = 46.0f + 16.0f;
    float cardH = 34.0f + 12.0f + linesH + 12.0f + weakH + 12.0f + hintH + btnH + 16.0f;
    D2D1_RECT_F card = flow.block(cardH);
    m_card = { x1, card.top + 34.0f, x1 + cw, card.top + 34.0f + cardH - 16.0f };
    float innerL = m_card.left + 24.0f;
    float innerR = m_card.right - 24.0f;

    // 按钮：卡内底部右侧（写入明日三要事 + 重新生成）
    float bw = 150.0f, bh = 38.0f;
    m_commitBtn = ui::MakeRect(innerR - bw, m_card.bottom - bh - 8.0f, bw, bh);
    m_refreshBtn = ui::MakeRect(innerR - bw * 2.0f - 12.0f, m_card.bottom - bh - 8.0f, bw, bh);

    SetContentHeight(flow.cursorY - area.top);
}

void ReviewView::Update(float dt, const Input& in)
{
    if (m_toast) { m_toastT += dt; if (m_toastT > 2.4f) m_toast = false; }

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    float mx = in.mouseX, my = shifted.mouseY;

    // 步进按钮 hover（仅开启时高亮）
    m_hoverMinus = m_nudgeOn && InRect(m_minusBtn, mx, my);
    m_hoverPlus  = m_nudgeOn && InRect(m_plusBtn, mx, my);

    if (in.clicked) {
        // 提醒开关：整行点击切换
        if (InRect(m_toggleRow, mx, my)) {
            m_nudgeOn = !m_nudgeOn;
            SaveNudge();
            Toast(m_nudgeOn ? L"已开启每日复盘提醒" : L"已关闭每日复盘提醒");
            return;
        }
        // 提醒时刻：仅开启时可调
        if (m_nudgeOn) {
            if (InRect(m_minusBtn, mx, my)) {
                m_nudgeHour = m_nudgeHour > 0 ? m_nudgeHour - 1 : 0;
                SaveNudge();
                std::wstring msg = L"提醒时间调整为 " + std::to_wstring(m_nudgeHour) + L":00";
                Toast(msg);
                return;
            }
            if (InRect(m_plusBtn, mx, my)) {
                m_nudgeHour = m_nudgeHour < 23 ? m_nudgeHour + 1 : 23;
                SaveNudge();
                std::wstring msg = L"提醒时间调整为 " + std::to_wstring(m_nudgeHour) + L":00";
                Toast(msg);
                return;
            }
        }
        if (InRect(m_commitBtn, mx, my)) {
            ReviewEngine::CommitWeakToJournal(FormatDate(Today()));
            Toast(L"已写入明日三要事");
            Regenerate();
            return;
        }
        if (InRect(m_refreshBtn, mx, my)) {
            Regenerate();
            Toast(L"已重新生成今日复盘");
            return;
        }
    }
    View::Update(dt, in);
}

void ReviewView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    // 标题
    TextStyle ht; ht.size = 22.0f; ht.role = FontRole::Serif; ht.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"今日复盘 · 本地搭子", { m_area.left + 28.0f, m_area.top + 24.0f,
            m_area.right - 28.0f, m_area.top + 60.0f }, ht, pal.ink900);
    TextStyle sub; sub.size = 12.0f; sub.role = FontRole::Sans;
    cv.Text(L"基于真实打卡 / 专注 / 模考数据，规则引擎比对方案目标，全程不联网", { m_area.left + 28.0f,
            m_area.top + 62.0f, m_area.right - 28.0f, m_area.top + 84.0f }, sub, pal.ink500);

    // ---- 复盘提醒设置卡 ----
    cv.PaperCard(m_setCard, 2.0f);
    cv.StrokeRoundRect(m_setCard, 2.0f, pal.rule, shape::kHair);
    // 左侧书脊：用 jade 与报告卡的 seal 区分
    cv.FillRect({ m_setCard.left, m_setCard.top, m_setCard.left + 2.5f, m_setCard.bottom },
                WithAlpha(pal.jade, 0.85f));

    float slx = m_setCard.left + 24.0f;
    float srx = m_setCard.right - 24.0f;
    float yTitle = m_setCard.top + 16.0f;
    TextStyle st; st.size = 11.0f; st.role = FontRole::Mono; st.letterSpacing = 2.0f;
    st.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SETTING · 每日复盘提醒", { slx, yTitle, srx, yTitle + 18.0f }, st, pal.ink300);

    // 切换行
    TextStyle lt; lt.size = 13.5f; lt.role = FontRole::Sans; lt.vAlign = VAlign::Middle;
    cv.Text(L"收工前自动提醒（托盘气泡，点击跳复盘）",
            { slx, m_toggleRow.top, m_toggleRow.right - 92.0f, m_toggleRow.bottom },
            lt, m_nudgeOn ? pal.ink900 : pal.ink500);
    // 开关
    {
        float sw = 52.0f, sh = 26.0f;
        float sx = m_toggleRow.right - sw;
        float sy = m_toggleRow.top + (m_toggleRow.bottom - m_toggleRow.top - sh) / 2.0f;
        D2D1_RECT_F track{ sx, sy, sx + sw, sy + sh };
        cv.FillRoundRect(track, sh / 2.0f, m_nudgeOn ? pal.seal : WithAlpha(pal.ink300, 0.4f));
        float kx = m_nudgeOn ? track.right - sh / 2.0f - 3.0f : track.left + sh / 2.0f + 3.0f;
        cv.FillCircle(kx, sy + sh / 2.0f, sh / 2.0f - 3.0f, pal.paperHi);
    }
    // 时间行
    TextStyle lh; lh.size = 13.5f; lh.role = FontRole::Sans; lh.vAlign = VAlign::Middle;
    cv.Text(L"提醒时间", { slx, m_minusBtn.top - 4.0f, m_minusBtn.left - 12.0f, m_minusBtn.bottom + 4.0f },
            lh, m_nudgeOn ? pal.ink900 : pal.ink300);
    auto StepBtn = [&](const D2D1_RECT_F& r, const wchar_t* ch, bool hov) {
        bool en = m_nudgeOn;
        cv.FillRoundRect(r, 6.0f, !en ? WithAlpha(pal.ink300, 0.10f)
                            : (hov ? WithAlpha(pal.seal, 0.14f) : WithAlpha(pal.rule, 0.10f)));
        cv.StrokeRoundRect(r, 6.0f, en ? pal.rule : WithAlpha(pal.ink300, 0.5f), shape::kHair);
        TextStyle ts; ts.size = 16.0f; ts.role = FontRole::Sans; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
        ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
        cv.Text(ch, r, ts, en ? pal.ink900 : pal.ink300);
    };
    StepBtn(m_minusBtn, L"−", m_hoverMinus);
    StepBtn(m_plusBtn,  L"+", m_hoverPlus);
    std::wstring hourStr = (m_nudgeHour < 10 ? L"0" : L"");
    hourStr += std::to_wstring(m_nudgeHour);
    hourStr += L":00";
    TextStyle hts; hts.size = 14.0f; hts.role = FontRole::Sans; hts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    hts.hAlign = HAlign::Center; hts.vAlign = VAlign::Middle;
    cv.Text(hourStr, m_hourRect, hts, m_nudgeOn ? pal.ink900 : pal.ink300);

    // 报告卡
    cv.PaperCard(m_card, 2.0f);
    cv.StrokeRoundRect(m_card, 2.0f, pal.rule, shape::kHair);
    // 左侧朱砂书脊
    cv.FillRect({ m_card.left, m_card.top, m_card.left + 2.5f, m_card.bottom },
                WithAlpha(pal.seal, 0.85f));

    float lx = m_card.left + 24.0f;
    float rx = m_card.right - 24.0f;
    float y = m_card.top + 18.0f;

    TextStyle sec; sec.size = 11.0f; sec.role = FontRole::Mono; sec.letterSpacing = 2.0f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · " + m_r.dateKey, { lx, y, rx, y + 18.0f }, sec, pal.ink300);
    y += 30.0f;

    // 逐行结论
    TextStyle ls; ls.size = 14.0f; ls.role = FontRole::Sans; ls.vAlign = VAlign::Top;
    for (size_t i = 0; i < m_r.lines.size(); ++i) {
        bool isHint = (i + 1 == m_r.lines.size());
        cv.Text(m_r.lines[i], { lx, y, rx, y + 26.0f }, ls,
                isHint ? pal.ink900 : pal.ink700);
        y += 30.0f;
    }
    y += 6.0f;

    // 薄弱项明细（若有）
    if (!m_r.weak.empty()) {
        TextStyle ws; ws.size = 12.5f; ws.role = FontRole::Sans; ws.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(L"薄弱项明细", { lx, y, rx, y + 20.0f }, ws, pal.ink500);
        y += 24.0f;
        TextStyle its; its.size = 12.5f; its.role = FontRole::Sans; its.vAlign = VAlign::Top;
        for (size_t i = 0; i < m_r.weak.size(); ++i) {
            const auto& w = m_r.weak[i];
            D2D1_COLOR_F bar = MixColor(pal.seal, pal.jade, (float)w.pct / 100.0f);
            // 进度条：实际 / 目标
            float barW = rx - lx - 120.0f;
            D2D1_RECT_F track = { lx, y + 4.0f, lx + barW, y + 14.0f };
            D2D1_RECT_F fill = { lx, y + 4.0f, lx + barW * (float)w.pct / 100.0f, y + 14.0f };
            cv.FillRoundRect(track, 3.0f, WithAlpha(pal.ink300, 0.12f));
            cv.FillRoundRect(fill, 3.0f, WithAlpha(bar, 0.9f));
            TextStyle ts; ts.size = 12.0f; ts.role = FontRole::Sans;
            cv.Text(w.tag + L" " + std::to_wstring(w.pct) + L"% / 目标 " + std::to_wstring(w.target) + L"%",
                    { lx, y - 2.0f, lx + barW + 8.0f, y + 22.0f }, ts, pal.ink700);
            y += 24.0f;
        }
        y += 6.0f;
    }

    // 一句提示块
    D2D1_RECT_F hb = { lx, y, rx, y + 48.0f };
    cv.FillRoundRect(hb, 8.0f, WithAlpha(pal.seal, 0.10f));
    cv.StrokeRoundRect(hb, 8.0f, WithAlpha(pal.seal, 0.5f), shape::kHair);
    TextStyle hs; hs.size = 13.5f; hs.role = FontRole::Sans; hs.vAlign = VAlign::Middle;
    cv.Text(m_r.hint, { lx + 14.0f, y, rx - 14.0f, y + 48.0f }, hs, pal.ink900);

    // 按钮
    auto Btn = [&](const D2D1_RECT_F& r, const wchar_t* label, bool primary) {
        cv.FillRoundRect(r, 8.0f, primary ? pal.seal : pal.paperLo);
        cv.StrokeRoundRect(r, 8.0f, primary ? pal.seal : pal.rule, shape::kHair);
        TextStyle bs; bs.size = 13.0f; bs.role = FontRole::Sans;
        bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle;
        cv.Text(label, r, bs, primary ? pal.paperHi : pal.ink700);
    };
    Btn(m_commitBtn, L"写入明日三要事", true);
    Btn(m_refreshBtn, L"重新生成", false);

    cv.PopTransform();
    cv.PopClip();

    if (m_toast) DrawToast(cv);
}

void ReviewView::DrawToast(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float w = 240.0f, h = 40.0f;
    float x = m_area.right - w - 28.0f;
    float y = m_area.bottom - h - 28.0f;
    D2D1_RECT_F r{ x, y, x + w, y + h };
    cv.FillRoundRect(r, 8.0f, pal.seal);
    TextStyle ts; ts.size = 13.0f; ts.role = FontRole::Sans;
    ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
    cv.Text(m_toastMsg, r, ts, pal.paperHi);
}

} // namespace lj
