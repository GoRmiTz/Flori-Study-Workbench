// ============================================================
//  MarkdownView.cpp — 轻量 Markdown 阅读视图实现
//  解析（SetMarkdown）→ 排版（Layout，缓存行盒）→ 绘制（Paint，
//  支持按可见带剔除）。全部走 Canvas 的 DirectWrite 文本，无第三方依赖。
// ============================================================
#include "ui/MarkdownView.h"
#include "core/Common.h"
#include <wincodec.h>
#include <cctype>

#pragma comment(lib, "windowscodecs.lib")

namespace lj {

// ---------------- 解析 ----------------
void MarkdownView::ParseInline(const std::wstring& text, std::vector<Span>& out) const
{
    out.clear();
    std::wstring cur;
    auto flush = [&]() {
        if (!cur.empty()) { out.push_back({ cur, false, false, false }); cur.clear(); }
    };
    for (size_t i = 0; i < text.size(); ) {
        // 行内代码 `...`
        if (text[i] == L'`') {
            size_t e = text.find(L'`', i + 1);
            if (e != std::wstring::npos) {
                flush();
                if (e > i + 1) out.push_back({ text.substr(i + 1, e - i - 1), false, true, false });
                i = e + 1;
                continue;
            }
        }
        // 粗体 **...**（也吃 __...__）
        if ((text[i] == L'*' && i + 1 < text.size() && text[i + 1] == L'*') ||
            (text[i] == L'_' && i + 1 < text.size() && text[i + 1] == L'_')) {
            wchar_t m = text[i];
            size_t e = text.find(std::wstring(2, m), i + 2);
            if (e != std::wstring::npos) {
                flush();
                if (e > i + 2) out.push_back({ text.substr(i + 2, e - i - 2), true, false, false });
                i = e + 2;
                continue;
            }
        }
        // 斜体 *...*（单星）
        if (text[i] == L'*' && i + 1 < text.size() && text[i + 1] != L' ' && text[i + 1] != L'*') {
            size_t e = text.find(L'*', i + 1);
            if (e != std::wstring::npos && e > i + 1) {
                flush();
                // 斜体以「弱化色」近似（TextStyle 无斜体位），标记为 link 色系
                out.push_back({ text.substr(i + 1, e - i - 1), false, false, true });
                i = e + 1;
                continue;
            }
        }
        // 链接 [text](url)
        if (text[i] == L'[') {
            size_t close = text.find(L']', i + 1);
            if (close != std::wstring::npos && close + 1 < text.size() && text[close + 1] == L'(') {
                size_t pe = text.find(L')', close + 2);
                if (pe != std::wstring::npos) {
                    flush();
                    std::wstring label = text.substr(i + 1, close - i - 1);
                    if (!label.empty()) out.push_back({ label, false, false, true });
                    i = pe + 1;
                    continue;
                }
            }
        }
        cur += text[i];
        ++i;
    }
    flush();
}

void MarkdownView::SetMarkdown(const std::wstring& src)
{
    m_blocks.clear();
    m_lines.clear();
    m_width = -1.0f;
    m_height = 0.0f;

    // 按行切（兼容 \r\n）
    std::vector<std::wstring> lines;
    {
        std::wstring cur;
        for (wchar_t c : src) {
            if (c == L'\n') { lines.push_back(cur); cur.clear(); }
            else if (c != L'\r') cur += c;
        }
        lines.push_back(cur);
    }

    Block para; bool paraOpen = false;
    auto closePara = [&]() {
        if (paraOpen && !para.spans.empty()) m_blocks.push_back(para);
        para = Block(); paraOpen = false;
    };
    auto addLineToPara = [&](const std::wstring& s) {
        std::vector<Span> sp;
        ParseInline(s, sp);
        if (!para.spans.empty()) para.spans.push_back({ L" ", false, false, false });
        para.spans.insert(para.spans.end(), sp.begin(), sp.end());
        paraOpen = true;
    };

    bool inCode = false;
    Block code;

    for (const auto& raw0 : lines) {
        // 去行尾空格便于匹配
        std::wstring raw = raw0;
        while (!raw.empty() && (raw.back() == L' ' || raw.back() == L'\t')) raw.pop_back();

        if (inCode) {
            if (raw.size() >= 3 && raw.substr(0, 3) == L"```") {
                inCode = false;
                if (!code.codeLines.empty()) m_blocks.push_back(code);
                code = Block();
            } else {
                code.codeLines.push_back(raw);
            }
            continue;
        }
        if (raw.size() >= 3 && raw.substr(0, 3) == L"```") {
            closePara();
            inCode = true;
            code = Block(); code.type = P_CODE;
            continue;
        }
        if (raw.empty()) { closePara(); continue; }

        // 批次 E：图片 ![alt](path)（独占一行）
        if (raw[0] == L'!' && raw.size() > 2 && raw[1] == L'[') {
            size_t close = raw.find(L']', 2);
            if (close != std::wstring::npos && close + 1 < raw.size() && raw[close + 1] == L'(') {
                size_t pe = raw.find(L')', close + 2);
                if (pe != std::wstring::npos) {
                    closePara();
                    Block b; b.type = P_IMG;
                    b.imgAlt  = raw.substr(2, close - 2);
                    b.imgPath = raw.substr(close + 2, pe - close - 2);
                    m_blocks.push_back(b);
                    continue;
                }
            }
        }

        // 批次 E：表格 —— 当前行是分隔行（|---|---|）且**当前 para**（表头行）含 |：
        // 收割 para 作表头、开表格块；随后连续 | 行作为数据行追加
        {
            bool isSep = raw.size() >= 3 && raw.find(L'|') != std::wstring::npos;
            if (isSep) {
                for (wchar_t c : raw) if (c != L'-' && c != L'|' && c != L':' && c != L' ') { isSep = false; break; }
            }
            auto splitRow = [](const std::wstring& line, std::vector<std::wstring>& out) {
                std::wstring s = line;
                while (!s.empty() && (s.front() == L'|' || s.front() == L' ')) s.erase(s.begin());
                while (!s.empty() && (s.back() == L'|' || s.back() == L' ')) s.pop_back();
                out.clear();
                std::wstring cur;
                for (wchar_t c : s) {
                    if (c == L'|') { out.push_back(cur); cur.clear(); }
                    else cur += c;
                }
                out.push_back(cur);
            };
            if (isSep && paraOpen && para.spans.size() == 1
                && para.spans[0].text.find(L'|') != std::wstring::npos) {
                std::vector<std::wstring> head;
                splitRow(para.spans[0].text, head);
                if (!head.empty()) {
                    paraOpen = false; para = Block();
                    Block t; t.type = P_TABLE;
                    t.cells.push_back(head);
                    m_blocks.push_back(t);
                    continue;
                }
            }
            // 表格数据行：上一块是表格 → 追加一行
            if (!m_blocks.empty() && m_blocks.back().type == P_TABLE && raw.find(L'|') != std::wstring::npos) {
                std::vector<std::wstring> row;
                splitRow(raw, row);
                if (!row.empty()) { m_blocks.back().cells.push_back(row); continue; }
            }
        }

        // 分隔线 --- *** ___
        {
            bool allSame = raw.size() >= 3;
            for (wchar_t c : raw) if (c != L'-' && c != L'*' && c != L'_') { allSame = false; break; }
            if (allSame && (raw[0] == L'-' || raw[0] == L'*' || raw[0] == L'_')) {
                closePara();
                Block b; b.type = P_HR;
                m_blocks.push_back(b);
                continue;
            }
        }

        // 标题
        if (raw[0] == L'#') {
            size_t h = 0;
            while (h < raw.size() && raw[h] == L'#' && h < 6) ++h;
            if (h < raw.size() && raw[h] == L' ') {
                closePara();
                Block b; b.type = P_H; b.level = (int)h;
                std::vector<Span> sp;
                ParseInline(raw.substr(h + 1), sp);
                b.spans = std::move(sp);
                m_blocks.push_back(b);
                continue;
            }
        }

        // 引用 >（连续行合并为一个块，行间以空格续接）
        if (raw[0] == L'>') {
            closePara();
            std::wstring inner = raw.substr(1);
            if (!inner.empty() && inner[0] == L' ') inner.erase(0, 1);
            // 与上一个引用块续接
            if (!m_blocks.empty() && m_blocks.back().type == P_QUOTE) {
                std::vector<Span> sp;
                ParseInline(inner, sp);
                auto& last = m_blocks.back();
                last.spans.push_back({ L" ", false, false, false });
                last.spans.insert(last.spans.end(), sp.begin(), sp.end());
            } else {
                Block b; b.type = P_QUOTE;
                ParseInline(inner, b.spans);
                m_blocks.push_back(b);
            }
            continue;
        }

        // 列表（含缩进嵌套：前导空格每 2 个一级）
        {
            size_t sp2 = 0;
            while (sp2 < raw.size() && raw[sp2] == L' ') ++sp2;
            std::wstring body = raw.substr(sp2);
            size_t d = 0;
            while (d < body.size() && iswdigit((wint_t)body[d])) ++d;
            bool ul = (body.size() >= 2 && (body[0] == L'-' || body[0] == L'*') && body[1] == L' ');
            bool ol = (d + 1 < body.size() && body[d] == L'.' && body[d + 1] == L' ');
            if (ul || ol) {
                closePara();
                Block b; b.type = ul ? P_UL : P_OL;
                b.indent = (int)(sp2 / 2);
                if (ol) {
                    b.level = _wtoi(body.c_str());
                    body = body.substr(d + 2);
                } else {
                    body = body.substr(2);
                }
                ParseInline(body, b.spans);
                m_blocks.push_back(b);
                continue;
            }
        }

        addLineToPara(raw);
    }
    if (inCode && !code.codeLines.empty()) m_blocks.push_back(code);
    closePara();
}

// 批次 E：加载本地图片 → ID2D1Bitmap（缓存；相对路径基于 m_basePath）
ID2D1Bitmap* MarkdownView::LoadImage(Canvas& cv, const std::wstring& path) const
{
    auto it = m_imgs.find(path);
    if (it != m_imgs.end()) return it->second.ok ? it->second.bmp.Get() : nullptr;

    ImgEntry e;
    // 解析路径：绝对路径直用；相对路径 join 基准目录
    std::wstring p = path;
    if (p.size() >= 2 && p[1] != L':' && p[0] != L'\\' && p[0] != L'/' && !m_basePath.empty())
        p = m_basePath + (m_basePath.back() == L'\\' ? L"" : L"\\") + p;
    // 网络地址等直接放弃
    if (p.rfind(L"http://", 0) == 0 || p.rfind(L"https://", 0) == 0) {
        m_imgs[path] = e;
        return nullptr;
    }

    ComPtr<IWICImagingFactory> wic;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&wic)))) {
        ComPtr<IWICBitmapDecoder> dec;
        if (SUCCEEDED(wic->CreateDecoderFromFilename(p.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnLoad, &dec))) {
            ComPtr<IWICBitmapFrameDecode> frame;
            if (SUCCEEDED(dec->GetFrame(0, &frame))) {
                UINT pw = 0, ph = 0;
                if (SUCCEEDED(frame->GetSize(&pw, &ph)) && pw > 0 && ph > 0) {
                    if (SUCCEEDED(cv.DC()->CreateBitmapFromWicBitmap(frame.Get(), nullptr, &e.bmp))) {
                        e.w = (float)pw;
                        e.h = (float)ph;
                        e.ok = true;
                    }
                }
            }
        }
    }
    m_imgs[path] = e;
    return e.ok ? e.bmp.Get() : nullptr;
}

// ---------------- 样式 ----------------
TextStyle MarkdownView::StyleFor(const Block& b, const Span& s)
{
    TextStyle t;
    switch (b.type) {
    case P_H:
        switch (b.level) {
        case 1: t.role = FontRole::Serif; t.size = 25.0f; t.weight = DWRITE_FONT_WEIGHT_BOLD; break;
        case 2: t.role = FontRole::Serif; t.size = 21.0f; t.weight = DWRITE_FONT_WEIGHT_BOLD; break;
        case 3: t.role = FontRole::Serif; t.size = 18.0f; t.weight = DWRITE_FONT_WEIGHT_BOLD; break;
        default: t.role = FontRole::Sans; t.size = 15.5f; t.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        }
        break;
    case P_UL: case P_OL:
        t.size = 13.5f;
        if (s.bold) t.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        if (s.code) t.role = FontRole::Mono, t.size = 12.5f;
        break;
    case P_QUOTE:
        t.size = 13.0f;
        if (s.bold) t.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        if (s.code) t.role = FontRole::Mono, t.size = 12.0f;
        break;
    default:
        t.size = 13.5f;
        if (s.bold) t.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        if (s.code) t.role = FontRole::Mono, t.size = 12.5f;
        break;
    }
    return t;
}

// ---------------- 排版 ----------------
float MarkdownView::Layout(float width, Canvas& cv)
{
    if (width <= 4.0f) return m_height;
    if (m_width == width && !m_lines.empty()) return m_height;

    m_lines.clear();
    m_width = width;
    float y = 0.0f;

    for (const auto& b : m_blocks) {
        int bi = (int)(&b - m_blocks.data());
        if (b.type == P_HR) {
            Line ln; ln.hr = true; ln.y = y + 6.0f; ln.h = 8.0f; ln.blockIdx = bi;
            m_lines.push_back(ln);
            y = ln.y + 8.0f + 10.0f;
            continue;
        }
        // 批次 E：图片
        if (b.type == P_IMG) {
            void* bmp = LoadImage(cv, b.imgPath);
            float iw = 0, ih = 0;
            if (bmp) {
                iw = m_imgs[b.imgPath].w;
                ih = m_imgs[b.imgPath].h;
            }
            float maxW = width - 24.0f;
            float dh = 90.0f, dw = maxW;   // 加载失败占位
            if (bmp && iw > 1 && ih > 1) {
                dw = (std::min)(maxW, iw);
                dh = dw * (ih / iw);
                if (dh > 520.0f) { dh = 520.0f; dw = dh * (iw / ih); }
            }
            y += 8.0f;
            Line ln; ln.img = true; ln.imgPath = b.imgPath; ln.imgAlt = b.imgAlt;
            ln.imgW = dw; ln.imgH = dh;
            ln.y = y; ln.h = dh + (b.imgAlt.empty() ? 0.0f : 20.0f); ln.blockIdx = bi;
            m_lines.push_back(ln);
            y = ln.y + ln.h + 10.0f;
            continue;
        }
        // 批次 E：表格（每逻辑行一个 Line；Paint 画网格与多行单元格）
        if (b.type == P_TABLE && !b.cells.empty()) {
            const size_t cols = (size_t)(std::max)(1,
                (int)std::max_element(b.cells.begin(), b.cells.end(),
                    [](const std::vector<std::wstring>& a, const std::vector<std::wstring>& c)
                    { return a.size() < c.size(); })->size());
            TextStyle cs; cs.size = 12.5f;
            // 列宽：各列最大内容宽，总宽超出可用宽则等比压到可用宽（下限 64）
            std::vector<float> colW(cols, 0.0f);
            for (const auto& row : b.cells)
                for (size_t c = 0; c < cols; ++c) {
                    const std::wstring& txt = c < row.size() ? row[c] : L"";
                    float w = cv.MeasureWidth(txt.empty() ? std::wstring(L"字") : txt, cs) + 16.0f;
                    if (w > colW[c]) colW[c] = w;
                }
            float total = 0.0f;
            for (float w : colW) total += w;
            const float avail = width - 20.0f;
            if (total > avail) {
                float k = avail / total;
                for (auto& w : colW) w = (std::max)(64.0f, w * k);
                total = 0.0f;
                for (float w : colW) total += w;
            } else if (total < avail * 0.999f) {
                // 窄表撑满可用宽（观感更整）
                float k = avail / total;
                for (auto& w : colW) w *= k;
                total = avail;
            }
            std::vector<float> colX(cols + 1, 0.0f);
            colX[0] = 10.0f;
            for (size_t c = 0; c < cols; ++c) colX[c + 1] = colX[c] + colW[c];

            y += 8.0f;
            for (size_t r = 0; r < b.cells.size(); ++r) {
                const auto& row = b.cells[r];
                // 每格折行
                std::vector<std::vector<std::wstring>> cellsWrap(cols);
                int maxLines = 1;
                for (size_t c = 0; c < cols; ++c) {
                    const std::wstring& txt = c < row.size() ? row[c] : L"";
                    float cw = colW[c] - 14.0f;
                    std::wstring run;
                    auto& out = cellsWrap[c];
                    for (wchar_t ch : txt) {
                        std::wstring one(1, ch);
                        if (cv.MeasureWidth(run + one, cs) > cw && !run.empty()) {
                            out.push_back(run); run.clear();
                        }
                        run += ch;
                    }
                    if (!run.empty() || out.empty()) out.push_back(run);
                    if ((int)out.size() > maxLines) maxLines = (int)out.size();
                }
                Line ln; ln.table = true; ln.tableHead = (r == 0);
                ln.tCells = std::move(cellsWrap);
                ln.tColX = colX;
                ln.tW = total;
                ln.y = y; ln.h = (float)maxLines * 19.0f + 10.0f;
                ln.blockIdx = bi;
                m_lines.push_back(ln);
                y += ln.h;
            }
            y += 12.0f;
            continue;
        }
        if (b.type == P_CODE) {
            y += 6.0f;
            for (const auto& cl : b.codeLines) {
                Line ln; ln.codeText = cl; ln.xOff = 12.0f; ln.h = 19.0f;
                ln.y = y; ln.blockIdx = bi;
                m_lines.push_back(ln);
                y += 19.0f;
            }
            y += 10.0f;
            continue;
        }

        // 行内块：样式 / 缩进 / 行高
        TextStyle base = StyleFor(b, Span{});
        float lh, mt, mb, xOff = (float)b.indent * 18.0f;
        bool quote = (b.type == P_QUOTE);
        if (quote) xOff += 26.0f;
        switch (b.type) {
        case P_H: {
            static const float lhT[7] = { 0, 36, 30, 27, 24, 22, 21 };
            static const float mtT[7] = { 0, 18, 16, 14, 12, 10, 10 };
            lh = lhT[(std::min)(6, (std::max)(1, b.level))];
            mt = mtT[(std::min)(6, (std::max)(1, b.level))];
            mb = 8.0f;
            break;
        }
        case P_QUOTE: lh = 22.0f; mt = 6.0f; mb = 8.0f; break;
        case P_UL: case P_OL: lh = 22.0f; mt = 3.0f; mb = 3.0f; break;
        default:              lh = 22.0f; mt = 2.0f; mb = 8.0f; break;
        }
        (void)base;

        // 前缀（列表符号）并入首行
        std::vector<Span> spans = b.spans;
        if (b.type == P_UL && !spans.empty()) spans.insert(spans.begin(), { L"• ", false, false, false });
        if (b.type == P_OL && !spans.empty()) spans.insert(spans.begin(),
            { std::to_wstring(b.level) + L". ", false, false, false });

        // 可用宽
        float avail = width - xOff - (quote ? 10.0f : 0.0f);
        if (avail < 40.0f) avail = 40.0f;

        // 逐字符断行（中英文通用）
        std::vector<Frag> cur;
        float cx = 0.0f;
        bool firstLine = true;
        auto emitCur = [&]() {
            Line ln; ln.frags = cur; ln.xOff = xOff + (quote ? 10.0f : 0.0f);
            ln.h = lh; ln.y = y + (firstLine ? mt : 0.0f); ln.quote = quote;
            ln.blockIdx = bi;
            m_lines.push_back(ln);
            y = ln.y + lh;
            firstLine = false;
            cur.clear(); cx = 0.0f;
        };
        auto flushRun = [&](std::vector<Frag>& into, const Span& s, std::wstring& run) {
            if (run.empty()) return;
            Frag f; f.si = (int)(&s - spans.data()); f.text = run;
            f.code = s.code; f.bold = s.bold; f.link = s.link;
            into.push_back(f);
            run.clear();
        };

        for (size_t si = 0; si < spans.size(); ++si) {
            const auto& s = spans[si];
            TextStyle st = StyleFor(b, s);
            std::wstring run;
            for (wchar_t ch : s.text) {
                std::wstring one(1, ch);
                float w = cv.MeasureWidth(one, st);
                if (cx + w > avail && cx > 0.0f) {
                    flushRun(cur, s, run);
                    emitCur();
                }
                run += ch;
                cx += w;
            }
            flushRun(cur, s, run);
        }
        emitCur();
        y += mb;
    }

    m_height = y + 10.0f;
    return m_height;
}

// ---------------- 绘制 ----------------
void MarkdownView::Paint(Canvas& cv, float x, float yTop, float cullTop, float cullBottom) const
{
    const auto& pal = cv.Pal();
    for (const auto& ln : m_lines) {
        float ay = yTop + ln.y;
        if (ay + ln.h < cullTop || ay > cullBottom) continue;

        if (ln.hr) {
            cv.PerforationH(x + 2.0f, x + m_width - 2.0f, ay + 4.0f, WithAlpha(pal.ruleStrong, 0.6f));
            continue;
        }
        // 批次 E：图片
        if (ln.img) {
            ID2D1Bitmap* bmp = LoadImage(cv, ln.imgPath);
            if (bmp) {
                cv.DrawBitmap(bmp,
                              { x + 12.0f, ay, x + 12.0f + ln.imgW, ay + ln.imgH }, 1.0f);
                cv.StrokeRoundRect({ x + 12.0f, ay, x + 12.0f + ln.imgW, ay + ln.imgH },
                                   2.0f, WithAlpha(pal.rule, 0.8f), shape::kHair);
            } else {
                cv.FillRoundRect({ x + 12.0f, ay, x + 12.0f + ln.imgW, ay + ln.imgH },
                                 4.0f, WithAlpha(pal.ink900, 0.05f));
                TextStyle t; t.size = 12.0f; t.hAlign = HAlign::Center; t.vAlign = VAlign::Middle;
                cv.Text(L"图片加载失败：" + ln.imgPath,
                        { x + 12.0f, ay, x + 12.0f + ln.imgW, ay + ln.imgH }, t, pal.ink300);
            }
            if (!ln.imgAlt.empty()) {
                TextStyle at; at.size = 11.0f; at.hAlign = HAlign::Center; at.vAlign = VAlign::Middle;
                cv.Text(ln.imgAlt, { x + 12.0f, ay + ln.imgH, x + 12.0f + ln.imgW, ay + ln.h },
                        at, pal.ink300);
            }
            continue;
        }
        // 批次 E：表格行（网格 + 多行单元格）
        if (ln.table) {
            // 背板（表头加深）
            cv.FillRect({ x + ln.tColX.front(), ay, x + ln.tColX.front() + ln.tW, ay + ln.h },
                        WithAlpha(pal.ink900, ln.tableHead ? 0.07f : 0.03f));
            // 每格文本
            TextStyle t; t.size = 12.5f; t.vAlign = VAlign::Top;
            if (ln.tableHead) t.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            size_t cols = ln.tColX.size() - 1;
            for (size_t c = 0; c < cols; ++c) {
                float cxx = x + ln.tColX[c] + 7.0f;
                float cyy = ay + 5.0f;
                if (c < ln.tCells.size())
                    for (const auto& wl : ln.tCells[c]) {
                        cv.Text(wl, { cxx, cyy, x + ln.tColX[c + 1] - 4.0f, cyy + 19.0f }, t, pal.ink900);
                        cyy += 19.0f;
                    }
            }
            // 网格：外框 + 列线 + 行底线
            float x0 = x + ln.tColX.front(), x1 = x + ln.tColX.front() + ln.tW;
            cv.StrokeRect({ x0, ay, x1, ay + ln.h }, WithAlpha(pal.ruleStrong, 0.8f), shape::kHair);
            for (size_t c = 0; c <= cols; ++c)
                if (c > 0 && c < cols)
                    cv.Line(x + ln.tColX[c], ay, x + ln.tColX[c], ay + ln.h, WithAlpha(pal.rule, 0.7f), 1.0f);
            if (!ln.tableHead)
                cv.Line(x0, ay, x1, ay, WithAlpha(pal.ruleStrong, 0.5f), 1.0f);
            continue;
        }
        if (!ln.codeText.empty()) {
            // 代码行：整宽背板 + mono 文本
            cv.FillRect({ x + 10.0f, ay, x + m_width - 10.0f, ay + ln.h },
                        WithAlpha(pal.ink900, 0.05f));
            TextStyle t; t.role = FontRole::Mono; t.size = 12.5f; t.vAlign = VAlign::Middle;
            cv.Text(ln.codeText, { x + 22.0f, ay, x + m_width - 22.0f, ay + ln.h }, t, pal.ink700);
            continue;
        }

        if (ln.quote) {
            cv.FillRect({ x + 8.0f, ay + 2.0f, x + 11.0f, ay + ln.h }, WithAlpha(pal.seal, 0.55f));
        }

        // 行样式：按块型重建（标题用标题字体），行内标记叠加
        const Block& b = (ln.blockIdx >= 0 && ln.blockIdx < (int)m_blocks.size())
                       ? m_blocks[ln.blockIdx] : m_blocks.front();
        Span dummy;

        float cx = x + ln.xOff;
        for (const auto& f : ln.frags) {
            if (f.text.empty()) continue;
            TextStyle t = StyleFor(b, dummy);
            if (f.code) { t.role = FontRole::Mono; t.size = (std::min)(t.size, 12.5f); }
            if (f.bold) t.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;

            float w = cv.MeasureWidth(f.text, t);
            if (f.code)   // 行内代码：淡背板
                cv.FillRoundRect({ cx - 3.0f, ay + 2.0f, cx + w + 3.0f, ay + ln.h - 1.0f }, 3.0f,
                                 WithAlpha(pal.ink900, 0.06f));
            D2D1_COLOR_F col = f.link ? pal.seal : (f.code ? pal.brass : pal.ink900);
            cv.Text(f.text, { cx, ay, cx + w + 2.0f, ay + ln.h }, t, col);
            cx += w;
        }
    }
}

} // namespace lj
