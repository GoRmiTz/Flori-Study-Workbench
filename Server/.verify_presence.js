"use strict";
// 临时验证：确认服务端在 b 加入公共自习室后，向 a 广播的 presence 含 2 人。
// 重点：收集 a 收到的全部 presence，取「最后一条」判断（规避 harness 竞态）。
const crypto = require("node:crypto");
const net = require("node:net");

const BASE = process.argv[2] || "http://127.0.0.1:8787";
const U = "vpres_" + crypto.randomBytes(3).toString("hex");
const P = "test1234";

function http(method, path, { token, body } = {}) {
  const headers = { "Content-Type": "application/json" };
  if (token) headers.Authorization = "Bearer " + token;
  return fetch(BASE + path, {
    method, headers, body: body === undefined ? undefined : JSON.stringify(body),
  }).then(async (r) => ({ status: r.status, json: await r.json().catch(() => ({})) }));
}

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
    const inbox = [];
    const waiters = [];
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
      wait(type, ms = 3000) {
        const i = inbox.findIndex((m) => m.type === type);
        if (i >= 0) return Promise.resolve(inbox.splice(i, 1)[0]);
        return new Promise((res2, rej2) => {
          const w = { type, resolve: res2 };
          w.timer = setTimeout(() => {
            const k = waiters.indexOf(w);
            if (k >= 0) waiters.splice(k, 1);
            rej2(new Error("wait timeout " + type));
          }, ms);
          waiters.push(w);
        });
      },
      drain(type) { for (let i = inbox.length - 1; i >= 0; i--) if (inbox[i].type === type) inbox.splice(i, 1); },
      close() { try { sock.destroy(); } catch (e) {} },
      // 收集某类型全部消息（不消费），用于竞态排查
      collect(type) { const out = inbox.filter((m) => m.type === type); return out; },
    };
    sock.on("data", (d) => {
      buf = Buffer.concat([buf, d]);
      if (!handshaked) {
        const i = buf.indexOf("\r\n\r\n");
        if (i < 0) return;
        const head = buf.subarray(0, i).toString();
        if (!/101/.test(head)) { reject(new Error("handshake fail")); return; }
        buf = buf.subarray(i + 4);
        handshaked = true;
        resolve(client);
      }
      while (buf.length >= 2) {
        const op = buf[0] & 0x0f;
        let len = buf[1] & 0x7f;
        let off = 2;
        if (len === 126) { if (buf.length < 4) break; len = buf.readUInt16BE(2); off = 4; }
        else if (len === 127) { if (buf.length < 10) break; len = Number(buf.readBigUInt64BE(2)); off = 10; }
        if (buf.length < off + len) break;
        const payload = buf.subarray(off, off + len);
        buf = buf.subarray(off + len);
        if (op === 0x8) { try { sock.destroy(); } catch (e) {} break; }
        if (op === 0x1 || op === 0x2) {
          try { const m = JSON.parse(payload.toString("utf8")); if (m && m.type) deliver(m); } catch (e) {}
        }
      }
    });
  });
}

(async () => {
  const wsUrl = BASE.replace(/^http/, "ws");
  const reg = await http("POST", "/auth/register", { body: { username: U, password: P } });
  const token = reg.json.accessToken;
  const a = await wsConnect(wsUrl);
  const b = await wsConnect(wsUrl);
  a.send({ type: "auth", token }); await a.wait("auth:ok"); await a.wait("rooms:list");
  b.send({ type: "auth", token }); await b.wait("auth:ok"); await b.wait("rooms:list");

  // a 进入 public
  a.send({ type: "room:join", room: "public" });
  await a.wait("room:joined");
  await a.wait("chat:history");
  await new Promise((r) => setTimeout(r, 200)); // 等 a 自己的 presence(count=1) 落地
  a.drain("presence");

  // b 进入 public
  b.send({ type: "room:join", room: "public" });
  await b.wait("room:joined");
  await b.wait("chat:history");
  await new Promise((r) => setTimeout(r, 300));

  // 取 a 收到的全部 presence（不消费），看最后一条
  const list = a.collect("presence");
  console.log("a 收到的 presence 条数:", list.length);
  list.forEach((m, i) => console.log(`  [${i}] count=${m.count} members=${JSON.stringify(m.members.map((x) => x.name))}`));
  const last = list[list.length - 1];
  if (last && last.count === 2) console.log("\n✅ 服务端在场广播正确：最后一条 count=2，含 a、b 两人");
  else console.log("\n❌ 服务端在场广播异常");
  a.close(); b.close();
  process.exit(0);
})().catch((e) => { console.error("异常:", e.message); process.exit(2); });
