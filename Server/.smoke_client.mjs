// Headless contract test: exercises the exact protocol the desktop client (Cloud.cpp) uses.
const BASE = "http://127.0.0.1:8799";

function auth(token) {
  return token ? { Authorization: "Bearer " + token } : {};
}
async function j(method, path, body, token) {
  const res = await fetch(BASE + path, {
    method,
    headers: { "Content-Type": "application/json", ...auth(token) },
    body: body ? JSON.stringify(body) : undefined,
  });
  const text = await res.text();
  let data = null;
  try { data = JSON.parse(text); } catch {}
  return { status: res.status, data, raw: text };
}

const log = (...a) => console.log(...a);

(async () => {
  let ok = true;
  const fail = (m) => { ok = false; log("  ✗ " + m); };
  const pass = (m) => log("  ✓ " + m);

  // 1) health
  log("[1] GET /health");
  let r = await j("GET", "/health");
  if (r.status === 200 && r.data && r.data.service) pass("health ok: " + r.data.service + " v" + r.data.version);
  else fail("health unexpected: " + r.status + " " + r.raw);

  const cred = { username: "smoke01", password: "pass1234" };

  // 2) register
  log("[2] POST /auth/register");
  r = await j("POST", "/auth/register", cred);
  if (r.status === 200 && r.data.accessToken && r.data.refreshToken && r.data.user && r.data.user.uid && r.data.user.username) {
    pass("register ok, uid=" + r.data.user.uid + " user=" + r.data.user.username + " expiresIn=" + r.data.expiresIn);
  } else fail("register unexpected: " + r.status + " " + r.raw);
  const at = r.data.accessToken;

  // 3) login
  log("[3] POST /auth/login");
  r = await j("POST", "/auth/login", cred);
  if (r.status === 200 && r.data.accessToken) pass("login ok");
  else { fail("login unexpected: " + r.status + " " + r.raw); }

  // 4) push full payload (mode=merge)
  log("[4] PUT /sync/all?mode=merge (full 6-block payload)");
  const payload = {
    app: "FLORI", version: 1, account: "smoke01", exportedAt: 1754270000,
    checkin: { "2026-08-04": { "早起": 1, "读书": 0 } },
    focus: [ { date: "2026-08-04", start: 1754270000, end: 1754270060, min: 1, tag: "x" } ],
    items: { daily: ["A"], sat: ["B"], sun: [] },
    journal: { "2026-08-04": { summary: "ok", next: "go" } },
    rhythm: { "2026-08-04": { wake: "07:00", sleep: "23:00" } },
    settings: { dark: true },
  };
  r = await j("PUT", "/sync/all?mode=merge", payload, at);
  if (r.status === 200 && r.data.written && Object.keys(r.data.written).length === 6) pass("push ok, written blocks=" + Object.keys(r.data.written).join(","));
  else fail("push unexpected: " + r.status + " " + r.raw);

  // 5) pull
  log("[5] GET /sync/all");
  r = await j("GET", "/sync/all", null, at);
  if (r.status !== 200) { fail("pull status " + r.status + " " + r.raw); }
  else {
    const d = r.data;
    if (d.app === "FLORI") pass("pull has app=FLORI");
    else fail("pull missing app=FLORI");
    // top-level block data present
    const blocks = ["checkin","focus","items","journal","rhythm","settings"];
    let allBlocks = true;
    for (const b of blocks) if (!(b in d)) { allBlocks = false; fail("pull missing top-level block: " + b); }
    if (allBlocks) pass("pull has all 6 top-level blocks");
    // meta must be NESTED, not clobbering blocks
    if (d.meta && d.meta.checkin && typeof d.meta.checkin === "object" && "rev" in d.meta.checkin) pass("meta nested under .meta (rev present), does not clobber block data");
    else fail("meta not nested correctly: " + JSON.stringify(d.meta));
    // verify checkin content survived (not overwritten by meta)
    if (d.checkin && d.checkin["2026-08-04"] && d.checkin["2026-08-04"]["早起"] === 1) pass("checkin data intact after round-trip");
    else fail("checkin data corrupted: " + JSON.stringify(d.checkin));
    // settings dark bool
    if (d.settings && d.settings.dark === true) pass("settings.dark=true preserved");
    else fail("settings.dark missing: " + JSON.stringify(d.settings));
  }

  // 6) incremental push: only checkin updated, items kept (merge must not clobber)
  log("[6] PUT /sync/all?mode=merge (only checkin changed)");
  const inc = {
    app: "FLORI", version: 1, account: "smoke01", exportedAt: 1754271000,
    checkin: { "2026-08-04": { "早起": 1, "读书": 1 }, "2026-08-05": { "运动": 1 } },
  };
  r = await j("PUT", "/sync/all?mode=merge", inc, at);
  if (r.status === 200) pass("incremental push ok");
  else fail("incremental push: " + r.status + " " + r.raw);
  r = await j("GET", "/sync/all", null, at);
  if (r.data.items && Array.isArray(r.data.items.daily) && r.data.items.daily[0] === "A") pass("items preserved through merge (not clobbered)");
  else fail("items clobbered by merge: " + JSON.stringify(r.data.items));
  if (r.data.checkin["2026-08-05"] && r.data.checkin["2026-08-05"]["运动"] === 1) pass("new checkin day merged in");
  else fail("new checkin day missing: " + JSON.stringify(r.data.checkin));

  // 7) refresh token
  log("[7] POST /auth/refresh");
  const reg = await j("POST", "/auth/login", cred);
  const refresh = reg.data.refreshToken;
  r = await j("POST", "/auth/refresh", { refreshToken: refresh });
  if (r.status === 200 && r.data.accessToken) pass("refresh ok, new accessToken issued");
  else fail("refresh unexpected: " + r.status + " " + r.raw);

  // 8) 404 path: login with unknown account returns 404 (client maps NotFound)
  log("[8] POST /auth/login (unknown account → 404)");
  r = await j("POST", "/auth/login", { username: "nope_xyz", password: "pass1234" });
  if (r.status === 404) pass("unknown account → 404 (client treats as NotFound/offline-mirror)");
  else fail("expected 404, got " + r.status + " " + r.raw);

  log(ok ? "\nRESULT: ALL PASS ✅" : "\nRESULT: FAILURES ❌");
  process.exit(ok ? 0 : 1);
})().catch((e) => { console.error("HARNESS ERROR", e); process.exit(2); });
