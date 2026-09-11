// ============================================================
//  ReviewEngine.cpp — F-D7 本地复盘搭子（规则版）
//  纯本地规则引擎：不联网、不调用模型、不读窗内文本。
//  输入：CheckinStore 的当日打卡 / 专注 / 模考 / 复盘数据。
//  输出：完成率、薄弱项（实际 < 方案目标）、一句提示、逐行结论。
// ============================================================
#include "core/ReviewEngine.h"
#include "app/Store.h"
#include "app/Data.h"
#include <map>
#include <algorithm>
#include <ctime>

namespace lj {

namespace {
// 方案文档目标正确率（与 DashView::PlanTargetPct 口径保持一致；
// 若调整目标，请同步 DashView.cpp 中的同名表）。未知 tag 默认 80%。
int PlanTargetPct(const std::wstring& tag)
{
    static const std::map<std::wstring, int> t = {
        { L"行测", 85 }, { L"申论", 80 }, { L"英语", 80 }, { L"专业课", 85 },
        { L"政治", 85 }, { L"时政", 75 }, { L"真题", 80 }, { L"模考", 80 },
        { L"错题", 75 }, { L"复盘", 90 }, { L"单词", 80 }, { L"专业课/政治", 85 },
    };
    auto it = t.find(tag);
    return it != t.end() ? it->second : 80;
}

// 读取某日某打卡项是否完成：有记录取记录，无记录一律按未完成（诚实）。
// 与 DashView::DoneOf 同口径（CheckinView 按 Key() 落盘，优先 Key，回退 title）。
bool DoneOf(const std::map<std::wstring, bool>& dm, const CheckItem& it)
{
    auto f = dm.find(it.Key());
    if (f != dm.end()) return f->second;
    f = dm.find(it.title);
    if (f != dm.end()) return f->second;
    return false;
}

Date ParseKey(const std::wstring& key)
{
    Date d{ 2026, 1, 1 };
    int y = 0, m = 0, day = 0;
    if (swscanf_s(key.c_str(), L"%d-%d-%d", &y, &m, &day) == 3)
        d = { y, m, day };
    return d;
}

std::wstring EpochToKey(long long epoch)
{
    time_t t = (time_t)epoch;
    struct tm tm;
    localtime_s(&tm, &t);
    wchar_t buf[16];
    swprintf_s(buf, L"%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}
} // namespace

DayReview ReviewEngine::Generate(const std::wstring& dateKey)
{
    DayReview r;
    r.dateKey = dateKey;

    auto& store = CheckinStore::Instance();
    ChecklistBundle b = store.LoadItems();
    Date d = ParseKey(dateKey);
    auto dm = store.LoadDay(dateKey);

    // ---- 打卡：整体 + 按 tag ----
    std::map<std::wstring, std::pair<int, int>> acc;  // tag -> {done, total}
    for (const auto& it : ItemsForDate(b, d)) {
        bool done = DoneOf(dm, it);
        ++r.total;
        if (done) ++r.done;
        auto& a = acc[it.tag];
        a.second += 1;
        if (done) a.first += 1;
    }
    r.overallPct = r.total ? (int)((float)r.done / (float)r.total * 100.0f + 0.5f) : 0;

    // ---- 专注：当日分钟 / 次数 ----
    for (const auto& f : store.LoadFocus())
        if (f.date == dateKey) { r.focusMin += f.min; ++r.focusCnt; }

    // ---- 模考中断：当日开始的模考报告计数 ----
    for (const auto& e : store.LoadExamReports())
        if (EpochToKey(e.startTime) == dateKey) ++r.interrupts;

    // ---- 复盘：当日是否已写 ----
    r.journaled = !store.LoadJournal(dateKey).Empty();

    // ---- 薄弱项：实际完成率 < 方案目标（按缺口降序）----
    std::vector<ReviewWeakItem> weak;
    for (const auto& kv : acc) {
        if (kv.second.second == 0) continue;
        int pct = (int)((float)kv.second.first / (float)kv.second.second * 100.0f + 0.5f);
        int target = PlanTargetPct(kv.first);
        if (pct < target) weak.push_back({ kv.first, pct, kv.second.second, target });
    }
    std::sort(weak.begin(), weak.end(),
              [](const ReviewWeakItem& a, const ReviewWeakItem& b) { return a.gap() > b.gap(); });
    r.weak = weak;

    // ---- 一句提示（规则版，不调用模型）----
    if (r.total == 0) {
        r.hint = L"今日还没有打卡项生效，先把基础打卡补上，再来谈复盘。";
    } else if (weak.empty()) {
        if (r.focusMin >= 120) r.hint = L"各模块达标、今日专注充足，保持节奏即可。";
        else if (r.focusMin > 0) r.hint = L"打卡达标，但专注时长偏短（"
            + std::to_wstring(r.focusMin) + L" 分钟），注意番茄钟节奏。";
        else r.hint = L"打卡达标，但今天还没记到专注——用番茄钟锁一段深度学习。";
    } else {
        const auto& w = weak[0];
        r.hint = L"优先补强「" + w.tag + L"」（方案目标 " + std::to_wstring(w.target)
               + L"% · 今日实际 " + std::to_wstring(w.pct) + L"%）。";
        if (!r.journaled) r.hint += L" 收工前写两行今日复盘。";
    }

    // ---- 逐行结论 ----
    r.lines.push_back(L"完成率：" + std::to_wstring(r.overallPct) + L"% （"
                      + std::to_wstring(r.done) + L"/" + std::to_wstring(r.total) + L"）");
    r.lines.push_back(L"专注：" + std::to_wstring(r.focusMin) + L" 分钟 · "
                      + std::to_wstring(r.focusCnt) + L" 次");
    if (r.interrupts > 0)
        r.lines.push_back(L"模考中断：" + std::to_wstring(r.interrupts) + L" 次");
    r.lines.push_back(L"复盘：" + std::wstring(r.journaled ? L"已写" : L"未写"));
    if (weak.empty())
        r.lines.push_back(L"薄弱项：无，各模块均达方案目标。");
    else {
        std::wstring s;
        for (size_t i = 0; i < weak.size() && i < 4; ++i) {
            if (i) s += L"；";
            s += weak[i].tag + L" " + std::to_wstring(weak[i].pct) + L"%";
        }
        r.lines.push_back(L"薄弱项：" + s);
    }
    r.lines.push_back(L"提示：" + r.hint);

    return r;
}

std::wstring ReviewEngine::TodayKey()
{
    time_t t = time(nullptr);
    struct tm tm;
    localtime_s(&tm, &t);
    wchar_t buf[16];
    swprintf_s(buf, L"%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

bool ReviewEngine::CommitWeakToJournal(const std::wstring& dateKey)
{
    DayReview r = Generate(dateKey);
    std::wstring next;
    if (r.weak.empty()) {
        next = L"1. 保持今日节奏，继续推进主线。";
    } else {
        for (size_t i = 0; i < r.weak.size() && i < 3; ++i) {
            next += std::to_wstring(i + 1) + L". 补强 " + r.weak[i].tag
                  + L"（方案目标 " + std::to_wstring(r.weak[i].target) + L"% · 今日实际 "
                  + std::to_wstring(r.weak[i].pct) + L"%）。\n";
        }
    }
    DayJournal j = CheckinStore::Instance().LoadJournal(dateKey);
    j.next = next;
    CheckinStore::Instance().SaveJournal(dateKey, j);
    return true;
}

} // namespace lj
