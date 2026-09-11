#pragma once
// ============================================================
//  ReviewNudge.h — F-D7 增强：复盘每日自动 nudge
//  独立 message-only 窗口 + 60s 定时器；到点（默认 21:00）汇总当日复盘
//  弹托盘气泡提醒，点击气泡跳转复盘页。纯本地、不联网、不读窗内文本。
//  考试模式（F-D6）进行中不打扰；跨重启按 reviewNudgeLast 去重。
// ============================================================
#include <windows.h>
#include <string>

namespace lj {

class App;  // 前向声明，避免头文件相互依赖

class ReviewNudge
{
public:
    static ReviewNudge& Instance();

    void Init(App* app);   // 创建窗口 + 定时器，持有 App 指针用于跳转
    void Kick();           // 立即尝试一次（app 就绪后调用）
    void Shutdown();

private:
    ReviewNudge() = default;
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    void Tick();           // 定时器回调 → MaybeNudge
    void MaybeNudge();     // 到点且当日未提醒 → 弹气泡

    HWND m_hwnd   = nullptr;
    UINT m_timer  = 0;
    App* m_app    = nullptr;
    bool m_inited = false;
};

} // namespace lj
