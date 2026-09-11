"use strict";
// ============================================================
//  routes/media.js — §5 媒体存储与上传（白名单授权）
//  对应桌面端占位：MediaView 本地扫描 assets/media → 升级为云端资源库。
//  存储：data/media/<kind>/<安全文件名>，索引 data/media.json
// ============================================================
const fs = require("node:fs");
const path = require("node:path");
const crypto = require("node:crypto");
const cfg = require("../config");
const { readJSON, writeJSON, writeRaw } = require("../util/fsjson");
const { ok, fail, readRawBody } = require("../util/http");

const KINDS = ["videos", "music", "images"];
/** 白名单扩展名：与桌面端 MediaView 的三类扫描规则对齐。 */
const ALLOWED_EXT = {
  videos: [".mp4", ".mkv", ".avi", ".mov", ".webm", ".wmv", ".flv", ".m4v"],
  music: [".mp3", ".wav", ".ogg", ".flac", ".m4a", ".aac", ".wma"],
  images: [".png", ".jpg", ".jpeg", ".gif", ".bmp", ".webp", ".svg", ".tiff"],
};
const MIME = {
  ".mp4": "video/mp4", ".webm": "video/webm", ".mov": "video/quicktime", ".mkv": "video/x-matroska",
  ".mp3": "audio/mpeg", ".wav": "audio/wav", ".ogg": "audio/ogg", ".flac": "audio/flac", ".m4a": "audio/mp4",
  ".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".gif": "image/gif",
  ".webp": "image/webp", ".svg": "image/svg+xml", ".bmp": "image/bmp",
};

const DEFAULT = { version: 1, whitelist: [cfg.DEMO_USER], items: [] };
let db = readJSON(cfg.MEDIA_INDEX, DEFAULT);
if (!Array.isArray(db.whitelist)) db.whitelist = [cfg.DEMO_USER];
if (!Array.isArray(db.items)) db.items = [];
const persist = () => writeJSON(cfg.MEDIA_INDEX, db);

const canUpload = (user) => db.whitelist.includes(user.name);

/** 文件名消毒：去路径分隔符与危险字符，保留中文，限长。 */
function safeName(name) {
  const base = path.basename(String(name || "file"));
  const cleaned = base
    .replace(/[\\/?%*:|"<>]/g, "_")
    .replace(/[^\w.\-\u4e00-\u9fa5 ]/g, "")
    .trim()
    .slice(0, 120);
  return cleaned || "file";
}

module.exports = [
  {
    method: "GET", path: "/media/list", auth: true,
    handler: ({ res, user, query }) => {
      const kind = query.get("kind");
      const items = db.items
        .filter((m) => !kind || m.kind === kind)
        .sort((a, b) => b.ts - a.ts)
        .map((m) => ({ ...m, mine: m.uid === user.sub }));
      return ok(res, { items, canUpload: canUpload(user), kinds: KINDS });
    },
  },
  {
    // 上传：X-Meta 为 base64(JSON){ kind, title, note, filename }，请求体为原始字节
    method: "POST", path: "/media/upload", auth: true, raw: true,
    handler: async ({ req, res, user }) => {
      if (!canUpload(user)) return fail(res, 403, "无上传权限（需管理员加入白名单）");
      let meta;
      try {
        meta = JSON.parse(Buffer.from(String(req.headers["x-meta"] || ""), "base64").toString("utf8"));
      } catch (e) { return fail(res, 400, "X-Meta 元信息无效"); }

      const kind = KINDS.includes(meta.kind) ? meta.kind : null;
      if (!kind) return fail(res, 400, "资源类型无效（应为 videos/music/images）");
      const fname = safeName(meta.filename);
      const ext = path.extname(fname).toLowerCase();
      if (!ALLOWED_EXT[kind].includes(ext)) {
        return fail(res, 415, `扩展名 ${ext || "(空)"} 不在 ${kind} 白名单内`);
      }

      let buf;
      try { buf = await readRawBody(req); }
      catch (e) { return fail(res, e.status || 400, e.message || "读取失败"); }
      if (!buf.length) return fail(res, 400, "文件内容为空");

      const stamp = Date.now().toString(36) + "_" + crypto.randomBytes(3).toString("hex");
      const stored = stamp + "_" + fname;
      const dir = path.join(cfg.MEDIA_DIR, kind);
      const fp = path.join(dir, stored);
      if (!path.resolve(fp).startsWith(path.resolve(dir))) return fail(res, 400, "非法文件名");
      if (!writeRaw(fp, buf)) return fail(res, 500, "服务端写入失败");

      const item = {
        id: "med_" + stamp, kind, stored,
        title: String(meta.title || fname).slice(0, 100),
        note: String(meta.note || "").slice(0, 500),
        size: buf.length, user: user.name, uid: user.sub, ts: Date.now(),
        url: `/media/file/med_${stamp}`,
      };
      db.items.push(item);
      persist();
      return ok(res, { item });
    },
  },
  {
    method: "GET", path: "/media/file/:id", auth: true, rawResponse: true,
    handler: ({ res, params }) => {
      const m = db.items.find((x) => x.id === params.id);
      if (!m) return fail(res, 404, "资源不存在");
      const fp = path.join(cfg.MEDIA_DIR, m.kind, m.stored);
      let data;
      try { data = fs.readFileSync(fp); }
      catch (e) { return fail(res, 404, "文件已丢失"); }
      const ext = path.extname(m.stored).toLowerCase();
      res.writeHead(200, {
        "Content-Type": MIME[ext] || "application/octet-stream",
        "Content-Length": data.length,
        "Cache-Control": "private, max-age=3600",
      });
      res.end(data);
    },
  },
  {
    method: "DELETE", path: "/media/:id", auth: true,
    handler: ({ res, user, params }) => {
      const i = db.items.findIndex((x) => x.id === params.id);
      if (i < 0) return fail(res, 404, "资源不存在");
      if (db.items[i].uid !== user.sub) return fail(res, 403, "只能删除自己上传的资源");
      const m = db.items[i];
      try { fs.unlinkSync(path.join(cfg.MEDIA_DIR, m.kind, m.stored)); } catch (e) { /* 文件可能已不在 */ }
      db.items.splice(i, 1);
      persist();
      return ok(res, { id: params.id });
    },
  },
  {
    // 白名单管理：仅演示账户（管理员）可操作
    method: "POST", path: "/media/whitelist", auth: true,
    handler: ({ res, user, body }) => {
      if (user.name !== cfg.DEMO_USER) return fail(res, 403, "仅管理员可管理白名单");
      const target = String((body || {}).user || "").trim();
      if (!target) return fail(res, 400, "缺少用户名");
      const add = (body || {}).add !== false;
      if (add) { if (!db.whitelist.includes(target)) db.whitelist.push(target); }
      else { if (target === cfg.DEMO_USER) return fail(res, 400, "不能移除管理员"); db.whitelist = db.whitelist.filter((u) => u !== target); }
      persist();
      return ok(res, { whitelist: db.whitelist });
    },
  },
];
