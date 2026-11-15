/* Tests of the full page (index.html) in a real headless Chrome, with the stand-in
 * device in place of Web Bluetooth. Run: node --test "tests/*.test.js"
 *
 * The tests click the same buttons as a user. Time in the page runs faster
 * (setTimeout is divided), thus a wait of 30 seconds takes 1.5 seconds.
 * Set SCREENSHOTS=<folder> to keep a picture of the end of each test.
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const crypto = require("node:crypto");
const fs = require("node:fs");
const path = require("node:path");
const { Browser, serve, findChrome } = require("./browser.js");
const { ROOT, loadPageCode } = require("./helpers.js");

const FAKE_SOURCE = fs.readFileSync(path.join(__dirname, "fake-device.js"), "utf8");
const SKIP = findChrome() ? false : "no Chrome, Chromium or Edge on this computer";

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

/* The SHA-256 of the test firmware, as GitHub gives it for a release file. */
function digestOf(firmwareOptions) {
  const { FakeStatTrak } = loadPageCode();
  const bytes = FakeStatTrak.makeFirmware(firmwareOptions);
  return "sha256:" + crypto.createHash("sha256").update(Buffer.from(bytes)).digest("hex");
}

function initScript(deviceOptions, options) {
  const releases = options.releases || [];
  return `
    (() => {
      const real = window.setTimeout.bind(window);
      window.setTimeout = (fn, ms, ...args) => real(fn, (ms || 0) / ${options.speed}, ...args);
    })();
    ${FAKE_SOURCE}
    window.__device = new FakeStatTrak(${JSON.stringify(deviceOptions)});
    Object.defineProperty(navigator, "bluetooth", { value: window.__device.bluetooth(), configurable: true });
    window.fetch = async () => new Response(${JSON.stringify(JSON.stringify(releases))}, { status: 200 });
  `;
}

async function load(page) {
  await page.goto(site.url + "/index.html");
  // This limit is real time in the page code. Make it shorter by the same factor as the timers.
  await page.evaluate(`BleOta.RECONNECT_GIVE_UP_MS = ${300000 / page.settings.speed}`);
}

/* Opens the page with a stand-in device. */
async function open(t, deviceOptions, options) {
  const settings = Object.assign({ speed: 20, releases: [] }, options || {});
  const page = await browser.newPage();
  page.settings = settings;
  // What a test that stops at a wait shows: the last lines of the page console, and the device
  page.diagnose = () => page.evaluate(`JSON.stringify({
    flashState: state.flashState, connState: state.connState, file: !!state.uploadedFile,
    log: state.log.slice(-12).map((line) => line.m), device: window.__device.events.slice(-12),
  }, null, 1)`);
  await page.setInitScript(initScript(deviceOptions || {}, settings));
  await load(page);
  t.after(async () => {
    if (process.env.SCREENSHOTS) {
      const name = t.name.replace(/[^a-z0-9]+/gi, "-").toLowerCase().slice(0, 80);
      await page.screenshot(path.join(process.env.SCREENSHOTS, name + ".png"));
    }
    assert.deepEqual(page.errors, [], "errors in the page");
    await page.close();
  });
  return page;
}

const visible = (id) => `!document.getElementById("${id}").hidden`;
const text = (page, id) => page.evaluate(`document.getElementById("${id}").textContent.trim()`);
const click = (page, id) => page.evaluate(`document.getElementById("${id}").click()`);
const device = (page, expression) => page.evaluate(`window.__device.${expression}`);

async function pair(page) {
  await click(page, "pairBtn");
  await page.waitFor(visible("step1After"));
}

async function dropFile(page, firmwareOptions, name = "stattrak.bin") {
  await page.evaluate(`(() => {
    const bytes = FakeStatTrak.makeFirmware(${JSON.stringify(firmwareOptions || {})});
    const transfer = new DataTransfer();
    transfer.items.add(new File([bytes], ${JSON.stringify(name)}));
    document.getElementById("dropzone").dispatchEvent(
      new DragEvent("drop", { dataTransfer: transfer, bubbles: true, cancelable: true }));
  })()`);
}

/* Pair, select a file, and start the update, as a user does. */
async function startUpdate(page, firmwareOptions) {
  await pair(page);
  await click(page, "step1NextBtn");
  await dropFile(page, firmwareOptions);
  await page.waitFor(visible("fileCard"));
  await click(page, "step2NextBtn");
  await click(page, "flashBtn");
  await page.evaluate(`document.querySelector("[data-confirm]").click()`);
}

const noticeTitle = (page) => text(page, "noticeTitle");
const pendingMark = (page) => page.evaluate(`localStorage.getItem("stattrak.pendingAccept")`);

test("a good update: the page shows success only after the device confirmed", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1" });
  await startUpdate(page, { version: "2.0.0" });

  // The transfer is complete, the device did not restart yet: this is not success
  await page.waitFor(`document.getElementById("flashLabel").textContent === "Waiting for the device"`);
  assert.equal(await page.evaluate(visible("successPanel")), false);
  assert.equal(await page.evaluate(visible("flashHint")), true);
  assert.notEqual(await pendingMark(page), null);

  await page.waitFor(visible("successPanel"));
  assert.equal(await text(page, "successVersion"), "2.0.0");
  assert.equal(await device(page, "display"), "Complete v2.0.0");
  assert.equal(await device(page, "acceptCount"), 2); // one before the transfer, one after the restart
  assert.equal(await pendingMark(page), null);
  assert.equal(await page.evaluate(visible("noticeBanner")), false);

  // The user presses the button on the device: the page keeps the result and does not connect again
  await device(page, "pressOk()");
  await new Promise((resolve) => setTimeout(resolve, 300));
  assert.equal(await text(page, "successVersion"), "2.0.0");
  assert.equal(await page.evaluate(visible("successPanel")), true);
});

test("Flash again after a success does a full second update", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1" });
  await startUpdate(page, { version: "2.0.0" });
  await page.waitFor(visible("successPanel"));

  // The device still shows "Complete" and is connected
  await click(page, "flashAgainBtn");
  await click(page, "step2NextBtn");
  await click(page, "flashBtn");
  await page.evaluate(`document.querySelector("[data-confirm]").click()`);

  await page.waitFor(`window.__device.events.filter((e) => e === "accepted 2.0.0").length === 2`);
  await page.waitFor(visible("successPanel"));
  assert.equal(await device(page, "display"), "Complete v2.0.0");
  assert.equal(await page.evaluate(visible("noticeBanner")), false);
});

test("a firmware that waited before the transfer does not make a bad transfer a success", { skip: SKIP }, async (t) => {
  // The device shows "Verifying…" for firmware 1.5.0 of an earlier transfer. The new file is damaged.
  const page = await open(t, { version: "1.5.0", waitsForConfirmation: true });
  await startUpdate(page, { version: "2.0.0", valid: false });

  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "The device refused the file");
  assert.equal(await page.evaluate(visible("successPanel")), false);
  // The earlier firmware is confirmed, thus the device keeps it
  assert.equal(await device(page, "pendingVerify"), false);
  assert.equal(await device(page, "version"), "1.5.0");
});

test("a device that does not answer before the transfer: the transfer does not start", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", ignoreAccept: true });
  await startUpdate(page, { version: "2.0.0" });

  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "The device did not answer");
  assert.equal(await device(page, "startCount"), 0);
});

test("a new firmware that does not answer the confirmation: a message, not an endless wait", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", newFirmwareIgnoresAccept: true }, { speed: 60 });
  await startUpdate(page, { version: "2.0.0" });

  await page.waitFor(visible("noticeBanner"), 20000);
  assert.equal(await noticeTitle(page), "No answer from the device");
  assert.equal(await page.evaluate(visible("successPanel")), false);
  assert.notEqual(await pendingMark(page), null);
  // The page is back at step 1, where the user can pair again
  await page.waitFor(visible("step1Before"));
  assert.equal(await device(page, "gatt.connected"), false);
});

test("an answer 'yes' that was lost: the page does not say that the firmware did not start", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", loseAcceptReplies: 1 });
  await startUpdate(page, { version: "2.0.0" });

  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "Look at the device display");
  assert.match(await text(page, "noticeBody"), /runs firmware 2\.0\.0/);
  assert.equal(await device(page, "display"), "Complete v2.0.0");
  assert.equal(await page.evaluate(visible("successPanel")), false);
  assert.equal(await page.evaluate(`document.getElementById("noticeBanner").className`), "banner banner--warn");
});

test("a second confirmation while the first one waits does not hide a lost 'yes'", { skip: SKIP }, async (t) => {
  // The device is back shortly before the restart time of the page ends. Its "yes" is lost,
  // and the page still waits for it when the restart time ends.
  const page = await open(t, { version: "1.0.1", loseAcceptReplies: 1, restartMs: 24000 });
  await page.evaluate(`BleOta.INDICATION_TIMEOUT_MS = 60000`);
  await startUpdate(page, { version: "2.0.0" });

  await page.waitFor(visible("noticeBanner"), 20000);
  assert.equal(await noticeTitle(page), "Look at the device display");
  assert.equal(await device(page, "display"), "Complete v2.0.0");
  assert.notEqual(await pendingMark(page), null);
});

test("a confirmation with no answer is sent again, and a 'yes' then completes the update", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", newFirmwareIgnoresAccept: true });
  await startUpdate(page, { version: "2.0.0" });
  await page.waitFor(`window.__device.screen === "accept"`);
  await new Promise((resolve) => setTimeout(resolve, 600));
  assert.equal(await page.evaluate(visible("successPanel")), false);

  // The device answers again
  await page.evaluate(`window.__device.options.ignoreAccept = false`);
  await page.waitFor(visible("successPanel"));
  assert.equal(await device(page, "display"), "Complete v2.0.0");
});

test("a late answer of an earlier connection does not change the result of the next transfer", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", newFirmwareIgnoresAccept: true }, { speed: 60 });
  await page.evaluate(`BleOta.INDICATION_TIMEOUT_MS = 3600000`);
  await startUpdate(page, { version: "2.0.0" });

  // The new firmware does not answer. After 6 minutes the page stops, with the confirmation still open.
  await page.waitFor(visible("noticeBanner"), 20000);
  assert.equal(await noticeTitle(page), "No answer from the device");
  await page.evaluate(`window.__oldCustom = window.__device.links[window.__device.links.length - 1].characteristics[0x8023]`);

  // The device answers again. The user pairs again: the firmware is confirmed.
  await page.evaluate(`window.__device.options.ignoreAccept = false`);
  await pair(page);
  await page.waitFor(`document.getElementById("noticeTitle").textContent === "Firmware confirmed"`);

  // Then the user sends a damaged file
  await click(page, "step1NextBtn");
  await click(page, "fileCardRemove");
  await dropFile(page, { version: "3.0.0", valid: false });
  await page.waitFor(visible("fileCard"));
  await click(page, "step2NextBtn");
  await click(page, "flashBtn");
  await page.evaluate(`document.querySelector("[data-confirm]").click()`);
  await page.waitFor(`window.__device.display === "Error"`);

  // The open confirmation of the first connection ends now, with no valid answer
  await page.evaluate(`(() => {
    window.__oldCustom.value = new DataView(new ArrayBuffer(20));
    window.__oldCustom.dispatchEvent(new Event("characteristicvaluechanged"));
  })()`);

  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "The device refused the file");
  assert.equal(await pendingMark(page), null);
});

test("the confirm button and the start of a transfer at the same time: the update completes", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.5.0", waitsForConfirmation: true });
  await pair(page);
  await click(page, "step1NextBtn");
  await dropFile(page, { version: "2.0.0" });
  await page.waitFor(visible("fileCard"));
  await click(page, "step2NextBtn");
  await click(page, "flashBtn");
  // The answer to the button is still open when the transfer starts
  await page.evaluate(`(() => {
    document.getElementById("confirmFwBtn").click();
    document.querySelector("[data-confirm]").click();
  })()`);

  await page.waitFor(`window.__device.events.includes("accepted 2.0.0")`);
  await page.waitFor(visible("successPanel"));
  assert.equal(await text(page, "successVersion"), "2.0.0");
  assert.ok(await page.evaluate(`window.__device.events.includes("accepted 1.5.0")`));
});

test("an update of firmware 1.0.1", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", legacy: true });
  await startUpdate(page, { version: "2.0.0" });
  await page.waitFor(visible("successPanel"));
  assert.equal(await text(page, "successVersion"), "2.0.0");
  assert.equal(await device(page, "acceptCount"), 2);
});

test("a file that the device refuses after the transfer: no success", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1" });
  await startUpdate(page, { version: "2.0.0", valid: false });

  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "The device refused the file");
  assert.equal(await page.evaluate(visible("successPanel")), false);
  assert.equal(await device(page, "display"), "Error");
  assert.equal(await device(page, "version"), "1.0.1");
  assert.equal(await pendingMark(page), null);
  // The user can start again
  assert.equal(await page.evaluate(`document.getElementById("flashBtn").disabled`), false);
});

test("a new firmware that does not start: the page says that the old firmware is back", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", newFirmwareCrashes: true });
  await startUpdate(page, { version: "2.0.0" });

  await page.waitFor(`window.__device.events.includes("rollback")`);
  // The restart time of the page (30 s) is over, then the user opens the update screen again
  await new Promise((resolve) => setTimeout(resolve, 30000 / page.settings.speed + 300));
  assert.equal(await page.evaluate(visible("successPanel")), false);
  await device(page, "enterUpdateMode()");

  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "The new firmware did not start");
  assert.match(await text(page, "noticeBody"), /1\.0\.1/);
  assert.equal(await page.evaluate(visible("successPanel")), false);
});

test("a device that does not come back: no success, and the next pairing can confirm", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", staysOffAfterRestart: true }, { speed: 60 });
  await startUpdate(page, { version: "2.0.0" });

  // The page stops its connection tries after 5 minutes
  await page.waitFor(visible("noticeBanner"), 20000);
  assert.equal(await noticeTitle(page), "No answer from the device");
  assert.equal(await page.evaluate(visible("successPanel")), false);
  assert.notEqual(await pendingMark(page), null);

  // The user switches the device on and pairs again
  await device(page, "powerOn()");
  await pair(page);
  await page.waitFor(`document.getElementById("noticeTitle").textContent === "Firmware confirmed"`);
  assert.equal(await device(page, "display"), "Complete v2.0.0");
  assert.equal(await pendingMark(page), null);
});

test("the old firmware came back and the page did not see it: the next pairing tells the user", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", newFirmwareCrashes: true }, { speed: 60 });
  await startUpdate(page, { version: "2.0.0" });
  await page.waitFor(visible("noticeBanner"), 20000);
  assert.equal(await noticeTitle(page), "No answer from the device");

  await device(page, "enterUpdateMode()");
  await pair(page);
  await page.waitFor(`document.getElementById("noticeTitle").textContent === "No new firmware on the device"`);
  assert.match(await text(page, "noticeBody"), /runs firmware 1\.0\.1.*\(2\.0\.0\)/);
  assert.equal(await pendingMark(page), null);
  assert.equal(await page.evaluate(visible("successPanel")), false);
});

test("the device that switches off at the restart: success after the user switches it on", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", staysOffAfterRestart: true });
  await startUpdate(page, { version: "2.0.0" });

  await page.waitFor(`window.__device.screen === "off"`);
  // Longer than the restart time of the page: the page must continue to wait
  await new Promise((resolve) => setTimeout(resolve, 30000 / page.settings.speed + 500));
  assert.equal(await page.evaluate(visible("successPanel")), false);
  assert.equal(await page.evaluate(visible("noticeBanner")), false);
  assert.equal(await page.evaluate(visible("flashHint")), true);

  await device(page, "powerOn()");
  await page.waitFor(visible("successPanel"));
  assert.equal(await text(page, "successVersion"), "2.0.0");
});

test("the page was loaded again after the transfer: the next pairing confirms the firmware", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", staysOffAfterRestart: true });
  await startUpdate(page, { version: "2.0.0" });
  await page.waitFor(`window.__device.screen === "off"`);

  // The user loads the page again and switches the device on: it shows "Verifying…"
  await page.setInitScript(initScript({ version: "2.0.0", waitsForConfirmation: true }, page.settings));
  await load(page);
  assert.equal(await device(page, "display"), "Verifying...");

  await pair(page);
  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "Firmware confirmed");
  assert.equal(await device(page, "display"), "Complete v2.0.0");
  assert.equal(await pendingMark(page), null);
});

test("a pairing with no open transfer sends no confirmation", { skip: SKIP }, async (t) => {
  // Firmware 1.0.1 shows "Accepted" for each confirmation command
  const page = await open(t, { version: "1.0.1", legacy: true });
  await pair(page);
  await new Promise((resolve) => setTimeout(resolve, 200));
  assert.equal(await device(page, "acceptCount"), 0);
  assert.equal(await device(page, "display"), "Waiting...");
  assert.equal(await text(page, "deviceFw"), "1.0.1");
  assert.equal(await text(page, "deviceIdVal"), "34B7DA010203");
});

test("a transfer to an other device sends no confirmation to this device", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", legacy: true });
  await page.evaluate(`localStorage.setItem("stattrak.pendingAccept",
    JSON.stringify({ uid: "AABBCCDDEEFF", version: "2.0.0", time: Date.now() }))`);
  await pair(page);
  await new Promise((resolve) => setTimeout(resolve, 200));
  assert.equal(await device(page, "acceptCount"), 0);
  assert.notEqual(await pendingMark(page), null);
});

test("a short loss of the connection before the restart does not stop the update", { skip: SKIP }, async (t) => {
  // The page connects again to the old firmware, which answers "nothing to confirm"
  const page = await open(t, { version: "1.0.1", restartMs: 24000 });
  await startUpdate(page, { version: "2.0.0" });
  await page.waitFor(`window.__device.display === "Completed"`);
  await device(page, "loseConnection()");
  await page.waitFor(`window.__device.acceptCount === 2`);
  assert.equal(await page.evaluate(visible("noticeBanner")), false);

  await page.waitFor(visible("successPanel"));
  assert.equal(await device(page, "acceptCount"), 3);
  assert.equal(await text(page, "successVersion"), "2.0.0");
});

test("the button for a device that shows Verifying confirms the firmware", { skip: SKIP }, async (t) => {
  // The transfer was made with an other browser: this page has no mark of it
  const page = await open(t, { version: "2.0.0", waitsForConfirmation: true });
  await pair(page);
  assert.equal(await device(page, "acceptCount"), 0);

  await click(page, "confirmFwBtn");
  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "Firmware confirmed");
  assert.equal(await device(page, "display"), "Complete v2.0.0");

  await click(page, "confirmFwBtn");
  await page.waitFor(`document.getElementById("noticeTitle").textContent === "Nothing to confirm"`);
});

test("after a confirmation the device leaves: the page does not connect again", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "2.0.0", waitsForConfirmation: true });
  await page.evaluate(`BleOta.RECONNECT_GIVE_UP_MS = 200`);
  await pair(page);
  await click(page, "confirmFwBtn");
  await page.waitFor(`document.getElementById("noticeTitle").textContent === "Firmware confirmed"`);

  const tries = await page.evaluate(`window.__device.events.filter((e) => e === "connect try").length`);
  await device(page, "pressOk()");
  await page.waitFor(visible("step1Before"));
  await new Promise((resolve) => setTimeout(resolve, 800));
  assert.equal(await noticeTitle(page), "Firmware confirmed");
  assert.equal(await page.evaluate(`window.__device.events.filter((e) => e === "connect try").length`), tries);
});

test("a wrong file is refused when the user selects it", { skip: SKIP }, async (t) => {
  const page = await open(t, {});
  await pair(page);
  await click(page, "step1NextBtn");
  await dropFile(page, { project: "blink" }, "blink.bin");

  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "This file cannot be used");
  assert.match(await text(page, "noticeBody"), /blink\.bin.*project name/);
  assert.equal(await page.evaluate(visible("fileCard")), false);
  assert.equal(await page.evaluate(visible("step2NextRow")), false);

  // A file that is too large is refused before the page reads it
  await dropFile(page, { size: 1024 * 1024 + 1 }, "large.bin");
  await page.waitFor(`document.getElementById("noticeBody").textContent.includes("large.bin")`);
  assert.match(await text(page, "noticeBody"), /largest firmware is 1048576 bytes/);
  assert.equal(await page.evaluate(visible("fileCard")), false);

  // A good file after that is accepted, and the message goes away
  await dropFile(page, { version: "2.0.0" });
  await page.waitFor(visible("fileCard"));
  assert.equal(await page.evaluate(visible("noticeBanner")), false);
  assert.match(await text(page, "fileCardMeta"), /StatTrak firmware · version 2\.0\.0/);
});

test("a sector that arrives damaged does not stop the update", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", damageSector: { 1: 2 } });
  await startUpdate(page, { version: "2.0.0" });
  await page.waitFor(visible("successPanel"));
  assert.equal(await page.evaluate(`window.__device.events.filter((e) => e === "damaged sector 1").length`), 2);
});

test("a connection that is lost during the transfer: no success, the user can start again", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1", disconnectAtSector: 1 });
  await startUpdate(page, { version: "2.0.0" });

  await page.waitFor(visible("noticeBanner"));
  assert.equal(await noticeTitle(page), "The transfer stopped");
  assert.equal(await page.evaluate(visible("successPanel")), false);
  assert.equal(await pendingMark(page), null);
  assert.equal(await device(page, "version"), "1.0.1");
});

test("the Disconnect button disconnects, and the page does not connect again", { skip: SKIP }, async (t) => {
  const page = await open(t, {});
  await pair(page);
  await click(page, "disconnectBtn");
  await page.waitFor(visible("step1Before"));
  await new Promise((resolve) => setTimeout(resolve, 400));
  assert.equal(await device(page, "gatt.connected"), false);
  assert.equal(await page.evaluate(visible("step1Before")), true);
});

test("the page tells if the file is an official release", { skip: SKIP }, async (t) => {
  const releases = [{
    id: 1, tag_name: "v2.0.0", name: "v2.0.0", draft: false, prerelease: false,
    html_url: "https://example.invalid/release", published_at: "2026-11-01T00:00:00Z", body: "Test release",
    assets: [{ name: "stattrak.bin", size: 10000, digest: digestOf({ version: "2.0.0" }) }],
  }];
  const page = await open(t, {}, { releases });
  await pair(page);
  await click(page, "step1NextBtn");

  await dropFile(page, { version: "2.0.0" });
  await page.waitFor(`document.getElementById("fileCardMeta").textContent.includes("official release v2.0.0")`);
  await click(page, "step2NextBtn");
  await click(page, "flashBtn");
  assert.equal(await page.evaluate(`document.querySelector("[data-unofficial]").hidden`), true);
  // While the question is open, the links to the other pages are locked; Cancel unlocks them
  const locked = () => page.evaluate(`["tabStart", "tabDesigner", "tabApps"].map((id) => document.getElementById(id).getAttribute("aria-disabled"))`);
  assert.deepEqual(await locked(), ["true", "true", "true"]);
  await page.evaluate(`document.querySelector("[data-cancel]").click()`);
  assert.deepEqual(await locked(), [null, null, null]);

  // A file that a user built is not in the list
  await click(page, "fileCardRemove");
  await dropFile(page, { version: "2.0.1-mine" });
  await page.waitFor(`document.getElementById("fileCardMeta").textContent.includes("not an official release")`);
  await click(page, "step2NextBtn");
  await click(page, "flashBtn");
  assert.equal(await page.evaluate(`document.querySelector("[data-unofficial]").hidden`), false);
});

test("step 1 plays the way to the update mode of firmware 1.0.1 on the device picture", { skip: SKIP }, async (t) => {
  const page = await open(t);
  const dev = `document.querySelector("#step1Before st-device")`;
  await page.waitFor(`${dev}.dataset.frame !== undefined && ${dev}.querySelector("img").naturalWidth > 0`);
  assert.equal(await page.evaluate(`${dev}.querySelector("img").naturalWidth`), 1896);
  assert.match(await page.evaluate(`${dev}.getAttribute("aria-label")`), /hold the right button/);
  // The display has lit pixels
  const lit = await page.evaluate(`(() => { const c = ${dev}.querySelector("canvas");
    const d = c.getContext("2d").getImageData(0, 0, c.width, c.height).data; let n = 0;
    for (let i = 3; i < d.length; i += 4) if (d[i]) n++; return n; })()`);
  assert.ok(lit > 100, "lit pixels: " + lit);
  // It plays: both buttons (to Settings), then button 2 held (the update mode)
  await page.waitFor(`${dev}.dataset.press === "B"`);
  await page.waitFor(`${dev}.dataset.press === "H2"`, 20000);
});

test("the fonts of the page load, and all files come from the page's own address", { skip: SKIP }, async (t) => {
  const page = await open(t, {});
  // load() gives the declared faces that it loaded. A family that is not declared gives none.
  const fonts = JSON.parse(await page.evaluate(`(async () => JSON.stringify({
    headline: (await document.fonts.load("italic 900 36px 'Barlow Condensed'")).length,
    body: (await document.fonts.load("400 16px 'Barlow Condensed'")).length,
    mono: (await document.fonts.load("400 12px 'JetBrains Mono'")).length,
    failed: [...document.fonts].filter((font) => font.status === "error").map((font) => font.family),
    bodyFamily: getComputedStyle(document.body).fontFamily,
    hosts: [...new Set(performance.getEntriesByType("resource").map((entry) => new URL(entry.name).host))],
    own: location.host,
  }))()`));
  assert.deepEqual(fonts.failed, []);
  assert.deepEqual([fonts.headline, fonts.body, fonts.mono], [1, 1, 1]);
  assert.match(fonts.bodyFamily, /Barlow Condensed/);
  assert.deepEqual(fonts.hosts, [fonts.own]);
});

test("the page has no links without a target", { skip: SKIP }, async (t) => {
  const page = await open(t, {});
  const dead = await page.evaluate(`Array.from(document.querySelectorAll("a")).filter((a) => {
    const href = a.getAttribute("href");
    return !href || href === "#";
  }).length`);
  assert.equal(dead, 0);
});

test("the firmware tab tells added scripts about the device: connected, flashed, disconnected", { skip: SKIP }, async (t) => {
  const page = await open(t, { version: "1.0.1" });
  await page.evaluate(`window.__events = []; window.addEventListener("stattrak:device", (e) => window.__events.push(e.detail))`);
  await startUpdate(page, { version: "2.0.0" });
  await page.waitFor(visible("successPanel"));
  const events = await page.evaluate("window.__events");
  assert.deepEqual(events[0], { event: "connected", serial: "34B7DA010203", fw: "1.0.1", previousFw: "", name: "" });
  const flashed = events.find((e) => e.event === "flashed");
  assert.deepEqual(flashed, { event: "flashed", serial: "34B7DA010203", fw: "2.0.0", previousFw: "1.0.1", name: "" });
  assert.equal(await page.evaluate("SiteEvents.device.fw"), "2.0.0");
  // The StatTrak button of the header shows the device of the firmware tab
  assert.match(await page.evaluate(`document.querySelector("#devBar .devbtn").textContent`), /StatTrak/);
  await click(page, "doneBtn");
  assert.equal((await page.evaluate("window.__events")).at(-1).event, "disconnected");
  assert.equal(await page.evaluate("SiteEvents.device"), null);
  assert.match(await page.evaluate(`document.querySelector("#devBar .devbtn").textContent`), /Connect/);
});
