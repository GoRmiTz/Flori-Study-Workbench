#pragma once
// ============================================================
//  RoadmapView.h — 专栏（No.04）
//  每账户私有、可编辑的长文档区（对应网页端「ns_columns」）。
//  · 每个账户在 accounts/<name>/columns.json 维护自己的专栏列表，不跨用户共享；
//  · 存在个人种子时账户首次进入播种「我的总线路图」一条，默认无种子则从空白开始；
//  · 所有账户（含演示账户）均可新建 / 编辑 / 删除自己的专栏；
//  · 编辑弹层用 Win32 EDIT 控件承载中文 IME（标题单行 + 正文多行）。
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "ui/FieldEdit.h"
#include "ui/MarkdownView.h"
#include "app/Data.h"
#include "app/Store.h"
#include "app/Cloud.h"       // §4 公共专栏 REST
#include "net/Realtime.h"    // §4 col:changed 实时事件
#include "app/Json.h"        // §4 响应解析
#include <windows.h>
#include <richedit.h>       // CHARFORMAT2W / PARAFORMAT2 / PFN_* / SF_SELECTION 等 RichEdit 常量
#include <vector>
#include <mutex>
#include <atomic>

namespace lj {

class RoadmapView : public View
{
public:
    const wchar_t* Id() const override { return L"roadmap"; }
    const wchar_t* Title() const override { return L"专 栏"; }

    void OnEnter() override;
    void OnLeave() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    void PaintTitle(Canvas& cv, float x0, float y, float w);
    void PaintList(Canvas& cv, float x0, float y, float w);
    void PaintEmpty(Canvas& cv, float x0, float y, float w);
    void PaintEditorOverlay(Canvas& cv);
    void PaintPreviewOverlay(Canvas& cv);

    void OpenPreview(const std::wstring& id);    // 只读预览（点卡片进入）
    void ClosePreview();
    void ComputePreviewRects();

    // ---- §4 公共专栏详情弹层（列表 → 详情 → 点赞/收藏/评论）----
    struct NetPost {
        std::string  id;
        std::wstring title, excerpt, author, sectionId;
        long long createdAt = 0, updatedAt = 0;
        int  likeCount = 0, commentCount = 0, favCount = 0;
        bool liked = false, mine = false, faved = false;
    };
    struct NetComment { std::wstring user, text; long long ts = 0; };
    struct PubDetail {
        NetPost post;
        std::wstring body;
        std::vector<NetComment> comments;
        bool loaded = false;
    };
    void OpenPubPreview(const std::string& id);
    void ClosePubPreview();
    void ComputePubPreviewRects();
    void PaintPubPreviewOverlay(Canvas& cv);
    void LoadPubDetail(const std::string& id);   // 后台 GET /columns/:id
    void SubmitPubComment();                     // POST /columns/:id/comment
    void OnPubDetail(const net::Response& r);    // UI 线程消费后台结果

    void LoadColumns();                 // 从 CheckinStore 取当前账户专栏
    void RelayoutList();                // 仅依 m_cols 重算卡片矩形（编辑后无需 Canvas）
    void EnsureEditors();
    void DestroyEditors();
    void OpenEditor(const std::wstring& id);   // 空 id = 新建
    void CloseEditor(bool save);
    void DeleteColumn(const std::wstring& id);
    void ComputeEditorRects();
    std::wstring FormatTs(long long ts) const;
    std::wstring GenId() const;
    void DebugForceOpen() override;
    void DebugForcePreview() override;

    // 列表状态
    std::vector<Column> m_cols;
    std::vector<D2D1_RECT_F> m_cardRects;
    std::vector<D2D1_RECT_F> m_editBtns;   // 悬浮「编辑」按钮
    std::vector<D2D1_RECT_F> m_delBtns;    // 悬浮「删除」按钮
    std::vector<D2D1_RECT_F> m_viewBtns;   // 悬浮「查看」按钮
    std::vector<D2D1_RECT_F> m_favBtns;    // 悬浮「☆收藏 / ★已收藏」按钮
    std::vector<Favorite>    m_favs;       // 当前账户收藏（判定 ★，个人界面「我的」共用）
    int m_hoverCard = -1;
    bool IsColFav(const std::wstring& id) const;

    // 私有专栏预览弹层（只读，点卡片进入；正文 Markdown 阅读视图）
    bool      m_previewing = false;
    std::wstring m_previewId;
    float     m_previewAnim = 0.0f;
    float     m_previewScroll = 0.0f;
    D2D1_RECT_F m_pvCard{}, m_pvBody{}, m_pvClose{}, m_pvEdit{}, m_pvDelete{};
    float     m_pvContentH = 0.0f;       // 正文实测高度（用于滚轮裁剪）
    MarkdownView m_pvMd;                 // 正文渲染器（SetMarkdown + Layout 按宽缓存）

    // 公共专栏详情弹层
    bool      m_pubPreviewing = false;
    std::string m_pubPreviewId;
    float     m_pubPreviewAnim = 0.0f;
    float     m_pubPreviewScroll = 0.0f;
    D2D1_RECT_F m_pubPvCard{}, m_pubPvBody{}, m_pubPvClose{};
    D2D1_RECT_F m_pubPvLike{}, m_pubPvFav{}, m_pubPvSend{};
    D2D1_RECT_F m_pubPvInput{};
    float     m_pubPvContentH = 0.0f;
    PubDetail m_pubDetail;
    // 详情异步加载结果（后台写 + UI 消费）
    std::mutex  m_pubDetailMu;
    net::Response m_pubDetailResp{};
    std::atomic<bool> m_pubDetailDirty{ false };
    // 评论输入（v2 统一输入框，单行；文字/光标/IME 由 D3D 自绘）
    FieldEdit m_pubComment;
    bool      m_pubCommentEdit = false;
    std::wstring m_pubCommentDraft;
    void BeginPubCommentEdit();
    void EndPubCommentEdit(bool submit);

    Button m_newBtn;
    Button m_backBtn;
    std::vector<Widget*> m_widgets;

    // 编辑器（正文 RichEdit 承载富文本；标题为 v2 统一输入框）
    bool      m_editing = false;
    std::wstring m_editId;                // 正在编辑的专栏 id；空 = 新建
    HWND      m_edBody = nullptr;         // 正文（多行 RichEdit）
    HFONT     m_edFont = nullptr;
    FieldEdit m_edTitle;                  // 标题（v2 统一输入框）
    std::wstring m_edTitleBuf;            // 标题缓冲（焦点在正文/标题间切换时保持）
    bool      m_titleActive = false;      // 标题编辑会话进行中
    Canvas*   m_cvCached = nullptr;
    void CommitTitleEdit();               // 标题失焦/回车时把缓冲落回 m_edTitleBuf
    float     m_editAnim = 0.0f;          // 弹层淡入淡出 0..1

    D2D1_RECT_F m_edCard{}, m_edTitleBox{}, m_edBodyBox{};
    D2D1_RECT_F m_edSave{}, m_edCancel{}, m_edDelete{}, m_edPublish{};
    void DoPublish();                       // 保存本地 + POST /columns 发布到公共区

    // 格式工具栏（D2D 绘制，作用于 m_edBody 的 RichEdit 选区 / 插入点）
    // 第一行：撤回 / 前进 / 格式刷 / 清除 / 标题123 / 加粗 / 斜体 / 下划线 / 删除线 / 字号小中中
    D2D1_RECT_F m_edUndo{}, m_edRedo{};
    D2D1_RECT_F m_edBrush{}, m_edClear{};
    D2D1_RECT_F m_edH1{}, m_edH2{}, m_edH3{};
    D2D1_RECT_F m_edBold{}, m_edItalic{}, m_edUnderline{}, m_edStrike{};
    D2D1_RECT_F m_edSizeS{}, m_edSizeM{}, m_edSizeL{};
    // 第二行：字体色 / 背景色 / 对齐(左中右) / 有序 / 无序 / 图片 / 链接 / 分割线
    D2D1_RECT_F m_edColor{}, m_edBg{};
    D2D1_RECT_F m_edAlignL{}, m_edAlignC{}, m_edAlignR{};
    D2D1_RECT_F m_edOList{}, m_edUList{};
    D2D1_RECT_F m_edImage{}, m_edLink{}, m_edHr{};
    // 颜色选择浮层（字体色 / 背景色）
    int   m_edColorMode = 0;                  // 0=关 1=字体色 2=背景色
    std::vector<D2D1_RECT_F> m_edSwatches;    // 浮层色板矩形
    D2D1_RECT_F m_edSwatchPanel{};            // 浮层面板矩形
    // 格式刷（两步：点一下取格式 → 再点一下把格式刷到当前选区）
    bool  m_fmtBrush = false;
    CHARFORMAT2W m_fmtBrushCf{};
    PARAFORMAT2  m_fmtBrushPf{};

    bool EdHasEffect(DWORD eff) const;                       // 当前选区/插入点是否含某效果
    void EdApplyFormat(DWORD mask, DWORD effects, LONG yHeight = 0);  // 对选区/插入点设字符格式
    void EdApplyPara(DWORD mask, WORD alignment = 0, WORD numbering = 0);  // 段落格式
    void EdUndo();                                           // 撤回
    void EdRedo();                                           // 前进
    void EdClearFormat();                                    // 清除格式
    void EdApplyHeading(int level);                          // 标题 1/2/3
    void EdSetColor(COLORREF c, bool bg);                    // 字体色 / 背景色
    void EdSetAlignment(WORD align);                         // 左/中/右对齐
    void EdToggleList(WORD kind);                            // 有序/无序列表
    void EdFormatBrush();                                    // 格式刷（取/刷）
    void EdInsertImage();                                    // 插入图片
    void EdInsertLink();                                     // 插入链接
    void EdInsertDivider();                                  // 插入分割线
    bool EdInsertRtf(const std::string& rtf);                // 以 SF_SELECTION 在插入点流式插入 RTF
    WORD EdGetAlign() const;                                 // 当前段落对齐
    WORD EdGetNumbering() const;                             // 当前列表类型

    // 布局锚点
    float m_contentTop = 0;
    float m_listY = 0;
    float m_x0 = 0;
    float m_contentW = 0;
    D2D1_RECT_F m_emptyRect{};
    float m_t = 0.0f;

    // ---- §4 公共专栏（服务端）----
    struct NetSection { std::string id; std::wstring name, desc; };

    std::mutex  m_cloudMu;
    std::atomic<bool> m_cloudDirty{ false };

    std::vector<NetPost>    m_srcPosts;
    std::vector<NetSection> m_srcSections;
    bool m_srcFetched = false;

    // UI 快照
    std::vector<NetPost>    m_pubPosts;
    std::vector<NetSection> m_pubSections;
    bool m_pubLive = false;

    // 公共专栏布局
    float m_pubTitleY = 0.0f;
    float m_pubListY  = 0.0f;
    std::vector<D2D1_RECT_F> m_pubCardRects;
    std::vector<D2D1_RECT_F> m_pubLikeBtns;
    int m_hoverPub = -1;
    // 发布结果提示（后台线程置原子码，UI 线程消费后显示）
    std::atomic<int>  m_pubResult{ 0 };    // 0=无 1=发布成功 2=发布失败
    std::wstring      m_pubHint;
    long long         m_pubHintUntil = 0;  // 提示过期时刻（epoch 秒）

    void FetchPublicColumns();                 // 后台拉取 GET /columns
    void OnColChanged(const lj::json::JVal&);  // col:changed → 重拉
    void SnapshotCloud();                       // 来源 → 快照
};

} // namespace lj
