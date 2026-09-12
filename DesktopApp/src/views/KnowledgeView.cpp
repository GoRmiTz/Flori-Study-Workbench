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

// 截图自检：造一条含完整 Markdown 语法的示例卡并展开，验证阅读视图
void KnowledgeView::DebugForcePreview()
{
    LogLine(L"[kb] DebugForcePreview enter, cards=%d", (int)m_cards.size());
    if (m_cards.empty()) {
        KCard c;
        c.id = L"demo_md";
        c.title = L"示例考点 · Markdown 阅读视图";
        c.source = L"文件拖入";
        c.ts = (long long)time(nullptr);
        c.body =
            L"# 一级标题：卷宗编号 07\n"
            L"## 二级标题：采集规范\n"
            L"正文段落：从网页或资料里选中重点，**加粗强调**、*斜体补充*、"
            L"`inline_code()`，以及[参考链接](https://example.com)。\n"
            L"- 无序列表 · 采集要点一\n"
            L"- 无序列表 · 采集要点二\n"
            L"  - 嵌套条目（缩进两级）\n"
            L"1. 有序步骤一\n"
            L"2. 有序步骤二\n"
            L"> 引用：保持初心，方得始终。\n"
            L"---\n"
            L"```cpp\nint main() {\n    return 0;   // 代码块\n}\n```\n"
            L"## 科目进度表\n"
            L"| 科目 | 进度 | 目标 |\n"
            L"|---|---|---|\n"
            L"| 资料分析 | 80% | 90% |\n"
            L"| 判断推理 | 65% | 85% |\n"
            L"| 申论写作 | 40% | 75% |\n"
            L"表格之后的收尾段落：阅读视图由自研 MarkdownView 渲染，可切回源码对照。";
        m_cards.push_back(c);
        // 不走 Reload()：它会重新 LoadKnowledge 覆盖示例卡；手动同步并行数组
        m_expanded.assign(m_cards.size(), true);
        m_md.assign(m_cards.size(), {});
        m_mdMode.assign(m_cards.size(), true);
        RecomputeLayout();
    }
    for (size_t i = 0; i < m_cards.size(); ++i) m_expanded[i] = true;
    RecomputeLayout();
    LogLine(L"[kb] DebugForcePreview done, cards=%d", (int)m_cards.size());
}

void KnowledgeView::Reload()
{
    m_cards = CheckinStore::Instance().LoadKnowledge();
    m_expanded.assign(m_cards.size(), false);
    m_md.assign(m_cards.size(), {});        // 阅读视图随卡片重建（Lazy 解析）
    m_mdMode.assign(m_cards.size(), true);  // 默认阅读视图
    RecomputeLayout();
}

void KnowledgeView::RecomputeLayout()
{
    SyncArrays();
    if (m_cv) Layout(m_area, *m_cv);
}

// 并行数组与 m_cards 对齐（防越界：任何路径改了卡片数，这里兜底重建）
void KnowledgeView::SyncArrays()
{
    const size_t n = m_cards.size();
    if (m_expanded.size() != n) {
        std::vector<bool> oldE = std::move(m_expanded);
        m_expanded.assign(n, false);
        for (size_t i = 0; i < n && i < oldE.size(); ++i) m_expanded[i] = oldE[i];
    }
    if (m_mdMode.size() != n) {
        std::vector<bool> oldM = std::move(m_mdMode);
        m_mdMode.assign(n, true);
        for (size_t i = 0; i < n && i < oldM.size(); ++i) m_mdMode[i] = oldM[i];
    }
    if (m_md.size() != n) {
        m_md.assign(n, {});
        for (size_t i = 0; i < n && i < m_cards.size(); ++i)
            if (!m_cards[i].body.empty()) {
                m_md[i].SetMarkdown(m_cards[i].body);
                m_md[i].SetBasePath(m_cards[i].basePath);   // 批次 E：图片相对路径基准
            }
    }
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
    m_mdToggleRects.clear();
    m_renRects.clear();

    TextStyle bodyTs; bodyTs.size = 13.0f; bodyTs.role = FontRole::Sans;

    if (m_cards.empty()) {
        v.block(120.0f);   // 空态占位（实际卡片在 Paint 里单独绘制）
    } else {
        for (size_t i = 0; i < m_cards.size(); ++i) {
            float maxW = w - 32.0f - 170.0f;   // 右侧留给按钮
            // Markdown 阅读视图：首次展开前也预排版（Lazy——仅当卡内容非空）
            if (m_md[i].Empty() && !m_cards[i].body.empty()) {
                m_md[i].SetMarkdown(m_cards[i].body);
                m_md[i].SetBasePath(m_cards[i].basePath);   // 批次 E：图片相对路径基准
            }
            float bodyH;
            if (m_expanded[i]) {
                bodyH = (m_mdMode[i] && !m_md[i].Empty())
                      ? m_md[i].Layout(maxW, cv) + 6.0f
                      : cv.MeasureHeight(m_cards[i].body.empty() ? L" " : m_cards[i].body, bodyTs, maxW);
            } else {
                bodyH = 38.0f;
            }
            float h = 16.0f /*top*/ + 26.0f /*title*/ + 18.0f /*meta*/ + 10.0f + bodyH + 14.0f /*bottom*/;
            D2D1_RECT_F r = v.block(h);
            m_cardRects.push_back(r);
            float bw = 70.0f, bh = 30.0f;
            m_delRects.push_back(ui::MakeRect(r.right - bw - 14.0f, r.top + 14.0f, bw, bh));
            m_expandRects.push_back(ui::MakeRect(r.right - 2.0f * bw - 24.0f, r.top + 14.0f, bw, bh));
            m_mdToggleRects.push_back(ui::MakeRect(r.right - 3.0f * bw - 34.0f, r.top + 14.0f, bw, bh));
            // 批次 E：展开态「✎ 改名」按钮（重命名标题）
            if (m_expanded[i])
                m_renRects.push_back(ui::MakeRect(r.right - 4.0f * bw - 44.0f, r.top + 14.0f, bw, bh));
        }
    }
    SetContentHeight((v.bottom() - area.top) + 40.0f);
}

void KnowledgeView::Update(float dt, const Input& in)
{
    if (m_toast) { m_toastT += dt; if (m_toastT > 2.4f) m_toast = false; }
    SyncArrays();   // 防越界兜底（云同步/拖入等任何改卡路径）

    // F-D5+：拖入收集反馈——版本号变化 = 刚收集成功 → 刷新+Toast；
    // 拖拽悬停中 → Paint 画高亮蒙层
    long long rev = KnowledgeRev();
    if (rev != m_lastRev) { m_lastRev = rev; Reload(); Toast(L"已收集考点"); }
    m_dragActive = KnowledgeDragActive();

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    float mx = in.mouseX, my = shifted.mouseY;

    // ---- 批次 E：标题重命名编辑（原地 FieldEdit）----
    if (m_renActive && m_cv) {
        TextStyle ts; ts.size = 15.0f; ts.role = FontRole::Sans; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
        if (m_renIdx >= 0 && m_renIdx < (int)m_cardRects.size()) {
            D2D1_RECT_F box = RenBox(m_renIdx);
            m_ren.HandleMouse(in, *m_cv, box, ts, 0.0f, 0.0f);
            if (in.clicked && !InRect(box, mx, my)) { CommitRename(); }
        }
    }

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
            if (InRect(m_mdToggleRects[i], mx, my)) {
                m_mdMode[i] = !m_mdMode[i];   // 阅读视图 ⇄ 源码
                RecomputeLayout();
                return;
            }
            // 批次 E：✎ 改名 → 标题行原地编辑
            if (m_expanded[i] && i < m_renRects.size() && InRect(m_renRects[i], mx, my)) {
                if (m_renActive && m_renIdx == (int)i) { CommitRename(); return; }
                CommitRename();   // 收尾上一个
                m_renIdx = (int)i;
                m_ren.onEnter     = [this] { CommitRename(); };
                m_ren.onKillFocus = [this] { CommitRename(); };
                m_ren.Begin(m_cards[m_renIdx].title, false, 15.0f);
                m_renActive = true;
                return;
            }
        }
    }
    View::Update(dt, in);
}

// 批次 E：重命名编辑框几何（覆盖标题行）
D2D1_RECT_F KnowledgeView::RenBox(int idx) const
{
    D2D1_RECT_F r{};
    if (idx < 0 || idx >= (int)m_cardRects.size()) return r;
    const D2D1_RECT_F& cr = m_cardRects[idx];
    float padL = cr.left + 16.0f;
    float maxW = cr.right - 16.0f - 170.0f;
    return { padL - 4.0f, cr.top + 12.0f, padL + maxW + 4.0f, cr.top + 42.0f };
}

// 批次 E：重命名提交（回车 / 失焦 / 点外）——写回 KCard.title 并落盘
void KnowledgeView::CommitRename()
{
    if (!m_renActive) return;
    std::wstring t;
    m_ren.End(true, t);
    m_renActive = false;
    if (m_renIdx >= 0 && m_renIdx < (int)m_cards.size()) {
        std::wstring nt = t;
        while (!nt.empty() && (nt.front() == L' ' || nt.front() == L'\n')) nt.erase(nt.begin());
        while (!nt.empty() && (nt.back() == L' ' || nt.back() == L'\n')) nt.pop_back();
        m_cards[m_renIdx].title = nt;
        CheckinStore::Instance().SaveKnowledge(m_cards);
        Toast(nt.empty() ? L"已清空标题（显示正文首行）" : L"已重命名");
    }
    m_renIdx = -1;
}

// 批次 E：显示标题——title 为空时取正文第一行（# 去井号；普通首行截 48）
std::wstring KnowledgeView::DispTitle(const KCard& c)
{
    if (!c.title.empty()) return c.title;
    size_t b = 0;
    while (b < c.body.size() && (c.body[b] == L'\n' || c.body[b] == L'\r' || c.body[b] == L' ')) ++b;
    size_t e = c.body.find(L'\n', b);
    std::wstring line = c.body.substr(b, (e == std::wstring::npos) ? std::wstring::npos : e - b);
    while (!line.empty() && (line.front() == L' ' || line.front() == L'#')) line.erase(line.begin());
    while (!line.empty() && line.back() == L' ') line.pop_back();
    if (line.empty()) return L"未命名考点";
    if (line.size() > 48) line = line.substr(0, 48) + L"…";
    return line;
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

    // 批次 E：标题（重命名编辑态由 FieldEdit 原地绘制；否则显示 DispTitle）
    TextStyle ts; ts.size = 15.0f; ts.role = FontRole::Sans; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    if (m_renActive && m_renIdx == idx) {
        D2D1_RECT_F box = RenBox(idx);
        cv.FillRoundRect(box, 4.0f, WithAlpha(pal.seal, 0.08f));
        cv.StrokeRoundRect(box, 4.0f, WithAlpha(pal.seal, 0.8f), shape::kHair);
        m_ren.Paint(cv, box, ts, pal.ink900, L"", pal.ink300, 0.0f, 0.0f);
    } else {
        cv.Text(DispTitle(c), { padL, r.top + 14.0f, padL + maxW, r.top + 40.0f }, ts,
                c.title.empty() ? pal.ink500 : pal.ink900);
    }

    TextStyle ms; ms.size = 11.0f; ms.role = FontRole::Sans;
    cv.Text(c.source + L" · " + FmtTime(c.ts),
            { padL, r.top + 42.0f, padL + maxW, r.top + 60.0f }, ms, pal.ink500);

    TextStyle bs; bs.size = 13.0f; bs.role = FontRole::Sans;
    float by = r.top + 66.0f;
    if (expanded) {
        bool md = (m_mdMode[idx] && idx < (int)m_md.size() && !m_md[idx].Empty());
        if (md) {
            // Obsidian 式阅读视图（渲染标题 / 列表 / 粗体 / 代码块…）
            m_md[idx].Paint(cv, padL, by, by - 2.0f, r.bottom + 400.0f);
        } else {
            cv.Text(c.body, { padL, by, padL + maxW, r.bottom - 14.0f }, bs, pal.ink700);
        }
    } else {
        cv.Text(c.body, { padL, by, padL + maxW, by + 38.0f }, bs, pal.ink500);
    }

    cv.PopClip();

    PaintButton(cv, m_expandRects[idx], expanded ? L"收起" : L"展开", pal.paperDeep, pal.ink700);
    if (expanded) {
        PaintButton(cv, m_mdToggleRects[idx],
                    (m_mdMode[idx] ? L"源码" : L"阅读"), pal.paperDeep, pal.ink700);
        if (idx < (int)m_renRects.size())
            PaintButton(cv, m_renRects[idx], L"✎ 改名", pal.paperDeep, pal.ink700);
    }
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
