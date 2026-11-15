/* Tests of js/data-protocol.js (docs/DATA.md: backup, restore, app data, device log, start
 * picture, preview frame) in Node.js, with the stand-in device.
 * Run: node --test "tests/*.test.js"
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const { loadPageCode, once } = require("./helpers.js");

const RUNNER = 0xAABBCCDD, TIMER = 0x11223344;

/* A device in Settings > "Apps: SYNC" with the commands of docs/DATA.md, and a connection */
async function connect(deviceOptions) {
  const page = loadPageCode(Object.assign({ data: true, appQuestionMs: 400 }, deviceOptions || {}));
  page.StatTrakApps.STATUS_POLL_MS = 5;
  page.StatTrakApps.DRAIN_MS = 250;
  page.StatTrakApps.QUESTION_WAIT_MS = 2000;
  page.device.screen = "counter";
  page.device.openAppSync();
  // Also a device with an other Bluetooth name (DEVICE_NAME), as the Apps tab
  const ota = await page.StatTrakApps.pair();
  await once(ota, "bleConnected");
  const apps = new page.StatTrakApps(ota);
  return { page, device: page.device, ota, apps, data: new page.StatTrakData(apps) };
}

const makeApps = (FakeStatTrak) => [
  FakeStatTrak.makeApp({ name: "RUNNER", id: RUNNER, version: 3, saved: 3, settings: [{ name: "LEVEL", values: ["EASY", "HARD"] }, { name: "SOUND", values: ["OFF", "ON", "LOUD"] }] }),
  FakeStatTrak.makeApp({ name: "TIMER", id: TIMER, saved: 1 }),
];
/* The stand-in device needs the packages before it is made: make them with the class of a first load */
const { FakeStatTrak } = loadPageCode();
const APPS = makeApps(FakeStatTrak);

const plain = (value) => JSON.parse(JSON.stringify(value));
const same = (actual, expected, message) => assert.deepEqual(plain(actual), plain(expected), message);
const noUpdateCommands = (device) => {
  assert.equal(device.startCount, 0, "a firmware update was started");
  assert.equal(device.acceptCount, 0, "a firmware was confirmed");
};

/* A frame with some pixels on: a box at the left and a line at the bottom */
function picture(FrameBuffer) {
  const pixels = new Uint8Array(128 * 32);
  for (let y = 2; y < 10; y++) for (let x = 3; x < 20; x++) pixels[y * 128 + x] = 1;
  for (let x = 0; x < 128; x++) pixels[31 * 128 + x] = 1;
  return FrameBuffer.fromPixels(pixels);
}

test("an older firmware: CONFIG_INFO answers status 1, the page gets null", async () => {
  const t = await connect({ data: false, apps: true });
  assert.equal(await t.data.info(), null);
  noUpdateCommands(t.device);
});

test("CONFIG_INFO, the flash values with their keys, the saved values of an app", async () => {
  const t = await connect({ installedApps: APPS, appValues: { [RUNNER]: [12, -5, 7] }, configValues: { total_kills: 120, brightness: 4 },
    deviceLog: [{ reason: 5, battery: 41, flags: 1, boot: 17 }] });
  const info = await t.data.info();
  same({ version: info.version, count: info.valueCount, backup: info.backup, preview: info.preview, picture: info.picture, size: info.dataSize, app: info.dataAppSize, log: info.logCount },
    { version: 1, count: 14, backup: true, preview: true, picture: 0, size: 8192, app: 4096, log: 1 });
  const entries = await t.data.entries(info.valueCount);
  same(entries.map((e) => e.key), ["init", "total_kills", "total_deaths", "game_kills", "game_deaths", "battery_type", "show_battery",
    "brightness", "dim_screen", "dim_level", "auto_off", "counter", "sound", "low_bat"]);
  same(entries.find((e) => e.key === "total_kills"), { index: 1, key: "total_kills", value: 120, max: 0xFFFFFFFF });
  assert.equal(entries.find((e) => e.key === "brightness").value, 4);
  same(await t.data.appValues(RUNNER), [12, -5, 7]);
  same(await t.data.appValues(TIMER), [0]);
  assert.equal(await t.data.appValues(1), null, "a built-in app has no saved values");
});

test("a key of 14 and 15 characters comes in 2 parts", async () => {
  const t = await connect();
  t.device.config.push({ key: "a_long_key_name", value: 3, max: 9 }, { key: "fourteen_chars", value: 1, max: 1 });
  const entries = await t.data.entries();
  same(entries.slice(-2).map((e) => e.key), ["a_long_key_name", "fourteen_chars"]);
});

test("backup: readAll and the file of docs/DATA.md 5.3", async () => {
  const t = await connect({ installedApps: APPS, hiddenApps: [2], deviceName: "MIKUS", appValues: { [RUNNER]: [12, 0, 7] },
    configValues: { total_kills: 120, total_deaths: 30, game_kills: 4, game_deaths: 1, battery_type: 2 },
    deviceLog: [{ reason: 5, battery: 41, flags: 0, boot: 17 }, { reason: 2, battery: 255, flags: 1, boot: 16 }] });
  const pic = picture(t.page.FrameBuffer);
  t.device.setStartPicture(pic, false);
  await t.apps.setSetting(RUNNER, 1, 2);
  const read = await t.data.readAll({ firmware: "1.2.0-b2" });
  const file = t.page.BackupFile.make(read, new Date("2026-11-15T12:00:00.000Z"));
  same(Object.keys(file), ["type", "version", "created", "firmware", "stats", "settings", "apps", "deviceName", "startPicture", "deviceLog"]);
  assert.equal(file.type, "stattrak-backup");
  assert.equal(file.created, "2026-11-15T12:00:00.000Z");
  same(file.stats, { total_kills: 120, total_deaths: 30, game_kills: 4, game_deaths: 1 });
  same(file.settings, { battery_type: 2, show_battery: 0, brightness: 8, dim_screen: 3, dim_level: 2, auto_off: 0, counter: 0, sound: 1, low_bat: 0 });
  same(file.apps, [
    { id: 1, name: "KILLS", builtin: true, hidden: false },
    { id: 2, name: "KILLS + DEAD", builtin: true, hidden: true },
    { id: 3, name: "STATS", builtin: true, hidden: false },
    { id: RUNNER, name: "RUNNER", version: 3, hidden: false, settings: [0, 2], values: [12, 0, 7] },
    { id: TIMER, name: "TIMER", version: 1, hidden: false, settings: [], values: [0] },
  ]);
  assert.equal(file.deviceName, "MIKUS");
  assert.equal(file.startPicture.show, false);
  same(Array.from(t.page.FrameBuffer.fromBase64(file.startPicture.pixels)), Array.from(pic));
  same(file.deviceLog, [{ reason: 5, battery: 41, firmwareChanged: false, boot: 17 }, { reason: 2, battery: 255, firmwareChanged: true, boot: 16 }]);
  assert.equal(t.page.BackupFile.fileName(new Date(2026, 10, 5)), "stattrak-backup-2026-11-05.json");
  // The file reads back
  const back = t.page.BackupFile.parse(JSON.stringify(file));
  assert.ok(back.file);
  assert.equal(t.page.BackupFile.parse("{}").problem, "This file is not a StatTrak backup.");
  assert.equal(t.page.BackupFile.parse("not json").problem, "This file is not a StatTrak backup.");
  noUpdateCommands(t.device);
});

/* A backup of device `from`, restored on device `to` */
async function backupOf(options) {
  const t = await connect(options);
  const read = await t.data.readAll();
  return t.page.BackupFile.parse(JSON.stringify(t.page.BackupFile.make(read))).file;
}

test("restore: the STBK data round trip through the device, the order of the apps, the hidden flags", async () => {
  const file = await backupOf({ installedApps: APPS, hiddenApps: [2, TIMER], deviceName: "TEAM 1", appValues: { [RUNNER]: [5, 6, 7], [TIMER]: [99] },
    configValues: { total_kills: 500, game_kills: 9, brightness: 3, sound: 0 } });
  file.startPicture = { show: true, pixels: picture((await connect()).page.FrameBuffer) };
  // The file has an other menu order: TIMER, KILLS, ... ; RUNNER settings 1, 2
  const timer = file.apps.find((a) => a.id === TIMER);
  file.apps = [timer].concat(file.apps.filter((a) => a !== timer));
  file.apps.find((a) => a.id === RUNNER).settings = [1, 2];

  // The new device: the same apps in the standard order, no values
  const t = await connect({ installedApps: APPS });
  const device = await t.data.readAll();
  const B = t.page.BackupFile;
  const parts = { stats: true, settings: true, apps: true, deviceName: true, startPicture: true };
  same(B.appOrder(file, device).map((o) => o.name), ["TIMER", "KILLS", "KILLS + DEAD", "STATS", "RUNNER"]);
  const bytes = B.restoreData(file, device, parts);
  assert.equal(String.fromCharCode(...bytes.slice(0, 4)), "STBK");
  assert.equal(bytes[4], 1);

  let asked = 0;
  const result = await t.data.restore(bytes, null, () => { asked++; setTimeout(() => t.device.holdButton2(), 20); });
  assert.equal(asked, 1);
  assert.equal(result.result, "done");
  assert.equal(t.device.display, "BACKUP RESTORED");
  same(Array.from(t.device.restores[0]), Array.from(bytes));
  assert.equal(result.skipped, 0);
  assert.ok(result.written > 10, "written " + result.written);
  // The device has the values of the file
  assert.equal(t.device.configValue("total_kills"), 500);
  assert.equal(t.device.configValue("game_kills"), 9);
  assert.equal(t.device.configValue("brightness"), 3);
  assert.equal(t.device.configValue("sound"), 0);
  same(t.device.appNames(), ["TIMER", "KILLS", "KILLS + DEAD", "STATS", "RUNNER"]);
  assert.equal(t.device.appHidden("TIMER"), true);
  assert.equal(t.device.appHidden("KILLS + DEAD"), true);
  same(t.device.appSettings(RUNNER), [1, 2]);
  same(t.device.savedValues(RUNNER), [5, 6, 7]);
  same(t.device.savedValues(TIMER), [99]);
  assert.equal(t.device.deviceName, "TEAM 1");
  same(Array.from(t.device.startPic), Array.from(file.startPicture.pixels));
  assert.equal(t.device.startPicShow, true);
  // Read again: nothing is different
  const after = await t.data.readAll();
  same(B.skipped(file, after, parts), []);
  noUpdateCommands(t.device);
});

test("restore: the device skips values it cannot use, and the page lists them", async () => {
  const file = await backupOf({ installedApps: APPS, configValues: { total_kills: 77 } });
  file.settings.brightness = 50;              // above the largest valid value (10)
  file.settings.new_setting = 1;              // a key that this firmware does not have
  file.apps.find((a) => a.id === RUNNER).settings = [1, 7];   // 7 is out of range for SOUND
  file.apps.find((a) => a.id === RUNNER).values = [1, 2];     // an other saved value count
  file.deviceName = " BAD";                   // not a valid name part
  file.apps.push({ id: 0x55555555, name: "GONE", builtin: false, hidden: false, settings: [], values: [] });

  const t = await connect({ installedApps: APPS });
  const device = await t.data.readAll();
  const B = t.page.BackupFile;
  same(B.missingApps(file, device).map((a) => a.name), ["GONE"]);
  const parts = { stats: true, settings: true, apps: true, deviceName: true, startPicture: false };
  // The page leaves out a name that is not correct; send it anyway, as an other page could
  const bytes = Array.from(B.restoreData(file, device, parts));
  bytes.push(5, 5, 0, 4, 32, 66, 65, 68);   // DEVICE_NAME " BAD"
  const result = await t.data.restore(Uint8Array.from(bytes), null, () => setTimeout(() => t.device.holdButton2(), 20));
  assert.equal(result.result, "done");
  // brightness, new_setting, SOUND = 7, the RUNNER values, the name
  assert.equal(result.skipped, 5);
  assert.equal(t.device.configValue("total_kills"), 77);
  assert.equal(t.device.configValue("brightness"), 8);
  same(t.device.appSettings(RUNNER), [1, 0]);
  const after = await t.data.readAll();
  same(B.skipped(file, after, parts).map((r) => r.label).sort(), ["Bluetooth name", "Brightness", "RUNNER", "new_setting"].sort());
});

test("restore: only the ticked parts are sent", async () => {
  const file = await backupOf({ installedApps: APPS, deviceName: "OLD", configValues: { total_kills: 9, brightness: 2 } });
  const t = await connect({ installedApps: APPS, configValues: { total_kills: 1, brightness: 5 } });
  const device = await t.data.readAll();
  const bytes = t.page.BackupFile.restoreData(file, device, { stats: true });
  await t.data.restore(bytes, null, () => setTimeout(() => t.device.holdButton2(), 20));
  assert.equal(t.device.configValue("total_kills"), 9);
  assert.equal(t.device.configValue("brightness"), 5, "settings were not ticked");
  assert.equal(t.device.deviceName, "", "the name was not ticked");
});

test("restore: no, no answer, a damaged backup", async () => {
  const t = await connect({ installedApps: APPS });
  const device = await t.data.readAll();
  const file = t.page.BackupFile.parse(JSON.stringify(t.page.BackupFile.make(device))).file;
  const bytes = t.page.BackupFile.restoreData(file, device, { stats: true });
  same(await t.data.restore(bytes, null, () => setTimeout(() => t.device.holdButton1(), 20)), { result: "refused" });
  same(await t.data.restore(bytes, null, () => {}), { result: "timeout" });
  // A section that goes past the end: status 10, nothing is written
  const bad = Uint8Array.from([0x53, 0x54, 0x42, 0x4B, 1, 1, 50, 0, 3]);
  let error = null;
  try {
    await t.data.restore(bad, null, () => setTimeout(() => t.device.holdButton2(), 20));
  } catch (e) { error = e; }
  assert.equal(error.kind, "status");
  assert.equal(error.status, 10);
  assert.equal(error.reason, "a section is outside the data");
});

test("the table: on the device now / in the file", async () => {
  const file = await backupOf({ installedApps: APPS, configValues: { brightness: 10, total_kills: 5 } });
  const t = await connect({ installedApps: [APPS[0]], configValues: { brightness: 8, total_kills: 5 } });
  const rows = t.page.BackupFile.compare(file, await t.data.readAll());
  const row = (label) => plain(rows.find((r) => r.label === label));
  same(row("Brightness"), { part: "settings", label: "Brightness", device: "80%", file: "100%", same: false });
  same(row("Total kills"), { part: "stats", label: "Total kills", device: "5", file: "5", same: true });
  same(row("TIMER"), { part: "apps", label: "TIMER", device: "Not installed", file: "Shown", same: false });
  same(row("Start picture"), { part: "startPicture", label: "Start picture", device: "None", file: "None", same: true });
  same(row("Bluetooth name"), { part: "deviceName", label: "Bluetooth name", device: "StatTrak", file: "StatTrak", same: true });
});

test("start picture: send (in order from 0), read, on, off, delete", async () => {
  const t = await connect();
  const pic = picture(t.page.FrameBuffer);
  const progress = [];
  same(await t.data.sendStartPicture(pic, (f) => progress.push(f)), { result: "done" });
  assert.equal(progress[progress.length - 1], 1);
  assert.equal(t.device.xferDataCount, Math.ceil(512 / 14), "all parts, also the empty ones");
  same(Array.from(t.device.startPic), Array.from(pic));
  assert.equal(t.device.display, "START PICTURE");
  assert.equal((await t.data.info()).picture, 1);
  same(Array.from(await t.data.readStartPicture()), Array.from(pic));
  assert.equal(await t.data.setStartPictureShown(false), 2);
  assert.equal(t.device.startPicShow, false);
  assert.equal(await t.data.setStartPictureShown(true), 1);
  assert.equal(await t.data.deleteStartPicture(), 0);
  assert.equal(t.device.startPic, null);
  assert.equal(await t.data.readStartPicture(), null);
  await assert.rejects(t.data.setStartPictureShown(true), (e) => e.kind === "status" && e.status === 2);
  noUpdateCommands(t.device);
});

test("preview frame: only the parts that are not empty go, END with the CRC of all 512 bytes, PREVIEW_STOP", async () => {
  const t = await connect();
  const pic = picture(t.page.FrameBuffer);
  same(await t.data.sendPreview(pic), { result: "done" });
  // The box (bytes 3..19 of page 0 and 1) and the line (page 3, all 128 bytes)
  const parts = new Set();
  for (let i = 0; i < 512; i++) if (pic[i]) parts.add(Math.floor(i / 14));
  assert.equal(t.device.xferDataCount, parts.size);
  assert.ok(parts.size < 20, "empty parts were sent: " + parts.size);
  same(t.page.FakeStatTrak.frameRows(t.device.previewFrame), t.page.FakeStatTrak.frameRows(pic));
  assert.equal(t.device.syncState, 6);
  assert.equal(await t.apps.status(), 6);
  // A second frame replaces the first one
  const empty = new Uint8Array(512);
  await t.data.sendPreview(empty);
  assert.equal(t.device.previewCount, 2);
  same(Array.from(t.device.previewFrame), Array.from(empty));
  await t.data.previewStop();
  assert.equal(t.device.syncState, 0);
  assert.equal(t.device.display, "Waiting...");
  noUpdateCommands(t.device);
});

test("app data: DATA_INFO, the columns, the rows, the CSV with the TIME column; delete after yes", async () => {
  const rows = [[83456, 3], [1000, 0], [-5, 12]];
  const t = await connect({ installedApps: APPS, appData: { [RUNNER]: { columns: ["TIME", "HITS"], rows } } });
  same(await t.data.dataInfo(RUNNER), { columns: 2, rows: 3, fit: 512, full: false, otherColumns: false });
  same(await t.data.dataInfo(TIMER), { columns: 0, rows: 0, fit: 0, full: false, otherColumns: false });
  assert.equal(await t.data.dataInfo(0x99999999), null);
  const names = await t.data.dataColumns(RUNNER, 2);
  same(names, ["TIME", "HITS"]);
  const read = await t.data.dataRows(RUNNER, 2, 3);
  same(read, rows);
  const csv = t.page.DataCsv.appData(names, read);
  assert.equal(csv, "TIME,TIME mm:ss.mmm,HITS\r\n83456,01:23.456,3\r\n1000,00:01.000,0\r\n-5,-00:00.005,12\r\n");
  assert.equal(t.page.DataCsv.appData(["SCORE"], [[1], [2]]), "SCORE\r\n1\r\n2\r\n");
  assert.equal(t.page.DataCsv.appFileName("ROUND TIMER", new Date(2026, 0, 2)), "round-timer-data-2026-01-02.csv");
  assert.equal(t.page.DataCsv.time(3725004), "62:05.004");

  // Delete: the device asks; no = the rows stay; yes = gone
  assert.equal(await t.data.dataClear(RUNNER, () => setTimeout(() => t.device.holdButton1(), 20)), "refused");
  assert.equal(t.device.appData(RUNNER).length, 3);
  let asked = false;
  assert.equal(await t.data.dataClear(RUNNER, () => {
    asked = true;
    assert.equal(t.device.display, "DELETE RUNNER DATA?");
    setTimeout(() => t.device.holdButton2(), 20);
  }), "deleted");
  assert.ok(asked);
  assert.equal(t.device.appData(RUNNER).length, 0);
  same(await t.data.dataInfo(RUNNER), { columns: 2, rows: 0, fit: 512, full: false, otherColumns: false });
  // No rows: DATA_CLEAR answers status 2
  await assert.rejects(t.data.dataClear(RUNNER), (e) => e.kind === "status" && e.status === 2);
});

test("app data: a store that fills all 4096 bytes is full; deleting the app deletes its data", async () => {
  const rows = Array.from({ length: 1024 }, (_, i) => [i]);
  const t = await connect({ installedApps: APPS, appData: { [TIMER]: { columns: ["SCORE"], rows } } });
  same(await t.data.dataInfo(TIMER), { columns: 1, rows: 1024, fit: 1024, full: true, otherColumns: false });
  const progress = [];
  const read = await t.data.dataRows(TIMER, 1, 1024, (f) => progress.push(f));
  assert.equal(read.length, 1024);
  assert.equal(read[1023][0], 1023);
  assert.equal(progress[progress.length - 1], 1);
  await t.apps.delete(TIMER);
  assert.equal(t.device.appData(TIMER), null);
});

test("device log: newest first, the reason texts, the CSV", async () => {
  const entries = Array.from({ length: 64 }, (_, i) => ({ reason: i % 8, battery: i === 0 ? 255 : 100 - i, flags: i === 1 ? 1 : 0, boot: (200 - i) & 255 }));
  const t = await connect({ deviceLog: entries });
  const log = await t.data.deviceLog();
  assert.equal(log.length, 64);
  same(log[0], { reason: 0, battery: 255, firmwareChanged: false, boot: 200 });
  same(log[1], { reason: 1, battery: 99, firmwareChanged: true, boot: 199 });
  same(log[63].boot, (200 - 63) & 255);
  assert.equal(t.page.DataCsv.reasonText(5), "Low supply (brown-out)");
  assert.equal(t.page.DataCsv.batteryText(255), "USB / no battery");
  const csv = t.page.DataCsv.deviceLog(log.slice(0, 2)).split("\r\n");
  same(csv.slice(0, 3), ["Start number,Reason,Reason code,Battery,New firmware", "200,Unknown,0,USB / no battery,", "199,Turned on,1,99,yes"]);
  // An empty log
  const t2 = await connect();
  same(await t2.data.deviceLog(), []);
});

test("while the device asks, the commands that change something get status 11", async () => {
  const t = await connect({ installedApps: APPS, appData: { [RUNNER]: { columns: ["A"], rows: [[1]] } } });
  // DATA_CLEAR alone (without the wait of dataClear): the device asks
  await t.apps._ok(t.page.StatTrakData.CMD__DATA_CLEAR, t.page.StatTrakApps._id(RUNNER));
  assert.equal(t.device.display, "DELETE RUNNER DATA?");
  await assert.rejects(t.data.setStartPictureShown(true), (e) => e.kind === "waiting");
  await assert.rejects(t.data.previewStop(), (e) => e.kind === "waiting");
  await assert.rejects(t.apps.setHidden(2, true), (e) => e.kind === "waiting");
  assert.equal((await t.data.info()).version, 1, "reading still works");
  t.device.holdButton1();
  assert.equal(await t.apps.status(), 4);
  assert.equal(t.device.appData(RUNNER).length, 1);
});

test("FrameBuffer: pixels, fit, the dark limit, base64", () => {
  const { FrameBuffer } = loadPageCode();
  const pixels = new Uint8Array(4096);
  pixels[0] = 1; pixels[7 * 128 + 5] = 1; pixels[8 * 128 + 127] = 1; pixels[31 * 128 + 64] = 1;
  const bytes = FrameBuffer.fromPixels(pixels);
  assert.equal(bytes[0], 1);
  assert.equal(bytes[5], 0x80);
  assert.equal(bytes[128 + 127], 1);
  assert.equal(bytes[3 * 128 + 64], 0x80);
  same(Array.from(FrameBuffer.toPixels(bytes)), Array.from(pixels));
  same(FrameBuffer.fit(128, 32), { x: 0, y: 0, w: 128, h: 32, scaled: false });
  same(FrameBuffer.fit(64, 64), { x: 48, y: 0, w: 32, h: 32, scaled: true });
  same(FrameBuffer.fit(512, 64), { x: 0, y: 8, w: 128, h: 16, scaled: true });
  // Gray 40 % bright: off at 50 %, on at 70 % ("how dark is on")
  const rgba = new Uint8Array(4096 * 4);
  for (let i = 0; i < 4096; i++) rgba.set([102, 102, 102, 255], i * 4);
  rgba.set([255, 255, 255, 0], 0);   // transparent = off
  assert.equal(FrameBuffer.fromRgba(rgba, 50)[1], 0);
  assert.equal(FrameBuffer.fromRgba(rgba, 70)[1], 1);
  assert.equal(FrameBuffer.fromRgba(rgba, 70)[0], 0);
  for (const n of [0, 1, 2, 3, 511, 512]) {
    const b = Uint8Array.from({ length: n }, (_, i) => (i * 37) & 255);
    same(Array.from(FrameBuffer.fromBase64(FrameBuffer.toBase64(b))), Array.from(b));
  }
  assert.equal(FrameBuffer.toBase64(Uint8Array.from([77, 97, 110])), "TWFu");
});
