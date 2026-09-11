// ============================================================
//  KnowledgeDrop.cpp — 知识库页内拖拽收集（OLE IDropTarget）
//  实现说明见 KnowledgeDrop.h。拖放目标挂在主窗口客户区；
//  主线程已 CoInitializeEx(APARTMENTTHREADED)，满足 OLE STA 要求。
// ============================================================
#include "core/KnowledgeDrop.h"
#include "core/TrayIcon.h"
#include "app/Store.h"
#include <atomic>
#include <cstring>
#include <string>
#include <ctime>
#include <ole2.h>
#include <oleidl.h>
#include <shlobj.h>   // CF_HDROP / DragQueryFileW

namespace lj {

static std::atomic<bool>      s_dragActive{ false };
static std::atomic<long long> s_rev{ 0 };

namespace {

bool HasFormat(IDataObject* pdo, CLIPFORMAT fmt)
{
    FORMATETC fe{ fmt, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    return pdo->QueryGetData(&fe) == S_OK;
}

bool Acceptable(IDataObject* pdo)
{
    return HasFormat(pdo, CF_UNICODETEXT) || HasFormat(pdo, CF_HDROP);
}

std::wstring Trim(const std::wstring& t)
{
    size_t a = 0, b = t.size();
    while (a < b && (t[a] == L'\r' || t[a] == L'\n' || t[a] == L' ' || t[a] == L'\t')) a++;
    while (b > a && (t[b - 1] == L'\r' || t[b - 1] == L'\n' || t[b - 1] == L' ' || t[b - 1] == L'\t')) b--;
    return t.substr(a, b - a);
}

std::wstring FirstLine(const std::wstring& t)
{
    size_t a = 0;
    while (a < t.size() && (t[a] == L'\r' || t[a] == L'\n' || t[a] == L' ' || t[a] == L'\t')) a++;
    size_t b = a;
    while (b < t.size() && t[b] != L'\r' && t[b] != L'\n') b++;
    std::wstring s = t.substr(a, b - a);
    if (s.size() > 48) s = s.substr(0, 48) + L"…";
    if (s.empty()) s = L"未命名考点";
    return s;
}

std::wstring GenId()
{
    static long long s_seq = 0;
    return L"kc_" + std::to_wstring((long long)time(nullptr)) + L"_" + std::to_wstring(++s_seq);
}

std::wstring GetText(IDataObject* pdo)
{
    FORMATETC fe{ CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM sm{};
    if (SUCCEEDED(pdo->GetData(&fe, &sm)) && sm.tymed == TYMED_HGLOBAL && sm.hGlobal) {
        wchar_t* p = (wchar_t*)GlobalLock(sm.hGlobal);
        std::wstring s = p ? p : L"";
        GlobalUnlock(sm.hGlobal);
        ReleaseStgMedium(&sm);
        return s;
    }
    FORMATETC ft{ CF_TEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    if (SUCCEEDED(pdo->GetData(&ft, &sm)) && sm.tymed == TYMED_HGLOBAL && sm.hGlobal) {
        char* p = (char*)GlobalLock(sm.hGlobal);
        int n = p ? (int)strlen(p) : 0;
        std::wstring s(n, L' ');
        for (int i = 0; i < n; ++i) s[i] = (wchar_t)(unsigned char)p[i];
        GlobalUnlock(sm.hGlobal);
        ReleaseStgMedium(&sm);
        return s;
    }
    return L"";
}

// 读文本文件（UTF-8 优先，失败退 ANSI），上限 64KB；失败返回空
std::wstring ReadTextFile(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return L""; }
    const long long cap = 64 * 1024;
    DWORD n = (DWORD)(sz.QuadPart > cap ? cap : sz.QuadPart);
    std::string raw(n, '\0');
    DWORD rd = 0;
    BOOL okR = ReadFile(h, &raw[0], n, &rd, nullptr);
    CloseHandle(h);
    if (!okR || rd == 0) return L"";
    raw.resize(rd);
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
        raw.erase(0, 3);   // UTF-8 BOM
    // 严格 UTF-8；不合法则按 ANSI（记事本另存 ANSI 的老文件）
    int wl = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                 raw.data(), (int)raw.size(), nullptr, 0);
    if (wl > 0) {
        std::wstring w(wl, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, raw.data(), (int)raw.size(), &w[0], wl);
        return w;
    }
    wl = MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), nullptr, 0);
    if (wl <= 0) return L"";
    std::wstring w(wl, L'\0');
    MultiByteToWideChar(CP_ACP, 0, raw.data(), (int)raw.size(), &w[0], wl);
    return w;
}

// 文件拖入：单个 .md/.txt 读正文（标题取文件名），多个/其他类型记路径
std::wstring FileDropBody(IDataObject* pdo, std::wstring& title)
{
    FORMATETC fe{ CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM sm{};
    if (!SUCCEEDED(pdo->GetData(&fe, &sm)) || !sm.hGlobal) return L"";
    HDROP h = (HDROP)sm.hGlobal;
    UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
    std::wstring body;
    if (n == 1) {
        wchar_t buf[MAX_PATH + 1] = { 0 };
        if (DragQueryFileW(h, 0, buf, MAX_PATH + 1) > 0) {
            std::wstring path = buf;
            // 文件名（不含目录）作标题候选
            size_t slash = path.find_last_of(L"\\/");
            std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
            size_t dot = name.find_last_of(L'.');
            auto EqExt = [&](const wchar_t* e) {
                return dot != std::wstring::npos && _wcsicmp(name.c_str() + dot, e) == 0;
            };
            bool isText = EqExt(L".md") || EqExt(L".markdown") || EqExt(L".txt");
            if (isText) {
                std::wstring content = Trim(ReadTextFile(path));
                if (!content.empty()) {
                    title = name;
                    body = content;
                }
            }
            if (body.empty()) { body = path; title.clear(); }   // 非文本文件记路径
        }
    } else {
        for (UINT i = 0; i < n; ++i) {
            wchar_t buf[MAX_PATH + 1] = { 0 };
            if (DragQueryFileW(h, i, buf, MAX_PATH + 1) > 0) {
                body += buf;
                if (i + 1 < n) body += L"\n";
            }
        }
    }
    ReleaseStgMedium(&sm);
    return body;
}

class KnowledgeDropTarget : public IDropTarget
{
public:
    KnowledgeDropTarget() : m_ref(1) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pdo, DWORD, POINTL, DWORD* pe) override
    {
        m_canDrop = Acceptable(pdo);
        s_dragActive = m_canDrop;   // 知识库页立即画高亮蒙层
        *pe = m_canDrop ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* pe) override
    {
        s_dragActive = m_canDrop;
        *pe = m_canDrop ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override
    {
        s_dragActive = false;
        m_canDrop = false;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pdo, DWORD, POINTL, DWORD* pe) override
    {
        s_dragActive = false;
        m_canDrop = false;

        std::wstring text = GetText(pdo);
        std::wstring title;
        std::wstring src = L"页内拖入";
        if (text.empty()) {
            text = FileDropBody(pdo, title);
            if (!title.empty()) src = L"文件拖入";
        }
        if (!Trim(text).empty()) {
            KCard c;
            c.id     = GenId();
            c.title  = title.empty() ? FirstLine(text) : title;
            c.body   = Trim(text);
            c.source = src;
            c.ts     = (long long)time(nullptr);
            CheckinStore::Instance().AddKnowledge(c);
            TrayIcon::Instance().Balloon(L"已收集考点", c.title.c_str());
            s_rev++;   // KnowledgeView 轮询 → 即时刷新 + Toast
            *pe = DROPEFFECT_COPY;
        } else {
            *pe = DROPEFFECT_NONE;
        }
        return S_OK;
    }

private:
    ULONG m_ref = 1;
    bool  m_canDrop = false;
};

} // namespace

void RegisterKnowledgeDrop(HWND hwnd)
{
    if (!hwnd) return;
    static KnowledgeDropTarget* s_target = nullptr;
    if (!s_target) {
        s_target = new KnowledgeDropTarget();
        s_target->AddRef();   // 常驻引用：与进程同生命周期
    }
    // 注册失败要留痕：最常见的根因是线程没有先 OleInitialize（表现为「文件拖不进软件」）
    HRESULT hr = RegisterDragDrop(hwnd, s_target);
    if (FAILED(hr))
        LogLine(L"[drop] RegisterDragDrop 失败 hr=0x%08X（需 OleInitialize STA）", (unsigned)hr);
    else
        LogLine(L"[drop] RegisterDragDrop OK");
}

void UnregisterKnowledgeDrop(HWND hwnd)
{
    if (hwnd) RevokeDragDrop(hwnd);
    s_dragActive = false;
}

bool KnowledgeDragActive() { return s_dragActive; }
long long KnowledgeRev()   { return s_rev; }

} // namespace lj
