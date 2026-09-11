#pragma once
// ============================================================
//  ReviewView.h — F-D7 本地复盘搭子（规则版）入口视图
//  展示今日复盘结论（完成率 / 专注 / 中断 / 薄弱项 / 一句提示），
//  并提供「写入明日三要事」「重新生成」。数据全部来自 ReviewEngine，
//  不外发、不联网。
// ============================================================
#include "ui/View.h"
#include "core/ReviewEngine.h"
#include <vector>

namespace lj {

class ReviewView : public View
{
public:
    const wchar_t* Id() const override { return L"review"; }
    const wchar_t* Title() const override { return L"复  盘"; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    void Regenerate();
    void Toast(const std::wstring& msg);
    void DrawToast(Canvas& cv);
    void LoadNudge();    // 从 settings.json 读取提醒开关与时刻
    void SaveNudge();    // 写回 settings.json（ReviewNudge 每次自检实时读取，无需重启生效）

    DayReview m_r;
    Canvas*     m_cv = nullptr;   // 供按钮操作后即时重排（不依赖下一帧 Layout）
    D2D1_RECT_F m_area  = { 0, 0, 0, 0 };
    D2D1_RECT_F m_card  = { 0, 0, 0, 0 };
    D2D1_RECT_F m_commitBtn = { 0, 0, 0, 0 };  // 写入明日三要事
    D2D1_RECT_F m_refreshBtn = { 0, 0, 0, 0 }; // 重新生成

    // ---- 复盘提醒设置（F-D7 增强：从 settings.json 起开关 UI）----
    D2D1_RECT_F m_setCard   = { 0, 0, 0, 0 };   // 提醒设置卡
    D2D1_RECT_F m_toggleRow = { 0, 0, 0, 0 };   // 整行点击切换开关
    D2D1_RECT_F m_minusBtn  = { 0, 0, 0, 0 };   // 时刻 −
    D2D1_RECT_F m_plusBtn   = { 0, 0, 0, 0 };   // 时刻 ＋
    D2D1_RECT_F m_hourRect  = { 0, 0, 0, 0 };   // 时刻文本
    bool        m_nudgeOn   = true;             // 镜像 AppSettings.reviewNudge
    int         m_nudgeHour = 21;               // 镜像 AppSettings.reviewNudgeHour
    bool        m_hoverMinus = false;
    bool        m_hoverPlus  = false;

    bool        m_toast = false;
    float       m_toastT = 0.0f;
    std::wstring m_toastMsg;

    static bool InRect(const D2D1_RECT_F& r, float x, float y);
};

} // namespace lj
