/* A stand-in for a StatTrak device, and for the Web Bluetooth API of the browser.
 *
 * The tests use it in place of a real device. It runs in Node.js and in a browser.
 * The packet code is a line-by-line port of the firmware (repository `stattrak`):
 *   components/ble_ota/ble_ota.c   _writeCommand, _writeFw, _writeCustom, _notify
 *   components/ble_ota/ota.c       the update task, _customCallback
 *   main/main.c                    restart after a good transfer, "Verifying…" at the start
 * `legacy: true` gives the packet code of firmware 1.0.1 (no check of the sector CRC,
 * and the "accept" command changes the display also when no firmware waits).
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
      }, options || {});

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

      if (this.options.waitsForConfirmation) {
        this.oldVersion = "1.0.1";
        this.pendingVerify = true;
        this.screen = "accept";
        this.display = "Verifying...";
      }

      this._resetUpdateCode();
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
      return this.screen === "update" || this.screen === "accept";
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
        requestDevice: async function () {
          device.events.push("requestDevice");
          if (!device.advertising()) {
            const error = new Error("User cancelled the requestDevice() chooser.");
            error.name = "NotFoundError";
            throw error;
          }
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

  FakeStatTrak.crc16 = crc16;
  global.FakeStatTrak = FakeStatTrak;
})(typeof globalThis !== "undefined" ? globalThis : this);
