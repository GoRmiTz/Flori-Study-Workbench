"use strict";
// ============================================================
//  routes/auth.js — §1 用户中心 & 鉴权
//  对应桌面端占位：AccountStore（本地 FNV-1a）→ 升级为服务端 scrypt + JWT
// ============================================================
const auth = require("../core/auth");
const users = require("../core/users");
const cfg = require("../config");
const { ok, fail } = require("../util/http");

/** 统一的登录成功响应：访问令牌 + 刷新令牌 + 用户信息。 */
function issue(res, user) {
  return ok(res, {
    user: users.publicUser(user),
    accessToken: auth.sign({ sub: user.uid, name: user.username, tv: user.tv || 1, typ: "access" }, cfg.ACCESS_TTL),
    refreshToken: auth.sign({ sub: user.uid, name: user.username, tv: user.tv || 1, typ: "refresh" }, cfg.REFRESH_TTL),
    expiresIn: cfg.ACCESS_TTL,
  });
}

module.exports = [
  {
    method: "POST", path: "/auth/register", auth: false,
    handler: ({ res, body }) => {
      const r = users.register(body.username, body.password);
      if (r.error) return fail(res, r.status || 400, r.error);
      return issue(res, r.user);
    },
  },
  {
    method: "POST", path: "/auth/login", auth: false,
    handler: ({ res, body }) => {
      const r = users.login(body.username, body.password);
      if (r.error) return fail(res, r.status || 401, r.error);
      return issue(res, r.user);
    },
  },
  {
    // 用刷新令牌换新的访问令牌；tv 不匹配（改密 / 退出全部设备）则拒绝
    method: "POST", path: "/auth/refresh", auth: false,
    handler: ({ res, body }) => {
      const p = auth.verify(body.refreshToken);
      if (!p || p.typ !== "refresh") return fail(res, 401, "刷新令牌无效或已过期");
      const user = users.findByUid(p.sub);
      if (!user) return fail(res, 404, "用户不存在");
      if ((p.tv || 1) !== (user.tv || 1)) return fail(res, 401, "登录状态已失效，请重新登录");
      return issue(res, user);
    },
  },
  {
    // 默认为无状态登出（客户端丢弃令牌）；body.all=true 时抬高 tv 踢掉全部设备
    method: "POST", path: "/auth/logout", auth: true,
    handler: ({ res, body, user }) => {
      if (body && body.all) users.bumpTokenVersion(user.sub);
      return ok(res, { all: !!(body && body.all) });
    },
  },
  {
    method: "GET", path: "/auth/me", auth: true,
    handler: ({ res, user }) => {
      const u = users.findByUid(user.sub);
      if (!u) return fail(res, 404, "用户不存在");
      return ok(res, { user: users.publicUser(u) });
    },
  },
  {
    // 「我的」档案上云（P1#3）：bios/major/school/birthday/gender 落盘并返回最新用户视图
    method: "PUT", path: "/auth/me", auth: true,
    handler: ({ res, body, user }) => {
      const r = users.updateProfile(user.sub, body || {});
      if (r.error) return fail(res, r.status || 400, r.error);
      return ok(res, { user: users.publicUser(r.user) });
    },
  },
  {
    method: "POST", path: "/auth/password", auth: true,
    handler: ({ res, body, user }) => {
      const u = users.findByUid(user.sub);
      if (!u) return fail(res, 404, "用户不存在");
      if (!auth.verifyPassword(body.oldPassword, u.pass)) return fail(res, 401, "原密码不正确");
      const r = users.changePassword(user.sub, body.newPassword);
      if (r.error) return fail(res, r.status || 400, r.error);
      return issue(res, r.user);   // 改密后直接换发新令牌，避免用户被自己踢下线
    },
  },
];
