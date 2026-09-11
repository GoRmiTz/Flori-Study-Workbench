"use strict";
// ============================================================
//  auth.js — 密码哈希（scrypt）与令牌签发（HS256 JWT）
//  取代桌面端本地的 FNV-1a 哈希：FNV 不是密码学哈希，无盐、可秒破。
//  零第三方依赖，全部基于 node:crypto。
// ============================================================
const crypto = require("node:crypto");
const cfg = require("../config");

const SCRYPT = { N: 16384, r: 8, p: 1, keylen: 32 };

/** 生成 scrypt 密码哈希，格式：scrypt$N$r$p$<saltB64>$<hashB64> */
function hashPassword(pass) {
  const salt = crypto.randomBytes(16);
  const dk = crypto.scryptSync(String(pass), salt, SCRYPT.keylen, {
    N: SCRYPT.N, r: SCRYPT.r, p: SCRYPT.p, maxmem: 64 * 1024 * 1024,
  });
  return `scrypt$${SCRYPT.N}$${SCRYPT.r}$${SCRYPT.p}$${salt.toString("base64")}$${dk.toString("base64")}`;
}

/** 校验密码；恒定时间比较，避免计时侧信道。 */
function verifyPassword(pass, stored) {
  try {
    const parts = String(stored || "").split("$");
    if (parts.length !== 6 || parts[0] !== "scrypt") return false;
    const [, N, r, p, saltB64, hashB64] = parts;
    const salt = Buffer.from(saltB64, "base64");
    const expect = Buffer.from(hashB64, "base64");
    const dk = crypto.scryptSync(String(pass), salt, expect.length, {
      N: parseInt(N, 10), r: parseInt(r, 10), p: parseInt(p, 10), maxmem: 64 * 1024 * 1024,
    });
    return dk.length === expect.length && crypto.timingSafeEqual(dk, expect);
  } catch (e) {
    return false;
  }
}

// ---------------- JWT (HS256) ----------------
const b64u = (buf) => Buffer.from(buf).toString("base64url");

function sign(payload, ttlSeconds) {
  const now = Math.floor(Date.now() / 1000);
  const body = { ...payload, iat: now, exp: now + ttlSeconds };
  const head = b64u(JSON.stringify({ alg: "HS256", typ: "JWT" }));
  const data = head + "." + b64u(JSON.stringify(body));
  const sig = crypto.createHmac("sha256", cfg.SECRET).update(data).digest("base64url");
  return data + "." + sig;
}

/** 校验并解析 JWT；失败返回 null（过期 / 签名不符 / 结构损坏）。 */
function verify(token) {
  try {
    const parts = String(token || "").split(".");
    if (parts.length !== 3) return null;
    const data = parts[0] + "." + parts[1];
    const expect = crypto.createHmac("sha256", cfg.SECRET).update(data).digest("base64url");
    const a = Buffer.from(parts[2]);
    const b = Buffer.from(expect);
    if (a.length !== b.length || !crypto.timingSafeEqual(a, b)) return null;
    const payload = JSON.parse(Buffer.from(parts[1], "base64url").toString("utf8"));
    if (typeof payload.exp !== "number" || payload.exp < Math.floor(Date.now() / 1000)) return null;
    return payload;
  } catch (e) {
    return null;
  }
}

const issueAccess = (user) =>
  sign({ sub: user.uid, name: user.username, typ: "access" }, cfg.ACCESS_TTL);
const issueRefresh = (user) =>
  sign({ sub: user.uid, name: user.username, typ: "refresh" }, cfg.REFRESH_TTL);

/** 从请求头取 Bearer 令牌并校验，仅接受 access 类型。 */
function bearer(req) {
  const h = req.headers["authorization"] || "";
  const m = /^Bearer\s+(.+)$/i.exec(h.trim());
  if (!m) return null;
  const p = verify(m[1]);
  return p && p.typ === "access" ? p : null;
}

module.exports = {
  hashPassword, verifyPassword, sign, verify,
  issueAccess, issueRefresh, bearer,
};
