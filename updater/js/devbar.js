/* The StatTrak button in the header (index.html): which StatTrak this page talks to.
 *
 * - Connected in "Apps: SYNC" (My device, js/apps.js): the name of the device and the number of
 *   app updates. A click opens a panel: serial, firmware, app storage, the updates (Update), My
 *   device, Disconnect.
 * - Connected in the firmware update (js/main.js): "StatTrak", and in the panel the serial, the
 *   firmware, a link to the firmware tab and Disconnect.
 * - Not connected: "Connect". The panel shows the way to "Apps: SYNC" and a Connect button.
 *
 * The same button on every page (Get started, Designer too): they load the connection code of My
 * device. Only the Firmware tab has its own connection (the device in the update mode).
 * The console (js/main.js) opens from the panel when a device is connected.
 */

const DevBar = (() => {
  let open = false;

  const $ = (selector) => document.querySelector(selector);
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const kb = (bytes) => (Math.round((bytes / 1024) * 10) / 10).toFixed(1).replace(/\.0$/, "") + " KB";
  const icon = (d, size) => `<svg width="${size || 16}" height="${size || 16}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${d}</svg>`;
  const BT = '<path d="m7 7 10 10-5 5V2l5 5L7 17"/>';
  const CHEV = '<polyline points="6 9 12 15 18 9"/>';
  const ARROW = '<line x1="5" y1="12" x2="19" y2="12"/><polyline points="12 5 19 12 12 19"/>';

  const apps = () => (typeof AppsTab !== "undefined" ? AppsTab.api : null);
  // A tab of index.html: "#name" there, else the address of index.html (the other pages)
  const page = (name) => (document.getElementById("appsView") ? "#" + name : SiteUI.base() + "index.html#" + name);
  const fwState = () => (typeof state !== "undefined" ? state : null);

  /* What the button shows: { kind: sync | fw | off, ... } */
  function now() {
    const a = apps();
    if (a && a.state.conn === "on") {
      const s = a.state;
      return {
        kind: "sync", name: a.namePart() || "StatTrak", serial: s.serial, fw: s.fw,
        used: a.usedBytes(), max: a.maxBytes, updates: a.updates(), inst: s.inst, busy: a.busy(), err: s.err,
      };
    }
    const f = fwState();
    if (f && f.device) return { kind: "fw", name: "StatTrak", serial: f.device.id, fw: f.device.fw, busy: f.flashState === "flashing" || f.flashState === "verifying" };
    return { kind: "off", connecting: !!(a && a.state.conn === "connecting"), locked: !!(a && a.firmwareBusy()),
      oldfw: !!(a && a.state.conn === "oldfw") ? a.state.fw || "this StatTrak" : "", banner: a && a.state.banner };
  }

  function vButton(d) {
    if (d.kind === "off") {
      return `<button class="devbtn devbtn--off" type="button" data-dev="toggle" aria-expanded="${open}" aria-haspopup="dialog">` +
        `${icon(BT)}<span>${d.connecting ? "Connecting…" : "Connect"}</span>${icon(CHEV, 14)}</button>`;
    }
    const waiting = d.inst && d.inst.phase === "ask";
    const count = d.updates ? d.updates.length : 0;
    const label = `StatTrak ${d.name} connected` + (count ? `, ${count} update${count > 1 ? "s" : ""}` : "");
    return `<button class="devbtn${waiting ? " devbtn--wait" : ""}" type="button" data-dev="toggle" aria-expanded="${open}" aria-haspopup="dialog" aria-label="${esc(label)}">` +
      `<span class="devbtn__dot"></span><span class="devbtn__name">${waiting ? "Answer on StatTrak" : esc(d.name)}</span>` +
      (count ? `<span class="devbtn__count">${count}</span>` : "") + `${icon(CHEV, 14)}</button>`;
  }

  function vPanel(d) {
    if (d.kind === "off") {
      // The messages of My device (old firmware, connection lost, not in Apps: SYNC): here too,
      // because the other pages have no My device view
      if (d.oldfw) {
        return '<div class="devmenu" role="dialog" aria-label="Update the firmware first"><div class="devmenu__head"><span class="devmenu__title">Update the firmware first</span></div>' +
          `<p class="devmenu__msg">Firmware ${esc(d.oldfw)} has no apps.</p>` +
          `<a class="btn btn--primary" href="${page("firmware")}" data-dev="close">Update firmware</a></div>`;
      }
      return '<div class="devmenu" role="dialog" aria-label="Connect StatTrak"><div class="devmenu__head"><span class="devmenu__title">Connect StatTrak</span></div>' +
        (d.banner ? `<p class="devmenu__msg devmenu__msg--${esc(d.banner.kind)}"><b>${esc(d.banner.title)}</b>${d.banner.body ? " " + esc(d.banner.body) : ""}</p>` : "") +
        SiteUI.crumbs(["Settings", "Apps: SYNC", "Hold right button"]) +
        `<button class="btn btn--primary" type="button" data-dev="connect"${d.connecting || d.locked ? " disabled" : ""}>${icon(BT)}Connect</button>` +
        (d.locked ? '<p class="devmenu__note">A firmware update runs. Wait until it is complete.</p>' : '<p class="devmenu__note">For a firmware update, open the Firmware tab.</p>') + "</div>";
    }
    let h = `<div class="devmenu" role="dialog" aria-label="Your StatTrak"><div class="devmenu__head"><span class="devbtn__dot"></span>` +
      `<span class="devmenu__title">${esc(d.name)}</span><span class="devmenu__state">${d.kind === "fw" ? "Update mode" : "Connected"}</span></div>` +
      '<div class="devmenu__sec">' +
      `<div class="devmenu__row"><span class="devmenu__k">Serial</span><span class="devmenu__v" data-dev="serial">${esc(d.serial || "—")}</span></div>` +
      `<div class="devmenu__row"><span class="devmenu__k">Firmware</span><span class="devmenu__v">${esc(d.fw || "—")}</span></div>`;
    if (d.kind === "sync") {
      const pct = Math.min(100, (d.used / d.max) * 100);
      h += `<div class="devmenu__row"><span class="devmenu__k">Apps</span><span class="devmenu__v devmenu__store">${kb(d.used)} / ${kb(d.max)}` +
        `<span class="devmenu__bar"><span style="width:${pct}%"></span></span></span></div>`;
    }
    h += "</div>";
    if (d.kind === "sync" && d.updates.length) {
      h += `<div class="devmenu__sec"><div class="devmenu__upd">${d.updates.length} update${d.updates.length > 1 ? "s" : ""}</div>` +
        d.updates.map((u) => {
          const mine = d.inst && d.inst.key === u.item.key;
          const room = apps().roomFor(u.item);
          const act = mine ? `<span class="devmenu__wait">${d.inst.phase === "transfer" ? Math.round(d.inst.pct) + "%" : "Answer on StatTrak"}</span>`
            : room ? `<span class="devmenu__v">${esc(room.text)}</span>`
            : `<button class="btn btn--primary btn--sm" type="button" data-dev="update" data-id="${u.app.id}"${d.busy ? " disabled" : ""}>Update</button>`;
          const err = d.err && d.err.key === u.item.key ? `<div class="devmenu__msg devmenu__msg--error">${esc(d.err.text)}</div>` : "";
          return `<div class="devmenu__row"><span><b>${esc(u.app.name)}</b> <span class="devmenu__v">v${u.app.version} → v${u.item.version}</span></span>${act}</div>` + err;
        }).join("") + "</div>";
    }
    if (typeof SiteConsole !== "undefined") {
      h += `<button class="btn btn--ghost btn--sm devmenu__console" type="button" data-dev="console">Console <span class="devmenu__count">${SiteConsole.count()}</span></button>`;
    }
    h += '<div class="devmenu__btns">' +
      (d.kind === "sync" ? `<a class="btn btn--ghost btn--sm" href="${page("device")}" data-dev="close">My device ${icon(ARROW, 14)}</a>`
        : `<a class="btn btn--ghost btn--sm" href="${page("firmware")}" data-dev="close">Firmware ${icon(ARROW, 14)}</a>`) +
      `<button class="btn btn--ghost btn--sm" type="button" data-dev="disconnect"${d.busy ? " disabled" : ""}>Disconnect</button></div></div>`;
    return h;
  }

  function render() {
    const root = $("#devBar");
    if (!root) return;
    const d = now();
    const focused = document.activeElement && root.contains(document.activeElement) ? document.activeElement.dataset.dev : null;
    root.innerHTML = vButton(d) + (open ? vPanel(d) : "");
    if (focused) {
      const again = root.querySelector(`[data-dev="${focused}"]`);
      if (again) again.focus();
    }
  }

  function setOpen(value) {
    open = value;
    render();
  }

  function onClick(event) {
    const root = $("#devBar");
    if (!root) return;
    if (!root.contains(event.target)) {
      if (open) setOpen(false);
      return;
    }
    const b = event.target.closest("[data-dev]");
    if (!b || b.disabled) return;
    const what = b.dataset.dev;
    const a = apps();
    if (what === "toggle") return setOpen(!open);
    if (what === "close") return setOpen(false);
    if (what === "console" && typeof SiteConsole !== "undefined") { setOpen(false); SiteConsole.open(); return; }
    if (what === "connect" && a && !a.firmwareBusy()) {
      // The connection of My device: from the firmware tab, open My device first
      if (a.state.tab === "firmware") location.hash = "#device";
      a.connect();
      return setOpen(false);
    }
    if (what === "update" && a) {
      const u = a.updates().find((x) => x.app.id === Number(b.dataset.id));
      if (u) a.install(u.item);
      return;
    }
    if (what === "disconnect") {
      setOpen(false);
      const d = now();
      if (d.kind === "sync" && a) a.disconnect();
      else if (d.kind === "fw") {
        const button = document.getElementById("disconnectBtn");
        if (button) button.click();
      }
    }
  }

  function init() {
    if (!$("#devBar")) return;
    document.addEventListener("click", onClick);
    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && open) {
        setOpen(false);
        const b = $('#devBar [data-dev="toggle"]');
        if (b) b.focus();
      }
    });
    if (apps()) apps().subscribe(render);
    window.addEventListener("stattrak:device", render);
    render();
  }

  return { init, render, isOpen: () => open };
})();

document.addEventListener("DOMContentLoaded", () => DevBar.init());
