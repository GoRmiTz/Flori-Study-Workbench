#pragma once
// ============================================================
//  Glyphs.h — 矢量播放 / 暂停图标（替代 ⏸ 表情的蓝色方块）
//  纯 D2D 绘制，随主题色板自适应；供自习室音乐卡与浮空播放器复用。
// ============================================================
#include "ui/Canvas.h"

namespace lj {

// 在 r 内居中画「播放」三角（c 填充）。轻微右偏使视觉重心居中。
void PaintPlayGlyph(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);

// 在 r 内居中画「暂停」双竖条（c 填充）。
void PaintPauseGlyph(Canvas& cv, const D2D1_RECT_F& r, const D2D1_COLOR_F& c);

// 在 r 内画一个主色(seal)圆角按钮，居中放播放/暂停矢量图标；
// showLabel=true 时在图标右侧写「播放」/「暂停」文案。
void PaintPlayPauseButton(Canvas& cv, const D2D1_RECT_F& r, bool playing,
                          bool hover, bool press, bool showLabel);

} // namespace lj
