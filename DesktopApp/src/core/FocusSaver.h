#pragma once
// ============================================================
//  FocusSaver.h — 专注屏保 v3（独立线程 + 独立全屏覆盖窗口）
//  仿 ExamMode 的独立 WS_POPUP TOPMOST 窗口，但**运行在专用线程**：
//  消息循环 / 250ms 定时器 / GDI 全屏重绘全部与主线程隔离，
//  主窗口（D3D 渲染循环）不再被屏保拖慢——这是 v2「卡 UI」的结构性修法。
//
//  息屏逻辑：GetLastInputInfo 系统级空闲时间（鼠标 + 键盘都算——
//  v2 只盯鼠标坐标，用户打字时也会息屏）。静止 5s → 全屏专注封面；
//  任何输入（移动/打字/点击）→ 立即缩回顶部胶囊，桌面正常露出。
//
//  线程模型：
//   - Start/Stop/PushState/ConsumeActions 在主线程调用；
//   - 计时状态走 atomic；曲目名 Start 时一次性拷贝；
//   - GDI（窗口/字体/绘制）全部在屏保线程，句柄不出线程；
//   - 暂停/结束按钮置原子计数，主循环 ConsumeActions 取走后
//     由 RoomView 在主线程执行（UI 数据不跨线程触碰）；
//     音乐控制（Prev/Next/Toggle/SetVolume）直接调用 MusicPlayer
//     ——其 XAudio2 路径本就线程安全。
// ============================================================

#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace lj {

class FocusSaver
{
public:
    static FocusSaver& Instance();

    bool Running() const { return m_running.load(std::memory_order_acquire); }

    // 启动屏保（item = 当前打卡项标题；remainSec 仅作首帧显示，之后以 PushState 为准）
    void Start(const std::wstring& item, int remainSec);
    void Stop();

    // RoomView 每帧推送计时状态（主线程）
    void PushState(int remainSec, bool paused);

    // 主循环轮询：取走按钮动作。返回 true 表示有动作。
    bool ConsumeActions(bool& pauseToggle, bool& endFocus);

private:
    FocusSaver() = default;
    ~FocusSaver() = default;
    FocusSaver(const FocusSaver&) = delete;
    FocusSaver& operator=(const FocusSaver&) = delete;

    void ThreadProc();               // 屏保线程主体（窗口 + 消息循环 + 绘制）
    void ApplyShape();               // 全屏 ↔ 顶部胶囊（屏保线程内调用）
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    void Paint(HDC hdc);
    void PaintFull(HDC hdc, int W, int H);
    void PaintPill(HDC hdc, int W, int H);
    bool Hit(const RECT& r, LPARAM lp) const;
    void EnsureFonts();
    void ReleaseFonts();

    // —— 屏保线程私有（勿跨线程访问）——
    HWND m_hwnd = nullptr;
    UINT m_timer = 0;
    bool m_full = false;             // false=顶部胶囊（起步） true=全屏封面
    int  m_lastPaintSec = -1;        // 重绘节流
    bool m_volDrag = false;
    RECT m_rPrev{}, m_rPlay{}, m_rNext{}, m_rVol{}, m_rPause{}, m_rEnd{};
    RECT m_rPillPause{}, m_rPillEnd{};
    HFONT m_fTag = nullptr, m_fBig = nullptr, m_fItem = nullptr,
          m_fMus = nullptr, m_fBtn = nullptr, m_fPill = nullptr, m_fVolS = nullptr;

    // —— 跨线程状态 ——
    std::thread        m_thread;
    std::atomic<bool>  m_running{ false };
    std::atomic<bool>  m_stopReq{ false };
    std::atomic<DWORD> m_threadId{ 0 };
    std::atomic<int>   m_remain{ 0 };
    std::atomic<bool>  m_paused{ false };
    std::atomic<int>   m_actPause{ 0 };   // 按钮动作计数（主线程取走）
    std::atomic<int>   m_actEnd{ 0 };
    mutable std::mutex m_itemMu;
    std::wstring       m_item;            // Start 时写，线程内只读拷贝
};

} // namespace lj
