#pragma once
// ============================================================
//  AdvisorView.h — 选岗参谋 F2（P1-7 选岗最痛点）
//  导入国考/省考职位表（CSV）→ 按专业四层口径匹配 →
//  结合模考分与近三年进面分给冲/稳/保三档 → 候选岗可加入备考打卡。
//  纯前端 MVP，依赖：无。
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

struct Position
{
    std::wstring code, dept, name, majorReq, edu, politic;
    int count = 1;       // 招考人数
    int ratio = 3;       // 面试比例
    int cut = -1;        // 近三年进面分（-1 未知）
    // 派生
    int match = 0;       // 0 不匹配 / 1 门类 / 2 专业类 / 3 专业
    int tier = -1;       // -1 不匹配 / 0 冲 / 1 稳 / 2 保
    std::wstring region; // F-D10 解析出的省级行政区（从 dept/name 或 CSV 工作地点列）
};

class AdvisorView : public View
{
public:
    const wchar_t* Id() const override { return L"advisor"; }
    const wchar_t* Title() const override { return L"选 岗 参 谋"; }

    void OnEnter() override;
    void OnLeave() override;
    void Layout(const D2D1_RECT_F& area, Canvas& cv) override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;

private:
    // ---- 候选画像（默认：数字媒体艺术 130508）----
    std::wstring m_majorCode = L"130508";
    std::wstring m_majorName = L"数字媒体艺术";
    std::wstring m_catCode = L"1305";     // 设计学类
    std::wstring m_classCode = L"13";     // 艺术学
    int m_mock = 65;                       // 模考总分（用户可调）

    // ---- 数据 ----
    std::vector<Position> m_pos;
    std::vector<int> m_matchIdx;          // 匹配上的下标（已排序）
    bool m_loaded = false;

    // ---- 专业代码编辑（v2 统一输入框，数字）----
    bool m_editing = false;
    FieldEdit m_edit;
    D2D1_RECT_F m_majorRect{};
    Canvas* m_cvCached = nullptr;
    void BeginEdit(); void CommitEdit(); void CancelEdit();

    // ---- 控件 ----
    Button m_mockMinus, m_mockPlus, m_importBtn, m_backBtn;
    Button m_detailClose, m_joinBtn;
    bool m_showDetail = false; int m_detailIdx = -1;
    bool m_toast = false; float m_toastT = 0;
    float m_t = 0.0f;

    std::vector<Widget*> m_widgets;       // 画像/返回等（常驻）
    std::vector<Widget*> m_detailWidgets; // 详情弹层按钮（仅弹层时）

    // ---- F-D10 选岗地图可视化（省区气泡散点，不画边界，合规）----
    struct RegionBubble { std::wstring region; float cx = 0, cy = 0, r = 0; int count = 0, t0 = 0, t1 = 0, t2 = 0; };
    std::vector<RegionBubble> m_bubbles;   // 按省区聚合（布局时填充，内容坐标）
    std::wstring m_selRegion;              // 当前选中省（点击气泡切换，空=未选）
    D2D1_RECT_F m_mapCard{}, m_mapPlot{}, m_mapInfo{};
    void PaintMap(Canvas& cv);
    static std::wstring RegionOf(const std::wstring& dept, const std::wstring& name);

    // ---- 列表（内容坐标，随页面滚动）----
    std::vector<D2D1_RECT_F> m_rowRects;
    D2D1_RECT_F m_listCard{};
    D2D1_RECT_F m_profileCard{};
    D2D1_RECT_F m_area{};        // 最近一次布局区域（详情弹层定位用）

    D2D1_RECT_F DetailCardRect() const;

    // ---- 方法 ----
    void LoadSample();                                  // 内置样例（开箱即用）
    void ImportCsv();                                   // 导入职位表 CSV
    void Recompute();                                   // 匹配 + 分档 + 排序
    static std::vector<Position> ParseCsv(const std::wstring& path);

    void PaintProfile(Canvas& cv);
    void PaintList(Canvas& cv);
    void PaintDetail(Canvas& cv);

    static std::wstring ToW(const std::string& s);          // UTF-8 → wstring
    static std::wstring FirstDigitRun(const std::wstring& s); // 首个连续数字串
};

} // namespace lj
