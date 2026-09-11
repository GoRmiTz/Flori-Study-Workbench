#pragma once
// ============================================================
//  ExamMode.h — F-D6 考场模式（模考环境模拟）
//  一键进入「模考环境」：全屏顶部 HUD（topmost + 点击穿透）+ 真实倒计时，
//  复用 F-D1 前台进程感知做「中断检测」（切到非学习进程标记一次中断）。
//  到点自动结束并出报告；可中途退出（标记放弃）。
//  不锁系统、不强制关进程（只提示 + 标记中断），避免破坏用户环境。
// ============================================================
#include "app/Store.h"   // ExamReport 结构定义所在
#include <windows.h>
#include <string>
#include <functional>
#include <atomic>

namespace lj {

class ExamMode
{
public:
    static ExamMode& Instance();

    bool Running() const { return m_running.load(); }
    int  RemainingSec() const;                 // 剩余秒（HUD 用）
    int  Interrupts() const { return m_report.interrupts; }

    void Start(int minutes = 120);             // 进入考场（默认 120 分钟行测模考）
    void Stop();                              // 中途退出（标记放弃）
    void SetOnFinish(std::function<void()> cb) { m_onFinish = std::move(cb); }

    // HUD 文案（供浮层绘制）
    std::wstring CountdownText() const;
    std::wstring StatusText() const;          // L"专注中" / L"⚠ 中断：xxx"

private:
    ExamMode() = default;
    ~ExamMode() { Destroy(); }

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    void CreateOverlay(HINSTANCE hInst);
    void Destroy();
    void Paint(HDC hdc);
    void Tick();                              // 1s 定时器：倒计时 + 中断检测
    void Finish();                            // 结束并出报告

    HWND m_hwnd = nullptr;
    UINT m_timer = 0;
    std::atomic<bool> m_running{ false };

    long long m_endAt = 0;                   // 结束 epoch 秒
    int       m_planned = 120;
    long long m_startAt = 0;
    ExamReport m_report{};

    // 中断检测状态机
    bool      m_wasFocus = true;             // 上一 tick 是否处于学习态
    long long m_graceUntil = 0;              // 开始后的宽限截止（避免起手误判）
    long long m_interruptStart = 0;          // 当前中断段起点（0 = 不在中断中）
    int       m_totalInterruptSec = 0;       // 累计中断秒数
    std::wstring m_lastProc;                 // 最近中断进程名

    std::function<void()> m_onFinish;
};

} // namespace lj
