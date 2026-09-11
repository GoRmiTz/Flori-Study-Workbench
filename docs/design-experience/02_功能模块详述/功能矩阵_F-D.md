# 02-A · 功能矩阵 · 桌面端独占功能（F-D 系列）

> 来源：用户长期记忆中的「功能落点（F-D）」与 `core/`、`kanban/` 实现。这些是芙洛理区别于 WebApp 的**桌面独占能力**，芙洛理应完整继承。

| 编号 | 功能 | 现状（芙洛理实现要点） | 芙洛理重构提示 |
| --- | --- | --- | --- |
| **F-D1** | FocusTracker 专注追踪 | `core/FocusTracker.cpp`：后台线程每秒 `GetForegroundWindow`+`QueryFullProcessImageNameW` 取进程名，`GetLastInputInfo` 取 idle；每 60s 落 `focus.json`；`SetUserApps`/`SetEntApps` 分类 Study/Entertainment/Idle | 数据核心留原生；Godot 可做"专注可视化"回传 |
| **F-D2** | FloatLayer 常驻浮层+托盘+COM拖放 | `core/FloatLayer.cpp`：常驻 `WS_EX_NOACTIVATE|TOPMOST|TOOLWINDOW` 普通 `WS_POPUP`（**7.1 bug 修订：去 WS_EX_LAYERED + AW_BLEND**）；OLE `IDropTarget` 跨窗口拖入收集考点 | Windows 系统交互，保留原生侧或 GDExtension |
| **F-D3** | 全局热键 | `Ctrl+Alt+C/T/P` 截屏/计时/暂停（`App::Initialize` 注册） | 系统级热键，原生侧保留 |
| **F-D4** | 申论字数统计 | `core/DocxStat.cpp`：零依赖 `.docx` 字数（自写 mini-zip+DEFLATE `Inflate`，`CountNonSpaceChars`） | 纯算法，可复用/迁服务端 |
| **F-D5** | 拖拽收集考点 → knowledge.json | `views/KnowledgeView.cpp` + FloatLayer COM 拖放 | 数据落 `knowledge.json`，Godot 可做收集动效 |
| **F-D6** | 考场模式 | `core/ExamMode.cpp`：全屏顶部 HUD（`WS_EX_TOPMOST|TRANSPARENT|NOACTIVATE`），1s 定时器；复用 FocusTracker 做**中断检测**（3s 宽限，`interrupts++`） | 系统级全屏 HUD，原生侧保留 |
| **F-D7** | 本地复盘 + 每日 nudge | `core/ReviewEngine.cpp` + `ReviewNudge.cpp`：默认 **21:00** 托盘气泡 → `NavigateTo("review")` | 数据核心留原生；Godot 可做"复盘仪式"呈现 |
| **F-D10** | 选岗地图 | `views/AdvisorView.cpp`：CSV 导入 + 四层匹配 + 冲稳保；**仅气泡散点，绝不画国界/省界**（红线） | 信息可视化，适合 Godot 2D |
| **F-D11** | 看板娘 | ⏸️ **已暂停归档**（2026-09-10，运行时移至 `DesktopApp/_attic/`，恢复见 `_attic/README.md`）；架构经验见 `看板娘与对话AI.md` |

## 状态说明
- ✅ 已实现（芙洛理可运行）
- 芙洛理目标：**F-D 全部继承**，其中 F-D1/F-D5/F-D7/F-D10/F-D11 的"可视化/游戏化呈现"用 Godot 升级，逻辑核心留原生。

## 红线提醒
- F-D2 的"去 WS_EX_LAYERED"修订是真实踩坑结论，重做时**勿回退**。
- F-D10 **绝不绘制国界/省界**，只用气泡散点表达选岗匹配。
- F-D6 不锁系统，只做 HUD + 中断检测。
