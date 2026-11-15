/* Tests of js/apps-protocol.js (the app commands and the .stapp check) in Node.js, with the
 * stand-in device, and of the device pictures of js/apps-pictures.js.
 * Run: node --test "tests/*.test.js"
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { ROOT, loadPageCode, pair, once } = require("./helpers.js");

const CATALOG = JSON.parse(fs.readFileSync(path.join(ROOT, "apps", "catalog.json"), "utf8"));

/* A package of the catalog */
function catalogFile(name) {
  const app = CATALOG.apps.find((a) => a.name === name);
  return new Uint8Array(fs.readFileSync(path.join(ROOT, "apps", app.file)));
}

/* A device in Settings > "Apps: SYNC", and a connection to it */
async function connect(deviceOptions, options) {
  const page = loadPageCode(Object.assign({ apps: true, appQuestionMs: 400 }, deviceOptions || {}));
  page.StatTrakApps.STATUS_POLL_MS = 5;
  page.StatTrakApps.DRAIN_MS = 250;
  page.StatTrakApps.QUESTION_WAIT_MS = 2000;
  if (!(options && options.screen)) {
    page.device.screen = "counter";
    page.device.openAppSync();
  }
  const ota = await pair(page);
  const apps = new page.StatTrakApps(ota);
  return { page, device: page.device, ota, apps };
}

/* Installs a package; the person on the device does `answer` when the device asks */
async function install(t, bytes, answer) {
  const steps = [];
  const result = await t.apps.install(bytes, (fraction) => steps.push(fraction), () => {
    steps.push("waiting");
    if (answer) setTimeout(() => t.device[answer](), 20);
  });
  return { result, steps };
}

/* deepEqual for values of the page context (other Array and Object classes) */
const plain = (value) => JSON.parse(JSON.stringify(value));
const same = (actual, expected, message) => assert.deepEqual(plain(actual), plain(expected), message);

async function rejects(promise, check) {
  try {
    await promise;
  } catch (error) {
    for (const [key, value] of Object.entries(check)) assert.equal(error[key], value, key);
    return error;
  }
  assert.fail("no error");
}

const noUpdateCommands = (device) => {
  assert.equal(device.startCount, 0, "a firmware update was started");
  assert.equal(device.acceptCount, 0, "a firmware was confirmed");
};

test("INFO and LIST: the built-in apps, the installed apps and their full names", async () => {
  const timer = catalogFile("ROUND TIMER");
  const t2 = await connect({ installedApps: [timer], hiddenApps: [2] });

  const info = await t2.apps.info();
  same({ ...info }, { engine: 1, format: 2, free: 24576 - timer.length, maxPackage: 4096, count: 1, maxApps: 8 });

  const list = await t2.apps.list();
  same(list.map((a) => a.name), ["KILLS", "KILLS + DEAD", "STATS", "ROUND TIMER"]);
  same(list.map((a) => a.hidden), [false, true, false, false]);
  same(list.map((a) => a.builtin), [true, true, true, false]);
  const round = list[3];
  assert.equal(round.id, 0xcc78e229);
  assert.equal(round.size, timer.length);
  assert.equal(round.settingCount, 2);
  assert.equal(round.hasSettings, true);
  noUpdateCommands(t2.device);
  t2.ota.disconnect();
});

test("an install: the question on the device first, 'yes', then the transfer, and the app is in the list", async () => {
  const t = await connect({});
  const bytes = catalogFile("REACTION");
  const { result, steps } = await install(t, bytes, "holdButton2");
  assert.equal(result, "installed");
  assert.equal(steps[0], "waiting", "the device asks before any data is sent");
  assert.equal(steps[1], 0);
  assert.equal(steps[steps.length - 1], 1);
  same(t.device.events.filter((e) => /question|app/.test(e)), ["question REACTION", "app yes REACTION", "app installed REACTION"]);
  // The order of the commands: INFO, BEGIN, ASK, STATUS until yes, DATA, END
  const commands = t.device.appCommands.filter((c, i, all) => c !== all[i - 1]);
  same(commands, [0x0110, 0x0113, 0x011B, 0x0116, 0x0114, 0x0115]);
  same(Array.from(t.device.installed.get(0x62e49bbb).pkg), Array.from(bytes));
  same(t.device.appNames(), ["KILLS", "KILLS + DEAD", "STATS", "REACTION"]);
  assert.equal(t.device.syncState, 3);
  assert.equal(t.device.display, "APP INSTALLED");
  assert.equal((await t.apps.info()).free, 24576 - bytes.length);
  noUpdateCommands(t.device);
  t.ota.disconnect();
});

test("the person says 'no', or does not answer in 30 s: no data is sent, the app is not installed", async () => {
  const t = await connect({});
  const bytes = catalogFile("REACTION");
  assert.equal((await install(t, bytes, "holdButton1")).result, "refused");
  assert.equal((await install(t, bytes, null)).result, "timeout");
  same(t.device.appNames(), ["KILLS", "KILLS + DEAD", "STATS"]);
  assert.equal(t.device.appCommands.includes(0x0114), false, "DATA was sent");
  assert.equal(t.device.appWrites, 0);
  t.ota.disconnect();
});

test("the device stops the transfer (hold button 1): 'stopped', the next install starts again with BEGIN", async () => {
  const t = await connect({});
  const bytes = catalogFile("SLOTS");
  let stopped = false;
  const result = await t.apps.install(bytes, (fraction) => {
    if (fraction > 0.3 && !stopped) { stopped = true; t.device.holdButton1(); }
  }, () => setTimeout(() => t.device.holdButton2(), 20));
  assert.equal(result, "stopped");
  assert.equal(t.device.syncState, 0);
  assert.equal(t.device.installed.size, 0);
  assert.equal((await install(t, bytes, "holdButton2")).result, "installed");
  t.ota.disconnect();
});

test("a lost answer to DATA: the page sends again from the offset that the device expects", async () => {
  const t = await connect({ loseDataAnswerAt: 28 });
  const bytes = catalogFile("HEADSHOT %");
  const { result } = await install(t, bytes, "holdButton2");
  assert.equal(result, "installed");
  assert.ok(t.device.events.includes("lost answer of DATA 28"));
  same(Array.from(t.device.installed.get(0x3b4aad40).pkg), Array.from(bytes));
  t.ota.disconnect();
});

test("a lost answer to END: STATUS 3 says the app is installed", async () => {
  const t = await connect({ loseAppAnswers: { 0x0115: 1 } });
  const bytes = catalogFile("HEADSHOT %");
  const { result } = await install(t, bytes, "holdButton2");
  assert.equal(result, "installed");
  assert.ok(t.device.events.includes("lost answer of 0x115"));
  assert.ok(t.device.installed.has(0x3b4aad40));
  t.ota.disconnect();
});

test("a late answer of an earlier DATA command does not make a gap in the package", async () => {
  const t = await connect({});
  const bytes = catalogFile("ROUND TIMER");
  // The device gets the data and answers late: the page reads the answer of the next command
  const write = t.device._userCommand.bind(t.device);
  let delayed = false;
  t.device._userCommand = function (cmdId, d) {
    if (cmdId === 0x0114 && !delayed && (d[0] | (d[1] << 8)) === 42) {
      delayed = true;
      setTimeout(() => write(cmdId, d), 300); // later than the time limit of the page
      return;
    }
    write(cmdId, d);
  };
  const { result } = await install(t, bytes, "holdButton2");
  assert.equal(result, "installed");
  same(Array.from(t.device.installed.get(0xcc78e229).pkg), Array.from(bytes));
  t.ota.disconnect();
});

test("no space, too many apps, too large: the device refuses at BEGIN", async () => {
  const { FakeStatTrak } = loadPageCode();
  const big = (i) => FakeStatTrak.makeApp({ name: "BIG " + i, id: 0x10000 + i, size: 4096 });
  let t = await connect({ installedApps: [1, 2, 3, 4, 5, 6].map(big) });
  await rejects(t.apps.install(FakeStatTrak.makeApp({ id: 0x999, size: 400 })), { kind: "status", status: 5 });
  t.ota.disconnect();

  const small = (i) => FakeStatTrak.makeApp({ name: "APP " + i, id: 0x20000 + i });
  t = await connect({ installedApps: [1, 2, 3, 4, 5, 6, 7, 8].map(small) });
  const ninth = FakeStatTrak.makeApp({ name: "NINTH", id: 0x777 });
  await rejects(t.apps.install(ninth), { kind: "status", status: 7 });
  // A new version of an installed app replaces it: no 9th app
  const update = FakeStatTrak.makeApp({ name: "APP 1", id: 0x20001, version: 2 });
  assert.equal((await install(t, update, "holdButton2")).result, "installed");
  assert.equal(t.device.installed.get(0x20001).version, 2);
  assert.equal(t.device.installed.size, 8);
  t.ota.disconnect();
});

test("a package that the device check refuses: status 10 and the reason, the display shows NOT INSTALLED", async () => {
  const t = await connect({ refusePackage: 10 });
  await rejects(install(t, catalogFile("REACTION"), "holdButton2"), { kind: "status", status: 10, reason: "bad code" });
  assert.equal(t.device.syncState, 0);
  assert.equal(t.device.display, "NOT INSTALLED");
  t.ota.disconnect();
});

test("package format 2: installed on a firmware that reads it; refused by the page for a firmware with format 1", async () => {
  const { FakeStatTrak } = loadPageCode();
  const runner = catalogFile("RUNNER");
  assert.equal(runner[4], 2, "RUNNER is a format 2 package");
  let t = await connect({});
  assert.equal((await install(t, runner, "holdButton2")).result, "installed");
  const small = FakeStatTrak.makeApp({ name: "DOWN", id: 0x5151, format: 2 });
  assert.equal(small.length, 313);
  assert.equal((await install(t, small, "holdButton2")).result, "installed");
  t.ota.disconnect();

  t = await connect({ appFormat: 1 });
  assert.equal((await t.apps.info()).format, 1);
  await rejects(t.apps.install(runner), { kind: "file", problem: "newer format" });
  assert.equal(t.device.appCommands.includes(0x0113), false, "BEGIN was sent");
  // A format 1 package still goes
  assert.equal((await install(t, catalogFile("REACTION"), "holdButton2")).result, "installed");
  t.ota.disconnect();
});

test("firmware without apps: 'old firmware' for 1.1.0, no answer for 1.0.1", async () => {
  let t = await connect({ apps: false, userCommands: true, version: "1.1.0" }, { screen: "update" });
  await rejects(t.apps.info(), { kind: "old firmware" });
  t.ota.disconnect();
  t = await connect({ apps: false, version: "1.0.1" }, { screen: "update" });
  await rejects(t.apps.info(), { kind: "no answer" });
  t.ota.disconnect();
});

test("a device that does not show Apps: SYNC answers status 3 to each app command", async () => {
  const t = await connect({}, { screen: "update" });
  await rejects(t.apps.info(), { kind: "not in sync" });
  await rejects(t.apps.list(), { kind: "not in sync" });
  await rejects(t.apps.install(catalogFile("REACTION")), { kind: "not in sync" });
  await rejects(t.apps.deviceName(), { kind: "not in sync" });
  assert.equal(t.device.syncState, 0);
  noUpdateCommands(t.device);
  t.ota.disconnect();
});

test("delete, hide, order and settings", async () => {
  const t2 = await connect({ installedApps: [catalogFile("ROUND TIMER"), catalogFile("REACTION")] });
  const apps = t2.apps;
  const list = await apps.list();
  const ids = list.map((a) => a.id);

  // Order: 5 apps = 2 parts
  await apps.setOrder([ids[4], ids[3], ids[0], ids[1], ids[2]]);
  same(t2.device.appNames(), ["REACTION", "ROUND TIMER", "KILLS", "KILLS + DEAD", "STATS"]);
  await rejects(apps.setOrder([ids[0], ids[1]]), { kind: "status", status: 2 });

  // Hide: not the last shown app
  for (const id of ids.slice(0, 4)) await apps.setHidden(id, true);
  await rejects(apps.setHidden(ids[4], true), { kind: "status", status: 13 });
  await apps.setHidden(ids[0], false);
  assert.equal(t2.device.appHidden("KILLS"), false);

  // Settings of ROUND TIMER: "ROUND TIME" default 1, "SOUND" default 0
  assert.equal(await apps.getSetting(0xcc78e229, 0), 1);
  assert.equal(await apps.setSetting(0xcc78e229, 0, 4), 4);
  assert.equal(await apps.getSetting(0xcc78e229, 0), 4);
  await rejects(apps.setSetting(0xcc78e229, 1, 2), { kind: "status", status: 2 });
  await rejects(apps.getSetting(0xcc78e229, 2), { kind: "status", status: 2 });

  // Delete: not a built-in app
  await rejects(apps.delete(1), { kind: "status", status: 12 });
  await apps.delete(0xcc78e229);
  same(t2.device.appNames(), ["REACTION", "KILLS", "KILLS + DEAD", "STATS"]);
  await rejects(apps.delete(0xcc78e229), { kind: "status", status: 2 });
  noUpdateCommands(t2.device);
  t2.ota.disconnect();
});

test("while the device asks INSTALL, no command changes the apps (status 11)", async () => {
  const t = await connect({ appQuestionMs: 5000 });
  t.page.StatTrakApps.STATUS_POLL_MS = 300; // no STATUS command while the other page sends
  let asks;
  const question = new Promise((resolve) => { asks = resolve; });
  const done = t.apps.install(catalogFile("REACTION"), null, () => asks());
  await question;
  assert.equal(t.device.syncState, 2);
  // A second connection plays an other page
  const other = new t.page.StatTrakApps(t.ota);
  await rejects(other.delete(0x62e49bbb), { kind: "waiting" });
  await rejects(other.setHidden(1, true), { kind: "waiting" });
  await rejects(other.setDeviceName("RED"), { kind: "waiting" });
  same(await other.deviceName(), { custom: false, part: "", name: "StatTrak" });
  t.device.holdButton2();
  assert.equal(await done, "installed");
  t.ota.disconnect();
});

test("each command is checked: an answer with an other command number is not used", async () => {
  const t = await connect({});
  const write = t.device._userCommand.bind(t.device);
  t.device._userCommand = (cmdId, d) => write(cmdId === 0x0110 ? 0x0111 : cmdId, d);
  await rejects(t.apps.info(), { kind: "bad answer" });
  t.ota.disconnect();
});

test("the .stapp check: the catalog files are good, damaged and other files are refused", () => {
  const { StappFile, FakeStatTrak } = loadPageCode();
  for (const app of CATALOG.apps) {
    const bytes = new Uint8Array(fs.readFileSync(path.join(ROOT, "apps", app.file)));
    const info = StappFile.inspect(bytes);
    assert.equal(info.ok, true, app.file);
    assert.equal(info.idHex, app.id);
    assert.equal(info.name, app.name);
    assert.equal(info.size, app.size);
    assert.equal(info.format, bytes[4]);
    same(info.permissions, app.permissions);
    assert.equal(Buffer.from(info.picture).toString("base64"), app.picture);
    same(JSON.parse(JSON.stringify(info.settings)), app.settings);
    assert.equal(StappFile.crc32(bytes), FakeStatTrak.crc32(bytes));
  }
  const good = FakeStatTrak.makeApp({ name: "GOOD" });
  assert.equal(StappFile.inspect(good).ok, true);
  assert.equal(StappFile.inspect(good).format, 1);
  const damaged = good.slice();
  damaged[100] ^= 1;
  assert.equal(StappFile.inspect(damaged).problem, "damaged");
  assert.equal(StappFile.inspect(new TextEncoder().encode("PK\x03\x04 a zip file".padEnd(400, "x"))).problem, "not an app");
  assert.equal(StappFile.inspect(new Uint8Array(10)).problem, "not an app");
  const large = FakeStatTrak.makeApp({ size: 5000 });
  assert.equal(StappFile.inspect(large).problem, "too large");

  // Format 2: 10 handlers, the smallest package is 312 bytes
  const two = FakeStatTrak.makeApp({ name: "TWO", format: 2 });
  assert.equal(StappFile.inspect(two).ok, true);
  assert.equal(StappFile.inspect(two).format, 2);
  const withFormat = (bytes, format) => {
    const b = bytes.slice();
    b[4] = format;
    new DataView(b.buffer).setUint32(b.length - 4, FakeStatTrak.crc32(b.subarray(0, b.length - 4)), true);
    return b;
  };
  const tiny = FakeStatTrak.makeApp({ name: "TINY", size: 310 });
  assert.equal(StappFile.inspect(tiny).ok, true, "310 bytes is a good format 1 size");
  assert.equal(StappFile.inspect(withFormat(tiny, 2)).problem, "damaged", "310 bytes is too small for format 2");
  assert.equal(StappFile.inspect(withFormat(good, 4)).problem, "newer format");
  assert.equal(StappFile.inspect(withFormat(good, 0)).problem, "damaged");
});

test("the app code sends no firmware update command", () => {
  const source = fs.readFileSync(path.join(ROOT, "js", "apps-protocol.js"), "utf8") +
    fs.readFileSync(path.join(ROOT, "js", "apps.js"), "utf8");
  for (const name of ["sendCmd(", ".update(", "acceptFirmware", "sendFwData", "sendSector", "CMD_ID__START", "CUSTOM_ID__ACCEPT_FW", "CUSTOM_ID__REJECT_FW", "CUSTOM_ID__REBOOT"]) {
    assert.ok(!source.includes(name), "the app code uses " + name);
  }
});

test("the Bluetooth name: read, write, back to StatTrak, the rules; the page refuses a bad name before it sends", async () => {
  const t = await connect({});
  same(await t.apps.deviceName(), { custom: false, part: "", name: "StatTrak" });
  same(await t.apps.setDeviceName("Red 7"), { custom: true, part: "Red 7", name: "StatTrak | Red 7" });
  same(await t.apps.deviceName(), { custom: true, part: "Red 7", name: "StatTrak | Red 7" });
  assert.equal(t.device.deviceName, "Red 7");
  same(await t.apps.setDeviceName(""), { custom: false, part: "", name: "StatTrak" });
  assert.equal(t.device.deviceName, "");
  const sent = t.device.appCommands.length;
  for (const bad of ["", " AB", "AB ", "THIRTEEN CHAR", "Rīga", "A\tB"].slice(1)) {
    await rejects(t.apps.setDeviceName(bad), { kind: "name" });
  }
  assert.equal(t.device.appCommands.length, sent, "a bad name was sent");
  const { StatTrakApps } = t.page;
  assert.equal(StatTrakApps.nameProblem("ABCDEFGHIJKL"), "");
  assert.equal(StatTrakApps.nameProblem("~!@#$%^&"), "");
  assert.match(StatTrakApps.nameProblem("ABCDEFGHIJKLM"), /At most 12/);
  assert.match(StatTrakApps.nameProblem(" A"), /No space/);
  t.ota.disconnect();
});

test("pair(): a renamed device (StatTrak | <part>) is found by the name start; the firmware tab looks for StatTrak only", async () => {
  // The name is used from the next start of "Apps: SYNC"
  const page = loadPageCode({ apps: true, deviceName: "RED 7" });
  page.device.screen = "counter";
  page.device.openAppSync();
  assert.equal(page.device.bleName(), "StatTrak | RED 7");
  const ota = await page.StatTrakApps.pair();
  await once(ota, "bleConnected");
  same(page.device.requests[0], {
    filters: [{ namePrefix: "StatTrak" }, { services: ["03b80e5a-ede8-4b33-a751-6ce34ec4c700"] }],
    optionalServices: [0x8018],
  });
  assert.equal(page.StatTrakApps.ADVERTISED_UUID, page.FakeStatTrak.ADVERTISED_UUID);
  assert.equal(ota.device.name, "StatTrak | RED 7");
  ota.disconnect();

  // The firmware tab: the exact name "StatTrak" only; it does not find "StatTrak | RED 7" (the update
  // view is "StatTrak")
  await rejects(page.BleOta.create(), { name: "NotFoundError" });
  same(page.device.requests[1], { filters: [{ name: "StatTrak" }], optionalServices: [0x8018] });
});

test("pair(): a browser that refuses the UUID filter gets the name filter", async () => {
  let page = loadPageCode({ apps: true, uuidFilterBlocked: true });
  page.device.screen = "counter";
  page.device.openAppSync();
  const ota = await page.StatTrakApps.pair();
  await once(ota, "bleConnected");
  same(page.device.requests.map((r) => r.filters.length), [2, 1]);
  ota.disconnect();

  // A renamed device is found too: its name starts with "StatTrak"
  page = loadPageCode({ apps: true, uuidFilterBlocked: true, deviceName: "RED 7" });
  page.device.screen = "counter";
  page.device.openAppSync();
  const renamed = await page.StatTrakApps.pair();
  await once(renamed, "bleConnected");
  assert.equal(renamed.device.name, "StatTrak | RED 7");
  renamed.disconnect();
});

/* ---- docs/APPS.md section 6, point by point, with the raw commands ---------------------- */

/* One command as bytes: { status, data (13 bytes) } */
async function raw(t, cmd, bytes) {
  const payload = new Uint8Array(16);
  payload.set(bytes || []);
  const answer = await t.ota.sendCustomCmd(cmd, payload);
  return { status: answer[0], data: answer.slice(1, 14) };
}
const u32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];
const u16 = (v) => [v & 0xFF, (v >> 8) & 0xFF];
const text = (s) => Array.from(s, (c) => c.charCodeAt(0));
const answer16 = (a) => a.data[0] | (a.data[1] << 8);
const BEGIN = 0x0113, DATA = 0x0114, END = 0x0115, STATUS = 0x0116, ASK = 0x011B, DEVICE_NAME = 0x011C;
const T = 0xcc78e229;   // ROUND TIMER

function begin(bytes, id, crc) {
  const { FakeStatTrak } = loadPageCode();
  return [...u32(id === undefined ? (bytes[8] | (bytes[9] << 8) | (bytes[10] << 16) | (bytes[11] << 24)) >>> 0 : id),
    ...u16(bytes.length), ...u32(crc === undefined ? FakeStatTrak.crc32(bytes) : crc)];
}
const state = async (t) => (await raw(t, STATUS)).data[0];
/* BEGIN, ASK (the name field of the package), and "yes" on the device */
async function beginAskYes(t, bytes, beginData) {
  assert.equal((await raw(t, BEGIN, beginData || begin(bytes))).status, 0, "BEGIN");
  assert.equal((await raw(t, ASK, bytes.subarray(16, 32))).status, 0, "ASK");
  t.device.holdButton2();
  assert.equal(await state(t), 1, "receiving");
}
async function sendAll(t, bytes) {
  for (let offset = 0; offset < bytes.length; offset += 14) {
    const a = await raw(t, DATA, [...u16(offset), ...bytes.subarray(offset, offset + 14)]);
    assert.equal(a.status, 0);
    assert.equal(answer16(a), Math.min(offset + 14, bytes.length), "DATA answers the next expected offset");
  }
}

test("section 6.1: BEGIN then ASK shows the question; while it shows, each change answers 11", async () => {
  const t = await connect({ appQuestionMs: 5000, installedApps: [catalogFile("ROUND TIMER")] });
  const bytes = catalogFile("REACTION");
  assert.equal((await raw(t, BEGIN, begin(bytes))).status, 0);
  assert.equal(await state(t), 0, "BEGIN alone does not ask");
  assert.equal((await raw(t, ASK, bytes.subarray(16, 32))).status, 0);
  assert.equal(t.device.display, "INSTALL REACTION?");
  assert.equal(await state(t), 2);
  const changes = [[BEGIN, begin(bytes)], [ASK, bytes.subarray(16, 32)], [DATA, [0, 0]], [END, []], [0x0117, u32(T)],
    [0x0118, [4, ...u32(1), ...u32(2), ...u32(3), 0]], [0x0119, [...u32(1), 1]], [0x011A, [...u32(T), 0, 1]],
    [DEVICE_NAME, [1, ...text("RED")]], [DEVICE_NAME, [2]]];
  for (const [cmd, data] of changes) assert.equal((await raw(t, cmd, data)).status, 11, "0x" + cmd.toString(16));
  const reads = [[0x0110, []], [0x0111, [0]], [0x0112, [...u32(1), 0]], [STATUS, []], [0x011A, [...u32(T), 0, 0xFF]], [DEVICE_NAME, [0]]];
  for (const [cmd, data] of reads) assert.equal((await raw(t, cmd, data)).status, 0, "0x" + cmd.toString(16));
  assert.equal(t.device.deviceName, "");
  t.device.holdButton2();
  assert.equal(await state(t), 1);
  await sendAll(t, bytes);
  assert.equal((await raw(t, END)).status, 0);
  assert.equal(await state(t), 3);
  assert.equal(t.device.display, "APP INSTALLED");
  t.ota.disconnect();
});

test("section 6.1: DATA only after 'yes'; ASK only after BEGIN and with a correct name field", async () => {
  const t = await connect({ appQuestionMs: 5000 });
  const bytes = catalogFile("REACTION");
  const field = (name) => { const f = new Uint8Array(16); f.set(text(name)); return f; };
  // ASK without BEGIN
  assert.equal((await raw(t, ASK, field("REACTION"))).status, 2, "ASK without BEGIN");
  // DATA and END after BEGIN, before the question
  assert.equal((await raw(t, BEGIN, begin(bytes))).status, 0);
  let a = await raw(t, DATA, [...u16(0), ...bytes.subarray(0, 14)]);
  assert.deepEqual([a.status, answer16(a)], [2, 0], "DATA before ASK");
  assert.equal((await raw(t, END)).status, 2, "END before ASK");
  // Bad name fields: lower case, empty, 15 characters, a byte after the 0
  const bad = [field("reaction"), field(""), field("ABCDEFGHIJKLMNO"), Uint8Array.from([65, 66, 0, 67, ...new Array(12).fill(0)]), field("A_B")];
  for (const name of bad) assert.equal((await raw(t, ASK, name)).status, 2, "ASK " + JSON.stringify(Array.from(name)));
  assert.equal(await state(t), 0);
  // The question shows: DATA is 11 (the person did not answer)
  assert.equal((await raw(t, ASK, field("REACTION"))).status, 0);
  assert.equal((await raw(t, DATA, [...u16(0), ...bytes.subarray(0, 14)])).status, 11, "DATA while the question shows");
  // A hold that started before the question does not answer it
  t.device.holdButton1();   // "no"
  assert.equal(await state(t), 4);
  assert.equal((await raw(t, DATA, [...u16(0), ...bytes.subarray(0, 14)])).status, 2, "DATA after no");
  assert.equal((await raw(t, BEGIN, begin(bytes))).status, 0);
  t.device.buttonDown(2);
  assert.equal((await raw(t, ASK, field("REACTION"))).status, 0);
  t.device.longPress(2);
  t.device.buttonUp(2);
  assert.equal(await state(t), 2, "the hold started before the question: no answer");
  t.device.holdButton2();
  assert.equal(await state(t), 1);
  // ASK while it receives: 2
  assert.equal((await raw(t, ASK, field("REACTION"))).status, 2, "ASK while receiving");
  await sendAll(t, bytes);
  assert.equal((await raw(t, END)).status, 0);
  t.ota.disconnect();
});

test("section 6.1: END refuses a package with an other name or id than BEGIN and ASK (status 10)", async () => {
  const t = await connect({});
  const reaction = catalogFile("REACTION");
  const timer = catalogFile("ROUND TIMER");
  // The person said yes to "ROUND TIMER", the data is REACTION (with its own id and CRC in BEGIN)
  assert.equal((await raw(t, BEGIN, begin(reaction))).status, 0);
  assert.equal((await raw(t, ASK, timer.subarray(16, 32))).status, 0);
  assert.equal(t.device.display, "INSTALL ROUND TIMER?");
  t.device.holdButton2();
  await sendAll(t, reaction);
  let a = await raw(t, END);
  assert.deepEqual([a.status, a.data[0]], [10, 5], "bad header");
  assert.equal(t.device.display, "NOT INSTALLED");
  assert.equal(await state(t), 0);
  // An other id in BEGIN
  await beginAskYes(t, reaction, begin(reaction, 0x777));
  await sendAll(t, reaction);
  a = await raw(t, END);
  assert.deepEqual([a.status, a.data[0]], [10, 5]);
  assert.equal(t.device.installed.size, 0);
  assert.equal(t.device.appWrites, 0);
  t.ota.disconnect();
});

test("section 6.1: a new BEGIN stops an install that runs, also an earlier 'yes'", async () => {
  const t = await connect({});
  const bytes = catalogFile("REACTION");
  await beginAskYes(t, bytes);
  await sendAll(t, bytes.subarray(0, 28));
  assert.equal((await raw(t, BEGIN, begin(bytes))).status, 0);
  assert.equal(await state(t), 0);
  assert.equal((await raw(t, DATA, [...u16(0), ...bytes.subarray(0, 14)])).status, 2, "the yes does not count for the new BEGIN");
  assert.equal((await raw(t, ASK, bytes.subarray(16, 32))).status, 0);
  t.device.holdButton2();
  await sendAll(t, bytes);
  assert.equal((await raw(t, END)).status, 0);
  // After the install, a new BEGIN works without SYNC MORE
  const timer = catalogFile("ROUND TIMER");
  await beginAskYes(t, timer);
  await sendAll(t, timer);
  assert.equal((await raw(t, END)).status, 0);
  same(t.device.appNames(), ["KILLS", "KILLS + DEAD", "STATS", "REACTION", "ROUND TIMER"]);
  t.ota.disconnect();
});

test("section 6.1: the transfer stops after 10 s without DATA, or by a hold of button 1: STATUS 0, NOT INSTALLED", async () => {
  const t = await connect({ appDataMs: 150 });
  const bytes = catalogFile("REACTION");
  await beginAskYes(t, bytes);
  await sendAll(t, bytes.subarray(0, 42));
  await new Promise((resolve) => setTimeout(resolve, 250));
  assert.equal(await state(t), 0);
  assert.equal(t.device.display, "NOT INSTALLED");
  assert.equal((await raw(t, DATA, [...u16(42), ...bytes.subarray(42, 56)])).status, 2);
  // Hold button 1 while it receives
  await beginAskYes(t, bytes);
  t.device.holdButton1();
  assert.equal(await state(t), 0);
  assert.equal(t.device.screen, "appsync", "the view stays open");
  assert.equal(t.device.display, "NOT INSTALLED");
  assert.equal((await raw(t, END)).status, 2);
  t.ota.disconnect();
});

test("section 6.1: after the install: SYNC MORE (hold 1) waits for the next app; OPEN (hold 2) opens it, Bluetooth goes off", async () => {
  const t = await connect({});
  const bytes = catalogFile("REACTION");
  await beginAskYes(t, bytes);
  await sendAll(t, bytes);
  assert.equal((await raw(t, END)).status, 0);
  // A short press does nothing: the buttons on the display answer a hold
  t.device.press(1);
  t.device.press(2);
  assert.equal(await state(t), 3);
  assert.equal(t.device.screen, "appsync");
  t.device.holdButton1();
  assert.equal(await state(t), 0);
  assert.equal(t.device.display, "Waiting...");
  const timer = catalogFile("ROUND TIMER");
  await beginAskYes(t, timer);
  await sendAll(t, timer);
  assert.equal((await raw(t, END)).status, 0);
  const lost = once(t.ota, "bleDisconnected");
  t.device.holdButton2();
  await lost;
  assert.equal(t.device.screen, "app");
  assert.equal(t.device.openedApp, T);
  assert.equal(t.device.gatt.connected, false);
  t.ota.disconnect();
});

test("section 6.2: DATA answers the expected offset; a wrong offset is 8; END with missing data is 8", async () => {
  const t = await connect({});
  const bytes = catalogFile("REACTION");
  await beginAskYes(t, bytes);
  let a = await raw(t, DATA, [...u16(0), ...bytes.subarray(0, 14)]);
  assert.deepEqual([a.status, answer16(a)], [0, 14]);
  a = await raw(t, DATA, [...u16(42), ...bytes.subarray(42, 56)]);
  assert.deepEqual([a.status, answer16(a)], [8, 14]);
  a = await raw(t, END);
  assert.deepEqual([a.status, answer16(a)], [8, 14]);
  t.ota.disconnect();
});

test("section 6.3: BEGIN: CRC of the whole file, ids 0..255 and 0xFFFFFFFF, size under 308, replace", async () => {
  const { FakeStatTrak } = loadPageCode();
  const reaction = catalogFile("REACTION");
  const t = await connect({ installedApps: [catalogFile("ROUND TIMER"), reaction], appErrors: { [T]: 3 }, hiddenApps: [T] });
  for (const id of [0, 3, 255, 0xFFFFFFFF]) assert.equal((await raw(t, BEGIN, begin(reaction, id))).status, 2, "id " + id);
  assert.equal((await raw(t, BEGIN, [...u32(0x777), ...u16(307), 0, 0, 0, 0])).status, 2, "size 307");
  assert.equal((await raw(t, BEGIN, [...u32(0x777), ...u16(308), 0, 0, 0, 0])).status, 0, "size 308");

  // A CRC in BEGIN that is not the CRC of the file: END answers 9
  await beginAskYes(t, reaction, begin(reaction, undefined, 1234));
  await sendAll(t, reaction);
  assert.equal((await raw(t, END)).status, 9);
  assert.equal(t.device.display, "NOT INSTALLED");

  // ROUND TIMER was switched off by errors. A new version keeps its place and is shown again.
  const before = t.device.appNames();
  assert.equal(t.device.appHidden("ROUND TIMER"), true);
  assert.equal((await t.apps.list())[3].error, true);
  const newTimer = FakeStatTrak.makeApp({ name: "ROUND TIMER", id: T, version: 2 });
  assert.equal((await install(t, newTimer, "holdButton2")).result, "installed");
  same(t.device.appNames(), before);
  const after = (await t.apps.list())[3];
  assert.deepEqual([after.name, after.version, after.error, after.hidden], ["ROUND TIMER", 2, false, false]);
  t.ota.disconnect();
});

test("section 6.4: SET_ORDER: parts of 16 bytes in order, ids after the end 0, each app one time", async () => {
  const t = await connect({ installedApps: [catalogFile("ROUND TIMER")] });
  const order = (count, ids, part) => {
    const d = [count];
    for (let i = 0; i < 3; i++) d.push(...u32(ids[i] || 0));
    d.push(part, 0, 0);
    assert.equal(d.length, 16);
    return d;
  };
  // 4 apps = 2 parts: [T, 3, 2], [1, 0, 0]
  assert.equal((await raw(t, 0x0118, order(4, [T, 3, 2], 0))).status, 0);
  assert.equal((await raw(t, 0x0118, order(4, [1], 1))).status, 0);
  same(t.device.appNames(), ["ROUND TIMER", "STATS", "KILLS + DEAD", "KILLS"]);
  // A part out of order, an id two times, a missing app, an unknown id: 2, and no change
  assert.equal((await raw(t, 0x0118, order(4, [1], 1))).status, 2, "part 1 first");
  assert.equal((await raw(t, 0x0118, order(4, [1, 2, 3], 0))).status, 0);
  assert.equal((await raw(t, 0x0118, order(4, [1], 1))).status, 2, "KILLS two times");
  assert.equal((await raw(t, 0x0118, order(3, [1, 2, 3], 0))).status, 2, "ROUND TIMER is missing");
  assert.equal((await raw(t, 0x0118, order(4, [1, 2, 3], 0))).status, 0);
  assert.equal((await raw(t, 0x0118, order(4, [0x999], 1))).status, 2, "unknown id");
  same(t.device.appNames(), ["ROUND TIMER", "STATS", "KILLS + DEAD", "KILLS"]);
  // The page sends the same layout
  await t.apps.setOrder([1, 2, 3, T]);
  same(t.device.appNames(), ["KILLS", "KILLS + DEAD", "STATS", "ROUND TIMER"]);
  t.ota.disconnect();
});

test("section 6.5: unknown ids, values out of range and bad bytes answer 2; DELETE of a built-in app 12", async () => {
  const t = await connect({ installedApps: [catalogFile("ROUND TIMER")] });
  const cases = [
    ["NAME unknown id", 0x0112, [...u32(0x999), 0], 2],
    ["SETTING unknown id", 0x011A, [...u32(0x999), 0, 0xFF], 2],
    ["SETTING of a built-in app", 0x011A, [...u32(1), 0, 0xFF], 2],
    ["SETTING number 2", 0x011A, [...u32(T), 2, 0xFF], 2],
    ["SETTING value 2 of SOUND", 0x011A, [...u32(T), 1, 2], 2],
    ["SET_HIDDEN byte 2", 0x0119, [...u32(T), 2], 2],
    ["SET_HIDDEN unknown id", 0x0119, [...u32(0x999), 1], 2],
    ["DELETE unknown id", 0x0117, u32(0x999), 2],
    ["DELETE KILLS", 0x0117, u32(1), 12],
    ["LIST index 4", 0x0111, [4], 4],
    ["a command that does not exist", 0x011D, [], 1],
  ];
  for (const [what, cmd, data, status] of cases) assert.equal((await raw(t, cmd, data)).status, status, what);
  same(t.device.appNames(), ["KILLS", "KILLS + DEAD", "STATS", "ROUND TIMER"]);
  assert.equal(t.device.appWrites, 0);
  t.ota.disconnect();
});

test("section 6.6: 3 errors switch an app off, also the last shown one; shown again = no errors", async () => {
  const t = await connect({ installedApps: [catalogFile("ROUND TIMER")], hiddenApps: [1, 2, 3] });
  t.device.appError(T);
  t.device.appError(T);
  let app = (await t.apps.list())[3];
  assert.deepEqual([app.hidden, app.error], [false, false]);
  t.device.appError(T);
  app = (await t.apps.list())[3];
  assert.deepEqual([app.hidden, app.error, app.flags & 0x08], [true, true, 0x08]);
  assert.equal(t.device._shownCount(), 0, "the last shown app is hidden too");
  await t.apps.setHidden(T, false);
  app = (await t.apps.list())[3];
  assert.deepEqual([app.hidden, app.error], [false, false]);
  assert.equal(t.device.installed.get(T).errors, 0);
  // SET_HIDDEN 0 of an app that is shown also sets the error count back to 0
  t.device.appError(T);
  await t.apps.setHidden(T, false);
  assert.equal(t.device.installed.get(T).errors, 0);
  t.ota.disconnect();
});

test("section 6.7: no answer to the question in 30 s: STATUS 5, the display shows REFUSED", async () => {
  const { device: plain } = loadPageCode({ apps: true });
  assert.equal(plain.options.appQuestionMs, 30000);
  assert.equal(plain.options.appDataMs, 10000);
  const t = await connect({ appQuestionMs: 100 });
  assert.equal((await install(t, catalogFile("REACTION"), null)).result, "timeout");
  assert.equal((await raw(t, STATUS)).data[0], 5);
  assert.equal(t.device.display, "REFUSED");
  same(t.device.appNames(), ["KILLS", "KILLS + DEAD", "STATS"]);
  t.ota.disconnect();
});

test("section 6.8: DEVICE_NAME: read, write, the rules of a name, back to StatTrak, used from the next Apps: SYNC", async () => {
  const t = await connect({});
  const name = (s, extra) => { const d = new Uint8Array(16); d[0] = 1; d.set(text(s), 1); for (const [i, v] of extra || []) d[i] = v; return d; };
  const read = async () => { const a = await raw(t, DEVICE_NAME, [0]); return [a.status, a.data[0], String.fromCharCode(...a.data.subarray(1, 13)).replace(/\0+$/, "")]; };
  assert.deepEqual(await read(), [0, 0, ""]);
  let a = await raw(t, DEVICE_NAME, name("RED 7"));
  assert.deepEqual([a.status, a.data[0], String.fromCharCode(...a.data.subarray(1, 6)), a.data[6]], [0, 1, "RED 7", 0]);
  assert.deepEqual(await read(), [0, 1, "RED 7"]);
  // Bad: a space at the start or the end, a byte out of 0x20 .. 0x7E, bytes after the name, op 2
  const bad = [
    ["space at the start", name(" RED")], ["space at the end", name("RED ")], ["0x7F", name("RE\x7F")],
    ["0x1F", name("RE\x1F")], ["0x80", name("RE", [[3, 0x80]])], ["a byte after the 0", name("RE", [[4, 65]])],
    ["a byte after the 12 characters", name("ABCDEFGHIJKL", [[13, 65]])], ["a byte at the end", name("AB", [[15, 1]])],
    ["op 2", Uint8Array.from([2, ...text("RED")])],
  ];
  for (const [what, data] of bad) assert.equal((await raw(t, DEVICE_NAME, data)).status, 2, what);
  assert.deepEqual(await read(), [0, 1, "RED 7"]);
  // 12 characters, all signs
  assert.equal((await raw(t, DEVICE_NAME, name("~a Z!0}"))).status, 0);
  assert.equal((await raw(t, DEVICE_NAME, name("ABCDEFGHIJKL"))).status, 0);
  assert.deepEqual(await read(), [0, 1, "ABCDEFGHIJKL"]);
  // This start of "Apps: SYNC" keeps its name; the next start uses the new one
  assert.equal(t.device.bleName(), "StatTrak");
  t.ota.disconnect();
  t.device.leaveAppSync();
  t.device.openAppSync();
  assert.equal(t.device.bleName(), "StatTrak | ABCDEFGHIJKL");
  // The update view is always "StatTrak"
  t.device.screen = "counter";
  t.device.enterUpdateMode();
  assert.equal(t.device.bleName(), "StatTrak");
  // All 0 = back to "StatTrak"
  t.device.screen = "counter";
  t.device.openAppSync();
  const ota = await t.page.StatTrakApps.pair();
  await once(ota, "bleConnected");
  t.ota = ota;
  assert.deepEqual([(await raw(t, DEVICE_NAME, [1])).status, ...(await read()).slice(1)], [0, 0, ""]);
  assert.equal(t.device.deviceName, "");
  ota.disconnect();
});

/* ---- The device pictures of the page (js/apps-pictures.js) ------------------------------ */

test("the device screens of the page are the screens of the firmware, pixel by pixel", () => {
  const context = vm.createContext({ atob });
  vm.runInContext(fs.readFileSync(path.join(ROOT, "js", "apps-pictures.js"), "utf8") + "\nglobalThis.AppPictures = AppPictures;", context);
  const AppPictures = context.AppPictures;
  // tests/sync-screens.json: frames of the firmware (the headless simulator)
  const fixture = JSON.parse(fs.readFileSync(path.join(__dirname, "sync-screens.json"), "utf8"));
  assert.ok(fixture.screens.length >= 10);
  const kinds = new Set();
  for (const screen of fixture.screens) {
    const want = screen.rows.map((row) => [...row].map((h) => parseInt(h, 16).toString(2).padStart(4, "0")).join("").replace(/1/g, "#").replace(/0/g, "."));
    const got = AppPictures.rows(AppPictures.syncScreen(screen.kind, screen.arg, screen.fill));
    same(got, want, `${screen.kind} ${screen.arg || ""} ${JSON.stringify(screen.fill || {})}`);
    kinds.add(screen.kind);
  }
  same([...kinds].sort(), ["idle", "installed", "notinstalled", "question", "receiving", "refused"]);
});
