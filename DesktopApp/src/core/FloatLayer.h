#pragma once
// ============================================================
//  FloatLayer.h — F-D2 常驻浮层（桌面护城河）
//  独立 topmost / no-activate 工具窗口，桌面角落常驻显示：
//    今日专注 · 当前连续 · 目标倒计时 · 自习室在线
//  不抢前台焦点（WS_EX_NOACTIVATE），看得见点得着（左键恢复主窗）。
//  与 FloatingPlayer（应用内浮层）不同：本类为真实 OS 窗口，
//  芙洛理最小化到托盘、用户正在其它 App 学习时仍常驻可见。
// ============================================================
#include <windows.h>
#include <string>

// F-D5 OLE 拖放目标：全局 COM 接口前向声明（oleidl.h 在 FloatLayer.cpp 内完整 include；
// 此处仅用指针不完全类型即可，供头文件被其它 TU 包含时识别 ::IDropTarget）。
struct IDropTarget;

namespace lj {

// 自习室在线人数：由 RoomView 的 presence 广播更新，浮层只读。
// 放在这里是为了让 RoomView 与 FloatLayer 共享同一符号、零耦合。
extern int g_studyRoomOnline;

class FloatLayer
{
public:
    static FloatLayer& Instance();

    // scale: DPI 缩放（g_dpi / 96）。owner 为主窗口，用于定位到同显示器角落。
    bool Create(HWND owner, HINSTANCE hInst, float scale = 1.0f);
    void Destroy();
    void Show();
    void Hide();
    void Toggle();
    // 浮现/淡出（ANIMATE_WINDOW AW_BLEND 真·alpha 渐变）：由浮层显隐逻辑调用。
    void ShowWithFade();
    void HideWithFade();
    bool Visible() const { return m_visible; }

private:
    FloatLayer() = default;
    ~FloatLayer() { Destroy(); }

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void PositionToCorner();
    void Recompute();
    void Paint(HDC hdc);

    HWND  m_hwnd   = nullptr;
    HWND  m_owner  = nullptr;
    float m_scale  = 1.0f;
    bool  m_visible = false;
    UINT  m_timer  = 0;
    ::IDropTarget* m_drop = nullptr;   // F-D5 OLE 拖放目标（RegisterDragDrop 持有引用）

    // 缓存显示文本（避免每次 WM_PAINT 重新取数）
    std::wstring m_focusLine;   // 今日专注
    std::wstring m_contLine;    // 当前连续
    std::wstring m_countLine;   // 目标倒计时
    std::wstring m_roomLine;    // 自习室在线
    int m_tick = 0;
};

} // namespace lj
