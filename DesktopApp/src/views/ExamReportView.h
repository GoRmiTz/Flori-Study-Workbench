#pragma once
// ============================================================
//  ExamReportView.h — F-D6 考场模式（模考报告 / 开始入口）
//  展示上次模考报告，并提供「开始模考」入口（默认 120 分钟行测模考）。
//  进入后由 ExamMode 接管全屏 HUD；结束自动回到本页刷新报告。
// ============================================================
#include "ui/View.h"
#include "app/Store.h"
#include "ui/Layout.h"
#include <vector>

namespace lj {

class ExamReportView : public View
{
public:
    const wchar_t* Id() const override { return L"exam"; }
    const wchar_t* Title() const override { return L"考 场 模 考"; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    void Reload();
    void RecomputeLayout();
    void PaintReport(Canvas& cv, const ExamReport& r, const D2D1_RECT_F& box);
    void PaintButton(Canvas& cv, const D2D1_RECT_F& r, const std::wstring& label,
                    const D2D1_COLOR_F& bg, const D2D1_COLOR_F& fg);
    void Toast(const std::wstring& m);
    void DrawToast(Canvas& cv);

    ExamReport m_last{};
    bool       m_running = false;

    std::vector<int>         m_presets{ 60, 90, 120, 150 };  // 模考时长预设（分钟）
    std::vector<D2D1_RECT_F> m_btnRects;   // 与 presets 对应的开始按钮
    D2D1_RECT_F m_card1{}, m_card2{};      // 两张卡片的布局矩形
    D2D1_RECT_F m_stopRect{};              // 退出考场按钮（仅考试中可见）

    Canvas* m_cv = nullptr;
    bool    m_toast = false;
    float   m_toastT = 0.0f;
    std::wstring m_toastMsg;

    static bool         InRect(const D2D1_RECT_F& r, float x, float y);
    static std::wstring FmtTime(long long ts);
};

} // namespace lj
