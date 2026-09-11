"use strict";
// ============================================================
//  gateway.js — §3 实时通信网关（自习室）
//  对应桌面端占位：RoomView 的「在场成员 / 公共聊天 / 背景音乐」三处待接入。
//
//  协议（JSON over WebSocket，同端口同源）
//  上行：auth / rooms:list / room:create / room:join / room:leave / room:admin
//        presence / chat:msg / chat:history / music:list
//  下行：auth:ok|auth:err / rooms:list / room:joined / room:error
//        presence / chat:msg / chat:history / music:meta / col:changed
// ============================================================
const crypto = require("node:crypto");
const cfg = require("../config");
const auth = require("../core/auth");
const users = require("../core/users");
const friends = require("../core/friends");   // #37 私聊历史 / 好友校验
const { readJSON, writeJSON } = require("../util/fsjson");

/** 三个内置公共房间，对应 RoomView 的公共 / 静音 / 冲刺。 */
const PUBLIC_ROOMS = [
  { id: "public", name: "公共自习室", desc: "随时可进，聊天开放", builtin: true, max: 0 },
  { id: "silent", name: "静音自习室", desc: "只看在场，不聊天", builtin: true, max: 0, muted: true },
  { id: "sprint", name: "冲刺自习室", desc: "冲刺期高强度专注", builtin: true, max: 0 },
];

let rooms = readJSON(cfg.ROOMS_FILE, []);          // 用户自建房间
let chats = readJSON(cfg.ROOMCHAT_FILE, {});        // { roomId: [ msg ] }
if (!Array.isArray(rooms)) rooms = [];
if (!chats || typeof chats !== "object") chats = {};
const saveRooms = () => writeJSON(cfg.ROOMS_FILE, rooms);
const saveChats = () => writeJSON(cfg.ROOMCHAT_FILE, chats);

const clients = new Set();     // 已认证连接集合
const clamp = (s, n) => String(s === undefined || s === null ? "" : s).slice(0, n);

const findRoom = (id) =>
  PUBLIC_ROOMS.find((r) => r.id === id) || rooms.find((r) => r.id === id) || null;
const findByCode = (code) => rooms.find((r) => r.code === String(code || "").trim().toUpperCase()) || null;

function counts() {
  const c = {};
  for (const conn of clients) if (conn.data.room) c[conn.data.room] = (c[conn.data.room] || 0) + 1;
  return c;
}

function roomsPayload() {
  const c = counts();
  const now = Date.now();
  const mine = rooms.map((r) => {
    const age = now - (r.createdAt || now);
    return {
      id: r.id, code: r.code, name: r.name, hasPwd: !!r.password, max: r.max,
      creator: r.creator, count: c[r.id] || 0, builtin: false,
      expiring: age >= cfg.ROOM_TTL - 7 * 86400000 && age < cfg.ROOM_TTL,
      daysLeft: Math.max(0, Math.ceil((cfg.ROOM_TTL - age) / 86400000)),
    };
  });
  const pub = PUBLIC_ROOMS.map((r) => ({
    id: r.id, name: r.name, desc: r.desc, builtin: true, max: 0,
    muted: !!r.muted, count: c[r.id] || 0, hasPwd: false,
  }));
  return { type: "rooms:list", rooms: [...pub, ...mine] };
}

const memberOf = (conn) => ({
  uid: conn.data.uid, name: conn.data.name,
  status: conn.data.status || "online",
  focusContent: conn.data.focusContent || "",
  focusSeconds: conn.data.focusSeconds || 0,
  since: conn.data.since,
});

/** 在场成员：只广播给同房间的人（与网页端语义一致）。 */
function broadcastPresence(roomId) {
  if (!roomId) return;
  const list = [];
  for (const c of clients) if (c.data.room === roomId) list.push(memberOf(c));
  const out = JSON.stringify({ type: "presence", room: roomId, members: list, count: list.length });
  for (const c of clients) if (c.data.room === roomId) c.send(out);
}

function broadcast(obj) {
  const out = JSON.stringify(obj);
  for (const c of clients) c.send(out);
}
function broadcastRooms() { broadcast(roomsPayload()); }

function toRoom(roomId, obj) {
  const out = JSON.stringify(obj);
  for (const c of clients) if (c.data.room === roomId) c.send(out);
}

/** 房间到期清理：用户自建室 30 天过期，公共室不删。 */
function cleanupRooms() {
  const now = Date.now();
  let changed = false;
  for (let i = rooms.length - 1; i >= 0; i--) {
    if (now - (rooms[i].createdAt || now) >= cfg.ROOM_TTL) {
      const gone = rooms.splice(i, 1)[0];
      delete chats[gone.id];
      for (const c of clients) if (c.data.room === gone.id) c.data.room = "";
      changed = true;
    }
  }
  if (changed) { saveRooms(); saveChats(); broadcastRooms(); }
}

// ---------------- 消息处理 ----------------
function handle(conn, msg) {
  const d = conn.data;

  // 未认证时只接受 auth
  if (!d.uid) {
    if (msg.type !== "auth") { conn.sendJSON({ type: "auth:err", msg: "请先认证" }); return; }
    // 未带令牌 → 匿名访客只读（能看在场/聊天/音乐，不能发）
    if (!msg.token) {
      d.uid = "g_" + crypto.randomBytes(4).toString("hex");
      d.name = "访客";
      d.guest = true;
      d.since = Date.now(); d.room = ""; d.status = "online";
      clients.add(conn);
      conn.sendJSON({ type: "auth:ok", user: { uid: d.uid, username: "访客", isDemo: false, isGuest: true, createdAt: 0 } });
      conn.sendJSON(roomsPayload());
      return;
    }
    // 带了令牌但无效/过期/账户不存在 → 明确拒绝（而非静默降级为访客），
    // 让客户端感知到需重新登录。对应 smoke「WS 非法令牌被拒」。
    const p = auth.verify(msg.token);
    const u = p && p.typ === "access" ? users.findByUid(p.sub) : null;
    if (!u) {
      conn.sendJSON({ type: "auth:err", msg: "令牌无效或已过期，请重新登录" });
      return;
    }
    d.uid = u.uid; d.name = u.username; d.since = Date.now(); d.room = ""; d.status = "online";
    clients.add(conn);
    conn.sendJSON({ type: "auth:ok", user: users.publicUser(u) });
    conn.sendJSON(roomsPayload());
    return;
  }

  // 访客只读：写操作（建房/管房/发状态/发言）一律拒绝
  if (d.guest && (msg.type === "room:create" || msg.type === "room:admin" ||
                  msg.type === "presence"    || msg.type === "chat:msg")) {
    conn.sendJSON({ type: "room:error", msg: "访客只读，注册登录后即可发言/建房" });
    return;
  }

  switch (msg.type) {
    case "rooms:list":
      conn.sendJSON(roomsPayload());
      break;

    case "room:create": {
      if (rooms.find((r) => r.creator === d.name)) {
        conn.sendJSON({ type: "room:error", msg: "你已创建过一个自习室" }); break;
      }
      const room = {
        id: "room_" + Date.now().toString(36) + crypto.randomBytes(2).toString("hex"),
        code: crypto.randomBytes(4).toString("hex").slice(0, 6).toUpperCase(),
        name: clamp(msg.name || "未命名自习室", 40),
        password: clamp(msg.password, 40),
        max: Math.max(1, Math.min(50, parseInt(msg.max, 10) || 10)),
        creator: d.name, creatorUid: d.uid, createdAt: Date.now(), music: "",
      };
      rooms.push(room); saveRooms();
      d.room = room.id;
      conn.sendJSON({ type: "room:joined", room: { id: room.id, code: room.code, name: room.name, mine: true } });
      conn.sendJSON({ type: "chat:history", room: room.id, list: [] });
      broadcastRooms(); broadcastPresence(room.id);
      break;
    }

    case "room:join": {
      const prev = d.room;
      let room = msg.room ? findRoom(msg.room) : null;
      if (!room && msg.code) room = findByCode(msg.code);
      if (!room) { conn.sendJSON({ type: "room:error", msg: "房间不存在或随机码无效" }); break; }
      if (!room.builtin) {
        if (room.password && room.password !== String(msg.password || "")) {
          conn.sendJSON({ type: "room:error", msg: "密码错误" }); break;
        }
        const cnt = counts()[room.id] || 0;
        if (room.max > 0 && cnt >= room.max && prev !== room.id) {
          conn.sendJSON({ type: "room:error", msg: "房间已满" }); break;
        }
      }
      d.room = room.id;
      conn.sendJSON({
        type: "room:joined",
        room: { id: room.id, name: room.name, code: room.code || "", builtin: !!room.builtin, muted: !!room.muted, mine: room.creatorUid === d.uid },
      });
      conn.sendJSON({ type: "chat:history", room: room.id, list: (chats[room.id] || []).slice(-100) });
      if (room.music) conn.sendJSON({ type: "music:meta", room: room.id, track: room.music });
      if (prev && prev !== room.id) broadcastPresence(prev);
      broadcastPresence(room.id); broadcastRooms();
      break;
    }

    case "room:leave": {
      const prev = d.room;
      d.room = "";
      if (prev) broadcastPresence(prev);
      broadcastRooms();
      break;
    }

    case "room:admin": {
      const room = rooms.find((r) => r.id === msg.id);
      if (!room || room.creatorUid !== d.uid) { conn.sendJSON({ type: "room:error", msg: "无权限" }); break; }
      if (msg.action === "rename") room.name = clamp(msg.value, 40);
      else if (msg.action === "password") room.password = clamp(msg.value, 40);
      else if (msg.action === "max") room.max = Math.max(1, Math.min(50, parseInt(msg.value, 10) || 10));
      else if (msg.action === "music") {
        room.music = clamp(msg.value, 2000);
        toRoom(room.id, { type: "music:meta", room: room.id, track: room.music });
      } else if (msg.action === "disband") {
        rooms.splice(rooms.indexOf(room), 1);
        delete chats[room.id];
        for (const c of clients) if (c.data.room === room.id) c.data.room = "";
        saveRooms(); saveChats(); broadcastRooms();
        break;
      }
      saveRooms(); broadcastRooms();
      break;
    }

    case "presence":
      d.status = clamp(msg.status || "online", 20);
      d.focusContent = clamp(msg.focusContent, 80);
      d.focusSeconds = parseInt(msg.focusSeconds, 10) || 0;
      broadcastPresence(d.room);
      break;

    case "chat:msg": {
      const rid = d.room;
      if (!rid) { conn.sendJSON({ type: "room:error", msg: "请先加入自习室" }); break; }
      const room = findRoom(rid);
      if (room && room.muted) { conn.sendJSON({ type: "room:error", msg: "静音自习室不开放聊天" }); break; }
      const text = clamp(msg.text, 2000).trim();
      if (!text) break;
      const m = { id: crypto.randomBytes(6).toString("hex"), user: d.name, uid: d.uid, text, ts: Date.now() };
      if (!chats[rid]) chats[rid] = [];
      chats[rid].push(m);
      if (chats[rid].length > 200) chats[rid] = chats[rid].slice(-200);
      saveChats();
      toRoom(rid, { type: "chat:msg", room: rid, msg: m });
      break;
    }

    case "chat:history":
      conn.sendJSON({ type: "chat:history", room: d.room, list: (chats[d.room] || []).slice(-100) });
      break;

    case "music:list": {
      // 曲目元数据来自媒体库中 kind=music 的资源（§5），客户端用 XAudio2 播放
      const media = readJSON(cfg.MEDIA_INDEX, { items: [] });
      const tracks = (media.items || [])
        .filter((m) => m.kind === "music")
        .map((m) => ({ id: m.id, title: m.title, url: m.url, size: m.size }));
      conn.sendJSON({ type: "music:list", tracks });
      break;
    }

    case "chat:dm": {
      // #37 私聊：仅好友可发；落历史 + 在线直达目标 + 发送者回执
      const to = String(msg.to || "").trim();
      if (!to) { conn.sendJSON({ type: "dm:error", msg: "缺少接收者" }); break; }
      if (!friends.ids(d.uid).includes(to)) {
        conn.sendJSON({ type: "dm:error", msg: "只能给好友发送私聊" }); break;
      }
      const r = friends.sendDm(d.uid, to, msg.text);
      if (r.error) { conn.sendJSON({ type: "dm:error", msg: r.error }); break; }
      for (const c of clients) if (c.data.uid === to)
        c.sendJSON({ type: "chat:dm", from: d.uid, msg: r.msg });
      conn.sendJSON({ type: "chat:dm", from: d.uid, to, msg: r.msg, mine: true });
      break;
    }

    case "dm:history": {
      // #37 拉取与某用户的私聊历史
      const withUid = String(msg.with || "").trim();
      conn.sendJSON({ type: "dm:history", with: withUid, list: friends.dmHistory(d.uid, withUid) });
      break;
    }

    default:
      conn.sendJSON({ type: "error", msg: "未知消息类型：" + msg.type });
  }
}

function onConnection(conn) {
  conn.data = {};
  conn.on("message", (raw) => {
    let m;
    try { m = JSON.parse(raw); } catch (e) { return; }
    if (!m || typeof m.type !== "string") return;
    try { handle(conn, m); }
    catch (e) { conn.sendJSON({ type: "error", msg: "服务端处理异常" }); }
  });
  conn.on("close", () => {
    const room = conn.data.room;
    clients.delete(conn);
    if (room) broadcastPresence(room);
    broadcastRooms();
  });
}

setInterval(cleanupRooms, 60 * 60 * 1000).unref?.();

module.exports = {
  onConnection, broadcast, broadcastRooms, cleanupRooms,
  online: () => clients.size,
  onlineUid: (uid) => { for (const c of clients) if (c.data.uid === uid) return true; return false; },
  PUBLIC_ROOMS,
};
