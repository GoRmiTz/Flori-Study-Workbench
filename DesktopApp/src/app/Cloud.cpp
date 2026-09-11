#define _CRT_SECURE_NO_WARNINGS
// ============================================================
//  Cloud.cpp — 云端对接层实现
//  与 Server/ 工程的接口对应关系：
//    POST /auth/register  /auth/login  /auth/refresh  /auth/logout
//    GET  /sync/meta      GET /sync/all      PUT /sync/all?mode=merge
//    GET  /health
// ============================================================
#include "core/Common.h"
#include "app/Cloud.h"
#include "app/Store.h"
#include "app/AccountStore.h"
#include "app/Json.h"
#include "net/Realtime.h"   // BindAccount 切换账户时触发 WS 重连（Realtime.h 不回依赖 Cloud.h）

#include <ctime>
#include <chrono>

// 注：LoadConfig / SaveConfig / LoadSession / SaveSession / ClearSession 一律
//     「由调用方持有 m_mu」，函数内部不再自锁，避免同一把锁重入。

namespace lj {

using namespace lj::json;
using net::ToUtf8;
using net::FromUtf8;

namespace {

long long NowSec() { return (long long)time(nullptr); }

std::wstring HHMM(long long t)
{
    std::tm tmv{};
    time_t tt = (time_t)t;
    localtime_s(&tmv, &tt);
    wchar_t buf[16];
    swprintf_s(buf, L"%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return buf;
}

/** 组装 {"username":"…","password":"…"}，字段值走 JSON 转义。 */
std::string CredBody(const std::wstring& name, const std::wstring& pass)
{
    return "{\"username\":" + JQuote(ToUtf8(name)) +
           ",\"password\":" + JQuote(ToUtf8(pass)) + "}";
}

// 媒体上传的 X-Meta 头需要 base64(JSON)，这里放一个最小实现（无第三方依赖）
static const char* kB64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string Base64Encode(const std::string& in)
{
    std::string o; o.reserve(((in.size() + 2) / 3) * 4);
    for (size_t i = 0; i < in.size(); i += 3) {
        unsigned int n = (unsigned char)in[i] << 16;
        if (i + 1 < in.size()) n |= (unsigned char)in[i + 1] << 8;
        if (i + 2 < in.size()) n |= (unsigned char)in[i + 2];
        o += kB64Alphabet[(n >> 18) & 63];
        o += kB64Alphabet[(n >> 12) & 63];
        o += (i + 1 < in.size()) ? kB64Alphabet[(n >> 6) & 63] : '=';
        o += (i + 2 < in.size()) ? kB64Alphabet[n & 63] : '=';
    }
    return o;
}

// 防抖：本地改动后等这么久再上推，把连续勾选合并成一次请求
constexpr int kDebounceMs = 2500;
// 定期探测服务端在线状态的间隔
constexpr int kIdleProbeMs = 60000;

} // namespace

Cloud& Cloud::Instance()
{
    static Cloud s;
    return s;
}

// ============================================================
//  配置 / 会话持久化
// ============================================================
std::wstring Cloud::ConfigPath() const
{
    return AccountStore::Instance().AccountsRoot() + L"cloud-config.json";
}

std::wstring Cloud::SessionPath(const std::wstring& account) const
{
    return AccountStore::Instance().AccountsRoot() + account + L"\\cloud-session.json";
}

void Cloud::LoadConfig()
{
    std::string buf;
    if (!ReadFileRaw(ConfigPath(), buf)) return;      // 没有配置就用默认值
    Parser p(buf.data(), buf.size());
    JVal v = p.parse();
    if (v.type != JVal::Obj) return;

    if (auto* h = JGet(v, "host"); h && h->type == JVal::Str && !h->str.empty())
        m_ep.host = FromUtf8(h->str);
    if (auto* n = JGet(v, "port"); n && n->type == JVal::Num && n->num > 0)
        m_ep.port = (int)n->num;
    if (auto* s = JGet(v, "secure"))
        m_ep.secure = (s->type == JVal::Bool) ? s->bval : (s->type == JVal::Num && s->num != 0);
    if (auto* e = JGet(v, "enabled"))
        m_enabled = (e->type == JVal::Bool) ? e->bval : !(e->type == JVal::Num && e->num == 0);
}

void Cloud::SaveConfig()
{
    std::string out = "{\n";
    out += "  \"host\": " + JQuote(ToUtf8(m_ep.host)) + ",\n";
    out += "  \"port\": " + std::to_string(m_ep.port) + ",\n";
    out += "  \"secure\": " + std::string(m_ep.secure ? "true" : "false") + ",\n";
    out += "  \"enabled\": " + std::string(m_enabled.load() ? "true" : "false") + "\n";
    out += "}\n";
    WriteFileRaw(ConfigPath(), out);
}

void Cloud::LoadSession(const std::wstring& account)
{
    m_sess = CloudSession{};
    m_firstSyncDone = false;
    if (account.empty()) return;

    std::string buf;
    if (!ReadFileRaw(SessionPath(account), buf)) return;
    Parser p(buf.data(), buf.size());
    JVal v = p.parse();
    if (v.type != JVal::Obj) return;

    m_sess.uid          = FromUtf8(JStr(v, "uid"));
    m_sess.username     = FromUtf8(JStr(v, "username"));
    m_sess.accessToken  = JStr(v, "accessToken");
    m_sess.refreshToken = JStr(v, "refreshToken");
    if (auto* e = JGet(v, "expiresAt"); e && e->type == JVal::Num) m_sess.expiresAt = (long long)e->num;
    if (auto* s = JGet(v, "synced"); s && s->type == JVal::Bool) m_firstSyncDone = s->bval;
}

void Cloud::SaveSession()
{
    if (m_boundAccount.empty()) return;
    // 账户目录可能还没建（注册流程里 Cloud 先于 AccountStore 落盘），先确保存在
    const std::wstring root = AccountStore::Instance().AccountsRoot();
    CreateDirectoryW(root.c_str(), nullptr);
    CreateDirectoryW((root + m_boundAccount).c_str(), nullptr);

    std::string out = "{\n";
    out += "  \"uid\": " + JQuote(ToUtf8(m_sess.uid)) + ",\n";
    out += "  \"username\": " + JQuote(ToUtf8(m_sess.username)) + ",\n";
    out += "  \"accessToken\": " + JQuote(m_sess.accessToken) + ",\n";
    out += "  \"refreshToken\": " + JQuote(m_sess.refreshToken) + ",\n";
    out += "  \"expiresAt\": " + std::to_string(m_sess.expiresAt) + ",\n";
    out += "  \"synced\": " + std::string(m_firstSyncDone.load() ? "true" : "false") + "\n";
    out += "}\n";
    WriteFileRaw(SessionPath(m_boundAccount), out);
}

void Cloud::ClearSession()
{
    if (!m_boundAccount.empty()) _wremove(SessionPath(m_boundAccount).c_str());
    m_sess = CloudSession{};
    m_firstSyncDone = false;
}

// ============================================================
//  生命周期
// ============================================================
void Cloud::Init()
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        LoadConfig();
        m_boundAccount = AccountStore::Instance().IsGuest()
                       ? std::wstring()
                       : AccountStore::Instance().Current().user;
        LoadSession(m_boundAccount);
        m_status = m_enabled ? L"正在连接…" : L"云同步已关闭";
    }
    m_ready = true;
    EnsureWorker();
    if (m_enabled) {
        // 启动时不阻塞 UI：探测 + 若已有令牌则顺手同步一次
        std::lock_guard<std::mutex> lk(m_wmu);
        m_wantProbe = true;
        m_wantSync = LoggedIn();
        m_cv.notify_all();
    }

}

void Cloud::Shutdown()
{
    if (m_ready.load()) Flush();   // 尽力把最后的改动送上去
    // 无论 m_ready 与否都要停线程：worker 可能在线程未 ready 时就被
    // EnsureWorker 拉起，若带着 joinable 的 std::thread 走析构会 abort()。
    m_quit = true;
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();
    m_ready = false;
}

void Cloud::EnsureWorker()
{
    if (m_worker.joinable()) return;
    m_worker = std::thread([this] { Worker(); });
}

void Cloud::SetEnabled(bool on)
{
    m_enabled = on;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        SaveConfig();
        m_status = on ? L"正在连接…" : L"云同步已关闭";
    }
    if (on) { ProbeAsync(); SyncAsync(); }
    else m_online = false;
}

net::Endpoint Cloud::EndpointCfg() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_ep;
}

void Cloud::SetEndpoint(const std::wstring& host, int port, bool secure)
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_ep.host = host.empty() ? L"127.0.0.1" : host;
        m_ep.port = (port > 0 && port < 65536) ? port : 8787;
        m_ep.secure = secure;
        SaveConfig();
        m_status = L"正在连接…";
    }
    m_online = false;
    ProbeAsync();
}

void Cloud::BindAccount(const std::wstring& account)
{
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_boundAccount = account;
        LoadSession(account);
        m_status = account.empty() ? L"访客模式 · 仅本地"
                                   : (m_sess.Valid() ? L"正在同步…" : L"未登录云端");
    }
    if (m_enabled && LoggedIn()) SyncAsync();
    // 账户切换：访客(空账户)允许匿名连实时网关只读；非访客需令牌。用新策略重连。
    Realtime::Instance().SetAllowAnonymous(account.empty());
    Realtime::Instance().Reconnect();
}

// ============================================================
//  请求
// ============================================================
std::wstring Cloud::ErrorOf(const net::Response& r, const wchar_t* fallback)
{
    if (!r.body.empty()) {
        Parser p(r.body.data(), r.body.size());
        JVal v = p.parse();
        std::string e = JStr(v, "error");
        if (!e.empty()) return FromUtf8(e);
    }
    return fallback;
}

net::Response Cloud::Call(const std::wstring& method, const std::wstring& path,
                          const std::string& body, bool needAuth)
{
    net::Endpoint ep;
    std::string token;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        ep = m_ep;
        token = needAuth ? m_sess.accessToken : std::string();
    }

    net::Response r = net::Request(ep, method, path, body, token);

    // 令牌过期 → 刷新一次再重试；刷新也失败就把会话标记为需要重新登录
    if (needAuth && r.transport && r.status == 401) {
        if (RefreshToken()) {
            {
                std::lock_guard<std::mutex> lk(m_mu);
                token = m_sess.accessToken;
            }
            r = net::Request(ep, method, path, body, token);
        }
    }

    m_online = r.transport;
    return r;
}

bool Cloud::ApplyIssue(const std::string& respBody)
{
    Parser p(respBody.data(), respBody.size());
    JVal v = p.parse();
    if (v.type != JVal::Obj) return false;

    std::string access = JStr(v, "accessToken");
    if (access.empty()) return false;

    std::lock_guard<std::mutex> lk(m_mu);
    m_sess.accessToken = access;
    std::string refresh = JStr(v, "refreshToken");
    if (!refresh.empty()) m_sess.refreshToken = refresh;

    long long ttl = 7200;
    if (auto* e = JGet(v, "expiresIn"); e && e->type == JVal::Num && e->num > 0) ttl = (long long)e->num;
    // 提前 60 秒判定过期，避开「刚好卡在边界」的往返
    m_sess.expiresAt = NowSec() + ttl - 60;

    if (auto* u = JGet(v, "user"); u && u->type == JVal::Obj) {
        m_sess.uid = FromUtf8(JStr(*u, "uid"));
        m_sess.username = FromUtf8(JStr(*u, "username"));
    }
    return true;
}

bool Cloud::RefreshToken()
{
    std::string refresh;
    net::Endpoint ep;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        refresh = m_sess.refreshToken;
        ep = m_ep;
    }
    if (refresh.empty()) return false;

    std::string body = "{\"refreshToken\":" + JQuote(refresh) + "}";
    net::Response r = net::Request(ep, L"POST", L"/auth/refresh", body, std::string());
    if (!r.Ok()) {
        if (r.transport) {
            // 服务端明确拒绝（刷新令牌也过期 / 被踢下线）→ 清掉，回到未登录
            std::lock_guard<std::mutex> lk(m_mu);
            ClearSession();
            m_status = L"登录已失效，请重新登录";
        }
        return false;
    }
    if (!ApplyIssue(r.body)) return false;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        SaveSession();
    }
    return true;
}

bool Cloud::Probe()
{
    if (!m_enabled) return false;
    net::Endpoint ep = EndpointCfg();
    net::Response r = net::Request(ep, L"GET", L"/health", std::string(), std::string(), 2500);
    bool up = r.Ok();
    m_online = up;
    if (!up) SetStatus(L"离线 · 本地已保存");
    else if (!LoggedIn()) SetStatus(L"已连接 · 未登录云端");
    return up;
}

void Cloud::ProbeAsync()
{
    EnsureWorker();
    std::lock_guard<std::mutex> lk(m_wmu);
    m_wantProbe = true;
    m_cv.notify_all();
}

// ============================================================
//  账户
// ============================================================
CloudResult Cloud::Login(const std::wstring& name, const std::wstring& pass, std::wstring& err)
{
    if (!m_enabled) return CloudResult::Disabled;

    net::Endpoint ep = EndpointCfg();
    net::Response r = net::Request(ep, L"POST", L"/auth/login", CredBody(name, pass), std::string());
    m_online = r.transport;

    if (r.Offline()) { err = r.error; SetStatus(L"离线 · 本地已保存"); return CloudResult::Offline; }
    if (r.status == 404) { err = ErrorOf(r, L"云端没有这个账户"); return CloudResult::NotFound; }
    if (!r.Ok())     { err = ErrorOf(r, L"云端登录失败"); return CloudResult::Rejected; }

    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_boundAccount = name;
    }
    m_firstSyncDone = false;
    if (!ApplyIssue(r.body)) { err = L"云端返回的登录数据无法解析"; return CloudResult::Rejected; }
    {
        std::lock_guard<std::mutex> lk(m_mu);
        SaveSession();
        m_status = L"已登录 · 正在同步…";
    }
    return CloudResult::Ok;
}

CloudResult Cloud::Register(const std::wstring& name, const std::wstring& pass, std::wstring& err)
{
    if (!m_enabled) return CloudResult::Disabled;

    net::Endpoint ep = EndpointCfg();
    net::Response r = net::Request(ep, L"POST", L"/auth/register", CredBody(name, pass), std::string());
    m_online = r.transport;

    if (r.Offline()) { err = r.error; SetStatus(L"离线 · 本地已保存"); return CloudResult::Offline; }
    if (!r.Ok())     { err = ErrorOf(r, L"云端注册失败"); return CloudResult::Rejected; }

    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_boundAccount = name;
    }
    m_firstSyncDone = false;
    if (!ApplyIssue(r.body)) { err = L"云端返回的注册数据无法解析"; return CloudResult::Rejected; }
    {
        std::lock_guard<std::mutex> lk(m_mu);
        SaveSession();
        m_status = L"已登录 · 正在同步…";
    }
    return CloudResult::Ok;
}

void Cloud::Logout(bool allDevices)
{
    if (LoggedIn() && m_enabled) {
        std::string body = allDevices ? "{\"all\":true}" : "{}";
        Call(L"POST", L"/auth/logout", body, true);   // 尽力而为，失败无所谓
    }
    {
        std::lock_guard<std::mutex> lk(m_mu);
        ClearSession();
        m_status = L"已退出云端";
    }
}

bool Cloud::LoggedIn() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_sess.Valid();
}

std::wstring Cloud::Uid() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_sess.uid;
}

std::wstring Cloud::BoundUser() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_sess.username.empty() ? m_boundAccount : m_sess.username;
}

// ============================================================
//  同步
//  顺序：先推后拉。
//    1) PUT /sync/all?mode=merge —— 本地并入云端（打卡 / 复盘 / 作息按键合并，
//       专注记录按复合键取并集），因此绝不会因为「另一台机器先同步」而丢数据。
//    2) GET /sync/all            —— 把合并后的全集写回本地。
//  本地不存在的数据块不参与上推（见 BuildPayload(false) 的注释）。
// ============================================================
bool Cloud::SyncNow(std::wstring& err)
{
    if (!m_enabled)  { err = L"云同步已关闭"; return false; }
    if (!LoggedIn()) { err = L"尚未登录云端"; return false; }

    // 1) 上推
    std::string local = CheckinStore::Instance().BuildPayload(false);
    net::Response up = Call(L"PUT", L"/sync/all?mode=merge", local, true);
    if (up.Offline()) { err = up.error; SetStatus(L"离线 · 本地已保存"); return false; }
    if (!up.Ok()) {
        // 400「没有可同步的数据块」属于正常情况：本地是全新账户，什么都还没写
        if (up.status != 400) { err = ErrorOf(up, L"上传失败"); SetStatus(L"同步失败 · 本地已保存"); return false; }
    }

    // 2) 回拉合集
    net::Response down = Call(L"GET", L"/sync/all", std::string(), true);
    if (down.Offline()) { err = down.error; SetStatus(L"离线 · 本地已保存"); return false; }
    if (!down.Ok())     { err = ErrorOf(down, L"下载失败"); SetStatus(L"同步失败 · 本地已保存"); return false; }

    std::wstring aerr;
    bool changed = false;
    if (CheckinStore::Instance().ApplyPayload(down.body, aerr, &changed)) {
        // 只有内容真变了才惊动 UI —— 常规上推后的回拉内容与本地一致，
        // 若无脑置位，用户每次打卡都会看到界面重建、滚动位置回弹。
        if (changed) m_pulled = true;
    } else if (!aerr.empty()) {
        LogLine(L"[cloud] 云端载荷未写入本地：%s", aerr.c_str());
    }

    m_firstSyncDone = true;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        SaveSession();
        m_status = L"已同步 · " + HHMM(NowSec());
    }
    return true;
}

void Cloud::SyncAsync()
{
    if (!m_enabled) return;
    EnsureWorker();
    std::lock_guard<std::mutex> lk(m_wmu);
    m_wantSync = true;
    m_cv.notify_all();
}

void Cloud::MarkDirty()
{
    if (!m_enabled || !m_ready.load()) return;
    EnsureWorker();
    std::lock_guard<std::mutex> lk(m_wmu);
    m_dirty = true;
    m_cv.notify_all();
}

void Cloud::Flush()
{
    if (!m_enabled || !LoggedIn()) return;
    bool pending;
    {
        std::lock_guard<std::mutex> lk(m_wmu);
        pending = m_dirty || m_wantSync;
        m_dirty = false;
        m_wantSync = false;
    }
    if (!pending) return;
    std::wstring err;
    SyncNow(err);            // 阻塞，退出路径上可接受
}

// ============================================================
//  专栏 & 媒体（§4 / §5）
//  这些方法都在后台线程调用（经 RunAsync 或 Realtime 触发），内部走私有 Call，
//  自动带 Bearer 并在 401 时刷新令牌重试一次，与 SyncNow 同一信任链。
// ============================================================
std::string Cloud::AccessToken() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_sess.accessToken;
}

bool Cloud::TryRefresh()
{
    return RefreshToken();
}

void Cloud::RunAsync(std::function<void()> job)
{
    if (!job) return;
    std::thread t(job);
    t.detach();
}

net::Response Cloud::GetColumns()
{
    return Call(L"GET", L"/columns", std::string(), true);
}

net::Response Cloud::GetColumn(const std::string& id)
{
    if (id.empty()) return net::Response{};
    return Call(L"GET", L"/columns/" + FromUtf8(id), std::string(), true);
}

CloudResult Cloud::CreateColumn(const std::wstring& title, const std::wstring& body,
                                const std::wstring& sectionId, std::wstring& err)
{
    err.clear();
    if (ToUtf8(title).empty()) { err = L"标题不能为空"; return CloudResult::Rejected; }
    // 服务端 §4 只存纯文本（title/body/sectionId/hidden），富文本 RTF 暂不上云
    std::string b = "{\"title\":" + JQuote(ToUtf8(title)) +
                    ",\"body\":" + JQuote(ToUtf8(body)) +
                    ",\"sectionId\":" + JQuote(ToUtf8(sectionId)) + "}";
    net::Response r = Call(L"POST", L"/columns", b, true);
    if (r.Offline()) { err = ErrorOf(r, L"未连上服务端"); return CloudResult::Offline; }
    if (!r.Ok())     { err = ErrorOf(r, L"发布失败"); return CloudResult::Rejected; }
    return CloudResult::Ok;
}

// ---------------- 档案上云（P1-1 · GET/PUT /auth/me）----------------
CloudResult Cloud::UpdateProfile(const AccountProfile& p, std::wstring& err)
{
    err.clear();
    std::string b = "{\"bio\":" + JQuote(ToUtf8(p.bio)) +
                    ",\"major\":" + JQuote(ToUtf8(p.major)) +
                    ",\"school\":" + JQuote(ToUtf8(p.school)) +
                    ",\"birthday\":" + JQuote(ToUtf8(p.birthday)) +
                    ",\"gender\":" + JQuote(ToUtf8(p.gender)) + "}";
    net::Response r = Call(L"PUT", L"/auth/me", b, true);
    if (r.Offline()) { err = ErrorOf(r, L"未连上服务端"); return CloudResult::Offline; }
    if (!r.Ok())     { err = ErrorOf(r, L"档案同步失败"); return CloudResult::Rejected; }
    return CloudResult::Ok;
}

AccountProfile Cloud::GetProfile(std::wstring& err)
{
    err.clear();
    AccountProfile out;
    net::Response r = Call(L"GET", L"/auth/me", std::string(), true);
    if (r.Offline() || !r.Ok()) { err = ErrorOf(r, L"未连上服务端"); return out; }
    Parser p(r.body.data(), r.body.size());
    JVal root = p.parse();
    if (root.type != JVal::Obj) return out;
    const JVal* user = JGet(root, "user");
    const JVal* prof = user ? JGet(*user, "profile") : JGet(root, "profile");
    if (!prof || prof->type != JVal::Obj) return out;
    out.bio      = FromUtf8(JStr(*prof, "bio"));
    out.major    = FromUtf8(JStr(*prof, "major"));
    out.school   = FromUtf8(JStr(*prof, "school"));
    out.birthday = FromUtf8(JStr(*prof, "birthday"));
    out.gender   = FromUtf8(JStr(*prof, "gender"));
    return out;
}

// 拉取媒体文件原始字节（带 Bearer；供软件内图片查看等场景，避免裸 URL 401）
net::Response Cloud::GetMediaFile(const std::string& id)
{
    if (id.empty()) return net::Response{};
    return Call(L"GET", L"/media/file/" + FromUtf8(id), std::string(), true);
}

CloudResult Cloud::LikeColumn(const std::string& id, bool& on, int& count)
{
    on = false; count = 0;
    if (id.empty()) return CloudResult::Rejected;
    net::Response r = Call(L"POST", L"/columns/" + FromUtf8(id) + L"/like", "{}", true);
    if (r.Offline()) return CloudResult::Offline;
    if (!r.Ok())     return CloudResult::Rejected;
    JVal root = Parser(r.body.data(), r.body.size()).parse();
    if (const JVal* po = JGet(root, "on"))    if (po->type == JVal::Bool)  on = po->bval;
    if (const JVal* pc = JGet(root, "count")) if (pc->type == JVal::Num)   count = (int)pc->num;
    return CloudResult::Ok;
}

CloudResult Cloud::FavColumn(const std::string& id, bool& on, int& count)
{
    on = false; count = 0;
    if (id.empty()) return CloudResult::Rejected;
    net::Response r = Call(L"POST", L"/columns/" + FromUtf8(id) + L"/fav", "{}", true);
    if (r.Offline()) return CloudResult::Offline;
    if (!r.Ok())     return CloudResult::Rejected;
    JVal root = Parser(r.body.data(), r.body.size()).parse();
    if (const JVal* po = JGet(root, "on"))    if (po->type == JVal::Bool)  on = po->bval;
    if (const JVal* pc = JGet(root, "count")) if (pc->type == JVal::Num)   count = (int)pc->num;
    return CloudResult::Ok;
}

CloudResult Cloud::CommentColumn(const std::string& id, const std::wstring& text)
{
    if (id.empty()) return CloudResult::Rejected;
    std::string body = "{\"text\":" + JQuote(ToUtf8(text)) + "}";
    net::Response r = Call(L"POST", L"/columns/" + FromUtf8(id) + L"/comment", body, true);
    if (r.Offline()) return CloudResult::Offline;
    if (!r.Ok())     return CloudResult::Rejected;
    return CloudResult::Ok;
}

net::Response Cloud::GetMediaList()
{
    return Call(L"GET", L"/media/list", std::string(), true);
}

CloudResult Cloud::UploadMedia(const std::string& kind, const std::string& title,
                               const std::string& note, const std::string& filename,
                               const std::string& bin, std::wstring& err)
{
    err.clear();
    if (bin.empty()) { err = L"文件内容为空"; return CloudResult::Rejected; }

    // X-Meta: base64(JSON{kind,title,note,filename})
    std::string meta = "{\"kind\":" + JQuote(kind) + ",\"title\":" + JQuote(title) +
                       ",\"note\":" + JQuote(note) + ",\"filename\":" + JQuote(filename) + "}";
    std::string extra = "X-Meta: " + Base64Encode(meta) + "\r\n";

    auto doPost = [this, &bin, &extra]() {
        return net::Request(EndpointCfg(), L"POST", L"/media/upload", bin,
                            AccessToken(), 30000, "application/octet-stream", extra);
    };
    net::Response r = doPost();
    if (r.status == 401 && RefreshToken()) r = doPost();   // 令牌过期则刷新后重试一次
    if (r.Offline()) { err = r.error; return CloudResult::Offline; }
    if (!r.Ok())     { err = ErrorOf(r, L"上传失败"); return CloudResult::Rejected; }
    return CloudResult::Ok;
}

CloudResult Cloud::DeleteMedia(const std::string& id)
{
    if (id.empty()) return CloudResult::Rejected;
    net::Response r = Call(L"DELETE", L"/media/" + FromUtf8(id), std::string(), true);
    if (r.Offline()) return CloudResult::Offline;
    if (!r.Ok())     return CloudResult::Rejected;
    return CloudResult::Ok;
}

// #37 好友系统
net::Response Cloud::GetFriends()
{
    return Call(L"GET", L"/friends", std::string(), true);
}

CloudResult Cloud::AddFriend(const std::wstring& username, std::wstring& err)
{
    err.clear();
    if (ToUtf8(username).empty()) { err = L"请输入要添加的用户名"; return CloudResult::Rejected; }
    std::string b = "{\"username\":" + JQuote(ToUtf8(username)) + "}";
    net::Response r = Call(L"POST", L"/friends/add", b, true);
    if (r.Offline()) { err = ErrorOf(r, L"未连上服务端"); return CloudResult::Offline; }
    if (!r.Ok())     { err = ErrorOf(r, L"添加失败"); return CloudResult::Rejected; }
    return CloudResult::Ok;
}

CloudResult Cloud::DeleteFriend(const std::string& uid)
{
    if (uid.empty()) return CloudResult::Rejected;
    net::Response r = Call(L"DELETE", L"/friends/" + FromUtf8(uid), std::string(), true);
    if (r.Offline()) return CloudResult::Offline;
    if (!r.Ok())     return CloudResult::Rejected;
    return CloudResult::Ok;
}

// ============================================================
//  后台线程
//  唯一做网络 IO 的地方。UI 线程只往这里丢标记，不等待、不阻塞。
// ============================================================
void Cloud::Worker()
{
    long long lastProbe = 0;

    for (;;) {
        bool doSync = false, doProbe = false;

        {
            std::unique_lock<std::mutex> lk(m_wmu);
            m_cv.wait_for(lk, std::chrono::milliseconds(kIdleProbeMs),
                          [this] { return m_quit.load() || m_dirty || m_wantSync || m_wantProbe; });
            if (m_quit) return;

            if (m_dirty) {
                // 防抖：等安静 kDebounceMs 再推。期间若又有改动，wait_for 会被唤醒重来。
                bool more = m_cv.wait_for(lk, std::chrono::milliseconds(kDebounceMs),
                                          [this] { return m_quit.load() || m_wantSync; });
                if (m_quit) return;
                (void)more;
                m_dirty = false;
                doSync = true;
            }
            if (m_wantSync)  { m_wantSync = false;  doSync = true; }
            if (m_wantProbe) { m_wantProbe = false; doProbe = true; }
        }

        if (!m_enabled) continue;

        long long now = NowSec();
        if (doProbe || (!doSync && now - lastProbe >= kIdleProbeMs / 1000)) {
            lastProbe = now;
            Probe();
        }

        if (doSync) {
            std::wstring err;
            if (!SyncNow(err) && !err.empty())
                LogLine(L"[cloud] 同步未完成：%s", err.c_str());
            lastProbe = NowSec();
        }
    }
}

// ============================================================
void Cloud::SetStatus(const std::wstring& s)
{
    std::lock_guard<std::mutex> lk(m_mu);
    m_status = s;
}

std::wstring Cloud::StatusText() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_status;
}

} // namespace lj
