#pragma once
// ============================================================
//  Hwnd.h — 向视图暴露主窗口句柄与 DPI
//  用于自定义管理页（#/manage）承载 Win32 EDIT 控件做中文文本编辑。
// ============================================================
#include <windows.h>
#include "ui/Theme.h"   // Palette：WM_CTLCOLOREDIT 主题化 EDIT 背景/文字需要

namespace lj {
HWND AppHwnd();
UINT  AppDpi();

// 当前调色板（含主题切换 tween 的中间值）：供 Win32 子窗口（EDIT 等）
// 在 WM_CTLCOLOREDIT 里取 paperLo / ink900 做主题化。按值返回（避免空指针时引用悬空）。
Palette AppPalette();

// D2D 浮点色 → COLORREF（0..255），供 EDIT / RichEdit 上色用
COLORREF RgbOf(const D2D1_COLOR_F& c);

// 输入框背景：每个视图在打开 EDIT / RichEdit 前，把该输入框「背后卡片的面色」设进来，
// WM_CTLCOLOREDIT 用同一色画 EDIT 背景、视图用 EM_SETBKGNDCOLOR 给 RichEdit 上同色，
// 使输入框与卡片无缝融合 —— 不再出现一块额外的亮/暗矩形盖在卡片上。
void SetEditBackdrop(const D2D1_COLOR_F& c);
const D2D1_COLOR_F& EditBackdrop();

// 计算 PaperCard(lift) 的面色（与 Canvas::PaperCard 完全一致），让输入框背景精确匹配卡片。
D2D1_COLOR_F CardFace(float lift);

// 主题：账户切换 / 导入备份后，请求应用按当前账户的 settings.json 重新取主题。
// immediate=true 直接落色板（无过渡），false 走 0.55s 交叉淡入。
void AppSyncTheme(bool immediate);
} // namespace lj
