"use strict";
// ============================================================
//  smoke.js — 端到端冒烟自检
//  覆盖：健康检查 → 注册 → 登录 → me → 六块同步（推/拉/合并）
//        → 单块读写 → 专栏 CRUD/点赞/评论 → WebSocket 认证/房间/聊天/在场
//  用法：先启动 server.js，再 node tools/smoke.js [baseUrl]
// ============================================================
const crypto = require("node:crypto");
const net = require("node:net");

const BASE = process.argv[2] || "http://127.0.0.1:8787";
const U = "smoke_" + crypto.randomBytes(3).toString("hex");
const P = "test1234";

let pass = 0, failed = 0;
const okMark = "  \u2713 ";
const badMark = "  \u2717 ";
function check(name, cond, detail) {
  if (cond) { pass++; console.log(okMark + name); }
  else { failed++; console.log(badMark + name + (detail ? "  → " + JSON.stringify(detail) : "")); }
}
function section(t) { console.log("\n\u2500\u2500 " + t + " \u2500\u2500"); }

async function api(method, path, { token, body } = {}) {
  const headers = { "Content-Type": "application/json" };
  if (token) headers.Authorization = "Bearer " + token;
  const r = await fetch(BASE + path, {
    method, headers, body: body === undefined ? undefined : JSON.stringify(body),
  });
  let json = null;
  try { json = await r.json(); } catch (e) { json = { ok: false, error: "非 JSON 响应" }; }
  return { status: r.status, json };
}

// ---------------- 最小 WebSocket 客户端（掩码帧，够跑自检） ----------------
function wsConnect(url) {
  return new Promise((resolve, reject) => {
    const u = new URL(url);
    const key = crypto.randomBytes(16).toString("base64");
    const sock = net.connect(parseInt(u.port, 10) || 80, u.hostname, () => {
      sock.write(
        `GET ${u.pathname || "/"} HTTP/1.1\r\nHost: ${u.host}\r\nUpgrade: websocket\r\n` +
        `Connection: Upgrade\r\nSec-WebSocket-Key: ${key}\r\nSec-WebSocket-Version: 13\r\n\r\n`
      );
    });
    let buf = Buffer.alloc(0);
    let handshaked = false;
    const inbox = [];      // 已收到但未被 wait 消费的消息（避免竞态丢帧）
    const waiters = [];    // { type, resolve, timer }
    /** 新消息先尝试唤醒等待者，无人认领则入队留待后续 wait 取走。 */
    function deliver(m) {
      for (let i = 0; i < waiters.length; i++) {
        if (waiters[i].type === m.type) {
          const w = waiters.splice(i, 1)[0];
          clearTimeout(w.timer);
          w.resolve(m);
          return;
        }
      }
      inbox.push(m);
    }
    const client = {
      send(obj) {
        const payload = Buffer.from(JSON.stringify(obj), "utf8");
        const mask = crypto.randomBytes(4);
        const len = payload.length;
        let head;
        if (len < 126) { head = Buffer.alloc(2); head[1] = 0x80 | len; }
        else { head = Buffer.alloc(4); head[1] = 0x80 | 126; head.writeUInt16BE(len, 2); }
        head[0] = 0x81;
        const masked = Buffer.from(payload);
        for (let i = 0; i < masked.length; i++) masked[i] ^= mask[i & 3];
        sock.write(Buffer.concat([head, mask, masked]));
      },
      /** 等待某个 type 的下行消息；已在队列里则立即返回，否则挂起至超时。 */
      wait(type, ms = 3000) {
        const i = inbox.findIndex((m) => m.type === type);
        if (i >= 0) return Promise.resolve(inbox.splice(i, 1)[0]);
        return new Promise((res2, rej2) => {
          const w = { type, resolve: res2 };
          w.timer = setTimeout(() => {
            const k = waiters.indexOf(w);
            if (k >= 0) waiters.splice(k, 1);
            rej2(new Error("等待 " + type + " 超时"));
          }, ms);
          waiters.push(w);
        });
      },
      /** 丢弃队列中已堆积的指定类型消息（用于「只关心此刻之后的广播」场景）。 */
      drain(type) {
        for (let i = inbox.length - 1; i >= 0; i--) if (inbox[i].type === type) inbox.splice(i, 1);
      },
      /** 非消费式查看：返回队列中所有该类型消息（用于竞态排查，不取出）。 */
      peek(type) { return inbox.filter((m) => m.type === type); },
      close() { try { sock.destroy(); } catch (e) { /* ignore */ } },
    };
    sock.on("data", (d) => {
      buf = Buffer.concat([buf, d]);
      if (!handshaked) {
        const i = buf.indexOf("\r\n\r\n");
        if (i < 0) return;
        const head = buf.subarray(0, i).toString();
        if (!/101/.test(head)) { reject(new Error("握手失败: " + head.split("\r\n")[0])); return; }
        buf = buf.subarray(i + 4);
        handshaked = true;
        resolve(client);
      }
      // 解析服务端帧（不带掩码）
      while (buf.length >= 2) {
        const op = buf[0] & 0x0f;
        let len = buf[1] & 0x7f;
        let off = 2;
        if (len === 126) { if (buf.length < 4) return; len = buf.readUInt16BE(2); off = 4; }
        else if (len === 127) { if (buf.length < 10) return; len = Number(buf.readBigUInt64BE(2)); off = 10; }
        if (buf.length < off + len) return;
        const payload = buf.subarray(off, off + len);
        buf = buf.subarray(off + len);
        if (op === 0x1) {
          let m; try { m = JSON.parse(payload.toString("utf8")); } catch (e) { continue; }
          deliver(m);
        }
      }
    });
    sock.on("error", reject);
  });
}

(async function main() {
  console.log("芙洛理 Flori 服务端 · 端到端冒烟自检");
  console.log("目标: " + BASE + "   测试账户: " + U);

  section("健康检查");
  const h = await api("GET", "/health");
  check("GET /health 返回 ok", h.json.ok === true, h.json);
  check("服务标识正确", h.json.service === "flori-server", h.json.service);

  section("§1 用户中心 & 鉴权");
  const shortPw = await api("POST", "/auth/register", { body: { username: U, password: "1" } });
  check("弱密码被拒（400）", shortPw.status === 400, shortPw.json);

  const reg = await api("POST", "/auth/register", { body: { username: U, password: P } });
  check("注册成功", reg.json.ok === true, reg.json);
  check("返回访问令牌", typeof reg.json.accessToken === "string", Object.keys(reg.json));
  check("响应不含密码哈希", !JSON.stringify(reg.json).includes("scrypt$"));

  const dup = await api("POST", "/auth/register", { body: { username: U, password: P } });
  check("重复注册被拒（409）", dup.status === 409, dup.json);

  const bad = await api("POST", "/auth/login", { body: { username: U, password: "wrong" } });
  check("错误密码被拒（401）", bad.status === 401, bad.json);

  const login = await api("POST", "/auth/login", { body: { username: U, password: P } });
  check("登录成功", login.json.ok === true, login.json);
  const token = login.json.accessToken;
  const refresh = login.json.refreshToken;

  const noAuth = await api("GET", "/sync/all");
  check("无令牌访问受保护接口被拒（401）", noAuth.status === 401, noAuth.json);
  const badTok = await api("GET", "/sync/all", { token: token.slice(0, -3) + "xxx" });
  check("篡改令牌被拒（401）", badTok.status === 401, badTok.json);

  const me = await api("GET", "/auth/me", { token });
  check("GET /auth/me 返回本人", me.json.ok && me.json.user.username === U, me.json);
  check("GET /auth/me 含默认档案字段", me.json.user.profile && "bio" in me.json.user.profile && "gender" in me.json.user.profile, me.json.user.profile);

  const prof = await api("PUT", "/auth/me", { token, body: { bio: "芙洛理 Flori 用户", major: "数字媒体艺术", school: "湖大", birthday: "2004-01-04", gender: "男" } });
  check("PUT /auth/me 更新档案成功", prof.json.ok === true, prof.json);
  check("档案字段落盘正确", prof.json.user.profile.bio === "芙洛理 Flori 用户" && prof.json.user.profile.major === "数字媒体艺术", prof.json.user.profile);
  const me2 = await api("GET", "/auth/me", { token });
  check("档案重启读取一致", me2.json.user.profile.school === "湖大" && me2.json.user.profile.gender === "男", me2.json.user.profile);
  const over = await api("PUT", "/auth/me", { token, body: { bio: "x".repeat(501) } });
  check("档案超长被拒（400）", over.status === 400, over.json);

  const ref = await api("POST", "/auth/refresh", { body: { refreshToken: refresh } });
  check("刷新令牌可换新访问令牌", ref.json.ok === true, ref.json);

  section("§2 数据同步（六数据块）");
  const payload = {
    app: "FLORI", version: 1, account: U,
    checkin: { "2026-08-04": { "行测·言语": 1, "英语单词": 0 } },
    focus: [{ date: "2026-08-04", start: 1754300000, end: 1754301500, min: 25, tag: "主线" }],
    items: { daily: [{ id: "i1", slot: "07:00", title: "早起", standard: "", tag: "作息", minutes: 0, link: "", folder: "" }], sat: [], sun: [] },
    journal: { "2026-08-04": { summary: "完成两套行测", next: "申论大作文" } },
    rhythm: { "2026-08-04": { wake: 400, sleep: 1380 } },
    settings: { dark: true },
  };
  const push = await api("PUT", "/sync/all", { token, body: payload });
  check("全量上推成功", push.json.ok === true, push.json);
  check("六个块全部落地", Object.keys(push.json.written || {}).length === 6, push.json.written);

  const pull = await api("GET", "/sync/all", { token });
  check("全量拉取成功", pull.json.ok === true, pull.json);
  check("checkin 往返一致", JSON.stringify(pull.json.checkin) === JSON.stringify(payload.checkin), pull.json.checkin);
  check("focus 往返一致", JSON.stringify(pull.json.focus) === JSON.stringify(payload.focus), pull.json.focus);
  check("settings.dark 往返一致", pull.json.settings.dark === true, pull.json.settings);
  check("载荷含 FLORI 标识（可直接 ImportAll）", pull.json.app === "FLORI");

  // 增量合并：新增一天，旧数据必须保留
  const merge = await api("PUT", "/sync/all?mode=merge", {
    token, body: { checkin: { "2026-08-05": { "申论·归纳": 1 } } },
  });
  check("增量合并上推成功", merge.json.ok === true, merge.json);
  const pull2 = await api("GET", "/sync/all", { token });
  check("合并后旧日期仍在", !!pull2.json.checkin["2026-08-04"], pull2.json.checkin);
  check("合并后新日期已加入", !!pull2.json.checkin["2026-08-05"], pull2.json.checkin);

  // focus 并集去重
  await api("PUT", "/sync/block/focus?mode=merge", { token, body: { data: payload.focus } });
  const f = await api("GET", "/sync/block/focus", { token });
  check("focus 重复上推不产生重复记录", f.json.data.length === 1, f.json.data);

  const rev1 = (await api("GET", "/sync/block/settings", { token })).json.rev;
  await api("PUT", "/sync/block/settings", { token, body: { data: { dark: false } } });
  const rev2 = (await api("GET", "/sync/block/settings", { token })).json.rev;
  check("单块写入后 rev 递增", rev2 === rev1 + 1, { rev1, rev2 });

  const badType = await api("PUT", "/sync/block/focus", { token, body: { data: { notAnArray: 1 } } });
  check("块类型不符被拒（400）", badType.status === 400, badType.json);
  const badBlock = await api("PUT", "/sync/block/nope", { token, body: { data: {} } });
  check("未知数据块被拒（404）", badBlock.status === 404, badBlock.json);

  const st = await api("GET", "/sync/stats", { token });
  check("聚合统计可用", st.json.ok && st.json.stats.focusMinutes === 25, st.json.stats);

  section("§4 公共专栏");
  const create = await api("POST", "/columns", { token, body: { title: "冒烟测试专栏", body: "正文内容", sectionId: "method" } });
  check("发布专栏成功", create.json.ok === true, create.json);
  const cid = create.json.id;
  const list = await api("GET", "/columns", { token });
  check("专栏列表含新文章", (list.json.posts || []).some((p) => p.id === cid), list.json.posts);
  check("内置分区已就绪", (list.json.sections || []).length >= 5, list.json.sections);
  const like = await api("POST", `/columns/${cid}/like`, { token });
  check("点赞成功", like.json.ok && like.json.on === true && like.json.count === 1, like.json);
  const unlike = await api("POST", `/columns/${cid}/like`, { token });
  check("再次点赞即取消", unlike.json.on === false && unlike.json.count === 0, unlike.json);
  const cm = await api("POST", `/columns/${cid}/comment`, { token, body: { text: "写得好" } });
  check("评论成功", cm.json.ok && cm.json.count === 1, cm.json);
  const detail = await api("GET", `/columns/${cid}`, { token });
  check("详情含正文与评论", detail.json.post.body === "正文内容" && detail.json.post.comments.length === 1, detail.json.post);

  // 越权：另一个账户不能改别人的专栏
  const other = await api("POST", "/auth/register", { body: { username: U + "_b", password: P } });
  const t2 = other.json.accessToken;
  const forbid = await api("PUT", `/columns/${cid}`, { token: t2, body: { title: "篡改" } });
  check("非作者编辑被拒（403）", forbid.status === 403, forbid.json);
  const del = await api("DELETE", `/columns/${cid}`, { token });
  check("作者删除成功", del.json.ok === true, del.json);

  section("§3 实时自习室（WebSocket）");
  const wsUrl = BASE.replace(/^http/, "ws");
  const a = await wsConnect(wsUrl);
  const b = await wsConnect(wsUrl);

  a.send({ type: "auth", token });
  const authOkA = await a.wait("auth:ok");
  check("WS 认证成功", authOkA.user.username === U, authOkA.user);

  const roomsMsg = await a.wait("rooms:list");
  check("认证后自动下发房间列表", Array.isArray(roomsMsg.rooms) && roomsMsg.rooms.length >= 3, roomsMsg.rooms?.length);
  check("三个内置公共房间就绪", ["public", "silent", "sprint"].every((id) => roomsMsg.rooms.some((r) => r.id === id)));

  b.send({ type: "auth", token: t2 });
  await b.wait("auth:ok");
  await b.wait("rooms:list");

  a.send({ type: "room:join", room: "public" });
  const joined = await a.wait("room:joined");
  check("加入公共自习室成功", joined.room.id === "public", joined.room);
  await a.wait("chat:history");

  a.drain("presence");   // 丢掉 a 自己入室时那条
  b.send({ type: "room:join", room: "public" });
  await new Promise((r) => setTimeout(r, 350)); // 等 b 入室广播落定，规避 wait/drain 竞态
  const presences = a.peek("presence");
  const pres = presences[presences.length - 1];
  check("在场成员实时广播（2 人）", pres && pres.count === 2, presences.map((p) => p.count));

  const chatP = b.wait("chat:msg");
  a.send({ type: "chat:msg", text: "自习室聊天联通" });
  const chat = await chatP;
  check("公共聊天跨连接广播", chat.msg.text === "自习室聊天联通" && chat.msg.user === U, chat.msg);

  a.send({ type: "room:join", room: "silent" });
  await a.wait("room:joined");
  a.send({ type: "chat:msg", text: "应当被拒" });
  const muted = await a.wait("room:error");
  check("静音自习室拒绝聊天", /静音/.test(muted.msg), muted);

  a.send({ type: "music:list" });
  const music = await a.wait("music:list");
  check("背景音乐曲目元数据接口可用", Array.isArray(music.tracks), music);

  const badAuth = await wsConnect(wsUrl);
  badAuth.send({ type: "auth", token: "garbage" });
  const authErr = await badAuth.wait("auth:err");
  check("WS 非法令牌被拒", /无效|过期/.test(authErr.msg), authErr);
  badAuth.close();

  a.close(); b.close();

  section("结果");
  console.log(`  通过 ${pass} 项，失败 ${failed} 项`);
  process.exit(failed === 0 ? 0 : 1);
})().catch((e) => {
  console.error("\n自检异常中断:", e.message);
  process.exit(2);
});
