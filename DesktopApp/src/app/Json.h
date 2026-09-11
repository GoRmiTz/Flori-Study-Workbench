#pragma once
// ============================================================
//  Json.h — 极简 JSON（头文件实现，无第三方依赖）
//  原先内嵌在 Store.cpp 的匿名命名空间里；接入云同步后 Cloud.cpp 也要解析
//  服务端响应，故抽到 lj::json 命名空间共用，避免两份解析器各自漂移。
//  仅覆盖本项目需要的子集：对象 / 数组 / 字符串 / 数字 / true / false / null。
// ============================================================
#include <string>
#include <map>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace lj::json {

struct JVal
{
    enum T { Null, Bool, Num, Str, Obj, Arr } type = Null;
    bool   bval = false;
    double num = 0.0;
    std::string str;
    std::map<std::string, JVal> obj;
    std::vector<JVal> arr;

    bool IsObj() const { return type == Obj; }
    bool IsArr() const { return type == Arr; }
};

struct Parser
{
    const char* p; const char* e;
    Parser(const char* s, size_t n) : p(s), e(s + n) {}

    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p; }
    JVal parse() { ws(); return parseValue(); }

    JVal parseValue()
    {
        ws();
        if (p >= e) return JVal{};
        char c = *p;
        if (c == '{') return parseObj();
        if (c == '[') return parseArr();
        if (c == '"') { JVal v; v.type = JVal::Str; v.str = parseStr(); return v; }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNum();
        if (c == 't' && e - p >= 4) { p += 4; JVal v; v.type = JVal::Bool; v.bval = true;  return v; }
        if (c == 'f' && e - p >= 5) { p += 5; JVal v; v.type = JVal::Bool; v.bval = false; return v; }
        if (c == 'n' && e - p >= 4) { p += 4; return JVal{}; }
        return JVal{};
    }

    JVal parseArr()
    {
        JVal v; v.type = JVal::Arr;
        ++p;                       // [
        ws();
        if (p < e && *p == ']') { ++p; return v; }
        while (p < e) {
            ws();
            v.arr.push_back(parseValue());
            ws();
            if (p < e && *p == ',') { ++p; continue; }
            if (p < e && *p == ']') { ++p; break; }
            break;
        }
        return v;
    }

    JVal parseObj()
    {
        JVal v; v.type = JVal::Obj;
        ++p;                       // {
        ws();
        if (p < e && *p == '}') { ++p; return v; }
        while (p < e) {
            ws();
            if (*p != '"') break;
            std::string key = parseStr();
            ws();
            if (p >= e || *p != ':') break;
            ++p;                   // :
            v.obj[key] = parseValue();
            ws();
            if (p < e && *p == ',') { ++p; continue; }
            if (p < e && *p == '}') { ++p; break; }
            break;
        }
        return v;
    }

    JVal parseNum()
    {
        const char* start = p;
        if (*p == '-') ++p;
        while (p < e && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E'
                         || *p == '+' || *p == '-')) ++p;
        JVal v; v.type = JVal::Num;
        if (p > start) v.num = strtod(start, nullptr);
        return v;
    }

    std::string parseStr()
    {
        ++p;                       // 开头引号
        std::string out;
        while (p < e) {
            char c = *p++;
            if (c == '"') break;
            if (c != '\\') { out += c; continue; }
            if (p >= e) break;
            char esc = *p++;
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    if (p + 4 > e) break;
                    int cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        char h = *p++; cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                    }
                    // 代理对：\uD800-\uDBFF 后面必跟低位代理，合成 4 字节 UTF-8
                    if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 <= e && p[0] == '\\' && p[1] == 'u') {
                        const char* save = p;
                        p += 2;
                        int lo = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = *p++; lo <<= 4;
                            if (h >= '0' && h <= '9') lo |= h - '0';
                            else if (h >= 'a' && h <= 'f') lo |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') lo |= h - 'A' + 10;
                        }
                        if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else p = save;
                    }
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xF0 | (cp >> 18));
                        out += (char)(0x80 | ((cp >> 12) & 0x3F));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += esc; break;
            }
        }
        return out;
    }
};

// ---------------- 序列化 ----------------
inline std::string JEsc(const std::string& s)
{
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b";  break;
            case '\f': o += "\\f";  break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default: {
                unsigned char u = (unsigned char)c;
                if (u < 0x20) { char buf[8]; snprintf(buf, sizeof(buf), "\\u%04x", u); o += buf; }
                else o += c;
            }
        }
    }
    return o;
}

inline std::string JQuote(const std::string& s) { return "\"" + JEsc(s) + "\""; }

inline std::string JDump(const JVal& v)
{
    switch (v.type) {
        case JVal::Str:  return JQuote(v.str);
        case JVal::Bool: return v.bval ? "true" : "false";
        case JVal::Num: {
            double r = v.num;
            long long i = (long long)r;
            if ((double)i == r) return std::to_string(i);
            char buf[40]; snprintf(buf, sizeof(buf), "%.10g", r); return buf;
        }
        case JVal::Obj: {
            std::string s = "{";
            bool first = true;
            for (auto& kv : v.obj) {
                if (!first) s += ",";
                first = false;
                s += JQuote(kv.first) + ":" + JDump(kv.second);
            }
            return s + "}";
        }
        case JVal::Arr: {
            std::string s = "[";
            for (size_t i = 0; i < v.arr.size(); ++i) {
                if (i) s += ",";
                s += JDump(v.arr[i]);
            }
            return s + "]";
        }
        default: return "null";
    }
}

/** 从对象里安全取子节点；不是对象或键不存在时返回 nullptr。 */
inline const JVal* JGet(const JVal& o, const char* key)
{
    if (o.type != JVal::Obj) return nullptr;
    auto it = o.obj.find(key);
    return it == o.obj.end() ? nullptr : &it->second;
}

/** 取字符串字段，缺失回退 def。 */
inline std::string JStr(const JVal& o, const char* key, const char* def = "")
{
    const JVal* v = JGet(o, key);
    return (v && v->type == JVal::Str) ? v->str : std::string(def);
}

// ---------------- 文件读写（UTF-8 原文） ----------------
#pragma warning(push)
#pragma warning(disable : 4996)   // _wfopen 在本项目里是刻意选择（无需 CRT 安全版本）

inline bool ReadFileRaw(const std::wstring& path, std::string& out)
{
    out.clear();
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return false; }
    out.resize((size_t)sz);
    size_t rd = fread(&out[0], 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { out.clear(); return false; }
    return true;
}

inline bool WriteFileRaw(const std::wstring& path, const std::string& buf)
{
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    size_t wr = fwrite(buf.data(), 1, buf.size(), f);
    fclose(f);
    return wr == buf.size();
}

#pragma warning(pop)

} // namespace lj::json
