// ============================================================
//  AchieveView.cpp — 成就体系 F3 + 契约组队 F4（P1-8 留存抓手）
//  F3：从真实本地数据（专注/打卡/复盘/自习室）派生阶段徽章、
//      连续里程碑、年度回顾，可导出分享。
//  F4：本地优先「自律契约」——期限 + 规则 + 每日目标，自动比对专注进度，
//      违约公示。（多人实时同步为服务端后续项，本视图标注。）
// ============================================================
#include "views/AchieveView.h"
#include "ui/Layout.h"
#include "ui/Canvas.h"
#include "core/Hwnd.h"
#include "core/FocusTracker.h"
#include "app/Cloud.h"
#include "app/AccountStore.h"
#include "app/Store.h"
#include "app/Json.h"
#include "core/DocxStat.h"
#include <commdlg.h>
#include <algorithm>
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <ctime>

using namespace lj::json;

namespace lj {

static bool InRect(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

// wstring → UTF-8（落盘 / 剪贴板用）
std::wstring AchieveView::ToU(const std::wstring& s)
{
    if (s.empty()) return L"";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return L"";
    std::string out; out.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return std::wstring(out.begin(), out.end()); // 返回 wstring 仅为签名兼容，调用处再转回
}
static std::string W2U(const std::wstring& s)
{
    if (s.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out; out.resize(n);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}
static std::wstring U2W(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w; w.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// "YYYY-MM-DD" -> Date（ParseDateStr 仅在 Store.cpp 内可见，这里本地实现）
static Date ParseYMD(const std::wstring& s)
{
    Date d{ 2026, 1, 1 };
    int y = 2026, m = 1, dd = 1;
    if (swscanf_s(s.c_str(), L"%d-%d-%d", &y, &m, &dd) == 3) { d.y = y; d.m = m; d.d = dd; }
    return d;
}

// ---------------- 编辑（v2 统一输入框：契约名称 / 规则）----------------
void AchieveView::BeginEdit(int field)
{
    if (m_editing) CommitEdit();
    m_editField = field;
    m_editing = true;
    // 1×1 透明代理只收键盘 + IME，字段上无任何 GDI 子窗口（无白块）
    m_edit.onEnter     = [this] { CommitEdit(); };
    m_edit.onEsc       = [this] { CancelEdit(); };
    m_edit.onKillFocus = [this] { CommitEdit(); };
    std::wstring cur = (field == 1) ? m_pactName : m_pactRule;
    m_edit.Begin(cur, false, 15.0f);
}

void AchieveView::CommitEdit()
{
    if (!m_editing) return;
    std::wstring buf;
    m_edit.End(true, buf);
    if (m_editField == 1) m_pactName = buf;
    else if (m_editField == 2) m_pactRule = buf;
    m_editing = false;
}

void AchieveView::CancelEdit()
{
    if (!m_editing) return;
    m_edit.Cancel();
    m_editing = false;
}

// ---------------- 生命周期 ----------------
void AchieveView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    LoadPacts();
    Recompute();

    m_tabF3.label = L"成就 · 徽章"; m_tabF3.fontSize = 13.0f;
    m_tabF3.onClick = [this] { m_tab = 0; };
    m_tabF4.label = L"契约 · 组队"; m_tabF4.fontSize = 13.0f;
    m_tabF4.onClick = [this] { m_tab = 1; };

    m_newPactBtn.label = L"＋ 新建契约"; m_newPactBtn.fontSize = 13.0f; m_newPactBtn.primary = true;
    m_newPactBtn.onClick = [this] {
        m_editingPact = true; m_pactName = L""; m_pactRule = L"每日专注备考，不刷手机";
        m_pactDays = 21; m_pactTarget = 60;
    };
    m_createPactBtn.label = L"创建契约"; m_createPactBtn.fontSize = 13.0f; m_createPactBtn.primary = true;
    m_createPactBtn.onClick = [this] {
        if (m_pactName.empty()) m_pactName = L"我的自律契约";
        Pact p;
        p.id = L"pact_" + std::to_wstring((long long)time(nullptr));
        p.name = m_pactName;
        p.rule = m_pactRule;
        p.created = FormatDate(Today());
        p.days = m_pactDays;
        p.targetMinPerDay = m_pactTarget;
        PactMember self; self.name = L"芙洛理·我"; self.isSelf = true; self.progressMin = 0;
        p.members.push_back(self);
        m_pacts.push_back(p);
        SavePacts();
        m_editingPact = false;
        m_toast = true; m_toastT = 3.0f; m_toastMsg = L"契约已建立 ✓";
    };
    m_cancelPactBtn.label = L"取消"; m_cancelPactBtn.fontSize = 13.0f; m_cancelPactBtn.primary = false;
    m_cancelPactBtn.onClick = [this] { m_editingPact = false; };

    auto stepBtn = [&](Button& b, const std::wstring& lbl, int lo, int hi, int d, int* pv) {
        b.label = lbl; b.fontSize = 18.0f; b.primary = false;
        b.onClick = [this, lo, hi, d, pv] { *pv = (std::max)(lo, (std::min)(hi, *pv + d)); };
    };
    stepBtn(m_pactDaysMinus, L"−", 3, 99, -1, &m_pactDays);
    stepBtn(m_pactDaysPlus, L"+", 3, 99, 1, &m_pactDays);
    stepBtn(m_pactTargetMinus, L"−", 10, 240, -5, &m_pactTarget);
    stepBtn(m_pactTargetPlus, L"+", 10, 240, 5, &m_pactTarget);

    m_shareBtn.label = L"复制成就总结"; m_shareBtn.fontSize = 13.0f; m_shareBtn.primary = false;
    m_shareBtn.onClick = [this] {
        std::wstring s = L"【芙洛理 · 我的成就】\n";
        s += L"连续打卡 " + std::to_wstring(m_streak) + L" 天\n";
        s += L"累计专注 " + std::to_wstring(m_totalFocus) + L" 分钟（约 "
           + std::to_wstring(m_totalFocus / 60) + L" 小时）\n";
        s += L"自习室专注 " + std::to_wstring(m_roomSessions) + L" 次\n";
        s += L"写下复盘 " + std::to_wstring(m_journalDays) + L" 篇\n";
        s += L"本周平均完成率 " + std::to_wstring(m_weekAvg) + L"%\n";
        s += L"本年度专注 " + std::to_wstring(m_yearFocus) + L" 分钟（"
           + std::to_wstring(m_yearDays) + L" 天）\n";
        int got = 0; for (auto& b : m_badges) if (b.got) got++;
        s += L"已解锁徽章 " + std::to_wstring(got) + L" / " + std::to_wstring((int)m_badges.size()) + L"\n";
        s += L"—— 芙洛理 Flori";
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            int n = (int)s.size() + 1;
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n * sizeof(wchar_t));
            if (h) {
                wchar_t* p = (wchar_t*)GlobalLock(h);
                wcscpy_s(p, n, s.c_str());
                GlobalUnlock(h);
                SetClipboardData(CF_UNICODETEXT, h);
            }
            CloseClipboard();
        }
        m_toast = true; m_toastT = 3.0f; m_toastMsg = L"成就总结已复制到剪贴板 ✓";
    };

    m_backBtn.label = L"返 回 首 页"; m_backBtn.fontSize = 13.0f;
    m_backBtn.onClick = [this] { Go(L"home"); };
}

void AchieveView::OnLeave()
{
    if (m_editing) CancelEdit();
}

// ---------------- 布局 ----------------
void AchieveView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    m_area = area;
    m_cvCached = &cv;

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;
    float ix = x0 + 26.0f, right = x0 + contentW - 26.0f;

    lj::ui::VLayout flow(x0, area.top + 24.0f, contentW, 0.0f);

    // 顶部分段切换
    {
        const float tabH = 44.0f;
        D2D1_RECT_F tabBlock = flow.block(tabH + 24.0f);
        m_tabF3.bounds = { x0, tabBlock.top, x0 + contentW * 0.5f, tabBlock.top + tabH };
        m_tabF4.bounds = { x0 + contentW * 0.5f, tabBlock.top, x0 + contentW, tabBlock.top + tabH };
    }

    if (m_tab == 0) {
        // ---- F3 成就 ----
        m_badgeRects.clear();
        int cols = 3;
        float gap = 16.0f;
        float colW = (right - ix - (cols - 1) * gap) / cols;
        float rowH = 86.0f;
        if (colW < 150.0f) { cols = 2; colW = (right - ix - gap) / 2; }
        int rows = ((int)m_badges.size() + cols - 1) / cols;
        if (rows < 1) rows = 1;
        {
            D2D1_RECT_F gridBlock = flow.block((float)rows * (rowH + gap));
            float gridTop = gridBlock.top;
            for (int i = 0; i < (int)m_badges.size(); ++i) {
                int c = i % cols, r = i / cols;
                m_badgeRects.push_back({ ix + c * (colW + gap), gridTop + r * (rowH + gap),
                                         ix + c * (colW + gap) + colW, gridTop + r * (rowH + gap) + rowH });
            }
        }

        // 年度回顾卡
        {
            D2D1_RECT_F ycBlock = flow.block(8.0f + 132.0f + 24.0f);
            float ycTop = ycBlock.top + 8.0f;
            m_yearCard = { x0, ycTop, x0 + contentW, ycTop + 132.0f };
        }

        // 今日时间分布 + 记了什么（F-D1）
        {
            int appN = (int)m_todayByApp.size(); if (appN > 5) appN = 5;
            float listH = appN > 0 ? (float)appN * 20.0f : 24.0f;
            float innerH = 136.0f + listH;   // 标题 + 2 条迷你条 + 「记了什么」标题 + 列表 + 底部留白
            D2D1_RECT_F cb = flow.block(innerH + 24.0f);
            m_focusCard = { x0, cb.top + 8.0f, x0 + contentW, cb.top + 8.0f + innerH };
        }

        // F-D4 申论字数统计卡
        {
            float innerH = m_docxOk ? 96.0f : 88.0f;
            D2D1_RECT_F cb = flow.block(innerH + 24.0f);
            m_docxCard = { x0, cb.top + 8.0f, x0 + contentW, cb.top + 8.0f + innerH };
            float bx = m_docxCard.left + 26.0f, br = m_docxCard.right - 26.0f;
            m_docxBtnRect    = { bx, m_docxCard.top + 46.0f, bx + 176.0f, m_docxCard.top + 82.0f };
            m_docxRevokeRect = { br - 96.0f, m_docxCard.top + 46.0f, br, m_docxCard.top + 82.0f };
        }

        {
            D2D1_RECT_F endBlock = flow.block(46.0f + 30.0f);
            m_backBtn.bounds = { x0, endBlock.top, x0 + 150.0f, endBlock.top + 46.0f };
        }
        SetContentHeight(flow.cursorY - area.top);

        m_widgets.clear();
        m_widgets.push_back(&m_tabF3);
        m_widgets.push_back(&m_tabF4);
        m_widgets.push_back(&m_shareBtn);
        m_widgets.push_back(&m_backBtn);
    } else {
        // ---- F4 契约 ----
        {
            D2D1_RECT_F aBlock = flow.block(44.0f + 18.0f);
            m_newPactBtn.bounds = { x0, aBlock.top, x0 + 160.0f, aBlock.top + 44.0f };
        }

        if (m_editingPact) {
            D2D1_RECT_F pBlock = flow.block(268.0f + 50.0f + 24.0f);
            float panelTop = pBlock.top;
            float panelH = 268.0f;
            m_pactNameRect = { ix, panelTop + 44.0f, right, panelTop + 80.0f };
            m_pactRuleRect = { ix, panelTop + 124.0f, right, panelTop + 168.0f };
            m_pactDaysMinus.bounds = { ix, panelTop + 196.0f, ix + 40.0f, panelTop + 236.0f };
            m_pactDaysPlus.bounds  = { ix + 100.0f, panelTop + 196.0f, ix + 140.0f, panelTop + 236.0f };
            m_pactTargetMinus.bounds = { right - 140.0f, panelTop + 196.0f, right - 100.0f, panelTop + 236.0f };
            m_pactTargetPlus.bounds  = { right - 40.0f, panelTop + 196.0f, right, panelTop + 236.0f };
            m_createPactBtn.bounds = { ix, panelTop + panelH + 8.0f, ix + 130.0f, panelTop + panelH + 50.0f };
            m_cancelPactBtn.bounds = { ix + 142.0f, panelTop + panelH + 8.0f, ix + 252.0f, panelTop + panelH + 50.0f };

            m_widgets.clear();
            m_widgets.push_back(&m_tabF3);
            m_widgets.push_back(&m_tabF4);
            m_widgets.push_back(&m_pactDaysMinus);
            m_widgets.push_back(&m_pactDaysPlus);
            m_widgets.push_back(&m_pactTargetMinus);
            m_widgets.push_back(&m_pactTargetPlus);
            m_widgets.push_back(&m_createPactBtn);
            m_widgets.push_back(&m_cancelPactBtn);
            m_widgets.push_back(&m_backBtn);
        } else {
            m_widgets.clear();
            m_widgets.push_back(&m_tabF3);
            m_widgets.push_back(&m_tabF4);
            m_widgets.push_back(&m_newPactBtn);
            m_widgets.push_back(&m_backBtn);
        }

        // 契约列表
        m_pactRects.clear();
        m_pactDelRects.clear();
        {
            float cardH = 96.0f;
            int n = (int)m_pacts.size();
            D2D1_RECT_F listBlock = flow.block((float)n * (cardH + 14.0f) + 10.0f);
            float listTop = listBlock.top;
            for (int i = 0; i < n; ++i) {
                D2D1_RECT_F r = { x0, listTop + (float)i * (cardH + 14.0f),
                                   x0 + contentW, listTop + (float)i * (cardH + 14.0f) + cardH };
                m_pactRects.push_back(r);
                m_pactDelRects.push_back({ r.right - 34.0f, r.top + 12.0f, r.right - 12.0f, r.top + 34.0f });
            }
        }

        {
            D2D1_RECT_F endBlock = flow.block(46.0f + 30.0f);
            m_backBtn.bounds = { x0, endBlock.top, x0 + 150.0f, endBlock.top + 46.0f };
        }
        SetContentHeight(flow.cursorY - area.top);
    }
}

// ---------------- 更新 ----------------
void AchieveView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    if (m_toast) { m_toastT -= dt; if (m_toastT <= 0.0f) m_toast = false; }

    float mx = in.mouseX, my = in.mouseY + ScrollY();

    // 编辑态：鼠标先交给字段（点击定位光标 / 拖拽框选），点字段外才提交
    if (m_editing && m_cvCached) {
        TextStyle es; es.role = FontRole::Sans; es.size = 15.0f; es.vAlign = VAlign::Middle;
        const D2D1_RECT_F& r = (m_editField == 1) ? m_pactNameRect : m_pactRuleRect;
        D2D1_RECT_F tbox{ r.left + 8.0f, r.top, r.right - 6.0f, r.bottom };
        bool inside = m_edit.HandleMouse(in, *m_cvCached, tbox, es, ScrollY());
        if (!inside && in.clicked) CommitEdit();
    }

    UpdateWidgets(m_widgets, dt, in);

    if (m_tab == 1 && m_editingPact) {
        if (in.clicked && InRect(m_pactNameRect, mx, my)) BeginEdit(1);
        if (in.clicked && InRect(m_pactRuleRect, mx, my)) BeginEdit(2);
    }

    // 契约列表：删除点击
    if (m_tab == 1 && in.clicked) {
        for (int i = 0; i < (int)m_pactDelRects.size(); ++i) {
            if (InRect(m_pactDelRects[i], mx, my)) {
                m_pacts.erase(m_pacts.begin() + i);
                SavePacts();
                break;
            }
        }
    }

    // F-D4 申论字数：连接 / 撤销（F3 面板内）
    if (m_tab == 0 && in.clicked) {
        if (InRect(m_docxBtnRect, mx, my)) OnDocxConnect();
        if (InRect(m_docxRevokeRect, mx, my)) OnDocxRevoke();
    }
}

// ---------------- F-D4 申论字数：授权 + 选文件 / 撤销 ----------------
void AchieveView::OnDocxConnect()
{
    auto& cs = CheckinStore::Instance();
    auto s = cs.LoadSettings();
    if (!s.docxEnabled) {
        int r = MessageBoxW(AppHwnd(),
            L"芙洛理仅读取你选择的申论文档以统计「字数」，不会上传或留存正文内容。是否授权连接你的申论 Word 草稿？",
            L"申论字数统计 · 授权", MB_YESNO | MB_ICONINFORMATION);
        if (r != IDYES) return;
    }
    wchar_t path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = AppHwnd();
    ofn.lpstrFilter = L"Word 文档 (*.docx)\0*.docx\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"选择申论文档（仅本地字数统计）";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;     // 用户取消
    s.docxEnabled = true;
    s.docxPaths.clear();
    s.docxPaths.push_back(path);
    cs.SaveSettings(s);
    Recompute();                              // 刷新显示
    m_toast = true; m_toastT = 2.5f; m_toastMsg = L"已连接申论文档 ✓";
}

void AchieveView::OnDocxRevoke()
{
    int r = MessageBoxW(AppHwnd(),
        L"撤销后芙洛理将停止统计该申论文档的字数。确定撤销授权？",
        L"撤销申论授权", MB_YESNO | MB_ICONQUESTION);
    if (r != IDYES) return;
    auto& cs = CheckinStore::Instance();
    auto s = cs.LoadSettings();
    s.docxEnabled = false;
    s.docxPaths.clear();
    cs.SaveSettings(s);
    Recompute();
    m_toast = true; m_toastT = 2.5f; m_toastMsg = L"已撤销申论授权";
}

void AchieveView::PaintDocxButton(Canvas& cv, const D2D1_RECT_F& r,
                                  const std::wstring& label, const D2D1_COLOR_F& col)
{
    const auto& pal = cv.Pal();
    cv.FillRoundRect(r, shape::kEdge, col);
    TextStyle ts; ts.role = FontRole::Sans; ts.size = 12.0f;
    ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ts.vAlign = VAlign::Middle;
    cv.Text(label, { r.left + 12.0f, r.top, r.right, r.bottom }, ts, pal.paperHi);
}

// ---------------- 绘制 ----------------
void AchieveView::Paint(Canvas& cv)
{
    if (m_tab == 0) PaintF3(cv);
    else PaintF4(cv);

    if (m_editing) {
        // 编辑时在字段上直接绘制缓冲文字/光标/选区/IME 组合串（v2 统一输入框）
        const auto& pal = cv.Pal();
        TextStyle es; es.role = FontRole::Sans; es.size = 15.0f; es.vAlign = VAlign::Middle;
        const D2D1_RECT_F& r = (m_editField == 1) ? m_pactNameRect : m_pactRuleRect;
        m_edit.Paint(cv, { r.left + 8.0f, r.top, r.right - 6.0f, r.bottom },
                     es, pal.ink900, L"", pal.ink300, 0.0f, ScrollY());
    }

    if (m_toast) {
        const auto& pal = cv.Pal();
        float cw = 340.0f, ch = 44.0f;
        float cx = m_area.left + (m_area.right - m_area.left - cw) * 0.5f;
        float cy = m_area.bottom - 70.0f;
        cv.FillRoundRect({ cx, cy, cx + cw, cy + ch }, shape::kEdge, pal.jade);
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
        cv.Text(m_toastMsg, { cx, cy, cx + cw, cy + ch }, ts, pal.paperHi);
    }
}

void AchieveView::PaintF3(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01((m_t - 0.05f) / 0.5f);
    if (a <= 0.004f) return;
    float e = ease::OutCubic(a);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 14.0f));
    cv.PushOpacity(e);

    // 分段切换条
    {
        D2D1_RECT_F tr = m_tabF3.bounds;
        cv.FillRoundRect(m_tabF3.bounds, shape::kEdge, m_tab == 0 ? pal.seal : pal.paperLo);
        cv.FillRoundRect(m_tabF4.bounds, shape::kEdge, m_tab == 1 ? pal.seal : pal.paperLo);
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
        cv.Text(m_tabF3.label, m_tabF3.bounds, ts, m_tab == 0 ? pal.paperHi : pal.ink500);
        cv.Text(m_tabF4.label, m_tabF4.bounds, ts, m_tab == 1 ? pal.paperHi : pal.ink500);
        (void)tr;
    }

    // 标题
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 阶段徽章（由你的本地数据自动派发）",
            { m_tabF3.bounds.left + 26.0f, m_tabF3.bounds.bottom + 14.0f, m_tabF3.bounds.right - 26.0f, m_tabF3.bounds.bottom + 34.0f }, sec, pal.ink300);

    // 徽章网格
    for (int i = 0; i < (int)m_badgeRects.size(); ++i) {
        const auto& r = m_badgeRects[i];
        const auto& b = m_badges[i];
        D2D1_COLOR_F accent = b.got ? pal.seal : pal.ink300;
        cv.FillRoundRect(r, shape::kEdge, b.got ? WithAlpha(pal.seal, 0.10f) : pal.paperHi);
        cv.StrokeRoundRect(r, shape::kEdge, b.got ? pal.seal : pal.rule, shape::kHair);

        // 徽章圆章
        float cx = r.left + 28.0f, cy = (r.top + r.bottom) * 0.5f;
        cv.FillCircle(cx, cy, 16.0f, accent);
        TextStyle ds; ds.role = FontRole::Mono; ds.size = 13.0f; ds.weight = DWRITE_FONT_WEIGHT_BOLD;
        ds.hAlign = HAlign::Center; ds.vAlign = VAlign::Middle;
        cv.Text(b.got ? L"✓" : L"·", { cx - 16.0f, cy - 16.0f, cx + 16.0f, cy + 16.0f }, ds, pal.paperHi);

        TextStyle ns; ns.role = FontRole::Sans; ns.size = 14.0f; ns.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ns.vAlign = VAlign::Middle;
        cv.Text(b.name, { r.left + 52.0f, r.top + 14.0f, r.right - 10.0f, r.top + 38.0f }, ns, b.got ? pal.ink900 : pal.ink500);
        TextStyle ds2; ds2.role = FontRole::Sans; ds2.size = 11.0f; ds2.vAlign = VAlign::Middle;
        cv.Text(b.desc, { r.left + 52.0f, r.top + 40.0f, r.right - 10.0f, r.top + 60.0f }, ds2, pal.ink500);

        TextStyle ps; ps.role = FontRole::Mono; ps.size = 11.0f; ps.vAlign = VAlign::Middle;
        std::wstring prog = (b.goal > 0 ? std::to_wstring(b.cur) + L" / " + std::to_wstring(b.goal) : std::to_wstring(b.cur));
        cv.Text(prog, { r.left + 52.0f, r.top + 62.0f, r.right - 10.0f, r.top + 80.0f }, ps, accent);
    }

    // 年度回顾卡
    if (m_yearCard.bottom > m_yearCard.top) {
        cv.FillRoundRect(m_yearCard, shape::kEdge, pal.paperHi);
        cv.StrokeRoundRect(m_yearCard, shape::kEdge, pal.rule, shape::kHair);
        float ix = m_yearCard.left + 26.0f, right = m_yearCard.right - 26.0f;
        TextStyle ys; ys.role = FontRole::Mono; ys.size = 10.5f; ys.letterSpacing = 2.0f; ys.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"SECTION · 年度回顾 · " + std::to_wstring(Today().y),
                { ix, m_yearCard.top + 16.0f, right, m_yearCard.top + 34.0f }, ys, pal.ink300);

        auto stat = [&](int k, const std::wstring& label, const std::wstring& val) {
            float x = ix + (float)k * (right - ix) / 4.0f;
            TextStyle ls; ls.role = FontRole::Sans; ls.size = 11.0f; ls.hAlign = HAlign::Left; ls.vAlign = VAlign::Top;
            cv.Text(label, { x, m_yearCard.top + 48.0f, x + (right - ix) / 4.0f, m_yearCard.top + 66.0f }, ls, pal.ink500);
            TextStyle vs; vs.role = FontRole::Mono; vs.size = 18.0f; vs.weight = DWRITE_FONT_WEIGHT_BOLD; vs.vAlign = VAlign::Top;
            cv.Text(val, { x, m_yearCard.top + 68.0f, x + (right - ix) / 4.0f, m_yearCard.top + 94.0f }, vs, pal.ink900);
        };
        stat(0, L"累计专注", std::to_wstring(m_totalFocus) + L" 分");
        stat(1, L"连续打卡", std::to_wstring(m_streak) + L" 天");
        stat(2, L"自习室", std::to_wstring(m_roomSessions) + L" 次");
        stat(3, L"复盘", std::to_wstring(m_journalDays) + L" 篇");
        TextStyle hs; hs.role = FontRole::Sans; hs.size = 11.5f;
        cv.Text(L"本年度已专注 " + std::to_wstring(m_yearFocus) + L" 分钟，覆盖 " + std::to_wstring(m_yearDays) + L" 天。",
                { ix, m_yearCard.top + 104.0f, right, m_yearCard.top + 128.0f }, hs, pal.ink500);
    }

    // 今日时间分布 + 记了什么（F-D1）
    if (m_focusCard.bottom > m_focusCard.top) {
        cv.FillRoundRect(m_focusCard, shape::kEdge, pal.paperHi);
        cv.StrokeRoundRect(m_focusCard, shape::kEdge, pal.rule, shape::kHair);
        float fx = m_focusCard.left + 26.0f, fr = m_focusCard.right - 26.0f;
        TextStyle fs2; fs2.role = FontRole::Mono; fs2.size = 10.5f; fs2.letterSpacing = 2.0f; fs2.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"SECTION · 今日时间分布", { fx, m_focusCard.top + 16.0f, fr, m_focusCard.top + 34.0f }, fs2, pal.ink300);

        // 两条迷你条：学习 / 娱乐降权
        auto bar = [&](float y, const std::wstring& label, int val, D2D1_COLOR_F col) {
            TextStyle ls; ls.role = FontRole::Sans; ls.size = 11.0f; ls.vAlign = VAlign::Middle;
            cv.Text(label, { fx, y, fx + 70.0f, y + 18.0f }, ls, pal.ink500);
            float bx = fx + 76.0f, bw = fr - bx;
            cv.FillRoundRect({ bx, y + 4.0f, bx + bw, y + 14.0f }, 5.0f, pal.paperLo);
            int maxv = (std::max)(1, (std::max)(m_todayFocus, m_todayEnt));
            float ratio = (float)val / (float)maxv;
            if (ratio > 0.0f) cv.FillRoundRect({ bx, y + 4.0f, bx + bw * ratio, y + 14.0f }, 5.0f, col);
            TextStyle vs; vs.role = FontRole::Mono; vs.size = 11.0f; vs.vAlign = VAlign::Middle;
            cv.Text(std::to_wstring(val) + L" 分", { bx + bw + 8.0f, y, fr, y + 18.0f }, vs, pal.ink700);
        };
        float by = m_focusCard.top + 44.0f;
        bar(by, L"学习", m_todayFocus, pal.seal);
        bar(by + 22.0f, L"娱乐降权", m_todayEnt, pal.brass);

        // 记了什么
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 11.5f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        float ly = by + 56.0f;
        cv.Text(L"记了什么（进程 · 时长 · 类型）", { fx, ly, fr, ly + 20.0f }, ts, pal.ink700);
        ly += 24.0f;
        TextStyle as; as.role = FontRole::Sans; as.size = 11.0f;
        if (m_todayByApp.empty()) {
            cv.Text(L"今日暂未记录到前台应用（芙洛理在后台运行时才会被动计时）。", { fx, ly, fr, ly + 18.0f }, as, pal.ink500);
        } else {
            int n = (int)m_todayByApp.size(); if (n > 5) n = 5;
            for (int i = 0; i < n; ++i) {
                const auto& a = m_todayByApp[i];
                std::wstring t = (a.cat == 2) ? L"娱乐" : L"学习";
                cv.Text(L"· " + a.proc + L"　" + std::to_wstring(a.min) + L" 分　" + t,
                         { fx, ly, fr, ly + 18.0f }, as, a.cat == 2 ? pal.brass : pal.ink700);
                ly += 20.0f;
            }
        }
    }

    // F-D4 申论字数统计卡
    if (m_docxCard.bottom > m_docxCard.top) {
        cv.FillRoundRect(m_docxCard, shape::kEdge, pal.paperHi);
        cv.StrokeRoundRect(m_docxCard, shape::kEdge, pal.rule, shape::kHair);
        float dx = m_docxCard.left + 26.0f, dr = m_docxCard.right - 26.0f;
        TextStyle fs2; fs2.role = FontRole::Mono; fs2.size = 10.5f; fs2.letterSpacing = 2.0f; fs2.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"SECTION · 申论字数", { dx, m_docxCard.top + 16.0f, dr, m_docxCard.top + 34.0f }, fs2, pal.ink300);
        if (!m_docxOk) {
            TextStyle as; as.role = FontRole::Sans; as.size = 11.0f;
            cv.Text(L"连接申论 Word 草稿，统计字数（仅本地、不上传正文）。", { dx, m_docxCard.top + 44.0f, dr, m_docxCard.top + 62.0f }, as, pal.ink500);
            PaintDocxButton(cv, m_docxBtnRect, L"连接申论文档", pal.seal);
        } else {
            TextStyle vs; vs.role = FontRole::Mono; vs.size = 14.0f; vs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            cv.Text(L"今日写作：" + std::to_wstring(m_docxChars) + L" 字", { dx, m_docxCard.top + 44.0f, dr - 120.0f, m_docxCard.top + 70.0f }, vs, pal.ink900);
            TextStyle as; as.role = FontRole::Sans; as.size = 10.5f;
            cv.Text(m_docxFile, { dx, m_docxCard.top + 72.0f, dr - 120.0f, m_docxCard.top + 90.0f }, as, pal.ink500);
            PaintDocxButton(cv, m_docxRevokeRect, L"撤销", pal.brass);
        }
    }

    m_shareBtn.Paint(cv);
    m_backBtn.Paint(cv);

    cv.PopOpacity();
    cv.PopTransform();
}

void AchieveView::PaintF4(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01((m_t - 0.05f) / 0.5f);
    if (a <= 0.004f) return;
    float e = ease::OutCubic(a);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 14.0f));
    cv.PushOpacity(e);

    // 分段切换条
    cv.FillRoundRect(m_tabF3.bounds, shape::kEdge, m_tab == 0 ? pal.seal : pal.paperLo);
    cv.FillRoundRect(m_tabF4.bounds, shape::kEdge, m_tab == 1 ? pal.seal : pal.paperLo);
    TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
    cv.Text(m_tabF3.label, m_tabF3.bounds, ts, m_tab == 0 ? pal.paperHi : pal.ink500);
    cv.Text(m_tabF4.label, m_tabF4.bounds, ts, m_tab == 1 ? pal.paperHi : pal.ink500);

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 自律契约（本地优先；多人实时同步为服务端后续项）",
            { m_tabF3.bounds.left + 26.0f, m_tabF3.bounds.bottom + 14.0f, m_tabF3.bounds.right - 26.0f, m_tabF3.bounds.bottom + 34.0f }, sec, pal.ink300);

    if (!m_editingPact) m_newPactBtn.Paint(cv);   // 编辑态由「创建 / 取消」取代，避免冗余

    if (m_editingPact) {
        D2D1_RECT_F pr = { m_pactNameRect.left - 8.0f, m_pactNameRect.top - 30.0f,
                           m_pactRuleRect.right + 8.0f, m_pactTargetMinus.bounds.bottom + 10.0f };
        cv.FillRoundRect(pr, shape::kEdge, pal.paperHi);
        cv.StrokeRoundRect(pr, shape::kEdge, pal.rule, shape::kHair);
        float ix = pr.left + 18.0f, right = pr.right - 18.0f;

        TextStyle ls; ls.role = FontRole::Sans; ls.size = 11.0f;
        cv.Text(L"契约名称", { m_pactNameRect.left, m_pactNameRect.top - 18.0f, m_pactNameRect.right, m_pactNameRect.top - 4.0f }, ls, pal.ink300);
        cv.FillRoundRect(m_pactNameRect, shape::kEdgeSoft, pal.paperLo);
        cv.StrokeRoundRect(m_pactNameRect, shape::kEdgeSoft, pal.rule, shape::kHair);

        cv.Text(L"契约规则", { m_pactRuleRect.left, m_pactRuleRect.top - 18.0f, m_pactRuleRect.right, m_pactRuleRect.top - 4.0f }, ls, pal.ink300);
        cv.FillRoundRect(m_pactRuleRect, shape::kEdgeSoft, pal.paperLo);
        cv.StrokeRoundRect(m_pactRuleRect, shape::kEdgeSoft, pal.rule, shape::kHair);

        cv.Text(L"持续天数", { m_pactDaysMinus.bounds.left, m_pactDaysMinus.bounds.top - 18.0f, m_pactDaysPlus.bounds.right, m_pactDaysMinus.bounds.top - 4.0f }, ls, pal.ink300);
        m_pactDaysMinus.Paint(cv); m_pactDaysPlus.Paint(cv);
        TextStyle vs; vs.role = FontRole::Mono; vs.size = 18.0f; vs.weight = DWRITE_FONT_WEIGHT_BOLD; vs.vAlign = VAlign::Middle;
        cv.Text(std::to_wstring(m_pactDays) + L" 天", { m_pactDaysPlus.bounds.right + 8.0f, m_pactDaysMinus.bounds.top, m_pactDaysPlus.bounds.right + 120.0f, m_pactDaysMinus.bounds.bottom }, vs, pal.seal);

        cv.Text(L"每日目标专注", { m_pactTargetMinus.bounds.left, m_pactTargetMinus.bounds.top - 18.0f, m_pactTargetPlus.bounds.right, m_pactTargetMinus.bounds.top - 4.0f }, ls, pal.ink300);
        m_pactTargetMinus.Paint(cv); m_pactTargetPlus.Paint(cv);
        TextStyle vs2 = vs;
        cv.Text(std::to_wstring(m_pactTarget) + L" 分", { m_pactTargetPlus.bounds.right + 8.0f, m_pactTargetMinus.bounds.top, m_pactTargetPlus.bounds.right + 120.0f, m_pactTargetMinus.bounds.bottom }, vs2, pal.seal);

        m_createPactBtn.Paint(cv);
        m_cancelPactBtn.Paint(cv);
    }

    // 契约列表
    for (int i = 0; i < (int)m_pactRects.size(); ++i) {
        const auto& r = m_pactRects[i];
        const auto& p = m_pacts[i];
        cv.FillRoundRect(r, shape::kEdge, pal.paperHi);
        cv.StrokeRoundRect(r, shape::kEdge, pal.rule, shape::kHair);
        float ix = r.left + 26.0f, right = r.right - 26.0f;

        TextStyle ns; ns.role = FontRole::Sans; ns.size = 15.0f; ns.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ns.vAlign = VAlign::Middle;
        cv.Text(p.name, { ix, r.top + 12.0f, right - 150.0f, r.top + 38.0f }, ns, pal.ink900);

        TextStyle ds; ds.role = FontRole::Sans; ds.size = 11.5f; ds.vAlign = VAlign::Middle;
        cv.Text(p.rule, { ix, r.top + 40.0f, right - 150.0f, r.top + 62.0f }, ds, pal.ink500);

        // 进度（自评 = 今日专注 / 目标）
        int dayIdx = 0;
        {
            Date cd = ParseYMD(p.created);
            int zc = DaysFromCivil(cd.y, cd.m, cd.d);
            int zt = DaysFromCivil(Today().y, Today().m, Today().d);
            dayIdx = zt - zc + 1;
        }
        bool finished = dayIdx > p.days;
        bool brokeToday = (!finished && dayIdx >= 1 && m_todayFocus < p.targetMinPerDay);
        float ratio = p.targetMinPerDay > 0 ? (float)m_todayFocus / (float)p.targetMinPerDay : 0.0f;
        ratio = (std::max)(0.0f, (std::min)(1.0f, ratio));

        float barX = ix, barW = (right - ix) * 0.5f, barY = r.top + 72.0f, barH = 8.0f;
        cv.FillRoundRect({ barX, barY, barX + barW, barY + barH }, 4.0f, pal.paperLo);
        D2D1_COLOR_F bc = brokeToday ? pal.brass : (finished ? pal.jade : pal.seal);
        cv.FillRoundRect({ barX, barY, barX + barW * ratio, barY + barH }, 4.0f, bc);
        TextStyle ps; ps.role = FontRole::Mono; ps.size = 11.0f; ps.vAlign = VAlign::Middle;
        cv.Text(L"今日 " + std::to_wstring(m_todayFocus) + L"/" + std::to_wstring(p.targetMinPerDay) + L" 分",
                { barX + barW + 12.0f, barY - 2.0f, right - 44.0f, barY + barH + 2.0f }, ps, pal.ink700);

        // 状态徽标（右上角）
        std::wstring st = finished ? L"已完成" : (brokeToday ? L"今日违约" : (L"第 " + std::to_wstring((std::max)(1, dayIdx)) + L"/" + std::to_wstring(p.days) + L" 天"));
        D2D1_COLOR_F sc = finished ? pal.jade : (brokeToday ? pal.brass : pal.ink500);
        TextStyle ss; ss.role = FontRole::Sans; ss.size = 11.5f; ss.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ss.hAlign = HAlign::Right; ss.vAlign = VAlign::Middle;
        cv.Text(st, { right - 140.0f, r.top + 12.0f, right - 44.0f, r.top + 38.0f }, ss, sc);

        // 删除按钮
        D2D1_RECT_F dr = m_pactDelRects[i];
        cv.FillRoundRect(dr, 4.0f, WithAlpha(pal.ink300, 0.12f));
        TextStyle xs; xs.role = FontRole::Sans; xs.size = 14.0f; xs.weight = DWRITE_FONT_WEIGHT_BOLD;
        xs.hAlign = HAlign::Center; xs.vAlign = VAlign::Middle;
        cv.Text(L"×", dr, xs, pal.ink500);
    }

    if (m_pactRects.empty()) {
        TextStyle es; es.role = FontRole::Sans; es.size = 13.0f;
        cv.Text(L"还没有契约。点「＋ 新建契约」，为自己定一个期限 + 规则的自律约定。",
                { m_tabF3.bounds.left + 26.0f, m_newPactBtn.bounds.bottom + 30.0f, m_tabF3.bounds.right - 26.0f, m_newPactBtn.bounds.bottom + 54.0f }, es, pal.ink500);
    }

    m_backBtn.Paint(cv);

    cv.PopOpacity();
    cv.PopTransform();
}

// ---------------- 数据：F3 派生 ----------------
void AchieveView::Recompute()
{
    auto& cs = CheckinStore::Instance();
    auto focus = cs.LoadFocus();
    m_totalFocus = 0; m_roomSessions = 0; m_yearFocus = 0; m_yearDays = 0; m_todayFocus = 0;
    m_todayEnt = 0; m_yearEnt = 0; m_todayByApp.clear();
    std::set<std::wstring> yearDaysSet;
    std::wstring todayKey = FormatDate(Today());
    int thisYear = Today().y;
    for (auto& f : focus) {
        bool study = (f.category == 0 || f.category == 1);   // 旧数据无 cat 字段 → 默认学习
        if (study) {
            m_totalFocus += f.min;
            if (f.tag.find(L"自习室") != std::wstring::npos) m_roomSessions++;
            if (f.date == todayKey) m_todayFocus += f.min;
            if (f.date.size() >= 4 && f.date.substr(0, 4) == std::to_wstring(thisYear)) {
                m_yearFocus += f.min;
                yearDaysSet.insert(f.date);
            }
        } else if (f.category == 2) {   // 娱乐降权：记但不计入专注
            if (f.date == todayKey) m_todayEnt += f.min;
            if (f.date.size() >= 4 && f.date.substr(0, 4) == std::to_wstring(thisYear))
                m_yearEnt += f.min;
        }
        // 「记了什么」：今日各前台进程时长（按类型聚合）
        if (f.date == todayKey && !f.proc.empty()) {
            bool found = false;
            for (auto& a : m_todayByApp) if (a.proc == f.proc) { a.min += f.min; found = true; break; }
            if (!found) m_todayByApp.push_back({ f.proc, f.min, f.category });
        }
    }
    // 按分钟降序，便于「记了什么」展示
    std::sort(m_todayByApp.begin(), m_todayByApp.end(),
              [](const AppTime& a, const AppTime& b) { return a.min > b.min; });
    m_yearDays = (int)yearDaysSet.size();

    // F-D4 申论字数统计：授权后对各 docx 累加字数（仅本地，正文不出端）
    m_docxChars = 0; m_docxOk = false; m_docxFile.clear();
    {
        auto s = cs.LoadSettings();
        if (s.docxEnabled && !s.docxPaths.empty()) {
            int total = 0; bool anyOk = false; std::wstring lastName;
            for (auto& p : s.docxPaths) {
                lj::DocxStat r = lj::CountDocxWords(p);
                if (r.ok) { total += r.chars; anyOk = true; }
                size_t bs = p.find_last_of(L"/\\");
                lastName = (bs == std::wstring::npos) ? p : p.substr(bs + 1);
            }
            if (anyOk) { m_docxChars = total; m_docxOk = true; m_docxFile = lastName; }
        }
    }

    // 连续打卡天数（倒推，今日尚未勾选则从昨日计）
    m_streak = 0;
    auto days = cs.LastNDays(400);
    int i = (int)days.size() - 1;
    if (i >= 0) {
        auto tm = cs.LoadDay(days[i]);
        bool todayAny = false;
        for (auto& kv : tm) if (kv.second) { todayAny = true; break; }
        if (!todayAny) i--;
        for (; i >= 0; --i) {
            auto m = cs.LoadDay(days[i]);
            bool any = false;
            for (auto& kv : m) if (kv.second) { any = true; break; }
            if (any) m_streak++; else break;
        }
    }

    // 复盘天数
    m_journalDays = (int)cs.LoadJournals().size();

    // 本周平均完成率（周一–周日）
    m_weekAvg = 0;
    {
        Date td = Today();
        int zMon = DaysFromCivil(2026, 8, 3);   // 已知锚点：2026-08-03 为周一
        int z = DaysFromCivil(td.y, td.m, td.d);
        int wd = ((z - zMon) % 7 + 7) % 7;       // 0=周一 … 6=周日
        int zMonThis = z - wd;
        auto bundle = cs.LoadItems();
        int sum = 0, cnt = 0;
        for (int k = 0; k < 7; ++k) {
            Date d = DateFromCivil(zMonThis + k);
            std::wstring key = FormatDate(d);
            auto items = ItemsForDate(bundle, d);
            if (items.empty()) continue;
            int done = 0;
            auto dm = cs.LoadDay(key);
            for (auto& it : items) if (dm[it.Key()]) done++;
            sum += done * 100 / (int)items.size();
            cnt++;
        }
        if (cnt > 0) m_weekAvg = sum / cnt;
    }

    // 徽章（阶段成就）
    m_badges.clear();
    auto add = [&](const wchar_t* id, const wchar_t* name, const wchar_t* desc, int cur, int goal) {
        Badge b; b.id = id; b.name = name; b.desc = desc; b.cur = cur; b.goal = goal;
        b.got = (goal > 0 && cur >= goal); m_badges.push_back(b);
    };
    add(L"focus1", L"专注启程", L"累计专注满 1 小时", m_totalFocus, 60);
    add(L"focus10", L"专注进阶", L"累计专注满 10 小时", m_totalFocus, 600);
    add(L"focus30", L"专注狂魔", L"累计专注满 30 小时", m_totalFocus, 1800);
    add(L"streak3", L"三连打卡", L"连续打卡满 3 天", m_streak, 3);
    add(L"streak7", L"一周不辍", L"连续打卡满 7 天", m_streak, 7);
    add(L"streak21", L"二十一天养成", L"连续打卡满 21 天", m_streak, 21);
    add(L"journal1", L"复盘起步", L"写下 1 篇复盘", m_journalDays, 1);
    add(L"journal10", L"复盘成习", L"写下 10 篇复盘", m_journalDays, 10);
    add(L"room1", L"自习常客", L"自习室完成 1 次专注", m_roomSessions, 1);
    add(L"room10", L"自习老兵", L"自习室专注满 10 次", m_roomSessions, 10);
    add(L"week80", L"本周达标", L"本周平均完成率 ≥ 80%", m_weekAvg, 80);
    add(L"year50", L"年度专注", L"本年度专注满 50 小时", m_yearFocus, 3000);
}

// ---------------- 数据：F4 契约持久化 ----------------
void AchieveView::LoadPacts()
{
    m_pacts.clear();
    std::wstring fp = AccountStore::Instance().CurrentRoot() + L"pacts.json";
    std::string buf;
    if (!ReadFileRaw(fp, buf)) return;
    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Arr) return;
    for (auto& el : v.arr) {
        if (el.type != JVal::Obj) continue;
        auto g = [&](const char* k) -> const JVal* {
            auto it = el.obj.find(k); return it == el.obj.end() ? nullptr : &it->second;
        };
        Pact p;
        if (auto* x = g("id")) p.id = U2W(x->str);
        if (auto* x = g("name")) p.name = U2W(x->str);
        if (auto* x = g("rule")) p.rule = U2W(x->str);
        if (auto* x = g("created")) p.created = U2W(x->str);
        if (auto* x = g("days")) p.days = (int)x->num;
        if (auto* x = g("target")) p.targetMinPerDay = (int)x->num;
        if (auto* x = g("members")) {
            if (x->type == JVal::Arr) {
                for (auto& m : x->arr) {
                    if (m.type != JVal::Obj) continue;
                    PactMember pm;
                    auto gm = [&](const char* k) -> const JVal* {
                        auto it = m.obj.find(k); return it == m.obj.end() ? nullptr : &it->second;
                    };
                    if (auto* a = gm("name")) pm.name = U2W(a->str);
                    if (auto* a = gm("self")) pm.isSelf = (a->type == JVal::Bool) ? a->bval : (a->num != 0.0);
                    p.members.push_back(pm);
                }
            }
        }
        if (p.id.empty()) p.id = L"pact_" + std::to_wstring((long long)time(nullptr)) + std::to_wstring(p.name.size());
        if (p.members.empty()) { PactMember self; self.name = L"芙洛理·我"; self.isSelf = true; p.members.push_back(self); }
        m_pacts.push_back(p);
    }
}

void AchieveView::SavePacts()
{
    std::wstring fp = AccountStore::Instance().CurrentRoot() + L"pacts.json";
    std::string out = "[\n";
    for (size_t i = 0; i < m_pacts.size(); ++i) {
        const auto& p = m_pacts[i];
        out += "  {\n";
        out += "    \"id\": " + JQuote(W2U(p.id)) + ",\n";
        out += "    \"name\": " + JQuote(W2U(p.name)) + ",\n";
        out += "    \"rule\": " + JQuote(W2U(p.rule)) + ",\n";
        out += "    \"created\": " + JQuote(W2U(p.created)) + ",\n";
        out += "    \"days\": " + std::to_string(p.days) + ",\n";
        out += "    \"target\": " + std::to_string(p.targetMinPerDay) + ",\n";
        out += "    \"members\": [\n";
        for (size_t j = 0; j < p.members.size(); ++j) {
            const auto& m = p.members[j];
            out += "      { \"name\": " + JQuote(W2U(m.name)) + ", \"self\": " + std::string(m.isSelf ? "true" : "false") + " }";
            out += (j + 1 < p.members.size()) ? ",\n" : "\n";
        }
        out += "    ]\n";
        out += (i + 1 < m_pacts.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    Cloud::Instance().MarkDirty();
}

} // namespace lj
