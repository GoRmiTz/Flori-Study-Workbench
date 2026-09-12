// ============================================================
//  QuizBoxView.cpp — 题集卡片盒 v3 星云架构（批次 G8）
//  星云即唯一界面：恒星=题集 / 轨道=题盒 / 小方块=卡片。
//  崩溃防线：任何树修改后立即清空全部拖拽/命中缓存（AfterTreeChange），
//  所有索引访问前 bounds 检查。
//  需求细节见 docs/题集卡片盒·星云架构v3.md。
// ============================================================
#include "views/QuizBoxView.h"
#include "ui/Layout.h"
#include <commdlg.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>

namespace lj {

static bool InRect(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

// 视图渐入：150ms
static float ViewAlpha(float t) { return Clamp01(t / 0.15f); }

// ---------------- G4 T6：导入解析 ----------------
namespace {

std::wstring TrimW(const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r')) ++a;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r')) --b;
    return s.substr(a, b - a);
}

bool StartsWithW(const std::wstring& s, const std::wstring& p)
{
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

std::vector<QuizBoxImpBox> ParseImportMd(const std::wstring& src)
{
    std::vector<QuizBoxImpBox> out;
    std::vector<std::wstring> lines;
    {
        std::wstring cur;
        for (wchar_t c : src) {
            if (c == L'\n') { lines.push_back(cur); cur.clear(); }
            else if (c != L'\r') cur += c;
        }
        lines.push_back(cur);
    }
    QuizBoxImpBox curBox;
    QCard curCard;
    bool inCard = false;
    auto flushCard = [&]() {
        if (inCard && !curCard.front.empty()) curBox.cards.push_back(curCard);
        curCard = QCard(); inCard = false;
    };
    auto flushBox = [&]() {
        flushCard();
        if (!curBox.cards.empty()) out.push_back(curBox);
        curBox = QuizBoxImpBox();
    };
    for (auto& l0 : lines) {
        std::wstring l = TrimW(l0);
        if (l.empty()) continue;
        if (l.size() >= 2 && l[0] == L'#' && l[1] != L'#') {
            flushBox();
            curBox.name = TrimW(l.substr(1));
            continue;
        }
        if (StartsWithW(l, L"## ")) {
            flushCard();
            curCard.front = TrimW(l.substr(3));
            curCard.difficulty = 3;
            inCard = true;
            continue;
        }
        if (!inCard) continue;
        if (StartsWithW(l, L"- A:") || StartsWithW(l, L"-A:")) {
            size_t p = l.find(L':');
            curCard.back = TrimW(l.substr(p + 1));
            continue;
        }
        if (StartsWithW(l, L"- #:") || StartsWithW(l, L"-#:")) {
            size_t p = l.find(L':');
            curCard.tag = TrimW(l.substr(p + 1));
            continue;
        }
        if (StartsWithW(l, L"- D:") || StartsWithW(l, L"-D:")) {
            size_t p = l.find(L':');
            int d = _wtoi(l.c_str() + p + 1);
            if (d >= 1 && d <= 5) curCard.difficulty = d;
            continue;
        }
        if (curCard.front.empty()) curCard.front = l;
        else curCard.front += L" " + l;
    }
    flushBox();
    return out;
}

std::vector<QuizBoxImpBox> ParseImportCsv(const std::wstring& src)
{
    std::vector<QuizBoxImpBox> out;
    std::vector<std::wstring> lines;
    {
        std::wstring cur;
        for (wchar_t c : src) {
            if (c == L'\n') { lines.push_back(cur); cur.clear(); }
            else if (c != L'\r') cur += c;
        }
        lines.push_back(cur);
    }
    QuizBoxImpBox box;
    box.name = L"导入题盒";
    bool first = true;
    for (auto& l0 : lines) {
        std::wstring l = TrimW(l0);
        if (l.empty()) continue;
        std::vector<std::wstring> cols;
        std::wstring cur;
        for (wchar_t c : l) {
            if (c == L',') { cols.push_back(TrimW(cur)); cur.clear(); }
            else cur += c;
        }
        cols.push_back(TrimW(cur));
        if (first) {
            first = false;
            if (!cols.empty() && (cols[0] == L"题面" || cols[0] == L"front")) continue;
        }
        if (cols.empty() || cols[0].empty()) continue;
        QCard c;
        c.front = cols[0];
        c.back  = cols.size() > 1 ? cols[1] : L"";
        c.tag   = cols.size() > 2 ? cols[2] : L"";
        c.difficulty = 3;
        if (cols.size() > 3) {
            int d = _wtoi(cols[3].c_str());
            if (d >= 1 && d <= 5) c.difficulty = d;
        }
        box.cards.push_back(c);
    }
    if (!box.cards.empty()) out.push_back(box);
    return out;
}

std::wstring ReadImportFile(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return L"";
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return L""; }
    std::string buf; buf.resize((size_t)sz);
    size_t rd = fread(&buf[0], 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) return L"";
    size_t off = 0;
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB
        && (unsigned char)buf[2] == 0xBF) off = 3;
    std::string s = buf.substr(off);
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) {
        std::wstring w; w.resize(s.size());
        for (size_t i = 0; i < s.size(); ++i) w[i] = (wchar_t)(unsigned char)s[i];
        return w;
    }
    std::wstring w; w.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

} // namespace

// ============================================================
//  基础
// ============================================================
void QuizBoxView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_view = V_NEBULA;          // v3：星云即入口（N1）
    m_ceOpen = false; m_impOpen = false;
    m_expOpen = false; m_renActive = false;
    m_nebDrag = false; m_nebDragIdx = -1; m_nebDropBox = -1;
    m_hoverNeb = -1;
    m_sets = BoxStore::Instance().Load();
    RebuildVisible();
    m_viewT = 0.0f;
}

void QuizBoxView::RebuildVisible()
{
    m_visibleSets.clear();
    for (int i = 0; i < (int)m_sets.size(); ++i) m_visibleSets.push_back(i);
    m_focusSet = -1;
}

void QuizBoxView::SetCenters()
{
    m_setCenters.clear(); m_setHitR.clear();
    float W = m_area.right - m_area.left;
    float cxm = W * 0.5f;
    float cym = (m_area.bottom - m_area.top) * 0.5f + m_area.top;
    int n = (int)m_visibleSets.size();
    if (n <= 0) return;
    auto push = [&](float x, float y) {
        m_setCenters.push_back({ x, y });
        m_setHitR.push_back({ x - 60.0f, y - 60.0f, x + 60.0f, y + 60.0f });
    };
    if (n == 1)      push(cxm, cym);
    else if (n == 2) { push(W * 0.28f, cym); push(W * 0.72f, cym); }
    else if (n == 3) { push(W * 0.2f, cym); push(cxm, cym - 30.0f); push(W * 0.8f, cym); }
    else {
        int cols = 2;
        for (int i = 0; i < n; ++i) {
            int r = i / cols, c = i % cols;
            push(W * (0.26f + 0.48f * (c % cols == 0 ? 0.0f : 1.0f) + (c == 0 ? 0.0f : 0.0f)),
                 cym + (r == 0 ? -90.0f : 120.0f));
        }
        // 网格版：直接均分横排（避免复杂）
        m_setCenters.clear(); m_setHitR.clear();
        for (int i = 0; i < n; ++i)
            push(W * (float)(i + 1) / (float)(n + 1), cym);
    }
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

// G3 T10：红警示热度
float QuizBoxView::CardHeat(const QCard& c) const
{
    float wrong = (std::min)(1.0f, c.wrongCount / 3.0f);
    float stale = 0.0f;
    if (c.lastReview > 0) {
        long long days = ((long long)time(nullptr) - c.lastReview) / 86400;
        stale = (std::min)(1.0f, days / 14.0f);
    } else if (c.added > 0) {
        long long days = ((long long)time(nullptr) - c.added) / 86400;
        stale = (std::min)(1.0f, days / 21.0f);
    }
    return Clamp01(wrong * 0.45f + stale * 0.55f);
}

// ---------------- 矢量图形（占位，UI 稿后替换）----------------
void QuizBoxView::DrawHouse(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& accent) const
{
    float w = r.right - r.left, h = r.bottom - r.top;
    float cx = (r.left + r.right) * 0.5f;
    cv.FillRect({ r.left + w * 0.14f, r.top + h * 0.46f, r.right - w * 0.14f, r.bottom },
                WithAlpha(accent, 0.16f));
    cv.StrokeRect({ r.left + w * 0.14f, r.top + h * 0.46f, r.right - w * 0.14f, r.bottom },
                  accent, 1.4f);
    cv.Line(r.left + w * 0.06f, r.top + h * 0.5f, cx, r.top + h * 0.06f, accent, 1.6f);
    cv.Line(cx, r.top + h * 0.06f, r.right - w * 0.06f, r.top + h * 0.5f, accent, 1.6f);
    cv.Line(r.left + w * 0.06f, r.top + h * 0.5f, r.right - w * 0.06f, r.top + h * 0.5f, accent, 1.6f);
    cv.FillRect({ cx - w * 0.09f, r.bottom - h * 0.36f, cx + w * 0.09f, r.bottom - h * 0.04f },
                WithAlpha(accent, 0.5f));
}

// ============================================================
//  星云（v3 主界面）：多恒星 + 轨道 + 卡片 + 展开 + 侧边栏
// ============================================================
void QuizBoxView::DrawNebula(Canvas& cv, float s)
{
    (void)s;
    const auto& pal = cv.Pal();
    SetCenters();

    float va = ViewAlpha(m_viewT);
    cv.PushOpacity(va);

    // 顶部标题（右上：导入入口——N3 纯文字）
    float x0 = m_area.left + 28.0f;
    float y0 = m_area.top + 20.0f;
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 题集卡片盒 · 星云", { x0, y0, x0 + 420.0f, y0 + 16.0f }, sec, pal.ink300);

    float availW = m_area.right - m_area.left;
    m_impBtn = { m_area.right - 150.0f, y0 - 4.0f, m_area.right - 28.0f, y0 + 32.0f };
    cv.FillRoundRect(m_impBtn, 6.0f, pal.paperLo);
    cv.StrokeRoundRect(m_impBtn, 6.0f, pal.rule, shape::kHair);
    TextStyle bt; bt.size = 12.5f; bt.role = FontRole::Sans;
    bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle;
    cv.Text(L"导入题库", m_impBtn, bt, pal.ink700);

    auto diffColor = [&](int d) -> D2D1_COLOR_F {
        switch (d) {
        case 1: case 2: return pal.jade;
        case 3: return pal.brass;
        case 4: return pal.seal;
        default: return pal.vermilion;
        }
    };

    // hover 卡难度（同步高亮）
    int hoverDiff = -1, hoverBox = -1;
    if (m_hoverNeb >= 0 && m_hoverNeb < (int)m_nebCards.size()) {
        const auto& nc = m_nebCards[m_hoverNeb];
        if (nc.setIdx >= 0 && nc.setIdx < (int)m_sets.size()) {
            const auto& st2 = m_sets[nc.setIdx];
            if (nc.boxIdx >= 0 && nc.boxIdx < (int)st2.boxes.size()
                && nc.cardIdx >= 0 && nc.cardIdx < (int)st2.boxes[nc.boxIdx].cards.size()) {
                hoverDiff = st2.boxes[nc.boxIdx].cards[nc.cardIdx].difficulty;
                hoverBox = nc.boxIdx;
            }
        }
    }

    bool searching = !m_searchStr.empty() && !m_searchHits.empty();
    auto isHit = [&](int si, int bi, int ci) -> bool {
        for (auto& h : m_searchHits)
            if (h.first == si && h.second.first == bi && h.second.second == ci) return true;
        return false;
    };

    m_nebCards.clear();
    m_setHitR.clear();

    // ---- 每个可见题集 = 一颗恒星（中心）+ 轨道系统 ----
    SetCenters();
    for (size_t vi = 0; vi < m_visibleSets.size() && vi < m_setCenters.size(); ++vi) {
        int si = m_visibleSets[vi];
        if (si < 0 || si >= (int)m_sets.size()) continue;
        const auto& st = m_sets[si];
        const auto& ctr = m_setCenters[vi];
        bool focused = (m_focusSet == si);

        // 恒星（房子 + 名；聚焦态放大）
        float hs = focused ? 1.15f : 1.0f;
        DrawHouse(cv, { ctr.x - 30.0f * hs, ctr.y - 40.0f * hs, ctr.x + 30.0f * hs, ctr.y + 26.0f * hs },
                  focused ? pal.seal : WithAlpha(pal.seal, 0.8f));
        TextStyle sn; sn.role = FontRole::Serif; sn.size = focused ? 15.0f : 13.0f;
        sn.weight = DWRITE_FONT_WEIGHT_BOLD; sn.hAlign = HAlign::Center; sn.vAlign = VAlign::Top;
        cv.Text(st.name, { ctr.x - 90.0f, ctr.y + 30.0f * hs, ctr.x + 90.0f, ctr.y + 54.0f * hs },
                sn, focused ? pal.ink900 : pal.ink700);
        // N1：恒星命中区（点击聚焦/取消聚焦）
        m_setHitR.push_back({ ctr.x - 60.0f, ctr.y - 64.0f, ctr.x + 60.0f, ctr.y + 58.0f });

        // ---- 轨道系统 ----
        int nb = (int)st.boxes.size();
        float baseRx = focused ? 150.0f : 92.0f;      // 聚焦态轨道更大
        if (m_visibleSets.size() > 2) baseRx = 84.0f;
        for (int bi = 0; bi < nb; ++bi) {
            const auto& b = st.boxes[bi];
            bool dropHot = (m_nebDrag && m_nebDropBox == bi);
            float rx = baseRx + bi * 58.0f;
            if (dropHot) rx *= 1.04f;
            float ry = rx * 0.30f;
            bool laneHot = (hoverBox == bi) || dropHot;
            cv.StrokeEllipse(ctr.x, ctr.y, rx, ry,
                             dropHot ? WithAlpha(pal.seal, 0.95f)
                                     : WithAlpha(pal.rule, laneHot ? 0.9f : 0.4f),
                             dropHot ? 2.0f : 1.0f);
            // N6：轨道名仅 hover 该轨道带时显示
            if (laneHot) {
                TextStyle lt; lt.size = 10.5f; lt.role = FontRole::Mono; lt.vAlign = VAlign::Middle;
                cv.Text(b.name, { ctr.x + rx + 6.0f, ctr.y - 9.0f, ctr.x + rx + 126.0f, ctr.y + 9.0f },
                        lt, dropHot ? pal.seal : pal.ink500);
            }

            // 卡片角度分配（排列方式）
            int n = (int)b.cards.size();
            std::vector<int> order(n);
            for (int i = 0; i < n; ++i) order[i] = i;
            if (m_sortMode == 1)
                std::sort(order.begin(), order.end(), [&](int a, int c) {
                    return b.cards[a].added > b.cards[c].added; });
            else if (m_sortMode == 2)
                std::sort(order.begin(), order.end(), [&](int a, int c) {
                    return b.cards[a].difficulty < b.cards[c].difficulty; });
            else if (m_sortMode == 3)
                std::sort(order.begin(), order.end(), [&](int a, int c) {
                    return b.cards[a].wrongCount > b.cards[c].wrongCount; });

            for (int pi = 0; pi < n; ++pi) {
                int ci = order[pi];
                float ang = m_orbit + (float)pi / (float)n * 6.2831853f;
                float px = ctr.x + std::cos(ang) * rx;
                float py = ctr.y + std::sin(ang) * ry;
                float z = std::sin(ang);
                float scale = 0.78f + (z + 1.0f) * 0.22f;
                float alpha = 0.5f + (z + 1.0f) * 0.25f;
                NebCard nc;
                nc.setIdx = si; nc.boxIdx = bi; nc.cardIdx = ci;
                nc.pos = { px, py };
                nc.z = z; nc.scale = scale; nc.alpha = alpha;
                float w = 16.0f * scale, h = 20.0f * scale;
                nc.rect = { px - w * 0.5f, py - h * 0.5f, px + w * 0.5f, py + h * 0.5f };
                m_nebCards.push_back(nc);
            }
        }
    }

    // 深度排序（远 → 近）
    std::vector<int> order(m_nebCards.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b2) {
        return m_nebCards[a].z < m_nebCards[b2].z;
    });

    for (int idx : order) {
        if (idx >= (int)m_nebCards.size()) continue;
        const auto& nc = m_nebCards[idx];
        if (nc.setIdx < 0 || nc.setIdx >= (int)m_sets.size()) continue;
        const auto& st = m_sets[nc.setIdx];
        if (nc.boxIdx < 0 || nc.boxIdx >= (int)st.boxes.size()) continue;
        if (nc.cardIdx < 0 || nc.cardIdx >= (int)st.boxes[nc.boxIdx].cards.size()) continue;
        const auto& cd = st.boxes[nc.boxIdx].cards[nc.cardIdx];

        D2D1_COLOR_F dc2 = diffColor(cd.difficulty);
        float heat = CardHeat(cd);
        if (heat > 0.05f) {
            dc2.r = dc2.r + (pal.vermilion.r - dc2.r) * heat;
            dc2.g = dc2.g + (pal.vermilion.g - dc2.g) * heat;
            dc2.b = dc2.b + (pal.vermilion.b - dc2.b) * heat;
        }

        bool lit = (hoverDiff == cd.difficulty);
        bool self = (m_hoverNeb == idx);
        bool hit = searching && isHit(nc.setIdx, nc.boxIdx, nc.cardIdx);
        bool dropLane = (m_nebDrag && m_nebDropBox == nc.boxIdx);
        float a = nc.alpha;
        if (m_nebDrag) a = dropLane ? 1.0f : 0.15f;
        else if (searching) a = hit ? 1.0f : 0.10f;
        else a *= (hoverDiff >= 0 ? (lit ? 1.0f : 0.18f) : 1.0f);
        float sc = self ? 1.35f : 1.0f;

        float w = (nc.rect.right - nc.rect.left) * sc;
        float h = (nc.rect.bottom - nc.rect.top) * sc;
        D2D1_RECT_F r{ nc.pos.x - w * 0.5f, nc.pos.y - h * 0.5f,
                       nc.pos.x + w * 0.5f, nc.pos.y + h * 0.5f };
        cv.PushOpacity(a);
        cv.FillRoundRect(r, 2.5f, WithAlpha(dc2, (lit || hit) ? 0.38f : 0.20f));
        cv.StrokeRoundRect(r, 2.5f, WithAlpha(dc2, (lit || hit) ? 1.0f : 0.75f), 1.2f);
        if (lit || hit)
            cv.StrokeRoundRect({ r.left - 3.0f, r.top - 3.0f, r.right + 3.0f, r.bottom + 3.0f },
                               3.5f, WithAlpha(dc2, 0.35f), 1.0f);
        if (hit)
            cv.StrokeRoundRect({ r.left - 7.0f, r.top - 7.0f, r.right + 7.0f, r.bottom + 7.0f },
                               5.0f, WithAlpha(dc2, 0.30f), 1.4f);
        if (heat > 0.5f)
            cv.FillCircle(nc.pos.x, r.top - 2.0f, 2.6f, pal.vermilion);   // N3：色点代替 ⚠
        cv.PopOpacity();
        m_nebCards[idx].rect = r;
    }

    // ---- G6 T9 拖拽跟随小卡 ----
    if (m_nebDrag && m_nebDragIdx >= 0 && m_nebDragIdx < (int)m_nebCards.size()) {
        const auto& dc0 = m_nebCards[m_nebDragIdx];
        if (dc0.setIdx >= 0 && dc0.setIdx < (int)m_sets.size()
            && dc0.boxIdx >= 0 && dc0.boxIdx < (int)m_sets[dc0.setIdx].boxes.size()
            && dc0.cardIdx >= 0
            && dc0.cardIdx < (int)m_sets[dc0.setIdx].boxes[dc0.boxIdx].cards.size()) {
            const auto& cd0 = m_sets[dc0.setIdx].boxes[dc0.boxIdx].cards[dc0.cardIdx];
            D2D1_RECT_F r{ m_nebDragX - 13.0f, m_nebDragY - 16.0f, m_nebDragX + 13.0f, m_nebDragY + 16.0f };
            cv.PushOpacity(0.92f);
            cv.FillRoundRect(r, 3.0f, pal.paperHi);
            cv.StrokeRoundRect(r, 3.0f, diffColor(cd0.difficulty), 1.6f);
            cv.PopOpacity();
            std::wstring fr = cd0.front;
            if (fr.size() > 14) fr = fr.substr(0, 14) + L"…";
            TextStyle dl; dl.size = 11.0f; dl.role = FontRole::Sans; dl.vAlign = VAlign::Middle;
            cv.Text(fr, { m_nebDragX + 20.0f, m_nebDragY - 12.0f, m_nebDragX + 240.0f, m_nebDragY + 12.0f },
                    dl, pal.ink900);
        }
    }

    // ---- G5 hover tooltip ----
    if (!m_expOpen && m_hoverNeb >= 0 && m_hoverNeb < (int)m_nebCards.size()) {
        const auto& nc = m_nebCards[m_hoverNeb];
        if (nc.setIdx >= 0 && nc.setIdx < (int)m_sets.size()
            && nc.boxIdx >= 0 && nc.boxIdx < (int)m_sets[nc.setIdx].boxes.size()
            && nc.cardIdx >= 0 && nc.cardIdx < (int)m_sets[nc.setIdx].boxes[nc.boxIdx].cards.size()) {
            const auto& cd = m_sets[nc.setIdx].boxes[nc.boxIdx].cards[nc.cardIdx];
            float tw = 280.0f, th = 92.0f;
            float tx = nc.pos.x + 20.0f;
            if (tx + tw > m_area.right - 290.0f) tx = nc.pos.x - tw - 20.0f;
            float ty = nc.pos.y - th * 0.5f;
            if (ty < m_area.top + 50.0f) ty = m_area.top + 50.0f;
            D2D1_RECT_F tip{ tx, ty, tx + tw, ty + th };
            cv.FillRoundRect(tip, 6.0f, pal.paperHi);
            cv.StrokeRoundRect(tip, 6.0f, WithAlpha(diffColor(cd.difficulty), 0.9f), shape::kHair);
            TextStyle t1; t1.size = 12.5f; t1.role = FontRole::Sans;
            t1.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            std::wstring fr = cd.front;
            if (fr.size() > 24) fr = fr.substr(0, 24) + L"…";
            cv.Text(fr, { tip.left + 12.0f, tip.top + 8.0f, tip.right - 12.0f, tip.top + 30.0f }, t1, pal.ink900);
            TextStyle t2; t2.size = 10.5f; t2.role = FontRole::Mono;
            wchar_t lb[96];
            swprintf_s(lb, L"%s · 难度 D%d · 错 %d 次",
                       m_sets[nc.setIdx].boxes[nc.boxIdx].name.c_str(), cd.difficulty, cd.wrongCount);
            cv.Text(lb, { tip.left + 12.0f, tip.top + 34.0f, tip.right - 12.0f, tip.top + 52.0f }, t2, pal.ink500);
            if (!cd.tag.empty())
                cv.Text(L"#" + cd.tag, { tip.left + 12.0f, tip.top + 56.0f, tip.right - 12.0f, tip.top + 74.0f },
                        t2, pal.jade);
            TextStyle t3; t3.size = 9.5f; t3.role = FontRole::Mono;
            cv.Text(L"点击展开卡片 · 按住可拖到其他轨道",
                    { tip.left + 12.0f, tip.bottom - 22.0f, tip.right - 12.0f, tip.bottom - 6.0f },
                    t3, pal.ink300);
        }
    }

    // ---- N7：聚焦态提示（恒星位置即弹窗中心）----
    if (m_focusSet >= 0) {
        TextStyle fp; fp.size = 10.0f; fp.role = FontRole::Mono;
        fp.hAlign = HAlign::Center;
        cv.Text(L"聚焦中 · 点击恒星返回全览",
                { m_area.left + 28.0f, m_area.bottom - 34.0f, m_area.left + 400.0f, m_area.bottom - 16.0f },
                fp, pal.ink300);
    }

    cv.PopOpacity();
}

// ============================================================
//  N4：展开卡（星云内放大卡面 / 翻面卡背 / 记住·答错 / 点空白收回）
// ============================================================
void QuizBoxView::DrawExpand(Canvas& cv)
{
    const auto& pal = cv.Pal();
    if (m_expBox < 0 || m_expBox >= (int)m_sets.size()) return;
    const auto& st = m_sets[m_expBox];
    if (m_expCard < 0 || m_expCard >= (int)st.boxes.size()) return;
    const auto& b = st.boxes[m_expCard];
    if (m_expCard2 < 0 || m_expCard2 >= (int)b.cards.size()) return;
    const auto& cd = b.cards[m_expCard2];

    float a = Clamp01(m_expT / 0.25f);
    float e = ease::OutBack(a);
    float W = m_area.right - m_area.left;
    float H = m_area.bottom - m_area.top;
    cv.FillRect(m_area, WithAlpha(pal.ink900, 0.5f * a));

    float cw = (std::min)(520.0f, W - 80.0f);
    float chh = 380.0f;
    float px = (W - cw) * 0.5f, py = (H - chh) * 0.5f;
    // 展开动画：从 0.3 缩放浮入
    float k = 0.3f + 0.7f * e;
    float cx = W * 0.5f, cy = H * 0.5f;
    float hw = cw * 0.5f * k, hh = chh * 0.5f * k;
    D2D1_RECT_F card{ cx - hw, cy - hh - (1.0f - e) * 14.0f, cx + hw, cy + hh - (1.0f - e) * 14.0f };

    cv.PushOpacity(a);
    cv.PaperCard(card, 2.0f);

    auto diffColor = [&](int d) -> D2D1_COLOR_F {
        switch (d) {
        case 1: case 2: return pal.jade;
        case 3: return pal.brass;
        case 4: return pal.seal;
        default: return pal.vermilion;
        }
    };
    float heat = CardHeat(cd);
    D2D1_COLOR_F edge = diffColor(cd.difficulty);
    if (heat > 0.05f) {
        edge.r = edge.r + (pal.vermilion.r - edge.r) * heat;
        edge.g = edge.g + (pal.vermilion.g - edge.g) * heat;
        edge.b = edge.b + (pal.vermilion.b - edge.b) * heat;
    }
    cv.StrokeRoundRect(card, shape::kEdge, edge, 2.0f);
    cv.DoubleFrame(card, WithAlpha(pal.seal, 0.35f));

    // 难度方块
    for (int d = 0; d < 5; ++d)
        cv.FillRect({ card.left + 26.0f + d * 13.0f, card.top + 24.0f,
                      card.left + 26.0f + d * 13.0f + 9.0f, card.top + 35.0f },
                    d < cd.difficulty ? edge : WithAlpha(pal.rule, 0.5f));
    if (!cd.tag.empty()) {
        D2D1_RECT_F badge{ card.right - 130.0f, card.top + 18.0f, card.right - 26.0f, card.top + 44.0f };
        cv.FillRoundRect(badge, 11.0f, WithAlpha(pal.jade, 0.14f));
        TextStyle bgt; bgt.size = 11.0f; bgt.role = FontRole::Mono;
        bgt.hAlign = HAlign::Center; bgt.vAlign = VAlign::Middle;
        cv.Text(L"#" + cd.tag, badge, bgt, pal.jade);
    }
    if (heat > 0.5f) {
        cv.FillCircle(card.left + 34.0f, card.top + 18.0f, 3.5f, pal.vermilion);
        TextStyle wt; wt.size = 10.0f; wt.role = FontRole::Mono; wt.vAlign = VAlign::Middle;
        cv.Text(L"需要维护", { card.left + 42.0f, card.top + 10.0f, card.left + 150.0f, card.top + 28.0f },
                wt, pal.vermilion);
    }

    float tx0 = card.left + 34.0f, tx1 = card.right - 34.0f;
    if (!m_expFlip) {
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 11.0f;
        cv.Text(L"题 面", { tx0, card.top + 56.0f, tx1, card.top + 78.0f }, lb, pal.ink300);
        TextStyle ft; ft.role = FontRole::Serif; ft.size = 20.0f;
        ft.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ft.vAlign = VAlign::Middle;
        cv.Text(cd.front.empty() ? L"（无题面）" : cd.front,
                { tx0, card.top + 84.0f, tx1, card.bottom - 90.0f }, ft, pal.ink900);
    } else {
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 11.0f;
        cv.Text(L"答 案", { tx0, card.top + 56.0f, tx1, card.top + 78.0f }, lb, pal.seal);
        TextStyle ft; ft.role = FontRole::Serif; ft.size = 18.0f; ft.vAlign = VAlign::Middle;
        cv.Text(cd.back.empty() ? L"（未填写答案）" : cd.back,
                { tx0, card.top + 84.0f, tx1, card.bottom - 90.0f }, ft, pal.ink900);
        // 卡背属性（N4：答案、标签、归属、难度、错次、复习状态）
        TextStyle at; at.size = 11.0f; at.role = FontRole::Mono; at.vAlign = VAlign::Middle;
        long long days = cd.lastReview > 0 ? ((long long)time(nullptr) - cd.lastReview) / 86400 : -1;
        wchar_t ab[128];
        swprintf_s(ab, L"归属 %s · 错 %d 次 · %s", b.name.c_str(), cd.wrongCount,
                   days >= 0 ? (days == 0 ? L"今天复习过" : L"距上次复习 %d 天") : L"从未复习");
        cv.Text(ab, { tx0, card.bottom - 82.0f, tx1, card.bottom - 62.0f }, at, pal.ink500);
    }

    // 翻面按钮 + 反馈（卡背时）
    float by = card.bottom - 52.0f;
    m_expFlipBtn = { card.left + 30.0f, by, card.left + 160.0f, by + 38.0f };
    cv.FillRoundRect(m_expFlipBtn, 6.0f, m_expFlip ? pal.paperLo : pal.seal);
    cv.StrokeRoundRect(m_expFlipBtn, 6.0f, m_expFlip ? pal.rule : pal.seal, shape::kHair);
    TextStyle bb; bb.size = 12.5f; bb.role = FontRole::Sans;
    bb.hAlign = HAlign::Center; bb.vAlign = VAlign::Middle;
    cv.Text(m_expFlip ? L"看题面" : L"看答案", m_expFlipBtn, bb,
            m_expFlip ? pal.ink700 : pal.paperHi);
    if (m_expFlip) {
        m_expRemBtn   = { card.right - 240.0f, by, card.right - 140.0f, by + 38.0f };
        m_expWrongBtn = { card.right - 130.0f, by, card.right - 30.0f, by + 38.0f };
        cv.FillRoundRect(m_expRemBtn, 6.0f, pal.jade);
        cv.Text(L"记住了", m_expRemBtn, bb, pal.paperHi);
        cv.FillRoundRect(m_expWrongBtn, 6.0f, pal.vermilion);
        cv.Text(L"答错了", m_expWrongBtn, bb, pal.paperHi);
    } else {
        m_expRemBtn = m_expWrongBtn = { 0, 0, 0, 0 };
    }
    cv.PopOpacity();
}

// ============================================================
//  侧边栏 v3：题集勾选 + 盒管理 + 搜索/展现/排列/聚焦 + 导入
// ============================================================
void QuizBoxView::DrawSidebar(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float sbW = 250.0f;
    float sbx = m_area.right - sbW - 18.0f;
    float sby = m_area.top + 66.0f;

    float sbH = m_area.bottom - m_area.top - sby - 20.0f;
    D2D1_RECT_F sb{ sbx, sby, sbx + sbW, sby + sbH };
    cv.FillRoundRect(sb, 8.0f, WithAlpha(pal.paperHi, 0.97f));
    cv.StrokeRoundRect(sb, 8.0f, pal.rule, shape::kHair);

    float yy = sby + 14.0f;
    TextStyle lb; lb.size = 10.5f; lb.role = FontRole::Mono; lb.letterSpacing = 2.0f;
    lb.weight = DWRITE_FONT_WEIGHT_BOLD;

    // 题集勾选列表（N1：勾选集合驱动可见性）
    cv.Text(L"题 集", { sbx + 14.0f, yy, sbx + sbW - 14.0f, yy + 18.0f }, lb, pal.ink500);
    yy += 24.0f;
    m_sbChkRects.clear();
    for (size_t i = 0; i < m_sets.size() && yy < sby + sbH - 200.0f; ++i) {
        bool vis = false;
        for (int vi : m_visibleSets) if (vi == (int)i) { vis = true; break; }
        D2D1_RECT_F row{ sbx + 14.0f, yy, sbx + sbW - 14.0f, yy + 30.0f };
        m_sbChkRects.push_back(row);
        // checkbox 方块
        D2D1_RECT_F chk{ row.left + 4.0f, row.top + 6.0f, row.left + 22.0f, row.top + 24.0f };
        cv.FillRoundRect(chk, 3.0f, vis ? pal.seal : pal.paperLo);
        cv.StrokeRoundRect(chk, 3.0f, vis ? pal.seal : pal.rule, shape::kHair);
        TextStyle rt; rt.size = 13.0f; rt.role = FontRole::Sans; rt.vAlign = VAlign::Middle;
        rt.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(m_sets[i].name, { chk.right + 8.0f, row.top, row.right, row.bottom }, rt,
                vis ? pal.ink900 : pal.ink300);
        yy += 34.0f;
    }
    // 新建题集（纯文字按钮）
    {
        D2D1_RECT_F r{ sbx + 14.0f, yy, sbx + 130.0f, yy + 28.0f };
        m_newSetBtn = r;   // 侧边栏：新建题集命中
        cv.FillRoundRect(r, 5.0f, WithAlpha(pal.seal, 0.1f));
        cv.StrokeRoundRect(r, 5.0f, WithAlpha(pal.seal, 0.6f), shape::kHair);
        TextStyle nt; nt.size = 12.0f; nt.role = FontRole::Sans;
        nt.hAlign = HAlign::Center; nt.vAlign = VAlign::Middle;
        cv.Text(L"新建题集", r, nt, pal.seal);
        yy += 36.0f;
    }

    // 聚焦题集的盒管理（N5：新建一律普通题盒；N3 纯文字按钮）
    if (m_focusSet >= 0 && m_focusSet < (int)m_sets.size()) {
        const auto& st = m_sets[m_focusSet];
        cv.Text(L"题 盒", { sbx + 14.0f, yy, sbx + sbW - 14.0f, yy + 18.0f }, lb, pal.ink500);
        yy += 24.0f;
        m_sbBoxRows.clear(); m_sbBoxRenR.clear(); m_sbBoxDelR.clear();
        for (size_t i = 0; i < st.boxes.size() && yy < sby + sbH - 140.0f; ++i) {
            D2D1_RECT_F row{ sbx + 14.0f, yy, sbx + sbW - 14.0f, yy + 30.0f };
            m_sbBoxRows.push_back(row);
            TextStyle rt; rt.size = 12.5f; rt.role = FontRole::Sans; rt.vAlign = VAlign::Middle;
            cv.Text(st.boxes[i].name, { row.left + 4.0f, row.top, row.right - 96.0f, row.bottom },
                    rt, pal.ink700);
            D2D1_RECT_F ren{ row.right - 90.0f, row.top + 3.0f, row.right - 48.0f, row.bottom - 3.0f };
            D2D1_RECT_F del{ row.right - 44.0f, row.top + 3.0f, row.right - 4.0f, row.bottom - 3.0f };
            m_sbBoxRenR.push_back(ren);
            m_sbBoxDelR.push_back(del);
            TextStyle bt2; bt2.size = 11.0f; bt2.role = FontRole::Sans;
            bt2.hAlign = HAlign::Center; bt2.vAlign = VAlign::Middle;
            cv.Text(L"改名", ren, bt2, pal.ink500);
            cv.Text(L"删除", del, bt2, pal.vermilion);
            yy += 34.0f;
        }
        // 新建题盒（N5：一律普通题盒，无 kind 交替）
        m_sbNewBoxBtn = { sbx + 14.0f, yy, sbx + 140.0f, yy + 28.0f };
        cv.FillRoundRect(m_sbNewBoxBtn, 5.0f, WithAlpha(pal.jade, 0.1f));
        cv.StrokeRoundRect(m_sbNewBoxBtn, 5.0f, WithAlpha(pal.jade, 0.6f), shape::kHair);
        TextStyle nt; nt.size = 12.0f; nt.role = FontRole::Sans;
        nt.hAlign = HAlign::Center; nt.vAlign = VAlign::Middle;
        cv.Text(L"新建题盒", m_sbNewBoxBtn, nt, pal.jade);
        yy += 36.0f;
    }

    // 搜索 + 展现
    m_sbSearchBox = { sbx + 14.0f, yy, sbx + sbW - 96.0f, yy + 30.0f };
    m_sbSearchGo  = { sbx + sbW - 90.0f, yy, sbx + sbW - 14.0f, yy + 30.0f };
    if (m_searchActive) {
        cv.FillRoundRect(m_sbSearchBox, 5.0f, pal.paperHi);
        cv.StrokeRoundRect(m_sbSearchBox, 5.0f, WithAlpha(pal.seal, 0.95f), shape::kStroke);
        TextStyle st9; st9.size = 12.5f; st9.role = FontRole::Sans; st9.vAlign = VAlign::Middle;
        m_search.Paint(cv, { m_sbSearchBox.left + 10.0f, m_sbSearchBox.top,
                            m_sbSearchBox.right - 10.0f, m_sbSearchBox.bottom },
                       st9, pal.ink900, L"搜索题面/答案/标签…", pal.ink300, 0.0f, 0.0f);
    } else {
        cv.FillRoundRect(m_sbSearchBox, 5.0f, pal.paperLo);
        cv.StrokeRoundRect(m_sbSearchBox, 5.0f, WithAlpha(pal.rule, 0.5f), shape::kHair);
        TextStyle ph; ph.size = 12.0f; ph.role = FontRole::Sans; ph.vAlign = VAlign::Middle;
        cv.Text(m_searchStr.empty() ? L"搜索题面/答案/标签…" : m_searchStr,
                { m_sbSearchBox.left + 10.0f, m_sbSearchBox.top, m_sbSearchBox.right, m_sbSearchBox.bottom },
                ph, m_searchStr.empty() ? pal.ink300 : pal.ink900);
    }
    cv.FillRoundRect(m_sbSearchGo, 5.0f, pal.seal);
    TextStyle sbt; sbt.size = 12.0f; sbt.role = FontRole::Sans;
    sbt.hAlign = HAlign::Center; sbt.vAlign = VAlign::Middle;
    cv.Text(L"搜索", m_sbSearchGo, sbt, pal.paperHi);
    m_sbShowBtn = { sbx + 14.0f, yy + 38.0f, sbx + sbW - 14.0f, yy + 66.0f };
    bool canShow = !m_searchHits.empty();
    cv.FillRoundRect(m_sbShowBtn, 5.0f, canShow ? WithAlpha(pal.jade, 0.9f) : WithAlpha(pal.rule, 0.12f));
    cv.Text(L"展开命中卡片", m_sbShowBtn, sbt, canShow ? pal.paperHi : pal.ink300);
    yy += 76.0f;

    // 排列
    cv.Text(L"排列", { sbx + 14.0f, yy, sbx + sbW - 14.0f, yy + 18.0f }, lb, pal.ink500);
    const wchar_t* sortNames[3] = { L"时间", L"难度", L"热度" };
    for (int i = 0; i < 3; ++i) {
        m_sbSortR[i] = { sbx + 14.0f + i * 76.0f, yy + 22.0f, sbx + 14.0f + i * 76.0f + 70.0f, yy + 50.0f };
        bool on = (m_sortMode == i + 1);
        cv.FillRoundRect(m_sbSortR[i], 5.0f, on ? WithAlpha(pal.seal, 0.9f) : pal.paperLo);
        cv.StrokeRoundRect(m_sbSortR[i], 5.0f, on ? pal.seal : pal.rule, shape::kHair);
        cv.Text(sortNames[i], m_sbSortR[i], sbt, on ? pal.paperHi : pal.ink700);
    }
    yy += 60.0f;

    // 聚焦
    m_sbFocusBtn = { sbx + 14.0f, yy, sbx + sbW - 14.0f, yy + 28.0f };
    cv.FillRoundRect(m_sbFocusBtn, 5.0f, m_focusOn ? WithAlpha(pal.seal, 0.9f) : pal.paperLo);
    cv.StrokeRoundRect(m_sbFocusBtn, 5.0f, m_focusOn ? pal.seal : pal.rule, shape::kHair);
    cv.Text(m_focusOn ? L"聚焦已开（跟随鼠标）" : L"开启聚焦放大",
            m_sbFocusBtn, sbt, m_focusOn ? pal.paperHi : pal.ink700);
}

// ============================================================
//  制卡弹窗（v3：目标盒直指定；N7 弹窗中心跟随聚焦恒星）
// ============================================================
D2D1_POINT_2F QuizBoxView::EditorCenter() const
{
    if (m_focusSet >= 0 && !m_setCenters.empty())
        return m_setCenters.front();   // 聚焦态：可见恒星即聚焦题集
    return { (m_area.left + m_area.right) * 0.5f, (m_area.top + m_area.bottom) * 0.5f };
}

void QuizBoxView::OpenCardEditor(const QCard* edit, const std::wstring& targetBox)
{
    m_ceOpen = true;
    m_ceField = 0;
    m_ceTargetBox = targetBox;
    if (edit) {
        m_ceEditId = edit->id;
        m_ceFront = edit->front; m_ceBack = edit->back; m_ceTag = edit->tag;
        m_ceDiff = edit->difficulty;
    } else {
        m_ceEditId.clear();
        m_ceFront.clear(); m_ceBack.clear(); m_ceTag.clear();
        m_ceDiff = 3;
    }
    m_viewT = 0.0f;
    m_edit.Begin(m_ceFront, false, 14.0f);
    m_edit.onEnter     = [this] { CeNext(); };
    m_edit.onEsc       = [this] { CeCancel(); };
    m_edit.onKillFocus = [this] {};
}

void QuizBoxView::CeNext()
{
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

void QuizBoxView::CeCancel()
{
    m_edit.Cancel();
    m_ceOpen = false;
}

void QuizBoxView::CeCommit(bool keepOpen)
{
    std::wstring t;
    m_edit.End(true, t);
    if (m_ceField == 0) m_ceFront = t;
    else if (m_ceField == 1) m_ceBack = t;
    else m_ceTag = t;

    if (!m_ceTargetBox.empty()) {
        QCard c;
        c.id = m_ceEditId.empty() ? (L"qc_" + std::to_wstring(GetTickCount64())) : m_ceEditId;
        c.front = m_ceFront; c.back = m_ceBack; c.tag = m_ceTag;
        c.difficulty = m_ceDiff;
        c.added = (long long)time(nullptr);
        if (m_ceEditId.empty()) {
            BoxStore::Instance().AddCard(m_ceTargetBox, c);
            Toast(keepOpen ? L"已收进盒中，继续制卡" : L"已收进盒中");
        } else {
            BoxStore::Instance().UpdateCard(m_ceTargetBox, c);
            Toast(L"已保存修改");
        }
        AfterTreeChange();
    }
    if (keepOpen) {
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

// 崩溃防线：任何树修改后调用（N2）
void QuizBoxView::AfterTreeChange()
{
    m_sets = BoxStore::Instance().Load();
    m_nebCards.clear();
    m_nebDragIdx = -1; m_nebDropBox = -1;
    m_hoverNeb = -1;
    m_nebDrag = false;
    m_expOpen = false;
}

// ============================================================
//  更新
// ============================================================
void QuizBoxView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    m_viewT += dt;
    m_newT += dt;
    if (m_expT < 1.0f) m_expT += dt;
    if (m_expFlipT < 1.0f) m_expFlipT += dt;
    if (m_toastT > 0.0f) m_toastT = (std::max)(0.0f, m_toastT - dt);

    float s = ScrollY();
    float mx = in.mouseX, my = in.mouseY + s;

    // G4 导入预览弹窗独占
    if (m_impOpen) {
        if (in.clicked) {
            if (InRect(m_impCancelR, mx, my)) { m_impOpen = false; return; }
            if (InRect(m_impOkR, mx, my)) { DoImport(); return; }
            if (!InRect(m_impCard, mx, my)) { m_impOpen = false; return; }
        }
        if (in.keyDown[VK_ESCAPE]) { m_impOpen = false; return; }
        return;
    }

    // 制卡弹窗独占
    if (m_ceOpen) {
        if (m_cv) {
            TextStyle ft; ft.size = 14.0f; ft.role = FontRole::Sans; ft.vAlign = VAlign::Middle;
            const D2D1_RECT_F& box = m_ceField == 0 ? m_ceFrontR :
                                     m_ceField == 1 ? m_ceBackR : m_ceTagR;
            m_edit.HandleMouse(in, *m_cv, { box.left + 12.0f, box.top, box.right - 12.0f, box.bottom },
                               ft, ScrollY(), 0.0f);
        }
        if (in.clicked) {
            for (int d = 0; d < 5; ++d)
                if (InRect(m_ceDiffR[d], mx, my)) { m_ceDiff = d + 1; return; }
            if (InRect(m_ceFrontR, mx, my) && m_ceField != 0) { CeSwitchField(0); return; }
            if (InRect(m_ceBackR, mx, my) && m_ceField != 1) { CeSwitchField(1); return; }
            if (InRect(m_ceTagR, mx, my) && m_ceField != 2) { CeSwitchField(2); return; }
            if (InRect(m_ceCancelR, mx, my)) { CeCancel(); return; }
            if (InRect(m_ceSaveR, mx, my)) { CeCommit(false); return; }
            if (InRect(m_ceSaveMoreR, mx, my)) { CeCommit(true); return; }
            if (!InRect(m_ceCard, mx, my)) { CeCancel(); return; }
        }
        if (in.keyDown[VK_ESCAPE]) { CeCancel(); return; }
        return;
    }

    // ---- 星云交互 ----
    // 聚焦中心跟随鼠标
    m_focusX = in.mouseX;
    m_focusY = in.mouseY;

    // 聚焦反算（世界坐标）
    float k = m_focusOn ? 1.8f : 1.0f;
    float wx = (in.mouseX - m_focusX) / k + m_focusX;
    float wy = (in.mouseY - m_focusY) / k + m_focusY;
    if (!m_focusOn) { wx = in.mouseX; wy = in.mouseY; }

    // 搜索框编辑态
    if (m_searchActive) {
        if (m_cv) {
            TextStyle st9; st9.size = 12.5f; st9.role = FontRole::Sans; st9.vAlign = VAlign::Middle;
            m_search.HandleMouse(in, *m_cv,
                                 { m_sbSearchBox.left + 10.0f, m_sbSearchBox.top,
                                   m_sbSearchBox.right - 10.0f, m_sbSearchBox.bottom },
                                 st9, 0.0f, 0.0f);
        }
        if (in.clicked && !InRect(m_sbSearchBox, mx, my) &&
            !InRect(m_sbSearchGo, mx, my) && !InRect(m_sbShowBtn, mx, my)) {
            std::wstring t;
            m_search.End(true, t);
            m_searchStr = t;
            m_searchActive = false;
            ApplySearch();
        }
        return;
    }

    // 改名独占
    if (m_renActive) {
        if (m_cv && m_renBox2.right > m_renBox2.left) {
            TextStyle ns; ns.role = FontRole::Serif; ns.size = 20.0f;
            ns.weight = DWRITE_FONT_WEIGHT_BOLD; ns.vAlign = VAlign::Middle;
            m_ren.HandleMouse(in, *m_cv, m_renBox2, ns, ScrollY(), 0.0f);
            if (in.clicked && !InRect(m_renBox2, mx, my)) {
                std::wstring t;
                m_ren.End(true, t);
                m_renActive = false;
                if (!t.empty() && m_renBox >= 0 && m_renBox < (int)m_sets.size()) {
                    m_sets[m_renBox].name = t;
                    BoxStore::Instance().RenameSet(m_sets[m_renBox].id, t);
                }
            }
        }
        if (in.keyDown[VK_ESCAPE]) { m_ren.Cancel(); m_renActive = false; }
        return;
    }

    // 展开卡独占（N4）
    if (m_expOpen) {
        if (in.clicked) {
            if (InRect(m_expFlipBtn, mx, my)) { m_expFlip = !m_expFlip; m_expFlipT = 0.0f; return; }
            if (m_expFlip) {
                if (InRect(m_expRemBtn, mx, my) || InRect(m_expWrongBtn, mx, my)) {
                    if (m_expBox >= 0 && m_expBox < (int)m_sets.size()) {
                        auto& st9 = m_sets[m_expBox];
                        if (m_expCard >= 0 && m_expCard < (int)st9.boxes.size()) {
                            auto& b2 = st9.boxes[m_expCard];
                            if (m_expCard2 >= 0 && m_expCard2 < (int)b2.cards.size()) {
                                QCard c2 = b2.cards[m_expCard2];
                                if (InRect(m_expRemBtn, mx, my)) {
                                    c2.lastReview = (long long)time(nullptr);
                                    Toast(L"已记录：记住了");
                                } else {
                                    c2.wrongCount += 1;
                                    c2.lastReview = (long long)time(nullptr);
                                    Toast(L"已记录：答错了（卡片将变红）");
                                }
                                BoxStore::Instance().UpdateCard(b2.id, c2);
                                AfterTreeChange();
                                m_expOpen = true; m_expT = 1.0f;   // 保持展开
                            }
                        }
                    }
                    return;
                }
            }
            // 点空白收回
            m_expOpen = false;
            return;
        }
        if (in.keyDown[VK_ESCAPE]) { m_expOpen = false; return; }
        return;
    }

    // ---- T9 星云拖拽 ----
    {
        if (in.pressed && !m_nebDrag && m_hoverNeb >= 0) {
            m_nebDragIdx = m_hoverNeb;
            m_nebDragX = wx; m_nebDragY = wy;
            m_dragStartX = wx; m_dragStartY = wy;
        }
        if (m_nebDragIdx >= 0 && !m_nebDrag && in.pressed &&
            (std::fabs(wx - m_dragStartX) > 8.0f || std::fabs(wy - m_dragStartY) > 8.0f))
            m_nebDrag = true;
        if (m_nebDrag) {
            m_nebDragX = wx; m_nebDragY = wy;
            m_nebDropBox = -1;
            // 椭圆带命中（按所属题集局部坐标）
            if (m_nebDragIdx >= 0 && m_nebDragIdx < (int)m_nebCards.size()) {
                const auto& dc0 = m_nebCards[m_nebDragIdx];
                if (dc0.setIdx >= 0 && dc0.setIdx < (int)m_sets.size()) {
                    const auto& st5 = m_sets[dc0.setIdx];
                    D2D1_POINT_2F ctr{ (m_area.left + m_area.right) * 0.5f,
                                       (m_area.top + m_area.bottom) * 0.5f + m_area.top * 0.0f };
                    // v3：恒星中心取 SetCenters 结果（聚焦态单中心）
                    if (!m_setCenters.empty()) ctr = m_setCenters.front();
                    for (int bi = 0; bi < (int)st5.boxes.size(); ++bi) {
                        float rx = (m_focusSet >= 0 ? 150.0f : 92.0f) + bi * 58.0f;
                        float ry = rx * 0.30f;
                        float v = ((wx - ctr.x) / rx) * ((wx - ctr.x) / rx)
                                + ((wy - ctr.y) / ry) * ((wy - ctr.y) / ry);
                        if (v > 0.62f && v < 1.45f) { m_nebDropBox = bi; break; }
                    }
                }
            }
            if (in.released) {
                bool moved = false;
                if (m_nebDropBox >= 0 && m_nebDragIdx >= 0 && m_nebDragIdx < (int)m_nebCards.size()) {
                    const auto& nc5 = m_nebCards[m_nebDragIdx];
                    if (nc5.setIdx >= 0 && nc5.setIdx < (int)m_sets.size()) {
                        const auto& st5 = m_sets[nc5.setIdx];
                        if (nc5.boxIdx >= 0 && nc5.boxIdx < (int)st5.boxes.size()
                            && nc5.cardIdx >= 0 && nc5.cardIdx < (int)st5.boxes[nc5.boxIdx].cards.size()) {
                            const auto& fromId = st5.boxes[nc5.boxIdx].id;
                            const auto& cardId = st5.boxes[nc5.boxIdx].cards[nc5.cardIdx].id;
                            if (m_focusSet >= 0 && m_focusSet < (int)m_sets.size()) {
                                const auto& stT = m_sets[m_focusSet];
                                if (m_nebDropBox < (int)stT.boxes.size()) {
                                    const auto& toId = stT.boxes[m_nebDropBox].id;
                                    if (fromId != toId) {
                                        BoxStore::Instance().MoveCard(fromId, cardId, toId);
                                        AfterTreeChange();   // N2 崩溃防线：清缓存
                                        Toast(L"已归入：" + stT.boxes[m_nebDropBox].name);
                                        moved = true;
                                    }
                                }
                            }
                        }
                    }
                }
                (void)moved;
                m_nebDrag = false; m_nebDragIdx = -1; m_nebDropBox = -1;
            }
            return;   // 拖拽中独占
        }
    }

    // hover
    m_hoverNeb = -1;
    for (size_t i = 0; i < m_nebCards.size(); ++i) {
        const auto& r = m_nebCards[i].rect;
        if (wx >= r.left - 4.0f && wx <= r.right + 4.0f &&
            wy >= r.top - 6.0f && wy <= r.bottom + 6.0f) {
            m_hoverNeb = (int)i;
            m_overInteractive = true;
            break;
        }
    }
    if (m_hoverNeb < 0) m_orbit += dt * 0.10f;

    if (!in.clicked) return;

    // 恒星点击（N1：聚焦 / 取消聚焦）
    for (size_t i = 0; i < m_setHitR.size(); ++i) {
        if (InRect(m_setHitR[i], mx, my)) {
            int si = i < m_visibleSets.size() ? m_visibleSets[i] : -1;
            if (m_focusSet == si) { RebuildVisible(); Toast(L"已回到全览"); }
            else {
                m_focusSet = si;
                m_visibleSets.clear();
                m_visibleSets.push_back(si);
                m_focusOn = false;
            }
            m_viewT = 0.0f;
            return;
        }
    }

    // 侧边栏
    if (InRect(m_impBtn, mx, my)) { BrowseImport(); return; }
    for (size_t i = 0; i < m_sbChkRects.size(); ++i) {
        if (InRect(m_sbChkRects[i], mx, my)) {
            // 勾选切换
            int si = (int)i;
            bool vis = false;
            for (int vi : m_visibleSets) if (vi == si) { vis = true; break; }
            if (vis) {
                if (m_visibleSets.size() > 1) {
                    std::vector<int> nv;
                    for (int vi : m_visibleSets) if (vi != si) nv.push_back(vi);
                    m_visibleSets = nv;
                    if (m_focusSet == si) m_focusSet = -1;
                }
            } else {
                m_visibleSets.push_back(si);
            }
            return;
        }
    }
    if (InRect(m_newSetBtn, mx, my)) {
        // 新建题集
        int n = (int)m_sets.size();
        wchar_t nb[40];
        swprintf_s(nb, L"新题集 %d", n + 1);
        BoxStore::Instance().AddSet(nb);
        AfterTreeChange();
        Toast(L"已创建题集");
        return;
    }
    // 聚焦态盒管理
    if (m_focusSet >= 0 && m_focusSet < (int)m_sets.size()) {
        const auto& st = m_sets[m_focusSet];
        for (size_t i = 0; i < m_sbBoxDelR.size(); ++i) {
            if (InRect(m_sbBoxDelR[i], mx, my) && i < st.boxes.size()) {
                BoxStore::Instance().DeleteBox(st.boxes[i].id);
                AfterTreeChange();
                Toast(L"已删除题盒");
                return;
            }
        }
        for (size_t i = 0; i < m_sbBoxRenR.size(); ++i) {
            if (InRect(m_sbBoxRenR[i], mx, my) && i < st.boxes.size()) {
                m_ceTargetBox = st.boxes[i].id;   // 借用：改名走 FieldEdit 简化——此处直接 Begin
                m_ren.Begin(st.boxes[i].name, false, 13.0f);
                m_renActive = true;
                m_renBox = (int)i;   // 复用字段：盒索引（针对聚焦题集）
                return;
            }
        }
        if (InRect(m_sbNewBoxBtn, mx, my)) {
            wchar_t nb2[40];
            swprintf_s(nb2, L"新题盒 %d", (int)st.boxes.size() + 1);
            BoxStore::Instance().AddBox(st.id, nb2, 0);   // N5：一律普通题盒
            AfterTreeChange();
            Toast(L"已创建题盒（可在侧边栏改名）");
            return;
        }
    }
    if (InRect(m_sbSearchBox, mx, my)) {
        m_search.Begin(m_searchStr, false, 12.5f);
        m_search.onEnter     = [this] {
            std::wstring t; m_search.End(true, t);
            m_searchStr = t; m_searchActive = false; ApplySearch(); };
        m_search.onEsc       = [this] { m_search.Cancel(); m_searchActive = false; };
        m_search.onKillFocus = [this] {
            std::wstring t; m_search.End(true, t);
            m_searchStr = t; m_searchActive = false; ApplySearch(); };
        m_searchActive = true;
        return;
    }
    if (InRect(m_sbSearchGo, mx, my)) { ApplySearch(); return; }
    if (InRect(m_sbShowBtn, mx, my)) { EnterShowMode(); return; }
    for (int i = 0; i < 3; ++i)
        if (InRect(m_sbSortR[i], mx, my)) {
            m_sortMode = (m_sortMode == i + 1) ? 0 : (i + 1);
            return;
        }
    if (InRect(m_sbFocusBtn, mx, my)) { m_focusOn = !m_focusOn; return; }

    // 点中小卡 → 展开卡（N4）
    if (m_hoverNeb >= 0 && m_hoverNeb < (int)m_nebCards.size()) {
        const auto& nc = m_nebCards[m_hoverNeb];
        if (nc.setIdx >= 0 && nc.setIdx < (int)m_sets.size()
            && nc.boxIdx >= 0 && nc.boxIdx < (int)m_sets[nc.setIdx].boxes.size()
            && nc.cardIdx >= 0 && nc.cardIdx < (int)m_sets[nc.setIdx].boxes[nc.boxIdx].cards.size()) {
            m_expBox = nc.setIdx;
            m_expCard = nc.boxIdx;
            m_expCard2 = nc.cardIdx;
            m_expOpen = true;
            m_expT = 0.0f;
            m_expFlip = false;
            return;
        }
    }
    // 点空白轻推公转
    m_orbit += 0.35f;
}

// G6 T11：搜索
void QuizBoxView::ApplySearch()
{
    m_searchHits.clear();
    if (m_searchStr.empty()) return;
    std::wstring key = m_searchStr;
    for (auto& c : key) c = (wchar_t)towlower(c);
    for (size_t si = 0; si < m_sets.size(); ++si)
        for (size_t bi = 0; bi < m_sets[si].boxes.size(); ++bi)
            for (size_t ci = 0; ci < m_sets[si].boxes[bi].cards.size(); ++ci) {
                const auto& cd = m_sets[si].boxes[bi].cards[ci];
                std::wstring hay = cd.front + L"\n" + cd.back + L"\n" + cd.tag;
                for (auto& c : hay) c = (wchar_t)towlower(c);
                if (hay.find(key) != std::wstring::npos)
                    m_searchHits.push_back({ (int)si, { (int)bi, (int)ci } });
            }
}

void QuizBoxView::EnterShowMode()
{
    ApplySearch();
    if (m_searchHits.empty()) { Toast(L"没有命中的卡片"); return; }
    m_showMode = true;
    m_showT = 0.0f;
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

    // 展现模式（搜索结果网格）
    if (m_showMode) {
        float availW2 = m_area.right - m_area.left - 300.0f;
        float gx = m_area.left + 28.0f;
        float gy = m_area.top + 66.0f;
        TextStyle stt; stt.role = FontRole::Mono; stt.size = 10.5f; stt.letterSpacing = 2.0f;
        stt.weight = DWRITE_FONT_WEIGHT_BOLD;
        wchar_t hb[64];
        swprintf_s(hb, L"搜索结果 · %d 张（点击卡片展开）", (int)m_searchHits.size());
        cv.Text(hb, { gx, m_area.top + 24.0f, gx + availW2, m_area.top + 44.0f }, stt, pal.seal);
        auto dc9 = [&](int d) -> D2D1_COLOR_F {
            switch (d) {
            case 1: case 2: return pal.jade;
            case 3: return pal.brass;
            case 4: return pal.seal;
            default: return pal.vermilion;
            }
        };
        int cols = (std::max)(2, (int)(availW2 / 236.0f));
        for (size_t i = 0; i < m_searchHits.size(); ++i) {
            float kk = Clamp01((m_showT - 0.06f * (float)i) / 0.3f);
            if (kk <= 0.0f) break;
            int rr2 = (int)i / cols, c2 = (int)i % cols;
            float bx = gx + c2 * 236.0f, by = gy + rr2 * 138.0f;
            D2D1_RECT_F card{ bx, by, bx + 226.0f, by + 128.0f };
            cv.PushOpacity(kk);
            cv.PaperCard(card, 1.5f);
            const auto& pr = m_searchHits[i];
            if (pr.first < (int)m_sets.size() && pr.second.first < (int)m_sets[pr.first].boxes.size()
                && pr.second.second < (int)m_sets[pr.first].boxes[pr.second.first].cards.size()) {
                const auto& cd = m_sets[pr.first].boxes[pr.second.first].cards[pr.second.second];
                cv.FillRect({ card.left, card.top, card.left + 3.0f, card.bottom },
                            WithAlpha(dc9(cd.difficulty), 0.85f));
                TextStyle f9; f9.size = 12.5f; f9.role = FontRole::Sans;
                f9.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
                std::wstring fr = cd.front;
                if (fr.size() > 30) fr = fr.substr(0, 30) + L"…";
                cv.Text(fr, { card.left + 14.0f, card.top + 10.0f, card.right - 14.0f, card.top + 34.0f }, f9, pal.ink900);
                TextStyle m9; m9.size = 10.5f; m9.role = FontRole::Mono; m9.vAlign = VAlign::Middle;
                wchar_t lb[96];
                swprintf_s(lb, L"%s · D%d · 错%d", m_sets[pr.first].boxes[pr.second.first].name.c_str(),
                           cd.difficulty, cd.wrongCount);
                cv.Text(lb, { card.left + 14.0f, card.bottom - 30.0f, card.right - 14.0f, card.bottom - 12.0f }, m9, pal.ink500);
            }
            cv.PopOpacity();
        }
        DrawSidebar(cv);
        cv.PopTransform();
        cv.PopClip();
        // Toast
        if (m_toastT > 0.0f) {
            cv.PushOpacity(Clamp01(m_toastT / 0.5f));
            D2D1_RECT_F r{ m_area.right - 288.0f, m_area.bottom - 68.0f, m_area.right - 28.0f, m_area.bottom - 28.0f };
            cv.FillRoundRect(r, 8.0f, pal.seal);
            TextStyle ts; ts.size = 12.5f; ts.role = FontRole::Sans;
            ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
            cv.Text(m_toast, r, ts, pal.paperHi);
            cv.PopOpacity();
        }
        return;
    }

    DrawNebula(cv, s);
    DrawSidebar(cv);

    // 展开卡（最上层）
    if (m_expOpen) DrawExpand(cv);

    // 制卡弹窗（最上层）
    if (m_ceOpen) {
        float W = m_area.right - m_area.left;
        float H = m_area.bottom - m_area.top;
        cv.FillRect(m_area, WithAlpha(pal.ink900, 0.5f * Clamp01(m_viewT / 0.22f)));
        D2D1_POINT_2F ctr = EditorCenter();   // N7：聚焦恒星位置
        float cw = (std::min)(560.0f, W - 80.0f);
        float chh = 470.0f;
        float px = ctr.x - cw * 0.5f, py = ctr.y - chh * 0.5f;
        if (px < m_area.left + 12.0f) px = m_area.left + 12.0f;
        if (py < m_area.top + 12.0f) py = m_area.top + 12.0f;
        if (px + cw > m_area.right - 290.0f) px = m_area.right - 290.0f - cw;
        if (py + chh > m_area.bottom - 12.0f) py = m_area.bottom - 12.0f - chh;

        cv.PushOpacity(Clamp01(m_viewT / 0.22f));
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
            TextStyle lb9; lb9.role = FontRole::Mono; lb9.size = 10.5f; lb9.letterSpacing = 2.0f;
            lb9.weight = DWRITE_FONT_WEIGHT_BOLD;
            cv.Text(label, { ix, box.top - 20.0f, ir, box.top - 4.0f }, lb9,
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
        {
            float dy = m_ceCard.top + 76.0f + 3 * 78.0f;
            TextStyle lb9; lb9.role = FontRole::Mono; lb9.size = 10.5f; lb9.letterSpacing = 2.0f;
            lb9.weight = DWRITE_FONT_WEIGHT_BOLD;
            cv.Text(L"难 度", { ix, dy - 20.0f, ir, dy - 4.0f }, lb9, pal.ink500);
            auto dc10 = [&](int d) -> D2D1_COLOR_F {
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
                cv.FillRoundRect(m_ceDiffR[d], 6.0f, on ? WithAlpha(dc10(d + 1), 0.9f)
                                                        : WithAlpha(pal.rule, 0.12f));
                cv.StrokeRoundRect(m_ceDiffR[d], 6.0f, on ? dc10(d + 1) : pal.rule, shape::kHair);
                TextStyle dt2; dt2.size = 13.0f; dt2.role = FontRole::Mono;
                dt2.hAlign = HAlign::Center; dt2.vAlign = VAlign::Middle;
                wchar_t db[8]; swprintf_s(db, L"D%d", d + 1);
                cv.Text(db, m_ceDiffR[d], dt2, on ? pal.paperHi : pal.ink500);
            }
        }
        float by = m_ceCard.bottom - 62.0f;
        m_ceCancelR   = { m_ceCard.left + 26.0f, by, m_ceCard.left + 26.0f + 96.0f, by + 42.0f };
        m_ceSaveMoreR = { m_ceCard.right - 26.0f - 232.0f, by, m_ceCard.right - 26.0f - 120.0f, by + 42.0f };
        m_ceSaveR     = { m_ceCard.right - 26.0f - 106.0f, by, m_ceCard.right - 26.0f, by + 42.0f };
        auto Btn = [&](const D2D1_RECT_F& r, const wchar_t* label, int style) {
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

    // G4 导入预览弹窗
    if (m_impOpen) {
        float W = m_area.right - m_area.left;
        float H = m_area.bottom - m_area.top;
        cv.FillRect(m_area, WithAlpha(pal.ink900, 0.5f));
        float cw2 = (std::min)(620.0f, W - 80.0f);
        float ch2 = (std::min)(470.0f, H - 60.0f);
        float px2 = (W - cw2) * 0.5f, py2 = (H - ch2) * 0.5f;
        m_impCard = { px2, py2, px2 + cw2, py2 + ch2 };
        cv.PaperCard(m_impCard, 0.4f, shape::kEdge);
        cv.DoubleFrame(m_impCard, WithAlpha(pal.seal, 0.8f));
        TextStyle ttl; ttl.role = FontRole::Serif; ttl.size = 20.0f;
        ttl.weight = DWRITE_FONT_WEIGHT_BOLD; ttl.letterSpacing = 1.4f;
        cv.Text(L"导 入 题 库", { px2 + 26.0f, py2 + 18.0f, px2 + cw2 - 200.0f, py2 + 48.0f }, ttl, pal.ink900);
        size_t slash = m_impFile.find_last_of(L"\\/");
        std::wstring fname = slash == std::wstring::npos ? m_impFile : m_impFile.substr(slash + 1);
        TextStyle fs; fs.role = FontRole::Mono; fs.size = 10.5f; fs.hAlign = HAlign::Right;
        fs.vAlign = VAlign::Middle;
        cv.Text(fname, { px2 + cw2 - 280.0f, py2 + 20.0f, px2 + cw2 - 26.0f, py2 + 46.0f }, fs, pal.ink300);
        cv.PerforationH(px2 + 26.0f, px2 + cw2 - 26.0f, py2 + 62.0f, WithAlpha(pal.ruleStrong, 0.5f));
        float iy = py2 + 76.0f;
        int totalCards2 = 0;
        for (auto& ib : m_impBoxes) totalCards2 += (int)ib.cards.size();
        for (size_t i = 0; i < m_impBoxes.size() && iy < py2 + ch2 - 80.0f; ++i) {
            const auto& ib = m_impBoxes[i];
            TextStyle bn; bn.role = FontRole::Serif; bn.size = 16.0f;
            bn.weight = DWRITE_FONT_WEIGHT_BOLD;
            wchar_t bns[96];
            swprintf_s(bns, L"%s · %d 张", ib.name.c_str(), (int)ib.cards.size());
            cv.Text(bns, { px2 + 30.0f, iy, px2 + cw2 - 30.0f, iy + 24.0f }, bn, pal.ink900);
            iy += 26.0f;
            for (size_t kk = 0; kk < ib.cards.size() && kk < 2 && iy < py2 + ch2 - 80.0f; ++kk) {
                TextStyle pl; pl.size = 12.0f; pl.role = FontRole::Sans;
                std::wstring fr = ib.cards[kk].front;
                if (fr.size() > 40) fr = fr.substr(0, 40) + L"…";
                cv.Text(L"· " + fr, { px2 + 44.0f, iy, px2 + cw2 - 40.0f, iy + 20.0f }, pl, pal.ink500);
                iy += 20.0f;
            }
            if (ib.cards.size() > 2) {
                TextStyle et; et.size = 11.0f; et.role = FontRole::Mono;
                wchar_t em[48];
                swprintf_s(em, L"… 其余 %d 张", (int)ib.cards.size() - 2);
                cv.Text(em, { px2 + 44.0f, iy, px2 + cw2 - 40.0f, iy + 18.0f }, et, pal.ink300);
                iy += 22.0f;
            }
            iy += 8.0f;
        }
        float by2 = py2 + ch2 - 62.0f;
        m_impCancelR = { px2 + 26.0f, by2, px2 + 26.0f + 96.0f, by2 + 42.0f };
        m_impOkR     = { px2 + cw2 - 26.0f - 220.0f, by2, px2 + cw2 - 26.0f, by2 + 42.0f };
        cv.FillRoundRect(m_impCancelR, 6.0f, pal.paperLo);
        cv.StrokeRoundRect(m_impCancelR, 6.0f, pal.rule, shape::kHair);
        TextStyle bts2; bts2.size = 13.0f; bts2.role = FontRole::Sans;
        bts2.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bts2.hAlign = HAlign::Center; bts2.vAlign = VAlign::Middle; bts2.letterSpacing = 1.0f;
        cv.Text(L"取消", m_impCancelR, bts2, pal.ink700);
        cv.FillRoundRect(m_impOkR, 6.0f, pal.seal);
        wchar_t ok[64];
        swprintf_s(ok, L"导入 %d 盒 %d 张卡片", (int)m_impBoxes.size(), totalCards2);
        cv.Text(ok, m_impOkR, bts2, pal.paperHi);
    }

    cv.PopTransform();
    cv.PopClip();

    // Toast
    if (m_toastT > 0.0f) {
        cv.PushOpacity(Clamp01(m_toastT / 0.5f));
        D2D1_RECT_F r{ m_area.right - 288.0f, m_area.bottom - 68.0f, m_area.right - 28.0f, m_area.bottom - 28.0f };
        cv.FillRoundRect(r, 8.0f, pal.seal);
        TextStyle ts; ts.size = 12.5f; ts.role = FontRole::Sans;
        ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
        cv.Text(m_toast, r, ts, pal.paperHi);
        cv.PopOpacity();
    }
}

void QuizBoxView::BrowseImport()
{
    wchar_t path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"题库文件 (*.md;*.markdown;*.csv;*.txt)\0*.md;*.markdown;*.csv;*.txt\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"选择要导入的题库文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring src = ReadImportFile(path);
    if (src.empty()) { Toast(L"文件读取失败或为空"); return; }
    std::wstring p = path;
    size_t dot = p.find_last_of(L'.');
    std::wstring ext = dot == std::wstring::npos ? L"" : p.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    m_impBoxes = (ext == L".csv") ? ParseImportCsv(src) : ParseImportMd(src);
    int total = 0;
    for (auto& ib : m_impBoxes) total += (int)ib.cards.size();
    if (m_impBoxes.empty() || total == 0) {
        Toast(L"没有解析到卡片——检查格式（# 盒 / ## 题面 / - A: 答案）");
        return;
    }
    m_impFile = path;
    m_impOpen = true;
}

void QuizBoxView::DoImport()
{
    if (m_curSet < 0 || m_curSet >= (int)m_sets.size()) { m_impOpen = false; return; }
    const auto& st = m_sets[m_curSet];
    int total = 0;
    for (auto& ib : m_impBoxes) {
        std::wstring targetId;
        for (const auto& b : st.boxes)
            if (b.name == ib.name) { targetId = b.id; break; }
        if (targetId.empty()) {
            BoxStore::Instance().AddBox(st.id, ib.name.empty() ? L"导入题盒" : ib.name, ib.kind);
            auto sets2 = BoxStore::Instance().Load();
            for (const auto& s2 : sets2)
                if (s2.id == st.id)
                    for (const auto& b2 : s2.boxes)
                        if (b2.name == ib.name) { targetId = b2.id; break; }
        }
        if (!targetId.empty())
            for (const auto& c : ib.cards) {
                BoxStore::Instance().AddCard(targetId, c);
                ++total;
            }
    }
    AfterTreeChange();
    m_curSet = 0;
    m_impOpen = false;
    wchar_t msg[80];
    swprintf_s(msg, L"已导入 %d 张卡片", total);
    Toast(msg);
}

void QuizBoxView::DebugForcePreview()
{
    // 截图自检：空库造两层示例（题集 + 盒 + 卡），进星云
    if (m_sets.empty()) {
        BoxStore::Instance().AddSet(L"考公");
        m_sets = BoxStore::Instance().Load();
        if (!m_sets.empty()) {
            BoxStore::Instance().AddBox(m_sets.front().id, L"资料分析题盒", 0);
            BoxStore::Instance().AddBox(m_sets.front().id, L"常识判断题盒", 0);
            m_sets = BoxStore::Instance().Load();
            auto& st = m_sets.front();
            if (!st.boxes.empty()) {
                QCard c1; c1.id = L"qc_d1"; c1.front = L"甲乙两车相向而行，速度和 120km/h，300km 几小时相遇？";
                c1.back = L"300 ÷ 120 = 2.5 小时。"; c1.tag = L"行程问题";
                c1.difficulty = 2; c1.added = (long long)time(nullptr);
                QCard c2; c2.id = L"qc_d2"; c2.front = L"增长率比较：甲 8%、乙 12%，谁增速快？";
                c2.back = L"乙（12% > 8%）。"; c2.tag = L"资料分析";
                c2.difficulty = 4; c2.wrongCount = 2; c2.added = (long long)time(nullptr);
                QCard c3; c3.id = L"qc_d3"; c3.front = L"宪法是国家的根本大法。";
                c3.back = L"正确。"; c3.tag = L"常识";
                c3.difficulty = 1; c3.added = (long long)time(nullptr);
                BoxStore::Instance().AddCard(st.boxes.front().id, c1);
                BoxStore::Instance().AddCard(st.boxes.front().id, c2);
                if (st.boxes.size() > 1) BoxStore::Instance().AddCard(st.boxes.back().id, c3);
            }
        }
        m_sets = BoxStore::Instance().Load();
    }
    m_view = V_NEBULA;
    m_viewT = 1.0f;
    RebuildVisible();
}

void QuizBoxView::DebugForceOpen()
{
    DebugForcePreview();
    m_focusSet = 0;
    m_visibleSets.clear();
    m_visibleSets.push_back(0);
    m_focusOn = false;
}

} // namespace lj
