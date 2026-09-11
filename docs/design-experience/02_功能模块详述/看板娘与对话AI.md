# 02-D · 看板娘与对话 AI（F-D11）

> ⚠️ **状态（2026-09-10）**：看板娘运行时已**整体暂停并归档**至 `DesktopApp/_attic/`（恢复步骤见 `_attic/README.md`）。本文保留架构与经验记录。**AI 凭据链路（`kanban_ai.json`）仍被练考 AI 复用**，隐私红线继续有效。

> 来源：`kanban/`（23 文件）。绿井/Lüjing 看板娘是芙洛理的陪伴层。芙洛理可保留陪伴定位，用 Godot 做更轻的 2D 呈现。

## 1. 分层架构（门面 → 后端）

```
App → Kanban(门面/状态机) → KanbanWindow(分层窗口/渲染循环/交互)
                            → MascotBackend(抽象) → PngBackend(默认零依赖静态皮套)
      + KanbanAI(DeepSeek 对话) + KanbanState + KanbanChat + KanbanSettingsDlg + KanbanMod + KanbanBus + KanbanRaise + KanbanCorpus
```

- `Kanban::Init(hInst, owner, scale)` 在 `App::Initialize` 末尾调用。
- `KanbanWindow` 主线程建分层窗口（`WS_EX_LAYERED|TOPMOST|NOACTIVATE|TOOLWINDOW`），30–60fps 定时器驱动 `Kanban::Frame` → 状态机 Tick、养成累加、主动说话。
- 渲染分层：`Compose`→`UpdateLayeredWindowIndirect(ULW_ALPHA)` 做 Per-Pixel Alpha；后端只写 premultiplied ARGB32 角色像素，气泡/窗口由窗口负责。

## 2. 商业铁律（务必保留）
- **看板娘后端可插拔**：`MascotBackend` 抽象接口；当前仅 `PngBackend`（零依赖静态皮套，WIC 解码 PNG）。未来可新增更轻的 2D/3D 后端，不影响门面与状态机。
- 商业铁律：**绝不绑定单一渲染技术**，配置驱动（`kanban.cfg`）不重编切换。
- 模型由 `assets/kanban/kanban.cfg` 配置驱动（`model`/`anchor`/`offsetX/Y`），改文件重启即生效、不重编；`anchor` 默认 bottom-right（非写死）。

## 3. AI 对话（KanbanAI）
- `KanbanAI` 是 OpenAI 兼容 `/chat/completions` 客户端（`winhttp` 零依赖）。`enabled = !apiBase.empty() && !apiKey.empty()`（双空为 false → 走 `LocalLine` 本地罐头）。
- 模型默认 `deepseek-chat`；`apiBase` 默认 `https://api.deepseek.com/v1`；双预算（时间窗 `InActiveWindow` + 每日 token `TokenExhausted`）。
- **凭据红线（隐私）**：**绝不硬编码 / 绝不入库**。实际实现经 `CheckinStore::SaveSettings` 写入 `settings.json` 的 `kanban` 段（代码侧核实）；重写时仍须遵守"本地文件、不入库"原则，且**云端回写不可覆盖真实 Key**（芙洛理曾因云端 payload 覆盖导致 AI 失效，已用本地独占文件/段覆盖解决）。
- 对话窗极简：仅输入框、回车发送、无聊天历史、头顶气泡 `SetBubble/DrawBubbleOn`；请求体只含聚合数值 `DailyContext`，断网/超预算走 `LocalLine` 兜底。

## 4. 渲染（PngBackend）
- `PngBackend` 用 WIC 解码 `assets/kanban/lvjing.png` 静态皮套，直接写 premultiplied ARGB32 角色像素（零依赖、无自建 D3D 设备）；分层窗口 `Compose`→`UpdateLayeredWindowIndirect(ULW_ALPHA)` 做 Per-Pixel Alpha。
- 视线 `SetGazeTarget`、缩放 `m_userZoom∈[0.4,3.0]`、气泡贴头 `TopBandFrac()`。
- 铁律：SEH(C2712) 含析构对象函数禁 `__try/__except`；缩放须同比例重建超采样画布；拖拽全程屏幕坐标；`WM_NCHITTEST` 先 `ScreenToClient`。

## 5. 给芙洛理 / Godot 的关键提示
- **位置/层级/气泡/对话** → Godot `Control/CanvasItem` 承担；看板娘渲染当前为 `PngBackend` 静态皮套，若未来用 Godot 重做优先 **Sprite2D**（MIT 友好、更轻）。
- **winhttp AI → Godot `HTTPClient`**；凭据仍走本地文件、不入库。
- **看板娘设置面板** `KanbanSettingsDlg` 自绘无系统观感 → Godot 用 `Control` + 主题。
- 商业铁律（可移除付费层、可插拔后端、配置驱动不重编）**务必在芙洛理延续**，避免绑定单一渲染技术。
