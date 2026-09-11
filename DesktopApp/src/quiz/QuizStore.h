#pragma once
// ============================================================
//  QuizStore.h — 练考模块 Phase 4：成绩落盘 + 薄弱知识点聚合
//  零依赖（仅用通用 app/Json.h），不耦合未提交看板娘层（Store.h/CheckinStore）。
//  成绩落 accounts/<账户>/quiz_results.json；薄弱知识点（答错题的知识点）
//  跨次聚合，供列表展示与后续复盘接入。
// ============================================================
#include "quiz/QuizParser.h"   // 复用 quiz::QuizReport / QuizDoc 概念
#include <string>
#include <vector>
#include <utility>

namespace lj {
namespace quiz {

// 单次答题/模考结果
struct QuizAttempt
{
    long long   ts = 0;            // 完成时间戳（秒）
    std::wstring dateKey;         // YYYY-MM-DD
    std::wstring title;
    std::wstring category;
    std::wstring mode;            // "quiz" / "exam"
    int total = 0;
    int correct = 0;
    int wrong = 0;
    int unanswered = 0;
    float accuracy = 0.0f;       // 0..100
    int plannedSec = 0;
    int usedSec = 0;
    bool timeout = false;
    std::vector<std::wstring> weakPoints;   // 答错题目的知识点
};

// 调度器运行时配置（与 QuizScheduler::SetConfig 对应）
struct QuizSettings
{
    bool        enabled = true;
    int         hour = 7;
    int         minute = 30;
    std::wstring category = L"时政";
    int         qcount = 10;
    bool        rss = true;
};

class QuizStore
{
public:
    static QuizStore& Instance();

    // 账户切换时由 QuizView 注入当前账户根目录（<exe>/accounts/<name>/）
    void SetRoot(const std::wstring& root) { m_root = root; }

    // 记录一次作答（追加并落盘）；返回是否写入成功
    bool RecordAttempt(const QuizAttempt& a);

    // 读全部历史（文件缺失返回空）
    std::vector<QuizAttempt> LoadAll() const;

    // 薄弱知识点聚合：知识点 → 答错次数（降序），取前 n（n<=0 全取）
    std::vector<std::pair<std::wstring, int>> TopWeakPoints(int n = 10) const;

    // 设置读写（quiz_settings.json）
    bool SaveSettings(const QuizSettings& s);
    QuizSettings LoadSettings() const;

    bool empty() const { return m_root.empty(); }

private:
    std::wstring m_root;
    std::wstring Path()     const { return m_root + L"quiz_results.json"; }
    std::wstring SetPath()  const { return m_root + L"quiz_settings.json"; }
};

} // namespace quiz
} // namespace lj
