// ============================================================
//  main.cpp — 进程入口
//  芙洛理 Flori · Windows 原生桌面端
//  D3D11 跑纸基着色器，Direct2D/DirectWrite 叠矢量 UI，无 WebView
// ============================================================
#include "core/App.h"
#include <shellscalingapi.h>
#include <windows.h>

#pragma comment(lib, "shcore.lib")

// 单实例互斥：避免双击 exe / 重复点击启动多个进程，
// 导致多个主窗口精准叠在同一坐标（表现为「关闭一个又露出另一个」）。
static const wchar_t* kSingleInstanceMutex = L"Local\\FloriDesktopSingleInstance_v1";

// 自定义消息：第二个实例用来请求已有实例把主窗口提到前台（在自己线程里 SetForegroundWindow 才被允许）
static UINT g_wmBringFront = 0;

static BOOL CALLBACK FindFloriWindow(HWND hwnd, LPARAM)
{
    wchar_t cls[64] = {};
    if (GetClassNameW(hwnd, cls, 64) && wcscmp(cls, L"FloriDossierWindow") == 0) {
        // 已在运行的实例可能最小化到托盘（窗口被 Hide）或普通最小化：
        // 直接请它自己恢复并置前（跨进程 SetForegroundWindow 会被系统限流，故走消息）。
        if (g_wmBringFront) PostMessageW(hwnd, g_wmBringFront, 0, 0);
        return FALSE; // 找到一个即可
    }
    return TRUE;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int nCmdShow)
{
    // 每显示器 DPI 感知：多屏不同缩放时不糊
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    g_wmBringFront = RegisterWindowMessageW(L"Flori.Desktop.BringFront");

    // 无头截图/CI 模式（--shot）：跳过单实例互斥。
    // 否则真机开着程序时，自检进程会被互斥拦掉静默退出（exit 0 但不出图），
    // 表现为「截图命令成功但文件不存在」，且每次验证都要先关掉用户正开着的程序。
    bool shotMode = cmdLine && wcsstr(cmdLine, L"--shot") != nullptr;

    // 单实例：先尝试拿互斥量
    HANDLE hMutex = nullptr;
    if (!shotMode) {
        hMutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
        DWORD mutexErr = GetLastError();
        if (hMutex == nullptr || mutexErr == ERROR_ALREADY_EXISTS) {
            // 已有实例在运行：把它的主窗口提到前台（支持托盘隐藏/最小化恢复）
            if (g_wmBringFront) EnumWindows(FindFloriWindow, 0);
            if (hMutex) CloseHandle(hMutex);
            return 0;
        }
    }

    // OLE STA 初始化：RegisterDragDrop（知识库/浮窗文件拖入）依赖 OleInitialize，
    // 只调 CoInitializeEx 时拖放目标会静默注册失败（表现为「文件拖不进软件」）。
    HRESULT coHr = OleInitialize(nullptr);
    bool oleOk = SUCCEEDED(coHr);
    if (!oleOk) {
        // 极少见：线程已以别的模式初始化过。退回纯 COM，功能降级（拖放不可用）。
        coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    }

    int code;
    {
        lj::App app;
        code = app.Run(hInst, nCmdShow, cmdLine ? cmdLine : L"");
    }

    if (oleOk) OleUninitialize();
    else if (SUCCEEDED(coHr)) CoUninitialize();
    if (hMutex) CloseHandle(hMutex);
    return code;
}
