// ============================================================
//  QuizScheduler.cpp — 练考模块 Phase 2-3：每日定时出题调度器
//  仿 ReviewNudge（core/ReviewNudge.cpp）：message-only 窗口承载 60s 定时器，
//  到点（默认 07:30）校验后于后台线程抓 RSS + 调 QuizGen 落盘。纯本地规则，不读窗内文本。
// ============================================================
#include "quiz/QuizScheduler.h"
#include "quiz/QuizGen.h"
#include "quiz/QuizRss.h"
#include "app/AccountStore.h"
#include "app/Json.h"
#include <process.h>
#include <ctime>

namespace lj {

// ---------------- 字符串工具 ----------------
static std::wstring U2W(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring o; o.resize((size_t)n);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n);
    return o;
}

// AI 出题凭据（单一来源：账户目录 ai.json，由设置页「练考」分区写入；
// 本地独占、刻意不参与云端同步）。兼容迁移：旧版配置文件 kanban_ai.json
// 作为回退，未配置则字段为空 → 视为未启用。
static quiz::QuizAIConfig LoadLocalAIConfig()
{
    quiz::QuizAIConfig cfg;
    std::wstring root = AccountStore::Instance().CurrentRoot();
    std::string buf;
    for (const wchar_t* name : { L"ai.json", L"kanban_ai.json" }) {
        if (lj::json::ReadFileRaw(root + name, buf) && !buf.empty()) break;
        buf.clear();
    }
    if (buf.empty()) return cfg;
    using namespace lj::json;
    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    auto get = [&](const char* k) -> std::wstring {
        if (auto* o = JGet(v, k)) if (o->type == JVal::Str) return U2W(o->str);
        return L"";
    };
    cfg.apiBase = get("apiBase");
    cfg.apiKey  = get("apiKey");
    cfg.model   = get("model");
    if (cfg.model.empty()) cfg.model = L"deepseek-chat";
    cfg.enabled = !cfg.apiBase.empty() && !cfg.apiKey.empty();
    return cfg;
}

static const wchar_t* kSchedulerClass = L"FloriQuizSchedulerWindow";

QuizScheduler& QuizScheduler::Instance()
{
    static QuizScheduler s;
    return s;
}

void QuizScheduler::Init()
{
    if (m_inited) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &QuizScheduler::WndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kSchedulerClass;
    RegisterClassExW(&wc);   // 重入注册失败无害

    m_hwnd = CreateWindowExW(0, kSchedulerClass, L"FloriQuizScheduler", 0,
                             0, 0, 0, 0, HWND_MESSAGE, nullptr,
                             GetModuleHandleW(nullptr), this);
    if (!m_hwnd) return;
    m_timer = (UINT)SetTimer(m_hwnd, 1, 60000, nullptr);  // 每 60s 检查是否到出题时刻
    m_inited = true;
}

void QuizScheduler::SetConfig(bool enabled, int hour, int minute,
                             const std::wstring& category, int qcount, bool rss)
{
    m_enabled = enabled;
    m_hour = hour; m_minute = minute;
    if (!category.empty()) m_category = category;
    if (qcount > 0) m_qcount = qcount;
    m_rss = rss;
}

void QuizScheduler::GetConfig(bool& enabled, int& hour, int& minute,
                             std::wstring& category, int& qcount, bool& rss) const
{
    std::lock_guard<std::mutex> lk(m_mu);
    enabled = m_enabled; hour = m_hour; minute = m_minute;
    category = m_category; qcount = m_qcount; rss = m_rss;
}

void QuizScheduler::SetQuiet(bool q) { m_quiet.store(q); }

QuizGenResult QuizScheduler::Last() const
{
    std::lock_guard<std::mutex> lk(m_mu);
    return m_last;
}

std::wstring QuizScheduler::TodayISO()
{
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[16];
    swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return buf;
}

// ---------------- 后台生成线程 ----------------
unsigned __stdcall QuizScheduler::GenThread(void* arg)
{
    GenArgs* a = (GenArgs*)arg;
    a->self->RunGen(a);
    return 0;
}

void QuizScheduler::RunGen(GenArgs* a)
{
    std::wstring err, digest, rssErr;

    if (a->rss) {
        // 拼默认源（为空则 QuizFetchRssDigest 直接返回空，不联网）
        std::wstring feeds;
        for (int i = 0; i < lj::quiz::kQuizRssDefaultFeedCount; ++i) {
            if (!lj::quiz::kQuizRssDefaultFeeds[i]) continue;
            if (!feeds.empty()) feeds += L";";
            feeds += lj::quiz::kQuizRssDefaultFeeds[i];
        }
        lj::quiz::QuizFetchRssDigest(feeds, digest, rssErr);
    }

    bool ok = lj::quiz::QuizGenerateDaily(a->outDir, a->cfg, a->dateISO,
                                          a->category, a->qcount, digest, err);
    if (ok && !rssErr.empty()) err = rssErr + L"\n" + err;  // 附 RSS 降级提示

    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_last.ok = ok;
        m_last.msg = err;
        m_last.dateISO = a->dateISO;
        m_last.ts = (long long)time(nullptr);
    }
    m_busy.store(false);
    delete a;
}

void QuizScheduler::MaybeStart(bool manual)
{
    if (!m_inited) return;
    if (m_busy.load()) return;
    // 考试模式零打扰：自动路径遇 quiet 静默跳过（手动 KickNow 不受影响）
    if (!manual && m_quiet.load()) return;

    // 访客不生成
    if (AccountStore::Instance().IsGuest()) {
        if (manual) {
            std::lock_guard<std::mutex> lk(m_mu);
            m_last = { false, L"访客账户不生成试卷（请登录账户）", TodayISO(), (long long)time(nullptr) };
        }
        return;
    }

    // AI 出题凭据（设置页「练考」分区写入的本地 ai.json）
    quiz::QuizAIConfig cfg = LoadLocalAIConfig();

    if (!m_enabled || !cfg.enabled) {
        if (manual) {
            std::lock_guard<std::mutex> lk(m_mu);
            m_last = { false, L"未配置 AI 凭据（请在设置页「练考」分区填写 API 地址与密钥）",
                       TodayISO(), (long long)time(nullptr) };
        }
        return;
    }

    std::wstring dateISO = TodayISO();
    std::wstring root = AccountStore::Instance().CurrentRoot();
    std::wstring quizDir = root + L"quiz\\";
    CreateDirectoryW(quizDir.c_str(), nullptr);
    std::wstring outDir = quizDir + m_category + L"\\";
    std::wstring path = outDir + dateISO + L".md";

    // 去重：今日已生成则跳过
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        if (manual) {
            std::lock_guard<std::mutex> lk(m_mu);
            m_last = { true, L"今日已生成：" + path, dateISO, (long long)time(nullptr) };
        }
        return;
    }

    // 定时窗：自动模式需已过目标时刻（当天补生成）；手动忽略
    if (!manual) {
        time_t t = time(nullptr);
        struct tm tm; localtime_s(&tm, &t);
        int nowMin = tm.tm_hour * 60 + tm.tm_min;
        int targetMin = m_hour * 60 + m_minute;
        if (nowMin < targetMin) return;     // 还没到出题时刻，继续等

        // 失败冷却：30 分钟内不重试，避免 API 故障刷屏
        if (time(nullptr) - m_lastAttempt < 30 * 60) return;
    }

    // 启动后台线程
    m_lastAttempt = (long long)time(nullptr);
    m_busy.store(true);
    GenArgs* a = new GenArgs{};
    a->cfg = cfg;
    a->outDir = outDir;
    a->dateISO = dateISO;
    a->category = m_category;
    a->qcount = m_qcount;
    a->rss = m_rss;
    a->self = this;
    HANDLE h = (HANDLE)_beginthreadex(nullptr, 0, &QuizScheduler::GenThread, a, 0, nullptr);
    if (h) CloseHandle(h);
    else { m_busy.store(false); delete a; }
}

void QuizScheduler::KickNow() { MaybeStart(true); }

void QuizScheduler::Tick() { MaybeStart(false); }

LRESULT CALLBACK QuizScheduler::WndProcStatic(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    QuizScheduler* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<QuizScheduler*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<QuizScheduler*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT QuizScheduler::WndProc(UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER) { Tick(); return 0; }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}

} // namespace lj
