"use strict";
// ============================================================
//  ws.js — 极简 WebSocket 服务端（RFC 6455 子集，零第三方依赖）
//  支持：握手、文本/二进制帧、分片续帧、ping/pong 心跳、close 握手。
//  不支持：permessage-deflate 扩展、子协议协商（本项目用不到）。
// ============================================================
const crypto = require("node:crypto");
const { EventEmitter } = require("node:events");

const GUID = "258EAFA5-E914-47DA-95CA-5AB0DC85B11F";
const MAX_MESSAGE = 4 * 1024 * 1024;   // 单条消息上限 4MB
const OP = { CONT: 0x0, TEXT: 0x1, BIN: 0x2, CLOSE: 0x8, PING: 0x9, PONG: 0xa };

class Conn extends EventEmitter {
  constructor(socket, req) {
    super();
    this.socket = socket;
    this.req = req;
    this.alive = true;
    this.closed = false;
    this.data = {};                 // 业务侧挂载（user / room / presence 等）
    this._buf = Buffer.alloc(0);
    this._fragOp = 0;
    this._frags = [];

    socket.setNoDelay(true);
    socket.on("data", (d) => this._onData(d));
    socket.on("close", () => this._finish());
    socket.on("error", () => this._finish());
  }

  _onData(chunk) {
    this._buf = Buffer.concat([this._buf, chunk]);
    if (this._buf.length > MAX_MESSAGE * 2) { this.close(1009, "缓冲区溢出"); return; }
    while (!this.closed && this._readFrame()) { /* 逐帧消费 */ }
  }

  /** 从缓冲区尝试解析一帧；数据不足返回 false 等待更多字节。 */
  _readFrame() {
    const b = this._buf;
    if (b.length < 2) return false;
    const fin = (b[0] & 0x80) !== 0;
    const opcode = b[0] & 0x0f;
    const masked = (b[1] & 0x80) !== 0;
    let len = b[1] & 0x7f;
    let off = 2;

    if (len === 126) {
      if (b.length < off + 2) return false;
      len = b.readUInt16BE(off); off += 2;
    } else if (len === 127) {
      if (b.length < off + 8) return false;
      const big = b.readBigUInt64BE(off);
      if (big > BigInt(MAX_MESSAGE)) { this.close(1009, "消息过大"); return false; }
      len = Number(big); off += 8;
    }

    let mask = null;
    if (masked) {
      if (b.length < off + 4) return false;
      mask = b.subarray(off, off + 4); off += 4;
    }
    if (b.length < off + len) return false;

    const payload = Buffer.from(b.subarray(off, off + len));
    if (mask) for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
    this._buf = b.subarray(off + len);

    this._dispatch(fin, opcode, payload);
    return true;
  }

  _dispatch(fin, opcode, payload) {
    if (opcode === OP.CLOSE) { this.close(1000, ""); return; }
    if (opcode === OP.PING) { this._write(OP.PONG, payload); return; }
    if (opcode === OP.PONG) { this.alive = true; return; }

    if (opcode === OP.CONT) {
      if (!this._fragOp) return;                 // 无起始帧的续帧，丢弃
      this._frags.push(payload);
    } else {
      if (!fin) { this._fragOp = opcode; this._frags = [payload]; return; }
      this._emit(opcode, payload);
      return;
    }
    if (fin) {
      const op = this._fragOp;
      const full = Buffer.concat(this._frags);
      this._fragOp = 0; this._frags = [];
      this._emit(op, full);
    }
  }

  _emit(opcode, payload) {
    if (payload.length > MAX_MESSAGE) { this.close(1009, "消息过大"); return; }
    if (opcode === OP.TEXT) this.emit("message", payload.toString("utf8"));
    else if (opcode === OP.BIN) this.emit("binary", payload);
  }

  /** 服务端 → 客户端不加掩码（RFC 6455 §5.1）。 */
  _write(opcode, payload) {
    if (this.closed || this.socket.destroyed) return;
    const len = payload.length;
    let header;
    if (len < 126) {
      header = Buffer.alloc(2);
      header[1] = len;
    } else if (len < 65536) {
      header = Buffer.alloc(4);
      header[1] = 126;
      header.writeUInt16BE(len, 2);
    } else {
      header = Buffer.alloc(10);
      header[1] = 127;
      header.writeBigUInt64BE(BigInt(len), 2);
    }
    header[0] = 0x80 | opcode;
    try { this.socket.write(Buffer.concat([header, payload])); } catch (e) { this._finish(); }
  }

  send(text) { this._write(OP.TEXT, Buffer.from(String(text), "utf8")); }
  sendJSON(obj) { this.send(JSON.stringify(obj)); }
  ping() { this.alive = false; this._write(OP.PING, Buffer.alloc(0)); }

  close(code, reason) {
    if (this.closed) return;
    const r = Buffer.from(String(reason || ""), "utf8");
    const payload = Buffer.alloc(2 + r.length);
    payload.writeUInt16BE(code || 1000, 0);
    r.copy(payload, 2);
    this._write(OP.CLOSE, payload);
    this._finish();
    try { this.socket.end(); } catch (e) { /* 已断开 */ }
  }

  _finish() {
    if (this.closed) return;
    this.closed = true;
    this.emit("close");
  }
}

/**
 * 挂到既有 http.Server 上（与 REST 同端口同源）。
 * @param onConnection (conn, req) => void
 */
function attach(server, onConnection) {
  const conns = new Set();

  server.on("upgrade", (req, socket) => {
    const key = req.headers["sec-websocket-key"];
    const upgrade = String(req.headers["upgrade"] || "").toLowerCase();
    if (upgrade !== "websocket" || !key) {
      socket.write("HTTP/1.1 400 Bad Request\r\n\r\n");
      socket.destroy();
      return;
    }
    const accept = crypto.createHash("sha1").update(key + GUID).digest("base64");
    socket.write(
      "HTTP/1.1 101 Switching Protocols\r\n" +
      "Upgrade: websocket\r\n" +
      "Connection: Upgrade\r\n" +
      `Sec-WebSocket-Accept: ${accept}\r\n\r\n`
    );
    const conn = new Conn(socket, req);
    conns.add(conn);
    conn.on("close", () => conns.delete(conn));
    onConnection(conn, req);
  });

  // 心跳：30s 一轮，上一轮没回 pong 的连接判定为掉线
  const timer = setInterval(() => {
    for (const c of conns) {
      if (!c.alive) { c.close(1001, "心跳超时"); continue; }
      c.ping();
    }
  }, 30000);
  timer.unref?.();

  return { conns };
}

module.exports = { attach, Conn, MAX_MESSAGE };
