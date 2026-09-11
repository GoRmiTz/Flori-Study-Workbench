#include "views/MediaView.h"
#include "app/Store.h"
#include "core/Hwnd.h"       // §5 AppHwnd() for GetOpenFileNameW
#include "ui/ImageViewer.h"  // 云端图片软件内查看（带鉴权）
#include "ui/Layout.h"
#include "audio/MusicPlayer.h" // P1-3 本地音乐播放
#include <cmath>
#include <algorithm>
#include <functional>
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>     // §5 GetOpenFileNameW

namespace lj {

// ---------- 本地资源扫描辅助 ----------
static std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring s(buf, n);
    size_t pos = s.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"" : s.substr(0, pos);
}

static std::wstring LowerExt(const std::wstring& path)
{
    size_t p = path.find_last_of(L".");
    if (p == std::wstring::npos) return L"";
    std::wstring e = path.substr(p + 1);
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    return e;
}

// 0 源视频 / 1 源音乐 / 2 图片 / -1 跳过
static int KindOf(const std::wstring& ext)
{
    if (ext == L"mp4" || ext == L"mkv" || ext == L"avi" || ext == L"mov" ||
        ext == L"webm" || ext == L"wmv" || ext == L"flv" || ext == L"m4v") return 0;
    if (ext == L"mp3" || ext == L"wav" || ext == L"ogg" || ext == L"flac" ||
        ext == L"m4a" || ext == L"aac" || ext == L"wma") return 1;
    if (ext == L"png" || ext == L"jpg" || ext == L"jpeg" || ext == L"gif" ||
        ext == L"bmp" || ext == L"webp" || ext == L"svg" || ext == L"tiff") return 2;
    return -1;
}

static void ScanRecursive(const std::wstring& dir, std::function<void(const std::wstring&)> out)
{
    WIN32_FIND_DATAW fd;
    std::wstring pat = dir + L"\\*";
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ScanRecursive(full, out);
        else out(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void OpenPath(const std::wstring& path)
{
    if (path.empty()) return;
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void MediaView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_hoverRect = { 0,0,0,0 };

    // 扫描本地资源库：<exe>/assets/media（递归，按扩展名分类）
    // 0 音乐 / 1 课程视频 / 2 图片素材 —— 与云端按 kind 合并进同一分类
    for (int i = 0; i < 3; ++i) m_local[i].clear();

    std::wstring root = ExeDir() + L"\\assets\\media";
    ScanRecursive(root, [&](const std::wstring& full) {
        std::wstring ext = LowerExt(full);
        int k = KindOf(ext);
        if (k < 0) return;
        MediaFile f;
        f.path = full;
        f.ext = ext;
        std::wstring fn = full.substr(full.find_last_of(L"\\/") + 1);
        size_t dp = fn.find_last_of(L".");
        f.name = (dp != std::wstring::npos) ? fn.substr(0, dp) : fn;
        // KindOf: 0 视频 / 1 音乐 / 2 图片 → 本页分类顺序：音乐(0) 视频(1) 图片(2)
        int ci = (k == 1) ? 0 : (k == 0) ? 1 : 2;
        m_local[ci].push_back(std::move(f));
    });

    // 先以本地数据建合并分类（云端拉取完成后由 SnapshotCloud 重建）
    BuildMergedCats();

    // §5 拉取云端资源库
    m_srcFetched = false;
    m_pubLive = false;
    FetchCloudMedia();
}

void MediaView::OnLeave()
{
    View::OnLeave();
    // 图片查看器是全局单例：切走若不关闭，回来时全屏遮罩仍在 → 页面「变黑」
    ImageViewer::Instance().Close();
}

D2D1_COLOR_F MediaAccent(int a, const Palette& pal)
{
    if (a == 1) return pal.brass;
    if (a == 2) return pal.jade;
    return pal.seal;
}

// 本地 + 云端快照 → 三个合并分类（音乐 / 课程视频 / 图片素材）
void MediaView::BuildMergedCats()
{
    m_cats.clear();
    m_cats.push_back({ L"音乐 · 自习室可播", 1, {}, {}, {} });
    m_cats.push_back({ L"课程视频",           0, {}, {}, {} });
    m_cats.push_back({ L"图片素材",           2, {}, {}, {} });

    for (int ci = 0; ci < 3; ++ci) {
        for (auto& f : m_local[ci]) {
            UnifiedItem it; it.src = Src::Local; it.local = f;   // 拷贝：m_local 需保留给下次重建
            m_cats[ci].items.push_back(std::move(it));
        }
    }
    // 云端 kind → 分类：music→0 videos→1 images→2
    for (auto& n : m_pubMedia) {
        int ci = (n.kind == "music") ? 0 : (n.kind == "videos") ? 1 : 2;
        if (ci < 0 || ci > 2) continue;
        UnifiedItem it; it.src = Src::Cloud; it.net = n;          // 拷贝：m_pubMedia 快照保持完整
        m_cats[ci].items.push_back(std::move(it));
    }
}

// ============================================================
//  布局
// ============================================================
void MediaView::Layout(const D2D1_RECT_F& area, Canvas& cv)
{
    View::Layout(area, cv);

    float availW = area.right - area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = area.left + (availW - contentW) * 0.5f;

    // 纵向流式布局：区块只声明自身高度，位置由 flow 推进
    lj::ui::VLayout flow(x0, area.top + 30.0f, contentW, 0.0f);

    // 标题区
    m_contentTop = flow.cursorY;
    flow.block(96.0f);

    // 说明卡（本地 + 云端合并说明，高度按内容动态测量）
    TextStyle ds; ds.role = FontRole::Sans; ds.size = 12.0f;
    float noteH = 20.0f;
    noteH += cv.MeasureHeight(L"一个资源库，两类来源：本地（assets/media 目录扫描）+ 云端（本账户上传）。每条卡片右侧徽标标明来源：金「本地」/ 碧「云端」。", ds, contentW - 36.0f) + 8.0f;
    noteH += cv.MeasureHeight(L"想加本地素材？点这张说明卡打开 assets/media 文件夹，音乐丢 music、课程视频丢 videos、图片丢 images，回本页重进即自动收录。想上云？点右上「上传到云端」。", ds, contentW - 36.0f) + 12.0f;
    m_noteRect = { x0, flow.cursorY, x0 + contentW, flow.cursorY + noteH + 28.0f };
    m_noteY = flow.block(noteH + 28.0f).top;
    flow.block(16.0f);   // 说明卡与工具栏间距

    // 工具栏：云端状态 + 上传按钮（右对齐）
    m_uploadBtn.label = L"↑ 上传到云端";
    m_uploadBtn.fontSize = 12.5f;
    {
        float top = flow.block(34.0f + 8.0f).top;
        m_uploadBtn.bounds = { x0 + contentW - 150.0f, top, x0 + contentW, top + 34.0f };
    }
    m_uploadBtn.onClick = [this] {
        wchar_t szFile[MAX_PATH] = { 0 };
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = AppHwnd();
        ofn.lpstrFile = szFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"媒体文件\0*.mp4;*.mkv;*.avi;*.mov;*.webm;*.mp3;*.wav;*.ogg;*.flac;*.m4a;*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp\0所有文件\0*.*\0";
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) DoUpload(szFile);
    };

    // 三个合并分类
    m_catY = flow.cursorY;
    float gap = 16.0f;
    for (size_t ci = 0; ci < m_cats.size(); ++ci) {
        auto& cat = m_cats[ci];
        cat.cardRects.clear();
        cat.delBtns.clear();
        float innerH = 44.0f + 12.0f;
        if (cat.items.empty()) {
            innerH += 46.0f + 8.0f;
        } else {
            innerH += (float)cat.items.size() * (50.0f + 8.0f);
        }
        float top = flow.block(innerH + 6.0f + gap).top;
        float yy = top + 44.0f + 12.0f;
        if (cat.items.empty()) {
            cat.cardRects.push_back({ x0 + 14.0f, yy, x0 + contentW - 14.0f, yy + 46.0f });
            cat.delBtns.push_back({ 0,0,0,0 });
            yy += 46.0f + 8.0f;
        } else {
            for (size_t i = 0; i < cat.items.size(); ++i) {
                D2D1_RECT_F row{ x0 + 14.0f, yy, x0 + contentW - 14.0f, yy + 50.0f };
                cat.cardRects.push_back(row);
                const auto& it = cat.items[i];
                if (it.src == Src::Cloud && it.net.mine)
                    cat.delBtns.push_back({ row.right - 74.0f, row.top + 12.0f, row.right - 16.0f, row.top + 38.0f });
                else
                    cat.delBtns.push_back({ 0,0,0,0 });
                yy += 50.0f + 8.0f;
            }
        }
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
    m_widgets.push_back(&m_backBtn);
    if (m_pubCanUpload) m_widgets.push_back(&m_uploadBtn);
}

void MediaView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);

    // §5 云端快照消费 → 重建合并分类（下一帧 Layout 对齐矩形）
    if (m_cloudDirty.exchange(false)) SnapshotCloud();

    // 卡片：悬停高亮 + 点击（本地打开 / 云端删除自己的）
    float mx = in.mouseX, my = shifted.mouseY;
    m_hoverRect = { 0,0,0,0 };
    for (size_t ci = 0; ci < m_cats.size(); ++ci) {
        auto& cat = m_cats[ci];
        for (size_t i = 0; i < cat.cardRects.size() && i < cat.items.size(); ++i) {
            const auto& r = cat.cardRects[i];
            if (mx < r.left || mx > r.right || my < r.top || my > r.bottom) continue;
            m_hoverRect = r;
            const auto& it = cat.items[i];
            if (!in.clicked) break;
            if (it.src == Src::Local) {
                // P1-3：本地音乐点击即播放（走 MusicPlayer），视频/图片仍用默认程序打开
                if (ci == 0 && MusicPlayer::Instance().GetState() != MusicPlayer::State::Loading) {
                    MusicPlayer::Instance().PlayLocal(it.local.path, it.local.name);
                } else {
                    OpenPath(it.local.path);
                }
            } else {
                // 云端：删除按钮（自己的资源）
                const auto& db = cat.delBtns[i];
                bool onDel = (it.net.mine && mx >= db.left && mx <= db.right &&
                              my >= db.top && my <= db.bottom);
                if (onDel) {
                    std::string mid = it.net.id;
                    Cloud::RunAsync([this, mid] {
                        Cloud::Instance().DeleteMedia(mid);
                        FetchCloudMedia();
                    });
                } else if (it.net.kind == "images") {
                    // 云端图片：软件内查看（带鉴权拉取 + WIC 解码），不再裸 URL 401
                    ImageViewer::Instance().Open(it.net.id, it.net.title);
                }
                // 云端音乐/视频条目点击暂不响应（音频/视频播放引擎范围）
            }
            break;
        }
    }

    // 软件内图片查看浮层（未打开时零开销）
    ImageViewer::Instance().Update(dt, in);
}

// ============================================================
//  绘制
// ============================================================
void MediaView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    PaintTitle(cv, x0, m_contentTop, contentW);
    PaintNote(cv, x0, m_noteY, contentW);

    // 工具栏：云端连接状态 + 上传按钮
    {
        float ty = m_noteRect.bottom + 16.0f;
        TextStyle st; st.role = FontRole::Mono; st.size = 10.5f; st.vAlign = VAlign::Middle;
        cv.Text(m_pubLive ? L"● 云端已连接 · 上传的资源所有用户可见" : L"○ 云端离线 · 仅显示本地资源",
                { x0 + 4.0f, ty, x0 + contentW - 160.0f, ty + 34.0f }, st,
                m_pubLive ? pal.jade : pal.ink300);
        if (m_pubCanUpload) m_uploadBtn.Paint(cv);
    }

    PaintCats(cv, x0, m_catY, contentW);

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

    // 软件内图片查看浮层（最上层）
    ImageViewer::Instance().Paint(cv, m_area);
}

void MediaView::PaintTitle(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    const float w = contentW;
    (void)w;
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    float ha = Clamp01(m_t / 0.5f);
    cv.PushOpacity(ha);
    cv.Text(L"SECTION · 资源库 · 本地 + 云端", { x0, y, x0 + 420.0f, y + 16.0f }, sec, pal.ink300);
    cv.PopOpacity();

    TextStyle h1; h1.role = FontRole::Serif; h1.size = 38.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"资源库", x0, y + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);

    cv.PerforationH(x0, x0 + contentW, y + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));
}

void MediaView::PaintNote(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.3f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    cv.PaperCard(m_noteRect, 0.0f, shape::kEdge);
    cv.FillRect({ m_noteRect.left, m_noteRect.top, m_noteRect.left + 3.0f, m_noteRect.bottom }, WithAlpha(pal.brass, 0.8f));

    TextStyle ts; ts.role = FontRole::Sans; ts.size = 12.0f; ts.vAlign = VAlign::Top;
    float ix = m_noteRect.left + 16.0f;
    cv.Text(L"一个资源库，两类来源：本地（assets/media 目录扫描）+ 云端（本账户上传）。每条卡片右侧徽标标明来源：金「本地」/ 碧「云端」。",
            { ix, m_noteRect.top + 12.0f, m_noteRect.right - 16.0f, m_noteRect.top + 50.0f }, ts, pal.ink700);
    cv.Text(L"想加本地素材？点这张说明卡打开 assets/media 文件夹，音乐丢 music、课程视频丢 videos、图片丢 images，回本页重进即自动收录。想上云？点右上「上传到云端」。",
            { ix, m_noteRect.top + 56.0f, m_noteRect.right - 16.0f, m_noteRect.bottom - 10.0f }, ts, pal.ink500);

    cv.PopOpacity();
    cv.PopTransform();
}

void MediaView::PaintCats(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.4f) / 0.6f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    for (size_t ci = 0; ci < m_cats.size(); ++ci) {
        auto& cat = m_cats[ci];
        D2D1_COLOR_F ac = MediaAccent(cat.accent, pal);
        float ca = Clamp01((m_t - 0.5f - (float)ci * 0.08f) / 0.5f);
        if (ca <= 0.004f) continue;
        float er = ease::OutCubic(ca);

        // 分类头
        float headerTop = (cat.cardRects.empty()) ? 0.0f : (cat.cardRects[0].top - 56.0f);
        D2D1_RECT_F header{ x0, headerTop, x0 + contentW, headerTop + 44.0f };
        cv.PushOpacity(er);
        cv.PaperCard(header, 0.0f, shape::kEdge);
        cv.FillRect({ header.left, header.top, header.left + 3.0f, header.bottom }, WithAlpha(ac, 0.85f));
        TextStyle hs; hs.role = FontRole::Sans; hs.size = 14.5f; hs.weight = DWRITE_FONT_WEIGHT_BOLD;
        hs.vAlign = VAlign::Middle;
        cv.Text(cat.title, { header.left + 16.0f, header.top, header.right - 200.0f, header.bottom }, hs, pal.ink900);
        // 计数：本地 + 云端
        int nLocal = 0, nCloud = 0;
        for (const auto& it : cat.items) { if (it.src == Src::Local) nLocal++; else nCloud++; }
        TextStyle ct; ct.role = FontRole::Mono; ct.size = 11.0f; ct.hAlign = HAlign::Right; ct.vAlign = VAlign::Middle;
        std::wstring cnt = std::to_wstring(nLocal) + L" 本地 · " + std::to_wstring(nCloud) + L" 云端";
        cv.Text(cnt, { header.left, header.top, header.right - 16.0f, header.bottom }, ct,
                nCloud > 0 ? pal.ink700 : pal.ink300);
        cv.PopOpacity();

        // 卡片
        for (size_t i = 0; i < cat.items.size() && i < cat.cardRects.size(); ++i) {
            const auto& it = cat.items[i];
            const auto& row = cat.cardRects[i];
            cv.PushOpacity(er);
            bool hov = (row.left == m_hoverRect.left && row.top == m_hoverRect.top &&
                        row.right == m_hoverRect.right && row.bottom == m_hoverRect.bottom);
            cv.FillRoundRect(row, shape::kEdgeSoft, WithAlpha(hov ? ac : pal.sealWash, hov ? 0.22f : (pal.dark ? 0.22f : 0.30f)));
            cv.StrokeRoundRect(row, shape::kEdgeSoft, WithAlpha(hov ? ac : pal.rule, hov ? 0.9f : 0.5f), 1.0f);

            // 来源徽标：本地=金 / 云端=碧
            D2D1_COLOR_F srcC = (it.src == Src::Cloud) ? pal.jade : pal.brass;
            D2D1_RECT_F sb{ row.left + 10.0f, row.top + 13.0f, row.left + 58.0f, row.bottom - 13.0f };
            cv.FillRoundRect(sb, 3.0f, WithAlpha(srcC, 0.16f));
            cv.StrokeRoundRect(sb, 3.0f, WithAlpha(srcC, 0.7f), 1.0f);
            TextStyle ss; ss.role = FontRole::Mono; ss.size = 10.0f; ss.hAlign = HAlign::Center; ss.vAlign = VAlign::Middle;
            cv.Text(it.src == Src::Cloud ? L"云端" : L"本地", sb, ss, srcC);

            if (it.src == Src::Local) {
                // 本地：文件名 + 扩展名徽标
                TextStyle fs; fs.role = FontRole::Sans; fs.size = 13.0f; fs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
                fs.vAlign = VAlign::Middle;
                cv.Text(it.local.name, { row.left + 68.0f, row.top, row.right - 100.0f, row.bottom }, fs, pal.ink900);
                float bw = cv.MeasureWidth(it.local.ext, fs) + 18.0f;
                D2D1_RECT_F badge{ row.right - bw - 12.0f, row.top + 13.0f, row.right - 12.0f, row.bottom - 13.0f };
                cv.FillRoundRect(badge, 3.0f, WithAlpha(ac, 0.16f));
                cv.StrokeRoundRect(badge, 3.0f, WithAlpha(ac, 0.7f), 1.0f);
                TextStyle bs; bs.role = FontRole::Mono; bs.size = 10.0f; bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle;
                cv.Text(it.local.ext, badge, bs, ac);
            } else {
                // 云端：类型徽标 + 标题 + 用户/大小 + 删除（自己的）
                const auto& m = it.net;
                std::wstring kindLabel = (m.kind == "videos") ? L"视频" : (m.kind == "music") ? L"音乐" : L"图片";
                D2D1_COLOR_F kc = (m.kind == "videos") ? pal.seal : (m.kind == "music") ? pal.brass : pal.jade;
                TextStyle ks; ks.role = FontRole::Mono; ks.size = 10.0f; ks.hAlign = HAlign::Center; ks.vAlign = VAlign::Middle;
                D2D1_RECT_F kb{ row.left + 68.0f, row.top + 13.0f, row.left + 116.0f, row.bottom - 13.0f };
                cv.FillRoundRect(kb, 3.0f, WithAlpha(kc, 0.16f));
                cv.Text(kindLabel, kb, ks, kc);

                TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.0f; ts.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
                ts.vAlign = VAlign::Middle;
                cv.Text(m.title, { row.left + 126.0f, row.top, row.right - 240.0f, row.bottom }, ts, pal.ink900);
                TextStyle us; us.role = FontRole::Mono; us.size = 10.5f; us.vAlign = VAlign::Middle; us.hAlign = HAlign::Right;
                wchar_t szBuf[32];
                if (m.size > 1024 * 1024) swprintf_s(szBuf, L"%.1f MB", (float)m.size / 1048576.0f);
                else if (m.size > 1024) swprintf_s(szBuf, L"%.0f KB", (float)m.size / 1024.0f);
                else swprintf_s(szBuf, L"%lld B", m.size);
                cv.Text(m.user + L"  ·  " + szBuf, { row.right - 230.0f, row.top, row.right - 86.0f, row.bottom }, us, pal.ink500);

                // 删除按钮（仅自己的资源）
                const auto& db = cat.delBtns[i];
                if (m.mine && db.right > db.left) {
                    cv.FillRoundRect(db, 5.0f, WithAlpha(pal.vermilion, 0.12f));
                    cv.StrokeRoundRect(db, 5.0f, WithAlpha(pal.vermilion, 0.6f), shape::kHair);
                    TextStyle ds; ds.role = FontRole::Sans; ds.size = 11.0f; ds.hAlign = HAlign::Center; ds.vAlign = VAlign::Middle;
                    cv.Text(L"删除", db, ds, pal.vermilion);
                }
            }
            cv.PopOpacity();
        }

        // 空态
        if (cat.items.empty()) {
            const auto& row = cat.cardRects[0];
            cv.PushOpacity(er);
            cv.FillRoundRect(row, shape::kEdgeSoft, WithAlpha(pal.sealWash, pal.dark ? 0.18f : 0.24f));
            TextStyle es; es.role = FontRole::Sans; es.size = 12.0f; es.vAlign = VAlign::Middle;
            cv.Text(L"暂无素材 —— 本地：把文件放进 assets/media 对应子文件夹后重进本页；云端：点「上传到云端」。",
                    { row.left + 16.0f, row.top, row.right - 16.0f, row.bottom }, es, pal.ink500);
            cv.PopOpacity();
        }

        cv.PopOpacity();
    }

    cv.PopOpacity();
    cv.PopTransform();
}

// ============================================================
//  §5 云端资源库：拉取 / 快照 / 上传
// ============================================================
void MediaView::FetchCloudMedia()
{
    Cloud::RunAsync([this] {
        auto r = Cloud::Instance().GetMediaList();
        if (!r.Ok()) return;
        lj::json::Parser pp(r.body.data(), r.body.size());
        lj::json::JVal root = pp.parse();
        if (!root.IsObj()) return;

        std::vector<NetMedia> items;
        const auto* arr = lj::json::JGet(root, "items");
        if (arr && arr->IsArr()) {
            for (const auto& v : arr->arr) {
                NetMedia m;
                m.id = lj::json::JStr(v, "id");
                m.kind = lj::json::JStr(v, "kind");
                m.title = net::FromUtf8(lj::json::JStr(v, "title"));
                m.note = net::FromUtf8(lj::json::JStr(v, "note"));
                m.user = net::FromUtf8(lj::json::JStr(v, "user"));
                const auto* sz = lj::json::JGet(v, "size");
                if (sz) m.size = (long long)sz->num;
                const auto* ts = lj::json::JGet(v, "ts");
                if (ts) m.ts = (long long)ts->num;
                const auto* mn = lj::json::JGet(v, "mine");
                if (mn) m.mine = mn->bval;
                items.push_back(std::move(m));
            }
        }
        bool canUpload = false;
        const auto* cu = lj::json::JGet(root, "canUpload");
        if (cu) canUpload = cu->bval;

        {
            std::lock_guard<std::mutex> lk(m_cloudMu);
            m_srcMedia = std::move(items);
            m_srcCanUpload = canUpload;
            m_srcFetched = true;
        }
        m_cloudDirty.store(true);
    });
}

void MediaView::SnapshotCloud()
{
    std::lock_guard<std::mutex> lk(m_cloudMu);
    m_pubMedia = m_srcMedia;
    m_pubCanUpload = m_srcCanUpload;
    m_pubLive = m_srcFetched && Cloud::Instance().Online();
    // 重建合并分类（本地 + 云端），下一帧 Layout 按新列表重排
    BuildMergedCats();
}

void MediaView::DoUpload(const std::wstring& path)
{
    // 读取文件（二进制）
    std::string bin;
    if (!lj::json::ReadFileRaw(path, bin)) return;

    // 从扩展名推断 kind
    std::wstring ext = LowerExt(path);
    std::string kind;
    if (ext == L"mp4" || ext == L"mkv" || ext == L"avi" || ext == L"mov" ||
        ext == L"webm" || ext == L"wmv" || ext == L"flv" || ext == L"m4v") kind = "videos";
    else if (ext == L"mp3" || ext == L"wav" || ext == L"ogg" || ext == L"flac" ||
             ext == L"m4a" || ext == L"aac" || ext == L"wma") kind = "music";
    else if (ext == L"png" || ext == L"jpg" || ext == L"jpeg" || ext == L"gif" ||
             ext == L"bmp" || ext == L"webp" || ext == L"svg" || ext == L"tiff") kind = "images";
    else return;

    // 文件名 + 标题
    std::wstring fn = path.substr(path.find_last_of(L"\\/") + 1);
    std::wstring title = fn;
    size_t dp = fn.find_last_of(L".");
    if (dp != std::wstring::npos) title = fn.substr(0, dp);

    std::string sKind = kind;
    std::string sTitle = net::ToUtf8(title);
    std::string sFilename = net::ToUtf8(fn);

    Cloud::RunAsync([this, sKind, sTitle, sFilename, bin] {
        std::wstring err;
        Cloud::Instance().UploadMedia(sKind, sTitle, "", sFilename, bin, err);
        FetchCloudMedia();
    });
}

} // namespace lj
