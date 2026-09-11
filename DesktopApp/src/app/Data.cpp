#include "app/Data.h"
#include "app/AccountStore.h"
#include "app/Store.h"   // CheckinStore::ReloadColumns（账户切换时清空专栏缓存）

namespace lj {

// Howard Hinnant 的 days_from_civil
int DaysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

Date Today()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return Date{ st.wYear, st.wMonth, st.wDay };
}

int DaysUntil(const Date& t)
{
    Date now = Today();
    return DaysFromCivil(t.y, t.m, t.d) - DaysFromCivil(now.y, now.m, now.d);
}

std::wstring FormatDate(const Date& d)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%04d-%02d-%02d", d.y, d.m, d.d);
    return buf;
}

std::wstring WeekdayCN(const Date& d)
{
    static const wchar_t* names[] = { L"星期日", L"星期一", L"星期二", L"星期三",
                                      L"星期四", L"星期五", L"星期六" };
    int days = DaysFromCivil(d.y, d.m, d.d);
    int w = (days + 4) % 7;           // 1970-01-01 是星期四
    if (w < 0) w += 7;
    return names[w];
}

// 当前激活账户内容（账户切换时由 SetActive 写入）
Content Content::s_active;

const Content& Content::Get()
{
    if (s_active.modules.empty()) s_active = BuildDefaultContent(); // 兜底，避免空内容
    return s_active;
}

void Content::SetActive(const Content& c)
{
    s_active = c;
}

// 首页模块导航（通用外壳，所有账户一致）
static std::vector<ModuleEntry> BuildModules()
{
    return {
        { L"No.01", L"checkin",   L"每日打卡", L"按系统日期自动刷新当日任务，勾选即记，跨天清零重来", L"每日", 0 },
        { L"No.02", L"room",      L"自 习 室", L"专注计时 + 背景音乐 + 成员面板 + 公共频道", L"实时", 1 },
        { L"No.03", L"dash",      L"仪 表 盘", L"总时长 / 专注时长 / 表现曲线 / 年度热力图", L"统计", 2 },
        { L"No.04", L"roadmap",   L"专　　栏", L"总路线图改造 · 可自建栏目的长文档区", L"可编辑", 1 },
        { L"No.05", L"materials", L"资料推荐", L"按专业匹配的教材、网课与真题清单", L"参考", 0 },
        { L"No.06", L"media",     L"资 源 库", L"本地音乐 / 课程视频文件夹，供自习室直读即播", L"本地", 2 },
        { L"No.07", L"video",     L"视频图片", L"公共内容广场：用户上传的视频与图片，可播放收藏", L"云端", 0 },
        { L"No.08", L"profile",   L"我　　的", L"头像 / 简介 / 专业 / 生日，以及收藏与浏览历史", L"个人", 1 },
        { L"No.09", L"friend",    L"好　　友", L"好友列表 · 私聊 · 互看打卡与专注进度", L"社交", 2 },
    };
}

// 默认内容 —— 干净默认，等同新用户 / 访客态。
//
// 这里原本放的是一份个人备考种子（考试倒计时、每日打卡表、备考纪律、
// 个人路线图、含本地化渠道与网盘链接的资料清单）。
// 现全部移至 private/ 目录存档（该目录已被 .gitignore 排除），测试时可取回。
// 仓库中的软件不含任何个人信息 —— 任何账户启动都等同于全新安装。
Content Content::BuildDefaultContent()
{
    return BuildShellContent();
}

// 演示账户「我的总线路图」种子正文：把结构化路线图整理成可读长文，
// 作为该账户的第一条、可编辑专栏落地（与网页端「我的总线路图」种子一致）。
std::wstring RoadmapSeedBody()
{
    const Roadmap& r = Content::BuildDefaultContent().roadmap;
    std::wstring s;
    s += L"【三个设计原则】\n";
    for (size_t i = 0; i < r.principles.size(); ++i)
        s += L"  " + std::to_wstring(i + 1) + L". " + r.principles[i] + L"\n";
    s += L"\n【A 段三战线】\n";
    for (const auto& f : r.fronts)
        s += L"  · " + f.name + L" —— " + f.time + L"（" + f.role + L"）\n";
    s += L"\n【决策路径】\n";
    s += L"  判定规则：" + r.decision.rule + L"\n";
    s += L"  分支 A（上岸）：" + r.decision.branchA + L"\n";
    s += L"  分支 B（全灭转研）：" + r.decision.branchB + L"\n";
    s += L"\n【全程月历】\n";
    for (const auto& mo : r.months)
        s += L"  " + mo.m + L"  " + mo.t + L"\n";
    s += L"\n（这是你的第一篇专栏，可随手改写、增删，把它变成真正属于你的总计划。）";
    return s;
}

// 空壳内容：只保留首页模块导航与一句通用资料，用户数据为空（新账户 / 访客启动态）。
// 倒计时、打卡项、路线图、专栏、资料明细都由用户自己添加 —— 与全新安装的体验一致。
Content Content::BuildShellContent()
{
    Content x;
    x.modules    = BuildModules();   // 应用外壳导航始终存在
    x.subtitle   = L"打卡、专注、复盘，都记在这一处。";
    x.dossierNo  = L"自律档案";
    x.coverLines = {
        L"先把今天要做的，摊开看一眼。",
        L"勾掉一项，就少操一份心。",
        L"没做完也没事，明天接着来。",
    };

    // 资料推荐给一份不针对任何具体考试 / 地区的通用底稿，
    // 让新用户有东西可看，又不夹带任何个人信息。
    x.materials.scheme = L"通用备考（默认方案）";
    x.materials.main   = L"先用免费工具跑起来：题库 APP + 纸质真题 + 错题本 + 番茄钟，确认方向后再针对性买课";
    x.materials.budget = L"先用免费的（题库 APP + 真题卷）跑通一轮，再按需投入，通常几百元即可起步。";
    x.materials.categories = {
        { L"通用学习工具（人人适用）", {
            { L"专注法：番茄钟（25+5）",  L"所有学习的基础节奏；配合本应用「自习室」计时使用", L"" },
            { L"错题本：纸质 + 电子双轨",  L"任何考试通用；错题当天整理、每周重做", L"" },
            { L"间隔复习：艾宾浩斯/Anki",  L"记忆类科目（常识、政治、单词）用，碎片时间过", L"" },
            { L"主流题库 APP（免费部分）",  L"题库 + 模考 + 错题本一体，先摸清自己的强弱项", L"" },
            { L"B站 / 知乎 方法经验贴",     L"先看方法论与经验，再决定要不要花钱买课", L"" },
        }},
        { L"真题与题库（任何考试通用）", {
            { L"近 3—5 年真题",         L"真题是唯一权威的练习材料，反复做、逐题归因", L"" },
            { L"纸质真题卷",            L"全真模考涂卡手感，按套购买即可", L"" },
            { L"留最近 1—2 套真题",      L"最后冲刺模考用，之前别做", L"" },
        }},
        { L"选岗 / 择校信息渠道", {
            { L"官方公告与职位表 / 招生简章", L"唯一权威来源，以官方发布为准", L"" },
            { L"目标单位或院校的官网",       L"查历史数据、报录比、专业目录", L"" },
        }},
    };
    x.materials.pitfalls = {
        L"别囤资料——一套主资料 + 真题就够，囤资料=焦虑。",
        L"别同时跟两家系统课——方法冲突，考场犹豫。",
        L"只做真题，少碰模拟题——模拟题思路偏。",
        L"先摸清强弱项再投入：把时间给提分最快的模块。",
        L"资料只是辅助，打卡执行才见效——每天在本应用勾掉计划。",
    };
    return x;
}

// 依据当前账户载入内容。
// 仓库默认不含任何个人种子，因此所有账户（含演示账户）都走空壳内容，
// 体验与全新安装 / 访客态一致。个人种子见 private/seed_personal.cpp。
void Content::ApplyCurrentAccount()
{
    auto& acc = AccountStore::Instance();
    Content c = BuildShellContent();
    // 倒计时每账户独立：由用户在「自定义管理 · 关键倒计时」自行添加
    c.milestones = CheckinStore::Instance().LoadMilestones();
    SetActive(c);
    // 账户切换：丢弃旧账户的专栏内存缓存，下次进入专栏按新账户根目录重读
    CheckinStore::Instance().ReloadColumns();
}

// ---------------- 打卡项分组 ----------------
ChecklistBundle DefaultChecklist()
{
    ChecklistBundle b;
    const auto& C = Content::Get();
    b.daily.reserve(C.checklist.size());
    for (size_t i = 0; i < C.checklist.size(); ++i) {
        CheckItem it = C.checklist[i];
        it.id = L"d" + std::to_wstring(i);
        b.daily.push_back(std::move(it));
    }
    // sat / sun 留空：用户在管理页自行添加（工作日清单已覆盖主线）
    return b;
}

std::vector<CheckItem> ItemsForDate(const ChecklistBundle& b, const Date& d)
{
    std::vector<CheckItem> out = b.daily;
    int wd = (DaysFromCivil(d.y, d.m, d.d) + 4) % 7;   // 0=周日 … 6=周六
    if (wd == 6) for (const auto& x : b.sat) out.push_back(x);
    if (wd == 0) for (const auto& x : b.sun) out.push_back(x);
    return out;
}

} // namespace lj
