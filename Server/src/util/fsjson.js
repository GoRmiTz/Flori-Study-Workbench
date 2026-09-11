"use strict";
// ============================================================
//  fsjson.js — JSON 文件持久化（原子写 + 内存缓存）
//  写入策略：先写 <file>.tmp 再 rename，避免断电/崩溃留下半截文件。
// ============================================================
const fs = require("node:fs");
const path = require("node:path");

/** 读 JSON；文件不存在或损坏时返回 def 的深拷贝。 */
function readJSON(file, def) {
  try {
    const raw = fs.readFileSync(file, "utf8");
    if (!raw.trim()) return clone(def);
    return JSON.parse(raw);
  } catch (e) {
    return clone(def);
  }
}

/** 原子写 JSON。返回是否成功。 */
function writeJSON(file, obj, pretty) {
  try {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const tmp = file + ".tmp";
    fs.writeFileSync(tmp, JSON.stringify(obj, null, pretty ? 2 : 0), "utf8");
    fs.renameSync(tmp, file);
    return true;
  } catch (e) {
    return false;
  }
}

/** 写原始文本（同样走原子 rename）。 */
function writeRaw(file, buf) {
  try {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const tmp = file + ".tmp";
    fs.writeFileSync(tmp, buf);
    fs.renameSync(tmp, file);
    return true;
  } catch (e) {
    return false;
  }
}

function clone(v) {
  if (v === undefined || v === null) return v;
  return JSON.parse(JSON.stringify(v));
}

module.exports = { readJSON, writeJSON, writeRaw, clone };
