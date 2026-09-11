"use strict";
// ============================================================
//  config.js — 运行期配置与密钥
//  所有可调项均可用环境变量覆盖，默认值面向「单机自托管」场景。
// ============================================================
const path = require("node:path");
const fs = require("node:fs");
const crypto = require("node:crypto");

const ROOT = path.resolve(__dirname, "..");
const DATA_DIR = path.resolve(process.env.FLORI_DATA_DIR || path.join(ROOT, "data"));

const cfg = {
  ROOT,
  DATA_DIR,
  USERS_DIR: path.join(DATA_DIR, "users"),       // 每用户一目录：<uid>/{6 个数据块}.json
  INDEX_FILE: path.join(DATA_DIR, "users.json"), // 用户索引（用户名 → uid）
  COLUMNS_FILE: path.join(DATA_DIR, "columns.json"),
  ROOMS_FILE: path.join(DATA_DIR, "rooms.json"),
  ROOMCHAT_FILE: path.join(DATA_DIR, "room-chat.json"),
  FRIENDS_FILE: path.join(DATA_DIR, "friends.json"),   // 好友关系 { uid: [好友uid] } 双向
  DMS_FILE: path.join(DATA_DIR, "dms.json"),           // 私聊历史 { threadKey: [msg] }
  MEDIA_DIR: path.join(DATA_DIR, "media"),
  MEDIA_INDEX: path.join(DATA_DIR, "media.json"),

  HOST: process.env.FLORI_HOST || "0.0.0.0",
  PORT: parseInt(process.env.FLORI_PORT || "8787", 10),

  // 令牌有效期
  ACCESS_TTL: parseInt(process.env.FLORI_ACCESS_TTL || String(2 * 60 * 60), 10),        // 2 小时
  REFRESH_TTL: parseInt(process.env.FLORI_REFRESH_TTL || String(30 * 24 * 60 * 60), 10), // 30 天

  // 限额
  MAX_BODY: 8 * 1024 * 1024,          // 单个 JSON 请求体 8MB（items/checkin 足够）
  MAX_UPLOAD: 120 * 1024 * 1024,      // 媒体上传 120MB（与网页端 server.js 对齐）
  ROOM_TTL: 30 * 24 * 60 * 60 * 1000, // 用户自建自习室 30 天过期

  // 演示账户。
  // 开源仓库不含任何真实账户名与口令：默认留空，只有显式设置环境变量后才会创建。
  //   Windows:  set FLORI_DEMO_USER=你的账户名 && set FLORI_DEMO_PASS=你的口令 && node server.js
  //   Bash:     FLORI_DEMO_USER=... FLORI_DEMO_PASS=... node server.js
  // 未设置时不会预置任何账户 —— 与全新安装 / 访客态一致。
  DEMO_USER: process.env.FLORI_DEMO_USER || "",
  DEMO_PASS: process.env.FLORI_DEMO_PASS || "",
};

// ---------- 签名密钥：环境变量优先，否则在 data/ 下自动生成并持久化 ----------
function loadSecret() {
  if (process.env.FLORI_SECRET) return Buffer.from(process.env.FLORI_SECRET, "utf8");
  const keyFile = path.join(DATA_DIR, "secret.key");
  try {
    const raw = fs.readFileSync(keyFile);
    if (raw && raw.length >= 32) return raw;
  } catch (e) { /* 首次运行，落到下面生成 */ }
  const buf = crypto.randomBytes(48);
  fs.mkdirSync(DATA_DIR, { recursive: true });
  fs.writeFileSync(keyFile, buf, { mode: 0o600 });
  return buf;
}

fs.mkdirSync(cfg.USERS_DIR, { recursive: true });
fs.mkdirSync(cfg.MEDIA_DIR, { recursive: true });
cfg.SECRET = loadSecret();

module.exports = cfg;
