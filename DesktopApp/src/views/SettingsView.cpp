// ============================================================
//  SettingsView.cpp — 全局设置页（route=settings）
//  由主窗口右上「用户名卡片」弹层中的「设置」项进入（TopBar 注入）。
//  两分区：通用 / 练考（含 AI 出题凭据）。
//  持久化刻意走 CheckinStore::LoadSettings/SaveSettings 单一通道；
//  AI 凭据落账户目录 ai.json（本地独占，不参与云端同步）。
// ============================================================
#include "views/SettingsView.h"
#include "core/Hwnd.h"
#include "app/AccountStore.h"
#include <windows.h>
#include <wincodec.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

namespace lj {

// ---------------- 本地工具 ----------------
static float Clamp01f(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static std::wstring ToUtf8(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &u[0], n, nullptr, nullptr);
    return std::wstring(u.begin(), u.end());   // 仅承载字节，写盘前按 char 取
}

static std::wstring Utf8ToWide(const std::string& s)
{
    int wl = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(wl, L'\0');
    if (wl > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], wl);
    return w;
}

static std::string ToUtf8Bytes(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string u(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &u[0], n, nullptr, nullptr);
    return u;
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
    m_focusFs = s.focusFullscreen;
    m_reviewHour = ((s.reviewNudgeHour % 24) + 24) % 24;
    m_exitAction = (s.exitAction >= 0 && s.exitAction <= 2) ? s.exitAction : 0;
    m_focusApps.clear();
    for (size_t i = 0; i < s.focusApps.size(); ++i) {
        if (i) m_focusApps += L", ";
        m_focusApps += s.focusApps[i];
    }

    ReadAI();

    m_quiz = quiz::QuizStore::Instance().LoadSettings();
}

// ---------------- AI 出题凭据（本地 ai.json）----------------
void SettingsView::ReadAI()
{
    m_aiBase.clear(); m_aiKey.clear(); m_aiModel.clear();
    // 兼容旧文件名：历史版本的 kanban_ai.json 作为底稿迁入
    std::wstring root = AccountStore::Instance().CurrentRoot();
    for (const wchar_t* name : { L"ai.json", L"kanban_ai.json" }) {
        FILE* f = _wfopen((root + name).c_str(), L"rb");
        if (!f) continue;
        std::string buf;
        char chunk[4096];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) buf.append(chunk, n);
        fclose(f);
        // 极简 JSON：只抓 apiBase / apiKey / model 三个字符串字段
        auto grab = [&](const char* key) -> std::wstring {
            std::string pat = std::string("\"") + key + "\":";
            size_t p = buf.find(pat);
            if (p == std::string::npos) return L"";
            p = buf.find('"', p + pat.size());
            if (p == std::string::npos) return L"";
            std::string out;
            for (++p; p < buf.size() && buf[p] != '"'; ++p) {
                if (buf[p] == '\\' && p + 1 < buf.size()) { ++p; out += buf[p]; }
                else out += buf[p];
            }
            return Utf8ToWide(out);
        };
        m_aiBase  = grab("apiBase");
        m_aiKey   = grab("apiKey");
        m_aiModel = grab("model");
        if (!m_aiKey.empty() || !m_aiBase.empty()) {
            if (wcscmp(name, L"ai.json") != 0) Apply();   // 首次从旧文件迁入 → 立即落新文件
            break;
        }
        m_aiBase.clear(); m_aiKey.clear(); m_aiModel.clear();
    }
    if (m_aiModel.empty()) m_aiModel = L"deepseek-chat";
}

void SettingsView::Apply()
{
    AppSettings s = CheckinStore::Instance().LoadSettings();
    s.dark = m_dark;
    s.reviewNudge = m_reviewNudge;
    s.focusFullscreen = m_focusFs;
    s.reviewNudgeHour = m_reviewHour;
    s.exitAction = (m_exitAction >= 0 && m_exitAction <= 2) ? m_exitAction : 0;

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

    CheckinStore::Instance().SaveSettings(s);

    // AI 出题凭据：落账户目录 ai.json（本地独占，不参与云端同步）
    {
        std::string out = "{\n";
        out += "  \"apiBase\": \"" + ToUtf8Bytes(m_aiBase) + "\",\n";
        out += "  \"apiKey\": \""  + ToUtf8Bytes(m_aiKey)  + "\",\n";
        out += "  \"model\": \""   + ToUtf8Bytes(m_aiModel) + "\"\n";
        out += "}\n";
        std::wstring path = AccountStore::Instance().CurrentRoot() + L"ai.json";
        FILE* f = _wfopen(path.c_str(), L"wb");
        if (f) { fwrite(out.data(), 1, out.size(), f); fclose(f); }
    }

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
    m_rows.push_back(SRow{ .type = SRow::Toggle, .label = L"专注时全屏覆盖（鼠标静止 2 秒渐显信息）", .pBool = &m_focusFs, .sec = 0 });
    m_rows.push_back(SRow{ .type = SRow::Stepper, .label = L"提醒时刻", .pInt = &m_reviewHour,
                           .step = 1, .minv = 0, .maxv = 23, .unit = L"时", .sec = 0 });
    m_rows.push_back(SRow{ .type = SRow::Stepper, .label = L"点关闭按钮时", .pInt = &m_exitAction,
                           .step = 1, .minv = 0, .maxv = 2, .tag = TAG_EXIT, .sec = 0 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"专注白名单（逗号分隔，如 notepad）", .pStr = &m_focusApps, .sec = 0 });

    // —— 1 练考（AI 出题凭据本地独占，不入库不上云）——
    m_rows.push_back(SRow{ .type = SRow::Toggle, .label = L"启用每日练考", .pBool = &m_quiz.enabled, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Stepper, .label = L"出题时刻（时）", .pInt = &m_quiz.hour,
                           .step = 1, .minv = 0, .maxv = 23, .unit = L"时", .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Stepper, .label = L"出题时刻（分）", .pInt = &m_quiz.minute,
                           .step = 5, .minv = 0, .maxv = 59, .unit = L"分", .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Stepper, .label = L"题量", .pInt = &m_quiz.qcount,
                           .step = 1, .minv = 1, .maxv = 50, .unit = L"题", .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Toggle, .label = L"抓取 RSS 资讯", .pBool = &m_quiz.rss, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"知识类别", .pStr = &m_quiz.category, .tag = TAG_CAT, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"AI API 地址", .pStr = &m_aiBase, .tag = TAG_AIBASE, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"AI API 密钥", .pStr = &m_aiKey, .password = true, .tag = TAG_AIKEY, .sec = 1 });
    m_rows.push_back(SRow{ .type = SRow::Text, .label = L"AI 模型名", .pStr = &m_aiModel, .tag = TAG_AIMODEL, .sec = 1 });
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

    int counts[2] = { 0, 0 };
    for (auto& r : m_rows) counts[r.sec]++;

    const float rowH = 50.0f, titleH = 40.0f, padB = 14.0f, gap = 16.0f;
    for (int sec = 0; sec < 2; ++sec) {
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
                float btn = 30.0f, bw = (r.tag == TAG_EXIT) ? 126.0f : 64.0f, bh = 30.0f, sp = 6.0f;
                r.inc   = { right - btn, ry + (rowH - btn) * 0.5f, right, ry + (rowH + btn) * 0.5f };
                r.value = { right - btn - bw - sp, ry + (rowH - bh) * 0.5f, right - btn - sp, ry + (rowH + bh) * 0.5f };
                r.dec   = { right - btn - bw - sp - btn - sp, ry + (rowH - btn) * 0.5f, right - btn - bw - sp, ry + (rowH + btn) * 0.5f };
            } else {
                // 批次 H：白名单行 box 左移 160px，右侧留出「+文件 / +运行中」按钮列
                float boxR = (r.pStr == &m_focusApps) ? right - 160.0f : right;
                r.box = { boxR - 300.0f, ry + 8.0f, boxR, ry + rowH - 8.0f };
                if (r.pStr == &m_focusApps) {
                    m_wlFileBtn = { boxR + 8.0f,  ry + 8.0f,  boxR + 76.0f,  ry + rowH - 8.0f };
                    m_wlProcBtn = { boxR + 84.0f, ry + 8.0f,  boxR + 152.0f, ry + rowH - 8.0f };
                }
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

    // 批次 H：正在运行软件浮层打开时独占输入
    if (m_procOpen) {
        if (in.wheel != 0.0f) {
            m_procScroll -= in.wheel * 26.0f;
            m_procScroll = (std::max)(0.0f, (std::min)(m_procScroll,
                (float)(std::max)(0, (int)m_procs.size() * 26 - 300)));
        }
        if (in.clicked) {
            bool hitRow = false;
            for (size_t i = 0; i < m_procRows.size(); ++i) {
                if (InRect(m_procRows[i], mx, my)) {
                    AddWhitelist(m_procs[i]);
                    m_procOpen = false;
                    hitRow = true;
                    break;
                }
            }
            if (!hitRow && !InRect(m_procPanel, mx, my)) m_procOpen = false;   // 点外关闭
        }
        return;   // 浮层独占
    }

    for (auto& r : m_rows) {
        if (!InRect(r.rect, mx, my)) continue;
        if (!in.clicked) break;

        // 批次 H：白名单行的「+文件 / +运行中」快速添加按钮
        if (r.type == SRow::Text && r.pStr == &m_focusApps) {
            if (InRect(m_wlFileBtn, mx, my)) { BrowseWhitelistFile(); return; }
            if (InRect(m_wlProcBtn, mx, my)) { OpenProcList(); return; }
        }

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
            case TAG_AIBASE:  m_aiBase = buf; break;
            case TAG_AIKEY:   m_aiKey = buf; break;
            case TAG_AIMODEL: m_aiModel = buf.empty() ? L"deepseek-chat" : buf; break;
            default:
                if (m_active->pStr) *m_active->pStr = buf;
                break;
        }
    }
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

    const wchar_t* titles[2] = { L"SECTION · 通用", L"SECTION · 练考" };
    for (int sec = 0; sec < 2; ++sec) {
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

            // 批次 H：白名单行「+文件 / +运行中」快速添加按钮
            if (r.type == SRow::Text && r.pStr == &m_focusApps) {
                auto DrawAddBtn = [&](const D2D1_RECT_F& br, const wchar_t* label) {
                    cv.FillRoundRect(br, 5.0f, pal.paperLo);
                    cv.StrokeRoundRect(br, 5.0f, pal.rule, shape::kHair);
                    TextStyle bs2; bs2.role = FontRole::Sans; bs2.size = 11.5f;
                    bs2.hAlign = HAlign::Center; bs2.vAlign = VAlign::Middle;
                    cv.Text(label, br, bs2, pal.ink700);
                };
                DrawAddBtn(m_wlFileBtn, L"+ 文件");
                DrawAddBtn(m_wlProcBtn, L"+ 运行中");
            }
        }
    }

    // 批次 H：正在运行软件选择浮层（最上层）
    if (m_procOpen) PaintProcList(cv);

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
    std::wstring val;
    if (r.tag == TAG_EXIT) {
        // 关闭按钮行为：数值 → 文案
        static const wchar_t* kExitModes[] = { L"每次询问", L"直接退出", L"最小化到托盘" };
        int v = r.pInt ? *r.pInt : 0;
        if (v < 0 || v > 2) v = 0;
        ts.size = 13.0f;
        val = kExitModes[v];
    } else {
        val = std::to_wstring(r.pInt ? *r.pInt : 0) + (r.unit.empty() ? L"" : (L" " + r.unit));
    }
    cv.Text(val, r.value, ts, pal.ink900);

    PaintStepBtn2(cv, r.dec, L"\u2212", pal);  // −
    PaintStepBtn2(cv, r.inc, L"+", pal);
}

void SettingsView::PaintStepBtn2(Canvas& cv, const D2D1_RECT_F& r, const wchar_t* sym, const Palette& pal)
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

// ============================================================
//  批次 H：专注白名单快速添加（选文件 / 选正在运行的软件）
// ============================================================
void SettingsView::AddWhitelist(const std::wstring& exeName)
{
    if (exeName.empty()) return;
    // 规整：去路径/去 .exe/转小写（与 NormProc 同口径）
    std::wstring name = exeName;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos && _wcsicmp(name.c_str() + dot, L".exe") == 0) name = name.substr(0, dot);
    for (auto& c : name) c = (wchar_t)towlower(c);
    if (name.empty()) return;

    // 去重后追加到逗号串
    std::wstring cur = m_focusApps;
    std::wstring low = cur;
    for (auto& c : low) c = (wchar_t)towlower(c);
    std::wstring lowName = name;
    for (auto& c : lowName) c = (wchar_t)towlower(c);
    if (low.find(lowName) != std::wstring::npos) return;   // 已存在
    if (!cur.empty()) cur += L", ";
    m_focusApps = cur + name;
    Apply();
}

void SettingsView::BrowseWhitelistFile()
{
    wchar_t path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = L"程序 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"选择要加入专注白名单的程序";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) AddWhitelist(path);
}

void SettingsView::OpenProcList()
{
    m_procs.clear();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            std::vector<std::wstring> all;
            do {
                if (pe.szExeFile[0]) all.push_back(pe.szExeFile);
            } while (Process32NextW(snap, &pe));
            CloseHandle(snap);
            std::sort(all.begin(), all.end());
            all.erase(std::unique(all.begin(), all.end()), all.end());
            m_procs = std::move(all);
        }
    }
    m_procScroll = 0.0f;
    m_procOpen = true;
}

// 浮层：居中面板 + 可滚动进程列表（点击加入白名单，点外关闭）
void SettingsView::PaintProcList(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float W = m_area.right - m_area.left;
    float H = m_area.bottom - m_area.top;

    cv.FillRect(m_area, WithAlpha(pal.ink900, 0.45f));
    float pw = 380.0f, ph = 380.0f;
    float px = (W - pw) * 0.5f, py = (H - ph) * 0.5f;
    m_procPanel = { px, py, px + pw, py + ph };

    cv.PaperCard(m_procPanel, 0.4f, shape::kEdge);
    cv.StrokeRoundRect(m_procPanel, shape::kEdge, pal.rule, shape::kHair);

    TextStyle hs; hs.role = FontRole::Mono; hs.size = 11.0f; hs.letterSpacing = 2.0f;
    hs.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"正在运行的软件", { px + 20.0f, py + 16.0f, px + pw - 20.0f, py + 34.0f }, hs, pal.ink500);

    float ly = py + 48.0f;
    float lh = 26.0f;
    int visible = (int)((py + ph - 20.0f - ly) / lh);
    m_procRows.clear();
    cv.PushClip({ px + 8.0f, ly, px + pw - 8.0f, py + ph - 16.0f });
    int maxOff = (int)m_procs.size() - visible;
    int first = (int)(m_procScroll / lh);
    for (int i = first; i < (int)m_procs.size(); ++i) {
        float ry = ly + (float)(i - first) * lh - (m_procScroll - (float)first * lh);
        if (ry > py + ph - 14.0f) break;
        D2D1_RECT_F row{ px + 14.0f, ry, px + pw - 14.0f, ry + lh };
        if (ry >= ly - 1.0f) {
            m_procRows.push_back(row);
            TextStyle ts; ts.role = FontRole::Mono; ts.size = 12.0f; ts.vAlign = VAlign::Middle;
            cv.Text(m_procs[i], { row.left + 8.0f, row.top, row.right - 8.0f, row.bottom }, ts, pal.ink700);
        }
    }
    cv.PopClip();

    TextStyle ft; ft.role = FontRole::Sans; ft.size = 11.0f;
    cv.Text(L"点击条目加入白名单 · 点空白处关闭",
            { px + 20.0f, py + ph - 30.0f, px + pw - 20.0f, py + ph - 12.0f }, ft, pal.ink300);
}

void SettingsView::DebugForcePreview()
{
    if (m_rows.empty()) { Load(); BuildRows(); }
    m_editing = false;
    m_active = nullptr;
}

} // namespace lj
