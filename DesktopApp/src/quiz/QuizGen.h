#pragma once
// ============================================================
//  QuizGen.h — 练考模块 Phase 2-1：AI 出题生成器
//  零依赖 winhttp 调 OpenAI 兼容 /chat/completions，按《题目MD规范》§7 模板
//  生成纯 Markdown 试卷，写 accounts/<账户>/quiz/<类别>/YYYY-MM-DD.md。
//  自带解析自校验：返回后先用 QuizParser 验证≥1题，失败则剥离代码围栏重试。
//  红线：请求体仅含 日期/类别/题量/可选RSS摘要，绝不喂窗内文本/路径/URL/私人数据。
//  依赖：QuizParser（解析校验）、lj::json（响应解析/落盘）。
// ============================================================
#include <string>

namespace lj {
namespace quiz {

// AI 凭据（复用看板娘已填设置，见 QuizScheduler 装配）
struct QuizAIConfig
{
    std::wstring apiBase;   // 如 L"https://api.deepseek.com/v1"
    std::wstring apiKey;    // 仅本地面板配置，不硬编码不入库
    std::wstring model = L"deepseek-v4-flash";
    bool        enabled = false;  // apiBase+apiKey 均非空才视为可用
};

// 生成「每日一练」并写入 outDir/<dateISO>.md（outDir 末尾含分隔符）。
//  rssDigest：可选，RSS 当日要闻摘要（纯文本），空则模型凭知识生成。
//  成功返回 true；errOut 带回失败原因（网络/解析/写盘）。
bool QuizGenerateDaily(const std::wstring& outDir,
                       const QuizAIConfig& cfg,
                       const std::wstring& dateISO,
                       const std::wstring& category,
                       int questionCount,
                       const std::wstring& rssDigest,
                       std::wstring& errOut);

// 本地今日日期 ISO（YYYY-MM-DD）
std::wstring QuizTodayISO();

} // namespace quiz
} // namespace lj
