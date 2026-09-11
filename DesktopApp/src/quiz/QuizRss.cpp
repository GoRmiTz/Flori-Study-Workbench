// ============================================================
//  QuizRss.cpp — 练考模块 Phase 2-4：RSS/Atom 时政抓取（增强项）
//  零依赖：winhttp GET + 极简 XML 标签抽取（RSS 2.0 <item>/<title>/<description>
//  与 Atom <entry>/<title>/<summary>）。任一源失败即跳过，绝不致命。
// ============================================================
#include "quiz/QuizRss.h"
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")

namespace lj {
namespace quiz {

// 默认源留空（以 nullptr 哨兵占位）：不开任何外部请求，生成器走模型知识保底。
// 启用方式：把可用 RSS/Atom 地址填入此数组（分号或换行分隔传入 QuizFetchRssDigest）。
const wchar_t* kQuizRssDefaultFeeds[] = {
    nullptr,   // L"https://example.com/rss/politics.xml",
    // L"https://example.com/atom/news.xml",
};
const int kQuizRssDefaultFeedCount = (int)(sizeof(kQuizRssDefaultFeeds) / sizeof(kQuizRssDefaultFeeds[0]));

// ---------------- 工具 ----------------
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
static std::wstring Trim(const std::wstring& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) a++;
    while (b > a && (s[b-1] == L' ' || s[b-1] == L'\t' || s[b-1] == L'\r' || s[b-1] == L'\n')) b--;
    return s.substr(a, b - a);
}
static std::wstring ToLower(const std::wstring& s)
{
    std::wstring o = s;
    for (auto& c : o) c = (wchar_t)towlower(c);
    return o;
}
// 解码基础 XML 实体
static std::wstring DecodeEntities(const std::wstring& in)
{
    std::wstring o; o.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == L'&') {
            size_t j = in.find(L';', i);
            if (j != std::wstring::npos && j - i <= 8) {
                std::wstring ent = in.substr(i + 1, j - i - 1);
                if (ent == L"amp") { o += L'&'; i = j; continue; }
                if (ent == L"lt")  { o += L'<'; i = j; continue; }
                if (ent == L"gt")  { o += L'>'; i = j; continue; }
                if (ent == L"quot"){ o += L'"'; i = j; continue; }
                if (ent == L"apos"){ o += L'\''; i = j; continue; }
                if (ent.size() > 1 && ent[0] == L'#') {
                    int cp = 0;
                    if (ent[1] == L'x' || ent[1] == L'X') {
                        for (size_t k = 2; k < ent.size(); ++k)
                            if (ent[k] >= L'0' && ent[k] <= L'9') cp = cp * 16 + (ent[k] - L'0');
                            else if (ent[k] >= L'a' && ent[k] <= L'f') cp = cp * 16 + (ent[k] - L'a' + 10);
                            else if (ent[k] >= L'A' && ent[k] <= L'F') cp = cp * 16 + (ent[k] - L'A' + 10);
                    } else {
                        for (size_t k = 1; k < ent.size(); ++k)
                            if (ent[k] >= L'0' && ent[k] <= L'9') cp = cp * 10 + (ent[k] - L'0');
                    }
                    if (cp > 0) { o += (wchar_t)cp; i = j; continue; }
                }
            }
        }
        o += in[i];
    }
    return o;
}

// 取第一个标签对之间的文本（小写标签名；支持 self-close 返回空）
static bool ExtractTag(const std::wstring& xml, const wchar_t* tag, size_t from,
                       std::wstring& out, size_t& nextPos)
{
    std::wstring open = L"<" + ToLower(tag);
    std::wstring close = L"</" + ToLower(tag) + L">";
    std::wstring low = ToLower(xml);
    size_t p = low.find(open, from);
    if (p == std::wstring::npos) { nextPos = from; return false; }
    size_t tagEnd = xml.find(L'>', p);
    if (tagEnd == std::wstring::npos) { nextPos = from; return false; }
    // 处理 <tag ...> 带属性的情况
    if (xml[tagEnd - 1] == L'/') { out.clear(); nextPos = tagEnd + 1; return true; } // self-close
    size_t c = low.find(close, tagEnd);
    if (c == std::wstring::npos) { nextPos = tagEnd + 1; return false; }
    std::wstring raw = xml.substr(tagEnd + 1, c - tagEnd - 1);
    // 去 CDATA
    std::wstring cd = ToLower(raw);
    size_t cd0 = cd.find(L"<![cdata[");
    if (cd0 != std::wstring::npos) {
        size_t cd1 = cd.find(L"]]>", cd0);
        if (cd1 != std::wstring::npos) raw = raw.substr(cd0 + 9, cd1 - cd0 - 9);
    }
    out = Trim(DecodeEntities(raw));
    nextPos = c + close.size();
    return true;
}

static bool HttpGet(const std::wstring& urlW, std::string& outBody)
{
    std::string u = W2U(urlW);
    size_t p = u.find("://");
    if (p == std::string::npos) return false;
    std::string rest = u.substr(p + 3);
    bool secure = (u.substr(0, p) == "https");
    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    std::string path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    size_t colon = hostport.find(':');
    std::string host = hostport;
    int port = secure ? 443 : 80;
    if (colon != std::string::npos) { host = hostport.substr(0, colon); port = atoi(hostport.substr(colon+1).c_str()); }

    std::wstring whost = U2W(host);
    HINTERNET hSess = WinHttpOpen(L"LJQuizRss/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return false;
    int timeoutMs = 8000;  // 每条源 8s 上限，避免拖垮出题
    WinHttpSetOption(hSess, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hSess, WINHTTP_OPTION_SEND_TIMEOUT,    &timeoutMs, sizeof(timeoutMs));
    WinHttpSetOption(hSess, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
    HINTERNET hConn = WinHttpConnect(hSess, whost.c_str(), (INTERNET_PORT)port, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return false; }

    std::wstring wpath = U2W(path.empty() ? std::string("/") : path);
    DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", wpath.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return false; }

    bool ok = false;
    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0)) {
        if (WinHttpReceiveResponse(hReq, nullptr)) {
            DWORD avail = 0; std::string buf;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                size_t old = buf.size(); buf.resize(old + avail);
                DWORD rd = 0;
                if (!WinHttpReadData(hReq, &buf[old], avail, &rd) || rd == 0) { buf.resize(old); break; }
                buf.resize(old + rd);
            }
            outBody = std::move(buf);
            ok = true;
        }
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return ok;
}

// 从一个 RSS/Atom 文档抽取前 maxItems 条（title + summary）
static void ExtractEntries(const std::wstring& xml, int maxItems, std::wstring& out)
{
    const wchar_t* itemTags[2] = { L"item", L"entry" };
    size_t pos = 0;
    int taken = 0;
    while (taken < maxItems) {
        // 找下一个 item 或 entry 起点
        size_t best = std::wstring::npos;
        for (auto t : itemTags) {
            std::wstring open = L"<" + std::wstring(t);
            size_t q = ToLower(xml).find(open, pos);
            if (q != std::wstring::npos && (best == std::wstring::npos || q < best)) best = q;
        }
        if (best == std::wstring::npos) break;
        size_t end = best;
        // 找该块的闭合（简单配对：统计同名开合标签）
        std::wstring blkTag = (ToLower(xml).substr(best, 5).find(L"item") != std::wstring::npos) ? L"item" : L"entry";
        std::wstring openT = L"<" + blkTag, closeT = L"</" + blkTag + L">";
        int depth = 0; bool closed = false;
        size_t scan = best;
        while (scan < xml.size()) {
            std::wstring low = ToLower(xml);
            size_t o = low.find(openT, scan);
            size_t c = low.find(closeT, scan);
            if (o != std::wstring::npos && (c == std::wstring::npos || o < c)) { depth++; scan = o + openT.size(); }
            else if (c != std::wstring::npos) {
                depth--; scan = c + closeT.size();
                if (depth <= 0) { end = scan; closed = true; break; }
            } else break;
        }
        if (!closed) break;
        std::wstring block = xml.substr(best, end - best);
        std::wstring title, summary;
        size_t np = 0;
        if (ExtractTag(block, L"title", 0, title, np)) {
            size_t sp = 0;
            // description(RSS) 或 summary(Atom)
            if (!ExtractTag(block, L"description", 0, summary, sp)) ExtractTag(block, L"summary", 0, summary, sp);
            if (!title.empty()) {
                out += L"· " + title;
                if (!summary.empty()) {
                    if (summary.size() > 160) summary = summary.substr(0, 160) + L"…";
                    out += L" —— " + summary;
                }
                out += L"\n";
                ++taken;
            }
        }
        pos = end;
    }
}

bool QuizFetchRssDigest(const std::wstring& feedsJoined, std::wstring& digestOut, std::wstring& errOut)
{
    digestOut.clear(); errOut.clear();
    if (feedsJoined.empty()) return true;   // 空 → 不联网，走模型知识保底

    // 拆分多源（分号 / 换行）
    std::vector<std::wstring> feeds;
    std::wstring cur;
    for (wchar_t c : feedsJoined) {
        if (c == L';' || c == L'\n' || c == L'\r') {
            cur = Trim(cur);
            if (!cur.empty()) feeds.push_back(cur);
            cur.clear();
        } else cur += c;
    }
    cur = Trim(cur);
    if (!cur.empty()) feeds.push_back(cur);
    if (feeds.empty()) return true;

    const int kMaxPerFeed = 5;
    const size_t kMaxTotal = 4000;  // digest 长度上限，避免 prompt 爆掉
    std::wstring digest;
    int okFeeds = 0;
    for (auto& f : feeds) {
        std::string body;
        if (!HttpGet(f, body)) continue;
        std::wstring xml = U2W(body);
        if (xml.empty()) continue;
        std::wstring part;
        ExtractEntries(xml, kMaxPerFeed, part);
        if (!part.empty()) {
            digest += part;
            ++okFeeds;
        }
        if (digest.size() >= kMaxTotal) break;
    }
    if (digest.size() > kMaxTotal) digest = digest.substr(0, kMaxTotal);
    digestOut = digest;
    if (okFeeds == 0 && !feeds.empty())
        errOut = L"所有 RSS 源抓取失败（网络/源不可用），已忽略，生成器将走模型知识保底";
    return true;
}

} // namespace quiz
} // namespace lj
