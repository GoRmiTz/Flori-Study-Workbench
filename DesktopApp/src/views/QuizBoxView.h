#pragma once
// ============================================================
//  QuizBoxView.h — 题集卡片盒 v2（批次 G2）
//  三层结构：题集（房子）→ 题盒（盒子）→ 卡片（方块）。
//  四态：题集架 / 盒架 / 盒内 / 抽卡 + 居中制卡弹窗。
//  视图切换渐入、新建弹入、hover 浮起（T2 基础动效）。
//  完整需求见 docs/题集卡片盒·开发文档.md。
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
    void DebugForcePreview() override;   // 截图自检：题集架
    void DebugForceOpen() override;     // 截图自检：盒内 + 制卡弹窗

private:
    enum V { V_SETS = 0, V_BOXES = 1, V_BOX = 2, V_DRAW = 3, V_NEBULA = 4 };

    // 当前位置（三层导航）
    int  m_view = V_SETS;
    int  m_curSet = -1;          // 题集索引
    int  m_curBox = -1;          // 盒索引（m_sets[m_curSet].boxes 内）
    int  m_drawIdx = -1;         // 抽中卡索引
    bool m_flip = false;

    // 数据
    std::vector<QuizSet> m_sets;

    // ---- 绘制（各态）----
    void DrawSets(Canvas& cv, float s);
    void DrawBoxes(Canvas& cv, float s);
    void DrawBox(Canvas& cv, float s);
    void DrawDraw(Canvas& cv, float s);
    void DrawCardEditor(Canvas& cv, float s);   // T3 制卡弹窗
    void DrawHouse(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& accent) const;
    void DrawBoxIcon(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& accent,
                     bool wrong) const;
    void Toast(const std::wstring& msg);

    // ---- 命中区（内容坐标，Layout/Paint/Update 共用）----
    std::vector<D2D1_RECT_F> m_setRects, m_setDelRects;
    D2D1_RECT_F m_newSetRect{};
    std::vector<D2D1_RECT_F> m_boxRects, m_boxRenRects, m_boxDelRects;
    D2D1_RECT_F m_newBoxRect{};
    D2D1_RECT_F m_nebBtn{};           // G5：盒架「🌌 星云」入口
    D2D1_RECT_F m_backRect{}, m_drawBtn{}, m_addBtn{};
    std::vector<D2D1_RECT_F> m_cardRects, m_cardDelRects;
    D2D1_RECT_F m_flipRect{}, m_nextRect{}, m_backDrawRect{};

    // ---- T3 制卡弹窗（居中卡片窗：题面/答案/标签/难度 1-5）----
    FieldEdit m_edit;                // 制卡弹窗输入（front/back/tag 三段）
    bool  m_ceOpen = false;
    int   m_ceField = 0;              // 0=front 1=back 2=tag
    int   m_ceDiff = 3;               // 难度
    std::wstring m_ceFront, m_ceBack, m_ceTag;
    std::wstring m_ceEditId;           // 非空 = 编辑既有卡
    D2D1_RECT_F m_ceCard{}, m_ceFrontR{}, m_ceBackR{}, m_ceTagR{},
                m_ceDiffR[5]{}, m_ceSaveR{}, m_ceSaveMoreR{}, m_ceCancelR{};
    void OpenCardEditor(const QCard* edit);   // edit=null 新建
    void CeCommit(bool keepOpen);
    void CeCancel();
    void CeNext();                    // 回车推进字段 / 最后字段保存
    void CeSwitchField(int idx);     // 点击字段切换

    // ---- 改名（题集 / 题盒共用一个 FieldEdit）----
    FieldEdit m_ren;
    bool  m_renActive = false;
    int   m_renSet = -1, m_renBox = -1;   // 目标（-1 = 该层不适用）
    D2D1_RECT_F m_renBox2{};              // 盒名就地编辑框

    // ---- 动效（T2）----
    float m_viewT = 1.0f;            // 视图切换计时（0→1，150ms 渐入：12px 上滑 + alpha）
    float m_newT = 0.0f;             // 新建条目弹入计时
    int   m_newHighlight = -1;      // 新建高亮下标（盒架/题集架）
    int   m_hoverCard = -1;          // 盒内 hover 卡（浮起）

    // ---- G3：T5 抽卡动效 ----
    float m_popT = 1.0f;             // 弹卡计时（0→0.45s：盖子开 + 卡飞出）
    float m_flipT = 1.0f;            // 翻面计时（0→0.28s：rotateY 模拟）
    // ---- G3：T10 反馈闭环（记住了 / 答错了）----
    D2D1_RECT_F m_remRect{}, m_wrongRect{};
    // ---- G3：T4 卡片拖拽跨盒 ----
    bool  m_dragging = false;
    int   m_dragIdx = -1;
    float m_dragX = 0.0f, m_dragY = 0.0f;
    float m_dragStartX = 0.0f, m_dragStartY = 0.0f;
    bool  m_moveOpen = false;        // 「移动到题盒」浮层
    std::vector<std::wstring> m_moveBoxIds;
    std::vector<std::wstring> m_moveBoxNames;
    std::vector<D2D1_RECT_F>  m_moveRects;
    D2D1_RECT_F m_movePanel{};
    void StartDraw();                 // 抽一张（重置弹卡动画）
    float CardHeat(const QCard& c) const;   // 红警示 0..1

    // ---- G5：T8 卡片星云（轨道模型 + 伪 3D + 难度高亮）----
    struct NebCard {
        int boxIdx = -1, cardIdx = -1;
        D2D1_POINT_2F pos{};
        float z = 0.0f;             // 深度 -1（远）..1（近）
        float scale = 1.0f, alpha = 1.0f;
        D2D1_RECT_F rect{};          // 屏幕坐标（含缩放后尺寸）
    };
    std::vector<NebCard> m_nebCards;  // 每帧由 Paint 重建
    float m_orbit = 0.0f;             // 公转角（慢速漂移）
    int   m_hoverNeb = -1;           // hover 小卡（同步高亮同难度）
    D2D1_RECT_F m_nebBackRect{};
    void DrawNebula(Canvas& cv, float s);

    D2D1_RECT_F m_area{};
    Canvas* m_cv = nullptr;
    float m_t = 0.0f;
    std::wstring m_toast;
    float m_toastT = 0.0f;
};

} // namespace lj
