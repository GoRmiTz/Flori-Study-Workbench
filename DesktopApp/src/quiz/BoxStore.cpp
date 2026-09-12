// ============================================================
//  BoxStore.cpp — 题集卡片盒 v2 落盘（三层嵌套 JSON + v1 迁移）
// ============================================================
#include "quiz/BoxStore.h"
#include "app/Json.h"
#include "core/Common.h"
#include <cstdio>
#include <ctime>

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

static std::wstring GenId(const wchar_t* prefix)
{
    wchar_t buf[48];
    swprintf_s(buf, L"%s%lld", prefix, (long long)GetTickCount64());
    return buf;
}

// 卡片 JSON → QCard
static QCard ParseCard(const JVal& cel)
{
    QCard c;
    auto gs = [&](const char* k) -> std::wstring {
        auto it = cel.obj.find(k);
        return it == cel.obj.end() ? L"" : U2W(it->second.str);
    };
    auto gn = [&](const char* k) -> long long {
        auto it = cel.obj.find(k);
        return it == cel.obj.end() ? 0 : (long long)it->second.num;
    };
    c.id = gs("id"); c.front = gs("front"); c.back = gs("back"); c.tag = gs("tag");
    c.difficulty = (int)gn("difficulty");
    if (c.difficulty < 1 || c.difficulty > 5) c.difficulty = 3;
    c.wrongCount = (int)gn("wrongCount");
    c.lastReview = gn("lastReview");
    c.added = gn("added");
    return c;
}

std::vector<QuizSet> BoxStore::Load()
{
    std::vector<QuizSet> out;
    std::wstring fp = Path();
    FILE* f = _wfopen(fp.c_str(), L"rb");
    if (!f) { MigrateFromV1(out); return out; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); MigrateFromV1(out); return out; }
    std::string buf; buf.resize((size_t)sz);
    size_t rd = fread(&buf[0], 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) return out;

    Parser parser(buf.data(), buf.size());
    JVal v = parser.parse();
    if (v.type != JVal::Arr) return out;
    for (auto& el : v.arr) {
        if (el.type != JVal::Obj) continue;
        QuizSet st;
        auto gs = [&](const JVal& o, const char* k) -> std::wstring {
            auto it = o.obj.find(k);
            return it == o.obj.end() ? L"" : U2W(it->second.str);
        };
        auto gn = [&](const JVal& o, const char* k) -> long long {
            auto it = o.obj.find(k);
            return it == o.obj.end() ? 0 : (long long)it->second.num;
        };
        st.id = gs(el, "id");
        st.name = gs(el, "name");
        st.created = gn(el, "created");
        auto bi = el.obj.find("boxes");
        if (bi != el.obj.end() && bi->second.type == JVal::Arr) {
            for (auto& bel : bi->second.arr) {
                if (bel.type != JVal::Obj) continue;
                QuizBox b;
                b.id = gs(bel, "id");
                b.name = gs(bel, "name");
                b.kind = (int)gn(bel, "kind");
                b.created = gn(bel, "created");
                auto ci = bel.obj.find("cards");
                if (ci != bel.obj.end() && ci->second.type == JVal::Arr)
                    for (auto& cel : ci->second.arr)
                        if (cel.type == JVal::Obj) b.cards.push_back(ParseCard(cel));
                if (!b.id.empty()) st.boxes.push_back(std::move(b));
            }
        }
        if (st.id.empty()) continue;
        out.push_back(std::move(st));
    }
    return out;
}

void BoxStore::Save(const std::vector<QuizSet>& sets)
{
    std::string out = "[\n";
    for (size_t i = 0; i < sets.size(); ++i) {
        const auto& st = sets[i];
        out += "  {\n";
        out += "    \"id\": "      + JQuote(W2U(st.id))      + ",\n";
        out += "    \"name\": "    + JQuote(W2U(st.name))    + ",\n";
        out += "    \"created\": " + std::to_string(st.created) + ",\n";
        out += "    \"boxes\": [\n";
        for (size_t j = 0; j < st.boxes.size(); ++j) {
            const auto& b = st.boxes[j];
            out += "      {\n";
            out += "        \"id\": "      + JQuote(W2U(b.id))      + ",\n";
            out += "        \"name\": "    + JQuote(W2U(b.name))    + ",\n";
            out += "        \"kind\": "    + std::to_string(b.kind) + ",\n";
            out += "        \"created\": " + std::to_string(b.created) + ",\n";
            out += "        \"cards\": [\n";
            for (size_t k = 0; k < b.cards.size(); ++k) {
                const auto& c = b.cards[k];
                out += "          {\n";
                out += "            \"id\": "         + JQuote(W2U(c.id))         + ",\n";
                out += "            \"front\": "      + JQuote(W2U(c.front))      + ",\n";
                out += "            \"back\": "       + JQuote(W2U(c.back))       + ",\n";
                out += "            \"tag\": "        + JQuote(W2U(c.tag))         + ",\n";
                out += "            \"difficulty\": " + std::to_string(c.difficulty) + ",\n";
                out += "            \"wrongCount\": " + std::to_string(c.wrongCount) + ",\n";
                out += "            \"lastReview\": " + std::to_string(c.lastReview) + ",\n";
                out += "            \"added\": "      + std::to_string(c.added)      + "\n";
                out += (k + 1 < b.cards.size()) ? "          },\n" : "          }\n";
            }
            out += "        ]\n";
            out += (j + 1 < st.boxes.size()) ? "      },\n" : "      }\n";
        }
        out += "    ]\n";
        out += (i + 1 < sets.size()) ? "  },\n" : "  }\n";
    }
    out += "]\n";
    FILE* f = _wfopen(Path().c_str(), L"wb");
    if (!f) return;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
}

// v1 迁移：quiz_boxes.json（两层数组）→ 默认题集「我的题集」
void BoxStore::MigrateFromV1(std::vector<QuizSet>& sets)
{
    if (sets.empty() && !m_root.empty()) {
        FILE* f = _wfopen(OldPath().c_str(), L"rb");
        if (!f) return;
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); return; }
        std::string buf; buf.resize((size_t)sz);
        size_t rd = fread(&buf[0], 1, (size_t)sz, f);
        fclose(f);
        if (rd != (size_t)sz) return;
        Parser parser(buf.data(), buf.size());
        JVal v = parser.parse();
        if (v.type != JVal::Arr) return;
        QuizSet st;
        st.id = GenId(L"st_");
        st.name = L"我的题集";
        st.created = (long long)time(nullptr);
        for (auto& el : v.arr) {
            if (el.type != JVal::Obj) continue;
            QuizBox b;
            auto gs = [&](const char* k) -> std::wstring {
                auto it = el.obj.find(k);
                return it == el.obj.end() ? L"" : U2W(it->second.str);
            };
            auto gn = [&](const char* k) -> long long {
                auto it = el.obj.find(k);
                return it == el.obj.end() ? 0 : (long long)it->second.num;
            };
            b.id = gs("id"); b.name = gs("name");
            b.kind = (int)gn("kind"); b.created = gn("created");
            auto ci = el.obj.find("cards");
            if (ci != el.obj.end() && ci->second.type == JVal::Arr)
                for (auto& cel : ci->second.arr)
                    if (cel.type == JVal::Obj) b.cards.push_back(ParseCard(cel));
            if (!b.id.empty()) st.boxes.push_back(std::move(b));
        }
        if (!st.boxes.empty()) {
            sets.push_back(std::move(st));
            Save(sets);
            LogLine(L"[boxstore] migrated v1 quiz_boxes.json -> quiz_sets.json (%d boxes)",
                   (int)sets.front().boxes.size());
        }
    }
}

// ---------------- 便捷操作 ----------------
void BoxStore::AddSet(const std::wstring& name)
{
    auto v = Load();
    QuizSet st;
    st.id = GenId(L"st_"); st.name = name;
    st.created = (long long)time(nullptr);
    v.push_back(st);
    Save(v);
}

void BoxStore::DeleteSet(const std::wstring& setId)
{
    auto v = Load();
    for (size_t i = 0; i < v.size(); ) {
        if (v[i].id == setId) v.erase(v.begin() + i);
        else ++i;
    }
    Save(v);
}

void BoxStore::RenameSet(const std::wstring& setId, const std::wstring& name)
{
    auto v = Load();
    for (auto& st : v) if (st.id == setId) { st.name = name; break; }
    Save(v);
}

void BoxStore::AddBox(const std::wstring& setId, const std::wstring& name, int kind)
{
    auto v = Load();
    QuizBox b;
    b.id = GenId(L"bx_"); b.name = name; b.kind = kind;
    b.created = (long long)time(nullptr);
    for (auto& st : v) if (st.id == setId) { st.boxes.push_back(b); break; }
    Save(v);
}

void BoxStore::DeleteBox(const std::wstring& boxId)
{
    auto v = Load();
    for (auto& st : v)
        for (size_t i = 0; i < st.boxes.size(); ) {
            if (st.boxes[i].id == boxId) st.boxes.erase(st.boxes.begin() + i);
            else ++i;
        }
    Save(v);
}

void BoxStore::RenameBox(const std::wstring& boxId, const std::wstring& name)
{
    auto v = Load();
    for (auto& st : v)
        for (auto& b : st.boxes) if (b.id == boxId) { b.name = name; return Save(v); }
}

void BoxStore::AddCard(const std::wstring& boxId, const QCard& card)
{
    auto v = Load();
    for (auto& st : v)
        for (auto& b : st.boxes) if (b.id == boxId) { b.cards.push_back(card); return Save(v); }
}

void BoxStore::DeleteCard(const std::wstring& boxId, const std::wstring& cardId)
{
    auto v = Load();
    for (auto& st : v)
        for (auto& b : st.boxes) if (b.id == boxId) {
            for (size_t i = 0; i < b.cards.size(); ) {
                if (b.cards[i].id == cardId) b.cards.erase(b.cards.begin() + i);
                else ++i;
            }
        }
    Save(v);
}

void BoxStore::UpdateCard(const std::wstring& boxId, const QCard& card)
{
    auto v = Load();
    for (auto& st : v)
        for (auto& b : st.boxes) if (b.id == boxId) {
            for (auto& c : b.cards) if (c.id == card.id) { c = card; return Save(v); }
        }
}

void BoxStore::MoveCard(const std::wstring& fromBox, const std::wstring& cardId,
                        const std::wstring& toBox)
{
    if (fromBox == toBox) return;
    auto v = Load();
    QCard moved;
    bool found = false;
    for (auto& st : v)
        for (auto& b : st.boxes) if (b.id == fromBox) {
            for (size_t i = 0; i < b.cards.size(); ) {
                if (b.cards[i].id == cardId) { moved = b.cards[i]; b.cards.erase(b.cards.begin() + i); found = true; }
                else ++i;
            }
        }
    if (!found) return;
    for (auto& st : v)
        for (auto& b : st.boxes) if (b.id == toBox) { b.cards.push_back(moved); return Save(v); }
}

} // namespace lj
