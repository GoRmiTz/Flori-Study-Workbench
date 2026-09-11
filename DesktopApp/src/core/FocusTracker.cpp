// ============================================================
//  FocusTracker.cpp — 前台学习检测与专注计时
// ============================================================
#include "core/FocusTracker.h"
#include "app/Store.h"
#include "app/AccountStore.h"
#include "core/Common.h"
#include <psapi.h>
#include <cwctype>
#include <ctime>
#include <algorithm>

namespace lj {

FocusTracker& FocusTracker::Instance()
{
    static FocusTracker s;
    return s;
}

std::wstring FocusTracker::ToLower(std::wstring s)
{
    for (auto& c : s) c = (wchar_t)std::towlower((wint_t)c);
    return s;
}

std::wstring FocusTracker::BaseName(const std::wstring& path)
{
    auto i = path.find_last_of(L"\\/");
    std::wstring name = (i == std::wstring::npos) ? path : path.substr(i + 1);
    auto dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos) name = name.substr(0, dot);
    return ToLower(name);
}

void FocusTracker::SetStudyApps(std::vector<std::wstring> names)
{
    for (auto& n : names) n = ToLower(n);
    std::lock_guard<std::mutex> lk(m_cfgMu);
    m_studyApps = std::move(names);
}

void FocusTracker::SetStudyKeywords(std::vector<std::wstring> kw)
{
    for (auto& k : kw) k = ToLower(k);
    std::lock_guard<std::mutex> lk(m_cfgMu);
    m_studyKeywords = std::move(kw);
}

// ---------------- #71 用户白名单 ----------------
void FocusTracker::SetUserApps(std::vector<std::wstring> names)
{
    // 归一化：小写 + 去 .exe 后缀 + 去空白 + 去重
    std::vector<std::wstring> clean;
    for (auto& n : names) {
        std::wstring s = ToLower(n);
        while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(s.begin());
        while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.pop_back();
        if (s.size() > 4 && s.compare(s.size() - 4, 4, L".exe") == 0) s = s.substr(0, s.size() - 4);
        if (s.empty()) continue;
        if (std::find(clean.begin(), clean.end(), s) != clean.end()) continue;
        clean.push_back(s);
    }
    std::lock_guard<std::mutex> lk(m_cfgMu);
    m_userApps = std::move(clean);
}

std::vector<std::wstring> FocusTracker::UserApps() const
{
    std::lock_guard<std::mutex> lk(m_cfgMu);
    return m_userApps;
}

std::vector<std::wstring> FocusTracker::DefaultUserApps()
{
    // 默认覆盖「看老师视频 + 刷题 + 记笔记」三类常见专注场景
    return {
        L"potplayer", L"vlc", L"mpc-hc", L"mpv",          // 本地课程视频
        L"bilibili", L"tencentvideo", L"txedu",            // 网课客户端
        L"chrome", L"msedge", L"firefox",                  // 浏览器（网课/题库）
        L"obsidian", L"typora", L"notepad", L"onenote",    // 笔记
        L"anki", L"wpsoffice", L"winword", L"excel",       // 记忆 / 文档
        L"acrobat", L"sumatrapdf", L"foxitreader"          // 讲义 PDF
    };
}

std::vector<std::wstring> FocusTracker::LoadUserAppsFromSettings()
{
    AppSettings s = CheckinStore::Instance().LoadSettings();
    if (!s.focusAppsSeeded) {
        // 首次运行：播种默认名单并落盘，让用户一进设置就看到可用清单
        s.focusApps = DefaultUserApps();
        s.focusAppsSeeded = true;
        CheckinStore::Instance().SaveSettings(s);
    }
    return s.focusApps;
}

void FocusTracker::SaveUserApps(const std::vector<std::wstring>& names)
{
    SetUserApps(names);                      // 先归一化并即时生效
    AppSettings s = CheckinStore::Instance().LoadSettings();
    s.focusApps = UserApps();                // 取归一化后的结果落盘
    s.focusAppsSeeded = true;
    CheckinStore::Instance().SaveSettings(s);
}

// ---------------- 娱乐降权名单 ----------------
void FocusTracker::SetEntApps(std::vector<std::wstring> names)
{
    for (auto& n : names) n = ToLower(n);
    std::lock_guard<std::mutex> lk(m_cfgMu);
    m_entApps = std::move(names);
}
std::vector<std::wstring> FocusTracker::UserEntApps() const
{
    std::lock_guard<std::mutex> lk(m_cfgMu);
    return m_entApps;
}
std::vector<std::wstring> FocusTracker::DefaultEntApps()
{
    // 明确娱乐的进程：游戏客户端 + 长视频站（与学习白名单不冲突者）
    return {
        L"steam", L"steamwebhelper", L"csgo", L"dota2", L"leagueclient", L"wegame",
        L"epicgameslauncher", L"genshinimpact", L"valorant", L"minecraft", L"terraria",
        L"wutheringwaves", L"honkaiimpact3", L"cyberpunk2077", L"eldenring",
        L"youku", L"iqiyi", L"mgtv", L"sohutv", L"tudou", L"kuaishou", L"douyin"
    };
}
std::vector<std::wstring> FocusTracker::LoadEntAppsFromSettings()
{
    AppSettings s = CheckinStore::Instance().LoadSettings();
    if (!s.focusEntSeeded) {
        s.focusEntApps = DefaultEntApps();
        s.focusEntSeeded = true;
        CheckinStore::Instance().SaveSettings(s);
    }
    return s.focusEntApps;
}
void FocusTracker::SaveEntApps(const std::vector<std::wstring>& names)
{
    SetEntApps(names);
    AppSettings s = CheckinStore::Instance().LoadSettings();
    s.focusEntApps = UserEntApps();
    s.focusEntSeeded = true;
    CheckinStore::Instance().SaveSettings(s);
}

bool FocusTracker::IsStudyWindow(const std::wstring& proc, const std::wstring& title) const
{
    // 芙洛理自身 foreground 即视为专注：否则番茄钟联动会在用户停留在应用内
    // （看专栏、在自习室、写笔记）时误判为「离开学习」而暂停计时。
    // idle 仍由上层用 st.idle 剔除。
    if (proc == L"Flori") return true;

    std::lock_guard<std::mutex> lk(m_cfgMu);

    // #71 用户白名单优先：非空则「只认名单」——命中即专注（不查标题关键词，
    // 看老师视频/播放课件都能正常计时），名单外一律返回 false → 番茄钟自动暂停。
    if (!m_userApps.empty()) {
        for (const auto& a : m_userApps) {
            if (!a.empty() && proc.find(a) != std::wstring::npos) return true;
        }
        return false;
    }

    // 名单为空 → 回退内置智能判定
    bool appMatch = false;
    for (const auto& a : m_studyApps) {
        if (proc.find(a) != std::wstring::npos) { appMatch = true; break; }
    }
    if (!appMatch) return false;
    // 浏览器/通用应用必须标题含学习关键词，避免把刷社交也算成学习
    for (const auto& k : m_studyKeywords) {
        if (title.find(k) != std::wstring::npos) return true;
    }
    return false;
}

bool FocusTracker::MatchesEnt(const std::wstring& proc) const
{
    std::lock_guard<std::mutex> lk(m_cfgMu);
    for (const auto& a : m_entApps)
        if (!a.empty() && proc.find(a) != std::wstring::npos) return true;
    return false;
}

void FocusTracker::Start()
{
    if (m_running.exchange(true)) return;
    m_requestStop.store(false);

    // 默认学习应用白名单（可后续改为读配置）
    if (m_studyApps.empty()) {
        SetStudyApps({
            L"chrome", L"msedge", L"firefox", L"opera",          // 浏览器
            L"notepad", L"obsidian", L"typora", L"anki",          // 笔记/记忆
            L"potplayer", L"vlc", L"mpc-hc",                       // 本地视频
            L"zoom", L"txedu", L"dingtalk", L"wechat",             // 网课/会议（标题仍要关键词）
            L"xuetangx", L"icourse163", L"bilibili", L"mooc"
        });
    }
    // #71 用户白名单：从 settings.json 载入（首次运行自动播种默认清单）
    if (m_userApps.empty()) SetUserApps(LoadUserAppsFromSettings());
    // 娱乐降权名单：从 settings.json 载入（首次运行自动播种默认清单）
    if (m_entApps.empty()) SetEntApps(LoadEntAppsFromSettings());
    if (m_studyKeywords.empty()) {
        SetStudyKeywords({
            L"网课", L"课程", L"学习", L"刷题", L"题库",
            L"申论", L"行测", L"考研", L"英语", L"单词",
            L"政治", L"专业课", L"视频", L"直播", L"教室",
            L"慕课", L"mooc", L"学堂", L"腾讯课堂", L"bilibili",
            L"coursera", L"edx", L"khan", L"leetcode", L"牛客"
        });
    }

    m_thread = std::thread([this] { WorkerLoop(); });
}

void FocusTracker::Stop()
{
    if (!m_running.exchange(false)) return;
    m_requestStop.store(true);
    if (m_thread.joinable()) m_thread.join();
    Flush();
}

void FocusTracker::Pause()
{
    std::lock_guard<std::mutex> lk(m_stateMu);
    // 立即结束进行中的会话并落盘，避免暂停期间已累积分钟丢失
    FlushCategoryLocked((int)FocusCat::Study, m_studySec, m_studyStart, m_inStudy, m_state.process, L"自动计时");
    FlushCategoryLocked((int)FocusCat::Entertainment, m_entSec, m_entStart, m_inEnt, m_entProc, L"娱乐降权");
    m_paused = true;
}

void FocusTracker::Resume()
{
    m_paused = false;
}

void FocusTracker::Flush()
{
    std::lock_guard<std::mutex> lk(m_stateMu);
    FlushCategoryLocked((int)FocusCat::Study, m_studySec, m_studyStart, m_inStudy, m_state.process, L"自动计时");
    FlushCategoryLocked((int)FocusCat::Entertainment, m_entSec, m_entStart, m_inEnt, m_entProc, L"娱乐降权");
}

void FocusTracker::FlushCategoryLocked(int cat, int& sec, long long& start, bool& inSession,
                                      const std::wstring& proc, const std::wstring& tag)
{
    if (!inSession || sec <= 0) return;

    int minutes = sec / 60;
    if (minutes <= 0) { sec = 0; return; }

    FocusSession fs;
    fs.start = start;
    fs.end = start + sec;
    fs.min = minutes;
    fs.category = cat;
    fs.proc = proc;

    time_t now = (time_t)fs.end;
    struct tm tmv{};
    localtime_s(&tmv, &now);
    wchar_t buf[16]{};
    swprintf_s(buf, L"%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    fs.date = buf;
    fs.tag = tag;

    CheckinStore::Instance().AddFocus(fs);

    LogLine(L"[focus] flush cat=%d %d min", cat, fs.min);

    // 保留不足 60s 的余量继续累积
    sec = sec % 60;
    if (sec > 0) {
        start = (long long)time(nullptr) - sec;
    } else {
        inSession = false;
        start = 0;
    }
}

FocusState FocusTracker::Snapshot() const
{
    std::lock_guard<std::mutex> lk(m_stateMu);
    return m_state;
}

std::wstring FocusTracker::LastOtherApp() const
{
    std::lock_guard<std::mutex> lk(m_stateMu);
    return m_lastOther;
}

void FocusTracker::WorkerLoop()
{
    while (!m_requestStop.load()) {
        Sleep(1000);
        if (m_requestStop.load()) break;

        FocusState st;
        HWND hwnd = GetForegroundWindow();
        if (hwnd) {
            wchar_t titleBuf[256]{};
            GetWindowTextW(hwnd, titleBuf, 256);
            st.title = titleBuf;

            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid) {
                HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
                if (h) {
                    wchar_t pathBuf[MAX_PATH]{};
                    DWORD sz = MAX_PATH;
                    if (QueryFullProcessImageNameW(h, 0, pathBuf, &sz)) {
                        st.process = BaseName(pathBuf);
                    }
                    CloseHandle(h);
                }
            }
        }

        LASTINPUTINFO li{};
        li.cbSize = sizeof(li);
        st.idle = false;
        if (GetLastInputInfo(&li)) {
            DWORD idleMs = GetTickCount() - li.dwTime;
            st.idle = idleMs >= m_idleMs;
        }

        st.studying = IsStudyWindow(st.process, st.title) && !st.idle;

        // 分类：挂机 > 学习 > 娱乐 > 其他（决定「记了什么」与是否计入专注）
        int cat;
        if (st.idle)                                  cat = (int)FocusCat::Idle;
        else if (IsStudyWindow(st.process, st.title)) cat = (int)FocusCat::Study;
        else if (MatchesEnt(st.process))              cat = (int)FocusCat::Entertainment;
        else                                          cat = (int)FocusCat::Other;
        st.category = cat;
        st.studying = (cat == (int)FocusCat::Study);

        std::lock_guard<std::mutex> lk(m_stateMu);
        m_state = st;
        // 记住最近一次非芙洛理前台进程：供自习室「一键加入刚才用的应用」使用
        if (!st.process.empty() && st.process != L"Flori") m_lastOther = st.process;

        if (m_paused.load(std::memory_order_acquire)) {
            // F-D3 暂停态：结束进行中的会话、不累计专注，连续秒数归零
            if (m_inStudy)
                FlushCategoryLocked((int)FocusCat::Study, m_studySec, m_studyStart, m_inStudy, st.process, L"自动计时");
            if (m_inEnt)
                FlushCategoryLocked((int)FocusCat::Entertainment, m_entSec, m_entStart, m_inEnt, m_entProc, L"娱乐降权");
            m_state.continuousSec = 0;
        } else if (cat == (int)FocusCat::Study) {
            if (!m_inStudy) { m_inStudy = true; m_studyStart = (long long)time(nullptr); m_studySec = 0; }
            m_studySec++;
            m_state.continuousSec = m_studySec;
            if (m_studySec % 60 == 0)
                FlushCategoryLocked((int)FocusCat::Study, m_studySec, m_studyStart, m_inStudy, st.process, L"自动计时");
            if (m_inEnt)
                FlushCategoryLocked((int)FocusCat::Entertainment, m_entSec, m_entStart, m_inEnt, m_entProc, L"娱乐降权");
        } else if (cat == (int)FocusCat::Entertainment) {
            if (m_inStudy)
                FlushCategoryLocked((int)FocusCat::Study, m_studySec, m_studyStart, m_inStudy, st.process, L"自动计时");
            if (!m_inEnt) { m_inEnt = true; m_entStart = (long long)time(nullptr); m_entSec = 0; m_entProc = st.process; }
            m_entSec++;
            m_state.continuousSec = 0;
            if (m_entSec % 60 == 0)
                FlushCategoryLocked((int)FocusCat::Entertainment, m_entSec, m_entStart, m_inEnt, m_entProc, L"娱乐降权");
        } else {
            // 挂机 / 其他：结束任何进行中的会话
            if (m_inStudy)
                FlushCategoryLocked((int)FocusCat::Study, m_studySec, m_studyStart, m_inStudy, st.process, L"自动计时");
            if (m_inEnt)
                FlushCategoryLocked((int)FocusCat::Entertainment, m_entSec, m_entStart, m_inEnt, m_entProc, L"娱乐降权");
            m_state.continuousSec = 0;
        }
    }
}

} // namespace lj
