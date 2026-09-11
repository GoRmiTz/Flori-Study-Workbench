#pragma once
// ============================================================
//  MarkdownView.h — 轻量 Markdown 阅读视图（自研，零依赖）
//
//  与 Obsidian 的「阅读视图」对齐的渲染目标：
//    · # ~ ###### 标题（衬线，层级字号）
//    · 段落（自动折行）
//    · **粗体**、*斜体*（近似：灰度弱化）、`行内代码`（等宽+高亮色）
//    · - / * 无序列表、1. 有序列表（支持缩进嵌套）
//    · > 引用（左竖线 + 缩进）
//    · ``` 围栏代码块（背板 + 等宽逐行）
//    · --- 分隔线
//    · [文字](链接)（渲染为高亮文字；点击跳转后续接入）
//  不支持（后续按需扩展）：图片、表格、HTML 内联。
//
//  用法：
//      md.SetMarkdown(src);
//      float h = md.Layout(width, cv);     // 排版并取总高（供滚动）
//      md.Paint(cv, x, yTop, cullTop, cullBottom);  // 画可见带
// ============================================================
#include "ui/Canvas.h"
#include <string>
#include <vector>

namespace lj {

class MarkdownView
{
public:
    void SetMarkdown(const std::wstring& src);
    bool Empty() const { return m_blocks.empty(); }

    // 按给定宽度排版并缓存行盒；返回内容总高（DIP）。宽度变化会自动重排。
    float Layout(float width, Canvas& cv);

    // 绘制。x/yTop 为排版原点（调用方通常 yTop = -scrollY + 区块顶）；
    // cullTop/cullBottom 为内容坐标下的可见带，只画其中的行。
    void Paint(Canvas& cv, float x, float yTop, float cullTop, float cullBottom) const;

    float Height() const { return m_height; }
    float Width()  const { return m_width; }

private:
    struct Span { std::wstring text; bool bold = false; bool code = false; bool link = false; };

    enum { P_PARA = 0, P_H = 1, P_UL = 7, P_OL = 8, P_QUOTE = 9, P_CODE = 10, P_HR = 11 };

    struct Block
    {
        int  type = P_PARA;
        int  level = 0;              // 标题级别 / 列表序号起始
        int  indent = 0;             // 列表嵌套层级（2 空格一级）
        std::vector<Span> spans;     // 行内内容（代码块/分隔线为空）
        std::vector<std::wstring> codeLines;   // 代码块逐行
    };
    std::vector<Block> m_blocks;

    struct Frag { int si = -1; std::wstring text; bool code = false; bool bold = false; bool link = false; };
    struct Line
    {
        std::vector<Frag> frags;
        std::wstring codeText;       // 代码块行（非空 → 画背板 + mono）
        bool hr = false;
        bool quote = false;          // 行首竖线
        float xOff = 0.0f;           // 缩进
        float y = 0.0f, h = 0.0f;
        int blockIdx = -1;           // 所属块（重建样式用）
    };
    std::vector<Line> m_lines;
    float m_width = -1.0f;
    float m_height = 0.0f;

    void ParseInline(const std::wstring& text, std::vector<Span>& out) const;
    static TextStyle StyleFor(const Block& b, const Span& s);
};

} // namespace lj
