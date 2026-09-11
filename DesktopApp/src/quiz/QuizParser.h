#pragma once
// ============================================================
//  QuizParser.h — 题目 MD 解析器（练考模块 Phase 0-2）
//  零依赖、零 Win32：自含 UTF-8 解码，纯状态机解析《题目MD规范文档》。
//  输入：符合规范的 .md 文本/文件；输出：QuizDoc（meta + 有序题目）。
//  复用点：QuizView / QuizGen / ReviewEngine 均消费 QuizDoc。
// ============================================================
#include <string>
#include <vector>
#include <map>

namespace lj {
namespace quiz {

struct QuizOption
{
    std::wstring key;    // "A".."E"
    std::wstring text;   // 选项文本
};

struct QuizQuestion
{
    int id = 0;                 // 题目序号（Q 后数字，决定排序）
    std::wstring type = L"单选"; // 单选 | 多选 | 判断
    std::wstring stem;          // 题干
    std::vector<QuizOption> options;        // 按 A–E 顺序（解析后排序）
    std::vector<std::wstring> answer;       // 正确 key 集合，如 {"A"} / {"A","B","D"}
    std::wstring explain;       // 解析（可多行）
    std::wstring point;         // 知识点
    int difficulty = 1;         // 1–3
    bool hasAnswer = false;     // 答案是否解析到（false=待批改）
};

struct QuizDoc
{
    std::wstring title;                       // 卷标题
    std::map<std::wstring, std::wstring> meta; // YAML 元数据：title/date/category/mode/duration/source/model
    std::vector<QuizQuestion> questions;
};

// 纯文本解析
QuizDoc ParseQuizText(const std::wstring& text);

// 从文件加载（UTF-8，无 BOM 亦可）
bool LoadQuiz(const std::wstring& path, QuizDoc& out);

// 判分：所选集合 == 答案集合（无序、去重）即正确
bool IsCorrect(const QuizQuestion& q, const std::vector<std::wstring>& selected);

// ---------------- 模考报告（Phase 3） ----------------
// 纯数据 + 纯逻辑，无 Win32 依赖；渲染由 QuizView 负责。
struct QuizReport
{
    int total = 0;              // 题目总数
    int correct = 0;            // 答对题数
    int wrong = 0;              // 答错题数
    int unanswered = 0;         // 未答题数
    float accuracy = 0.0f;      // 正确率 0..100
    int plannedSec = 0;         // 计划时长（秒）
    int usedSec = 0;            // 实际用时（秒）
    bool timeout = false;       // 是否超时自动收卷
    bool abandoned = false;     // 是否中途放弃（预留，Phase 3 内联模式不追踪）
    std::vector<bool> correctByQ;   // 每题对错（与 questions 对应）
};

// 生成报告：依据作答集合与每题答案判分，并汇总统计。
QuizReport ComputeQuizReport(const QuizDoc& doc,
                             const std::vector<std::vector<std::wstring>>& sel,
                             int plannedSec, int usedSec, bool timeout);

} // namespace quiz
} // namespace lj
