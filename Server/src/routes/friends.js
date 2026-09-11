"use strict";
// ============================================================
//  routes/friends.js — #37 好友系统 REST 接口
//  GET    /friends         好友列表（含在线状态 + 进度统计）
//  POST   /friends/add     按用户名添加好友（双向互加）
//  DELETE /friends/:uid    删除好友（双向移除）
//  私聊走实时网关：chat:dm / dm:history（见 gateway.js）
// ============================================================
const friends = require("../core/friends");
const users = require("../core/users");
const { ok, fail } = require("../util/http");

let gateway = null;
const attachGateway = (g) => { gateway = g; };

module.exports = [
  {
    method: "GET", path: "/friends", auth: true,
    handler: ({ res, user }) => {
      const list = friends.friends(user.sub, (uid) => gateway && gateway.onlineUid(uid));
      for (const f of list) {          // 补用户名（core 只返回 uid + 统计）
        const u = users.findByUid(f.uid);
        if (u) f.username = u.username;
      }
      return ok(res, { friends: list });
    },
  },
  {
    method: "POST", path: "/friends/add", auth: true,
    handler: ({ res, user, body }) => {
      const name = String((body || {}).username || "").trim();
      if (!name) return fail(res, 400, "缺少用户名");
      const target = users.findByName(name);
      if (!target) return fail(res, 404, "用户不存在");
      const r = friends.add(user.sub, target.uid);
      if (r.error) return fail(res, r.status, r.error);
      return ok(res, { uid: target.uid, username: target.username });
    },
  },
  {
    method: "DELETE", path: "/friends/:uid", auth: true,
    handler: ({ res, user, params }) => {
      friends.remove(user.sub, params.uid);
      return ok(res, { id: params.uid });
    },
  },
];
module.exports.attachGateway = attachGateway;
