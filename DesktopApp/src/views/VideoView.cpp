#include "views/VideoView.h"
#include "app/AccountStore.h"
#include "core/Hwnd.h"
#include "ui/ImageViewer.h"     // 云端图片软件内查看（带鉴权，非浏览器）
#include "ui/Layout.h"
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <algorithm>
#include <cmath>

namespace lj {

// ---------- 小工具 ----------
static std::wstring VvLowerExt(const std::wstring& path)
{
    size_t p = path.find_last_of(L".");
    if (p == std::wstring::npos) return L"";
    std::wstring e = path.substr(p + 1);
    std::transform(e.begin(), e.end(), e.begin(), ::towlower);
    return e;
}

static std::wstring VvFormatTs(long long ts)
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

static std::wstring VvFormatSize(long long b)
{
    wchar_t buf[32];
    if (b > 1024 * 1024) swprintf_s(buf, L"%.1f MB", (float)b / 1048576.0f);
    else if (b > 1024)   swprintf_s(buf, L"%.0f KB", (float)b / 1024.0f);
    else                 swprintf_s(buf, L"%lld B", b);
    return buf;
}

// ============================================================
void VideoView::OnEnter()
{
    View::OnEnter();
    m_t = 0.0f;
    m_hover = -1;
    m_toastT = 0.0f;
    m_srcFetched = false;
    m_live = false;
    m_favs = CheckinStore::Instance().LoadFavorites();
    FetchFeed();
}

void VideoView::OnLeave()
{
    View::OnLeave();
    // 图片查看器是全局单例：切走若不关闭，回来时全屏遮罩仍在 → 页面「变黑」
    ImageViewer::Instance().Close();
}

std::vector<const VideoView::Feed*> VideoView::Visible() const
{
    std::vector<const Feed*> out;
    const char* want = (m_tab == 0) ? "videos" : "images";
    for (const auto& f : m_feed) if (f.kind == want) out.push_back(&f);
    // 新的在前
    std::sort(out.begin(), out.end(), [](const Feed* a, const Feed* b) { return a->ts > b->ts; });
    return out;
}

std::wstring VideoView::UrlOf(const Feed& f) const
{
    net::Endpoint ep = Cloud::Instance().EndpointCfg();
    std::wstring scheme = ep.secure ? L"https://" : L"http://";
    return scheme + ep.host + L":" + std::to_wstring(ep.port) +
           L"/media/file/" + net::FromUtf8(f.id);
}

void VideoView::Play(const Feed& f)
{
    // 图片：软件内查看（带鉴权拉取字节 + WIC 解码），不再 ShellExecute 打开裸 URL（会 401）
    if (f.kind == "images") {
        ImageViewer::Instance().Open(f.id, f.title);
        CheckinStore::Instance().PushHistory(L"image", net::FromUtf8(f.id), f.title);
        return;
    }
    // 视频：暂用系统默认播放器（媒体 URL 鉴权播放属后续音频/视频引擎范围）
    std::wstring url = UrlOf(f);
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    CheckinStore::Instance().PushHistory(L"video", net::FromUtf8(f.id), f.title);
}

// ============================================================
//  布局
// ============================================================
void VideoView::Layout(const D2D1_RECT_F& area, Canvas& cv)
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

    // ---- Tab 条 ----
    m_tabY = flow.block(36.0f + 22.0f).top;
    {
        float tw = 128.0f, th = 36.0f;
        m_tabRects[0] = { x0, m_tabY, x0 + tw, m_tabY + th };
        m_tabRects[1] = { x0 + tw + 10.0f, m_tabY, x0 + tw * 2 + 10.0f, m_tabY + th };

        m_uploadBtn.label = L"↑ 上传";
        m_uploadBtn.fontSize = 13.0f;
        m_uploadBtn.bounds = { x0 + contentW - 130.0f, m_tabY, x0 + contentW, m_tabY + th + 8.0f };
        m_uploadBtn.onClick = [this] { DoUpload(); };
    }

    // ---- 卡片网格 ----
    m_cards.clear();
    auto vis = Visible();
    m_gridY = flow.cursorY;   // 空态分支也需要该锚点（绘制空提示用）

    if (vis.empty()) {
        flow.block(74.0f);
    } else {
        int cols = (contentW >= 940.0f) ? 3 : (contentW >= 620.0f) ? 2 : 1;
        float gap = 16.0f;
        float cw = (contentW - gap * (cols - 1)) / (float)cols;
        float thumbH = cw * 0.5625f;             // 16:9
        if (thumbH > 190.0f) thumbH = 190.0f;
        float infoH = 96.0f;
        float ch = thumbH + infoH;
        int rows = ((int)vis.size() + cols - 1) / cols;
        flow.block((float)rows * (ch + gap));
        for (size_t i = 0; i < vis.size(); ++i) {
            int r = (int)i / cols, c = (int)i % cols;
            float cx = x0 + c * (cw + gap);
            float cy = m_gridY + r * (ch + gap);
            CardHit h;
            h.card = { cx, cy, cx + cw, cy + ch };
            h.thumb = { cx, cy, cx + cw, cy + thumbH };
            float ay = h.card.bottom - 34.0f;
            h.playBtn = { cx + 14.0f, ay, cx + 14.0f + 66.0f, ay + 24.0f };
            h.favBtn = { cx + 88.0f, ay, cx + 88.0f + 80.0f, ay + 24.0f };
            h.delBtn = { h.card.right - 62.0f, ay, h.card.right - 14.0f, ay + 24.0f };
            // 反查在 m_feed 中的下标
            h.index = -1;
            for (size_t k = 0; k < m_feed.size(); ++k) if (&m_feed[k] == vis[i]) { h.index = (int)k; break; }
            m_cards.push_back(h);
        }
    }
    flow.block(12.0f);

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
    if (m_canUpload) m_widgets.push_back(&m_uploadBtn);
}

// ============================================================
//  更新
// ============================================================
void VideoView::Update(float dt, const Input& in)
{
    View::Update(dt, in);
    m_t += dt;
    if (m_toastT > 0.0f) m_toastT -= dt;

    if (m_dirty.exchange(false)) SnapshotFeed();

    Input shifted = in;
    shifted.mouseY = in.mouseY + ScrollY();
    UpdateWidgets(m_widgets, dt, shifted);

    float mx = in.mouseX, my = shifted.mouseY;
    auto hit = [&](const D2D1_RECT_F& r) {
        return mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom;
    };

    // Tab 切换
    for (int i = 0; i < 2; ++i) {
        if (hit(m_tabRects[i])) {
            m_overInteractive = true;
            if (in.clicked && m_tab != i) { m_tab = i; SetScroll(0.0f); }
        }
    }

    // 卡片交互
    m_hover = -1;
    for (size_t i = 0; i < m_cards.size(); ++i) {
        const auto& h = m_cards[i];
        if (!hit(h.card)) continue;
        m_hover = (int)i;
        m_overInteractive = true;
        if (!in.clicked) break;
        if (h.index < 0 || h.index >= (int)m_feed.size()) break;
        const Feed& f = m_feed[h.index];

        if (hit(h.favBtn)) {
            CheckinStore::Instance().ToggleFav(f.kind == "videos" ? L"video" : L"image",
                                               net::FromUtf8(f.id), f.title, f.user);
            m_favs = CheckinStore::Instance().LoadFavorites();
            m_toast = L"收藏已更新";
            m_toastT = 1.8f;
        } else if (f.mine && hit(h.delBtn)) {
            std::string mid = f.id;
            Cloud::RunAsync([this, mid] {
                Cloud::Instance().DeleteMedia(mid);
                FetchFeed();
            });
            m_toast = L"已请求删除";
            m_toastT = 1.8f;
        } else {
            Play(f);
        }
        break;
    }

    // 软件内图片查看浮层（未打开时零开销）
    ImageViewer::Instance().Update(dt, in);
}

// ============================================================
//  绘制
// ============================================================
void VideoView::Paint(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;
    float s = ScrollY();

    cv.PushClip(m_area);
    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, -s));

    PaintTitle(cv, x0, m_contentTop, contentW);
    PaintTabs(cv);
    PaintGrid(cv);

    m_backBtn.Paint(cv);

    cv.PopTransform();
    cv.PopClip();

    // 滚动条
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

    // Toast
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

    // 软件内图片查看浮层（最上层）
    ImageViewer::Instance().Paint(cv, m_area);
}

void VideoView::PaintTitle(Canvas& cv, float x0, float y, float contentW)
{
    const auto& pal = cv.Pal();
    TextStyle sec; sec.role = FontRole::Mono; sec.size = 10.5f;
    sec.letterSpacing = 2.4f; sec.weight = DWRITE_FONT_WEIGHT_BOLD;
    float ha = Clamp01(m_t / 0.5f);
    cv.PushOpacity(ha);
    cv.Text(m_live ? L"SECTION · 公共内容广场 · 已连线" : L"SECTION · 公共内容广场 · 离线",
            { x0, y, x0 + 460.0f, y + 16.0f }, sec, m_live ? pal.jade : pal.ink300);
    cv.PopOpacity();

    TextStyle h1; h1.role = FontRole::Serif; h1.size = 38.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 3.0f;
    cv.CharsReveal(L"视频 · 图片", x0, y + 24.0f, h1, pal.ink900, m_t * 1.15f, 0.05f, 16.0f);

    float da = Clamp01((m_t - 0.25f) / 0.6f);
    if (da > 0.0f) {
        TextStyle ds; ds.role = FontRole::Mono; ds.size = 11.0f; ds.letterSpacing = 1.6f;
        ds.hAlign = HAlign::Right;
        cv.PushOpacity(ease::OutCubic(da));
        cv.Text(L"PUBLIC · 用户上传 · 全员可见",
                { x0 + contentW - 360.0f, y + 2.0f, x0 + contentW, y + 20.0f }, ds, pal.ink500);
        cv.PopOpacity();
    }
    cv.PerforationH(x0, x0 + contentW, y + 78.0f, WithAlpha(pal.ruleStrong, 0.6f * ha));
}

void VideoView::PaintTabs(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.25f) / 0.5f);
    if (appear <= 0.004f) return;
    cv.PushOpacity(ease::OutCubic(appear));

    int nVid = 0, nImg = 0;
    for (const auto& f : m_feed) { if (f.kind == "videos") nVid++; else if (f.kind == "images") nImg++; }

    const wchar_t* names[2] = { L"视频", L"图片" };
    int counts[2] = { nVid, nImg };
    for (int i = 0; i < 2; ++i) {
        bool on = (m_tab == i);
        const auto& r = m_tabRects[i];
        cv.FillRoundRect(r, shape::kEdge, on ? WithAlpha(pal.seal, 0.14f)
                                             : WithAlpha(pal.sealWash, pal.dark ? 0.16f : 0.22f));
        cv.StrokeRoundRect(r, shape::kEdge, WithAlpha(on ? pal.seal : pal.rule, on ? 0.9f : 0.5f),
                           on ? shape::kStroke : shape::kHair);
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 13.5f;
        ts.weight = on ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        ts.hAlign = HAlign::Center; ts.vAlign = VAlign::Middle;
        cv.Text(std::wstring(names[i]) + L"（" + std::to_wstring(counts[i]) + L"）",
                r, ts, on ? pal.seal : pal.ink500);
    }

    if (m_canUpload) m_uploadBtn.Paint(cv);
    cv.PopOpacity();
}

void VideoView::PaintGrid(Canvas& cv)
{
    const auto& pal = cv.Pal();
    float appear = Clamp01((m_t - 0.35f) / 0.55f);
    if (appear <= 0.004f) return;
    float e = ease::OutCubic(appear);

    float availW = m_area.right - m_area.left;
    float contentW = (std::min)(shape::kMaxWidth, availW - 72.0f);
    float x0 = m_area.left + (availW - contentW) * 0.5f;

    cv.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, (1.0f - e) * 12.0f));
    cv.PushOpacity(e);

    if (m_cards.empty()) {
        D2D1_RECT_F r{ x0, m_gridY, x0 + contentW, m_gridY + 64.0f };
        cv.FillRoundRect(r, shape::kEdge, WithAlpha(pal.sealWash, pal.dark ? 0.16f : 0.22f));
        cv.StrokeRoundRect(r, shape::kEdge, WithAlpha(pal.rule, 0.5f), shape::kHair);
        TextStyle es; es.role = FontRole::Sans; es.size = 12.5f; es.vAlign = VAlign::Middle;
        std::wstring msg;
        if (!m_live) msg = L"未连上服务端 —— 公共视频 / 图片来自云端，联网后自动显示。启动 Server 并在「设置」里配好地址即可。";
        else if (m_tab == 0) msg = m_canUpload ? L"还没有视频。点右上「↑ 上传」发布第一条吧。"
                                               : L"还没有视频。上传需要管理员白名单授权。";
        else msg = m_canUpload ? L"还没有图片。点右上「↑ 上传」发布第一张吧。"
                               : L"还没有图片。上传需要管理员白名单授权。";
        cv.Text(msg, { r.left + 18.0f, r.top, r.right - 18.0f, r.bottom }, es, pal.ink500);
        cv.PopOpacity();
        cv.PopTransform();
        return;
    }

    for (size_t i = 0; i < m_cards.size(); ++i) {
        const auto& h = m_cards[i];
        if (h.index < 0 || h.index >= (int)m_feed.size()) continue;
        const Feed& f = m_feed[h.index];
        bool hov = (m_hover == (int)i);
        bool isVid = (f.kind == "videos");
        D2D1_COLOR_F ac = isVid ? pal.seal : pal.jade;

        float ca = Clamp01((m_t - 0.42f - (float)i * 0.035f) / 0.5f);
        if (ca <= 0.004f) continue;
        cv.PushOpacity(ease::OutCubic(ca));

        // 卡片底
        cv.PaperCard(h.card, hov ? 0.5f : 0.15f, shape::kEdge);
        cv.StrokeRoundRect(h.card, shape::kEdge, WithAlpha(hov ? ac : pal.rule, hov ? 0.85f : 0.45f),
                           hov ? shape::kStroke : shape::kHair);

        // 缩略图占位（首字 + 类型底色；桌面端未内嵌解码器，用版式化占位）
        cv.FillRoundRect(h.thumb, shape::kEdge, WithAlpha(ac, pal.dark ? 0.20f : 0.14f));
        {
            TextStyle bs; bs.role = FontRole::Serif; bs.size = 44.0f;
            bs.weight = DWRITE_FONT_WEIGHT_BLACK; bs.letterSpacing = 4.0f;
            bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle;
            std::wstring ini = f.title.empty() ? L"?" : f.title.substr(0, (std::min)((size_t)2, f.title.size()));
            cv.Text(ini, h.thumb, bs, WithAlpha(ac, 0.55f));
        }
        // 类型徽标
        {
            D2D1_RECT_F kb{ h.thumb.left + 10.0f, h.thumb.top + 10.0f,
                            h.thumb.left + 58.0f, h.thumb.top + 32.0f };
            cv.FillRoundRect(kb, 3.0f, WithAlpha(ac, 0.85f));
            TextStyle ks; ks.role = FontRole::Mono; ks.size = 10.0f;
            ks.hAlign = HAlign::Center; ks.vAlign = VAlign::Middle;
            ks.weight = DWRITE_FONT_WEIGHT_BOLD;
            cv.Text(isVid ? L"VIDEO" : L"IMAGE", kb, ks, pal.paperHi);
        }
        // 体积徽标（右下）
        {
            TextStyle zs; zs.role = FontRole::Mono; zs.size = 10.0f;
            zs.hAlign = HAlign::Right; zs.vAlign = VAlign::Middle;
            cv.Text(VvFormatSize(f.size),
                    { h.thumb.right - 100.0f, h.thumb.bottom - 26.0f,
                      h.thumb.right - 10.0f, h.thumb.bottom - 8.0f }, zs, WithAlpha(pal.ink700, 0.85f));
        }
        // 悬停播放提示
        if (hov) {
            TextStyle ps; ps.role = FontRole::Sans; ps.size = 12.5f;
            ps.weight = DWRITE_FONT_WEIGHT_BOLD;
            ps.hAlign = HAlign::Center; ps.vAlign = VAlign::Bottom;
            cv.Text(isVid ? L"点击播放 →" : L"点击查看 →",
                    { h.thumb.left, h.thumb.top, h.thumb.right, h.thumb.bottom - 8.0f }, ps, ac);
        }

        // 标题
        TextStyle ts; ts.role = FontRole::Sans; ts.size = 14.0f;
        ts.weight = DWRITE_FONT_WEIGHT_BOLD;
        cv.Text(f.title, { h.card.left + 14.0f, h.thumb.bottom + 10.0f,
                           h.card.right - 14.0f, h.thumb.bottom + 34.0f }, ts, pal.ink900);
        // 作者 · 时间
        TextStyle ms; ms.role = FontRole::Mono; ms.size = 10.5f;
        cv.Text(f.user + L"  ·  " + VvFormatTs(f.ts),
                { h.card.left + 14.0f, h.thumb.bottom + 36.0f,
                  h.card.right - 14.0f, h.thumb.bottom + 54.0f }, ms, pal.ink500);

        // 操作行
        auto pill = [&](const D2D1_RECT_F& r, const std::wstring& label,
                        const D2D1_COLOR_F& c, bool solid) {
            cv.FillRoundRect(r, 4.0f, WithAlpha(c, solid ? 0.18f : 0.10f));
            cv.StrokeRoundRect(r, 4.0f, WithAlpha(c, solid ? 0.9f : 0.5f), shape::kHair);
            TextStyle bs; bs.role = FontRole::Sans; bs.size = 11.0f;
            bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle;
            cv.Text(label, r, bs, c);
        };
        pill(h.playBtn, isVid ? L"▶ 播放" : L"◎ 查看", ac, true);

        bool faved = false;
        std::wstring wid = net::FromUtf8(f.id);
        for (const auto& fv : m_favs) if (fv.id == wid) { faved = true; break; }
        pill(h.favBtn, faved ? L"★ 已收藏" : L"☆ 收藏", faved ? pal.brass : pal.ink500, faved);

        if (f.mine) pill(h.delBtn, L"删除", pal.vermilion, false);

        cv.PopOpacity();
    }

    cv.PopOpacity();
    cv.PopTransform();
}

// ============================================================
//  云端：拉取 / 快照 / 上传
// ============================================================
void VideoView::FetchFeed()
{
    Cloud::RunAsync([this] {
        auto r = Cloud::Instance().GetMediaList();
        if (!r.Ok()) return;
        lj::json::Parser pp(r.body.data(), r.body.size());
        lj::json::JVal root = pp.parse();
        if (!root.IsObj()) return;

        std::vector<Feed> items;
        const auto* arr = lj::json::JGet(root, "items");
        if (arr && arr->IsArr()) {
            for (const auto& v : arr->arr) {
                Feed m;
                m.kind = lj::json::JStr(v, "kind");
                if (m.kind != "videos" && m.kind != "images") continue;   // 音乐留给资源库
                m.id = lj::json::JStr(v, "id");
                m.title = net::FromUtf8(lj::json::JStr(v, "title"));
                m.note = net::FromUtf8(lj::json::JStr(v, "note"));
                m.user = net::FromUtf8(lj::json::JStr(v, "user"));
                const auto* sz = lj::json::JGet(v, "size"); if (sz) m.size = (long long)sz->num;
                const auto* ts = lj::json::JGet(v, "ts");   if (ts) m.ts = (long long)ts->num;
                const auto* mn = lj::json::JGet(v, "mine"); if (mn) m.mine = mn->bval;
                items.push_back(std::move(m));
            }
        }
        bool canUpload = false;
        const auto* cu = lj::json::JGet(root, "canUpload");
        if (cu) canUpload = cu->bval;

        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_src = std::move(items);
            m_srcCanUpload = canUpload;
            m_srcFetched = true;
        }
        m_dirty.store(true);
    });
}

void VideoView::SnapshotFeed()
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_feed = m_src;
        m_canUpload = m_srcCanUpload;
        m_live = m_srcFetched && Cloud::Instance().Online();
    }
    m_favs = CheckinStore::Instance().LoadFavorites();
    // 数据变化后需要重排网格
    if (m_area.right > m_area.left) {
        // Layout 每帧由 App 调用，这里只需清掉悬停避免越界
        m_hover = -1;
    }
}

void VideoView::DoUpload()
{
    wchar_t szFile[MAX_PATH] = { 0 };
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = AppHwnd();
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"视频 / 图片\0*.mp4;*.mkv;*.avi;*.mov;*.webm;*.png;*.jpg;*.jpeg;*.gif;*.bmp;*.webp\0所有文件\0*.*\0";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return;

    std::wstring path = szFile;
    std::string bin;
    if (!lj::json::ReadFileRaw(path, bin)) { m_toast = L"读取文件失败"; m_toastT = 2.0f; return; }

    std::wstring ext = VvLowerExt(path);
    std::string kind;
    if (ext == L"mp4" || ext == L"mkv" || ext == L"avi" || ext == L"mov" ||
        ext == L"webm" || ext == L"wmv" || ext == L"flv" || ext == L"m4v") kind = "videos";
    else if (ext == L"png" || ext == L"jpg" || ext == L"jpeg" || ext == L"gif" ||
             ext == L"bmp" || ext == L"webp") kind = "images";
    else { m_toast = L"仅支持视频 / 图片格式"; m_toastT = 2.0f; return; }

    std::wstring fn = path.substr(path.find_last_of(L"\\/") + 1);
    std::wstring title = fn;
    size_t dp = fn.find_last_of(L".");
    if (dp != std::wstring::npos) title = fn.substr(0, dp);

    std::string sKind = kind;
    std::string sTitle = net::ToUtf8(title);
    std::string sFilename = net::ToUtf8(fn);

    m_toast = L"上传中…"; m_toastT = 2.5f;
    Cloud::RunAsync([this, sKind, sTitle, sFilename, bin] {
        std::wstring err;
        Cloud::Instance().UploadMedia(sKind, sTitle, "", sFilename, bin, err);
        FetchFeed();
    });
}

} // namespace lj
