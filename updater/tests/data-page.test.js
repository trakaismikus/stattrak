/* Tests of the parts above the app list of My device (boot image, backup, app data, device log:
 * js/apps-data.js) and of the Screen Designer buttons Download PNG and Show on StatTrak, in a
 * real headless Chrome with the stand-in device in place of Web Bluetooth.
 * Run: node --test "tests/*.test.js"
 *
 * Set SCREENSHOTS=<folder> to keep a picture of the end of each test.
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
const RUNNER = 0xAABBCCDD;

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

/* The device (options of FakeStatTrak, plus `apps`: options of FakeStatTrak.makeApp for the
   installed apps, `screen`: appsync | update, `noBluetooth`) and the downloads of the page
   (window.__downloads: { name, bytes }) */
function initScript(deviceOptions, speed) {
  const o = Object.assign({ data: true, screen: "appsync", extraApps: [] }, deviceOptions);
  return `
    (() => {
      const real = window.setTimeout.bind(window);
      window.setTimeout = (fn, ms, ...args) => real(fn, (ms || 0) / ${speed}, ...args);
      // The page must not use browser dialogs
      for (const name of ["alert", "confirm", "prompt"]) window[name] = () => { throw new Error("a browser dialog: " + name); };
      window.__downloads = [];
      const click = HTMLAnchorElement.prototype.click;
      HTMLAnchorElement.prototype.click = function () {
        if (!this.download) return click.call(this);
        const entry = { name: this.download, bytes: null };
        window.__downloads.push(entry);
        fetch(this.href).then((r) => r.arrayBuffer()).then((b) => { entry.bytes = Array.from(new Uint8Array(b)); });
      };
    })();
    ${FAKE_SOURCE}
    (() => {
      const o = ${JSON.stringify(o)};
      o.installedApps = o.extraApps.map((a) => FakeStatTrak.makeApp(a));
      window.__device = new FakeStatTrak(o);
      if (o.screen === "appsync") { window.__device.screen = "counter"; window.__device.openAppSync(); }
      if (!o.noBluetooth) Object.defineProperty(navigator, "bluetooth", { value: window.__device.bluetooth(), configurable: true });
      else Object.defineProperty(navigator, "bluetooth", { value: undefined, configurable: true });
      const realFetch = window.fetch.bind(window);
      window.fetch = async (url, init) => /api\\.github\\.com/.test(String(url)) ? new Response("[]", { status: 200 }) : realFetch(url, init);
    })();
  `;
}

async function openPage(t, url, deviceOptions, speed) {
  const page = await browser.newPage();
  await page.setInitScript(initScript(deviceOptions || {}, speed));
  await page.goto(site.url + url);
  t.after(async () => {
    if (process.env.SCREENSHOTS) {
      const name = "data-" + t.name.replace(/[^a-z0-9]+/gi, "-").toLowerCase().slice(0, 80);
      await page.screenshot(path.join(process.env.SCREENSHOTS, name + ".png"));
    }
    assert.deepEqual(page.errors, [], "errors in the page");
    // These pages never send a firmware update command
    assert.equal(await page.evaluate("window.__device.startCount"), 0);
    assert.equal(await page.evaluate("window.__device.acceptCount"), 0);
    await page.close();
  });
  return page;
}

/* The Apps tab, connected */
async function openApps(t, deviceOptions) {
  const page = await openPage(t, "/index.html", deviceOptions, SPEED);
  page.diagnose = () => page.evaluate(`JSON.stringify({ conn: AppsTab.state.conn, work: SyncParts.state.work, supported: SyncParts.state.supported,
    device: window.__device.events.slice(-10) }, null, 1)`);
  await page.evaluate(`BleOta.RECONNECT_GIVE_UP_MS = ${300000 / SPEED}; StatTrakApps.QUESTION_WAIT_MS = ${40000 / SPEED}; location.hash = "#apps"`);
  await page.waitFor(`!document.getElementById("appsView").hidden`);
  await act(page, '[data-act="connect"]');
  await page.waitFor(has("#appsList"));
  return page;
}

const act = (page, selector) => page.evaluate(`(() => { const el = document.querySelector(${JSON.stringify(selector)}); if (!el) throw new Error("not there: " + ${JSON.stringify(selector)}); el.click(); })()`);
const has = (selector) => `document.querySelector(${JSON.stringify(selector)})`;
const text = (page, selector) => page.evaluate(`(document.querySelector(${JSON.stringify(selector)}) || {}).textContent || ""`);
const sectionText = (title) => `([...document.querySelectorAll("#appsMain section")].find((s) => s.querySelector(".h2").textContent === ${JSON.stringify(title)}) || {}).textContent || ""`;
/* Opens a part that folds (Boot image, Backup, App data, Device log) */
const openFold = async (page, key) => {
  await page.waitFor(`document.querySelector('[data-fold="${key}"]')`);
  if (!(await page.evaluate(`document.querySelector('[data-fold="${key}"]').classList.contains("fold--open")`))) await act(page, `[data-fold="${key}"] .fold__head`);
};
const inSection = (title, selector) => `[...document.querySelectorAll("#appsMain section")].find((s) => s.querySelector(".h2").textContent === ${JSON.stringify(title)}).querySelector(${JSON.stringify(selector)})`;
const download = async (page, name) => {
  await page.waitFor(`window.__downloads.some((d) => d.name === ${JSON.stringify(name)} && d.bytes)`);
  return page.evaluate(`window.__downloads.find((d) => d.name === ${JSON.stringify(name)}).bytes`);
};
const day = () => { const d = new Date(); return `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, "0")}-${String(d.getDate()).padStart(2, "0")}`; };
const asText = (bytes) => Buffer.from(bytes).toString("utf8");

/* Gives the file input `id` a file (made in the page by the expression `make`, a Promise of a File) */
const pickFile = (page, id, make) => page.evaluate(`(async () => {
  const file = await (${make});
  const dt = new DataTransfer(); dt.items.add(file);
  const input = document.getElementById(${JSON.stringify(id)});
  input.files = dt.files;
  input.dispatchEvent(new Event("change", { bubbles: true }));
})()`);

/* A PNG of w x h in the page: black, with a white box in the left half */
const pngFile = (w, h) => `new Promise((resolve) => {
  const c = document.createElement("canvas"); c.width = ${w}; c.height = ${h};
  const x = c.getContext("2d"); x.fillStyle = "#000"; x.fillRect(0, 0, ${w}, ${h}); x.fillStyle = "#fff"; x.fillRect(0, 0, ${w} / 2, ${h});
  c.toBlob((b) => resolve(new File([b], "logo.png", { type: "image/png" })), "image/png");
})`;

const RUNNER_APP = { name: "RUNNER", id: RUNNER, saved: 2, format: 2, settings: [{ name: "LEVEL", values: ["EASY", "HARD"] }] };

test("My device: boot image, backup, app data and device log are above the apps and fold; closed at first; the page keeps the choice", { skip: SKIP }, async (t) => {
  const page = await openApps(t);
  await page.waitFor(`SyncParts.state.log !== null && document.querySelectorAll(".fold").length === 4`);
  const heads = await page.evaluate(`[...document.querySelectorAll("#appsMain .h2")].map((h) => h.textContent)`);
  assert.deepEqual(heads, ["Boot image", "Backup", "App data", "Device log", "On your StatTrak", "Add apps"]);
  assert.deepEqual(await page.evaluate(`[...document.querySelectorAll(".fold")].map((f) => f.classList.contains("fold--open"))`), [false, false, false, false]);
  assert.deepEqual(await page.evaluate(`[...document.querySelectorAll(".fold__body")].map((b) => b.hidden)`), [true, true, true, true]);
  assert.equal(await text(page, '[data-fold="pic"] .fold__sum'), "None");
  assert.equal(await text(page, '[data-fold="log"] .fold__sum'), "Empty");
  await act(page, '[data-fold="log"] .fold__head');
  assert.equal(await page.evaluate(`document.querySelector('[data-fold="log"] .fold__head').getAttribute("aria-expanded")`), "true");
  assert.equal(await page.evaluate(`document.querySelector('[data-fold="log"] .fold__body').hidden`), false);
  assert.equal(await page.evaluate(`!!document.querySelector('[data-fold="log"] .fold__sum')`), false);
  assert.deepEqual(await page.evaluate(`JSON.parse(localStorage.getItem("stattrak.syncFolds"))`), { log: true });
  await act(page, '[data-fold="log"] .fold__head');
  assert.equal(await page.evaluate(`document.querySelector('[data-fold="log"] .fold__body').hidden`), true);
});

test("an older firmware: one line in place of the new parts", { skip: SKIP }, async (t) => {
  const page = await openApps(t, { data: false, apps: true });
  await page.waitFor(`/Update the firmware to use this\\./.test(document.getElementById("appsMain").textContent)`);
  assert.equal(await page.evaluate(`!!document.querySelector('[data-act="sync-backup"]')`), false);
  assert.equal(await page.evaluate(`!!document.querySelector('[data-act="sync-pic-file"]')`), false);
});

test("start picture: a PNG that is not 128 x 32 is scaled to fit, shows on the device picture, goes to StatTrak; show at power-on; delete asks first", { skip: SKIP }, async (t) => {
  const page = await openApps(t);
  await page.waitFor(has('[data-act="sync-pic-file"]'));
  await openFold(page, "pic");
  assert.match(await page.evaluate(sectionText("Boot image")), /No boot image on StatTrak/);
  assert.equal(await page.evaluate(`${inSection("Boot image", ".sync__link")}.getAttribute("href")`), "designer/index.html");
  assert.match(await page.evaluate(sectionText("Boot image")), /Make one in the Screen Designer, then use Download PNG there/);

  await pickFile(page, "syncPicFile", pngFile(256, 64));
  await page.waitFor(has("#syncPicDevice"));
  assert.match(await page.evaluate(sectionText("Boot image")), /256 x 64, scaled to fit/);
  assert.equal(await text(page, "#syncDarkValue"), "50%");
  assert.equal(await page.evaluate(`document.getElementById("syncPicDevice").getAttribute("anim")`), "still");
  // The slider: a new picture on the device picture, the slider stays
  const before = await page.evaluate(`document.getElementById("syncPicDevice").getAttribute("arg")`);
  await page.evaluate(`(() => { const s = document.getElementById("syncDark"); s.value = 80; s.dispatchEvent(new Event("input", { bubbles: true })); })()`);
  assert.equal(await text(page, "#syncDarkValue"), "80%");
  assert.notEqual(await page.evaluate(`document.getElementById("syncPicDevice").getAttribute("arg")`), before);

  await act(page, '[data-act="sync-pic-send"]');
  await page.waitFor(`window.__device.startPic`);
  await page.waitFor(has(".spic__img"));
  // The white left half is on: 128 x 32 scaled from 256 x 64
  const rows = await page.evaluate(`FakeStatTrak.frameRows(window.__device.startPic)`);
  assert.equal(rows[10], "#".repeat(64) + ".".repeat(64));
  assert.match(await page.evaluate(sectionText("Boot image")), /Sent\. StatTrak shows it for 2 s\./);

  await act(page, '[data-act="sync-pic-show"]');
  await page.waitFor(`window.__device.startPicShow === false`);
  await page.waitFor(`document.querySelector('[data-act="sync-pic-show"]').getAttribute("aria-checked") === "false"`);

  await act(page, '[data-act="sync-pic-del"]');
  await page.waitFor(has(".sync__ask"));
  assert.match(await text(page, ".sync__ask"), /Delete the boot image on StatTrak\?/);
  assert.ok(await page.evaluate("!!window.__device.startPic"), "not deleted before the answer");
  await act(page, '[data-act="sync-pic-really-del"]');
  await page.waitFor(`window.__device.startPic === null`);
  await page.waitFor(`/No boot image on StatTrak/.test(${sectionText("Boot image")})`);
});

test("start picture: a file that is not a picture", { skip: SKIP }, async (t) => {
  const page = await openApps(t);
  await page.waitFor(has('[data-act="sync-pic-file"]'));
  await pickFile(page, "syncPicFile", `Promise.resolve(new File(["hello"], "notes.txt", { type: "text/plain" }))`);
  await page.waitFor(`/notes\\.txt is not a PNG or JPG picture\\./.test(${sectionText("Boot image")})`);
});

test("backup: Download backup saves the file of docs/DATA.md 5.3", { skip: SKIP }, async (t) => {
  const page = await openApps(t, { extraApps: [RUNNER_APP], configValues: { total_kills: 321 }, appValues: { [RUNNER]: [4, 5] }, deviceName: "TEAM 1",
    deviceLog: [{ reason: 1, battery: 80, flags: 0, boot: 3 }] });
  await page.waitFor(has('[data-act="sync-backup"]'));
  await act(page, '[data-act="sync-backup"]');
  const file = JSON.parse(asText(await download(page, `stattrak-backup-${day()}.json`)));
  assert.equal(file.type, "stattrak-backup");
  assert.equal(file.version, 1);
  assert.equal(file.stats.total_kills, 321);
  assert.equal(file.deviceName, "TEAM 1");
  assert.equal(file.startPicture, null);
  assert.deepEqual(file.apps.find((a) => a.id === RUNNER), { id: RUNNER, name: "RUNNER", version: 1, hidden: false, settings: [0], values: [4, 5] });
  assert.deepEqual(file.deviceLog, [{ reason: 1, battery: 80, firmwareChanged: false, boot: 3 }]);
  await page.waitFor(`/Saved stattrak-backup-/.test(${sectionText("Backup")})`);
});

test("restore: the table, the parts to restore, the question on the device, then the values that were skipped", { skip: SKIP }, async (t) => {
  const page = await openApps(t, { extraApps: [RUNNER_APP], configValues: { total_kills: 5, brightness: 8 } });
  await page.waitFor(has('[data-act="sync-restore"]'));
  const backup = {
    type: "stattrak-backup", version: 1, created: "2026-11-15T12:00:00.000Z", firmware: "1.2.0-b2",
    stats: { total_kills: 900, total_deaths: 10, game_kills: 1, game_deaths: 0 },
    settings: { brightness: 2, sound: 0 },
    apps: [{ id: RUNNER, name: "RUNNER", version: 1, hidden: false, settings: [1], values: [7, 8] },
      { id: 1, name: "KILLS", builtin: true, hidden: false }, { id: 2, name: "KILLS + DEAD", builtin: true, hidden: true },
      { id: 0x77777777, name: "SLOTS", version: 1, hidden: false, settings: [], values: [] }],
    deviceName: " BAD NAME", startPicture: null, deviceLog: [],
  };
  await pickFile(page, "syncBackupFile", `Promise.resolve(new File([${JSON.stringify(JSON.stringify(backup))}], "my-backup.json", { type: "application/json" }))`);
  await page.waitFor(has(".bk"));
  const heads = await page.evaluate(`[...document.querySelectorAll(".bk thead th")].map((th) => th.textContent)`);
  assert.deepEqual(heads, ["Part", "On the device now", "In the file"]);
  const table = await text(page, ".bk");
  assert.match(table, /Total kills5900/);
  assert.match(table, /Brightness80%20%/);
  assert.match(await text(page, "#syncRestore"), /Install these apps first, or restore without them: SLOTS\./);
  // All parts are ticked at the start; untick the settings
  assert.deepEqual(await page.evaluate(`[...document.querySelectorAll(".bk input[type=checkbox]")].map((c) => c.dataset.part + ":" + c.checked)`),
    ["stats:true", "settings:true", "apps:true", "deviceName:true", "startPicture:true"]);
  await page.evaluate(`(() => { const c = document.querySelector('.bk input[data-part="settings"]'); c.checked = false; c.dispatchEvent(new Event("change", { bubbles: true })); })()`);
  await page.waitFor(`!document.querySelector('.bk input[data-part="settings"]').checked`);

  await act(page, '[data-act="sync-restore-go"]');
  await page.waitFor(`/Answer on StatTrak/.test(document.getElementById("syncRestore").textContent)`);
  assert.equal(await page.evaluate("window.__device.display"), "RESTORE BACKUP?");
  // Locked while StatTrak asks
  assert.equal(await page.evaluate(`document.querySelector('[data-act="sync-backup"]').disabled`), true);
  await page.evaluate("window.__device.holdButton2()");
  await page.waitFor(`/Backup restored\\./.test(document.getElementById("syncRestore").textContent) && !SyncParts.state.work`);
  assert.equal(await page.evaluate("window.__device.configValue('total_kills')"), 900);
  assert.equal(await page.evaluate("window.__device.configValue('brightness')"), 8, "settings were not ticked");
  assert.deepEqual(await page.evaluate(`window.__device.savedValues(${RUNNER})`), [7, 8]);
  assert.deepEqual(await page.evaluate(`window.__device.appNames()`), ["RUNNER", "KILLS", "KILLS + DEAD", "STATS"]);
  // The app list of the page has the new order
  await page.waitFor(`[...document.querySelectorAll("#appsList .row__name")].map((n) => n.textContent).join() === "RUNNER,KILLS,KILLS + DEAD,STATS,SETTINGS"`);
  // The page left out the name " BAD NAME" (not a correct name): it is in the list of skipped values
  const skipped = await text(page, ".sync__skipped");
  assert.match(skipped, /Not restored: StatTrak skipped these/);
  assert.match(skipped, /Bluetooth name: on StatTrak StatTrak, in the file StatTrak \|  BAD NAME/);
  assert.doesNotMatch(skipped, /Brightness/, "an unticked part is not in the list");
  await act(page, '[data-act="sync-restore-close"]');
  await page.waitFor(`!document.getElementById("syncRestore")`);
});

test("restore: a file that is not a backup", { skip: SKIP }, async (t) => {
  const page = await openApps(t);
  await page.waitFor(has('[data-act="sync-restore"]'));
  await pickFile(page, "syncBackupFile", `Promise.resolve(new File(["{}"], "other.json", { type: "application/json" }))`);
  await page.waitFor(`/other\\.json: This file is not a StatTrak backup\\./.test(${sectionText("Backup")})`);
});

test("app data: rows used / rows that fit, Download CSV with the TIME column, Delete data asks on the device", { skip: SKIP }, async (t) => {
  const page = await openApps(t, { extraApps: [RUNNER_APP, { name: "QUIET", id: 0x31313131 }],
    appData: { [RUNNER]: { columns: ["TIME", "HITS"], rows: [[61500, 2], [125, 0]] } } });
  await page.waitFor(`/RUNNER/.test(${sectionText("App data")})`);
  assert.doesNotMatch(await page.evaluate(sectionText("App data")), /QUIET/, "an app without a data store");
  assert.match(await page.evaluate(sectionText("App data")), /2 \/ 512 rows/);
  await act(page, `[data-data-id="${RUNNER}"] [data-act="sync-csv"]`);
  const csv = asText(await download(page, `runner-data-${day()}.csv`));
  assert.equal(csv, "TIME,TIME mm:ss.mmm,HITS\r\n61500,01:01.500,2\r\n125,00:00.125,0\r\n");

  await act(page, `[data-data-id="${RUNNER}"] [data-act="sync-clear"]`);
  await page.waitFor(`/Answer on StatTrak/.test(document.querySelector('[data-data-id="${RUNNER}"]').textContent)`);
  assert.equal(await page.evaluate("window.__device.display"), "DELETE RUNNER DATA?");
  await page.evaluate("window.__device.holdButton2()");
  await page.waitFor(`/Data deleted\\./.test(document.querySelector('[data-data-id="${RUNNER}"]').textContent)`);
  assert.match(await page.evaluate(sectionText("App data")), /0 \/ 512 rows/);
  assert.equal(await page.evaluate(`document.querySelector('[data-data-id="${RUNNER}"] [data-act="sync-csv"]').disabled`), true);
  assert.deepEqual(await page.evaluate(`window.__device.appData(${RUNNER})`), []);
});

test("device log: newest first with plain reasons, Download CSV", { skip: SKIP }, async (t) => {
  const page = await openApps(t, { deviceLog: [{ reason: 5, battery: 41, flags: 0, boot: 17 }, { reason: 2, battery: 255, flags: 1, boot: 16 }] });
  await page.waitFor(`document.querySelectorAll(".dlog li").length === 2`);
  const lines = await page.evaluate(`[...document.querySelectorAll(".dlog li")].map((li) => li.textContent)`);
  assert.deepEqual(lines, ["Start 17Low supply (brown-out)41%", "Start 16Restart (software or firmware update)USB / no batteryNew firmware"]);
  await act(page, '[data-act="sync-log-csv"]');
  const csv = asText(await download(page, `stattrak-log-${day()}.csv`)).split("\r\n");
  assert.deepEqual(csv.slice(0, 3), ["Start number,Reason,Reason code,Battery,New firmware", "17,Low supply (brown-out),5,41,", "16,Restart (software or firmware update),2,USB / no battery,yes"]);
});

/* ---- Screen Designer ------------------------------------------------------------------- */

async function openDesigner(t, deviceOptions) {
  const page = await openPage(t, "/designer/index.html", deviceOptions, 1);
  await page.waitFor(`window.StatTrakDesigner && document.getElementById("showBtn")`);
  return page;
}

/* The frame on the device is the selected screen of the designer (in the window `win`) */
const frameIsScreen = (win) => `(() => { const want = ${win}.StatTrakDesigner.pixels(), rows = FakeStatTrak.frameRows(window.__device.previewFrame);
  for (let y = 0; y < 32; y++) for (let x = 0; x < 128; x++) if ((rows[y][x] === "#") !== !!want[y * 128 + x]) return false; return true; })()`;

test("designer: Download PNG saves the selected screen as an exact 128 x 32 PNG, white = on", { skip: SKIP }, async (t) => {
  const page = await openDesigner(t);
  await act(page, "#pngBtn");
  await page.waitFor(`window.__downloads.length === 1 && window.__downloads[0].bytes`);
  const name = await page.evaluate("window.__downloads[0].name");
  assert.match(name, /\.png$/);
  const bytes = Buffer.from(await page.evaluate("window.__downloads[0].bytes"));
  assert.equal(bytes.subarray(1, 4).toString("latin1"), "PNG");
  assert.equal(bytes.readUInt32BE(16), 128, "width");
  assert.equal(bytes.readUInt32BE(20), 32, "height");
  // The pixels of the picture are the pixels of the screen
  const same = await page.evaluate(`(async () => {
    const bmp = await createImageBitmap(new Blob([new Uint8Array(window.__downloads[0].bytes)], { type: "image/png" }));
    const c = document.createElement("canvas"); c.width = 128; c.height = 32;
    const x = c.getContext("2d"); x.drawImage(bmp, 0, 0);
    const d = x.getImageData(0, 0, 128, 32).data;
    const want = StatTrakDesigner.pixels();
    let bad = 0, on = 0;
    for (let i = 0; i < 4096; i++) {
      const white = d[i * 4] === 255 && d[i * 4 + 1] === 255 && d[i * 4 + 2] === 255;
      const black = d[i * 4] === 0 && d[i * 4 + 1] === 0 && d[i * 4 + 2] === 0;
      if (want[i] ? !white : !black) bad++;
      if (want[i]) on++;
    }
    return { bad, on };
  })()`);
  assert.equal(same.bad, 0);
  assert.ok(same.on > 50, "the example screen has pixels");
});

test("designer: Show on StatTrak only while StatTrak is connected; one press = one frame; the state says when the screen changed", { skip: SKIP }, async (t) => {
  const page = await openDesigner(t);
  // No StatTrak: no button, no state
  assert.equal(await page.evaluate(`document.getElementById("showBtn").hidden`), true);
  assert.equal(await page.evaluate(`document.getElementById("showState").hidden`), true);
  // Connect from the StatTrak button of the header
  await act(page, '#devBar [data-dev="toggle"]');
  await act(page, '#devBar [data-dev="connect"]');
  await page.waitFor(`AppsTab.state.conn === "on" && !document.getElementById("showBtn").hidden && !document.getElementById("showBtn").disabled`);
  assert.equal(await text(page, "#showLbl"), "Show on StatTrak");
  assert.equal(await page.evaluate("window.__device.previewCount || 0"), 0, "nothing goes to StatTrak before a press");

  await act(page, "#showBtn");
  await page.waitFor(`window.__device.previewCount === 1 && StatTrakDesigner.show.shown`);
  assert.equal(await page.evaluate(frameIsScreen("window")), true, "the frame on the device is the screen");
  assert.ok(await page.evaluate("window.__device.xferDataCount < 37"), "empty parts were sent");
  assert.equal(await page.evaluate("window.__device.syncState"), 6);
  assert.equal(await text(page, "#showState"), "StatTrak shows this screen until you press a button on StatTrak.");

  // An other screen: nothing is sent by itself; the state says that StatTrak shows the older one
  await act(page, '.tile[data-i="1"]');
  await page.waitFor(`/older version/.test(document.getElementById("showState").textContent)`);
  assert.equal(await page.evaluate("window.__device.previewCount"), 1);
  await act(page, "#showBtn");
  await page.waitFor(`window.__device.previewCount === 2 && /shows this screen/.test(document.getElementById("showState").textContent)`);
  assert.equal(await page.evaluate(frameIsScreen("window")), true, "the frame on the device is the new screen");

  // Disconnect: the button goes away
  await act(page, '#devBar [data-dev="toggle"]');
  await act(page, '#devBar [data-dev="disconnect"]');
  await page.waitFor(`document.getElementById("showBtn").hidden && document.getElementById("showState").hidden`);
});

test("designer: a firmware without the preview: the button is off and the state says why", { skip: SKIP }, async (t) => {
  const page = await openDesigner(t, { data: false, apps: true });
  await act(page, '#devBar [data-dev="toggle"]');
  await act(page, '#devBar [data-dev="connect"]');
  await page.waitFor(`AppsTab.state.conn === "on" && SyncParts.state.supported === false`);
  await page.waitFor(`!document.getElementById("showBtn").hidden && document.getElementById("showBtn").disabled`);
  assert.equal(await text(page, "#showState"), "Update the firmware of StatTrak: it cannot show a screen from here.");
});

test("index.html: Get started and the Designer are tabs of the page; the StatTrak connection stays; Show on StatTrak uses it", { skip: SKIP }, async (t) => {
  const page = await openApps(t);
  await page.waitFor(`SyncParts.state.supported === true`);
  assert.equal(await page.evaluate(`document.getElementById("tabDesigner").getAttribute("href")`), "#designer");
  assert.equal(await page.evaluate(`document.getElementById("tabStart").getAttribute("href")`), "#start");
  // To the Designer: the same page, the connection stays, the Designer has no own header
  await act(page, "#tabDesigner");
  await page.waitFor(`location.hash === "#designer" && !document.getElementById("designerView").hidden && document.body.classList.contains("frame-on")`);
  const designer = `document.querySelector("#designerView iframe").contentWindow`;
  await page.waitFor(`${designer}.StatTrakDesigner && ${designer}.document.getElementById("showBtn")`);
  assert.equal(await page.evaluate("AppsTab.state.conn"), "on");
  assert.equal(await page.evaluate(`${designer}.getComputedStyle(${designer}.document.querySelector(".site-header")).display`), "none");
  assert.equal(await page.evaluate(`document.getElementById("tabDesigner").classList.contains("tab--on")`), true);
  // Show on StatTrak goes through the connection of index.html
  await page.waitFor(`!${designer}.document.getElementById("showBtn").hidden && !${designer}.document.getElementById("showBtn").disabled`);
  await page.evaluate(`${designer}.document.getElementById("showBtn").click()`);
  await page.waitFor(`window.__device.previewCount === 1 && ${designer}.StatTrakDesigner.show.shown`);
  assert.equal(await page.evaluate(frameIsScreen(designer)), true, "the frame on the device is the screen of the Designer");
  // Get started, then back to My device: still connected, no second pairing
  await act(page, "#tabStart");
  await page.waitFor(`!document.getElementById("startView").hidden && document.querySelector("#startView iframe")`);
  assert.equal(await page.evaluate("AppsTab.state.conn"), "on");
  // A link of Get started to My device changes the tab of index.html (no new page)
  const start = `document.querySelector("#startView iframe").contentWindow`;
  await page.waitFor(`${start}.document.readyState === "complete" && ${start}.document.querySelector('a[href*="index.html#device"]')`);
  await page.evaluate(`${start}.document.querySelector('a[href*="index.html#device"]').click()`);
  await page.waitFor(`location.hash === "#device" && !document.getElementById("appsView").hidden && !document.body.classList.contains("frame-on")`);
  assert.equal(await page.evaluate("AppsTab.state.conn"), "on");
  assert.equal(await page.evaluate("window.__device.requests.length"), 1, "one pairing only");
});
