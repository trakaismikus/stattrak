/* StatTrak apps: the app commands over Bluetooth (docs/APPS.md section 6) and the check of a
 * .stapp file. No page here, thus the tests can use this file directly.
 *
 * The commands go over the connection of js/ble.js (BleOta.sendCustomCmd: characteristic
 * 0x8023, 20 bytes, CRC-16). They work only while the device shows Settings > "Apps: SYNC".
 * This file sends no firmware update command: only the app commands 0x0110 .. 0x011C and
 * "get version" (0x0001), which only reads.
 */

const StappFile = (() => {
  const MAGIC = "STAP";
  const HEADER_SIZE = 48;
  const PICTURE_OFFSET = 48;
  const PICTURE_SIZE = 240;
  const HANDLERS_OFFSET = 288;
  const HANDLER_COUNT = { 1: 8, 2: 10, 3: 12 };   // format 1: open .. close; 2: also down1, down2; 3: also kill, death
  const MIN_SIZE = HANDLERS_OFFSET + 2 * HANDLER_COUNT[1] + 4;   // 308: header, picture, handler table, CRC
  const MAX_SIZE = 4096;
  const FORMAT_VERSION = 3;            // the newest format that this page knows
  const PERMISSIONS = ["counters", "sound", "data", "awake", "background"];   // format 3: data, awake, background

  // CRC-32 (IEEE), as zlib.crc32 and Stapp_crc32()
  const TABLE = (() => {
    const table = new Uint32Array(256);
    for (let i = 0; i < 256; i++) {
      let c = i;
      for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
      table[i] = c >>> 0;
    }
    return table;
  })();
  function crc32(bytes) {
    let crc = 0xFFFFFFFF;
    for (let i = 0; i < bytes.length; i++) crc = TABLE[(crc ^ bytes[i]) & 0xFF] ^ (crc >>> 8);
    return (crc ^ 0xFFFFFFFF) >>> 0;
  }

  function text(bytes, offset, length) {
    let out = "";
    for (let i = offset; i < offset + length && i < bytes.length && bytes[i] !== 0; i++) out += String.fromCharCode(bytes[i]);
    return out;
  }

  /* The settings block: for each setting a name (12), the value count, the default, the
     values (8 each). Returns null if it is not inside the file. */
  function settingsOf(bytes, view) {
    const count = bytes[34];
    let p = view.getUint16(38, true);
    const settings = [];
    for (let i = 0; i < count; i++) {
      if (p + 14 > bytes.length - 4) return null;
      const valueCount = bytes[p + 12];
      if (valueCount < 2 || valueCount > 6 || p + 14 + valueCount * 8 > bytes.length - 4) return null;
      const values = [];
      for (let v = 0; v < valueCount; v++) values.push(text(bytes, p + 14 + v * 8, 8));
      settings.push({ name: text(bytes, p, 12), values, default: bytes[p + 13] });
      p += 14 + valueCount * 8;
    }
    return settings;
  }

  /* The checks of the page: size, magic, format, total size, CRC. The device does the full
     check (docs/APPS.md section 3.1) after the transfer.
     Returns { ok, problem, id, idHex, name, version, size, permissions, picture, settings, engine,
     format }.
     problem: "not an app" | "too large" | "damaged" | "newer format" */
  function inspect(bytes) {
    const result = { ok: false, problem: "" };
    if (!ArrayBuffer.isView(bytes) || bytes.length < MIN_SIZE || text(bytes, 0, 4) !== MAGIC) {
      result.problem = bytes && bytes.length > MAX_SIZE && text(bytes, 0, 4) === MAGIC ? "too large" : "not an app";
      return result;
    }
    if (bytes.length > MAX_SIZE) {
      result.problem = "too large";
      return result;
    }
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    if (bytes[4] > FORMAT_VERSION) {
      result.problem = "newer format";
      return result;
    }
    // Format 2 has 2 more handlers: the data area starts at 308, the smallest package is 312 bytes
    if (!HANDLER_COUNT[bytes[4]] || bytes.length < HANDLERS_OFFSET + 2 * HANDLER_COUNT[bytes[4]] + 4 ||
        view.getUint16(14, true) !== bytes.length || crc32(bytes.subarray(0, bytes.length - 4)) !== view.getUint32(bytes.length - 4, true)) {
      result.problem = "damaged";
      return result;
    }
    const settings = settingsOf(bytes, view);
    const id = view.getUint32(8, true);
    const name = text(bytes, 16, 16);
    if (!settings || !name || id <= 255 || id === 0xFFFFFFFF) {
      result.problem = "damaged";
      return result;
    }
    const permissionBits = view.getUint16(6, true);
    return {
      ok: true,
      problem: "",
      id,
      idHex: id.toString(16).padStart(8, "0"),
      name,
      version: view.getUint16(12, true),
      size: bytes.length,
      engine: bytes[5],
      format: bytes[4],
      permissions: PERMISSIONS.filter((_, bit) => permissionBits & (1 << bit)),
      picture: bytes.slice(PICTURE_OFFSET, PICTURE_OFFSET + PICTURE_SIZE),
      settings,
    };
  }

  return { inspect, crc32, MAX_SIZE, HEADER_SIZE, PICTURE_SIZE, FORMAT_VERSION };
})();


/* An error of an app command. kind:
     "old firmware"  the firmware has no app commands (status 1)
     "not in sync"   the device does not show "Apps: SYNC" (status 3)
     "waiting"       the device asks "INSTALL <NAME>?" (status 11)
     "status"        an other status (see .status, and .reason for status 10)
     "no answer"     no answer, or the connection is lost
     "bad answer"    an answer that is not correct
     "file"          the page does not send this file (.problem: see StappFile.inspect; "newer
                     format" = the firmware of the device cannot read it)
     "name"          a Bluetooth name that is not correct (.message says why) */
class AppsError extends Error {
  constructor(kind, message, details) {
    super(message);
    this.name = "AppsError";
    this.kind = kind;
    Object.assign(this, details || {});
  }
}

class StatTrakApps {

  // Commands (main/ble_commands.h)
  static CMD__INFO       = 0x0110;
  static CMD__LIST       = 0x0111;
  static CMD__NAME       = 0x0112;
  static CMD__BEGIN      = 0x0113;
  static CMD__DATA       = 0x0114;
  static CMD__END        = 0x0115;
  static CMD__STATUS     = 0x0116;
  static CMD__DELETE     = 0x0117;
  static CMD__SET_ORDER  = 0x0118;
  static CMD__SET_HIDDEN = 0x0119;
  static CMD__SETTING    = 0x011A;
  static CMD__ASK        = 0x011B;
  static CMD__DEVICE_NAME = 0x011C;

  // Status of an answer (main/ble_commands.h)
  static STATUS__OK              = 0;
  static STATUS__UNKNOWN         = 1;
  static STATUS__ERROR           = 2;
  static STATUS__NOT_IN_APP_SYNC = 3;
  static STATUS__INDEX           = 4;
  static STATUS__NO_SPACE        = 5;
  static STATUS__TOO_LARGE       = 6;
  static STATUS__TOO_MANY        = 7;
  static STATUS__OFFSET          = 8;
  static STATUS__BAD_CRC         = 9;
  static STATUS__BAD_PACKAGE     = 10;
  static STATUS__WAITING         = 11;
  static STATUS__BUILTIN         = 12;
  static STATUS__LAST_SHOWN      = 13;

  // Install state of STATUS (main/views/app_sync_view.h)
  static STATE__IDLE      = 0;
  static STATE__RECEIVING = 1;
  static STATE__WAITING   = 2;
  static STATE__INSTALLED = 3;
  static STATE__REFUSED   = 4;
  static STATE__TIMEOUT   = 5;

  // Flags of LIST (main/apps.h)
  static FLAG__HIDDEN   = 0x01;
  static FLAG__BUILTIN  = 0x02;
  static FLAG__SETTINGS = 0x04;
  static FLAG__ERROR    = 0x08;

  static DATA_BYTES = 14;          // package bytes in one DATA command
  static ORDER_IDS_PER_PART = 3;
  static NAME_PART = 13;
  static MAX_LIST = 11;            // APPS_LIST_MAX: 3 built-in + 8 installed

  // Install: tries and waits
  static NO_ANSWER_TRIES = 3;      // a DATA command with no answer is sent again
  static MAX_RESENDS = 40;         // "wrong offset" answers in one install
  static DRAIN_MS = 1000;          // after a lost or a late answer: the time for late answers to arrive
  static STATUS_POLL_MS = 500;
  static QUESTION_WAIT_MS = 40000; // the device gives up after 30 s (state 5)

  // The Bluetooth name part (DEVICE_NAME): 1 to 12 characters 0x20 .. 0x7E, no space at the start or
  // the end
  static DEFAULT_NAME = "StatTrak";
  static NAME_PREFIX = "StatTrak | ";   // The app sync name is NAME_PREFIX + the part of the person
  static NAME_MAX = 12;               // The part (main/apps.h APPS_DEVICE_NAME_MAX)

  // The 128-bit service UUID that the device advertises (components/app_ble/app_ble.c). The Apps
  // tab finds a device with any Bluetooth name by it.
  static ADVERTISED_UUID = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";

  // Stapp_resultText(), in the order of stapp_result_t
  static RESULT_TEXTS = ["ok", "bad size", "bad magic", "bad version", "bad crc", "bad header", "bad layout",
    "bad settings", "bad strings", "bad bitmaps", "bad code", "bad global", "bad sys", "bad handler", "stack",
    "call depth", "too many steps", "bad arg", "bad index"];

  constructor(ota) {
    this.ota = ota;
    this.queue = Promise.resolve();
  }

  /* One command at a time: two commands at the same time would both read the first answer. */
  _serial(task) {
    const run = this.queue.then(task, task);
    this.queue = run.catch(() => {});
    return run;
  }

  /* Sends one command. Returns { status, data } (data: 13 bytes). Throws AppsError for
     no answer, a bad answer, and the status 1, 3 and 11. */
  async _command(cmd, data) {
    const payload = new Uint8Array(16);
    payload.set(data || []);
    let response;
    try {
      response = await this.ota.sendCustomCmd(cmd, payload);
    } catch (error) {
      throw new AppsError("no answer", "No answer from StatTrak: " + error.message);
    }
    // sendCustomCmd checked the CRC, the ACK and the command number of the answer
    if (!response || response.length < 14) throw new AppsError("bad answer", "The answer of StatTrak is not correct");
    const status = response[0];
    const answer = response.slice(1, 14);
    if (status === StatTrakApps.STATUS__UNKNOWN) throw new AppsError("old firmware", "The firmware of StatTrak has no apps");
    if (status === StatTrakApps.STATUS__NOT_IN_APP_SYNC) throw new AppsError("not in sync", "StatTrak does not show Apps: SYNC");
    if (status === StatTrakApps.STATUS__WAITING) throw new AppsError("waiting", "StatTrak asks INSTALL first");
    return { status, data: answer };
  }

  /* A command that must answer OK */
  async _ok(cmd, data) {
    const answer = await this._command(cmd, data);
    if (answer.status !== StatTrakApps.STATUS__OK) {
      throw new AppsError("status", `StatTrak answered status ${answer.status} to 0x${cmd.toString(16)}`, { status: answer.status });
    }
    return answer.data;
  }

  static _id(id) {
    const bytes = new Uint8Array(4);
    new DataView(bytes.buffer).setUint32(0, id >>> 0, true);
    return bytes;
  }

  static _u16(data, offset) {
    return data[offset] | (data[offset + 1] << 8);
  }

  static _u32(data, offset) {
    return (data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24)) >>> 0;
  }

  static _text(data, length) {
    let out = "";
    for (let i = 0; i < length && data[i] !== 0; i++) {
      if (data[i] < 0x20 || data[i] > 0x7E) throw new AppsError("bad answer", "A name with a character that is not correct");
      out += String.fromCharCode(data[i]);
    }
    return out;
  }

  /* INFO: { engine, format, free, maxPackage, count, maxApps } */
  info() {
    return this._serial(async () => {
      const d = await this._ok(StatTrakApps.CMD__INFO);
      const info = {
        engine: d[0], format: d[1], free: StatTrakApps._u16(d, 2), maxPackage: StatTrakApps._u16(d, 4),
        count: d[6], maxApps: d[7],
      };
      if (info.engine === 0 || info.maxPackage === 0 || info.count > info.maxApps) {
        throw new AppsError("bad answer", "The answer to INFO is not correct");
      }
      return info;
    });
  }

  /* The app list in menu order (without SETTINGS):
     [{ id, version, size, flags, hidden, builtin, hasSettings, error, settingCount, name }] */
  list() {
    return this._serial(async () => {
      const apps = [];
      for (let index = 0; index <= StatTrakApps.MAX_LIST; index++) {
        const answer = await this._command(StatTrakApps.CMD__LIST, [index]);
        if (answer.status === StatTrakApps.STATUS__INDEX) return apps;
        if (answer.status !== StatTrakApps.STATUS__OK) {
          throw new AppsError("status", `StatTrak answered status ${answer.status} to LIST`, { status: answer.status });
        }
        const d = answer.data;
        const flags = d[8];
        const app = {
          id: StatTrakApps._u32(d, 0),
          version: StatTrakApps._u16(d, 4),
          size: StatTrakApps._u16(d, 6),
          flags,
          hidden: !!(flags & StatTrakApps.FLAG__HIDDEN),
          builtin: !!(flags & StatTrakApps.FLAG__BUILTIN),
          hasSettings: !!(flags & StatTrakApps.FLAG__SETTINGS),
          error: !!(flags & StatTrakApps.FLAG__ERROR),
          settingCount: d[9],
          name: StatTrakApps._text(d.subarray(10), 3),
        };
        if ((flags & ~0x0F) || app.settingCount > 4 || app.size > 4096 || (!app.builtin && app.size === 0) ||
            apps.some((other) => other.id === app.id)) {
          throw new AppsError("bad answer", "The answer to LIST is not correct");
        }
        if (app.name.length === 3) app.name = await this._name(app.id);
        apps.push(app);
      }
      throw new AppsError("bad answer", "The app list is too long");
    });
  }

  /* NAME, in parts of 13 letters (a name has 14 at most) */
  async _name(id) {
    let name = "";
    for (let part = 0; part < 2; part++) {
      const d = await this._ok(StatTrakApps.CMD__NAME, [...StatTrakApps._id(id), part]);
      const piece = StatTrakApps._text(d, StatTrakApps.NAME_PART);
      name += piece;
      if (piece.length < StatTrakApps.NAME_PART) break;
    }
    if (!name) throw new AppsError("bad answer", "An app without a name");
    return name;
  }

  /* Installs a package (docs/APPS.md section 6): BEGIN, ASK, the device asks the person
     "INSTALL <NAME>?", then after "yes" DATA and END.
     onWaitingForDevice() when the device asks; onProgress(fraction 0..1) during the transfer,
     which starts after "yes". Returns "installed", "refused" (the person said no), "timeout"
     (no answer in 30 s) or "stopped" (the device stopped the transfer: a hold of button 1, or
     10 s without data). Throws AppsError: "file" (not sent; problem "newer format" = update
     the firmware first), "status" (5 no space, 6 too large, 7 too many apps, 9 damaged, 10
     refused by the check with .reason, 2 refused). */
  install(bytes, onProgress, onWaitingForDevice) {
    return this._serial(async () => {
      const file = StappFile.inspect(bytes);
      if (!file.ok) throw new AppsError("file", "Not a correct .stapp file: " + file.problem, { problem: file.problem });
      // A firmware that reads only an older package format refuses the package: do not send it
      const info = await this._ok(StatTrakApps.CMD__INFO);
      if (file.format > info[1] || file.engine > info[0]) {
        throw new AppsError("file", "Update the firmware first: the app needs app engine " + file.engine + " and package format " + file.format, { problem: "newer format" });
      }
      const size = bytes.length;
      const crc = StappFile.crc32(bytes);
      const begin = new Uint8Array(10);
      const view = new DataView(begin.buffer);
      view.setUint32(0, file.id, true);
      view.setUint16(4, size, true);
      view.setUint32(6, crc, true);
      await this._ok(StatTrakApps.CMD__BEGIN, begin);
      // The name field of the package (offset 16): the device asks "INSTALL <NAME>?"
      await this._ok(StatTrakApps.CMD__ASK, bytes.subarray(16, 32));

      // The person answers on the device
      if (onWaitingForDevice) onWaitingForDevice();
      const answer = await this._waitForPerson(StatTrakApps.STATE__RECEIVING);
      if (answer !== "yes") return answer;

      const sent = await this._sendParts(bytes, onProgress);
      if (sent === "stopped") return "stopped";
      if (sent.end.status === StatTrakApps.STATUS__BAD_PACKAGE) {
        const reason = StatTrakApps.RESULT_TEXTS[sent.end.data[0]] || "bad package";
        throw new AppsError("status", "StatTrak answered status 10 to END", { status: sent.end.status, reason });
      }
      return "installed";
    });
  }

  /* After a question on the device: STATUS until the person answers. `yes` = the state of "yes"
     (1 receiving: send the data; 3 done: the device did it). Returns "yes", "refused" (the person
     said no) or "timeout" (no answer in 30 s). */
  async _waitForPerson(yes) {
    const start = Date.now();
    for (;;) {
      await new Promise((resolve) => setTimeout(resolve, StatTrakApps.STATUS_POLL_MS));
      const state = await this._state();
      if (state === yes) return "yes";
      if (state === StatTrakApps.STATE__REFUSED) return "refused";
      if (state === StatTrakApps.STATE__TIMEOUT) return "timeout";
      if (state !== StatTrakApps.STATE__WAITING) throw new AppsError("bad answer", "The state is not correct: " + state);
      if (Date.now() - start > StatTrakApps.QUESTION_WAIT_MS) return "timeout";
    }
  }

  /* The DATA and END steps of a transfer: a package (install) or a transfer of docs/DATA.md 5.1.
     options.sparse: send only the parts that are not all 0, and take each one that the device
     answers with OK (the preview frame: any order, missing parts are 0).
     options.doneState: the STATUS state after a good END (default 3), for an END whose answer was
     lost. Returns { end } (the END answer: status 0, or 10 with the reason in end.data[0]) or
     "stopped" (the device stopped the transfer: a hold of button 1, or 10 s without data). Throws
     AppsError for an other status. */
  async _sendParts(bytes, onProgress, options) {
    const size = bytes.length;
    const sparse = !!(options && options.sparse);
    const doneState = options && options.doneState !== undefined ? options.doneState : StatTrakApps.STATE__INSTALLED;
    const step = StatTrakApps.DATA_BYTES;
    // The next part to send from `offset` (sparse: the parts that are all 0 stay out)
    const next = (offset) => {
      if (!sparse) return offset;
      while (offset < size && bytes.subarray(offset, offset + step).every((b) => b === 0)) offset += step;
      return offset;
    };
    let offset = 0;
    let resends = 0;
    let noAnswer = 0;
    const resend = (expected) => {
      if (expected > size || ++resends > StatTrakApps.MAX_RESENDS) {
        throw new AppsError("bad answer", "The transfer does not go forward");
      }
      return expected;
    };
    /* After a lost answer, or an answer that belongs to an earlier command: wait until the
       late answers are in. Else each next command reads the answer of the command before it. */
    const drain = () => new Promise((resolve) => setTimeout(resolve, StatTrakApps.DRAIN_MS));
    /* Status 2 to DATA or END and STATUS 0: the device stopped the transfer (a hold of
       button 1, or 10 s without data) */
    const refused = async (cmd, status) => {
      if (status === StatTrakApps.STATUS__ERROR && (await this._state()) === StatTrakApps.STATE__IDLE) return "stopped";
      throw new AppsError("status", `StatTrak answered status ${status} to ${cmd}`, { status });
    };
    if (onProgress) onProgress(0);
    for (;;) {
      while ((offset = next(offset)) < size) {
        const data = new Uint8Array(16);
        data[0] = offset & 0xFF;
        data[1] = offset >> 8;
        data.set(bytes.subarray(offset, offset + step), 2);
        let answer;
        try {
          answer = await this._command(StatTrakApps.CMD__DATA, data);
        } catch (error) {
          // A lost answer: the same data again. The device answers "wrong offset" if it has it.
          if (error.kind === "no answer" && ++noAnswer < StatTrakApps.NO_ANSWER_TRIES && this.ota.isConnected()) {
            await drain();
            continue;
          }
          throw error;
        }
        noAnswer = 0;
        const expected = StatTrakApps._u16(answer.data, 0);
        if (answer.status === StatTrakApps.STATUS__OK) {
          const after = Math.min(offset + step, size);
          if (!sparse && expected !== after) {
            // The answer of an earlier DATA command: the answer to this one comes later
            await drain();
            offset = resend(Math.min(expected, offset));
          } else {
            offset = after;
          }
        } else if (answer.status === StatTrakApps.STATUS__OFFSET && !sparse) {
          offset = resend(expected);
        } else {
          return refused("DATA", answer.status);
        }
        if (onProgress) onProgress(offset / size);
      }

      // END checks the data and uses it: status 0 = done
      let end;
      try {
        end = await this._command(StatTrakApps.CMD__END);
      } catch (error) {
        // END does the work: if only its answer was lost, STATUS says done
        if (error.kind === "no answer" && (await this._state()) === doneState) {
          return { end: { status: StatTrakApps.STATUS__OK, data: new Uint8Array(13), lost: true } };
        }
        throw error;
      }
      if (end.status === StatTrakApps.STATUS__OK || end.status === StatTrakApps.STATUS__BAD_PACKAGE) return { end };
      if (end.status === StatTrakApps.STATUS__OFFSET && !sparse) {
        offset = resend(StatTrakApps._u16(end.data, 0));
        continue;
      }
      return refused("END", end.status);
    }
  }

  /* STATUS: the install state (STATE__*) */
  status() {
    return this._serial(() => this._state());
  }

  async _state() {
    return (await this._ok(StatTrakApps.CMD__STATUS))[0];
  }

  /* The problem of the name part (after "StatTrak | ") as a text for the person, or "" for a good
     part ("" is good too: back to "StatTrak"). The rules of Apps_isDeviceNameValid() (main/apps.c). */
  static nameProblem(name) {
    if (name === "") return "";
    if (name.length > StatTrakApps.NAME_MAX) return `At most ${StatTrakApps.NAME_MAX} characters.`;
    if (/[^\x20-\x7E]/.test(name)) return "Use only A-Z, a-z, 0-9, spaces and the signs of an English keyboard.";
    if (name[0] === " " || name[name.length - 1] === " ") return "No space at the start or at the end.";
    return "";
  }

  /* DEVICE_NAME, read: { custom, part, name }. custom = the person chose a part; part = the text
     after "StatTrak | "; name = the Bluetooth name in "Apps: SYNC" ("StatTrak" when there is no
     part). null = a firmware without it. */
  deviceName() {
    return this._serial(async () => {
      try {
        return StatTrakApps._nameAnswer(await this._ok(StatTrakApps.CMD__DEVICE_NAME, [0]));
      } catch (error) {
        if (error.kind === "old firmware") return null;
        throw error;
      }
    });
  }

  /* DEVICE_NAME, write the part after "StatTrak | ". "" = back to "StatTrak". The device uses it
     from the next start of "Apps: SYNC". Returns { custom, part, name }. */
  setDeviceName(name) {
    return this._serial(async () => {
      const problem = StatTrakApps.nameProblem(name);
      if (problem) throw new AppsError("name", problem);
      const data = new Uint8Array(1 + StatTrakApps.NAME_MAX);
      data[0] = 1;
      for (let i = 0; i < name.length; i++) data[1 + i] = name.charCodeAt(i);
      const answer = StatTrakApps._nameAnswer(await this._ok(StatTrakApps.CMD__DEVICE_NAME, data));
      if (answer.part !== name || answer.custom !== !!name) {
        throw new AppsError("bad answer", "The name did not change");
      }
      return answer;
    });
  }

  static _nameAnswer(d) {
    const part = StatTrakApps._text(d.subarray(1), StatTrakApps.NAME_MAX);
    if (d[0] > 1 || (d[0] === 1) !== (part !== "")) {
      throw new AppsError("bad answer", "The answer to DEVICE_NAME is not correct");
    }
    const name = part ? StatTrakApps.NAME_PREFIX + part : StatTrakApps.DEFAULT_NAME;
    return { custom: d[0] === 1, part, name };
  }

  /* The device chooser of the Apps tab (Web Bluetooth): a name that starts with "StatTrak"
     ("StatTrak" and "StatTrak | <part>"), or the service UUID that it advertises (in the scan
     response for a long name). A browser that refuses the UUID filter (a TypeError, e.g. a UUID on
     its block list) gets the name filter only. */
  static async pair() {
    const optionalServices = [BleOta.OTA_SERVICE_UUID];
    try {
      return await BleOta.create({
        filters: [{ namePrefix: StatTrakApps.DEFAULT_NAME }, { services: [StatTrakApps.ADVERTISED_UUID] }],
        optionalServices,
      });
    } catch (error) {
      if (!(error instanceof TypeError)) throw error;
      return BleOta.create({ filters: [{ namePrefix: StatTrakApps.DEFAULT_NAME }], optionalServices });
    }
  }

  delete(id) {
    return this._serial(() => this._ok(StatTrakApps.CMD__DELETE, StatTrakApps._id(id)));
  }

  /* The full menu order: the ids of all apps of list(), each one time */
  setOrder(ids) {
    return this._serial(async () => {
      const count = ids.length;
      const parts = Math.ceil(count / StatTrakApps.ORDER_IDS_PER_PART);
      for (let part = 0; part < parts; part++) {
        const data = new Uint8Array(14);
        data[0] = count;
        for (let i = 0; i < StatTrakApps.ORDER_IDS_PER_PART; i++) {
          const id = ids[part * StatTrakApps.ORDER_IDS_PER_PART + i];
          if (id !== undefined) data.set(StatTrakApps._id(id), 1 + 4 * i);
        }
        data[13] = part;
        await this._ok(StatTrakApps.CMD__SET_ORDER, data);
      }
    });
  }

  setHidden(id, hidden) {
    return this._serial(() => this._ok(StatTrakApps.CMD__SET_HIDDEN, [...StatTrakApps._id(id), hidden ? 1 : 0]));
  }

  getSetting(id, setting) {
    return this._serial(async () => (await this._ok(StatTrakApps.CMD__SETTING, [...StatTrakApps._id(id), setting, 0xFF]))[0]);
  }

  setSetting(id, setting, value) {
    return this._serial(async () => {
      const answer = (await this._ok(StatTrakApps.CMD__SETTING, [...StatTrakApps._id(id), setting, value]))[0];
      if (answer !== value) throw new AppsError("bad answer", "The setting did not change");
      return answer;
    });
  }
}
