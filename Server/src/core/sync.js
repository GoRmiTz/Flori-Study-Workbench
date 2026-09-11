"use strict";
// ============================================================
//  sync.js — 六数据块云端同步（checkin/focus/items/journal/rhythm/settings）
//
//  落盘：data/users/<uid>/<block>.json + data/users/<uid>/_meta.json
//  _meta: { <block>: { rev, updatedAt } }  —— rev 单调递增，用于客户端判定「云端是否更新」
//
//  冲突策略：last-write-wins + 分块 merge（见 BLOCKS[].merge）。
//  客户端本地优先、离线可写，联网后上推；服务端只保证最终一致。
//
//  ⚠ schema 权威来源是桌面端真实落盘文件（非需求清单 §0 的示意）：
//     focus 元素为 { date, start(epoch秒), end(epoch秒), min, tag }，无 id 字段。
// ============================================================
const path = require("node:path");
const fs = require("node:fs");
const cfg = require("../config");
const { readJSON, writeJSON } = require("../util/fsjson");

/** 块定义：kind 决定空值与类型校验，merge 决定增量合并语义。 */
const BLOCKS = {
  // 打卡档案：{ "YYYY-MM-DD": { "任务标题": 0|1 } }
  checkin:  { kind: "object", empty: () => ({}), merge: mergeByKey },
  // 专注记录：[ { date, start, end, min, tag } ]
  focus:    { kind: "array",  empty: () => ([]), merge: mergeFocus },
  // 打卡项清单：{ daily:[], sat:[], sun:[] }
  items:    { kind: "object", empty: () => ({ daily: [], sat: [], sun: [] }), merge: replace },
  // 每日复盘：{ "YYYY-MM-DD": { summary, next } }
  journal:  { kind: "object", empty: () => ({}), merge: mergeByKey },
  // 作息：{ "YYYY-MM-DD": { wake, sleep } }
  rhythm:   { kind: "object", empty: () => ({}), merge: mergeByKey },
  // 设置：{ dark: bool }
  settings: { kind: "object", empty: () => ({}), merge: shallowMerge },
};
const NAMES = Object.keys(BLOCKS);

function replace(_base, incoming) { return incoming; }
function shallowMerge(base, incoming) { return { ...(base || {}), ...(incoming || {}) }; }
/** 顶层按 key 合并：incoming 的键覆盖 base 的同名键，base 独有键保留。 */
function mergeByKey(base, incoming) {
  const out = { ...(base || {}) };
  for (const [k, v] of Object.entries(incoming || {})) out[k] = v;
  return out;
}
/** 专注记录并集去重（无 id，用 date|start|end|tag 作复合键），按开始时间排序。 */
function mergeFocus(base, incoming) {
  const key = (s) => [s.date, s.start, s.end, s.tag].join("|");
  const map = new Map();
  for (const s of Array.isArray(base) ? base : []) map.set(key(s), s);
  for (const s of Array.isArray(incoming) ? incoming : []) map.set(key(s), s);
  return [...map.values()].sort((a, b) => (a.start || 0) - (b.start || 0));
}

const userDir = (uid) => path.join(cfg.USERS_DIR, uid);
const blockFile = (uid, name) => path.join(userDir(uid), name + ".json");
const metaFile = (uid) => path.join(userDir(uid), "_meta.json");

function loadMeta(uid) {
  const m = readJSON(metaFile(uid), {});
  for (const n of NAMES) if (!m[n]) m[n] = { rev: 0, updatedAt: 0 };
  return m;
}
function saveMeta(uid, m) { writeJSON(metaFile(uid), m); }

/** 类型校验：块必须是声明的 object / array，否则拒绝写入。 */
function validBlock(name, data) {
  const def = BLOCKS[name];
  if (!def) return false;
  if (data === null || data === undefined) return false;
  if (def.kind === "array") return Array.isArray(data);
  return typeof data === "object" && !Array.isArray(data);
}

function getBlock(uid, name) {
  const def = BLOCKS[name];
  if (!def) return null;
  const meta = loadMeta(uid)[name];
  return { data: readJSON(blockFile(uid, name), def.empty()), rev: meta.rev, updatedAt: meta.updatedAt };
}

/**
 * 写入一个块。
 * @param mode "replace"（整块覆盖，默认）| "merge"（按块语义增量合并）
 * @returns { rev, updatedAt } 或 { error }
 */
function putBlock(uid, name, data, mode) {
  const def = BLOCKS[name];
  if (!def) return { error: "未知数据块：" + name, status: 404 };
  if (!validBlock(name, data)) {
    return { error: `数据块 ${name} 类型不符（应为 ${def.kind}）`, status: 400 };
  }
  fs.mkdirSync(userDir(uid), { recursive: true });
  let next = data;
  if (mode === "merge") {
    const base = readJSON(blockFile(uid, name), def.empty());
    next = def.merge(base, data);
  }
  if (!writeJSON(blockFile(uid, name), next)) {
    return { error: "服务端写入失败", status: 500 };
  }
  const meta = loadMeta(uid);
  meta[name] = { rev: (meta[name].rev || 0) + 1, updatedAt: Date.now() };
  saveMeta(uid, meta);
  return { rev: meta[name].rev, updatedAt: meta[name].updatedAt };
}

/** 拉全量：结构与桌面端 *.flori.json 备份文件一致，客户端可直接 ImportAll。 */
function getAll(uid) {
  const meta = loadMeta(uid);
  const blocks = {};
  for (const n of NAMES) blocks[n] = readJSON(blockFile(uid, n), BLOCKS[n].empty());
  return { blocks, meta };
}

/** 推全量：接受 *.flori.json 同构载荷，逐块写入，缺失块跳过。 */
function putAll(uid, payload, mode) {
  const result = {};
  const errors = [];
  for (const n of NAMES) {
    if (!(n in (payload || {}))) continue;
    const r = putBlock(uid, n, payload[n], mode);
    if (r.error) errors.push(`${n}: ${r.error}`);
    else result[n] = r;
  }
  return { written: result, errors };
}

/** 汇总统计（仪表盘 / 分析服务的只读聚合基础）。 */
function stats(uid) {
  const { blocks } = getAll(uid);
  const checkin = blocks.checkin || {};
  const focus = Array.isArray(blocks.focus) ? blocks.focus : [];
  let days = 0, done = 0, total = 0;
  for (const day of Object.values(checkin)) {
    days++;
    for (const v of Object.values(day)) { total++; if (v) done++; }
  }
  return {
    days,
    checkedTotal: done,
    itemsTotal: total,
    focusCount: focus.length,
    focusMinutes: focus.reduce((s, f) => s + (f.min || 0), 0),
    journalCount: Object.keys(blocks.journal || {}).length,
  };
}

module.exports = { BLOCKS, NAMES, getBlock, putBlock, getAll, putAll, stats };
