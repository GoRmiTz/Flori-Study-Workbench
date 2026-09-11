// ============================================================
//  QuizGen.cpp — 练考模块 Phase 2-1：AI 出题生成器
//  严格遵循 docs/quiz-module/题目MD规范文档.md §2–§7。
//  零第三方依赖：winhttp + 自含 UTF-8 编解码 + lj::json 响应解析。
// ============================================================
#include "quiz/QuizGen.h"
#include "quiz/QuizParser.h"
#include "app/Json.h"
#include <windows.h>
#include <winhttp.h>
#include <ctime>
#include <cstdlib>

#pragma comment(lib, "winhttp.lib")

namespace lj {
namespace quiz {

// ---------------- 字符串工具 ----------------
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
static std::string JsonEscape(const std::string& in)
{
    std::string o;
    for (char c : in) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            default:   o += c;
        }
    }
    return o;
}

std::wstring QuizTodayISO()
{
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[16];
    swprintf_s(buf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return buf;
}

// ---------------- URL / HTTP ----------------
struct UrlParts { bool secure=false; std::string host; int port=443; std::string path="/"; };
static bool ParseUrl(const std::wstring& url, UrlParts& out)
{
    std::string u = W2U(url);
    size_t p = u.find("://");
    if (p == std::string::npos) return false;
    std::string rest = u.substr(p + 3);
    out.secure = (u.substr(0, p) == "https");
    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out.path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    size_t colon = hostport.find(':');
    if (colon == std::string::npos) { out.host = hostport; out.port = out.secure ? 443 : 80; }
    else { out.host = hostport.substr(0, colon); out.port = atoi(hostport.substr(colon+1).c_str()); }
    return !out.host.empty();
}

// 同步 POST JSON；返回 (body, ok, tokens)
static bool HttpPostJson(const UrlParts& url, const std::wstring& apiKey,
                        const std::string& bodyUtf8, std::string& outBody, int& outTokens)
{
    outTokens = 0;
    std::wstring whost = U2W(url.host);
    HINTERNET hSess = WinHttpOpen(L"LJQuiz/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return false;
    // 推理模型思考较慢，但必须兜底失败而非永久挂起
    int timeoutMs = 120000;  // 连接/发送/接收各 120s
    WinHttpSetOption(hSess, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hSess, WINHTTP_OPTION_SEND_TIMEOUT,    &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hSess, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    HINTERNET hConn = WinHttpConnect(hSess, whost.c_str(), (INTERNET_PORT)url.port, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return false; }

    std::wstring wpath = U2W(url.path) + L"/chat/completions";
    DWORD flags = url.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", wpath.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return false; }

    std::string hdr = "Content-Type: application/json; charset=utf-8\r\n";
    if (!apiKey.empty()) hdr += "Authorization: Bearer " + W2U(apiKey) + "\r\n";
    std::wstring whdr = U2W(hdr);

    bool ok = false;
    if (WinHttpSendRequest(hReq, whdr.c_str(), (DWORD)whdr.size(),
                           (LPVOID)bodyUtf8.data(), (DWORD)bodyUtf8.size(), (DWORD)bodyUtf8.size(), 0)) {
        if (WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD avail = 0;
            std::string buf;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                size_t old = buf.size();
                buf.resize(old + avail);
                DWORD rd = 0;
                if (!WinHttpReadData(hReq, &buf[old], avail, &rd) || rd == 0) { buf.resize(old); break; }
                buf.resize(old + rd);
            }
            outBody = std::move(buf);
            ok = true;
            using namespace lj::json;
            Parser parser(outBody.data(), outBody.size());
            JVal v = parser.parse();
            if (auto* u = JGet(v, "usage")) {
                if (auto* t = JGet(*u, "total_tokens")) outTokens = (int)t->num;
            }
        }
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return ok;
}

// ---------------- 去除代码围栏（模型常见 ```markdown 包裹） ----------------
static std::wstring StripCodeFence(const std::wstring& md)
{
    std::wstring s = md;
    // 去首尾空白
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) a++;
    while (b > a && (s[b-1] == L' ' || s[b-1] == L'\t' || s[b-1] == L'\r' || s[b-1] == L'\n')) b--;
    s = s.substr(a, b - a);
    // 开头 ``` 或 ```markdown / ```md
    if (s.size() >= 3 && s[0] == L'`' && s[1] == L'`' && s[2] == L'`') {
        size_t i = 3;
        while (i < s.size() && s[i] != L'\n') i++;          // 跳过语言标识
        if (i < s.size()) i++;                               // 跳过换行
        s = s.substr(i);
    }
    // 结尾 ```
    if (s.size() >= 3 && s[s.size()-3] == L'`' && s[s.size()-2] == L'`' && s[s.size()-1] == L'`') {
        s = s.substr(0, s.size() - 3);
    }
    return s;
}

// ---------------- 提示词模板（规范 §7） ----------------
static std::wstring BuildSystemPrompt()
{
    return
        L"你是一位考公（公务员/选调生考试）出题专家。请依据用户提供的「日期」与「近期时政摘要」，"
        L"生成一套练习卷，严格按下面的 Markdown 规范输出，不要输出规范以外的任何内容。\n\n"
        L"【输出格式规范】\n"
        L"- 文件以两个 --- 包裹的 YAML 元数据开头：title / date / category / mode / duration / source / model。\n"
        L"- 元数据后写 # 卷标题，可加一句 > 卷首说明。\n"
        L"- 每题以 ## Q<序号> 开头（序号从1递增）。\n"
        L"- 题干用 **题干**：开头一行。\n"
        L"- 选项每行为 \"- A. 文本\" / \"- B. 文本\" …… 字母 A–E 必须连续无跳号。\n"
        L"- 题型行：**题型**：单选 / 多选 / 判断。\n"
        L"- 答案行：**答案**：单选填 A，多选填 ABD，判断填 A 或 B。\n"
        L"- 解析行：**解析**：给出简明依据（引政策/事实）。\n"
        L"- 知识点行：**知识点**：如 中央一号文件。\n"
        L"- 难度行：**难度**：1–3。\n\n"
        L"【质量要求】\n"
        L"- 题干贴近行测常识/时政真题风格，选项为 plausible 干扰项（基于真实易错点，不要荒诞填充）。\n"
        L"- 每题必须有解析、知识点、难度。\n"
        L"- 判断题型选项固定 \"A. 正确\" / \"B. 错误\"。\n"
        L"- 仅使用给定日期前可确认的事实；不确定则不出。\n"
        L"- 输出纯 Markdown，不要 ``` 代码块包裹，不要任何开场白/结尾备注。";
}

static std::wstring BuildUserPrompt(const std::wstring& dateISO,
                                    const std::wstring& category,
                                    int qcount,
                                    const std::wstring& rssDigest)
{
    std::wstring u;
    u += L"日期：" + dateISO + L"\n";
    u += L"类别：" + category + L"\n";
    u += L"题量：" + std::to_wstring(qcount) + L"\n";
    u += L"近期时政摘要：（以下为可选 RSS 抓取内容，若为空则由你凭知识生成）\n";
    u += rssDigest.empty() ? L"（无外部摘要，请凭可靠知识生成）\n" : rssDigest;
    return u;
}

// ---------------- 主生成 ----------------
bool QuizGenerateDaily(const std::wstring& outDir,
                       const QuizAIConfig& cfg,
                       const std::wstring& dateISO,
                       const std::wstring& category,
                       int questionCount,
                       const std::wstring& rssDigest,
                       std::wstring& errOut)
{
    errOut.clear();
    if (!cfg.enabled || cfg.apiBase.empty() || cfg.apiKey.empty()) {
        errOut = L"未配置 AI 凭据（请在看板娘设置面板填写 API 地址与密钥）";
        return false;
    }
    int qcount = (questionCount < 1) ? 10 : (questionCount > 25 ? 25 : questionCount);

    UrlParts url;
    if (!ParseUrl(cfg.apiBase, url)) { errOut = L"API 地址解析失败"; return false; }

    std::string modelU = W2U(cfg.model.empty() ? L"deepseek-v4-flash" : cfg.model);
    std::wstring sys = BuildSystemPrompt();
    std::wstring usr = BuildUserPrompt(dateISO, category, qcount, rssDigest);

    std::string body = "{\"model\":\"" + modelU + "\",\"temperature\":0.5,\"max_tokens\":2400,\"messages\":["
                       "{\"role\":\"system\",\"content\":\"" + JsonEscape(W2U(sys)) + "\"},"
                       "{\"role\":\"user\",\"content\":\"" + JsonEscape(W2U(usr)) + "\"}]}";

    std::string out; int toks = 0;
    if (!HttpPostJson(url, cfg.apiKey, body, out, toks)) {
        errOut = L"网络请求失败（无法连接 API 或超时）";
        return false;
    }

    // 解析 choices[0].message.content
    std::wstring md;
    using namespace lj::json;
    Parser parser(out.data(), out.size());
    JVal v = parser.parse();
    if (auto* ch = JGet(v, "choices")) {
        if (ch->type == JVal::Arr && !ch->arr.empty()) {
            if (auto* msg = JGet(ch->arr[0], "message")) {
                if (auto* c = JGet(*msg, "content")) md = U2W(c->str);
            }
        }
    }
    if (md.empty()) {
        // 有些服务商把错误放在顶层 error 字段
        if (auto* e = JGet(v, "error")) {
            if (auto* m = JGet(*e, "message")) errOut = L"API 返回错误：" + U2W(m->str);
            else errOut = L"API 返回错误（无 content）";
        } else {
            errOut = L"API 响应中未找到题目内容";
        }
        return false;
    }

    // 自校验：先直接解析；失败则剥离代码围栏再试
    quiz::QuizDoc doc = quiz::ParseQuizText(md);
    if (doc.questions.empty()) {
        md = StripCodeFence(md);
        doc = quiz::ParseQuizText(md);
    }
    if (doc.questions.empty()) {
        errOut = L"AI 返回内容不符合《题目MD规范》，解析到 0 题（已尝试剥离代码围栏）";
        return false;
    }

    // 写盘：outDir/dateISO.md（UTF-8 无 BOM）
    CreateDirectoryW(outDir.c_str(), nullptr);
    std::wstring path = outDir + dateISO + L".md";
    std::string utf8;
    // 直接把模型返回的 Markdown 落盘（已是规范格式）
    int n = WideCharToMultiByte(CP_UTF8, 0, md.c_str(), (int)md.size(), nullptr, 0, nullptr, nullptr);
    utf8.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, md.c_str(), (int)md.size(), &utf8[0], n, nullptr, nullptr);

    if (!lj::json::WriteFileRaw(path, utf8)) {
        errOut = L"写盘失败：" + path;
        return false;
    }
    errOut = L"成功生成 " + std::to_wstring((int)doc.questions.size()) + L" 题 → " + path;
    return true;
}

} // namespace quiz
} // namespace lj
