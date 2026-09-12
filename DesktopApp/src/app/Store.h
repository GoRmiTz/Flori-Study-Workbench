#pragma once
// ============================================================
//  Store.h — 打卡档案本地持久化
//  按日期键（YYYY-MM-DD）存取当日勾选集合，落盘为
//  <exe 目录>/archive/checkin.json（UTF-8，极简手写 JSON）。
//  单例：CheckinStore::Instance()。
// ============================================================
#include <string>
#include <map>
#include <vector>
#include "app/Data.h"
#include "app/AccountStore.h"

namespace lj {

// ---------------- 专注会话 ----------------
struct FocusSession
{
    std::wstring date;     // YYYY-MM-DD
    long long    start = 0; // 开始 epoch 秒
    long long    end = 0;   // 结束 epoch 秒
    int          min = 0;   // 时长（分钟）
    std::wstring tag;       // 标签，如「自习室」
    int          category = 0;  // FocusCat：0/1 学习，2 娱乐降权（F-D1 类型分桶）
    std::wstring proc;      // 前台进程名（小写，F-D1「记了什么」）
};

// ---------------- 应用设置（settings.json，按账户隔离）----------------
struct AppSettings
{
    bool dark = false;      // 主题：夜间档案室

    // ---- 专注白名单（#71 升级）----
    // 前台进程名（小写，不含 .exe / 路径）。番茄钟联动前台时：
    //   · 前台进程命中名单 → 视为专注，倒计时继续（看网课视频、刷题网站都算）
    //   · 前台进程在名单之外 → 自动暂停
    // 名单为空 = 回退内置智能判定（进程 + 标题关键词），避免用户清空后全程暂停。
    std::vector<std::wstring> focusApps;
    bool focusAppsSeeded = false;   // 已播种过默认名单（区分「首次运行」与「用户主动清空」）
    std::vector<std::wstring> focusEntApps;  // 娱乐降权名单（游戏/视频站，F-D1）
    bool focusEntSeeded = false;    // 已播种过默认娱乐名单

    // ---- 全局热键（F-D3）----
    // mod = MOD_CONTROL | MOD_ALT 等位掩码；vkey = 虚拟键码（如 'C' / VK_F9）。
    // vkey==0 表示未配置（App 注册时用内置默认组合）。
    struct HotkeyCombo { unsigned int mod = 0; unsigned int vkey = 0; };
    HotkeyCombo hotCheckin;     // 一键打卡
    HotkeyCombo hotPomodoro;    // 番茄钟切换
    HotkeyCombo hotPause;       // 暂停 / 恢复专注计时

    // ---- 申论字数统计（F-D4 本地文件集成）----
    // 默认关闭（隐私红线）；授权后仅存用户显式选择的 docx 路径，统计字数，正文不出端。
    bool docxEnabled = false;                        // 是否已授权
    std::vector<std::wstring> docxPaths;             // 用户授权的申论文档路径

    // ---- 复盘每日 nudge（F-D7 增强：自动托盘提醒）----
    // 纯本地、不联网；到点汇总当日复盘并弹托盘气泡，点击跳转复盘页。
    bool        reviewNudge = true;      // 是否开启每日自动提醒（默认开）
    int         reviewNudgeHour = 21;    // 提醒时刻（24h，默认 21:00 收工前）
    std::wstring reviewNudgeLast;        // 已提醒日期 YYYY-MM-DD（跨重启去重）

    // ---- 关闭按钮行为 ----
    // 0 = 每次询问（弹自绘确认层）1 = 直接退出 2 = 最小化到托盘
    int exitAction = 0;
};

// ---------------- 每日复盘（journal.json）----------------
struct DayJournal
{
    std::wstring summary;   // 今日小结
    std::wstring next;      // 明日三要事
    bool Empty() const { return summary.empty() && next.empty(); }
};

// ---------------- 作息记录（rhythm.json）----------------
//  勾选「作息」类打卡项时自动落时刻，用于仪表盘作息环形图。
struct DayRhythm
{
    int wake  = -1;         // 起床时刻（当天第几分钟 0..1439），-1 = 未记录
    int sleep = -1;         // 就寝时刻
    bool Empty() const { return wake < 0 && sleep < 0; }
};

// ---------------- 收藏（favorites.json，按账户隔离）----------------
//  跨内容类型：专栏(column) / 视频(video) / 图片(image)
struct Favorite
{
    std::wstring kind;   // "column" | "video" | "image"
    std::wstring id;
    std::wstring title;
    std::wstring author;
    long long    ts = 0;
};

// ---------------- 历史（history.json，按账户隔离）----------------
//  最近浏览的专栏 / 最近播放的媒体（视频、图片、音频）
struct HistoryItem
{
    std::wstring kind;   // "column" | "video" | "image" | "audio"
    std::wstring id;
    std::wstring title;
    long long    ts = 0;
};

// ---------------- 知识库（跨窗口拖拽收集的考点，落盘 accounts/<name>/knowledge.json）----------------
struct KCard
{
    std::wstring id;       // 稳定标识
    std::wstring title;    // 首行截断（≤48 字）
    std::wstring body;     // 全文
    std::wstring source;   // 来源说明（如「跨窗口拖入」）
    std::wstring tags;     // 标签（可空）
    long long    ts = 0;   // 收集时间（Unix 秒）
};

// ---------------- 模考报告（考场模式，落盘 accounts/<name>/exam_reports.json）----------------
struct ExamReport
{
    long long startTime   = 0;   // 开始 epoch 秒
    int       plannedMin  = 0;   // 计划时长（分钟）
    int       actualSec   = 0;   // 实际用时（秒）
    int       effectiveSec = 0;  // 有效专注（秒）= 实际 - 中断累计
    int       interrupts  = 0;   // 中断次数（切到非学习进程的段数）
    bool      abandoned   = false; // 是否中途放弃
};

class CheckinStore
{
public:
    static CheckinStore& Instance();

    // 读取某天勾选集合：title -> 是否勾选
    std::map<std::wstring, bool> LoadDay(const std::wstring& dateKey);
    // 写入某天勾选集合
    void SaveDay(const std::wstring& dateKey, const std::map<std::wstring, bool>& done);

    // 最近 n 天日期键，索引 0 = n-1 天前 … 末尾 = 今天
    std::vector<std::wstring> LastNDays(int n);

    // ---------------- 专注会话（落盘 archive/focus.json）----------------
    std::vector<FocusSession> LoadFocus();
    void AddFocus(const FocusSession& s);

    // ---------------- 自定义打卡项（落盘 archive/items.json）------------
    // 返回已保存的分组；无存档时回退默认清单（DefaultChecklist）。
    ChecklistBundle LoadItems();
    void SaveItems(const ChecklistBundle& b);
    void ResetItems();   // 清除自定义，回退默认

    // ---------------- 应用设置（落盘 settings.json）--------------------
    AppSettings LoadSettings();
    void SaveSettings(const AppSettings& s);

    // ---------------- 关键倒计时（落盘 accounts/<name>/milestones.json）--------
    //  每个账户独立维护：演示账户首次进入用种子播种，其余账户从空白开始，
    //  与「其他用户的档案首页也需要自定义倒计时」一致（非演示账户无内置种子）。
    std::vector<Milestone> LoadMilestones() const;
    void SeedMilestones(const std::vector<Milestone>& seed);   // 文件缺失才写入
    void SaveMilestones(const std::vector<Milestone>& ms);     // 落盘并打脏上推

    // ---------------- 收藏 / 历史（落盘 accounts/<name>/favorites.json / history.json）--------
    //  个人界面「我的」使用：收藏跨内容类型（专栏/视频/图片），历史记录最近浏览/播放。
    std::vector<Favorite>  LoadFavorites() const;
    bool IsFav(const std::wstring& kind, const std::wstring& id) const;
    void ToggleFav(const std::wstring& kind, const std::wstring& id,
                   const std::wstring& title, const std::wstring& author);
    std::vector<HistoryItem> LoadHistory() const;
    void PushHistory(const std::wstring& kind, const std::wstring& id, const std::wstring& title);

    // ---------------- 每日复盘（落盘 journal.json）----------------------
    DayJournal LoadJournal(const std::wstring& dateKey);
    void SaveJournal(const std::wstring& dateKey, const DayJournal& j);
    // 全部复盘，按日期升序；仅返回非空条目
    std::vector<std::pair<std::wstring, DayJournal>> LoadJournals();

    // ---------------- 作息记录（落盘 rhythm.json）-----------------------
    std::map<std::wstring, DayRhythm> LoadRhythms();
    DayRhythm LoadRhythm(const std::wstring& dateKey);
    // sleepSide=false 记起床，true 记就寝；minuteOfDay 为当天分钟数
    void MarkRhythm(const std::wstring& dateKey, bool sleepSide, int minuteOfDay);

    // ---------------- 备份：整账户导出 / 导入 ---------------------------
    //  单文件 JSON（app/version/account/exportedAt + 六个数据块）
    bool ExportAll(const std::wstring& path, std::wstring& err);
    bool ImportAll(const std::wstring& path, std::wstring& err);

    // 导入前的「看一眼」：不落盘，只解析出这份备份是谁的、什么时候导的、含几块。
    // 用来在覆盖前把真相摆给用户看 —— 导入是整账户替换，选错文件不可逆。
    struct BackupInfo {
        std::wstring account;     // 备份来源账户名
        long long    exportedAt = 0;  // 导出时间（Unix 秒）
        int          blocks = 0;      // 可恢复的数据块数量
    };
    bool PeekBackup(const std::wstring& path, BackupInfo& info, std::wstring& err);

    // 覆盖前自动存一份当前状态到账户目录，导错了还能捞回来。
    // 成功返回落盘路径，失败返回空串（不阻断导入，只是没有后悔药）。
    std::wstring SaveRollbackSnapshot();

    // ---------------- 云同步载荷（与导出文件同构，不落盘）---------------
    //  Cloud 层直接拿这份字符串上推 / 把服务端响应喂回来，磁盘备份与云同步
    //  共用同一套 schema，避免两条路径各自演化。
    //  includeEmpty=false 时跳过「本地根本不存在」的数据块 —— 上推时至关重要：
    //  新装的机器 items.json 尚未生成，若补一个空块推上去，服务端 items 的
    //  replace 语义会把云端辛苦定制的清单直接抹平。
    std::string BuildPayload(bool includeEmpty = true);
    //  changed 回传「是否真的改动了本地文件」。云同步每次都会回拉一份合集，
    //  绝大多数时候内容与本地一致；只有真变了才值得让界面重建缓存，
    //  否则用户每点一次打卡、2.5 秒后滚动位置就被弹回顶部。
    bool ApplyPayload(const std::string& json, std::wstring& err, bool* changed = nullptr);

    // ---------------- 专栏（每账户私有、可编辑，落盘 accounts/<name>/columns.json）--------
    //  对应网页端「ns_columns」：内容存于本账户命名空间，不跨用户共享；
    //  存在个人种子时账户首次进入播种「我的总线路图」一条，默认无种子则从空白开始。
    std::wstring ColumnsFilePath() const;
    // 含演示账户播种；按账户根缓存，避免每帧重读
    std::vector<Column> LoadColumns();
    void SaveColumns(const std::vector<Column>& cols);   // 落盘并打脏上推
    void ReloadColumns();                                // 账户切换时清空缓存

    // ---------------- 知识库（跨窗口拖拽收集的考点，落盘 accounts/<name>/knowledge.json）--------
    std::vector<KCard> LoadKnowledge();
    void SaveKnowledge(const std::vector<KCard>& cards);
    void AddKnowledge(const KCard& c);
    void RemoveKnowledge(const std::wstring& id);

    // ---------------- 模考报告（考场模式，落盘 accounts/<name>/exam_reports.json）--------
    std::vector<ExamReport> LoadExamReports();
    void SaveExamReports(const std::vector<ExamReport>& v);
    void AddExamReport(const ExamReport& r);
    ExamReport LastExamReport();   // 最近一条（空则返回默认）

    // 账户切换时重置内存态（重新从当前账户目录读取）
    void Reload();

private:
    CheckinStore() = default;

    std::wstring FilePath() const;
    std::wstring FocusFilePath() const;
    std::wstring ItemsFilePath() const;
    std::wstring SettingsFilePath() const;
    std::wstring JournalFilePath() const;
    std::wstring RhythmFilePath() const;
    std::wstring MilestonesFilePath() const;
    std::wstring FavoritesFilePath() const;
    std::wstring HistoryFilePath() const;
    std::wstring KnowledgeFilePath() const;   // F-D5 知识库
    std::wstring ExamReportsFilePath() const;  // F-D6 考场模式
    void EnsureLoaded();
    void LoadAll(std::map<std::wstring, std::map<std::wstring, int>>& out);
    void WriteAll(const std::map<std::wstring, std::map<std::wstring, int>>& all);

    ChecklistBundle ParseItems(const std::string& buf) const;
    std::string   SerializeItems(const ChecklistBundle& b) const;

    bool m_loaded = false;
    std::map<std::wstring, std::map<std::wstring, int>> m_all;

    // 专栏缓存（按账户根）
    bool                 m_colsLoaded = false;
    std::wstring         m_colsRoot;
    std::vector<Column>  m_columns;
};

} // namespace lj
