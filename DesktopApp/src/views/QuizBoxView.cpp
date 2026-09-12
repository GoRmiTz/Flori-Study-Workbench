// ============================================================
//  QuizBoxView.cpp — 题集卡片盒 v2 实现（批次 G2）
//  需求细节见 docs/题集卡片盒·开发文档.md（不得丢失）。
// ============================================================
#include "views/QuizBoxView.h"
#include "ui/Layout.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace lj {

static bool InRect(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

// 视图渐入：150ms，12px 上滑
static float ViewAlpha(float t) { return Clamp01(t / 0.15f); }

void QuizBoxView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_view = V_SETS;
    m_curSet = -1; m_curBox = -1; m_drawIdx = -1;
    m_ceOpen = false; m_renActive = false;
    m_viewT = 0.0f;
    m_sets = BoxStore::Instance().Load();
}

void QuizBoxView::Toast(const std::wstring& msg)
{
    m_toast = msg;
    m_toastT = 2.2f;
}

void QuizBoxView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    m_area = area;
    m_cv = &cv;
}

// ============================================================
//  矢量图形：房子（题集）/ 盒子（题盒）—— 占位图形，UI 稿后替换
// ============================================================
void QuizBoxView::DrawHouse(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& accent) const
{
    // 屋顶三角 + 房身 + 门
    float w = r.right - r.left, h = r.bottom - r.top;
    float cx = (r.left + r.right) * 0.5f;
    cv.FillRect({ r.left + w * 0.14f, r.top + h * 0.46f, r.right - w * 0.14f, r.bottom },
                WithAlpha(accent, 0.16f));
    cv.StrokeRect({ r.left + w * 0.14f, r.top + h * 0.46f, r.right - w * 0.14f, r.bottom },
                  accent, 1.4f);
    cv.Line(r.left + w * 0.06f, r.top + h * 0.5f, cx, r.top + h * 0.06f, accent, 1.6f);
    cv.Line(cx, r.top + h * 0.06f, r.right - w * 0.06f, r.top + h * 0.5f, accent, 1.6f);
    cv.Line(r.left + w * 0.06f, r.top + h * 0.5f, r.right - w * 0.06f, r.top + h * 0.5f, accent, 1.6f);
    // 门
    cv.FillRect({ cx - w * 0.09f, r.bottom - h * 0.36f, cx + w * 0.09f, r.bottom - h * 0.04f },
                WithAlpha(accent, 0.5f));
}

void QuizBoxView::DrawBoxIcon(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& accent,
                              bool wrong) const
{
    // 盒身 + 盖沿 + 正面标签线
    float w = r.right - r.left, h = r.bottom - r.top;
    D2D1_RECT_F lid{ r.left + w * 0.08f, r.top + h * 0.2f, r.right - w * 0.08f, r.top + h * 0.42f };
    D2D1_RECT_F body{ r.left + w * 0.16f, r.top + h * 0.42f, r.right - w * 0.16f, r.bottom };
    cv.FillRoundRect(body, 3.0f, WithAlpha(accent, 0.16f));
    cv.StrokeRoundRect(body, 3.0f, accent, 1.4f);
    cv.FillRoundRect(lid, 3.0f, WithAlpha(accent, 0.34f));
    cv.StrokeRoundRect(lid, 3.0f, accent, 1.4f);
    // 正面标签线（错题盒画 ✕）
    float my = (body.top + body.bottom) * 0.5f;
    if (wrong) {
        cv.Line(my - 0.0f + body.left + w * 0.32f, my - h * 0.1f, body.left + w * 0.44f, my + h * 0.1f,
                accent, 1.6f);
        cv.Line(body.left + w * 0.44f, my - h * 0.1f, body.left + w * 0.32f, my + h * 0.1f, accent, 1.6f);
    } else {
        cv.Line(body.left + w * 0.3f, my, body.left + w * 0.55f, my, WithAlpha(accent, 0.7f), 1.2f);
    }
}

// ============================================================
//  题集架（V_SETS）
// ============================================================
void QuizBoxView::DrawSets(Canvas& cv, float s)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_area.top + 30.0f;

    float va = ViewAlpha(m_viewT);
    float slide = (1.0f - ease::OutCubic(va)) * 12.0f;
    cv.PushOpacity(va);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, slide));

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 题集 · 学习区域", { x0, y0, x0 + 420.0f, y0 + 16.0f }, sec, pal.ink300);
    TextStyle h1; h1.role = FontRole::Serif; h1.size = 34.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"题 集", x0, y0 + 24.0f, h1, pal.ink900, m_t * 1.1f, 0.05f, 16.0f);
    TextStyle lead; lead.size = 12.0f; lead.role = FontRole::Sans;
    cv.Text(L"每个题集是一个学习区域（房子），内含多个题盒（盒子），收题、错题、抽卡回顾。",
            { x0, y0 + 70.0f, x0 + contentW, y0 + 90.0f }, lead, pal.ink500);
    cv.PerforationH(x0, x0 + contentW, y0 + 102.0f, WithAlpha(pal.ruleStrong, 0.5f));

    m_setRects.clear(); m_setDelRects.clear();
    float gw = (contentW - 24.0f) / 2.0f, gh = 170.0f, gap = 24.0f;
    float gy = y0 + 122.0f;
    for (size_t i = 0; i < m_sets.size(); ++i) {
        int c = (int)i % 2, rr = (int)i / 2;
        float bx = x0 + c * (gw + gap), by = gy + rr * (gh + gap);
        D2D1_RECT_F card{ bx, by, bx + gw, by + gh };
        m_setRects.push_back(card);
        const auto& st = m_sets[i];

        // 新建弹入（T2）：OutBack scale 近似（位移 + alpha）
        float ea = 1.0f;
        if ((int)i == m_newHighlight && m_newT < 0.4f) {
            float k = Clamp01(m_newT / 0.4f);
            ea = ease::OutBack(k);
            cv.PushOpacity(k);
            cv.PushTransform(D2D1::Matrix3x2F::Translation(
                0.0f, (1.0f - ea) * 14.0f));
        }

        cv.PaperCard(card, 2.0f);
        cv.StrokeRoundRect(card, shape::kEdge, WithAlpha(pal.seal, 0.5f), shape::kHair);
        // 房子图标（左）
        DrawHouse(cv, { bx + 18.0f, by + 18.0f, bx + 92.0f, by + 96.0f }, pal.seal);
        // 题集名 + 统计
        TextStyle nst; nst.role = FontRole::Serif; nst.size = 20.0f;
        nst.weight = DWRITE_FONT_WEIGHT_BOLD; nst.vAlign = VAlign::Middle;
        cv.Text(st.name, { bx + 106.0f, by + 22.0f, bx + gw - 66.0f, by + 52.0f }, nst, pal.ink900);
        int cardN = 0;
        for (auto& b : st.boxes) cardN += (int)b.cards.size();
        wchar_t sb[48];
        swprintf_s(sb, L"%d 个题盒 · %d 张卡片", (int)st.boxes.size(), cardN);
        TextStyle cst; cst.size = 11.5f; cst.role = FontRole::Mono; cst.vAlign = VAlign::Middle;
        cv.Text(sb, { bx + 106.0f, by + 56.0f, bx + gw - 66.0f, by + 78.0f }, cst, pal.ink500);
        cv.PerforationH(bx + 18.0f, bx + gw - 18.0f, by + gh - 22.0f, WithAlpha(pal.rule, 0.6f));
        // ✕ 删除
        D2D1_RECT_F del{ bx + gw - 50.0f, by + 18.0f, bx + gw - 20.0f, by + 44.0f };
        m_setDelRects.push_back(del);
        TextStyle dbt; dbt.size = 13.0f; dbt.hAlign = HAlign::Center; dbt.vAlign = VAlign::Middle;
        cv.Text(L"✕", del, dbt, pal.vermilion);

        if ((int)i == m_newHighlight && m_newT < 0.4f) {
            cv.PopTransform();
            cv.PopOpacity();
        }
    }

    // ＋ 新建题集
    {
        int c = (int)m_sets.size() % 2, rr = (int)m_sets.size() / 2;
        float bx = x0 + c * (gw + gap), by = gy + rr * (gh + gap);
        m_newSetRect = { bx, by, bx + gw, by + gh };
        cv.FillRoundRect(m_newSetRect, shape::kEdge, WithAlpha(pal.seal, 0.06f));
        cv.StrokeRoundRect(m_newSetRect, shape::kEdge, WithAlpha(pal.seal, 0.6f), shape::kHair);
        TextStyle nt; nt.size = 15.0f; nt.role = FontRole::Sans;
        nt.hAlign = HAlign::Center; nt.vAlign = VAlign::Middle; nt.letterSpacing = 2.0f;
        D2D1_RECT_F half{ bx, by, bx + gw, by + gh * 0.5f };
        cv.Text(L"＋ 新建题集", half, nt, pal.seal);
        TextStyle st2; st2.size = 11.0f; st2.role = FontRole::Sans;
        st2.hAlign = HAlign::Center; st2.vAlign = VAlign::Middle;
        D2D1_RECT_F half2{ bx, by + gh * 0.5f, bx + gw, by + gh };
        cv.Text(L"创建一个学习区域", half2, st2, pal.ink300);
    }

    if (m_sets.empty()) {
        TextStyle et; et.size = 12.0f; et.role = FontRole::Sans;
        et.hAlign = HAlign::Center; et.vAlign = VAlign::Middle;
        cv.Text(L"建第一个题集——比如「考公」「考研英语」，每个区域独立收题。",
                { x0, gy + gh + 18.0f, x0 + contentW, gy + gh + 44.0f }, et, pal.ink500);
    }

    cv.PopTransform();
    cv.PopOpacity();
}

// ============================================================
//  盒架（V_BOXES）：某题集内
// ============================================================
void QuizBoxView::DrawBoxes(Canvas& cv, float s)
{
    if (m_curSet < 0 || m_curSet >= (int)m_sets.size()) { m_view = V_SETS; return; }
    const auto& pal = cv.Pal();
    const auto& st = m_sets[m_curSet];
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_area.top + 30.0f;

    float va = ViewAlpha(m_viewT);
    float slide = (1.0f - ease::OutCubic(va)) * 12.0f;
    cv.PushOpacity(va);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, slide));

    m_backRect = { x0, y0, x0 + 150.0f, y0 + 38.0f };
    cv.FillRoundRect(m_backRect, 6.0f, pal.paperLo);
    cv.StrokeRoundRect(m_backRect, 6.0f, pal.rule, shape::kHair);
    TextStyle bt; bt.size = 12.5f; bt.role = FontRole::Sans;
    bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle;
    cv.Text(L"🏠 题集架", m_backRect, bt, pal.ink700);

    TextStyle h1; h1.role = FontRole::Serif; h1.size = 28.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 2.0f; h1.vAlign = VAlign::Middle;
    cv.Text(st.name, { x0 + 170.0f, y0 - 2.0f, x0 + contentW, y0 + 38.0f }, h1, pal.ink900);
    cv.PerforationH(x0, x0 + contentW, y0 + 62.0f, WithAlpha(pal.ruleStrong, 0.5f));

    m_boxRects.clear(); m_boxRenRects.clear(); m_boxDelRects.clear();
    float gw = (contentW - 24.0f) / 2.0f, gh = 156.0f, gap = 24.0f;
    float gy = y0 + 82.0f;
    for (size_t i = 0; i < st.boxes.size(); ++i) {
        int c = (int)i % 2, rr = (int)i / 2;
        float bx = x0 + c * (gw + gap), by = gy + rr * (gh + gap);
        D2D1_RECT_F card{ bx, by, bx + gw, by + gh };
        m_boxRects.push_back(card);
        const auto& b = st.boxes[i];
        bool wrong = (b.kind == 1);
        D2D1_COLOR_F accent = wrong ? pal.vermilion : pal.jade;

        float ea = 1.0f;
        if ((int)i == m_newHighlight && m_newT < 0.4f) {
            float k = Clamp01(m_newT / 0.4f);
            ea = ease::OutBack(k);
            cv.PushOpacity(k);
            cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - ea) * 14.0f));
        }

        cv.PaperCard(card, 2.0f);
        cv.StrokeRoundRect(card, shape::kEdge, WithAlpha(accent, 0.55f), shape::kHair);
        DrawBoxIcon(cv, { bx + 18.0f, by + 20.0f, bx + 90.0f, by + 100.0f }, accent, wrong);
        TextStyle nst; nst.role = FontRole::Serif; nst.size = 19.0f;
        nst.weight = DWRITE_FONT_WEIGHT_BOLD; nst.vAlign = VAlign::Middle;
        cv.Text(b.name, { bx + 104.0f, by + 22.0f, bx + gw - 66.0f, by + 50.0f }, nst, pal.ink900);
        TextStyle cst; cst.size = 11.5f; cst.role = FontRole::Mono; cst.vAlign = VAlign::Middle;
        wchar_t cb[40];
        swprintf_s(cb, L"%d 张卡片", (int)b.cards.size());
        cv.Text(cb, { bx + 104.0f, by + 54.0f, bx + gw - 66.0f, by + 76.0f }, cst, pal.ink500);
        cv.PerforationH(bx + 18.0f, bx + gw - 18.0f, by + gh - 20.0f, WithAlpha(pal.rule, 0.6f));

        D2D1_RECT_F ren{ bx + gw - 78.0f, by + 18.0f, bx + gw - 50.0f, by + 42.0f };
        D2D1_RECT_F del{ bx + gw - 44.0f, by + 18.0f, bx + gw - 18.0f, by + 42.0f };
        m_boxRenRects.push_back(ren);
        m_boxDelRects.push_back(del);
        TextStyle sbt; sbt.size = 13.0f; sbt.hAlign = HAlign::Center; sbt.vAlign = VAlign::Middle;
        cv.Text(L"✎", ren, sbt, pal.ink500);
        cv.Text(L"✕", del, sbt, pal.vermilion);

        if ((int)i == m_newHighlight && m_newT < 0.4f) {
            cv.PopTransform();
            cv.PopOpacity();
        }
    }

    // ＋ 新建题盒
    {
        int c = (int)st.boxes.size() % 2, rr = (int)st.boxes.size() / 2;
        float bx = x0 + c * (gw + gap), by = gy + rr * (gh + gap);
        m_newBoxRect = { bx, by, bx + gw, by + gh };
        cv.FillRoundRect(m_newBoxRect, shape::kEdge, WithAlpha(pal.jade, 0.07f));
        cv.StrokeRoundRect(m_newBoxRect, shape::kEdge, WithAlpha(pal.jade, 0.7f), shape::kHair);
        TextStyle nt; nt.size = 15.0f; nt.role = FontRole::Sans;
        nt.hAlign = HAlign::Center; nt.vAlign = VAlign::Middle; nt.letterSpacing = 2.0f;
        D2D1_RECT_F half{ bx, by, bx + gw, by + gh * 0.5f };
        cv.Text(L"＋ 新建题盒", half, nt, pal.jade);
        TextStyle st2; st2.size = 11.0f; st2.role = FontRole::Sans;
        st2.hAlign = HAlign::Center; st2.vAlign = VAlign::Middle;
        D2D1_RECT_F half2{ bx, by + gh * 0.5f, bx + gw, by + gh };
        cv.Text(L"正常题盒 / 错题盒", half2, st2, pal.ink300);
    }

    cv.PopTransform();
    cv.PopOpacity();
}

// ============================================================
//  盒内（V_BOX）：卡片列表 + 抽一张 + 添加卡片（弹窗）
// ============================================================
void QuizBoxView::DrawBox(Canvas& cv, float s)
{
    if (m_curSet < 0 || m_curSet >= (int)m_sets.size()) { m_view = V_SETS; return; }
    const auto& st = m_sets[m_curSet];
    if (m_curBox < 0 || m_curBox >= (int)st.boxes.size()) { m_view = V_BOXES; return; }
    const auto& pal = cv.Pal();
    const auto& b = st.boxes[m_curBox];
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_area.top + 30.0f;

    float va = ViewAlpha(m_viewT);
    float slide = (1.0f - ease::OutCubic(va)) * 12.0f;
    cv.PushOpacity(va);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, slide));

    // 顶部：返回 / 盒名（✎ 改名）/ 抽一张
    m_backRect = { x0, y0, x0 + 150.0f, y0 + 38.0f };
    cv.FillRoundRect(m_backRect, 6.0f, pal.paperLo);
    cv.StrokeRoundRect(m_backRect, 6.0f, pal.rule, shape::kHair);
    TextStyle bt; bt.size = 12.5f; bt.role = FontRole::Sans;
    bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle;
    cv.Text(L"📦 盒架", m_backRect, bt, pal.ink700);

    m_drawBtn = { x0 + contentW - 150.0f, y0, x0 + contentW, y0 + 38.0f };
    cv.FillRoundRect(m_drawBtn, 6.0f, pal.seal);
    cv.Text(L"🎯 抽一张", m_drawBtn, bt, pal.paperHi);

    bool wrong = (b.kind == 1);
    D2D1_RECT_F nameR{ x0 + 170.0f, y0 + 2.0f, x0 + contentW - 170.0f, y0 + 36.0f };
    if (m_renActive && m_renBox == m_curBox) {
        TextStyle nst; nst.role = FontRole::Serif; nst.size = 21.0f;
        nst.weight = DWRITE_FONT_WEIGHT_BOLD; nst.vAlign = VAlign::Middle;
        m_ren.Paint(cv, nameR, nst, pal.ink900, L"", pal.ink300, 0.0f, 0.0f);
    } else {
        TextStyle nst; nst.role = FontRole::Serif; nst.size = 23.0f;
        nst.weight = DWRITE_FONT_WEIGHT_BOLD; nst.letterSpacing = 1.0f; nst.vAlign = VAlign::Middle;
        cv.Text(b.name + (wrong ? L"（错题盒）" : L""), nameR, nst, pal.ink900);
    }
    // 改名提示与骑缝线之间留足空白（T1：不再重叠）
    cv.PerforationH(x0, x0 + contentW, y0 + 62.0f, WithAlpha(pal.ruleStrong, 0.5f));

    // ---- 卡片列表（hover 浮起 T2；难度边框 T4 基础版）----
    float ly = y0 + 84.0f;
    m_cardRects.clear(); m_cardDelRects.clear();
    if (b.cards.empty()) {
        // T1 修复：空态提示独占一行，不再与添加按钮重叠
        TextStyle et; et.size = 13.0f; et.role = FontRole::Sans; et.vAlign = VAlign::Middle;
        cv.Text(L"盒是空的——点下方「＋ 添加卡片」收第一张题卡，或拖入 md/csv 题库文件（下版本支持）。",
                { x0, ly, x0 + contentW, ly + 36.0f }, et, pal.ink500);
        ly += 52.0f;
    } else {
        for (size_t i = 0; i < b.cards.size(); ++i) {
            const auto& cd = b.cards[i];
            bool hot = (m_hoverCard == (int)i);
            float rowY = ly + (hot ? -2.0f : 0.0f);   // hover 浮起 2px
            D2D1_RECT_F row{ x0, rowY, x0 + contentW, rowY + 52.0f };
            m_cardRects.push_back({ x0, ly, x0 + contentW, ly + 52.0f });
            // 难度边框色（T4 临时方案：1..5 黛青→竹青→赭黄→朱砂→正红渐进）
            auto diffColor = [&](int d) -> D2D1_COLOR_F {
                switch (d) {
                case 1: return pal.jade;
                case 2: return WithAlpha(pal.jade, 1.0f);
                case 3: return pal.brass;
                case 4: return pal.seal;
                default: return pal.vermilion;
                }
            };
            D2D1_COLOR_F dc2 = diffColor(cd.difficulty);
            cv.PaperCard(row, hot ? 2.5f : 1.5f);
            cv.FillRect({ row.left, row.top, row.left + 3.0f, row.bottom }, WithAlpha(dc2, 0.85f));
            cv.StrokeRoundRect(row, shape::kEdge, WithAlpha(dc2, hot ? 1.0f : 0.45f), shape::kHair);
            TextStyle ft; ft.size = 13.5f; ft.role = FontRole::Sans; ft.vAlign = VAlign::Middle;
            ft.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            std::wstring front = cd.front.empty() ? L"（无题面）" : cd.front;
            if (front.size() > 36) front = front.substr(0, 36) + L"…";
            cv.Text(front, { row.left + 14.0f, row.top, row.right - 210.0f, row.bottom }, ft, pal.ink900);
            // 难度方块（1..5 小方块角标）+ 标签
            float dx = row.right - 196.0f;
            for (int d = 0; d < 5; ++d) {
                cv.FillRect({ dx + d * 9.0f, row.top + 20.0f, dx + d * 9.0f + 6.0f, row.top + 30.0f },
                            d < cd.difficulty ? WithAlpha(dc2, 0.95f) : WithAlpha(pal.rule, 0.5f));
            }
            TextStyle tt; tt.size = 11.0f; tt.role = FontRole::Mono; tt.vAlign = VAlign::Middle;
            tt.hAlign = HAlign::Right;
            cv.Text(cd.tag.empty() ? L"" : (L"#" + cd.tag),
                    { row.right - 190.0f, row.top, row.right - 126.0f, row.bottom }, tt, pal.jade);
            D2D1_RECT_F del{ row.right - 116.0f, row.top + 11.0f, row.right - 88.0f, row.bottom - 11.0f };
            m_cardDelRects.push_back(del);
            TextStyle dt; dt.size = 12.0f; dt.hAlign = HAlign::Center; dt.vAlign = VAlign::Middle;
            cv.Text(L"✕", del, dt, pal.vermilion);
            ly += 60.0f;
        }
    }

    // ---- ＋ 添加卡片（打开制卡弹窗）----
    ly += 10.0f;
    m_addBtn = { x0, ly, x0 + contentW, ly + 46.0f };
    cv.FillRoundRect(m_addBtn, shape::kEdge, WithAlpha(pal.seal, 0.08f));
    cv.StrokeRoundRect(m_addBtn, shape::kEdge, WithAlpha(pal.seal, 0.8f), shape::kHair);
    TextStyle at; at.size = 13.5f; at.role = FontRole::Sans;
    at.hAlign = HAlign::Center; at.vAlign = VAlign::Middle; at.letterSpacing = 1.0f;
    cv.Text(L"＋ 添加卡片（题面 / 答案 / 标签 / 难度）", m_addBtn, at, pal.seal);

    cv.PopTransform();
    cv.PopOpacity();
}

// ============================================================
//  抽卡（V_DRAW）
// ============================================================
void QuizBoxView::DrawDraw(Canvas& cv, float s)
{
    if (m_curSet < 0 || m_curSet >= (int)m_sets.size()) { m_view = V_SETS; return; }
    const auto& st = m_sets[m_curSet];
    if (m_curBox < 0 || m_curBox >= (int)st.boxes.size()) { m_view = V_BOXES; return; }
    const auto& b = st.boxes[m_curBox];
    if (m_drawIdx < 0 || m_drawIdx >= (int)b.cards.size()) { m_view = V_BOX; return; }
    const auto& cd = b.cards[m_drawIdx];
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_area.top + 30.0f;

    float va = ViewAlpha(m_viewT);
    float slide = (1.0f - ease::OutCubic(va)) * 16.0f;
    cv.PushOpacity(va);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, slide));

    m_backDrawRect = { x0, y0, x0 + 120.0f, y0 + 38.0f };
    cv.FillRoundRect(m_backDrawRect, 6.0f, pal.paperLo);
    cv.StrokeRoundRect(m_backDrawRect, 6.0f, pal.rule, shape::kHair);
    TextStyle bt; bt.size = 12.5f; bt.role = FontRole::Sans;
    bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle;
    cv.Text(L"◀ 放回", m_backDrawRect, bt, pal.ink700);
    TextStyle hs; hs.role = FontRole::Mono; hs.size = 11.0f; hs.letterSpacing = 2.0f;
    hs.weight = DWRITE_FONT_WEIGHT_BOLD; hs.vAlign = VAlign::Middle; hs.hAlign = HAlign::Center;
    cv.Text(m_flip ? L"回 顾 · 答 案" : L"回 顾 · 题 面",
            { x0, y0, x0 + contentW, y0 + 38.0f }, hs, pal.seal);

    float cy0 = y0 + 70.0f, ch = 300.0f;
    D2D1_RECT_F card{ x0, cy0, x0 + contentW, cy0 + ch };
    cv.PaperCard(card, 2.0f);
    cv.DoubleFrame(card, WithAlpha(pal.seal, m_flip ? 0.5f : 0.8f));
    if (!cd.tag.empty()) {
        D2D1_RECT_F badge{ card.right - 130.0f, card.top + 16.0f, card.right - 26.0f, card.top + 42.0f };
        cv.FillRoundRect(badge, 11.0f, WithAlpha(pal.jade, 0.14f));
        TextStyle bgt; bgt.size = 11.0f; bgt.role = FontRole::Mono;
        bgt.hAlign = HAlign::Center; bgt.vAlign = VAlign::Middle;
        cv.Text(L"#" + cd.tag, badge, bgt, pal.jade);
    }
    // 难度方块
    for (int d = 0; d < 5; ++d) {
        cv.FillRect({ card.left + 26.0f + d * 12.0f, card.top + 26.0f,
                      card.left + 26.0f + d * 12.0f + 8.0f, card.top + 36.0f },
                    d < cd.difficulty ? pal.seal : WithAlpha(pal.rule, 0.5f));
    }
    float tx0 = card.left + 34.0f, tx1 = card.right - 34.0f;
    if (!m_flip) {
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 11.0f;
        cv.Text(L"题 面", { tx0, card.top + 52.0f, tx1, card.top + 74.0f }, lb, pal.ink300);
        TextStyle ft; ft.role = FontRole::Serif; ft.size = 20.0f;
        ft.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ft.vAlign = VAlign::Middle;
        cv.Text(cd.front.empty() ? L"（无题面）" : cd.front,
                { tx0, card.top + 78.0f, tx1, card.bottom - 30.0f }, ft, pal.ink900);
    } else {
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 11.0f;
        cv.Text(L"答 案", { tx0, card.top + 52.0f, tx1, card.top + 74.0f }, lb, pal.seal);
        TextStyle ft; ft.role = FontRole::Serif; ft.size = 19.0f;
        ft.vAlign = VAlign::Middle;
        cv.Text(cd.back.empty() ? L"（未填写答案）" : cd.back,
                { tx0, card.top + 78.0f, tx1, card.bottom - 30.0f }, ft, pal.ink900);
    }

    float by = cy0 + ch + 26.0f;
    m_flipRect = { x0 + contentW * 0.5f - 240.0f, by, x0 + contentW * 0.5f - 80.0f, by + 48.0f };
    m_nextRect = { x0 + contentW * 0.5f - 60.0f, by, x0 + contentW * 0.5f + 100.0f, by + 48.0f };
    auto BigBtn = [&](const D2D1_RECT_F& r, const wchar_t* label, bool primary) {
        cv.FillRoundRect(r, 8.0f, primary ? pal.seal : pal.paperLo);
        cv.StrokeRoundRect(r, 8.0f, primary ? pal.seal : pal.rule, shape::kHair);
        TextStyle bs; bs.size = 14.0f; bs.role = FontRole::Sans;
        bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle; bs.letterSpacing = 1.0f;
        cv.Text(label, r, bs, primary ? pal.paperHi : pal.ink700);
    };
    BigBtn(m_flipRect, m_flip ? L"◀ 看题面" : L"翻面看答案", !m_flip);
    if (m_flip) BigBtn(m_nextRect, L"下一张 ▶", true);

    cv.PopTransform();
    cv.PopOpacity();
}

// ============================================================
//  T3 制卡弹窗（居中卡片窗：题面/答案/标签/难度）
// ============================================================
void QuizBoxView::OpenCardEditor(const QCard* edit)
{
    m_ceOpen = true;
    m_ceField = 0;
    if (edit) {
        m_ceEditId = edit->id;
        m_ceFront = edit->front; m_ceBack = edit->back; m_ceTag = edit->tag;
        m_ceDiff = edit->difficulty;
    } else {
        m_ceEditId.clear();
        m_ceFront.clear(); m_ceBack.clear(); m_ceTag.clear();
        m_ceDiff = 3;
    }
    m_viewT = 0.0f;   // 弹窗弹入动画
    m_edit.Begin(m_ceFront, false, 14.0f);
    m_edit.onEnter     = [this] { CeNext(); };
    m_edit.onEsc       = [this] { CeCancel(); };
    m_edit.onKillFocus = [this] { /* 留在弹窗内，不提交 */ };
}

void QuizBoxView::CeCancel()
{
    m_edit.Cancel();
    m_ceOpen = false;
}

void QuizBoxView::CeNext()
{
    // 当前字段收值 → 推进到下一字段；最后字段回车 = 保存
    std::wstring t;
    m_edit.End(true, t);
    if (m_ceField == 0) m_ceFront = t;
    else if (m_ceField == 1) m_ceBack = t;
    else m_ceTag = t;
    if (m_ceField < 2) {
        ++m_ceField;
        std::wstring cur = m_ceField == 1 ? m_ceBack : m_ceTag;
        m_edit.Begin(cur, false, 14.0f);
        m_edit.onEnter     = [this] { CeNext(); };
        m_edit.onEsc       = [this] { CeCancel(); };
        m_edit.onKillFocus = [this] {};
    } else {
        CeCommit(false);
    }
}

void QuizBoxView::CeCommit(bool keepOpen)
{
    std::wstring t;
    m_edit.End(true, t);
    if (m_ceField == 0) m_ceFront = t;
    else if (m_ceField == 1) m_ceBack = t;
    else m_ceTag = t;

    if (m_curSet >= 0 && m_curSet < (int)m_sets.size()) {
        const auto& st = m_sets[m_curSet];
        if (m_curBox >= 0 && m_curBox < (int)st.boxes.size()) {
            const auto& b = st.boxes[m_curBox];
            QCard c;
            c.id = m_ceEditId.empty() ? (L"qc_" + std::to_wstring(GetTickCount64())) : m_ceEditId;
            c.front = m_ceFront; c.back = m_ceBack; c.tag = m_ceTag;
            c.difficulty = m_ceDiff;
            c.added = (long long)time(nullptr);
            if (m_ceEditId.empty()) {
                BoxStore::Instance().AddCard(b.id, c);
                Toast(keepOpen ? L"已收进盒中，继续制卡" : L"已收进盒中");
            } else {
                BoxStore::Instance().UpdateCard(b.id, c);
                Toast(L"已保存修改");
            }
            m_sets = BoxStore::Instance().Load();
        }
    }
    if (keepOpen) {
        // 保存并继续：清空进入下一张
        m_ceEditId.clear();
        m_ceFront.clear(); m_ceBack.clear(); m_ceTag.clear();
        m_ceDiff = 3;
        m_ceField = 0;
        m_edit.Begin(L"", false, 14.0f);
        m_edit.onEnter     = [this] { CeNext(); };
        m_edit.onEsc       = [this] { CeCancel(); };
        m_edit.onKillFocus = [this] {};
    } else {
        m_ceOpen = false;
    }
}

void QuizBoxView::DrawCardEditor(Canvas& cv, float s)
{
    const auto& pal = cv.Pal();
    float W = m_area.right - m_area.left;
    float H = m_area.bottom - m_area.top;

    float a = Clamp01(m_viewT / 0.22f);
    float e = ease::OutBack(a);
    float slide = (1.0f - e) * 18.0f;

    cv.FillRect(m_area, WithAlpha(pal.ink900, 0.5f * a));

    float cw = (std::min)(560.0f, W - 80.0f);
    float chh = 470.0f;
    float px = (W - cw) * 0.5f;
    float py = (H - chh) * 0.5f + slide;

    cv.PushOpacity(a);
    m_ceCard = { px, py, px + cw, py + chh };
    cv.PaperCard(m_ceCard, 0.4f, shape::kEdge);
    cv.DoubleFrame(m_ceCard, WithAlpha(pal.seal, 0.8f));

    TextStyle ttl; ttl.role = FontRole::Serif; ttl.size = 20.0f;
    ttl.weight = DWRITE_FONT_WEIGHT_BOLD; ttl.letterSpacing = 1.4f;
    cv.Text(m_ceEditId.empty() ? L"制 卡" : L"改 卡",
            { m_ceCard.left + 26.0f, m_ceCard.top + 18.0f,
              m_ceCard.right - 140.0f, m_ceCard.top + 48.0f }, ttl, pal.ink900);
    TextStyle hs; hs.role = FontRole::Mono; hs.size = 10.0f; hs.letterSpacing = 1.6f;
    hs.hAlign = HAlign::Right; hs.vAlign = VAlign::Middle;
    cv.Text(L"ENTER 下一项 · ESC 取消",
            { m_ceCard.right - 220.0f, m_ceCard.top + 20.0f,
              m_ceCard.right - 26.0f, m_ceCard.top + 46.0f }, hs, pal.ink300);
    cv.PerforationH(m_ceCard.left + 26.0f, m_ceCard.right - 26.0f,
                     m_ceCard.top + 62.0f, WithAlpha(pal.ruleStrong, 0.5f));

    float ix = m_ceCard.left + 30.0f, ir = m_ceCard.right - 30.0f;
    auto Field = [&](D2D1_RECT_F& box, const wchar_t* label, const std::wstring& val,
                     const wchar_t* hint, int idx) {
        box = { ix, 0, ir, 0 };
        box.top = m_ceCard.top + 76.0f + idx * 78.0f;
        box.bottom = box.top + 52.0f;
        bool editing = (m_ceField == idx);
        cv.FillRoundRect(box, shape::kEdge, editing ? pal.paperHi : pal.paperLo);
        cv.StrokeRoundRect(box, shape::kEdge, WithAlpha(editing ? pal.seal : pal.rule,
                          editing ? 0.95f : 0.5f), editing ? shape::kStroke : shape::kHair);
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 10.5f; lb.letterSpacing = 2.0f;
        lb.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(label, { ix, box.top - 20.0f, ir, box.top - 4.0f }, lb,
                editing ? pal.seal : pal.ink500);
        if (editing) {
            TextStyle ft; ft.size = 14.0f; ft.role = FontRole::Sans; ft.vAlign = VAlign::Middle;
            m_edit.Paint(cv, { box.left + 12.0f, box.top, box.right - 12.0f, box.bottom },
                         ft, pal.ink900, hint, pal.ink300, 0.0f, 0.0f);
        } else {
            TextStyle ft; ft.size = 13.5f; ft.role = FontRole::Sans; ft.vAlign = VAlign::Middle;
            if (val.empty()) cv.Text(hint, { box.left + 12.0f, box.top, box.right - 12.0f, box.bottom },
                                     ft, pal.ink300);
            else cv.Text(val, { box.left + 12.0f, box.top, box.right - 12.0f, box.bottom }, ft, pal.ink900);
        }
    };
    Field(m_ceFrontR, L"题 面", m_ceFront, L"点击输入题面…", 0);
    Field(m_ceBackR,  L"答 案", m_ceBack, L"点击输入答案 / 解析…", 1);
    Field(m_ceTagR,   L"标 签", m_ceTag, L"点击输入标签（可留空）…", 2);

    // 难度 1..5 方块选择
    {
        float dy = m_ceCard.top + 76.0f + 3 * 78.0f;
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 10.5f; lb.letterSpacing = 2.0f;
        lb.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"难 度", { ix, dy - 20.0f, ir, dy - 4.0f }, lb, pal.ink500);
        auto diffColor = [&](int d) -> D2D1_COLOR_F {
            switch (d) {
            case 1: return pal.jade;
            case 2: return pal.jade;
            case 3: return pal.brass;
            case 4: return pal.seal;
            default: return pal.vermilion;
            }
        };
        for (int d = 0; d < 5; ++d) {
            m_ceDiffR[d] = { ix + d * 62.0f, dy, ix + d * 62.0f + 54.0f, dy + 40.0f };
            bool on = (m_ceDiff == d + 1);
            cv.FillRoundRect(m_ceDiffR[d], 6.0f, on ? WithAlpha(diffColor(d + 1), 0.9f)
                                                    : WithAlpha(pal.rule, 0.12f));
            cv.StrokeRoundRect(m_ceDiffR[d], 6.0f, on ? diffColor(d + 1) : pal.rule, shape::kHair);
            TextStyle dt2; dt2.size = 13.0f; dt2.role = FontRole::Mono;
            dt2.hAlign = HAlign::Center; dt2.vAlign = VAlign::Middle;
            wchar_t db[8]; swprintf_s(db, L"D%d", d + 1);
            cv.Text(db, m_ceDiffR[d], dt2, on ? pal.paperHi : pal.ink500);
        }
    }

    // 底部按钮
    float by = m_ceCard.bottom - 62.0f;
    m_ceCancelR   = { m_ceCard.left + 26.0f, by, m_ceCard.left + 26.0f + 96.0f, by + 42.0f };
    m_ceSaveMoreR = { m_ceCard.right - 26.0f - 232.0f, by, m_ceCard.right - 26.0f - 120.0f, by + 42.0f };
    m_ceSaveR     = { m_ceCard.right - 26.0f - 106.0f, by, m_ceCard.right - 26.0f, by + 42.0f };
    auto Btn = [&](const D2D1_RECT_F& r, const wchar_t* label, int style) {
        // 0=次 1=主 2=描边
        if (style == 1) cv.FillRoundRect(r, 6.0f, pal.seal);
        else if (style == 2) cv.FillRoundRect(r, 6.0f, pal.paperHi);
        else cv.FillRoundRect(r, 6.0f, pal.paperLo);
        cv.StrokeRoundRect(r, 6.0f, style == 1 ? pal.seal : pal.rule, shape::kHair);
        TextStyle bts; bts.size = 13.0f; bts.role = FontRole::Sans;
        bts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bts.hAlign = HAlign::Center; bts.vAlign = VAlign::Middle; bts.letterSpacing = 1.0f;
        cv.Text(label, r, bts, style == 1 ? pal.paperHi : pal.ink700);
    };
    Btn(m_ceCancelR,   L"取消", 0);
    Btn(m_ceSaveMoreR, L"保存并继续", 2);
    Btn(m_ceSaveR,      L"保存", 1);

    cv.PopOpacity();
}

// ============================================================
//  更新（输入分发）
// ============================================================
void QuizBoxView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    m_viewT += dt;
    m_newT += dt;
    if (m_toastT > 0.0f) m_toastT = (std::max)(0.0f, m_toastT - dt);

    float s = ScrollY();
    float mx = in.mouseX, my = in.mouseY + s;

    // ---- 制卡弹窗独占 ----
    if (m_ceOpen) {
        if (m_cv) {
            TextStyle ft; ft.size = 14.0f; ft.role = FontRole::Sans; ft.vAlign = VAlign::Middle;
            const D2D1_RECT_F& box = m_ceField == 0 ? m_ceFrontR :
                                     m_ceField == 1 ? m_ceBackR : m_ceTagR;
            m_edit.HandleMouse(in, *m_cv, { box.left + 12.0f, box.top, box.right - 12.0f, box.bottom },
                               ft, ScrollY(), 0.0f);
        }
        if (in.clicked) {
            // 难度按钮
            for (int d = 0; d < 5; ++d)
                if (InRect(m_ceDiffR[d], mx, my)) { m_ceDiff = d + 1; return; }
            // 字段切换
            if (InRect(m_ceFrontR, mx, my) && m_ceField != 0) { CeSwitchField(0); return; }
            if (InRect(m_ceBackR, mx, my) && m_ceField != 1) { CeSwitchField(1); return; }
            if (InRect(m_ceTagR, mx, my) && m_ceField != 2) { CeSwitchField(2); return; }
            // 底部按钮
            if (InRect(m_ceCancelR, mx, my)) { CeCancel(); return; }
            if (InRect(m_ceSaveR, mx, my)) { CeCommit(false); return; }
            if (InRect(m_ceSaveMoreR, mx, my)) { CeCommit(true); return; }
            // 点弹窗外 = 取消
            if (!InRect(m_ceCard, mx, my)) { CeCancel(); return; }
        }
        return;
    }

    // ---- 改名独占 ----
    if (m_renActive) {
        if (m_cv && m_renBox2.right > m_renBox2.left) {
            TextStyle ns; ns.role = FontRole::Serif; ns.size = 21.0f;
            ns.weight = DWRITE_FONT_WEIGHT_BOLD; ns.vAlign = VAlign::Middle;
            m_ren.HandleMouse(in, *m_cv, m_renBox2, ns, ScrollY(), 0.0f);
            if (in.clicked && !InRect(m_renBox2, mx, my)) {
                std::wstring t;
                m_ren.End(true, t);
                m_renActive = false;
                if (!t.empty()) {
                    if (m_renBox >= 0 && m_curSet >= 0 && m_curSet < (int)m_sets.size()) {
                        auto& st = m_sets[m_curSet];
                        if (m_renBox < (int)st.boxes.size()) {
                            st.boxes[m_renBox].name = t;
                            BoxStore::Instance().RenameBox(st.boxes[m_renBox].id, t);
                            Toast(L"已改名");
                        }
                    }
                }
            }
        }
        if (in.keyDown[VK_ESCAPE]) { m_ren.Cancel(); m_renActive = false; }
        return;
    }

    if (in.keyDown[VK_ESCAPE]) {
        if (m_view == V_DRAW) { m_view = V_BOX; m_viewT = 0.0f; }
        else if (m_view == V_BOX) { m_view = V_BOXES; m_viewT = 0.0f; }
        else if (m_view == V_BOXES) { m_view = V_SETS; m_viewT = 0.0f; }
    }

    // hover（盒内卡片浮起）
    m_hoverCard = -1;
    if (m_view == V_BOX) {
        for (size_t i = 0; i < m_cardRects.size(); ++i)
            if (InRect(m_cardRects[i], mx, my)) { m_hoverCard = (int)i; m_overInteractive = true; break; }
    }

    if (!in.clicked) return;

    if (m_view == V_SETS) {
        for (size_t i = 0; i < m_setRects.size(); ++i) {
            if (InRect(m_setRects[i], mx, my)) {
                if (InRect(m_setDelRects[i], mx, my)) {
                    BoxStore::Instance().DeleteSet(m_sets[i].id);
                    m_sets = BoxStore::Instance().Load();
                    Toast(L"已删除题集");
                    return;
                }
                m_curSet = (int)i;
                m_view = V_BOXES; m_viewT = 0.0f;
                return;
            }
        }
        if (InRect(m_newSetRect, mx, my)) {
            int n = (int)m_sets.size();
            wchar_t nb[40];
            swprintf_s(nb, L"新题集 %d", n + 1);
            BoxStore::Instance().AddSet(nb);
            m_sets = BoxStore::Instance().Load();
            m_newHighlight = (int)m_sets.size() - 1;
            m_newT = 0.0f;
            Toast(L"已创建题集，点击名字区域可改名（下版本）");
            return;
        }
    } else if (m_view == V_BOXES) {
        if (m_curSet < 0 || m_curSet >= (int)m_sets.size()) { m_view = V_SETS; return; }
        const auto& st = m_sets[m_curSet];
        if (InRect(m_backRect, mx, my)) { m_view = V_SETS; m_viewT = 0.0f; return; }
        for (size_t i = 0; i < m_boxRects.size(); ++i) {
            if (InRect(m_boxRects[i], mx, my)) {
                if (InRect(m_boxDelRects[i], mx, my)) {
                    BoxStore::Instance().DeleteBox(st.boxes[i].id);
                    m_sets = BoxStore::Instance().Load();
                    Toast(L"已删除题盒");
                    return;
                }
                if (InRect(m_boxRenRects[i], mx, my)) {
                    m_curBox = (int)i;
                    m_renBox = m_curBox; m_renSet = m_curSet;
                    m_ren.Begin(st.boxes[i].name, false, 21.0f);
                    m_renActive = true;
                    m_view = V_BOX; m_viewT = 0.0f;
                    return;
                }
                m_curBox = (int)i;
                m_view = V_BOX; m_viewT = 0.0f;
                return;
            }
        }
        if (InRect(m_newBoxRect, mx, my)) {
            int wrongCount = 0;
            for (auto& b : st.boxes) if (b.kind == 1) ++wrongCount;
            int kind = (wrongCount == 0 && !st.boxes.empty() && st.boxes.back().kind == 0) ? 1 : 0;
            wchar_t nb[40];
            swprintf_s(nb, L"新题盒 %d", (int)st.boxes.size() + 1);
            BoxStore::Instance().AddBox(st.id, nb, kind);
            m_sets = BoxStore::Instance().Load();
            m_newHighlight = (int)m_sets[m_curSet].boxes.size() - 1;
            m_newT = 0.0f;
            Toast(kind == 1 ? L"已创建错题盒，点击盒名可改名" : L"已创建题盒，点击盒名可改名");
            return;
        }
    } else if (m_view == V_BOX) {
        if (m_curSet < 0 || m_curSet >= (int)m_sets.size()) { m_view = V_SETS; return; }
        const auto& st = m_sets[m_curSet];
        if (m_curBox < 0 || m_curBox >= (int)st.boxes.size()) { m_view = V_BOXES; return; }
        const auto& b = st.boxes[m_curBox];
        if (InRect(m_backRect, mx, my)) { m_view = V_BOXES; m_viewT = 0.0f; return; }
        if (InRect(m_drawBtn, mx, my)) {
            if (b.cards.empty()) { Toast(L"盒是空的，先加几张卡"); return; }
            m_drawIdx = rand() % (int)b.cards.size();
            m_flip = false;
            m_view = V_DRAW; m_viewT = 0.0f;
            return;
        }
        if (InRect(m_addBtn, mx, my)) { OpenCardEditor(nullptr); return; }
        for (size_t i = 0; i < m_cardDelRects.size(); ++i) {
            if (InRect(m_cardDelRects[i], mx, my)) {
                if (i < b.cards.size()) {
                    BoxStore::Instance().DeleteCard(b.id, b.cards[i].id);
                    m_sets = BoxStore::Instance().Load();
                    Toast(L"已删除卡片");
                }
                return;
            }
        }
        // 点卡片 → 改卡（制卡窗回填）
        for (size_t i = 0; i < m_cardRects.size(); ++i) {
            if (InRect(m_cardRects[i], mx, my) && i < b.cards.size()) {
                OpenCardEditor(&b.cards[i]);
                return;
            }
        }
        // 点盒名 → 就地改名
        float availW = m_area.right - m_area.left;
        float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
        float x0 = m_area.left + (availW - contentW) * 0.5f;
        m_renBox2 = { x0 + 170.0f, m_area.top + 32.0f, x0 + contentW - 170.0f, m_area.top + 66.0f };
        if (InRect(m_renBox2, mx, my)) {
            m_renBox = m_curBox; m_renSet = m_curSet;
            m_ren.Begin(b.name, false, 21.0f);
            m_renActive = true;
            return;
        }
    } else if (m_view == V_DRAW) {
        if (InRect(m_backDrawRect, mx, my)) { m_view = V_BOX; m_viewT = 0.0f; return; }
        if (InRect(m_flipRect, mx, my)) { m_flip = !m_flip; return; }
        if (m_flip && InRect(m_nextRect, mx, my)) {
            const auto& b = m_sets[m_curSet].boxes[m_curBox];
            if (!b.cards.empty()) { m_drawIdx = rand() % (int)b.cards.size(); m_flip = false; }
            return;
        }
    }
}

void QuizBoxView::CeSwitchField(int idx)
{
    std::wstring t;
    m_edit.End(true, t);
    if (m_ceField == 0) m_ceFront = t;
    else if (m_ceField == 1) m_ceBack = t;
    else m_ceTag = t;
    m_ceField = idx;
    std::wstring cur = idx == 0 ? m_ceFront : idx == 1 ? m_ceBack : m_ceTag;
    m_edit.Begin(cur, false, 14.0f);
    m_edit.onEnter     = [this] { CeNext(); };
    m_edit.onEsc       = [this] { CeCancel(); };
    m_edit.onKillFocus = [this] {};
}

// ============================================================
//  绘制入口
// ============================================================
void QuizBoxView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float s = ScrollY();
    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    if (m_view == V_SETS)       DrawSets(cv, s);
    else if (m_view == V_BOXES) DrawBoxes(cv, s);
    else if (m_view == V_BOX)   DrawBox(cv, s);
    else                        DrawDraw(cv, s);

    cv.PopTransform();
    cv.PopClip();

    // T3 制卡弹窗（最上层）
    if (m_ceOpen) DrawCardEditor(cv, s);

    // Toast
    if (m_toastT > 0.0f) {
        float a = Clamp01(m_toastT / 0.5f);
        cv.PushOpacity(a);
        float w = 260.0f, h = 40.0f;
        D2D1_RECT_F r{ m_area.right - w - 28.0f, m_area.bottom - h - 28.0f,
                       m_area.right - 28.0f, m_area.bottom - 28.0f };
        cv.FillRoundRect(r, 8.0f, pal.seal);
        TextStyle ts; ts.size = 12.5f; ts.role = FontRole::Sans;
        ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
        cv.Text(m_toast, r, ts, pal.paperHi);
        cv.PopOpacity();
    }
}

// ============================================================
//  截图自检：造示例题集（房子→盒→卡 三层）
// ============================================================
void QuizBoxView::DebugForcePreview()
{
    // 截图自检：空库造示例题集（房子→盒→卡 三层）
    if (m_sets.empty()) {
        BoxStore::Instance().AddSet(L"考公");
        m_sets = BoxStore::Instance().Load();
        if (!m_sets.empty()) {
            BoxStore::Instance().AddBox(m_sets.front().id, L"资料分析题盒", 0);
            BoxStore::Instance().AddBox(m_sets.front().id, L"错题盒", 1);
            m_sets = BoxStore::Instance().Load();
            auto& st = m_sets.front();
            if (!st.boxes.empty()) {
                QCard c1; c1.id = L"qc_d1"; c1.front = L"甲乙两车相向而行，速度和 120km/h，300km 几小时相遇？";
                c1.back = L"300 ÷ 120 = 2.5 小时。"; c1.tag = L"行程问题";
                c1.difficulty = 2; c1.added = (long long)time(nullptr);
                QCard c2; c2.id = L"qc_d2"; c2.front = L"增长率比较：甲 8%、乙 12%，谁增速快？";
                c2.back = L"乙（12% > 8%）。"; c2.tag = L"资料分析";
                c2.difficulty = 4; c2.added = (long long)time(nullptr);
                BoxStore::Instance().AddCard(st.boxes.front().id, c1);
                BoxStore::Instance().AddCard(st.boxes.front().id, c2);
            }
        }
        m_sets = BoxStore::Instance().Load();
    }
    // --edit：进盒内 + 打开制卡弹窗（一张图覆盖盒内卡片+制卡弹窗两层）
    m_view = V_BOX;
    m_viewT = 1.0f;
    m_curSet = 0;
    m_curBox = 0;
    if (!m_sets.empty() && !m_sets[0].boxes.empty() && !m_sets[0].boxes[0].cards.empty()) {
        m_drawIdx = 0; m_flip = true;   // 顺带把翻面也截了
    }
    // 打开制卡弹窗（题面预填示例，字段在题面让用户看清弹窗结构）
    QCard demo; demo.id = L"qc_demo"; demo.front = L"示例题面：相邻两数的差是 7，和是 35，求这两数。";
    demo.back = L"35 ÷ 2 ± 7 ÷ 2 → 11 与 18。"; demo.tag = L"计算题";
    demo.difficulty = 3; demo.added = (long long)time(nullptr);
    OpenCardEditor(&demo);
}

void QuizBoxView::DebugForceOpen()
{
    // --edit 截图：进盒内 + 打开制卡弹窗（一图覆盖盒内卡片+制卡弹窗）
    if (m_sets.empty()) DebugForcePreview();
    if (!m_sets.empty()) {
        m_view = V_BOX; m_viewT = 1.0f;
        m_curSet = 0; m_curBox = 0;
        if (!m_sets[0].boxes.empty() && !m_sets[0].boxes[0].cards.empty()) {
            m_drawIdx = 0; m_flip = true;
        }
        QCard demo; demo.id = L"qc_demo";
        demo.front = L"示例题面：相邻两数的差是 7，和是 35，求这两数。";
        demo.back  = L"35 ÷ 2 ± 7 ÷ 2 → 11 与 18。"; demo.tag = L"计算题";
        demo.difficulty = 3; demo.added = (long long)time(nullptr);
        OpenCardEditor(&demo);
    }
}

} // namespace lj
