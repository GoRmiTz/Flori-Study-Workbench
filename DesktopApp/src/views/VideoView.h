#pragma once
// ============================================================
//  VideoView.h — 视频 / 图片（No.07，公共模块）
//  对应 WebApp vVideo()：独立的公共内容广场（类视频网站），
//  内容来自「用户上传 → 服务端」，所有账户共享可见。
//
//  与「资源库（MediaView, No.06）」的职责区分（用户口径）：
//   · 本页 = 公共的视频 / 图片站，内容取自服务端 /media/list 的 videos + images，
//     卡片可播放（调起系统默认程序打开 URL）、收藏（本地 favorites.json）、删除（仅自己上传的）。
//   · 资源库 = 本地音乐 / 课程视频文件夹，服务于自习室音频播放与本地素材归档。
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "app/Data.h"
#include "app/Store.h"
#include "app/Cloud.h"
#include "app/Json.h"
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

namespace lj {

class VideoView : public View
{
public:
    const wchar_t* Id() const override { return L"video"; }
    const wchar_t* Title() const override { return L"视频 / 图片"; }

    void OnEnter() override;
    void OnLeave() override;    // 关闭软件内图片查看器（避免切走残留遮罩导致页面变黑）
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    struct Feed {
        std::string  id;          // med_xxx
        std::string  kind;        // "videos" | "images"
        std::wstring title;
        std::wstring note;
        std::wstring user;
        long long    size = 0;
        long long    ts = 0;
        bool         mine = false;
    };

    void PaintTitle(Canvas& cv, float x0, float y, float w);
    void PaintTabs(Canvas& cv);
    void PaintGrid(Canvas& cv);

    void FetchFeed();          // 后台 GET /media/list
    void SnapshotFeed();       // 来源 → UI 快照
    void DoUpload();           // 选文件并上传（videos / images）
    void Play(const Feed& f);  // 调起默认程序打开 URL，并记历史
    std::wstring UrlOf(const Feed& f) const;
    std::vector<const Feed*> Visible() const;   // 按当前 tab 过滤

    // ---- 数据 ----
    std::mutex        m_mu;
    std::atomic<bool> m_dirty{ false };
    std::vector<Feed> m_src;
    bool m_srcCanUpload = false;
    bool m_srcFetched = false;

    std::vector<Feed> m_feed;      // UI 快照
    bool m_canUpload = false;
    bool m_live = false;
    std::vector<Favorite> m_favs;  // 本地收藏（判定 ★）

    // ---- 布局 ----
    int   m_tab = 0;               // 0 视频 / 1 图片
    float m_contentTop = 0.0f;
    float m_tabY = 0.0f;
    float m_gridY = 0.0f;
    D2D1_RECT_F m_tabRects[2]{};

    struct CardHit {
        D2D1_RECT_F card{};
        D2D1_RECT_F thumb{};
        D2D1_RECT_F playBtn{};
        D2D1_RECT_F favBtn{};
        D2D1_RECT_F delBtn{};
        int index = -1;            // 指向 m_feed 的下标
    };
    std::vector<CardHit> m_cards;
    int m_hover = -1;

    Button m_uploadBtn;
    Button m_backBtn;
    std::vector<Widget*> m_widgets;

    float m_t = 0.0f;
    std::wstring m_toast;
    float m_toastT = 0.0f;
};

} // namespace lj
