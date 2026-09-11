"use strict";
// ============================================================
//  friends.js — #37 好友系统（好友关系 / 私聊历史 / 进度统计）
//  落盘：data/friends.json = { version, list: { <uid>: [<好友uid>...] } }（双向维护）
//        data/dms.json     = { version, threads: { "<uA>_<uB>": [msg] } }
//   线程键 = 两个 uid 字典序拼接，保证 A↔B 共用一条历史。
//  消息：{ id, from, text, ts }
// ============================================================
const crypto = require("node:crypto");
const cfg = require("../config");
const { readJSON, writeJSON } = require("../util/fsjson");
const sync = require("./sync");

const FR_DEFAULT = { version: 1, list: {} };
const DM_DEFAULT = { version: 1, threads: {} };
let fdb = readJSON(cfg.FRIENDS_FILE, FR_DEFAULT);
let ddb = readJSON(cfg.DMS_FILE, DM_DEFAULT);
if (!fdb.list) fdb.list = {};
if (!ddb.threads) ddb.threads = {};
const saveF = () => writeJSON(cfg.FRIENDS_FILE, fdb);
const saveD = () => writeJSON(cfg.DMS_FILE, ddb);

const threadKey = (a, b) => (a < b ? a + "_" + b : b + "_" + a);

/** 加好友（双向互加）。返回 { error, status } 或 { ok }。 */
function add(uid, friendUid) {
  if (uid === friendUid) return { error: "不能添加自己为好友", status: 400 };
  if (!fdb.list[uid]) fdb.list[uid] = [];
  if (!fdb.list[friendUid]) fdb.list[friendUid] = [];
  if (fdb.list[uid].includes(friendUid)) return { error: "已经是好友了", status: 409 };
  fdb.list[uid].push(friendUid);
  fdb.list[friendUid].push(uid);   // 双向
  saveF();
  return { ok: true };
}

/** 删好友（双向移除）。 */
function remove(uid, friendUid) {
  fdb.list[uid] = (fdb.list[uid] || []).filter((x) => x !== friendUid);
  fdb.list[friendUid] = (fdb.list[friendUid] || []).filter((x) => x !== uid);
  saveF();
  return { ok: true };
}

/** 我的好友 uid 列表。 */
function ids(uid) { return fdb.list[uid] || []; }

/** 发私聊：校验 + 落历史。返回 { msg } 或 { error, status }。 */
function sendDm(from, to, text) {
  const t = String(text || "").trim().slice(0, 2000);
  if (!t) return { error: "消息内容为空", status: 400 };
  const key = threadKey(from, to);
  if (!ddb.threads[key]) ddb.threads[key] = [];
  const msg = { id: crypto.randomBytes(6).toString("hex"), from, text: t, ts: Date.now() };
  ddb.threads[key].push(msg);
  if (ddb.threads[key].length > 500) ddb.threads[key] = ddb.threads[key].slice(-500);
  saveD();
  return { msg };
}

/** 与某用户的私聊历史（最近 200 条）。 */
function dmHistory(uid, withUid) {
  return (ddb.threads[threadKey(uid, withUid)] || []).slice(-200);
}

/** 好友列表（含在线状态 + 公开进度统计）。onlineFn(uid) 由网关注入。 */
function friends(uid, onlineFn) {
  return ids(uid).map((fid) => {
    const st = sync.stats(fid);
    return {
      uid: fid,
      online: !!(onlineFn && onlineFn(fid)),
      days: st.days,
      focusMinutes: st.focusMinutes,
      focusCount: st.focusCount,
      journalCount: st.journalCount,
    };
  });
}

module.exports = { add, remove, ids, sendDm, dmHistory, friends };
