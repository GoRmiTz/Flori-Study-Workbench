// ============================================================
//  QuizParser.cpp — 题目 MD 解析器（练考模块 Phase 0-2）
//  严格遵循 docs/quiz-module/题目MD规范文档.md §2–§6。
// ============================================================
#include "quiz/QuizParser.h"
#include <cstdio>
#include <cwchar>
#include <algorithm>

namespace lj {
namespace quiz {

// ---------------- UTF-8 解码（自含，不依赖 Win32） ----------------
static std::wstring Utf8ToWString(const char* p, size_t n)
{
    std::wstring out;
    out.reserve(n);
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x80) { out.push_back((wchar_t)c); i += 1; }
        else if ((c >> 5) == 0x06) {
            if (i + 1 >= n) break;
            unsigned int cp = ((c & 0x1F) << 6) | (p[i+1] & 0x3F);
            out.push_back((wchar_t)cp); i += 2;
        }
        else if ((c >> 4) == 0x0E) {
            if (i + 2 >= n) break;
            unsigned int cp = ((c & 0x0F) << 12) | ((p[i+1] & 0x3F) << 6) | (p[i+2] & 0x3F);
            out.push_back((wchar_t)cp); i += 3;
        }
        else if ((c >> 3) == 0x1E) {
            if (i + 3 >= n) break;
            unsigned int cp = ((c & 0x07) << 18) | ((p[i+1] & 0x3F) << 12) | ((p[i+2] & 0x3F) << 6) | (p[i+3] & 0x3F);
            if (cp > 0xFFFF) cp = 0xFFFD;
            out.push_back((wchar_t)cp); i += 4;
        }
        else { out.push_back(L'?'); i += 1; }
    }
    return out;
}

static bool ReadFileUtf8(const std::wstring& path, std::wstring& out)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    std::string buf;
    buf.resize((size_t)sz);
    size_t rd = fread(&buf[0], 1, (size_t)sz, f);
    fclose(f);
    buf.resize(rd);
    if (rd >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        out = Utf8ToWString(buf.data() + 3, rd - 3);
    else
        out = Utf8ToWString(buf.data(), rd);
    return true;
}

// ---------------- 工具 ----------------
static std::wstring Trim(const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r')) a++;
    while (b > a && (s[b-1] == L' ' || s[b-1] == L'\t' || s[b-1] == L'\r')) b--;
    return s.substr(a, b - a);
}

static std::wstring ValueAfter(const std::wstring& line, const std::wstring& kw)
{
    size_t pos = line.find(kw);
    if (pos == std::wstring::npos) return L"";
    std::wstring rest = line.substr(pos + kw.size());
    for (wchar_t d : { L'：', L':', L'*', L' ' }) {
        size_t p2 = rest.find(d);
        if (p2 != std::wstring::npos) { rest = rest.substr(p2 + 1); break; }
    }
    return Trim(rest);
}

static bool IsQuestionHeader(const std::wstring& line, int& id, std::wstring& rest)
{
    std::wstring t = Trim(line);
    if (t.size() < 3 || t[0] != L'#' || t[1] != L'#') return false;
    size_t i = 2;
    while (i < t.size() && (t[i] == L' ' || t[i] == L'\t')) i++;
    if (i < t.size() && (t[i] == L'Q' || t[i] == L'q')) i++;
    else if (i + 1 < t.size() && t[i] == L'第') i++;
    std::wstring num;
    while (i < t.size() && t[i] >= L'0' && t[i] <= L'9') { num.push_back(t[i]); i++; }
    if (num.empty()) return false;
    id = 0; for (wchar_t c : num) id = id * 10 + (c - L'0');
    while (i < t.size() && (t[i] == L'题' || t[i] == L'、' || t[i] == L'.' || t[i] == L' ' || t[i] == L'\t')) i++;
    rest = Trim(t.substr(i));
    return true;
}

static bool IsOptionLine(const std::wstring& line, std::wstring& key, std::wstring& text)
{
    std::wstring t = Trim(line);
    if (t.empty() || t[0] != L'-') return false;
    size_t i = 1;
    while (i < t.size() && (t[i] == L' ' || t[i] == L'\t')) i++;
    if (i >= t.size()) return false;
    wchar_t k = t[i];
    if (!((k >= L'A' && k <= L'E') || (k >= L'a' && k <= L'e'))) return false;
    key = (k <= L'Z') ? std::wstring(1, k) : std::wstring(1, (wchar_t)(k - L'a' + L'A'));
    i++;
    if (i >= t.size()) return false;
    if (t[i] != L'.' && t[i] != L')' && t[i] != L'、') return false;
    i++;
    while (i < t.size() && (t[i] == L' ' || t[i] == L'\t')) i++;
    text = Trim(t.substr(i));
    return true;
}

static std::vector<std::wstring> ExtractAnswerLetters(const std::wstring& v)
{
    std::vector<std::wstring> ans;
    std::wstring s = Trim(v);
    if (s.find(L"正确") != std::wstring::npos) { ans.push_back(L"A"); return ans; }
    if (s.find(L"错误") != std::wstring::npos) { ans.push_back(L"B"); return ans; }
    for (wchar_t c : s) {
        if (c >= L'A' && c <= L'E') ans.push_back(std::wstring(1, c));
        else if (c >= L'a' && c <= L'e') ans.push_back(std::wstring(1, (wchar_t)(c - L'a' + L'A')));
    }
    return ans;
}

static void NormalizeQuestion(QuizQuestion& q)
{
    std::sort(q.options.begin(), q.options.end(),
              [](const QuizOption& a, const QuizOption& b) { return a.key < b.key; });
    std::sort(q.answer.begin(), q.answer.end());
    q.answer.erase(std::unique(q.answer.begin(), q.answer.end()), q.answer.end());
    q.hasAnswer = !q.answer.empty();
}

// ---------------- 主解析 ----------------
QuizDoc ParseQuizText(const std::wstring& text)
{
    QuizDoc doc;
    std::vector<std::wstring> lines;
    std::wstring cur;
    for (wchar_t c : text) {
        if (c == L'\n') { lines.push_back(cur); cur.clear(); }
        else if (c != L'\r') cur.push_back(c);
    }
    if (!cur.empty()) lines.push_back(cur);

    int fmState = 0;          // 0 前 / 1 内 / 2 结束
    bool inExplain = false;
    QuizQuestion curQ; bool hasCur = false;

    auto Flush = [&]() {
        if (hasCur && !curQ.stem.empty() && !curQ.options.empty()) {
            NormalizeQuestion(curQ);
            doc.questions.push_back(curQ);
        }
        hasCur = false;
    };

    for (const std::wstring& raw : lines) {
        std::wstring line = Trim(raw);

        // Frontmatter
        if (line == L"---") {
            if (fmState == 0) { fmState = 1; continue; }
            else if (fmState == 1) { fmState = 2; continue; }
        }
        if (fmState == 1) {
            size_t cpos = line.find(L':');
            if (cpos != std::wstring::npos) {
                std::wstring k = Trim(line.substr(0, cpos));
                std::wstring v = Trim(line.substr(cpos + 1));
                if (!k.empty()) {
                    doc.meta[k] = v;
                    if (k == L"title" && doc.title.empty()) doc.title = v;
                }
            }
            continue;
        }

        // 卷标题（单个 #）
        if (line.size() >= 2 && line[0] == L'#' && line[1] != L'#' && line[1] == L' ') {
            if (doc.title.empty()) doc.title = Trim(line.substr(1));
            continue;
        }

        // 题目头
        int qid = 0; std::wstring rest;
        if (IsQuestionHeader(line, qid, rest)) {
            Flush();
            curQ = QuizQuestion(); curQ.id = qid;
            if (!rest.empty()) curQ.stem = rest;
            hasCur = true; inExplain = false;
            continue;
        }

        if (!hasCur) continue;   // 题目块外的杂行忽略

        // 选项
        std::wstring okey, otext;
        if (IsOptionLine(line, okey, otext)) { curQ.options.push_back({ okey, otext }); inExplain = false; continue; }

        // 字段行
        if (line.find(L"题型") != std::wstring::npos) { curQ.type = ValueAfter(line, L"题型"); inExplain = false; continue; }
        if (line.find(L"答案") != std::wstring::npos || line.find(L"正确选项") != std::wstring::npos) {
            std::wstring v = ValueAfter(line, L"答案");
            if (v.empty()) v = ValueAfter(line, L"正确选项");
            curQ.answer = ExtractAnswerLetters(v);
            inExplain = false; continue;
        }
        if (line.find(L"知识点") != std::wstring::npos) { curQ.point = ValueAfter(line, L"知识点"); inExplain = false; continue; }
        if (line.find(L"难度") != std::wstring::npos) {
            std::wstring v = ValueAfter(line, L"难度");
            if (!v.empty() && v[0] >= L'0' && v[0] <= L'9') curQ.difficulty = v[0] - L'0';
            inExplain = false; continue;
        }
        if (line.find(L"题干") != std::wstring::npos) { if (curQ.stem.empty()) curQ.stem = ValueAfter(line, L"题干"); inExplain = false; continue; }
        if (line.find(L"解析") != std::wstring::npos || line.find(L"详解") != std::wstring::npos || line.find(L"解答") != std::wstring::npos) {
            std::wstring v = ValueAfter(line, L"解析");
            if (v.empty()) v = ValueAfter(line, L"详解");
            if (v.empty()) v = ValueAfter(line, L"解答");
            if (!curQ.explain.empty()) curQ.explain += L"\n";
            curQ.explain += v;
            inExplain = true;
            continue;
        }

        // 续行（解析多行）
        if (inExplain && !line.empty()) { curQ.explain += (curQ.explain.empty() ? L"" : L"\n") + line; continue; }
    }
    Flush();
    return doc;
}

bool LoadQuiz(const std::wstring& path, QuizDoc& out)
{
    std::wstring text;
    if (!ReadFileUtf8(path, text)) return false;
    out = ParseQuizText(text);
    return true;
}

bool IsCorrect(const QuizQuestion& q, const std::vector<std::wstring>& selected)
{
    std::vector<std::wstring> a = q.answer, s = selected;
    std::sort(a.begin(), a.end());
    std::sort(s.begin(), s.end());
    a.erase(std::unique(a.begin(), a.end()), a.end());
    s.erase(std::unique(s.begin(), s.end()), s.end());
    return a == s;
}

QuizReport ComputeQuizReport(const QuizDoc& doc,
                             const std::vector<std::vector<std::wstring>>& sel,
                             int plannedSec, int usedSec, bool timeout)
{
    QuizReport r;
    r.plannedSec = plannedSec;
    r.usedSec = usedSec;
    r.timeout = timeout;
    r.total = (int)doc.questions.size();
    r.correctByQ.resize(r.total, false);
    for (size_t i = 0; i < doc.questions.size(); ++i) {
        const auto& q = doc.questions[i];
        bool answered = (i < sel.size()) && !sel[i].empty();
        bool ok = answered && quiz::IsCorrect(q, (i < sel.size()) ? sel[i] : std::vector<std::wstring>{});
        r.correctByQ[i] = ok;
        if (ok) ++r.correct;
        else if (answered) ++r.wrong;
        else ++r.unanswered;
    }
    r.accuracy = r.total > 0 ? (100.0f * (float)r.correct / (float)r.total) : 0.0f;
    return r;
}

} // namespace quiz
} // namespace lj
