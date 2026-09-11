// ============================================================
//  Updater.cpp — P2-3 方案 A · 客户端自动更新
// ============================================================
#include "app/Updater.h"
#include "app/Cloud.h"
#include "core/TrayIcon.h"
#include "core/Common.h"   // windows.h / shellapi.h
#include "net/Http.h"
#include "app/Json.h"

#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <bcrypt.h>          // S0-1：CNG SHA-256（系统库，已在 CMake 白名单）

namespace lj {

// ============================================================
//  S0-1 安全校验小工具
// ============================================================
bool Updater::Sha256Hex(const std::string& data, std::string& hex)
{
    hex.clear();
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return false;
    unsigned char digest[32] = { 0 };
    NTSTATUS st = BCryptHash(alg, nullptr, 0,
                             (PUCHAR)data.data(), (ULONG)data.size(),
                             digest, sizeof(digest));
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st != 0) return false;

    static const char* kHex = "0123456789abcdef";
    hex.reserve(64);
    for (unsigned char b : digest) {
        hex += kHex[b >> 4];
        hex += kHex[b & 0x0F];
    }
    return true;
}

bool Updater::IsLoopback(const std::wstring& host)
{
    std::wstring lo = host;
    for (wchar_t& c : lo) c = (wchar_t)::towlower(c);
    return lo == L"127.0.0.1" || lo == L"localhost" || lo == L"::1" || lo == L"[::1]";
}

bool Updater::VerifyPackage(const std::string& bytes, const std::string& wantSha,
                            long long wantSize, std::wstring& err)
{
    err.clear();
    // ① 必须有期望哈希——没有就不认，宁可不更新
    if (wantSha.size() != 64) {
        err = L"服务端未下发合法 sha256（需 64 位十六进制）";
        return false;
    }
    // ② 长度预检（可选字段）
    if (wantSize > 0 && (long long)bytes.size() != wantSize) {
        err = L"安装包长度与服务端声明不符";
        return false;
    }
    // ③ PE 头粗检，挡掉错误页/HTML 被当成 exe 落地
    if (bytes.size() < 64 || bytes[0] != 'M' || bytes[1] != 'Z') {
        err = L"下载内容不是可执行文件（缺少 PE 头）";
        return false;
    }
    // ④ 内容哈希比对（大小写不敏感）
    std::string got;
    if (!Sha256Hex(bytes, got)) { err = L"本地 SHA-256 计算失败"; return false; }
    std::string want = wantSha;
    for (char& c : want) c = (char)::tolower((unsigned char)c);
    if (got != want) { err = L"SHA-256 校验不通过，安装包可能被篡改"; return false; }
    return true;
}

Updater& Updater::Instance()
{
    static Updater s;
    return s;
}

bool Updater::HasUpdate() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_info.available;
}

UpdateInfo Updater::Info() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_info;
}

std::wstring Updater::ExePath() const
{
    wchar_t buf[MAX_PATH * 2] = { 0 };
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(wchar_t)));
    return (n > 0) ? std::wstring(buf, n) : std::wstring();
}

std::wstring Updater::UpdatesDir() const
{
    std::wstring exe = ExePath();
    if (exe.empty()) return L"updates";
    std::wstring::size_type sl = exe.find_last_of(L"\\/");
    return (sl == std::wstring::npos) ? L"updates" : exe.substr(0, sl) + L"\\updates";
}

bool Updater::FileExists(const std::wstring& p)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d))
        return false;
    return (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool Updater::PendingExists() const
{
    return FileExists(UpdatesDir() + L"\\pending.json");
}

bool Updater::IsDevPath() const
{
    std::wstring exe = ExePath();
    if (exe.empty()) return true;
    std::wstring lo = exe;
    for (wchar_t& c : lo) c = (wchar_t)::towlower(c);
    if (lo.find(L"\\desktopapp\\build\\") != std::wstring::npos) return true;
    if (lo.find(L"\\desktopapp\\out\\")    != std::wstring::npos) return true;
    if (lo.find(L"\\.git\\")               != std::wstring::npos) return true;
    return false;
}

void Updater::CheckAsync()
{
    if (m_checking.exchange(true)) return;
    Cloud::RunAsync([this]() {
        m_checking = false;
        auto ep = Cloud::Instance().EndpointCfg();
        net::Response r = net::Request(ep, L"GET", L"/version", "", "", 4000);
        if (!r.transport) { LogLine(L"[upd] /version 离线，跳过自动更新检查"); return; }
        if (!r.Ok())      { LogLine(L"[upd] /version 返回 %d", r.status); return; }
        Parse(r.body);
    });
}

void Updater::CheckNow()
{
    CheckAsync();   // 同一后台路径；结果经气泡/菜单反馈
}

void Updater::Parse(const std::string& body)
{
    json::Parser ps(body.c_str(), body.size());
    json::JVal root = ps.parse();
    UpdateInfo info;
    info.version     = json::JStr(root, "version");
    info.minVersion  = json::JStr(root, "minVersion");
    info.downloadUrl = json::JStr(root, "downloadUrl");
    info.notes       = json::JStr(root, "notes");
    info.sha256      = json::JStr(root, "sha256");        // S0-1：必填，缺失则不下载
    if (const json::JVal* sz = json::JGet(root, "size"))
        if (sz->type == json::JVal::Num) info.size = (long long)sz->num;
    if (info.version.empty()) return;

    if (CompareVersion(info.version, kAppVersion) > 0) {
        info.available = true;
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_info = info;
        }
        NotifyAvailable();
        DownloadAsync();   // 后台预下载，用户点「重启并更新」时已就绪
        LogLine(L"[upd] 发现新版本 %S（当前 %S）", info.version.c_str(), kAppVersion);
    } else {
        LogLine(L"[upd] 已是最新 %S", kAppVersion);
    }
}

void Updater::NotifyAvailable()
{
    UpdateInfo info;
    { std::lock_guard<std::mutex> lk(m_mu); info = m_info; }
    if (!m_notified.exchange(true)) {
        // 挂出「重启并更新」菜单项（带版本号）
        std::wstring label = L"重启并更新 (v" + net::FromUtf8(info.version) + L")";
        TrayIcon::Instance().AddMenuItem(101, label);
    }
    if (TrayIcon::Instance().Visible()) {
        std::wstring tip = L"发现新版本 v" + net::FromUtf8(info.version);
        std::wstring msg = net::FromUtf8(info.notes.empty() ? "点击托盘「重启并更新」即可升级。" : info.notes);
        TrayIcon::Instance().Balloon(tip.c_str(), msg.c_str(), NIIF_INFO);
    }
}

bool Updater::ParseUrl(const std::string& url, net::Endpoint& ep, std::wstring& path)
{
    size_t i = 0;
    bool secure = false;
    if (url.rfind("https://", 0) == 0) { secure = true; i = 8; }
    else if (url.rfind("http://", 0) == 0) { i = 7; }
    else return false;

    size_t slash = url.find('/', i);
    std::string hostport = (slash == std::string::npos) ? url.substr(i) : url.substr(i, slash - i);
    path = (slash == std::string::npos) ? L"/" : net::FromUtf8(url.substr(slash));

    size_t c = hostport.find(':');
    if (c != std::string::npos) {
        ep.host = net::FromUtf8(hostport.substr(0, c));
        ep.port = atoi(hostport.substr(c + 1).c_str());
    } else {
        ep.host = net::FromUtf8(hostport);
        ep.port = secure ? 443 : 80;
    }
    ep.secure = secure;
    if (ep.host.empty()) return false;

    // S0-1 ①：明文 http 只对回环放行（本机联调），其余一律拒绝。
    // 之前这里无条件接受 http:// → 中间人可替换整个安装包。
    if (!secure && !IsLoopback(ep.host)) {
        LogLine(L"[upd] 拒绝明文 http 下载地址（仅回环可用），请改 https");
        return false;
    }
    return true;
}

void Updater::DownloadAsync()
{
    Cloud::RunAsync([this]() {
        UpdateInfo info;
        { std::lock_guard<std::mutex> lk(m_mu); info = m_info; }
        if (!info.available || info.downloadUrl.empty()) {
            LogLine(L"[upd] 无下载地址，跳过后台下载");
            return;
        }
        // S0-1 ②前置：没有合法 sha256 就根本不去下载，省得留下半个可执行文件
        if (info.sha256.size() != 64) {
            LogLine(L"[upd] /version 未下发合法 sha256，出于安全拒绝下载本次更新");
            return;
        }
        net::Endpoint ep;
        std::wstring path;
        if (info.downloadUrl[0] == '/') {
            ep = Cloud::Instance().EndpointCfg();
            path = net::FromUtf8(info.downloadUrl);
        } else if (!ParseUrl(info.downloadUrl, ep, path)) {
            LogLine(L"[upd] downloadUrl 解析失败：%S", info.downloadUrl.c_str());
            return;
        }

        net::Response r = net::Request(ep, L"GET", path, "", "", 20000,
                                             "application/octet-stream");
        if (!r.Ok()) { LogLine(L"[upd] 下载失败 状态 %d", r.status); return; }

        // S0-1 ②：先在内存里验完再落盘——磁盘上永远不出现未校验的可执行文件
        std::wstring verr;
        if (!VerifyPackage(r.body, info.sha256, info.size, verr)) {
            LogLine(L"[upd] 安装包校验失败：%s（已丢弃，不落盘）", verr.c_str());
            TrayIcon::Instance().Balloon(L"更新被拒绝", verr.c_str(), NIIF_WARNING);
            return;
        }

        std::wstring dir = UpdatesDir();
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring staged = dir + L"\\Flori.next.exe";
        if (!json::WriteFileRaw(staged, r.body)) {
            LogLine(L"[upd] staging 写入失败");
            return;
        }
        // 把哈希/长度一并写进 pending，交换前再验一次（防落盘后被本地替换）
        std::string pj = std::string("{\"version\":") + json::JQuote(info.version)
                        + ",\"staged\":\"Flori.next.exe\""
                        + ",\"sha256\":" + json::JQuote(info.sha256)
                        + ",\"size\":" + std::to_string((long long)r.body.size()) + "}";
        json::WriteFileRaw(dir + L"\\pending.json", pj);
        LogLine(L"[upd] 已下载并校验 v%S（sha256 前八位 %.8S），待重启应用",
                info.version.c_str(), info.sha256.c_str());
    });
}

void Updater::ApplyNow()
{
    if (IsDevPath()) {
        TrayIcon::Instance().Balloon(L"开发预览", L"开发/截图路径不自动替换可执行文件。", NIIF_INFO);
        return;
    }
    if (!HasUpdate()) {
        TrayIcon::Instance().Balloon(L"芙洛理 Flori", L"已经是最新版本。", NIIF_INFO);
        return;
    }
    if (!PendingExists()) {
        TrayIcon::Instance().Balloon(L"正在准备更新", L"更新还在后台下载，请稍候再点「重启并更新」。", NIIF_INFO);
        DownloadAsync();
        return;
    }
    SwapAndRelaunch();
}

void Updater::ApplyStartupPending(const std::wstring& cmdLine)
{
    m_cmdLine = cmdLine;
    if (IsDevPath()) return;          // 开发/截图路径不自动替换
    if (!PendingExists()) return;
    if (!SwapAndRelaunch()) {
        // 交换失败：删 pending 防止反复尝试死循环，继续正常启动
        DeleteFileW((UpdatesDir() + L"\\pending.json").c_str());
        LogLine(L"[upd] 启动交换失败，已清除 pending，继续正常启动");
    }
}

bool Updater::SwapAndRelaunch()
{
    std::wstring exe = ExePath();
    if (exe.empty()) return false;
    std::wstring::size_type sl = exe.find_last_of(L"\\/");
    std::wstring dir = (sl == std::wstring::npos) ? L"." : exe.substr(0, sl);
    std::wstring staged = UpdatesDir() + L"\\Flori.next.exe";
    std::wstring prev = dir + L"\\Flori.prev.exe";

    if (!FileExists(staged)) return false;

    // ---- S0-1 ③ 落地二次校验：交换前重新读盘算哈希 ----
    // 下载时校验过，但从「下载完成」到「用户点重启」之间 staging 文件仍可能
    // 被本地恶意进程替换；这里再验一次，不过关就整体丢弃。
    {
        std::string pend, bytes;
        std::string wantSha; long long wantSize = 0;
        if (json::ReadFileRaw(UpdatesDir() + L"\\pending.json", pend)) {
            json::Parser ps(pend.c_str(), pend.size());
            json::JVal pr = ps.parse();
            wantSha = json::JStr(pr, "sha256");
            if (const json::JVal* sz = json::JGet(pr, "size"))
                if (sz->type == json::JVal::Num) wantSize = (long long)sz->num;
        }
        std::wstring verr;
        if (!json::ReadFileRaw(staged, bytes)) {
            LogLine(L"[upd] staging 读取失败，放弃交换");
            return false;
        }
        if (!VerifyPackage(bytes, wantSha, wantSize, verr)) {
            LogLine(L"[upd] 交换前校验失败：%s（已删除 staging）", verr.c_str());
            DeleteFileW(staged.c_str());
            DeleteFileW((UpdatesDir() + L"\\pending.json").c_str());
            TrayIcon::Instance().Balloon(L"更新已取消", verr.c_str(), NIIF_WARNING);
            return false;
        }
    }

    // 移除旧的 .prev（上一次失败的残留），失败不致命
    DeleteFileW(prev.c_str());

    // 重命名当前运行中的 exe → .prev（Windows 允许重命名在跑的 exe，只是不能删）
    if (!MoveFileExW(exe.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        LogLine(L"[upd] 重命名当前 exe 失败 err=%lu", GetLastError());
        return false;
    }
    // 把 staging 移入正式 exe 名
    if (!MoveFileExW(staged.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        // 回滚：把 .prev 还原回 exe
        MoveFileExW(prev.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        LogLine(L"[upd] 移入 staging 失败 err=%lu", GetLastError());
        return false;
    }
    // 清 pending，避免新进程再次进入交换
    DeleteFileW((UpdatesDir() + L"\\pending.json").c_str());

    LogLine(L"[upd] 已交换新版本，重启新进程");
    TrayIcon::Instance().Destroy();   // 清掉旧托盘图标，新进程会重建
    ShellExecuteW(nullptr, L"open", exe.c_str(),
                  m_cmdLine.empty() ? nullptr : m_cmdLine.c_str(),
                  dir.c_str(), SW_SHOWDEFAULT);
    ExitProcess(0);                  // 退出旧进程；新进程已经起来
    return true;
}

} // namespace lj
