/* Helpers for the tests that run the page code in Node.js (no browser). */

const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const ROOT = path.join(__dirname, "..");
const quiet = { log() {}, warn() {}, error() {}, info() {} };

/* Loads the page code (js/utils.js, js/ble.js, js/firmware.js, js/apps-protocol.js) and a stand-in device
   into one script context, as a browser does with <script> tags. */
function loadPageCode(deviceOptions) {
  const context = vm.createContext({
    console: process.env.DEBUG ? console : quiet,
    setTimeout,
    clearTimeout,
    EventTarget,
    Event,
    CustomEvent,
    TextDecoder,
    crypto: globalThis.crypto,
  });
  for (const file of ["tests/fake-device.js", "js/utils.js", "js/ble.js", "js/firmware.js", "js/apps-protocol.js"]) {
    vm.runInContext(fs.readFileSync(path.join(ROOT, file), "utf8"), context, { filename: file });
  }

  context.deviceOptions = Object.assign(
    { restartMs: 30, bootMs: 20, connectMs: 1, connectFailMs: 10 },
    deviceOptions || {}
  );
  const device = vm.runInContext("var device = new FakeStatTrak(deviceOptions); device", context);
  context.navigator = { bluetooth: device.bluetooth() };

  const BleOta = vm.runInContext("BleOta", context);
  // Short times, thus the tests are fast
  BleOta.INDICATION_TIMEOUT_MS = 200;
  BleOta.RECONNECT_FIRST_MS = 5;
  BleOta.RECONNECT_MAX_MS = 20;
  BleOta.RECONNECT_GIVE_UP_MS = 200;

  return {
    context,
    device,
    BleOta,
    FakeStatTrak: vm.runInContext("FakeStatTrak", context),
    FirmwareFile: vm.runInContext("FirmwareFile", context),
    crc16_ccitt: vm.runInContext("crc16_ccitt", context),
    StappFile: vm.runInContext("StappFile", context),
    StatTrakApps: vm.runInContext("StatTrakApps", context),
  };
}

function once(target, name, timeoutMs = 3000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("no event " + name)), timeoutMs);
    target.addEventListener(name, (event) => { clearTimeout(timer); resolve(event); }, { once: true });
  });
}

async function waitFor(condition, what, timeoutMs = 3000) {
  const start = Date.now();
  while (!condition()) {
    if (Date.now() - start > timeoutMs) throw new Error("timeout: " + what);
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
}

/* Pairs with the stand-in device and waits for the connection. */
async function pair(page) {
  const ota = await page.BleOta.create();
  await once(ota, "bleConnected");
  return ota;
}

module.exports = { ROOT, loadPageCode, once, waitFor, pair };
