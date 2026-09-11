# 芙洛理 Flori

> 面向考公 / 考研备考人群的常驻轻量专注桌面应用。
> 原生 Windows C++ 桌面端 + 零依赖 Node 服务端。

![status](https://img.shields.io/badge/status-%E5%BC%80%E5%8F%91%E4%B8%AD%20%7C%20in%20development-268678) ![license](https://img.shields.io/badge/license-MIT-green) ![platform](https://img.shields.io/badge/platform-Windows-blue)

> **当前状态：开发中（`v0.1.0-dev`）。** 本仓库为公开的开发快照：核心闭环已经可跑通，
> 但界面、交互与文档仍在持续打磨，版本号在首个正式 Release 前均为 `0.x.x-dev`。
> 已知限制与进行中的事项见下方 [Roadmap](#roadmap)。

---

## 项目简介

核心闭环：**打卡 → 专注 → 复盘 → 练考 → 数据分析**。

设计上强调「真实交互驱动、信息可视化优先」——动效服务于信息传达，
不做无意义的装饰。

### 架构原则

- **原生 C++ 壳（数据 / 状态核心，零依赖）** 持有全部业务状态与判分权
- **表现 / 渲染层只做「演 + 收 + 回传」，绝不持有业务数据**
- 视觉基线：浅色纸底 `#F6F2E9` + 青绿 `#268678` + 衬线标题 / 等宽数字 + 8px 栅格

### English

> **Flori** is a lightweight always-on desktop study companion for exam preparation,
> built as a native Windows C++ application with zero third-party dependencies,
> paired with a dependency-free Node server.
>
> **Status: in development (`v0.1.0-dev`).** This repository is a public development
> snapshot — the core loop works end to end, but polish, docs and packaging are
> still ongoing. See [Roadmap](#roadmap) for known limitations.

---

## 功能总览（已实现）

| 模块 | 说明 |
|---|---|
| 打卡 | 每日打卡；自定义打卡项（工作日 / 周六专项 / 周日专项）、关键倒计时、标签与排序，本地持久化 |
| 专注 | 自习室（公共 / 静音 / 冲刺）：番茄钟 + 自由计时、实时在场与公共聊天、专注白名单联动（名单外自动暂停）、静默专注覆盖层 |
| 复盘 | 每日复盘（今日小结 + 明日三件事）、历史回顾、定时提醒（系统气泡） |
| 练考 | 每日练考调度（题量 / 时刻 / 类别可配）、RSS 资讯抓取、判分在 C++ 壳内完成 |
| 数据 | 专注热力图、累计统计、成就徽章、连续里程碑、年度回顾，支持导出分享 |
| 专栏 | 私有专栏（富文本编辑器：标题 / 字号 / 加粗 / 列表 / 图片 / 链接）+ 公共专栏（浏览 / 点赞 / 收藏 / 评论，走云端） |
| 知识库 | 考点收集：窗口内拖入文字或 `.md` / `.txt` 文件即收集成卡，支持检索 |
| 数据管理 | 多账户本地存档、访客模式（不进云端）、整账户导出 / 导入备份（含回滚快照） |
| 桌面集成 | 系统托盘常驻（后台运行、右键菜单）、全局热键（打卡 / 番茄钟 / 暂停）、单实例、拖拽收集 |
| 云同步（可选） | Node 服务端：账户中心、数据同步、实时自习室、公共专栏；本地优先，不连服务端功能不阻塞 |

> 服务端**零框架零依赖**，本地优先：不启动服务端时，客户端全部核心功能照常可用。

---

## 目录结构

```
Flori-Desktop/
├── README.md              # 本文件
├── LICENSE                # 开源协议
├── .gitignore
├── DesktopApp/            # 客户端：原生 Windows C++ 桌面端（CMake）
│   ├── CMakeLists.txt     # project(FloriDesktop)
│   ├── build.bat          # 一键构建（VS BuildTools / CMake / Ninja）
│   ├── src/  Store/  assets/  mods/  tools/  docs/
├── Server/                # 服务端：零框架零依赖 Node（REST + WebSocket）
│   ├── server.js  package.json  src/  docs/
└── docs/                  # 关键开发文档
    ├── README.md          # 文档索引
    ├── Godot版经验归纳.md  # 跨技术栈可复用经验（IPC 三铁律 / 视觉 / 动效铁律…）
    └── design-experience/ # Godot 版探索期的交接文档（00–07 八卷）
```

---

## 构建与运行

### 客户端

需要 **Visual Studio Build Tools + CMake + Ninja**，然后运行：

```bat
DesktopApp\build.bat
```

详见 `DesktopApp/CMakeLists.txt`。

> 第三方授权 SDK / 美术素材由 `DesktopApp/tools/` 下的脚本取回，
> **默认不入库**（见 `.gitignore`）。请勿手动提交这些授权内容。

### 服务端

零框架零依赖的 Node 实现，默认监听 `0.0.0.0:8787`。

见 `Server/README.md` 与 `Server/启动服务端.bat`。

---

## 开源协议

见 [LICENSE](./LICENSE) 与 [NOTICE.md](./NOTICE.md)。

需要注意：**仓库中的第三方 SDK 与美术 / 音频素材不适用本协议的授权**（例如部分看板娘模型、字体有其独立授权），
这些文件已被 `.gitignore` 排除，不会随仓库分发。

---

## Roadmap

> 低调做事，按优先级推进；这里只列方向，不排时间表。

- [ ] 首个垂直考试包（考公优先）：格式规范 + 示例数据，判分权在 C++ 壳
- [ ] 多行输入组件（复盘弹层等处的统一自绘多行编辑）
- [ ] 打包与预编译 Release（免装 Build Tools 即可体验）
- [ ] 服务端默认回环监听（`127.0.0.1`）与鉴权加固
- [ ] 数据分析增强（趋势对比 / 考点掌握图谱）
- [ ] 构建脚本与 CI（GitHub Actions 自动构建）

## 作者

**GoRmiTz** —— 芙洛理 Flori 的作者与维护者。

Godot 版探索阶段的策划、前端、后端、美术、TA、测试经验已归纳在 `docs/` 下。
