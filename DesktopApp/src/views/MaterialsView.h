#pragma once
// ============================================================
//  MaterialsView.h — 资料推荐（No.05）
//  移植 WebApp vMaterials() 可本地运行的部分：一句话主推 + 预算参考 +
//  分类资料清单（系统课 / 分科名师 / 申论 …）+ 避坑。数据来自
//  Content::Get().materials（已在 Data.cpp 落地，默认给一份通用备考底稿）。
//  带外链的条目渲染「↗ 资源」徽标，点击经 ShellExecute 打开默认浏览器
//  （链接均为本地 Data 中可信静态地址）。
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "app/Data.h"
#include "app/Store.h"

namespace lj {

class MaterialsView : public View
{
public:
    const wchar_t* Id() const override { return L"materials"; }
    const wchar_t* Title() const override { return L"资料推荐"; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    void PaintTitle(Canvas& cv, float x0, float y, float contentW);
    void PaintLead(Canvas& cv, float x0, float y, float contentW);
    void PaintCategories(Canvas& cv, float x0, float y, float contentW);
    void PaintPitfalls(Canvas& cv, float x0, float y, float contentW);

    D2D1_COLOR_F Accent(int a, const Palette& pal) const;

    // 单分类几何缓存
    struct CatGeom {
        D2D1_RECT_F header{ 0,0,0,0 };
        std::vector<D2D1_RECT_F> items;     // 每条资料行
        std::vector<D2D1_RECT_F> linkRects; // 与 items 平行，空=无外链
        std::vector<std::wstring>  linkUrls;
        int accent = 0;
    };

    float m_contentTop = 0;
    float m_leadY = 0;
    float m_catY  = 0;
    float m_pitY  = 0;
    float m_phY   = 0;

    D2D1_RECT_F m_leadRect{ 0,0,0,0 };
    D2D1_RECT_F m_pitRect{ 0,0,0,0 };

    std::vector<CatGeom> m_cats;
    D2D1_RECT_F m_hoverRect{ 0,0,0,0 };   // 当前悬停的外链徽标（坐标命中）

    Button m_backBtn;
    Button m_resBtn;       // 资料库内跳转：本地资源库
    Button m_videoBtn;     // 资料库内跳转：公共视频广场
    std::vector<Widget*> m_widgets;

    float m_t = 0.0f;
};

} // namespace lj
