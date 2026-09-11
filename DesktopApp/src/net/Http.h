#pragma once
// ============================================================
//  Http.h — 极简同步 HTTP 客户端（WinHTTP，零第三方依赖）
//  只覆盖与「芙洛理」服务端对话所需：JSON 请求体 + Bearer 令牌 + 短超时。
//  · 同步阻塞调用，务必放在后台线程或明确可接受停顿的路径上。
//  · 传输失败（服务未开 / 断网）与业务失败（4xx/5xx）分开表达，
//    上层据此决定「离线降级」还是「向用户报错」。
// ============================================================
#include <string>

namespace lj::net {

// 服务端地址。默认对应 Server/ 工程的本机默认监听。
struct Endpoint
{
    std::wstring host = L"127.0.0.1";
    int          port = 8787;
    bool         secure = false;      // true → https

    std::wstring Text() const;        // "127.0.0.1:8787"
};

struct Response
{
    bool         transport = false;   // 是否拿到了 HTTP 响应（false = 网络层失败）
    int          status = 0;          // HTTP 状态码
    std::string  body;                // 响应体（UTF-8 原文）
    std::wstring error;               // 传输层错误描述（transport=false 时有值）

    bool Ok() const { return transport && status >= 200 && status < 300; }
    bool Offline() const { return !transport; }
};

/**
 * 发起一次同步请求。
 * @param method        L"GET" / L"POST" / L"PUT" / L"DELETE"
 * @param pathAndQuery  形如 L"/sync/all?mode=merge"
 * @param body          UTF-8 请求体（JSON 或二进制皆可，按 contentType 解释；空串表示无体）
 * @param bearer        非空时附加 Authorization: Bearer <token>
 * @param timeoutMs     连接/收发超时（毫秒）
 * @param contentType   请求体类型，默认 JSON；上传二进制时传 "application/octet-stream"
 * @param extraHeaders  额外请求头（调用方自带 "\r\n" 结尾），如媒体上传的 X-Meta
 */
Response Request(const Endpoint& ep,
                 const std::wstring& method,
                 const std::wstring& pathAndQuery,
                 const std::string& body = std::string(),
                 const std::string& bearer = std::string(),
                 int timeoutMs = 6000,
                 const std::string& contentType = "application/json; charset=utf-8",
                 const std::string& extraHeaders = std::string());

// ---------------- 编码小工具（Cloud 层共用）----------------
std::string  ToUtf8(const std::wstring& s);
std::wstring FromUtf8(const std::string& s);

} // namespace lj::net
