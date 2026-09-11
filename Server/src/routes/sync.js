"use strict";
// ============================================================
//  routes/sync.js — §2 数据同步（六数据块）
//  对应桌面端占位：CheckinStore 全部本地落盘 → 升级为「本地优先 + 云端同步」
//  载荷结构与桌面端 ExportAll/ImportAll 的 *.flori.json 完全一致。
// ============================================================
const sync = require("../core/sync");
const { ok, fail } = require("../util/http");

const mode = (q) => (q.get("mode") === "merge" ? "merge" : "replace");

module.exports = [
  {
    // 全量拉取：返回可直接喂给客户端 ImportAll 的载荷
    method: "GET", path: "/sync/all", auth: true,
    handler: ({ res, user }) => {
      const { blocks, meta } = sync.getAll(user.sub);
      return ok(res, {
        app: "FLORI", version: 1, account: user.name,
        exportedAt: Math.floor(Date.now() / 1000),
        ...blocks, meta,
      });
    },
  },
  {
    // 全量上推：接受同构载荷，逐块写入
    method: "PUT", path: "/sync/all", auth: true,
    handler: ({ res, body, user, query }) => {
      if (body && body.app && body.app !== "FLORI") return fail(res, 400, "载荷缺少 Flori 标识");
      const r = sync.putAll(user.sub, body, mode(query));
      if (Object.keys(r.written).length === 0) {
        return fail(res, 400, r.errors.length ? r.errors.join("; ") : "载荷内没有可同步的数据块");
      }
      return ok(res, { written: r.written, errors: r.errors });
    },
  },
  {
    method: "GET", path: "/sync/meta", auth: true,
    handler: ({ res, user }) => ok(res, { meta: sync.getAll(user.sub).meta }),
  },
  {
    method: "GET", path: "/sync/stats", auth: true,
    handler: ({ res, user }) => ok(res, { stats: sync.stats(user.sub) }),
  },
  {
    method: "GET", path: "/sync/block/:name", auth: true,
    handler: ({ res, params, user }) => {
      const r = sync.getBlock(user.sub, params.name);
      if (!r) return fail(res, 404, "未知数据块：" + params.name);
      return ok(res, { block: params.name, ...r });
    },
  },
  {
    // 单块上推。body 形如 { data: <块内容> }；也兼容直接把块内容当 body 传
    method: "PUT", path: "/sync/block/:name", auth: true,
    handler: ({ res, params, body, user, query }) => {
      const payload = body && Object.prototype.hasOwnProperty.call(body, "data") ? body.data : body;
      const r = sync.putBlock(user.sub, params.name, payload, mode(query));
      if (r.error) return fail(res, r.status || 400, r.error);
      return ok(res, { block: params.name, ...r });
    },
  },
];
