#include "views/ProfileView.h"
#include "app/Cloud.h"
#include "core/Hwnd.h"
#include "ui/FieldText.h"
#include "ui/Layout.h"
#include <wincodec.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>
#include <ctime>

namespace lj {

namespace {
bool Hit(const D2D1_RECT_F& r, float x, float y)
{
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

const wchar_t* kGenders[] = { L"男", L"女", L"保密" };
const int kGenderN = 3;

std::wstring PvFormatTs(long long ts)
{
    if (ts <= 0) return L"—";
    time_t t = (time_t)(ts / 1000);
    tm lt{};
    localtime_s(&lt, &t);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d",
               lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    return buf;
}

std::wstring KindLabel(const std::wstring& k)
{
    if (k == L"column") return L"专栏";
    if (k == L"video")  return L"视频";
    if (k == L"image")  return L"图片";
    if (k == L"audio")  return L"音频";
    return L"内容";
}
} // namespace

// ============================================================
//  编辑（v2 统一输入框）
// ============================================================
void ProfileView::BeginEdit(int field, const D2D1_RECT_F& r)
{
    if (m_editingOn) CommitEdit();
    if (field < 0 || field > 5) return;
    m_editField = field;
    m_editingOn = true;
    // 1×1 透明代理只收键盘 + IME，字段上无任何 GDI 子窗口（无白块）；
    // 文字/光标/选区/IME 组合串全由 D3D 绘制。
    m_edit.onEnter     = [this] { CommitEdit(); };
    m_edit.onEsc       = [this] { CancelEdit(); };
    m_edit.onKillFocus = [this] { CommitEdit(); };

    std::wstring cur;
    switch (field) {
        case PF_BIO:    cur = m_profile.bio; break;
        case PF_MAJOR:  cur = m_profile.major; break;
        case PF_SCHOOL: cur = m_profile.school; break;
        case PF_BIRTH:  cur = m_profile.birthday; break;
        case PF_NAME:   cur = m_profile.displayName; break;   // 批次 H：昵称（不改账号）
        default: break;
    }
    m_edit.Begin(cur, false, field == PF_NAME ? 22.0f : 13.0f);
    (void)r;   // 位置由 Paint 每帧按字段矩形摆放（1×1 代理贴光标）
}

void ProfileView::DebugForceOpen()
{
    // 截图自检：强制打开「简介」编辑，验证隐藏代理 + D3D 自绘无覆盖块
    BeginEdit(PF_BIO, m_fieldRects[PF_BIO]);
}

void ProfileView::CommitEdit()
{
    if (!m_editingOn) { m_editingOn = false; m_editField = -1; return; }
    std::wstring txt;
    m_edit.End(true, txt);

    switch (m_editField) {
        case PF_BIO:    m_profile.bio = txt; break;
        case PF_MAJOR:  m_profile.major = txt; break;
        case PF_SCHOOL: m_profile.school = txt; break;
        case PF_BIRTH:  m_profile.birthday = txt; break;
        case PF_NAME:   m_profile.displayName = txt; break;   // 批次 H：仅改显示名，账号不变
        default: break;
    }
    m_editingOn = false;
    m_editField = -1;
    SaveProfile();
}

void ProfileView::CancelEdit()
{
    m_edit.Cancel();
    m_editingOn = false;
    m_editField = -1;
}

// 当前编辑字段的文字框（与 Paint 中非编辑态文字框完全一致，避免点击后文字跳动）
D2D1_RECT_F ProfileView::EditTextBox() const
{
    if (m_editField == PF_BIO)
        return { m_bioRect.left + 12.0f, m_bioRect.top, m_bioRect.right - 12.0f, m_bioRect.bottom };
    if (m_editField == PF_NAME)   // 批次 H：昵称（Hero 名字行）
        return { m_nameRect.left, m_nameRect.top, m_nameRect.right, m_nameRect.bottom };
    if (m_editField < 0 || m_editField > 4) return { 0, 0, 0, 0 };
    const D2D1_RECT_F& r = m_fieldRects[m_editField];
    D2D1_RECT_F vr{ r.left + 8.0f, r.top + 26.0f, r.right - 8.0f, r.bottom - 8.0f };
    return { vr.left + 4.0f, vr.top, vr.right - 4.0f, vr.bottom };
}

void ProfileView::SaveProfile()
{
    AccountStore::Instance().SaveProfile(m_profile);
    TrySyncProfile();
}

void ProfileView::TrySyncProfile()
{
    auto& cloud = Cloud::Instance();
    if (!cloud.LoggedIn()) return;   // 未登录云端：仅本地保存

    // 超长字段拦截（与服务端 PROFILE_LIMITS 对齐：bio500/major40/school80/birthday20/gender10）
    auto over = [&](const std::wstring& v, size_t lim, const wchar_t* name) -> bool {
        if (v.size() > lim) {
            m_toast = std::wstring(name) + L"过长（上限 " + std::to_wstring(lim) + L" 字），未同步云端，仅存本机";
            m_toastBad = true; m_toastT = 4.0f;
            return true;
        }
        return false;
    };
    if (over(m_profile.bio, 500, L"简介") ||
        over(m_profile.major, 40, L"专业") ||
        over(m_profile.school, 80, L"学校") ||
        over(m_profile.birthday, 20, L"生日") ||
        over(m_profile.gender, 10, L"性别")) {
        return;   // 本地已保存，仅拦截云端
    }

    // 后台线程上推，UI 线程绝不直接碰网络 IO；快照避免与 UI 写竞争
    AccountProfile snap = m_profile;
    cloud.RunAsync([this, snap] {
        std::wstring err;
        CloudResult rc = Cloud::Instance().UpdateProfile(snap, err);
        if (rc == CloudResult::Rejected) m_cloudPushFailed = true;  // 由 UI 线程消费提示
        // Offline：静默降级，不弹错（本地已保存）
    });
}

void ProfileView::CycleGender()
{
    int cur = -1;
    for (int i = 0; i < kGenderN; ++i) if (m_profile.gender == kGenders[i]) { cur = i; break; }
    cur = (cur + 1 + kGenderN + (cur < 0 ? 1 : 0)) % kGenderN;
    m_profile.gender = kGenders[cur];
    SaveProfile();
}

// ============================================================
void ProfileView::ReloadAll()
{
    auto& as = AccountStore::Instance();
    auto& cs = CheckinStore::Instance();
    m_profile = as.LoadProfile();

    // 档案上云（P1-1）：已登录则后台拉取云端最新档案，合并到本地（跨设备可见）
    if (Cloud::Instance().LoggedIn()) {
        Cloud::Instance().RunAsync([this] {
            std::wstring err;
            AccountProfile p = Cloud::Instance().GetProfile(err);
            bool has = !p.bio.empty() || !p.major.empty() || !p.school.empty() ||
                       !p.birthday.empty() || !p.gender.empty();
            if (has) {
                std::lock_guard<std::mutex> lk(m_pendingMu);
                m_pendingProfile = p;
                m_hasPending = true;
            }
        });
    }
    m_favs = cs.LoadFavorites();
    m_hist = cs.LoadHistory();

    // 我的专栏
    auto cols = cs.LoadColumns();
    std::wstring me = as.CurrentName();
    m_colCount = 0;
    for (const auto& c : cols) if (c.author.empty() || c.author == me) m_colCount++;

    // 打卡天数（近 120 天里有任一勾选的日子）
    m_checkDays = 0;
    auto days = cs.LastNDays(120);
    for (const auto& d : days) {
        auto dm = cs.LoadDay(d);
        bool any = false;
        for (const auto& kv : dm) if (kv.second) { any = true; break; }
        if (any) m_checkDays++;
    }

    // 累计专注
    m_focusMin = 0;
    for (const auto& f : cs.LoadFocus()) m_focusMin += f.min;

    // 批次 H：个人错题本数据（练考薄弱知识点 TOP 8）
    quiz::QuizStore::Instance().SetRoot(as.CurrentRoot());
    m_weakPts = quiz::QuizStore::Instance().TopWeakPoints(8);
    m_avatarBmp.Reset();   // 账户可能切换，头像位图重载

    // 收藏 / 历史：新的在前
    std::sort(m_favs.begin(), m_favs.end(),
              [](const Favorite& a, const Favorite& b) { return a.ts > b.ts; });
    std::sort(m_hist.begin(), m_hist.end(),
              [](const HistoryItem& a, const HistoryItem& b) { return a.ts > b.ts; });
}

void ProfileView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_toastT = 0.0f;
    m_hoverFav = -1;
    m_hoverField = -1;
    if (m_editingOn) CancelEdit();
    ReloadAll();
}

void ProfileView::OnLeave()
{
    if (m_editingOn) CommitEdit();
}

// ============================================================
//  布局
// ============================================================
void ProfileView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);
    m_cvCached = &cv;

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;

    // 纵向流式布局：区块只声明自身高度，位置由 flow 推进
    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);

    // ---- 标题区 ----
    m_contentTop = flow.cursorY;
    flow.block(96.0f);

    // ---- 名片区 ----
    const float heroH = 176.0f;
    {
        float top = flow.block(heroH + 14.0f).top;
        m_heroRect = { x0, top, x0 + contentW, top + heroH };
        m_avatar = { x0 + 24.0f, top + 26.0f, x0 + 24.0f + 84.0f, top + 26.0f + 84.0f };
        float infoL = m_avatar.right + 24.0f;
        m_nameRect = { infoL, top + 20.0f, m_heroRect.right - 200.0f, top + 58.0f };   // 批次 H：点击改名
        m_bioRect = { infoL, top + 96.0f, m_heroRect.right - 24.0f, top + 96.0f + 40.0f };
        m_fieldRects[PF_BIO] = m_bioRect;
    }

    // ---- 资料四格：专业 / 学校 / 生日 / 性别 ----
    {
        float gap = 10.0f;
        int cols = (contentW >= 700.0f) ? 4 : 2;
        float fw = (contentW - gap * (cols - 1)) / (float)cols;
        const int order[4] = { PF_MAJOR, PF_SCHOOL, PF_BIRTH, PF_GENDER };
        int rows = (4 + cols - 1) / cols;
        float top = flow.block(rows * (62.0f + gap) + 12.0f).top;
        for (int i = 0; i < 4; ++i) {
            int r = i / cols, c = i % cols;
            float fx = x0 + c * (fw + gap);
            float fy = top + r * (62.0f + gap);
            m_fieldRects[order[i]] = { fx, fy, fx + fw, fy + 62.0f };
        }
    }

    // ---- 统计条 ----
    {
        float gap = 10.0f;
        float sw = (contentW - gap * 3) / 4.0f;
        m_statsY = flow.block(66.0f + 26.0f).top;
        for (int i = 0; i < 4; ++i) {
            float sx = x0 + i * (sw + gap);
            m_statRects[i] = { sx, m_statsY, sx + sw, m_statsY + 66.0f };
        }
    }

    // ---- 批次 H：个人错题本（薄弱知识点）----
    m_weakTitleY = flow.block(44.0f).top;
    if (m_weakPts.empty()) {
        flow.block(56.0f);
    } else {
        int n = (std::min)((int)m_weakPts.size(), 8);
        for (int i = 0; i < n; ++i) flow.block(34.0f + 6.0f);
    }
    flow.block(18.0f);

    // ---- 收藏区 ----
    m_favTitleY = flow.block(44.0f).top;
    m_favHits.clear();
    if (m_favs.empty()) {
        flow.block(56.0f);
    } else {
        int n = (std::min)((int)m_favs.size(), 20);
        for (int i = 0; i < n; ++i) {
            float top = flow.block(54.0f + 8.0f).top;
            m_favHits.push_back({ { x0, top, x0 + contentW, top + 54.0f }, i });
        }
    }
    flow.block(18.0f);

    // ---- 历史区 ----
    m_histTitleY = flow.block(44.0f).top;
    m_histRects.clear();
    if (m_hist.empty()) {
        flow.block(56.0f);
    } else {
        int n = (std::min)((int)m_hist.size(), 24);
        for (int i = 0; i < n; ++i) {
            float top = flow.block(40.0f + 6.0f).top;
            m_histRects.push_back({ x0, top, x0 + contentW, top + 40.0f });
        }
    }
    flow.block(22.0f);

    // ---- 返回 ----
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
    m_widgets.push_back(&m_backBtn);
}

// ============================================================
//  更新
// ============================================================
void ProfileView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    if (m_toastT > 0.0f) m_toastT = (std::max)(0.0f, m_toastT - dt);

    // 档案上云（P1-1）：消费云端拉取的待合并档案（仅在 UI 线程写 m_profile，本地为基准）
    if (m_hasPending.exchange(false)) {
        AccountProfile p;
        { std::lock_guard<std::mutex> lk(m_pendingMu); p = m_pendingProfile; }
        AccountProfile merged = m_profile;
        if (!p.bio.empty())      merged.bio = p.bio;
        if (!p.major.empty())    merged.major = p.major;
        if (!p.school.empty())   merged.school = p.school;
        if (!p.birthday.empty()) merged.birthday = p.birthday;
        if (!p.gender.empty())   merged.gender = p.gender;
        m_profile = merged;
        SaveProfile();   // 落本地并幂等回推
    }
    if (m_cloudPushFailed.exchange(false)) {
        m_toast = L"档案同步失败（云端已拒绝，内容已存本机）";
        m_toastBad = true; m_toastT = 4.0f;
    }

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);

    float mx = in.mouseX, my = in.mouseY + ScrollY();

    // 编辑态：鼠标先交给字段（点击定位光标 / 拖拽框选），点字段外才提交
    if (m_editingOn && m_editField >= 0 && m_editField < 5 && m_cvCached) {
        bool bio = (m_editField == PF_BIO);
        TextStyle ts; ts.role = FontRole::Sans;
        ts.size = bio ? 12.5f : 13.0f;
        if (!bio) ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        ts.vAlign = VAlign::Middle;
        bool inside = m_edit.HandleMouse(in, *m_cvCached, EditTextBox(), ts, ScrollY());
        if (!inside && in.clicked) CommitEdit();
    }

    // 悬停字段
    m_hoverField = -1;
    for (int i = 0; i < 5; ++i)
        if (Hit(m_fieldRects[i], mx, my)) { m_hoverField = i; m_overInteractive = true; break; }

    // 悬停收藏卡
    m_hoverFav = -1;
    for (size_t i = 0; i < m_favHits.size(); ++i)
        if (Hit(m_favHits[i].r, mx, my)) { m_hoverFav = (int)i; m_overInteractive = true; break; }

    if (!in.clicked) return;

    // 批次 H：头像点击 → 选图更换（访客无档案目录，不开放）；名字点击 → 就地改昵称
    if (Hit(m_avatar, mx, my)) {
        if (!AccountStore::Instance().IsGuest()) PickAvatar();
        return;
    }
    if (Hit(m_nameRect, mx, my)) { BeginEdit(PF_NAME, m_nameRect); return; }

    // 字段点击
    if (m_hoverField >= 0) {
        int f = m_hoverField;
        if (f == PF_GENDER)      { CycleGender(); return; }   // 性别仍点击循环
        BeginEdit(f, m_fieldRects[f]);                         // 专业/学校/生日/简介：点进编辑
        return;
    }
    if (m_editingOn) { CommitEdit(); }

    // 收藏卡点击 → 跳到对应模块
    if (m_hoverFav >= 0 && m_hoverFav < (int)m_favHits.size()) {
        int idx = m_favHits[m_hoverFav].idx;
        if (idx >= 0 && idx < (int)m_favs.size()) {
            const auto& f = m_favs[idx];
            if (f.kind == L"column") Go(L"roadmap");
            else Go(L"video");
        }
        return;
    }
}

// ============================================================
//  绘制
// ============================================================
void ProfileView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    PaintTitle(cv, x0, m_contentTop, contentW);
    PaintHero(cv);
    PaintStats(cv);
    PaintWeak(cv);   // 批次 H：个人错题本
    PaintFavs(cv);
    PaintHistory(cv);

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

    if (m_toastT > 0.0f && !m_toast.empty()) {
        float a = Clamp01(m_toastT / 0.5f);
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 12.5f;
        ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
        float w = cv.MeasureWidth(m_toast, ts) + 40.0f;
        D2D1_RECT_F r{ (m_area.left + m_area.right) * 0.5f - w * 0.5f, m_area.bottom - 68.0f,
                       (m_area.left + m_area.right) * 0.5f + w * 0.5f, m_area.bottom - 30.0f };
        cv.PushOpacity(a);
        cv.FillRoundRect(r, shape::kEdge, WithAlpha(pal.ink900, 0.9f));
        cv.Text(m_toast, r, ts, pal.paperHi);
        cv.PopOpacity();
    }
}

void ProfileView::PaintTitle(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    float ha = Clamp01(m_t / 0.5f);
    cv.PushOpacity(ha);
    cv.Text(L"SECTION · 个人档案 · 收藏与足迹", { x0, y, x0 + 460.0f, y + 16.0f }, sec, pal.ink300);
    cv.PopOpacity();

    TextStyle h1; h1.role = FontRole::Serif; h1.size = 38.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"我 的", x0, y + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);

    float da = Clamp01((m_t - 0.25f) / 0.6f);
    if (da > 0.0f) {
        TextStyle ds; ds.role = FontRole::Mono; ds.size = 11.0f; ds.letterSpacing = 1.6f;
        ds.hAlign = HAlign::Right;
        cv.PushOpacity(ease::OutCubic(da));
        cv.Text(L"点击任一字段即可就地修改",
                { x0 + contentW - 360.0f, y + 2.0f, x0 + contentW, y + 20.0f }, ds, pal.ink500);
        cv.PopOpacity();
    }
    cv.PerforationH(x0, x0 + contentW, y + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));
}

void ProfileView::PaintHero(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.2f) / 0.55f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    auto& as = AccountStore::Instance();
    std::wstring name = as.CurrentName();
    std::wstring uid = as.UserId();
    bool guest = as.IsGuest();
    // 批次 H：昵称优先（不改账号）；头像点击可换图
    std::wstring disp = m_profile.displayName.empty() ? name : m_profile.displayName;
    bool nameEditing = (m_editingOn && m_editField == PF_NAME);

    cv.PaperCard(m_heroRect, 0.35f, shape::kEdge);
    cv.FillRect({ m_heroRect.left, m_heroRect.top, m_heroRect.left + 3.0f, m_heroRect.bottom },
                WithAlpha(pal.seal, 0.85f));

    // 头像：自选图片优先，否则朱砂圆章 + 首字；点击头像可更换图片
    float cx = (m_avatar.left + m_avatar.right) * 0.5f;
    float cy = (m_avatar.top + m_avatar.bottom) * 0.5f;
    float rr = (m_avatar.right - m_avatar.left) * 0.5f;
    bool haveAvatar = false;
    if (!m_profile.avatar.empty()) {
        // 懒加载（账户目录内 avatar.*）
        std::wstring p = as.CurrentRoot() + m_profile.avatar;
        if (!m_avatarBmp || m_avatarBmpPath != p) {
            m_avatarBmpPath = p;
            m_avatarBmp.Reset();
            ComPtr<IWICImagingFactory> wic;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&wic)))) {
                ComPtr<IWICBitmapDecoder> dec;
                if (SUCCEEDED(wic->CreateDecoderFromFilename(p.c_str(), nullptr, GENERIC_READ,
                                                             WICDecodeMetadataCacheOnLoad, &dec))) {
                    ComPtr<IWICBitmapFrameDecode> frame;
                    if (SUCCEEDED(dec->GetFrame(0, &frame)))
                        cv.DC()->CreateBitmapFromWicBitmap(frame.Get(), nullptr, &m_avatarBmp);
                }
            }
        }
        haveAvatar = m_avatarBmp != nullptr;
    }
    if (haveAvatar) {
        cv.DrawBitmap(m_avatarBmp.Get(), m_avatar, 1.0f);
        cv.StrokeRoundRect(m_avatar, 6.0f, WithAlpha(pal.seal, 0.8f), shape::kStroke);
    } else {
        cv.FillCircle(cx, cy, rr, WithAlpha(pal.seal, pal.dark ? 0.26f : 0.14f));
        cv.StrokeCircle(cx, cy, rr, WithAlpha(pal.seal, 0.8f), shape::kStroke);
        cv.StrokeCircle(cx, cy, rr - 4.0f, WithAlpha(pal.seal, 0.35f), shape::kHair);
        TextStyle av; av.role = FontRole::Serif; av.size = 34.0f;
        av.weight = DWRITE_FONT_WEIGHT_BLACK;
        av.hAlign = HAlign::Center; av.vAlign = VAlign::Middle;
        cv.Text(disp.empty() ? L"?" : disp.substr(0, 1), m_avatar, av, pal.seal);
    }
    // 头像右下角小相机徽标（提示可点更换）
    {
        float bx = m_avatar.right - 10.0f, by = m_avatar.bottom - 10.0f;
        cv.FillCircle(bx, by, 10.0f, pal.seal);
        TextStyle cb; cb.size = 10.0f; cb.hAlign = HAlign::Center; cb.vAlign = VAlign::Middle;
        cv.Text(L"换", { bx - 10.0f, by - 10.0f, bx + 10.0f, by + 10.0f }, cb, pal.paperHi);
    }

    float infoL = m_avatar.right + 24.0f;
    // 名号（批次 H：点击就地改昵称；编辑态由 FieldEdit 绘制）
    if (nameEditing) {
        TextStyle ns2; ns2.role = FontRole::Serif; ns2.size = 22.0f;
        ns2.weight = DWRITE_FONT_WEIGHT_BOLD; ns2.vAlign = VAlign::Middle;
        m_edit.Paint(cv, m_nameRect, ns2, pal.ink900, L"", pal.ink300, 0.0f, ScrollY());
    } else {
        TextStyle ns; ns.role = FontRole::Serif; ns.size = 25.0f;
        ns.weight = DWRITE_FONT_WEIGHT_BOLD; ns.letterSpacing = 1.5f;
        cv.Text(disp, m_nameRect, ns, pal.ink900);
        TextStyle hnt; hnt.role = FontRole::Mono; hnt.size = 9.5f; hnt.letterSpacing = 1.0f;
        cv.Text(L"点击改名", { m_nameRect.right - 76.0f, m_nameRect.top - 6.0f,
                               m_nameRect.right, m_nameRect.top + 10.0f }, hnt, pal.ink300);
    }

    // UID + 角色（账号名固定不变，仅昵称显示层可改）
    TextStyle us; us.role = FontRole::Mono; us.size = 11.0f; us.letterSpacing = 1.0f;
    std::wstring role = guest ? L"访客（数据仅存本机）"
                              : (as.IsDemoCurrent() ? L"演示账户" : L"注册账户");
    std::wstring cloud = Cloud::Instance().LoggedIn() ? L"云端已登录" : L"云端未登录";
    std::wstring acctLine = (m_profile.displayName.empty() || guest)
        ? (L"UID " + uid + L"   ·   " + role + L"   ·   " + cloud)
        : (L"UID " + uid + L"   ·   账号 " + name + L"   ·   " + cloud);
    cv.Text(acctLine,
            { infoL, m_heroRect.top + 62.0f, m_heroRect.right - 24.0f, m_heroRect.top + 82.0f },
            us, pal.ink500);

    // 简介字段格（与卡片同色 + 描边界定，编辑时 EDIT 无缝嵌入其中）
    {
        bool hov = (m_hoverField == PF_BIO);
        bool editing = (m_editingOn && m_editField == PF_BIO);
        cv.FillRoundRect(m_bioRect, shape::kEdge, pal.paperHi);
        cv.StrokeRoundRect(m_bioRect, shape::kEdge,
                           WithAlpha(editing ? pal.seal : (hov ? pal.seal : pal.rule),
                                     editing ? 0.95f : (hov ? 0.7f : 0.45f)),
                           editing ? shape::kStroke : shape::kHair);
        TextStyle bs; bs.role = FontRole::Sans; bs.size = 12.5f; bs.vAlign = VAlign::Middle;
        bool empty = m_profile.bio.empty();
        const std::wstring hint = L"还没有简介 —— 点这里写一句关于自己的话";
        D2D1_RECT_F tbox{ m_bioRect.left + 12.0f, m_bioRect.top,
                          m_bioRect.right - 12.0f, m_bioRect.bottom };
        if (editing) {
            // v2 统一输入框：文字/光标/选区/IME 全由 D3D 绘制
            m_edit.Paint(cv, tbox, bs, pal.ink900, hint, pal.ink300, 0.0f, ScrollY());
        } else {
            lj::PaintFieldEdit(cv, tbox, bs, empty ? hint : m_profile.bio,
                               empty ? pal.ink300 : pal.ink700, -1, 0.0f);
        }
    }

    // 资料四格
    struct FDef { int f; const wchar_t* label; const wchar_t* hint; };
    const FDef defs[4] = {
        { PF_MAJOR,  L"专业", L"点击输入" },
        { PF_SCHOOL, L"学校", L"点击输入" },
        { PF_BIRTH,  L"生日", L"YYYY-MM-DD" },
        { PF_GENDER, L"性别", L"点击切换" },
    };
    for (const auto& d : defs) {
        const auto& r = m_fieldRects[d.f];
        bool hov = (m_hoverField == d.f);
        bool editing = (m_editingOn && m_editField == d.f);

        cv.PaperCard(r, 0.0f, shape::kEdge);
        cv.StrokeRoundRect(r, shape::kEdge,
                           WithAlpha(editing || hov ? pal.seal : pal.rule,
                                     editing ? 0.95f : (hov ? 0.7f : 0.4f)),
                           editing ? shape::kStroke : shape::kHair);

        TextStyle ls; ls.role = FontRole::Mono; ls.size = 10.0f; ls.letterSpacing = 1.4f;
        cv.Text(d.label, { r.left + 12.0f, r.top + 9.0f, r.right - 12.0f, r.top + 24.0f },
                ls, pal.ink300);

        std::wstring val;
        switch (d.f) {
            case PF_MAJOR:  val = m_profile.major; break;
            case PF_SCHOOL: val = m_profile.school; break;
            case PF_BIRTH:  val = m_profile.birthday; break;
            case PF_GENDER: val = m_profile.gender; break;
        }
        // 输入区：与卡片同色的圆角格；编辑时文字/光标/选区/IME 全由 D3D 绘制
        D2D1_RECT_F vr{ r.left + 8.0f, r.top + 26.0f, r.right - 8.0f, r.bottom - 8.0f };
        cv.FillRoundRect(vr, 3.0f, pal.paperHi);
        TextStyle vs; vs.role = FontRole::Sans; vs.size = 13.0f;
        vs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; vs.vAlign = VAlign::Middle;
        bool empty = val.empty();
        D2D1_RECT_F tbox{ vr.left + 4.0f, vr.top, vr.right - 4.0f, vr.bottom };
        if (editing) {
            m_edit.Paint(cv, tbox, vs, pal.ink900, d.hint, pal.ink300, 0.0f, ScrollY());
        } else {
            lj::PaintFieldEdit(cv, tbox, vs, empty ? d.hint : val,
                               empty ? pal.ink300 : pal.ink900, -1, 0.0f);
        }
    }

    cv.PopOpacity();
    cv.PopTransform();
}

void ProfileView::PaintStats(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.34f) / 0.55f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    struct SDef { const wchar_t* label; std::wstring value; D2D1_COLOR_F c; };
    wchar_t fbuf[32];
    if (m_focusMin >= 60) swprintf_s(fbuf, L"%.1f h", (float)m_focusMin / 60.0f);
    else swprintf_s(fbuf, L"%d min", m_focusMin);

    SDef defs[4] = {
        { L"收藏",     std::to_wstring(m_favs.size()),  pal.brass },
        { L"浏览历史", std::to_wstring(m_hist.size()),  pal.jade  },
        { L"我的专栏", std::to_wstring(m_colCount),     pal.seal  },
        { L"累计专注", fbuf,                            pal.vermilion },
    };
    for (int i = 0; i < 4; ++i) {
        const auto& r = m_statRects[i];
        cv.PaperCard(r, 0.15f, shape::kEdge);
        cv.FillRect({ r.left, r.top, r.left + 3.0f, r.bottom }, WithAlpha(defs[i].c, 0.8f));
        TextStyle ls; ls.role = FontRole::Mono; ls.size = 10.0f; ls.letterSpacing = 1.4f;
        cv.Text(defs[i].label, { r.left + 14.0f, r.top + 10.0f, r.right - 12.0f, r.top + 24.0f },
                ls, pal.ink300);
        TextStyle vs; vs.role = FontRole::Mono; vs.size = 21.0f;
        vs.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(defs[i].value, { r.left + 14.0f, r.top + 28.0f, r.right - 12.0f, r.bottom - 8.0f },
                vs, pal.ink900);
    }

    cv.PopOpacity();
    cv.PopTransform();
}

void ProfileView::PaintFavs(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.44f) / 0.55f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 我的收藏", { x0, m_favTitleY, x0 + 400.0f, m_favTitleY + 16.0f }, sec, pal.ink300);
    TextStyle h2; h2.role = FontRole::Serif; h2.size = 20.0f;
    h2.weight = DWRITE_FONT_WEIGHT_BOLD; h2.letterSpacing = 1.5f;
    cv.Text(L"收藏（" + std::to_wstring(m_favs.size()) + L"）",
            { x0, m_favTitleY + 18.0f, x0 + contentW, m_favTitleY + 42.0f }, h2, pal.ink900);

    if (m_favHits.empty()) {
        D2D1_RECT_F r{ x0, m_favTitleY + 44.0f, x0 + contentW, m_favTitleY + 44.0f + 48.0f };
        cv.FillRoundRect(r, shape::kEdge, WithAlpha(pal.sealWash, pal.dark ? 0.16f : 0.22f));
        TextStyle es; es.role = FontRole::Sans; es.size = 12.5f; es.vAlign = VAlign::Middle;
        cv.Text(L"还没有收藏。去「专栏」点 ☆ 收藏，或在「视频 / 图片」里收藏喜欢的内容。",
                { r.left + 18.0f, r.top, r.right - 18.0f, r.bottom }, es, pal.ink500);
    } else {
        for (size_t i = 0; i < m_favHits.size(); ++i) {
            const auto& h = m_favHits[i];
            if (h.idx < 0 || h.idx >= (int)m_favs.size()) continue;
            const auto& f = m_favs[h.idx];
            bool hov = (m_hoverFav == (int)i);
            D2D1_COLOR_F kc = (f.kind == L"column") ? pal.seal
                            : (f.kind == L"video")  ? pal.vermilion : pal.jade;

            cv.PaperCard(h.r, hov ? 0.45f : 0.12f, shape::kEdge);
            cv.StrokeRoundRect(h.r, shape::kEdge, WithAlpha(hov ? kc : pal.rule, hov ? 0.85f : 0.4f),
                               hov ? shape::kStroke : shape::kHair);

            D2D1_RECT_F kb{ h.r.left + 14.0f, h.r.top + 15.0f, h.r.left + 62.0f, h.r.bottom - 15.0f };
            cv.FillRoundRect(kb, 3.0f, WithAlpha(kc, 0.16f));
            cv.StrokeRoundRect(kb, 3.0f, WithAlpha(kc, 0.65f), shape::kHair);
            TextStyle ks; ks.role = FontRole::Sans; ks.size = 11.0f;
            ks.hAlign = HAlign::Center; ks.vAlign = VAlign::Middle;
            cv.Text(KindLabel(f.kind), kb, ks, kc);

            TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.5f;
            ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; ts.vAlign = VAlign::Middle;
            cv.Text(f.title, { h.r.left + 76.0f, h.r.top, h.r.right - 250.0f, h.r.bottom }, ts, pal.ink900);

            TextStyle ms; ms.role = FontRole::Mono; ms.size = 10.5f;
            ms.hAlign = HAlign::Right; ms.vAlign = VAlign::Middle;
            std::wstring meta = (f.author.empty() ? L"" : f.author + L"  ·  ") + PvFormatTs(f.ts);
            cv.Text(meta, { h.r.right - 240.0f, h.r.top, h.r.right - 16.0f, h.r.bottom }, ms, pal.ink500);
        }
    }

    cv.PopOpacity();
    cv.PopTransform();
}

void ProfileView::PaintHistory(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.54f) / 0.55f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.Text(L"SECTION · 浏览足迹", { x0, m_histTitleY, x0 + 400.0f, m_histTitleY + 16.0f }, sec, pal.ink300);
    TextStyle h2; h2.role = FontRole::Serif; h2.size = 20.0f;
    h2.weight = DWRITE_FONT_WEIGHT_BOLD; h2.letterSpacing = 1.5f;
    cv.Text(L"历史（" + std::to_wstring(m_hist.size()) + L"）",
            { x0, m_histTitleY + 18.0f, x0 + contentW, m_histTitleY + 42.0f }, h2, pal.ink900);

    if (m_histRects.empty()) {
        D2D1_RECT_F r{ x0, m_histTitleY + 44.0f, x0 + contentW, m_histTitleY + 44.0f + 48.0f };
        cv.FillRoundRect(r, shape::kEdge, WithAlpha(pal.sealWash, pal.dark ? 0.16f : 0.22f));
        TextStyle es; es.role = FontRole::Sans; es.size = 12.5f; es.vAlign = VAlign::Middle;
        cv.Text(L"还没有浏览记录。读过的专栏、播放过的视频与音频会自动记在这里。",
                { r.left + 18.0f, r.top, r.right - 18.0f, r.bottom }, es, pal.ink500);
    } else {
        for (size_t i = 0; i < m_histRects.size() && i < m_hist.size(); ++i) {
            const auto& r = m_histRects[i];
            const auto& hi = m_hist[i];
            D2D1_COLOR_F kc = (hi.kind == L"column") ? pal.seal
                            : (hi.kind == L"video")  ? pal.vermilion
                            : (hi.kind == L"audio")  ? pal.brass : pal.jade;

            cv.FillRoundRect(r, shape::kEdgeSoft, WithAlpha(pal.sealWash, pal.dark ? 0.14f : 0.20f));
            cv.FillRect({ r.left, r.top, r.left + 2.5f, r.bottom }, WithAlpha(kc, 0.75f));

            TextStyle ks; ks.role = FontRole::Mono; ks.size = 10.0f; ks.vAlign = VAlign::Middle;
            cv.Text(KindLabel(hi.kind), { r.left + 14.0f, r.top, r.left + 60.0f, r.bottom }, ks, kc);

            TextStyle ts; ts.role = FontRole::Sans; ts.size = 12.5f; ts.vAlign = VAlign::Middle;
            cv.Text(hi.title, { r.left + 66.0f, r.top, r.right - 170.0f, r.bottom }, ts, pal.ink700);

            TextStyle ms; ms.role = FontRole::Mono; ms.size = 10.0f;
            ms.hAlign = HAlign::Right; ms.vAlign = VAlign::Middle;
            cv.Text(PvFormatTs(hi.ts), { r.right - 160.0f, r.top, r.right - 14.0f, r.bottom }, ms, pal.ink300);
        }
    }

    cv.PopOpacity();
    cv.PopTransform();
}

// ============================================================
//  批次 H：个人错题本（练考薄弱知识点 TOP）
// ============================================================
void ProfileView::PaintWeak(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float a = Clamp01(m_t / 0.5f);
    if (a <= 0.004f) return;

    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    cv.PushOpacity(a);
    cv.Text(L"SECTION · 个人错题本 · 薄弱知识点", { m_area.left + 28.0f, m_weakTitleY,
            m_area.left + 460.0f, m_weakTitleY + 16.0f }, sec, pal.ink300);
    cv.PopOpacity();

    float y = m_weakTitleY + 26.0f;
    float w = m_area.right - m_area.left - 56.0f;
    float x0 = m_area.left + 28.0f;

    if (m_weakPts.empty()) {
        TextStyle et; et.size = 12.5f; et.role = FontRole::Sans; et.vAlign = VAlign::Middle;
        cv.Text(L"还没有错题记录。完成每日练考后，答错的知识点会自动汇入这里。",
                { x0, y, x0 + w, y + 34.0f }, et, pal.ink500);
        return;
    }

    int maxN = 1;
    for (auto& wp : m_weakPts) maxN = (std::max)(maxN, wp.second);
    int n = (std::min)((int)m_weakPts.size(), 8);
    for (int i = 0; i < n; ++i) {
        const auto& wp = m_weakPts[i];
        float rowY = y + (float)i * 40.0f;
        // 名称 + 次数
        TextStyle ts; ts.size = 13.0f; ts.role = FontRole::Sans; ts.vAlign = VAlign::Middle;
        ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        cv.Text(wp.first, { x0, rowY, x0 + w * 0.55f, rowY + 26.0f }, ts, pal.ink900);
        TextStyle cs; cs.role = FontRole::Mono; cs.size = 11.0f; cs.vAlign = VAlign::Middle;
        cs.hAlign = HAlign::Right;
        cv.Text(std::to_wstring(wp.second) + L" 次",
                { x0 + w - 90.0f, rowY, x0 + w, rowY + 26.0f }, cs, pal.seal);
        // 比例条
        float barW = w * 0.30f;
        float bx = x0 + w * 0.58f;
        float t = (float)wp.second / (float)maxN;
        cv.FillRoundRect({ bx, rowY + 9.0f, bx + barW, rowY + 15.0f }, 3.0f,
                         WithAlpha(pal.rule, 0.6f));
        if (t > 0.01f)
            cv.FillRoundRect({ bx, rowY + 9.0f, bx + barW * t, rowY + 15.0f }, 3.0f,
                             WithAlpha(pal.seal, 0.85f));
    }
}

// 批次 H：选择图片作为头像（拷入账户目录，存文件名到 profile.json）
void ProfileView::PickAvatar()
{
    wchar_t path[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = AppHwnd();
    ofn.lpstrFilter = L"图片 (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"选择头像图片";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return;

    auto& as = AccountStore::Instance();
    // 取扩展名 → 目标文件名 avatar.<ext>
    std::wstring src = path;
    size_t dot = src.find_last_of(L'.');
    std::wstring ext = (dot == std::wstring::npos) ? L".png" : src.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    std::wstring dst = as.CurrentRoot() + L"avatar" + ext;
    if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) {
        m_toast = L"头像拷贝失败，请重试";
        m_toastBad = true; m_toastT = 2.5f;
        return;
    }
    m_profile.avatar = L"avatar" + ext;
    SaveProfile();
    m_avatarBmp.Reset();          // 强制重载
    m_avatarBmpPath.clear();
    m_toast = L"头像已更新";
    m_toastBad = false; m_toastT = 2.5f;
}

} // namespace lj
