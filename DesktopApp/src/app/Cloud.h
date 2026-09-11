#pragma once
// ============================================================
//  Cloud.h — 云端对接层（对应 Server/ 工程的 §1 鉴权 + §2 同步）
//
//  设计原则：本地优先，云端是增强而非依赖。
//   · 服务端没开 / 断网 —— 应用一切照旧，全部功能走本地文件，不弹错、不卡界面。
//   · 登录成功 —— 后台拉取云端载荷与本地合并，再把合并结果推回去。
//   · 数据变更 —— 只打一个「脏」标记，由后台线程防抖后静默上推；
//                 UI 线程永远不做网络 IO。
//
//  令牌：访问令牌 + 刷新令牌，按账户存在 accounts/<用户名>/cloud.json；
//        服务端地址存在 accounts/cloud.json（所有账户共用一套后端）。
// ============================================================
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <functional>

#include "net/Http.h"
#include "app/AccountStore.h"   // AccountProfile（档案上云 P1-1）

namespace lj {

// 云端操作结果。区分「服务端明确拒绝」与「压根没连上」，
// 上层据此决定报错还是静默降级到本地。
enum class CloudResult
{
    Ok,          // 成功
    NotFound,    // 服务端不认识这个账户（登录 404）——多半是离线期在本机注册的
    Rejected,    // 服务端明确拒绝（密码错 / 重名 / 格式不合法…），err 有文案
    Offline,     // 没连上服务端
    Disabled,    // 用户关闭了云同步
};

struct CloudSession
{
    std::wstring uid;            // 服务端用户编号 u_XXXXXXXX
    std::wstring username;
    std::string  accessToken;
    std::string  refreshToken;
    long long    expiresAt = 0;  // 访问令牌到期时刻（epoch 秒）

    bool Valid() const { return !accessToken.empty(); }
};

class Cloud
{
public:
    static Cloud& Instance();

    // 程序启动时调用：读配置、读当前账户的令牌、起后台线程
    void Init();
    // 程序退出时调用：最后一次冲刷 + 停线程（阻塞，最多约 6 秒）
    void Shutdown();

    // ---------------- 配置 ----------------
    bool Enabled() const { return m_enabled.load(); }
    void SetEnabled(bool on);
    net::Endpoint EndpointCfg() const;
    void SetEndpoint(const std::wstring& host, int port, bool secure);

    // ---------------- 连通性 ----------------
    bool Online() const { return m_online.load(); }
    bool Probe();                     // GET /health（阻塞，短超时）
    void ProbeAsync();                // 丢给后台线程探测

    // ---------------- 账户 ----------------
    CloudResult Login(const std::wstring& name, const std::wstring& pass, std::wstring& err);
    CloudResult Register(const std::wstring& name, const std::wstring& pass, std::wstring& err);
    void Logout(bool allDevices = false);

    bool LoggedIn() const;
    std::wstring Uid() const;
    std::wstring BoundUser() const;   // 当前令牌属于哪个用户名
    std::string  AccessToken() const;  // 受 m_mu 保护的副本（Realtime/上传用）
    bool TryRefresh();                 // 主动刷新访问令牌（Realtime 令牌过期时调用）

    // 切换账户时调用：载入该账户的令牌（没有就是未登录云端）
    void BindAccount(const std::wstring& account);

    // ---------------- 同步 ----------------
    // 登录成功后走这条：拉云端 → 合并进本地 → 把合并结果推回云端。
    // 阻塞版本仅供后台线程使用。
    bool SyncNow(std::wstring& err);
    void SyncAsync();                 // 后台执行一次完整同步

    // 任何数据写盘之后调用：打脏标记，后台防抖 ~2.5 秒后静默上推
    void MarkDirty();
    // 立刻把脏数据推上去（阻塞，退出前用）
    void Flush();

    // ---------------- 专栏 & 媒体（§4 / §5）----------------
    //  均在后台线程调用（经 RunAsync）；内部走私有 Call，自动带 Bearer 并 401 自动刷新。
    net::Response GetColumns();                       // GET /columns
    net::Response GetColumn(const std::string& id);   // GET /columns/:id 全文（含正文+评论）
    net::Response GetMediaFile(const std::string& id); // GET /media/file/:id 原始字节（带 Bearer，软件内查看用）
    CloudResult   CreateColumn(const std::wstring& title, const std::wstring& body,
                               const std::wstring& sectionId, std::wstring& err); // POST /columns 发布公共专栏
    CloudResult   LikeColumn(const std::string& id, bool& on, int& count);   // POST /columns/:id/like
    CloudResult   FavColumn(const std::string& id, bool& on, int& count);    // POST /columns/:id/fav
    CloudResult   CommentColumn(const std::string& id, const std::wstring& text); // POST /columns/:id/comment
    net::Response GetMediaList();                     // GET /media/list
    CloudResult   UploadMedia(const std::string& kind, const std::string& title,
                              const std::string& note, const std::string& filename,
                              const std::string& bin, std::wstring& err);     // POST /media/upload (raw+X-Meta)
    CloudResult   DeleteMedia(const std::string& id); // DELETE /media/:id

    // ---------------- 档案（P1-1 · GET/PUT /auth/me）----------------
    //  本地优先：离线 → 返回 Offline，上层静默降级不弹错；字段超限 → Rejected。
    CloudResult   UpdateProfile(const AccountProfile& p, std::wstring& err); // PUT /auth/me
    AccountProfile GetProfile(std::wstring& err);                          // GET /auth/me

    // #37 好友系统（REST）
    net::Response GetFriends();                          // GET /friends
    CloudResult   AddFriend(const std::wstring& username, std::wstring& err); // POST /friends/add
    CloudResult   DeleteFriend(const std::string& uid);  // DELETE /friends/:uid

    // 后台取数辅助：detach 一个线程跑 job，避免 UI 线程直接碰网络 IO
    static void RunAsync(std::function<void()> job);

    // ---------------- 状态展示 ----------------
    //  给设置页 / 顶栏用的一句话状态，例如「已同步 · 12:03」「离线 · 本地已保存」
    std::wstring StatusText() const;

    // 后台线程把云端数据写回本地后置位。主循环每帧消费一次，
    // 在 UI 线程上做 Reload / 主题刷新 —— 后台线程绝不直接碰渲染与窗口。
    bool ConsumePulled() { return m_pulled.exchange(false); }

private:
    Cloud() = default;
    ~Cloud() = default;
    Cloud(const Cloud&) = delete;
    Cloud& operator=(const Cloud&) = delete;

    // 内部：一次带鉴权的调用（必要时自动刷新令牌后重试一次）
    net::Response Call(const std::wstring& method, const std::wstring& path,
                       const std::string& body, bool needAuth);
    bool RefreshToken();
    bool ApplyIssue(const std::string& respBody);   // 解析 /auth/* 的下发结果

    // 持久化
    std::wstring ConfigPath() const;                // accounts/cloud.json
    std::wstring SessionPath(const std::wstring& account) const;
    void LoadConfig();
    void SaveConfig();
    void LoadSession(const std::wstring& account);
    void SaveSession();
    void ClearSession();

    void Worker();
    void EnsureWorker();
    void SetStatus(const std::wstring& s);

    // 从服务端响应体里取 error 文案（没有就给个兜底）
    static std::wstring ErrorOf(const net::Response& r, const wchar_t* fallback);

    // ---- 配置 / 会话（受 m_mu 保护）----
    mutable std::mutex m_mu;
    net::Endpoint      m_ep;
    CloudSession       m_sess;
    std::wstring       m_boundAccount;
    std::wstring       m_status = L"未连接";

    std::atomic<bool> m_enabled{ true };
    std::atomic<bool> m_online{ false };
    std::atomic<bool> m_ready{ false };
    std::atomic<bool> m_pulled{ false };   // 云端数据已写回本地，待 UI 线程刷新
    std::atomic<bool> m_firstSyncDone{ false };

    // ---- 后台线程 ----
    std::thread             m_worker;
    std::mutex              m_wmu;
    std::condition_variable m_cv;
    std::atomic<bool> m_quit{ false };
    bool m_dirty = false;        // 有本地改动待上推
    bool m_wantSync = false;     // 请求一次完整同步（拉+合+推）
    bool m_wantProbe = false;
};

} // namespace lj
