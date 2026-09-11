"use strict";
// ============================================================
//  release.js — 客户端发布元信息（S0-1 安全加固配套）
//
//  背景：桌面端 Updater 从 2026-08-11 起强制校验安装包 SHA-256——
//  /version 不下发 sha256 就直接拒绝下载。本模块负责把「发布哪个包」
//  这件事变成放个文件就行，哈希由服务端实算，杜绝手抄错。
//
//  用法（发版三步）：
//    1. 把安装包丢进  Server/releases/Flori-1.0.1-setup.exe
//    2. 写 Server/data/release.json：
//       { "version": "1.0.1", "minVersion": "1.0.0",
//         "file": "Flori-1.0.1-setup.exe", "notes": "本次更新……" }
//    3. 重启（或不重启，本模块按 mtime+size 自动失效缓存）
//
//  未配置 release.json 时退化为「当前版本即最新、无下载地址」，
//  客户端表现为「已是最新」，与加固前行为一致。
// ============================================================
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const cfg = require("../config");

const RELEASE_DIR = path.join(cfg.ROOT, "releases");
const META_FILE = path.join(cfg.DATA_DIR, "release.json");

// 服务端自身版本，也是「没有配置发布时」对客户端声明的最新版本
const SERVER_VERSION = "1.0.0";

// 哈希缓存：key = 绝对路径，value = { mtimeMs, size, sha256 }
const hashCache = new Map();

/** 对文件算 SHA-256（同步流式，避免大包一次性读进内存）。带 mtime+size 缓存。 */
function sha256File(abs) {
  let st;
  try { st = fs.statSync(abs); } catch (e) { return null; }
  if (!st.isFile()) return null;

  const hit = hashCache.get(abs);
  if (hit && hit.mtimeMs === st.mtimeMs && hit.size === st.size) return hit;

  const h = crypto.createHash("sha256");
  const fd = fs.openSync(abs, "r");
  try {
    const buf = Buffer.allocUnsafe(1 << 20); // 1MB
    let n;
    while ((n = fs.readSync(fd, buf, 0, buf.length, null)) > 0) h.update(buf.subarray(0, n));
  } finally {
    fs.closeSync(fd);
  }
  const rec = { mtimeMs: st.mtimeMs, size: st.size, sha256: h.digest("hex") };
  hashCache.set(abs, rec);
  return rec;
}

/** 读发布配置；文件不存在或坏了都返回 null（静默退化，不影响服务启动）。 */
function readMeta() {
  try {
    const raw = fs.readFileSync(META_FILE, "utf8");
    const m = JSON.parse(raw);
    return m && typeof m === "object" ? m : null;
  } catch (e) {
    return null;
  }
}

/** 只允许纯文件名，挡掉 ../ 之类的路径穿越。 */
function safeName(name) {
  if (typeof name !== "string" || !name) return null;
  if (name.includes("/") || name.includes("\\") || name.includes("..")) return null;
  return name;
}

/**
 * GET /version 的响应体。
 * 契约：sha256 为 64 位小写十六进制；size 为字节数。
 * 客户端 Updater::DownloadAsync 见到 sha256 长度 !== 64 会拒绝下载。
 */
function info() {
  const base = {
    version: SERVER_VERSION,
    minVersion: SERVER_VERSION,
    downloadUrl: "",
    notes: "芙洛理 Flori 桌面端。自动更新：启动后后台比对版本，有新版本时托盘提示「重启并更新」。",
    sha256: "",
    size: 0,
  };

  const meta = readMeta();
  if (!meta) return base;

  const file = safeName(meta.file);
  if (!file) return base;

  const rec = sha256File(path.join(RELEASE_DIR, file));
  if (!rec) {
    // 配置了但包不在——宁可宣称「已是最新」，也不给一个下不动的地址
    console.warn("[release] release.json 指向的安装包不存在：" + file);
    return base;
  }

  return {
    version: String(meta.version || SERVER_VERSION),
    minVersion: String(meta.minVersion || SERVER_VERSION),
    // 同源相对路径；跨域分发时这里直接填完整 https URL（客户端拒绝明文 http）
    downloadUrl: meta.downloadUrl ? String(meta.downloadUrl) : "/releases/" + file,
    notes: String(meta.notes || base.notes),
    sha256: rec.sha256,
    size: rec.size,
  };
}

/**
 * GET /releases/<file> 静态下发安装包。
 * 命中返回 true（已接管响应），未命中返回 false 交回主路由。
 */
function serve(req, res, pathname) {
  const prefix = "/releases/";
  if (!pathname.startsWith(prefix)) return false;
  const file = safeName(pathname.slice(prefix.length));
  if (!file) { res.writeHead(400); res.end("bad name"); return true; }

  const abs = path.join(RELEASE_DIR, file);
  const rec = sha256File(abs);
  if (!rec) { res.writeHead(404); res.end("not found"); return true; }

  res.writeHead(200, {
    "Content-Type": "application/octet-stream",
    "Content-Length": String(rec.size),
    "Content-Disposition": 'attachment; filename="' + file + '"',
    // 让客户端/代理都能核对，与 /version 下发的必须一致
    "X-Content-SHA256": rec.sha256,
    "Cache-Control": "no-transform",
  });
  if (req.method === "HEAD") { res.end(); return true; }
  fs.createReadStream(abs).pipe(res);
  return true;
}

module.exports = { info, serve, sha256File, SERVER_VERSION, RELEASE_DIR, META_FILE };
