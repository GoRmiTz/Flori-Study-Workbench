#include "core/Common.h"
#include <cstdio>
#include <cstdarg>
#include <mutex>

namespace lj {

static std::wstring g_logPath;
static std::mutex   g_logMutex;

std::wstring ExeDir()
{
    static std::wstring cached;
    if (!cached.empty()) return cached;
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t slash = p.find_last_of(L"\\/");
    cached = (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
    return cached;
}

void LogInit()
{
    g_logPath = ExeDir() + L"\\flori.log";
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_logPath.c_str(), L"w, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"=== 芙洛理 Flori 桌面端 日志 ===\n");
        fclose(f);
    }
}

void LogLine(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _TRUNCATE, fmt, ap);
    va_end(ap);

    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logPath.empty()) return;
    FILE* f = nullptr;
    if (_wfopen_s(&f, g_logPath.c_str(), L"a, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"%s\n", buf);
        fclose(f);
    }
}

void LogHR(const wchar_t* what, HRESULT hr)
{
    LogLine(L"[HR 0x%08X] %s", static_cast<unsigned>(hr), what);
}

std::wstring ResolveAsset(const std::wstring& relative)
{
    std::wstring a = ExeDir() + L"\\assets\\" + relative;
    if (GetFileAttributesW(a.c_str()) != INVALID_FILE_ATTRIBUTES) return a;
#ifdef FLORI_SOURCE_DIR
    // 开发期回落：直接读源码树，方便改完热重载
    std::wstring srcDir;
    {
        const char* s = FLORI_SOURCE_DIR;
        int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
        srcDir.resize(n > 0 ? n - 1 : 0);
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, srcDir.data(), n);
    }
    std::wstring b = srcDir + L"\\assets\\" + relative;
    if (GetFileAttributesW(b.c_str()) != INVALID_FILE_ATTRIBUTES) return b;
#endif
    return a;
}

bool ReadTextFile(const std::wstring& path, std::string& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > (64 << 20)) {
        CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(read);
    return true;
}

uint64_t FileWriteTime(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    ULARGE_INTEGER u;
    u.LowPart = d.ftLastWriteTime.dwLowDateTime;
    u.HighPart = d.ftLastWriteTime.dwHighDateTime;
    return u.QuadPart;
}

} // namespace lj
