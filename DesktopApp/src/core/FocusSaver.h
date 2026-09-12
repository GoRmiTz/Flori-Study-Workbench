#pragma once
// ============================================================
//  FocusSaver.h — 专注屏保（批次 C 修正：独立于软件外的全屏覆盖层）
//  仿 ExamMode 的独立 WS_POPUP TOPMOST 窗口（不改动主窗口）：
//   · 鼠标静止 2 秒 → 全屏「专注封面」：大倒计时 + 打卡项 + 背景音乐控制
//   · 鼠标移动   → 窗口缩小为顶部胶囊，其余区域正常露出桌面（像屏保，
//                  只是内容换成专注信息）
//   · 胶囊内含 暂停/继续 与 结束专注；全屏封面另有上一首/下一首/播放/
//     音量条（直接驱动 MusicPlayer 单例）
//  状态来源：RoomView 每帧 PushState(剩余秒, 是否暂停)；暂停/结束通过
//  回调交还 RoomView（计时真相在 RoomView）。
// ============================================================

#include <windows.h>
#include <functional>
#include <string>

namespace lj {

class FocusSaver
{
public:
    static FocusSaver& Instance();

    bool Running() const { return m_hwnd != nullptr; }

    // 启动屏保（item = 当前打卡项标题；remainSec 仅作首帧显示，之后以 PushState 为准）
    void Start(const std::wstring& item, int remainSec);
    void Stop();

    // RoomView 每帧推送计时状态
    void PushState(int remainSec, bool paused);

    // 交还 RoomView 的动作
    std::function<void()> onPauseToggle;   // 暂停/继续（RoomView::ToggleStart）
    std::function<void()> onEnd;           // 结束专注（RoomView::ResetTimer）

private:
    FocusSaver() = default;
    ~FocusSaver() = default;
    FocusSaver(const FocusSaver&) = delete;
    FocusSaver& operator=(const FocusSaver&) = delete;

    void CreateOverlay(HINSTANCE hInst);
    void Destroy();
    void ApplyShape();               // 全屏 ↔ 顶部胶囊（SetWindowPos）
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    void Paint(HDC hdc);
    void PaintFull(HDC hdc, int W, int H);
    void PaintPill(HDC hdc, int W, int H);
    bool Hit(const RECT& r, LPARAM lp) const;

    HWND m_hwnd = nullptr;
    UINT m_timer = 0;
    bool m_full = false;             // false=顶部胶囊（默认起步） true=全屏封面
    float m_idle = 0.0f;             // 鼠标静止秒数（GetCursorPos 轮询，窗口收不到
                                     // mousemove 也能正确计时——胶囊态的关键）
    POINT m_lastPt{ -1, -1 };
    int  m_remain = 0;
    bool m_paused = false;
    std::wstring m_item;
    int  m_lastPaintSec = -1;        // 重绘节流：秒数/状态没变就不重绘

    bool m_volDrag = false;
    // 客户区像素命中区（Paint 每帧刷新）
    RECT m_rPrev{}, m_rPlay{}, m_rNext{}, m_rVol{}, m_rPause{}, m_rEnd{};
    RECT m_rPillPause{}, m_rPillEnd{};
    // GDI 字体缓存（每帧 CreateFont 是主线程卡顿的元凶，只建一次）
    HFONT m_fTag = nullptr, m_fBig = nullptr, m_fItem = nullptr,
          m_fMus = nullptr, m_fBtn = nullptr, m_fPill = nullptr, m_fVolS = nullptr;
    void EnsureFonts();
};

} // namespace lj
