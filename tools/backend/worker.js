// xBloom Panel backend (Cloudflare Worker + D1).
//  POST /v1/ping   {id, ver, build, beta}           one anonymous check-in per panel per day
//  POST /v1/crash  {id, ver, build, reason, task, pc, bt}   a crash report the owner chose to send
//  GET  /stats?key=…    panels active today / 7 / 30 days, by version (owner only)
//  GET  /crashes?key=…  the latest crash reports (owner only)
//  ?test=1 on a POST: the emulator test; checked, nothing kept
// No IP addresses or personal data are stored: a panel is a random ID it made itself.
const SCHEMA = [
  `CREATE TABLE IF NOT EXISTS panels (id TEXT PRIMARY KEY, ver TEXT, build INTEGER, beta INTEGER, first_seen INTEGER, last_seen INTEGER)`,
  `CREATE TABLE IF NOT EXISTS crashes (at INTEGER, id TEXT, ver TEXT, build INTEGER, reason TEXT, task TEXT, pc TEXT, bt TEXT)`,
];
const json = (o, s = 200) => new Response(JSON.stringify(o, null, 2), { status: s, headers: { "content-type": "application/json" } });
const clip = (v, n) => String(v ?? "").slice(0, n);
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

async function body(req) {
  const t = await req.text();
  if (t.length > 4096) throw new Error("too big");
  const o = JSON.parse(t);
  if (!/^[0-9a-f]{16}$/.test(o.id || "")) throw new Error("bad id");
  return o;
}

function page(title, inner) {
  return new Response(`<!doctype html><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>${title}</title>
<style>:root{color-scheme:dark}body{background:#000;color:#fff;font:15px -apple-system,system-ui,sans-serif;max-width:760px;margin:auto;padding:24px}
h1{font-size:24px}.big{display:flex;gap:12px;flex-wrap:wrap}.c{background:#1c1c1e;border-radius:14px;padding:16px 20px;min-width:140px}.c b{display:block;font-size:34px}
table{width:100%;border-collapse:collapse;margin-top:16px}td,th{text-align:left;padding:8px;border-bottom:1px solid #2c2c2e;vertical-align:top}pre{white-space:pre-wrap;margin:0;color:#aeaeb2;font-size:12px}</style>
<h1>${title}</h1>${inner}`, { headers: { "content-type": "text/html; charset=utf-8" } });
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    const db = env.DB;
    const now = Math.floor(Date.now() / 1000);
    if (url.pathname === "/v1/ping" && req.method === "POST") {
      let o; try { o = await body(req); } catch { return json({ ok: false }, 400); }
      if (url.searchParams.has("test")) return json({ ok: true, test: true });  // the emulator test: checked, not kept
      await db.prepare(`INSERT INTO panels (id, ver, build, beta, first_seen, last_seen) VALUES (?1, ?2, ?3, ?4, ?5, ?5)
        ON CONFLICT(id) DO UPDATE SET ver = ?2, build = ?3, beta = ?4, last_seen = ?5`)
        .bind(o.id, clip(o.ver, 24), o.build | 0, o.beta ? 1 : 0, now).run();
      return json({ ok: true });
    }
    if (url.pathname === "/v1/crash" && req.method === "POST") {
      let o; try { o = await body(req); } catch { return json({ ok: false }, 400); }
      if (url.searchParams.has("test")) return json({ ok: true, test: true });
      const recent = await db.prepare(`SELECT COUNT(*) n FROM crashes WHERE id = ?1 AND at > ?2`).bind(o.id, now - 3600).first();
      if (recent.n >= 5) return json({ ok: true, dropped: true });  // a panel in a crash loop sends at most 5 an hour
      await db.prepare(`INSERT INTO crashes VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)`)
        .bind(now, o.id, clip(o.ver, 24), o.build | 0, clip(o.reason, 64), clip(o.task, 32), clip(o.pc, 128), clip(o.bt, 1500)).run();
      return json({ ok: true });
    }
    if (url.searchParams.get("key") !== env.ADMIN_KEY) return new Response("Not found", { status: 404 });
    if (url.pathname === "/stats") {
      const n = async (s) => (await db.prepare(`SELECT COUNT(*) n FROM panels WHERE last_seen > ?1`).bind(now - s).first()).n;
      const [d1, d7, d30] = [await n(86400), await n(7 * 86400), await n(30 * 86400)];
      const total = (await db.prepare(`SELECT COUNT(*) n FROM panels`).first()).n;
      const vers = (await db.prepare(`SELECT ver, beta, COUNT(*) n FROM panels WHERE last_seen > ?1 GROUP BY ver, beta ORDER BY n DESC`).bind(now - 30 * 86400).all()).results;
      if (url.searchParams.get("format") === "json") return json({ today: d1, week: d7, month: d30, ever: total, versions: vers });
      return page("☕ xBloom Panels",
        `<div class=big><div class=c><b>${d1}</b>active today</div><div class=c><b>${d7}</b>this week</div><div class=c><b>${d30}</b>this month</div><div class=c><b>${total}</b>ever</div></div>
<table><tr><th>Version (last 30 days)</th><th>Panels</th></tr>${vers.map((v) => `<tr><td>${esc(v.ver)}${v.beta ? " · beta" : ""}</td><td>${v.n}</td></tr>`).join("")}</table>`);
    }
    if (url.pathname === "/crashes") {
      const rows = (await db.prepare(`SELECT * FROM crashes ORDER BY at DESC LIMIT 50`).all()).results;
      return page("🧯 Crash reports",
        `<p>${rows.length ? "Latest first." : "None yet. 🎉"} Decode one with the "Decode a crash" GitHub workflow (tag + backtrace).</p><table><tr><th>When (UTC)</th><th>Version</th><th>What</th></tr>${rows.map((r) =>
          `<tr><td>${new Date(r.at * 1000).toISOString().slice(0, 16).replace("T", " ")}</td><td>${esc(r.ver)}<br><small>panel ${esc(r.id.slice(0, 6))}</small></td><td>${esc(r.reason)} in ${esc(r.task)}<pre>pc ${esc(r.pc)}\n${esc(r.bt)}</pre></td></tr>`).join("")}</table>`);
    }
    if (url.pathname === "/setup") {
      for (const s of SCHEMA) await db.prepare(s).run();
      return json({ ok: true });
    }
    return new Response("Not found", { status: 404 });
  },
};
