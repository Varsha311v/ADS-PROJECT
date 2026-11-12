/* Browser end-to-end tests (Playwright + Chromium).
 *   node tests/e2e.js            runs all three scenarios
 * Needs: the backend built (cd backend && make), python3 for the static-only scenario,
 *        and `npm i -D playwright && npx playwright install chromium`. */
const path = require("path");
const os = require("os");
const fs = require("fs");
const net = require("net");
const { spawn } = require("child_process");
const { chromium } = require(process.env.PLAYWRIGHT_PATH || "playwright");

const ROOT = path.join(__dirname, "..");
const BIN = process.env.FF_BIN || path.join(ROOT, "backend", "build", "friend-finder");
let pass = 0, fail = 0;
const check = (name, ok, extra) => { ok ? pass++ : fail++; console.log((ok ? "PASS " : "FAIL ") + name + (ok ? "" : "  " + (extra || ""))); };

const freePort = () => new Promise(r => { const s = net.createServer().listen(0, "127.0.0.1", () => { const p = s.address().port; s.close(() => r(p)); }); });
async function waitUp(port) {
  for (let i = 0; i < 100; i++) {
    try { await new Promise((ok, no) => net.connect(port, "127.0.0.1").once("connect", function () { this.destroy(); ok(); }).once("error", no)); return; }
    catch (e) { await new Promise(r => setTimeout(r, 50)); }
  }
  throw new Error("server did not start on " + port);
}
async function startServer(dataFile) {
  const port = await freePort();
  const p = spawn(BIN, ["--port", String(port), "--bind", "127.0.0.1", "--static", ROOT, "--data", dataFile], { stdio: "ignore" });
  await waitUp(port);
  return { port, p, url: "http://127.0.0.1:" + port + "/" };
}
async function startStatic() {
  const port = await freePort();
  const p = spawn("python3", ["-m", "http.server", String(port), "--bind", "127.0.0.1", "--directory", ROOT], { stdio: "ignore" });
  await waitUp(port);
  return { port, p, url: "http://127.0.0.1:" + port + "/" };
}

/* The shared UI scenario: runs identically against any backend, which is the point. */
async function scenario(label, page, expectKind) {
  const T = (n, ok, x) => check(`[${label}] ${n}`, ok, x);
  const errors = [];
  page.on("pageerror", e => errors.push("pageerror: " + e.message));
  page.on("console", m => m.type() === "error" && !/Failed to load resource|ERR_|404|net::|CORS policy/.test(m.text()) && errors.push("console: " + m.text()));

  await page.waitForFunction(() => /server|browser/.test(document.getElementById("mode").textContent));
  const modeText = await page.locator("#mode").innerText();
  T("mode banner says " + expectKind, expectKind === "server" ? /C\+\+ server/.test(modeText) : /your browser/.test(modeText), modeText);
  T("empty state visible", await page.locator("#empty").isVisible());

  await page.click("#example");
  await page.waitForFunction(() => document.querySelectorAll("#canvas .node").length === 8);
  T("graph visible with real size", await page.evaluate(() => { const r = document.getElementById("canvas").getBoundingClientRect(); return r.width > 300 && r.height > 200; }));
  T("8 edges", (await page.locator("#canvas .edge").count()) === 8);

  await page.click('[data-mode="route"]');
  await page.selectOption("#ea", "Asha"); await page.selectOption("#eb", "Farah"); await page.click("#run");
  await page.waitForFunction(() => /steps apart/.test(document.getElementById("result").textContent));
  T("route: 3 steps apart", (await page.locator("#result").innerText()).includes("3 steps apart"));
  T("route: 4 nodes highlighted", (await page.locator("#canvas .node.path").count()) === 4);

  await page.click('[data-mode="route"]');
  await page.selectOption("#ea", "Asha"); await page.selectOption("#eb", "Gopal"); await page.click("#run");
  await page.waitForFunction(() => /no route/.test(document.getElementById("result").textContent));
  T("route: no route message", true);

  await page.click('[data-mode="common"]');
  await page.selectOption("#ea", "Asha"); await page.selectOption("#eb", "Dev"); await page.click("#run");
  await page.waitForFunction(() => /both know/.test(document.getElementById("result").textContent));
  T("common = Ben", (await page.locator("#result .chip").allInnerTexts()).join() === "Ben");

  await page.click('[data-mode="suggest"]');
  await page.selectOption("#ea", "Asha"); await page.click("#run");
  await page.waitForFunction(() => /might know/.test(document.getElementById("result").textContent));
  T("suggest Dev then Esha", /Dev[\s\S]*Esha/.test(await page.locator("#result").innerText()));

  await page.click('[data-mode="visit"]');
  await page.selectOption("#order", "dfs"); await page.selectOption("#ea", "Asha"); await page.click("#run");
  await page.waitForFunction(() => /reached/.test(document.getElementById("result").textContent));
  const t = await page.locator("#result").innerText();
  T("dfs: 6 reached, 2 not connected", t.includes("6 people") && t.includes("2 not connected"), t);
  T("dfs: visit numbers drawn", (await page.locator("#canvas .step").count()) === 6);

  await page.fill("#nameInput", "<img src=x onerror=alert(1)>"); await page.click("#addForm button");
  await page.waitForFunction(() => document.querySelectorAll("#canvas .node").length === 9);
  T("xss-looking name rendered as text", (await page.locator("#canvas img").count()) === 0 && (await page.locator("#fa option", { hasText: "<img" }).count()) === 1);
  await page.fill("#nameInput", "asha"); await page.click("#addForm button");
  await page.waitForFunction(() => /already/.test(document.getElementById("addMsg").textContent));
  T("duplicate rejected with message", true);
  await page.fill("#nameInput", "   "); await page.click("#addForm button");
  await page.waitForFunction(() => /name first/.test(document.getElementById("addMsg").textContent));
  T("blank name rejected with message", true);

  await page.selectOption("#fa", "Asha"); await page.selectOption("#fb", "Ben"); await page.click("#friendForm button");
  await page.waitForFunction(() => /already friends/.test(document.getElementById("friendMsg").textContent));
  T("duplicate friendship rejected", true);

  await page.reload();
  await page.waitForFunction(() => document.querySelectorAll("#canvas .node").length === 9);
  T("state survives reload", true);

  await page.selectOption("#theme", "night");
  T("theme switches", (await page.getAttribute("body", "data-theme")) === "night");
  await page.selectOption("#theme", "warm");

  await page.setViewportSize({ width: 375, height: 800 });
  T("no horizontal scroll at 375px", await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth));
  await page.setViewportSize({ width: 1100, height: 900 });

  page.once("dialog", d => d.accept()); await page.click("#reset");
  await page.waitForFunction(() => document.getElementById("empty").offsetParent !== null);
  T("reset -> empty state", true);
  T("no JS errors", errors.length === 0, errors.join(" | "));
}

(async () => {
  const browser = await chromium.launch();
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "ff-e2e-"));
  const procs = [];
  try {
    // 1) real C++ server
    {
      const s = await startServer(path.join(tmp, "a.json")); procs.push(s.p);
      const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
      await page.goto(s.url);
      await scenario("C++ server", page, "server");
      // state really lives on the server: a brand-new browser context sees it
      await page.click("#example"); await page.waitForFunction(() => document.querySelectorAll("#canvas .node").length === 8);
      const ctx2 = await browser.newContext(); const p2 = await ctx2.newPage();
      await p2.goto(s.url); await p2.waitForFunction(() => document.querySelectorAll("#canvas .node").length === 8);
      check("[C++ server] a second browser profile sees the same network", true);
      await ctx2.close();

      // 3) server dies mid-session -> clear error, app keeps working visually
      s.p.kill("SIGKILL"); await new Promise(r => setTimeout(r, 200));
      await page.fill("#nameInput", "Ghost"); await page.click("#addForm button");
      await page.waitForFunction(() => /reach the server/.test(document.getElementById("addMsg").textContent));
      check("[server down] shows 'can't reach the server'", true);
      check("[server down] button re-enabled", await page.locator("#addForm button").isEnabled());
      check("[server down] existing graph still drawn", (await page.locator("#canvas .node").count()) === 8);
      await page.close();
    }
    // 2) static hosting, no backend -> local fallback
    {
      const s = await startStatic(); procs.push(s.p);
      const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
      await page.goto(s.url);
      await scenario("static only", page, "local");
      await page.close();
    }
    // 5) site and API on different origins (e.g. Netlify + a hosted C++ server)
    {
      const site = path.join(tmp, "site");
      fs.mkdirSync(path.join(site, "js"), { recursive: true }); fs.mkdirSync(path.join(site, "css"), { recursive: true });
      for (const f of ["index.html", "css/style.css", "js/graph.js", "js/store.js", "js/app.js"]) fs.copyFileSync(path.join(ROOT, f), path.join(site, f));
      const apiPort = await freePort(), sitePort = await freePort();
      const writeConfig = () => fs.writeFileSync(path.join(site, "js", "config.js"), `window.FRIEND_FINDER_API = "http://127.0.0.1:${apiPort}/";`);
      writeConfig();
      const sitePy = spawn("python3", ["-m", "http.server", String(sitePort), "--bind", "127.0.0.1", "--directory", site], { stdio: "ignore" }); procs.push(sitePy);
      await waitUp(sitePort);
      const origin = "http://127.0.0.1:" + sitePort;
      // 5a) CORS configured -> works
      let api = spawn(BIN, ["--port", String(apiPort), "--bind", "127.0.0.1", "--static", path.join(tmp, "nothing"), "--data", path.join(tmp, "c.json"), "--cors", origin], { stdio: "ignore" }); procs.push(api);
      await waitUp(apiPort);
      let page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
      await page.goto(origin + "/");
      await scenario("cross-origin + CORS", page, "server");
      await page.close(); api.kill("SIGKILL"); await new Promise(r => setTimeout(r, 200));
      // 5b) CORS NOT configured -> browser blocks it, app must fall back to local instead of breaking
      api = spawn(BIN, ["--port", String(apiPort), "--bind", "127.0.0.1", "--static", path.join(tmp, "nothing"), "--data", path.join(tmp, "d.json")], { stdio: "ignore" }); procs.push(api);
      await waitUp(apiPort);
      page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
      await page.goto(origin + "/");
      await scenario("cross-origin, no CORS", page, "local");
      await page.close();
    }
    // 4) file:// -> local fallback
    {
      const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
      await page.goto(require("url").pathToFileURL(path.join(ROOT, "index.html")).href);
      await scenario("file://", page, "local");
      await page.close();
    }
  } catch (e) {
    fail++; console.log("FAIL harness error: " + e.message);
  } finally {
    procs.forEach(p => { try { p.kill("SIGKILL"); } catch (e) { /* already gone */ } });
    await browser.close(); fs.rmSync(tmp, { recursive: true, force: true });
  }
  console.log(`\n${pass} passed, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
