// ============================================================
//  SettingsView.cpp — 全局设置页（route=settings）
//  由主窗口右上「用户名卡片」弹层中的「设置」项进入（TopBar 注入）。
//  三分区：通用 / 看板娘 / 练考。
//  持久化刻意走 CheckinStore::LoadSettings/SaveSettings 单一通道：
//  它会同时写 settings.json（kanban 段）与本地独占的 kanban_ai.json，
//  与 Kanban.cpp::SetKanbanSettings 完全同源，避免凭据漂移。
//  看板娘 AI 凭据不入库、不进记忆，仅落本地盘。
// ============================================================
#include "views/SettingsView.h"
#include "core/Hwnd.h"
#include "kanban/KanbanTypes.h"
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

namespace lj {

// ---------------- 本地工具 ----------------
static float Clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static std::wstring FormatHM(int minutes)
{
    if (minutes < 0) minutes = 0;
    if (minutes > 1439) minutes = 1439;
    int h = minutes / 60, m = minutes % 60;
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", h, m);
    return buf;
}

static bool ParseHM(const std::wstring& s, int& out)
{
    int h = 0, m = 0;
    if (swscanf_s(s.c_str(), L"%d:%d", &h, &m) == 2 ||
        swscanf_s(s.c_str(), L"%d.%d", &h, &m) == 2) {
        if (h < 0 || h > 23 || m < 0 || m > 59) return false;
        out = h * 60 + m;
        return true;
    }
    return false;
}

// 进程名规整：转小写并去掉 .exe（专注白名单按小写进程名匹配）
static std::wstring NormProc(const std::wstring& s)
{
    std::wstring o;
    for (wchar_t c : s) {
        if (c >= L'A' && c <= L'Z') o += (wchar_t)(c - L'A' + L'a');
        else o += c;
    }
    if (o.size() >= 4 && o.substr(o.size() - 4) == L".exe")
        o = o.substr(0, o.size() - 4);
    return o;
}

// 密钥掩码：已填写时一律显示为圆点，不泄露长度之外的信息
static std::wstring MaskSecret(const std::wstring& s)
{
    size_t n = (std::min)((size_t)12, s.size());
    return std::wstring(n, L'\x2022'); // 圆点 •
}

// ---------------- 生命周期 ----------------
SettingsView::SettingsView() = default;

void SettingsView::OnEnter()
{
    View::OnEnter();
    Load();
    BuildRows();
}

void SettingsView::OnLeave()
{
    if (m_editing) CancelEdit();
}

void SettingsView::Load()
{
    AppSettings s = CheckinStore::Instance().LoadSettings();
    m_dark = s.dark;
    m_reviewNudge = s.reviewNudge;
    m_reviewHour = ((s.reviewNudgeHour % 24) + 24) % 24;
    m_focusApps.clear();
    for (size_t i = 0; i < s.focusApps.size(); ++i) {
        if (i) m_focusApps += L", ";
        m_focusApps += s.focusApps[i];
    }

    ReadKanban();

    m_quiz = quiz::QuizStore::Instance().LoadSettings();
}

void SettingsView::ReadKanban()
{
    AppSettings s = CheckinStore::Instance().LoadSettings();
    m_kb.enabled = s.kanban.enabled;
    m_kb.activeFrom = s.kanban.activeFrom;
    m_kb.activeTo = s.kanban.activeTo;
    m_kb.dailyTokenBudget = s.kanban.dailyTokenBudget;
    m_kb.apiBase = s.kanban.apiBase;
    m_kb.apiKey = s.kanban.apiKey;
    m_kb.model = s.kanban.model.empty() ? L"deepseek-chat" : s.kanban.model;
    m_kb.personaCute = (s.kanban.persona == kanban::Persona::Cute);
    m_fromStr = FormatHM(m_kb.activeFrom);
    m_toStr = FormatHM(m_kb.activeTo);
    m_budgetStr = std::to_wstring(m_kb.dailyTokenBudget);
}

void SettingsView::Apply()
{
    AppSettings s = CheckinStore::Instance().LoadSettings();
    s.dark = m_dark;
    s.reviewNudge = m_reviewNudge;
    s.reviewNudgeHour = m_reviewHour;

    // 专注白名单：按逗号/空格/顿号切分并规整
    s.focusApps.clear();
    std::wstring cur;
    for (wchar_t c : m_focusApps) {
        if (c == L',' || c == L'\uff0c' || c == L' ' || c == L'\u3001' ||
            c == L';' || c == L'\uff1b' || c == L'\t' || c == L'\n' || c == L'\r') {
            if (!cur.empty()) { s.focusApps.push_back(NormProc(cur)); cur.clear(); }
        } else cur += c;
    }
    if (!cur.empty()) s.focusApps.push_back(NormProc(cur));
    s.focusAppsSeeded = true;

    // 看板娘：写回 kanban 段（SaveSettings 会同步落 kanban_ai.json）
    s.kanban.enabled = m_kb.enabled;
    s.kanban.activeFrom = m_kb.activeFrom;
    s.kanban.activeTo = m_kb.activeTo;
    s.kanban.dailyTokenBudget = m_kb.dailyTokenBudget;
    s.kanban.apiBase = m_kb.apiBase;
    s.kanban.apiKey = m_kb.apiKey;
    s.kanban.model = m_kb.model.empty() ? L"deepseek-chat" : m_kb.model;
    s.kanban.persona = m_kb.personaCute ? kanban::Persona::Cute : kanban::Persona::Tsundere;
    CheckinStore::Instance().SaveSettings(s);

    // 练考：落 quiz_settings.json 并即时推给调度器
    quiz::QuizSettings q = m_quiz;
    quiz::QuizStore::Instance().SaveSettings(q);
    QuizScheduler::Instance().SetConfig(q.enabled, q.hour, q.minute, q.category, q.qcount, q.rss);
}

void SettingsView::BuildRows()
{
    m_rows.clear();

    // —— 0 通用 ——
    m_rows.push_back(SRow{ .type = SRow::Toggle, .label = L"暗色主题（夜间档案室）", .pBool = &m_dark, .sec = 0 });
    m_rows.push_back(SRow{ .type = SRow::Toggle, .label = L"每日复盘提醒", .pBool = &m_reviewNudge, .sec = 0 });
    m_rows.push_back(SRow{ .type = SRow::Stepper, .label = L"提醒时刻", .pInt = &m_reviewHour,
                           .step = 1, .minv = 0, .maxv = 23, .unit = L"时", .sec = 0 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"专注白名单（逗号分隔，如 notepad）", .pStr = &m_focusApps, .sec = 0 });

    // —— 1 看板娘 ——
    m_rows.push_back(SRow{ .type = SRow::Toggle, .label = L"启用看板娘（绿井）", .pBool = &m_kb.enabled, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Toggle, .label = L"人格模式", .pBool = &m_kb.personaCute,
                           .onText = L"可爱", .offText = L"傲娇", .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"API 地址", .pStr = &m_kb.apiBase, .tag = TAG_APIBASE, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"API 密钥", .pStr = &m_kb.apiKey, .password = true, .tag = TAG_APIKEY, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"模型名", .pStr = &m_kb.model, .tag = TAG_MODEL, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"活跃起（HH:MM）", .pStr = &m_fromStr, .tag = TAG_FROM, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"活跃止（HH:MM）", .pStr = &m_toStr, .tag = TAG_TO, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"每日额度（token）", .pStr = &m_budgetStr, .tag = TAG_BUDGET, .sec = 1 });

    // —— 2 练考 ——
    m_rows.push_back(SRow{ .type = SRow::Toggle, .label = L"启用每日练考", .pBool = &m_quiz.enabled, .sec = 2 });
    m_rows.push_back(SRow{ .type = SRow::Stepper, .label = L"出题时刻（时）", .pInt = &m_quiz.hour,
                           .step = 1, .minv = 0, .maxv = 23, .unit = L"时", .sec = 2 });
    m_rows.push_back(SRow{ .type = SRow::Stepper, .label = L"出题时刻（分）", .pInt = &m_quiz.minute,
                           .step = 5, .minv = 0, .maxv = 59, .unit = L"分", .sec = 2 });
    m_rows.push_back(SRow{ .type = SRow::Stepper, .label = L"题量", .pInt = &m_quiz.qcount,
                           .step = 1, .minv = 1, .maxv = 50, .unit = L"题", .sec = 2 });
    m_rows.push_back(SRow{ .type = SRow::Toggle, .label = L"抓取 RSS 资讯", .pBool = &m_quiz.rss, .sec = 2 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"知识类别", .pStr = &m_quiz.category, .tag = TAG_CAT, .sec = 2 });
}

// ---------------- 布局 ----------------
void SettingsView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    m_cv = &cv;
    m_area = area;
    if (m_rows.empty()) { Load(); BuildRows(); }

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;
    float ix = x0 + 26.0f, right = x0 + contentW - 26.0f;

    float y = area.top + 24.0f;
    m_header = { x0, y, x0 + contentW, y + 64.0f };
    y = m_header.bottom + 16.0f;

    int counts[3] = { 0, 0, 0 };
    for (auto& r : m_rows) counts[r.sec]++;

    const float rowH = 50.0f, titleH = 40.0f, padB = 14.0f, gap = 16.0f;
    for (int sec = 0; sec < 3; ++sec) {
        int n = counts[sec];
        float cardH = titleH + (float)n * rowH + padB;
        D2D1_RECT_F card = { x0, y, x0 + contentW, y + cardH };
        m_secCards[sec] = card;

        float ry = card.top + titleH;
        for (auto& r : m_rows) {
            if (r.sec != sec) continue;
            r.rect = { ix, ry, right, ry + rowH };
            if (r.type == SRow::Toggle) {
                float th = 26.0f, tw = 46.0f;
                r.toggle = { right - tw, ry + (rowH - th) * 0.5f, right, ry + (rowH + th) * 0.5f };
            } else if (r.type == SRow::Stepper) {
                float btn = 30.0f, bw = 64.0f, bh = 30.0f, sp = 6.0f;
                r.inc   = { right - btn, ry + (rowH - btn) * 0.5f, right, ry + (rowH + btn) * 0.5f };
                r.value = { right - btn - bw - sp, ry + (rowH - bh) * 0.5f, right - btn - sp, ry + (rowH + bh) * 0.5f };
                r.dec   = { right - btn - bw - sp - btn - sp, ry + (rowH - btn) * 0.5f, right - btn - bw - sp, ry + (rowH + btn) * 0.5f };
            } else {
                r.box = { right - 300.0f, ry + 8.0f, right, ry + rowH - 8.0f };
            }
            ry += rowH;
        }
        y = card.bottom + gap;
    }

    const float bw = 140.0f, bh = 44.0f;
    m_backBtn = { x0, y + 4.0f, x0 + bw, y + 4.0f + bh };
    y = m_backBtn.bottom + 40.0f;
    SetContentHeight(y - area.top);
}

// ---------------- 更新 ----------------
void SettingsView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    if (m_editing) m_caretT += dt;
    m_caretOn = ((int)(m_caretT * 2.0f) % 2) == 0;

    float mx = in.mouseX;
    float my = in.mouseY + ScrollY();

    // 编辑态：鼠标先交给字段（点击定位光标 / 拖拽框选），点字段外才提交并吞掉本次点击
    if (m_editing && m_active && m_cv) {
        TextStyle tx; tx.role = FontRole::Sans; tx.size = 13.5f; tx.vAlign = VAlign::Middle;
        bool inside = m_edit.HandleMouse(in, *m_cv, m_active->box, tx, ScrollY(), 10.0f);
        if (!inside && in.clicked) {
            CommitEdit();
            return;
        }
    }

    // 返回
    if (in.clicked && InRect(m_backBtn, mx, my)) { Go(L"home"); return; }

    for (auto& r : m_rows) {
        if (!InRect(r.rect, mx, my)) continue;
        if (!in.clicked) break;
        if (r.type == SRow::Toggle) {
            if (r.pBool) {
                bool wasDark = m_dark;
                *r.pBool = !*r.pBool;
                Apply();
                if (r.pBool == &m_dark && m_dark != wasDark) AppSyncTheme(true);
            }
            return;
        } else if (r.type == SRow::Stepper) {
            if (InRect(r.dec, mx, my)) {
                if (r.pInt) { *r.pInt = (std::max)(r.minv, *r.pInt - r.step); Apply(); }
                return;
            }
            if (InRect(r.inc, mx, my)) {
                if (r.pInt) { *r.pInt = (std::min)(r.maxv, *r.pInt + r.step); Apply(); }
                return;
            }
            return;
        } else { // Text → 进入编辑
            BeginEdit(r);
            return;
        }
    }
}

// ---------------- 编辑（v2 统一输入框）----------------
void SettingsView::BeginEdit(SRow& r)
{
    if (m_editing) CommitEdit();
    m_active = &r;
    m_editing = true;
    // 1×1 透明代理只收键盘 + IME，字段上无任何 GDI 子窗口（无白块）；
    // 文字/光标/选区/IME 组合串全由 D3D 绘制。
    m_edit.onEnter     = [this] { CommitEdit(); };
    m_edit.onEsc       = [this] { CancelEdit(); };
    m_edit.onKillFocus = [this] { CommitEdit(); };
    std::wstring cur = r.pStr ? *r.pStr : L"";
    m_edit.Begin(cur, r.password, 13.5f);
    m_caretT = 0.0f;
}

void SettingsView::CommitEdit()
{
    if (!m_editing) { m_active = nullptr; return; }
    std::wstring buf;
    m_edit.End(true, buf);
    if (m_active) {
        switch (m_active->tag) {
            case TAG_FROM: { int m = 0; if (ParseHM(buf, m)) m_kb.activeFrom = m; break; }
            case TAG_TO:   { int m = 0; if (ParseHM(buf, m)) m_kb.activeTo = m; break; }
            case TAG_BUDGET: {
                int v = 0;
                if (!buf.empty()) { try { v = std::stoi(buf); } catch (...) { v = 0; } }
                m_kb.dailyTokenBudget = (std::max)(0, v);
                break;
            }
            default:
                if (m_active->pStr) *m_active->pStr = buf;
                break;
        }
    }
    // 重算显示字符串（HH:MM / 数值）
    m_fromStr = FormatHM(m_kb.activeFrom);
    m_toStr = FormatHM(m_kb.activeTo);
    m_budgetStr = std::to_wstring(m_kb.dailyTokenBudget);

    m_editing = false;
    m_active = nullptr;
    Apply();
}

void SettingsView::CancelEdit()
{
    m_edit.Cancel();
    m_editing = false;
    m_active = nullptr;
}

// ---------------- 绘制 ----------------
void SettingsView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01f(m_entered / 0.25f);
    cv.PushOpacity(a);
    float s = ScrollY();
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    // 页眉
    TextStyle ht; ht.role = FontRole::Sans; ht.size = 30.0f; ht.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"设置", m_header, ht, pal.ink900);
    TextStyle hs; hs.role = FontRole::Mono; hs.size = 11.0f; hs.letterSpacing = 2.0f; hs.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SETTINGS · 全局设置", { m_header.left, m_header.top + 38.0f, m_header.right, m_header.top + 54.0f }, hs, pal.ink300);

    const wchar_t* titles[3] = { L"SECTION · 通用", L"SECTION · 看板娘", L"SECTION · 练考" };
    for (int sec = 0; sec < 3; ++sec) {
        D2D1_RECT_F card = m_secCards[sec];
        cv.FillRoundRect(card, shape::kEdge, pal.paperHi);
        cv.StrokeRoundRect(card, shape::kEdge, pal.rule, shape::kHair);
        TextStyle st; st.role = FontRole::Mono; st.size = 10.5f; st.letterSpacing = 2.4f; st.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(titles[sec], { card.left + 26.0f, card.top + 12.0f, card.right - 26.0f, card.top + 30.0f }, st, pal.ink300);

        for (auto& r : m_rows) {
            if (r.sec != sec) continue;
            cv.Line(card.left + 26.0f, r.rect.top, card.right - 26.0f, r.rect.top, WithAlpha(pal.rule, 0.6f), shape::kHair);
            TextStyle ls; ls.role = FontRole::Sans; ls.size = 14.0f; ls.vAlign = VAlign::Middle;
            cv.Text(r.label, { r.rect.left, r.rect.top, r.rect.right - 170.0f, r.rect.bottom }, ls, pal.ink700);

            if (r.type == SRow::Toggle)      PaintToggle(cv, r.toggle, r.pBool ? *r.pBool : false, pal);
            else if (r.type == SRow::Stepper) PaintStepper(cv, r, pal);
            else                              PaintTextBox(cv, r, pal);
        }
    }

    // 返回按钮
    cv.FillRoundRect(m_backBtn, shape::kEdge, pal.paperLo);
    cv.StrokeRoundRect(m_backBtn, shape::kEdge, pal.rule, shape::kHair);
    TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.0f; bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle;
    cv.Text(L"\u2190 返回", m_backBtn, bs, pal.ink700);

    cv.PopTransform();
    cv.PopOpacity();
}

void SettingsView::PaintToggle(Canvas& cv, const D2D1_RECT_F& r, bool on, const Palette& pal)
{
    float rad = (r.bottom - r.top) * 0.5f;
    cv.FillRoundRect(r, rad, on ? pal.jade : pal.rule);
    float knob = rad - 3.0f;
    float kx = on ? (r.right - rad) : (r.left + rad);
    cv.FillCircle(kx, (r.top + r.bottom) * 0.5f, knob, pal.paperHi);
}

void SettingsView::PaintStepper(Canvas& cv, SRow& r, const Palette& pal)
{
    cv.FillRoundRect(r.value, shape::kEdgeSoft, pal.paperLo);
    cv.StrokeRoundRect(r.value, shape::kEdgeSoft, pal.rule, shape::kHair);
    TextStyle ts; ts.role = FontRole::Mono; ts.size = 15.0f; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
    std::wstring val = std::to_wstring(r.pInt ? *r.pInt : 0) + (r.unit.empty() ? L"" : (L" " + r.unit));
    cv.Text(val, r.value, ts, pal.ink900);

    PaintStepBtn(cv, r.dec, L"\u2212", pal);  // −
    PaintStepBtn(cv, r.inc, L"+", pal);
}

void SettingsView::PaintStepBtn(Canvas& cv, const D2D1_RECT_F& r, const wchar_t* sym, const Palette& pal)
{
    cv.FillRoundRect(r, shape::kEdgeSoft, pal.paperLo);
    cv.StrokeRoundRect(r, shape::kEdgeSoft, pal.rule, shape::kHair);
    TextStyle ts; ts.role = FontRole::Sans; ts.size = 18.0f; ts.weight = DWRITE_FONT_WEIGHT_BOLD;
    ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
    cv.Text(sym, r, ts, pal.ink700);
}

void SettingsView::PaintTextBox(Canvas& cv, SRow& r, const Palette& pal)
{
    D2D1_RECT_F box = r.box;
    bool editingThis = (m_editing && m_active == &r);
    cv.FillRoundRect(box, shape::kEdgeSoft, editingThis ? pal.paperHi : pal.paperLo);
    cv.StrokeRoundRect(box, shape::kEdgeSoft, pal.rule, shape::kHair);

    const float pad = 10.0f;
    D2D1_RECT_F tbox = { box.left + pad, box.top, box.right - pad, box.bottom };
    TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.5f; ts.vAlign = VAlign::Middle;

    if (editingThis) {
        // v2 统一输入框：文字/光标/选区/IME 组合串全由 D3D 绘制（1×1 透明代理）
        m_edit.Paint(cv, tbox, ts, pal.ink900, L"点击填写\u2026", pal.ink300, 0.0f, ScrollY());
    } else {
        std::wstring disp;
        if (r.password) disp = (r.pStr && !r.pStr->empty()) ? MaskSecret(*r.pStr) : L"";
        else            disp = r.pStr ? *r.pStr : L"";
        if (disp.empty()) cv.Text(L"点击填写\u2026", tbox, ts, pal.ink300);
        else              cv.Text(disp, tbox, ts, pal.ink900);
    }
}

void SettingsView::DebugForcePreview()
{
    if (m_rows.empty()) { Load(); BuildRows(); }
    m_editing = false;
    m_active = nullptr;
}

} // namespace lj
