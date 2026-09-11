#pragma once
// ============================================================
//  Metrics.h — P2-1 留存指标（需求验证用，纯派生、无副作用）
//  清单原文：先小范围验证「桌面常驻自习室」真实留存（DAU / 时长 /
//  次日留存），指标优先于功能扩张；拿到百级用户真实曲线作为是否做
//  移动端的决策依据。
//
//  单机本地端没有「百级独立用户」，但每个账户已落盘 focus.json /
//  checkin 勾选，足以派生留存口径的等价信号：
//    · 活跃日（DAU-proxy）   = 当日有专注会话或任意打卡的天
//    · 时长                   = 当日专注分钟（focus.json.min 之和）
//    · 次日留存               = 活跃日 d 之后 d+1 仍活跃的比例
//    · 7 日留存               = 活跃日 d 之后 d+7 仍活跃的比例
//  真实百级分账户曲线需先有分发（见 P2-3）；本助手只把口径算清楚、
//  把单机信号算出来，供真机 / 服务端聚合时直接对齐。
// ============================================================
#include <map>
#include <set>
#include <string>
#include "app/Data.h"
#include "app/Store.h"

namespace lj {

struct RetentionMetrics
{
    int   activeDaysLast30   = 0;   // 近 30 天活跃天数（DAU-proxy 的分子）
    int   totalFocusMin30    = 0;   // 近 30 天累计专注分钟
    int   avgDailyFocusMin   = 0;   // 活跃日均专注（totalFocus / activeDays）
    float nextDayRetention   = 0.0f;// 次日留存率 [0,1]
    float d7Retention        = 0.0f;// 7 日留存率 [0,1]
};

inline RetentionMetrics ComputeRetention()
{
    RetentionMetrics out;
    auto& cs = CheckinStore::Instance();

    // 活跃日集合：近 90 天（次日/7日留存需前瞻，故取窗更长）
    std::set<std::wstring> active;
    auto focus = cs.LoadFocus();
    for (auto& f : focus) if (!f.date.empty()) active.insert(f.date);
    auto keys = cs.LastNDays(90);
    for (auto& k : keys) {
        auto m = cs.LoadDay(k);
        for (auto& kv : m) if (kv.second) { active.insert(k); break; }
    }

    // 近 30 天指标
    Date td = Today();
    int zt = DaysFromCivil(td.y, td.m, td.d);
    int total30 = 0, focus30 = 0;
    for (int off = 0; off < 30; ++off) {
        std::wstring key = FormatDate(DateFromCivil(zt - off));
        if (active.count(key)) total30++;
    }
    for (auto& f : focus) {
        // 判断 f.date 是否落在近 30 天
        int zf = 0;
        if (f.date.size() >= 10) {
            int y = 0, mth = 0, d = 0;
            if (swscanf_s(f.date.c_str(), L"%d-%d-%d", &y, &mth, &d) == 3)
                zf = DaysFromCivil(y, mth, d);
        }
        if (zf > 0 && (zt - zf) >= 0 && (zt - zf) < 30) focus30 += f.min;
    }
    out.activeDaysLast30 = total30;
    out.totalFocusMin30  = focus30;
    out.avgDailyFocusMin = total30 > 0 ? focus30 / total30 : 0;

    // 次日 / 7 日留存：遍历活跃日，看其后 1 / 7 天是否仍活跃
    int nNext = 0, hitNext = 0, n7 = 0, hit7 = 0;
    for (auto& k : active) {
        int y = 0, mth = 0, d = 0;
        if (swscanf_s(k.c_str(), L"%d-%d-%d", &y, &mth, &d) != 3) continue;
        int z = DaysFromCivil(y, mth, d);
        if (zt - z < 1)  continue;          // 太近，无法判次日
        // 次日
        std::wstring kn = FormatDate(DateFromCivil(z + 1));
        nNext++; if (active.count(kn)) hitNext++;
        // 7 日（仅统计 7 天前及更早的活跃日，避免边界噪声）
        if (zt - z >= 7) {
            std::wstring k7 = FormatDate(DateFromCivil(z + 7));
            n7++; if (active.count(k7)) hit7++;
        }
    }
    out.nextDayRetention = nNext > 0 ? (float)hitNext / (float)nNext : 0.0f;
    out.d7Retention      = n7    > 0 ? (float)hit7    / (float)n7    : 0.0f;
    return out;
}

} // namespace lj
