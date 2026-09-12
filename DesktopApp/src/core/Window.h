#pragma once
// ============================================================
//  Window.h — 无边框 Win32 窗口
//  去掉系统标题栏但保留投影、贴边、缩放与最大化动画；
//  标题栏由应用自绘，命中测试回调给 App 决定哪里可拖拽。
// ============================================================
#include "core/Common.h"
#include <functional>

namespace lj {

class Window
{
public:
    bool Create(HINSTANCE hInst, const wchar_t* title, int widthDip, int heightDip);
    void Show(int nCmdShow);
    void Destroy();

    HWND Handle() const { return m_hwnd; }
    UINT Dpi() const { return m_dpi; }
    float Scale() const { return m_dpi / 96.0f; }
    // 注意：winuser.h 把 IsMaximized 定义成了宏（=IsZoomed），此处必须换名
    bool Maximized() const;
    bool Closed() const { return m_closed; }

    void Minimize();
    void ToggleMaximize();
    void Close();
    // 绕过 onCloseRequest 确认链直接退出（App 弹层「退出」/ 托盘菜单「退出」用）
    void ForceClose();

    // 自绘标题栏高度（DIP），用于 WM_NCHITTEST
    float captionHeight = 0.0f;
    // 返回 true 表示该点属于按钮等控件，不应拖拽窗口
    std::function<bool(float, float)> isBlockedPoint;

    // 用户点关闭（自绘 × / Alt+F4 / WM_CLOSE 且非强制）时回调；
    // App 在此决定直接退出 / 最小化到托盘 / 弹确认层。不设置则行为同以前。
    std::function<void()> onCloseRequest;

    std::function<void(UINT widthPx, UINT heightPx, UINT dpi)> onResize;
    std::function<void(UINT msg, WPARAM, LPARAM)> onInput;
    std::function<void()> onIdlePaint;

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HitTest(POINT screenPt);

    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;
    bool m_closed = false;
    bool m_tracking = false;
};

} // namespace lj
