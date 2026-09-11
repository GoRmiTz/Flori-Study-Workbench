# 01 · 芙洛理架构设计 · 原生壳 + Godot 集成与 IPC

> 本文件定义「芙洛理」的整体架构：**原生 C++ 壳（数据/状态核心，零依赖） + Godot 4 子进程（动效/游戏性体验层）**，二者通过本地 IPC 通信。这是相对芙洛理"全 D2D 自绘"的**重新设计**核心。

---

## 1. 设计原则

1. **数据/状态核心留在原生侧**（或服务端）：打卡、专注、复盘、错题集、账户、云端同步——这些业务逻辑与持久化不进 Godot。
2. **Godot 只做"演 + 收 + 回传"**：渲染动效、捕获交互、把"标记结果"回传原生侧落库。
3. **Godot 不持有任何业务数据**：题库来自原生的 QuizParser；标记结果回传原生写 ReviewEngine / 错题集。
4. **零依赖红线仅约束原生壳**：Godot 作为按需启动的**外部二进制**（MIT，无 royalty），不算"依赖进主程序"。
5. **可移除**：Godot 体验包删掉，核心功能毫发无损。

## 2. 进程模型

```
┌─────────────────────────────┐         IPC           ┌────────────────────────────┐
│   芙洛理/芙洛理 原生壳 .exe     │  行分隔 JSON           │   motion.exe (Godot 4)        │
│   （C++20 · 零依赖）          │ 127.0.0.1:7821        │   （MIT · 桌面原生导出）       │
│                              │ 或 命名管道            │                              │
│  ├ 账户 / 会话 / 口令哈希     │ ── deck ──>           │  ├ 场景：墨显 / 抽笺 /       │
│  ├ 打卡 / 专注 / 复盘 / 错题  │                      │  │        舆图 / 织缕 …       │
│  ├ QuizParser 题库           │ <─ result ──          │  ├ 纯渲染 + 交互捕获         │
│  ├ Cloud 同步                │                      │  └ 无持久化、无数据库        │
│  └ 动效入口（按需启动子进程） │                      │                              │
└─────────────────────────────┘                      └─────────────────────────────┘
        ▲                                                      │
        └──────────── 标记 → ReviewEngine / 错题集 / KPI ──────┘
```

- **启动**：用户点开某动效概念（如"抽笺"）→ 原生壳 `CreateProcess` 拉起 `motion.exe --exp=chou_jian --uid=<令牌> --port=7821`。
- **握手**：Godot 启动后发 `{"t":"ready"}`；原生侧推题库 `{"t":"deck","items":[...]}`。
- **运行**：用户交互（抽笺/显影/勘察）→ Godot 捕获标记 → 回传 `{"t":"result","qid":...,"mark":"known"|"weak","ms":...}`。
- **收尾**：用户关闭 → Godot 发 `{"t":"close"}` 或原生侧 `TerminateProcess`。

## 3. IPC 协议（最小可行，行分隔 JSON）

```
# 启动参数（原生 → Godot）
motion.exe --exp=<概念id> --uid=<账户令牌> --port=7821

# 握手
{"t":"ready"}                       # Godot → 宿主
{"t":"hello","uid":"..."}           # 宿主 → Godot（确认）

# 推题库（宿主 → Godot）
{"t":"deck","exp":"chou_jian","items":[
  {"qid":"判断-003","title":"...","body":"...","point":"知识点","difficulty":2}
]}

# 回标记（Godot → 宿主）
{"t":"result","qid":"判断-003","mark":"known"|"weak","ms":4821}

# 进度查询（可选）
{"t":"progress","known":12,"weak":3,"coverage":0.45}

# 收尾
{"t":"close"}
```

> **数据闭环（铁律㉟ 的买点）**：`result` 回传 → 原生 `QuizStore::RecordAttempt` 写薄弱项 → `ReviewEngine` 重算 → Dashboard KPI 实时更新。Godot 永远不写业务库。

## 4. 技术栈与约束

| 层 | 技术 | 约束 |
| --- | --- | --- |
| 原生壳 | C++20 / Win32 / D3D11+D2D（沿用芙洛理，可保留或精简） | 零第三方依赖（系统库白名单） |
| 数据层 | 手写 JSON（沿用 `CheckinStore`）或迁服务端 | 账户隔离 `accounts/<name>/*.json` |
| 动效层 | **Godot 4**（MIT） | 2D 无光照（默认即 flat），禁用 WorldEnvironment glow |
| 通信 | 本地 TCP / 命名管道 | 行分隔 JSON，零第三方库 |
| 视觉 | 亮色纸 `#F6F2E9` + 墨线 + 零辉光 + 青绿 `#268678` | 见 `04/视觉规范_芙洛理.md` |

## 5. 模块依赖图（芙洛理目标态）

```
[原生壳]
  AccountStore ──> Cloud(Sync/Auth)
  CheckinStore ──> (checkin/focus/items/journal/rhythm/settings)
  QuizParser/QuizStore ──> ReviewEngine ──> Dashboard KPI
  Kanban(AI对话) ──> 本地凭据文件
        │
        └── 动效入口 ──CreateProcess──> [Godot motion.exe]
                                        │  IPC deck/result
                                        └──< 回传标记 ──> ReviewEngine/错题集
```

## 6. 相对芙洛理的"重新设计"方向

| 芙洛理痛点 | 芙洛理改进 |
| --- | --- |
| D2D 手写游戏循环，布局易重叠（S2-1） | Godot 用 Container（VBox/Grid）自动布局，根治手工锚点碰撞 |
| 动效"像花瓶、缺买点" | 每个动效必须回答买点 + 回传标记（铁律㉟），接 QuizParser/ReviewEngine |
| 看板娘渲染重、专有 | Godot 用 Sprite2D 重做，更轻 |
| 暗底/墨线在 D2D 实现成本高 | Godot 2D 默认无光照，亮纸+墨线几乎是出厂态 |
| 概念原型是 web 草图 | 定型后直接用 Godot 实现，跳过"web→C++ 重写"两道工序 |

## 7. 风险与对策

| 风险 | 对策 |
| --- | --- |
| 两进程焦点/层级跳 | Godot borderless 窗口贴宿主区；必要时 `SetParent` 嵌入 |
| Godot 冷启动 1–2s | 宿主侧加"墨韵加载"过渡；或常驻一个 `motion.exe` 切场景 |
| 二进制体积 ~50–100MB | 作为"可选体验包"独立分发，核心零依赖不受影响 |
| 美学漂移 | 统一 Godot 主题（纸底 ColorRect + Line2D 墨线 + 禁用 glow），code review 把关 |
| IPC 不可靠 | 行分隔 JSON + 超时重连；标记失败本地缓存重试 |

---

*下一步：按 `02_功能模块详述` 认领模块；IPC 协议以本文件第 3 节为基线，可在 M1 阶段细化。*
