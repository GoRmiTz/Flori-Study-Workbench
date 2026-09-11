// 临时验证：服务端干净启动 + EADDRINUSE 友好提示。自清理，跑完不残留进程。
"use strict";
const { spawn } = require("node:child_process");
const http = require("node:http");

const PORT = 8787;
const HOST = "127.0.0.1";

function healthOK() {
  return new Promise((resolve) => {
    const req = http.get(`http://${HOST}:${PORT}/health`, (res) => {
      let b = "";
      res.on("data", (c) => (b += c));
      res.on("end", () => {
        try { resolve(JSON.parse(b).ok === true); } catch { resolve(false); }
      });
    });
    req.on("error", () => resolve(false));
    req.setTimeout(1500, () => { req.destroy(); resolve(false); });
  });
}

async function waitForUp(retries = 20) {
  for (let i = 0; i < retries; i++) {
    if (await healthOK()) return true;
    await new Promise((r) => setTimeout(r, 250));
  }
  return false;
}

(async () => {
  // 1) 起第一个实例（应当成功监听）
  const srv1 = spawn(process.execPath, ["server.js"], { cwd: __dirname + "/..", stdio: ["ignore", "pipe", "pipe"] });
  let out1 = "";
  srv1.stdout.on("data", (c) => (out1 += c));
  srv1.stderr.on("data", (c) => (out1 += c));

  const up = await waitForUp();
  console.log("[1] 第一个实例健康检查:", up ? "OK" : "FAIL");
  console.log("[1] 启动输出:\n" + out1.split("\n").map((s) => "    " + s).join("\n"));

  if (!up) { try { srv1.kill("SIGKILL"); } catch {} console.log("RESULT: FAIL (第一个实例没起来)"); process.exit(1); }

  // 2) 起第二个实例撞端口（应当打印友好提示并退出码 1）
  const srv2 = spawn(process.execPath, ["server.js"], { cwd: __dirname + "/..", stdio: ["ignore", "pipe", "pipe"] });
  let out2 = "";
  srv2.stdout.on("data", (c) => (out2 += c));
  srv2.stderr.on("data", (c) => (out2 += c));

  const code2 = await new Promise((resolve) => srv2.on("exit", resolve));
  console.log("\n[2] 第二个实例退出码:", code2, "(期望非 0)");
  console.log("[2] 撞端口输出:\n" + out2.split("\n").map((s) => "    " + s).join("\n"));

  const friendly = out2.includes("已被占用") && !out2.includes("throw er");
  console.log("\n[2] 友好提示(含'已被占用'且无堆栈):", friendly ? "PASS" : "FAIL");

  // 3) 清理：杀掉第一个实例
  try { srv1.kill("SIGKILL"); } catch {}
  // 等端口释放
  for (let i = 0; i < 20; i++) {
    if (!(await healthOK())) { console.log("\n[3] 端口已释放，无残留进程"); break; }
    await new Promise((r) => setTimeout(r, 200));
  }

  console.log("\nRESULT:", (up && code2 !== 0 && friendly) ? "PASS" : "FAIL");
  process.exit((up && code2 !== 0 && friendly) ? 0 : 1);
})();
