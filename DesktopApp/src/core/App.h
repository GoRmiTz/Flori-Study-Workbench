#pragma once
// ============================================================
//  App.h — 应用主体
//  组装：窗口 / 渲染底座 / 纸基着色器 / 路由 / 顶栏 / 光标 / 尘埃
// ============================================================
#include "core/Window.h"
#include "gfx/Graphics.h"
#include "gfx/PaperShader.h"
#include "ui/Canvas.h"
#include "ui/Router.h"
#include "ui/TopBar.h"
#include "ui/Cursor.h"
#include "ui/Dust.h"

namespace lj {

class App
{
public:
    int Run(HINSTANCE hInst, int nCmdShow, const std::wstring& cmdLine);

    // 按当前账户的 settings.json 取主题；账户切换 / 导入备份后由视图层调用
    void ApplyStoredTheme(bool immediate);

    // 路由跳转（供核心模块回调，如考场模式结束自动回到报告页）
    void NavigateTo(const std::wstring& id) { m_router.GoTo(id); }

    // 当前调色板（含主题 tween 中间值），供 Win32 子窗口主题化（WM_CTLCOLOREDIT）
    const Palette& PaletteNow() const { return m_pal; }

private:
    bool Initialize(HINSTANCE hInst);
    void BuildViews();
    void ParseCommandLine(const std::wstring& cmd);
    void HandleInput(UINT msg, WPARAM wp, LPARAM lp);
    void OnHotkey(int id);   // F-D3 全局热键分发（打卡 / 番茄钟 / 暂停）
    void OnResize(UINT w, UINT h, UINT dpi);
    void Frame();
    void ToggleTheme();

    Window      m_window;
    Graphics    m_gfx;
    Canvas      m_canvas;
    PaperShader m_paper;
    Router      m_router;
    TopBar      m_topbar;
    DraftingCursor m_cursor;
    DustField   m_dust;

    // 主题：切换时两套色板做交叉淡入，而不是硬切
    Palette m_pal     = LightPalette();
    Palette m_palFrom = LightPalette();
    Palette m_palTo   = LightPalette();
    Tween   m_themeTween;
    bool  m_dark = false;
    bool  m_darkForced = false;      // 命令行 --dark：截图模式下不被 settings.json 覆盖

    Input m_input;
    float m_time = 0.0f;
    LARGE_INTEGER m_freq{}, m_last{};

    Smooth m_barH{ 0.0f, 0.18f };      // 顶栏滑入滑出
    class LoaderView* m_loader = nullptr;   // 取全局显影进度
    bool m_ready = false;

    // 截图模式
    bool m_shotMode = false;
    std::wstring m_shotPath;
    float m_shotAt = 2.6f;
    float m_shotScroll = 0.0f;       // 截图自检：固定滚动位置（长文档分段验证）
    std::wstring m_shotRoute;
    bool m_shotForceEdit = false;    // 截图自检：强制打开当前视图的编辑器浮层
    bool m_shotForcePreview = false; // 截图自检：强制打开只读预览浮层
    bool m_shotForcePopup = false;   // 截图自检：强制打开账户弹层
    bool m_shotDone = false;
    int  m_frameCount = 0;
};

} // namespace lj
