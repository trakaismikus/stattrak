/* A stand-in for a StatTrak device, and for the Web Bluetooth API of the browser.
 *
 * The tests use it in place of a real device. It runs in Node.js and in a browser.
 * The packet code is a line-by-line port of the firmware in this repository:
 *   ../components/ble_ota/ble_ota.c   _writeCommand, _writeFw, _writeCustom, _notify
 *   ../components/ble_ota/ota.c       the update task, _customCallback
 *   ../main/main.c                    restart after a good transfer, "Verifying…" at the start
 * `legacy: true` gives the packet code of firmware 1.0.1 (no check of the sector CRC,
 * and the "accept" command changes the display also when no firmware waits).
 * `apps: true` gives the app commands 0x0110 .. 0x011C (docs/APPS.md section 6), a port of
 *   ../main/ble_commands.c, ../main/apps.c and ../main/views/app_sync_view.c, with the buttons
 *   of "Apps: SYNC" (buttonDown, longPress, press, holdButton1, holdButton2).
 * requestDevice() uses the filters as Chrome does: the name, or the 128-bit service UUID that
 *   the device advertises (components/app_ble/app_ble.c).
 * `userCommands: true` (firmware 1.1.0): user commands get an answer, status 1 for an
 *   unknown command. Without it (firmware 1.0.1) a user command gets no answer.
 * `data: true` (firmware 1.2.0-b2) gives the commands 0x0120 .. 0x012A of docs/DATA.md: the flash
 *   values, the saved values of the apps, the transfers (restore, start picture, preview frame),
 *   the app data, the device log. Test hooks: setAppData, setDeviceLog, setStartPicture,
 *   setConfigValue, configValue; holdButton2 / holdButton1 answer the questions of the device.
 *
 * This is a model. It is not a proof that a real device does the same.
 */

(function (global) {
  const SERVICE_UUID = 0x8018;
  const RECV_FW_UUID = 0x8020;
  const OTA_BAR_UUID = 0x8021;
  const COMMAND_UUID = 0x8022;
  const CUSTOM_UUID = 0x8023;

  const CMD_START = 1, CMD_STOP = 2, CMD_ACK = 3;
  const ACK_SUCCESS = 0, ACK_CRC_ERR = 1, ACK_SECTOR_IDX_ERR = 2, ACK_PAYLOAD_LEN_ERR = 3;
  const CUSTOM_GET_VERSION = 1, CUSTOM_GET_UID = 2, CUSTOM_ACCEPT_FW = 3, CUSTOM_REJECT_FW = 4, CUSTOM_REBOOT = 5;

  const USER_CMD_FIRST = 0x0100;

  const BLOCK = 4096;
  const BUFFER = BLOCK + 2;
  const CMD_SIZE = 20;

  // crc16_ccitt() of the firmware
  function crc16(bytes) {
    let crc = 0;
    for (let i = 0; i < bytes.length; i++) {
      crc ^= bytes[i] << 8;
      for (let bit = 0; bit < 8; bit++) {
        crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) : (crc << 1);
        crc &= 0xFFFF;
      }
    }
    return crc;
  }

  function toBytes(data) {
    if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength));
    return new Uint8Array(data.slice(0));
  }

  function putText(bytes, offset, text) {
    for (let i = 0; i < text.length; i++) bytes[offset + i] = text.charCodeAt(i);
  }

  // The stand-in for the checksum of a real firmware file: the last byte
  function checksum(bytes) {
    let sum = 0xEF;
    for (let i = 0; i < bytes.length - 1; i++) sum ^= bytes[i];
    return sum;
  }

  class FakeStatTrak {
    constructor(options) {
      this.options = Object.assign({
        name: "StatTrak",
        version: "1.0.1",                    // the firmware that runs now
        uid: [0x34, 0xB7, 0xDA, 0x01, 0x02, 0x03],
        legacy: false,                       // true: the packet code of firmware 1.0.1
        slotSize: 1472 * 1024,               // size of a firmware slot
        restartMs: 4000,                     // "Restarting in 3 seconds…" after a good transfer
        bootMs: 1000,                        // from the restart to the first screen
        connectMs: 20,                       // time to make a connection
        connectFailMs: 500,                  // time until a connection try stops when the device is not there
        staysOffAfterRestart: false,         // the power switches off at the restart; a person must switch it on
        newFirmwareCrashes: false,           // the new firmware does not start; the old one comes back
        damageSector: {},                    // { sector number: how many times the data arrives damaged }
        loseAckOfSector: null,               // the answer for this sector does not arrive
        disconnectAtSector: null,            // the connection is lost at the end of this sector
        loseStartReply: false,               // the answer to "start" does not arrive
        loseAcceptReplies: 0,                // how many answers "yes" to "confirm" do not arrive (the device does the command)
        ignoreAccept: false,                 // the device does not do and does not answer "confirm"
        newFirmwareIgnoresAccept: false,     // the same, but only the new firmware after the restart
        waitsForConfirmation: false,         // at the start: a new firmware runs and shows "Verifying…"
        userCommands: false,                 // user commands (0x0100 and up) get an answer
        apps: false,                         // the app commands 0x0110 .. 0x011C
        appFormat: 2,                        // STAPP_FORMAT_VERSION: the newest package format of the firmware (INFO)
        installedApps: [],                   // packages (Uint8Array) installed at the start
        hiddenApps: [],                      // ids (numbers) hidden at the start
        appQuestionMs: 30000,                // APP_SYNC_QUESTION_MS: the time to answer "INSTALL <NAME>?"
        appDataMs: 10000,                    // APP_SYNC_DATA_MS: no DATA for this time = the transfer stopped
        deviceName: "",                      // the Bluetooth name that the person chose ("" = "StatTrak")
        uuidFilterBlocked: false,            // requestDevice() refuses the UUID filter (TypeError), as for a UUID on the block list
        appErrors: {},                       // { id: error count } at the start
        loseAppAnswers: {},                  // { command number: how many answers do not arrive }
        loseDataAnswerAt: null,              // DATA at this offset: the device uses it, the answer does not arrive
        refusePackage: null,                 // END: the package check fails with this reason (stapp_result_t)
        flashFails: false,                   // Apps_install: the flash write fails
        data: false,                         // the commands of docs/DATA.md (firmware 1.2.0-b2)
        configValues: {},                    // { key: value } of the flash values at the start
        appValues: {},                       // { id: [saved values] } at the start
        appData: {},                         // { id: { columns: [names], rows: [[values]] } } at the start
        deviceLog: [],                       // [{ reason, battery, flags, boot }] at the start, newest first
        startPicture: null,                  // 512 bytes (frame buffer format) at the start
        startPictureShow: true,              // the start picture shows at power-on
        freeFlash: 20000,                    // free NVS bytes (CONFIG_INFO)
      }, options || {});
      if (this.options.data) this.options.apps = true;
      if (this.options.apps) this.options.userCommands = true;

      this.version = this.options.version;
      this.screen = "update";                // update | accept | counter | off
      this.display = "Waiting...";
      this.pendingVerify = false;
      this.events = [];                      // what occurred, for the tests
      this.received = null;                  // the firmware bytes that the last transfer wrote
      this.startCount = 0;
      this.stopCount = 0;
      this.acceptCount = 0;
      this.timers = [];
      this.links = [];                       // all connections, also the ones that ended
      this.requests = [];                    // the options of each requestDevice() call

      if (this.options.waitsForConfirmation) {
        this.oldVersion = "1.0.1";
        this.pendingVerify = true;
        this.screen = "accept";
        this.display = "Verifying...";
      }

      this._resetUpdateCode();
      this._appsInit();
      this._makeWebDevice();
    }

    /* ---- Test helpers ---------------------------------------------------------------- */

    /* A firmware file that the page checks accept. `valid: false` gives a file that
       the device refuses after the transfer (as a real damaged file). */
    static makeFirmware(options) {
      const o = Object.assign({ size: 10000, version: "9.9.9", project: "stattrak", chipId: 0x000C, valid: true }, options || {});
      const bytes = new Uint8Array(o.size);
      for (let i = 0; i < bytes.length; i++) bytes[i] = (i * 31 + 7) & 0xFF;
      bytes.fill(0, 0, 0x70);
      bytes[0] = 0xE9;
      bytes[12] = o.chipId & 0xFF;
      bytes[13] = o.chipId >> 8;
      bytes.set([0x32, 0x54, 0xCD, 0xAB], 0x20);
      putText(bytes, 0x30, o.version);
      putText(bytes, 0x50, o.project);
      bytes[bytes.length - 1] = checksum(bytes) ^ (o.valid ? 0 : 0x55);
      return bytes;
    }

    advertising() {
      return this.screen === "update" || this.screen === "accept" || this.screen === "appsync";
    }

    /* A person switches the device on after it went off at the restart. */
    powerOn() {
      if (this.screen !== "off") return;
      this._boot();
    }

    /* A person switches the device off and on. A firmware that was not confirmed is lost. */
    powerCycle() {
      this._dropLink();
      this._boot();
    }

    /* A person opens the update screen from the settings. */
    enterUpdateMode() {
      if (this.screen !== "counter") return;
      this._resetUpdateCode();
      this.screen = "update";
      this.display = "Waiting...";
    }

    /* A person presses a button on the "Complete" screen. Bluetooth goes off. */
    pressOk() {
      if (this.screen !== "accept" || this.pendingVerify) return;
      this._dropLink();
      this.screen = "counter";
      this.display = "counter";
    }

    /* The connection is lost (the device is too far away). */
    loseConnection() {
      this._dropLink();
    }

    bluetooth() {
      const device = this;
      return {
        requestDevice: async function (options) {
          device.events.push("requestDevice");
          device.requests.push(JSON.parse(JSON.stringify(options || {})));
          const filters = (options && options.filters) || [];
          if (device.options.uuidFilterBlocked && filters.some((f) => f.services)) {
            throw new TypeError("Failed to execute 'requestDevice' on 'Bluetooth': UUID is blocklisted.");
          }
          // A filter matches when all its parts match: the name, the advertised services
          const name = device.bleName();
          const match = filters.some((f) => (f.name === undefined || f.name === name) &&
            (f.namePrefix === undefined || name.startsWith(f.namePrefix)) &&
            (f.services === undefined || f.services.every((uuid) => uuid === ADVERTISED_UUID)));
          if (!device.advertising() || !match) {
            const error = new Error("User cancelled the requestDevice() chooser.");
            error.name = "NotFoundError";
            throw error;
          }
          device.webDevice.name = name;
          return device.webDevice;
        },
      };
    }

    /* ---- The device side ------------------------------------------------------------- */

    _later(ms, fn) {
      const id = setTimeout(fn, ms);
      if (id && typeof id.unref === "function") id.unref(); // Node.js: do not keep a test run open
      this.timers.push(id);
      return id;
    }

    _resetUpdateCode() {
      // ble_ota.c
      this.otaInProgress = false;
      this.curSector = 0;
      this.curPacket = 0;
      this.fwLen = 0;
      this.fwBuf = new Uint8Array(BUFFER);
      this.fwBufOffset = 0;
      this.sectorError = false;
      // ota.c, the update task
      this.taskActive = false;
      this.taskBytes = [];
      this.taskTotal = 0;
    }

    _resetSector() {
      this.curPacket = 0;
      this.fwBufOffset = 0;
      this.sectorError = false;
    }

    _boot() {
      this._resetUpdateCode();
      if (this.newFirmware) {
        const image = this.newFirmware;
        this.newFirmware = null;
        if (this.options.newFirmwareCrashes) {
          // The bootloader goes back to the old firmware. Bluetooth is off in normal use.
          this.events.push("rollback");
          this.screen = "counter";
          this.display = "counter";
          return;
        }
        this.oldVersion = this.version;
        this.version = image.version;
        this.pendingVerify = true;
        this.screen = "accept";
        this.display = "Verifying...";
        this.events.push("boot new " + this.version);
        if (this.options.newFirmwareIgnoresAccept) this.options.ignoreAccept = true;
        return;
      }
      if (this.pendingVerify) {
        // The new firmware was not confirmed before this restart: the old firmware comes back
        this.pendingVerify = false;
        this.version = this.oldVersion;
        this.events.push("rollback");
      }
      this.screen = "counter";
      this.display = "counter";
    }

    _restartAfterGoodTransfer(image) {
      this.display = "Completed";
      this._later(this.options.restartMs, () => {
        this._dropLink();
        this.newFirmware = image;
        this.screen = "off";
        this.display = "";
        this.events.push("restart");
        if (!this.options.staysOffAfterRestart) this._later(this.options.bootMs, () => this._boot());
      });
    }

    // BleOta_disconnect() + the browser side of a lost connection
    _dropLink() {
      const link = this.link;
      if (!link || !link.alive) return;
      link.alive = false;
      this.link = null;
      this.gatt.connected = false;

      if (this.otaInProgress) {
        this.otaInProgress = false;
        this._taskStop();
      }

      this.events.push("disconnect");
      // As Chrome: the state changes and the event is sent in the same step
      this.webDevice.dispatchEvent(new Event("gattserverdisconnected"));
    }

    // _notify(): the answer is sent only when the page asked for answers on this characteristic
    _notify(uuid, bytes) {
      const link = this.link;
      if (!link || !link.alive) return;
      const characteristic = link.characteristics[uuid];
      if (!characteristic.notifyEnabled) return;
      setTimeout(() => {
        if (!link.alive) return;
        characteristic.value = new DataView(bytes.buffer);
        characteristic.dispatchEvent(new Event("characteristicvaluechanged"));
      }, 0);
    }

    _write(uuid, bytes) {
      if (uuid === RECV_FW_UUID) this._writeFw(bytes);
      else if (uuid === COMMAND_UUID) this._writeCommand(bytes);
      else if (uuid === CUSTOM_UUID) this._writeCustom(bytes);
    }

    // ble_ota.c: _writeCommand()
    _writeCommand(bytes) {
      if (bytes.length !== CMD_SIZE) return;
      const view = new DataView(bytes.buffer);
      if (crc16(bytes.subarray(0, 18)) !== view.getUint16(18, true)) return;

      const cmdId = view.getUint16(0, true);
      let accept = false;

      if (cmdId === CMD_START) {
        const fwLen = view.getUint32(2, true);
        this.startCount++;
        this.otaInProgress = false;
        this.curSector = 0;
        this.fwLen = fwLen;
        this._resetSector();

        accept = fwLen > 0;
        if (accept) accept = this._taskStart();
        this.otaInProgress = accept;
      } else if (cmdId === CMD_STOP) {
        this.stopCount++;
        this.otaInProgress = false;
        accept = true;
        this._taskStop();
      }

      const ack = new Uint8Array(CMD_SIZE);
      const ackView = new DataView(ack.buffer);
      ackView.setUint16(0, CMD_ACK, true);
      ackView.setUint16(2, cmdId, true);
      ackView.setUint16(4, accept ? 0 : 1, true);
      ackView.setUint16(18, crc16(ack.subarray(0, 18)), true);
      if (cmdId === CMD_START && this.options.loseStartReply) {
        this.options.loseStartReply = false;
        return;
      }
      this._notify(COMMAND_UUID, ack);
    }

    // ota.c: _eventCallback(START) and the start of the update task
    _taskStart() {
      if (this.fwLen > this.options.slotSize) return false;
      this.taskActive = true;
      this.taskBytes = [];
      this.taskTotal = 0;
      this.display = "Initiated";
      return true;
    }

    // ota.c: NOTIFY__STOP in the update task
    _taskStop() {
      if (!this.taskActive) return;
      this.taskActive = false;
      // Firmware 1.0.1 has no code for "stop": its display does not change
      if (!this.options.legacy) this.display = "Waiting...";
    }

    // ota.c: the update task gets the firmware bytes of one sector
    _taskData(bytes) {
      if (!this.taskActive) return;
      this.taskBytes.push(bytes);
      this.taskTotal += bytes.length;
      if (this.taskTotal < this.fwLen) {
        this.display = "Receiving " + Math.floor((this.taskTotal * 100) / this.fwLen) + "%";
        return;
      }

      // All bytes are here: the device checks the full file (esp_ota_end)
      this.taskActive = false;
      const image = new Uint8Array(this.taskTotal);
      let offset = 0;
      for (const part of this.taskBytes) { image.set(part, offset); offset += part.length; }
      this.received = image;

      const valid = image[0] === 0xE9 && image[image.length - 1] === checksum(image);
      if (valid) {
        let version = "";
        for (let i = 0x30; i < 0x50 && image[i] !== 0; i++) version += String.fromCharCode(image[i]);
        this.events.push("transfer good");
        this._restartAfterGoodTransfer({ version });
      } else {
        // _fail(): the device stays in the update screen and shows "Error"
        this.events.push("transfer bad");
        this.otaInProgress = false;
        this.display = "Error";
      }
    }

    // ble_ota.c: _writeFw()
    _writeFw(bytes) {
      if (this.options.legacy) return this._writeFwLegacy(bytes);

      if (bytes.length < 3) return;
      const sector = bytes[0] | (bytes[1] << 8);
      const packetSeq = bytes[2];
      const dataLen = bytes.length - 3;
      const lastPacket = packetSeq === 0xFF;

      if (!this.otaInProgress) {
        if (lastPacket) this._sendFwAck(sector, ACK_SECTOR_IDX_ERR);
        return;
      }
      if (sector !== this.curSector) {
        if (lastPacket) this._sendFwAck(this.curSector, ACK_SECTOR_IDX_ERR);
        return;
      }
      if (!lastPacket && packetSeq !== this.curPacket) this.sectorError = true;

      if (this.fwBufOffset + dataLen > BUFFER) {
        this.sectorError = true;
      } else if (!this.sectorError) {
        this.fwBuf.set(bytes.subarray(3), this.fwBufOffset);
        this.fwBufOffset += dataLen;
      }

      if (!lastPacket) {
        this.curPacket++;
        return;
      }

      // Test fault: the device goes out of range
      if (this.options.disconnectAtSector === sector) {
        this.options.disconnectAtSector = null;
        this._dropLink();
        return;
      }

      const sectorStart = this.curSector * BLOCK;
      const remaining = this.fwLen > sectorStart ? this.fwLen - sectorStart : 0;
      const fwDataLen = Math.min(remaining, BLOCK);

      if (this.sectorError || this.fwBufOffset !== BUFFER || fwDataLen === 0) {
        this._resetSector();
        this._sendFwAck(sector, ACK_PAYLOAD_LEN_ERR);
        return;
      }

      // Test fault: the data arrives damaged
      if (this.options.damageSector[sector] > 0) {
        this.options.damageSector[sector]--;
        this.fwBuf[0] ^= 0x01;
        this.events.push("damaged sector " + sector);
      }

      const crc = crc16(this.fwBuf.subarray(0, fwDataLen));
      const recvCrc = this.fwBuf[BLOCK] | (this.fwBuf[BLOCK + 1] << 8);
      if (crc !== recvCrc) {
        this._resetSector();
        this._sendFwAck(sector, ACK_CRC_ERR);
        return;
      }

      this._taskData(this.fwBuf.slice(0, fwDataLen));
      this.curSector++;
      this._resetSector();
      this._sendFwAck(sector, ACK_SUCCESS);
    }

    // _writeFw() of firmware 1.0.1: the CRC of a sector is not checked
    _writeFwLegacy(bytes) {
      if (!this.otaInProgress) return;
      if (bytes.length < 3) return;
      const sector = bytes[0] | (bytes[1] << 8);
      const packetSeq = bytes[2];

      if (sector !== this.curSector && sector !== 0xFFFF) {
        this._sendFwAck(this.curSector, ACK_SECTOR_IDX_ERR);
      }
      if (this.fwBufOffset + (bytes.length - 3) > BUFFER) {
        throw new Error("firmware 1.0.1 stops here: assert(0)");
      }
      this.fwBuf.set(bytes.subarray(3), this.fwBufOffset);
      this.fwBufOffset += bytes.length - 3;

      if (packetSeq !== 0xFF) {
        this.curPacket++;
        return;
      }
      this.curPacket = 0;
      this.curSector++;

      if (this.options.damageSector[sector] > 0) {
        this.options.damageSector[sector]--;
        this.fwBuf[0] ^= 0x01;
        this.events.push("damaged sector " + sector);
      }

      // The task of 1.0.1 writes OTA_BLOCK_SIZE bytes and stops at the firmware length
      const left = this.fwLen - this.taskTotal;
      const length = Math.min(this.fwBufOffset < BUFFER ? this.fwBufOffset : BLOCK, left);
      this._taskData(this.fwBuf.slice(0, length));

      this.fwBufOffset = 0;
      this.fwBuf.fill(0);
      this._sendFwAck(sector, ACK_SUCCESS, true);
    }

    _sendFwAck(sector, status, legacy) {
      if (this.options.loseAckOfSector === sector && status === ACK_SUCCESS) {
        this.options.loseAckOfSector = null;
        this.events.push("lost answer of sector " + sector);
        return;
      }
      const ack = new Uint8Array(CMD_SIZE);
      const view = new DataView(ack.buffer);
      view.setUint16(0, sector, true);
      view.setUint16(2, status, true);
      if (!legacy) view.setUint16(4, this.curSector, true);
      view.setUint16(18, crc16(ack.subarray(0, 18)), true);
      this._notify(RECV_FW_UUID, ack);
    }

    // ble_ota.c: _writeCustom() and ota.c: _customCallback()
    _writeCustom(bytes) {
      if (bytes.length !== CMD_SIZE) return;
      const view = new DataView(bytes.buffer);
      if (crc16(bytes.subarray(0, 18)) !== view.getUint16(18, true)) return;
      const cmdId = view.getUint16(0, true);

      // ota.c: commands from OTA_USER_CMD_FIRST go to the queue of the main loop (ble_commands.c)
      if (cmdId >= USER_CMD_FIRST && this.options.userCommands) {
        this._userCommand(cmdId, bytes.slice(2, 18));
        return;
      }

      const resp = new Uint8Array(CMD_SIZE);
      const respView = new DataView(resp.buffer);
      respView.setUint16(0, CMD_ACK, true);
      respView.setUint16(2, cmdId, true);

      if (cmdId === CUSTOM_GET_VERSION) {
        putText(resp, 4, this.version.slice(0, 13));
      } else if (cmdId === CUSTOM_GET_UID) {
        resp.set(this.options.uid, 4);
      } else if (cmdId === CUSTOM_ACCEPT_FW) {
        if (this.options.ignoreAccept) return;
        this.acceptCount++;
        const accepted = this.pendingVerify;
        resp[4] = accepted ? 1 : 0;
        if (accepted) {
          this.pendingVerify = false;
          this.events.push("accepted " + this.version);
        }
        if (accepted || this.options.legacy) {
          // Firmware 1.0.1 changes the display also when no firmware waits
          this.display = this.screen === "accept" ? "Complete v" + this.version : "Accepted";
        }
      } else if (cmdId === CUSTOM_REJECT_FW) {
        resp[4] = 0;
      } else if (cmdId === CUSTOM_REBOOT) {
        resp[4] = 1;
      } else {
        return; // a user command or an unknown command: no direct answer
      }

      respView.setUint16(18, crc16(resp.subarray(0, 18)), true);
      if (cmdId === CUSTOM_ACCEPT_FW && resp[4] === 1 && this.options.loseAcceptReplies > 0) {
        this.options.loseAcceptReplies--;
        return;
      }
      this._notify(CUSTOM_UUID, resp);
    }

    /* ---- The browser side (Web Bluetooth) -------------------------------------------- */

    _makeWebDevice() {
      const device = this;
      const webDevice = new EventTarget();
      webDevice.name = this.options.name;
      webDevice.id = "fake-stattrak";

      let pendingConnect = null;

      const gatt = {
        connected: false,
        device: webDevice,

        connect() {
          device.events.push("connect try");
          return new Promise((resolve, reject) => {
            const attempt = { reject, done: false };
            pendingConnect = attempt;

            const finish = () => {
              if (attempt.done) return;
              attempt.done = true;
              if (pendingConnect === attempt) pendingConnect = null;
              if (!device.advertising()) {
                const error = new Error("Bluetooth Device is no longer in range.");
                error.name = "NetworkError";
                reject(error);
                return;
              }
              if (!gatt.connected) {
                gatt.connected = true;
                device._openLink();
              }
              resolve(gatt);
            };

            if (device.advertising()) setTimeout(finish, device.options.connectMs);
            else setTimeout(finish, device.options.connectFailMs);
          });
        },

        disconnect() {
          if (pendingConnect && !pendingConnect.done) {
            pendingConnect.done = true;
            const error = new Error("The connection try was stopped.");
            error.name = "AbortError";
            pendingConnect.reject(error);
            pendingConnect = null;
          }
          if (gatt.connected) device._dropLink();
        },

        async getPrimaryService(uuid) {
          const link = device.link;
          if (!link || !link.alive) throw new Error("GATT Server is disconnected.");
          if (uuid !== SERVICE_UUID) throw new Error("No Services matching UUID found in Device.");
          return {
            async getCharacteristic(characteristicUuid) {
              if (!link.alive) throw new Error("GATT Server is disconnected.");
              const characteristic = link.characteristics[characteristicUuid];
              if (!characteristic) throw new Error("No Characteristics matching UUID found in Service.");
              return characteristic;
            },
          };
        },
      };

      webDevice.gatt = gatt;
      this.webDevice = webDevice;
      this.gatt = gatt;
      this.link = null;
    }

    _openLink() {
      const device = this;
      const link = { alive: true, characteristics: {} };

      for (const uuid of [RECV_FW_UUID, OTA_BAR_UUID, COMMAND_UUID, CUSTOM_UUID]) {
        const characteristic = new EventTarget();
        characteristic.uuid = uuid;
        characteristic.value = null;
        characteristic.notifyEnabled = false;
        characteristic.startNotifications = async function () {
          if (!link.alive) throw new Error("GATT Server is disconnected.");
          characteristic.notifyEnabled = true;
          return characteristic;
        };
        characteristic.stopNotifications = async function () {
          if (!link.alive) throw new Error("GATT Server is disconnected.");
          characteristic.notifyEnabled = false;
          return characteristic;
        };
        characteristic.writeValueWithResponse = async function (data) {
          const bytes = toBytes(data);
          await Promise.resolve();
          if (!link.alive) throw new Error("GATT Server is disconnected.");
          device._write(uuid, bytes);
        };
        link.characteristics[uuid] = characteristic;
      }

      this.link = link;
      this.links.push(link);
      this.events.push("connect");
    }
  }

  /* ---- Apps (docs/APPS.md section 6) ------------------------------------------------- */

  // ble_commands.h
  const APP_INFO = 0x0110, APP_LIST = 0x0111, APP_NAME = 0x0112, APP_BEGIN = 0x0113, APP_DATA = 0x0114,
    APP_END = 0x0115, APP_STATUS = 0x0116, APP_DELETE = 0x0117, APP_SET_ORDER = 0x0118,
    APP_SET_HIDDEN = 0x0119, APP_SETTING = 0x011A, APP_ASK = 0x011B, APP_DEVICE_NAME = 0x011C;
  const ST_OK = 0, ST_UNKNOWN = 1, ST_ERROR = 2, ST_NOT_IN_SYNC = 3, ST_INDEX = 4, ST_NO_SPACE = 5,
    ST_TOO_LARGE = 6, ST_TOO_MANY = 7, ST_OFFSET = 8, ST_BAD_CRC = 9, ST_BAD_PACKAGE = 10, ST_WAITING = 11,
    ST_BUILTIN = 12, ST_LAST_SHOWN = 13;
  // apps.h, stapp.h
  const APPS_MAX_INSTALLED = 8, APPS_MAX_BYTES = 24 * 1024, APPS_LIST_MAX = 11, APPS_ERROR_LIMIT = 3;
  const APPS_DEVICE_NAME_MAX = 12, GAP_NAME = "StatTrak", NAME_PREFIX = "StatTrak | ";
  const STAPP_MAX_SIZE = 4096, STAPP_RESERVED_IDS = 255, STAPP_MAX_NAME = 14;
  const STAPP_HANDLERS_OFFSET = 288, STAPP_HANDLERS_V1 = 8, STAPP_HANDLERS_V2 = 10;
  const STAPP_MIN_SIZE = STAPP_HANDLERS_OFFSET + 2 * STAPP_HANDLERS_V1 + 4;   // 308
  const FLAG_HIDDEN = 1, FLAG_BUILTIN = 2, FLAG_SETTINGS = 4, FLAG_ERROR = 8;
  // app_sync_view.h
  const SYNC_IDLE = 0, SYNC_RECEIVING = 1, SYNC_WAITING = 2, SYNC_INSTALLED = 3, SYNC_REFUSED = 4, SYNC_TIMEOUT = 5,
    SYNC_PREVIEW = 6;
  // docs/DATA.md 5.1: the kinds of a transfer (STATUS byte 1)
  const XFER_PACKAGE = 0, XFER_RESTORE = 1, XFER_START_PIC = 2, XFER_PREVIEW = 3, XFER_DATA_CLEAR = 4;
  // docs/DATA.md 5.2
  const CFG_INFO = 0x0120, CFG_ENTRY = 0x0121, CFG_APP_VALUES = 0x0122, CFG_XFER_BEGIN = 0x0123, CFG_START_PIC = 0x0124,
    CFG_PREVIEW_STOP = 0x0125, CFG_DATA_INFO = 0x0126, CFG_DATA_COLUMN = 0x0127, CFG_DATA_READ = 0x0128,
    CFG_DATA_CLEAR = 0x0129, CFG_LOG_READ = 0x012A;
  const BUILTIN = [[1, "KILLS"], [2, "KILLS + DEAD"], [3, "STATS"]];
  // stapp_result_t
  const STAPP_ERR_SIZE = 1, STAPP_ERR_MAGIC = 2, STAPP_ERR_VERSION = 3, STAPP_ERR_CRC = 4, STAPP_ERR_HEADER = 5;
  // The 128-bit service UUID in the advertising packet (components/app_ble/app_ble.c)
  const ADVERTISED_UUID = "03b80e5a-ede8-4b33-a751-6ce34ec4c700";

  // Stapp_crc32(): CRC-32 (IEEE), as zlib.crc32
  function crc32(bytes) {
    let crc = 0xFFFFFFFF;
    for (let i = 0; i < bytes.length; i++) {
      crc ^= bytes[i];
      for (let bit = 0; bit < 8; bit++) crc = (crc & 1) ? ((crc >>> 1) ^ 0xEDB88320) : (crc >>> 1);
    }
    return (crc ^ 0xFFFFFFFF) >>> 0;
  }

  const get16 = (b, o) => b[o] | (b[o + 1] << 8);
  const get32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
  function put16(b, o, v) { if (v > 0xFFFF) v = 0xFFFF; b[o] = v & 0xFF; b[o + 1] = (v >> 8) & 0xFF; }
  function put32(b, o, v) { b[o] = v & 0xFF; b[o + 1] = (v >>> 8) & 0xFF; b[o + 2] = (v >>> 16) & 0xFF; b[o + 3] = (v >>> 24) & 0xFF; }
  function cText(b, o, n) { let s = ""; for (let i = 0; i < n && b[o + i]; i++) s += String.fromCharCode(b[o + i]); return s; }

  // stapp_check.c _isNameChar(), _fixedText(): Stapp_isNameField()
  const isNameChar = (c) => (c >= 0x41 && c <= 0x5A) || (c >= 0x30 && c <= 0x39) || " +-%/.:".includes(String.fromCharCode(c));
  function isNameField(b, o, len) {
    let n = 0;
    while (n < len && b[o + n] !== 0) {
      if (!isNameChar(b[o + n])) return false;
      n++;
    }
    if (n === 0 || n > STAPP_MAX_NAME) return false;
    for (let i = n; i < len; i++) if (b[o + i] !== 0) return false;
    return true;
  }

  // apps.c Apps_isDeviceNameValid()
  function isDeviceNameValid(name) {
    if (name.length < 1 || name.length > APPS_DEVICE_NAME_MAX || name[0] === " " || name[name.length - 1] === " ") return false;
    for (let i = 0; i < name.length; i++) if (name.charCodeAt(i) < 0x20 || name.charCodeAt(i) > 0x7E) return false;
    return true;
  }

  /* The part of Stapp_check() that this model needs: size, magic, versions (format 1 or 2,
     `appFormat` is the newest that this firmware reads), total size, CRC, id, name. The other
     rules (layout, code) are in the firmware and in tools/app.py: the option refusePackage plays
     a package that breaks one of them. Returns [result, info]. */
  function checkPackage(b, newestFormat) {
    if (b.length < STAPP_MIN_SIZE || b.length > STAPP_MAX_SIZE) return [STAPP_ERR_SIZE];
    if (cText(b, 0, 4) !== "STAP") return [STAPP_ERR_MAGIC];
    if (b[4] < 1 || b[4] > (newestFormat || 2) || b[5] !== 1) return [STAPP_ERR_VERSION];
    // Format 2 has 2 more handlers: the data area starts at 308
    const handlers = b[4] >= 2 ? STAPP_HANDLERS_V2 : STAPP_HANDLERS_V1;
    if (b.length < STAPP_HANDLERS_OFFSET + 2 * handlers + 4) return [STAPP_ERR_SIZE];
    if (get16(b, 14) !== b.length) return [STAPP_ERR_SIZE];
    if (crc32(b.subarray(0, b.length - 4)) !== get32(b, b.length - 4)) return [STAPP_ERR_CRC];
    const id = get32(b, 8);
    if (id <= STAPP_RESERVED_IDS || id === 0xFFFFFFFF || (get16(b, 6) & ~3) || !isNameField(b, 16, 16)) return [STAPP_ERR_HEADER];
    const settings = [];
    let p = get16(b, 38);
    for (let i = 0; i < b[34]; i++) {
      const count = b[p + 12];
      settings.push({ name: cText(b, p, 12), count, def: b[p + 13] });
      p += 14 + count * 8;
    }
    return [0, { id, version: get16(b, 12), size: b.length, name: cText(b, 16, 16), settings, format: b[4] }];
  }

  const P = FakeStatTrak.prototype;

  P._appsInit = function () {
    // apps.c: s_list (menu order, hidden flag) and s_installed
    this.appList = BUILTIN.map(([id]) => ({ id, flags: 0 }));
    this.installed = new Map();
    for (const pkg of this.options.installedApps) {
      const [result, info] = checkPackage(pkg, this.options.appFormat);
      if (result !== 0) throw new Error("installedApps: the check refuses a package: " + result);
      this._cacheInstalled(pkg, info);
      this.appList.push({ id: info.id, flags: 0 });
    }
    for (const id of this.options.hiddenApps) {
      const entry = this.appList.find((e) => e.id === id);
      if (entry) entry.flags |= FLAG_HIDDEN;
    }
    for (const [id, errors] of Object.entries(this.options.appErrors)) {
      const app = this.installed.get(Number(id));
      if (app) app.errors = errors;
    }
    this.appWrites = 0;                     // flash writes of the list, the packages and the settings
    this.appCommands = [];                  // the app commands that the device got
    // The Bluetooth name that the person chose (key "bname"; "" = none, the device is "StatTrak")
    this.deviceName = isDeviceNameValid(this.options.deviceName || "") ? this.options.deviceName : "";
    this.syncBleName = GAP_NAME;            // the name of this start of "Apps: SYNC"
    this.syncFailed = false;
    this.openedApp = null;                  // the app that OPEN opened
    this._syncReset();
    this._dataInit();
    // SET_ORDER comes in parts
    this.order = [];
    this.orderCount = 0;
    this.orderNextPart = 0;
  };

  P._cacheInstalled = function (pkg, info) {
    const old = this.installed.get(info.id);
    this.installed.set(info.id, {
      id: info.id, name: info.name, version: info.version, size: info.size, pkg: pkg.slice(),
      valueCounts: info.settings.map((s) => s.count),
      settings: old && old.settings.length === info.settings.length ? old.settings : info.settings.map((s) => s.def),
      errors: 0,
    });
  };

  /* The name in the advertising packet: "Apps: SYNC" uses the name of the person (from its
     start), the update view and the accept view always "StatTrak" */
  P.bleName = function () {
    return this.screen === "appsync" ? this.syncBleName : GAP_NAME;
  };

  // app_sync_view.c _reset()
  P._syncReset = function () {
    this.syncState = SYNC_IDLE;
    this.syncKind = XFER_PACKAGE;   // STATUS byte 1: what the state is about (docs/DATA.md 5.1)
    this.syncBegun = false;
    this.syncReceived = 0;
    this.syncSize = 0;
    this.syncHolding = [false, false];
    if (!this.syncBuffer) this.syncBuffer = new Uint8Array(STAPP_MAX_SIZE);
    if (this.syncId === undefined) { this.syncId = 0; this.syncCrc = 0; this.syncName = ""; }
    clearTimeout(this.syncQuestionTimer);
    clearTimeout(this.syncDataTimer);
    this._syncDisplay();
  };

  /* The text of the display (a short form of _draw()) */
  P._syncDisplay = function () {
    if (this.screen !== "appsync") return;
    this._checkScreen();
    const texts = {
      [SYNC_RECEIVING]: () => "Receiving " + (this.syncSize > 0 ? Math.floor((this.syncReceived * 100) / this.syncSize) : 0) + "%",
      [SYNC_WAITING]: () => ({ [XFER_RESTORE]: "RESTORE BACKUP?", [XFER_DATA_CLEAR]: "DELETE " + this.syncName + " DATA?" })[this.syncKind] ||
        "INSTALL " + this.syncName + "?",
      [SYNC_INSTALLED]: () => ({ [XFER_RESTORE]: "BACKUP RESTORED", [XFER_START_PIC]: "START PICTURE", [XFER_DATA_CLEAR]: "DATA DELETED" })[this.syncKind] ||
        "APP INSTALLED",
      [SYNC_PREVIEW]: () => "PREVIEW",
      [SYNC_REFUSED]: () => "REFUSED",
      [SYNC_TIMEOUT]: () => "REFUSED",
    };
    this.display = texts[this.syncState] ? texts[this.syncState]() : (this.syncFailed ? "NOT INSTALLED" : "Waiting...");
  };

  // AppSync_update(): a transfer that stopped (10 s without DATA)
  P._syncDataTimeout = function () {
    clearTimeout(this.syncDataTimer);
    this.syncDataTimer = this._later(this.options.appDataMs, () => {
      if (this.syncState !== SYNC_RECEIVING) return;
      this._syncReset();
      this.syncFailed = true;
      this._syncDisplay();
      this.events.push("transfer stopped");
    });
  };

  /* A person opens Settings > "Apps: SYNC" (app_sync_view.c _init: Ota_init, firmware refused,
     the Bluetooth name of the person for this start) */
  P.openAppSync = function () {
    if (this.screen !== "counter") return;
    this.screen = "appsync";
    this._syncReset();
    this.syncFailed = false;
    this.syncBleName = this.deviceName ? NAME_PREFIX + this.deviceName : GAP_NAME;
    this._syncDisplay();
  };

  /* A person leaves "Apps: SYNC" (a long press of button 1 while no question shows and no
     transfer runs). Bluetooth goes off. */
  P.leaveAppSync = function () {
    if (this.screen !== "appsync" || this.syncState === SYNC_WAITING || this.syncState === SYNC_RECEIVING) return;
    this._closeAppSync("counter");
  };

  // app_sync_view.c _deinit(): Ota_deinit (Bluetooth off), _reset()
  P._closeAppSync = function (screen) {
    this._dropLink();
    this._syncReset();
    this.screen = screen;
    this.display = screen;
  };

  /* The buttons (app_sync_view.c _eventHandler, main/views/hold_button.c). button: 1 or 2.
     The buttons on the display answer a hold that starts while the screen shows; a new screen
     (an other install state, or "NOT INSTALLED") forgets the holds that started before.
     buttonDown(): EVENT__RAW_PRESS.
     buttonUp(): EVENT__RAW_RELEASE.
     longPress(): EVENT__LONG_PRESS (700 ms after the press, the button is still down).
     press(): a short press (down, up, then EVENT__PRESS): it does nothing. */
  P._checkScreen = function () {   // Also in _syncDisplay(): the firmware does it at each draw
    const shown = this.syncState * 2 + (this.syncFailed ? 1 : 0);
    if (shown !== this.syncShown) {
      this.syncShown = shown;
      this.syncHolding = [false, false];
    }
  };

  P.buttonDown = function (button) {
    if (this.screen !== "appsync") return;
    if (this.syncState === SYNC_PREVIEW) {
      // A button ends the preview frame (the press does nothing else)
      this._syncReset();
      this.events.push("preview stopped by a button");
      return;
    }
    this._checkScreen();
    this.syncHolding[button - 1] = true;
  };

  P.buttonUp = function (button) {
    if (this.screen !== "appsync") return;
    this.syncHolding[button - 1] = false;
  };

  P.longPress = function (button) {
    if (this.screen !== "appsync") return;
    this._checkScreen();
    if (!this.syncHolding[button - 1]) return;
    if (this.syncState === SYNC_INSTALLED && this.syncKind !== XFER_PACKAGE) {
      // "BACKUP RESTORED" and OK (or an other end screen of docs/DATA.md): back to waiting
      this.syncState = SYNC_IDLE;
      this.syncKind = XFER_PACKAGE;
      this._syncDisplay();
      return;
    }
    if (button === 2) {
      if (this.syncState === SYNC_WAITING) {
        this._syncAnswer(true);
      } else if (this.syncState === SYNC_INSTALLED) {
        // OPEN: the app opens, Bluetooth goes off with this view
        this.openedApp = this.syncId;
        this.events.push("app opened " + this.syncName);
        this._closeAppSync("app");
      }
      return;
    }
    if (this.syncState === SYNC_WAITING) {
      this._syncAnswer(false);
    } else if (this.syncState === SYNC_INSTALLED) {
      this.syncState = SYNC_IDLE;   // SYNC MORE: wait for the next app
      this.events.push("sync more");
      this._syncDisplay();
    } else if (this.syncState === SYNC_RECEIVING) {
      // Stop this transfer; the page must start again with BEGIN
      this._syncReset();
      this.syncFailed = true;   // The display shows "NOT INSTALLED"
      this._syncDisplay();
      this.events.push("transfer stopped");
    } else {
      this._closeAppSync("counter");
    }
  };

  P.press = function (button) {
    if (this.screen !== "appsync") return;
    this.buttonDown(button);
    this.buttonUp(button);   // The buttons on the display answer a hold only
  };

  /* Hold button 2 (down while the question shows, then the long press): "yes" */
  P.holdButton2 = function () {
    this.buttonDown(2);
    this.longPress(2);
    this.buttonUp(2);
  };

  /* Hold button 1: "no" to the question; while the data comes: stop the transfer; after the
     install: SYNC MORE; else BACK */
  P.holdButton1 = function () {
    this.buttonDown(1);
    this.longPress(1);
    this.buttonUp(1);
  };

  // app_sync_view.c _answer()
  P._syncAnswer = function (yes) {
    if (this.syncState !== SYNC_WAITING) return;
    clearTimeout(this.syncQuestionTimer);
    this.syncHolding = [false, false];
    if (!yes) {
      this.syncState = SYNC_REFUSED;
      this.events.push(this.syncKind === XFER_PACKAGE ? "app refused" : "refused kind " + this.syncKind);
      this._syncDisplay();
      return;
    }
    if (this.syncKind === XFER_DATA_CLEAR) {
      // DELETE <NAME> DATA? yes: the rows are deleted
      const store = this.dataStores.get(this.syncId);
      if (store) store.values = [];
      this.syncState = SYNC_INSTALLED;
      this.events.push("data cleared " + this.syncName);
      this._syncDisplay();
      return;
    }
    // Yes: now the page sends the data
    this.syncReceived = 0;
    this.syncState = SYNC_RECEIVING;
    this.events.push(this.syncKind === XFER_PACKAGE ? "app yes " + this.syncName : "yes kind " + this.syncKind);
    this._syncDataTimeout();
    this._syncDisplay();
  };

  /* An installed app has a run-time error (Apps_noteError). At 3 errors it is switched off:
     hidden, also when it is the last shown app (the menu then shows KILLS). */
  P.appError = function (id) {
    const app = this.installed.get(id);
    if (!app) return;
    if (app.errors < 0xFF) app.errors++;
    this.appWrites++;
    if (app.errors >= APPS_ERROR_LIMIT) {
      const entry = this.appList.find((e) => e.id === id);
      if (entry) entry.flags |= FLAG_HIDDEN;
    }
  };

  /* The names of the app list in menu order (SETTINGS is not in the list) */
  P.appNames = function () {
    return this.appList.map((e) => this._appName(e.id));
  };

  P.appHidden = function (name) {
    const entry = this.appList.find((e) => this._appName(e.id) === name);
    return !!entry && (entry.flags & FLAG_HIDDEN) !== 0;
  };

  P._appName = function (id) {
    const builtin = BUILTIN.find((b) => b[0] === id);
    if (builtin) return builtin[1];
    const app = this.installed.get(id);
    return app ? app.name : null;
  };

  P._usedBytes = function () {
    let total = 0;
    for (const app of this.installed.values()) total += app.size;
    return total;
  };

  P._shownCount = function () {
    return this.appList.filter((e) => !(e.flags & FLAG_HIDDEN)).length;
  };

  P._appFlags = function (id) {
    const entry = this.appList.find((e) => e.id === id);
    if (!entry) return 0;
    let flags = entry.flags & FLAG_HIDDEN;
    if (BUILTIN.some((b) => b[0] === id)) flags |= FLAG_BUILTIN;
    const app = this.installed.get(id);
    if (app) {
      if (app.valueCounts.length > 0) flags |= FLAG_SETTINGS;
      if (app.errors >= APPS_ERROR_LIMIT) flags |= FLAG_ERROR;
    }
    return flags;
  };

  // Apps_canInstall()
  P._appsCanInstall = function (id, size) {
    if (id <= STAPP_RESERVED_IDS || id === 0xFFFFFFFF) return "bad";
    if (size > STAPP_MAX_SIZE) return "too large";
    const old = this.installed.get(id);
    if (!old && this.installed.size >= APPS_MAX_INSTALLED) return "too many";
    if (this._usedBytes() - (old ? old.size : 0) + size > APPS_MAX_BYTES) return "no space";
    return "ok";
  };

  // Apps_install()
  P._appsInstall = function (pkg, info) {
    const can = this._appsCanInstall(info.id, info.size);
    if (can !== "ok") return can;
    const old = this.installed.get(info.id);
    const wasSwitchedOff = !!old && old.errors >= APPS_ERROR_LIMIT;
    this._cacheInstalled(pkg, info);
    this.appWrites++;
    if (!old) {
      this.appList.push({ id: info.id, flags: 0 });
    } else if (wasSwitchedOff) {
      const entry = this.appList.find((e) => e.id === info.id);
      if (entry) entry.flags &= ~FLAG_HIDDEN;
    }
    return "ok";
  };

  // _appsStatus()
  P._appsStatus = function (result) {
    const map = { ok: ST_OK, builtin: ST_BUILTIN, "last shown": ST_LAST_SHOWN, "no space": ST_NO_SPACE,
      "too large": ST_TOO_LARGE, "too many": ST_TOO_MANY };
    return result in map ? map[result] : ST_ERROR;
  };

  /* ble_commands.c: BleCommands_update() -> _handle() -> Ota_sendUserResponse() */
  P._userCommand = function (cmdId, d) {
    const response = new Uint8Array(13);
    let status = ST_UNKNOWN;
    let length = 0;
    const answer = (s, len) => { status = s; length = len || 0; };

    if (cmdId >= APP_INFO && cmdId <= APP_DEVICE_NAME && this.options.apps) {
      this.appCommands.push(cmdId);
      this._appCommand(cmdId, d, response, answer);
    } else if (cmdId >= CFG_INFO && cmdId <= CFG_LOG_READ && this.options.data) {
      this.appCommands.push(cmdId);
      this._dataCommand(cmdId, d, response, answer);
    }

    const lose = this.options.loseAppAnswers[cmdId];
    if (lose > 0) {
      this.options.loseAppAnswers[cmdId] = lose - 1;
      this.events.push("lost answer of 0x" + cmdId.toString(16));
      return;
    }
    if (cmdId === APP_DATA && this.options.loseDataAnswerAt === get16(d, 0)) {
      this.options.loseDataAnswerAt = null;
      this.events.push("lost answer of DATA " + get16(d, 0));
      return;
    }

    // ota.c: Ota_sendUserResponse(): ACK, the command, the status, 13 data bytes, the CRC
    const resp = new Uint8Array(CMD_SIZE);
    const view = new DataView(resp.buffer);
    view.setUint16(0, CMD_ACK, true);
    view.setUint16(2, cmdId, true);
    resp[4] = status;
    resp.set(response.subarray(0, length), 5);
    view.setUint16(18, crc16(resp.subarray(0, 18)), true);
    this._notify(CUSTOM_UUID, resp);
  };

  // ble_commands.c: _handleApp()
  P._appCommand = function (cmdId, d, r, answer) {
    if (this.screen !== "appsync") return answer(ST_NOT_IN_SYNC);

    // While the person answers "INSTALL <NAME>?", nothing can change the apps
    const changes = [APP_BEGIN, APP_ASK, APP_DATA, APP_END, APP_DELETE, APP_SET_ORDER, APP_SET_HIDDEN].includes(cmdId) ||
      (cmdId === APP_SETTING && d[5] !== 0xFF) || (cmdId === APP_DEVICE_NAME && d[0] !== 0);
    if (changes && this.syncState === SYNC_WAITING) return answer(ST_WAITING);

    switch (cmdId) {
      case APP_INFO: {
        r[0] = 1;
        r[1] = this.options.appFormat;
        put16(r, 2, Math.max(0, APPS_MAX_BYTES - this._usedBytes()));
        put16(r, 4, STAPP_MAX_SIZE);
        r[6] = this.installed.size;
        r[7] = APPS_MAX_INSTALLED;
        return answer(ST_OK, 8);
      }
      case APP_LIST: {
        const entry = this.appList[d[0]];
        if (!entry) return answer(ST_INDEX);
        const app = this.installed.get(entry.id);
        const name = this._appName(entry.id) || "";
        put32(r, 0, entry.id);
        put16(r, 4, app ? app.version : 0);
        put16(r, 6, app ? app.size : 0);
        r[8] = this._appFlags(entry.id);
        r[9] = app ? app.valueCounts.length : 0;
        for (let i = 0; i < 3; i++) r[10 + i] = name.length > i ? name.charCodeAt(i) : 0;
        return answer(ST_OK, 13);
      }
      case APP_NAME: {
        const name = this._appName(get32(d, 0));
        if (name === null) return answer(ST_ERROR);
        const start = d[4] * 13;
        const part = start < name.length ? name.slice(start, start + 13) : "";
        for (let i = 0; i < part.length; i++) r[i] = part.charCodeAt(i);
        return answer(ST_OK, part.length);
      }
      case APP_BEGIN: {
        // AppSync_begin()
        const id = get32(d, 0), size = get16(d, 4), crc = get32(d, 6);
        const can = this._appsCanInstall(id, size);
        if (can !== "ok") return answer(this._appsStatus(can));
        if (size < STAPP_MIN_SIZE) return answer(ST_ERROR);   // Smaller than the smallest package
        // A new install: an earlier "yes" does not count for it
        this._syncReset();
        this.syncId = id;
        this.syncSize = size;
        this.syncCrc = crc;
        this.syncBegun = true;
        this.syncFailed = false;
        this._syncDisplay();
        return answer(ST_OK);
      }
      case APP_ASK: {
        // AppSync_ask(): the name field of the package
        if (!this.syncBegun || this.syncState === SYNC_RECEIVING || !isNameField(d, 0, 16)) return answer(ST_ERROR);
        this.syncName = cText(d, 0, STAPP_MAX_NAME);
        this.syncBegun = false;
        this.syncHolding = [false, false];
        this.syncState = SYNC_WAITING;
        this.events.push("question " + this.syncName);
        this._syncDisplay();
        // AppSync_update(): no answer in 30 s
        clearTimeout(this.syncQuestionTimer);
        this.syncQuestionTimer = this._later(this.options.appQuestionMs, () => {
          if (this.syncState !== SYNC_WAITING) return;
          this.syncState = SYNC_TIMEOUT;
          this.events.push("question timeout");
          this._syncDisplay();
        });
        return answer(ST_OK);
      }
      case APP_DATA: {
        if (this.syncKind !== XFER_PACKAGE) return this._xferData(d, r, answer);
        // AppSync_data(): only after "yes"
        const offset = get16(d, 0);
        put16(r, 0, this.syncReceived);
        if (this.syncState !== SYNC_RECEIVING) return answer(ST_ERROR, 2);
        if (offset !== this.syncReceived) return answer(ST_OFFSET, 2);
        const count = Math.min(14, this.syncSize - this.syncReceived);
        this.syncBuffer.set(d.subarray(2, 2 + count), this.syncReceived);
        this.syncReceived += count;
        this._syncDataTimeout();
        this._syncDisplay();
        put16(r, 0, this.syncReceived);
        return answer(ST_OK, 2);
      }
      case APP_END: {
        if (this.syncKind !== XFER_PACKAGE) return this._xferEnd(r, answer);
        // AppSync_end()
        put16(r, 0, this.syncReceived);
        if (this.syncState !== SYNC_RECEIVING) return answer(ST_ERROR);
        if (this.syncReceived !== this.syncSize) return answer(ST_OFFSET, 2);
        // A failed check ends this install: the page starts again with BEGIN (and the question)
        const failed = (status, len) => {
          this._syncReset();
          this.syncFailed = true;
          this._syncDisplay();
          this.events.push("app install failed");
          return answer(status, len);
        };
        const pkg = this.syncBuffer.subarray(0, this.syncSize);
        if (crc32(pkg) !== this.syncCrc) return failed(ST_BAD_CRC);
        let [result, info] = checkPackage(pkg, this.options.appFormat);
        if (result === 0 && this.options.refusePackage) result = this.options.refusePackage;
        // The person said yes to this id and this name: the package must have both
        if (result === 0 && (info.id !== this.syncId || info.name !== this.syncName)) result = STAPP_ERR_HEADER;
        if (result !== 0) {
          r[0] = result;
          return failed(ST_BAD_PACKAGE, 1);
        }
        // The one flash write of the package
        if (this.options.flashFails || this._appsInstall(pkg, info) !== "ok") return failed(ST_ERROR);
        clearTimeout(this.syncDataTimer);
        this.syncState = SYNC_INSTALLED;
        this.events.push("app installed " + info.name);
        this._syncDisplay();
        return answer(ST_OK);
      }
      case APP_STATUS:
        // u8 state, u8 kind (docs/DATA.md 5.1). Old pages read only the first byte.
        r[0] = this.syncState;
        r[1] = this.syncKind;
        return answer(ST_OK, this.options.data ? 2 : 1);
      case APP_DELETE: {
        // Apps_delete()
        const id = get32(d, 0);
        if (BUILTIN.some((b) => b[0] === id)) return answer(ST_BUILTIN);
        const index = this.appList.findIndex((e) => e.id === id);
        if (!this.installed.has(id) || index < 0) return answer(ST_ERROR);
        this.appList.splice(index, 1);
        this.installed.delete(id);
        if (this.dataStores) this.dataStores.delete(id);   // Deleting an app deletes its app data
        this.appWrites++;
        this.events.push("app deleted " + id.toString(16));
        return answer(ST_OK);
      }
      case APP_SET_ORDER:
        return answer(this._setOrder(d));
      case APP_SET_HIDDEN: {
        // Apps_setHidden()
        if (d[4] > 1) return answer(ST_ERROR);
        const id = get32(d, 0), hidden = d[4] === 1;
        const entry = this.appList.find((e) => e.id === id);
        if (!entry) return answer(ST_ERROR);
        const isHidden = (entry.flags & FLAG_HIDDEN) !== 0;
        const app = this.installed.get(id);
        if (hidden) {
          if (isHidden) return answer(ST_OK);
          if (this._shownCount() <= 1) return answer(ST_LAST_SHOWN);
          entry.flags |= FLAG_HIDDEN;
        } else {
          // Shown again: an app that was switched off for errors gets a new chance
          if (app && app.errors > 0) app.errors = 0;
          if (!isHidden) return answer(ST_OK);
          entry.flags &= ~FLAG_HIDDEN;
        }
        this.appWrites++;
        return answer(ST_OK);
      }
      case APP_SETTING: {
        const id = get32(d, 0), n = d[4];
        const app = this.installed.get(id);
        if (!app || n >= app.valueCounts.length) return answer(ST_ERROR);
        if (d[5] !== 0xFF) {
          // Apps_setSetting()
          if (d[5] >= app.valueCounts[n]) return answer(ST_ERROR);
          if (app.settings[n] !== d[5]) {
            app.settings[n] = d[5];
            this.appWrites++;
          }
        }
        r[0] = app.settings[n];
        return answer(ST_OK, 1);
      }
      case APP_DEVICE_NAME: {
        // The part after "StatTrak | ". u8 0 = read, 1 = write; then 12 bytes at most, 0-padded (all 0 =
        // no part: "StatTrak"). Answer: u8 1 = there is a part, then the part (12 bytes, 0-padded).
        if (d[0] > 1) return answer(ST_ERROR);
        if (d[0] === 1) {
          const name = cText(d, 1, APPS_DEVICE_NAME_MAX);
          // Only 0 bytes after the name
          for (let i = name.length; i < 15; i++) if (d[1 + i] !== 0) return answer(ST_ERROR);
          // Apps_setDeviceName()
          if (name !== "" && !isDeviceNameValid(name)) return answer(ST_ERROR);
          if (this.options.flashFails) return answer(ST_ERROR);
          this.deviceName = name;
          this.appWrites++;
          this.events.push("device name " + (name || "(none)"));
        }
        r[0] = this.deviceName ? 1 : 0;
        for (let i = 0; i < this.deviceName.length; i++) r[1 + i] = this.deviceName.charCodeAt(i);
        return answer(ST_OK, 1 + APPS_DEVICE_NAME_MAX);
      }
    }
    return answer(ST_UNKNOWN);
  };

  // ble_commands.c: _setOrder(), apps.c: Apps_setOrder()
  P._setOrder = function (d) {
    const count = d[0], part = d[13];
    if (count === 0 || count > APPS_LIST_MAX || part !== this.orderNextPart || (part > 0 && count !== this.orderCount) ||
        part * 3 >= count) {
      this.orderNextPart = 0;
      return ST_ERROR;
    }
    this.orderCount = count;
    const first = part * 3;
    for (let i = 0; i < 3 && first + i < count; i++) this.order[first + i] = get32(d, 1 + 4 * i);
    if (first + 3 < count) {
      this.orderNextPart++;
      return ST_OK;   // More parts come
    }
    this.orderNextPart = 0;

    const ids = this.order.slice(0, count);
    if (count !== this.appList.length) return ST_ERROR;
    const order = [];
    for (let i = 0; i < count; i++) {
      const entry = this.appList.find((e) => e.id === ids[i]);
      if (!entry || ids.indexOf(ids[i]) !== i) return ST_ERROR;
      order.push(entry);
    }
    if (order.every((e, i) => e === this.appList[i])) return ST_OK;   // No change, no flash write
    this.appList = order;
    this.appWrites++;
    return ST_OK;
  };

  /* ---- Backup, app data, device log, start picture, preview (docs/DATA.md) ----------- */

  // main/app.h ENTRY_LIST: the flash values in their order (key, default, largest valid value)
  const CONFIG_ENTRIES = [
    ["init", 1, 1], ["total_kills", 0, 0xFFFFFFFF], ["total_deaths", 0, 0xFFFFFFFF], ["game_kills", 0, 0xFFFFFFFF],
    ["game_deaths", 0, 0xFFFFFFFF], ["battery_type", 0, 7], ["show_battery", 0, 1], ["brightness", 8, 10],
    ["dim_screen", 3, 7], ["dim_level", 2, 9], ["auto_off", 0, 5], ["counter", 0, 1], ["sound", 1, 1], ["low_bat", 0, 4],
    ["device_model", 0, 2],     // 0 = not set, 1 = DIY, 2 = PEQ
  ];
  const DATA_APP_VALUES = 1024, DATA_ALL_VALUES = 2048;     // 4096 bytes for each app, 8192 bytes for all
  const LOG_MAX = 64, PICTURE_BYTES = 512, RESTORE_MAX = 4096, RESTORE_MIN = 6;
  const BAD_BACKUP_MAGIC = 1, BAD_BACKUP_VERSION = 2, BAD_BACKUP_SECTION = 3;
  const getI32 = (b, o) => get32(b, o) | 0;

  P._dataInit = function () {
    const o = this.options;
    // A device that is set up, if the test does not say it: the person answered DIY to "StatTrak version"
    const start = Object.assign({ device_model: 1 }, o.configValues);
    this.config = CONFIG_ENTRIES.map(([key, value, max]) => ({ key, value: key in start ? start[key] >>> 0 : value, max }));
    this.dataStores = new Map();             // id -> { columns: [names], values: [i32] }
    for (const [id, store] of Object.entries(o.appData)) this.setAppData(Number(id), store.columns, store.rows);
    for (const [id, values] of Object.entries(o.appValues)) {
      const app = this.installed.get(Number(id));
      if (app) app.values = values.map((v) => v | 0);
    }
    this.deviceLog = o.deviceLog.slice(0, LOG_MAX).map((e) => Object.assign({ reason: 1, battery: 255, flags: 0, boot: 0 }, e));
    this.startPic = o.startPicture ? Uint8Array.from(o.startPicture) : null;
    this.startPicShow = o.startPictureShow;
    this.previewFrame = null;               // the frame that the display shows (state 6)
    this.previewCount = 0;
    this.restores = [];                     // the restore data of each restore (for the tests)
    this.xferDataCount = 0;                 // DATA commands of the last transfer
  };

  // The saved values of an installed app: header byte 33 = the saved variable count
  P._appValues = function (app) {
    if (!app.values || app.values.length !== app.pkg[33]) app.values = new Array(app.pkg[33]).fill(0);
    return app.values;
  };

  /* Test hooks */
  P.setAppData = function (id, columns, rows) {
    const values = [];
    for (const row of rows || []) for (let c = 0; c < columns.length; c++) values.push((row[c] || 0) | 0);
    this.dataStores.set(id, { columns: columns.slice(), values });
  };
  P.appData = function (id) {
    const store = this.dataStores.get(id);
    if (!store) return null;
    const rows = [];
    for (let i = 0; i + store.columns.length <= store.values.length; i += store.columns.length) rows.push(store.values.slice(i, i + store.columns.length));
    return rows;
  };
  P.setDeviceLog = function (entries) { this.deviceLog = entries.slice(0, LOG_MAX); };
  P.setStartPicture = function (bytes, show) {
    this.startPic = bytes ? Uint8Array.from(bytes) : null;
    this.startPicShow = show !== false;
  };
  P.setConfigValue = function (key, value) { this.config.find((e) => e.key === key).value = value >>> 0; };
  P.configValue = function (key) { const e = this.config.find((x) => x.key === key); return e ? e.value : undefined; };
  P.appSettings = function (id) { const app = this.installed.get(id); return app ? app.settings.slice() : null; };
  P.savedValues = function (id) { const app = this.installed.get(id); return app ? this._appValues(app).slice() : null; };
  P.setSavedValues = function (id, values) { this.installed.get(id).values = values.map((v) => v | 0); };

  P._startPicState = function () { return !this.startPic ? 0 : (this.startPicShow ? 1 : 2); };
  P._dataUsedValues = function () { let n = 0; for (const s of this.dataStores.values()) n += s.values.length; return n; };

  // A question of the device (the same wait and buttons as "INSTALL <NAME>?")
  P._ask = function (kind, name) {
    this.syncKind = kind;
    this.syncName = name;
    this.syncHolding = [false, false];
    this.syncState = SYNC_WAITING;
    this.events.push("question kind " + kind);
    this._syncDisplay();
    clearTimeout(this.syncQuestionTimer);
    this.syncQuestionTimer = this._later(this.options.appQuestionMs, () => {
      if (this.syncState !== SYNC_WAITING) return;
      this.syncState = SYNC_TIMEOUT;
      this.events.push("question timeout");
      this._syncDisplay();
    });
  };

  // ble_commands.c: _handleData() (docs/DATA.md 5.2)
  P._dataCommand = function (cmdId, d, r, answer) {
    if (this.screen !== "appsync") return answer(ST_NOT_IN_SYNC);
    const op = d[0];
    const changes = cmdId === CFG_XFER_BEGIN || cmdId === CFG_PREVIEW_STOP || cmdId === CFG_DATA_CLEAR ||
      (cmdId === CFG_START_PIC && op >= 1 && op <= 3);
    if (changes && this.syncState === SYNC_WAITING) return answer(ST_WAITING);

    switch (cmdId) {
      case CFG_INFO: {
        r[0] = 1;
        r[1] = this.config.length;
        r[2] = 0x1F;                                   // backup, app data, device log, start picture, preview
        r[3] = this._startPicState();
        put16(r, 4, this._dataUsedValues() * 4);
        put16(r, 6, DATA_ALL_VALUES * 4);
        put16(r, 8, DATA_APP_VALUES * 4);
        put16(r, 10, Math.min(65535, this.options.freeFlash));
        r[12] = this.deviceLog.length;
        return answer(ST_OK, 13);
      }
      case CFG_ENTRY: {
        const entry = this.config[d[0]];
        if (!entry) return answer(ST_INDEX);
        if (d[1] === 0) {
          put32(r, 0, entry.value);
          put32(r, 4, entry.max);
          r[8] = entry.key.length;
          return answer(ST_OK, 9);
        }
        if (d[1] > 2) return answer(ST_ERROR);
        const part = d[1] === 1 ? entry.key.slice(0, 13) : entry.key.slice(13, 15);
        for (let i = 0; i < part.length; i++) r[i] = part.charCodeAt(i);
        return answer(ST_OK, part.length);
      }
      case CFG_APP_VALUES: {
        const app = this.installed.get(get32(d, 0));
        if (!app) return answer(ST_ERROR);
        const values = this._appValues(app), first = d[4];
        r[0] = values.length;
        let n = 0;
        for (; n < 3 && first + n < values.length; n++) put32(r, 1 + 4 * n, values[first + n]);
        return answer(ST_OK, 1 + 4 * n);
      }
      case CFG_XFER_BEGIN: {
        const kind = d[0], size = get16(d, 1), crc = get32(d, 3);
        const sizeOk = kind === XFER_RESTORE ? size >= RESTORE_MIN && size <= RESTORE_MAX :
          (kind === XFER_START_PIC || kind === XFER_PREVIEW) && size === PICTURE_BYTES;
        if (!sizeOk) return answer(ST_ERROR);
        this._syncReset();
        this.syncFailed = false;
        this.syncKind = kind;
        this.syncSize = size;
        this.syncCrc = crc;
        this.syncBuffer.fill(0);
        this.xferDataCount = 0;
        if (kind === XFER_RESTORE) {
          this._ask(kind, "");
        } else {
          this.syncState = SYNC_RECEIVING;
          if (kind === XFER_START_PIC) this._syncDataTimeout();
          this._syncDisplay();
        }
        return answer(ST_OK);
      }
      case CFG_START_PIC: {
        if (op > 4) return answer(ST_ERROR);
        if (!this.startPic && (op === 1 || op === 2 || op === 4)) return answer(ST_ERROR);
        if (op === 4) {
          const offset = get16(d, 1);
          if (offset > PICTURE_BYTES) return answer(ST_ERROR);
          const part = this.startPic.subarray(offset, offset + 12);
          r.set(part, 0);
          return answer(ST_OK, part.length);
        }
        if (op === 1 || op === 2) this.startPicShow = op === 1;
        if (op === 3 && this.startPic) { this.startPic = null; this.events.push("start picture deleted"); }
        if (op !== 0) this.appWrites++;
        r[0] = this._startPicState();
        return answer(ST_OK, 1);
      }
      case CFG_PREVIEW_STOP:
        if (this.syncState === SYNC_PREVIEW || (this.syncKind === XFER_PREVIEW && this.syncState === SYNC_RECEIVING)) {
          this._syncReset();
          this.events.push("preview stopped");
        }
        this.previewFrame = null;
        return answer(ST_OK);
      case CFG_DATA_INFO: {
        const id = get32(d, 0);
        if (!this.appList.some((e) => e.id === id)) return answer(ST_ERROR);
        const store = this.dataStores.get(id);
        if (!store) { r[0] = 0; return answer(ST_OK, 6); }
        const columns = store.columns.length;
        const rows = Math.floor(store.values.length / columns);
        const left = DATA_ALL_VALUES - this._dataUsedValues() + store.values.length;
        const fit = Math.floor(Math.min(DATA_APP_VALUES, left) / columns);
        r[0] = columns;
        put16(r, 1, rows);
        put16(r, 3, fit);
        r[5] = rows >= fit ? 1 : 0;
        return answer(ST_OK, 6);
      }
      case CFG_DATA_COLUMN: {
        const store = this.dataStores.get(get32(d, 0));
        if (!store || d[4] >= store.columns.length) return answer(ST_ERROR);
        const name = store.columns[d[4]].slice(0, 12);
        for (let i = 0; i < name.length; i++) r[i] = name.charCodeAt(i);
        return answer(ST_OK, name.length);
      }
      case CFG_DATA_READ: {
        const store = this.dataStores.get(get32(d, 0));
        if (!store) return answer(ST_ERROR);
        const first = get16(d, 4);
        let n = 0;
        for (; n < 3 && first + n < store.values.length; n++) put32(r, 1 + 4 * n, store.values[first + n]);
        r[0] = n;
        return answer(ST_OK, 1 + 4 * n);
      }
      case CFG_DATA_CLEAR: {
        const id = get32(d, 0);
        const store = this.dataStores.get(id);
        if (!store || store.values.length === 0) return answer(ST_ERROR);
        this._syncReset();
        this.syncId = id;
        this._ask(XFER_DATA_CLEAR, this._appName(id) || "");
        return answer(ST_OK);
      }
      case CFG_LOG_READ: {
        const first = d[0];
        r[0] = this.deviceLog.length;
        let n = 0;
        for (; n < 3 && first + n < this.deviceLog.length; n++) {
          const e = this.deviceLog[first + n];
          r.set([e.reason, e.battery, e.flags, e.boot], 1 + 4 * n);
        }
        return answer(ST_OK, 1 + 4 * n);
      }
    }
    return answer(ST_UNKNOWN);
  };

  // DATA (0x0114) of a transfer of docs/DATA.md 5.1
  P._xferData = function (d, r, answer) {
    const offset = get16(d, 0);
    put16(r, 0, this.syncReceived);
    if (this.syncState !== SYNC_RECEIVING) return answer(ST_ERROR, 2);
    this.xferDataCount++;
    if (this.syncKind === XFER_PREVIEW) {
      // Any order; the parts that do not come stay 0
      if (offset >= this.syncSize) return answer(ST_OFFSET, 2);
      const count = Math.min(14, this.syncSize - offset);
      this.syncBuffer.set(d.subarray(2, 2 + count), offset);
      put16(r, 0, offset + count);
      return answer(ST_OK, 2);
    }
    if (offset !== this.syncReceived) return answer(ST_OFFSET, 2);
    const count = Math.min(14, this.syncSize - this.syncReceived);
    this.syncBuffer.set(d.subarray(2, 2 + count), this.syncReceived);
    this.syncReceived += count;
    this._syncDataTimeout();
    this._syncDisplay();
    put16(r, 0, this.syncReceived);
    return answer(ST_OK, 2);
  };

  // END (0x0115) of a transfer of docs/DATA.md 5.1
  P._xferEnd = function (r, answer) {
    put16(r, 0, this.syncReceived);
    if (this.syncState !== SYNC_RECEIVING) return answer(ST_ERROR);
    const kind = this.syncKind;
    if (kind !== XFER_PREVIEW && this.syncReceived !== this.syncSize) return answer(ST_OFFSET, 2);
    const data = this.syncBuffer.slice(0, this.syncSize);
    const failed = (status, len) => {
      this._syncReset();
      this.syncFailed = true;
      this._syncDisplay();
      this.events.push("transfer failed kind " + kind);
      return answer(status, len);
    };
    if (crc32(data) !== this.syncCrc) return failed(ST_BAD_CRC);
    clearTimeout(this.syncDataTimer);
    if (kind === XFER_PREVIEW) {
      this.previewFrame = data;
      this.previewCount++;
      this.syncState = SYNC_PREVIEW;
      this.events.push("preview frame");
      this._syncDisplay();
      return answer(ST_OK);
    }
    if (this.options.flashFails) return failed(ST_ERROR);
    if (kind === XFER_START_PIC) {
      this.startPic = data;
      this.startPicShow = true;
      this.appWrites++;
      this.events.push("start picture saved");
      this.syncState = SYNC_INSTALLED;
      this._syncDisplay();
      return answer(ST_OK);
    }
    // XFER_RESTORE: first the walk of all sections, then the writes
    const reason = this._checkRestore(data);
    if (reason) { r[0] = reason; return failed(ST_BAD_PACKAGE, 1); }
    this.restores.push(data);
    const counts = this._applyRestore(data);
    r[0] = Math.min(255, counts.written);
    r[1] = Math.min(255, counts.skipped);
    this.events.push(`backup restored ${counts.written} written ${counts.skipped} skipped`);
    this.syncState = SYNC_INSTALLED;
    this._syncDisplay();
    return answer(ST_OK, 2);
  };

  P._checkRestore = function (b) {
    if (cText(b, 0, 4) !== "STBK") return BAD_BACKUP_MAGIC;
    if (b[4] !== 1) return BAD_BACKUP_VERSION;
    let p = 5;
    while (p < b.length) {
      if (p + 3 > b.length || p + 3 + get16(b, p + 1) > b.length) return BAD_BACKUP_SECTION;
      p += 3 + get16(b, p + 1);
    }
    return 0;
  };

  P._applyRestore = function (b) {
    let written = 0, skipped = 0;
    const ok = (n) => { written += n === undefined ? 1 : n; };
    const skip = () => { skipped++; };
    for (let p = 5; p < b.length; p += 3 + get16(b, p + 1)) {
      const type = b[p], length = get16(b, p + 1), s = b.subarray(p + 3, p + 3 + length);
      if (type === 1) {
        // VALUE: u8 key length, key, u32 value
        const n = s[0];
        if (length !== 1 + n + 4 || n < 1 || n > 15) { skip(); continue; }
        const key = cText(s, 1, n), value = get32(s, 1 + n);
        const entry = this.config.find((e) => e.key === key);
        // main/backup.c: never "init" and the device version; the battery type only for DIY
        const model = this.config.find((e) => e.key === "device_model").value;
        if (!entry || key === "init" || key === "device_model" || value > entry.max || (key === "battery_type" && model !== 1)) { skip(); continue; }
        entry.value = value;
        ok();
      } else if (type === 2) {
        // APP_ORDER: u8 count, count x (u32 id, u8 hidden)
        const count = s[0];
        const items = [];
        for (let i = 0; i < count && 1 + 5 * i + 5 <= length; i++) items.push({ id: get32(s, 1 + 5 * i), hidden: s[5 + 5 * i] === 1 });
        const ids = items.map((x) => x.id);
        const exact = length === 1 + 5 * count && count === this.appList.length && new Set(ids).size === count &&
          ids.every((id) => this.appList.some((e) => e.id === id));
        if (!exact) { skip(); continue; }
        this.appList = ids.map((id) => this.appList.find((e) => e.id === id));
        // The hidden flags: shown apps first, then hidden ones; the last shown app is never hidden
        for (const x of items) if (!x.hidden) this.appList.find((e) => e.id === x.id).flags &= ~FLAG_HIDDEN;
        for (const x of items) {
          if (!x.hidden) continue;
          const entry = this.appList.find((e) => e.id === x.id);
          if (entry.flags & FLAG_HIDDEN) continue;
          if (this._shownCount() <= 1) { skip(); continue; }
          entry.flags |= FLAG_HIDDEN;
        }
        this.appWrites++;
        ok();
      } else if (type === 3) {
        // APP_SETTINGS: u32 id, u8 count, count x u8
        const app = length >= 5 ? this.installed.get(get32(s, 0)) : null;
        if (!app || s[4] !== app.valueCounts.length || length !== 5 + s[4]) { skip(); continue; }
        for (let i = 0; i < s[4]; i++) {
          if (s[5 + i] >= app.valueCounts[i]) { skip(); continue; }
          app.settings[i] = s[5 + i];
          ok();
        }
      } else if (type === 4) {
        // APP_VALUES: u32 id, u8 count, count x i32
        const app = length >= 5 ? this.installed.get(get32(s, 0)) : null;
        if (!app || s[4] !== app.pkg[33] || length !== 5 + 4 * s[4]) { skip(); continue; }
        app.values = [];
        for (let i = 0; i < s[4]; i++) app.values.push(getI32(s, 5 + 4 * i));
        ok(s[4]);
      } else if (type === 5) {
        // DEVICE_NAME: u8 length (0..12), the name part
        const n = s[0];
        const name = length === 1 + n && n <= APPS_DEVICE_NAME_MAX ? cText(s, 1, n) : null;
        if (name === null || name.length !== n || (name !== "" && !isDeviceNameValid(name))) { skip(); continue; }
        this.deviceName = name;
        ok();
      } else if (type === 6) {
        // START_PIC: u8 show (0 delete, 1 on, 2 off), then 512 bytes (not for 0)
        const show = s[0];
        if (show === 0 && length === 1) { this.startPic = null; ok(); continue; }
        if ((show !== 1 && show !== 2) || length !== 1 + PICTURE_BYTES) { skip(); continue; }
        this.startPic = s.slice(1, 1 + PICTURE_BYTES);
        this.startPicShow = show === 1;
        ok();
      } else {
        skip();
      }
    }
    return { written, skipped };
  };

  /* The pixels of a frame (512 bytes: 4 pages of 128 bytes, bit 0 = the top pixel of the page) as
     32 rows of "#" and "." (for the tests) */
  FakeStatTrak.frameRows = function (frame) {
    const rows = [];
    for (let y = 0; y < 32; y++) {
      let row = "";
      for (let x = 0; x < 128; x++) row += (frame[(y >> 3) * 128 + x] >> (y & 7)) & 1 ? "#" : ".";
      rows.push(row);
    }
    return rows;
  };

  /* A package for the tests. The checks of the page and of this model accept it.
     format 1: 8 handlers, the data area starts at 304; format 2: 10 handlers, at 308. */
  FakeStatTrak.makeApp = function (options) {
    const o = Object.assign({ name: "TEST APP", id: 0x12345678, version: 1, permissions: 0, settings: [], size: 0, format: 1, saved: 0 }, options || {});
    const handlers = o.format >= 2 ? STAPP_HANDLERS_V2 : STAPP_HANDLERS_V1;
    const dataStart = STAPP_HANDLERS_OFFSET + 2 * handlers;
    let settingsBytes = [];
    for (const s of o.settings) {
      const block = new Uint8Array(14 + s.values.length * 8);
      putText(block, 0, s.name);
      block[12] = s.values.length;
      block[13] = s.default || 0;
      s.values.forEach((v, i) => putText(block, 14 + i * 8, v));
      settingsBytes = settingsBytes.concat(Array.from(block));
    }
    const size = Math.max(dataStart + settingsBytes.length + 1 + 4, o.size);
    const b = new Uint8Array(size);
    putText(b, 0, "STAP");
    b[4] = o.format;
    b[5] = 1;
    put16(b, 6, o.permissions);
    put32(b, 8, o.id);
    put16(b, 12, o.version);
    put16(b, 14, size);
    putText(b, 16, o.name);
    b[32] = o.saved;                      // global variables: the saved ones only
    b[33] = o.saved;                      // saved variable count
    b[34] = o.settings.length;
    put16(b, 38, dataStart);
    put16(b, 44, dataStart + settingsBytes.length);
    put16(b, 46, 1);
    for (let i = 48; i < 288; i++) b[i] = (i * 7) & 0xFF;      // a picture
    for (let i = 0; i < handlers; i++) put16(b, STAPP_HANDLERS_OFFSET + 2 * i, i === 1 ? 0 : 0xFFFF);
    b.set(settingsBytes, dataStart);
    // The code: one HALT, then 0 bytes up to the CRC
    put32(b, size - 4, crc32(b.subarray(0, size - 4)));
    return b;
  };

  FakeStatTrak.ADVERTISED_UUID = ADVERTISED_UUID;
  FakeStatTrak.crc32 = crc32;

  FakeStatTrak.crc16 = crc16;
  global.FakeStatTrak = FakeStatTrak;
})(typeof globalThis !== "undefined" ? globalThis : this);
