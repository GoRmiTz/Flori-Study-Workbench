"use strict";
// ============================================================
//  columns.js — §4 公共专栏（分区 / 文章 / 点赞 / 收藏 / 评论）
//  落盘：data/columns.json = { sections: [...], posts: [...] }
//  与网页端 server.js 的 col_* WebSocket 消息语义对齐，改为 REST + 事件广播。
// ============================================================
const crypto = require("node:crypto");
const cfg = require("../config");
const { readJSON, writeJSON } = require("../util/fsjson");

/** 默认分区：对应桌面端 RoadmapView 的分区轮播占位。 */
const DEFAULT_SECTIONS = [
  { id: "roadmap",   name: "总线路图", desc: "个人长期规划与决策路径" },
  { id: "gongkao",   name: "考公",     desc: "行测 / 申论 / 时政 / 选岗" },
  { id: "kaoyan",    name: "考研",     desc: "专业课 / 英语 / 政治" },
  { id: "method",    name: "方法论",   desc: "学习方法、工具与复盘" },
  { id: "general",   name: "随笔",     desc: "不限主题" },
];

const DEFAULT = { version: 1, sections: DEFAULT_SECTIONS, posts: [] };
let db = readJSON(cfg.COLUMNS_FILE, DEFAULT);
if (!Array.isArray(db.sections) || db.sections.length === 0) db.sections = DEFAULT_SECTIONS;
if (!Array.isArray(db.posts)) db.posts = [];

function persist() { writeJSON(cfg.COLUMNS_FILE, db); }
const genId = (p) => p + "_" + Date.now().toString(36) + crypto.randomBytes(3).toString("hex");
const clamp = (s, n) => String(s === undefined || s === null ? "" : s).slice(0, n);

const sections = () => db.sections;

/** 列表视图：隐藏文章仅作者可见；不返回正文，减小载荷。 */
function list(viewerUid, sectionId) {
  return db.posts
    .filter((p) => !p.hidden || p.authorUid === viewerUid)
    .filter((p) => !sectionId || p.sectionId === sectionId)
    .sort((a, b) => b.createdAt - a.createdAt)
    .map((p) => summary(p, viewerUid));
}

function summary(p, viewerUid) {
  return {
    id: p.id, title: p.title, sectionId: p.sectionId,
    author: p.author, authorUid: p.authorUid,
    createdAt: p.createdAt, updatedAt: p.updatedAt,
    excerpt: p.body.slice(0, 120),
    likeCount: p.likers.length, commentCount: p.comments.length,
    favCount: p.favers.length,
    liked: p.likers.includes(viewerUid), faved: p.favers.includes(viewerUid),
    hidden: !!p.hidden, mine: p.authorUid === viewerUid,
  };
}

function get(id, viewerUid) {
  const p = db.posts.find((x) => x.id === id);
  if (!p) return null;
  if (p.hidden && p.authorUid !== viewerUid) return null;
  return { ...summary(p, viewerUid), body: p.body, comments: p.comments };
}

function create(user, input) {
  const sectionId = db.sections.some((s) => s.id === input.sectionId) ? input.sectionId : "general";
  const now = Date.now();
  const post = {
    id: genId("col"),
    title: clamp(input.title || "未命名专栏", 100),
    body: clamp(input.body, 20000),
    sectionId,
    author: user.name, authorUid: user.sub,
    createdAt: now, updatedAt: now,
    hidden: !!input.hidden,
    likers: [], favers: [], comments: [],
  };
  db.posts.push(post);
  persist();
  return post;
}

function update(id, user, input) {
  const p = db.posts.find((x) => x.id === id);
  if (!p) return { error: "专栏不存在", status: 404 };
  if (p.authorUid !== user.sub) return { error: "只能编辑自己的专栏", status: 403 };
  if (input.title !== undefined) p.title = clamp(input.title, 100);
  if (input.body !== undefined) p.body = clamp(input.body, 20000);
  if (input.sectionId !== undefined && db.sections.some((s) => s.id === input.sectionId)) p.sectionId = input.sectionId;
  if (input.hidden !== undefined) p.hidden = !!input.hidden;
  p.updatedAt = Date.now();
  persist();
  return { post: p };
}

function remove(id, user) {
  const i = db.posts.findIndex((x) => x.id === id);
  if (i < 0) return { error: "专栏不存在", status: 404 };
  if (db.posts[i].authorUid !== user.sub) return { error: "只能删除自己的专栏", status: 403 };
  db.posts.splice(i, 1);
  persist();
  return { removed: id };
}

/** 点赞 / 收藏：同一动作再次调用即取消（toggle）。 */
function toggle(id, user, field) {
  const p = db.posts.find((x) => x.id === id);
  if (!p) return { error: "专栏不存在", status: 404 };
  const arr = p[field];
  const i = arr.indexOf(user.sub);
  if (i >= 0) arr.splice(i, 1); else arr.push(user.sub);
  persist();
  return { on: i < 0, count: arr.length };
}

function comment(id, user, text) {
  const p = db.posts.find((x) => x.id === id);
  if (!p) return { error: "专栏不存在", status: 404 };
  const t = clamp(text, 500).trim();
  if (!t) return { error: "评论内容为空", status: 400 };
  const c = { id: genId("cm"), user: user.name, uid: user.sub, text: t, ts: Date.now() };
  p.comments.push(c);
  if (p.comments.length > 200) p.comments = p.comments.slice(-200);
  persist();
  return { comment: c, count: p.comments.length };
}

module.exports = { sections, list, get, create, update, remove, toggle, comment };
