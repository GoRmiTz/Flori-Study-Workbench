"use strict";
// ============================================================
//  芙洛理 Flori · 服务端 (v1.0.0)
//  §1 用户中心鉴权 + §2 六数据块同步 + §3 实时自习室 + §4 公共专栏 + §5 媒体库
//  零第三方依赖（仅 Node 内置模块）。REST 与 WebSocket 同端口同源。
//
//  启动：node server.js            默认 http://0.0.0.0:8787
//  自检：node tools/smoke.js       端到端冒烟（注册→登录→同步→专栏→实时）
// ============================================================
const http = require("node:http");
const cfg = require("./src/config");
const { CORS, ok, fail, sendJSON, readJSONBody, match } = require("./src/util/http");
const authCore = require("./src/core/auth");
const users = require("./src/core/users");
const release = require("./src/core/release");
const ws = require("./src/realtime/ws");
const gateway = require("./src/realtime/gateway");

const columnsRoutes = require("./src/routes/columns");
columnsRoutes.attachGateway(gateway);

const friendsRoutes = require("./src/routes/friends");
friendsRoutes.attachGateway(gateway);

const ROUTES = [
  ...require("./src/routes/auth"),
  ...require("./src/routes/sync"),
  ...columnsRoutes,
  ...require("./src/routes/media"),
  ...friendsRoutes,
];

const started = Date.now();

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, "http://localhost");
  const pathname = decodeURIComponent(url.pathname);

  // 预检
  if (req.method === "OPTIONS") { res.writeHead(204, CORS); res.end(); return; }

  // 无需鉴权的运维端点
  if (pathname === "/health") {
    return ok(res, {
      service: "flori-server", version: "1.0.0",
      uptime: Math.floor((Date.now() - started) / 1000),
      users: users.count(), online: gateway.online(),
    });
  }
  // 客户端自动更新检查（P2-3 方案 A + S0-1 安全加固）：无需鉴权。
  // 桌面端启动后后台拉这个端点，比对本地 kAppVersion；有新版本时托盘提示「重启并更新」。
  // 响应含 sha256/size —— 客户端强制校验，缺失即拒绝下载。发版方式见 src/core/release.js 顶部说明。
  if (pathname === "/version") {
    return ok(res, release.info());
  }
  // 安装包静态下发（配合 /version 的 downloadUrl="/releases/<file>"）
  if (release.serve(req, res, pathname)) return;
  if (pathname === "/" || pathname === "/api") {
    return ok(res, {
      service: "芙洛理 Flori 服务端",
      endpoints: ROUTES.map((r) => `${r.method} ${r.path}`).sort(),
      websocket: `ws://<host>:${cfg.PORT}/  （连接后先发 {"type":"auth","token":"<accessToken>"}）`,
    });
  }

  // 路由匹配
  for (const route of ROUTES) {
    if (route.method !== req.method) continue;
    const params = match(route.path, pathname);
    if (!params) continue;

    let user = null;
    if (route.auth) {
      user = authCore.bearer(req);
      if (!user) return fail(res, 401, "未认证或令牌已过期");
      const u = users.findByUid(user.sub);
      if (!u) return fail(res, 401, "用户不存在");
      if ((user.tv || 1) !== (u.tv || 1)) return fail(res, 401, "登录状态已失效，请重新登录");
    }
    // 可选鉴权：有令牌就解析（用于标记 mine/liked），无令牌或令牌无效当作匿名访客，不返回 401。
    // 供 GET /columns 这类「公开可读、登录后体验更全」的接口用。
    if (route.authOptional) {
      const u2 = authCore.bearer(req);
      if (u2) {
        const uu = users.findByUid(u2.sub);
        if (uu && (u2.tv || 1) === (uu.tv || 1)) user = u2;
      }
    }

    let body = {};
    if (!route.raw && (req.method === "POST" || req.method === "PUT" || req.method === "DELETE")) {
      try { body = await readJSONBody(req); }
      catch (e) { return fail(res, e.status || 400, e.message || "请求体解析失败"); }
    }

    try {
      await route.handler({ req, res, params, query: url.searchParams, user, body });
    } catch (e) {
      console.error("[路由异常]", route.method, route.path, e);
      if (!res.headersSent) fail(res, 500, "服务端内部错误");
    }
    return;
  }

  sendJSON(res, 404, { ok: false, error: "接口不存在：" + req.method + " " + pathname });
});

// WebSocket 与 REST 共用端口
ws.attach(server, (conn) => gateway.onConnection(conn));

users.seedDemo();

server.listen(cfg.PORT, cfg.HOST, () => {
  console.log(`芙洛理 Flori 服务端已启动  http://${cfg.HOST}:${cfg.PORT}`);
  console.log(`  数据目录 : ${cfg.DATA_DIR}`);
  console.log(`  已注册   : ${users.count()} 个账户（含演示账户 ${cfg.DEMO_USER}）`);
  console.log(`  WebSocket: 同端口同源，先发 auth 帧`);
  gateway.cleanupRooms();
});

// 端口被占 / 监听失败时给一句人话，而不是抛 Node 堆栈把人吓一跳。
server.on("error", (err) => {
  if (err.code === "EADDRINUSE") {
    console.error("");
    console.error("[启动失败] 端口 " + cfg.PORT + " 已被占用 —— 可能已经有一个服务端在跑了。");
    console.error("  解决：把旧的服务端窗口按 Ctrl+C 关掉，或者在任务管理器里结束占用 " + cfg.PORT + " 的 node 进程，再重试。");
    console.error("  查谁在占用：在命令行输入  netstat -ano | findstr :" + cfg.PORT);
  } else {
    console.error("[启动失败]", err.code || err.message);
  }
  process.exit(1);
});

module.exports = server;
