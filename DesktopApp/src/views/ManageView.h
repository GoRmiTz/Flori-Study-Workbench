#pragma once
// ============================================================
//  ManageView.h — 自定义打卡项管理页（#/manage）
//  移植 WebApp vManage() / bindInlineManage()：
//  daily / sat / sun 三组可编辑行，支持
//   · 文本字段就地编辑（时段/内容/时长/链接/文件夹，Win32 EDIT 承载，含中文 IME）
//   · 标签切换（点击循环）
//   · 增 / 删 / 上移 / 下移 排序
//   · 确定保存（落盘 archive/items.json）、返回打卡、恢复默认
//  另附本地数据管理：整账户「导出备份 / 导入备份」（单文件 JSON）。
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "ui/FieldEdit.h"
#include "app/Data.h"
#include "app/Store.h"
#include "core/Hwnd.h"
#include <windows.h>

namespace lj {

class ManageView : public View
{
public:
    const wchar_t* Id() const override { return L"manage"; }
    const wchar_t* Title() const override { return L"自定义打卡项"; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;
    void OnLeave() override;   // 离开页面时收起编辑器

private:
    enum F { F_TIME = 0, F_TITLE, F_DUR, F_LINK, F_FOLDER,
             F_MLABEL = 5, F_MDATE, F_MNOTE };   // 关键倒计时字段
    struct FieldHit { D2D1_RECT_F r; int g; int i; int f; };   // g:0 daily 1 sat 2 sun 3 milestones
    struct RowBtn   { D2D1_RECT_F r; int g; int i; int kind; }; // kind 0 up 1 down 2 del 3 tag · 4 urgent 5 ms-del
    struct RowGeom  { float top = 0, h = 0, y1 = 0, y2 = 0, y3 = 0; }; // 每行几何：Layout 计算后供 Paint 复用，根绝 Layout/Paint 不一致

    void CommitEdit();
    void CancelEdit();
    void BeginEdit(const FieldHit& fh);
    void DebugForceOpen() override;   // 截图自检：强制打开首个打卡项字段编辑
    void Save();
    void ReloadDefault();
    void DoExport();     // 导出当前账户全部档案为单文件 JSON
    void DoImport();     // 从备份文件恢复到当前账户
    void Toast(const std::wstring& msg, bool bad);

    ChecklistBundle m_bundle;
    std::vector<Milestone> m_milestones;   // 关键倒计时（g=3 分组）
    bool m_changed = false;
    bool m_built = false;

    std::vector<FieldHit> m_fields;
    std::vector<RowBtn>   m_rowBtn;
    std::vector<D2D1_RECT_F> m_groupAdd;   // 3 个「+ 添加一项」
    D2D1_RECT_F m_msCard{};                 // 关键倒计时卡片外框
    D2D1_RECT_F m_msAdd{};                  // 「+ 添加倒计时」
    D2D1_RECT_F m_btnOk{}, m_btnBack{}, m_btnReset{};
    D2D1_RECT_F m_btnExport{}, m_btnImport{};
    std::wstring m_toast;                  // 导出 / 导入结果提示
    bool  m_toastBad = false;
    float m_toastT = 0.0f;                 // 剩余显示秒数
    D2D1_RECT_F m_hotRect{};               // 当前悬停的可点元素（绘制高亮）
    std::vector<D2D1_RECT_F> m_groupRects; // 三组卡片外框（绘制）
    std::vector<RowGeom> m_rowGeom[3];      // daily/sat/sun 每组每行的几何（Paint 复用，避免错位）
    std::vector<RowGeom> m_msGeom;          // 关键倒计时每行的几何

    // 编辑器（v2 统一输入框：1×1 透明代理 + 全 D3D 自绘，无白块）
    FieldEdit m_edit;
    FieldHit m_editing{};
    bool   m_editingOn = false;

    float m_t = 0.0f;
    float m_contentTop = 0.0f;

    // 数据变化后用于就地重排
    D2D1_RECT_F m_areaCached{};
    Canvas* m_cvCached = nullptr;
};

} // namespace lj
