/* StatTrak firmware file checks. No page and no Bluetooth here, thus the tests
 * can use this file directly.
 *
 * The device does the full check of a firmware (all data, checksum) after the
 * transfer, and it refuses a bad file. The checks here find a wrong file in a
 * second, not after some minutes of transfer.
 * The same rules are in `tools/build.py --check-file` of the firmware repository.
 */

const FirmwareFile = (() => {
  const IMAGE_MAGIC = 0xE9;            // first byte of each ESP firmware file
  const CHIP_ID_ESP32C2 = 0x000C;      // the chip of the StatTrak
  const APP_MAGIC = 0xABCD5432;        // start of the application description
  const APP_DESC_OFFSET = 0x20;
  const VERSION_OFFSET = APP_DESC_OFFSET + 16;
  const PROJECT_OFFSET = APP_DESC_OFFSET + 48;
  const TEXT_LENGTH = 32;
  const MIN_SIZE = APP_DESC_OFFSET + 256;   // the header and the full application description
  const PROJECT_NAME = "stattrak";
  const MAX_SIZE = 1024 * 1024;        // the smallest firmware slot of the device

  function text(bytes, offset) {
    let out = "";
    for (let i = offset; i < offset + TEXT_LENGTH && i < bytes.length && bytes[i] !== 0; i++) {
      out += String.fromCharCode(bytes[i]);
    }
    return out;
  }

  /* Returns { ok, problem, version, project }. `problem` is a sentence for the user. */
  function inspect(bytes) {
    const result = { ok: false, problem: "", version: "", project: "" };
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

    if (bytes.length < MIN_SIZE) {
      result.problem = "The file is too small to be a firmware.";
      return result;
    }
    if (bytes[0] !== IMAGE_MAGIC) {
      result.problem = "The file does not start as a firmware file.";
      return result;
    }
    const chipId = view.getUint16(12, true);
    if (chipId !== CHIP_ID_ESP32C2) {
      result.problem = "The file is for an other chip, not for the StatTrak.";
      return result;
    }
    if (view.getUint32(APP_DESC_OFFSET, true) !== APP_MAGIC) {
      result.problem = "The file is not an application. It can be a bootloader or a full flash picture.";
      return result;
    }

    result.version = text(bytes, VERSION_OFFSET);
    result.project = text(bytes, PROJECT_OFFSET);

    if (result.project !== PROJECT_NAME) {
      result.problem = `The project name in the file is "${result.project}", not "${PROJECT_NAME}".`;
      return result;
    }
    if (bytes.length > MAX_SIZE) {
      result.problem = `The file has ${bytes.length} bytes. The largest firmware is ${MAX_SIZE} bytes.`;
      return result;
    }

    result.ok = true;
    return result;
  }

  /* SHA-256 of the file as hex text, or null where the browser cannot calculate it. */
  async function sha256Hex(bytes) {
    const subtle = (typeof crypto !== "undefined") ? crypto.subtle : null;
    if (!subtle) return null;
    const digest = new Uint8Array(await subtle.digest("SHA-256", bytes));
    return Array.from(digest).map((b) => b.toString(16).padStart(2, "0")).join("");
  }

  /* Is this file one of the official release files?
     `assets` is a list of { release, digest } from the GitHub releases, where
     digest is "sha256:<hex>" or null (GitHub does not have it for old files).
     Returns { status: "official", release } | { status: "unofficial" } | { status: "unknown" }. */
  function matchRelease(hashHex, assets) {
    if (!hashHex || !Array.isArray(assets) || assets.length === 0) return { status: "unknown" };

    let allHaveDigest = true;
    for (const asset of assets) {
      if (!asset.digest) {
        allHaveDigest = false;
        continue;
      }
      if (asset.digest.toLowerCase() === "sha256:" + hashHex) {
        return { status: "official", release: asset.release };
      }
    }
    return { status: allHaveDigest ? "unofficial" : "unknown" };
  }

  return { inspect, sha256Hex, matchRelease, MAX_SIZE };
})();
