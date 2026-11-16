/* Tests of the Apps tab of the page (index.html#apps) in a real headless Chrome, with the
 * stand-in device in place of Web Bluetooth. Run: node --test "tests/*.test.js"
 *
 * The tests click the same buttons as a user. Time in the page runs faster (setTimeout is
 * divided). Set SCREENSHOTS=<folder> to keep a picture of the end of each test.
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { Browser, serve, findChrome } = require("./browser.js");
const { ROOT } = require("./helpers.js");

const FAKE_SOURCE = fs.readFileSync(path.join(__dirname, "fake-device.js"), "utf8");
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

/* The device: options of FakeStatTrak, plus `screen` (appsync | update) and `install`
   (names of catalog apps that are installed at the start) */
function initScript(deviceOptions) {
  const o = Object.assign({ apps: true, screen: "appsync", install: [] }, deviceOptions);
  return `
    (() => {
      const real = window.setTimeout.bind(window);
      window.setTimeout = (fn, ms, ...args) => real(fn, (ms || 0) / ${SPEED}, ...args);
      // The page must not use browser dialogs
      for (const name of ["alert", "confirm", "prompt"]) window[name] = () => { throw new Error("a browser dialog: " + name); };
    })();
    ${FAKE_SOURCE}
    (() => {
      const o = ${JSON.stringify(o)};
      const read = (name) => { const x = new XMLHttpRequest(); x.open("GET", "/apps/" + name, false); x.overrideMimeType("text/plain; charset=x-user-defined"); x.send();
        return Uint8Array.from(x.responseText, (c) => c.charCodeAt(0) & 0xFF); };
      const catalog = (() => { const x = new XMLHttpRequest(); x.open("GET", "/apps/catalog.json", false); x.send(); return JSON.parse(x.responseText); })();
      o.installedApps = o.install.map((name) => read(catalog.apps.find((a) => a.name === name).file)).concat((o.extraApps || []).map((a) => FakeStatTrak.makeApp(a)));
      window.__device = new FakeStatTrak(o);
      if (o.screen === "appsync") { window.__device.screen = "counter"; window.__device.openAppSync(); }
      Object.defineProperty(navigator, "bluetooth", { value: window.__device.bluetooth(), configurable: true });
      const realFetch = window.fetch.bind(window);
      window.fetch = async (url, init) => /api\\.github\\.com/.test(String(url)) ? new Response("[]", { status: 200 }) : realFetch(url, init);
    })();
  `;
}

async function open(t, deviceOptions) {
  const page = await browser.newPage();
  page.diagnose = () => page.evaluate(`JSON.stringify({ conn: AppsTab.state.conn, inst: AppsTab.state.inst, err: AppsTab.state.err,
    log: state.log.slice(-10).map((l) => l.m), device: window.__device.events.slice(-10) }, null, 1)`);
  await page.setInitScript(initScript(deviceOptions || {}));
  await page.goto(site.url + "/index.html");
  await page.evaluate(`BleOta.RECONNECT_GIVE_UP_MS = ${300000 / SPEED}; StatTrakApps.QUESTION_WAIT_MS = ${40000 / SPEED};
    location.hash = "#apps"`);
  await page.waitFor(`!document.getElementById("appsView").hidden`);
  t.after(async () => {
    if (process.env.SCREENSHOTS) {
      const name = "apps-" + t.name.replace(/[^a-z0-9]+/gi, "-").toLowerCase().slice(0, 80);
      await page.screenshot(path.join(process.env.SCREENSHOTS, name + ".png"));
    }
    assert.deepEqual(page.errors, [], "errors in the page");
    // The Apps tab never sends a firmware update command
    assert.equal(await device(page, "startCount"), 0);
    assert.equal(await device(page, "acceptCount"), 0);
    await page.close();
  });
  return page;
}

const device = (page, expression) => page.evaluate(`window.__device.${expression}`);
const act = (page, selector) => page.evaluate(`(() => { const el = document.querySelector(${JSON.stringify(selector)}); if (!el) throw new Error("not there: " + ${JSON.stringify(selector)}); el.click(); })()`);
const texts = (page, selector) => page.evaluate(`[...document.querySelectorAll(${JSON.stringify(selector)})].map((el) => el.textContent.trim())`);
const has = (selector) => `document.querySelector(${JSON.stringify(selector)})`;
const mainText = (page) => page.evaluate(`document.getElementById("appsMain").textContent`);
const row = (name) => `[...document.querySelectorAll("#appsList .row")].find((r) => r.querySelector(".row__name").textContent === ${JSON.stringify(name)})`;
const inRow = (name, selector) => `${row(name)}.querySelector(${JSON.stringify(selector)})`;
const catRow = (name) => `[...document.querySelectorAll(".cat")].find((r) => r.querySelector(".row__name").textContent === ${JSON.stringify(name)})`;
const click = (page, expression) => page.evaluate(`(${expression}).click()`);

async function connect(page) {
  await act(page, '[data-act="connect"]');
  await page.waitFor(has("#appsList"));
  // The catalog comes from a file and can come after the device list (more so on a busy computer)
  try { await page.waitFor(has(".cat"), 5000); } catch (error) { /* a test without a catalog */ }
}

/* The page shows the question of the device ("Answer on StatTrak") */
const asking = (page, timeoutMs) => page.waitFor(`/Answer on StatTrak/.test((document.querySelector("#instpanel .inst__big") || {}).textContent)`, timeoutMs || 20000);

test("connect: the apps of the device in menu order, SETTINGS last, the storage and the catalog", { skip: SKIP }, async (t) => {
  const page = await open(t, { install: ["ROUND TIMER"], hiddenApps: [2] });
  assert.match(await mainText(page), /Apps: SYNC.*Hold right button/);
  await connect(page);
  assert.deepEqual(await texts(page, "#appsList .row__name"), ["KILLS", "KILLS + DEAD", "STATS", "ROUND TIMER", "SETTINGS"]);
  assert.deepEqual(await texts(page, "#appsList .row .lbl"), ["Shown", "Hidden", "Shown", "Shown"]);
  assert.deepEqual(await texts(page, "#appsList .tag"), ["Built-in", "Built-in", "Built-in", "Installed · 1.1 KB", "Always last"]);
  assert.match(await texts(page, ".status .store span").then((t) => t[0]), /^1\.1 KB \/ 24 KB$/);
  // Installed apps are not in the catalog; the others are, with their permissions
  assert.deepEqual(await texts(page, ".cat .row__name"), ["BATTERY LOG", "CLIP MARKS", "HEADSHOT %", "REACTION", "RUNNER", "SLOTS", "WALLPAPER"]);
  // The Bluetooth name of the device
  assert.match(await page.evaluate(`document.querySelector(".namebox").textContent`), /StatTrak shows the name StatTrak in Apps: SYNC \(the standard name\)/);
  assert.equal(await page.evaluate(`!!document.querySelector('[data-act="name-reset"]')`), false);
  assert.match(await page.evaluate(`${catRow("HEADSHOT %")}.textContent`), /Changes your kill counts/);
  // Built-in apps have no delete button; only apps with known settings have a gear
  assert.equal(await page.evaluate(`!!${inRow("KILLS", '[data-act="del"]')}`), false);
  assert.equal(await page.evaluate(`!!${inRow("ROUND TIMER", '[data-act="edit"]')}`), true);
  assert.equal(await page.evaluate(`!!${inRow("STATS", '[data-act="edit"]')}`), false);
});

test("hide and show; the last shown app cannot be hidden", { skip: SKIP }, async (t) => {
  const page = await open(t, {});
  await connect(page);
  await click(page, inRow("KILLS + DEAD", '[data-act="toggle"]'));
  await page.waitFor(`window.__device.appHidden("KILLS + DEAD")`);
  await click(page, inRow("STATS", '[data-act="toggle"]'));
  await page.waitFor(`window.__device.appHidden("STATS")`);
  const writes = await device(page, "appWrites");
  await click(page, inRow("KILLS", '[data-act="toggle"]'));
  await page.waitFor(has(".hint"));
  assert.equal(await page.evaluate(`document.querySelector(".hint").textContent`), "One app must stay shown.");
  assert.equal(await device(page, "appHidden('KILLS')"), false);
  assert.equal(await device(page, "appWrites"), writes);
  await click(page, inRow("STATS", '[data-act="toggle"]'));
  await page.waitFor(`!window.__device.appHidden("STATS")`);
});

test("move with the arrows, with the keyboard and by drag", { skip: SKIP }, async (t) => {
  const page = await open(t, { install: ["ROUND TIMER"] });
  await connect(page);
  await click(page, inRow("KILLS", '[data-act="down"]'));
  await page.waitFor(`window.__device.appNames().join() === "KILLS + DEAD,KILLS,STATS,ROUND TIMER"`);
  await page.evaluate(`${inRow("ROUND TIMER", "[data-grip]")}.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowUp", bubbles: true }))`);
  await page.waitFor(`window.__device.appNames().join() === "KILLS + DEAD,KILLS,ROUND TIMER,STATS"`);
  // Drag STATS above everything
  await page.evaluate(`(() => {
    const grip = ${inRow("STATS", "[data-grip]")};
    const top = document.querySelector("#appsList .row").getBoundingClientRect().top;
    grip.dispatchEvent(new PointerEvent("pointerdown", { bubbles: true, pointerId: 1 }));
    document.dispatchEvent(new PointerEvent("pointermove", { bubbles: true, clientY: top + 2, pointerId: 1 }));
    document.dispatchEvent(new PointerEvent("pointerup", { bubbles: true, pointerId: 1 }));
  })()`);
  await page.waitFor(`window.__device.appNames().join() === "STATS,KILLS + DEAD,KILLS,ROUND TIMER"`);
  assert.deepEqual(await texts(page, "#appsList .row__name"), ["STATS", "KILLS + DEAD", "KILLS", "ROUND TIMER", "SETTINGS"]);
  assert.equal(await page.evaluate(`${inRow("STATS", '[data-act="up"]')}.disabled`), true);
});

test("delete asks in the page; Cancel keeps the app, Delete removes it", { skip: SKIP }, async (t) => {
  const page = await open(t, { install: ["ROUND TIMER"] });
  await connect(page);
  await click(page, inRow("ROUND TIMER", '[data-act="del"]'));
  assert.match(await page.evaluate(`${row("ROUND TIMER")}.textContent`), /Delete ROUND TIMER and its saved values\?/);
  await act(page, '[data-act="cancel"]');
  assert.equal(await page.evaluate(`!!document.querySelector(".row--del")`), false);
  await click(page, inRow("ROUND TIMER", '[data-act="del"]'));
  await act(page, '[data-act="really-del"]');
  await page.waitFor(`!${row("ROUND TIMER")}`);
  assert.deepEqual(await device(page, "appNames()"), ["KILLS", "KILLS + DEAD", "STATS"]);
  // It is in the catalog again, and the storage is free again
  await page.waitFor(catRow("ROUND TIMER"));
  assert.equal((await texts(page, ".status .store span"))[0], "0 KB / 24 KB");
});

test("install from the catalog: the question on the device first, then the transfer, then 'Installed'", { skip: SKIP }, async (t) => {
  const page = await open(t, { appQuestionMs: 60000 });
  await connect(page);
  await click(page, `${catRow("REACTION")}.querySelector('[data-act="install"]')`);
  await asking(page);
  // During the install the other catalog apps and the device list are locked
  assert.deepEqual(await texts(page, ".cat .row__name"), ["REACTION"]);
  assert.equal(await page.evaluate(`document.getElementById("appsList").classList.contains("list--busy")`), true);
  assert.equal(await page.evaluate(`document.getElementById("tabFirmware").getAttribute("aria-disabled")`), "true");
  assert.match(await page.evaluate(`document.getElementById("instpanel").textContent`), /Answer on StatTrak.*Hold the right button = yes, hold the left button = no\./);
  // The device picture plays the question and a hold of button 2 (js/device.js)
  assert.deepEqual(await page.evaluate(`(() => { const d = document.querySelector("#instpanel st-device"); return [d.getAttribute("anim"), d.getAttribute("arg"), d.getAttribute("aria-label")]; })()`),
    ["sync-question", "REACTION", "StatTrak screen: INSTALL REACTION? Hold the right button = yes"]);
  await page.waitFor(`document.querySelector("#instpanel st-device").dataset.frame !== undefined`);
  assert.equal(await device(page, "display"), "INSTALL REACTION?");
  assert.equal(await device(page, "appCommands.includes(0x0114)"), false, "no data before the answer");
  assert.equal(await page.evaluate(`!!document.querySelector("#instpanel .prog")`), false);
  // Yes: the transfer, with its progress
  await page.evaluate(`window.__progress = []; new MutationObserver(() => { const p = document.querySelector("#instpanel .pct"); if (p) window.__progress.push(p.textContent); })
    .observe(document.getElementById("appsMain"), { childList: true, subtree: true, characterData: true })`);
  await device(page, "holdButton2()");
  await page.waitFor(row("REACTION"));
  assert.ok((await page.evaluate("window.__progress")).length > 0, "no progress was shown");
  assert.deepEqual(await texts(page, "#appsList .row__name"), ["KILLS", "KILLS + DEAD", "STATS", "REACTION", "SETTINGS"]);
  assert.match(await page.evaluate(`${row("REACTION")}.textContent`), /Installed · 1\.2 KB/);
  assert.equal(await page.evaluate(`${row("REACTION")}.classList.contains("row--new")`), true);
  // What the device shows now, and what the buttons do
  assert.match(await page.evaluate(`document.getElementById("donepanel").textContent`),
    /Installed.*On StatTrak: hold the left button = SYNC MORE, hold the right button = OPEN\. OPEN closes this connection\./);
  assert.deepEqual(await page.evaluate(`(() => { const d = document.querySelector("#donepanel st-device"); return [d.getAttribute("anim"), d.getAttribute("arg"), d.getAttribute("aria-label")]; })()`),
    ["sync-installed", "REACTION", "StatTrak screen: APP INSTALLED, SYNC MORE, OPEN"]);
  assert.equal(await device(page, "display"), "APP INSTALLED");
  assert.equal(await page.evaluate(`!!${catRow("REACTION")}`), false);
  assert.equal(await page.evaluate(`document.getElementById("tabFirmware").getAttribute("aria-disabled")`), null);
});

test("OPEN on the device after the install: a calm message, not 'Connection lost'", { skip: SKIP }, async (t) => {
  const page = await open(t, { appQuestionMs: 60000 });
  await connect(page);
  await click(page, `${catRow("REACTION")}.querySelector('[data-act="install"]')`);
  await asking(page);
  await device(page, "holdButton2()");
  await page.waitFor(has("#donepanel"));
  // A change in the page (here: the order) does not change it
  await click(page, inRow("KILLS", '[data-act="down"]'));
  await page.waitFor(`window.__device.appNames()[0] === "KILLS + DEAD"`);
  await device(page, "holdButton2()");
  await page.waitFor(has("#appsMain .banner--info"));
  assert.match(await mainText(page), /REACTION is installed.*The connection ended\. OPEN on StatTrak opens the app and closes the connection\./);
  assert.equal(await page.evaluate(`!!document.querySelector("#appsMain .banner--error")`), false);
  assert.equal(await device(page, "screen"), "app");
  assert.equal(await device(page, "openedApp"), 0x62e49bbb);
});

test("SYNC MORE on the device: the next install works; a lost connection later is 'Connection lost'", { skip: SKIP }, async (t) => {
  const page = await open(t, { appQuestionMs: 60000 });
  await connect(page);
  await click(page, `${catRow("REACTION")}.querySelector('[data-act="install"]')`);
  await asking(page);
  await device(page, "holdButton2()");
  await page.waitFor(has("#donepanel"));
  await device(page, "holdButton1()");
  assert.equal(await device(page, "syncState"), 0);
  // The page sees it (STATUS): the device does not show APP INSTALLED any more
  await page.waitFor(`AppsTab.state.opened === null`);
  await click(page, `${catRow("ROUND TIMER")}.querySelector('[data-act="install"]')`);
  await asking(page);
  await device(page, "holdButton2()");
  await page.waitFor(row("ROUND TIMER"));
  await device(page, "holdButton1()");
  await page.waitFor(`AppsTab.state.opened === null`);
  await device(page, "loseConnection()");
  await page.waitFor(has("#appsMain .banner--error"));
  assert.match(await mainText(page), /Connection lost/);
});

test("the person says no on the device: 'Not installed', Try again works", { skip: SKIP }, async (t) => {
  const page = await open(t, { appQuestionMs: 60000 });
  await connect(page);
  await click(page, `${catRow("ROUND TIMER")}.querySelector('[data-act="install"]')`);
  await asking(page);
  await device(page, "holdButton1()");
  await page.waitFor(`${catRow("ROUND TIMER")}.querySelector(".res--err")`);
  assert.match(await page.evaluate(`${catRow("ROUND TIMER")}.textContent`), /Not installed.*Declined on StatTrak or timed out/);
  assert.deepEqual(await texts(page, `.cat .btns button`), ["Close", "Try again"]);
  assert.equal(await device(page, "appCommands.includes(0x0114)"), false, "data was sent after no");
  await click(page, `${catRow("ROUND TIMER")}.querySelector('[data-act="install"]')`);
  await asking(page);
  await device(page, "holdButton2()");
  await page.waitFor(row("ROUND TIMER"));
});

test("no answer on the device in 30 s: 'Not installed'", { skip: SKIP }, async (t) => {
  const page = await open(t, {});
  await connect(page);
  await click(page, `${catRow("REACTION")}.querySelector('[data-act="install"]')`);
  await page.waitFor(`${catRow("REACTION")}.querySelector(".res--err")`, 30000);
  assert.ok((await device(page, "events")).includes("question timeout"));
  assert.deepEqual(await device(page, "appNames()"), ["KILLS", "KILLS + DEAD", "STATS"]);
});

test("a lost answer during the transfer: the page sends again from the expected offset", { skip: SKIP }, async (t) => {
  const page = await open(t, { loseDataAnswerAt: 70, appQuestionMs: 60000 });
  await connect(page);
  await click(page, `${catRow("HEADSHOT %")}.querySelector('[data-act="install"]')`);
  await asking(page, 30000);
  await device(page, "holdButton2()");
  await page.waitFor(row("HEADSHOT %"));
  assert.ok((await device(page, "events")).includes("lost answer of DATA 70"));
  const same = await page.evaluate(`(async () => {
    const file = new Uint8Array(await (await fetch("apps/headshot.stapp")).arrayBuffer());
    return Array.from(window.__device.installed.get(0x3b4aad40).pkg).join() === Array.from(file).join();
  })()`);
  assert.equal(same, true);
});

test("an app that does not fit: 'Needs … free' in place of Install", { skip: SKIP }, async (t) => {
  const big = (i) => ({ name: "BIG " + i, id: 0x10000 + i, size: 4096 });
  const page = await open(t, { extraApps: [1, 2, 3, 4, 5].map(big).concat([{ name: "MID", id: 0x20000, size: 3900 }]) });
  await connect(page);
  assert.match(await page.evaluate(`${catRow("REACTION")}.querySelector(".need").textContent`), /^Needs 1\.2 KB· 0\.2 KB free$/);
  assert.equal(await page.evaluate(`!!${catRow("REACTION")}.querySelector('[data-act="install"]')`), false);
  assert.equal(await page.evaluate(`!!document.querySelector(".store--full")`), true);
});

test("app settings: the values of the device, a click changes the device", { skip: SKIP }, async (t) => {
  const page = await open(t, { install: ["ROUND TIMER"] });
  await connect(page);
  await click(page, inRow("ROUND TIMER", '[data-act="edit"]'));
  await page.waitFor(`${inRow("ROUND TIMER", '.seg button[aria-pressed="true"]')}`);
  assert.deepEqual(await texts(page, '.setting__label'), ["ROUND TIME", "SOUND"]);
  assert.deepEqual(await texts(page, '.seg button[aria-pressed="true"]'), ["1:00", "ON"]);
  await page.evaluate(`[...document.querySelectorAll('.seg button')].find((b) => b.textContent === "3:00").click()`);
  await page.waitFor(`window.__device.installed.get(0xcc78e229).settings[0] === 4`);
  assert.deepEqual(await texts(page, '.seg button[aria-pressed="true"]'), ["3:00", "ON"]);
});

test("old firmware: 'Update the firmware first', and the button opens the firmware tab", { skip: SKIP }, async (t) => {
  const page = await open(t, { apps: false, userCommands: true, version: "1.1.0", screen: "update" });
  await act(page, '[data-act="connect"]');
  await page.waitFor(`document.querySelector("#appsMain .banner--warn")`);
  assert.match(await mainText(page), /Update the firmware first.*Firmware 1\.1\.0 has no apps\./);
  await act(page, '#appsMain a[href="#firmware"]');
  await page.waitFor(`!document.getElementById("firmwareView").hidden`);
  assert.equal(await page.evaluate(`document.getElementById("appsView").hidden`), true);
  assert.equal(await device(page, "gatt.connected"), false);
});

test("firmware 1.0.1, which does not answer user commands: also 'Update the firmware first'", { skip: SKIP }, async (t) => {
  const page = await open(t, { apps: false, version: "1.0.1", screen: "update" });
  await act(page, '[data-act="connect"]');
  await page.waitFor(`document.querySelector("#appsMain .banner--warn")`);
  assert.match(await mainText(page), /Firmware 1\.0\.1 has no apps\./);
});

test("a device that does not show Apps: SYNC: the page says what to do on the device", { skip: SKIP }, async (t) => {
  const page = await open(t, { screen: "update" });
  await act(page, '[data-act="connect"]');
  await page.waitFor(`document.querySelector("#appsMain .banner--warn")`);
  assert.match(await mainText(page), /StatTrak is not in Apps: SYNC.*Open Apps: SYNC on StatTrak, then connect\./);
  assert.match(await mainText(page), /Settings→Apps: SYNC→Hold right button/);
  assert.equal(await device(page, "gatt.connected"), false);
});

async function pickFile(page, name, bytesExpression) {
  await page.evaluate(`(() => {
    const transfer = new DataTransfer();
    transfer.items.add(new File([${bytesExpression}], ${JSON.stringify(name)}));
    const input = document.getElementById("appsFile");
    input.files = transfer.files;
    input.dispatchEvent(new Event("change", { bubbles: true }));
  })()`);
}

test("a file that is not an app, a damaged app, then a good app from a file", { skip: SKIP }, async (t) => {
  const page = await open(t, { appQuestionMs: 60000 });
  await connect(page);
  await pickFile(page, "kills-skin.zip", `new Uint8Array([0x50, 0x4b, 3, 4])`);
  await page.waitFor(has("#appsMain .banner--error"));
  assert.match(await mainText(page), /Not a StatTrak app.*kills-skin\.zip is not a \.stapp file\./);
  assert.deepEqual(await texts(page, "#appsMain .banner--error button"), ["Pick another file", ""]);
  await act(page, '#appsMain .banner--error [data-act="dismiss"]');
  assert.equal(await page.evaluate(`!!document.querySelector("#appsMain .banner--error")`), false);

  await pickFile(page, "broken.stapp", `(() => { const b = FakeStatTrak.makeApp({ name: "BROKEN" }); b[200] ^= 1; return b; })()`);
  await page.waitFor(has("#appsMain .banner--error"));
  assert.match(await mainText(page), /broken\.stapp is damaged\./);
  assert.deepEqual(await device(page, "appCommands.filter((c) => c === 0x0113)"), []);

  await pickFile(page, "my-app.stapp", `FakeStatTrak.makeApp({ name: "MY APP", id: 0x4242, settings: [{ name: "MODE", values: ["A", "B"] }] })`);
  await asking(page);
  assert.equal(await page.evaluate(`!!document.querySelector("#appsMain .banner--error")`), false);
  await device(page, "holdButton2()");
  await page.waitFor(row("MY APP"));
  // Its settings come from the file
  await click(page, inRow("MY APP", '[data-act="edit"]'));
  await page.waitFor(`${inRow("MY APP", '.seg button[aria-pressed="true"]')}`);
  assert.deepEqual(await texts(page, '.seg button'), ["A", "B"]);
});

test("the connection is lost during an install: 'Connection lost', the tabs work again", { skip: SKIP }, async (t) => {
  const page = await open(t, {});
  await connect(page);
  await click(page, `${catRow("REACTION")}.querySelector('[data-act="install"]')`);
  await asking(page);
  await device(page, "holdButton2()");
  await page.waitFor(`window.__device.syncReceived > 200`, 20000);
  await device(page, "loseConnection()");
  await page.waitFor(`document.querySelector("#appsMain .banner--error")`);
  assert.match(await mainText(page), /Connection lost.*Turn on Apps: SYNC again, then connect\./);
  assert.equal(await page.evaluate(`document.getElementById("tabFirmware").getAttribute("aria-disabled")`), null);
  // Connect again
  await connect(page);
});

test("the device stops the transfer (hold button 1): 'The transfer stopped', Try again works", { skip: SKIP }, async (t) => {
  const page = await open(t, { appQuestionMs: 60000 });
  await connect(page);
  await click(page, `${catRow("SLOTS")}.querySelector('[data-act="install"]')`);
  await asking(page);
  await device(page, "holdButton2()");
  await page.waitFor(`window.__device.syncReceived > 100`, 20000);
  await device(page, "holdButton1()");
  await page.waitFor(`${catRow("SLOTS")}.querySelector(".res--err")`);
  assert.match(await page.evaluate(`${catRow("SLOTS")}.textContent`), /Not installed.*The transfer stopped on StatTrak\. Try again\./);
  await click(page, `${catRow("SLOTS")}.querySelector('[data-act="install"]')`);
  await asking(page);
  await device(page, "holdButton2()");
  await page.waitFor(row("SLOTS"));
});

test("a format 2 app on a firmware that reads format 1 only: 'Update the firmware first', nothing is sent", { skip: SKIP }, async (t) => {
  const page = await open(t, { appFormat: 1 });
  await connect(page);
  await click(page, `${catRow("RUNNER")}.querySelector('[data-act="install"]')`);
  await page.waitFor(`${catRow("RUNNER")}.querySelector(".res--err")`);
  assert.match(await page.evaluate(`${catRow("RUNNER")}.textContent`), /Not installed.*Update the firmware first: this app needs a newer StatTrak firmware\./);
  assert.equal(await device(page, "appCommands.includes(0x0113)"), false, "BEGIN was sent");
  // A format 2 file
  await act(page, '[data-act="dismiss"]');
  await pickFile(page, "down.stapp", `FakeStatTrak.makeApp({ name: "DOWN", id: 0x5151, format: 2 })`);
  await page.waitFor(has("#appsMain .banner--error"));
  assert.match(await mainText(page), /down\.stapp needs a newer firmware\. Update the firmware first\./);
  assert.equal(await device(page, "appCommands.includes(0x0113)"), false, "BEGIN was sent");
});

/* Types into the name field as a person does */
const typeName = (page, value) => page.evaluate(`(() => { const input = document.getElementById("appsName"); input.value = ${JSON.stringify(value)};
  input.dispatchEvent(new Event("input", { bubbles: true })); })()`);
const submitName = (page) => page.evaluate(`document.querySelector('[data-act="name-save"]').click()`);

test("the Bluetooth name: the rules, Save, and Back to StatTrak", { skip: SKIP }, async (t) => {
  const page = await open(t, {});
  await connect(page);
  const nameMsg = () => page.evaluate(`document.getElementById("appsNameMsg").textContent`);
  assert.equal(await page.evaluate(`document.getElementById("appsName").maxLength`), 12);
  // Empty, a space at the start, too long, a letter that is not ASCII: a message, nothing is sent
  const sent = await device(page, "appCommands.length");
  await submitName(page);
  assert.equal(await nameMsg(), "Type a name first.");
  await typeName(page, " RED");
  assert.equal(await nameMsg(), "No space at the start or at the end.");
  assert.equal(await page.evaluate(`document.getElementById("appsName").getAttribute("aria-invalid")`), "true");
  await submitName(page);
  assert.equal(await nameMsg(), "No space at the start or at the end.");
  await typeName(page, "THIRTEEN CHAR");
  assert.equal(await nameMsg(), "At most 12 characters.");
  await typeName(page, "Rīga");
  assert.match(await nameMsg(), /Use only A-Z, a-z, 0-9/);
  assert.equal(await device(page, "appCommands.length"), sent, "a bad name was sent");
  // A good name
  await typeName(page, "Red 7");
  assert.equal(await nameMsg(), "");
  await submitName(page);
  await page.waitFor(`window.__device.deviceName === "Red 7"`);
  // A popup says when the name changes; OK closes it
  await page.waitFor(`!!document.querySelector("#modalRoot .modal-panel--info")`);
  assert.equal(await page.evaluate(`document.querySelector("#modalRoot .modal__text").textContent`),
    "Name change will take place next time you open apps sync on device.");
  assert.equal(await page.evaluate(`document.activeElement.textContent`), "OK");
  await act(page, "#modalRoot [data-ok]");
  assert.equal(await page.evaluate(`document.getElementById("modalRoot").hidden && document.getElementById("modalRoot").innerHTML === ""`), true);
  assert.equal(await nameMsg(), "");
  assert.match(await page.evaluate(`document.querySelector(".namebox").textContent`), /StatTrak shows the name StatTrak \| Red 7 in Apps: SYNC\./);
  // This start of Apps: SYNC keeps its name
  assert.equal(await device(page, "bleName()"), "StatTrak");
  await act(page, '[data-act="name-reset"]');
  await page.waitFor(`window.__device.deviceName === "" && !document.querySelector('[data-act="name-reset"]')`);
  assert.match(await page.evaluate(`document.querySelector(".namebox").textContent`), /StatTrak shows the name StatTrak in Apps: SYNC \(the standard name\)/);
});

test("a renamed StatTrak (StatTrak | <part>) is found, and shows its name", { skip: SKIP }, async (t) => {
  const page = await open(t, { deviceName: "RED 7", install: ["ROUND TIMER"] });
  await connect(page);
  assert.deepEqual(await device(page, "requests[0].filters"), [{ namePrefix: "StatTrak" }, { services: ["03b80e5a-ede8-4b33-a751-6ce34ec4c700"] }]);
  assert.equal(await device(page, "webDevice.name"), "StatTrak | RED 7");
  assert.match(await page.evaluate(`document.querySelector(".namebox").textContent`), /StatTrak shows the name StatTrak \| RED 7 in Apps: SYNC\./);
  assert.equal(await page.evaluate(`!!document.querySelector('[data-act="name-reset"]')`), true);
  assert.deepEqual(await texts(page, "#appsList .row__name"), ["KILLS", "KILLS + DEAD", "STATS", "ROUND TIMER", "SETTINGS"]);
});

test("a browser that refuses the UUID filter: the page connects by the name StatTrak", { skip: SKIP }, async (t) => {
  const page = await open(t, { uuidFilterBlocked: true });
  await connect(page);
  assert.deepEqual(await device(page, "requests.map((r) => r.filters.length)"), [2, 1]);
});

test("the Apps tab is locked while a firmware update runs", { skip: SKIP }, async (t) => {
  const page = await open(t, {});
  await page.evaluate(`location.hash = "#firmware"`);
  await page.waitFor(`!document.getElementById("firmwareView").hidden`);
  await page.evaluate(`state.flashState = "flashing"; render()`);
  assert.equal(await page.evaluate(`document.getElementById("tabApps").getAttribute("aria-disabled")`), "true");
  // The links to the other pages are locked too: a click (mouse or Enter) does not leave the page
  for (const id of ["siteLogo", "tabStart", "tabDesigner"]) {
    assert.equal(await page.evaluate(`document.getElementById("${id}").getAttribute("aria-disabled")`), "true", id);
    assert.equal(await page.evaluate(`!document.getElementById("${id}").dispatchEvent(new MouseEvent("click", { bubbles: true, cancelable: true }))`), true, id);
  }
  await page.evaluate(`location.hash = "#apps"`);
  await new Promise((resolve) => setTimeout(resolve, 200));
  assert.equal(await page.evaluate(`document.getElementById("appsView").hidden`), true);
  assert.equal(await page.evaluate(`location.hash`), "#firmware");
  await page.evaluate(`state.flashState = "verifying"; render()`);
  assert.equal(await page.evaluate(`document.getElementById("tabStart").getAttribute("aria-disabled")`), "true");
  await page.evaluate(`state.flashState = "ready"; render()`);
  assert.equal(await page.evaluate(`document.getElementById("tabStart").getAttribute("aria-disabled")`), null);
  assert.equal(await page.evaluate(`document.getElementById("tabDesigner").getAttribute("aria-disabled")`), null);
  assert.equal(await page.evaluate(`document.getElementById("siteLogo").getAttribute("aria-disabled")`), null);
  await page.evaluate(`location.hash = "#apps"`);
  await page.waitFor(`!document.getElementById("appsView").hidden`);
});

test("a firmware tab connection is closed when the Apps tab opens", { skip: SKIP }, async (t) => {
  const page = await open(t, { screen: "update" });
  await page.evaluate(`location.hash = "#firmware"`);
  await page.waitFor(`!document.getElementById("firmwareView").hidden`);
  await page.evaluate(`document.getElementById("pairBtn").click()`);
  await page.waitFor(`!document.getElementById("step1After").hidden`);
  // The firmware tab looks for the name "StatTrak" only
  assert.deepEqual(await device(page, "requests[0]"), { filters: [{ name: "StatTrak" }], optionalServices: [0x8018] });
  await page.evaluate(`location.hash = "#apps"`);
  await page.waitFor(`!document.getElementById("appsView").hidden`);
  assert.equal(await device(page, "gatt.connected"), false);
  assert.equal(await page.evaluate(`state.device`), null);
});
