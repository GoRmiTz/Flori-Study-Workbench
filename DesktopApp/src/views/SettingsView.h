#pragma once
// ============================================================
//  SettingsView.h — 全局设置页（route=settings）
//  由主窗口右上「用户名卡片」弹层中的「设置」项进入。
//  三分区：通用 / 看板娘 / 练考。
//  看板娘凭据刻意不依赖未提交 Store.h 的 AppSettings.kanban 字段，
//  直接读 kanban_ai.json + settings.json 的 kanban 段（与 QuizGen 一致的解耦方式）。
// ============================================================
#include "ui/View.h"
#include "ui/FieldText.h"
#include "app/Store.h"
#include "quiz/QuizStore.h"
#include "quiz/QuizScheduler.h"
#include <windows.h>
#include <string>
#include <vector>

namespace lj {

// 一行设置控件
struct SRow
{
    enum T { Toggle, Stepper, Text } type = Toggle;
    std::wstring label;
    // toggle
    bool*       pBool = nullptr;
    std::wstring onText = L"开", offText = L"关";
    // stepper
    int*  pInt = nullptr;
    int   step = 1, minv = 0, maxv = 0;
    std::wstring unit;
    // text
    std::wstring* pStr = nullptr;
    bool  password = false;
    int   tag = 0;
    int   sec = 0;               // 所属分区（0 通用 / 1 看板娘 / 2 练考）
    // 布局（内容坐标，随滚动平移）
    D2D1_RECT_F rect{};          // 控件主框（标签命中区）
    D2D1_RECT_F dec{}, inc{};    // stepper 的 − / + 按钮框
    D2D1_RECT_F toggle{};        // 开关药丸框
    D2D1_RECT_F value{};         // stepper 数值框
    D2D1_RECT_F box{};           // 文本编辑框
};

struct SecInfo
{
    std::wstring title;
    int start = 0, count = 0;
    D2D1_RECT_F card{};
};

enum RowTag
{
    TAG_FOCUS = 1, TAG_APIBASE, TAG_APIKEY, TAG_MODEL,
    TAG_FROM, TAG_TO, TAG_BUDGET, TAG_CAT
};

// 看板娘配置本地副本（不使用 kanban::KanbanSettings，避免耦合未提交层）
struct KanbanCfg
{
    bool        enabled = true;
    int         activeFrom = 8 * 60;     // 分钟
    int         activeTo   = 23 * 60;
    int         dailyTokenBudget = 4000;
    std::wstring apiBase;
    std::wstring apiKey;
    std::wstring model = L"deepseek-chat";
    bool        personaCute = false;     // false=傲娇监督 true=天真可爱
};

class SettingsView : public View
{
public:
    SettingsView();
    const wchar_t* Id() const override { return L"settings"; }
    const wchar_t* Title() const override { return L"设置"; }

    void OnEnter() override;
    void OnLeave() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;
    void DebugForcePreview() override;

private:
    void Load();
    void Apply();
    void BuildRows();
    void ReadKanban();

    void EnsureEditor();
    void BeginEdit(SRow& r);
    void CommitEdit();
    void CancelEdit();
    static LRESULT CALLBACK EditProc(HWND w, UINT m, WPARAM wp, LPARAM lp);

    void PaintToggle(Canvas& cv, const D2D1_RECT_F& r, bool on, const Palette& pal);
    void PaintStepper(Canvas& cv, SRow& r, const Palette& pal);
    void PaintStepBtn(Canvas& cv, const D2D1_RECT_F& r, const wchar_t* sym, const Palette& pal);
    void PaintTextBox(Canvas& cv, SRow& r, const Palette& pal);

    static bool InRect(const D2D1_RECT_F& r, float x, float y)
    { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; }

    // 通用
    bool        m_dark = false, m_reviewNudge = true;
    int         m_reviewHour = 21;
    std::wstring m_focusApps;                  // 逗号分隔
    // 看板娘
    KanbanCfg   m_kb;
    std::wstring m_fromStr, m_toStr, m_budgetStr;  // 文本行显示值
    // 练考
    quiz::QuizSettings m_quiz;

    std::vector<SRow> m_rows;
    D2D1_RECT_F m_area{};
    D2D1_RECT_F m_backBtn{};
    D2D1_RECT_F m_secCards[3]{};
    D2D1_RECT_F m_header{};
    bool        m_caretOn = false;
    Canvas*     m_cv = nullptr;

    // 隐藏 EDIT 代理（复用，逐个字段编辑）
    HWND   m_edit = nullptr;
    WNDPROC m_editOld = nullptr;
    HFONT  m_editFont = nullptr;
    SRow*  m_active = nullptr;
    bool   m_editing = false;
    float  m_caretT = 0.0f;
};

} // namespace lj
