/* StatTrak apps: the app commands over Bluetooth (docs/APPS.md section 6) and the check of a
 * .stapp file. No page here, thus the tests can use this file directly.
 *
 * The commands go over the connection of js/ble.js (BleOta.sendCustomCmd: characteristic
 * 0x8023, 20 bytes, CRC-16). They work only while the device shows Settings > "Apps: SYNC".
 * This file sends no firmware update command: only the app commands 0x0110 .. 0x011A and
 * "get version" (0x0001), which only reads.
 */

const StappFile = (() => {
  const MAGIC = "STAP";
  const HEADER_SIZE = 48;
  const PICTURE_OFFSET = 48;
  const PICTURE_SIZE = 240;
  const MIN_SIZE = 288 + 16 + 4;       // header, picture, handler table, CRC
  const MAX_SIZE = 4096;
  const FORMAT_VERSION = 1;
  const PERMISSIONS = ["counters", "sound"];

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
     Returns { ok, problem, id, idHex, name, version, size, permissions, picture, settings, engine }.
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
    if (bytes[4] !== FORMAT_VERSION) {
      result.problem = "newer format";
      return result;
    }
    if (view.getUint16(14, true) !== bytes.length || crc32(bytes.subarray(0, bytes.length - 4)) !== view.getUint32(bytes.length - 4, true)) {
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
      permissions: PERMISSIONS.filter((_, bit) => permissionBits & (1 << bit)),
      picture: bytes.slice(PICTURE_OFFSET, PICTURE_OFFSET + PICTURE_SIZE),
      settings,
    };
  }

  return { inspect, crc32, MAX_SIZE, HEADER_SIZE, PICTURE_SIZE };
})();


/* An error of an app command. kind:
     "old firmware"  the firmware has no app commands (status 1)
     "not in sync"   the device does not show "Apps: SYNC" (status 3)
     "waiting"       the device asks "INSTALL <NAME>?" (status 11)
     "status"        an other status (see .status, and .reason for status 10)
     "no answer"     no answer, or the connection is lost
     "bad answer"    an answer that is not correct */
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

  // Stapp_resultText(), in the order of stapp_result_t
  static RESULT_TEXTS = ["ok", "bad size", "bad magic", "bad version", "bad crc", "bad header", "bad layout",
    "bad settings", "bad strings", "bad bitmaps", "bad code", "bad global", "bad sys", "bad handler", "stack",
    "call depth", "too many steps", "bad arg"];

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

  /* Installs a package. The device asks the person "INSTALL <NAME>?".
     onProgress(fraction 0..1) during the transfer; onWaitingForDevice() when the device asks.
     Returns "installed", "refused" or "timeout". Throws AppsError (status 5 no space,
     6 too large, 7 too many apps, 9 damaged, 10 refused by the check with .reason). */
  install(bytes, onProgress, onWaitingForDevice) {
    return this._serial(async () => {
      const file = StappFile.inspect(bytes);
      if (!file.ok) throw new AppsError("file", "Not a correct .stapp file: " + file.problem, { problem: file.problem });
      const size = bytes.length;
      const crc = StappFile.crc32(bytes);
      const begin = new Uint8Array(10);
      const view = new DataView(begin.buffer);
      view.setUint32(0, file.id, true);
      view.setUint16(4, size, true);
      view.setUint32(6, crc, true);
      await this._ok(StatTrakApps.CMD__BEGIN, begin);

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
      if (onProgress) onProgress(0);
      for (;;) {
        while (offset < size) {
          const data = new Uint8Array(16);
          data[0] = offset & 0xFF;
          data[1] = offset >> 8;
          data.set(bytes.subarray(offset, offset + StatTrakApps.DATA_BYTES), 2);
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
            const next = Math.min(offset + StatTrakApps.DATA_BYTES, size);
            if (expected !== next) {
              // The answer of an earlier DATA command: the answer to this one comes later
              await drain();
              offset = resend(Math.min(expected, offset));
            } else {
              offset = next;
            }
          } else if (answer.status === StatTrakApps.STATUS__OFFSET) {
            offset = resend(expected);
          } else {
            throw new AppsError("status", `StatTrak answered status ${answer.status} to DATA`, { status: answer.status });
          }
          if (onProgress) onProgress(offset / size);
        }

        const end = await this._command(StatTrakApps.CMD__END);
        if (end.status === StatTrakApps.STATUS__OK) break;
        if (end.status === StatTrakApps.STATUS__OFFSET) {
          offset = resend(StatTrakApps._u16(end.data, 0));
          continue;
        }
        const details = { status: end.status };
        if (end.status === StatTrakApps.STATUS__BAD_PACKAGE) details.reason = StatTrakApps.RESULT_TEXTS[end.data[0]] || "bad package";
        throw new AppsError("status", `StatTrak answered status ${end.status} to END`, details);
      }

      // The device shows "INSTALL <NAME>?" and waits for the person
      if (onWaitingForDevice) onWaitingForDevice();
      const start = Date.now();
      for (;;) {
        await new Promise((resolve) => setTimeout(resolve, StatTrakApps.STATUS_POLL_MS));
        const state = (await this._ok(StatTrakApps.CMD__STATUS))[0];
        if (state === StatTrakApps.STATE__INSTALLED) return "installed";
        if (state === StatTrakApps.STATE__REFUSED) return "refused";
        if (state === StatTrakApps.STATE__TIMEOUT) return "timeout";
        if (state !== StatTrakApps.STATE__WAITING) throw new AppsError("bad answer", "The install state is not correct: " + state);
        if (Date.now() - start > StatTrakApps.QUESTION_WAIT_MS) return "timeout";
      }
    });
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
