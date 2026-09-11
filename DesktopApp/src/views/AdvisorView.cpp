// ============================================================
//  AdvisorView.cpp — 选岗参谋 F2（P1-7）
// ============================================================
#include "views/AdvisorView.h"
#include "ui/Layout.h"
#include "ui/Canvas.h"
#include "core/Hwnd.h"
#include <commdlg.h>
#include <fstream>
#include <algorithm>
#include <cwctype>
#include <unordered_map>
#include <array>
#include <utility>
#include <windows.h>

namespace lj {

static bool InRect(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
static bool StartsWith(const std::wstring& s, const std::wstring& pre)
{
    return s.size() >= pre.size() && s.compare(0, pre.size(), pre) == 0;
}

// F-D10 省区坐标表前向声明（定义在文件下方 FirstDigitRun 之后）
static const std::vector<std::pair<std::wstring, std::pair<float,float>>>& RegionCoords();

// ---------------- 隐藏 EDIT 代理（专业代码，纯数字）----------------
LRESULT CALLBACK AdvisorView::EditProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    AdvisorView* self = (AdvisorView*)GetWindowLongPtrW(w, GWLP_USERDATA);
    if (self) {
        if (msg == WM_KEYDOWN) {
            if (wp == VK_RETURN) { self->CommitEdit(); return 0; }
            if (wp == VK_ESCAPE) { self->CancelEdit(); return 0; }
        } else if (msg == WM_KILLFOCUS) {
            self->CommitEdit();
        }
    }
    WNDPROC old = self ? self->m_editOld : nullptr;
    LRESULT r = old ? CallWindowProcW(old, w, msg, wp, lp) : DefWindowProcW(w, msg, wp, lp);
    if (msg == WM_SETFOCUS || msg == WM_KEYDOWN || msg == WM_CHAR) HideCaret(w);
    return r;
}

void AdvisorView::EnsureEditor()
{
    if (m_edit) return;
    HWND parent = AppHwnd();
    if (!parent) return;
    m_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
                             0, 0, 10, 10, parent, nullptr,
                             (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
    if (!m_edit) return;
    m_editOld = (WNDPROC)SetWindowLongPtrW(m_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);
    SetWindowLongPtrW(m_edit, GWLP_USERDATA, (LONG_PTR)this);
    m_editFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (m_editFont) SendMessageW(m_edit, WM_SETFONT, (WPARAM)m_editFont, TRUE);
    ShowWindow(m_edit, SW_HIDE);
}

void AdvisorView::BeginEdit()
{
    EnsureEditor();
    if (!m_edit) return;
    SetWindowTextW(m_edit, m_majorCode.c_str());
    float s = (float)AppDpi() / 96.0f;
    int L = (int)(m_majorRect.left * s), T = (int)(m_majorRect.top * s);
    int W = (int)((m_majorRect.right - m_majorRect.left) * s);
    int H = (int)((m_majorRect.bottom - m_majorRect.top) * s);
    SetWindowPos(m_edit, nullptr, L, T, W > 1 ? W : 1, H > 1 ? H : 1, SWP_NOZORDER);
    SendMessageW(m_edit, WM_SETREDRAW, FALSE, 0);
    ShowWindow(m_edit, SW_SHOW);
    SetFocus(m_edit);
    int len = GetWindowTextLengthW(m_edit);
    SendMessageW(m_edit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    m_editing = true;
}

void AdvisorView::CommitEdit()
{
    if (!m_editing) return;
    std::wstring buf = ReadEditBuffer(m_edit);
    std::wstring digits;
    for (wchar_t c : buf) if (iswdigit((wint_t)c)) digits += c;
    if (!digits.empty()) {
        if (digits.size() > 6) digits = digits.substr(0, 6);
        m_majorCode = digits;
        m_catCode   = digits.size() >= 4 ? digits.substr(0, 4) : L"";
        m_classCode = digits.size() >= 2 ? digits.substr(0, 2) : L"";
    }
    ShowWindow(m_edit, SW_HIDE);
    m_editing = false;
    Recompute();
}

void AdvisorView::CancelEdit()
{
    if (!m_editing) return;
    ShowWindow(m_edit, SW_HIDE);
    m_editing = false;
}

// ---------------- 生命周期 ----------------
void AdvisorView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    if (!m_loaded) { LoadSample(); m_loaded = true; }

    m_mockMinus.label = L"−"; m_mockMinus.fontSize = 18.0f;
    m_mockMinus.onClick = [this] { m_mock = (std::max)(0, m_mock - 1); Recompute(); };
    m_mockPlus.label = L"+"; m_mockPlus.fontSize = 18.0f;
    m_mockPlus.onClick = [this] { m_mock = (std::min)(100, m_mock + 1); Recompute(); };
    m_importBtn.label = L"导入职位表 CSV"; m_importBtn.fontSize = 12.5f; m_importBtn.primary = false;
    m_importBtn.onClick = [this] { ImportCsv(); };
    m_backBtn.label = L"返 回 首 页"; m_backBtn.tag = L"00"; m_backBtn.fontSize = 13.0f;
    m_backBtn.onClick = [this] { Go(L"home"); };

    m_detailClose.label = L"关 闭"; m_detailClose.fontSize = 13.0f; m_detailClose.primary = false;
    m_joinBtn.label = L"加入备考打卡"; m_joinBtn.fontSize = 13.0f; m_joinBtn.primary = true;
    m_joinBtn.onClick = [this] {
        if (m_detailIdx < 0 || m_detailIdx >= (int)m_pos.size()) return;
        const auto& p = m_pos[m_detailIdx];
        auto b = CheckinStore::Instance().LoadItems();
        CheckItem ni;
        ni.id = L"pos_" + p.code + L"_" + std::to_wstring((long long)time(nullptr));
        ni.title = L"备考：" + (p.name.empty() ? p.dept : p.name);
        ni.tag = L"选岗"; ni.minutes = 60; ni.slot = L"";
        b.daily.push_back(ni);
        CheckinStore::Instance().SaveItems(b);
        m_toast = true; m_toastT = 3.0f;
        m_showDetail = false;
    };
}

void AdvisorView::OnLeave()
{
    if (m_editing) CancelEdit();
    if (m_edit) ShowWindow(m_edit, SW_HIDE);
}

// ---------------- 布局 ----------------
void AdvisorView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    m_area = area;

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;
    float ix = x0 + 26.0f, right = x0 + contentW - 26.0f;

    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);

    // 画像卡
    {
        D2D1_RECT_F pBlock = flow.block(210.0f + 24.0f);
        m_profileCard = { x0, pBlock.top, x0 + contentW, pBlock.top + 210.0f };
        m_majorRect = { ix, pBlock.top + 92.0f, ix + 170.0f, pBlock.top + 128.0f };
        m_mockMinus.bounds = { ix + 230.0f, pBlock.top + 92.0f, ix + 270.0f, pBlock.top + 128.0f };
        m_mockPlus.bounds  = { ix + 350.0f, pBlock.top + 92.0f, ix + 390.0f, pBlock.top + 128.0f };
        m_importBtn.bounds = { x0 + contentW - 26.0f - 180.0f, pBlock.top + 150.0f, x0 + contentW - 26.0f, pBlock.top + 186.0f };
    }

    // 地图卡（F-D10 选岗地域分布：省区气泡散点，不画边界）
    {
        float mapH = 300.0f;
        D2D1_RECT_F mBlock = flow.block(mapH + 24.0f);
        m_mapCard = { x0, mBlock.top, x0 + contentW, mBlock.top + mapH };
        float plotX = ix, plotY = m_mapCard.top + 56.0f;
        float plotW = (right - ix) * 0.60f, plotH = 200.0f;
        m_mapPlot = { plotX, plotY, plotX + plotW, plotY + plotH };
        m_mapInfo = { plotX + plotW + 20.0f, plotY, right, plotY + plotH };

        // 聚合：按 region 统计匹配岗数与 tier（冲/稳/保）分布
        std::unordered_map<std::wstring, std::array<int,4>> agg; // [count, t0, t1, t2]
        for (const auto& p : m_pos) {
            if (p.match <= 0 || p.region.empty()) continue;
            auto& a = agg[p.region];
            a[0]++;
            if (p.tier == 0) a[1]++; else if (p.tier == 1) a[2]++; else if (p.tier == 2) a[3]++;
        }
        int maxC = 1;
        for (auto& kv : agg) maxC = (std::max)(maxC, kv.second[0]);

        m_bubbles.clear();
        for (const auto& pr : RegionCoords()) {
            const auto& rc = pr.second;
            RegionBubble b; b.region = pr.first;
            b.cx = m_mapPlot.left + rc.first  * (m_mapPlot.right  - m_mapPlot.left);
            b.cy = m_mapPlot.top  + rc.second * (m_mapPlot.bottom - m_mapPlot.top);
            auto it = agg.find(pr.first);
            if (it != agg.end()) {
                b.count = it->second[0]; b.t0 = it->second[1]; b.t1 = it->second[2]; b.t2 = it->second[3];
                b.r = 5.0f + ((float)b.count / (float)maxC) * 15.0f;
            } else { b.count = 0; b.t0 = b.t1 = b.t2 = 0; b.r = 3.0f; }
            m_bubbles.push_back(b);
        }
    }

    // 列表卡
    {
        float headH = 34.0f, rowH = 66.0f;
        int rows = (int)m_matchIdx.size();
        if (rows == 0) rows = 1;
        D2D1_RECT_F lBlock = flow.block(headH + (float)rows * rowH + 16.0f + 24.0f);
        m_listCard = { x0, lBlock.top, x0 + contentW, lBlock.top + headH + (float)rows * rowH + 16.0f };
        m_rowRects.clear();
        for (int i = 0; i < (int)m_matchIdx.size(); ++i)
            m_rowRects.push_back({ ix, lBlock.top + headH + (float)i * rowH,
                                   right, lBlock.top + headH + (float)(i + 1) * rowH - 6.0f });
    }

    {
        D2D1_RECT_F endBlock = flow.block(46.0f + 30.0f);
        m_backBtn.bounds = { x0, endBlock.top, x0 + 150.0f, endBlock.top + 46.0f };
    }

    SetContentHeight(flow.cursorY - area.top);

    // 控件列表
    m_widgets.clear();
    m_widgets.push_back(&m_mockMinus);
    m_widgets.push_back(&m_mockPlus);
    m_widgets.push_back(&m_importBtn);
    m_widgets.push_back(&m_backBtn);
    m_detailWidgets.clear();
    m_detailWidgets.push_back(&m_detailClose);
    m_detailWidgets.push_back(&m_joinBtn);
}

// ---------------- 更新 ----------------
void AdvisorView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    if (m_toast) { m_toastT -= dt; if (m_toastT <= 0.0f) m_toast = false; }

    float mx = in.mouseX, my = in.mouseY + ScrollY();

    // 编辑态：点击字段外即提交
    if (m_editing) {
        if (in.clicked && !InRect(m_majorRect, mx, my)) CommitEdit();
    }

    if (m_showDetail) {
        UpdateWidgets(m_detailWidgets, dt, in);
        // 点击弹层外空白关闭
        D2D1_RECT_F dc = DetailCardRect();
        if (in.clicked && !InRect(dc, mx, my)) m_showDetail = false;
        return;
    }

    UpdateWidgets(m_widgets, dt, in);

    // F-D10 地图点击（命中气泡切换选中，点地图卡空白清除）
    if (in.clicked) {
        bool hit = false;
        for (const auto& b : m_bubbles) {
            float dx = mx - b.cx, dy = my - b.cy;
            if (dx*dx + dy*dy <= (b.r + 5.0f)*(b.r + 5.0f)) {
                m_selRegion = (b.region == m_selRegion) ? L"" : b.region;
                hit = true; break;
            }
        }
        if (hit) return;
        if (InRect(m_mapCard, mx, my)) { m_selRegion = L""; return; }
    }

    // 专业代码字段点击 → 编辑
    if (in.clicked && InRect(m_majorRect, mx, my)) BeginEdit();

    // 行点击 → 详情
    if (in.clicked) {
        for (int i = 0; i < (int)m_rowRects.size(); ++i)
            if (InRect(m_rowRects[i], mx, my)) { m_showDetail = true; m_detailIdx = m_matchIdx[i]; break; }
    }
}

D2D1_RECT_F AdvisorView::DetailCardRect() const
{
    float cw = 600.0f, ch = 400.0f;
    float cx = m_area.left + (m_area.right - m_area.left - cw) * 0.5f;
    float cy = m_area.top + (m_area.bottom - m_area.top - ch) * 0.5f - 30.0f;
    return { cx, cy, cx + cw, cy + ch };
}

// ---------------- 绘制 ----------------
void AdvisorView::Paint(Canvas& cv)
{
    PaintProfile(cv);
    PaintMap(cv);
    PaintList(cv);
    if (m_showDetail) PaintDetail(cv);
    if (m_toast) {
        const auto& pal = cv.Pal();
        float cw = 320.0f, ch = 44.0f;
        float cx = m_area.left + (m_area.right - m_area.left - cw) * 0.5f;
        float cy = m_area.bottom - 70.0f;
        cv.FillRoundRect({ cx, cy, cx + cw, cy + ch }, shape::kEdge, pal.jade);
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
        cv.Text(L"已加入备考打卡 ✓", { cx, cy, cx + cw, cy + ch }, ts, pal.paperHi);
    }
}

void AdvisorView::PaintProfile(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01((m_t - 0.05f) / 0.5f);
    if (a <= 0.004f) return;
    float e = ease::OutCubic(a);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 14.0f));
    cv.PushOpacity(e);

    cv.FillRoundRect(m_profileCard, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(m_profileCard, shape::kEdge, pal.rule, shape::kHair);
    float ix = m_profileCard.left + 26.0f, right = m_profileCard.right - 26.0f;

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 我的报考画像", { ix, m_profileCard.top + 20.0f, right, m_profileCard.top + 36.0f }, sec, pal.ink300);

    TextStyle fs; fs.role = FontRole::Sans; fs.size = 13.0f; fs.vAlign = VAlign::Middle;
    cv.Text(L"专业四层：" + m_classCode + L" 门类 · " + (m_catCode.empty() ? L"—" : m_catCode) + L" 专业类 · "
            + m_majorCode + L" " + m_majorName,
            { ix, m_profileCard.top + 54.0f, right, m_profileCard.top + 78.0f }, fs, pal.ink700);

    // 专业代码字段
    TextStyle ls; ls.role = FontRole::Sans; ls.size = 11.0f;
    cv.Text(L"专业代码", { m_majorRect.left, m_majorRect.top - 18.0f, m_majorRect.right, m_majorRect.top - 4.0f }, ls, pal.ink300);
    cv.FillRoundRect(m_majorRect, shape::kEdgeSoft, pal.paperLo);
    cv.StrokeRoundRect(m_majorRect, shape::kEdgeSoft, pal.rule, shape::kHair);
    if (m_editing) {
        std::wstring buf = ReadEditBuffer(m_edit);
        TextStyle es; es.role = FontRole::Mono; es.size = 16.0f; es.vAlign = VAlign::Middle;
        cv.Text(buf, { m_majorRect.left + 8.0f, m_majorRect.top, m_majorRect.right - 6.0f, m_majorRect.bottom }, es, pal.ink900);
    } else {
        TextStyle es; es.role = FontRole::Mono; es.size = 16.0f; es.vAlign = VAlign::Middle;
        cv.Text(m_majorCode, { m_majorRect.left + 8.0f, m_majorRect.top, m_majorRect.right - 6.0f, m_majorRect.bottom }, es, pal.ink900);
    }

    // 模考总分
    cv.Text(L"模考总分", { m_mockMinus.bounds.left, m_mockMinus.bounds.top - 18.0f, m_mockPlus.bounds.right, m_mockMinus.bounds.top - 4.0f }, ls, pal.ink300);
    m_mockMinus.Paint(cv);
    m_mockPlus.Paint(cv);
    TextStyle vs; vs.role = FontRole::Mono; vs.size = 18.0f; vs.weight = DWRITE_FONT_WEIGHT_BOLD; vs.vAlign = VAlign::Middle;
    cv.Text(std::to_wstring(m_mock), { m_mockMinus.bounds.right + 8.0f, m_mockMinus.bounds.top, m_mockPlus.bounds.left - 8.0f, m_mockMinus.bounds.bottom }, vs, pal.seal);

    m_importBtn.Paint(cv);
    TextStyle hs; hs.role = FontRole::Sans; hs.size = 11.5f;
    cv.Text(L"导入国考/省考职位表 CSV，按专业四层匹配并分冲/稳/保档", { ix, m_profileCard.top + 152.0f, right, m_profileCard.top + 184.0f }, hs, pal.ink500);

    cv.PopOpacity();
    cv.PopTransform();
}

void AdvisorView::PaintList(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01((m_t - 0.12f) / 0.5f);
    if (a <= 0.004f) return;
    float e = ease::OutCubic(a);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 14.0f));
    cv.PushOpacity(e);

    cv.FillRoundRect(m_listCard, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(m_listCard, shape::kEdge, pal.rule, shape::kHair);
    float ix = m_listCard.left + 26.0f, right = m_listCard.right - 26.0f;

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 候选岗位（冲 / 稳 / 保）", { ix, m_listCard.top + 12.0f, right, m_listCard.top + 28.0f }, sec, pal.ink300);

    if (m_matchIdx.empty()) {
        TextStyle es; es.role = FontRole::Sans; es.size = 13.0f;
        cv.Text(L"暂无匹配岗位。请检查专业代码，或导入含「专业要求」列的职位表。",
                { ix, m_listCard.top + 50.0f, right, m_listCard.top + 74.0f }, es, pal.ink500);
        cv.PopOpacity(); cv.PopTransform(); return;
    }

    for (int i = 0; i < (int)m_matchIdx.size(); ++i) {
        int pi = m_matchIdx[i];
        const auto& p = m_pos[pi];
        D2D1_RECT_F r = m_rowRects[i];
        cv.FillRoundRect(r, shape::kEdgeSoft, WithAlpha(pal.ink300, 0.06f));
        cv.StrokeRoundRect(r, shape::kEdgeSoft, pal.rule, shape::kHair);

        // 档位徽章
        D2D1_COLOR_F badge = p.tier == 2 ? pal.jade : (p.tier == 1 ? pal.seal : pal.brass);
        std::wstring tl = p.tier == 2 ? L"保" : (p.tier == 1 ? L"稳" : L"冲");
        cv.FillRoundRect({ r.left + 12.0f, r.top + 20.0f, r.left + 44.0f, r.top + 46.0f }, 6.0f, badge);
        TextStyle bs; bs.role = FontRole::Sans; bs.size = 15.0f; bs.weight = DWRITE_FONT_WEIGHT_BOLD;
        bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle;
        cv.Text(tl, { r.left + 12.0f, r.top + 20.0f, r.left + 44.0f, r.top + 46.0f }, bs, pal.paperHi);

        // 部门 + 职位
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 14.0f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ts.vAlign = VAlign::Middle;
        std::wstring title = (p.dept.empty() ? L"" : p.dept + L" · ") + (p.name.empty() ? L"(见职位表)" : p.name);
        cv.Text(title, { r.left + 56.0f, r.top + 10.0f, right - 150.0f, r.top + 34.0f }, ts, pal.ink900);

        TextStyle ds; ds.role = FontRole::Sans; ds.size = 11.5f; ds.vAlign = VAlign::Middle;
        std::wstring sub = L"专业：" + (p.majorReq.empty() ? L"—" : p.majorReq)
                         + L" ｜ 招" + std::to_wstring(p.count) + L"人 ｜ 进面分 "
                         + (p.cut > 0 ? std::to_wstring(p.cut) : L"未知");
        cv.Text(sub, { r.left + 56.0f, r.top + 38.0f, right - 150.0f, r.top + 58.0f }, ds, pal.ink500);

        // 右侧匹配度
        TextStyle ms; ms.role = FontRole::Mono; ms.size = 11.0f; ms.vAlign = VAlign::Middle;
        std::wstring ml = p.match == 3 ? L"专业匹配" : (p.match == 2 ? L"专业类匹配" : L"门类匹配");
        cv.Text(ml, { right - 140.0f, r.top + 10.0f, right - 12.0f, r.top + 34.0f }, ms,
                p.match == 3 ? pal.seal : pal.ink700);
        cv.Text(L"点击查看 →", { right - 140.0f, r.top + 38.0f, right - 12.0f, r.top + 58.0f }, ms, pal.ink300);
    }

    cv.PopOpacity();
    cv.PopTransform();
}

void AdvisorView::PaintDetail(Canvas& cv)
{
    const auto& pal = cv.Pal();
    // 暗化背景
    cv.FillRoundRect(m_area, 0.0f, WithAlpha(pal.ink900, 0.45f));
    D2D1_RECT_F dc = DetailCardRect();
    cv.FillRoundRect(dc, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(dc, shape::kEdge, pal.rule, shape::kHair);

    float ix = dc.left + 28.0f, right = dc.right - 28.0f;
    const auto& p = m_pos[m_detailIdx];

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.0f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 岗位详情", { ix, dc.top + 20.0f, right, dc.top + 36.0f }, sec, pal.ink300);

    D2D1_COLOR_F badge = p.tier == 2 ? pal.jade : (p.tier == 1 ? pal.seal : pal.brass);
    std::wstring tl = p.tier == 2 ? L"保" : (p.tier == 1 ? L"稳" : L"冲");
    cv.FillRoundRect({ ix, dc.top + 48.0f, ix + 40.0f, dc.top + 80.0f }, 6.0f, badge);
    TextStyle bs; bs.role = FontRole::Sans; bs.size = 18.0f; bs.weight = DWRITE_FONT_WEIGHT_BOLD;
    bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle;
    cv.Text(tl, { ix, dc.top + 48.0f, ix + 40.0f, dc.top + 80.0f }, bs, pal.paperHi);

    TextStyle ts; ts.role = FontRole::Sans; ts.size = 16.0f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ts.vAlign = VAlign::Middle;
    std::wstring title = (p.dept.empty() ? L"" : p.dept + L" · ") + (p.name.empty() ? L"(见职位表)" : p.name);
    cv.Text(title, { ix + 52.0f, dc.top + 48.0f, right, dc.top + 80.0f }, ts, pal.ink900);

    TextStyle ls; ls.role = FontRole::Sans; ls.size = 13.0f; ls.vAlign = VAlign::Middle;
    auto line = [&](int k, const std::wstring& label, const std::wstring& val) {
        cv.Text(label, { ix, dc.top + 100.0f + (float)k * 30.0f, ix + 110.0f, dc.top + 120.0f + (float)k * 30.0f }, ls, pal.ink300);
        cv.Text(val, { ix + 116.0f, dc.top + 100.0f + (float)k * 30.0f, right, dc.top + 120.0f + (float)k * 30.0f }, ls, pal.ink700);
    };
    line(0, L"职位代码", p.code.empty() ? L"—" : p.code);
    line(1, L"专业要求", p.majorReq.empty() ? L"—" : p.majorReq);
    line(2, L"学历 / 政治面貌", (p.edu.empty() ? L"不限" : p.edu) + L" / " + (p.politic.empty() ? L"不限" : p.politic));
    line(3, L"招考人数 / 面试比", std::to_wstring(p.count) + L" 人 / 1:" + std::to_wstring(p.ratio));
    line(4, L"近三年进面分", p.cut > 0 ? std::to_wstring(p.cut) : L"未知");
    line(5, L"你的模考分", std::to_wstring(m_mock) + (p.cut > 0 ? (m_mock >= p.cut ? L"（已达进面线）" : L"（距进面线差 " + std::to_wstring(p.cut - m_mock) + L" 分）") : L""));

    m_detailClose.bounds = { dc.left + 28.0f, dc.bottom - 56.0f, dc.left + 150.0f, dc.bottom - 16.0f };
    m_joinBtn.bounds = { dc.right - 200.0f, dc.bottom - 56.0f, dc.right - 28.0f, dc.bottom - 16.0f };
    m_detailClose.Paint(cv);
    m_joinBtn.Paint(cv);

    cv.PopOpacity();
}

// ---------------- 数据 ----------------
std::wstring AdvisorView::ToW(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w; w.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring AdvisorView::FirstDigitRun(const std::wstring& s)
{
    size_t i = 0, n = s.size();
    while (i < n && !iswdigit((wint_t)s[i])) ++i;
    if (i >= n) return L"";
    size_t j = i; while (j < n && iswdigit((wint_t)s[j])) ++j;
    return s.substr(i, j - i);
}

// ---------------- F-D10 省区坐标表（示意归一化，仅散点排布，不画边界）----------------
static const std::vector<std::pair<std::wstring, std::pair<float,float>>>& RegionCoords()
{
    // (x: 西→东 0..1, y: 北→南 0..1) 大致地理相对位置，仅用于气泡排布
    static const std::vector<std::pair<std::wstring, std::pair<float,float>>> t = {
        { L"北京", {0.62f, 0.30f} }, { L"天津", {0.65f, 0.32f} },
        { L"河北", {0.60f, 0.33f} }, { L"山西", {0.55f, 0.34f} },
        { L"内蒙古", {0.50f, 0.22f} }, { L"辽宁", {0.70f, 0.25f} },
        { L"吉林", {0.75f, 0.20f} }, { L"黑龙江", {0.78f, 0.12f} },
        { L"上海", {0.78f, 0.45f} }, { L"江苏", {0.72f, 0.44f} },
        { L"浙江", {0.75f, 0.50f} }, { L"安徽", {0.66f, 0.46f} },
        { L"福建", {0.73f, 0.57f} }, { L"江西", {0.66f, 0.55f} },
        { L"山东", {0.66f, 0.38f} }, { L"河南", {0.60f, 0.42f} },
        { L"湖北", {0.60f, 0.50f} }, { L"湖南", {0.58f, 0.57f} },
        { L"广东", {0.66f, 0.64f} }, { L"广西", {0.58f, 0.66f} },
        { L"海南", {0.60f, 0.74f} }, { L"重庆", {0.52f, 0.53f} },
        { L"四川", {0.47f, 0.52f} }, { L"贵州", {0.53f, 0.60f} },
        { L"云南", {0.47f, 0.66f} }, { L"西藏", {0.32f, 0.52f} },
        { L"陕西", {0.53f, 0.40f} }, { L"甘肃", {0.45f, 0.37f} },
        { L"青海", {0.40f, 0.40f} }, { L"宁夏", {0.50f, 0.37f} },
        { L"新疆", {0.25f, 0.28f} },
    };
    return t;
}

std::wstring AdvisorView::RegionOf(const std::wstring& dept, const std::wstring& name)
{
    // 先精确匹配省级行政区名（含"中央部委"等归入北京）
    static const wchar_t* kProv[] = {
        L"北京", L"天津", L"河北", L"山西", L"内蒙古", L"辽宁", L"吉林", L"黑龙江",
        L"上海", L"江苏", L"浙江", L"安徽", L"福建", L"江西", L"山东", L"河南",
        L"湖北", L"湖南", L"广东", L"广西", L"海南", L"重庆", L"四川", L"贵州",
        L"云南", L"西藏", L"陕西", L"甘肃", L"青海", L"宁夏", L"新疆"
    };
    for (const auto* p : kProv)
        if (dept.find(p) != std::wstring::npos || name.find(p) != std::wstring::npos)
            return p;
    // 中央 / 国家部委 → 北京（中央）
    if (dept.find(L"中央") != std::wstring::npos ||
        dept.find(L"部委") != std::wstring::npos ||
        dept.find(L"国家") != std::wstring::npos)
        return L"北京";
    return L"";
}

void AdvisorView::Recompute()
{
    m_matchIdx.clear();
    for (int i = 0; i < (int)m_pos.size(); ++i) {
        auto& p = m_pos[i];
        std::wstring run = FirstDigitRun(p.majorReq);
        int m = 0;
        if (!run.empty()) {
            if (run.size() >= 6 && StartsWith(run.substr(0, 6), m_majorCode)) m = 3;
            else if (run.size() >= 4 && !m_catCode.empty() && StartsWith(run.substr(0, 4), m_catCode)) m = 2;
            else if (run.size() >= 2 && !m_classCode.empty() && StartsWith(run.substr(0, 2), m_classCode)) m = 1;
        }
        if (m == 0) {
            if (p.majorReq.find(m_majorName) != std::wstring::npos) m = 3;
            else if (p.majorReq.find(L"设计学") != std::wstring::npos) m = 2;
            else if (p.majorReq.find(L"艺术") != std::wstring::npos) m = 1;
        }
        p.match = m;
        if (m > 0) {
            if (p.cut > 0) {
                if (m_mock >= p.cut + 10) p.tier = 2;
                else if (m_mock >= p.cut - 5) p.tier = 1;
                else p.tier = 0;
            } else {
                p.tier = 0; // 缺进面分，按冲处理
            }
        } else {
            p.tier = -1;
        }
        if (m > 0) m_matchIdx.push_back(i);
    }
    // 排序：档位降序（保>稳>冲），同档按进面分降序
    std::sort(m_matchIdx.begin(), m_matchIdx.end(), [&](int a, int b) {
        if (m_pos[a].tier != m_pos[b].tier) return m_pos[a].tier > m_pos[b].tier;
        return m_pos[a].cut > m_pos[b].cut;
    });
}

void AdvisorView::LoadSample()
{
    // 内置样例：覆盖多个地区与常见专业门类，开箱即用，不针对任何特定考生。
    struct S { const wchar_t* dept; const wchar_t* name; const wchar_t* major; int cut; };
    const S sample[] = {
        { L"国家税务总局某市税务局", L"一级行政执法员（信息管理）", L"计算机类(0809)、电子信息类(0807)", 62 },
        { L"某中央部委直属机构",     L"综合管理岗",                 L"中国语言文学类(0501)、法学类(0301)", 72 },
        { L"某省广播电视局",         L"新媒体运营",                 L"新闻传播学类(0503)、计算机类(0809)", 58 },
        { L"某市委宣传部",           L"宣传干事",                   L"新闻传播学类(0503)、法学类(0301)", 55 },
        { L"某市财政局",             L"财务管理岗",                 L"工商管理类(1202)、应用经济学(0202)", 60 },
        { L"某省文化和旅游厅",       L"文化活动策划",               L"公共管理类(1204)、中国语言文学类(0501)", 51 },
        { L"某省教育厅",             L"教育信息化岗",               L"计算机类(0809)、教育学类(0401)", 56 },
        { L"某市统计局",             L"数据分析岗",                 L"统计学类(0712)、数学类(0701)", 53 },
    };
    m_pos.clear();
    for (const auto& s : sample) {
        Position p;
        p.dept = s.dept; p.name = s.name; p.majorReq = s.major; p.cut = s.cut;
        p.count = 1 + (int)(s.cut % 3); p.ratio = 3; p.edu = L"本科及以上"; p.politic = L"不限";
        p.region = RegionOf(p.dept, p.name);   // F-D10
        m_pos.push_back(p);
    }
    Recompute();
}

void AdvisorView::ImportCsv()
{
    wchar_t path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = AppHwnd();
    ofn.lpstrFilter = L"职位表 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"导入国考 / 省考职位表 CSV";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;  // 用户取消

    auto imported = ParseCsv(path);
    if (!imported.empty()) { m_pos = std::move(imported); Recompute(); m_loaded = true; }
}

std::vector<Position> AdvisorView::ParseCsv(const std::wstring& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::wstring text = ToW(bytes);

    // 按行拆分
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (wchar_t c : text) {
        if (c == L'\n') { if (!cur.empty() && cur.back() == L'\r') cur.pop_back(); lines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) lines.push_back(cur);
    if (lines.size() < 2) return {};

    // 表头 → 列索引
    auto split = [](const std::wstring& line) {
        std::vector<std::wstring> out; std::wstring t;
        for (wchar_t c : line) { if (c == L',') { out.push_back(t); t.clear(); } else t += c; }
        out.push_back(t); return out;
    };
    auto cols = split(lines[0]);
    auto findCol = [&](const std::wstring& kw) -> int {
        for (int i = 0; i < (int)cols.size(); ++i)
            if (cols[i].find(kw) != std::wstring::npos) return i;
        return -1;
    };
    int cDept = findCol(L"部门"), cName = findCol(L"职位名称"),
        cCode = findCol(L"职位代码"), cMajor = findCol(L"专业"),
        cEdu = findCol(L"学历"), cPol = findCol(L"政治面貌"),
        cCount = findCol(L"招考人数"), cRatio = findCol(L"面试比例"),
        cCut = findCol(L"进面");
    int cRegion = findCol(L"工作地点");   // F-D10 优先用 CSV 工作地点列
    if (cRegion < 0) cRegion = findCol(L"地区");
    if (cRegion < 0) cRegion = findCol(L"城市");
    if (cDept < 0 && cName < 0 && cMajor < 0) return {};  // 不是职位表

    auto toInt = [](const std::wstring& s) {
        std::wstring d; for (wchar_t c : s) if (iswdigit((wint_t)c)) d += c;
        return d.empty() ? -1 : std::stoi(d);
    };

    std::vector<Position> out;
    for (size_t r = 1; r < lines.size(); ++r) {
        auto cells = split(lines[r]);
        if (cells.empty()) continue;
        Position p;
        auto get = [&](int ci) -> std::wstring {
            return (ci >= 0 && ci < (int)cells.size()) ? cells[ci] : L"";
        };
        p.dept = get(cDept); p.name = get(cName); p.code = get(cCode);
        p.majorReq = get(cMajor); p.edu = get(cEdu); p.politic = get(cPol);
        p.count = std::max(1, toInt(get(cCount)));
        int rt = toInt(get(cRatio)); p.ratio = rt > 0 ? rt : 3;
        p.cut = toInt(get(cCut));
        if (cRegion >= 0) p.region = get(cRegion);          // F-D10：CSV 工作地点优先
        else p.region = RegionOf(p.dept, p.name);           // 否则从部门/职位名解析
        if (p.dept.empty() && p.name.empty() && p.majorReq.empty()) continue;
        out.push_back(p);
    }
    return out;
}

void AdvisorView::PaintMap(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01((m_t - 0.10f) / 0.5f);
    if (a <= 0.004f) return;
    cv.PushOpacity(a);
    cv.FillRoundRect(m_mapCard, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(m_mapCard, shape::kEdge, pal.rule, shape::kHair);
    float ix = m_mapCard.left + 26.0f, right = m_mapCard.right - 26.0f;

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
    sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 岗位地域分布（气泡大小=对口岗数，颜色=冲/稳/保主导）",
            { ix, m_mapCard.top + 18.0f, right, m_mapCard.top + 36.0f }, sec, pal.ink300);

    // 图例
    float lx = ix, ly = m_mapCard.top + 42.0f;
    TextStyle ls; ls.role = FontRole::Sans; ls.size = 11.0f; ls.vAlign = VAlign::Middle;
    cv.FillCircle(lx + 6, ly + 6, 6, pal.seal);
    cv.Text(L"冲", { lx + 16, ly, lx + 44, ly + 14 }, ls, pal.ink700);
    cv.FillCircle(lx + 58, ly + 6, 6, pal.vermilion);
    cv.Text(L"稳", { lx + 68, ly, lx + 96, ly + 14 }, ls, pal.ink700);
    cv.FillCircle(lx + 110, ly + 6, 6, pal.jade);
    cv.Text(L"保", { lx + 120, ly, lx + 148, ly + 14 }, ls, pal.ink700);
    cv.Text(L"灰点 = 无对口岗", { lx + 162, ly, lx + 290, ly + 14 }, ls, pal.ink500);

    // 气泡散点（不画任何边界线，合规）
    for (const auto& b : m_bubbles) {
        D2D1_COLOR_F col;
        if (b.count == 0) col = pal.ink300;
        else if (b.t2 >= b.t1 && b.t2 >= b.t0) col = pal.jade;
        else if (b.t1 >= b.t0) col = pal.vermilion;
        else col = pal.seal;
        bool sel = (b.region == m_selRegion);
        cv.FillCircle(b.cx, b.cy, b.r, WithAlpha(col, b.count ? 0.85f : 0.4f));
        if (sel) cv.StrokeCircle(b.cx, b.cy, b.r + 3.0f, pal.ink900, 2.0f);
        if (b.count > 0 || sel) {
            TextStyle ns; ns.role = FontRole::Sans; ns.size = 10.0f; ns.hAlign = HAlign::Center; ns.vAlign = VAlign::Middle;
            cv.Text(b.region, { b.cx - 34.0f, b.cy + b.r + 1.0f, b.cx + 34.0f, b.cy + b.r + 15.0f }, ns, pal.ink700);
        }
    }

    // 信息区
    TextStyle its; its.role = FontRole::Sans; its.size = 13.0f;
    if (m_selRegion.empty()) {
        cv.Text(L"点击地图上的省份气泡，查看该省对口岗位明细。",
                { m_mapInfo.left, m_mapInfo.top + 4.0f, m_mapInfo.right, m_mapInfo.top + 36.0f }, its, pal.ink500);
    } else {
        const RegionBubble* sb = nullptr;
        for (const auto& b : m_bubbles) if (b.region == m_selRegion) { sb = &b; break; }
        if (sb) {
            TextStyle ht; ht.role = FontRole::Mono; ht.size = 11.0f; ht.letterSpacing = 1.5f; ht.weight = DWRITE_FONT_WEIGHT_BOLD;
            cv.Text(L"「" + sb->region + L"」对口 " + std::to_wstring(sb->count) + L" 岗",
                    { m_mapInfo.left, m_mapInfo.top, m_mapInfo.right, m_mapInfo.top + 22.0f }, ht, pal.ink900);
            TextStyle st; st.role = FontRole::Sans; st.size = 12.0f;
            cv.Text(L"保 " + std::to_wstring(sb->t2) + L" · 稳 " + std::to_wstring(sb->t1) + L" · 冲 " + std::to_wstring(sb->t0),
                    { m_mapInfo.left, m_mapInfo.top + 28.0f, m_mapInfo.right, m_mapInfo.top + 48.0f }, st, pal.ink700);
            TextStyle lt; lt.role = FontRole::Sans; lt.size = 11.5f;
            float yy = m_mapInfo.top + 60.0f;
            int shown = 0;
            for (const auto& p : m_pos) {
                if (p.match <= 0 || p.region != m_selRegion) continue;
                std::wstring title = (p.dept.empty() ? L"" : p.dept + L" · ") + (p.name.empty() ? L"(见职位表)" : p.name);
                if (title.size() > 20) title = title.substr(0, 19) + L"…";
                std::wstring tg = (p.tier == 2 ? L"保 " : p.tier == 1 ? L"稳 " : L"冲 ");
                cv.Text(tg + title, { m_mapInfo.left, yy, m_mapInfo.right, yy + 18.0f }, lt, pal.ink700);
                yy += 20.0f; if (++shown >= 6) break;
            }
        }
    }
    cv.PopOpacity();
}

} // namespace lj
