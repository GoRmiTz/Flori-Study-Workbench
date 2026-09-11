#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// ============================================================
//  Realtime.cpp — §3 实时自习室 WebSocket 客户端
//  传输层：手写 RFC 6455 over Winsock（ws2_32 / bcrypt 均为 Windows 系统库）。
//
//  为何不用 WinHTTP 的 WebSocket API：
//    实测 WinHttpWebSocketCompleteUpgrade 在目标环境上恒定返回
//    ERROR_WINHTTP_INVALID_SERVER_RESPONSE(12152)——即便服务端回包完全合规
//    （已用裸 TCP 复核：101 状态行与 Sec-WebSocket-Accept 均正确），
//    连微软官方示例的逐字复刻亦然。故自行实现握手与帧编解码：行为完全可控，
//    且与服务端 Server/src/realtime/ws.js 的手写实现口径一致。
//
//  对接口径：Server/src/realtime/gateway.js（与 REST 同端口同源，连上先发 auth）。
// ============================================================
#include "net/Realtime.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <vector>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace lj {

using namespace lj::json;

namespace {

// RFC 6455 操作码
enum : unsigned char { OP_CONT = 0x0, OP_TEXT = 0x1, OP_BIN = 0x2,
                       OP_CLOSE = 0x8, OP_PING = 0x9, OP_PONG = 0xA };

const char* kGuid = "258EAFA5-E914-47DA-95CA-5AB0DC85B11F";
const size_t kMaxMessage = 4u * 1024 * 1024;      // 与服务端 MAX_MESSAGE 对齐

// 构造 JSON 叶子的小助手
JVal S(const std::string& s) { JVal v; v.type = JVal::Str; v.str = s; return v; }
JVal N(double n)             { JVal v; v.type = JVal::Num; v.num = n; return v; }

void EnsureWinsock()
{
    static bool once = [] {
        WSADATA wsa{};
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    (void)once;
}

std::string B64(const unsigned char* p, size_t n)
{
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o; o.reserve(((n + 2) / 3) * 4);
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)p[i] << 16;
        if (i + 1 < n) v |= (unsigned)p[i + 1] << 8;
        if (i + 2 < n) v |= (unsigned)p[i + 2];
        o += T[(v >> 18) & 63];
        o += T[(v >> 12) & 63];
        o += (i + 1 < n) ? T[(v >> 6) & 63] : '=';
        o += (i + 2 < n) ? T[v & 63] : '=';
    }
    return o;
}

bool Sha1(const std::string& in, unsigned char out[20])
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) return false;
    NTSTATUS st = BCryptHash(alg, nullptr, 0, (PUCHAR)in.data(), (ULONG)in.size(), out, 20);
    BCryptCloseAlgorithmProvider(alg, 0);
    return st == 0;
}

void RandBytes(unsigned char* p, size_t n)
{
    if (BCryptGenRandom(nullptr, p, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) return;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)(GetTickCount() * 2654435761u >> (i & 7) & 0xFF);
}

std::string Lower(const std::string& s)
{
    std::string o = s;
    for (char& c : o) if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return o;
}

bool SendAll(SOCKET s, const char* p, size_t n)
{
    while (n) {
        int k = ::send(s, p, (int)n, 0);
        if (k <= 0) return false;
        p += k; n -= (size_t)k;
    }
    return true;
}

// 客户端→服务端的帧必须加掩码（RFC 6455 §5.3）
bool WriteFrame(SOCKET s, unsigned char opcode, const char* data, size_t len)
{
    std::vector<unsigned char> h;
    h.push_back((unsigned char)(0x80 | opcode));          // FIN + opcode
    if (len < 126) {
        h.push_back((unsigned char)(0x80 | len));
    } else if (len <= 0xFFFF) {
        h.push_back(0x80 | 126);
        h.push_back((unsigned char)(len >> 8));
        h.push_back((unsigned char)(len & 0xFF));
    } else {
        h.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i) h.push_back((unsigned char)(((unsigned long long)len >> (i * 8)) & 0xFF));
    }
    unsigned char mask[4]; RandBytes(mask, 4);
    h.insert(h.end(), mask, mask + 4);

    std::vector<unsigned char> body(len);
    for (size_t i = 0; i < len; ++i) body[i] = (unsigned char)data[i] ^ mask[i & 3];

    if (!SendAll(s, (const char*)h.data(), h.size())) return false;
    if (len && !SendAll(s, (const char*)body.data(), body.size())) return false;
    return true;
}

// HTTP 升级握手。成功时 carry 带回「响应头之后已经读到的字节」（可能已含首帧）。
bool Handshake(SOCKET s, const net::Endpoint& ep, std::string& carry)
{
    unsigned char raw[16]; RandBytes(raw, 16);
    std::string key = B64(raw, 16);

    char hostPort[320];
    std::string host = net::ToUtf8(ep.host);
    _snprintf(hostPort, sizeof(hostPort), "%s:%d", host.c_str(), ep.port);

    std::string req =
        "GET / HTTP/1.1\r\n"
        "Host: " + std::string(hostPort) + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + key + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";
    if (!SendAll(s, req.data(), req.size())) return false;

    // 读到响应头结束
    std::string resp;
    char tmp[2048];
    size_t hdrEnd = std::string::npos;
    for (int guard = 0; guard < 64; ++guard) {
        int k = ::recv(s, tmp, sizeof(tmp), 0);
        if (k <= 0) return false;
        resp.append(tmp, (size_t)k);
        hdrEnd = resp.find("\r\n\r\n");
        if (hdrEnd != std::string::npos) break;
        if (resp.size() > 16384) return false;
    }
    if (hdrEnd == std::string::npos) return false;

    std::string head = resp.substr(0, hdrEnd);
    carry.assign(resp, hdrEnd + 4, std::string::npos);

    std::string lower = Lower(head);
    if (lower.compare(0, 8, "http/1.1") != 0 || lower.find(" 101") == std::string::npos) return false;

    // 校验 Sec-WebSocket-Accept
    size_t at = lower.find("sec-websocket-accept:");
    if (at == std::string::npos) return false;
    size_t vs = head.find(':', at) + 1;
    size_t ve = head.find("\r\n", vs);
    std::string got = head.substr(vs, (ve == std::string::npos ? head.size() : ve) - vs);
    while (!got.empty() && (got.front() == ' ' || got.front() == '\t')) got.erase(got.begin());
    while (!got.empty() && (got.back() == ' ' || got.back() == '\r' || got.back() == '\t')) got.pop_back();

    unsigned char sha[20];
    if (!Sha1(key + kGuid, sha)) return false;
    return got == B64(sha, 20);
}

} // namespace

Realtime& Realtime::Instance()
{
    static Realtime s;
    return s;
}

void Realtime::Init(std::function<net::Endpoint()> epProvider,
                    std::function<std::string()> tokenProvider,
                    std::function<bool()> refreshProvider,
                    bool allowAnon)
{
    m_epProvider = epProvider;
    m_tokenProvider = tokenProvider;
    m_refreshProvider = refreshProvider;
    m_allowAnon = allowAnon;
    if (m_thread.joinable()) return;
    m_quit = false;
    m_thread = std::thread([this] { Thread(); });
}

void Realtime::SetAllowAnonymous(bool on)
{
    m_allowAnon = on;
}

void Realtime::CloseSock()
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_sock != (std::uintptr_t)INVALID_SOCKET) {
        ::closesocket((SOCKET)m_sock);
        m_sock = (std::uintptr_t)INVALID_SOCKET;
    }
}

void Realtime::Shutdown()
{
    m_quit = true;
    m_wake = true;
    CloseSock();                         // 唤醒阻塞在 recv 的后台线程
    if (m_thread.joinable()) m_thread.join();
}

void Realtime::Reconnect()
{
    m_wake = true;                       // 跳过剩余退避
    CloseSock();                         // 断开当前连接，后台线程会用新令牌重连
}

void Realtime::On(const std::string& type, Handler cb)
{
    std::lock_guard<std::mutex> lk(m_mu);
    m_handlers[type] = cb;
}

void Realtime::Off(const std::string& type)
{
    std::lock_guard<std::mutex> lk(m_mu);
    m_handlers.erase(type);
}

bool Realtime::SendOp(unsigned char opcode, const std::string& payload)
{
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_sock == (std::uintptr_t)INVALID_SOCKET) return false;
    return WriteFrame((SOCKET)m_sock, opcode, payload.data(), payload.size());
}

void Realtime::SendAuth()
{
    JVal m; m.type = JVal::Obj;
    m.obj["type"] = S("auth");
    m.obj["token"] = S(m_tokenProvider ? m_tokenProvider() : std::string());
    Send(m);
}

void Realtime::Send(const JVal& m) { SendRaw(JDump(m)); }

void Realtime::SendRaw(const std::string& s)
{
    SendOp(OP_TEXT, s);                  // 未连接时 SendOp 直接返回 false（上行非关键，丢弃）
}

void Realtime::JoinRoom(const std::string& id)
{
    JVal m; m.type = JVal::Obj;
    m.obj["type"] = S("room:join");
    m.obj["room"] = S(id);
    Send(m);
}

void Realtime::LeaveRoom()
{
    JVal m; m.type = JVal::Obj;
    m.obj["type"] = S("room:leave");
    Send(m);
}

void Realtime::SendChat(const std::string& text)
{
    if (text.empty()) return;
    JVal m; m.type = JVal::Obj;
    m.obj["type"] = S("chat:msg");
    m.obj["text"] = S(text);
    Send(m);
}

void Realtime::SendDm(const std::string& to, const std::string& text)
{
    if (to.empty() || text.empty()) return;
    JVal m; m.type = JVal::Obj;
    m.obj["type"] = S("chat:dm");
    m.obj["to"] = S(to);
    m.obj["text"] = S(text);
    Send(m);
}

void Realtime::RequestDmHistory(const std::string& with)
{
    if (with.empty()) return;
    JVal m; m.type = JVal::Obj;
    m.obj["type"] = S("dm:history");
    m.obj["with"] = S(with);
    Send(m);
}

void Realtime::SendPresence(const std::string& status, const std::string& focusContent, int focusSeconds)
{
    JVal m; m.type = JVal::Obj;
    m.obj["type"] = S("presence");
    m.obj["status"] = S(status);
    m.obj["focusContent"] = S(focusContent);
    m.obj["focusSeconds"] = N(focusSeconds);
    Send(m);
}

void Realtime::RequestMusic()
{
    JVal m; m.type = JVal::Obj;
    m.obj["type"] = S("music:list");
    Send(m);
}

void Realtime::HandleFrame(const std::string& text)
{
    JVal m = Parser(text.data(), text.size()).parse();
    const JVal* t = JGet(m, "type");
    if (!t || t->type != JVal::Str) return;
    if (t->str == "auth:ok") {
        m_connected = true;
    } else if (t->str == "auth:err") {
        m_connected = false;
        // 令牌失效：先刷新，Thread 退避重连时会经 provider 取到新令牌
        if (m_refreshProvider) m_refreshProvider();
    }
    Dispatch(t->str, m);
}

void Realtime::Dispatch(const std::string& type, const JVal& m)
{
    Handler cb;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        auto it = m_handlers.find(type);
        if (it == m_handlers.end()) return;
        cb = it->second;
    }
    if (cb) cb(m);     // 回调里禁止碰渲染：只写受锁缓冲 + 置原子标记
}

bool Realtime::ConnectAndRun()
{
    auto ep = m_epProvider ? m_epProvider() : net::Endpoint{};
    std::string token = m_tokenProvider ? m_tokenProvider() : std::string();
    if (token.empty() && !m_allowAnon) return false;   // 未登录且非访客模式，Thread 会重试

    EnsureWinsock();

    char portStr[16];
    _snprintf(portStr, sizeof(portStr), "%d", ep.port);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    std::string host = net::ToUtf8(ep.host);
    if (::getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || !res) return false;

    SOCKET s = INVALID_SOCKET;
    for (addrinfo* p = res; p; p = p->ai_next) {
        s = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (::connect(s, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        ::closesocket(s); s = INVALID_SOCKET;
    }
    ::freeaddrinfo(res);
    if (s == INVALID_SOCKET) return false;

    BOOL nodelay = TRUE;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    DWORD rcvto = 1000;                            // 收超时：让 recv 定期返回以检查退出标记
    ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvto, sizeof(rcvto));

    std::string carry;
    if (!Handshake(s, ep, carry)) { ::closesocket(s); return false; }

    { std::lock_guard<std::mutex> lk(m_mu); m_sock = (std::uintptr_t)s; }

    m_connected = false;
    SendAuth();                                    // 连上后第一帧必须是 auth

    // ---- 收帧循环 ----
    std::string buf = carry;
    std::string fragBody; unsigned char fragOp = 0; bool inFrag = false;
    bool closing = false;

    while (!m_quit && !closing) {
        // 先把缓冲里能解出的帧全部解掉
        for (;;) {
            if (buf.size() < 2) break;
            const unsigned char* p = (const unsigned char*)buf.data();
            bool fin = (p[0] & 0x80) != 0;
            unsigned char op = p[0] & 0x0F;
            bool masked = (p[1] & 0x80) != 0;      // 服务端→客户端本不应加掩码，容错处理
            unsigned long long len = (unsigned long long)(p[1] & 0x7F);

            size_t need = 2;
            if (len == 126) {
                need = 4; if (buf.size() < need) break;
                len = ((unsigned long long)p[2] << 8) | p[3];
            } else if (len == 127) {
                need = 10; if (buf.size() < need) break;
                len = 0; for (int i = 0; i < 8; ++i) len = (len << 8) | p[2 + i];
            }
            size_t maskOff = need;
            if (masked) { need += 4; if (buf.size() < need) break; }
            if (len > kMaxMessage) { closing = true; break; }
            if (buf.size() < need + (size_t)len) break;

            std::string payload(buf.data() + need, (size_t)len);
            if (masked) {
                const unsigned char* mk = p + maskOff;
                for (size_t i = 0; i < payload.size(); ++i)
                    payload[i] = (char)((unsigned char)payload[i] ^ mk[i & 3]);
            }
            buf.erase(0, need + (size_t)len);

            if (op == OP_PING)  { SendOp(OP_PONG, payload); continue; }
            if (op == OP_PONG)  { continue; }
            if (op == OP_CLOSE) { closing = true; break; }

            if (op == OP_CONT) {                    // 续帧
                if (!inFrag) continue;
                if (fragBody.size() + payload.size() > kMaxMessage) { closing = true; break; }
                fragBody += payload;
                if (fin) {
                    if (fragOp == OP_TEXT) HandleFrame(fragBody);
                    fragBody.clear(); inFrag = false;
                }
                continue;
            }
            // OP_TEXT / OP_BIN
            if (!fin) { inFrag = true; fragOp = op; fragBody = payload; continue; }
            if (op == OP_TEXT) HandleFrame(payload);
        }
        if (closing || m_quit) break;

        char tmp[8192];
        int k = ::recv(s, tmp, sizeof(tmp), 0);
        if (k > 0) { buf.append(tmp, (size_t)k); continue; }
        if (k == 0) break;                          // 对端正常关闭
        if (::WSAGetLastError() == WSAETIMEDOUT) continue;   // 只是收超时
        break;                                      // 连接错误 / 被 CloseSock 关闭
    }

    CloseSock();                                    // 幂等（Reconnect 可能已关过）
    m_connected = false;
    return true;
}

void Realtime::Thread()
{
    int backoff = 2000;
    while (!m_quit) {
        if (!m_epProvider || !m_tokenProvider) { Sleep(500); continue; }
        if (m_tokenProvider().empty() && !m_allowAnon) { Sleep(1000); continue; }   // 未登录且非访客，等

        bool handshook = ConnectAndRun();
        if (m_quit) break;
        if (handshook) backoff = 2000;              // 连上过就重置退避

        // 分片睡眠：Reconnect() 置 m_wake 时立即醒来
        m_wake = false;
        for (int slept = 0; slept < backoff && !m_quit && !m_wake; slept += 250) Sleep(250);
        if (!m_wake) backoff = (backoff * 2 > 30000) ? 30000 : backoff * 2;  // 指数退避，上限 30s
    }
    m_connected = false;
}

} // namespace lj
