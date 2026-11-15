/* Tests of "My device" (index.html#device), the StatTrak button in the header (js/devbar.js) and
 * the interface for added scripts (AppsTab.api, the event "stattrak:device"), in a real headless
 * Chrome with the stand-in device. Run: node --test "tests/*.test.js"
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { Browser, serve, findChrome } = require("./browser.js");
const { ROOT } = require("./helpers.js");

const FAKE_SOURCE = fs.readFileSync(path.join(__dirname, "fake-device.js"), "utf8");
const CATALOG = JSON.parse(fs.readFileSync(path.join(ROOT, "apps", "catalog.json"), "utf8"));
const SKIP = findChrome() ? false : "no Chrome, Chromium or Edge on this computer";
const SPEED = 20;

let browser = null;
let site = null;

test.before(async () => {
  if (SKIP) return;
  site = await serve(ROOT);
  browser = await Browser.launch();
});
test.after(async () => {
  if (browser) await browser.close();
  if (site) await site.close();
});

function initScript(deviceOptions) {
  const o = Object.assign({ apps: true, screen: "appsync", install: [] }, deviceOptions);
  return `
    (() => {
      const real = window.setTimeout.bind(window);
      window.setTimeout = (fn, ms, ...args) => real(fn, (ms || 0) / ${SPEED}, ...args);
      for (const name of ["alert", "confirm", "prompt"]) window[name] = () => { throw new Error("a browser dialog: " + name); };
    })();
    ${FAKE_SOURCE}
    (() => {
      const o = ${JSON.stringify(o)};
      const read = (name) => { const x = new XMLHttpRequest(); x.open("GET", "/apps/" + name, false); x.overrideMimeType("text/plain; charset=x-user-defined"); x.send();
        return Uint8Array.from(x.responseText, (c) => c.charCodeAt(0) & 0xFF); };
      const catalog = (() => { const x = new XMLHttpRequest(); x.open("GET", "/apps/catalog.json", false); x.send(); return JSON.parse(x.responseText); })();
      o.installedApps = o.install.map((name) => read(catalog.apps.find((a) => a.name === name).file));
      window.__device = new FakeStatTrak(o);
      if (o.screen === "appsync") { window.__device.screen = "counter"; window.__device.openAppSync(); }
      Object.defineProperty(navigator, "bluetooth", { value: window.__device.bluetooth(), configurable: true });
      const realFetch = window.fetch.bind(window);
      window.fetch = async (url, init) => /api\\.github\\.com/.test(String(url)) ? new Response("[]", { status: 200 }) : realFetch(url, init);
      window.__events = [];
      window.addEventListener("stattrak:device", (e) => window.__events.push(e.detail));
    })();
  `;
}

async function open(t, deviceOptions, hash) {
  const page = await browser.newPage();
  await page.setInitScript(initScript(deviceOptions || {}));
  await page.goto(site.url + "/index.html");
  await page.evaluate(`BleOta.RECONNECT_GIVE_UP_MS = ${300000 / SPEED}; StatTrakApps.QUESTION_WAIT_MS = ${40000 / SPEED}`);
  if (hash) await page.evaluate(`location.hash = ${JSON.stringify(hash)}`);
  t.after(async () => {
    if (process.env.SCREENSHOTS) {
      const name = "devbar-" + t.name.replace(/[^a-z0-9]+/gi, "-").toLowerCase().slice(0, 80);
      await page.screenshot(path.join(process.env.SCREENSHOTS, name + ".png"));
    }
    assert.deepEqual(page.errors, [], "errors in the page");
    await page.close();
  });
  return page;
}

const has = (selector) => `document.querySelector(${JSON.stringify(selector)})`;
const act = (page, selector) => page.evaluate(`(() => { const el = document.querySelector(${JSON.stringify(selector)}); if (!el) throw new Error("not there: " + ${JSON.stringify(selector)}); el.click(); })()`);
// The text of an element, with a space between the texts of its parts
const textOf = (page, selector) => page.evaluate(`(() => { const el = ${has(selector)}; if (!el) return "";
  const w = document.createTreeWalker(el, NodeFilter.SHOW_TEXT), parts = []; while (w.nextNode()) parts.push(w.currentNode.textContent);
  return parts.join(" ").replace(/\\s+/g, " ").trim(); })()`);
const row = (name) => `[...document.querySelectorAll("#appsList .row")].find((r) => r.querySelector(".row__name").textContent === ${JSON.stringify(name)})`;

async function connectFromHeader(page) {
  await act(page, '#devBar [data-dev="toggle"]');
  await page.waitFor(has('#devBar [data-dev="connect"]'));
  await act(page, '#devBar [data-dev="connect"]');
  await page.waitFor(`AppsTab.state.conn === "on"`);
}

test("My device: the tab name, the address #device, and the old #apps", { skip: SKIP }, async (t) => {
  const page = await open(t, {}, "#device");
  await page.waitFor(`!document.getElementById("appsView").hidden`);
  assert.equal(await textOf(page, "#tabApps"), "My device");
  assert.equal(await page.evaluate(`document.getElementById("tabApps").getAttribute("aria-current")`), "page");
  await page.evaluate(`location.hash = "#firmware"`);
  await page.waitFor(`document.getElementById("appsView").hidden`);
  await page.evaluate(`location.hash = "#apps"`);
  await page.waitFor(`!document.getElementById("appsView").hidden`);
});

test("the StatTrak button: Connect from any tab, then the name, serial, firmware and storage; Disconnect", { skip: SKIP }, async (t) => {
  const page = await open(t, { deviceName: "BRAVO-6", install: ["ROUND TIMER"] });
  assert.equal(await textOf(page, "#devBar .devbtn"), "Connect");
  // From the firmware tab, Connect opens My device and connects in Apps: SYNC
  await connectFromHeader(page);
  await page.waitFor(`!document.getElementById("appsView").hidden`);
  await page.waitFor(`/BRAVO-6/.test(document.querySelector("#devBar .devbtn").textContent)`);
  assert.equal(await page.evaluate(`!!${has("#devBar .devbtn__count")}`), false, "no updates");
  await act(page, '#devBar [data-dev="toggle"]');
  const panel = await textOf(page, "#devBar .devmenu");
  assert.match(panel, /BRAVO-6 Connected/);
  assert.match(panel, /Serial 34B7DA010203/);
  assert.match(panel, /Firmware \S+/);
  assert.match(panel, /Apps 1\.1 KB \/ 24 KB/);
  // Escape closes the panel; a click outside too
  await page.evaluate(`document.dispatchEvent(new KeyboardEvent("keydown", { key: "Escape" }))`);
  assert.equal(await page.evaluate(`!!${has("#devBar .devmenu")}`), false);
  await act(page, '#devBar [data-dev="toggle"]');
  await page.evaluate(`document.querySelector("footer").click()`);
  assert.equal(await page.evaluate(`!!${has("#devBar .devmenu")}`), false);
  // Disconnect from the panel
  await act(page, '#devBar [data-dev="toggle"]');
  await act(page, '#devBar [data-dev="disconnect"]');
  await page.waitFor(`AppsTab.state.conn === "off"`);
  assert.equal(await textOf(page, "#devBar .devbtn"), "Connect");
});

test("an app with a newer version: 'Update available' on My device, a count on the button, Update installs it", { skip: SKIP }, async (t) => {
  const round = CATALOG.apps.find((a) => a.name === "ROUND TIMER");
  const page = await open(t, { install: ["ROUND TIMER"], appQuestionMs: 60000 }, "#device");
  await act(page, '[data-act="connect"]');
  await page.waitFor(has("#appsList"));
  assert.equal(await page.evaluate(`!!${row("ROUND TIMER")}.querySelector('[data-act="update"]')`), false);
  // An added script knows a newer ROUND TIMER
  const next = round.version + 1;
  await page.evaluate(`AppsTab.api.addCatalog("test", [{ key: "test-round", id: 0x${round.id}, name: "ROUND TIMER", version: ${next},
    bytes: FakeStatTrak.makeApp({ name: "ROUND TIMER", id: 0x${round.id}, version: ${next} }),
    size: FakeStatTrak.makeApp({ name: "ROUND TIMER", id: 0x${round.id}, version: ${next} }).length }])`);
  assert.match(await page.evaluate(`${row("ROUND TIMER")}.textContent`), new RegExp(`Update available \\(v${round.version} → v${next}\\)`));
  assert.equal(await textOf(page, "#devBar .devbtn__count"), "1");
  await act(page, '#devBar [data-dev="toggle"]');
  assert.match(await textOf(page, "#devBar .devmenu"), new RegExp(`1 update ROUND TIMER v${round.version} → v${next}`));
  // Update from the panel: the device asks, the button says so, yes installs it
  await act(page, '#devBar [data-dev="update"]');
  await page.waitFor(`/Answer on StatTrak/.test(document.querySelector("#devBar .devbtn").textContent)`);
  await page.evaluate("window.__device.holdButton2()");
  await page.waitFor(`${row("ROUND TIMER")} && !${row("ROUND TIMER")}.querySelector('[data-act="update"]')`, 20000);
  assert.equal(await page.evaluate(`!!${has("#devBar .devbtn__count")}`), false);
  assert.equal(await page.evaluate(`AppsTab.state.list.find((a) => a.name === "ROUND TIMER").version`), next);
});

test("an added tab keeps the Apps: SYNC connection; the firmware tab ends it", { skip: SKIP }, async (t) => {
  const page = await open(t, {}, "#store/app/bomb-timer");
  await page.evaluate(`(() => {
    const tab = document.createElement("a"); tab.className = "tab"; tab.href = "#store"; tab.id = "tabStore"; tab.textContent = "App store";
    document.querySelector(".site-nav").appendChild(tab);
    const view = document.createElement("div"); view.id = "storeView"; view.hidden = true; view.textContent = "STORE";
    document.querySelector(".app").insertBefore(view, document.querySelector("footer"));
    window.__shown = [];
    AppsTab.api.addTab("store", { tab, view, onShow: () => window.__shown.push(location.hash) });
  })()`);
  // The address of the added tab opens it, also with more after the name
  await page.waitFor(`!document.getElementById("storeView").hidden`);
  assert.deepEqual(await page.evaluate("window.__shown"), ["#store/app/bomb-timer"]);
  assert.equal(await page.evaluate(`document.getElementById("tabStore").classList.contains("tab--on")`), true);
  assert.equal(await page.evaluate(`document.getElementById("firmwareView").hidden && document.getElementById("appsView").hidden`), true);
  await connectFromHeader(page);
  // Still the added tab: Connect does not leave it
  assert.equal(await page.evaluate(`!document.getElementById("storeView").hidden`), true);
  await page.evaluate(`location.hash = "#device"`);
  await page.waitFor(`!document.getElementById("appsView").hidden`);
  assert.equal(await page.evaluate("AppsTab.state.conn"), "on");
  await page.evaluate(`location.hash = "#store"`);
  await page.waitFor(`!document.getElementById("storeView").hidden`);
  assert.equal(await page.evaluate("AppsTab.state.conn"), "on");
  await page.evaluate(`location.hash = "#firmware"`);
  await page.waitFor(`!document.getElementById("firmwareView").hidden`);
  assert.equal(await page.evaluate("AppsTab.state.conn"), "off");
});

test("the event stattrak:device: connected with serial, firmware and name; disconnected", { skip: SKIP }, async (t) => {
  const page = await open(t, { deviceName: "ALPHA" }, "#device");
  await act(page, '[data-act="connect"]');
  await page.waitFor(`AppsTab.state.conn === "on"`);
  const events = await page.evaluate("window.__events");
  assert.equal(events.length, 1);
  assert.equal(events[0].event, "connected");
  assert.equal(events[0].serial, "34B7DA010203");
  assert.equal(events[0].name, "ALPHA");
  assert.match(events[0].fw, /^\d+\.\d+\.\d+/);
  assert.equal(await page.evaluate("SiteEvents.device.serial"), "34B7DA010203");
  await act(page, '[data-act="disconnect"]');
  assert.deepEqual((await page.evaluate("window.__events")).map((e) => e.event), ["connected", "disconnected"]);
  assert.equal(await page.evaluate("SiteEvents.device"), null);
});

test("the other pages: a Connect link to My device in the header", { skip: SKIP }, async (t) => {
  const page = await browser.newPage();
  t.after(() => page.close());
  for (const name of ["start/index.html", "designer/index.html"]) {
    await page.goto(site.url + "/" + name);
    assert.equal(await page.evaluate(`new URL(document.querySelector(".site-header .devbtn").href).hash`), "#device");
    assert.equal(await textOf(page, ".site-header .devbtn"), "Connect");
  }
});
