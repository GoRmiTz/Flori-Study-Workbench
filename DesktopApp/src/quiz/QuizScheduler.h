#pragma once
// ============================================================
//  QuizScheduler.h — 练考模块 Phase 2-3：每日定时出题调度器
//  仿 ReviewNudge：独立 message-only 窗口 + 60s 定时器；每日定时（默认 07:30）
//  校验 AI 配置 / 访客 / 文件去重 / 冷却后，后台线程抓 RSS + 调 QuizGen 落盘。
//  不阻塞主线程：网络与写盘均在后台线程；主线程只读 m_last / m_busy。
//  依赖：QuizGen / QuizRss / AccountStore（当前账户目录）。
// ============================================================
#include <windows.h>
#include <string>
#include <mutex>
#include <atomic>
#include "quiz/QuizGen.h"

namespace lj {

// 最近一次生成结果（主线程只读）
struct QuizGenResult
{
    bool        ok = false;
    std::wstring msg;          // 成功路径 or 失败原因
    std::wstring dateISO;      // 对应日期
    long long   ts = 0;        // 完成时间戳（秒）
};

class QuizScheduler
{
public:
    static QuizScheduler& Instance();

    // 创建 message-only 窗口 + 定时器（幂等，主线程调用）
    void Init();
    // 手动立即生成（UI 按钮调用，忽略定时窗与冷却，仍做配置/访客/去重校验）
    void KickNow();
    // 运行时可调参数（Settings UI 尚未做，先用默认值）
    void SetConfig(bool enabled, int hour, int minute,
                   const std::wstring& category, int qcount, bool rss);
    // 读取当前配置（Settings UI 初始化用）
    void GetConfig(bool& enabled, int& hour, int& minute,
                  std::wstring& category, int& qcount, bool& rss) const;
    // 考试模式零打扰：true 时抑制自动出题（手动 KickNow 不受影响）
    void SetQuiet(bool q);

    QuizGenResult Last() const;   // 返回副本（避免与后台线程竞态）
    bool IsGenerating() const { return m_busy.load(); }

private:
    QuizScheduler() = default;
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    void Tick();
    void MaybeStart(bool manual);
    std::wstring TodayISO();

    // 后台生成线程参数（嵌套结构，cpp 装配）
    struct GenArgs {
        quiz::QuizAIConfig cfg;
        std::wstring outDir;     // .../quiz/<category>/
        std::wstring dateISO;
        std::wstring category;
        int         qcount = 10;
        bool        rss = true;
        QuizScheduler* self = nullptr;
    };
    static unsigned __stdcall GenThread(void* arg);   // _beginthreadex 入口（静态）
    void RunGen(GenArgs* a);                          // 实例方法，可访问私有成员

    HWND m_hwnd   = nullptr;
    UINT m_timer  = 0;
    bool m_inited = false;
    std::atomic<bool> m_busy{ false };

    // 配置（默认：启用 / 07:30 / 时政 / 10 题 / 开 RSS）
    bool        m_enabled  = true;
    int         m_hour     = 7;
    int         m_minute   = 30;
    std::wstring m_category = L"时政";
    int         m_qcount   = 10;
    bool        m_rss      = true;

    long long   m_lastAttempt = 0;   // 上次尝试时间戳（秒），用于失败冷却
    std::atomic<bool> m_quiet{ false };   // 考试模式零打扰：true 抑制自动出题
    mutable std::mutex m_mu;
    QuizGenResult m_last{};
};

} // namespace lj
