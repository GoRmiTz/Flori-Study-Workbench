#include "views/MaterialsView.h"
#include "app/Store.h"
#include "ui/Layout.h"
#include <cmath>
#include <algorithm>
#include <windows.h>
#include <shellapi.h>

namespace lj {

static void OpenUrl(const std::wstring& url)
{
    if (url.empty()) return;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void MaterialsView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_hoverRect = { 0,0,0,0 };
}

D2D1_COLOR_F MaterialsView::Accent(int a, const Palette& pal) const
{
    if (a == 1) return pal.brass;
    if (a == 2) return pal.jade;
    return pal.seal;
}

// ============================================================
//  布局
// ============================================================
void MaterialsView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    const auto& C = Content::Get();

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;

    // 纵向流式布局：区块只声明自身高度，位置由 flow 推进
    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);

    // 标题区
    m_contentTop = flow.cursorY;
    flow.block(96.0f);

    // 导语卡：高度按「当前方案 + scheme + 一句话主推 + 主推正文 + 预算金笺」逐项累加，
    // 避免主推正文被裁切或与预算条重叠
    {
        TextStyle ds; ds.role = FontRole::Sans; ds.size = 12.5f;
        float leadH = 16.0f + 14.0f + 22.0f + 14.0f;          // 顶部留白 + 当前方案标签 + scheme + 主推标签
        leadH += cv.MeasureHeight(C.materials.main, ds, contentW - 36.0f);  // 主推正文
        leadH += 12.0f + 34.0f + 10.0f;                        // 间距 + 预算金笺 + 底部留白
        m_leadRect = { x0, flow.cursorY, x0 + contentW, flow.cursorY + leadH };
        m_leadY = flow.block(leadH + 22.0f).top;
    }

    // 分类
    m_catY = flow.cursorY;
    m_cats.clear();
    float gap = 14.0f;
    TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f;  // 条目标题
    TextStyle ds; ds.role = FontRole::Sans; ds.size = 12.0f;  // 条目描述
    for (size_t ci = 0; ci < C.materials.categories.size(); ++ci) {
        const auto& cat = C.materials.categories[ci];
        CatGeom g; g.accent = (int)(ci % 3);
        // 先算整个分类块高度
        float innerH = 36.0f + 12.0f;
        for (size_t ii = 0; ii < cat.items.size(); ++ii) {
            const auto& it = cat.items[ii];
            float th = 22.0f;                                              // 标题高
            float dh = cv.MeasureHeight(it.d, ds, contentW - 64.0f) + 4.0f; // 描述（可能多行）
            float rh = th + dh + 14.0f;
            innerH += rh + 8.0f;
        }
        float top = flow.block(innerH + 6.0f + gap).top;
        g.header = { x0, top, x0 + contentW, top + 36.0f };
        float yy = top + 36.0f + 12.0f;
        for (size_t ii = 0; ii < cat.items.size(); ++ii) {
            const auto& it = cat.items[ii];
            float th = 22.0f;
            float dh = cv.MeasureHeight(it.d, ds, contentW - 64.0f) + 4.0f;
            float rh = th + dh + 14.0f;
            D2D1_RECT_F row{ x0 + 14.0f, yy, x0 + contentW - 14.0f, yy + rh };
            g.items.push_back(row);
            if (!it.url.empty()) {
                TextStyle bs; bs.role = FontRole::Mono; bs.size = 10.0f;
                float bw = cv.MeasureWidth(L"↗ 资源", bs) + 18.0f;
                g.linkRects.push_back({ row.right - bw, row.top + 3.0f, row.right, row.top + 3.0f + 20.0f });
                g.linkUrls.push_back(it.url);
            } else {
                g.linkRects.push_back({ 0,0,0,0 });
                g.linkUrls.push_back(L"");
            }
            yy += rh + 8.0f;
        }
        m_cats.push_back(g);
    }

    // 避坑：每条固定预留 40px 文本框 + 12px 间距，卡片高度据此累加，避免末条被裁切
    float pitH = 30.0f;   // 标题
    for (size_t i = 0; i < C.materials.pitfalls.size(); ++i) {
        pitH += 40.0f;
        if (i + 1 < C.materials.pitfalls.size()) pitH += 12.0f;
    }
    pitH += 14.0f;        // 底部留白
    m_pitRect = { x0, flow.cursorY, x0 + contentW, flow.cursorY + pitH };
    m_pitY = flow.block(pitH + 22.0f).top;

    // 资料库内跳转：本地资源库 / 公共视频广场（顶栏只留「资料库」一项）
    {
        float top = flow.block(44.0f + 18.0f).top;
        float bw = (contentW - 12.0f) * 0.5f;
        m_resBtn.label = L"本地资源库";
        m_resBtn.fontSize = 12.5f;
        m_resBtn.primary = false;
        m_resBtn.bounds = { x0, top, x0 + bw, top + 44.0f };
        m_resBtn.onClick = [this] { Go(L"media"); };
        m_videoBtn.label = L"公共视频广场";
        m_videoBtn.fontSize = 12.5f;
        m_videoBtn.primary = false;
        m_videoBtn.bounds = { x0 + bw + 12.0f, top, x0 + contentW, top + 44.0f };
        m_videoBtn.onClick = [this] { Go(L"video"); };
    }

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

    m_widgets.clear();
    m_widgets.push_back(&m_resBtn);
    m_widgets.push_back(&m_videoBtn);
    m_widgets.push_back(&m_backBtn);
}

void MaterialsView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);

    // 外链徽标：悬停高亮 + 点击打开
    float mx = in.mouseX, my = shifted.mouseY;
    m_hoverRect = { 0,0,0,0 };
    for (const auto& g : m_cats) {
        for (size_t i = 0; i < g.linkRects.size(); ++i) {
            const auto& r = g.linkRects[i];
            if (r.right <= r.left) continue;
            if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                m_hoverRect = r;
                if (in.clicked) OpenUrl(g.linkUrls[i]);
                goto doneLinks;
            }
        }
    }
doneLinks:
    (void)0;
}

// ============================================================
//  绘制
// ============================================================
void MaterialsView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    PaintTitle(cv, x0, m_contentTop, contentW);
    PaintLead(cv, x0, m_leadY, contentW);
    PaintCategories(cv, x0, m_catY, contentW);
    PaintPitfalls(cv, x0, m_pitY, contentW);

    m_resBtn.Paint(cv);
    m_videoBtn.Paint(cv);
    m_backBtn.Paint(cv);

    cv.PopTransform();
    cv.PopClip();

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

void MaterialsView::PaintTitle(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    const float w = contentW;
    (void)w;
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    float ha = Clamp01(m_t / 0.5f);
    cv.PushOpacity(ha);
    cv.Text(L"SECTION · 资料推荐 · 备考资料库", { x0, y, x0 + 420.0f, y + 16.0f }, sec, pal.ink300);
    cv.PopOpacity();

    TextStyle h1; h1.role = FontRole::Serif; h1.size = 38.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"资料推荐", x0, y + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);

    cv.PerforationH(x0, x0 + contentW, y + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));
}

void MaterialsView::PaintLead(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();
    float appear = Clamp01((m_t - 0.3f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    cv.PaperCard(m_leadRect, 0.0f, shape::kEdge);

    float ix = m_leadRect.left + 18.0f;
    float iy = m_leadRect.top + 16.0f;
    // 当前方案
    TextStyle lab; lab.role = FontRole::Mono; lab.size = 10.0f; lab.letterSpacing = 1.4f;
    cv.Text(L"当前方案", { ix, iy, m_leadRect.right - 18.0f, iy + 14.0f }, lab, pal.ink300);
    TextStyle sv; sv.role = FontRole::Sans; sv.size = 13.0f; sv.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    cv.Text(C.materials.scheme, { ix, iy + 16.0f, m_leadRect.right - 18.0f, iy + 38.0f }, sv, pal.ink900);

    // 一句话主推
    float my = iy + 44.0f;
    TextStyle ml; ml.role = FontRole::Mono; ml.size = 10.0f; ml.letterSpacing = 1.4f;
    cv.Text(L"一句话主推", { ix, my, m_leadRect.right - 18.0f, my + 14.0f }, ml, pal.ink300);
    TextStyle mt; mt.role = FontRole::Sans; mt.size = 13.5f; mt.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    mt.vAlign = VAlign::Top;
    cv.Text(C.materials.main, { ix, my + 16.0f, m_leadRect.right - 18.0f, m_leadRect.bottom - 46.0f }, mt, pal.ink700);

    // 预算参考（金笺）
    float by = m_leadRect.bottom - 44.0f;
    D2D1_RECT_F note{ m_leadRect.left + 10.0f, by, m_leadRect.right - 10.0f, m_leadRect.bottom - 10.0f };
    cv.FillRoundRect(note, shape::kEdgeSoft, WithAlpha(pal.brassWash, pal.dark ? 0.5f : 0.62f));
    TextStyle bt; bt.role = FontRole::Mono; bt.size = 10.0f; bt.weight = DWRITE_FONT_WEIGHT_BOLD;
    bt.vAlign = VAlign::Middle;
    cv.Text(L"预算参考", { note.left + 12.0f, note.top, note.left + 84.0f, note.bottom }, bt, pal.brass);
    TextStyle bv; bv.role = FontRole::Sans; bv.size = 11.5f; bv.vAlign = VAlign::Middle;
    cv.Text(C.materials.budget, { note.left + 90.0f, note.top, note.right - 12.0f, note.bottom }, bv, pal.ink700);

    cv.PopOpacity();
    cv.PopTransform();
}

void MaterialsView::PaintCategories(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();
    float appear = Clamp01((m_t - 0.4f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    for (size_t ci = 0; ci < m_cats.size() && ci < C.materials.categories.size(); ++ci) {
        const auto& cat = C.materials.categories[ci];
        const auto& g = m_cats[ci];
        D2D1_COLOR_F ac = Accent(g.accent, pal);

        // 分类头
        float ca = Clamp01((m_t - 0.5f - (float)ci * 0.05f) / 0.5f);
        if (ca > 0.004f) {
            cv.PushOpacity(ease::OutCubic(ca));
            cv.PaperCard(g.header, 0.0f, shape::kEdge);
            cv.FillRect({ g.header.left, g.header.top, g.header.left + 3.0f, g.header.bottom }, WithAlpha(ac, 0.85f));
            TextStyle hs; hs.role = FontRole::Sans; hs.size = 14.5f; hs.weight = DWRITE_FONT_WEIGHT_BOLD;
            hs.vAlign = VAlign::Middle;
            cv.Text(cat.name, { g.header.left + 16.0f, g.header.top, g.header.right - 16.0f, g.header.bottom }, hs, pal.ink900);
            cv.PopOpacity();
        }

        // 条目
        for (size_t ii = 0; ii < g.items.size() && ii < cat.items.size(); ++ii) {
            const auto& it = cat.items[ii];
            const auto& row = g.items[ii];
            float ia = Clamp01((m_t - 0.6f - (float)ci * 0.05f - (float)ii * 0.04f) / 0.5f);
            if (ia <= 0.004f) continue;
            float er = ease::OutCubic(ia);
            cv.PushOpacity(er);
            cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - er) * 8.0f));
            cv.FillRoundRect(row, shape::kEdgeSoft, WithAlpha(pal.sealWash, pal.dark ? 0.22f : 0.30f));
            // 标题
            TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            cv.Text(it.t, { row.left + 14.0f, row.top + 10.0f, row.right - 86.0f, row.top + 32.0f }, ts, pal.ink900);
            // 外链徽标矩形（先取出，描述行右侧需避让，避免文字压到徽标上）
            const auto& br = g.linkRects[ii];
            // 描述（可换行）
            TextStyle ds; ds.role = FontRole::Sans; ds.size = 12.0f; ds.vAlign = VAlign::Top;
            float dright = (br.right > br.left) ? (br.left - 8.0f) : (row.right - 16.0f);
            cv.Text(it.d, { row.left + 14.0f, row.top + 32.0f, dright, row.bottom - 8.0f }, ds, pal.ink700);
            if (br.right > br.left) {
                bool hov = (br.left == m_hoverRect.left && br.top == m_hoverRect.top &&
                            br.right == m_hoverRect.right && br.bottom == m_hoverRect.bottom);
                D2D1_COLOR_F bc = hov ? pal.ink900 : pal.seal;
                cv.FillRoundRect(br, 3.0f, WithAlpha(bc, hov ? 0.95f : 0.16f));
                cv.StrokeRoundRect(br, 3.0f, WithAlpha(bc, hov ? 0.95f : 0.7f), 1.0f);
                TextStyle bs; bs.role = FontRole::Mono; bs.size = 10.0f; bs.hAlign = HAlign::Center;
                bs.vAlign = VAlign::Middle;
                cv.Text(L"↗ 资源", br, bs, hov ? pal.paperHi : bc);
            }
            cv.PopTransform();
            cv.PopOpacity();
        }
    }

    cv.PopOpacity();
    cv.PopTransform();
}

void MaterialsView::PaintPitfalls(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    const auto& C = Content::Get();
    float appear = Clamp01((m_t - 0.7f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    cv.PaperCard(m_pitRect, 0.0f, shape::kEdge);
    cv.FillRect({ m_pitRect.left, m_pitRect.top, m_pitRect.left + 3.0f, m_pitRect.bottom }, WithAlpha(pal.vermilion, 0.85f));

    TextStyle h2; h2.role = FontRole::Sans; h2.size = 15.0f; h2.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"四个避坑", { m_pitRect.left + 16.0f, m_pitRect.top + 14.0f, m_pitRect.right - 16.0f, m_pitRect.top + 38.0f }, h2, pal.ink900);

    float iy = m_pitRect.top + 44.0f;
    for (size_t i = 0; i < C.materials.pitfalls.size(); ++i) {
        D2D1_COLOR_F dot = (i == 2) ? pal.vermilion : pal.ink500;  // 协议班/包过班 高亮
        cv.FillCircle(m_pitRect.left + 24.0f, iy + 8.0f, 3.0f, dot);
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 12.5f; ts.vAlign = VAlign::Top;
        cv.Text(C.materials.pitfalls[i], { m_pitRect.left + 36.0f, iy, m_pitRect.right - 16.0f, iy + 40.0f }, ts, pal.ink700);
        iy += 52.0f;   // 40（文本框）+ 12（间距），与 Layout 累加保持一致
    }

    cv.PopOpacity();
    cv.PopTransform();
}

} // namespace lj
