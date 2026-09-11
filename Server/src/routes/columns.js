"use strict";
// ============================================================
//  routes/columns.js — §4 公共专栏 REST 接口
//  写操作成功后经实时网关广播 col:changed，客户端可即时刷新列表。
// ============================================================
const columns = require("../core/columns");
const { ok, fail } = require("../util/http");

let gateway = null;
const attachGateway = (g) => { gateway = g; };
const notify = (event, data) => { if (gateway) gateway.broadcast({ type: event, ...data }); };

const routes = [
  {
    method: "GET", path: "/columns", authOptional: true,   // 公开可读：访客也能看，登录后多标记 mine/liked
    handler: ({ res, user, query }) => ok(res, {
      sections: columns.sections(),
      posts: columns.list(user ? user.sub : null, query.get("section") || ""),
    }),
  },
  {
    method: "POST", path: "/columns", auth: true,
    handler: ({ res, user, body }) => {
      const p = columns.create(user, body || {});
      notify("col:changed", { action: "create", id: p.id });
      return ok(res, { id: p.id });
    },
  },
  {
    method: "GET", path: "/columns/:id", auth: true,
    handler: ({ res, user, params }) => {
      const p = columns.get(params.id, user.sub);
      if (!p) return fail(res, 404, "专栏不存在或不可见");
      return ok(res, { post: p });
    },
  },
  {
    method: "PUT", path: "/columns/:id", auth: true,
    handler: ({ res, user, params, body }) => {
      const r = columns.update(params.id, user, body || {});
      if (r.error) return fail(res, r.status, r.error);
      notify("col:changed", { action: "update", id: params.id });
      return ok(res, { id: params.id });
    },
  },
  {
    method: "DELETE", path: "/columns/:id", auth: true,
    handler: ({ res, user, params }) => {
      const r = columns.remove(params.id, user);
      if (r.error) return fail(res, r.status, r.error);
      notify("col:changed", { action: "delete", id: params.id });
      return ok(res, { id: params.id });
    },
  },
  {
    method: "POST", path: "/columns/:id/like", auth: true,
    handler: ({ res, user, params }) => {
      const r = columns.toggle(params.id, user, "likers");
      if (r.error) return fail(res, r.status, r.error);
      notify("col:changed", { action: "like", id: params.id });
      return ok(res, r);
    },
  },
  {
    method: "POST", path: "/columns/:id/fav", auth: true,
    handler: ({ res, user, params }) => {
      const r = columns.toggle(params.id, user, "favers");
      if (r.error) return fail(res, r.status, r.error);
      return ok(res, r);
    },
  },
  {
    method: "POST", path: "/columns/:id/comment", auth: true,
    handler: ({ res, user, params, body }) => {
      const r = columns.comment(params.id, user, (body || {}).text);
      if (r.error) return fail(res, r.status, r.error);
      notify("col:changed", { action: "comment", id: params.id });
      return ok(res, r);
    },
  },
];

module.exports = routes;
module.exports.attachGateway = attachGateway;
