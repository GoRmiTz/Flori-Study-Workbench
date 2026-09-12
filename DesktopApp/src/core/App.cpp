#include "core/App.h"
#include "core/Screenshot.h"
#include "core/FocusTracker.h"
#include "core/TrayIcon.h"
#include "core/FloatLayer.h"   // F-D2 常驻浮层
#include "core/KnowledgeDrop.h" // F-D5+ 知识库页内拖拽收集考点
#include "app/Data.h"
#include "app/Store.h"
#include "app/Cloud.h"
#include "net/Realtime.h"
#include "views/LoaderView.h"
#include "views/CoverView.h"
#include "views/HomeView.h"
#include "views/CheckinView.h"
#include "views/RoomView.h"
#include "views/DashView.h"
#include "views/RoadmapView.h"
#include "views/MaterialsView.h"
#include "views/AdvisorView.h"   // P1-7 选岗参谋 F2
#include "views/AchieveView.h"   // P1-8 成就体系 F3 + 契约组队 F4
#include "app/Metrics.h"        // P2-1 留存指标（需求验证口径）
#include "app/Updater.h"        // P2-3 方案 A · 客户端自动更新
#include "views/MediaView.h"
#include "views/VideoView.h"
#include "views/ProfileView.h"
#include "views/ManageView.h"
#include "views/FriendView.h"   // #37 好友
#include "views/KnowledgeView.h" // F-D5 知识库：跨窗口拖拽收集考点
#include "views/ExamReportView.h" // F-D6 考场模式：模考报告 / 开始入口
#include "views/ReviewView.h"      // F-D7 本地复盘搭子（规则版）
#include "views/QuizView.h"       // 练考模块 Phase 1（阅读器 + 答题卡）
#include "views/SettingsView.h"    // 全局设置页（route=settings，账户弹层「设置」进入）
#include "core/ExamMode.h"        // F-D6 考场模式：全屏 HUD + 中断检测
#include "core/ReviewNudge.h"      // F-D7 增强：复盘每日自动 nudge
#include "views/LoginView.h"
#include "core/Hwnd.h"
#include "ui/FloatingPlayer.h"   // #49 全局浮空音乐播放器（跨页常驻）

#include <windowsx.h>

namespace lj {

//
static HWND  g_hwnd = nullptr;
static UINT  g_dpi = 96;
static App*  g_app = nullptr;
HWND AppHwnd() { return g_hwnd; }
UINT  AppDpi()  { return g_dpi; }
Palette AppPalette() { return g_app ? g_app->PaletteNow() : LightPalette(); }
void AppSyncTheme(bool immediate) { if (g_app) g_app->ApplyStoredTheme(immediate); }

// Input background: views set before opening input to match card seamlessly
static D2D1_COLOR_F g_editBg = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
void SetEditBackdrop(const D2D1_COLOR_F& c) { g_editBg = c; }
const D2D1_COLOR_F& EditBackdrop() { return g_editBg; }

//
D2D1_COLOR_F CardFace(float lift)
{
    const Palette& pal = AppPalette();
    D2D1_COLOR_F base = pal.dark ? pal.paper : D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    float a = lift * 0.55f;
    auto mix = [](float x, float y, float t) { return x * (1.0f - t) + y * t; };
    return D2D1::ColorF(mix(pal.paperHi.r, base.r, a),
                        mix(pal.paperHi.g, base.g, a),
                        mix(pal.paperHi.b, base.b, a), 1.0f);
}

// D2D float -> COLORREF (0..255) for EDIT/RichEdit
COLORREF RgbOf(const D2D1_COLOR_F& c)
{
    return RGB((BYTE)(c.r * 255.0f + 0.5f),
               (BYTE)(c.g * 255.0f + 0.5f),
               (BYTE)(c.b * 255.0f + 0.5f));
}

// ------------------------------------------------------------
//
static Palette MixPalette(const Palette& a, const Palette& b, float t)
{
    Palette p{};
#define LJ_MIX(f) p.f = MixColor(a.f, b.f, t)
    LJ_MIX(ink900); LJ_MIX(ink700); LJ_MIX(ink500); LJ_MIX(ink300); LJ_MIX(ink100);
    LJ_MIX(paper);  LJ_MIX(paperHi); LJ_MIX(paperLo); LJ_MIX(paperDeep);
    LJ_MIX(rule);   LJ_MIX(ruleStrong);
    LJ_MIX(seal);   LJ_MIX(sealLo);  LJ_MIX(sealWash);
    LJ_MIX(brass);  LJ_MIX(brassWash);
    LJ_MIX(jade);   LJ_MIX(jadeWash);
    LJ_MIX(vermilion); LJ_MIX(vermWash);
#undef LJ_MIX
    p.ruleHairAlpha = Lerp(a.ruleHairAlpha, b.ruleHairAlpha, t);
    p.ruleGridAlpha = Lerp(a.ruleGridAlpha, b.ruleGridAlpha, t);
    p.dark = (t < 0.5f) ? a.dark : b.dark;
    return p;
}

// ============================================================
//
// ============================================================
int App::Run(HINSTANCE hInst, int nCmdShow, const std::wstring& cmdLine)
{
    LogInit();
    LogLine(L"==== FLORI Desktop Startup ====");
    ParseCommandLine(cmdLine);

    // P2-3 方案 A：若上次已后台下载更新，启动即交换并重启（截图模式跳过）
    if (!m_shotMode)
        Updater::Instance().ApplyStartupPending(cmdLine);

    if (!Initialize(hInst)) {
        MessageBoxW(nullptr,
                    L"Render device init failed.\nSee flori.log for details.",
                    L"FLORI - Startup Failed", MB_ICONERROR | MB_OK);
        return -1;
    }

    m_window.Show(m_shotMode ? SW_SHOWNOACTIVATE : nCmdShow);

    QueryPerformanceFrequency(&m_freq);
    QueryPerformanceCounter(&m_last);

    bool running = true;
    MSG msg{};
    while (running) {
        if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else if (m_ready) {
            Frame();
        } else {
            // Before first frame, keep message loop alive
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
        }
        if (!running) break;
    }
    FloatLayer::Instance().Destroy();
    UnregisterKnowledgeDrop(m_window.Handle());
    UnregisterHotKey(m_window.Handle(), 100);
    UnregisterHotKey(m_window.Handle(), 101);
    UnregisterHotKey(m_window.Handle(), 102);
    TrayIcon::Instance().Destroy();
    ReviewNudge::Instance().Shutdown();   // F-D7 增强：停定时器 + 销毁窗口
    FocusTracker::Instance().Stop();
    // 停后台线程（join）再退出：std::thread 若带 joinable 走单例析构会
    // std::terminate → abort()（表现为退出时弹「abort() has been called」）。
    Realtime::Instance().Shutdown();      // §3 实时 WS 后台线程
    Cloud::Instance().Shutdown();         // 云同步 worker（内部先 Flush 再 join；未登录时 m_ready=false 直接返回）
    return 0;
}

void App::ParseCommandLine(const std::wstring& cmd)
{
    std::vector<std::wstring> args;
    {
        std::wstring cur;
        bool quoted = false;
        for (wchar_t ch : cmd) {
            if (ch == L'"') { quoted = !quoted; continue; }
            if (!quoted && (ch == L' ' || ch == L'\t')) {
                if (!cur.empty()) { args.push_back(cur); cur.clear(); }
            } else cur.push_back(ch);
        }
        if (!cur.empty()) args.push_back(cur);
    }
    auto it = args.begin();
    auto end = args.end();

    for (auto a = args.begin(); a != args.end(); ++a) {
        if (*a == L"--shot") {
            m_shotMode = true;
            m_shotPath = L"shot.png";
            // 可选紧跟输出路径：--shot out\home.png。没有就沿用默认 shot.png。
            // （CI 逐路由出图要靠它区分文件名；以前无条件写死 shot.png，
            //   17 张图会互相覆盖，根本没法做基线 diff。）
            auto nxt = a + 1;
            if (nxt != args.end() && nxt->rfind(L"--", 0) != 0) { ++a; m_shotPath = *a; }
        }
        else if (*a == L"--dark")     { m_dark = true; m_darkForced = true; }
        else if (*a == L"--route" && a+1 != args.end()) { ++a; m_shotRoute = *a; }
        else if (*a == L"--at"   && a+1 != args.end()) { ++a; m_shotAt = (float)_wtof(a->c_str()); }
        else if (*a == L"--scroll" && a+1 != args.end()) { ++a; m_shotScroll = (float)_wtof(a->c_str()); }
        else if (*a == L"--edit")     { m_shotForceEdit = true; }
        else if (*a == L"--preview")  { m_shotForcePreview = true; }
        else if (*a == L"--popup")    { m_shotForcePopup = true; }
    }
    if (m_shotMode)
        LogLine(L"[shot] file=%s route=%s at=%.2f scroll=%.0f",
                m_shotPath.c_str(), m_shotRoute.c_str(), m_shotAt, m_shotScroll);
}

// ============================================================
//
bool App::Initialize(HINSTANCE hInst)
{
    //
    m_window.captionHeight = shape::kTitleBar;

    m_window.isBlockedPoint = [this](float x, float y) -> bool {
        bool full = m_router.Current() && m_router.Current()->FullBleed();
        const auto& rects = full ? m_topbar.ChromeRects() : m_topbar.BlockedRects();
        for (const auto& r : rects)
            if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return true;
        return false;
    };
    m_window.onResize = [this](UINT w, UINT h, UINT dpi) { OnResize(w, h, dpi); };
    m_window.onInput  = [this](UINT msg, WPARAM wp, LPARAM lp) { HandleInput(msg, wp, lp); };
    m_window.onIdlePaint = [this] { Frame(); };

    if (!m_window.Create(hInst, L"FLORI Desktop", 1280, 820))
        return false;
    g_hwnd = m_window.Handle();
    g_dpi = m_window.Dpi();
    g_app = this;

    // P1-5：系统托盘常驻（message-only 窗口，避免污染主窗口类；截图/CI 模式跳过）
    if (!m_shotMode) {
        TrayIcon::Instance().Create(m_window.Handle(), hInst, L"芙洛理 Flori — 点击恢复主窗口");
        // F-D2 常驻浮层（独立 topmost / no-activate 窗口，角落常驻不抢焦点）
        FloatLayer::Instance().Create(m_window.Handle(), hInst, g_dpi / 96.0f);
        // F-D5+ 主窗口注册 OLE 放目标：浏览器文字 / .md/.txt 文件拖进窗口即收集考点
        RegisterKnowledgeDrop(m_window.Handle());

        // F-D3 全局热键：Ctrl+Alt+{C/T/P} 为默认，设置可覆盖；冲突则气泡提示
        {
            AppSettings hs = CheckinStore::Instance().LoadSettings();
            auto pick = [](const AppSettings::HotkeyCombo& c, unsigned dmod, unsigned dvk)
                        -> std::pair<unsigned, unsigned> {
                return { c.vkey ? c.mod : dmod, c.vkey ? c.vkey : dvk };
            };
            auto reg = [&](int id, unsigned mod, unsigned vk) {
                if (!RegisterHotKey(m_window.Handle(), id, (UINT)mod, (UINT)vk)) {
                    DWORD e = GetLastError();
                    if (e == ERROR_HOTKEY_ALREADY_REGISTERED)
                        TrayIcon::Instance().Balloon(L"热键冲突",
                            L"全局快捷键被其它程序占用，请到设置中修改热键组合。", NIIF_WARNING);
                }
            };
            auto a = pick(hs.hotCheckin,   MOD_CONTROL | MOD_ALT, (unsigned)'C');
            auto b = pick(hs.hotPomodoro, MOD_CONTROL | MOD_ALT, (unsigned)'T');
            auto c = pick(hs.hotPause,     MOD_CONTROL | MOD_ALT, (unsigned)'P');
            reg(100, a.first, a.second);
            reg(101, b.first, b.second);
            reg(102, c.first, c.second);
        }
        // P2-3 方案 A：托盘右键菜单挂「检查更新 / 重启并更新」
        TrayIcon::Instance().AddMenuItem(100, L"检查更新");
        TrayIcon::Instance().AddMenuItem(101, L"重启并更新");
        TrayIcon::Instance().AddMenuItem(110, L"显示专注浮层");
        TrayIcon::Instance().AddMenuItem(120, L"退出考场模式");
        TrayIcon::Instance().AddMenuItem(130, L"今日复盘");
        TrayIcon::Instance().SetMenuHandler([](int id) {
            auto& u = Updater::Instance();
            if (id == 100) u.CheckNow();
            else if (id == 101) u.ApplyNow();
            else if (id == 110) {
                auto& fl = FloatLayer::Instance();
                fl.Toggle();
                // 菜单项文案随显隐翻转（AddMenuItem 对已存在 id 只更新 label）
                TrayIcon::Instance().AddMenuItem(110, fl.Visible() ? L"隐藏专注浮层" : L"显示专注浮层");
            }
            else if (id == 120) {
                auto& em = ExamMode::Instance();
                if (em.Running()) em.Stop();
                else TrayIcon::Instance().Balloon(L"考场模式", L"当前未处于考场模式");
            }
            else if (id == 130) {
                if (g_app) g_app->NavigateTo(L"review");   // F-D7 本地复盘搭子入口
            }
        });
    }

    if (!m_gfx.Initialize(m_window.Handle())) return false;

    RECT rc{};
    GetClientRect(m_window.Handle(), &rc);
    if (!m_gfx.Resize((UINT)(rc.right - rc.left), (UINT)(rc.bottom - rc.top), m_window.Dpi()))
        return false;

    if (!m_canvas.Init(m_gfx)) return false;
    if (!m_paper.Initialize(m_gfx)) {
        LogLine(L"[error] paper shader init failed: %s", m_paper.LastError().c_str());
        //
    }

    m_pal = m_dark ? DarkPalette() : LightPalette();
    m_palFrom = m_palTo = m_pal;
    m_canvas.BeginFrame(m_pal); //
    m_dust.Init();

    m_topbar.Init();
    m_topbar.onNavigate    = [this](const std::wstring& id) { m_router.GoTo(id); };
    m_topbar.onMinimize    = [this] { TrayIcon::Instance().MinimizeToTray(); };
    m_topbar.onMaximize    = [this] { m_window.ToggleMaximize(); };
    m_topbar.onClose       = [this] { m_window.Close(); };
    m_topbar.onToggleTheme = [this] { ToggleTheme(); };
    // 关闭按钮行为分流（设置 → 通用 → 「点关闭按钮时」）
    m_window.onCloseRequest = [this] { OnCloseRequest(); };
    m_topbar.onAccount      = [this] { m_router.GoTo(L"login"); };
    m_topbar.onLogout       = [this] {
        FocusTracker::Instance().Flush();
        AccountStore::Instance().Logout();
        // 退出后立即切回通用内容壳（首页模块等不残留旧账户数据），再跳登录页
        Content::ApplyCurrentAccount();
        CheckinStore::Instance().Reload();
        m_router.GoTo(L"login");
    };

    // Cloud: read config, restore tokens, start background sync
    // Screenshot mode skips network to avoid altering content
    AccountStore::Instance().Init();
    Content::ApplyCurrentAccount();

    // P1-5：启动前台学习检测（仅对已登录非访客账户持久化）
    if (!m_shotMode && !AccountStore::Instance().IsGuest()) {
        FocusTracker::Instance().Start();
    }

    if (!m_shotMode) {
        Cloud::Instance().Init();
        //
        // P2-3 方案 A：启动后后台比对服务端版本，有新版本则托盘提示并预下载
        Updater::Instance().CheckAsync();
        Realtime::Instance().Init(
            [] { return Cloud::Instance().EndpointCfg(); },
            [] { return Cloud::Instance().AccessToken(); },
            [] { return Cloud::Instance().TryRefresh(); },
            AccountStore::Instance().IsGuest());
    }

    // Load theme from current account settings.json
    if (!m_darkForced) {
        ApplyStoredTheme(true);
        m_canvas.BeginFrame(m_pal); //
    }

    BuildViews();

    // F-D6：考场模式结束（到点或中途退出）后，恢复主窗口并自动回到模考报告页
    ExamMode::Instance().SetOnFinish([]() {
        TrayIcon::Instance().RestoreFromTray();
        if (g_app) g_app->NavigateTo(L"exam");
    });

    // P2-1 留存指标一次性预热：验证 Metrics.h 派生路径可用（非每帧调用）
    {
        auto rm = ComputeRetention();
        (void)rm;
    }

    // Screenshot mode: skip cloud, jump to route
    if (m_shotMode && !m_shotRoute.empty()) {
        m_router.GoTo(m_shotRoute, true);
        m_cursor.enabled = false;
        // Force open editor overlay for screenshot verification
        if (m_shotForceEdit && m_router.Current())
            m_router.Current()->DebugForceOpen();
        if (m_shotForcePreview && m_router.Current())
            m_router.Current()->DebugForcePreview();
        if (m_shotForcePopup)
            m_topbar.ForceAccountPopup();
    }
    // 开屏动画（LoaderView）始终先播，不再因无会话而跳过；
    // 播完由 LoaderView 按会话状态自行决定去「封面 cover」或「登录 login」。

    m_barH.Snap((m_router.Current() && m_router.Current()->FullBleed()) ? 0.0f : shape::kTitleBar);
    m_ready = true;
    LogLine(L"[init] ready %ux%u @%u dpi", m_gfx.WidthPx(), m_gfx.HeightPx(), m_gfx.Dpi());

    // F-D7 增强：复盘每日自动 nudge（到点托盘提醒，纯本地）
    ReviewNudge::Instance().Init(this);
    ReviewNudge::Instance().Kick();

    return true;
}

// ============================================================
//
//
// ============================================================
void App::BuildViews()
{
    auto nav = [this](const std::wstring& id) { m_router.GoTo(id); };

    auto add = [&](std::unique_ptr<View> v) {
        v->navigate = nav;
        m_router.Add(std::move(v));
    };

    {
        auto loader = std::make_unique<LoaderView>();
        m_loader = loader.get();
        add(std::move(loader));
    }
    add(std::make_unique<CoverView>());
    add(std::make_unique<HomeView>());
    add(std::make_unique<CheckinView>());
    add(std::make_unique<RoomView>());
    add(std::make_unique<DashView>());
    add(std::make_unique<RoadmapView>());
    add(std::make_unique<MaterialsView>());
    add(std::make_unique<MediaView>());
    add(std::make_unique<VideoView>());
    add(std::make_unique<ProfileView>());
    add(std::make_unique<ManageView>());
    add(std::make_unique<AdvisorView>());  // P1-7 选岗参谋 F2
    add(std::make_unique<AchieveView>());  // P1-8 成就体系 F3 + 契约组队 F4
    add(std::make_unique<FriendView>());   // #37 好友：列表 / 私聊 / 进度
    add(std::make_unique<KnowledgeView>()); // F-D5 知识库：跨窗口拖拽收集考点
    add(std::make_unique<ExamReportView>());// F-D6 考场模式：模考报告 / 开始入口
    add(std::make_unique<ReviewView>());     // F-D7 本地复盘搭子（规则版）
    add(std::make_unique<QuizView>());       // 练考模块 Phase 1（阅读器 + 答题卡）
    add(std::make_unique<SettingsView>());    // 全局设置页（账户弹层「设置」进入，整合 通用/练考）
    add(std::make_unique<LoginView>());    // 登录 / 注册 / 访客入口（退出登录 / 未登录点胶囊都要跳到这里）
}

// ============================================================
//
void App::HandleInput(UINT msg, WPARAM wp, LPARAM lp)
{
    const float s = m_gfx.Ready() ? m_gfx.Scale() : m_window.Scale();

    switch (msg) {
    case WM_MOUSEMOVE:
        m_input.mouseX = GET_X_LPARAM(lp) / s;
        m_input.mouseY = GET_Y_LPARAM(lp) / s;
        m_input.inWindow = true;
        break;
    case WM_LBUTTONDOWN:
        m_input.clicked = true;
        m_input.pressed = true;
        break;
    case WM_LBUTTONUP:
        m_input.released = true;
        m_input.pressed = false;
        break;
    case WM_MOUSEWHEEL:
        // 取反：OS 向上滚给正增量，而视图层约定「wheel 正 = 向下滚」，
        // 不取反会导致「往下翻要往上滚」。每格 ±1.0，倍率由 View::Update 控制。
        m_input.wheel = -(float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
        break;
    case WM_MOUSELEAVE:
        m_input.inWindow = false;
        break;
    case WM_KEYDOWN:
        if (wp < 256) m_input.keyDown[wp] = true;
        if (wp == VK_F5) {
            bool ok = m_paper.Reload(m_gfx);
            LogLine(ok ? L"[hot] shader reloaded" : L"[hot] reload failed: %s",
                    ok ? L"" : m_paper.LastError().c_str());
            ToggleTheme();
        } else if (wp == VK_F11) {
            m_window.ToggleMaximize();
        } else if (wp == VK_ESCAPE) {
            if (m_router.Current() && m_router.Current()->Id() != L"home")
                m_router.GoTo(L"home");
        }
        break;
    case WM_HOTKEY:
        OnHotkey((int)wp);
        break;
    default: break;
    }

    //
    // m_input.pendingMsg = msg;
    // m_input.pendingWp  = wp;
    // m_input.pendingLp  = lp;
}

void App::OnHotkey(int id)
{
    switch (id) {
    case 100: {   // 一键打卡：立即落盘当前专注 + 汇报今日专注总时长
        FocusTracker::Instance().Flush();
        int min = 0;
        for (auto& f : CheckinStore::Instance().LoadFocus())
            if (f.category == (int)FocusCat::Study) min += f.min;
        TrayIcon::Instance().Balloon(L"已打卡 ✅",
            (L"今日专注 " + std::to_wstring(min) + L" 分钟").c_str());
        break;
    }
    case 101: {   // 番茄钟切换（静默，不切前台视图）
        if (auto* v = m_router.Find(L"room")) {
            bool running = static_cast<RoomView*>(v)->HotkeyToggleTimer();
            TrayIcon::Instance().Balloon(L"番茄钟", running ? L"已开始专注 ⏱" : L"已暂停 ⏸");
        }
        break;
    }
    case 102: {   // 暂停 / 恢复前台专注自动计时
        auto& ft = FocusTracker::Instance();
        if (ft.Paused()) { ft.Resume(); TrayIcon::Instance().Balloon(L"专注计时", L"已恢复 ▶"); }
        else             { ft.Pause();  TrayIcon::Instance().Balloon(L"专注计时", L"已暂停 ⏸"); }
        break;
    }
    }
}

// ============================================================
//
// ============================================================
void App::OnResize(UINT w, UINT h, UINT dpi)
{
    if (!m_gfx.Ready() || w == 0 || h == 0) return;
    m_gfx.Resize(w, h, dpi);
    m_topbar.SetMaximized(m_window.Maximized());
    if (m_ready) Frame(); //
}

// ============================================================
//
// ============================================================
void App::ToggleTheme()
{
    m_dark = !m_dark;
    m_palFrom = m_pal;
    m_palTo = m_dark ? DarkPalette() : LightPalette();
    m_themeTween.Start(0.0f, 1.0f, 0.55f, ease::InOutCubic);
    LogLine(L"[theme] switched to %s", m_dark ? L"dark" : L"light");

    //
    if (!m_shotMode) {
        AppSettings st = CheckinStore::Instance().LoadSettings();
        st.dark = m_dark;
        CheckinStore::Instance().SaveSettings(st);
    }
}

// ============================================================
//
void App::ApplyStoredTheme(bool immediate)
{
    if (m_darkForced) return; //
    const bool want = CheckinStore::Instance().LoadSettings().dark;
    if (want == m_dark && m_ready) return; //
    m_palTo = m_dark ? DarkPalette() : LightPalette();
    if (immediate) {
        m_pal = m_palFrom = m_palTo;
        m_themeTween = Tween{};
    } else {
        m_palFrom = m_pal;
        m_themeTween.Start(0.0f, 1.0f, 0.55f, ease::InOutCubic);
    }
}

// ============================================================
//
// ============================================================
void App::Frame()
{
    if (!m_ready || !m_gfx.Ready()) return;

    //
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    float dt = (float)((double)(now.QuadPart - m_last.QuadPart) / (double)m_freq.QuadPart);
    m_last = now;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f; //
    m_time += dt;
    m_input.dt = dt;
    m_input.time = m_time;

    //
    // Must run on UI thread, avoid racing CheckinStore
    if (Cloud::Instance().ConsumePulled()) {
        CheckinStore::Instance().Reload();
        if (!m_darkForced) ApplyStoredTheme(false);
        if (View* v = m_router.Current()) v->OnEnter(); //
    }

    //
    if (m_themeTween.running) {
        m_themeTween.Update(dt);
        m_pal = MixPalette(m_palFrom, m_palTo, m_themeTween.Value());
        if (m_themeTween.Done()) { m_pal = m_palTo; m_themeTween.running = false; }
    }

    //
    m_canvas.BeginFrame(m_pal);

    const float W = m_gfx.WidthDip();
    const float H = m_gfx.HeightDip();

    View* cur = m_router.Current();
    const bool full = cur && cur->FullBleed();

    m_barH.target = full ? 0.0f : shape::kTitleBar;
    m_barH.Update(dt);
    const float barH = m_barH.value;

    // Topbar first so router knows content area
    m_topbar.SetMaximized(m_window.Maximized());
    m_topbar.Layout(0.0f, 0.0f, W, (std::max)(barH, shape::kTitleBar));
    // Sync caption height for child window (EDIT) positioning
    m_window.captionHeight = (std::max)(barH, 40.0f);

    D2D1_RECT_F content{ 0.0f, barH, W, H };
    m_router.Layout(content, m_canvas);

    //
    if (m_shotMode && m_shotScroll > 0.0f && m_router.Current())
        m_router.Current()->SetScroll(m_shotScroll);

    // 退出确认弹层：压住一切输入（顶栏 / 视图 / 浮层播放器）
    const bool exitAsking = m_exitAsk;
    if (exitAsking) { ExitAskLayout(W, H); ExitAskUpdate(); }

    //
    //
    {
        Input barIn = m_input;
        if (exitAsking) { barIn.clicked = barIn.released = barIn.pressed = false; barIn.wheel = 0.0f; }
        if (!full) barIn.mouseY = m_input.mouseY - (barH - shape::kTitleBar);
        m_topbar.Update(dt, barIn, m_router.CurrentId(), full);
    }

    //
    Input viewIn = m_input;
    if (m_topbar.AccountPopupOpen() || exitAsking) {
        viewIn.clicked = false; viewIn.released = false; viewIn.pressed = false;
        viewIn.wheel = 0.0f;
    }
    // 浮空播放器（#49，全局常驻）：拦截落在它之上的输入，避免穿透到当前页
    if (!full && !exitAsking) {
        FloatingPlayer::Instance().Update(dt, m_input, D2D1_RECT_F{ 0.0f, 0.0f, W, H });
        if (FloatingPlayer::Instance().ConsumeInput())
            viewIn.clicked = viewIn.released = viewIn.pressed = false;
    }

    m_router.Update(dt, viewIn);

    float scroll = m_router.Current() ? m_router.Current()->ScrollY() : 0.0f;
    m_dust.Update(dt, W, H, scroll);

    bool overUI = m_topbar.HoveringAny() || (m_router.Current() && m_router.Current()->OverInteractive());
    m_cursor.Update(dt, m_input, overUI);

    //
    m_gfx.BeginFrame();

    PaperShader::Params pp;
    pp.time     = m_time;
    pp.scroll   = scroll;
    pp.reveal   = m_loader && m_router.CurrentId() == L"loader" ? m_loader->RevealAmount() : 1.0f;
    pp.vignette = m_pal.dark ? 0.22f : 0.14f;
    pp.grain    = m_pal.dark ? 0.038f : 0.030f;
    pp.parallax = 0.05f;
    m_paper.Render(m_gfx, m_pal, pp);  

    m_gfx.BeginD2D();
    m_canvas.BeginFrame(m_pal);

    m_dust.Paint(m_canvas);
    m_router.Paint(m_canvas);

    //
    if (full) {
        m_topbar.Paint(m_canvas, true);
    } else if (barH > 0.5f) {
        m_canvas.PushTransform(D2D1::Matrix3x2F::Translation(0.0f, barH - shape::kTitleBar));
        m_canvas.PushOpacity(Clamp01(barH / shape::kTitleBar));
        m_topbar.Paint(m_canvas, false);
        m_canvas.PopOpacity();
        m_canvas.PopTransform();
    }

    if (!full) FloatingPlayer::Instance().Paint(m_canvas, D2D1_RECT_F{ 0.0f, 0.0f, W, H });

    // 退出确认弹层（最顶层，光标之下）
    if (m_exitAsk) ExitAskPaint(m_canvas, W, H);

    m_cursor.Paint(m_canvas);

    m_gfx.EndD2D();
    m_gfx.Present(true);

    // EDIT boxes no longer force-repainted per frame (WS_CLIPCHILDREN)
    m_input.NewFrame();
    ++m_frameCount;

    //
    if (m_shotMode && !m_shotDone && m_time >= m_shotAt) {
        m_shotDone = true;
        bool ok = SaveBackBufferPNG(m_gfx, m_shotPath);
        LogLine(L"[shot] %s route=%s scroll=%.0f file=%s",
                ok ? L"saved" : L"failed",
                m_router.CurrentId().c_str(), scroll, m_shotPath.c_str());
        PostQuitMessage(0);
    }
}

// ============================================================
//  退出确认弹层（设置 → 通用 → 「点关闭按钮时」可改默认行为）
// ============================================================
void App::OnCloseRequest()
{
    AppSettings s = CheckinStore::Instance().LoadSettings();
    int act = s.exitAction;
    if (act < 0 || act > 2) act = 0;
    if (act == 1) { m_window.ForceClose(); return; }                 // 直接退出
    if (act == 2) { TrayIcon::Instance().MinimizeToTray(); return; } // 最小化到托盘
    m_exitAsk = true;                                                // 每次询问
    m_exitT = 0.0f;
    m_exitRemember = false;
}

void App::ApplyExitChoice(int action)
{
    AppSettings s = CheckinStore::Instance().LoadSettings();
    s.exitAction = action;
    CheckinStore::Instance().SaveSettings(s);
}

void App::ExitAskLayout(float W, float H)
{
    const float cw = 400.0f, ch = 218.0f;
    float cx = (W - cw) * 0.5f;
    float cy = (H - ch) * 0.5f;
    m_exitCard    = { cx, cy, cx + cw, cy + ch };
    float by = cy + ch - 62.0f;
    m_exitBtnTray = { cx + 24.0f, by, cx + 178.0f, by + 42.0f };
    m_exitBtnQuit = { cx + cw - 196.0f, by, cx + cw - 24.0f, by + 42.0f };
    m_exitChk     = { cx + 24.0f, by - 36.0f, cx + 40.0f, by - 20.0f };
}

bool App::ExitAskUpdate()
{
    m_exitT = (std::min)(1.0f, m_exitT + m_input.dt * 5.0f);
    if (m_input.clicked) {
        float mx = m_input.mouseX, my = m_input.mouseY;
        auto hit = [](const D2D1_RECT_F& r, float x, float y) {
            return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
        };
        if (hit(m_exitBtnQuit, mx, my)) {
            if (m_exitRemember) ApplyExitChoice(1);
            m_exitAsk = false;
            m_window.ForceClose();
            return true;
        }
        if (hit(m_exitBtnTray, mx, my)) {
            if (m_exitRemember) ApplyExitChoice(2);
            m_exitAsk = false;
            TrayIcon::Instance().MinimizeToTray();
            return true;
        }
        if (hit(m_exitChk, mx, my)) m_exitRemember = !m_exitRemember;
    }
    return true;   // 弹层期间吃掉一切输入
}

void App::ExitAskPaint(Canvas& cv, float W, float H)
{
    const Palette& pal = cv.Pal();
    float e = ease::OutCubic(Clamp01(m_exitT));

    // 半透明遮罩
    cv.PushOpacity(0.55f * e);
    cv.FillRect({ 0.0f, 0.0f, W, H }, pal.ink900);
    cv.PopOpacity();

    // 卡片（轻微上滑弹入）
    cv.PushOpacity(e);
    float ty = (1.0f - e) * 26.0f;
    D2D1_RECT_F card = m_exitCard;
    card.top += ty; card.bottom += ty;
    cv.FillRoundRect(card, shape::kEdge, pal.paperHi);
    cv.StrokeRoundRect(card, shape::kEdge, pal.rule, shape::kHair);

    float tx = card.left + 28.0f;
    float rx = card.right - 28.0f;
    TextStyle h1; h1.role = FontRole::Serif; h1.size = 21.0f;
    h1.weight = DWRITE_FONT_WEIGHT_BLACK; h1.letterSpacing = 2.0f;
    cv.Text(L"退出芙洛理", { tx, card.top + 26.0f, rx, card.top + 54.0f }, h1, pal.ink900);
    TextStyle desc; desc.role = FontRole::Sans; desc.size = 12.0f;
    cv.Text(L"确定要退出吗？也可以先最小化到托盘，保持专注提醒。",
            { tx, card.top + 62.0f, rx, card.top + 84.0f }, desc, pal.ink500);

    // 「不再提示」勾选框
    D2D1_RECT_F chk = m_exitChk;
    chk.top += ty; chk.bottom += ty;
    if (m_exitRemember) {
        cv.FillRoundRect(chk, shape::kEdgeSoft, pal.seal);
        float cxm = (chk.left + chk.right) * 0.5f;
        float cym = (chk.top + chk.bottom) * 0.5f;
        cv.Line(cxm - 3.5f, cym + 0.5f, cxm - 1.0f, cym + 3.0f, pal.paperHi, 1.8f);
        cv.Line(cxm - 1.0f, cym + 3.0f, cxm + 4.0f, cym - 3.0f, pal.paperHi, 1.8f);
    } else {
        cv.StrokeRoundRect(chk, shape::kEdgeSoft, pal.ink500, shape::kHair);
    }
    TextStyle ct; ct.role = FontRole::Sans; ct.size = 11.5f;
    cv.Text(L"不再提示，记住我的选择",
            { chk.right + 8.0f, chk.top - 3.0f, rx, chk.bottom + 3.0f }, ct, pal.ink500);

    // 按钮行
    D2D1_RECT_F bTray = m_exitBtnTray; bTray.top += ty; bTray.bottom += ty;
    D2D1_RECT_F bQuit = m_exitBtnQuit; bQuit.top += ty; bQuit.bottom += ty;
    bool hotTray = m_input.mouseX >= bTray.left && m_input.mouseX <= bTray.right &&
                   m_input.mouseY >= bTray.top && m_input.mouseY <= bTray.bottom;
    bool hotQuit = m_input.mouseX >= bQuit.left && m_input.mouseX <= bQuit.right &&
                   m_input.mouseY >= bQuit.top && m_input.mouseY <= bQuit.bottom;
    cv.FillRoundRect(bTray, shape::kEdge, hotTray ? pal.paperLo : pal.paperHi);
    cv.StrokeRoundRect(bTray, shape::kEdge, hotTray ? pal.ink700 : pal.rule, shape::kHair);
    TextStyle bs; bs.role = FontRole::Sans; bs.size = 13.0f;
    bs.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    bs.hAlign = HAlign::Center; bs.vAlign = VAlign::Middle;
    cv.Text(L"最小化到托盘", bTray, bs, pal.ink700);

    cv.FillRoundRect(bQuit, shape::kEdge, pal.seal);
    cv.Text(L"退出程序", bQuit, bs, pal.paperHi);

    cv.PopOpacity();
}

} // namespace lj
