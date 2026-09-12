#pragma once
// ============================================================
//  QuizBoxView.h — 题集卡片盒 v3 星云架构（批次 G8）
//  星云即唯一界面：恒星=题集 / 轨道=题盒 / 小方块=卡片。
//  需求细节见 docs/题集卡片盒·星云架构v3.md（唯一基准）。
// ============================================================
#include "ui/View.h"
#include "ui/FieldEdit.h"
#include "quiz/BoxStore.h"
#include <vector>
#include <tuple>

namespace lj {

// G4 T6：导入解析结果
struct QuizBoxImpBox
{
    std::wstring name;
    int kind = 0;
    std::vector<QCard> cards;
};

class QuizBoxView : public View
{
public:
    const wchar_t* Id() const override { return L"quizbox"; }
    const wchar_t* Title() const override { return L"题 集 卡 片 盒"; }

    void OnEnter() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;
    void DebugForcePreview() override;
    void DebugForceOpen() override;

private:
    enum V { V_NEBULA = 0 };   // v3：星云即唯一界面
    int  m_view = V_NEBULA;

    // 数据
    std::vector<QuizSet> m_sets;
    int  m_curSet = 0;           // 导入目标题集

    // v3：可见题集 / 聚焦（N1）
    std::vector<int> m_visibleSets;
    int  m_focusSet = -1;
    std::vector<D2D1_POINT_2F> m_setCenters;
    std::vector<D2D1_RECT_F> m_setHitR;
    void RebuildVisible();
    void SetCenters();
    void AfterTreeChange();       // N2 崩溃防线

    // ---- G5/G6：星云渲染与交互 ----
    struct NebCard {
        int setIdx = -1;
        int boxIdx = -1, cardIdx = -1;
        D2D1_POINT_2F pos{};
        float z = 0.0f, scale = 1.0f, alpha = 1.0f;
        D2D1_RECT_F rect{};
    };
    std::vector<NebCard> m_nebCards;
    float m_orbit = 0.0f;
    int   m_hoverNeb = -1;
    bool  m_nebDrag = false;
    int   m_nebDragIdx = -1;
    float m_nebDragX = 0.0f, m_nebDragY = 0.0f;
    float m_dragStartX = 0.0f, m_dragStartY = 0.0f;
    int   m_nebDropBox = -1;
    // G9 N2 拖拽创新：聚焦态下方恒星带 → 跨题集拖拽
    int   m_dragGuest = -1;           // 悬停的未聚焦题集索引（-1 无）
    float m_guestScale = 0.0f;        // guest 恒星放大插值
    D2D1_POINT_2F m_guestAnchor{};    // guest 轨道系统锚点（画面右侧）
    void DrawNebula(Canvas& cv, float s);

    // ---- 侧边栏（G6 T11）----
    FieldEdit m_search;
    bool  m_searchActive = false;
    std::wstring m_searchStr;
    std::vector<std::pair<int, std::pair<int,int>>> m_searchHits;
    bool  m_showMode = false;
    float m_showT = 0.0f;
    int   m_sortMode = 0;
    bool  m_focusOn = false;
    float m_focusX = 0.0f, m_focusY = 0.0f;
    D2D1_RECT_F m_sbSearchBox{}, m_sbSearchGo{}, m_sbShowBtn{};
    D2D1_RECT_F m_sbSortR[3]{};
    D2D1_RECT_F m_sbFocusBtn{};
    std::vector<D2D1_RECT_F> m_sbChkRects;
    std::vector<D2D1_RECT_F> m_sbBoxRows, m_sbBoxRenR, m_sbBoxDelR;
    D2D1_RECT_F m_sbNewBoxBtn{};
    D2D1_RECT_F m_sbImportBtn{};
    D2D1_RECT_F m_newSetBtn{};
    void ApplySearch();
    void EnterShowMode();
    void DrawSidebar(Canvas& cv);

    // ---- G9：N8 右键菜单（改难度渐变/移动至飞行/改名/删除破碎）----
    bool  m_menuOpen = false;
    int   m_menuMode = 0;             // 0=主菜单 1=移动至子菜单
    int   m_menuSet = -1, m_menuBox = -1, m_menuCard = -1;
    float m_menuT = 1.0f;             // 淡入
    D2D1_POINT_2F m_menuPos{};
    D2D1_RECT_F m_menuPanel{}, m_menuDiffR[5]{}, m_menuMoveR{}, m_menuRenR{}, m_menuDelR{};
    std::vector<std::tuple<std::wstring, int, int>> m_menuTargets;  // (显示名, setIdx, boxIdx)
    std::vector<D2D1_RECT_F> m_menuTargetR;
    // 动效状态
    int   m_gradSet = -1, m_gradBox = -1, m_gradCard = -1;   // 改难度渐变
    int   m_gradFrom = 3, m_gradTo = 3;
    float m_gradT = 1.0f;
    // 飞行（移动至）
    bool  m_flyOpen = false;
    int   m_flySet = -1, m_flyBox = -1, m_flyCard = -1;
    float m_flyT = 1.0f;
    D2D1_POINT_2F m_flyFrom{}, m_flyTo{};
    std::wstring m_flyFromId, m_flyCardId, m_flyToId;
    // 破碎（删除）
    bool  m_shatterOpen = false;
    float m_shatterT = 1.0f;
    D2D1_POINT_2F m_shatterPos{};
    D2D1_COLOR_F m_shatterCol{};
    struct Shard { float vx, vy, rot; };
    std::vector<Shard> m_shards;
    void OpenMenu(int setIdx, int boxIdx, int cardIdx, float x, float y);
    void DrawMenu(Canvas& cv);
    void MenuActionDiff(int d);
    void MenuActionMove(int targetIdx);
    void MenuActionDelete();
    D2D1_COLOR_F pal_shatter_col(int si, int bi, int ci);

    // ---- N4：展开卡 ----
    bool  m_expOpen = false;
    int   m_expBox = -1;         // 题集索引
    int   m_expCard = -1;        // 盒索引
    int   m_expCard2 = -1;       // 卡索引
    float m_expT = 1.0f;
    bool  m_expFlip = false;
    float m_expFlipT = 1.0f;
    D2D1_RECT_F m_expFlipBtn{}, m_expRemBtn{}, m_expWrongBtn{};
    void DrawExpand(Canvas& cv);

    // ---- T3 制卡弹窗 ----
    FieldEdit m_edit;
    bool  m_ceOpen = false;
    int   m_ceField = 0;
    int   m_ceDiff = 3;
    std::wstring m_ceFront, m_ceBack, m_ceTag;
    std::wstring m_ceEditId;
    std::wstring m_ceTargetBox;
    D2D1_RECT_F m_ceCard{}, m_ceFrontR{}, m_ceBackR{}, m_ceTagR{},
                m_ceDiffR[5]{}, m_ceSaveR{}, m_ceSaveMoreR{}, m_ceCancelR{};
    void OpenCardEditor(const QCard* edit, const std::wstring& targetBox);
    void CeCommit(bool keepOpen);
    void CeCancel();
    void CeNext();
    void CeSwitchField(int idx);
    D2D1_POINT_2F EditorCenter() const;   // N7：弹窗中心（聚焦恒星/屏幕中心）

    // ---- 改名 ----
    FieldEdit m_ren;
    bool  m_renActive = false;
    int   m_renBox = -1;
    D2D1_RECT_F m_renBox2{};

    // ---- G4 导入 ----
    bool  m_impOpen = false;
    std::wstring m_impFile;
    std::vector<QuizBoxImpBox> m_impBoxes;
    D2D1_RECT_F m_impCard{}, m_impOkR{}, m_impCancelR{};
    D2D1_RECT_F m_impBtn{};
    void BrowseImport();
    void DoImport();

    // ---- 通用 ----
    void DrawHouse(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& accent) const;
    float CardHeat(const QCard& c) const;
    void Toast(const std::wstring& msg);

    D2D1_RECT_F m_area{};
    Canvas* m_cv = nullptr;
    float m_t = 0.0f;
    float m_viewT = 1.0f;
    float m_newT = 1.0f;
    std::wstring m_toast;
    float m_toastT = 0.0f;
};

} // namespace lj
