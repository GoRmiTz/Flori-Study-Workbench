// ============================================================
//  BoxStore.cpp — 题集卡片盒落盘（quiz_boxes.json，手写极简 JSON）
// ============================================================
#include "quiz/BoxStore.h"
#include "app/Json.h"
#include "core/Common.h"
#include <cstdio>

namespace lj {

// ---------------- 编码工具（与 Store.cpp 同口径）----------------
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
using namespace lj::json;

BoxStore& BoxStore::Instance()
{
    static BoxStore s;
    return s;
}

std::vector<QuizBox> BoxStore::Load()
{
    std::vector<QuizBox> out;
    std::wstring fp = Path();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return out; }
    std::string buf; buf.resize((size_t)sz);
    size_t rd = fread(&buf[0], 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) return out;

    json::Parser parser(buf.data(), buf.size());
    json::JVal v = parser.parse();
    if (v.type != json::JVal::Arr) return out;
    for (auto& el : v.arr) {
        if (el.type != JVal::Obj) continue;
        QuizBox b;
        auto gs = [&](const json::JVal& o, const char* k) -> std::wstring {
            auto it = o.obj.find(k);
            return it == o.obj.end() ? L"" : U2W(it->second.str);
        };
        auto gn = [&](const json::JVal& o, const char* k) -> long long {
            auto it = o.obj.find(k);
            return it == o.obj.end() ? 0 : (long long)it->second.num;
        };
        b.id      = gs(el, "id");
        b.name    = gs(el, "name");
        b.kind    = (int)gn(el, "kind");
        b.created = gn(el, "created");
        auto ci = el.obj.find("cards");
        if (ci != el.obj.end() && ci->second.type == json::JVal::Arr) {
            for (auto& cel : ci->second.arr) {
                if (cel.type != json::JVal::Obj) continue;
                BoxCard cd;
                cd.id    = gs(cel, "id");
                cd.front = gs(cel, "front");
                cd.back  = gs(cel, "back");
                cd.tag   = gs(cel, "tag");
                cd.added = gn(cel, "added");
                b.cards.push_back(std::move(cd));
            }
        }
        if (b.id.empty()) continue;
        out.push_back(std::move(b));
    }
    return out;
}

void BoxStore::Save(const std::vector<QuizBox>& boxes)
{
    std::string out = "[\n";
    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto& b = boxes[i];
        out += "  {\n";
        out += "    \"id\": "      + JQuote(W2U(b.id))      + ",\n";
        out += "    \"name\": "    + JQuote(W2U(b.name))    + ",\n";
        out += "    \"kind\": "    + std::to_string(b.kind) + ",\n";
        out += "    \"created\": " + std::to_string(b.created) + ",\n";
        out += "    \"cards\": [\n";
        for (size_t j = 0; j < b.cards.size(); ++j) {
            const auto& c = b.cards[j];
            out += "      {\n";
            out += "        \"id\": "    + JQuote(W2U(c.id))    + ",\n";
            out += "        \"front\": " + JQuote(W2U(c.front)) + ",\n";
            out += "        \"back\": "  + JQuote(W2U(c.back))  + ",\n";
            out += "        \"tag\": "   + JQuote(W2U(c.tag))   + ",\n";
            out += "        \"added\": " + std::to_string(c.added) + "\n";
            out += (j + 1 < b.cards.size()) ? "      },\n" : "      }\n";
        }
        out += "    ]\n";
        out += (i + 1 < boxes.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";
    FILE* f = _wfopen(Path().c_str(), L"wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
}

static std::wstring GenId(const wchar_t* prefix)
{
    wchar_t buf[40];
    swprintf_s(buf, L"%s%lld", prefix, (long long)GetTickCount64());
    return buf;
}

void BoxStore::AddBox(const std::wstring& name, int kind)
{
    auto v = Load();
    QuizBox b;
    b.id = GenId(L"bx_");
    b.name = name;
    b.kind = kind;
    b.created = (long long)time(nullptr);
    v.push_back(b);
    Save(v);
}

void BoxStore::DeleteBox(const std::wstring& boxId)
{
    auto v = Load();
    for (size_t i = 0; i < v.size(); ) {
        if (v[i].id == boxId) v.erase(v.begin() + i);
        else ++i;
    }
    Save(v);
}

void BoxStore::RenameBox(const std::wstring& boxId, const std::wstring& name)
{
    auto v = Load();
    for (auto& b : v) if (b.id == boxId) { b.name = name; break; }
    Save(v);
}

void BoxStore::AddCard(const std::wstring& boxId, const BoxCard& card)
{
    auto v = Load();
    for (auto& b : v) if (b.id == boxId) { b.cards.push_back(card); break; }
    Save(v);
}

void BoxStore::DeleteCard(const std::wstring& boxId, const std::wstring& cardId)
{
    auto v = Load();
    for (auto& b : v) if (b.id == boxId) {
        for (size_t i = 0; i < b.cards.size(); ) {
            if (b.cards[i].id == cardId) b.cards.erase(b.cards.begin() + i);
            else ++i;
        }
        break;
    }
    Save(v);
}

} // namespace lj
