#pragma once
// ============================================================
//  KnowledgeView.h — F-D5 知识库（跨窗口拖拽收集的考点）
//  从专注浮窗或直接拖进软件窗口的文字 / .md/.txt 文件，自动建卡
//  进入个人知识库（落盘 accounts/<name>/knowledge.json）；本视图负责
//  查看 / 展开 / 删除。数据不出本机，符合隐私红线。
//  F-D5+：主窗口注册 OLE 放目标（core/KnowledgeDrop），拖拽悬停时
//  本页画高亮蒙层，收集成功经 KnowledgeRev() 轮询即时刷新。
// ============================================================
#include "ui/View.h"
#include "app/Store.h"
#include "ui/Layout.h"
#include "ui/MarkdownView.h"
#include "ui/FieldEdit.h"
#include <vector>

namespace lj {

class KnowledgeView : public View
{
public:
    const wchar_t* Id() const override { return L"knowledge"; }
    const wchar_t* Title() const override { return L"知 识 库"; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;
    void DebugForcePreview() override;   // 截图自检：造 md 示例卡并展开，验证阅读视图

private:
    void Reload();
    void RecomputeLayout();
    void SyncArrays();   // 并行数组（expanded/mdMode/md）与 m_cards 对齐，防越界
    void PaintCard(Canvas& cv, const KCard& c, bool expanded, const D2D1_RECT_F& r, int idx);
    void PaintButton(Canvas& cv, const D2D1_RECT_F& r, const std::wstring& label,
                     const D2D1_COLOR_F& bg, const D2D1_COLOR_F& fg);
    void Toast(const std::wstring& msg);
    void DrawToast(Canvas& cv);

    std::vector<KCard>       m_cards;
    std::vector<bool>        m_expanded;
    std::vector<D2D1_RECT_F> m_cardRects;
    std::vector<D2D1_RECT_F> m_delRects;
    std::vector<D2D1_RECT_F> m_expandRects;

    // Markdown 阅读视图：展开态按 Obsidian 阅读视图渲染，可切回源码
    std::vector<lj::MarkdownView> m_md;
    std::vector<bool>        m_mdMode;         // true = 阅读视图（默认）
    std::vector<D2D1_RECT_F> m_mdToggleRects;

    // 批次 E：标题重命名（展开态「✎ 改名」→ 标题行原地编辑）
    FieldEdit                m_ren;
    std::vector<D2D1_RECT_F> m_renRects;
    bool  m_renActive = false;
    int   m_renIdx = -1;
    D2D1_RECT_F RenBox(int idx) const;
    void CommitRename();
    static std::wstring DispTitle(const KCard& c);   // 标题空 → 取正文首行（#/普通首行）

    Canvas*     m_cv = nullptr;   // 供展开/删除后即时重排（不依赖下一帧 Layout）

    bool        m_toast = false;
    float       m_toastT = 0.0f;
    std::wstring m_toastMsg;

    long long   m_lastRev = 0;    // KnowledgeDrop 收集版本号（变化 → 刷新+Toast）
    bool        m_dragActive = false;  // 拖拽悬停中 → 画高亮蒙层

    static bool         InRect(const D2D1_RECT_F& r, float x, float y);
    static std::wstring FmtTime(long long ts);
};

} // namespace lj
