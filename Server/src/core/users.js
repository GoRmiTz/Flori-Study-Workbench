"use strict";
// ============================================================
//  users.js — 用户表（注册 / 查找 / 令牌版本）
//  落盘：data/users.json
//    { version, byName: { <小写用户名>: uid }, users: { uid: User } }
//  User: { uid, username, pass, createdAt, updatedAt, isDemo, tv }
//    · tv = token version，用于「退出全部设备」时批量失效历史令牌
// ============================================================
const crypto = require("node:crypto");
const cfg = require("../config");
const { readJSON, writeJSON } = require("../util/fsjson");
const auth = require("./auth");

const DEFAULT = { version: 1, byName: {}, users: {} };
let db = readJSON(cfg.INDEX_FILE, DEFAULT);
if (!db.byName) db.byName = {};
if (!db.users) db.users = {};

function persist() { writeJSON(cfg.INDEX_FILE, db, true); }

/** 用户编号：与桌面端 "#XXXXX" 观感一致，但保证全局唯一。 */
function genUid() {
  const alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for (let attempt = 0; attempt < 50; attempt++) {
    let s = "";
    const bytes = crypto.randomBytes(8);
    for (let i = 0; i < 8; i++) s += alphabet[bytes[i] % alphabet.length];
    const uid = "u_" + s;
    if (!db.users[uid]) return uid;
  }
  return "u_" + crypto.randomBytes(8).toString("hex");
}

const norm = (name) => String(name || "").trim().toLowerCase();

function findByName(name) {
  const uid = db.byName[norm(name)];
  return uid ? db.users[uid] || null : null;
}
function findByUid(uid) { return db.users[uid] || null; }

/** 校验用户名/密码格式，返回错误文案或 null。 */
function validate(name, pass) {
  const n = String(name || "").trim();
  if (n.length < 2) return "账户名至少 2 个字符";
  if (n.length > 32) return "账户名最多 32 个字符";
  if (/[\\/:*?"<>|]/.test(n)) return "账户名不能包含 \\ / : * ? \" < > |";
  if (n === "__guest__") return "该账户名为保留名";
  if (String(pass || "").length < 4) return "密码至少 4 位";
  if (String(pass || "").length > 128) return "密码过长";
  return null;
}

function register(name, pass) {
  const bad = validate(name, pass);
  if (bad) return { error: bad, status: 400 };
  const n = String(name).trim();
  if (findByName(n)) return { error: "该账户已存在", status: 409 };
  const now = Date.now();
  const user = {
    uid: genUid(),
    username: n,
    pass: auth.hashPassword(pass),
    createdAt: now,
    updatedAt: now,
    isDemo: n === cfg.DEMO_USER,
    tv: 1,
    // 「我的」档案：桌面端 ProfileView 编辑后上云（P1#3）
    profile: { bio: "", major: "", school: "", birthday: "", gender: "" },
  };
  db.users[user.uid] = user;
  db.byName[norm(n)] = user.uid;
  persist();
  return { user };
}

function login(name, pass) {
  const user = findByName(name);
  // 用户不存在时也跑一次哈希，抹平「账户是否存在」的响应时间差异
  if (!user) {
    auth.verifyPassword(String(pass || ""), "scrypt$16384$8$1$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=");
    return { error: "账户不存在", status: 404 };
  }
  if (!auth.verifyPassword(pass, user.pass)) return { error: "密码不正确", status: 401 };
  return { user };
}

function changePassword(uid, newPass) {
  const user = findByUid(uid);
  if (!user) return { error: "用户不存在", status: 404 };
  if (String(newPass || "").length < 4) return { error: "密码至少 4 位", status: 400 };
  user.pass = auth.hashPassword(newPass);
  user.tv = (user.tv || 1) + 1;   // 改密即踢下所有旧设备
  user.updatedAt = Date.now();
  persist();
  return { user };
}

/** 退出全部设备：抬高 token version，历史令牌全部失效。 */
function bumpTokenVersion(uid) {
  const user = findByUid(uid);
  if (!user) return null;
  user.tv = (user.tv || 1) + 1;
  user.updatedAt = Date.now();
  persist();
  return user;
}

/** 对外可见的用户信息（绝不含密码哈希）。profile 兼容旧账户（可能无该字段）。 */
const publicUser = (u) => ({
  uid: u.uid, username: u.username, createdAt: u.createdAt, isDemo: !!u.isDemo,
  profile: u.profile || { bio: "", major: "", school: "", birthday: "", gender: "" },
});

/** 「我的」档案字段长度上限（与桌面端 ProfileView 编辑框约束一致）。 */
const PROFILE_LIMITS = { bio: 500, major: 40, school: 80, birthday: 20, gender: 10 };
const PROFILE_FIELDS = ["bio", "major", "school", "birthday", "gender"];

/** 更新本人档案（PUT /auth/me）。仅允许已知字段，超限报错。 */
function updateProfile(uid, fields) {
  const user = findByUid(uid);
  if (!user) return { error: "用户不存在", status: 404 };
  if (!fields || typeof fields !== "object") return { error: "缺少资料字段", status: 400 };
  user.profile = user.profile || { bio: "", major: "", school: "", birthday: "", gender: "" };
  for (const k of PROFILE_FIELDS) {
    if (!(k in fields)) continue;
    const raw = fields[k];
    const v = typeof raw === "string" ? raw : String(raw == null ? "" : raw);
    const limit = PROFILE_LIMITS[k];
    if (v.length > limit) return { error: `字段「${k}」超过 ${limit} 字上限`, status: 400 };
    user.profile[k] = v;
  }
  user.updatedAt = Date.now();
  persist();
  return { user };
}

/** 首次启动播种演示账户（仅当设置了 FLORI_DEMO_USER/PASS 环境变量时才会创建）。 */
function seedDemo() {
  if (findByName(cfg.DEMO_USER)) return;
  register(cfg.DEMO_USER, cfg.DEMO_PASS);
}

module.exports = {
  findByName, findByUid, register, login, changePassword,
  bumpTokenVersion, publicUser, seedDemo, validate, updateProfile,
  count: () => Object.keys(db.users).length,
};
