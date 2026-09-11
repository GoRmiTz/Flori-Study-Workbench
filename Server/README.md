# 芙洛理 Flori 服务端 · 运行说明（howto）

零第三方依赖的 Node 服务（仅用内置模块）：用户中心/鉴权、六数据块同步、实时自习室（WebSocket）、公共专栏、媒体库。REST 与 WebSocket **同端口同源**。

---

## 1. 环境要求

- **Node.js ≥ 18**（开发/验证用 v22）。
- **无需 `npm install`**：零依赖，没有 `node_modules`。

---

## 2. 启动（三种方式任选）

1. 直接：`node server.js` → 默认监听 `0.0.0.0:8787`。
2. npm：`npm start`（等价）。
3. Windows 双击：`启动服务端.bat`（含 Node 存在性检查，保持窗口打开即运行）。

启动后确认存活：

```bash
curl http://127.0.0.1:8787/health
# → {"ok":true,"service":"flori-server","version":"1.0.0",...}
```

---

## 3. 配置（环境变量，全部可选）

| 变量 | 默认 | 说明 |
|---|---|---|
| `FLORI_HOST` | `0.0.0.0` | 监听地址 |
| `FLORI_PORT` | `8787` | 监听端口 |
| `FLORI_DATA_DIR` | `./data` | 数据目录（用户/专栏/房间/媒体） |
| `FLORI_SECRET` | 自动生成 | 令牌签名密钥；不设则首次运行在 `data/secret.key` 生成并持久化 |
| `FLORI_ACCESS_TTL` | `7200` | access token 秒数（默认 2h） |
| `FLORI_REFRESH_TTL` | `2592000` | refresh token 秒数（默认 30d） |
| `FLORI_DEMO_USER` | 空 | 演示账户名；不设则不预置任何账户（全新安装态） |
| `FLORI_DEMO_PASS` | 空 | 演示账户口令；**必须自行设置，仓库不含任何真实口令** |

示例（Linux/macOS）：
```bash
FLORI_PORT=9000 FLORI_DEMO_PASS='你的强口令' node server.js
```
Windows PowerShell：
```powershell
$env:FLORI_DEMO_PASS='你的强口令'; node server.js
```

---

## 4. 数据落盘

- `data/` 首次运行自动创建：`users/`（每用户一目录）、`columns.json`、`rooms.json`、`room-chat.json`、`friends.json`、`dms.json`、`media/`、`secret.key`。
- `secret.key` 权限 `0600`，自动生成；**请勿提交或外泄**（`.gitignore` 已排除）。
- 用户数据默认不出本机；云端是可选增强。

---

## 5. 自检（端到端冒烟）

先启动服务，另开终端：

```bash
node tools/smoke.js
```

覆盖：健康检查 → 注册 → 登录 → 六块同步 → 专栏 → WebSocket 认证/房间/聊天/在场，含「非法令牌被拒」。全绿退出 `0`，任一失败退出 `1`。可指定 baseUrl：

```bash
node tools/smoke.js http://ip:port
```

---

## 6. 主要接口（概览）

- `GET /health` — 运维健康检查（无需鉴权）
- `GET /version` — 客户端自动更新检查（返回 `sha256`/`size`，无需鉴权）
- `POST /auth/register` · `POST /auth/login` · `POST /auth/refresh` · `GET,PUT /auth/me`
- `GET,PUT /sync/all` · `GET,PUT /sync/block/<name>` · `GET /sync/stats`（均需 `Authorization: Bearer <accessToken>`）
- `GET,POST /columns` · `GET,PUT,DELETE /columns/<id>` 及点赞/评论
- `GET,POST /friends` 等好友接口
- **WebSocket** `ws://host:port/`（鉴权契约见 `docs/WS鉴权契约.md`）

---

## 7. 桌面端如何连接

桌面端在 `App` 初始化时连接 `ws://<服务端host>:<port>/`，升级后首帧发送 `{ "type": "auth", "token": "<accessToken>" }`。完整握手/访客只读/Token 规则见 `docs/WS鉴权契约.md`。

---

## 8. 安全与分发

- 对外分发前：覆盖 `FLORI_DEMO_PASS` 为强口令；确认 `secret.key` 不随安装包外泄；评估是否需要 HTTPS（本服务默认 HTTP，可在前面加反向代理补 TLS）。
- 自动更新：桌面端拉 `/version` 比对本地版本，有新版本提示重启更新；安装包经 `/releases/<file>` 下发，客户端强制校验 `sha256`。

---

## 9. 故障排查

- **端口被占**：改 `FLORI_PORT`。
- **所有令牌突然失效**：删除 `data/secret.key` 重启（会使已发 access token 全部作废，需重新登录）。
- **smoke 报错**：确认服务已启动且端口一致；查看 `server.run.log` 或控制台输出。
