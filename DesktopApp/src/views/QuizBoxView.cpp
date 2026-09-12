// ============================================================
//  QuizBoxView.cpp — 题集卡片盒 v2 实现（批次 G2）
//  需求细节见 docs/题集卡片盒·开发文档.md（不得丢失）。
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

// ============================================================
//  G4 T6：导入解析（符号约定见 docs/题集卡片盒·开发文档.md）
//  md：# 盒名 / ## 题面 / - A: 答案 / - #: 标签 / - D: 难度
//  csv：题面,答案,标签,难度 每行一卡（首行为表头则跳过）
// ============================================================
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

// md → 盒列表（# 分盒；## 分卡；- A:/- #:/- D: 属性）
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
        if (l.size() >= 2 && l[0] == L'#' && l[1] != L'#') {          // 一级标题 = 新盒
            flushBox();
            curBox.name = TrimW(l.substr(1));
            if (StartsWithW(curBox.name, L" ")) curBox.name.erase(0, 1);
            continue;
        }
        if (StartsWithW(l, L"## ")) {                                  // 二级标题 = 新卡
            flushCard();
            curCard.front = TrimW(l.substr(3));
            curCard.difficulty = 3;
            inCard = true;
            continue;
        }
        if (!inCard) continue;
        if (StartsWithW(l, L"- A:") || StartsWithW(l, L"-A:")) {      // 答案
            size_t p = l.find(L':');
            curCard.back = TrimW(l.substr(p + 1));
            continue;
        }
        if (StartsWithW(l, L"- #:") || StartsWithW(l, L"-#:")) {      // 标签
            size_t p = l.find(L':');
            curCard.tag = TrimW(l.substr(p + 1));
            continue;
        }
        if (StartsWithW(l, L"- D:") || StartsWithW(l, L"-D:")) {      // 难度
            size_t p = l.find(L':');
            int d = _wtoi(l.c_str() + p + 1);
            if (d >= 1 && d <= 5) curCard.difficulty = d;
            continue;
        }
        // 普通行：无 ## 前导时并作题面续行（首卡前忽略）
        if (curCard.front.empty()) curCard.front = l;
        else curCard.front += L" " + l;
    }
    flushBox();
    return out;
}

// csv → 单盒（题面,答案,标签,难度）
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
        // 简单逗号切分（不处理引号内逗号——约定单元格不含逗号）
        std::vector<std::wstring> cols;
        std::wstring cur;
        for (wchar_t c : l) {
            if (c == L',') { cols.push_back(TrimW(cur)); cur.clear(); }
            else cur += c;
        }
        cols.push_back(TrimW(cur));
        if (first) {                                                  // 表头检测
            first = false;
            bool header = !cols.empty() && (cols[0] == L"题面" || cols[0] == L"front");
            if (header) continue;
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
    // BOM
    size_t off = 0;
    if (buf.size() >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB
        && (unsigned char)buf[2] == 0xBF) off = 3;
    std::string s = buf.substr(off);
    bool ascii = true;
    for (unsigned char c : s) if (c & 0x80) { ascii = false; break; }
    std::wstring w;
    if (ascii) {
        w.resize(s.size());
        for (size_t i = 0; i < s.size(); ++i) w[i] = (wchar_t)(unsigned char)s[i];
        return w;
    }
    // 尝试 UTF-8 → UTF-16（逐字节简化：交给 MultiByteToWideChar）
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    w.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

} // namespace

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

    // G5：星云入口（当前题集全部卡片轨道视图）
    m_nebBtn = { x0 + contentW - 170.0f, y0, x0 + contentW, y0 + 38.0f };
    cv.FillRoundRect(m_nebBtn, 6.0f, pal.jade);
    cv.Text(L"🌌 卡片星云", m_nebBtn, bt, pal.paperHi);

    // G4：导入题库入口（md/csv）
    m_impBtn = { x0 + contentW - 330.0f, y0, x0 + contentW - 182.0f, y0 + 38.0f };
    cv.FillRoundRect(m_impBtn, 6.0f, pal.paperLo);
    cv.StrokeRoundRect(m_impBtn, 6.0f, pal.rule, shape::kHair);
    cv.Text(L"📥 导入题库", m_impBtn, bt, pal.ink700);

    TextStyle h1; h1.role = FontRole::Serif; h1.size = 28.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 2.0f; h1.vAlign = VAlign::Middle;
    cv.Text(st.name, { x0 + 170.0f, y0 - 2.0f, x0 + contentW - 190.0f, y0 + 38.0f }, h1, pal.ink900);
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
            // G3 T10：红警示（重复答错 + 久未复习 → 边框渐变朱砂正红）
            float heat = CardHeat(cd);
            if (heat > 0.05f) {
                dc2.r = dc2.r + (pal.vermilion.r - dc2.r) * heat;
                dc2.g = dc2.g + (pal.vermilion.g - dc2.g) * heat;
                dc2.b = dc2.b + (pal.vermilion.b - dc2.b) * heat;
                dc2.a = 1.0f;
            }
            cv.PaperCard(row, hot ? 2.5f : 1.5f);
            cv.FillRect({ row.left, row.top, row.left + 3.0f, row.bottom }, WithAlpha(dc2, 0.85f));
            cv.StrokeRoundRect(row, shape::kEdge, WithAlpha(dc2, hot ? 1.0f : 0.45f), shape::kHair);
            if (heat > 0.5f) {
                // 高警示：卡面淡红 wash + ⚠ 标记
                cv.FillRoundRect(row, shape::kEdge, WithAlpha(pal.vermilion, 0.06f * heat));
                TextStyle wt; wt.size = 12.0f; wt.vAlign = VAlign::Middle;
                cv.Text(L"⚠", { row.left + 6.0f, row.top, row.left + 24.0f, row.bottom }, wt, pal.vermilion);
            }
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

    // ---- G3 T4：拖拽中「移动到题盒」浮层（顶部横排目标盒）----
    if (m_moveOpen && !m_moveBoxIds.empty()) {
        m_moveRects.clear();
        float fpx = x0, fpy = y0 + 62.0f;
        cv.FillRoundRect({ x0 - 8.0f, fpy - 8.0f, x0 + contentW + 8.0f, fpy + 52.0f },
                         6.0f, pal.paperHi);
        cv.StrokeRoundRect({ x0 - 8.0f, fpy - 8.0f, x0 + contentW + 8.0f, fpy + 52.0f },
                           6.0f, pal.seal, shape::kHair);
        TextStyle fl; fl.role = FontRole::Mono; fl.size = 10.0f; fl.letterSpacing = 1.5f;
        fl.weight = DWRITE_FONT_WEIGHT_BOLD; fl.vAlign = VAlign::Middle;
        cv.Text(L"移动到 ▸", { fpx, fpy, fpx + 78.0f, fpy + 44.0f }, fl, pal.seal);
        float bx2 = fpx + 88.0f;
        for (size_t i = 0; i < m_moveBoxIds.size(); ++i) {
            float bw2 = 110.0f;
            D2D1_RECT_F br{ bx2, fpy + 6.0f, bx2 + bw2, fpy + 42.0f };
            m_moveRects.push_back(br);
            cv.FillRoundRect(br, 5.0f, WithAlpha(pal.jade, 0.14f));
            cv.StrokeRoundRect(br, 5.0f, pal.jade, shape::kHair);
            TextStyle mt; mt.size = 12.0f; mt.role = FontRole::Sans;
            mt.hAlign = HAlign::Center; mt.vAlign = VAlign::Middle;
            std::wstring nm = m_moveBoxNames[i];
            if (nm.size() > 7) nm = nm.substr(0, 7) + L"…";
            cv.Text(nm, br, mt, pal.jade);
            bx2 += bw2 + 10.0f;
        }
    }

    // ---- G3 T4：拖拽跟随小卡（半透明，跟随鼠标）----
    if (m_dragging && m_dragIdx >= 0 && m_dragIdx < (int)b.cards.size()) {
        const auto& dc3 = b.cards[m_dragIdx];
        D2D1_RECT_F mini{ m_dragX - 110.0f, m_dragY - 20.0f, m_dragX + 110.0f, m_dragY + 22.0f };
        cv.PushOpacity(0.88f);
        cv.FillRoundRect(mini, 5.0f, pal.paperHi);
        cv.StrokeRoundRect(mini, 5.0f, pal.seal, shape::kHair);
        TextStyle mt; mt.size = 12.0f; mt.role = FontRole::Sans;
        mt.hAlign = HAlign::Center; mt.vAlign = VAlign::Middle;
        std::wstring fr = dc3.front;
        if (fr.size() > 16) fr = fr.substr(0, 16) + L"…";
        cv.Text(fr, mini, mt, pal.ink900);
        cv.PopOpacity();
    }

    cv.PopTransform();
    cv.PopOpacity();
}

// ============================================================
//  G5 T8：卡片星云 —— 轨道模型（盒=轨道 绕 题集中心点公转）+
//  难度颜色 + hover 同难度高亮 + D2D 伪 3D（近大远小 + 深度排序）
// ============================================================
void QuizBoxView::DrawNebula(Canvas& cv, float s)
{
    if (m_curSet < 0 || m_curSet >= (int)m_sets.size()) { m_view = V_SETS; return; }
    const auto& pal = cv.Pal();
    const auto& st = m_sets[m_curSet];

    float va = ViewAlpha(m_viewT);
    cv.PushOpacity(va);

    // ---- G6 T11 展现模式：搜索命中卡片依次排列（瀑布式网格）----
    if (m_showMode) {
        float availW2 = m_area.right - m_area.left - 300.0f;   // 右侧留侧边栏
        float gx = m_area.left + 28.0f;
        float gy = m_area.top + 66.0f;
        TextStyle stt; stt.role = FontRole::Mono; stt.size = 10.5f; stt.letterSpacing = 2.0f;
        stt.weight = DWRITE_FONT_WEIGHT_BOLD;
        wchar_t hb[64];
        swprintf_s(hb, L"SECTION · 搜索结果 · %d 张（点击卡片进入抽卡）", (int)m_searchHits.size());
        cv.Text(hb, { gx, m_area.top + 24.0f, gx + availW2, m_area.top + 44.0f }, stt, pal.seal);
        // 收起按钮（左上，复用返回命中区）
        D2D1_RECT_F back2{ m_area.left + 28.0f, m_area.top - 2.0f, m_area.left + 150.0f, m_area.top + 32.0f };
        m_nebBackRect = back2;
        cv.FillRoundRect(back2, 6.0f, pal.paperLo);
        cv.StrokeRoundRect(back2, 6.0f, pal.rule, shape::kHair);
        TextStyle bt9; bt9.size = 12.0f; bt9.role = FontRole::Sans;
        bt9.hAlign = HAlign::Center; bt9.vAlign = VAlign::Middle;
        cv.Text(L"◀ 星云", back2, bt9, pal.ink700);

        auto diffColor9 = [&](int d) -> D2D1_COLOR_F {
            switch (d) {
            case 1: case 2: return pal.jade;
            case 3: return pal.brass;
            case 4: return pal.seal;
            default: return pal.vermilion;
            }
        };
        int cols = (std::max)(2, (int)(availW2 / 236.0f));
        for (size_t i = 0; i < m_searchHits.size(); ++i) {
            float k = Clamp01((m_showT - 0.06f * (float)i) / 0.3f);   // 依次淡入
            if (k <= 0.0f) break;
            int rr2 = (int)i / cols, c2 = (int)i % cols;
            float bx = gx + c2 * 236.0f, by = gy + rr2 * 138.0f;
            D2D1_RECT_F card{ bx, by, bx + 226.0f, by + 128.0f };
            cv.PushOpacity(k);
            cv.PaperCard(card, 1.5f);
            const auto& pr = m_searchHits[i];
            if (pr.first < (int)st.boxes.size() && pr.second < (int)st.boxes[pr.first].cards.size()) {
                const auto& cd = st.boxes[pr.first].cards[pr.second];
                cv.FillRect({ card.left, card.top, card.left + 3.0f, card.bottom },
                            WithAlpha(diffColor9(cd.difficulty), 0.85f));
                TextStyle f9; f9.size = 12.5f; f9.role = FontRole::Sans;
                f9.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
                std::wstring fr = cd.front;
                if (fr.size() > 30) fr = fr.substr(0, 30) + L"…";
                cv.Text(fr, { card.left + 14.0f, card.top + 10.0f, card.right - 14.0f, card.top + 34.0f }, f9, pal.ink900);
                TextStyle m9; m9.size = 10.5f; m9.role = FontRole::Mono; m9.vAlign = VAlign::Middle;
                wchar_t lb[96];
                swprintf_s(lb, L"%s · D%d · 错%d", st.boxes[pr.first].name.c_str(), cd.difficulty, cd.wrongCount);
                cv.Text(lb, { card.left + 14.0f, card.bottom - 30.0f, card.right - 14.0f, card.bottom - 12.0f }, m9, pal.ink500);
            }
            cv.PopOpacity();
        }
        m_nebCards.clear();   // 展现模式不命中轨道
        DrawSidebar(cv);      // 侧边栏保留（可再次搜索）
        cv.PopOpacity();
        return;
    }

    // 顶部：返回 + 题集名
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float y0 = m_area.top + 20.0f;
    m_nebBackRect = { x0, y0, x0 + 150.0f, y0 + 38.0f };
    cv.FillRoundRect(m_nebBackRect, 6.0f, pal.paperLo);
    cv.StrokeRoundRect(m_nebBackRect, 6.0f, pal.rule, shape::kHair);
    TextStyle bt; bt.size = 12.5f; bt.role = FontRole::Sans;
    bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle;
    cv.Text(L"📦 盒架", m_nebBackRect, bt, pal.ink700);
    TextStyle hs; hs.role = FontRole::Mono; hs.size = 11.0f; hs.letterSpacing = 2.0f;
    hs.weight = DWRITE_FONT_WEIGHT_BOLD; hs.vAlign = VAlign::Middle; hs.hAlign = HAlign::Center;
    int totalCards = 0;
    for (auto& b : st.boxes) totalCards += (int)b.cards.size();
    wchar_t tb[64];
    swprintf_s(tb, L"星云 · %s · %d 盒 %d 卡", st.name.c_str(), (int)st.boxes.size(), totalCards);
    cv.Text(tb, { x0, y0, x0 + contentW, y0 + 38.0f }, hs, pal.seal);

    // ---- 中心点（题集 = 房子）----
    float cx = (m_area.left + m_area.right) * 0.5f;
    float cy = (m_area.top + m_area.bottom) * 0.52f;
    DrawHouse(cv, { cx - 34.0f, cy - 44.0f, cx + 34.0f, cy + 30.0f }, pal.seal);
    TextStyle sn; sn.role = FontRole::Serif; sn.size = 14.0f;
    sn.weight = DWRITE_FONT_WEIGHT_BOLD; sn.hAlign = HAlign::Center; sn.vAlign = VAlign::Top;
    cv.Text(st.name, { cx - 100.0f, cy + 36.0f, cx + 100.0f, cy + 60.0f }, sn, pal.ink900);

    // ---- 轨道 + 卡片（伪 3D：椭圆透视 + 深度排序）----
    auto diffColor = [&](int d) -> D2D1_COLOR_F {
        switch (d) {
        case 1: case 2: return pal.jade;
        case 3: return pal.brass;
        case 4: return pal.seal;
        default: return pal.vermilion;
        }
    };

    // hover 卡所在难度（用于同步高亮）
    int hoverDiff = -1, hoverBox = -1;
    if (m_hoverNeb >= 0 && m_hoverNeb < (int)m_nebCards.size()) {
        const auto& nc = m_nebCards[m_hoverNeb];
        if (nc.boxIdx >= 0 && nc.boxIdx < (int)st.boxes.size()) {
            const auto& b2 = st.boxes[nc.boxIdx];
            if (nc.cardIdx >= 0 && nc.cardIdx < (int)b2.cards.size()) {
                hoverDiff = b2.cards[nc.cardIdx].difficulty;
                hoverBox = nc.boxIdx;
            }
        }
    }

    // G6：搜索命中集合（光环显现 + 其余暗化）
    auto isHit = [&](int bi, int ci) -> bool {
        for (auto& h : m_searchHits)
            if (h.first == bi && h.second == ci) return true;
        return false;
    };
    bool searching = !m_searchStr.empty() && !m_searchHits.empty();

    // G6 T11 聚焦：以鼠标为中心放大视窗（1.8×），侧边栏不参与
    bool focus = m_focusOn && !m_nebDrag;
    if (focus) {
        cv.PushTransform(D2D1::Matrix3x2F::Scale(1.8f, 1.8f,
                                                 D2D1::Point2F(m_focusX, m_focusY)));
    }

    m_nebCards.clear();
    int nb = (int)st.boxes.size();
    for (int bi = 0; bi < nb; ++bi) {
        const auto& b = st.boxes[bi];
        // 轨道半径：依盒序号外扩；俯视透视 ry = rx * 0.30
        bool dropHot = (m_nebDrag && m_nebDropBox == bi);
        float rx = 150.0f + bi * 66.0f;
        if (dropHot) rx *= 1.04f;   // G6 T9：拖拽悬停轨道放大
        float ry = rx * 0.30f;
        // 轨道线（细椭圆；hover 所在轨道 / 拖拽目标轨道加亮）
        bool laneHot = (hoverBox == bi) || dropHot;
        cv.StrokeEllipse(cx, cy, rx, ry,
                          dropHot ? WithAlpha(pal.seal, 0.95f) : WithAlpha(pal.rule, laneHot ? 0.9f : 0.45f),
                          dropHot ? 2.0f : 1.0f);
        // 轨道标签（最右端旁）
        TextStyle lt; lt.size = 10.5f; lt.role = FontRole::Mono; lt.vAlign = VAlign::Middle;
        cv.Text(b.name + (b.kind == 1 ? L" ✕" : L""), { cx + rx + 8.0f, cy - 9.0f, cx + rx + 130.0f, cy + 9.0f },
                lt, dropHot ? pal.seal : (laneHot ? pal.seal : pal.ink500));

        // G6 T11 排列方式：轨道上卡片的角度分配顺序（默认/时间/难度/热度）
        int n = (int)b.cards.size();
        std::vector<int> order(n);
        for (int i = 0; i < n; ++i) order[i] = i;
        if (m_sortMode == 1)      // 按添加时间（新→旧）
            std::sort(order.begin(), order.end(), [&](int a, int c) {
                return b.cards[a].added > b.cards[c].added; });
        else if (m_sortMode == 2) // 按难度（低→高）
            std::sort(order.begin(), order.end(), [&](int a, int c) {
                return b.cards[a].difficulty < b.cards[c].difficulty; });
        else if (m_sortMode == 3) // 按热度（错次多→少）
            std::sort(order.begin(), order.end(), [&](int a, int c) {
                return b.cards[a].wrongCount > b.cards[c].wrongCount; });

        // 卡片沿轨道分布（均分 + 公转；按 order 顺序分配角度）
        for (int pi = 0; pi < n; ++pi) {
            int ci = order[pi];
            float ang = m_orbit + (float)pi / (float)n * 6.2831853f;
            float px = cx + std::cos(ang) * rx;
            float py = cy + std::sin(ang) * ry;
            float z = std::sin(ang);                 // 下半（屏幕向下）= 近
            float scale = 0.78f + (z + 1.0f) * 0.22f;  // 0.78 .. 1.22
            float alpha = 0.5f + (z + 1.0f) * 0.25f;   // 0.5 .. 1.0
            NebCard nc;
            nc.boxIdx = bi; nc.cardIdx = ci;
            nc.pos = { px, py };
            nc.z = z; nc.scale = scale; nc.alpha = alpha;
            float w = 16.0f * scale, h = 20.0f * scale;
            nc.rect = { px - w * 0.5f, py - h * 0.5f, px + w * 0.5f, py + h * 0.5f };
            m_nebCards.push_back(nc);
        }
    }

    // 深度排序（远 → 近绘制）
    std::vector<int> order(m_nebCards.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b2) {
        return m_nebCards[a].z < m_nebCards[b2].z;
    });

    for (int idx : order) {
        const auto& nc = m_nebCards[idx];
        if (nc.boxIdx >= (int)st.boxes.size()) continue;
        const auto& b2 = st.boxes[nc.boxIdx];
        if (nc.cardIdx >= (int)b2.cards.size()) continue;
        const auto& cd = b2.cards[nc.cardIdx];

        D2D1_COLOR_F dc2 = diffColor(cd.difficulty);
        float heat = CardHeat(cd);
        if (heat > 0.05f) {
            dc2.r = dc2.r + (pal.vermilion.r - dc2.r) * heat;
            dc2.g = dc2.g + (pal.vermilion.g - dc2.g) * heat;
            dc2.b = dc2.b + (pal.vermilion.b - dc2.b) * heat;
        }

        // hover 同难度高亮；其他压暗；G6 搜索：命中光环、非命中极暗
        bool lit = (hoverDiff == cd.difficulty);
        bool self = (m_hoverNeb == idx);
        bool hit = searching && isHit(nc.boxIdx, nc.cardIdx);
        bool dropLane = (m_nebDrag && m_nebDropBox == nc.boxIdx);
        float a = nc.alpha;
        if (m_nebDrag) a = dropLane ? 1.0f : 0.15f;                    // 拖拽：目标轨道亮
        else if (searching) a = hit ? 1.0f : 0.10f;                     // 搜索：命中亮其余暗
        else a *= (hoverDiff >= 0 ? (lit ? 1.0f : 0.18f) : 1.0f);
        float sc = self ? 1.35f : 1.0f;    // hover 自身再放大

        float w = (nc.rect.right - nc.rect.left) * sc;
        float h = (nc.rect.bottom - nc.rect.top) * sc;
        D2D1_RECT_F r{ nc.pos.x - w * 0.5f, nc.pos.y - h * 0.5f,
                       nc.pos.x + w * 0.5f, nc.pos.y + h * 0.5f };
        cv.PushOpacity(a);
        // 卡片小方块：难度色边框 + 淡填充 + 高光（lit/hit 时）
        cv.FillRoundRect(r, 2.5f, WithAlpha(dc2, (lit || hit) ? 0.38f : 0.20f));
        cv.StrokeRoundRect(r, 2.5f, WithAlpha(dc2, (lit || hit) ? 1.0f : 0.75f), 1.2f);
        if (lit || hit) {
            // 光晕（双层描边模拟）
            cv.StrokeRoundRect({ r.left - 3.0f, r.top - 3.0f, r.right + 3.0f, r.bottom + 3.0f },
                               3.5f, WithAlpha(dc2, 0.35f), 1.0f);
        }
        if (hit) {
            // G6 T11 搜索光环：更大外圈光环
            cv.StrokeRoundRect({ r.left - 7.0f, r.top - 7.0f, r.right + 7.0f, r.bottom + 7.0f },
                               5.0f, WithAlpha(dc2, 0.30f), 1.4f);
        }
        if (heat > 0.5f) {
            // 警示微标（小 ✕ 点）
            cv.FillCircle(nc.pos.x, r.top - 2.0f, 2.6f, pal.vermilion);
        }
        cv.PopOpacity();

        // 命中矩形更新为缩放后尺寸（供 Update hover）
        m_nebCards[idx].rect = r;
    }

    // ---- G6 T9：拖拽跟随小卡（放大 1.5 + 题面摘要标签）----
    if (m_nebDrag && m_nebDragIdx >= 0 && m_nebDragIdx < (int)m_nebCards.size()) {
        const auto& dc0 = m_nebCards[m_nebDragIdx];
        if (dc0.boxIdx < (int)st.boxes.size() && dc0.cardIdx < (int)st.boxes[dc0.boxIdx].cards.size()) {
            const auto& cd0 = st.boxes[dc0.boxIdx].cards[dc0.cardIdx];
            float w2 = 26.0f, h2 = 32.0f;
            D2D1_RECT_F r{ m_nebDragX - w2 * 0.5f, m_nebDragY - h2 * 0.5f,
                           m_nebDragX + w2 * 0.5f, m_nebDragY + h2 * 0.5f };
            cv.PushOpacity(0.92f);
            cv.FillRoundRect(r, 3.0f, pal.paperHi);
            cv.StrokeRoundRect(r, 3.0f, diffColor(cd0.difficulty), 1.6f);
            cv.PopOpacity();
            // 跟随标签（题面摘要）
            std::wstring fr = cd0.front;
            if (fr.size() > 14) fr = fr.substr(0, 14) + L"…";
            TextStyle dl; dl.size = 11.0f; dl.role = FontRole::Sans; dl.vAlign = VAlign::Middle;
            cv.Text(fr, { m_nebDragX + 20.0f, m_nebDragY - 12.0f, m_nebDragX + 240.0f, m_nebDragY + 12.0f },
                    dl, pal.ink900);
        }
    }

    // G6 T11 聚焦变换结束（侧边栏不参与放大）
    if (focus) cv.PopTransform();

    // ---- hover tooltip（题面摘要 + 盒名 + 难度）----
    if (m_hoverNeb >= 0 && m_hoverNeb < (int)m_nebCards.size()) {
        const auto& nc = m_nebCards[m_hoverNeb];
        if (nc.boxIdx < (int)st.boxes.size()) {
            const auto& b2 = st.boxes[nc.boxIdx];
            if (nc.cardIdx < (int)b2.cards.size()) {
                const auto& cd = b2.cards[nc.cardIdx];
                float tw = 280.0f, th = 92.0f;
                float tx = nc.pos.x + 20.0f;
                if (tx + tw > m_area.right - 12.0f) tx = nc.pos.x - tw - 20.0f;
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
                wchar_t lb[80];
                swprintf_s(lb, L"%s · 难度 D%d · 错 %d 次", b2.name.c_str(), cd.difficulty, cd.wrongCount);
                cv.Text(lb, { tip.left + 12.0f, tip.top + 34.0f, tip.right - 12.0f, tip.top + 52.0f }, t2, pal.ink500);
                if (!cd.tag.empty())
                    cv.Text(L"#" + cd.tag, { tip.left + 12.0f, tip.top + 56.0f, tip.right - 12.0f, tip.top + 74.0f },
                            t2, pal.jade);
                TextStyle t3; t3.size = 9.5f; t3.role = FontRole::Mono;
                cv.Text(L"点击进入抽卡 · 按住可拖到其他轨道",
                        { tip.left + 12.0f, tip.bottom - 22.0f, tip.right - 12.0f, tip.bottom - 6.0f },
                        t3, pal.ink300);
            }
        }
    }

    // G6 T11：侧边栏（搜索 / 展现 / 排列 / 聚焦 / 题集切换）
    DrawSidebar(cv);

    cv.PopOpacity();
}

// ============================================================
//  G6 T11：星云侧边栏（搜索光环入口 / 展现 / 排列 / 聚焦 / 题集切换）
// ============================================================
void QuizBoxView::DrawSidebar(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float sbW = 240.0f;
    float sbx = m_area.right - sbW - 18.0f;
    float sby = m_area.top + 66.0f;

    // 其他题集（题集切换）
    m_sbOtherSets.clear();
    for (size_t i = 0; i < m_sets.size(); ++i)
        if ((int)i != m_curSet) m_sbOtherSets.push_back({ m_sets[i].name, (int)i });

    float sbH = 96.0f + 46.0f + 84.0f + 32.0f + 34.0f * (float)m_sbOtherSets.size() + 60.0f;
    D2D1_RECT_F sb{ sbx, sby, sbx + sbW, sby + sbH };
    cv.FillRoundRect(sb, 8.0f, WithAlpha(pal.paperHi, 0.97f));
    cv.StrokeRoundRect(sb, 8.0f, pal.rule, shape::kHair);

    // 搜索框 + 搜索按钮
    m_sbSearchBox = { sbx + 14.0f, sby + 16.0f, sbx + sbW - 96.0f, sby + 46.0f };
    m_sbSearchGo  = { sbx + sbW - 90.0f, sby + 16.0f, sbx + sbW - 14.0f, sby + 46.0f };
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
        cv.Text(m_searchStr.empty() ? L"点击搜索题面/答案/标签…" : m_searchStr,
                { m_sbSearchBox.left + 10.0f, m_sbSearchBox.top, m_sbSearchBox.right, m_sbSearchBox.bottom },
                ph, m_searchStr.empty() ? pal.ink300 : pal.ink900);
    }
    cv.FillRoundRect(m_sbSearchGo, 5.0f, pal.seal);
    TextStyle sbt; sbt.size = 12.0f; sbt.role = FontRole::Sans;
    sbt.hAlign = HAlign::Center; sbt.vAlign = VAlign::Middle;
    cv.Text(L"🔍 搜索", m_sbSearchGo, sbt, pal.paperHi);

    // 展现按钮（搜索命中后可用）
    m_sbShowBtn = { sbx + 14.0f, sby + 54.0f, sbx + sbW - 14.0f, sby + 84.0f };
    bool canShow = !m_searchHits.empty();
    cv.FillRoundRect(m_sbShowBtn, 5.0f, canShow ? WithAlpha(pal.jade, 0.9f) : WithAlpha(pal.rule, 0.12f));
    cv.Text(L"✨ 展开命中卡片", m_sbShowBtn, sbt, canShow ? pal.paperHi : pal.ink300);

    // 排列方式
    float sy = sby + 102.0f;
    TextStyle lb; lb.size = 10.5f; lb.role = FontRole::Mono; lb.letterSpacing = 2.0f;
    lb.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"排 列", { sbx + 14.0f, sy, sbx + sbW - 14.0f, sy + 18.0f }, lb, pal.ink500);
    const wchar_t* sortNames[3] = { L"时间", L"难度", L"热度" };
    for (int i = 0; i < 3; ++i) {
        m_sbSortR[i] = { sbx + 14.0f + i * 74.0f, sy + 22.0f, sbx + 14.0f + i * 74.0f + 68.0f, sy + 52.0f };
        bool on = (m_sortMode == i + 1);
        cv.FillRoundRect(m_sbSortR[i], 5.0f, on ? WithAlpha(pal.seal, 0.9f) : pal.paperLo);
        cv.StrokeRoundRect(m_sbSortR[i], 5.0f, on ? pal.seal : pal.rule, shape::kHair);
        cv.Text(sortNames[i], m_sbSortR[i], sbt, on ? pal.paperHi : pal.ink700);
    }

    // 聚焦
    sy = sby + 168.0f;
    cv.Text(L"视 图", { sbx + 14.0f, sy, sbx + sbW - 14.0f, sy + 18.0f }, lb, pal.ink500);
    m_sbFocusBtn = { sbx + 14.0f, sy + 22.0f, sbx + sbW - 14.0f, sy + 52.0f };
    cv.FillRoundRect(m_sbFocusBtn, 5.0f, m_focusOn ? WithAlpha(pal.seal, 0.9f) : pal.paperLo);
    cv.StrokeRoundRect(m_sbFocusBtn, 5.0f, m_focusOn ? pal.seal : pal.rule, shape::kHair);
    cv.Text(m_focusOn ? L"🔍 聚焦已开（跟随鼠标）" : L"🔍 开启聚焦放大",
            m_sbFocusBtn, sbt, m_focusOn ? pal.paperHi : pal.ink700);

    // 其他题集（切换浏览）
    sy = sby + 236.0f;
    if (!m_sbOtherSets.empty()) {
        cv.Text(L"切 换 题 集", { sbx + 14.0f, sy, sbx + sbW - 14.0f, sy + 18.0f }, lb, pal.ink500);
        m_sbSetRects.clear();
        for (size_t i = 0; i < m_sbOtherSets.size(); ++i) {
            D2D1_RECT_F r{ sbx + 14.0f, sy + 22.0f + (float)i * 34.0f,
                           sbx + sbW - 14.0f, sy + 50.0f + (float)i * 34.0f };
            m_sbSetRects.push_back(r);
            cv.FillRoundRect(r, 5.0f, pal.paperLo);
            cv.StrokeRoundRect(r, 5.0f, pal.rule, shape::kHair);
            cv.Text(m_sbOtherSets[i].first, { r.left + 10.0f, r.top, r.right, r.bottom }, sbt, pal.ink700);
        }
    }

    // 操作提示
    TextStyle hp; hp.size = 10.0f; hp.role = FontRole::Sans;
    cv.Text(L"拖动卡片到其他轨道可换盒归类",
            { sbx + 14.0f, sb.bottom - 30.0f, sbx + sbW - 14.0f, sb.bottom - 12.0f }, hp, pal.ink300);
}

// G6 T11：搜索 → 命中集合（题面/答案/标签 包含）
void QuizBoxView::ApplySearch()
{
    m_searchHits.clear();
    if (m_searchStr.empty() || m_curSet < 0 || m_curSet >= (int)m_sets.size()) return;
    std::wstring key = m_searchStr;
    for (auto& c : key) c = (wchar_t)towlower(c);
    const auto& st = m_sets[m_curSet];
    for (size_t bi = 0; bi < st.boxes.size(); ++bi)
        for (size_t ci = 0; ci < st.boxes[bi].cards.size(); ++ci) {
            const auto& cd = st.boxes[bi].cards[ci];
            std::wstring hay = cd.front + L"\n" + cd.back + L"\n" + cd.tag;
            for (auto& c : hay) c = (wchar_t)towlower(c);
            if (hay.find(key) != std::wstring::npos)
                m_searchHits.push_back({ (int)bi, (int)ci });
        }
}

void QuizBoxView::EnterShowMode()
{
    ApplySearch();
    if (m_searchHits.empty()) { Toast(L"没有命中的卡片"); return; }
    m_showMode = true;
    m_showT = 0.0f;
}
void QuizBoxView::StartDraw()
{
    if (m_curSet < 0 || m_curSet >= (int)m_sets.size()) return;
    const auto& st = m_sets[m_curSet];
    if (m_curBox < 0 || m_curBox >= (int)st.boxes.size()) return;
    const auto& b = st.boxes[m_curBox];
    if (b.cards.empty()) return;
    m_drawIdx = rand() % (int)b.cards.size();
    m_flip = false;
    m_flipT = 1.0f;
    m_popT = 0.0f;       // 重新弹卡
    m_view = V_DRAW;
    m_viewT = 0.0f;
}

// T10：红警示热度 0..1（重复答错 + 长期未复习双因子）
float QuizBoxView::CardHeat(const QCard& c) const
{
    float wrong = (std::min)(1.0f, c.wrongCount / 3.0f);               // 错 3 次 = 满格
    float stale = 0.0f;
    if (c.lastReview > 0) {
        long long days = ((long long)time(nullptr) - c.lastReview) / 86400;
        stale = (std::min)(1.0f, days / 14.0f);                        // 14 天未复习 = 满格
    } else if (c.added > 0) {
        long long days = ((long long)time(nullptr) - c.added) / 86400;
        stale = (std::min)(1.0f, days / 21.0f);                        // 从未复习：21 天满格
    }
    return Clamp01(wrong * 0.45f + stale * 0.55f);
}

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
    cv.PushOpacity(va);

    m_backDrawRect = { x0, y0, x0 + 120.0f, y0 + 38.0f };
    cv.FillRoundRect(m_backDrawRect, 6.0f, pal.paperLo);
    cv.StrokeRoundRect(m_backDrawRect, 6.0f, pal.rule, shape::kHair);
    TextStyle bt; bt.size = 12.5f; bt.role = FontRole::Sans;
    bt.hAlign = HAlign::Center; bt.vAlign = VAlign::Middle;
    cv.Text(L"◀ 放回", m_backDrawRect, bt, pal.ink700);
    TextStyle hs; hs.role = FontRole::Mono; hs.size = 11.0f; hs.letterSpacing = 2.0f;
    hs.weight = DWRITE_FONT_WEIGHT_BOLD; hs.vAlign = VAlign::Middle; hs.hAlign = HAlign::Center;
    cv.Text(b.name.c_str(), { x0, y0, x0 + contentW, y0 + 38.0f }, hs, pal.seal);

    // ---- T5：盒子特写（左下角小盒子 + 盖子打开动画）----
    float bk = Clamp01(m_popT / 0.25f);   // 盖子开度 0..1
    {
        D2D1_RECT_F boxR{ x0 + 26.0f, y0 + 66.0f, x0 + 116.0f, y0 + 176.0f };
        DrawBoxIcon(cv, boxR, WithAlpha(pal.seal, 0.5f), b.kind == 1);
        // 盖子打开：盖沿上抬（简单位移模拟）
        if (bk < 1.0f) {
            float lift = bk * 16.0f;
            cv.FillRoundRect({ boxR.left - 6.0f, boxR.top - 24.0f - lift + bk * 8.0f,
                               boxR.right + 6.0f, boxR.top - 8.0f - lift + bk * 8.0f },
                             4.0f, WithAlpha(pal.seal, 0.3f * (1.0f - bk)));
        }
    }

    // ---- T5：卡片从盒口飞出到中央（0.15~0.45s）----
    float pop = Clamp01((m_popT - 0.15f) / 0.3f);
    float pe = ease::OutBack(pop);
    float cy0 = y0 + 74.0f, ch = 290.0f;
    // 起点：盒口；终点：中央
    float sx0 = x0 + 40.0f, sy0 = y0 + 100.0f;
    float fx = x0 + (contentW - 0.0f) * 0.5f;   // 卡中心目标
    float cardX = x0 + 24.0f + (contentW - 48.0f) * 0.0f;
    float fromCx = sx0, toCx = x0 + contentW * 0.5f;
    float curCx = fromCx + (toCx - fromCx) * pe;
    float curCy = sy0 + (cy0 + ch * 0.5f - sy0) * pe;
    float scale = 0.28f + 0.72f * pe;
    float alpha = Clamp01(pop * 1.4f);
    float tilt = (1.0f - pe) * -8.0f;            // 飞出途中带 -8° 倾斜回落

    // ---- 翻面（rotateY 模拟：水平缩放 1→0→1）----
    float flipK = Clamp01(m_flipT / 0.28f);
    bool showBack = m_flip;
    float sx = m_flip ? (showBack ? flipK : 1.0f) : (showBack ? 1.0f : 1.0f);
    // 正在翻（m_flipT<1）时：前半正面压缩，后半背面展开
    float scaleX = 1.0f;
    if (m_flipT < 1.0f) {
        float t = m_flipT / 0.28f;
        scaleX = std::fabs(1.0f - 2.0f * t);      // 1→0→1
        showBack = (t > 0.5f) ? m_flip : !m_flip;
    }

    cv.PushTransform(D2D1::Matrix3x2F::Scale(scale * scaleX, scale,
                                            D2D1::Point2F(curCx, curCy)));
    cv.PushOpacity(alpha);

    float hw = contentW * 0.5f;   // 卡半宽（未缩放）
    D2D1_RECT_F card{ curCx - hw, curCy - ch * 0.5f, curCx + hw, curCy + ch * 0.5f };
    cv.PaperCard(card, 2.0f);
    // 难度边框色 + 红警示（T10：heat 高时边框转朱砂正红）
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
        // lerp 边框色 → 朱砂
        edge.r = edge.r + (pal.vermilion.r - edge.r) * heat;
        edge.g = edge.g + (pal.vermilion.g - edge.g) * heat;
        edge.b = edge.b + (pal.vermilion.b - edge.b) * heat;
        edge.a = 1.0f;
    }
    cv.StrokeRoundRect(card, shape::kEdge, WithAlpha(edge, 0.9f), 1.6f);
    cv.DoubleFrame(card, WithAlpha(pal.seal, 0.35f));

    if (!cd.tag.empty()) {
        D2D1_RECT_F badge{ card.right - 130.0f, card.top + 16.0f, card.right - 26.0f, card.top + 42.0f };
        cv.FillRoundRect(badge, 11.0f, WithAlpha(pal.jade, 0.14f));
        TextStyle bgt; bgt.size = 11.0f; bgt.role = FontRole::Mono;
        bgt.hAlign = HAlign::Center; bgt.vAlign = VAlign::Middle;
        cv.Text(L"#" + cd.tag, badge, bgt, pal.jade);
    }
    for (int d = 0; d < 5; ++d)
        cv.FillRect({ card.left + 26.0f + d * 12.0f, card.top + 26.0f,
                      card.left + 26.0f + d * 12.0f + 8.0f, card.top + 36.0f },
                    d < cd.difficulty ? edge : WithAlpha(pal.rule, 0.5f));
    // 红警示徽标（T10）
    if (heat > 0.5f) {
        D2D1_RECT_F hb{ card.left + 22.0f, card.top + 46.0f, card.left + 168.0f, card.top + 70.0f };
        cv.FillRoundRect(hb, 10.0f, WithAlpha(pal.vermilion, 0.16f));
        TextStyle ht; ht.size = 10.5f; ht.role = FontRole::Mono;
        ht.hAlign = HAlign::Center; ht.vAlign = VAlign::Middle;
        cv.Text(L"⚠ 需要维护 · 久未复习", hb, ht, pal.vermilion);
    }

    float tx0 = card.left + 34.0f, tx1 = card.right - 34.0f;
    if (!showBack) {
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 11.0f;
        cv.Text(L"题 面", { tx0, card.top + 78.0f, tx1, card.top + 100.0f }, lb, pal.ink300);
        TextStyle ft; ft.role = FontRole::Serif; ft.size = 20.0f;
        ft.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ft.vAlign = VAlign::Middle;
        cv.Text(cd.front.empty() ? L"（无题面）" : cd.front,
                { tx0, card.top + 104.0f, tx1, card.bottom - 30.0f }, ft, pal.ink900);
    } else {
        TextStyle lb; lb.role = FontRole::Mono; lb.size = 11.0f;
        cv.Text(L"答 案", { tx0, card.top + 78.0f, tx1, card.top + 100.0f }, lb, pal.seal);
        TextStyle ft; ft.role = FontRole::Serif; ft.size = 19.0f;
        ft.vAlign = VAlign::Middle;
        cv.Text(cd.back.empty() ? L"（未填写答案）" : cd.back,
                { tx0, card.top + 104.0f, tx1, card.bottom - 30.0f }, ft, pal.ink900);
    }
    cv.PopOpacity();
    cv.PopTransform();   // 弹卡缩放

    // 弹卡完成后才显示操作按钮
    if (pop >= 0.999f) {
        float by = cy0 + ch + 18.0f;
        m_flipRect = { x0 + contentW * 0.5f - 240.0f, by, x0 + contentW * 0.5f - 80.0f, by + 46.0f };
        m_nextRect = { x0 + contentW * 0.5f + 90.0f, by, x0 + contentW * 0.5f + 250.0f, by + 46.0f };
        // T10 反馈：记住了（jade）/ 答错了（朱砂）——翻面后出现
        if (m_flipT >= 1.0f && showBack) {
            m_remRect   = { x0 + contentW * 0.5f - 66.0f, by, x0 + contentW * 0.5f + 66.0f, by + 46.0f };
            m_wrongRect = { x0 + contentW * 0.5f - 190.0f, by, x0 + contentW * 0.5f - 78.0f, by + 46.0f };
        } else {
            m_remRect = m_wrongRect = { 0, 0, 0, 0 };
        }
        auto BigBtn = [&](const D2D1_RECT_F& r, const wchar_t* label, const D2D1_COLOR_F& fill,
                          const D2D1_COLOR_F& fg) {
            if (r.right <= r.left) return;
            cv.FillRoundRect(r, 8.0f, fill);
            cv.StrokeRoundRect(r, 8.0f, fill, shape::kHair);
            TextStyle bs; bs.size = 13.5f; bs.role = FontRole::Sans;
            bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle; bs.letterSpacing = 1.0f;
            cv.Text(label, r, bs, fg);
        };
        BigBtn(m_flipRect, showBack ? L"◀ 看题面" : L"翻面看答案",
               showBack ? pal.paperLo : pal.seal, showBack ? pal.ink700 : pal.paperHi);
        if (showBack) {
            BigBtn(m_remRect,   L"✓ 记住了", pal.jade, pal.paperHi);
            BigBtn(m_wrongRect, L"✗ 答错了", pal.vermilion, pal.paperHi);
        }
        BigBtn(m_nextRect, L"下一张 ▶", pal.paperLo, pal.ink700);
    } else {
        m_flipRect = m_nextRect = m_remRect = m_wrongRect = { 0, 0, 0, 0 };
    }

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

    // G4：导入预览弹窗独占
    if (m_impOpen) {
        if (in.clicked) {
            if (InRect(m_impCancelR, mx, my)) { m_impOpen = false; return; }
            if (InRect(m_impOkR, mx, my)) { DoImport(); return; }
            if (!InRect(m_impCard, mx, my)) { m_impOpen = false; return; }
        }
        if (in.keyDown[VK_ESCAPE]) { m_impOpen = false; return; }
        return;
    }

    // G3 动效计时：弹卡 / 翻面
    m_popT += dt;
    if (m_flipT < 1.0f) {
        m_flipT += dt;
        if (m_flipT > 1.0f) m_flipT = 1.0f;
    }

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
        if (m_view == V_NEBULA) { m_view = V_BOXES; m_viewT = 0.0f; }
        else if (m_view == V_DRAW) { m_view = V_BOX; m_viewT = 0.0f; }
        else if (m_view == V_BOX) { m_view = V_BOXES; m_viewT = 0.0f; }
        else if (m_view == V_BOXES) { m_view = V_SETS; m_viewT = 0.0f; }
    }

    // ---- G5/G6：星云交互（公转漂移 + hover 高亮 + 拖拽归类 + 侧边栏 + 聚焦）----
    if (m_view == V_NEBULA) {
        // 展现模式计时（卡片依次淡入）
        if (m_showMode) m_showT += dt;

        // 聚焦中心跟随鼠标
        m_focusX = in.mouseX;
        m_focusY = in.mouseY;

        // 聚焦反算：屏幕 → 星云世界坐标（hover / 拖拽统一用世界坐标）
        float k = m_focusOn ? 1.8f : 1.0f;
        float wx = (in.mouseX - m_focusX) / k + m_focusX;
        float wy = (in.mouseY - m_focusY) / k + m_focusY;
        if (!m_focusOn) { wx = in.mouseX; wy = in.mouseY; }

        // 搜索框编辑态：键盘先交给搜索 FieldEdit
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

        // ---- T9 拖拽归类（按住小卡拖到其他轨道）----
        if (!m_showMode) {
            if (in.pressed && !m_nebDrag && m_hoverNeb >= 0) {
                m_nebDragIdx = m_hoverNeb;
                m_nebDragX = wx; m_nebDragY = wy;
                m_dragStartX = wx; m_dragStartY = wy;
            }
            if (m_nebDragIdx >= 0 && in.pressed &&
                (std::fabs(wx - m_dragStartX) > 8.0f || std::fabs(wy - m_dragStartY) > 8.0f))
                m_nebDrag = true;
            if (m_nebDrag) {
                m_nebDragX = wx; m_nebDragY = wy;
                // 椭圆带命中：悬停哪条轨道（题盒）→ 高亮为落点
                m_nebDropBox = -1;
                if (m_curSet >= 0 && m_curSet < (int)m_sets.size()) {
                    const auto& st5 = m_sets[m_curSet];
                    float cx = (m_area.left + m_area.right) * 0.5f;
                    float cy = (m_area.top + m_area.bottom) * 0.52f;
                    for (int bi = 0; bi < (int)st5.boxes.size(); ++bi) {
                        float rx = 150.0f + bi * 66.0f;
                        float ry = rx * 0.30f;
                        float v = ((wx - cx) / rx) * ((wx - cx) / rx)
                                + ((wy - cy) / ry) * ((wy - cy) / ry);
                        if (v > 0.62f && v < 1.45f) { m_nebDropBox = bi; break; }
                    }
                }
                if (in.released) {
                    // 落入目标轨道（题盒）
                    bool moved = false;
                    if (m_nebDropBox >= 0 && m_curSet >= 0 && m_curSet < (int)m_sets.size()
                        && m_nebDragIdx >= 0 && m_nebDragIdx < (int)m_nebCards.size()) {
                        const auto& nc5 = m_nebCards[m_nebDragIdx];
                        const auto& st5 = m_sets[m_curSet];
                        if (nc5.boxIdx < (int)st5.boxes.size()
                            && nc5.cardIdx < (int)st5.boxes[nc5.boxIdx].cards.size()) {
                            const auto& fromId = st5.boxes[nc5.boxIdx].id;
                            const auto& cardId = st5.boxes[nc5.boxIdx].cards[nc5.cardIdx].id;
                            const auto& toId = st5.boxes[m_nebDropBox].id;
                            if (fromId != toId) {
                                BoxStore::Instance().MoveCard(fromId, cardId, toId);
                                m_sets = BoxStore::Instance().Load();
                                Toast(L"已归入：" + st5.boxes[m_nebDropBox].name);
                                moved = true;
                            }
                        }
                    }
                    (void)moved;
                    m_nebDrag = false; m_nebDragIdx = -1; m_nebDropBox = -1;
                }
                return;   // 拖拽中独占
            }
        }

        // hover 检测（世界坐标；hover 时公转暂停便于点击）
        m_hoverNeb = -1;
        if (!m_showMode) {
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
        }

        if (in.clicked) {
            // 返回（展现模式收起 → 星云）
            if (InRect(m_nebBackRect, mx, my)) {
                if (m_showMode) { m_showMode = false; return; }
                m_view = V_BOXES; m_viewT = 0.0f; return;
            }
            // 侧边栏：搜索框 / 搜索按钮 / 展现 / 排列 / 聚焦 / 题集切换
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
            for (size_t i = 0; i < m_sbSetRects.size(); ++i)
                if (InRect(m_sbSetRects[i], mx, my)) {
                    m_curSet = m_sbOtherSets[i].second;
                    m_searchStr.clear(); m_searchHits.clear(); m_showMode = false;
                    m_focusOn = false;
                    Toast(L"已切换题集：" + m_sets[m_curSet].name);
                    return;
                }
            // 点中小卡 → 进入该卡抽卡
            if (!m_showMode && m_hoverNeb >= 0 && m_hoverNeb < (int)m_nebCards.size()) {
                const auto& nc = m_nebCards[m_hoverNeb];
                if (nc.boxIdx >= 0 && m_curSet < (int)m_sets.size()
                    && nc.boxIdx < (int)m_sets[m_curSet].boxes.size()) {
                    m_curBox = nc.boxIdx;
                    m_drawIdx = nc.cardIdx;
                    m_flip = false;
                    m_flipT = 1.0f;
                    m_popT = 0.0f;
                    m_view = V_DRAW;
                    m_viewT = 0.0f;
                    return;
                }
            }
            // 点空白处轻推公转（探索感）
            if (!m_showMode) m_orbit += 0.35f;
        }
        return;   // 星云独占（不走列表逻辑）
    }

    // hover（盒内卡片浮起）
    m_hoverCard = -1;
    if (m_view == V_BOX) {
        for (size_t i = 0; i < m_cardRects.size(); ++i)
            if (InRect(m_cardRects[i], mx, my)) { m_hoverCard = (int)i; m_overInteractive = true; break; }
    }

    // ---- G3 T4：卡片拖拽跨盒（按住卡片行拖动 >8px → 跟随小卡 + 目标盒浮层）----
    if (m_view == V_BOX) {
        if (in.pressed && !m_dragging) {
            int hit = -1;
            for (size_t i = 0; i < m_cardRects.size(); ++i)
                if (InRect(m_cardRects[i], mx, my)) { hit = (int)i; break; }
            if (hit >= 0) { m_dragIdx = hit; m_dragStartX = mx; m_dragStartY = my; }
        }
        if (!m_dragging && m_dragIdx >= 0 && in.pressed &&
            (std::fabs(mx - m_dragStartX) > 8.0f || std::fabs(my - m_dragStartY) > 8.0f)) {
            m_dragging = true;
            m_moveOpen = true;
            m_moveBoxIds.clear(); m_moveBoxNames.clear();
            if (m_curSet >= 0 && m_curSet < (int)m_sets.size()) {
                const auto& st3 = m_sets[m_curSet];
                if (m_curBox >= 0 && m_curBox < (int)st3.boxes.size()) {
                    const auto& fromId = st3.boxes[m_curBox].id;
                    for (const auto& b3 : st3.boxes)
                        if (b3.id != fromId) {
                            m_moveBoxIds.push_back(b3.id);
                            m_moveBoxNames.push_back(b3.name);
                        }
                }
            }
        }
        if (m_dragging) {
            m_dragX = mx; m_dragY = my;
            if (in.released) {
                bool moved = false;
                if (m_curSet >= 0 && m_curSet < (int)m_sets.size() && m_curBox >= 0
                    && m_curBox < (int)m_sets[m_curSet].boxes.size()) {
                    const auto& b3 = m_sets[m_curSet].boxes[m_curBox];
                    if (m_dragIdx >= 0 && m_dragIdx < (int)b3.cards.size()) {
                        for (size_t i = 0; i < m_moveRects.size(); ++i)
                            if (InRect(m_moveRects[i], mx, my)) {
                                BoxStore::Instance().MoveCard(b3.id, b3.cards[m_dragIdx].id,
                                                              m_moveBoxIds[i]);
                                m_sets = BoxStore::Instance().Load();
                                Toast(L"已移动到：" + m_moveBoxNames[i]);
                                moved = true;
                                break;
                            }
                    }
                }
                (void)moved;
                m_dragging = false; m_moveOpen = false; m_dragIdx = -1;
            }
            return;   // 拖拽中独占输入
        }
        if (!in.pressed && !in.released) m_dragIdx = -1;   // 未构成拖拽的按压释放
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
        // G5：星云入口
        if (InRect(m_nebBtn, mx, my)) {
            if (st.boxes.empty()) { Toast(L"先建一个题盒，星云才有轨道"); return; }
            m_view = V_NEBULA; m_viewT = 0.0f; m_hoverNeb = -1;
            return;
        }
        // G4：导入题库入口
        if (InRect(m_impBtn, mx, my)) { BrowseImport(); return; }
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
            StartDraw();   // G3：带弹卡动画
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
        // 翻面（带 rotateY 动画）
        if (InRect(m_flipRect, mx, my)) {
            m_flip = !m_flip;
            m_flipT = 0.0f;   // 触发翻面动画
            return;
        }
        // T10 反馈：记住了 / 答错了（翻面后可用）
        if (m_curSet >= 0 && m_curSet < (int)m_sets.size()) {
            const auto& st2 = m_sets[m_curSet];
            if (m_curBox >= 0 && m_curBox < (int)st2.boxes.size()) {
                auto& b2 = st2.boxes[m_curBox];
                if (m_drawIdx >= 0 && m_drawIdx < (int)b2.cards.size()) {
                    QCard c2 = b2.cards[m_drawIdx];   // 拷贝改后落盘
                    bool acted = false;
                    if (InRect(m_remRect, mx, my)) {
                        c2.lastReview = (long long)time(nullptr);
                        acted = true;
                        Toast(L"已记录：记住了（红色警示将淡出）");
                    } else if (InRect(m_wrongRect, mx, my)) {
                        c2.wrongCount += 1;
                        c2.lastReview = (long long)time(nullptr);
                        acted = true;
                        Toast(L"已记录：答错了（卡片将变红警示）");
                    }
                    if (acted) {
                        BoxStore::Instance().UpdateCard(b2.id, c2);
                        m_sets = BoxStore::Instance().Load();
                        StartDraw();   // 下一张（重新弹卡）
                        return;
                    }
                }
            }
        }
        if (InRect(m_nextRect, mx, my)) { StartDraw(); return; }
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
    else if (m_view == V_DRAW) DrawDraw(cv, s);
    else                        DrawNebula(cv, s);

    cv.PopTransform();
    cv.PopClip();

    // T3 制卡弹窗（最上层）
    if (m_ceOpen) DrawCardEditor(cv, s);

    // G4 导入预览弹窗（最上层）
    if (m_impOpen) {
        const auto& pal2 = cv.Pal();
        float W = m_area.right - m_area.left;
        float H = m_area.bottom - m_area.top;
        cv.FillRect(m_area, WithAlpha(pal2.ink900, 0.5f));
        float cw2 = (std::min)(620.0f, W - 80.0f);
        float ch2 = (std::min)(470.0f, H - 60.0f);
        float px2 = (W - cw2) * 0.5f, py2 = (H - ch2) * 0.5f;
        m_impCard = { px2, py2, px2 + cw2, py2 + ch2 };
        cv.PaperCard(m_impCard, 0.4f, shape::kEdge);
        cv.DoubleFrame(m_impCard, WithAlpha(pal2.seal, 0.8f));

        TextStyle ttl; ttl.role = FontRole::Serif; ttl.size = 20.0f;
        ttl.weight = DWRITE_FONT_WEIGHT_BOLD; ttl.letterSpacing = 1.4f;
        cv.Text(L"导 入 题 库", { px2 + 26.0f, py2 + 18.0f, px2 + cw2 - 200.0f, py2 + 48.0f }, ttl, pal2.ink900);
        // 文件名
        size_t slash = m_impFile.find_last_of(L"\\/");
        std::wstring fname = slash == std::wstring::npos ? m_impFile : m_impFile.substr(slash + 1);
        TextStyle fs; fs.role = FontRole::Mono; fs.size = 10.5f; fs.hAlign = HAlign::Right;
        fs.vAlign = VAlign::Middle;
        cv.Text(fname, { px2 + cw2 - 280.0f, py2 + 20.0f, px2 + cw2 - 26.0f, py2 + 46.0f }, fs, pal2.ink300);
        cv.PerforationH(px2 + 26.0f, px2 + cw2 - 26.0f, py2 + 62.0f, WithAlpha(pal2.ruleStrong, 0.5f));

        // 摘要：每盒一块（盒名 + 卡数 + 前 2 张题面预览）
        float iy = py2 + 76.0f;
        int totalCards2 = 0;
        for (auto& ib : m_impBoxes) totalCards2 += (int)ib.cards.size();
        for (size_t i = 0; i < m_impBoxes.size() && iy < py2 + ch2 - 80.0f; ++i) {
            const auto& ib = m_impBoxes[i];
            TextStyle bn; bn.role = FontRole::Serif; bn.size = 16.0f;
            bn.weight = DWRITE_FONT_WEIGHT_BOLD;
            wchar_t bns[96];
            swprintf_s(bns, L"%s · %d 张", ib.name.c_str(), (int)ib.cards.size());
            cv.Text(bns, { px2 + 30.0f, iy, px2 + cw2 - 30.0f, iy + 24.0f }, bn, pal2.ink900);
            iy += 26.0f;
            for (size_t k = 0; k < ib.cards.size() && k < 2 && iy < py2 + ch2 - 80.0f; ++k) {
                TextStyle pl; pl.size = 12.0f; pl.role = FontRole::Sans;
                std::wstring fr = ib.cards[k].front;
                if (fr.size() > 40) fr = fr.substr(0, 40) + L"…";
                cv.Text(L"· " + fr, { px2 + 44.0f, iy, px2 + cw2 - 40.0f, iy + 20.0f }, pl, pal2.ink500);
                iy += 20.0f;
            }
            if (ib.cards.size() > 2) {
                TextStyle et; et.size = 11.0f; et.role = FontRole::Mono;
                wchar_t em[48];
                swprintf_s(em, L"… 其余 %d 张", (int)ib.cards.size() - 2);
                cv.Text(em, { px2 + 44.0f, iy, px2 + cw2 - 40.0f, iy + 18.0f }, et, pal2.ink300);
                iy += 22.0f;
            }
            iy += 8.0f;
        }

        // 底部按钮
        float by2 = py2 + ch2 - 62.0f;
        m_impCancelR = { px2 + 26.0f, by2, px2 + 26.0f + 96.0f, by2 + 42.0f };
        m_impOkR     = { px2 + cw2 - 26.0f - 220.0f, by2, px2 + cw2 - 26.0f, by2 + 42.0f };
        cv.FillRoundRect(m_impCancelR, 6.0f, pal2.paperLo);
        cv.StrokeRoundRect(m_impCancelR, 6.0f, pal2.rule, shape::kHair);
        TextStyle bts2; bts2.size = 13.0f; bts2.role = FontRole::Sans;
        bts2.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        bts2.hAlign = HAlign::Center; bts2.vAlign = VAlign::Middle; bts2.letterSpacing = 1.0f;
        cv.Text(L"取消", m_impCancelR, bts2, pal2.ink700);
        cv.FillRoundRect(m_impOkR, 6.0f, pal2.seal);
        wchar_t ok[64];
        swprintf_s(ok, L"导入 %d 盒 %d 张卡片", (int)m_impBoxes.size(), totalCards2);
        cv.Text(ok, m_impOkR, bts2, pal2.paperHi);
    }

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

// ============================================================
//  G4 T6：导入题库（选文件 → 解析 → 预览 → 确认导入）
// ============================================================
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

    // 扩展名分派：md/txt → md 解析；csv → csv 解析
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
        // 同名盒合并；否则新建
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
    m_sets = BoxStore::Instance().Load();
    m_impOpen = false;
    wchar_t msg[80];
    swprintf_s(msg, L"已导入 %d 张卡片", total);
    Toast(msg);
}

void QuizBoxView::DebugForceOpen()
{
    // --edit 截图：G4 导入预览弹窗（含示例解析结果）
    if (m_sets.empty()) DebugForcePreview();
    if (!m_sets.empty()) {
        m_view = V_BOXES; m_viewT = 1.0f;
        m_curSet = 0;
        m_impBoxes.clear();
        QuizBoxImpBox b1;
        b1.name = L"常识判断";
        QCard c1; c1.front = L"下列关于宪法的说法正确的是？"; c1.back = L"宪法是根本大法。";
        c1.tag = L"常识"; c1.difficulty = 2;
        QCard c2; c2.front = L"行政处罚的种类不包括？"; c2.back = L"罚金（属刑罚）。";
        c2.tag = L"常识"; c2.difficulty = 4;
        QCard c3; c3.front = c1.front; c3.back = c1.back; c3.tag = L"常识"; c3.difficulty = 3;
        b1.cards = { c1, c2, c3 };
        m_impBoxes.push_back(b1);
        m_impFile = L"C:\\题库\\行测常识.md";
        m_impOpen = true;
    }
}

} // namespace lj
