#pragma once
// ============================================================
//  AchieveView.h — 成就体系 F3 + 契约组队 F4（P1-8 留存抓手）
//  F3：从真实本地数据（专注/打卡/复盘/自习室）派生阶段徽章、
//      连续里程碑、年度回顾，可导出分享。
//  F4：本地优先「自律契约」——期限 + 规则 + 每日目标，自动比对专注进度，
//      违约公示。（多人实时同步为服务端后续项，本视图标注。）
// ============================================================
#include "ui/View.h"
#include "ui/Widget.h"
#include "ui/FieldEdit.h"
#include "app/Data.h"
#include "app/Store.h"
#include "ui/FieldText.h"
#include "core/Hwnd.h"
#include <vector>
#include <string>

namespace lj {

// ---------------- F4 契约 ----------------
struct PactMember
{
    std::wstring name;
    bool isSelf = false;
    int progressMin = 0;   // 仅 self 由专注数据自动填
};
struct Pact
{
    std::wstring id, name, rule, created;  // created: YYYY-MM-DD
    int days = 21;
    int targetMinPerDay = 60;
    std::vector<PactMember> members;
};

// F-D1 今日各前台进程时长（按类型）
struct AppTime { std::wstring proc; int min = 0; int cat = 0; };

class AchieveView : public View
{
public:
    const wchar_t* Id() const override { return L"achieve"; }
    const wchar_t* Title() const override { return L"成 就 · 契 约"; }

    void OnEnter() override;
    void OnLeave() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;
    void DebugForceOpen() override;      // 截图自检：切到「契约·组队」并打开新建面板
    void DebugForcePreview() override;   // 截图自检：切到「契约·组队」列表页

private:
    // ---- F3 数据 ----
    struct Badge { std::wstring id, name, desc; bool got = false; int cur = 0, goal = 0; };
    std::vector<Badge> m_badges;
    int m_streak = 0, m_totalFocus = 0, m_roomSessions = 0, m_journalDays = 0, m_weekAvg = 0;
    int m_yearFocus = 0, m_yearDays = 0;

    // ---- F-D1 今日时间分布 / 记了什么 ----
    std::vector<AppTime> m_todayByApp;   // 今日各进程时长（按类型）
    int m_todayEnt = 0, m_yearEnt = 0;   // 娱乐降权时长（不计专注）
    D2D1_RECT_F m_focusCard{};

    // ---- F-D4 申论字数统计 ----
    D2D1_RECT_F m_docxCard{};
    D2D1_RECT_F m_docxBtnRect{};
    D2D1_RECT_F m_docxRevokeRect{};
    int  m_docxChars = 0;
    bool m_docxOk = false;        // 已授权且字数统计成功
    std::wstring m_docxFile;      // 显示用文件名

    // ---- F4 契约 ----
    std::vector<Pact> m_pacts;
    bool m_editingPact = false;
    std::wstring m_pactName, m_pactRule;
    int m_pactDays = 21, m_pactTarget = 60;
    std::vector<D2D1_RECT_F> m_pactRects;
    std::vector<D2D1_RECT_F> m_pactDelRects;
    int m_todayFocus = 0;    // 今日专注分钟（F4 自评进度）

    // ---- F4 组队（本地优先；多人实时同步为服务端后续项）----
    FieldEdit m_mateEdit;                                  // 队友名输入（统一输入框）
    bool   m_mateEditing = false;
    int    m_matePactIdx = -1;                             // 正在为哪个契约添加队友
    std::wstring m_mateName;
    D2D1_RECT_F m_mateInputRect{};
    std::vector<D2D1_RECT_F> m_mateAddRects;               // 每契约「＋ 队友」按钮
    std::vector<std::vector<D2D1_RECT_F>> m_matePlusRects; // 每契约 · 每队友「+15 分」
    std::vector<std::vector<D2D1_RECT_F>> m_mateDelRects;  // 每契约 · 每队友「移除」
    void BeginMateEdit(int pactIdx); void CommitMateEdit(); void CancelMateEdit();

    // ---- 编辑（v2 统一输入框：契约名称 / 规则）----
    bool m_editing = false; int m_editField = 0;   // 1 名称 / 2 规则
    FieldEdit m_edit;
    Canvas* m_cvCached = nullptr;
    D2D1_RECT_F m_pactNameRect{}, m_pactRuleRect{};
    void BeginEdit(int field); void CommitEdit(); void CancelEdit();

    // ---- 控件 ----
    Button m_newPactBtn, m_createPactBtn, m_cancelPactBtn;
    Button m_pactDaysMinus, m_pactDaysPlus, m_pactTargetMinus, m_pactTargetPlus;
    Button m_shareBtn, m_backBtn;
    Button m_tabF3, m_tabF4;          // 成就 / 契约 切换
    int m_tab = 0;                    // 0 成就 · 1 契约
    std::vector<D2D1_RECT_F> m_badgeRects;
    D2D1_RECT_F m_yearCard{};
    D2D1_RECT_F m_streakCard{};        // 连续里程碑卡（3/7/21/50/100 天阶梯）
    std::vector<Widget*> m_widgets;
    float m_t = 0.0f;
    D2D1_RECT_F m_area{};
    bool m_toast = false; float m_toastT = 0.0f; std::wstring m_toastMsg;

    // ---- 方法 ----
    void Recompute();                       // F3 派生徽章/统计
    void LoadPacts(); void SavePacts();
    void PaintF3(Canvas& cv);               // 成就 + 年度回顾
    void PaintF4(Canvas& cv);               // 契约板
    void OnDocxConnect();                   // F-D4 授权 + 选文件
    void OnDocxRevoke();                   // F-D4 撤销授权
    void PaintDocxButton(Canvas& cv, const D2D1_RECT_F& r, const std::wstring& label, const D2D1_COLOR_F& col);
    static std::wstring ToU(const std::wstring& s);  // wstring → UTF-8
};

} // namespace lj
