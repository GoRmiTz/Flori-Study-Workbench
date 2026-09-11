#pragma once
// ============================================================
//  ReviewEngine.h — F-D7 本地复盘搭子（规则版，无依赖 / 无网络 / 无模型）
//  纯本地规则：汇总某日真实打卡 / 专注 / 模考中断 / 复盘数据，
//  比对方案文档目标正确率，产出薄弱项列表 + 一句提示 + 逐行复盘结论。
//  数据全部来自 CheckinStore，不外发、不联网、不调用任何 AI。
// ============================================================
#include <string>
#include <vector>

namespace lj {

struct ReviewWeakItem
{
    std::wstring tag;       // 模块标签（如「申论」「英语」）
    int pct = 0;            // 当日该模块完成率
    int total = 0;          // 当日该模块应完成项总数
    int target = 0;         // 方案文档目标正确率
    int gap() const { return target - pct; }
};

struct DayReview
{
    std::wstring dateKey;                 // YYYY-MM-DD
    int done = 0, total = 0;              // 当日打卡完成 / 应完成
    int overallPct = 0;                   // 当日整体完成率
    int focusMin = 0, focusCnt = 0;       // 当日专注时长（分钟）/ 次数
    int interrupts = 0;                   // 当日模考中断次数
    bool journaled = false;               // 当日是否已写复盘
    std::vector<ReviewWeakItem> weak;     // 薄弱项（已按缺口降序）
    std::wstring hint;                    // 一句提示
    std::vector<std::wstring> lines;      // 复盘结论（逐行）
};

class ReviewEngine
{
public:
    // 生成某日复盘（纯本地规则）。dateKey 形如 "2026-08-11"。
    static DayReview Generate(const std::wstring& dateKey);

    // 今天的日期键（本地时区，YYYY-MM-DD），供每日 nudge 去重与查询使用。
    static std::wstring TodayKey();

    // 把当日薄弱项写入今日复盘的「明日三要事」（数据不出本机）。
    // 无薄弱项时写入「保持节奏」兜底。返回 true 表示已写入。
    static bool CommitWeakToJournal(const std::wstring& dateKey);
};

} // namespace lj
