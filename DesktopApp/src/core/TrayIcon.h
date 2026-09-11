#pragma once
// ============================================================
//  TrayIcon.h — 系统托盘常驻（P1-5 桌面护城河）
//  独立 message-only 窗口承载托盘回调，不污染主 Window 类。
// ============================================================
#include "core/Common.h"   // UNICODE / NOMINMAX / WIN32_LEAN_AND_MEAN
#include <shellapi.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>

namespace lj {

class TrayIcon
{
public:
    static TrayIcon& Instance();

    bool Create(HWND owner, HINSTANCE hInst, const wchar_t* tip);
    void Destroy();

    void SetOwner(HWND owner) { m_owner = owner; }
    HWND Owner() const { return m_owner; }

    bool Visible() const { return m_visible; }

    void MinimizeToTray();    // 隐藏主窗口，确保托盘图标在
    void RestoreFromTray();   // 显示并还原主窗口

    // P2-3 方案 A：托盘右键菜单扩展（自动更新等）。id 须 >= 100，
    // 与内置「显示主窗口(1)/退出(2)」不冲突；点击经 onMenuExtra 回调。
    void AddMenuItem(int id, const std::wstring& label);
    void SetMenuHandler(std::function<void(int)> h) { m_onMenuExtra = std::move(h); }

    // 气泡提示（如「发现新版本」）。flags 见 NIIF_*。
    void Balloon(const wchar_t* title, const wchar_t* text, DWORD flags = NIIF_INFO);

    // 气泡被用户点击时回调（NIN_BALLOONUSERCLICK）。用于「点此查看」跳转。
    void SetBalloonClickHandler(std::function<void()> h) { m_onBalloonClick = std::move(h); }

private:
    TrayIcon() = default;
    ~TrayIcon() { Destroy(); }

    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void ShowContextMenu();
    HICON CreateSealIcon();

    HWND m_hwnd = nullptr;
    HWND m_owner = nullptr;
    HICON m_icon = nullptr;
    NOTIFYICONDATAW* m_nid = nullptr;
    bool m_visible = false;

    std::vector<std::pair<int, std::wstring>> m_menuExtras;
    std::function<void(int)> m_onMenuExtra;
    std::function<void()> m_onBalloonClick;   // F-D7 增强：气泡被点击（NIN_BALLOONUSERCLICK）
    std::mutex m_menuMu;
};

} // namespace lj
