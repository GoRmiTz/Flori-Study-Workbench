#pragma once
// ============================================================
//  DocxStat.h — 零依赖 .docx 字数统计（F-D4 本地文件集成）
//
//  不引入任何第三方库：自写 mini-zip（中央目录解析）+
//  RFC1951 DEFLATE inflate，提取 word/document.xml 文本后
//  统计「字数」（非空白字符数，贴近 Word「字符数（不计空格）」）。
//
//  隐私红线：仅读文档文本以统计字数，不向外发送、不残留正文。
//  纯本地、同步、无网络。
// ============================================================
#include <string>
#include <vector>

namespace lj {

struct DocxStat
{
    bool ok = false;
    int  chars = 0;          // 字数（非空白 Unicode 字符数）
    std::wstring err;        // 失败原因（ok==false 时有效）
};

// 统计指定 .docx 的字数。path 为用户显式授权的申论文档绝对路径。
// 失败返回 ok==false，err 描述原因（非 docx / 无法读取 / 解析错误等）。
DocxStat CountDocxWords(const std::wstring& path);

} // namespace lj
