"use strict";
// ============================================================
//  http.js — HTTP 层小工具：响应、请求体读取、路由匹配、CORS
// ============================================================
const cfg = require("../config");

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET,POST,PUT,DELETE,OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type,Authorization,X-Meta",
  "Access-Control-Max-Age": "86400",
};

function sendJSON(res, status, obj) {
  const body = Buffer.from(JSON.stringify(obj), "utf8");
  res.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": body.length,
    ...CORS,
  });
  res.end(body);
}

const ok = (res, obj) => sendJSON(res, 200, { ok: true, ...(obj || {}) });
const fail = (res, status, error, extra) =>
  sendJSON(res, status, { ok: false, error, ...(extra || {}) });

/** 读取并解析 JSON 请求体；超限或非法 JSON 时 reject。 */
function readJSONBody(req, limit) {
  const max = limit || cfg.MAX_BODY;
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    let done = false;
    req.on("data", (c) => {
      if (done) return;
      size += c.length;
      if (size > max) {
        done = true;
        req.destroy();
        reject(Object.assign(new Error("请求体过大"), { status: 413 }));
        return;
      }
      chunks.push(c);
    });
    req.on("error", (e) => { if (!done) { done = true; reject(e); } });
    req.on("end", () => {
      if (done) return;
      done = true;
      const raw = Buffer.concat(chunks).toString("utf8");
      if (!raw.trim()) return resolve({});
      try { resolve(JSON.parse(raw)); }
      catch (e) { reject(Object.assign(new Error("请求体不是合法 JSON"), { status: 400 })); }
    });
  });
}

/** 读取原始字节体（媒体上传用）。 */
function readRawBody(req, limit) {
  const max = limit || cfg.MAX_UPLOAD;
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    let done = false;
    req.on("data", (c) => {
      if (done) return;
      size += c.length;
      if (size > max) {
        done = true;
        req.destroy();
        reject(Object.assign(new Error("文件过大"), { status: 413 }));
        return;
      }
      chunks.push(c);
    });
    req.on("error", (e) => { if (!done) { done = true; reject(e); } });
    req.on("end", () => { if (!done) { done = true; resolve(Buffer.concat(chunks)); } });
  });
}

/**
 * 极简路由匹配：pattern 形如 "/sync/block/:name"。
 * 命中返回 params 对象，否则返回 null。
 */
function match(pattern, pathname) {
  const a = pattern.split("/").filter(Boolean);
  const b = pathname.split("/").filter(Boolean);
  if (a.length !== b.length) return null;
  const params = {};
  for (let i = 0; i < a.length; i++) {
    if (a[i].startsWith(":")) params[a[i].slice(1)] = decodeURIComponent(b[i]);
    else if (a[i] !== b[i]) return null;
  }
  return params;
}

module.exports = { CORS, sendJSON, ok, fail, readJSONBody, readRawBody, match };
