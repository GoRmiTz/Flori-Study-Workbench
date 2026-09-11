// ============================================================
//  QuizStore.cpp — 练考模块 Phase 4：成绩落盘 + 薄弱知识点聚合
//  零依赖（仅 app/Json.h）；UTF-8 往返；每账户隔离。
// ============================================================
#include "quiz/QuizStore.h"
#include "app/Json.h"
#include <map>
#include <algorithm>

namespace lj {
namespace quiz {

// ---------------- UTF-8 往返（wstring ↔ string） ----------------
static std::string W2U(const std::wstring& s)
{
    std::string out; out.reserve(s.size() * 2);
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned int c = (unsigned int)s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size()
            && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            ++i;
        }
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) {
            out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 0x3F));
            out += (char)(0x80 | (c & 0x3F));
        } else {
            out += (char)(0xF0 | (c >> 18)); out += (char)(0x80 | ((c >> 12) & 0x3F));
            out += (char)(0x80 | ((c >> 6) & 0x3F)); out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

static std::wstring U2W(const std::string& s)
{
    std::wstring out; size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out += (wchar_t)c; ++i; }
        else if ((c >> 5) == 0x06) {
            if (i + 1 >= s.size()) break;
            out += (wchar_t)(((c & 0x1F) << 6) | (s[i + 1] & 0x3F)); i += 2;
        }
        else if ((c >> 4) == 0x0E) {
            if (i + 2 >= s.size()) break;
            int cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
            i += 3;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < s.size()
                && (s[i] & 0xF0) == 0xF0) {
                int lo = ((s[i] & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12)
                       | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
                i += 4;
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else cp = 0xFFFD;
            }
            if (cp <= 0xFFFF) out += (wchar_t)cp;
            else { cp -= 0x10000; out += (wchar_t)(0xD800 + (cp >> 10)); out += (wchar_t)(0xDC00 + (cp & 0x3FF)); }
        }
        else if ((c >> 3) == 0x1E) {
            if (i + 3 >= s.size()) break;
            int cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
            i += 4;
            if (cp > 0x10FFFF) cp = 0xFFFD;
            if (cp <= 0xFFFF) out += (wchar_t)cp;
            else { cp -= 0x10000; out += (wchar_t)(0xD800 + (cp >> 10)); out += (wchar_t)(0xDC00 + (cp & 0x3FF)); }
        }
        else { out += L'?'; ++i; }
    }
    return out;
}

// ---------------- JVal 构造小助手 ----------------
static lj::json::JVal JNum(double d) { lj::json::JVal v; v.type = lj::json::JVal::Num; v.num = d; return v; }
static lj::json::JVal JStr(const std::string& s) { lj::json::JVal v; v.type = lj::json::JVal::Str; v.str = s; return v; }
static lj::json::JVal JBool(bool b) { lj::json::JVal v; v.type = lj::json::JVal::Bool; v.bval = b; return v; }

static lj::json::JVal BuildAttemptVal(const QuizAttempt& a)
{
    using namespace lj::json;
    JVal v; v.type = JVal::Obj;
    v.obj["ts"] = JNum((double)a.ts);
    v.obj["date"] = JStr(W2U(a.dateKey));
    v.obj["title"] = JStr(W2U(a.title));
    v.obj["category"] = JStr(W2U(a.category));
    v.obj["mode"] = JStr(W2U(a.mode));
    v.obj["total"] = JNum(a.total);
    v.obj["correct"] = JNum(a.correct);
    v.obj["wrong"] = JNum(a.wrong);
    v.obj["unanswered"] = JNum(a.unanswered);
    v.obj["accuracy"] = JNum((double)a.accuracy);
    v.obj["plannedSec"] = JNum(a.plannedSec);
    v.obj["usedSec"] = JNum(a.usedSec);
    v.obj["timeout"] = JBool(a.timeout);
    JVal arr; arr.type = JVal::Arr;
    for (auto& w : a.weakPoints) arr.arr.push_back(JStr(W2U(w)));
    v.obj["weak"] = arr;
    return v;
}

static int JInt(const lj::json::JVal& o, const char* k, int def)
{
    const lj::json::JVal* v = lj::json::JGet(o, k);
    return (v && v->type == lj::json::JVal::Num) ? (int)v->num : def;
}

// ---------------- QuizStore ----------------
QuizStore& QuizStore::Instance()
{
    static QuizStore s;
    return s;
}

bool QuizStore::RecordAttempt(const QuizAttempt& a)
{
    std::vector<QuizAttempt> all = LoadAll();
    all.push_back(a);

    using namespace lj::json;
    JVal root; root.type = JVal::Obj;
    JVal arr; arr.type = JVal::Arr;
    for (auto& x : all) arr.arr.push_back(BuildAttemptVal(x));
    root.obj["attempts"] = arr;

    std::string buf = JDump(root);
    return WriteFileRaw(Path(), buf);
}

std::vector<QuizAttempt> QuizStore::LoadAll() const
{
    using namespace lj::json;
    std::vector<QuizAttempt> out;
    std::string buf;
    if (!ReadFileRaw(Path(), buf) || buf.empty()) return out;
    JVal root = Parser(buf.data(), buf.size()).parse();
    const JVal* arr = JGet(root, "attempts");
    if (!arr || arr->type != JVal::Arr) return out;
    for (auto& e : arr->arr) {
        if (e.type != JVal::Obj) continue;
        QuizAttempt a;
        const JVal* ts = JGet(e, "ts");
        a.ts = ts ? (long long)ts->num : 0;
        a.dateKey = U2W(JStr(e, "date"));
        a.title = U2W(JStr(e, "title"));
        a.category = U2W(JStr(e, "category"));
        a.mode = U2W(JStr(e, "mode"));
        a.total = JInt(e, "total", 0);
        a.correct = JInt(e, "correct", 0);
        a.wrong = JInt(e, "wrong", 0);
        a.unanswered = JInt(e, "unanswered", 0);
        a.accuracy = (float)(JInt(e, "accuracy", 0));
        a.plannedSec = JInt(e, "plannedSec", 0);
        a.usedSec = JInt(e, "usedSec", 0);
        const JVal* to = JGet(e, "timeout");
        a.timeout = (to && to->type == JVal::Bool) ? to->bval : false;
        const JVal* w = JGet(e, "weak");
        if (w && w->type == JVal::Arr)
            for (auto& we : w->arr) if (we.type == JVal::Str) a.weakPoints.push_back(U2W(we.str));
        out.push_back(a);
    }
    return out;
}

std::vector<std::pair<std::wstring, int>> QuizStore::TopWeakPoints(int n) const
{
    std::map<std::wstring, int> cnt;
    for (auto& a : LoadAll())
        for (auto& w : a.weakPoints) if (!w.empty()) cnt[w]++;
    std::vector<std::pair<std::wstring, int>> v(cnt.begin(), cnt.end());
    std::sort(v.begin(), v.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
    if (n > 0 && (int)v.size() > n) v.resize(n);
    return v;
}

bool QuizStore::SaveSettings(const QuizSettings& s)
{
    using namespace lj::json;
    JVal v; v.type = JVal::Obj;
    v.obj["enabled"] = JBool(s.enabled);
    v.obj["hour"] = JNum(s.hour);
    v.obj["minute"] = JNum(s.minute);
    v.obj["category"] = JStr(W2U(s.category));
    v.obj["qcount"] = JNum(s.qcount);
    v.obj["rss"] = JBool(s.rss);
    return WriteFileRaw(SetPath(), JDump(v));
}

QuizSettings QuizStore::LoadSettings() const
{
    using namespace lj::json;
    QuizSettings s;
    std::string buf;
    if (!ReadFileRaw(SetPath(), buf) || buf.empty()) return s;
    JVal v = Parser(buf.data(), buf.size()).parse();
    const JVal* e = JGet(v, "enabled");
    if (e && e->type == JVal::Bool) s.enabled = e->bval;
    s.hour = JInt(v, "hour", s.hour);
    s.minute = JInt(v, "minute", s.minute);
    std::wstring cat = U2W(JStr(v, "category"));
    if (!cat.empty()) s.category = cat;
    int q = JInt(v, "qcount", s.qcount); if (q > 0) s.qcount = q;
    const JVal* r = JGet(v, "rss");
    if (r && r->type == JVal::Bool) s.rss = r->bval;
    return s;
}

} // namespace quiz
} // namespace lj
