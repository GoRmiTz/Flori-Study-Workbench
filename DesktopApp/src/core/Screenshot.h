#pragma once
// ============================================================
//  Screenshot.h — 把当前后台缓冲存成 PNG
//  用于自检渲染效果与出图，命令行 --shot <path> 触发
// ============================================================
#include "gfx/Graphics.h"

namespace lj {

bool SaveBackBufferPNG(Graphics& gfx, const std::wstring& path);

} // namespace lj
