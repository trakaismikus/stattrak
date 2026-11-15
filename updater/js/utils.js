/* Utils */

// This function keeps calling "toTry" until promise resolves or has
// retried "max" number of times. First retry has a delay of "delay" seconds.
// "success" is called upon success.
async function exponentialBackoff(max, delay, toTry, success, fail) {
try {
    const result = await toTry();
    success(result);
} catch(error) {
    if (max === 0) {
    return fail();
    }
    time('Retrying in ' + delay + 's... (' + max + ' tries left)');
    setTimeout(function() {
    exponentialBackoff(--max, delay * 2, toTry, success, fail);
    }, delay * 1000);
}
}
  
function time(text) {
console.log('[' + new Date().toJSON().substr(11, 8) + '] ' + text);
}
  

// Precomputed CRC-16/CCITT table
const CRC16_CCITT_TABLE = (() => {
  let table = new Uint16Array(256);
  let polynomial = 0x1021;
  
  for (let i = 0; i < 256; i++) {
      let crc = i << 8;
      for (let j = 0; j < 8; j++) {
          if (crc & 0x8000) {
              crc = (crc << 1) ^ polynomial;
          } else {
              crc <<= 1;
          }
      }
      table[i] = crc & 0xFFFF;
  }
  return table;
})();

function crc16_ccitt(buffer) {
  let crc = 0x0000; // Initial value
  for (let i = 0; i < buffer.length; i++) {
      let byte = buffer[i];
      let index = ((crc >> 8) ^ byte) & 0xFF;
      crc = ((crc << 8) ^ CRC16_CCITT_TABLE[index]) & 0xFFFF;
  }
  return crc;
}
/* Shared parts of the web site. Use these, do not write the same markup again in a page or in an
 * added script (updater/README.md, "Shared parts").
 *   SiteUI.crumbs(["Settings", "Apps: SYNC", "Hold right button"]): the way on the device to a
 *   screen, in the boxes of css/site.css (.step1-instructions, .path-crumb, .path-sep).
 *   SiteUI.base(): the path from this page to the site folder ("" or "../", <body data-base>). */
const SiteUI = {
  esc: (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c])),
  crumbs(steps, lead) {
    const boxes = steps.map((s) => `<span class="path-crumb">${SiteUI.esc(s)}</span>`).join('<span class="path-sep" aria-hidden="true">→</span>');
    return `<p class="step1-instructions">${SiteUI.esc(lead === undefined ? "Turn on StatTrak and navigate to" : lead)}&nbsp; ${boxes}</p>`;
  },
  base: () => (typeof document !== "undefined" && document.body && document.body.dataset.base) || "",
};

/* The site tells other scripts of the page about the StatTrak that it talks to. One browser event,
 * "stattrak:device", with detail = { event, serial, fw, previousFw, name }:
 *   event: "connected" | "flashed" | "disconnected"
 *   serial: the 12 hex digits of the device id (the same text as Settings > Serial on StatTrak)
 *   fw: the firmware version; previousFw: the version before a firmware update ("flashed" only)
 *   name: the part of the Bluetooth name after "StatTrak | " ("" = the standard name "StatTrak")
 * This page sends nothing over the network for it: a page that adds its own scripts can listen.
 * SiteEvents.device is the last one (null after "disconnected"). */
const SiteEvents = {
  device: null,
  emitDevice(detail) {
    const d = Object.assign({ serial: "", fw: "", previousFw: "", name: "" }, detail);
    SiteEvents.device = d.event === "disconnected" ? null : d;
    if (typeof window !== "undefined" && typeof window.dispatchEvent === "function" && typeof CustomEvent === "function") {
      window.dispatchEvent(new CustomEvent("stattrak:device", { detail: d }));
    }
  },
};
