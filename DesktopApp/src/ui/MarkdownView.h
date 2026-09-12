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
//    · 表格（批次 E）：| a | b | + |---|---| 分隔行，网格绘制、单元格折行
//    · 图片（批次 E）：![说明](路径)，本地路径（相对路径基于知识库基准目录）
//
//  用法：
//      md.SetMarkdown(src);
//      float h = md.Layout(width, cv);     // 排版并取总高（供滚动）
//      md.Paint(cv, x, yTop, cullTop, cullBottom);  // 画可见带
// ============================================================
#include "ui/Canvas.h"
#include <wrl/client.h>
#include <string>
#include <vector>
#include <map>

namespace lj {

class MarkdownView
{
public:
    void SetMarkdown(const std::wstring& src);
    bool Empty() const { return m_blocks.empty(); }

    // 图片相对路径的基准目录（知识库拖入文件所在目录；空 = 仅支持绝对路径）
    void SetBasePath(const std::wstring& dir) { m_basePath = dir; }

    // 按给定宽度排版并缓存行盒；返回内容总高（DIP）。宽度变化会自动重排。
    float Layout(float width, Canvas& cv);

    // 绘制。x/yTop 为排版原点（调用方通常 yTop = -scrollY + 区块顶）；
    // cullTop/cullBottom 为内容坐标下的可见带，只画其中的行。
    void Paint(Canvas& cv, float x, float yTop, float cullTop, float cullBottom) const;

    float Height() const { return m_height; }
    float Width()  const { return m_width; }

private:
    struct Span { std::wstring text; bool bold = false; bool code = false; bool link = false; };

    enum { P_PARA = 0, P_H = 1, P_UL = 7, P_OL = 8, P_QUOTE = 9, P_CODE = 10, P_HR = 11,
           P_TABLE = 12, P_IMG = 13 };

    struct Block
    {
        int  type = P_PARA;
        int  level = 0;              // 标题级别 / 列表序号起始
        int  indent = 0;             // 列表嵌套层级（2 空格一级）
        std::vector<Span> spans;     // 行内内容（代码块/分隔线为空）
        std::vector<std::wstring> codeLines;   // 代码块逐行
        // 批次 E：表格 / 图片
        std::vector<std::vector<std::wstring>> cells;   // 表格：每行每格（纯文本）
        std::wstring imgPath, imgAlt;                   // 图片：路径 / 说明
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
        // 批次 E：表格行（整行 = 表格一逻辑行；Paint 画网格 + 每格多行文本）
        bool table = false, tableHead = false;
        std::vector<std::vector<std::wstring>> tCells;   // 每格已折行文本
        std::vector<float> tColX;                        // 列起点（相对 x）
        float tW = 0.0f;                                 // 表宽
        // 批次 E：图片行
        bool img = false;
        std::wstring imgPath, imgAlt;
        float imgH = 0.0f, imgW = 0.0f;
    };
    std::vector<Line> m_lines;
    float m_width = -1.0f;
    float m_height = 0.0f;
    std::wstring m_basePath;

    // 图片位图缓存（路径 → D2D 位图 + 像素尺寸；Layout/Paint 共用，主线程）
    struct ImgEntry { Microsoft::WRL::ComPtr<ID2D1Bitmap> bmp; float w = 0, h = 0; bool ok = false; };
    mutable std::map<std::wstring, ImgEntry> m_imgs;
    ID2D1Bitmap* LoadImage(Canvas& cv, const std::wstring& path) const;   // 缓存命中直接返回

    void ParseInline(const std::wstring& text, std::vector<Span>& out) const;
    static TextStyle StyleFor(const Block& b, const Span& s);
};

} // namespace lj
