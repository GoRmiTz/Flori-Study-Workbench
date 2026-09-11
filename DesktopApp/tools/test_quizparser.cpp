// ============================================================
//  test_quizparser.cpp — QuizParser 独立自测（Phase 0-3）
//  编译：cl /EHsc /utf-8 /Isrc src/quiz/QuizParser.cpp tools/test_quizparser.cpp /Fe:tools/test_quizparser.exe
//  运行：tools/test_quizparser.exe [path/to/quiz.md]   （缺省用 docs/quiz-module/sample-quiz.md）
//  退出码 0=全部通过；非 0=存在断言失败。
// ============================================================
#include "quiz/QuizParser.h"
#include <cstdio>
#include <string>

using namespace lj::quiz;

static int g_fail = 0;
static void expect(bool cond, const char* msg)
{
    if (cond) printf("  [PASS] %s\n", msg);
    else { printf("  [FAIL] %s\n", msg); g_fail++; }
}

static std::wstring W(const char* s)
{
    std::wstring o; while (*s) o.push_back((wchar_t)(unsigned char)*s++); return o;
}

int main(int argc, char** argv)
{
    std::string path = "docs/quiz-module/sample-quiz.md";
    if (argc > 1) path = argv[1];

    QuizDoc doc;
    if (!LoadQuiz(W(path.c_str()), doc)) {
        printf("无法读取文件: %s\n", path.c_str());
        return 2;
    }

    printf("=== 卷标题: %ls ===\n", doc.title.c_str());
    printf("meta: ");
    for (auto& kv : doc.meta) printf("%ls=%ls  ", kv.first.c_str(), kv.second.c_str());
    printf("\n题目数: %zu\n\n", doc.questions.size());

    for (auto& q : doc.questions) {
        printf("Q%d [%ls] 难度%d  知识点:%ls\n", q.id, q.type.c_str(), q.difficulty, q.point.c_str());
        printf("  题干: %ls\n", q.stem.c_str());
        for (auto& o : q.options) printf("    %ls. %ls\n", o.key.c_str(), o.text.c_str());
        printf("  答案: ");
        for (auto& a : q.answer) printf("%ls ", a.c_str());
        printf("\n  解析: %ls\n\n", q.explain.c_str());
    }

    // ---- 断言（契约验证）----
    expect(doc.questions.size() == 3, "应解析出 3 道题");
    if (doc.questions.size() >= 3) {
        auto& q1 = doc.questions[0];
        auto& q2 = doc.questions[1];
        auto& q3 = doc.questions[2];
        expect(q1.type == L"单选", "Q1 题型=单选");
        expect(q1.options.size() == 4, "Q1 有 4 个选项");
        expect(q1.answer.size() == 1 && q1.answer[0] == L"A", "Q1 答案=A");
        expect(IsCorrect(q1, { L"A" }), "Q1 选A判定正确");
        expect(!IsCorrect(q1, { L"B" }), "Q1 选B判定错误");
        expect(q2.type == L"多选", "Q2 题型=多选");
        expect(q2.answer.size() == 3, "Q2 答案含3个字母");
        expect(IsCorrect(q2, { L"A", L"B", L"D" }), "Q2 选ABD判定正确");
        expect(!IsCorrect(q2, { L"A", L"B" }), "Q2 少选判定错误");
        expect(q3.type == L"判断", "Q3 题型=判断");
        expect(q3.answer.size() == 1 && q3.answer[0] == L"A", "Q3 判断答案=A(正确)");
        // 选项位置固定 A–E
        bool ordered = true;
        for (auto& q : doc.questions) {
            for (size_t i = 0; i < q.options.size(); i++) {
                wchar_t expectKey = (wchar_t)(L'A' + i);
                if (q.options[i].key != std::wstring(1, expectKey)) ordered = false;
            }
        }
        expect(ordered, "各题选项按 A/B/C/D 顺序排列");
        // 解析非空
        expect(!q1.explain.empty() && !q2.explain.empty() && !q3.explain.empty(), "三题均有解析");
    }

    // UTF-8 落盘，供外部工具确证 CJK 解码无误（控制台代码页问题不在此断言）
    {
        auto W2U = [](const std::wstring& s) -> std::string {
            std::string o; for (wchar_t c : s) {
                if (c < 0x80) o.push_back((char)c);
                else if (c < 0x800) { o.push_back((char)(0xC0 | (c >> 6))); o.push_back((char)(0x80 | (c & 0x3F))); }
                else { o.push_back((char)(0xE0 | (c >> 12))); o.push_back((char)(0x80 | ((c >> 6) & 0x3F))); o.push_back((char)(0x80 | (c & 0x3F))); }
            }
            return o;
        };
        FILE* d = fopen("tools/quiz_dump.txt", "wb");
        if (d) {
            fputs("\xEF\xBB\xBF", d); // BOM
            std::string h = "title: " + W2U(doc.title) + "\n"; fputs(h.c_str(), d);
            for (auto& q : doc.questions) {
                std::string b = "Q" + std::to_string(q.id) + " [" + W2U(q.type) + "] " + W2U(q.stem) + "\n";
                fputs(b.c_str(), d);
                for (auto& o : q.options) { std::string ol = "  " + W2U(o.key) + ". " + W2U(o.text) + "\n"; fputs(ol.c_str(), d); }
                std::string a = "  答案: "; for (auto& x : q.answer) a += W2U(x) + " "; a += "\n"; fputs(a.c_str(), d);
                std::string e = "  解析: " + W2U(q.explain) + "\n"; fputs(e.c_str(), d);
            }
            fclose(d);
        }
    }

    printf("\n=== 结果: %s ===\n", g_fail == 0 ? "ALL PASS" : "HAS FAILURES");
    return g_fail == 0 ? 0 : 1;
}
