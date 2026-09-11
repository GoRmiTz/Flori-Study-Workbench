#pragma once
// ============================================================
//  MediaView.h — 资源库（No.06）
//  集中存放 音乐 / 课程视频 / 图片素材，云端与本地合并为一个模块：
//  三个分类（音乐 / 课程视频 / 图片素材）下，本地文件与云端资源混排，
//  每条卡片右侧标注来源徽标（本地 / 云端），云端条目可删除（仅自己的）。
//  本地来源：扫描 <exe>/assets/media（递归，按扩展名分三类），点击调起默认程序；
//  云端来源：§5 Cloud REST（GetMediaList / UploadMedia / DeleteMedia）。
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "app/Data.h"
#include "app/Store.h"
#include "app/Cloud.h"       // §5 媒体 REST
#include "app/Json.h"        // §5 响应解析 + ReadFileRaw
#include <vector>
#include <mutex>
#include <atomic>

namespace lj {

class MediaView : public View
{
public:
    const wchar_t* Id() const override { return L"media"; }
    const wchar_t* Title() const override { return L"资源库"; }

    void OnEnter() override;
    void OnLeave() override;    // 关闭软件内图片查看器（避免切走残留遮罩导致页面变黑）
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    void PaintTitle(Canvas& cv, float x0, float y, float contentW);
    void PaintNote(Canvas& cv, float x0, float y, float contentW);
    void PaintCats(Canvas& cv, float x0, float y, float contentW);
    void BuildMergedCats();   // 本地 + 云端快照 → m_cats（SnapshotCloud 后调用）

    struct MediaFile { std::wstring name; std::wstring path; std::wstring ext; };
    struct NetMedia {
        std::string  id, kind;
        std::wstring title, note, user;
        long long size = 0, ts = 0;
        bool mine = false;
    };
    enum class Src { Local, Cloud };
    struct UnifiedItem {
        Src src = Src::Local;
        MediaFile local;      // Src::Local 用
        NetMedia  net;        // Src::Cloud 用
    };
    struct UnifiedCat {
        std::wstring title;
        int accent = 0;       // 分类主题色（0 朱砂 1 金 2 碧）
        std::vector<UnifiedItem> items;
        std::vector<D2D1_RECT_F> cardRects;   // 与 items 平行
        std::vector<D2D1_RECT_F> delBtns;     // 云端 mine 项的删除按钮，与 items 平行
    };

    float m_contentTop = 0;
    float m_noteY = 0;
    float m_catY  = 0;
    D2D1_RECT_F m_noteRect{};

    // UI 快照：三个合并分类（本地 + 云端）
    std::vector<UnifiedCat> m_cats;
    std::vector<MediaFile>  m_local[3];   // OnEnter 扫描的本地文件（0 音乐 / 1 视频 / 2 图片）
    D2D1_RECT_F m_hoverRect{};

    Button m_backBtn;
    Button m_uploadBtn;
    std::vector<Widget*> m_widgets;
    float m_t = 0.0f;

    // ---- §5 云端（后台来源缓冲）----
    std::mutex  m_cloudMu;
    std::atomic<bool> m_cloudDirty{ false };
    std::vector<NetMedia> m_srcMedia;
    bool m_srcCanUpload = false;
    bool m_srcFetched = false;

    // UI 快照
    std::vector<NetMedia> m_pubMedia;
    bool m_pubCanUpload = false;
    bool m_pubLive = false;

    void FetchCloudMedia();                 // 后台拉取 GET /media/list
    void SnapshotCloud();                   // 来源 → 快照 + 重建合并分类
    void DoUpload(const std::wstring& path); // 读取 + 后台上传
};

} // namespace lj
