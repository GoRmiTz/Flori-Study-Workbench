// ============================================================
//  Http.cpp — WinHTTP 实现
//  WinHTTP 是系统自带组件（winhttp.lib），不引入任何第三方依赖。
// ============================================================
#include "net/Http.h"

#include <windows.h>
#include <winhttp.h>

namespace lj::net {

// ---------------- 编码 ----------------
std::string ToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string o; o.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n, nullptr, nullptr);
    return o;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring o; o.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n);
    return o;
}

std::wstring Endpoint::Text() const
{
    return host + L":" + std::to_wstring(port);
}

// ---------------- 句柄守卫 ----------------
namespace {

struct HGuard
{
    HINTERNET h = nullptr;
    explicit HGuard(HINTERNET x = nullptr) : h(x) {}
    ~HGuard() { if (h) WinHttpCloseHandle(h); }
    HGuard(const HGuard&) = delete;
    HGuard& operator=(const HGuard&) = delete;
    operator HINTERNET() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

std::wstring LastErrText(const wchar_t* stage)
{
    DWORD e = GetLastError();
    wchar_t buf[160];
    switch (e) {
        case ERROR_WINHTTP_CANNOT_CONNECT:
            return L"无法连接服务端（服务未启动或地址有误）";
        case ERROR_WINHTTP_TIMEOUT:
            return L"连接服务端超时";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return L"服务端域名无法解析";
        case ERROR_WINHTTP_CONNECTION_ERROR:
            return L"与服务端的连接被中断";
        case ERROR_WINHTTP_SECURE_FAILURE:
            return L"HTTPS 证书校验失败";
        default:
            swprintf_s(buf, L"网络请求失败（%s，错误码 %lu）", stage, (unsigned long)e);
            return buf;
    }
}

Response Offline(const wchar_t* stage)
{
    Response r;
    r.transport = false;
    r.error = LastErrText(stage);
    return r;
}

} // namespace

// ---------------- 请求 ----------------
Response Request(const Endpoint& ep,
                 const std::wstring& method,
                 const std::wstring& pathAndQuery,
                 const std::string& body,
                 const std::string& bearer,
                 int timeoutMs,
                 const std::string& contentType,
                 const std::string& extraHeaders)
{
    HGuard session(WinHttpOpen(L"Flori-Desktop/1.0",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return Offline(L"初始化");

    WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    HGuard conn(WinHttpConnect(session, ep.host.c_str(), (INTERNET_PORT)ep.port, 0));
    if (!conn) return Offline(L"建立连接");

    DWORD flags = ep.secure ? WINHTTP_FLAG_SECURE : 0;
    HGuard req(WinHttpOpenRequest(conn, method.c_str(), pathAndQuery.c_str(),
                                  nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req) return Offline(L"创建请求");

    // 请求头
    std::wstring headers = L"Content-Type: " + FromUtf8(contentType) + L"\r\n";
    if (!bearer.empty()) headers += L"Authorization: Bearer " + FromUtf8(bearer) + L"\r\n";
    if (!extraHeaders.empty()) headers += FromUtf8(extraHeaders);
    if (!WinHttpAddRequestHeaders(req, headers.c_str(), (DWORD)-1L,
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        return Offline(L"写入请求头");
    }

    BOOL sent = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                   (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!sent) return Offline(L"发送请求");

    if (!WinHttpReceiveResponse(req, nullptr)) return Offline(L"接收响应");

    Response out;
    out.transport = true;

    DWORD status = 0, len = sizeof(status);
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX)) {
        out.status = (int)status;
    }

    // 读全部响应体
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) break;
        if (avail == 0) break;
        size_t base = out.body.size();
        out.body.resize(base + avail);
        DWORD read = 0;
        if (!WinHttpReadData(req, &out.body[base], avail, &read)) {
            out.body.resize(base);
            break;
        }
        out.body.resize(base + read);
        if (read == 0) break;
    }

    return out;
}

} // namespace lj::net
