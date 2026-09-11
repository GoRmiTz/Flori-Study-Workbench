// ============================================================
//  KnowledgeView.cpp — F-D5 知识库（跨窗口拖拽收集的考点）
//  列表展示考点卡，支持展开全文 / 删除。数据来自 CheckinStore::LoadKnowledge。
// ============================================================
#include "views/KnowledgeView.h"
#include "core/KnowledgeDrop.h"
#include <ctime>

namespace lj {

bool KnowledgeView::InRect(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

std::wstring KnowledgeView::FmtTime(long long ts)
{
    if (ts <= 0) return L"";
    time_t t = (time_t)ts;
    struct tm tm;
    localtime_s(&tm, &t);
    wchar_t buf[64];
    swprintf_s(buf, 64, L"%04d-%02d-%02d %02d:%02d",
               tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    return buf;
}

void KnowledgeView::OnEnter()
{
    View::OnEnter();
    Reload();
}

void KnowledgeView::Reload()
{
    m_cards = CheckinStore::Instance().LoadKnowledge();
    m_expanded.assign(m_cards.size(), false);
    RecomputeLayout();
}

void KnowledgeView::RecomputeLayout()
{
    if (m_cv) Layout(m_area, *m_cv);
}

void KnowledgeView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    m_area = area;
    m_cv = &cv;

    float pad = 28.0f;
    float x = area.left + pad;
    float w = (area.right - area.left) - 2.0f * pad;

    ui::VLayout v(x, area.top + 92.0f, w, 14.0f);
    m_cardRects.clear();
    m_delRects.clear();
    m_expandRects.clear();

    TextStyle bodyTs; bodyTs.size = 13.0f; bodyTs.role = FontRole::Sans;

    if (m_cards.empty()) {
        v.block(120.0f);   // 空态占位（实际卡片在 Paint 里单独绘制）
    } else {
        for (size_t i = 0; i < m_cards.size(); ++i) {
            float maxW = w - 32.0f - 170.0f;   // 右侧留给按钮
            float bodyH = m_expanded[i]
                ? cv.MeasureHeight(m_cards[i].body.empty() ? L" " : m_cards[i].body, bodyTs, maxW)
                : 38.0f;
            float h = 16.0f /*top*/ + 26.0f /*title*/ + 18.0f /*meta*/ + 10.0f + bodyH + 14.0f /*bottom*/;
            D2D1_RECT_F r = v.block(h);
            m_cardRects.push_back(r);
            float bw = 70.0f, bh = 30.0f;
            m_delRects.push_back(ui::MakeRect(r.right - bw - 14.0f, r.top + 14.0f, bw, bh));
            m_expandRects.push_back(ui::MakeRect(r.right - 2.0f * bw - 24.0f, r.top + 14.0f, bw, bh));
        }
    }
    SetContentHeight((v.bottom() - area.top) + 40.0f);
}

void KnowledgeView::Update(float dt, const Input& in)
{
    if (m_toast) { m_toastT += dt; if (m_toastT > 2.4f) m_toast = false; }

    // F-D5+：拖入收集反馈——版本号变化 = 刚收集成功 → 刷新+Toast；
    // 拖拽悬停中 → Paint 画高亮蒙层
    long long rev = KnowledgeRev();
    if (rev != m_lastRev) { m_lastRev = rev; Reload(); Toast(L"已收集考点"); }
    m_dragActive = KnowledgeDragActive();

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    float mx = in.mouseX, my = shifted.mouseY;

    if (in.clicked) {
        for (size_t i = 0; i < m_cardRects.size(); ++i) {
            if (InRect(m_delRects[i], mx, my)) {
                CheckinStore::Instance().RemoveKnowledge(m_cards[i].id);
                Reload();
                Toast(L"已删除考点");
                return;
            }
            if (InRect(m_expandRects[i], mx, my)) {
                m_expanded[i] = !m_expanded[i];
                RecomputeLayout();
                return;
            }
        }
    }
    View::Update(dt, in);
}

void KnowledgeView::Toast(const std::wstring& msg)
{
    m_toastMsg = msg;
    m_toast = true;
    m_toastT = 0.0f;
}

void KnowledgeView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    // 标题
    TextStyle ht; ht.size = 22.0f; ht.role = FontRole::Serif; ht.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"考点 · 知识库",
            { m_area.left + 28.0f, m_area.top + 24.0f, m_area.right - 28.0f, m_area.top + 60.0f }, ht, pal.ink900);
    TextStyle sub; sub.size = 12.0f; sub.role = FontRole::Sans;
    std::wstring hint = m_cards.empty()
        ? L"从浏览器 / PDF 选中文字，或拖入 .md / .txt 文件，直接拖进本页面即可收集为考点"
        : (std::to_wstring(m_cards.size()) + L" 条 · 继续拖文字 / md / txt 文件进窗口即可收集");
    cv.Text(hint,
            { m_area.left + 28.0f, m_area.top + 62.0f, m_area.right - 28.0f, m_area.top + 84.0f }, sub, pal.ink500);

    if (m_cards.empty()) {
        D2D1_RECT_F er = ui::MakeRect(m_area.left + 28.0f, m_area.top + 100.0f,
                                      m_area.right - m_area.left - 56.0f, 96.0f);
        cv.PaperCard(er, 2.0f);
        TextStyle et; et.size = 14.0f; et.role = FontRole::Sans;
        et.hAlign = HAlign::Center; et.vAlign = VAlign::Middle;
        cv.Text(L"暂无考点。从网页 / 资料里选中重点文字，直接拖进这个页面试试。",
                er, et, pal.ink500);
    } else {
        for (size_t i = 0; i < m_cards.size(); ++i)
            PaintCard(cv, m_cards[i], m_expanded[i], m_cardRects[i], (int)i);
    }

    // F-D5+ 拖拽悬停反馈：印章红蒙层 + 虚线内框 + 中央提示
    if (m_dragActive) {
        D2D1_RECT_F a = m_area;
        cv.FillRect(a, WithAlpha(pal.seal, 0.10f));
        D2D1_RECT_F inner{ a.left + 14.0f, a.top + 14.0f, a.right - 14.0f, a.bottom - 14.0f };
        cv.StrokeRoundRect(inner, shape::kEdgeSoft, WithAlpha(pal.seal, 0.85f), shape::kStroke);
        D2D1_RECT_F band{ a.left + (a.right - a.left) * 0.5f - 170.0f,
                          a.top + (a.bottom - a.top) * 0.5f - 26.0f,
                          a.left + (a.right - a.left) * 0.5f + 170.0f,
                          a.top + (a.bottom - a.top) * 0.5f + 26.0f };
        cv.FillRoundRect(band, shape::kEdgeSoft, pal.seal);
        TextStyle bt; bt.size = 14.0f; bt.role = FontRole::Sans;
        bt.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle; bt.letterSpacing = 1.0f;
        cv.Text(L"松开鼠标，收录为考点", band, bt, pal.paperHi);
    }

    cv.PopTransform();
    cv.PopClip();

    if (m_toast) DrawToast(cv);
}

void KnowledgeView::PaintCard(Canvas& cv, const KCard& c, bool expanded,
                              const D2D1_RECT_F& r, int idx)
{
    const auto& pal = cv.Pal();
    cv.PaperCard(r, 2.0f);
    cv.PushClip(r);

    float padL = r.left + 16.0f;
    float maxW = r.right - 16.0f - 170.0f;

    TextStyle ts; ts.size = 15.0f; ts.role = FontRole::Sans; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(c.title, { padL, r.top + 14.0f, padL + maxW, r.top + 40.0f }, ts, pal.ink900);

    TextStyle ms; ms.size = 11.0f; ms.role = FontRole::Sans;
    cv.Text(c.source + L" · " + FmtTime(c.ts),
            { padL, r.top + 42.0f, padL + maxW, r.top + 60.0f }, ms, pal.ink500);

    TextStyle bs; bs.size = 13.0f; bs.role = FontRole::Sans;
    float by = r.top + 66.0f;
    if (expanded) {
        cv.Text(c.body, { padL, by, padL + maxW, r.bottom - 14.0f }, bs, pal.ink700);
    } else {
        cv.Text(c.body, { padL, by, padL + maxW, by + 38.0f }, bs, pal.ink500);
    }

    cv.PopClip();

    PaintButton(cv, m_expandRects[idx], expanded ? L"收起" : L"展开", pal.paperDeep, pal.ink700);
    PaintButton(cv, m_delRects[idx], L"删除", pal.vermWash, pal.vermilion);
}

void KnowledgeView::PaintButton(Canvas& cv, const D2D1_RECT_F& r, const std::wstring& label,
                                const D2D1_COLOR_F& bg, const D2D1_COLOR_F& fg)
{
    cv.FillRoundRect(r, 6.0f, bg);
    TextStyle ts; ts.size = 12.0f; ts.role = FontRole::Sans;
    ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
    cv.Text(label, r, ts, fg);
}

void KnowledgeView::DrawToast(Canvas& cv)
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
