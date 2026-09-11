#pragma once
// ============================================================
//  AccountStore.h — 账户系统（移植自 WebApp store.js 的账户层）
//  职责：账户列表 / 会话 / 按账户隔离的数据根目录。
//  · 演示账户等同全新用户：仓库不含任何预置内容，一切从空白开始搭建。
//  · 新注册账户内容默认为空（与网页端一致），自行搭建。
//  · 认证权威在服务端（见 app/Cloud.h）：注册与登录先问云端，本地只保留一份
//    镜像用于离线校验。服务端没开或断网时全部功能照常，联网后自动回接。
// ============================================================
#include <string>
#include <vector>

namespace lj {

struct Account
{
    std::wstring username;
    // 本地口令校验用。格式 "pbkdf2$<迭代数>$<盐hex>$<派生hex>"（S0-2 后的新格式）。
    // 历史遗留的 16 位纯十六进制 = 旧 FNV-1a，仅用于兼容校验，登录成功即自动升级。
    std::wstring passHash;
    long long    createdAt = 0;
    std::wstring uid;          // 用户编号 #字母数字
    bool         isDemo = false;
};

struct Session
{
    std::wstring user;         // 用户名 或 "__guest__"
    std::wstring mode;         // "user" | "guest"
};

// 账户资料（个人界面「我的」展示与编辑）
struct AccountProfile
{
    std::wstring bio;       // 个人简介
    std::wstring major;     // 专业
    std::wstring school;    // 学校
    std::wstring birthday;  // 生日
    std::wstring gender;    // 性别
};

class AccountStore
{
public:
    static AccountStore& Instance();

    // 程序启动：确保 accounts 目录 + 演示账户存在；载入/建立会话
    void Init();

    bool Register(const std::wstring& name, const std::wstring& pass, std::wstring& err);
    bool Login(const std::wstring& name, const std::wstring& pass, std::wstring& err);
    void QuickLogin(const std::wstring& name);   // 本地已存在账户，免密切换
    void Guest();
    void Logout();

    const Session& Current() const { return m_session; }
    std::wstring CurrentName() const;     // 访客返回 L"访客"
    bool IsGuest() const { return m_session.mode == L"guest"; }
    std::wstring UserId();                // 当前账户 uid（访客临时生成）
    std::wstring CurrentRoot() const;     // <exe>/accounts/<name>/  （末尾含分隔符）
    std::wstring AccountsRoot() const;    // <exe>/accounts/          （末尾含分隔符）

    std::vector<Account> List() const { return m_accounts; }
    bool AccountExists(const std::wstring& name) const;
    bool HasSession() const { return !m_session.user.empty(); }

    // 当前是否为演示账户（用于内容播种判定）
    bool IsDemoCurrent() const;

    // 云端登录成功后把服务端编号(u_…)同步到本地账户镜像。
    // 自动登录路径只走 Cloud::Login、不经 AccountStore::Login，本地 uid 不会被覆盖，
    // 导致演示账户一直显示本地播种的旧编号——用它补齐。
    void SyncCloudUid(const std::wstring& account, const std::wstring& cloudUid);

    // 账户资料（落盘 accounts/<name>/profile.json）
    AccountProfile LoadProfile() const;
    void SaveProfile(const AccountProfile& p);

private:
    // 云端认证通过后，在本机留一份账户镜像（离线时凭它进入）
    void MirrorAccount(const std::wstring& name, const std::wstring& pass,
                       const std::wstring& cloudUid);
    bool LocalPasswordOk(const std::wstring& name, const std::wstring& pass) const;

    std::wstring ExeDir() const;

    // ---- S0-2 口令哈希（CNG PBKDF2-HMAC-SHA256，零第三方依赖）----
    // 旧实现是 FNV-1a：无盐无迭代，离线爆破成本近乎为零，等同明文。
    // 新实现：每账户 16 字节随机盐 + 12 万次迭代 + 32 字节派生密钥。
    std::wstring MakeHash(const std::wstring& pass) const;                 // 生成新格式
    bool VerifyHash(const std::wstring& stored, const std::wstring& name,
                    const std::wstring& pass) const;                       // 兼容新旧两种格式
    static bool IsLegacyHash(const std::wstring& stored);                  // 判定旧 FNV 格式
    std::wstring LegacyHash(const std::wstring& name, const std::wstring& pass) const;
    void UpgradeHashIfLegacy(const std::wstring& name, const std::wstring& pass);

    std::wstring GenUid(const std::wstring& prefix) const;
    void EnsureDir(const std::wstring& path) const;
    std::wstring AccountsFilePath() const;
    std::wstring SessionFilePath() const;
    void WriteAccounts();
    void WriteSession();
    void LoadAccounts();
    void MigrateArchiveIfNeeded();

    std::wstring m_exeDir;
    std::vector<Account> m_accounts;
    Session m_session{};
    bool m_ready = false;

    static const wchar_t* kGuestUser;
};

} // namespace lj
