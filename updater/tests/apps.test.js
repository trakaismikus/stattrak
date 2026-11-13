/* Tests of js/apps-protocol.js (the app commands and the .stapp check) in Node.js, with the
 * stand-in device. Run: node --test "tests/*.test.js"
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { ROOT, loadPageCode, pair } = require("./helpers.js");

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
  same({ ...info }, { engine: 1, format: 1, free: 24576 - timer.length, maxPackage: 4096, count: 1, maxApps: 8 });

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

test("an install: progress, the question on the device, 'yes', and the app is in the list", async () => {
  const t = await connect({});
  const bytes = catalogFile("REACTION");
  const { result, steps } = await install(t, bytes, "holdButton2");
  assert.equal(result, "installed");
  assert.equal(steps[0], 0);
  assert.ok(steps.indexOf(1) > 0 && steps.indexOf(1) < steps.indexOf("waiting"), "the transfer is complete before the question");
  same(t.device.events.filter((e) => /question|app/.test(e)), ["question REACTION", "app installed REACTION"]);
  same(Array.from(t.device.installed.get(0x62e49bbb).pkg), Array.from(bytes));
  same(t.device.appNames(), ["KILLS", "KILLS + DEAD", "STATS", "REACTION"]);
  assert.equal((await t.apps.info()).free, 24576 - bytes.length);
  noUpdateCommands(t.device);
  t.ota.disconnect();
});

test("the person says 'no', or does not answer in 30 s: the app is not installed", async () => {
  const t = await connect({});
  const bytes = catalogFile("REACTION");
  assert.equal((await install(t, bytes, "holdButton1")).result, "refused");
  assert.equal((await install(t, bytes, null)).result, "timeout");
  same(t.device.appNames(), ["KILLS", "KILLS + DEAD", "STATS"]);
  assert.equal(t.device.appWrites, 0);
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

test("a package that the device check refuses: status 10 and the reason", async () => {
  const t = await connect({ refusePackage: 10 });
  await rejects(t.apps.install(catalogFile("REACTION")), { kind: "status", status: 10, reason: "bad code" });
  assert.equal(t.device.syncState, 0);
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
    same(info.permissions, app.permissions);
    assert.equal(Buffer.from(info.picture).toString("base64"), app.picture);
    same(JSON.parse(JSON.stringify(info.settings)), app.settings);
    assert.equal(StappFile.crc32(bytes), FakeStatTrak.crc32(bytes));
  }
  const good = FakeStatTrak.makeApp({ name: "GOOD" });
  assert.equal(StappFile.inspect(good).ok, true);
  const damaged = good.slice();
  damaged[100] ^= 1;
  assert.equal(StappFile.inspect(damaged).problem, "damaged");
  assert.equal(StappFile.inspect(new TextEncoder().encode("PK\x03\x04 a zip file".padEnd(400, "x"))).problem, "not an app");
  assert.equal(StappFile.inspect(new Uint8Array(10)).problem, "not an app");
  const large = FakeStatTrak.makeApp({ size: 5000 });
  assert.equal(StappFile.inspect(large).problem, "too large");
  const newer = good.slice();
  newer[4] = 2;
  assert.equal(StappFile.inspect(newer).problem, "newer format");
});

test("the app code sends no firmware update command", () => {
  const source = fs.readFileSync(path.join(ROOT, "js", "apps-protocol.js"), "utf8") +
    fs.readFileSync(path.join(ROOT, "js", "apps.js"), "utf8");
  for (const name of ["sendCmd(", ".update(", "acceptFirmware", "sendFwData", "sendSector", "CMD_ID__START", "CUSTOM_ID__ACCEPT_FW", "CUSTOM_ID__REJECT_FW", "CUSTOM_ID__REBOOT"]) {
    assert.ok(!source.includes(name), "the app code uses " + name);
  }
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
const answer16 = (a) => a.data[0] | (a.data[1] << 8);
const BEGIN = 0x0113, DATA = 0x0114, END = 0x0115;
const T = 0xcc78e229;   // ROUND TIMER

function begin(bytes, id, crc) {
  const { FakeStatTrak } = loadPageCode();
  return [...u32(id), ...u16(bytes.length), ...u32(crc === undefined ? FakeStatTrak.crc32(bytes) : crc)];
}
async function sendAll(t, bytes) {
  for (let offset = 0; offset < bytes.length; offset += 14) {
    const a = await raw(t, DATA, [...u16(offset), ...bytes.subarray(offset, offset + 14)]);
    assert.equal(a.status, 0);
    assert.equal(answer16(a), Math.min(offset + 14, bytes.length), "DATA answers the next expected offset");
  }
}

test("section 6.1: END answers 0 and the device asks; while it asks, each change answers 11", async () => {
  const t = await connect({ appQuestionMs: 5000, installedApps: [catalogFile("ROUND TIMER")] });
  const bytes = catalogFile("REACTION");
  assert.equal((await raw(t, BEGIN, begin(bytes, 0x62e49bbb))).status, 0);
  await sendAll(t, bytes);
  assert.equal((await raw(t, END)).status, 0);
  assert.equal(t.device.display, "INSTALL REACTION?");
  const changes = [[BEGIN, begin(bytes, 0x62e49bbb)], [DATA, [0, 0]], [END, []], [0x0117, u32(T)],
    [0x0118, [4, ...u32(1), ...u32(2), ...u32(3), 0]], [0x0119, [...u32(1), 1]], [0x011A, [...u32(T), 0, 1]]];
  for (const [cmd, data] of changes) assert.equal((await raw(t, cmd, data)).status, 11, "0x" + cmd.toString(16));
  const reads = [[0x0110, []], [0x0111, [0]], [0x0112, [...u32(1), 0]], [0x0116, []], [0x011A, [...u32(T), 0, 0xFF]]];
  for (const [cmd, data] of reads) assert.equal((await raw(t, cmd, data)).status, 0, "0x" + cmd.toString(16));
  t.device.holdButton2();
  assert.equal((await raw(t, 0x0116)).data[0], 3);
  t.ota.disconnect();
});

test("section 6.2: DATA answers the expected offset; a wrong offset is 8; END with missing data is 8", async () => {
  const t = await connect({});
  const bytes = catalogFile("REACTION");
  assert.equal((await raw(t, BEGIN, begin(bytes, 0x62e49bbb))).status, 0);
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
  assert.equal((await raw(t, BEGIN, [...u32(0x777), ...u16(300), 0, 0, 0, 0])).status, 2, "size 300");
  assert.equal((await raw(t, BEGIN, [...u32(0x777), ...u16(308), 0, 0, 0, 0])).status, 0, "size 308");

  // A CRC in BEGIN that is not the CRC of the file: END answers 9
  assert.equal((await raw(t, BEGIN, begin(reaction, 0x62e49bbb, 1234))).status, 0);
  await sendAll(t, reaction);
  assert.equal((await raw(t, END)).status, 9);

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
    ["a command that does not exist", 0x011B, [], 1],
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
  const t = await connect({ appQuestionMs: 100 });
  assert.equal((await install(t, catalogFile("REACTION"), null)).result, "timeout");
  assert.equal((await raw(t, 0x0116)).data[0], 5);
  assert.equal(t.device.display, "REFUSED");
  same(t.device.appNames(), ["KILLS", "KILLS + DEAD", "STATS"]);
  t.ota.disconnect();
});
