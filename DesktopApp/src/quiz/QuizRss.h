#pragma once
// ============================================================
//  QuizRss.h — 练考模块 Phase 2-4：RSS/Atom 时政抓取（增强项）
//  零依赖 winhttp + 轻量 XML 抽取；抓取给定源，拼接「标题 + 摘要」为纯文本 digest，
//  注入生成器 prompt 提升时效。任一源失败均跳过（不致命）；feeds 为空则不发起任何网络请求。
//  红线性：只取公开新闻标题/摘要文本，绝不抓取/上传任何用户私人数据。
// ============================================================
#include <string>

namespace lj {
namespace quiz {

// 抓取 feeds（多个源以换行或分号分隔）拼出 digest。
//  feedsJoined 为空 → digestOut 直接置空（返回 true，不联网）。
//  返回 true 表示流程正常完成（即使所有源都失败，digest 为空由生成器走模型知识保底）。
bool QuizFetchRssDigest(const std::wstring& feedsJoined,
                        std::wstring& digestOut,
                        std::wstring& errOut);

// 默认源（示例，按可用性自行增删；留空则仅用模型知识保底）。
// 在你的部署里把可用 RSS/Atom 地址填入即可启用。
extern const wchar_t* kQuizRssDefaultFeeds[];
extern const int      kQuizRssDefaultFeedCount;

} // namespace quiz
} // namespace lj
