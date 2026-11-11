/* Tests of the Bluetooth code (js/ble.js) and the file checks (js/firmware.js)
 * against the stand-in device (tests/fake-device.js). Run: node --test tests/
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const { loadPageCode, once, waitFor, pair } = require("./helpers.js");

const list = (bytes) => Array.from(bytes);

test("the CRC of the page is the CRC of the firmware", () => {
  const page = loadPageCode();
  const text = new TextEncoder().encode("123456789");
  assert.equal(page.crc16_ccitt(text), 0x31C3); // CRC-16/XMODEM
  const data = page.FakeStatTrak.makeFirmware({ size: 5000 });
  assert.equal(page.crc16_ccitt(data), page.FakeStatTrak.crc16(data));
});

test("the page reads the version and the ID of the device", async () => {
  const page = loadPageCode({ version: "v1.0.1-added-rcr123a" });
  const ota = await pair(page);
  assert.equal(await ota.getVersion(), "v1.0.1-added-"); // the device sends 13 characters
  assert.deepEqual(list(await ota.getUid()), [0x34, 0xB7, 0xDA, 0x01, 0x02, 0x03]);
  ota.disconnect();
});

for (const legacy of [false, true]) {
  const name = legacy ? "firmware 1.0.1" : "the new firmware";

  for (const size of [1, 4095, 4096, 4097, 10000, 3 * 4096]) {
    test(`a file of ${size} bytes arrives complete on ${name}`, async () => {
      const page = loadPageCode({ legacy, restartMs: 100000 });
      const ota = await pair(page);
      const firmware = page.FakeStatTrak.makeFirmware({ size: Math.max(size, 0x70) });
      const file = firmware.slice(0, size);
      const progress = [];
      ota.addEventListener("progress", (event) => progress.push(event.detail.progress));

      assert.equal(await ota.update(file), true);
      assert.deepEqual(list(page.device.received), list(file));
      assert.equal(progress[progress.length - 1], 100);
      assert.equal(page.device.startCount, 1);
      assert.equal(page.device.stopCount, 0);
      ota.disconnect();
    });
  }
}

test("a sector that arrives damaged is sent again", async () => {
  for (const times of [1, 2]) {
    const page = loadPageCode({ damageSector: { 1: times }, restartMs: 100000 });
    const ota = await pair(page);
    const file = page.FakeStatTrak.makeFirmware({ size: 10000 });

    assert.equal(await ota.update(file), true);
    assert.deepEqual(list(page.device.received), list(file));
    assert.equal(page.device.events.filter((e) => e === "damaged sector 1").length, times);
    ota.disconnect();
  }
});

test("a sector that is damaged 3 times stops the update, and the device gets 'stop'", async () => {
  const page = loadPageCode({ damageSector: { 1: 3 } });
  const ota = await pair(page);
  const file = page.FakeStatTrak.makeFirmware({ size: 10000 });

  assert.equal(await ota.update(file), false);
  assert.equal(page.device.stopCount, 1);
  assert.equal(page.device.display, "Waiting...");
  assert.equal(page.device.received, null);

  // The device is ready for a new transfer
  assert.equal(await ota.update(file), true);
  assert.deepEqual(list(page.device.received), list(file));
  ota.disconnect();
});

test("an answer that does not arrive stops the update, and the device gets 'stop'", async () => {
  const page = loadPageCode({ loseAckOfSector: 0 });
  const ota = await pair(page);
  const file = page.FakeStatTrak.makeFirmware({ size: 10000 });

  await assert.rejects(ota.update(file), /Timeout/);
  assert.equal(page.device.stopCount, 1);
  assert.equal(page.device.display, "Waiting...");
  ota.disconnect();
});

test("a start command with no answer stops the update, and the device gets 'stop'", async () => {
  const page = loadPageCode({ loseStartReply: true });
  const ota = await pair(page);
  const file = page.FakeStatTrak.makeFirmware({ size: 10000 });

  await assert.rejects(ota.update(file), /Timeout/);
  assert.equal(page.device.startCount, 1);
  assert.equal(page.device.stopCount, 1);
  assert.equal(page.device.display, "Waiting...");
  ota.disconnect();
});

test("a file that is too large for the device is refused at the start", async () => {
  const page = loadPageCode({ slotSize: 8000 });
  const ota = await pair(page);
  const file = page.FakeStatTrak.makeFirmware({ size: 10000 });

  assert.equal(await ota.update(file), false);
  assert.equal(page.device.received, null);
  ota.disconnect();
});

test("a damaged file: all sectors are accepted, then the device shows Error and does not restart", async () => {
  // This is why "all data sent" is not "update complete" (see js/main.js)
  const page = loadPageCode();
  const ota = await pair(page);
  const file = page.FakeStatTrak.makeFirmware({ size: 10000, valid: false });

  assert.equal(await ota.update(file), true);
  await new Promise((resolve) => setTimeout(resolve, 100));
  assert.equal(page.device.display, "Error");
  assert.equal(ota.isConnected(), true);
  assert.equal(await ota.acceptFirmware(true), false);
  ota.disconnect();
});

test("after a good transfer the device restarts, the page connects again and confirms", async () => {
  const page = loadPageCode({ version: "1.0.1" });
  const ota = await pair(page);
  const file = page.FakeStatTrak.makeFirmware({ size: 10000, version: "2.0.0" });

  assert.equal(await ota.update(file), true);
  await once(ota, "bleDisconnected");
  await once(ota, "bleConnected");
  assert.equal(page.device.display, "Verifying...");
  assert.equal(await ota.getVersion(), "2.0.0");

  assert.equal(await ota.acceptFirmware(true), true);
  assert.equal(page.device.display, "Complete v2.0.0");
  assert.equal(await ota.acceptFirmware(true), false); // nothing waits now
  ota.disconnect();
});

test("a firmware that is not confirmed is lost at the next start of the device", async () => {
  const page = loadPageCode({ version: "1.0.1" });
  const ota = await pair(page);
  assert.equal(await ota.update(page.FakeStatTrak.makeFirmware({ version: "2.0.0" })), true);
  await once(ota, "bleDisconnected");
  await once(ota, "bleConnected");
  ota.disconnect();

  page.device.powerCycle();
  assert.equal(page.device.version, "1.0.1");
  assert.ok(page.device.events.includes("rollback"));
});

test("the device that switches off at the restart is found when a person switches it on", async () => {
  const page = loadPageCode({ staysOffAfterRestart: true });
  page.BleOta.RECONNECT_GIVE_UP_MS = 5000;
  const ota = await pair(page);
  assert.equal(await ota.update(page.FakeStatTrak.makeFirmware({ version: "2.0.0" })), true);
  await once(ota, "bleDisconnected");
  await new Promise((resolve) => setTimeout(resolve, 150));
  assert.equal(ota.isConnected(), false);

  page.device.powerOn();
  await once(ota, "bleConnected");
  assert.equal(await ota.acceptFirmware(true), true);
  ota.disconnect();
});

test("the page stops its connection tries after some time and says so", async () => {
  const page = loadPageCode();
  const ota = await pair(page);
  page.device.pressOk(); // no effect here
  page.device.screen = "counter"; // the user left the update screen: Bluetooth is off
  const gone = once(ota, "bleDisconnected");
  page.device.loseConnection();
  await gone;
  await once(ota, "bleConnectFailed");
  assert.equal(ota.isConnected(), false);
});

test("the time limit of the connection tries is real time, also when each try is slow", async () => {
  const page = loadPageCode({ connectFailMs: 100 });
  page.BleOta.RECONNECT_GIVE_UP_MS = 50;
  const ota = await pair(page);
  page.device.screen = "counter";
  const gone = once(ota, "bleDisconnected");
  page.device.loseConnection();
  await gone;
  const start = Date.now();
  await once(ota, "bleConnectFailed");
  assert.ok(Date.now() - start < 400, "the tries continued for " + (Date.now() - start) + " ms");
});

test("after disconnect() the page does not connect again", async () => {
  const page = loadPageCode();
  const ota = await pair(page);
  const tries = () => page.device.events.filter((e) => e === "connect try").length;
  const before = tries();
  const gone = once(ota, "bleDisconnected");
  ota.disconnect();
  await gone;
  await new Promise((resolve) => setTimeout(resolve, 100));
  assert.equal(tries(), before);
  assert.equal(ota.isConnected(), false);
});

test("firmware 1.0.1 shows 'Accepted' for a confirmation with no firmware that waits", async () => {
  // This is why the page does not send a confirmation at each connection
  const page = loadPageCode({ legacy: true });
  const ota = await pair(page);
  assert.equal(await ota.acceptFirmware(true), false);
  assert.equal(page.device.display, "Accepted");
  ota.disconnect();
});

test("an answer with a wrong CRC is not read as the answer 'no'", async () => {
  const page = loadPageCode();
  const ota = await pair(page);
  const custom = page.device.link.characteristics[0x8023];
  const dispatch = custom.dispatchEvent.bind(custom);
  custom.dispatchEvent = (event) => {
    const bytes = new Uint8Array(custom.value.buffer);
    bytes[19] ^= 0xFF;
    return dispatch(event);
  };
  await assert.rejects(ota.acceptFirmware(true), /No valid answer/);
  ota.disconnect();
});

test("an answer to an other command is not used", async () => {
  const page = loadPageCode({ waitsForConfirmation: true });
  const ota = await pair(page);
  const custom = page.device.link.characteristics[0x8023];
  const dispatch = custom.dispatchEvent.bind(custom);
  custom.dispatchEvent = (event) => {
    // The answer says that it is for the command "get version", with a correct CRC
    const bytes = new Uint8Array(custom.value.buffer);
    bytes[2] = 1;
    const crc = page.FakeStatTrak.crc16(bytes.subarray(0, 18));
    bytes[18] = crc & 0xFF;
    bytes[19] = crc >> 8;
    return dispatch(event);
  };
  await assert.rejects(ota.acceptFirmware(true), /No valid answer/);
  ota.disconnect();
});

/* ---- File checks ------------------------------------------------------------------- */

test("a StatTrak firmware file is accepted, and its version is read", () => {
  const { FirmwareFile, FakeStatTrak } = loadPageCode();
  const info = FirmwareFile.inspect(FakeStatTrak.makeFirmware({ version: "v1.0.1-added-rcr123a" }));
  assert.equal(info.ok, true);
  assert.equal(info.version, "v1.0.1-added-rcr123a");
  assert.equal(info.project, "stattrak");
});

test("wrong files are refused before the transfer", () => {
  const { FirmwareFile, FakeStatTrak } = loadPageCode();
  const make = (options) => FakeStatTrak.makeFirmware(options);
  const refused = (bytes, text) => {
    const info = FirmwareFile.inspect(bytes);
    assert.equal(info.ok, false);
    assert.match(info.problem, text);
  };

  refused(make().slice(0, 10), /too small/);
  refused(make().slice(0, 0x11F), /too small/); // the application description is not complete
  assert.equal(FirmwareFile.inspect(make({ size: 0x120 })).ok, true);
  const text = make();
  text[0] = 0x7B;
  refused(text, /does not start as a firmware/);
  refused(make({ chipId: 0x0005 }), /other chip/);
  const bootloader = make();
  bootloader[0x20] = 0;
  refused(bootloader, /not an application/);
  refused(make({ project: "blink" }), /project name/);
  refused(make({ size: FirmwareFile.MAX_SIZE + 1 }), /largest firmware/);
  assert.equal(FirmwareFile.inspect(make({ size: FirmwareFile.MAX_SIZE })).ok, true);
});

test("the hash of a file tells if it is an official release", async () => {
  const { FirmwareFile } = loadPageCode();
  const hash = await FirmwareFile.sha256Hex(new TextEncoder().encode("abc"));
  assert.equal(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  const official = [{ release: "v1.0.0", digest: "sha256:00" }, { release: "v1.0.1", digest: "SHA256:" + hash.toUpperCase() }];
  assert.deepEqual({ ...FirmwareFile.matchRelease(hash, official) }, { status: "official", release: "v1.0.1" });
  assert.equal(FirmwareFile.matchRelease(hash, [{ release: "v1.0.0", digest: "sha256:00" }]).status, "unofficial");
  // GitHub has no hash for old files: the page cannot know
  assert.equal(FirmwareFile.matchRelease(hash, [{ release: "v0.9", digest: null }]).status, "unknown");
  assert.equal(FirmwareFile.matchRelease(hash, []).status, "unknown");
  assert.equal(FirmwareFile.matchRelease(null, official).status, "unknown");
});
