#define _CRT_SECURE_NO_WARNINGS
#include "app/Store.h"
#include "app/Data.h"
#include "app/Json.h"
#include "app/Cloud.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lj {

// ---------------- 编码工具 ----------------
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

// 天序号 -> 公历日期 的实现见 Data.h（内联），Store.cpp 直接复用。

// ---------------- 极简 JSON ----------------
//  解析 / 序列化的实现已抽到 app/Json.h（命名空间 lj::json），与云同步层 Cloud.cpp
//  共用同一套代码——此前两边各留一份会随需求各自漂移，接入服务端后必须收敛为一处。
using namespace lj::json;

// ---------------- CheckinStore ----------------
CheckinStore& CheckinStore::Instance()
{
    static CheckinStore s;
    return s;
}

std::wstring CheckinStore::FilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"checkin.json";
}

void CheckinStore::EnsureLoaded()
{
    if (m_loaded) return;
    m_loaded = true;
    LoadAll(m_all);
}

void CheckinStore::LoadAll(std::map<std::wstring, std::map<std::wstring, int>>& out)
{
    std::wstring fp = FilePath();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (!f) return;                         // 首次运行：无档案
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }
    std::string buf; buf.resize((size_t)sz);
    size_t rd = fread(&buf[0], 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) return;

    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Obj) return;
    for (auto& day : v.obj) {
        std::wstring dateKey = U2W(day.first);
        std::map<std::wstring, int> items;
        if (day.second.type == JVal::Obj) {
            for (auto& it : day.second.obj) {
                int val = (it.second.type == JVal::Num) ? (int)it.second.num : 0;
                items[U2W(it.first)] = val;
            }
        }
        out[dateKey] = items;
    }
}

void CheckinStore::WriteAll(const std::map<std::wstring, std::map<std::wstring, int>>& all)
{
    std::string s = "{\n";
    bool firstDay = true;
    for (auto& day : all) {
        if (!firstDay) s += ",\n";
        firstDay = false;
        s += "  " + JQuote(W2U(day.first)) + ": {";
        bool firstItem = true;
        for (auto& it : day.second) {
            if (!firstItem) s += ", ";
            firstItem = false;
            s += JQuote(W2U(it.first)) + ": " + std::to_string(it.second);
        }
        s += "}";
    }
    s += "\n}\n";

    std::wstring fp = FilePath();
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (!f) return;
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
    Cloud::Instance().MarkDirty();   // 写盘即打脏：后台线程防抖后静默上推
}

std::map<std::wstring, bool> CheckinStore::LoadDay(const std::wstring& dateKey)
{
    EnsureLoaded();
    std::map<std::wstring, bool> r;
    auto it = m_all.find(dateKey);
    if (it != m_all.end())
        for (auto& kv : it->second) r[kv.first] = kv.second != 0;
    return r;
}

void CheckinStore::SaveDay(const std::wstring& dateKey, const std::map<std::wstring, bool>& done)
{
    EnsureLoaded();
    std::map<std::wstring, int> m;
    for (auto& kv : done) m[kv.first] = kv.second ? 1 : 0;
    m_all[dateKey] = m;
    WriteAll(m_all);
}

std::vector<std::wstring> CheckinStore::LastNDays(int n)
{
    std::vector<std::wstring> r;
    Date td = Today();
    int t = DaysFromCivil(td.y, td.m, td.d);
    for (int i = n - 1; i >= 0; --i) {
        int s = t - i;
        Date d = DateFromCivil(s);
        r.push_back(FormatDate(d));
    }
    return r;
}

// ---------------- 专注会话 ----------------
std::wstring CheckinStore::FocusFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"focus.json";
}

std::vector<FocusSession> CheckinStore::LoadFocus()
{
    std::vector<FocusSession> out;
    std::wstring fp = FocusFilePath();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (!f) return out;                     // 首次运行：无记录
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return out; }
    std::string buf; buf.resize((size_t)sz);
    size_t rd = fread(&buf[0], 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) return out;

    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Arr) return out;
    for (auto& el : v.arr) {
        if (el.type != JVal::Obj) continue;
        FocusSession fs;
        auto g = [&](const char* k) -> const JVal* {
            auto it = el.obj.find(k); return it == el.obj.end() ? nullptr : &it->second;
        };
        if (auto* d = g("date")) fs.date = U2W(d->str);
        if (auto* s = g("start")) fs.start = (long long)s->num;
        if (auto* e = g("end"))   fs.end   = (long long)e->num;
        if (auto* m = g("min"))   fs.min   = (int)m->num;
        if (auto* t = g("tag"))   fs.tag   = U2W(t->str);
        if (auto* c = g("cat"))   fs.category = (int)c->num;
        if (auto* p = g("proc"))  fs.proc  = U2W(p->str);
        out.push_back(fs);
    }
    return out;
}

void CheckinStore::AddFocus(const FocusSession& s)
{
    auto all = LoadFocus();
    all.push_back(s);
    std::string out = "[\n";
    for (size_t i = 0; i < all.size(); ++i) {
        const auto& fs = all[i];
        out += "  {\n";
        out += "    \"date\": " + JQuote(W2U(fs.date)) + ",\n";
        out += "    \"start\": " + std::to_string(fs.start) + ",\n";
        out += "    \"end\": "   + std::to_string(fs.end) + ",\n";
        out += "    \"min\": "   + std::to_string(fs.min) + ",\n";
        out += "    \"tag\": "   + JQuote(W2U(fs.tag)) + ",\n";
        out += "    \"cat\": "   + std::to_string(fs.category) + ",\n";
        out += "    \"proc\": "  + JQuote(W2U(fs.proc)) + "\n";
        out += (i + 1 < all.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";

    std::wstring fp = FocusFilePath();
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    Cloud::Instance().MarkDirty();   // 写盘即打脏：后台线程防抖后静默上推
}

// ---------------- 自定义打卡项（items.json） ----------------
std::wstring CheckinStore::ItemsFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"items.json";
}

namespace {
// 从 JVal::Obj 读一个打卡项（缺字段用空/默认）
lj::CheckItem ReadItem(const lj::JVal& o)
{
    lj::CheckItem it;
    auto g = [&](const char* k) -> const lj::JVal* {
        auto it2 = o.obj.find(k); return it2 == o.obj.end() ? nullptr : &it2->second;
    };
    if (auto* v = g("id"))       it.id = lj::U2W(v->str);
    if (auto* v = g("slot"))     it.slot = lj::U2W(v->str);
    if (auto* v = g("title"))    it.title = lj::U2W(v->str);
    if (auto* v = g("standard")) it.standard = lj::U2W(v->str);
    if (auto* v = g("tag"))      it.tag = lj::U2W(v->str);
    if (auto* v = g("minutes"))  it.minutes = (int)v->num;
    if (auto* v = g("link"))     it.link = lj::U2W(v->str);
    if (auto* v = g("folder"))   it.folder = lj::U2W(v->str);
    if (it.id.empty()) it.id = it.title;   // 兜底 key
    return it;
}
// 写一个打卡项为 JSON 片段
void WriteItem(std::string& out, const lj::CheckItem& it, bool last)
{
    out += "    {\n";
    out += "      \"id\": "   + JQuote(W2U(it.id)) + ",\n";
    out += "      \"slot\": " + JQuote(W2U(it.slot)) + ",\n";
    out += "      \"title\": " + JQuote(W2U(it.title)) + ",\n";
    out += "      \"standard\": " + JQuote(W2U(it.standard)) + ",\n";
    out += "      \"tag\": " + JQuote(W2U(it.tag)) + ",\n";
    out += "      \"minutes\": " + std::to_string(it.minutes) + ",\n";
    out += "      \"link\": " + JQuote(W2U(it.link)) + ",\n";
    out += "      \"folder\": " + JQuote(W2U(it.folder)) + "\n";
    out += (last ? "    }" : "    },");
}
} // namespace

ChecklistBundle CheckinStore::ParseItems(const std::string& buf) const
{
    ChecklistBundle b;
    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Obj) return b;
    auto readGroup = [&](const char* key, std::vector<CheckItem>& dst) {
        auto it = v.obj.find(key);
        if (it == v.obj.end() || it->second.type != JVal::Arr) return;
        for (auto& el : it->second.arr) {
            if (el.type == JVal::Obj) dst.push_back(ReadItem(el));
        }
    };
    readGroup("daily", b.daily);
    readGroup("sat", b.sat);
    readGroup("sun", b.sun);
    return b;
}

std::string CheckinStore::SerializeItems(const ChecklistBundle& b) const
{
    auto groupStr = [&](const char* key, const std::vector<CheckItem>& g) -> std::string {
        std::string s = "  \"" + std::string(key) + "\": [";
        for (size_t i = 0; i < g.size(); ++i) {
            s += "\n";
            WriteItem(s, g[i], i + 1 == g.size());
            if (i + 1 < g.size()) s += ",";
        }
        s += "\n  ]";
        return s;
    };
    std::string out = "{\n";
    out += groupStr("daily", b.daily);
    out += ",\n";
    out += groupStr("sat", b.sat);
    out += ",\n";
    out += groupStr("sun", b.sun);
    out += "\n}\n";
    return out;
}

ChecklistBundle CheckinStore::LoadItems()
{
    std::wstring fp = ItemsFilePath();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (!f) return DefaultChecklist();          // 无文件：演示账户播种默认清单，新账户返回空
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return DefaultChecklist(); }
    std::string buf; buf.resize((size_t)sz);
    size_t rd = fread(&buf[0], 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) return DefaultChecklist();
    ChecklistBundle b = ParseItems(buf);
    if (b.daily.empty() && b.sat.empty() && b.sun.empty()) return DefaultChecklist();
    return b;
}

void CheckinStore::Reload()
{
    m_loaded = false;
    m_all.clear();
}

void CheckinStore::SaveItems(const ChecklistBundle& b)
{
    std::string out = SerializeItems(b);
    std::wstring fp = ItemsFilePath();
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    Cloud::Instance().MarkDirty();   // 写盘即打脏：后台线程防抖后静默上推
}

void CheckinStore::ResetItems()
{
    std::wstring fp = ItemsFilePath();
    _wremove(fp.c_str());
    // 注意：这里刻意不打脏。items 在服务端是整块覆盖语义，
    // 本地删档后若把「空」推上去，会连带抹掉云端和其它设备上的自定义清单；
    // 恢复默认属于本机行为，下次真正编辑清单时再同步。
}

// ============================================================
//  应用设置（settings.json）
// ============================================================
std::wstring CheckinStore::SettingsFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"settings.json";
}

AppSettings CheckinStore::LoadSettings()
{
    AppSettings s;
    std::string buf;
    if (!ReadFileRaw(SettingsFilePath(), buf)) return s;
    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    // 桌面端自己写的是数字 0/1；网页端与服务端可能写 true/false，两种都要认，
    // 否则跨端同步回来主题会莫名其妙被重置。
    if (auto* d = JGet(v, "dark")) {
        if (d->type == JVal::Num)       s.dark = (d->num != 0.0);
        else if (d->type == JVal::Bool) s.dark = d->bval;
    }
    // #71 专注白名单：字符串数组（进程名，已小写）
    if (auto* fa = JGet(v, "focusApps")) {
        if (fa->type == JVal::Arr) {
            s.focusAppsSeeded = true;   // 文件里出现过该键 = 已播种（哪怕是空数组）
            for (const auto& el : fa->arr) {
                if (el.type != JVal::Str || el.str.empty()) continue;
                s.focusApps.push_back(U2W(el.str));
            }
        }
    }
    // F-D1 娱乐降权名单
    if (auto* ea = JGet(v, "focusEntApps")) {
        if (ea->type == JVal::Arr) {
            s.focusEntSeeded = true;
            for (const auto& el : ea->arr) {
                if (el.type != JVal::Str || el.str.empty()) continue;
                s.focusEntApps.push_back(U2W(el.str));
            }
        }
    }
    // F-D3 全局热键（缺省不配置 → 全部为 0，App 使用内置默认）
    auto rdhk = [&](const char* key, AppSettings::HotkeyCombo& dst) {
        if (auto* h = JGet(v, key)) {
            if (auto* m = JGet(*h, "mod"))  dst.mod  = (unsigned int)m->num;
            if (auto* k = JGet(*h, "vkey")) dst.vkey = (unsigned int)k->num;
        }
    };
    rdhk("hotCheckin", s.hotCheckin);
    rdhk("hotPomodoro", s.hotPomodoro);
    rdhk("hotPause", s.hotPause);
    // F-D4 申论字数统计
    if (auto* de = JGet(v, "docxEnabled")) s.docxEnabled = (de->num != 0);
    if (auto* dp = JGet(v, "docxPaths")) {
        if (dp->type == JVal::Arr) {
            for (const auto& el : dp->arr) {
                if (el.type != JVal::Str || el.str.empty()) continue;
                s.docxPaths.push_back(U2W(el.str));
            }
        }
    }
    // F-D7 增强：复盘每日自动 nudge
    if (auto* rn = JGet(v, "reviewNudge"))    s.reviewNudge = (rn->num != 0);
    if (auto* rh = JGet(v, "reviewNudgeHour")) s.reviewNudgeHour = (int)rh->num;
    if (auto* rl = JGet(v, "reviewNudgeLast")) s.reviewNudgeLast = U2W(rl->str);
    // 关闭按钮行为（0 询问 / 1 退出 / 2 托盘）
    if (auto* ex = JGet(v, "exitAction")) s.exitAction = (int)ex->num;
    if (s.exitAction < 0 || s.exitAction > 2) s.exitAction = 0;
    // 本地音乐文件夹（批次 B）
    if (auto* md = JGet(v, "musicDir")) s.musicDir = U2W(md->str);
    // 专注体系（批次 C）
    if (auto* fi = JGet(v, "focusItem"))       s.focusItem = U2W(fi->str);
    if (auto* ff = JGet(v, "focusFullscreen")) s.focusFullscreen = (ff->num != 0);
    if (auto* fd = JGet(v, "focusItemDirect")) s.focusItemDirect = (fd->num != 0);

    return s;
}

void CheckinStore::SaveSettings(const AppSettings& s)
{
    std::string out = "{\n";
    out += "  \"dark\": " + std::string(s.dark ? "1" : "0") + ",\n";
    out += "  \"focusApps\": [";
    for (size_t i = 0; i < s.focusApps.size(); ++i) {
        out += (i ? ", " : "");
        out += JQuote(W2U(s.focusApps[i]));
    }
    out += "],\n";
    out += "  \"focusEntApps\": [";
    for (size_t i = 0; i < s.focusEntApps.size(); ++i) {
        out += (i ? ", " : "");
        out += JQuote(W2U(s.focusEntApps[i]));
    }
    out += "],\n";
    auto hk = [&](const char* key, const AppSettings::HotkeyCombo& c) {
        out += std::string("  \"") + key + "\": {\"mod\": " + std::to_string(c.mod)
             + ", \"vkey\": " + std::to_string(c.vkey) + "}";
    };
    hk("hotCheckin", s.hotCheckin);   out += ",\n";
    hk("hotPomodoro", s.hotPomodoro); out += ",\n";
    hk("hotPause", s.hotPause);
    out += ",\n";
    // F-D4 申论字数统计：仅存授权状态 + 用户显式选择的 docx 路径
    out += "  \"docxEnabled\": " + std::string(s.docxEnabled ? "1" : "0") + ",\n";
    out += "  \"docxPaths\": [";
    for (size_t i = 0; i < s.docxPaths.size(); ++i) {
        out += (i ? ", " : "");
        out += JQuote(W2U(s.docxPaths[i]));
    }
    out += "],\n";
    // F-D7 增强：复盘每日自动 nudge 配置
    out += "  \"reviewNudge\": " + std::string(s.reviewNudge ? "1" : "0") + ",\n";
    out += "  \"reviewNudgeHour\": " + std::to_string(s.reviewNudgeHour) + ",\n";
    out += "  \"reviewNudgeLast\": " + JQuote(W2U(s.reviewNudgeLast)) + ",\n";
    // 关闭按钮行为（0 询问 / 1 退出 / 2 托盘）
    out += "  \"exitAction\": " + std::to_string(s.exitAction) + ",\n";
    // 本地音乐文件夹（批次 B）
    out += "  \"musicDir\": " + JQuote(W2U(s.musicDir)) + ",\n";
    // 专注体系（批次 C）
    out += "  \"focusItem\": " + JQuote(W2U(s.focusItem)) + ",\n";
    out += "  \"focusFullscreen\": " + std::string(s.focusFullscreen ? "1" : "0") + ",\n";
    out += "  \"focusItemDirect\": " + std::string(s.focusItemDirect ? "1" : "0") + "\n";
    out += "}\n";
    WriteFileRaw(SettingsFilePath(), out);
    Cloud::Instance().MarkDirty();   // 写盘即打脏：后台线程防抖后静默上推
}

// ---------------- 关键倒计时（milestones.json）----------------
namespace {
// "YYYY-MM-DD" -> Date；解析失败回退默认
Date ParseDateStr(const std::wstring& s)
{
    Date d{ 2026, 1, 1 };
    int y = 2026, m = 1, dd = 1;
    if (swscanf_s(s.c_str(), L"%d-%d-%d", &y, &m, &dd) == 3) {
        d.y = y; d.m = m; d.d = dd;
    }
    return d;
}
} // namespace

std::vector<Milestone> CheckinStore::LoadMilestones() const
{
    std::vector<Milestone> out;
    std::string buf;
    if (!ReadFileRaw(MilestonesFilePath(), buf)) return out;
    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Arr) return out;
    for (auto& el : v.arr) {
        if (el.type != JVal::Obj) continue;
        Milestone ms;
        auto g = [&](const char* k) -> const JVal* {
            auto it = el.obj.find(k); return it == el.obj.end() ? nullptr : &it->second;
        };
        if (auto* x = g("label")) ms.label = U2W(x->str);
        if (auto* x = g("note"))  ms.note  = U2W(x->str);
        if (auto* x = g("date"))  ms.date  = ParseDateStr(U2W(x->str));
        if (auto* x = g("urgent")) ms.urgent = (x->type == JVal::Bool) ? x->bval : (x->num != 0.0);
        out.push_back(ms);
    }
    return out;
}

void CheckinStore::SeedMilestones(const std::vector<Milestone>& seed)
{
    if (seed.empty()) return;
    FILE* f = _wfopen(MilestonesFilePath().c_str(), L"rb");
    if (f) { fclose(f); return; }   // 已有存档，不覆盖用户自定义
    SaveMilestones(seed);
}

void CheckinStore::SaveMilestones(const std::vector<Milestone>& ms)
{
    std::string out = "[\n";
    for (size_t i = 0; i < ms.size(); ++i) {
        const auto& m = ms[i];
        out += "  {\n";
        out += "    \"label\": " + JQuote(W2U(m.label)) + ",\n";
        out += "    \"note\": "  + JQuote(W2U(m.note))  + ",\n";
        out += "    \"date\": "  + JQuote(W2U(FormatDate(m.date))) + ",\n";
        out += "    \"urgent\": " + std::string(m.urgent ? "true" : "false") + "\n";
        out += (i + 1 < ms.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";
    std::wstring fp = MilestonesFilePath();
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    Cloud::Instance().MarkDirty();   // 写盘即打脏：后台线程防抖后静默上推
}

// ---------------- 收藏 / 历史（favorites.json / history.json）----------------
std::wstring CheckinStore::FavoritesFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"favorites.json";
}
std::wstring CheckinStore::HistoryFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"history.json";
}

std::vector<Favorite> CheckinStore::LoadFavorites() const
{
    std::vector<Favorite> out;
    std::string buf;
    if (!ReadFileRaw(FavoritesFilePath(), buf)) return out;
    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Arr) return out;
    for (auto& el : v.arr) {
        if (el.type != JVal::Obj) continue;
        Favorite fv;
        auto g = [&](const char* k) -> const JVal* {
            auto it = el.obj.find(k); return it == el.obj.end() ? nullptr : &it->second;
        };
        if (auto* x = g("kind"))  fv.kind  = U2W(x->str);
        if (auto* x = g("id"))    fv.id    = U2W(x->str);
        if (auto* x = g("title")) fv.title = U2W(x->str);
        if (auto* x = g("author")) fv.author = U2W(x->str);
        if (auto* x = g("ts"))    fv.ts = (long long)x->num;
        out.push_back(fv);
    }
    return out;
}

bool CheckinStore::IsFav(const std::wstring& kind, const std::wstring& id) const
{
    for (auto& f : LoadFavorites())
        if (f.kind == kind && f.id == id) return true;
    return false;
}

void CheckinStore::ToggleFav(const std::wstring& kind, const std::wstring& id,
                             const std::wstring& title, const std::wstring& author)
{
    auto list = LoadFavorites();
    bool found = false;
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i].kind == kind && list[i].id == id) { list.erase(list.begin() + i); found = true; break; }
    }
    if (!found) {
        Favorite fv;
        fv.kind = kind; fv.id = id; fv.title = title; fv.author = author;
        fv.ts = (long long)time(nullptr);
        list.push_back(fv);
    }
    std::string out = "[\n";
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& f = list[i];
        out += "  {\n";
        out += "    \"kind\": "  + JQuote(W2U(f.kind))  + ",\n";
        out += "    \"id\": "    + JQuote(W2U(f.id))    + ",\n";
        out += "    \"title\": " + JQuote(W2U(f.title)) + ",\n";
        out += "    \"author\": " + JQuote(W2U(f.author)) + ",\n";
        out += "    \"ts\": " + std::to_string(f.ts) + "\n";
        out += (i + 1 < list.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";
    WriteFileRaw(FavoritesFilePath(), out);
    Cloud::Instance().MarkDirty();
}

std::vector<HistoryItem> CheckinStore::LoadHistory() const
{
    std::vector<HistoryItem> out;
    std::string buf;
    if (!ReadFileRaw(HistoryFilePath(), buf)) return out;
    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Arr) return out;
    for (auto& el : v.arr) {
        if (el.type != JVal::Obj) continue;
        HistoryItem h;
        auto g = [&](const char* k) -> const JVal* {
            auto it = el.obj.find(k); return it == el.obj.end() ? nullptr : &it->second;
        };
        if (auto* x = g("kind"))  h.kind  = U2W(x->str);
        if (auto* x = g("id"))    h.id    = U2W(x->str);
        if (auto* x = g("title")) h.title = U2W(x->str);
        if (auto* x = g("ts"))    h.ts = (long long)x->num;
        out.push_back(h);
    }
    return out;
}

void CheckinStore::PushHistory(const std::wstring& kind, const std::wstring& id, const std::wstring& title)
{
    auto list = LoadHistory();
    // 去重：同 id 移到最前
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].kind == kind && list[i].id == id) { list.erase(list.begin() + i); break; }
    HistoryItem h;
    h.kind = kind; h.id = id; h.title = title; h.ts = (long long)time(nullptr);
    list.insert(list.begin(), h);
    if (list.size() > 60) list.resize(60);   // 最多保留 60 条
    std::string out = "[\n";
    for (size_t i = 0; i < list.size(); ++i) {
        const auto& f = list[i];
        out += "  {\n";
        out += "    \"kind\": "  + JQuote(W2U(f.kind))  + ",\n";
        out += "    \"id\": "    + JQuote(W2U(f.id))    + ",\n";
        out += "    \"title\": " + JQuote(W2U(f.title)) + ",\n";
        out += "    \"ts\": " + std::to_string(f.ts) + "\n";
        out += (i + 1 < list.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";
    WriteFileRaw(HistoryFilePath(), out);
    Cloud::Instance().MarkDirty();
}

// ============================================================
//  每日复盘（journal.json）
// ============================================================
std::wstring CheckinStore::JournalFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"journal.json";
}

std::vector<std::pair<std::wstring, DayJournal>> CheckinStore::LoadJournals()
{
    std::vector<std::pair<std::wstring, DayJournal>> out;
    std::string buf;
    if (!ReadFileRaw(JournalFilePath(), buf)) return out;
    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Obj) return out;
    for (auto& kv : v.obj) {              // std::map 已按键升序 = 日期升序
        DayJournal j;
        if (auto* s = JGet(kv.second, "summary")) j.summary = U2W(s->str);
        if (auto* n = JGet(kv.second, "next"))    j.next    = U2W(n->str);
        if (j.Empty()) continue;
        out.emplace_back(U2W(kv.first), j);
    }
    return out;
}

DayJournal CheckinStore::LoadJournal(const std::wstring& dateKey)
{
    for (auto& kv : LoadJournals())
        if (kv.first == dateKey) return kv.second;
    return DayJournal{};
}

void CheckinStore::SaveJournal(const std::wstring& dateKey, const DayJournal& j)
{
    auto all = LoadJournals();
    bool hit = false;
    for (auto& kv : all)
        if (kv.first == dateKey) { kv.second = j; hit = true; break; }
    if (!hit && !j.Empty()) all.emplace_back(dateKey, j);

    std::string out = "{\n";
    bool first = true;
    for (auto& kv : all) {
        if (kv.second.Empty()) continue;   // 清空即删除该日条目
        if (!first) out += ",\n";
        first = false;
        out += "  " + JQuote(W2U(kv.first)) + ": {";
        out += "\"summary\": " + JQuote(W2U(kv.second.summary)) + ", ";
        out += "\"next\": "    + JQuote(W2U(kv.second.next)) + "}";
    }
    out += "\n}\n";
    WriteFileRaw(JournalFilePath(), out);
    Cloud::Instance().MarkDirty();   // 写盘即打脏：后台线程防抖后静默上推
}

// ============================================================
//  作息记录（rhythm.json）
// ============================================================
std::wstring CheckinStore::RhythmFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"rhythm.json";
}

std::wstring CheckinStore::MilestonesFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"milestones.json";
}

std::map<std::wstring, DayRhythm> CheckinStore::LoadRhythms()
{
    std::map<std::wstring, DayRhythm> out;
    std::string buf;
    if (!ReadFileRaw(RhythmFilePath(), buf)) return out;
    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Obj) return out;
    for (auto& kv : v.obj) {
        DayRhythm r;
        if (auto* w = JGet(kv.second, "wake"))  r.wake  = (int)w->num;
        if (auto* s = JGet(kv.second, "sleep")) r.sleep = (int)s->num;
        if (r.Empty()) continue;
        out[U2W(kv.first)] = r;
    }
    return out;
}

DayRhythm CheckinStore::LoadRhythm(const std::wstring& dateKey)
{
    auto all = LoadRhythms();
    auto it = all.find(dateKey);
    return it == all.end() ? DayRhythm{} : it->second;
}

void CheckinStore::MarkRhythm(const std::wstring& dateKey, bool sleepSide, int minuteOfDay)
{
    if (minuteOfDay < 0 || minuteOfDay > 1439) return;
    auto all = LoadRhythms();
    DayRhythm& r = all[dateKey];
    if (sleepSide) r.sleep = minuteOfDay; else r.wake = minuteOfDay;

    std::string out = "{\n";
    bool first = true;
    for (auto& kv : all) {
        if (kv.second.Empty()) continue;
        if (!first) out += ",\n";
        first = false;
        out += "  " + JQuote(W2U(kv.first)) + ": {";
        out += "\"wake\": "  + std::to_string(kv.second.wake) + ", ";
        out += "\"sleep\": " + std::to_string(kv.second.sleep) + "}";
    }
    out += "\n}\n";
    WriteFileRaw(RhythmFilePath(), out);
    Cloud::Instance().MarkDirty();   // 写盘即打脏：后台线程防抖后静默上推
}

// ============================================================
//  备份 / 云同步：整账户载荷
//  ——「导出到文件」与「上推到服务端」是同一份载荷，只是落点不同；
//    「从文件恢复」与「从服务端拉取」同理。因此统一走 BuildPayload /
//    ApplyPayload，磁盘备份与云同步不会出现两套 schema。
// ============================================================
namespace {
struct Blk { const char* key; const wchar_t* file; const char* empty; };
const Blk kBlocks[] = {
    { "checkin",  L"checkin.json",  "{}" },
    { "focus",    L"focus.json",    "[]" },
    { "items",    L"items.json",    "{}" },
    { "journal",  L"journal.json",  "{}" },
    { "rhythm",   L"rhythm.json",   "{}" },
    { "columns",  L"columns.json",  "[]" },
    { "milestones", L"milestones.json", "[]" },
    { "favorites", L"favorites.json", "[]" },
    { "history",  L"history.json",  "[]" },
    { "settings", L"settings.json", "{}" },
};
} // namespace

std::string CheckinStore::BuildPayload(bool includeEmpty)
{
    const std::wstring root = AccountStore::Instance().CurrentRoot();
    // 返回块的 JSON 原文；本地无此块时返回空串（由调用方决定补空还是整块跳过）
    auto raw = [&](const wchar_t* name) -> std::string {
        std::string b;
        if (!ReadFileRaw(root + name, b) || b.empty()) return {};
        // 校验：读到的必须是合法 JSON，否则当作缺失，避免污染备份 / 污染云端
        Parser p(b.data(), b.size());
        JVal v = p.parse();
        if (v.type == JVal::Null) return {};
        return JDump(v);
    };

    std::vector<std::pair<std::string, std::string>> fields;
    for (const auto& b : kBlocks) {
        std::string body = raw(b.file);
        if (body.empty()) {
            if (!includeEmpty) continue;
            body = b.empty;
        }
        fields.emplace_back(b.key, body);
    }

    std::string out = "{\n";
    out += "  \"app\": \"FLORI\",\n";
    out += "  \"version\": 1,\n";
    out += "  \"account\": " + JQuote(W2U(AccountStore::Instance().CurrentName())) + ",\n";
    out += "  \"exportedAt\": " + std::to_string((long long)time(nullptr));
    for (const auto& f : fields) out += ",\n  " + JQuote(f.first) + ": " + f.second;
    out += "\n}\n";
    return out;
}

bool CheckinStore::ApplyPayload(const std::string& buf, std::wstring& err, bool* changed)
{
    if (changed) *changed = false;

    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Obj) { err = L"格式错误：不是有效的数据载荷。"; return false; }
    const JVal* app = JGet(v, "app");
    if (!app || app->type != JVal::Str || app->str != "FLORI") {
        err = L"格式错误：缺少 Flori 标识。"; return false;
    }

    // 本地文件是缩进过的，载荷是紧凑的，直接比字节必然次次「不同」。
    // 所以两边都先解析再按同一规则序列化，比的是内容不是排版。
    auto normalized = [](const std::string& raw) -> std::string {
        if (raw.empty()) return {};
        Parser p(raw.data(), raw.size());
        JVal j = p.parse();
        if (j.type == JVal::Null) return {};
        return JDump(j);
    };

    const std::wstring root = AccountStore::Instance().CurrentRoot();
    int matched = 0, touched = 0;
    for (const auto& b : kBlocks) {
        const JVal* n = JGet(v, b.key);
        if (!n || (n->type != JVal::Obj && n->type != JVal::Arr)) continue;
        ++matched;

        const std::string incoming = JDump(*n);
        std::string existing;
        ReadFileRaw(root + b.file, existing);
        if (normalized(existing) == incoming) continue;   // 内容一致，不动磁盘

        if (WriteFileRaw(root + b.file, incoming + "\n")) ++touched;
    }
    if (matched == 0) { err = L"载荷内没有可恢复的数据块。"; return false; }

    if (touched > 0) {
        Reload();      // 丢掉内存态，下次读取重新落地
        if (changed) *changed = true;
    }
    return true;
}

bool CheckinStore::ExportAll(const std::wstring& path, std::wstring& err)
{
    if (!WriteFileRaw(path, BuildPayload())) { err = L"写入失败：目标路径不可写。"; return false; }
    return true;
}

// ---------------- 专栏（每账户私有、可编辑）----------------
namespace {
// 生成稳定且足够唯一的专栏 id（时间戳 + 自增计数，避免同一秒编辑碰撞）
std::wstring GenColumnId()
{
    static long long s_seq = 0;
    long long t = (long long)time(nullptr);
    return L"col_" + std::to_wstring(t) + L"_" + std::to_wstring(++s_seq);
}
} // namespace

std::wstring CheckinStore::ColumnsFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"columns.json";
}

std::vector<Column> CheckinStore::LoadColumns()
{
    std::wstring root = AccountStore::Instance().CurrentRoot();
    if (m_colsLoaded && m_colsRoot == root) return m_columns;
    m_colsRoot = root;
    m_colsLoaded = true;
    m_columns.clear();

    std::wstring fp = ColumnsFilePath();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (f) {
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            std::string buf; buf.resize((size_t)sz);
            size_t rd = fread(&buf[0], 1, (size_t)sz, f);
            fclose(f);
            if (rd == (size_t)sz) {
                Parser parser(buf.data(), buf.size());
                JVal v = parser.parse();
                if (v.type == JVal::Arr) {
                    for (auto& el : v.arr) {
                        if (el.type != JVal::Obj) continue;
                        Column c;
                        auto g = [&](const char* k) -> const JVal* {
                            auto it = el.obj.find(k);
                            return it == el.obj.end() ? nullptr : &it->second;
                        };
                        if (auto* x = g("id"))         c.id       = U2W(x->str);
                        if (auto* x = g("title"))      c.title    = U2W(x->str);
                        if (auto* x = g("body"))       c.body     = U2W(x->str);
                        if (auto* x = g("bodyRtf"))    c.bodyRtf  = x->str;
                        if (auto* x = g("author"))     c.author   = U2W(x->str);
                        if (auto* x = g("createdAt"))  c.createdAt = (long long)x->num;
                        if (auto* x = g("updatedAt"))  c.updatedAt = (long long)x->num;
                        if (c.id.empty()) c.id = GenColumnId();
                        m_columns.push_back(c);
                    }
                    return m_columns;
                }
            }
        } else {
            fclose(f);
        }
    }

    // 无存档：仅当存在「总线路图」种子内容时才为当前账户播种一条，便于立即编辑 / 同步。
    // 开源仓库默认不含任何个人种子（BuildDefaultContent 为空壳），因此不会播种 ——
    // 演示账户与全新安装 / 访客态一致，从空白开始；测试时取回 private/seed_personal.cpp
    // 中的个人种子后，本分支会自动生效。
    const Roadmap& seedRoadmap = Content::Get().roadmap;
    if (AccountStore::Instance().IsDemoCurrent() && !seedRoadmap.principles.empty()) {
        Column seed;
        seed.id       = GenColumnId();
        seed.title    = L"我的总线路图";
        seed.body     = RoadmapSeedBody();
        seed.author   = AccountStore::Instance().CurrentName();
        long long now = (long long)time(nullptr);
        seed.createdAt = now;
        seed.updatedAt = now;
        m_columns.push_back(seed);
        SaveColumns(m_columns);   // 落盘，使后续编辑 / 云端同步都基于这条种子
    }
    return m_columns;
}

void CheckinStore::SaveColumns(const std::vector<Column>& cols)
{
    m_columns = cols;   // 内存同步，避免下次 LoadColumns 又读盘
    std::string out = "[\n";
    for (size_t i = 0; i < cols.size(); ++i) {
        const auto& c = cols[i];
        out += "  {\n";
        out += "    \"id\": "        + JQuote(W2U(c.id))     + ",\n";
        out += "    \"title\": "     + JQuote(W2U(c.title))  + ",\n";
        out += "    \"body\": "      + JQuote(W2U(c.body))   + ",\n";
        out += "    \"bodyRtf\": "   + JQuote(c.bodyRtf)     + ",\n";
        out += "    \"author\": "    + JQuote(W2U(c.author)) + ",\n";
        out += "    \"createdAt\": " + std::to_string(c.createdAt) + ",\n";
        out += "    \"updatedAt\": " + std::to_string(c.updatedAt) + "\n";
        out += (i + 1 < cols.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";
    std::wstring fp = ColumnsFilePath();
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    Cloud::Instance().MarkDirty();   // 写盘即打脏：后台线程防抖后静默上推
}

// ---------------- 知识库（跨窗口拖拽收集的考点）----------------
static std::wstring GenKnowledgeId()
{
    static long long s_seq = 0;
    long long t = (long long)time(nullptr);
    return L"kc_" + std::to_wstring(t) + L"_" + std::to_wstring(++s_seq);
}

std::wstring CheckinStore::KnowledgeFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"knowledge.json";
}

std::vector<KCard> CheckinStore::LoadKnowledge()
{
    std::vector<KCard> out;
    std::wstring fp = KnowledgeFilePath();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (f) {
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            std::string buf; buf.resize((size_t)sz);
            size_t rd = fread(&buf[0], 1, (size_t)sz, f);
            fclose(f);
            if (rd == (size_t)sz) {
                Parser parser(buf.data(), buf.size());
                JVal v = parser.parse();
                if (v.type == JVal::Arr) {
                    for (auto& el : v.arr) {
                        if (el.type != JVal::Obj) continue;
                        KCard c;
                        auto g = [&](const char* k) -> const JVal* {
                            auto it = el.obj.find(k);
                            return it == el.obj.end() ? nullptr : &it->second;
                        };
                        if (auto* x = g("id"))     c.id     = U2W(x->str);
                        if (auto* x = g("title"))  c.title  = U2W(x->str);
                        if (auto* x = g("body"))   c.body   = U2W(x->str);
                        if (auto* x = g("source")) c.source = U2W(x->str);
                        if (auto* x = g("tags"))   c.tags   = U2W(x->str);
                        if (auto* x = g("ts"))     c.ts     = (long long)x->num;
                        if (c.id.empty()) c.id = GenKnowledgeId();
                        out.push_back(c);
                    }
                }
            }
        } else fclose(f);
    }
    return out;
}

void CheckinStore::SaveKnowledge(const std::vector<KCard>& cards)
{
    std::string out = "[\n";
    for (size_t i = 0; i < cards.size(); ++i) {
        const auto& c = cards[i];
        out += "  {\n";
        out += "    \"id\": "     + JQuote(W2U(c.id))     + ",\n";
        out += "    \"title\": "  + JQuote(W2U(c.title))  + ",\n";
        out += "    \"body\": "   + JQuote(W2U(c.body))   + ",\n";
        out += "    \"source\": " + JQuote(W2U(c.source)) + ",\n";
        out += "    \"tags\": "   + JQuote(W2U(c.tags))   + ",\n";
        out += "    \"ts\": "     + std::to_string(c.ts)  + "\n";
        out += (i + 1 < cards.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";
    std::wstring fp = KnowledgeFilePath();
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    Cloud::Instance().MarkDirty();
}

void CheckinStore::AddKnowledge(const KCard& c)
{
    auto v = LoadKnowledge();
    v.push_back(c);
    SaveKnowledge(v);
}

void CheckinStore::RemoveKnowledge(const std::wstring& id)
{
    auto v = LoadKnowledge();
    for (size_t i = 0; i < v.size(); ) {
        if (v[i].id == id) v.erase(v.begin() + i);
        else ++i;
    }
    SaveKnowledge(v);
}

// ---------------- 考场模式（模考报告）----------------
std::wstring CheckinStore::ExamReportsFilePath() const
{
    return AccountStore::Instance().CurrentRoot() + L"exam_reports.json";
}

std::vector<ExamReport> CheckinStore::LoadExamReports()
{
    std::vector<ExamReport> out;
    std::wstring fp = ExamReportsFilePath();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (f) {
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            std::string buf; buf.resize((size_t)sz);
            size_t rd = fread(&buf[0], 1, (size_t)sz, f);
            fclose(f);
            if (rd == (size_t)sz) {
                Parser parser(buf.data(), buf.size());
                JVal v = parser.parse();
                if (v.type == JVal::Arr) {
                    for (auto& el : v.arr) {
                        if (el.type != JVal::Obj) continue;
                        ExamReport r;
                        auto g = [&](const char* k) -> const JVal* {
                            auto it = el.obj.find(k);
                            return it == el.obj.end() ? nullptr : &it->second;
                        };
                        if (auto* x = g("startTime"))    r.startTime    = (long long)x->num;
                        if (auto* x = g("plannedMin"))   r.plannedMin   = (int)x->num;
                        if (auto* x = g("actualSec"))    r.actualSec    = (int)x->num;
                        if (auto* x = g("effectiveSec")) r.effectiveSec = (int)x->num;
                        if (auto* x = g("interrupts"))   r.interrupts   = (int)x->num;
                        if (auto* x = g("abandoned"))    r.abandoned    = (x->num != 0);
                        out.push_back(r);
                    }
                }
            }
        } else fclose(f);
    }
    return out;
}

void CheckinStore::SaveExamReports(const std::vector<ExamReport>& v)
{
    std::string out = "[\n";
    for (size_t i = 0; i < v.size(); ++i) {
        const auto& r = v[i];
        out += "  {\n";
        out += "    \"startTime\": "    + std::to_string(r.startTime)    + ",\n";
        out += "    \"plannedMin\": "   + std::to_string(r.plannedMin)   + ",\n";
        out += "    \"actualSec\": "    + std::to_string(r.actualSec)    + ",\n";
        out += "    \"effectiveSec\": " + std::to_string(r.effectiveSec) + ",\n";
        out += "    \"interrupts\": "   + std::to_string(r.interrupts)   + ",\n";
        out += "    \"abandoned\": "    + std::string(r.abandoned ? "1" : "0") + "\n";
        out += (i + 1 < v.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";
    std::wstring fp = ExamReportsFilePath();
    FILE* f = _wfopen(fp.c_str(), L"wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    Cloud::Instance().MarkDirty();
}

void CheckinStore::AddExamReport(const ExamReport& r)
{
    auto v = LoadExamReports();
    v.push_back(r);
    if (v.size() > 50) v.erase(v.begin(), v.begin() + (v.size() - 50));
    SaveExamReports(v);
}

ExamReport CheckinStore::LastExamReport()
{
    auto v = LoadExamReports();
    if (v.empty()) return ExamReport{};
    return v.back();
}

void CheckinStore::ReloadColumns()
{
    m_colsLoaded = false;
    m_columns.clear();
    m_colsRoot.clear();
}

bool CheckinStore::ImportAll(const std::wstring& path, std::wstring& err)
{
    std::string buf;
    if (!ReadFileRaw(path, buf)) { err = L"读取失败：文件不存在或为空。"; return false; }
    if (!ApplyPayload(buf, err)) return false;
    // 本地被整体替换，云端也要跟上，否则下次拉取又被旧数据盖回来
    Cloud::Instance().MarkDirty();
    return true;
}

bool CheckinStore::PeekBackup(const std::wstring& path, BackupInfo& info, std::wstring& err)
{
    std::string buf;
    if (!ReadFileRaw(path, buf)) { err = L"读取失败：文件不存在或为空。"; return false; }

    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Obj) { err = L"格式错误：不是有效的数据载荷。"; return false; }
    const JVal* app = JGet(v, "app");
    if (!app || app->type != JVal::Str || app->str != "FLORI") {
        err = L"格式错误：缺少 Flori 标识。"; return false;
    }

    if (const JVal* a = JGet(v, "account"); a && a->type == JVal::Str) info.account = U2W(a->str);
    if (const JVal* t = JGet(v, "exportedAt"); t && t->type == JVal::Num) info.exportedAt = (long long)t->num;

    info.blocks = 0;
    for (const auto& b : kBlocks) {
        const JVal* n = JGet(v, b.key);
        if (n && (n->type == JVal::Obj || n->type == JVal::Arr)) ++info.blocks;
    }
    if (info.blocks == 0) { err = L"载荷内没有可恢复的数据块。"; return false; }
    return true;
}

std::wstring CheckinStore::SaveRollbackSnapshot()
{
    const std::wstring root = AccountStore::Instance().CurrentRoot();
    if (root.empty()) return {};

    // 文件名带时间戳，多次导入不互相覆盖：rollback-20260811-0930.flori.json
    wchar_t stamp[32] = { 0 };
    time_t now = time(nullptr);
    tm tmv{};
    localtime_s(&tmv, &now);
    wcsftime(stamp, 32, L"%Y%m%d-%H%M%S", &tmv);

    std::wstring path = root + L"rollback-" + stamp + L".flori.json";
    if (!WriteFileRaw(path, BuildPayload())) return {};
    return path;
}

} // namespace lj
