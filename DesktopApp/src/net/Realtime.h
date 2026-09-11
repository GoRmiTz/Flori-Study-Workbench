#pragma once
// ============================================================
//  Realtime.h — §3 实时自习室 WebSocket 客户端
//  传输层：手写 RFC 6455 over Winsock（ws2_32 / bcrypt 均为系统库，零第三方依赖）。
//
//  对接口径：Server/src/realtime/gateway.js（同端口同源，连上先发 auth）。
//  设计要点：
//   · 单后台线程持有 TCP 长连接，UI 线程只「投递」上行消息。
//   · 下行消息经 On(type, cb) 分发到视图注册的回调；回调里禁止碰渲染，
//     只能写受锁缓冲 + 置原子标记，由视图 Update() 在 UI 线程重排。
//   · 断线指数退避重连；账户切换/令牌过期时由 Cloud 调 Reconnect() 重连。
//   · 不直接 include app/Cloud.h（避免环依赖）；端点与令牌由 App 注入 provider。
// ============================================================
#include <string>
#include <functional>
#include <mutex>
#include <thread>
#include <atomic>
#include <map>
#include <cstdint>

#include "net/Http.h"
#include "app/Json.h"

namespace lj {

class Realtime
{
public:
    using Handler = std::function<void(const lj::json::JVal&)>;

    static Realtime& Instance();

    // App 注入：取端点 + 取当前 access 令牌（+ 可选：令牌失效时刷新）。
    // allowAnon=true 时即使无令牌也连（访客只读模式，服务端给匿名身份）。
    void Init(std::function<net::Endpoint()> epProvider,
              std::function<std::string()> tokenProvider,
              std::function<bool()> refreshProvider = {},
              bool allowAnon = false);
    void Shutdown();
    void Reconnect();                       // 账户切换 / 令牌刷新后调用（重连拿新令牌）
    void SetAllowAnonymous(bool on);        // 访客模式切换：空令牌是否允许连（服务端匿名只读）
    bool Connected() const { return m_connected.load(); }

    // 视图注册 / 注销下行处理器（OnEnter 注册，OnLeave 注销）
    void On(const std::string& type, Handler cb);
    void Off(const std::string& type);

    // 上行便捷方法（内部 Send；客户端帧按 RFC 6455 强制加掩码）
    void JoinRoom(const std::string& id);
    void LeaveRoom();
    void SendChat(const std::string& text);
    void SendDm(const std::string& to, const std::string& text);       // #37 私聊（仅好友）
    void RequestDmHistory(const std::string& with);                    // #37 拉私聊历史
    void SendPresence(const std::string& status, const std::string& focusContent, int focusSeconds);
    void RequestMusic();
    void SendRaw(const std::string& json);

private:
    Realtime() = default;
    ~Realtime() = default;
    Realtime(const Realtime&) = delete;
    Realtime& operator=(const Realtime&) = delete;

    void Thread();                          // 后台主循环：连→收→断线退避重连
    bool ConnectAndRun();                   // 单次连接生命周期（返回 true = 握手曾成功）
    void Send(const lj::json::JVal& m);
    void SendAuth();
    void HandleFrame(const std::string& text);
    void Dispatch(const std::string& type, const lj::json::JVal& m);

    bool SendOp(unsigned char opcode, const std::string& payload);  // 取锁后发一帧
    void CloseSock();                       // 关连接（唤醒阻塞在 recv 的后台线程）

    std::function<net::Endpoint()> m_epProvider;
    std::function<std::string()>    m_tokenProvider;
    std::function<bool()>           m_refreshProvider;

    mutable std::mutex m_mu;                // 保护 m_handlers 与 m_sock
    std::map<std::string, Handler> m_handlers;
    std::uintptr_t m_sock = (std::uintptr_t)-1;   // SOCKET，(uintptr_t)-1 = INVALID_SOCKET

    std::thread      m_thread;
    std::atomic<bool> m_quit{ false };
    std::atomic<bool> m_connected{ false };
    std::atomic<bool> m_wake{ false };      // Reconnect 置位：跳过剩余退避，立即重连
    bool m_allowAnon = false;               // 访客只读：空令牌也允许连（服务端给匿名身份）
};

} // namespace lj
