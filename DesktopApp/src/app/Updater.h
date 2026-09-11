#pragma once
// ============================================================
//  Updater.h — P2-3 方案 A · 桌面分发加固 · 客户端自动更新
//
//  机制（全部遵守「UI 线程不碰网络」「只替换、不删除运行中 exe」）：
//   1. 启动后后台拉 Server /version，与本地 kAppVersion 比对；
//   2. 有新版本 → 托盘气泡提示 + 后台下载 staging 到 updates/ 目录，
//      写 pending.json，并挂出「重启并更新」菜单项；
//   3. 用户点「重启并更新」（或下次启动时发现 pending）→ 后台线程
//      把运行中的 exe 改名为 .prev（Windows 允许重命名在跑的 exe），
//      把 staging 移入 exe 名，重启新进程，退出旧进程；
//   4. 开发 / 截图路径（DesktopApp\build 等）不自动替换，防误伤。
//
//  下载地址由 Server /version 下发；上线时填安装包（同服路径或完整
//  https URL）。代码签名证书需自行采购，本模块不碰付费。
//
//  ── S0-1 安全加固（2026-08-11）────────────────────────────
//  在采购代码签名证书之前，用「零成本三道闸」把远程代码执行面压下去：
//   ① 传输强制：绝对 URL 只接受 https://；http:// 仅在回环地址放行
//      （本机联调）。同源相对路径沿用已配置的 Cloud 端点。
//   ② 内容校验：/version 必须下发 sha256（十六进制 64 位），下载完成后
//      本地用 CNG(BCrypt) 算 SHA-256 比对，不一致直接删包放弃；可选 size
//      字段做长度预检。**没有 sha256 = 不下载**，宁可不更新也不装来路不明的包。
//   ③ 落地二次校验：交换 exe 前重新读 staging 文件再算一次哈希（防下载
//      完成到重启之间被本地替换），并校验 PE 头 "MZ"。
//  威胁模型：把「中间人改包 → 任意代码执行」降级为「需同时攻破 HTTPS
//  与服务端下发的哈希」。签名证书到位后再叠加 WinVerifyTrust。
// ============================================================
#include <string>
#include <atomic>
#include <mutex>
#include <vector>

namespace lj {

namespace net { struct Endpoint; }   // 前置声明，避免头文件引入 net/Http.h

// 当前客户端版本。发版时改这里（与 Server /version 对齐）。
inline constexpr const char* kAppVersion = "1.0.0";

struct UpdateInfo
{
    std::string version;       // 服务端最新版本
    std::string minVersion;    // 强制最低版本（低于则必须更新）
    std::string downloadUrl;   // 安装包地址（同服路径如 /releases/x.exe，或完整 https URL）
    std::string notes;         // 更新说明（纯文本）
    std::string sha256;        // S0-1：安装包 SHA-256（64 位小写十六进制）——缺失则拒绝下载
    long long   size = 0;      // S0-1：安装包字节数，可选；>0 时做长度预检
    bool       available = false;
};

// 版本比较：a>b → >0；a==b → 0；a<b → <0。解析 "1.2.3"。
inline int CompareVersion(const std::string& a, const std::string& b)
{
    auto parse = [](const std::string& s, std::vector<int>& v) {
        v.clear();
        std::string cur;
        for (char c : s) {
            if (c == '.') { v.push_back(cur.empty() ? 0 : atoi(cur.c_str())); cur.clear(); }
            else if (c >= '0' && c <= '9') cur += c;
        }
        if (!cur.empty()) v.push_back(atoi(cur.c_str()));
    };
    std::vector<int> va, vb;
    parse(a, va); parse(b, vb);
    size_t n = (std::max)(va.size(), vb.size());
    for (size_t i = 0; i < n; ++i) {
        int x = i < va.size() ? va[i] : 0;
        int y = i < vb.size() ? vb[i] : 0;
        if (x != y) return x - y;
    }
    return 0;
}

class Updater
{
public:
    static Updater& Instance();

    // 保存启动命令行，供重启时原样传递
    void SetCmdLine(const std::wstring& cmd) { m_cmdLine = cmd; }

    // 启动后后台检查一次（由 App 在 Cloud 初始化后调用；截图模式不走）
    void CheckAsync();
    // 用户手动「检查更新」
    void CheckNow();
    // 后台下载 staging + 写 pending.json（有新版本时自动触发，也可手动）
    void DownloadAsync();

    bool HasUpdate() const;
    UpdateInfo Info() const;

    // 「重启并更新」：立即交换并重启（UI 线程调用，开发路径不执行）
    void ApplyNow();
    // 启动时调用：若上次已下载则交换并重启；否则正常启动（开发/截图路径跳过）
    void ApplyStartupPending(const std::wstring& cmdLine);

private:
    Updater() = default;
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    bool        IsDevPath() const;                       // 开发/截图路径不自动替换
    std::wstring ExePath() const;
    std::wstring UpdatesDir() const;
    static bool FileExists(const std::wstring& p);
    bool        PendingExists() const;
    void        NotifyAvailable();                       // 托盘气泡 + 挂菜单项
    void        Parse(const std::string& body);          // 解析 /version 响应
    bool        SwapAndRelaunch();                       // 交换 exe 并重启新进程
    bool        ParseUrl(const std::string& url, net::Endpoint& ep, std::wstring& path);

    // ---- S0-1 安全校验 ----
    // 用 CNG(BCrypt) 算 SHA-256，输出 64 位小写十六进制；失败返回 false。
    static bool Sha256Hex(const std::string& data, std::string& hex);
    // 回环地址判定（127.0.0.1 / localhost / ::1）——只有回环才允许明文 http。
    static bool IsLoopback(const std::wstring& host);
    // 校验 staging 文件：PE 头 + 长度 + SHA-256。err 回填人类可读原因。
    static bool VerifyPackage(const std::string& bytes, const std::string& wantSha,
                              long long wantSize, std::wstring& err);

    mutable std::mutex m_mu;
    UpdateInfo m_info{};
    std::atomic<bool> m_checking{ false };
    std::atomic<bool> m_notified{ false };
    std::wstring      m_cmdLine;
};

} // namespace lj
