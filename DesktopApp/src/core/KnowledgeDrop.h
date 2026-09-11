#pragma once
// ============================================================
//  KnowledgeDrop.h — F-D5+ 知识库页内拖拽收集（OLE IDropTarget）
//  此前拖放目标只注册在右下角专注浮窗上，而浮窗默认隐藏
//  （看板娘悬停才出现）→ 知识库页写着「拖入收集」实际拖不进来。
//  现在把主窗口也注册为放目标：
//    · 浏览器 / PDF 里选中的文字 → 直接拖进软件窗口即建卡
//    · 资源管理器里的 .md / .txt 文件 → 读正文建卡（≤64KB）
//    · 其他文件 → 记录文件路径
//  反馈三件套：悬停高亮（KnowledgeDragActive，KnowledgeView 画
//  蒙层）+ 页内 Toast + 托盘气泡；收集成功后 KnowledgeRev() +1，
//  KnowledgeView 轮询版本号即时刷新列表。
// ============================================================
#include <windows.h>

namespace lj {

void RegisterKnowledgeDrop(HWND hwnd);   // 主窗口初始化时调用一次（非 shot 模式）
void UnregisterKnowledgeDrop(HWND hwnd); // 退出时对应释放

bool KnowledgeDragActive();              // 当前是否有拖拽悬停在窗口上
long long KnowledgeRev();                // 收集版本号（每次成功收集 +1）

} // namespace lj
