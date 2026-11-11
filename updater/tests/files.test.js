/* Tests of the files of the page: fonts and other sites. Run: node --test "tests/*.test.js" */

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { ROOT } = require("./helpers.js");

const read = (name) => fs.readFileSync(path.join(ROOT, name), "utf8");
const cssFiles = fs.readdirSync(path.join(ROOT, "css")).map((name) => "css/" + name);

test("the page loads no style, script, font or picture from an other site", () => {
  for (const name of cssFiles) {
    const css = read(name);
    assert.doesNotMatch(css, /@import/, name + " imports an other style file");
    assert.doesNotMatch(css, /url\(\s*['"]?(https?:)?\/\//, name + " loads a file from an other site");
  }
  const html = read("index.html");
  // Links for the user (<a href>) are not loaded by the page
  const loaded = html.match(/<(script|link|img|video|source|iframe)\b[^>]*>/g) || [];
  for (const tag of loaded) {
    assert.doesNotMatch(tag, /(src|href)\s*=\s*['"](https?:)?\/\//, "loaded from an other site: " + tag);
  }
});

test("each font file that the styles name is in the fonts folder", () => {
  let count = 0;
  for (const name of cssFiles) {
    for (const match of read(name).matchAll(/url\(\s*['"]?([^'")]+)['"]?\s*\)/g)) {
      const file = path.join(ROOT, "css", match[1]);
      assert.ok(fs.existsSync(file), name + " names a file that is not there: " + match[1]);
      count++;
    }
  }
  assert.ok(count >= 7, "the font declarations are missing");
});

test("the fonts folder has only the free fonts and their license texts", () => {
  // A public page sends its font files to each visitor: a font with no license for that must not be here
  const fonts = fs.readdirSync(path.join(ROOT, "fonts")).sort();
  assert.deepEqual(fonts, [
    "BarlowCondensed-400.woff2",
    "BarlowCondensed-500.woff2",
    "BarlowCondensed-700.woff2",
    "BarlowCondensed-800.woff2",
    "BarlowCondensed-800i.woff2",
    "BarlowCondensed-900i.woff2",
    "JetBrainsMono.woff2",
    "OFL-BarlowCondensed.txt",
    "OFL-JetBrainsMono.txt",
    "README.md",
  ]);
});

/* The page and the firmware are in one repository. The numbers that they use to talk to
   each other must be the same on the two sides. */
test("the Bluetooth numbers of the page are the numbers of the firmware", { skip: !fs.existsSync(path.join(ROOT, "..", "components", "ble_ota")) && "the firmware source is not here" }, () => {
  const firmware = (name) => fs.readFileSync(path.join(ROOT, "..", "components", "ble_ota", name), "utf8");
  const page = read("js/ble.js");
  const pageNumber = (name) => {
    const match = page.match(new RegExp("static " + name + "\\s*=\\s*(0x[0-9A-Fa-f]+|\\d+);"));
    assert.ok(match, "js/ble.js has no " + name);
    return Number(match[1]);
  };
  const firmwareNumber = (source, pattern, name) => {
    const match = source.match(pattern);
    assert.ok(match, "the firmware has no " + name);
    return Number(match[1]);
  };

  const bleOta = firmware("ble_ota.c");
  const header = firmware("ble_ota.h");
  const ota = firmware("ota.c");
  const pairs = [
    ["OTA_SERVICE_UUID", bleOta, /#define BLE_OTA_SERVICE_UUID\s+(0x[0-9A-Fa-f]+)/],
    ["RECV_FW_CHAR_UUID", bleOta, /#define RECV_FW_UUID\s+(0x[0-9A-Fa-f]+)/],
    ["OTA_BAR_CHAR_UUID", bleOta, /#define OTA_BAR_UUID\s+(0x[0-9A-Fa-f]+)/],
    ["COMMAND_CHAR_UUID", bleOta, /#define COMMAND_UUID\s+(0x[0-9A-Fa-f]+)/],
    ["CUSTOM_CHAR_UUID", bleOta, /#define CUSTOMER_UUID\s+(0x[0-9A-Fa-f]+)/],
    ["CMD_ID__START", bleOta, /CMD_ID__START\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["CMD_ID__STOP", bleOta, /CMD_ID__STOP\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["CMD_ID__ACK", bleOta, /CMD_ID__ACK\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["ACK_STATUS__SUCCESS", bleOta, /ACK_STATUS__SUCCESS\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["ACK_STATUS__CRC_ERR", bleOta, /ACK_STATUS__CRC_ERR\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["ACK_STATUS__SECTOR_IDX_ERR", bleOta, /ACK_STATUS__SECTOR_IDX_ERR\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["ACK_STATUS__PAYLOAD_LEN_ERR", bleOta, /ACK_STATUS__PAYLOAD_LEN_ERR\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["OTA_BLOCK_SIZE", header, /#define OTA_BLOCK_SIZE\s+\((\d+)\)/],
    ["CUSTOM_ID__GET_VERSION", ota, /CUSTOM_ID__GET_VERSION\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["CUSTOM_ID__GET_UID", ota, /CUSTOM_ID__GET_UID\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["CUSTOM_ID__ACCEPT_FW", ota, /CUSTOM_ID__ACCEPT_FW\s*=\s*(0x[0-9A-Fa-f]+)/],
    ["CUSTOM_ID__REJECT_FW", ota, /CUSTOM_ID__REJECT_FW\s*=\s*(0x[0-9A-Fa-f]+)/],
  ];
  for (const [name, source, pattern] of pairs) {
    assert.equal(pageNumber(name), firmwareNumber(source, pattern, name), name);
  }
});
