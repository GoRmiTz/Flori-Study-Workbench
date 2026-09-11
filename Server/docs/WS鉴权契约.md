# WS 鉴权契约（桌面端 ↔ 服务端）

> 芙洛理桌面端（`DesktopApp/src/net/Realtime.cpp`）与服务端（`Server/src/realtime/{ws,gateway}.js`）之间 WebSocket 实时通道的鉴权协议。
> 本文件是**双方对齐的权威依据**，已通过 `tools/smoke.js` 端到端验证（合法 token→`auth:ok`、非法 token→`auth:err`）。

---

## 1. 连接

- **端点**：与 REST 同主机同端口（默认 `http://0.0.0.0:8787`），WebSocket 升级请求路径为 **`/`**（RFC 6455）。
  - 例：REST `http://host:8787` → WS `ws://host:8787/`
- 客户端完成 HTTP 升级后，**第一帧必须是 `auth`**（见 §2）；否则服务端回 `auth:err` 并拒绝后续写操作。
- 帧格式：文本帧，载荷为 JSON。客户端→服务端帧**必须掩码**（RFC 要求，客户端已掩码）；服务端→客户端不掩码。
- 单条消息上限 **4MB**（服务端 `MAX_MESSAGE`）。

---

## 2. 认证流程

1. 客户端发起 WS 升级（`GET /` `Upgrade: websocket`）。
2. 升级成功后，客户端**立即**发送首帧：
   ```json
   { "type": "auth", "token": "<accessToken>" }
   ```
   - `token` 取自登录/刷新得到的 **access token**（JWT，HMAC-SHA256，默认 2h 有效期）。
   - 若未登录但允许访客：`token` 缺省或为空，走匿名只读。
3. 服务端 `gateway.js` 判定（按 `!d.uid` 未认证分支）：
   - **无 token** → 匿名访客：`auth:ok`（`user.isGuest=true`），仅可只读（在场/聊天/音乐/房间列表），不可建房/发言/发状态。
   - **带 token 且有效**（签名正确 + 未过期 + 用户存在）→ `auth:ok`（`user` 含 `uid`/`username` 等）。
   - **带 token 但无效/过期/用户不存在** → **`auth:err`**（`"令牌无效或已过期，请重新登录"`）。**明确拒绝，不再静默降级为访客。**
4. 客户端收 `auth:ok` → 置 `m_connected=true`，回放进房 / 拉音乐 / 发在场（`RoomView::OnNetAuthOk`）。
5. 客户端收 `auth:err` → 置 `m_connected=false`，调用 `m_refreshProvider()` 刷新 access token，后台线程退避后重连（新连接重新走第 2 步）。

---

## 3. 访客只读约束

已认证为访客（`d.guest`）后，以下**写操作**被服务端拒绝（回 `room:error` `"访客只读，注册登录后即可发言/建房"`）：
- `room:create` / `room:admin` / `presence` / `chat:msg`

读操作（`rooms:list` / `room:join` 进入 / `chat:history` / `music:list`）允许。

---

## 4. Token 说明

- 格式：JWT `header.payload.signature`，HMAC-SHA256（密钥 = 服务端 `data/secret.key` 或 `FLORI_SECRET`）。
- access token TTL 默认 2h（可 `FLORI_ACCESS_TTL` 覆盖）；refresh token 默认 30d。
- 客户端在 `auth:err` 时通过 refresh 换发新 access 再重连；**refresh token 不通过 WS 发送**。

---

## 5. 安全要点（红线）

- 服务端对「带 token 无效」与「无 token」**区分对待**：无 token = 匿名只读（产品允许），带 token 无效 = 明确拒绝（防越权、防静默降级）。
- 客户端绝不将明文口令/密钥发到 WS，只发 access token。
- 客户端收到 `auth:err` **必须刷新并重连**，不能忽略（否则静默离线、用户无感）。

---

## 6. 验证

`node tools/smoke.js`（需先 `node server.js`）覆盖：

- **「WS 认证成功」**：合法 token → `auth:ok`，`user.username` 正确。
- **「WS 非法令牌被拒」**：`token:"garbage"` → `auth:err`，`msg` 含 `无效|过期`。

两者均通过即代表握手契约对齐。
