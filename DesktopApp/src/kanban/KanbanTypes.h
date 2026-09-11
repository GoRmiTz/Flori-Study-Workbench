#pragma once
// ============================================================
//  KanbanTypes.h — 看板娘共享数据类型（settings schema）
//  NOTE: 看板娘运行时已归档至 _attic/kanban_runtime（暂停开发）；
//        本头文件仅为 Store/SettingsView/练考凭据链路保留，自包含无依赖。
//  状态组、动作代码协议、人格、养成属性、每日上下文。
//  全部零第三方依赖；命名前缀 lvjing（与美术资源/motion 协议一致）。
// ============================================================
#include <string>
#include <vector>

namespace lj {
namespace kanban {

// ---------------- 状态组（互斥顶层）----------------
enum class StateGroup : int
{
    Idle = 0,     // 空闲待机
    Focus,        // 专注组
    Meal,         // 三餐组
    Sleep,        // 睡眠组
    Curious,      // 好奇/发现摸鱼
    Talk,         // 对话中
};

inline const wchar_t* StateGroupName(StateGroup g)
{
    switch (g) {
        case StateGroup::Idle:    return L"idle";
        case StateGroup::Focus:   return L"focus";
        case StateGroup::Meal:    return L"meal";
        case StateGroup::Sleep:   return L"sleep";
        case StateGroup::Curious: return L"curious";
        case StateGroup::Talk:    return L"talk";
    }
    return L"idle";
}

// ---------------- 动作代码协议 [act:<group>:<motion>] ----------------
//  group 决定状态组；motion 是该组内的子动作名（与状态组与动作表.md 对齐）。
//  离线状态组直接用内部枚举派发同等的 motion，不经过 AI。
struct MotionCode
{
    StateGroup group = StateGroup::Idle;
    std::wstring motion;   // 子动作名，如 L"write" / L"cheer" / L"timer" / L"peek" / L"hungry" ...
};

// 解析 AI 回复尾部的 [act:<group>:<motion>]；无则返回 nullopt 语义（group=Idle）。
// 文本中若含该标签，outText 会被截掉标签（只留说话内容）。
bool ParseActTag(const std::wstring& inText, std::wstring& outText, MotionCode& outAct);

// ---------------- 人格（P2 双情感模式）----------------
enum class Persona : int
{
    Tsundere = 0,   // 傲娇监督型
    Cute = 1,       // 天真可爱陪伴型
};

inline const wchar_t* PersonaName(Persona p)
{
    return p == Persona::Tsundere ? L"tsundere" : L"cute";
}
inline Persona PersonaFromName(const std::wstring& s)
{
    return (s == L"cute") ? Persona::Cute : Persona::Tsundere;
}

// ---------------- 今日情绪（影响口吻，每日重算）----------------
enum class Emotion : int
{
    Calm = 0,       // 平静（默认/离线）
    Proud,          // 骄傲
    Concerned,      // 担忧
    Joyful,         // 雀跃
    Caring,         // 关切
};

// ---------------- 养成四属性（P3）----------------
struct Attributes
{
    int fullness = 70;   // 饱腹度 0..100
    int bond     = 50;   // 亲密度 0..100
    int energy   = 70;   // 活力 0..100
    int mood     = 60;   // 情绪 0..100（与 Emotion 标签对应，数值化便于 AI 读取）

    void Clamp() {
        auto cl = [](int& v) { if (v < 0) v = 0; if (v > 100) v = 100; };
        cl(fullness); cl(bond); cl(energy); cl(mood);
    }
};

// ---------------- 每日上下文文档（P2-5，纯数值/事实，红线：绝不含窗内文本/路径/URL）----------------
struct DailyContext
{
    Persona     persona = Persona::Tsundere;
    int         checkinPct = 0;       // 打卡完成率 %
    int         checkinTarget = 80;   // 目标 %
    int         focusMin = 0;         // 今日专注分钟
    int         focusLongestMin = 0;  // 连续最长分钟
    std::wstring curState;            // 当前状态（中文简述，不含进程标题）
    int         fullness = 70, bond = 50, energy = 70, mood = 60;
    std::wstring recentEvent;         // 近期事件（聚合事实，如「午休后未打卡」）
    int         tokenUsed = 0;        // 今日已用 token
    int         tokenBudget = 4000;   // 今日上限
    std::wstring clock;               // 当前时间 HH:MM
};

// ---------------- 看板娘运行配置（来自 AppSettings.kanban）----------------
//  红线：apiKey 仅本地面板配置，不硬编码、不进仓库。
struct KanbanSettings
{
    bool        enabled = true;             // 总开关
    int         x = -1, y = -1;             // 皮套窗口位置（屏幕坐标；-1=默认右下角）
    float       zoom = 1.0f;                // 模型自定义缩放（0.4~3.0，1=默认）
    Persona     persona = Persona::Tsundere;// 0=傲娇监督 / 1=天真可爱
    bool        dnd = false;                // 免打扰（全局静默）
    int         activeFrom = 8 * 60;        // 活跃时间窗起点（分钟，默认 08:00）
    int         activeTo   = 23 * 60;       // 活跃时间窗终点（分钟，默认 23:00）
    int         dailyTokenBudget = 4000;    // 每日 Token 额度
    std::wstring apiBase;                   // OpenAI 兼容端点（空=未配置，走本地兜底）
    std::wstring apiKey;                    // API 密钥（仅本地面板，不入库）
    std::wstring model = L"deepseek-chat";   // 默认 DeepSeek 对话模型
    bool        firstRun = true;            // 首次启动（用于引导/默认）
};

// ---------------- 看板娘资源/位置配置（来自 assets/kanban/kanban.cfg，用户可直接编辑）----------------
//  与 AppSettings（运行期用户设置）分离：这里是「换模型 / 调停靠锚点」的即插即用配置，
//  改文件重启即生效，无需重新编译。value 可含中文（如模型目录 米雪儿_vts）。
//  目的：把「选哪个模型」「默认停靠在哪」从写死的代码常量改为可编辑配置——
//  用户换模型 / 调位置像插件一样即插即用，不重编代码、不写死逻辑。
struct KanbanConfig
{
    std::wstring model;                     // 角色资源标识（预留给未来可插拔后端；PngBackend 当前固定用 assets/kanban/lvjing.png）；留空=默认
    std::wstring anchor = L"bottom-right";  // 默认停靠锚点：bottom-right / bottom-left / top-right / top-left / center
    int offsetX = 12;                       // 锚点内缩偏移（像素）
    int offsetY = 12;
};

// 读取 <baseDir>\assets\kanban\kanban.cfg（UTF-8 行式 key=value，# 注释）。
// 文件不存在/解析失败 → 返回 false，调用方使用默认值。
bool LoadKanbanConfig(const std::wstring& baseDir, KanbanConfig& out);

} // namespace kanban
} // namespace lj
