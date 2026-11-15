/* StatTrak backup, app data, device log, start picture and live preview: the commands of
 * docs/DATA.md (0x0120 .. 0x012A, and the transfers with DATA / END / STATUS) over the connection
 * of js/apps-protocol.js, and the files that the page makes from them (the backup file, the restore
 * data "STBK", CSV). No page here: the tests use this file directly. The Apps tab (js/apps-data.js)
 * and the Screen Designer (live preview) load it after js/apps-protocol.js.
 */

/* The frame buffer of the display: 128 x 32 pixels in 512 bytes, 4 pages of 128 bytes, bit 0 =
   the top pixel of the page (docs/DATA.md section 1, "spic"). Pixels: 4096 values 0 / 1, row by
   row (as StatTrakDesign.drawScreen and js/device.js). */
const FrameBuffer = (() => {
  const W = 128, H = 32, BYTES = 512;

  function fromPixels(pixels) {
    const bytes = new Uint8Array(BYTES);
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) if (pixels[y * W + x]) bytes[(y >> 3) * W + x] |= 1 << (y & 7);
    }
    return bytes;
  }

  function toPixels(bytes) {
    const pixels = new Uint8Array(W * H);
    for (let y = 0; y < H; y++) {
      for (let x = 0; x < W; x++) pixels[y * W + x] = (bytes[(y >> 3) * W + x] >> (y & 7)) & 1;
    }
    return pixels;
  }

  /* Where a picture of width x height goes: scaled to fit 128 x 32, the same aspect, centered.
     scaled = the picture is not 128 x 32. */
  function fit(width, height) {
    if (width === W && height === H) return { x: 0, y: 0, w: W, h: H, scaled: false };
    const scale = Math.min(W / width, H / height);
    const w = Math.max(1, Math.round(width * scale)), h = Math.max(1, Math.round(height * scale));
    return { x: Math.floor((W - w) / 2), y: Math.floor((H - h) / 2), w, h, scaled: true };
  }

  /* RGBA bytes of 128 x 32 (on black) -> pixels. darkPct (0 .. 100) = "how dark is on": a pixel is
     on when it is brighter than (100 - darkPct) %. 50 = the brighter half is on. */
  function fromRgba(rgba, darkPct) {
    const limit = (100 - darkPct) * 255;
    const pixels = new Uint8Array(W * H);
    for (let i = 0; i < W * H; i++) {
      const p = i * 4;
      const light = (0.299 * rgba[p] + 0.587 * rgba[p + 1] + 0.114 * rgba[p + 2]) * (rgba[p + 3] / 255);
      pixels[i] = light * 100 > limit ? 1 : 0;
    }
    return pixels;
  }

  const B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  function toBase64(bytes) {
    let out = "";
    for (let i = 0; i < bytes.length; i += 3) {
      const n = (bytes[i] << 16) | ((bytes[i + 1] || 0) << 8) | (bytes[i + 2] || 0);
      out += B64[(n >> 18) & 63] + B64[(n >> 12) & 63] + (i + 1 < bytes.length ? B64[(n >> 6) & 63] : "=") + (i + 2 < bytes.length ? B64[n & 63] : "=");
    }
    return out;
  }
  function fromBase64(text) {
    if (typeof text !== "string" || !/^[A-Za-z0-9+/]*={0,2}$/.test(text) || text.length % 4) return null;
    const out = [];
    for (let i = 0; i < text.length; i += 4) {
      const c = [0, 1, 2, 3].map((k) => (text[i + k] === "=" ? 0 : B64.indexOf(text[i + k])));
      const n = (c[0] << 18) | (c[1] << 12) | (c[2] << 6) | c[3];
      out.push((n >> 16) & 255);
      if (text[i + 2] !== "=") out.push((n >> 8) & 255);
      if (text[i + 3] !== "=") out.push(n & 255);
    }
    return Uint8Array.from(out);
  }

  return { W, H, BYTES, fromPixels, toPixels, fit, fromRgba, toBase64, fromBase64 };
})();


/* The texts of the device log (docs/DATA.md section 3) and the CSV files */
const DataCsv = (() => {
  // Why the device started. The reason says how the run before it ended.
  const REASONS = ["Unknown", "Turned on", "Restart (software or firmware update)", "Crash", "Watchdog (the firmware stopped)",
    "Low supply (brown-out)", "Woke from sleep", "Other"];
  const reasonText = (reason) => REASONS[reason] || "Unknown";
  const batteryText = (battery) => (battery === 255 ? "USB / no battery" : battery + "%");

  const field = (value) => {
    const text = String(value);
    return /[",\r\n]/.test(text) ? '"' + text.replace(/"/g, '""') + '"' : text;
  };
  const line = (values) => values.map(field).join(",");

  /* Milliseconds as mm:ss.mmm (the minutes can be more than 59) */
  function time(ms) {
    const sign = ms < 0 ? "-" : "";
    const a = Math.abs(ms);
    const minutes = Math.floor(a / 60000), seconds = Math.floor((a % 60000) / 1000), rest = a % 1000;
    return sign + String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0") + "." + String(rest).padStart(3, "0");
  }

  /* The app data: the column names, then one line for each row. A first column named TIME gets a
     column "TIME mm:ss.mmm" after it (its value is milliseconds). */
  function appData(columns, rows) {
    const withTime = columns.length > 0 && columns[0].toUpperCase() === "TIME";
    const head = withTime ? [columns[0], "TIME mm:ss.mmm"].concat(columns.slice(1)) : columns.slice();
    const out = [line(head)];
    for (const row of rows) out.push(line(withTime ? [row[0], time(row[0])].concat(row.slice(1)) : row));
    return out.join("\r\n") + "\r\n";
  }

  /* The device log, newest first */
  function deviceLog(entries) {
    const out = [line(["Start number", "Reason", "Reason code", "Battery", "New firmware"])];
    for (const e of entries) {
      out.push(line([e.boot, reasonText(e.reason), e.reason, e.battery === 255 ? "USB / no battery" : e.battery, e.firmwareChanged ? "yes" : ""]));
    }
    return out.join("\r\n") + "\r\n";
  }

  /* YYYY-MM-DD of the local date */
  const day = (date) => {
    const d = date || new Date();
    return d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0");
  };
  const slug = (name) => String(name).toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "") || "app";
  const appFileName = (name, date) => `${slug(name)}-data-${day(date)}.csv`;

  return { REASONS, reasonText, batteryText, time, appData, deviceLog, day, appFileName };
})();


/* The backup file (docs/DATA.md 5.3) and the restore data (5.4) */
const BackupFile = (() => {
  const STATS = ["total_kills", "total_deaths", "game_kills", "game_deaths"];
  const STAT_LABELS = { total_kills: "Total kills", total_deaths: "Total deaths", game_kills: "Game kills", game_deaths: "Game deaths" };
  // main/app.h SETTING_LIST: the names and the value texts of the settings
  const SETTINGS = {
    counter: ["Counter", ["TOTAL", "GAME"]],
    battery_type: ["Battery", ["NONE", "USB", "1xRCR123A", "3.7V(1S)", "7.4V(2S)", "11.1V(3S)", "3xAA/AAA", "9.6V(NiMH)"]],
    auto_off: ["Auto off", ["OFF", "30 min", "1 h", "2 h", "6 h", "12 h"]],
    show_battery: ["Show battery", ["OFF", "ON"]],
    low_bat: ["Low battery", ["OFF", "0%", "10%", "20%", "30%"]],
    brightness: ["Brightness", ["1%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"]],
    dim_screen: ["Dim screen", ["OFF", "5 sec", "10 sec", "30 sec", "1 min", "2 min", "5 min", "10 min"]],
    dim_level: ["Dim level", ["1%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%"]],
    sound: ["Sound", ["OFF", "ON"]],
  };
  const PARTS = ["stats", "settings", "apps", "deviceName", "startPicture"];
  const PART_LABELS = { stats: "Stats", settings: "Settings", apps: "Apps setup", deviceName: "Bluetooth name", startPicture: "Start picture" };
  const NAME_PREFIX = "StatTrak | ";
  const MAX_RESTORE = 4096;

  const SECTION = { VALUE: 1, APP_ORDER: 2, APP_SETTINGS: 3, APP_VALUES: 4, DEVICE_NAME: 5, START_PIC: 6 };

  const isU32 = (v) => Number.isInteger(v) && v >= 0 && v <= 0xFFFFFFFF;
  const isI32 = (v) => Number.isInteger(v) && v >= -0x80000000 && v <= 0x7FFFFFFF;
  const sameArray = (a, b) => Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((v, i) => v === b[i]);

  /* The file from what StatTrakData.readAll() read */
  function make(read, now) {
    const stats = {}, settings = {};
    for (const e of read.entries) {
      if (e.key === "init") continue;
      if (STATS.includes(e.key)) stats[e.key] = e.value; else settings[e.key] = e.value;
    }
    return {
      type: "stattrak-backup", version: 1,
      created: (now || new Date()).toISOString(), firmware: read.firmware || "",
      stats, settings,
      apps: read.apps.map((a) => (a.builtin ? { id: a.id, name: a.name, builtin: true, hidden: a.hidden }
        : { id: a.id, name: a.name, version: a.version, hidden: a.hidden, settings: a.settings.slice(), values: a.values.slice() })),
      deviceName: read.deviceName || "",
      startPicture: read.startPicture ? { show: read.startPicture.show, pixels: FrameBuffer.toBase64(read.startPicture.pixels) } : null,
      deviceLog: (read.log || []).map((e) => ({ reason: e.reason, battery: e.battery, firmwareChanged: e.firmwareChanged, boot: e.boot })),
    };
  }

  const fileName = (date) => `stattrak-backup-${DataCsv.day(date)}.json`;

  /* Reads a backup file (text). Returns { file } or { problem } (a text for the person). The
     values are checked when the restore data is made: a value that is not correct stays out. */
  function parse(text) {
    let data;
    try { data = JSON.parse(text); } catch (_) { return { problem: "This file is not a StatTrak backup." }; }
    if (!data || typeof data !== "object" || data.type !== "stattrak-backup") return { problem: "This file is not a StatTrak backup." };
    if (data.version !== 1) return { problem: "This backup is from a newer page. Use the newest page." };
    const obj = (v) => (v && typeof v === "object" && !Array.isArray(v) ? v : {});
    const file = {
      type: data.type, version: 1, created: String(data.created || ""), firmware: String(data.firmware || ""),
      stats: obj(data.stats), settings: obj(data.settings),
      apps: (Array.isArray(data.apps) ? data.apps : []).filter((a) => a && isU32(a.id) && typeof a.name === "string").map((a) => ({
        id: a.id, name: a.name, builtin: !!a.builtin, hidden: !!a.hidden, version: a.version,
        settings: Array.isArray(a.settings) ? a.settings : null, values: Array.isArray(a.values) ? a.values : null,
      })),
      deviceName: typeof data.deviceName === "string" ? data.deviceName : "",
      startPicture: null,
    };
    if (data.startPicture && typeof data.startPicture === "object") {
      const pixels = FrameBuffer.fromBase64(data.startPicture.pixels);
      if (pixels && pixels.length === FrameBuffer.BYTES) file.startPicture = { show: data.startPicture.show !== false, pixels };
    }
    return { file };
  }

  /* The apps of the file that the device does not have (not built-in) */
  const missingApps = (file, device) => file.apps.filter((a) => !a.builtin && !device.apps.some((d) => d.id === a.id));

  /* The menu order for the restore: the apps of the device; first the ones of the file in the order
     of the file, then the other apps of the device. Hidden: from the file for its apps. */
  function appOrder(file, device) {
    const order = [];
    for (const a of file.apps) {
      const d = device.apps.find((x) => x.id === a.id);
      if (d && !order.some((o) => o.id === d.id)) order.push({ id: d.id, name: d.name, hidden: a.hidden });
    }
    for (const d of device.apps) if (!order.some((o) => o.id === d.id)) order.push({ id: d.id, name: d.name, hidden: d.hidden });
    return order;
  }

  /* The restore data (docs/DATA.md 5.4) of the ticked parts. device = StatTrakData.readAll(). */
  function restoreData(file, device, parts) {
    const out = [0x53, 0x54, 0x42, 0x4B, 1];   // "STBK", version 1
    const u16 = (v) => [v & 255, (v >> 8) & 255];
    const u32 = (v) => [v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255];
    const section = (type, bytes) => out.push(type, ...u16(bytes.length), ...bytes);
    const value = (key, v) => {
      if (!/^[\x20-\x7E]{1,15}$/.test(key) || key === "init" || !isU32(v)) return;
      section(SECTION.VALUE, [key.length, ...Array.from(key, (c) => c.charCodeAt(0)), ...u32(v)]);
    };
    if (parts.stats) for (const key of Object.keys(file.stats)) value(key, file.stats[key]);
    if (parts.settings) for (const key of Object.keys(file.settings)) if (!STATS.includes(key)) value(key, file.settings[key]);
    if (parts.apps) {
      const order = appOrder(file, device);
      section(SECTION.APP_ORDER, [order.length, ...order.flatMap((o) => [...u32(o.id), o.hidden ? 1 : 0])]);
      for (const a of file.apps) {
        if (a.builtin || !device.apps.some((d) => d.id === a.id)) continue;
        if (a.settings && a.settings.length <= 255 && a.settings.every((v) => Number.isInteger(v) && v >= 0 && v <= 255)) {
          section(SECTION.APP_SETTINGS, [...u32(a.id), a.settings.length, ...a.settings]);
        }
        if (a.values && a.values.length <= 255 && a.values.every(isI32)) {
          section(SECTION.APP_VALUES, [...u32(a.id), a.values.length, ...a.values.flatMap((v) => u32(v >>> 0))]);
        }
      }
    }
    // A name part that is not correct stays out (the list after the restore shows it)
    if (parts.deviceName && StatTrakApps.nameProblem(file.deviceName) === "") {
      section(SECTION.DEVICE_NAME, [file.deviceName.length, ...Array.from(file.deviceName, (c) => c.charCodeAt(0))]);
    }
    if (parts.startPicture) {
      if (!file.startPicture) section(SECTION.START_PIC, [0]);
      else section(SECTION.START_PIC, [file.startPicture.show ? 1 : 2, ...file.startPicture.pixels]);
    }
    if (out.length > MAX_RESTORE) throw new Error("The backup is too large to restore");
    return Uint8Array.from(out);
  }

  /* The lines of the table "On the device now / In the file": [{ part, label, device, file, same }].
     device = StatTrakData.readAll(). A line of a part of the file that the file does not have
     (file "—") is the same. */
  function compare(file, device) {
    const rows = [];
    const values = {};
    for (const e of device.entries) values[e.key] = e.value;
    const valueText = (key, v) => {
      if (v === undefined) return "—";
      const s = SETTINGS[key];
      return s && s[1][v] !== undefined ? s[1][v] : String(v);
    };
    const add = (part, label, now, inFile, same) => rows.push({ part, label, device: now, file: inFile, same: same === undefined ? now === inFile : same });
    for (const key of STATS) {
      if (!(key in file.stats)) continue;
      add("stats", STAT_LABELS[key], valueText(key, values[key]), valueText(key, file.stats[key]), values[key] === file.stats[key]);
    }
    const keys = Object.keys(SETTINGS).filter((k) => k in values || k in file.settings)
      .concat(Object.keys(file.settings).filter((k) => !(k in SETTINGS) && !STATS.includes(k)));
    for (const key of keys) {
      if (!(key in file.settings)) continue;
      add("settings", SETTINGS[key] ? SETTINGS[key][0] : key, valueText(key, values[key]), valueText(key, file.settings[key]), values[key] === file.settings[key]);
    }
    const nameText = (part) => (part ? NAME_PREFIX + part : "StatTrak");
    add("deviceName", "Bluetooth name", nameText(device.deviceName || ""), nameText(file.deviceName));
    const picText = (p) => (!p ? "None" : p.show ? "Shown at power-on" : "Kept, not shown");
    const dp = device.startPicture, fp = file.startPicture;
    const picSame = (!dp && !fp) || (!!dp && !!fp && dp.show === fp.show && sameArray(Array.from(dp.pixels), Array.from(fp.pixels)));
    add("startPicture", "Start picture", picText(dp), picText(fp) + (dp && fp && !picSame && dp.show === fp.show ? " (an other picture)" : ""), picSame);
    // Apps: the order of the apps that both have, then each app
    // The order after a restore: the apps of the file first (appOrder)
    const order = appOrder(file, device);
    add("apps", "App order", device.apps.map((d) => d.name).join(", "), order.map((o) => o.name).join(", "),
      sameArray(device.apps.map((d) => d.id), order.map((o) => o.id)));
    for (const a of file.apps) {
      const d = device.apps.find((x) => x.id === a.id);
      const state = (app) => (app.hidden ? "Hidden" : "Shown");
      if (!d) { add("apps", a.name, "Not installed", state(a), false); continue; }
      let same = d.hidden === a.hidden, extra = [];
      if (!a.builtin && a.settings && !sameArray(a.settings, d.settings)) { same = false; extra.push("other settings"); }
      if (!a.builtin && a.values && !sameArray(a.values, d.values)) { same = false; extra.push("other saved values"); }
      add("apps", d.name, state(d), state(a) + (extra.length ? " (" + extra.join(", ") + ")" : ""), same);
    }
    return rows;
  }

  /* After a restore: the lines of the ticked parts that are not the same as in the file. An app of
     the file that is not installed is not in this list (the page lists those apps before). */
  function skipped(file, device, parts) {
    return compare(file, device).filter((r) => parts[r.part] && !r.same && r.device !== "Not installed");
  }

  return { STATS, SETTINGS, PARTS, PART_LABELS, SECTION, make, fileName, parse, missingApps, appOrder, restoreData, compare, skipped };
})();


class StatTrakData {

  // Commands (docs/DATA.md 5.2)
  static CMD__CONFIG_INFO  = 0x0120;
  static CMD__CONFIG_ENTRY = 0x0121;
  static CMD__APP_VALUES   = 0x0122;
  static CMD__XFER_BEGIN   = 0x0123;
  static CMD__START_PIC    = 0x0124;
  static CMD__PREVIEW_STOP = 0x0125;
  static CMD__DATA_INFO    = 0x0126;
  static CMD__DATA_COLUMN  = 0x0127;
  static CMD__DATA_READ    = 0x0128;
  static CMD__DATA_CLEAR   = 0x0129;
  static CMD__LOG_READ     = 0x012A;

  // The kinds of a transfer (5.1) and of STATUS byte 1
  static KIND__PACKAGE      = 0;
  static KIND__RESTORE      = 1;
  static KIND__START_PIC    = 2;
  static KIND__PREVIEW      = 3;
  static KIND__DATA_CLEAR   = 4;
  static STATE__PREVIEW     = 6;   // STATUS: a preview frame shows

  // START_PIC ops and states
  static PIC__READ = 0;
  static PIC__ON = 1;
  static PIC__OFF = 2;
  static PIC__DELETE = 3;
  static PIC__PIXELS = 4;
  static PIC_STATE__NONE = 0;
  static PIC_STATE__ON = 1;
  static PIC_STATE__OFF = 2;

  // END status 10 of a restore: u8 reason
  static BAD_BACKUP_TEXTS = { 1: "bad magic", 2: "bad version", 3: "a section is outside the data" };

  constructor(apps) {
    this.apps = apps;   // StatTrakApps: its connection and its queue (one command at a time)
  }

  _do(task) { return this.apps._serial(task); }
  _ok(cmd, data) { return this.apps._ok(cmd, data); }
  static _i32(d, o) { return StatTrakApps._u32(d, o) | 0; }

  /* CONFIG_INFO, or null for a firmware without these commands (status 1). */
  info() {
    return this._do(async () => {
      let d;
      try {
        d = await this._ok(StatTrakData.CMD__CONFIG_INFO);
      } catch (error) {
        if (error.kind === "old firmware") return null;
        throw error;
      }
      const u16 = (o) => StatTrakApps._u16(d, o);
      const features = d[2];
      return {
        version: d[0], valueCount: d[1], features,
        backup: !!(features & 1), appData: !!(features & 2), deviceLog: !!(features & 4), startPicture: !!(features & 8), preview: !!(features & 16),
        picture: d[3], dataUsed: u16(4), dataSize: u16(6), dataAppSize: u16(8), freeFlash: u16(10), logCount: d[12],
      };
    });
  }

  /* All flash values (CONFIG_ENTRY): [{ index, key, value, max }] */
  entries(count) {
    return this._do(async () => {
      const out = [];
      for (let index = 0; index < 256 && (count === undefined || index < count); index++) {
        const answer = await this.apps._command(StatTrakData.CMD__CONFIG_ENTRY, [index, 0]);
        if (answer.status === StatTrakApps.STATUS__INDEX) break;
        if (answer.status !== StatTrakApps.STATUS__OK) throw new AppsError("status", `StatTrak answered status ${answer.status} to CONFIG_ENTRY`, { status: answer.status });
        const d = answer.data;
        const length = d[8];
        if (length < 1 || length > 15) throw new AppsError("bad answer", "A flash value without a correct key");
        let key = StatTrakApps._text(await this._ok(StatTrakData.CMD__CONFIG_ENTRY, [index, 1]), Math.min(13, length));
        if (length > 13) key += StatTrakApps._text(await this._ok(StatTrakData.CMD__CONFIG_ENTRY, [index, 2]), length - 13);
        if (key.length !== length) throw new AppsError("bad answer", "A flash value without a correct key");
        out.push({ index, key, value: StatTrakApps._u32(d, 0), max: StatTrakApps._u32(d, 4) });
      }
      return out;
    });
  }

  /* The saved values of an installed app (APP_VALUES), or null (not an installed app) */
  appValues(id) {
    return this._do(async () => {
      const values = [];
      for (;;) {
        const answer = await this.apps._command(StatTrakData.CMD__APP_VALUES, [...StatTrakApps._id(id), values.length]);
        if (answer.status === StatTrakApps.STATUS__ERROR) return null;
        if (answer.status !== StatTrakApps.STATUS__OK) throw new AppsError("status", `StatTrak answered status ${answer.status} to APP_VALUES`, { status: answer.status });
        const count = answer.data[0];
        const n = Math.min(3, count - values.length);
        for (let i = 0; i < n; i++) values.push(StatTrakData._i32(answer.data, 1 + 4 * i));
        if (values.length >= count) return values;
      }
    });
  }

  /* START_PIC: the state (0 none, 1 shown at power-on, 2 kept but off) */
  startPicture() { return this._picOp(StatTrakData.PIC__READ); }
  setStartPictureShown(shown) { return this._picOp(shown ? StatTrakData.PIC__ON : StatTrakData.PIC__OFF); }
  deleteStartPicture() { return this._picOp(StatTrakData.PIC__DELETE); }
  _picOp(op) {
    return this._do(async () => (await this._ok(StatTrakData.CMD__START_PIC, [op]))[0]);
  }

  /* The pixels of the start picture (512 bytes), or null (no picture) */
  readStartPicture() {
    return this._do(async () => {
      const bytes = new Uint8Array(FrameBuffer.BYTES);
      for (let offset = 0; offset < FrameBuffer.BYTES; offset += 12) {
        const answer = await this.apps._command(StatTrakData.CMD__START_PIC, [StatTrakData.PIC__PIXELS, offset & 255, offset >> 8]);
        if (answer.status === StatTrakApps.STATUS__ERROR) return null;
        if (answer.status !== StatTrakApps.STATUS__OK) throw new AppsError("status", `StatTrak answered status ${answer.status} to START_PIC`, { status: answer.status });
        bytes.set(answer.data.subarray(0, Math.min(12, FrameBuffer.BYTES - offset)), offset);
      }
      return bytes;
    });
  }

  /* The preview ends; the display shows "Waiting..." */
  previewStop() {
    return this._do(() => this._ok(StatTrakData.CMD__PREVIEW_STOP));
  }

  /* DATA_INFO: { columns, rows, fit, full, otherColumns }, or null (an unknown id) */
  dataInfo(id) {
    return this._do(async () => {
      const answer = await this.apps._command(StatTrakData.CMD__DATA_INFO, StatTrakApps._id(id));
      if (answer.status === StatTrakApps.STATUS__ERROR) return null;
      if (answer.status !== StatTrakApps.STATUS__OK) throw new AppsError("status", `StatTrak answered status ${answer.status} to DATA_INFO`, { status: answer.status });
      const d = answer.data;
      if (d[0] > 4) throw new AppsError("bad answer", "The answer to DATA_INFO is not correct");
      return { columns: d[0], rows: StatTrakApps._u16(d, 1), fit: StatTrakApps._u16(d, 3), full: !!(d[5] & 1), otherColumns: !!(d[5] & 2) };
    });
  }

  /* The column names (DATA_COLUMN) */
  dataColumns(id, count) {
    return this._do(async () => {
      const names = [];
      for (let c = 0; c < count; c++) {
        const name = StatTrakApps._text(await this._ok(StatTrakData.CMD__DATA_COLUMN, [...StatTrakApps._id(id), c]), 12);
        if (!name) throw new AppsError("bad answer", "A column without a name");
        names.push(name);
      }
      return names;
    });
  }

  /* The rows of the app data (DATA_READ): [[value of each column]]. onProgress(fraction). */
  async dataRows(id, columns, rows, onProgress) {
    const total = columns * rows;
    const values = [];
    while (values.length < total) {
      const first = values.length;
      const d = await this._do(() => this._ok(StatTrakData.CMD__DATA_READ, [...StatTrakApps._id(id), first & 255, first >> 8]));
      const n = Math.min(d[0], 3, total - first);
      if (n === 0) break;   // The rows were deleted meanwhile
      for (let i = 0; i < n; i++) values.push(StatTrakData._i32(d, 1 + 4 * i));
      if (onProgress) onProgress(values.length / total);
    }
    const out = [];
    for (let i = 0; i + columns <= values.length; i += columns) out.push(values.slice(i, i + columns));
    return out;
  }

  /* DATA_CLEAR: the device asks "DELETE <NAME> DATA?". onWaiting() when it asks. Returns
     "deleted", "refused" or "timeout". */
  dataClear(id, onWaiting) {
    return this._do(async () => {
      await this._ok(StatTrakData.CMD__DATA_CLEAR, StatTrakApps._id(id));
      if (onWaiting) onWaiting();
      const answer = await this.apps._waitForPerson(StatTrakApps.STATE__INSTALLED);
      return answer === "yes" ? "deleted" : answer;
    });
  }

  /* The device log (LOG_READ), newest first: [{ reason, battery, firmwareChanged, boot }] */
  deviceLog() {
    return this._do(async () => {
      const out = [];
      for (;;) {
        const d = await this._ok(StatTrakData.CMD__LOG_READ, [out.length]);
        const count = d[0];
        const n = Math.min(3, count - out.length);
        for (let i = 0; i < n; i++) {
          const p = 1 + 4 * i;
          out.push({ reason: d[p], battery: d[p + 1], firmwareChanged: !!(d[p + 2] & 1), boot: d[p + 3] });
        }
        if (n <= 0 || out.length >= count) return out;
      }
    });
  }

  /* A transfer (5.1): XFER_BEGIN, then DATA and END. A restore first waits for the person
     ("RESTORE BACKUP?": onWaiting()). Returns { result, written, skipped }: result "done",
     "refused", "timeout" or "stopped". Throws AppsError ("status" 10 with .reason for a bad
     backup, 9 damaged, 2 refused). */
  transfer(kind, bytes, onProgress, onWaiting) {
    return this._do(async () => {
      const begin = new Uint8Array(7);
      const view = new DataView(begin.buffer);
      begin[0] = kind;
      view.setUint16(1, bytes.length, true);
      view.setUint32(3, StappFile.crc32(bytes), true);
      await this._ok(StatTrakData.CMD__XFER_BEGIN, begin);
      if (kind === StatTrakData.KIND__RESTORE) {
        if (onWaiting) onWaiting();
        const answer = await this.apps._waitForPerson(StatTrakApps.STATE__RECEIVING);
        if (answer !== "yes") return { result: answer };
      }
      const preview = kind === StatTrakData.KIND__PREVIEW;
      const sent = await this.apps._sendParts(bytes, onProgress,
        { sparse: preview, doneState: preview ? StatTrakData.STATE__PREVIEW : StatTrakApps.STATE__INSTALLED });
      if (sent === "stopped") return { result: "stopped" };
      if (sent.end.status === StatTrakApps.STATUS__BAD_PACKAGE) {
        throw new AppsError("status", "StatTrak answered status 10 to END", { status: 10, reason: StatTrakData.BAD_BACKUP_TEXTS[sent.end.data[0]] || "bad backup" });
      }
      const out = { result: "done" };
      if (kind === StatTrakData.KIND__RESTORE && !sent.end.lost) {
        out.written = sent.end.data[0];
        out.skipped = sent.end.data[1];
      }
      return out;
    });
  }

  restore(bytes, onProgress, onWaiting) { return this.transfer(StatTrakData.KIND__RESTORE, bytes, onProgress, onWaiting); }
  sendStartPicture(bytes, onProgress) { return this.transfer(StatTrakData.KIND__START_PIC, bytes, onProgress); }
  sendPreview(bytes) { return this.transfer(StatTrakData.KIND__PREVIEW, bytes); }

  /* Everything for a backup (or for the compare after a restore). onProgress(text). Returns
     { firmware, info, entries, apps, deviceName, startPicture, log }:
     apps = LIST + settings + saved values; deviceName = the part after "StatTrak | " ("" none);
     startPicture = null or { show, pixels }. */
  async readAll(options) {
    const o = options || {};
    const step = (text) => { if (o.onProgress) o.onProgress(text); };
    const info = await this.info();
    if (!info) throw new AppsError("old firmware", "The firmware of StatTrak has no backup");
    step("Reading the values");
    const entries = await this.entries(info.valueCount);
    step("Reading the apps");
    const list = await this.apps.list();
    const apps = [];
    for (const a of list) {
      const app = { id: a.id, name: a.name, builtin: a.builtin, hidden: a.hidden, version: a.version, settings: [], values: [] };
      for (let k = 0; k < a.settingCount; k++) app.settings.push(await this.apps.getSetting(a.id, k));
      if (!a.builtin) app.values = (await this.appValues(a.id)) || [];
      apps.push(app);
    }
    const name = await this.apps.deviceName();
    let startPicture = null;
    if (info.picture !== StatTrakData.PIC_STATE__NONE) {
      step("Reading the start picture");
      const pixels = await this.readStartPicture();
      if (pixels) startPicture = { show: info.picture === StatTrakData.PIC_STATE__ON, pixels };
    }
    let log = [];
    if (o.log !== false && info.logCount > 0) {
      step("Reading the device log");
      log = await this.deviceLog();
    }
    return { firmware: o.firmware || "", info, entries, apps, deviceName: name ? name.part : "", startPicture, log };
  }
}
