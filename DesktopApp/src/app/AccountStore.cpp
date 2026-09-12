#define _CRT_SECURE_NO_WARNINGS
#include "core/Common.h"
#include "app/AccountStore.h"
#include "app/Cloud.h"

#include <windows.h>
#include <bcrypt.h>      // S0-2：CNG PBKDF2-HMAC-SHA256（系统库，已在 CMake 白名单）
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <random>
#include <algorithm>
#include <vector>

namespace lj {

const wchar_t* AccountStore::kGuestUser = L"__guest__";

// ---------------- 本地 UTF-8 编解码 ----------------
static std::string W2U(const std::wstring& s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string o; o.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n, nullptr, nullptr);
    return o;
}
static std::wstring U2W(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring o; o.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n);
    return o;
}

// ---------------- 解析辅助（仅用于我们自己生成的 accounts.json） ----------------
static std::string jUnescape(const std::string& in)
{
    std::string o; o.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 1 < in.size()) {
            char e = in[++i];
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                default: o += e; break;
            }
        } else o += in[i];
    }
    return o;
}
static std::string findStr(const std::string& s, const char* key)
{
    std::string pat = std::string("\"") + key + "\":";
    auto p = s.find(pat);
    if (p == std::string::npos) return "";
    p = s.find('"', p + pat.size());
    if (p == std::string::npos) return "";
    auto q = s.find('"', p + 1);
    if (q == std::string::npos) return "";
    return jUnescape(s.substr(p + 1, q - p - 1));
}
static long long findNum(const std::string& s, const char* key)
{
    std::string pat = std::string("\"") + key + "\":";
    auto p = s.find(pat);
    if (p == std::string::npos) return 0;
    p += pat.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    return (long long)strtoll(s.c_str() + p, nullptr, 10);
}
static bool findBool(const std::string& s, const char* key)
{
    std::string pat = std::string("\"") + key + "\":";
    auto p = s.find(pat);
    if (p == std::string::npos) return false;
    p += pat.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
    return (p < s.size() && s[p] == 't');
}
static std::string jStr(const std::string& v)
{
    std::string o;
    for (char c : v) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return "\"" + o + "\"";
}

// ---------------- 路径 ----------------
std::wstring AccountStore::ExeDir() const
{
    wchar_t buf[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    auto pos = p.find_last_of(L'\\');
    if (pos != std::wstring::npos) p = p.substr(0, pos);
    return p;
}
std::wstring AccountStore::AccountsRoot() const
{
    return m_exeDir + L"\\accounts\\";
}
std::wstring AccountStore::AccountsFilePath() const
{
    return AccountsRoot() + L"accounts.json";
}
std::wstring AccountStore::SessionFilePath() const
{
    return AccountsRoot() + L"session.json";
}
void AccountStore::EnsureDir(const std::wstring& path) const
{
    CreateDirectoryW(path.c_str(), nullptr);
}

std::wstring AccountStore::CurrentRoot() const
{
    std::wstring name = IsGuest() ? std::wstring(kGuestUser) : m_session.user;
    std::wstring root = AccountsRoot() + name + L"\\";
    EnsureDir(AccountsRoot());
    EnsureDir(root);
    return root;
}

// ---------------- 哈希 / uid ----------------
//
//  S0-2（2026-08-11）：本地口令哈希由 FNV-1a 换成 PBKDF2-HMAC-SHA256。
//  FNV-1a 是**非加密**哈希——无随机盐、无迭代、单次乘异或，拿到 accounts.json
//  等于拿到明文口令（一张彩虹表就穿了）。本地存储同样不能用它存口令。
//  新方案全部走 Windows CNG（bcrypt.dll，已在 CMake 系统库白名单，零第三方依赖）。
//
//  存储格式： pbkdf2$<迭代数>$<盐hex 32 字符>$<派生hex 64 字符>
//  兼容策略： 旧记录是 16 位纯十六进制，VerifyHash 识别后按旧算法校验，
//            校验通过立刻改写成新格式（UpgradeHashIfLegacy），用户无感迁移。

// 迭代数按 OWASP 对 PBKDF2-HMAC-SHA256 的建议取 60 万。
// 本机实测：12 万次 ≈ 23ms → 60 万次 ≈ 116ms。只发生在注册/登录路径上，
// 而登录本来就要同步走一次 WinHTTP 请求（数百毫秒），这点开销无感。
// 迭代数写进哈希串里，将来上调不影响老记录校验。
static const int    kPbkdf2Iterations = 600000;
static const size_t kSaltBytes = 16;
static const size_t kDerivedBytes = 32;

static std::wstring BytesToHexW(const unsigned char* p, size_t n)
{
    static const wchar_t* kHex = L"0123456789abcdef";
    std::wstring o;
    o.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        o += kHex[p[i] >> 4];
        o += kHex[p[i] & 0x0F];
    }
    return o;
}

static bool HexToBytes(const std::wstring& hex, std::vector<unsigned char>& out)
{
    if (hex.size() % 2) return false;
    out.clear();
    out.reserve(hex.size() / 2);
    auto nib = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hex.size(); i += 2) {
        int hi = nib(hex[i]), lo = nib(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back((unsigned char)((hi << 4) | lo));
    }
    return true;
}

// PBKDF2-HMAC-SHA256 派生；失败返回 false（调用方须当作校验不通过）
static bool Pbkdf2(const std::string& passUtf8,
                   const unsigned char* salt, size_t saltLen,
                   int iterations, unsigned char* out, size_t outLen)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    // BCRYPT_ALG_HANDLE_HMAC_FLAG：把 SHA256 provider 当 HMAC 用，PBKDF2 必需
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr,
                                    BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
        return false;
    NTSTATUS st = BCryptDeriveKeyPBKDF2(
        alg,
        (PUCHAR)passUtf8.data(), (ULONG)passUtf8.size(),
        (PUCHAR)salt, (ULONG)saltLen,
        (ULONGLONG)iterations,
        out, (ULONG)outLen, 0);
    BCryptCloseAlgorithmProvider(alg, 0);
    return st == 0;
}

bool AccountStore::IsLegacyHash(const std::wstring& stored)
{
    // 旧格式：恰好 16 位、全十六进制、无分隔符
    if (stored.size() != 16) return false;
    for (wchar_t c : stored) {
        bool hex = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
        if (!hex) return false;
    }
    return true;
}

std::wstring AccountStore::LegacyHash(const std::wstring& name, const std::wstring& pass) const
{
    // 旧 FNV-1a 64（salt = name），输出 16 位十六进制。**只用于兼容校验，不再用于写入。**
    std::string key = W2U(name) + ":" + W2U(pass);
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : key) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    wchar_t out[32];
    swprintf_s(out, L"%016llx", h);
    return out;
}

std::wstring AccountStore::MakeHash(const std::wstring& pass) const
{
    unsigned char salt[kSaltBytes] = { 0 };
    if (BCryptGenRandom(nullptr, salt, (ULONG)sizeof(salt), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        // 系统 RNG 不可用属于极端异常；退回 random_device 也比固定盐强
        std::random_device rd;
        for (size_t i = 0; i < kSaltBytes; ++i) salt[i] = (unsigned char)(rd() & 0xFF);
    }
    unsigned char key[kDerivedBytes] = { 0 };
    if (!Pbkdf2(W2U(pass), salt, kSaltBytes, kPbkdf2Iterations, key, kDerivedBytes))
        return std::wstring();     // 空串 = 无法离线校验，联网仍可登录（权威在服务端）

    std::wstring s = L"pbkdf2$";
    s += std::to_wstring(kPbkdf2Iterations);
    s += L"$" + BytesToHexW(salt, kSaltBytes);
    s += L"$" + BytesToHexW(key, kDerivedBytes);
    return s;
}

bool AccountStore::VerifyHash(const std::wstring& stored, const std::wstring& name,
                              const std::wstring& pass) const
{
    if (stored.empty()) return false;
    if (IsLegacyHash(stored)) return stored == LegacyHash(name, pass);
    if (stored.rfind(L"pbkdf2$", 0) != 0) return false;

    // 切分 pbkdf2$iter$salt$hash
    size_t p1 = stored.find(L'$');
    size_t p2 = stored.find(L'$', p1 + 1);
    size_t p3 = stored.find(L'$', p2 + 1);
    if (p2 == std::wstring::npos || p3 == std::wstring::npos) return false;

    int iter = _wtoi(stored.substr(p1 + 1, p2 - p1 - 1).c_str());
    if (iter <= 0 || iter > 5000000) return false;          // 挡掉被篡改成天文数字的迭代
    std::vector<unsigned char> salt, want;
    if (!HexToBytes(stored.substr(p2 + 1, p3 - p2 - 1), salt)) return false;
    if (!HexToBytes(stored.substr(p3 + 1), want)) return false;
    if (salt.empty() || want.empty() || want.size() > 64) return false;

    std::vector<unsigned char> got(want.size(), 0);
    if (!Pbkdf2(W2U(pass), salt.data(), salt.size(), iter, got.data(), got.size()))
        return false;

    // 定时安全比较：逐字节异或累积，不因首字节不同就提前返回
    unsigned char diff = 0;
    for (size_t i = 0; i < want.size(); ++i) diff |= (unsigned char)(want[i] ^ got[i]);
    return diff == 0;
}

void AccountStore::UpgradeHashIfLegacy(const std::wstring& name, const std::wstring& pass)
{
    for (auto& a : m_accounts) {
        if (a.username != name) continue;
        if (!IsLegacyHash(a.passHash)) return;              // 已是新格式
        std::wstring nh = MakeHash(pass);
        if (nh.empty()) return;
        a.passHash = nh;
        WriteAccounts();
        LogLine(L"[acct] 已把 %s 的本地口令哈希升级为 PBKDF2", name.c_str());
        return;
    }
}
std::wstring AccountStore::GenUid(const std::wstring& prefix) const
{
    static const wchar_t chars[] = L"ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::wstring id = prefix;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> d(0, (int)wcslen(chars) - 1);
    for (int i = 0; i < 5; ++i) id += chars[d(gen)];
    return id;
}

// ---------------- 持久化 ----------------
void AccountStore::WriteAccounts()
{
    EnsureDir(AccountsRoot());
    std::string s = "[\n";
    for (size_t i = 0; i < m_accounts.size(); ++i) {
        const auto& a = m_accounts[i];
        s += "  {\n";
        s += "    \"username\": " + jStr(W2U(a.username)) + ",\n";
        s += "    \"passHash\": " + jStr(W2U(a.passHash)) + ",\n";
        s += "    \"createdAt\": " + std::to_string(a.createdAt) + ",\n";
        s += "    \"uid\": " + jStr(W2U(a.uid)) + ",\n";
        s += "    \"isDemo\": " + std::string(a.isDemo ? "true" : "false") + "\n";
        s += (i + 1 < m_accounts.size()) ? "  },\n" : "  }\n";
    }
    s += "]\n";
    std::wstring fp = AccountsFilePath();
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (f) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
}
void AccountStore::WriteSession()
{
    EnsureDir(AccountsRoot());
    std::string s = "{\n";
    s += "  \"user\": " + jStr(W2U(m_session.user)) + ",\n";
    s += "  \"mode\": " + jStr(W2U(m_session.mode)) + "\n";
    s += "}\n";
    std::wstring fp = SessionFilePath();
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (f) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
}
void AccountStore::LoadAccounts()
{
    m_accounts.clear();
    std::wstring fp = AccountsFilePath();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }
    std::string buf; buf.resize((size_t)sz);
    size_t rd = fread(&buf[0], 1, (size_t)sz, f); fclose(f);
    if (rd != (size_t)sz) return;
    // 按对象切分
    size_t i = 0;
    while (i < buf.size()) {
        auto ob = buf.find('{', i);
        if (ob == std::string::npos) break;
        auto cb = buf.find('}', ob);
        if (cb == std::string::npos) break;
        std::string obj = buf.substr(ob, cb - ob + 1);
        Account a;
        a.username = U2W(findStr(obj, "username"));
        a.passHash = U2W(findStr(obj, "passHash"));
        a.createdAt = findNum(obj, "createdAt");
        a.uid = U2W(findStr(obj, "uid"));
        a.isDemo = findBool(obj, "isDemo");
        if (!a.username.empty()) m_accounts.push_back(a);
        i = cb + 1;
    }
}

void AccountStore::MigrateArchiveIfNeeded()
{
    // 首次运行（无 accounts 目录）但旧 archive/ 存在 → 迁到 accounts/demo/
    std::wstring acctRoot = AccountsRoot();
    if (GetFileAttributesW(acctRoot.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    std::wstring archive = m_exeDir + L"\\archive";
    if (GetFileAttributesW(archive.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    // 建 demo 目录并搬文件
    std::wstring dest = acctRoot + std::wstring(L"demo") + L"\\";
    EnsureDir(acctRoot);
    EnsureDir(dest);
    wchar_t pat[MAX_PATH];
    swprintf_s(pat, L"%s\\*.json", archive.c_str());
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring src = archive + L"\\" + fd.cFileName;
            std::wstring dst = dest + fd.cFileName;
            MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

// ---------------- Init ----------------
void AccountStore::Init()
{
    m_exeDir = ExeDir();
    EnsureDir(AccountsRoot());
    MigrateArchiveIfNeeded();
    LoadAccounts();

    // 载入会话；无会话则首启需由登录页引导注册/登录（App.cpp 已据此路由到 login）。
    std::wstring fp = SessionFilePath();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (f) {
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            std::string buf; buf.resize((size_t)sz);
            fread(&buf[0], 1, (size_t)sz, f);
            fclose(f);
            std::wstring u = U2W(findStr(buf, "user"));
            std::wstring m = U2W(findStr(buf, "mode"));
            if (!u.empty()) {
                m_session.user = u;
                m_session.mode = (m == L"guest") ? L"guest" : L"user";
                // 校验账户仍存在
                if (m_session.mode == L"user" && !AccountExists(m_session.user)) {
                    m_session = Session{};
                }
            }
        } else fclose(f);
    }
    m_ready = true;
}

// ---------------- 操作 ----------------
bool AccountStore::AccountExists(const std::wstring& name) const
{
    for (const auto& a : m_accounts)
        if (a.username == name) return true;
    return false;
}

// 本地账户镜像：云端认证通过后在本机留一份，保证断网时还能进得去。
// 这份镜像只用于离线校验，权威始终在服务端。
void AccountStore::MirrorAccount(const std::wstring& name, const std::wstring& pass,
                                 const std::wstring& cloudUid)
{
    for (auto& a : m_accounts) {
        if (a.username != name) continue;
        a.passHash = MakeHash(pass);              // 密码可能在别的设备上改过
        if (!cloudUid.empty()) a.uid = cloudUid;  // 统一用服务端编号
        WriteAccounts();
        return;
    }
    Account a;
    a.username = name;
    a.passHash = MakeHash(pass);
    a.createdAt = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    a.uid = cloudUid.empty() ? GenUid(L"u_") : cloudUid;
    a.isDemo = (name == L"demo");
    m_accounts.push_back(a);
    WriteAccounts();
}

bool AccountStore::LocalPasswordOk(const std::wstring& name, const std::wstring& pass) const
{
    for (const auto& a : m_accounts)
        if (a.username == name) return VerifyHash(a.passHash, name, pass);
    return false;
}

bool AccountStore::Register(const std::wstring& name, const std::wstring& pass, std::wstring& err)
{
    std::wstring n = name;
    n.erase(0, n.find_first_not_of(L" \t"));
    n.erase(n.find_last_not_of(L" \t") + 1);
    if (n.size() < 2) { err = L"账户名至少 2 个字符"; return false; }
    if (pass.size() < 4) { err = L"密码至少 4 位"; return false; }
    if (AccountExists(n)) { err = L"该账户已存在"; return false; }

    // 账户名唯一性由服务端裁决——否则两台机器可以各注册一个同名账户，
    // 等到联网同步时才发现撞车，那时已经晚了。
    std::wstring cerr;
    CloudResult cr = Cloud::Instance().Register(n, pass, cerr);
    if (cr == CloudResult::Rejected) { err = cerr; return false; }
    // Ok / Offline / Disabled 一律继续建本地账户：离线也必须能用，这是底线。

    MirrorAccount(n, pass, cr == CloudResult::Ok ? Cloud::Instance().Uid() : std::wstring());
    m_session.user = n;
    m_session.mode = L"user";
    WriteSession();

    if (cr == CloudResult::Ok) Cloud::Instance().SyncAsync();
    else Cloud::Instance().BindAccount(n);
    return true;
}

bool AccountStore::Login(const std::wstring& name, const std::wstring& pass, std::wstring& err)
{
    std::wstring cerr;
    CloudResult cr = Cloud::Instance().Login(name, pass, cerr);

    if (cr == CloudResult::Ok) {
        MirrorAccount(name, pass, Cloud::Instance().Uid());
        m_session.user = name;
        m_session.mode = L"user";
        WriteSession();
        Cloud::Instance().SyncAsync();      // 后台拉取合并，不挡登录动画
        return true;
    }

    if (cr == CloudResult::Rejected) {
        // 服务端连上了并且明确说密码不对 —— 以服务端为准，不给本地旧哈希开后门
        err = cerr;
        return false;
    }

    // NotFound / Offline / Disabled → 回退本地校验
    if (!AccountExists(name)) {
        err = (cr == CloudResult::NotFound) ? cerr : L"账户不存在";
        return false;
    }
    if (!LocalPasswordOk(name, pass)) { err = L"密码不正确"; return false; }
    // 校验通过且还是旧 FNV 记录 → 就地升级成 PBKDF2（这是唯一能拿到明文口令的时机）
    UpgradeHashIfLegacy(name, pass);

    m_session.user = name;
    m_session.mode = L"user";
    WriteSession();
    Cloud::Instance().BindAccount(name);

    // 本机离线时注册的账户，服务端还不认识它 —— 顺手补注册，把它接回云端
    if (cr == CloudResult::NotFound) {
        std::wstring rerr;
        if (Cloud::Instance().Register(name, pass, rerr) == CloudResult::Ok) {
            MirrorAccount(name, pass, Cloud::Instance().Uid());
            Cloud::Instance().SyncAsync();
        }
    }
    return true;
}

void AccountStore::QuickLogin(const std::wstring& name)
{
    if (!AccountExists(name)) return;
    m_session.user = name;
    m_session.mode = L"user";
    WriteSession();
    // 免密切换：只认这台机器上已有的云端令牌，没有就是「本地已登录、云端未登录」。
    // 演示账户的后台自动登录改放在 Cloud::Init()（每次启动都跑），不放在这里——
    // 因为 session.json 存在时 Init 会直接读它、跳过 QuickLogin，放这里会成死代码。
    Cloud::Instance().BindAccount(name);
}

void AccountStore::Guest()
{
    m_session.user = kGuestUser;
    m_session.mode = L"guest";
    WriteSession();
    Cloud::Instance().BindAccount(std::wstring());   // 访客不碰云端
}

void AccountStore::Logout()
{
    Cloud::Instance().Flush();     // 走之前先把没推完的改动送上去
    Cloud::Instance().Logout();
    m_session = Session{};
    // 移除会话文件 → 下次启动回到登录页
    _wremove(SessionFilePath().c_str());
}

std::wstring AccountStore::CurrentName() const
{
    if (IsGuest()) return L"访客";
    if (!HasSession()) return L"未登录";   // 退出后顶栏显示「未登录」而不是空串
    return m_session.user;
}

bool AccountStore::IsDemoCurrent() const
{
    if (IsGuest()) return false;
    for (const auto& a : m_accounts)
        if (a.username == m_session.user) return a.isDemo;
    return false;
}

void AccountStore::SyncCloudUid(const std::wstring& account, const std::wstring& cloudUid)
{
    if (cloudUid.empty()) return;
    for (auto& a : m_accounts) {
        if (a.username != account) continue;
        if (a.uid != cloudUid) { a.uid = cloudUid; WriteAccounts(); }
        return;
    }
}

std::wstring AccountStore::UserId()
{
    if (IsGuest()) {
        // 访客临时 uid（按会话目录存一份）
        std::wstring path = CurrentRoot() + L"_uid";
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (f) {
            wchar_t buf[32] = { 0 };
            size_t rd = fread(buf, sizeof(wchar_t), 31, f);
            fclose(f);
            std::wstring id(buf, rd / sizeof(wchar_t));
            if (!id.empty()) return id;
        }
        std::wstring id = GenUid(L"·");
        FILE* w = _wfopen(path.c_str(), L"wb");
        if (w) { fwrite(id.c_str(), sizeof(wchar_t), id.size(), w); fclose(w); }
        return id;
    }
    // 未登录（会话已清空）：返回稳定空串，绝不在每次调用时生成新 uid
    // —— 否则退出登录后顶栏弹层/「我的」页每帧拿到随机 uid，界面疯狂跳动
    if (!HasSession()) return std::wstring();
    for (auto& a : m_accounts)
        if (a.username == m_session.user) {
            if (a.uid.empty()) {
                a.uid = GenUid(L"u_");
                WriteAccounts();
            }
            return a.uid;
        }
    return std::wstring();
}

// ---------------- 账户资料（profile.json）----------------
AccountProfile AccountStore::LoadProfile() const
{
    AccountProfile p;
    std::wstring fp = CurrentRoot() + L"profile.json";
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (!f) return p;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return p; }
    std::string buf; buf.resize((size_t)sz);
    size_t rd = fread(&buf[0], 1, (size_t)sz, f); fclose(f);
    if (rd != (size_t)sz) return p;
    auto gs = [&](const char* k) -> std::wstring {
        auto p0 = buf.find(std::string("\"") + k + "\":");
        if (p0 == std::string::npos) return L"";
        p0 = buf.find('"', p0 + strlen(k) + 3);
        if (p0 == std::string::npos) return L"";
        auto q = buf.find('"', p0 + 1);
        if (q == std::string::npos) return L"";
        return U2W(jUnescape(buf.substr(p0 + 1, q - p0 - 1)));
    };
    p.bio     = gs("bio");
    p.major   = gs("major");
    p.school  = gs("school");
    p.birthday = gs("birthday");
    p.gender  = gs("gender");
    p.displayName = gs("displayName");   // 批次 H
    p.avatar  = gs("avatar");            // 批次 H
    return p;
}

void AccountStore::SaveProfile(const AccountProfile& p)
{
    std::string s = "{\n";
    s += "  \"bio\": "     + jStr(W2U(p.bio))     + ",\n";
    s += "  \"major\": "   + jStr(W2U(p.major))   + ",\n";
    s += "  \"school\": "  + jStr(W2U(p.school))  + ",\n";
    s += "  \"birthday\": " + jStr(W2U(p.birthday)) + ",\n";
    s += "  \"gender\": "  + jStr(W2U(p.gender))  + ",\n";
    s += "  \"displayName\": " + jStr(W2U(p.displayName)) + ",\n";
    s += "  \"avatar\": "  + jStr(W2U(p.avatar))  + "\n";
    s += "}\n";
    std::wstring fp = CurrentRoot() + L"profile.json";
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (f) { fwrite(s.data(), 1, s.size(), f); fclose(f); }
}

// ---------------- 单例 ----------------
AccountStore& AccountStore::Instance()
{
    static AccountStore s;
    return s;
}

} // namespace lj
