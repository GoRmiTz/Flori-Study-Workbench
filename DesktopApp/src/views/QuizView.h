#pragma once
// ============================================================
//  QuizView.h — 练考模块 Phase 1（阅读器 + 答题卡）
//  路由 route=quiz：列表（扫描 accounts/<账户>/quiz/<类别>/*.md）
//  → 点击打开居中试卷弹层（仿专栏详情），左侧题目 + 右侧答题卡。
//  提交判分 → 逐题解析展开 → 得分。无 AI 依赖即可运行（用手写 .md 验证）。
//  复用：QuizParser / RoadmapView 居中弹层模式 / Palette / Canvas。
// ============================================================
#include "ui/View.h"
#include "quiz/QuizParser.h"
#include "quiz/QuizStore.h"
#include <vector>
#include <string>
#include <utility>

namespace lj {

// 列表中的一份试卷摘要
struct QuizMeta
{
    std::wstring path;       // 绝对路径
    std::wstring title;      // 卷标题
    std::wstring category;   // 类别（文件夹名 / 未分类）
    std::wstring date;       // 日期
    std::wstring mode;       // 模式：quiz（练习） / exam（模考）
    int         durationMin = 0;  // 模考计划时长（分钟，仅 exam）
    int         count = 0;   // 题数
};

// 左侧题目块的布局结果（内容坐标：以 m_qBody 顶部为 0）
struct QItem
{
    int q = 0;                       // 题目序号（0-based）
    float top = 0;                  // 块顶部（内容坐标）
    float bottom = 0;               // 块底部
    float stemH = 0;                // 题干高度
    float explainH = 0;              // 解析块高度（提交后）
    std::vector<D2D1_RECT_F> optRects; // 选项命中框（内容坐标）
};

class QuizView : public View
{
public:
    QuizView();   // 启动每日定时出题调度器（QuizScheduler，复用看板娘凭据）
    const wchar_t* Id() const override { return L"quiz"; }
    const wchar_t* Title() const override { return L"练考"; }

    void OnEnter() override;
    void Update(float dt, const Input& in) override;
    void Paint(Canvas& cv) override;
    void DebugForcePreview() override;   // 截图自检：直接打开样例

private:
    // ---- 列表模式 ----
    void ScanQuizzes();
    void OpenDoc(const std::wstring& path);
    void CloseDoc();
    void PaintList(Canvas& cv);
    void ComputeListRects();         // 列表卡片矩形（屏幕坐标，随滚动平移）
    void PaintWeakCard(Canvas& cv, const D2D1_RECT_F& r);
    void ApplySettings();            // 仅把已保存调度设置推送给 QuizScheduler（无 UI；UI 已迁至 SettingsView）
    static bool InRect(const D2D1_RECT_F& r, float x, float y)
    { return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom; }
    static std::wstring FmtMMSS(int sec);                 // 秒 → mm:ss
    static int ParseDurationMin(const std::wstring& s);   // 解析 "90" / "90 分钟" → 90

    // ---- 弹层模式 ----
    void LayoutModalRects();            // 弹层矩形 + 答题卡方块（无需 cv）
    void LayoutQuestions(Canvas& cv);   // 题目块布局（需 cv.MeasureHeight）
    void PaintModal(Canvas& cv);
    void PaintExamHud(Canvas& cv);      // 模考顶部倒计时 HUD（Phase 3）
    void PaintExamReport(Canvas& cv);   // 模考报告面板（Phase 3，右侧）
    void Submit();
    void ResetAnswers();
    bool IsExam() const { return m_exam; }

    void SeedSampleIfEmpty();          // 首次无卷时写入内置样例
    static void EnumMd(const std::wstring& dir, const std::wstring& cat,
                       std::vector<QuizMeta>& out);

    // 列表数据
    std::vector<QuizMeta> m_list;
    std::vector<D2D1_RECT_F> m_listRects;   // 内容坐标（随滚动平移）
    std::vector<std::wstring> m_listPaths;

    // 「立即生成今日时政」按钮（屏幕坐标，列表头右上）
    D2D1_RECT_F m_genBtn{};
    long long    m_lastSeenGenTs = 0;       // 已消费的最新生成结果时间戳

    // 当前调度设置（OnEnter 载入，仅用于推送给 QuizScheduler；设置 UI 已迁至 SettingsView）
    quiz::QuizSettings m_set;
    std::vector<std::pair<std::wstring,int>> m_weakPoints; // 薄弱知识点缓存（降序）
    D2D1_RECT_F m_weakRect{};                             // 列表底部「薄弱知识点」卡（内容坐标）

    // 「进入全屏考场模考」通栏按钮（屏幕坐标，固定不滚动）
    D2D1_RECT_F m_examBtn{};

    // 弹层状态
    bool m_open = false;
    float m_modalAnim = 0.0f;
    quiz::QuizDoc m_doc;
    std::vector<std::vector<std::wstring>> m_sel;  // 每题已选 key 集合
    bool m_submitted = false;
    std::vector<bool> m_correct;
    int  m_correctCount = 0;

    // 模考模式（Phase 3）：mode=exam + duration
    bool  m_exam = false;            // 当前卷是否模考
    float m_examTotalSec = 0.0f;     // 计划总时长（秒）
    float m_examElapsed = 0.0f;      // 已用时（秒，Update 累积 dt）
    bool  m_examTimedOut = false;    // 是否超时自动收卷
    quiz::QuizReport m_report{};      // 模考报告

    // 弹层布局
    D2D1_RECT_F m_card{};
    D2D1_RECT_F m_qBody{};       // 左侧题目滚动区
    D2D1_RECT_F m_ansPanel{};    // 右侧答题卡 / 模考报告
    D2D1_RECT_F m_hud{};         // 模考倒计时 HUD 条
    D2D1_RECT_F m_closeBtn{};
    D2D1_RECT_F m_submitBtn{};
    std::vector<QItem> m_qItems;
    std::vector<D2D1_RECT_F> m_ansSquares;   // 答题卡方块（屏幕坐标，不滚动）
    float m_qScroll = 0.0f;
    float m_qContentH = 0.0f;

    // 轻提示
    bool m_toast = false;
    float m_toastT = 0.0f;
    std::wstring m_toastMsg;
    void Toast(const std::wstring& m);
    void DrawToast(Canvas& cv);
};

} // namespace lj
