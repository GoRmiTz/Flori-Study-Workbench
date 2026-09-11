// ============================================================
//  DocxStat.cpp — 零依赖 .docx 字数统计
//  自写 mini-zip（中央目录）+ RFC1951 DEFLATE inflate。
//  仅依赖 STL + <fstream>；可脱离项目独立编译（用于单测）。
// ============================================================
#include "core/DocxStat.h"
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace {

// ---------------- 读整个文件到字节 ----------------
bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff len = f.tellg();
    if (len <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize((size_t)len);
    f.read(reinterpret_cast<char*>(out.data()), len);
    return (size_t)f.gcount() == out.size();
}

// ---------------- DEFLATE 比特读取（LSB first）----------------
struct BitReader
{
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;      // 当前字节下标
    int    bitbuf = 0;
    int    bitcnt = 0;

    int getBit()
    {
        if (bitcnt == 0) {
            if (pos >= size) return 0;
            bitbuf = data[pos++];
            bitcnt = 8;
        }
        int b = bitbuf & 1;
        bitbuf >>= 1;
        bitcnt--;
        return b;
    }
    int getBits(int n)
    {
        int v = 0;
        for (int i = 0; i < n; ++i) v |= getBit() << i;
        return v;
    }
    void align() { bitcnt = 0; bitbuf = 0; }   // 字节对齐（stored 块用）
};

// ---------------- 规范 Huffman 解码：返回真实符号值 ----------------
// 同长度下符号按升序分配码值；符号长度不连续分布（固定 Huffman 8 位段为
// 0–143 与 280–287 两截）时，必须返回真实符号索引，不能顺序计数。
struct Huff
{
    std::vector<int> len;   // 符号 i 的码长（0 表示不存在）

    int decode(BitReader& br) const
    {
        int code = 0, first = 0;
        for (int l = 1; l <= 15; ++l) {
            code = (code << 1) | br.getBit();
            int k = 0;
            for (size_t i = 0; i < len.size(); ++i) {
                if (len[i] == l) {
                    if (k == code - first) return (int)i;
                    k++;
                }
            }
            int count = 0;
            for (size_t i = 0; i < len.size(); ++i)
                if (len[i] == l) count++;
            first = (first + count) << 1;
        }
        return -1;   // 错误
    }
};

const int LEN_BASE[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const int LEN_EXT[29]  = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const int DIST_BASE[30]= {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const int DIST_EXT[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

// 解码一个压缩块，结果追加到 out
bool inflateBlock(BitReader& br, const Huff& lit, const Huff& dist, std::vector<uint8_t>& out)
{
    for (;;) {
        int sym = lit.decode(br);
        if (sym < 0) return false;
        if (sym == 256) return true;                 // 块结束
        if (sym < 256) { out.push_back((uint8_t)sym); continue; }
        int li = sym - 257;
        if (li >= 29) return false;
        int length = LEN_BASE[li] + br.getBits(LEN_EXT[li]);
        int ds = dist.decode(br);
        if (ds < 0 || ds >= 30) return false;
        int distv = DIST_BASE[ds] + br.getBits(DIST_EXT[ds]);
        if (distv > (int)out.size()) return false;
        // LZ77 回拷：源基址固定，逐字节推进；distv<length（重叠）时自动引用刚写入的字节。
        size_t src = out.size() - (size_t)distv;
        for (int i = 0; i < length; ++i)
            out.push_back(out[src + i]);
    }
}

Huff FixedLitHuff()
{
    Huff h;
    h.len.assign(288, 0);
    for (int i = 0;   i <= 143; ++i) h.len[i] = 8;
    for (int i = 144; i <= 255; ++i) h.len[i] = 9;
    for (int i = 256; i <= 279; ++i) h.len[i] = 7;
    for (int i = 280; i <= 287; ++i) h.len[i] = 8;
    return h;
}
Huff FixedDistHuff()
{
    Huff h;
    h.len.assign(32, 5);
    return h;
}

bool inflateDynamic(BitReader& br, std::vector<uint8_t>& out)
{
    int hlit  = br.getBits(5) + 257;
    int hdist = br.getBits(5) + 1;
    int hclen = br.getBits(4) + 4;
    static const int ORDER[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    Huff clHuff;
    clHuff.len.assign(19, 0);
    for (int i = 0; i < hclen; ++i)
        clHuff.len[ORDER[i]] = br.getBits(3);

    std::vector<int> litLen(hlit, 0), distLen(hdist, 0);
    int* dst = litLen.data();
    int  dstN = hlit;
    int  mode = 0;   // 0=lit，1=dist
    int  prev = -1;
    int  total = 0;
    while (total < hlit + hdist) {
        int s = clHuff.decode(br);
        if (s < 0) return false;
        if (s < 16) {
            dst[total++] = s; prev = s;
            if (total == hlit && mode == 0) { dst = distLen.data(); dstN = hdist; mode = 1; total = 0; }
        } else if (s == 16) {
            if (prev < 0) return false;
            int rep = 3 + br.getBits(2);
            for (int i = 0; i < rep && total < dstN; ++i) dst[total++] = prev;
            if (total == hlit && mode == 0) { dst = distLen.data(); dstN = hdist; mode = 1; total = 0; }
        } else if (s == 17) {
            int rep = 3 + br.getBits(3);
            for (int i = 0; i < rep && total < dstN; ++i) dst[total++] = 0;
            if (total == hlit && mode == 0) { dst = distLen.data(); dstN = hdist; mode = 1; total = 0; }
        } else if (s == 18) {
            int rep = 11 + br.getBits(7);
            for (int i = 0; i < rep && total < dstN; ++i) dst[total++] = 0;
            if (total == hlit && mode == 0) { dst = distLen.data(); dstN = hdist; mode = 1; total = 0; }
        } else return false;
    }

    Huff lit, dist;
    lit.len = litLen;
    dist.len = distLen;
    return inflateBlock(br, lit, dist, out);
}

bool Inflate(const uint8_t* src, size_t slen, std::vector<uint8_t>& out)
{
    BitReader br{ src, slen, 0, 0, 0 };
    bool last = false;
    while (!last) {
        int bfinal = br.getBit();
        int btype  = br.getBits(2);
        last = (bfinal != 0);
        if (btype == 0) {
            br.align();
            size_t p = br.pos;
            if (p + 4 > br.size) return false;
            int len = br.data[p] | (br.data[p+1] << 8);
            if (p + 2 + (size_t)len > br.size) return false;
            for (int i = 0; i < len; ++i) out.push_back(br.data[p + 2 + i]);
            br.pos = p + 2 + (size_t)len;
        } else if (btype == 1) {
            Huff lit = FixedLitHuff(), dist = FixedDistHuff();
            if (!inflateBlock(br, lit, dist, out)) return false;
        } else if (btype == 2) {
            if (!inflateDynamic(br, out)) return false;
        } else {
            return false;
        }
    }
    return true;
}

// ---------------- 从 xml 文本统计字数 ----------------
std::string DecodeEntities(const std::string& s)
{
    std::string out;
    size_t i = 0, n = s.size();
    while (i < n) {
        if (s[i] == '&') {
            size_t semi = s.find(';', i);
            if (semi != std::string::npos) {
                std::string ent = s.substr(i + 1, semi - i - 1);
                if (ent == "amp") out += '&';
                else if (ent == "lt") out += '<';
                else if (ent == "gt") out += '>';
                else if (ent == "quot") out += '"';
                else if (ent == "apos") out += '\'';
                else if (!ent.empty() && ent[0] == '#') {
                    int cp = 0; bool ok = true;
                    if (ent[1] == 'x' || ent[1] == 'X') {
                        for (size_t k = 2; k < ent.size(); ++k) {
                            char c = ent[k]; int d;
                            if (c >= '0' && c <= '9') d = c - '0';
                            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
                            else { ok = false; break; }
                            cp = cp * 16 + d;
                        }
                    } else {
                        for (size_t k = 1; k < ent.size(); ++k) {
                            if (ent[k] >= '0' && ent[k] <= '9') cp = cp * 10 + (ent[k] - '0');
                            else { ok = false; break; }
                        }
                    }
                    if (ok && cp > 0) {
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                    } else out += s.substr(i, semi - i + 1);
                } else out += s.substr(i, semi - i + 1);
                i = semi + 1;
                continue;
            }
        }
        out += s[i];
        ++i;
    }
    return out;
}

int CountNonSpaceChars(const std::string& utf8)
{
    int count = 0;
    size_t i = 0, n = utf8.size();
    while (i < n) {
        unsigned char c = (unsigned char)utf8[i];
        int cp, adv;
        if (c < 0x80)      { cp = c; adv = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; adv = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; adv = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; adv = 4; }
        else { i++; continue; }
        for (int k = 1; k < adv && i + (size_t)k < n; ++k)
            cp = (cp << 6) | ((unsigned char)utf8[i + k] & 0x3F);
        i += adv;
        if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x3000 || cp == 0x00A0)
            continue;
        count++;
    }
    return count;
}

std::string StripTags(const std::string& xml)
{
    std::string text;
    bool inTag = false;
    for (size_t i = 0; i < xml.size(); ++i) {
        char c = xml[i];
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; continue; }
        if (!inTag) text += c;
    }
    return text;
}

bool FindDocumentXml(const std::vector<uint8_t>& buf,
                    int& method, size_t& dataOff, size_t& compSize)
{
    // EOCD：0x06054b50（小端 50 4B 05 06），从末尾向前扫描
    size_t eocd = std::string::npos;
    for (size_t i = buf.size() >= 22 ? buf.size() - 22 : 0; ; --i) {
        if (buf[i] == 0x50 && buf[i+1] == 0x4B && buf[i+2] == 0x05 && buf[i+3] == 0x06) {
            eocd = i; break;
        }
        if (i == 0) break;
    }
    if (eocd == std::string::npos) return false;

    auto rd32 = [&](size_t o) -> uint32_t {
        return (uint32_t)buf[o] | ((uint32_t)buf[o+1] << 8) |
               ((uint32_t)buf[o+2] << 16) | ((uint32_t)buf[o+3] << 24);
    };
    uint32_t entries = rd32(eocd + 10);
    uint32_t cdOff   = rd32(eocd + 16);

    size_t p = cdOff;
    for (uint32_t e = 0; e < entries; ++e) {
        if (p + 46 > buf.size()) return false;
        if (!(buf[p] == 0x50 && buf[p+1] == 0x4B && buf[p+2] == 0x01 && buf[p+3] == 0x02))
            return false;
        uint16_t compMethod = (uint16_t)(buf[p+10] | (buf[p+11] << 8));
        uint32_t cSize = rd32(p + 20);
        uint16_t fnLen = (uint16_t)(buf[p+28] | (buf[p+29] << 8));
        uint16_t exLen = (uint16_t)(buf[p+30] | (buf[p+31] << 8));
        uint16_t cmLen = (uint16_t)(buf[p+32] | (buf[p+33] << 8));
        uint32_t lho   = rd32(p + 42);
        std::string name((const char*)&buf[p + 46], fnLen);
        if (name == "word/document.xml" ||
            (name.size() >= 17 && name.compare(name.size() - 17, 17, "word/document.xml") == 0)) {
            if (lho + 30 > buf.size()) return false;
            uint16_t lfn = (uint16_t)(buf[lho+26] | (buf[lho+27] << 8));
            uint16_t lex = (uint16_t)(buf[lho+28] | (buf[lho+29] << 8));
            size_t d = lho + 30 + lfn + lex;
            if (d + cSize > buf.size()) return false;   // 尺寸不可信，放弃
            method = compMethod;
            dataOff = d;
            compSize = cSize;
            return true;
        }
        p += 46 + fnLen + exLen + cmLen;
    }
    return false;
}

} // namespace

namespace lj {

DocxStat CountDocxWords(const std::wstring& path)
{
    DocxStat res;
    std::vector<uint8_t> buf;
    if (!ReadFileBytes(path, buf)) {
        res.err = L"无法读取文件（路径不存在或权限不足）";
        return res;
    }
    if (buf.size() < 4 || !(buf[0] == 0x50 && buf[1] == 0x4B)) {
        res.err = L"不是有效的 .docx / .zip 文件";
        return res;
    }
    int method = 0;
    size_t dataOff = 0, compSize = 0;
    if (!FindDocumentXml(buf, method, dataOff, compSize)) {
        res.err = L"未找到 word/document.xml（非标准 docx 或已加密）";
        return res;
    }
    std::vector<uint8_t> xml;
    if (method == 0) {
        xml.assign(buf.begin() + dataOff, buf.begin() + dataOff + compSize);
    } else if (method == 8) {
        if (!Inflate(buf.data() + dataOff, compSize, xml)) {
            res.err = L"解压 word/document.xml 失败（DEFLATE 解析错误）";
            return res;
        }
    } else {
        res.err = L"不支持的压缩方式（仅 stored/deflate）";
        return res;
    }
    std::string text = StripTags(std::string((const char*)xml.data(), xml.size()));
    std::string decoded = DecodeEntities(text);
    res.chars = CountNonSpaceChars(decoded);
    res.ok = true;
    return res;
}

} // namespace lj
