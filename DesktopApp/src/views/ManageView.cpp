#include "views/ManageView.h"
#include "core/Hwnd.h"
#include "ui/Layout.h"
#include "ui/FieldText.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <commdlg.h>   // 导出 / 导入文件对话框
#include "app/Data.h"  // Content::ApplyCurrentAccount
#include "app/AccountStore.h"  // 导入确认框要报当前登录账户名

namespace lj {

namespace {
bool Hit(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}
D2D1_COLOR_F TagColor(const Palette& pal, const std::wstring& t)
{
    if (t == L"主线") return pal.seal;
    if (t == L"英语" || t == L"手绘") return pal.jade;
    if (t == L"申论" || t == L"常识") return pal.brass;
    if (t == L"作息") return pal.ink500;
    return pal.ink500;
}
} // namespace

static const wchar_t* kTags[] = {
    L"主线", L"维护", L"积累", L"模考", L"习惯",
    L"早起", L"睡觉", L"休息", L"英语", L"申论", L"常识", L"手绘"
};
static const int kTagN = (int)(sizeof(kTags) / sizeof(kTags[0]));

static const wchar_t* kGroupTitle[] = { L"工作日（每日固定）", L"周六专项", L"周日专项" };

// ============================================================
void ManageView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_bundle = CheckinStore::Instance().LoadItems();
    m_milestones = CheckinStore::Instance().LoadMilestones();
    m_changed = false;
    m_built = false;
    if (m_editingOn) CancelEdit();
}

void ManageView::OnLeave()
{
    if (m_editingOn) CancelEdit();
}

void ManageView::BeginEdit(const FieldHit& fh)
{
    if (m_editingOn) CommitEdit();   // 收尾上一个字段
    m_editing = fh;
    m_editingOn = true;
    // v2 统一输入框：1×1 透明代理只收键盘 + IME，字段上无任何 GDI 子窗口（无白块）；
    // 文字/光标/选区/IME 组合串全由 D3D 绘制。
    m_edit.onEnter     = [this] { CommitEdit(); };
    m_edit.onEsc       = [this] { CancelEdit(); };
    m_edit.onKillFocus = [this] { CommitEdit(); };

    std::wstring cur;
    if (fh.g == 3) {
        if (fh.i < 0 || fh.i >= (int)m_milestones.size()) { m_editingOn = false; return; }
        auto& ms = m_milestones[fh.i];
        switch (fh.f) {
            case F_MLABEL: cur = ms.label; break;
            case F_MDATE:  cur = FormatDate(ms.date); break;
            case F_MNOTE:  cur = ms.note; break;
        }
    } else {
        auto* grp = (fh.g == 0) ? &m_bundle.daily
                  : (fh.g == 1) ? &m_bundle.sat  : &m_bundle.sun;
        if (fh.i < 0 || fh.i >= (int)grp->size()) { m_editingOn = false; return; }
        auto& it = (*grp)[fh.i];
        switch (fh.f) {
            case F_TIME:   cur = it.slot; break;
            case F_TITLE:  cur = it.title; break;
            case F_DUR:    cur = it.minutes > 0 ? std::to_wstring(it.minutes) : L""; break;
            case F_LINK:   cur = it.link; break;
            case F_FOLDER: cur = it.folder; break;
        }
    }
    m_edit.Begin(cur, false, 13.0f);
}

void ManageView::DebugForceOpen()
{
    // 截图自检：强制打开首个打卡项字段编辑，验证隐藏代理 + D3D 自绘无覆盖块
    FieldHit fh{ {0,0,0,0}, 0, 0, F_TIME };
    if (!m_bundle.daily.empty())       fh = { {0,0,0,0}, 0, 0, F_TIME };
    else if (!m_bundle.sat.empty())    fh = { {0,0,0,0}, 1, 0, F_TIME };
    else if (!m_bundle.sun.empty())    fh = { {0,0,0,0}, 2, 0, F_TIME };
    else if (!m_milestones.empty())    fh = { {0,0,0,0}, 3, 0, F_MLABEL };
    BeginEdit(fh);
}

void ManageView::CommitEdit()
{
    if (!m_editingOn) { m_editingOn = false; return; }
    std::wstring txt;
    m_edit.End(true, txt);
    // 【闪退修复】旧实现 `auto& it = (g==0)?daily[i]:(g==1)?sat[i]:sun[i]`
    //  在 g==3（关键倒计时）时三元链落到 sun[i]——sun 为空/过短即越界崩溃。
    //  现按组分开取，并加下标保护。
    if (m_editing.g == 3) {
        if (m_editing.i >= 0 && m_editing.i < (int)m_milestones.size()) {
            auto& ms = m_milestones[m_editing.i];
            switch (m_editing.f) {
                case F_MLABEL: ms.label = txt; break;
                case F_MDATE:  {
                    int y = 2027, m = 1, d = 1;
                    if (swscanf_s(txt.c_str(), L"%d-%d-%d", &y, &m, &d) == 3) ms.date = { y, m, d };
                    break;
                }
                case F_MNOTE:  ms.note = txt; break;
            }
            m_changed = true;
        }
    } else {
        auto* grp = (m_editing.g == 0) ? &m_bundle.daily
                  : (m_editing.g == 1) ? &m_bundle.sat  : &m_bundle.sun;
        if (m_editing.i >= 0 && m_editing.i < (int)grp->size()) {
            auto& it = (*grp)[m_editing.i];
            switch (m_editing.f) {
                case F_TIME:   it.slot = txt; break;
                case F_TITLE:  it.title = txt; break;
                case F_DUR:    { int v = 0; swscanf_s(txt.c_str(), L"%d", &v); it.minutes = v < 0 ? 0 : v; } break;
                case F_LINK:   it.link = txt; break;
                case F_FOLDER: it.folder = txt; break;
            }
            m_changed = true;
        }
    }
    m_editingOn = false;
}

void ManageView::CancelEdit()
{
    m_edit.Cancel();
    m_editingOn = false;
}

void ManageView::Save()
{
    if (m_editingOn) CommitEdit();
    CheckinStore::Instance().SaveItems(m_bundle);
    CheckinStore::Instance().SaveMilestones(m_milestones);
    // 同步到内存 Content，使首页倒计时立即刷新（无需重新进账户）
    Content cur = Content::Get();
    cur.milestones = m_milestones;
    Content::SetActive(cur);
    m_changed = false;
}

void ManageView::ReloadDefault()
{
    if (m_editingOn) CancelEdit();
    CheckinStore::Instance().ResetItems();
    m_bundle = CheckinStore::Instance().LoadItems();
    m_changed = false;
    m_built = false;
    Layout(m_areaCached, *m_cvCached);
}

void ManageView::DoExport()
{
    HWND owner = AppHwnd();
    wchar_t path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"FLORI 备份 (*.flori.json)\0*.flori.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"flori.json";
    ofn.lpstrInitialDir = nullptr;
    ofn.lpstrTitle = L"导出整账户备份";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return;        // 用户取消
    std::wstring err;
    if (CheckinStore::Instance().ExportAll(path, err))
        Toast(L"已导出备份：" + std::wstring(path), false);
    else
        Toast(L"导出失败：" + err, true);
}

void ManageView::DoImport()
{
    HWND owner = AppHwnd();
    wchar_t path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"FLORI 备份 (*.flori.json)\0*.flori.json\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"从备份恢复当前账户";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;        // 用户取消

    // 导入 = 整账户替换，不是合并。覆盖前先把这份备份的来历摆出来让人核对，
    // 否则选错一个文件，打卡记录 / 专栏 / 设置全没了，且不可逆。
    std::wstring err;
    CheckinStore::BackupInfo bi;
    if (!CheckinStore::Instance().PeekBackup(path, bi, err)) {
        Toast(L"导入失败：" + err, true);
        return;
    }

    std::wstring when = L"未知时间";
    if (bi.exportedAt > 0) {
        wchar_t tb[32] = { 0 };
        time_t t = (time_t)bi.exportedAt;
        tm tmv{};
        localtime_s(&tmv, &t);
        wcsftime(tb, 32, L"%Y-%m-%d %H:%M", &tmv);
        when = tb;
    }
    const std::wstring cur = AccountStore::Instance().CurrentName();
    std::wstring msg =
        L"即将用这份备份【整体替换】当前账户的本地数据：\n\n"
        L"　备份来源账户：" + (bi.account.empty() ? std::wstring(L"（未记录）") : bi.account) + L"\n"
        L"　备份导出时间：" + when + L"\n"
        L"　包含数据块：" + std::to_wstring(bi.blocks) + L" 块\n"
        L"　当前登录账户：" + cur + L"\n\n";
    if (!bi.account.empty() && bi.account != cur)
        msg += L"⚠ 备份来自另一个账户「" + bi.account + L"」，导入后当前账户的数据会被它顶掉。\n\n";
    msg += L"打卡记录、专栏、收藏、设置等都会被覆盖，此操作不可撤销。\n"
           L"（会先自动存一份当前状态到账户目录，导错了可以再导回来）\n\n确定继续吗？";

    if (MessageBoxW(owner, msg.c_str(), L"确认导入备份",
                    MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES)
        return;

    // 后悔药：先把当前状态另存一份，再动手覆盖
    const std::wstring roll = CheckinStore::Instance().SaveRollbackSnapshot();

    if (CheckinStore::Instance().ImportAll(path, err)) {
        Content::ApplyCurrentAccount();
        CheckinStore::Instance().Reload();
        AppSyncTheme(true);
        m_bundle = CheckinStore::Instance().LoadItems();
        m_changed = false;
        m_built = false;
        Toast(roll.empty() ? L"已从备份恢复当前账户"
                           : L"已从备份恢复；覆盖前的旧数据已存为 " + roll, false);
    } else {
        Toast(L"导入失败：" + err, true);
    }
}

void ManageView::Toast(const std::wstring& msg, bool bad)
{
    m_toast = msg;
    m_toastBad = bad;
    m_toastT = 4.0f;   // 显示约 4 秒后淡出
}

// ============================================================
void ManageView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    m_areaCached = area;
    m_cvCached = &cv;
    m_t += 0.0f;

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;

    m_fields.clear();
    m_rowBtn.clear();
    m_groupAdd.clear();
    m_groupRects.clear();
    for (int g = 0; g < 3; ++g) m_rowGeom[g].clear();
    m_msGeom.clear();

    float y = area.top + 30.0f;
    m_contentTop = y;
    y += 92.0f;   // 标题 + lead

    const float kPad = 14.0f;            // 卡片内边距
    const float kNarrow = 432.0f;        // 极窄窗口阈值：低于此宽触发三行重排，根治右簇碰撞
    bool narrow = contentW < kNarrow;

    for (int g = 0; g < 3; ++g) {
        auto& grp = (g == 0) ? m_bundle.daily : (g == 1) ? m_bundle.sat : m_bundle.sun;
        float headH = 40.0f;
        const float kRowGap = 8.0f;
        float fh = narrow ? 30.0f : 32.0f;
        // 行背景高与旧 kRowH 对应（正常 100 -> bg 92，窄窗 144 -> bg 136），
        // 先保证 0% 回归；后续只要把 rowBgH 改为按内容测量即可动态防重叠。
        float rowBgH = narrow ? 136.0f : 92.0f;
        float xL = x0 + kPad;
        float xR = x0 + contentW - kPad;
        float rowW = contentW - kPad * 2.0f;

        float ry = y + kPad + headH;
        lj::ui::VLayout rows(xL, ry, rowW, kRowGap);
        for (size_t i = 0; i < grp.size(); ++i) {
            D2D1_RECT_F rowR = rows.block(rowBgH);
            RowGeom rg;
            rg.top = rowR.top;
            rg.h = rowBgH + kRowGap;   // Paint 以 rg.h - kRowGap 作背景底
            float y1 = rowR.top + 12.0f;
            float y2 = y1 + fh + (narrow ? 8.0f : 10.0f);
            float y3 = y2 + fh + 8.0f;
            rg.y1 = y1; rg.y2 = y2; rg.y3 = y3;
            m_rowGeom[g].push_back(rg);

            if (!narrow) {
                // 现有两行布局（正常宽度，保持不变以零回归）
                m_fields.push_back({ { xL, y1, xL + 76.0f, y1 + fh }, g, (int)i, F_TIME });
                float tagL = xR - 196.0f;
                float durL = tagL - 84.0f;
                float titleL = xL + 76.0f + 10.0f;
                float titleR = (std::max)(durL - 10.0f, titleL + 64.0f);
                m_fields.push_back({ { titleL, y1, titleR, y1 + fh }, g, (int)i, F_TITLE });
                m_fields.push_back({ { durL, y1, tagL - 10.0f, y1 + fh }, g, (int)i, F_DUR });
                m_rowBtn.push_back({ { tagL, y1, tagL + 90.0f, y1 + fh }, g, (int)i, 3 });
                m_rowBtn.push_back({ { xR - 26.0f, y1, xR, y1 + fh }, g, (int)i, 2 });
                m_rowBtn.push_back({ { xR - 60.0f, y1, xR - 34.0f, y1 + fh }, g, (int)i, 0 });
                m_rowBtn.push_back({ { xR - 94.0f, y1, xR - 68.0f, y1 + fh }, g, (int)i, 1 });
                float half = (contentW - kPad * 2.0f - 10.0f) / 2.0f;
                m_fields.push_back({ { xL, y2, xL + half, y2 + fh }, g, (int)i, F_LINK });
                m_fields.push_back({ { xL + half + 10.0f, y2, xR, y2 + fh }, g, (int)i, F_FOLDER });
            } else {
                // 窄窗三行重排：行1 时段+名称(占满) · 行2 链接+文件夹 · 行3 标签+时长+右对齐动作簇
                m_fields.push_back({ { xL, y1, xL + 76.0f, y1 + fh }, g, (int)i, F_TIME });
                m_fields.push_back({ { xL + 86.0f, y1, xR, y1 + fh }, g, (int)i, F_TITLE });
                float half = (contentW - kPad * 2.0f - 10.0f) / 2.0f;
                m_fields.push_back({ { xL, y2, xL + half, y2 + fh }, g, (int)i, F_LINK });
                m_fields.push_back({ { xL + half + 10.0f, y2, xR, y2 + fh }, g, (int)i, F_FOLDER });
                m_rowBtn.push_back({ { xL, y3, xL + 90.0f, y3 + fh }, g, (int)i, 3 });        // 标签
                m_fields.push_back({ { xL + 100.0f, y3, xL + 170.0f, y3 + fh }, g, (int)i, F_DUR }); // 时长
                float cr = xR;
                m_rowBtn.push_back({ ui::PackRight(cr, y3, fh, 26.0f, 8.0f), g, (int)i, 2 }); // del
                m_rowBtn.push_back({ ui::PackRight(cr, y3, fh, 26.0f, 8.0f), g, (int)i, 0 }); // up
                m_rowBtn.push_back({ ui::PackRight(cr, y3, fh, 26.0f, 8.0f), g, (int)i, 1 }); // down
            }
        }
        float rowsH = rows.total();
        float addH = 44.0f;
        float cardH = headH + rowsH + addH + kPad * 2.0f;
        D2D1_RECT_F card{ x0, y, x0 + contentW, y + cardH };
        m_groupRects.push_back(card);

        // 添加一项
        D2D1_RECT_F add{ xL, ry + rowsH + 6.0f,
                         xR, ry + rowsH + 6.0f + 32.0f };
        m_groupAdd.push_back(add);

        y += cardH + 20.0f;
    }

    // ---- 关键倒计时（第 4 组 g=3）----
    {
        const float kMsHead = 40.0f;
        const float kMsGap = 8.0f;
        float fh = 30.0f;
        float rowBgH = narrow ? 124.0f : 88.0f;
        float xL = x0 + kPad, xR = x0 + contentW - kPad;
        float rowW = contentW - kPad * 2.0f;

        float ry = y + kPad + kMsHead;
        lj::ui::VLayout rows(xL, ry, rowW, kMsGap);
        for (size_t i = 0; i < m_milestones.size(); ++i) {
            D2D1_RECT_F rowR = rows.block(rowBgH);
            RowGeom rg;
            rg.top = rowR.top;
            rg.h = rowBgH + kMsGap;
            float y1 = rowR.top + 12.0f;
            float y2 = y1 + fh + 8.0f;
            float y3 = y2 + fh + 8.0f;
            rg.y1 = y1; rg.y2 = y2; rg.y3 = y3;
            m_msGeom.push_back(rg);

            float labelW = contentW * 0.40f;
            if (!narrow) {
                m_fields.push_back({ { xL, y1, xL + labelW, y1 + fh }, 3, (int)i, F_MLABEL });
                float dateL = xL + labelW + 10.0f;
                m_fields.push_back({ { dateL, y1, dateL + 120.0f, y1 + fh }, 3, (int)i, F_MDATE });
                float urgL = dateL + 120.0f + 10.0f;
                m_rowBtn.push_back({ { urgL, y1, urgL + 84.0f, y1 + fh }, 3, (int)i, 4 });
                m_rowBtn.push_back({ { xR - 30.0f, y1, xR, y1 + fh }, 3, (int)i, 5 });
                m_fields.push_back({ { xL, y2, xR, y2 + 26.0f }, 3, (int)i, F_MNOTE });
            } else {
                // 窄窗：行1 名称+日期 · 行2 备注 · 行3 紧迫(左)+删除(右)
                m_fields.push_back({ { xL, y1, xL + labelW, y1 + fh }, 3, (int)i, F_MLABEL });
                float dateL = xL + labelW + 10.0f;
                m_fields.push_back({ { dateL, y1, (std::min)(dateL + 120.0f, xR), y1 + fh }, 3, (int)i, F_MDATE });
                m_fields.push_back({ { xL, y2, xR, y2 + 26.0f }, 3, (int)i, F_MNOTE });
                m_rowBtn.push_back({ { xL, y3, xL + 84.0f, y3 + fh }, 3, (int)i, 4 });
                m_rowBtn.push_back({ { xR - 30.0f, y3, xR, y3 + fh }, 3, (int)i, 5 });
            }
        }
        float rowsH = rows.total();
        float addH = 44.0f;
        float cardH = kMsHead + rowsH + addH + kPad * 2.0f;
        m_msCard = { x0, y, x0 + contentW, y + cardH };
        m_msAdd = { xL, ry + rowsH + 6.0f,
                    xR, ry + rowsH + 6.0f + 32.0f };
        y += cardH + 20.0f;
    }

    // 页脚（确定保存 / 返回 / 恢复默认 / 导出 / 导入）
    if (!narrow) {
        float fy = y;
        m_btnBack  = { x0, fy, x0 + 150.0f, fy + 46.0f };
        m_btnReset = { x0 + 170.0f, fy, x0 + 340.0f, fy + 46.0f };
        m_btnOk    = { x0 + contentW - 200.0f, fy, x0 + contentW, fy + 46.0f };
        y += 46.0f + 18.0f;
        float by = y;
        m_btnExport = { x0, by, x0 + 170.0f, by + 40.0f };
        m_btnImport = { x0 + 186.0f, by, x0 + 356.0f, by + 40.0f };
        y += 40.0f + 30.0f;
    } else {
        // 窄窗：页脚纵向堆叠，避免按钮互相碰撞
        float fy = y;
        float bw = contentW;
        m_btnBack  = { x0, fy, x0 + bw, fy + 42.0f }; fy += 42.0f + 10.0f;
        m_btnReset = { x0, fy, x0 + bw, fy + 42.0f }; fy += 42.0f + 10.0f;
        m_btnOk    = { x0, fy, x0 + bw, fy + 42.0f }; fy += 42.0f + 10.0f;
        m_btnExport = { x0, fy, x0 + bw, fy + 38.0f }; fy += 38.0f + 10.0f;
        m_btnImport = { x0, fy, x0 + bw, fy + 38.0f }; fy += 38.0f + 30.0f;
        y = fy;
    }

    SetContentHeight(y - area.top);
    m_built = true;

    // 截图自检：锁定状态下每次重排后重新锚定标签浮层（Initialize 期
    // Layout 尚未跑过，行按钮几何不存在，只能在首次 Layout 后锚定）
    if (m_tagPopLock) {
        for (auto& b : m_rowBtn)
            if (b.kind == 3) { LayoutTagPop(b.g, b.i, b.r); break; }
    }
}

void ManageView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    if (m_toastT > 0.0f) m_toastT = (std::max)(0.0f, m_toastT - dt);

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    float mx = in.mouseX, my = in.mouseY + ScrollY();

    // 悬停高亮
    m_hotRect = { 0, 0, 0, 0 };
    auto hitAny = [&](const D2D1_RECT_F& r) {
        if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) m_hotRect = r;
    };
    for (auto& f : m_fields) hitAny(f.r);
    for (auto& b : m_rowBtn) hitAny(b.r);
    for (auto& a : m_groupAdd) hitAny(a);
    hitAny(m_btnBack); hitAny(m_btnReset); hitAny(m_btnOk);
    hitAny(m_btnExport); hitAny(m_btnImport);
    hitAny(m_msAdd);
    for (auto& ch : m_tagPopChips) hitAny(ch.first);

    // 标签悬停展开浮层（替代点击循环切换）：悬停标签按钮即展开，
    // 移出按钮与浮层即收起
    {
        int hoverG = -1, hoverI = -1; D2D1_RECT_F hoverR{};
        for (auto& b : m_rowBtn)
            if (b.kind == 3 && Hit(b.r, mx, my)) { hoverG = b.g; hoverI = b.i; hoverR = b.r; break; }
        bool overPop = (m_tagPopG >= 0) && Hit(m_tagPopRect, mx, my);
        if (hoverG >= 0) {
            if (m_tagPopG != hoverG || m_tagPopI != hoverI)
                LayoutTagPop(hoverG, hoverI, hoverR);
        } else if (m_tagPopG >= 0 && !overPop && !m_tagPopLock) {
            m_tagPopG = -1; m_tagPopI = -1;
        }
    }

    // 编辑态：鼠标先交给字段（点击定位光标 / 拖拽框选），点字段外才提交
    if (m_editingOn && m_cvCached) {
        TextStyle es; es.role = FontRole::Sans; es.size = 13.0f; es.vAlign = VAlign::Middle;
        es.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        D2D1_RECT_F tbox{ m_editing.r.left + 8.0f, m_editing.r.top,
                          m_editing.r.right - 6.0f, m_editing.r.bottom };
        bool inside = m_edit.HandleMouse(in, *m_cvCached, tbox, es, ScrollY());
        if (!inside && in.clicked) CommitEdit();
    }

    if (in.clicked) {
        // 页脚
        if (Hit(m_btnOk, mx, my))   { Save(); Go(L"checkin"); return; }
        if (Hit(m_btnBack, mx, my)) { if (m_changed) Save(); Go(L"checkin"); return; }
        if (Hit(m_btnReset, mx, my)){ ReloadDefault(); return; }
        if (Hit(m_btnExport, mx, my)) { DoExport(); return; }
        if (Hit(m_btnImport, mx, my)) { DoImport(); return; }
        // 添加倒计时
        if (Hit(m_msAdd, mx, my)) {
            Milestone nm;
            Date td = Today();
            nm.date = DateFromCivil(DaysFromCivil(td.y, td.m, td.d) + 30);
            nm.label = L"新倒计时"; nm.urgent = false;
            m_milestones.push_back(nm);
            m_changed = true; m_built = false; Layout(m_areaCached, *m_cvCached); return;
        }
        // 添加一项
        for (int g = 0; g < (int)m_groupAdd.size(); ++g)
            if (Hit(m_groupAdd[g], mx, my)) {
                auto& grp = (g == 0) ? m_bundle.daily : (g == 1) ? m_bundle.sat : m_bundle.sun;
                CheckItem ni; ni.id = std::to_wstring(g) + L"_" + std::to_wstring(grp.size()) + L"_"
                              + std::to_wstring((long long)time(nullptr));
                ni.title = L"新打卡项"; ni.tag = L"主线"; ni.minutes = 30;
                grp.push_back(ni);
                m_changed = true; m_built = false; Layout(m_areaCached, *m_cvCached); return;
            }
        // 标签浮层：选择标签（悬停展开后点击芯片）
        if (m_tagPopG >= 0) {
            for (auto& ch : m_tagPopChips) {
                if (Hit(ch.first, mx, my)) {
                    auto& grp = (m_tagPopG == 0) ? m_bundle.daily
                              : (m_tagPopG == 1) ? m_bundle.sat : m_bundle.sun;
                    if (m_tagPopI >= 0 && m_tagPopI < (int)grp.size()) {
                        grp[m_tagPopI].tag = kTags[ch.second];
                        m_changed = true;
                    }
                    m_tagPopG = -1; m_tagPopI = -1;
                    return;
                }
            }
        }
        // 行按钮
        for (auto& b : m_rowBtn) {
            if (!Hit(b.r, mx, my)) continue;
            if (b.g == 3) {   // 关键倒计时：紧迫切换 / 删除
                if (b.kind == 4) { m_milestones[b.i].urgent = !m_milestones[b.i].urgent; m_changed = true; }
                else if (b.kind == 5) {
                    if (b.i < (int)m_milestones.size()) m_milestones.erase(m_milestones.begin() + b.i);
                    m_changed = true; m_built = false; Layout(m_areaCached, *m_cvCached);
                }
                return;
            }
            auto& grp = (b.g == 0) ? m_bundle.daily : (b.g == 1) ? m_bundle.sat : m_bundle.sun;
            if (b.kind == 3) {  // 标签：改为悬停展开选择，点击本身不再循环
                return;
            } else if (b.kind == 2) { // 删除
                if (b.i < (int)grp.size()) grp.erase(grp.begin() + b.i);
                m_changed = true; m_built = false; Layout(m_areaCached, *m_cvCached);
            } else if (b.kind == 0 && b.i > 0) { // 上移
                std::swap(grp[b.i], grp[b.i - 1]);
                m_changed = true; m_built = false; Layout(m_areaCached, *m_cvCached);
            } else if (b.kind == 1 && b.i + 1 < (int)grp.size()) { // 下移
                std::swap(grp[b.i], grp[b.i + 1]);
                m_changed = true; m_built = false; Layout(m_areaCached, *m_cvCached);
            }
            return;
        }
        // 文本字段 → 编辑
        for (auto& f : m_fields)
            if (Hit(f.r, mx, my)) { BeginEdit(f); return; }
    }
}

// 截图自检：空库环境造两个示例打卡项，验证序号徽章与标签浮层
void ManageView::DebugForcePreview()
{
    if (m_bundle.daily.empty()) {
        CheckItem a; a.id = L"shot_d0"; a.slot = L"06:30"; a.title = L"晨读英语";
        a.tag = L"英语"; a.minutes = 40;
        CheckItem b; b.id = L"shot_d1"; b.slot = L"21:00"; b.title = L"行测刷题";
        b.tag = L"主线"; b.minutes = 90;
        m_bundle.daily.push_back(a);
        m_bundle.daily.push_back(b);
    }
    m_tagPopLock = true;   // 锁定浮层：Layout 后锚定 + Update 不收起
    m_built = false;
    if (m_areaCached.right > m_areaCached.left && m_cvCached)
        Layout(m_areaCached, *m_cvCached);
    // 浮层由 Layout() 尾部在 m_tagPopLock 下锚定
}

// 标签浮层布局：以标签按钮为锚，默认向下展开；贴卡底则向上，左右夹取防溢出
void ManageView::LayoutTagPop(int g, int i, const D2D1_RECT_F& anchor)
{
    m_tagPopG = g; m_tagPopI = i;
    m_tagPopChips.clear();
    const float cw = 72.0f, chh = 26.0f, gx = 8.0f, gy = 7.0f, pad = 12.0f;
    const int cols = 3;
    const int rows = (kTagN + cols - 1) / cols;
    const float pw = pad * 2.0f + cols * cw + (cols - 1) * gx;
    const float ph = pad * 2.0f + rows * chh + (rows - 1) * gy;

    const D2D1_RECT_F& card = m_groupRects[g];
    float x = anchor.right - pw;
    x = (std::max)(card.left + 8.0f, (std::min)(x, card.right - 8.0f - pw));
    float y = anchor.bottom + 6.0f;
    if (y + ph > card.bottom - 8.0f) y = anchor.top - ph - 6.0f;
    m_tagPopRect = { x, y, x + pw, y + ph };

    for (int k = 0; k < kTagN; ++k) {
        int c = k % cols, r = k / cols;
        float cx = x + pad + c * (cw + gx);
        float cy = y + pad + r * (chh + gy);
        m_tagPopChips.push_back({ { cx, cy, cx + cw, cy + chh }, k });
    }
}

void ManageView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    // 标题
    {
        float ha = Clamp01(m_t / 0.5f);
        cv.PushOpacity(ha);
        TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f; sec.letterSpacing = 2.4f;
        sec.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(L"SECTION · 自定义打卡项", { x0, m_contentTop, x0 + 360.0f, m_contentTop + 16.0f }, sec, pal.ink300);
        cv.PopOpacity();
        TextStyle h1; h1.role = FontRole::Serif; h1.size = 36.0f; h1.weight = DWRITE_FONT_WEIGHT_BLACK;
        h1.letterSpacing = 3.0f;
        cv.CharsReveal(L"自定义打卡项", x0, m_contentTop + 24.0f, h1, pal.ink900, m_t * 1.1f, 0.04f, 16.0f);
        TextStyle lead; lead.role = FontRole::Sans; lead.size = 12.5f;
        cv.Text(L"所有打卡内容可编辑、增删、排序、改标签；改完点「确定保存」，改动仅留在本机。",
                { x0, m_contentTop + 70.0f, x0 + contentW, m_contentTop + 90.0f }, lead, pal.ink500);
    }

    auto DrawField = [&](const D2D1_RECT_F& r, const std::wstring& text, bool hot, int fieldKind, bool editing) {
        cv.FillRoundRect(r, 4.0f, pal.paperHi);
        cv.StrokeRoundRect(r, 4.0f, hot ? pal.seal : pal.rule, hot ? shape::kStroke : shape::kHair);
        // 占位提示
        const wchar_t* ph = L"";
        switch (fieldKind) {
            case F_TIME:   ph = L"时段"; break;
            case F_TITLE:  ph = L"打卡项名称"; break;
            case F_DUR:    ph = L"分钟"; break;
            case F_LINK:   ph = L"学习资源链接（URL）"; break;
            case F_FOLDER: ph = L"本地资料夹路径"; break;
            case F_MLABEL: ph = L"倒计时名称（如 国考）"; break;
            case F_MDATE:  ph = L"YYYY-MM-DD"; break;
            case F_MNOTE:  ph = L"备注（可选）"; break;
            default:       ph = L"…"; break;
        }
        // 编辑态：文字/光标/选区/IME 组合串全由 D3D 绘制（1×1 透明代理，无白块）
        TextStyle fs; fs.role = FontRole::Sans; fs.size = 13.0f; fs.vAlign = VAlign::Middle;
        fs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        D2D1_RECT_F tbox{ r.left + 8.0f, r.top, r.right - 6.0f, r.bottom };
        if (editing) {
            m_edit.Paint(cv, tbox, fs, pal.ink900, ph, pal.ink300, 0.0f, s);
        } else {
            bool empty = text.empty();
            lj::PaintFieldEdit(cv, tbox, fs,
                              empty ? std::wstring(ph) : text,
                              empty ? pal.ink300 : pal.ink900, -1, 0.0f, -1, -1);
        }
    };

    for (int g = 0; g < 3; ++g) {
        D2D1_RECT_F card = m_groupRects[g];
        cv.PaperCard(card, 0.2f, shape::kEdge);
        cv.StrokeRoundRect(card, shape::kEdge, WithAlpha(pal.seal, 0.5f), shape::kHair);

        TextStyle gt; gt.role = FontRole::Mono; gt.size = 12.5f; gt.weight = DWRITE_FONT_WEIGHT_BOLD;
        gt.letterSpacing = 1.0f;
        cv.Text(kGroupTitle[g], { card.left + 18.0f, card.top + 12.0f,
                 card.right - 18.0f, card.top + 34.0f }, gt, pal.seal);
        cv.PerforationH(card.left + 18.0f, card.right - 18.0f, card.top + 36.0f,
                        WithAlpha(pal.ruleStrong, 0.5f));

        auto& grp = (g == 0) ? m_bundle.daily : (g == 1) ? m_bundle.sat : m_bundle.sun;
        for (size_t i = 0; i < grp.size() && i < m_rowGeom[g].size(); ++i) {
            const RowGeom& rg = m_rowGeom[g][i];
            D2D1_RECT_F rc{ card.left + 12.0f, rg.top, card.right - 12.0f, rg.top + rg.h - 8.0f };
            cv.FillRoundRect(rc, shape::kEdgeSoft, WithAlpha(pal.rule, 0.10f));

            wchar_t num[8]; swprintf_s(num, L"%02d", (int)i + 1);
            // 序号贴行卡右上角（小徽章），不再画在行中部被字段遮住
            TextStyle ns; ns.role = FontRole::Mono; ns.size = 9.5f; ns.weight = DWRITE_FONT_WEIGHT_BOLD;
            ns.hAlign = HAlign::Right; ns.vAlign = VAlign::Middle;
            cv.Text(num, { rc.right - 40.0f, rc.top + 1.0f, rc.right - 10.0f, rc.top + 12.0f }, ns, pal.ink300);
        }

        // 添加一项
        D2D1_RECT_F add = m_groupAdd[g];
        bool hot = (add.left == m_hotRect.left && add.top == m_hotRect.top);
        cv.FillRoundRect(add, shape::kEdgeSoft, WithAlpha(pal.jade, hot ? 0.22f : 0.12f));
        cv.StrokeRoundRect(add, shape::kEdgeSoft, WithAlpha(pal.jade, 0.8f), shape::kHair);
        TextStyle as; as.role = FontRole::Sans; as.size = 13.0f; as.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        as.hAlign = HAlign::Center; as.vAlign = VAlign::Middle; as.letterSpacing = 1.0f;
        cv.Text(L"＋ 添加一项", add, as, pal.jade);
    }

    // ---- 关键倒计时卡片框 + 行底（g=3，作为字段的背景层）----
    if (m_msCard.right > m_msCard.left) {
        D2D1_RECT_F card = m_msCard;
        cv.PaperCard(card, 0.2f, shape::kEdge);
        cv.StrokeRoundRect(card, shape::kEdge, WithAlpha(pal.brass, 0.5f), shape::kHair);
        TextStyle gt; gt.role = FontRole::Mono; gt.size = 12.5f; gt.weight = DWRITE_FONT_WEIGHT_BOLD; gt.letterSpacing = 1.0f;
        cv.Text(L"关键倒计时", { card.left + 18.0f, card.top + 12.0f, card.right - 18.0f, card.top + 34.0f }, gt, pal.brass);
        cv.PerforationH(card.left + 18.0f, card.right - 18.0f, card.top + 36.0f, WithAlpha(pal.ruleStrong, 0.5f));
        for (size_t i = 0; i < m_milestones.size() && i < m_msGeom.size(); ++i) {
            const RowGeom& rg = m_msGeom[i];
            D2D1_RECT_F rc{ card.left + 12.0f, rg.top, card.right - 12.0f, rg.top + rg.h - 8.0f };
            cv.FillRoundRect(rc, shape::kEdgeSoft, WithAlpha(pal.rule, 0.10f));
            wchar_t num[8]; swprintf_s(num, L"%02d", (int)i + 1);
            TextStyle ns; ns.role = FontRole::Mono; ns.size = 9.5f; ns.weight = DWRITE_FONT_WEIGHT_BOLD;
            ns.hAlign = HAlign::Right; ns.vAlign = VAlign::Middle;
            cv.Text(num, { rc.right - 40.0f, rc.top + 1.0f, rc.right - 10.0f, rc.top + 12.0f }, ns, pal.ink300);
        }
    }

    // 字段（按组/索引匹配值）
    for (auto& f : m_fields) {
        std::wstring txt;
        if (f.g == 3) {
            auto& ms = m_milestones[f.i];
            switch (f.f) {
                case F_MLABEL: txt = ms.label; break;
                case F_MDATE:  txt = FormatDate(ms.date); break;
                case F_MNOTE:  txt = ms.note; break;
            }
        } else {
            auto& it = (f.g == 0) ? m_bundle.daily[f.i] : (f.g == 1) ? m_bundle.sat[f.i] : m_bundle.sun[f.i];
            switch (f.f) {
                case F_TIME:   txt = it.slot; break;
                case F_TITLE:  txt = it.title; break;
                case F_DUR:    txt = it.minutes > 0 ? std::to_wstring(it.minutes) : L""; break;
                case F_LINK:   txt = it.link; break;
                case F_FOLDER: txt = it.folder; break;
            }
            if (f.f == F_DUR && !txt.empty()) txt += L" 分钟";
        }
        bool hot = (f.r.left == m_hotRect.left && f.r.top == m_hotRect.top);
        bool editing = m_editingOn && m_editing.g == f.g && m_editing.i == f.i && m_editing.f == f.f;
        DrawField(f.r, txt.empty() ? L"" : txt, hot, f.f, editing);
    }

    // 行按钮（关键倒计时按钮在循环后单独绘制）
    for (auto& b : m_rowBtn) {
        if (b.g == 3) continue;
        bool hot = (b.r.left == m_hotRect.left && b.r.top == m_hotRect.top);
        D2D1_COLOR_F col = pal.rule;
        const wchar_t* sym = L"";
        if (b.kind == 3) {
            auto& it = (b.g == 0) ? m_bundle.daily[b.i] : (b.g == 1) ? m_bundle.sat[b.i] : m_bundle.sun[b.i];
            col = TagColor(pal, it.tag); sym = L"";
        }
        else if (b.kind == 0) sym = L"▲";
        else if (b.kind == 1) sym = L"▼";
        else if (b.kind == 2) sym = L"✕";
        if (b.kind == 3) {
            auto& it = (b.g == 0) ? m_bundle.daily[b.i] : (b.g == 1) ? m_bundle.sat[b.i] : m_bundle.sun[b.i];
            cv.FillRoundRect(b.r, 4.0f, WithAlpha(col, pal.dark ? 0.28f : 0.16f));
            cv.StrokeRoundRect(b.r, 4.0f, WithAlpha(col, 0.85f), shape::kHair);
            TextStyle ts; ts.role = FontRole::Mono; ts.size = 11.0f; ts.hAlign = HAlign::Center;
            ts.vAlign = VAlign::Middle; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            cv.Text(it.tag, b.r, ts, col);
        } else {
            cv.FillRoundRect(b.r, 4.0f, WithAlpha(pal.paperLo, hot ? 0.9f : 0.6f));
            cv.StrokeRoundRect(b.r, 4.0f, hot ? pal.seal : pal.rule, shape::kHair);
            TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.hAlign = HAlign::Center;
            ts.vAlign = VAlign::Middle;
            cv.Text(sym, b.r, ts, b.kind == 2 ? pal.vermilion : pal.ink700);
        }
    }

    // ---- 关键倒计时行按钮（紧迫 / 删除）+ 添加（g=3，绘制在字段之上）----
    if (m_msCard.right > m_msCard.left) {
        for (auto& b : m_rowBtn) {
            if (b.g != 3) continue;
            bool hot = (b.r.left == m_hotRect.left && b.r.top == m_hotRect.top);
            if (b.kind == 5) {   // 删除
                cv.FillRoundRect(b.r, 4.0f, WithAlpha(pal.paperLo, hot ? 0.9f : 0.6f));
                cv.StrokeRoundRect(b.r, 4.0f, hot ? pal.seal : pal.rule, shape::kHair);
                TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
                cv.Text(L"✕", b.r, ts, pal.vermilion);
            } else if (b.kind == 4) {   // 紧迫切换
                bool urg = m_milestones[b.i].urgent;
                cv.FillRoundRect(b.r, 4.0f, WithAlpha(urg ? pal.vermilion : pal.paperLo, urg ? 0.20f : 0.6f));
                cv.StrokeRoundRect(b.r, 4.0f, urg ? pal.vermilion : pal.rule, shape::kHair);
                TextStyle ts; ts.role = FontRole::Sans; ts.size = 12.0f; ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
                ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
                cv.Text(urg ? L"紧迫" : L"常规", b.r, ts, urg ? pal.vermilion : pal.ink700);
            }
        }
        // 添加倒计时
        {
            bool hot = (m_msAdd.left == m_hotRect.left && m_msAdd.top == m_hotRect.top);
            cv.FillRoundRect(m_msAdd, shape::kEdgeSoft, WithAlpha(pal.jade, hot ? 0.22f : 0.12f));
            cv.StrokeRoundRect(m_msAdd, shape::kEdgeSoft, WithAlpha(pal.jade, 0.8f), shape::kHair);
            TextStyle as; as.role = FontRole::Sans; as.size = 13.0f; as.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            as.hAlign = HAlign::Center; as.vAlign = VAlign::Middle; as.letterSpacing = 1.0f;
            cv.Text(L"＋ 添加倒计时", m_msAdd, as, pal.jade);
        }
    }

    // 页脚
    auto DrawFooter = [&](const D2D1_RECT_F& r, const std::wstring& label, bool primary, bool hot) {
        cv.FillRoundRect(r, shape::kEdgeSoft, primary ? pal.seal : pal.paperLo);
        cv.StrokeRoundRect(r, shape::kEdgeSoft, primary ? pal.seal : (hot ? pal.seal : pal.rule), shape::kHair);
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.5f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle; ts.letterSpacing = 1.0f;
        cv.Text(label, r, ts, primary ? pal.paperHi : (hot ? pal.seal : pal.ink700));
    };
    DrawFooter(m_btnBack,  L"返 回 打 卡", false, m_btnBack.left == m_hotRect.left && m_btnBack.top == m_hotRect.top);
    DrawFooter(m_btnReset, L"恢 复 默 认", false, m_btnReset.left == m_hotRect.left && m_btnReset.top == m_hotRect.top);
    DrawFooter(m_btnOk,    L"确 定 保 存", true,  m_btnOk.left == m_hotRect.left && m_btnOk.top == m_hotRect.top);

    // 本地数据管理（整账户备份）
    DrawFooter(m_btnExport, L"导 出 备 份", false, m_btnExport.left == m_hotRect.left && m_btnExport.top == m_hotRect.top);
    DrawFooter(m_btnImport, L"导 入 备 份", false, m_btnImport.left == m_hotRect.left && m_btnImport.top == m_hotRect.top);

    // 标签悬停浮层（内容坐标内、压住普通内容）
    if (m_tagPopG >= 0 && m_tagPopI >= 0) {
        cv.FillRoundRect(m_tagPopRect, shape::kEdge, pal.paperHi);
        cv.StrokeRoundRect(m_tagPopRect, shape::kEdge, pal.rule, shape::kHair);
        auto& grp = (m_tagPopG == 0) ? m_bundle.daily : (m_tagPopG == 1) ? m_bundle.sat : m_bundle.sun;
        const std::wstring curTag = (m_tagPopI < (int)grp.size()) ? grp[m_tagPopI].tag : L"";
        for (auto& chip : m_tagPopChips) {
            const wchar_t* tag = kTags[chip.second];
            const D2D1_RECT_F& r = chip.first;
            bool hot = (r.left == m_hotRect.left && r.top == m_hotRect.top);
            bool cur = (curTag == tag);
            D2D1_COLOR_F col = TagColor(pal, tag);
            if (hot) {
                cv.FillRoundRect(r, shape::kEdgeSoft, col);
            } else if (cur) {
                cv.FillRoundRect(r, shape::kEdgeSoft, WithAlpha(col, 0.26f));
            } else {
                cv.FillRoundRect(r, shape::kEdgeSoft, WithAlpha(col, 0.10f));
            }
            cv.StrokeRoundRect(r, shape::kEdgeSoft,
                               cur ? col : WithAlpha(col, hot ? 1.0f : 0.55f),
                               cur ? 1.4f : shape::kHair);
            TextStyle cs; cs.role = FontRole::Sans; cs.size = 11.5f;
            cs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            cs.hAlign = HAlign::Center; cs.vAlign = VAlign::Middle;
            cv.Text(tag, r, cs, hot ? pal.paperHi : (cur ? col : pal.ink700));
        }
    }

    cv.PopTransform();
    cv.PopClip();

    // 导出 / 导入 提示气泡（固定在屏幕底部，不随滚动）
    if (m_toastT > 0.0f) {
        float a = Clamp01(m_toastT / 0.6f);     // 末尾 0.6s 淡出
        float bw = (std::min)(420.0f, availW - 48.0f);
        D2D1_RECT_F box{ m_area.left + (availW - bw) * 0.5f,
                         m_area.bottom - 64.0f,
                         m_area.left + (availW + bw) * 0.5f,
                         m_area.bottom - 24.0f };
        cv.PushOpacity(a);
        cv.FillRoundRect(box, 8.0f, m_toastBad ? pal.vermilion : pal.seal);
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.hAlign = HAlign::Center;
        ts.vAlign = VAlign::Middle; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(m_toast, box, ts, pal.paperHi);
        cv.PopOpacity();
    }

    // 滚动指示条
    if (MaxScroll() > 1.0f) {
        float trackTop = m_area.top + 8.0f;
        float trackH = (m_area.bottom - m_area.top) - 16.0f;
        float vh = m_area.bottom - m_area.top;
        float thumbH = (std::max)(40.0f, trackH * (vh / m_contentHeight));
        float pp = Clamp01(s / MaxScroll());
        float ty = trackTop + (trackH - thumbH) * pp;
        float bx = m_area.right - 6.0f;
        cv.FillRect({ bx, trackTop, bx + 2.0f, trackTop + trackH }, WithAlpha(pal.ink300, 0.16f));
        cv.FillRect({ bx, ty, bx + 2.0f, ty + thumbH }, WithAlpha(pal.seal, 0.55f));
    }
}

} // namespace lj
