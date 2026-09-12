#pragma once
// ============================================================
//  QuizBoxView.h — 题集卡片盒（批次 G，需求 8）
//  以「卡片 + 卡片盒」为灵感的题集收集页：
//   · 盒架：自定义题盒（正常题盒 / 错题盒），可新建 / 改名 / 删除
//   · 盒内：卡片列表（题面摘要 + 标签），可加卡 / 删卡
//   · 抽卡：随机抽一张 → 正面题面 → 翻面看答案 → 下一张 / 放回
//  数据落 accounts/<账户>/quiz_boxes.json（quiz/BoxStore）。
// ============================================================
#include "ui/View.h"
#include "ui/FieldEdit.h"
#include "quiz/BoxStore.h"
#include <vector>

namespace lj {

class QuizBoxView : public View
{
public:
    const wchar_t* Id() const override { return L"quizbox"; }
    const wchar_t* Title() const override { return L"题 集 卡 片 盒"; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;
    void DebugForcePreview() override;   // 截图自检：空库造示例盒

private:
    enum V { V_SHELF = 0, V_BOX = 1, V_DRAW = 2 };

    void Reload();
    void DrawShelf(Canvas& cv, float s);
    void DrawBox(Canvas& cv, float s);
    void DrawCard(Canvas& cv, float s);
    void Toast(const std::wstring& msg);

    std::vector<QuizBox> m_boxes;
    int  m_view = V_SHELF;
    int  m_curBox = -1;          // 盒内 / 抽卡态：当前盒索引
    int  m_drawIdx = -1;         // 抽中卡索引
    bool m_flip = false;         // 抽卡是否已翻面

    // 命中区（内容坐标，Layout/Update/Paint 共用）
    std::vector<D2D1_RECT_F> m_boxRects;     // 盒卡
    std::vector<D2D1_RECT_F> m_boxRenRects;  // 盒「✎」
    std::vector<D2D1_RECT_F> m_boxDelRects;  // 盒「✕」
    D2D1_RECT_F m_newBoxRect{};              // 「＋ 新建题盒」
    D2D1_RECT_F m_backRect{};                // 盒内「◀ 盒架」
    D2D1_RECT_F m_drawBtn{};                 // 盒内「🎯 抽一张」
    D2D1_RECT_F m_addBtn{};                  // 盒内「＋ 添加卡片」
    std::vector<D2D1_RECT_F> m_cardDelRects; // 盒内卡「✕」
    D2D1_RECT_F m_flipRect{};                // 抽卡「翻面」
    D2D1_RECT_F m_nextRect{};                // 抽卡「下一张」
    D2D1_RECT_F m_backDrawRect{};            // 抽卡「放回」

    // 新建卡内联编辑（题面 → 答案 → 标签 三段回车推进）
    FieldEdit m_edit;
    FieldEdit m_ren;             // 盒改名（就地 FieldEdit）
    bool  m_addOpen = false;
    int   m_addStage = 0;        // 0=front 1=back 2=tag
    std::wstring m_addFront, m_addBack;
    void AdvanceAdd();
    bool InRectEbox(const D2D1_RECT_F& r, float x, float y) const;

    // 盒改名（就地 FieldEdit）
    bool  m_renActive = false;
    std::wstring m_renBuf;
    D2D1_RECT_F m_area{};
    Canvas* m_cv = nullptr;
    float m_t = 0.0f;
    std::wstring m_toast;
    float m_toastT = 0.0f;
};

} // namespace lj
