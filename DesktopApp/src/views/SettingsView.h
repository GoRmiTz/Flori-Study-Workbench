#pragma once
// ============================================================
//  SettingsView.h — 全局设置页（route=settings）
//  由主窗口右上「用户名卡片」弹层中的「设置」项进入。
//  两分区：通用 / 练考（含 AI 出题凭据，落盘账户目录 ai.json，
//  本地独占、不参与云端同步）。
// ============================================================
#include "ui/View.h"
#include "ui/FieldText.h"
#include "ui/FieldEdit.h"
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
    int   sec = 0;               // 所属分区（0 通用 / 1 练考）
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
    TAG_FOCUS = 1, TAG_CAT,
    TAG_AIBASE, TAG_AIKEY, TAG_AIMODEL
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
    void ReadAI();

    void BeginEdit(SRow& r);
    void CommitEdit();
    void CancelEdit();

    void PaintToggle(Canvas& cv, const D2D1_RECT_F& r, bool on, const Palette& pal);
    void PaintStepper(Canvas& cv, SRow& r, const Palette& pal);
    void PaintStepBtn(Canvas& cv, SRow& r, const Palette& pal);
    void PaintStepBtn2(Canvas& cv, const D2D1_RECT_F& r, const wchar_t* sym, const Palette& pal);
    void PaintTextBox(Canvas& cv, SRow& r, const Palette& pal);

    static bool InRect(const D2D1_RECT_F& r, float x, float y)
    { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; }

    // 通用
    bool        m_dark = false, m_reviewNudge = true;
    int         m_reviewHour = 21;
    std::wstring m_focusApps;                  // 逗号分隔
    // AI 出题凭据（本地 ai.json，独占不进云）
    std::wstring m_aiBase, m_aiKey, m_aiModel;
    // 练考
    quiz::QuizSettings m_quiz;

    std::vector<SRow> m_rows;
    D2D1_RECT_F m_area{};
    D2D1_RECT_F m_backBtn{};
    D2D1_RECT_F m_secCards[2]{};
    D2D1_RECT_F m_header{};
    bool        m_caretOn = false;
    Canvas*     m_cv = nullptr;

    // v2 统一输入框（1×1 透明代理 + 全 D3D 自绘，无白块；逐行复用）
    FieldEdit m_edit;
    SRow*  m_active = nullptr;
    bool   m_editing = false;
    float  m_caretT = 0.0f;
};

} // namespace lj
