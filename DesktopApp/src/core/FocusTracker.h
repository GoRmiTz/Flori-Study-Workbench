#pragma once
// ============================================================
//  FocusTracker.h — 桌面端「前台学习」自动计时
//  后台线程每秒轮询：前台窗口进程/标题 + 用户 idle 状态，
//  命中学习白名单且非 idle 时累计专注秒数，满 60s 或会话结束
//  时通过 CheckinStore 写入 focus.json。
//  P1-5 桌面独占场景（护城河）核心模块。
// ============================================================
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <windows.h>

namespace lj {

// 前台进程分类：决定「记了什么」与是否计入专注
enum class FocusCat : int { Unspecified = 0, Study = 1, Entertainment = 2, Idle = 3, Other = 4 };

struct FocusState
{
    std::wstring process;   // 前台进程名（小写，不含路径）
    std::wstring title;     // 前台窗口标题
    bool studying = false;  // 前台为学习应用且用户未 idle
    bool idle = false;      // 用户超过阈值无输入
    int  continuousSec = 0; // 当前连续专注秒数
    int  category = 0;      // FocusCat：学习/娱乐/挂机/其他（满足「记了什么」与按类型分桶）
};

class FocusTracker
{
public:
    static FocusTracker& Instance();

    void Start();           // 启动后台轮询线程
    void Stop();            // 停止线程并 flush 当前会话
    void Flush();           // 将已累积的专注分钟写入 CheckinStore

    FocusState Snapshot() const;
    // 最近一次「非芙洛理」的前台进程名（小写）。用户在芙洛理里点按钮时前台必然是芙洛理自身，
    // 所以「一键加入刚才用的应用」要取这个值，而不是 Snapshot().process。
    std::wstring LastOtherApp() const;

    void SetIdleThresholdMs(DWORD ms) { m_idleMs = ms; }
    void SetStudyApps(std::vector<std::wstring> names); // 内置进程白名单（小写）
    void SetStudyKeywords(std::vector<std::wstring> kw); // 窗口标题关键词（小写）

    // ---- #71 用户专注白名单（settings.json 持久化，自习室页可编辑）----
    // 语义：名单非空时「只认名单」——前台进程命中即视为专注（不再要求标题关键词，
    // 因此看老师视频、播放本地课件都能正常计时），名单之外一律非专注 → 番茄钟自动暂停。
    // 名单为空则回退内置智能判定（SetStudyApps + SetStudyKeywords）。
    void SetUserApps(std::vector<std::wstring> names);
    std::vector<std::wstring> UserApps() const;
    // 内置默认名单（首次运行播种 / 用户点「恢复默认」）
    static std::vector<std::wstring> DefaultUserApps();
    // 从 settings.json 读用户名单；文件里从未出现该键（首次运行）则播种默认并落盘
    static std::vector<std::wstring> LoadUserAppsFromSettings();
    // 落盘用户名单到 settings.json（保留 dark 等其它字段）并即时生效
    void SaveUserApps(const std::vector<std::wstring>& names);

    // ---- 娱乐降权名单（游戏 / 视频站：记但不计入专注）----
    void SetEntApps(std::vector<std::wstring> names);
    std::vector<std::wstring> UserEntApps() const;
    static std::vector<std::wstring> DefaultEntApps();
    static std::vector<std::wstring> LoadEntAppsFromSettings();
    void SaveEntApps(const std::vector<std::wstring>& names);

    bool Running() const { return m_running; }

    // ---- F-D3 暂停 / 恢复前台专注自动计时 ----
    void Pause();            // 暂停累计（立即结束进行中的会话并落盘）
    void Resume();           // 恢复累计
    bool Paused() const { return m_paused; }

private:
    FocusTracker() = default;
    ~FocusTracker() { Stop(); }

    void WorkerLoop();

    static std::wstring BaseName(const std::wstring& path);
    static std::wstring ToLower(std::wstring s);
    bool IsStudyWindow(const std::wstring& proc, const std::wstring& title) const;
    bool MatchesEnt(const std::wstring& proc) const;
    void FlushCategoryLocked(int cat, int& sec, long long& start, bool& inSession,
                             const std::wstring& proc, const std::wstring& tag); // 持有 m_stateMu 时调用

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_requestStop{ false };
    std::atomic<bool> m_paused{ false };   // F-D3：暂停前台专注自动计时
    std::thread m_thread;

    DWORD m_idleMs = 60000; // 默认 60s 无输入视为 idle

    mutable std::mutex m_cfgMu;
    std::vector<std::wstring> m_studyApps;
    std::vector<std::wstring> m_studyKeywords;
    std::vector<std::wstring> m_userApps;   // #71 用户白名单（优先级最高，仅比进程名）
    std::vector<std::wstring> m_entApps;    // 娱乐降权名单（游戏 / 视频站）

    mutable std::mutex m_stateMu;
    FocusState m_state;
    std::wstring m_lastOther;   // 最近一次非芙洛理前台进程名

    // 学习会话（计入专注）
    int       m_studySec = 0;
    long long m_studyStart = 0;
    bool      m_inStudy = false;
    // 娱乐会话（记但不计入专注）
    int       m_entSec = 0;
    long long m_entStart = 0;
    bool      m_inEnt = false;
    std::wstring m_entProc;    // 当前娱乐段进程名（落盘用）
};

} // namespace lj
