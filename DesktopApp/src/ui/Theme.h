#pragma once
// ============================================================
//  Theme.h — 设计令牌「档案 · 图纸 DOSSIER」
//  1:1 移植自 WebApp/assets/css/dossier.css 的 :root 变量。
//  网页改色板时，这里同步改即可，全站生效。
// ============================================================
#include <d2d1_1.h>
#include <cstdint>

namespace lj {

// 从 #RRGGBB 十六进制构造 D2D 颜色
inline D2D1_COLOR_F Hex(uint32_t rgb, float a = 1.0f)
{
    return D2D1::ColorF(
        ((rgb >> 16) & 0xFF) / 255.0f,
        ((rgb >> 8)  & 0xFF) / 255.0f,
        ( rgb        & 0xFF) / 255.0f,
        a);
}

inline D2D1_COLOR_F WithAlpha(const D2D1_COLOR_F& c, float a)
{
    return D2D1::ColorF(c.r, c.g, c.b, a);
}

inline D2D1_COLOR_F MixColor(const D2D1_COLOR_F& a, const D2D1_COLOR_F& b, float t)
{
    return D2D1::ColorF(a.r + (b.r - a.r) * t,
                        a.g + (b.g - a.g) * t,
                        a.b + (b.b - a.b) * t,
                        a.a + (b.a - a.a) * t);
}

// ---------------- 色板 ----------------
struct Palette
{
    // 墨色 Ink
    D2D1_COLOR_F ink900, ink700, ink500, ink300, ink100;
    // 纸基 Paper
    D2D1_COLOR_F paper, paperHi, paperLo, paperDeep;
    // 制图线 Rule
    D2D1_COLOR_F rule, ruleStrong;
    float        ruleHairAlpha;   // 极细分隔线透明度
    float        ruleGridAlpha;   // 背景网格透明度
    // 标记色 Marks
    D2D1_COLOR_F seal, sealLo, sealWash;      // 朱砂印 · 主色
    D2D1_COLOR_F brass, brassWash;            // 黄铜 · 标注
    D2D1_COLOR_F jade, jadeWash;              // 青 · 完成
    D2D1_COLOR_F vermilion, vermWash;         // 朱 · 警示

    bool dark;
};

// 浅色（默认）：正在被填写的纸质档案
inline Palette LightPalette()
{
    Palette p{};
    p.ink900 = Hex(0x17140f);
    p.ink700 = Hex(0x332d24);
    p.ink500 = Hex(0x6a6154);
    p.ink300 = Hex(0x9b9384);
    p.ink100 = Hex(0xcfc8b8);

    p.paper     = Hex(0xf2efe6);
    p.paperHi   = Hex(0xfbfaf5);
    p.paperLo   = Hex(0xe7e2d5);
    p.paperDeep = Hex(0xdcd6c6);

    p.rule       = Hex(0xd5cebd);
    p.ruleStrong = Hex(0xb3a992);
    p.ruleHairAlpha = 0.07f;
    p.ruleGridAlpha = 0.055f;

    p.seal      = Hex(0x6B2A35);
    p.sealLo    = Hex(0x8f4450);
    p.sealWash  = Hex(0xede0e0);
    p.brass     = Hex(0x9c7326);
    p.brassWash = Hex(0xf2ead6);
    p.jade      = Hex(0x3b6448);
    p.jadeWash  = Hex(0xe3ebe2);
    p.vermilion = Hex(0xb23c22);
    p.vermWash  = Hex(0xf4e2da);

    p.dark = false;
    return p;
}

// 深色：夜间档案室（对应 style.css 的 [data-theme=dark]）
inline Palette DarkPalette()
{
    Palette p{};
    p.ink900 = Hex(0xf3eee2);
    p.ink700 = Hex(0xd9d2c2);
    p.ink500 = Hex(0xa59c8c);
    p.ink300 = Hex(0x6f6759);
    p.ink100 = Hex(0x453f34);

    p.paper     = Hex(0x1a1714);
    p.paperHi   = Hex(0x242019);
    p.paperLo   = Hex(0x14120f);
    p.paperDeep = Hex(0x0f0d0b);

    p.rule       = Hex(0x3a342a);
    p.ruleStrong = Hex(0x554d3e);
    p.ruleHairAlpha = 0.16f;
    p.ruleGridAlpha = 0.075f;

    p.seal      = Hex(0xc98a93);
    p.sealLo    = Hex(0xd8a7ae);
    p.sealWash  = Hex(0x34262a);
    p.brass     = Hex(0xd8ab5e);
    p.brassWash = Hex(0x3a2f1c);
    p.jade      = Hex(0x79b58c);
    p.jadeWash  = Hex(0x233127);
    p.vermilion = Hex(0xe08b76);
    p.vermWash  = Hex(0x3a2620);

    p.dark = true;
    return p;
}

// ---------------- 形制 Shape ----------------
namespace shape {
    constexpr float kGridUnit  = 8.0f;    // 版式基本单位
    constexpr float kGridMajor = 64.0f;   // 主网格
    constexpr float kEdge      = 4.0f;    // 文件卡微圆角
    constexpr float kEdgeSoft  = 2.0f;    // 编号签
    constexpr float kEdgeLarge = 7.0f;    // 弹窗
    constexpr float kHair      = 1.0f;    // 发丝线
    constexpr float kStroke    = 1.5f;    // 常规描边
    constexpr float kTitleBar  = 44.0f;   // 自定义标题栏高度
    constexpr float kTopNav    = 52.0f;   // 档案条导航高度
    constexpr float kMaxWidth  = 1080.0f; // 内容最大宽度
}

// ---------------- 字体 ----------------
namespace font {
    // DirectWrite 按族名查找；写多个候选由 Canvas 逐个探测。
    // 授权策略：候选只含 OFL 开源字体（思源/Noto，用户自行安装则优先采用）
    // 与 Windows 系统自带字体（随系统授权，不随本仓库分发），不引入任何
    // 需独立授权的第三方字体。仓库本身不含任何字体文件。
    inline const wchar_t* kSerifCandidates[] = {
        L"Noto Serif SC", L"Source Han Serif SC", L"Songti SC", L"SimSun", L"宋体"
    };
    inline const wchar_t* kSansCandidates[] = {
        L"Noto Sans SC", L"Source Han Sans SC",
        L"Microsoft YaHei UI", L"Microsoft YaHei", L"微软雅黑"
    };
    inline const wchar_t* kMonoCandidates[] = {
        L"JetBrains Mono", L"Cascadia Mono", L"Consolas", L"Courier New"
    };
}

// ---------------- 时长 Duration（秒）----------------
namespace dur {
    constexpr float kFast   = 0.14f;   // 微反馈
    constexpr float kBase   = 0.24f;   // 常规控件
    constexpr float kSlow   = 0.42f;   // 面板展开
    constexpr float kScene  = 0.72f;   // 场景转场
}


// ---- type scale: 1:1.25 modular ratio ----
namespace type_scale {
    constexpr float kXS   = 10.0f;   // footnote, meta
    constexpr float kSM   = 12.5f;   // secondary
    constexpr float kBase = 14.0f;   // body
    constexpr float kMD   = 16.0f;   // emphasized
    constexpr float kLG   = 20.0f;   // section header
    constexpr float kXL   = 26.0f;   // page title
    constexpr float k2XL  = 34.0f;   // hero
    constexpr float k3XL  = 44.0f;   // display
}
} // namespace lj
