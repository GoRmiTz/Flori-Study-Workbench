// ============================================================
//  QuizBoxView.cpp — 题集卡片盒实现（批次 G，需求 8）
//  盒架 / 盒内 / 抽卡三态；纸基风；FieldEdit 就地编辑。
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

void QuizBoxView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_view = V_SHELF;
    m_curBox = -1;
    m_drawIdx = -1;
    m_flip = false;
    m_addOpen = false;
    m_renActive = false;
    Reload();
}

void QuizBoxView::Reload()
{
    m_boxes = BoxStore::Instance().Load();
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
    (void)cv;
}

// ============================================================
//  盒架
// ============================================================
void QuizBoxView::DrawShelf(Canvas& cv, float s)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_area.top + 30.0f;

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 题集 · 卡片盒", { x0, y0, x0 + 420.0f, y0 + 16.0f }, sec, pal.ink300);
    TextStyle h1; h1.role = FontRole::Serif; h1.size = 34.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"题 集 卡 片 盒", x0, y0 + 24.0f, h1, pal.ink900, m_t * 1.1f, 0.05f, 16.0f);
    TextStyle lead; lead.size = 12.0f; lead.role = FontRole::Sans;
    cv.Text(L"自定义题盒收题、错题归盒；抽卡回顾，点卡片进入盒内。",
            { x0, y0 + 70.0f, x0 + contentW, y0 + 90.0f }, lead, pal.ink500);
    cv.PerforationH(x0, x0 + contentW, y0 + 102.0f, WithAlpha(pal.ruleStrong, 0.5f));

    // 盒卡网格（两列）
    m_boxRects.clear(); m_boxRenRects.clear(); m_boxDelRects.clear();
    float gw = (contentW - 24.0f) / 2.0f, gh = 148.0f, gap = 24.0f;
    float gy = y0 + 122.0f;
    for (size_t i = 0; i < m_boxes.size(); ++i) {
        int c = (int)i % 2, r = (int)i / 2;
        float bx = x0 + c * (gw + gap), by = gy + r * (gh + gap);
        D2D1_RECT_F card{ bx, by, bx + gw, by + gh };
        m_boxRects.push_back(card);

        const auto& b = m_boxes[i];
        bool wrong = (b.kind == 1);
        cv.PaperCard(card, 2.0f);
        cv.StrokeRoundRect(card, shape::kEdge, WithAlpha(wrong ? pal.vermilion : pal.jade, 0.55f),
                           shape::kHair);
        // 类型徽章
        D2D1_RECT_F badge{ bx + 18.0f, by + 16.0f, bx + 90.0f, by + 40.0f };
        cv.FillRoundRect(badge, 10.0f, WithAlpha(wrong ? pal.vermilion : pal.jade, 0.16f));
        TextStyle bt; bt.size = 11.0f; bt.role = FontRole::Mono; bt.letterSpacing = 1.0f;
        bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle;
        cv.Text(wrong ? L"错题盒" : L"题盒", badge, bt, wrong ? pal.vermilion : pal.jade);
        // 盒名
        TextStyle nst; nst.role = FontRole::Serif; nst.size = 20.0f;
        nst.weight = DWRITE_FONT_WEIGHT_BOLD; nst.vAlign = VAlign::Middle;
        cv.Text(b.name, { bx + 18.0f, by + 48.0f, bx + gw - 18.0f, by + 80.0f }, nst, pal.ink900);
        // 卡数 + 底线
        TextStyle cst; cst.size = 11.5f; cst.role = FontRole::Mono; cst.vAlign = VAlign::Middle;
        wchar_t cb[32];
        swprintf_s(cb, L"%d 张卡片", (int)b.cards.size());
        cv.Text(cb, { bx + 18.0f, by + 92.0f, bx + gw - 18.0f, by + 116.0f }, cst, pal.ink500);
        cv.PerforationH(bx + 18.0f, bx + gw - 18.0f, by + gh - 18.0f, WithAlpha(pal.rule, 0.6f));
        // ✎ 改名 / ✕ 删除
        D2D1_RECT_F ren{ bx + gw - 78.0f, by + 16.0f, bx + gw - 50.0f, by + 40.0f };
        D2D1_RECT_F del{ bx + gw - 44.0f, by + 16.0f, bx + gw - 18.0f, by + 40.0f };
        m_boxRenRects.push_back(ren);
        m_boxDelRects.push_back(del);
        TextStyle sbt; sbt.size = 13.0f; sbt.hAlign = HAlign::Center; sbt.vAlign = VAlign::Middle;
        cv.Text(L"✎", ren, sbt, pal.ink500);
        cv.Text(L"✕", del, sbt, pal.vermilion);
    }

    // ＋ 新建题盒（虚线占位卡）
    {
        int c = (int)m_boxes.size() % 2, r = (int)m_boxes.size() / 2;
        float bx = x0 + c * (gw + gap), by = gy + r * (gh + gap);
        m_newBoxRect = { bx, by, bx + gw, by + gh };
        cv.FillRoundRect(m_newBoxRect, shape::kEdge, WithAlpha(pal.jade, 0.07f));
        // 虚线边框（用骑缝点线近似）
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

    // 提示
    if (m_boxes.empty()) {
        TextStyle et; et.size = 12.0f; et.role = FontRole::Sans;
        et.hAlign = HAlign::Center; et.vAlign = VAlign::Middle;
        cv.Text(L"建一个题盒开始收题——错题单独归盒，抽卡回顾更顺手。",
                { x0, gy + gh + 18.0f, x0 + contentW, gy + gh + 44.0f }, et, pal.ink500);
    }
}

// ============================================================
//  盒内
// ============================================================
void QuizBoxView::DrawBox(Canvas& cv, float s)
{
    (void)s;
    if (m_curBox < 0 || m_curBox >= (int)m_boxes.size()) { m_view = V_SHELF; return; }
    const auto& pal = cv.Pal();
    const auto& b = m_boxes[m_curBox];
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_area.top + 30.0f;

    // 顶部：返回 + 盒名（点击改名）+ 抽一张
    m_backRect = { x0, y0, x0 + 120.0f, y0 + 38.0f };
    cv.FillRoundRect(m_backRect, 6.0f, pal.paperLo);
    cv.StrokeRoundRect(m_backRect, 6.0f, pal.rule, shape::kHair);
    TextStyle bt; bt.size = 12.5f; bt.role = FontRole::Sans;
    bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle;
    cv.Text(L"◀ 盒架", m_backRect, bt, pal.ink700);

    m_drawBtn = { x0 + contentW - 150.0f, y0, x0 + contentW, y0 + 38.0f };
    cv.FillRoundRect(m_drawBtn, 6.0f, pal.seal);
    cv.Text(L"🎯 抽一张", m_drawBtn, bt, pal.paperHi);

    bool wrong = (b.kind == 1);
    if (m_renActive) {
        TextStyle nst; nst.role = FontRole::Serif; nst.size = 22.0f;
        nst.weight = DWRITE_FONT_WEIGHT_BOLD; nst.vAlign = VAlign::Middle;
        m_ren.Paint(cv, { x0 + 140.0f, y0 + 2.0f, x0 + contentW - 170.0f, y0 + 36.0f },
                    nst, pal.ink900, L"", pal.ink300, 0.0f, 0.0f);
    } else {
        TextStyle nst; nst.role = FontRole::Serif; nst.size = 24.0f;
        nst.weight = DWRITE_FONT_WEIGHT_BOLD; nst.letterSpacing = 1.0f; nst.vAlign = VAlign::Middle;
        cv.Text(b.name + (wrong ? L"（错题盒）" : L""),
                { x0 + 140.0f, y0 + 2.0f, x0 + contentW - 170.0f, y0 + 36.0f }, nst, pal.ink900);
        TextStyle hnt; hnt.role = FontRole::Mono; hnt.size = 9.5f;
        cv.Text(L"点击盒名可改名", { x0 + 140.0f, y0 + 38.0f, x0 + contentW - 170.0f, y0 + 52.0f },
                hnt, pal.ink300);
    }
    cv.PerforationH(x0, x0 + contentW, y0 + 62.0f, WithAlpha(pal.ruleStrong, 0.5f));

    // 卡片列表
    float ly = y0 + 78.0f;
    m_cardDelRects.clear();
    if (b.cards.empty()) {
        TextStyle et; et.size = 12.5f; et.role = FontRole::Sans; et.vAlign = VAlign::Middle;
        cv.Text(L"盒是空的——点下方「＋ 添加卡片」收第一张题卡。",
                { x0, ly, x0 + contentW, ly + 40.0f }, et, pal.ink500);
    } else {
        for (size_t i = 0; i < b.cards.size(); ++i) {
            const auto& cd = b.cards[i];
            D2D1_RECT_F row{ x0, ly, x0 + contentW, ly + 52.0f };
            cv.PaperCard(row, 1.5f);
            cv.FillRect({ row.left, row.top, row.left + 3.0f, row.bottom },
                        WithAlpha(wrong ? pal.vermilion : pal.jade, 0.7f));
            TextStyle ft; ft.size = 13.5f; ft.role = FontRole::Sans; ft.vAlign = VAlign::Middle;
            ft.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            std::wstring front = cd.front.empty() ? L"（无题面）" : cd.front;
            if (front.size() > 40) front = front.substr(0, 40) + L"…";
            cv.Text(front, { row.left + 14.0f, row.top, row.right - 130.0f, row.bottom }, ft, pal.ink900);
            TextStyle tt; tt.size = 11.0f; tt.role = FontRole::Mono; tt.vAlign = VAlign::Middle;
            tt.hAlign = HAlign::Right;
            cv.Text(cd.tag.empty() ? L"" : (L"#" + cd.tag),
                    { row.right - 210.0f, row.top, row.right - 126.0f, row.bottom }, tt, pal.jade);
            D2D1_RECT_F del{ row.right - 116.0f, row.top + 11.0f, row.right - 88.0f, row.bottom - 11.0f };
            m_cardDelRects.push_back(del);
            TextStyle dt; dt.size = 12.0f; dt.hAlign = HAlign::Center; dt.vAlign = VAlign::Middle;
            cv.Text(L"✕", del, dt, pal.vermilion);
            ly += 60.0f;
        }
    }

    // ＋ 添加卡片
    ly += 8.0f;
    m_addBtn = { x0, ly, x0 + contentW, ly + 44.0f };
    if (m_addOpen) {
        cv.FillRoundRect(m_addBtn, shape::kEdge, WithAlpha(pal.seal, 0.06f));
        cv.StrokeRoundRect(m_addBtn, shape::kEdge, pal.seal, shape::kHair);
        TextStyle lb; lb.size = 11.0f; lb.role = FontRole::Mono; lb.letterSpacing = 1.0f;
        cv.Text(m_addStage == 0 ? L"① 题面（回车下一步）" :
                m_addStage == 1 ? L"② 答案 / 解析（回车下一步）" : L"③ 标签（回车保存；可留空）",
                { x0 + 14.0f, ly + 6.0f, x0 + contentW - 14.0f, ly + 22.0f }, lb, pal.seal);
        std::wstring cur = m_addStage == 0 ? L"输入题面…" :
                           m_addStage == 1 ? L"输入答案…" : L"输入标签（如：资料分析）…";
        TextStyle vs; vs.size = 13.5f; vs.role = FontRole::Sans; vs.vAlign = VAlign::Middle;
        m_edit.Paint(cv, { x0 + 14.0f, ly + 22.0f, x0 + contentW - 14.0f, ly + 38.0f },
                     vs, pal.ink900, cur, pal.ink300, 0.0f, 0.0f);
    } else {
        cv.FillRoundRect(m_addBtn, shape::kEdge, WithAlpha(pal.jade, 0.08f));
        cv.StrokeRoundRect(m_addBtn, shape::kEdge, WithAlpha(pal.jade, 0.8f), shape::kHair);
        TextStyle at; at.size = 13.5f; at.role = FontRole::Sans;
        at.hAlign = HAlign::Center; at.vAlign = VAlign::Middle; at.letterSpacing = 1.0f;
        cv.Text(L"＋ 添加卡片", m_addBtn, at, pal.jade);
    }
}

// ============================================================
//  抽卡
// ============================================================
void QuizBoxView::DrawCard(Canvas& cv, float s)
{
    (void)s;
    if (m_curBox < 0 || m_curBox >= (int)m_boxes.size()) { m_view = V_SHELF; return; }
    const auto& b = m_boxes[m_curBox];
    if (m_drawIdx < 0 || m_drawIdx >= (int)b.cards.size()) { m_view = V_BOX; return; }
    const auto& cd = b.cards[m_drawIdx];
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_area.top + 30.0f;

    // 顶部：放回
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

    // 大卡
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
    TextStyle ct; ct.role = FontRole::Serif; ct.size = 22.0f;
    ct.weight = DWRITE_FONT_WEIGHT_BOLD; ct.vAlign = VAlign::Middle;
    float tx0 = card.left + 34.0f, tx1 = card.right - 34.0f;
    if (!m_flip) {
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 11.0f;
        cv.Text(L"题 面", { tx0, card.top + 26.0f, tx1, card.top + 52.0f }, lb, pal.ink300);
        TextStyle ft; ft.role = FontRole::Serif; ft.size = 20.0f;
        ft.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ft.vAlign = VAlign::Middle;
        cv.Text(cd.front.empty() ? L"（无题面）" : cd.front,
                { tx0, card.top + 60.0f, tx1, card.bottom - 30.0f }, ft, pal.ink900);
    } else {
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 11.0f;
        cv.Text(L"答 案", { tx0, card.top + 26.0f, tx1, card.top + 52.0f }, lb, pal.seal);
        TextStyle ft; ft.role = FontRole::Serif; ft.size = 19.0f;
        ft.vAlign = VAlign::Middle;
        cv.Text(cd.back.empty() ? L"（未填写答案）" : cd.back,
                { tx0, card.top + 60.0f, tx1, card.bottom - 30.0f }, ft, pal.ink900);
    }

    // 底部按钮
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
    if (!m_flip) {
        BigBtn(m_flipRect, L"翻面看答案", true);
    } else {
        BigBtn(m_flipRect, L"◀ 看题面", false);
        BigBtn(m_nextRect, L"下一张 ▶", true);
    }
}

// ============================================================
//  主流程
// ============================================================
void QuizBoxView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    if (m_toastT > 0.0f) m_toastT = (std::max)(0.0f, m_toastT - dt);

    float s = ScrollY();
    float mx = in.mouseX, my = in.mouseY + s;

    if (in.keyDown[VK_ESCAPE]) {
        if (m_addOpen) { m_edit.Cancel(); m_addOpen = false; }
        else if (m_renActive) { m_ren.Cancel(); m_renActive = false; }
        else if (m_view == V_DRAW) m_view = V_BOX;
        else if (m_view == V_BOX) m_view = V_SHELF;
    }

    // 编辑态：键盘先交给 FieldEdit
    if (m_addOpen || m_renActive) {
        TextStyle ts; ts.size = 13.5f; ts.role = FontRole::Sans; ts.vAlign = VAlign::Middle;
        if (m_addOpen) {
            D2D1_RECT_F tbox{ m_area.left + (std::max)(8.0f, (m_area.right - m_area.left - (std::min)(shape::kMaxWidth, m_area.right - m_area.left - 72.0f)) * 0.5f) + 14.0f,
                              0, 0, 0 };   // 占位：实际框在 DrawBox 布局中
            // 简化：编辑坐标由 DrawBox 布局推算（与绘制一致）
            float availW = m_area.right - m_area.left;
            float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
            float x0 = m_area.left + (availW - contentW) * 0.5f;
            int cardCount = (m_curBox >= 0 && m_curBox < (int)m_boxes.size()) ? (int)m_boxes[m_curBox].cards.size() : 0;
            float ly = m_area.top + 30.0f + 78.0f + (float)cardCount * 60.0f + 8.0f;
            D2D1_RECT_F ebox{ x0 + 14.0f, ly + 22.0f, x0 + contentW - 14.0f, ly + 38.0f };
            m_edit.HandleMouse(in, *m_cv, ebox, ts, ScrollY(), 0.0f);
            if (m_edit.onEnter == nullptr) {
                m_edit.onEnter = [this] { AdvanceAdd(); };
                m_edit.onEsc   = [this] { m_edit.Cancel(); m_addOpen = false; };
            }
            if (in.clicked && !InRectEbox(ebox, mx, my)) {
                AdvanceAdd();   // 点外推进（front→back→保存）
            }
        }
        if (m_renActive) {
            float availW = m_area.right - m_area.left;
            float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
            float x0 = m_area.left + (availW - contentW) * 0.5f;
            D2D1_RECT_F rbox{ x0 + 140.0f, m_area.top + 32.0f, x0 + contentW - 170.0f, m_area.top + 66.0f };
            TextStyle ns; ns.role = FontRole::Serif; ns.size = 22.0f;
            ns.weight = DWRITE_FONT_WEIGHT_BOLD; ns.vAlign = VAlign::Middle;
            m_ren.HandleMouse(in, *m_cv, rbox, ns, ScrollY(), 0.0f);
            if (in.clicked && !InRect(rbox, mx, my)) {
                std::wstring t;
                m_ren.End(true, t);
                m_renActive = false;
                if (!t.empty() && m_curBox >= 0 && m_curBox < (int)m_boxes.size()) {
                    m_boxes[m_curBox].name = t;
                    BoxStore::Instance().RenameBox(m_boxes[m_curBox].id, t);
                    Toast(L"已改名");
                }
            }
        }
        return;   // 编辑态独占
    }

    if (!in.clicked) return;

    if (m_view == V_SHELF) {
        for (size_t i = 0; i < m_boxRects.size(); ++i) {
            if (InRect(m_boxRects[i], mx, my)) {
                if (InRect(m_boxDelRects[i], mx, my)) {
                    BoxStore::Instance().DeleteBox(m_boxes[i].id);
                    Reload();
                    Toast(L"已删除题盒");
                    return;
                }
                if (InRect(m_boxRenRects[i], mx, my)) {
                    m_curBox = (int)i;
                    m_ren.Begin(m_boxes[i].name, false, 22.0f);
                    m_renActive = true;
                    m_view = V_BOX;
                    return;
                }
                m_curBox = (int)i; m_view = V_BOX; m_drawIdx = -1;
                return;
            }
        }
        if (InRect(m_newBoxRect, mx, my)) {
            // 交替新建：上次为错题盒则这次建普通题盒
            int kind = 0;
            int n = (int)m_boxes.size();
            for (int i = n - 1; i >= 0; --i) if (m_boxes[i].kind == 1) { kind = 0; break; }
            int wrongCount = 0;
            for (auto& b : m_boxes) if (b.kind == 1) ++wrongCount;
            kind = (wrongCount == 0 && n > 0 && m_boxes.back().kind == 0) ? 1 : 0;
            wchar_t nb[40];
            swprintf_s(nb, L"新题盒 %d", n + 1);
            BoxStore::Instance().AddBox(nb, kind);
            Reload();
            // 直接进入新盒
            for (size_t i = 0; i < m_boxes.size(); ++i)
                if (m_boxes[i].name == nb) { m_curBox = (int)i; break; }
            m_view = V_BOX;
            Toast(L"已创建，点击盒名可改名");
            return;
        }
    } else if (m_view == V_BOX) {
        if (InRect(m_backRect, mx, my)) { m_view = V_SHELF; return; }
        if (InRect(m_drawBtn, mx, my)) {
            const auto& b = m_boxes[m_curBox];
            if (b.cards.empty()) { Toast(L"盒是空的，先加几张卡"); return; }
            m_drawIdx = rand() % (int)b.cards.size();
            m_flip = false;
            m_view = V_DRAW;
            return;
        }
        if (InRect(m_addBtn, mx, my) && !m_addOpen) {
            m_addOpen = true; m_addStage = 0;
            m_addFront.clear(); m_addBack.clear();
            m_edit.Begin(L"", false, 13.5f);
            m_edit.onEnter     = [this] { AdvanceAdd(); };
            m_edit.onEsc       = [this] { m_edit.Cancel(); m_addOpen = false; };
            m_edit.onKillFocus = [this] { AdvanceAdd(); };
            return;
        }
        for (size_t i = 0; i < m_cardDelRects.size(); ++i) {
            if (InRect(m_cardDelRects[i], mx, my)) {
                const auto& b = m_boxes[m_curBox];
                if (i < b.cards.size()) {
                    BoxStore::Instance().DeleteCard(b.id, b.cards[i].id);
                    Reload();
                    Toast(L"已删除卡片");
                }
                return;
            }
        }
        // 点盒名 → 改名
        float availW = m_area.right - m_area.left;
        float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
        float x0 = m_area.left + (availW - contentW) * 0.5f;
        if (InRect({ x0 + 140.0f, m_area.top + 30.0f, x0 + contentW - 170.0f, m_area.top + 70.0f }, mx, my)) {
            m_ren.Begin(m_boxes[m_curBox].name, false, 22.0f);
            m_renActive = true;
            return;
        }
    } else if (m_view == V_DRAW) {
        if (InRect(m_backDrawRect, mx, my)) { m_view = V_BOX; return; }
        if (!m_flip && InRect(m_flipRect, mx, my)) { m_flip = true; return; }
        if (m_flip && InRect(m_flipRect, mx, my)) { m_flip = false; return; }
        if (m_flip && InRect(m_nextRect, mx, my)) {
            const auto& b = m_boxes[m_curBox];
            if (!b.cards.empty()) { m_drawIdx = rand() % (int)b.cards.size(); m_flip = false; }
            return;
        }
    }
}

// 添加卡片三段推进：front → back → tag → 保存
void QuizBoxView::AdvanceAdd()
{
    if (!m_addOpen) return;
    std::wstring t;
    m_edit.End(true, t);
    while (!t.empty() && (t.front() == L' ' || t.front() == L'\n')) t.erase(t.begin());
    while (!t.empty() && (t.back() == L' ' || t.back() == L'\n')) t.pop_back();

    if (m_addStage == 0) {
        m_addFront = t.empty() ? L"（无题面）" : t;
        m_addStage = 1;
        m_edit.Begin(L"", false, 13.5f);
        m_edit.onEnter     = [this] { AdvanceAdd(); };
        m_edit.onEsc       = [this] { m_edit.Cancel(); m_addOpen = false; };
        m_edit.onKillFocus = [this] { AdvanceAdd(); };
    } else if (m_addStage == 1) {
        m_addBack = t;
        m_addStage = 2;
        m_edit.Begin(L"", false, 13.5f);
        m_edit.onEnter     = [this] { AdvanceAdd(); };
        m_edit.onEsc       = [this] { m_edit.Cancel(); m_addOpen = false; };
        m_edit.onKillFocus = [this] { AdvanceAdd(); };
    } else {
        if (m_curBox >= 0 && m_curBox < (int)m_boxes.size()) {
            BoxCard c;
            c.id = L"qc_" + std::to_wstring(GetTickCount64());
            c.front = m_addFront;
            c.back  = m_addBack;
            c.tag   = t;
            c.added = (long long)time(nullptr);
            BoxStore::Instance().AddCard(m_boxes[m_curBox].id, c);
            Reload();
            Toast(L"已收进盒中");
        }
        m_addOpen = false;
        m_addStage = 0;
    }
}

bool QuizBoxView::InRectEbox(const D2D1_RECT_F& r, float x, float y) const
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

void QuizBoxView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float s = ScrollY();
    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    if (m_view == V_SHELF)      DrawShelf(cv, s);
    else if (m_view == V_BOX)   DrawBox(cv, s);
    else                        DrawCard(cv, s);

    cv.PopTransform();
    cv.PopClip();

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

void QuizBoxView::DebugForcePreview()
{
    // 截图自检：空库造示例盒与卡片
    if (m_boxes.empty()) {
        BoxStore::Instance().AddBox(L"资料分析题盒", 0);
        BoxStore::Instance().AddBox(L"错题盒", 1);
        Reload();
        if (!m_boxes.empty() && !m_boxes.front().cards.empty()) return;
        auto boxes = BoxStore::Instance().Load();
        if (!boxes.empty()) {
            BoxCard c1; c1.id = L"qc_d1"; c1.front = L"甲乙两车相向而行，速度之和 120km/h，问几小时相遇 300km？";
            c1.back = L"300 ÷ 120 = 2.5 小时。"; c1.tag = L"行程问题"; c1.added = (long long)time(nullptr);
            BoxCard c2; c2.id = L"qc_d2"; c2.front = L"增长率比较：甲 8%、乙 12%，谁的增速快？";
            c2.back = L"乙（12% > 8%）。"; c2.tag = L"资料分析"; c2.added = (long long)time(nullptr);
            boxes.front().cards.push_back(c1);
            boxes.front().cards.push_back(c2);
            BoxStore::Instance().Save(boxes);
            Reload();
        }
    }
}

} // namespace lj
