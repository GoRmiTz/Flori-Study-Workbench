#pragma once
// ============================================================
//  Data.h — 内容单一数据源
//  对应 WebApp/assets/js/data.js：改文案不动结构
// ============================================================
#include "core/Common.h"

namespace lj {

// ---------- 日期工具 ----------
struct Date
{
    int y = 2026, m = 1, d = 1;
};

int  DaysFromCivil(int y, unsigned m, unsigned d);   // 天数序列号
// 天序号 -> 公历日期（Hinnant civil_from_days 逆运算；内联以便各视图直接调用）
inline Date DateFromCivil(int z)
{
    z += 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1;
    int m = mp + (mp < 10 ? 3 : -9);
    y += (m <= 2);
    return { y, m, d };
}
Date Today();
int  DaysUntil(const Date& target);                  // 距今天还有几天（可为负）
std::wstring FormatDate(const Date& d);              // 2026-11-29
std::wstring WeekdayCN(const Date& d);               // 星期一

// ---------- 倒计时节点 ----------
struct Milestone
{
    std::wstring label;
    std::wstring note;
    Date date;
    bool urgent = false;
};

// ---------- 模块入口 ----------
struct ModuleEntry
{
    std::wstring index;
    std::wstring id;        // 路由 id
    std::wstring title;
    std::wstring subtitle;
    std::wstring meta;
    int accent = 0;         // 0 朱砂 1 黄铜 2 青
};

// ---------- 打卡项 ----------
struct CheckItem
{
    std::wstring slot;      // 时段，如 "09:00"，可空（随时）
    std::wstring title;
    std::wstring standard;  // 完成标准（任务卡「怎么学」）
    std::wstring tag;       // 分类标签：主线 / 英语 / 申论 / 常识 / 作息 / 手绘 …
    int minutes = 30;
    bool done = false;      // 仅运行时用，不持久化
    std::wstring id;        // 稳定标识（自定义项用，空白处回退到 title 作 key）
    std::wstring link;      // 外链（任务卡「去哪学」，可选）
    std::wstring folder;    // 本地文件夹（可选）
    std::wstring Key() const { return id.empty() ? title : id; }  // 用于勾选/匹配
};

// ---------- 打卡项分组（自定义管理页） ----------
struct ChecklistBundle
{
    std::vector<CheckItem> daily;   // 工作日（每日固定）
    std::vector<CheckItem> sat;     // 周六专项
    std::vector<CheckItem> sun;     // 周日专项
};

// 默认打卡项：首次运行 / 未自定义时由静态清单播种（daily=清单，sat/sun 留空）
ChecklistBundle DefaultChecklist();
// 按日期取当日生效列表（周末并上 sat/sun）
std::vector<CheckItem> ItemsForDate(const ChecklistBundle& b, const Date& d);

// ---------- 专栏 · 每账户私有可编辑长文（对应 WebApp ns_columns） ----------
//  每个账户在自己的目录 accounts/<name>/columns.json 维护一份专栏列表；
//  存在个人种子时账户首次进入播种「我的总线路图」一条，默认无种子则从空白开始，
//  与网页端一致：内容不跨用户共享、切换账户互不可见、均可编辑。
struct Column
{
    std::wstring id;          // 稳定标识
    std::wstring title;       // 标题
    std::wstring body;        // 正文（纯文本 + 换行，供预览/列表摘要）
    std::string  bodyRtf;     // 正文富文本 RTF（字节流，承载加粗/字号等格式）
    std::wstring author;      // 作者（账户名）
    long long    createdAt = 0;
    long long    updatedAt = 0;
};
// 账户「我的总线路图」种子正文（由 BuildDefaultContent().roadmap 格式化而来；
// 默认空壳时返回带空章节的骨架，仅在有个人种子时才有实质内容）
std::wstring RoadmapSeedBody();

// ---------- 专栏 · 总线路图（结构化路线图，默认空；可由个人种子填充） ----------
struct RoadmapFront
{
    std::wstring name;      // 战线名
    std::wstring time;      // 时间
    std::wstring role;      // 定位
    int accent = 0;         // 0 朱砂 1 黄铜 2 青
};
struct RoadmapMonth
{
    std::wstring m;         // 月份标签，如 2026.7
    std::wstring t;         // 说明
};
struct RoadmapDecision
{
    std::wstring rule;      // 判定规则
    std::wstring branchA;   // 分支 A
    std::wstring branchB;   // 分支 B
};
struct Roadmap
{
    std::vector<std::wstring> principles;   // 三个设计原则
    std::vector<RoadmapFront>   fronts;     // A 段战线
    std::vector<RoadmapMonth>   months;     // 全程月历
    RoadmapDecision decision;               // 决策点
};

// ---------- 资料推荐（默认给一份通用备考底稿，不含任何个人 / 地区信息） ----------
struct MaterialItem
{
    std::wstring t;      // 标题
    std::wstring d;      // 描述
    std::wstring url;    // 可选资源链接（外链）
};
struct MaterialCat
{
    std::wstring name;              // 分类名
    std::vector<MaterialItem> items;
};
struct Materials
{
    std::wstring scheme;            // 当前方案（专业 · 学校）
    std::wstring main;              // 一句话主推
    std::wstring budget;            // 预算参考
    std::vector<MaterialCat>    categories;   // 分类（系统课 / 分科名师 / 申论 …）
    std::vector<std::wstring>    pitfalls;     // 避坑
};

// ---------- 全局内容 ----------
struct Content
{
    std::wstring appName = L"芙洛理";
    std::wstring appNameLatin = L"Flori";
    std::wstring subtitle = L"自律 · 规划 · 专注，一处归档";
    std::wstring dossierNo = L"DOSSIER · 自律档案";

    std::vector<Milestone>   milestones;
    std::vector<ModuleEntry> modules;
    std::vector<CheckItem>   checklist;
    std::vector<std::wstring> disciplines;   // 执行纪律
    std::vector<std::wstring> coverLines;    // 封面段落
    Roadmap               roadmap;           // 专栏 · 总线路图
    Materials              materials;        // 资料推荐

    static const Content& Get();
    static void SetActive(const Content& c);          // 切换账户时写入当前内容
    // 干净默认内容（等同新用户 / 访客态，不含任何个人信息）。
    // 个人种子已移至 private/seed_personal.cpp 存档，测试时可取回。
    static Content BuildDefaultContent();
    // 仅首页模块导航 + 通用资料，用户数据为空（新账户 / 访客启动态）
    static Content BuildShellContent();
    static void ApplyCurrentAccount();                // 依据 AccountStore 当前账户载入内容

private:
    static Content s_active;                           // 当前激活账户内容
};

} // namespace lj
