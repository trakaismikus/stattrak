/* StatTrak updater: the "Apps" tab. Manages the apps on the device over Bluetooth, while the
 * device shows Settings > "Apps: SYNC" (docs/APPS.md section 6).
 *
 * The tab has its own connection (js/ble.js) and sends only app commands (js/apps-protocol.js):
 * never a firmware update command. The tabs cannot change while a firmware update or an app
 * install runs.
 */

const AppsTab = (() => {
  const CATALOG_URL = "apps/catalog.json";
  const MAX_BYTES = 24 * 1024;          // all installed packages together (docs/APPS.md section 2)
  const PERMISSION_TEXT = { sound: "Uses sound", counters: "Changes your kill counts" };

  const A = {
    tab: "firmware",
    conn: "off",            // off | connecting | on | oldfw
    ota: null,
    apps: null,             // StatTrakApps
    fw: "",
    info: null,             // INFO
    list: [],               // LIST, in menu order (without SETTINGS)
    banner: null,           // { kind, title, body } on the connect view
    edit: null,             // id of the app with open settings
    values: {},             // id -> setting values
    del: null,              // id of the app with the delete question
    hint: null,             // id of the app that shows "One app must stay shown."
    inst: null,             // { key, phase: start | ask | transfer, pct }
    err: null,              // { key, text } under a catalog row
    fileErr: null,          // text of the file error
    fileItem: null,         // an app from a file, shown under "Add apps"
    fresh: null,            // id of the app that was just installed
    opened: null,           // { id, name }: the device shows APP INSTALLED (OPEN there closes the connection)
    devName: null,          // DEVICE_NAME { custom, name }, null = a firmware without it
    nameDraft: "",          // the text in the name field
    nameMsg: null,          // { kind: ok | err, text } under the name field
    catalog: null,          // [item]
    known: {},              // idHex -> { picture, settings } of catalog and file apps
  };

  const $ = (selector) => document.querySelector(selector);
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const hex = (id) => (id >>> 0).toString(16).padStart(8, "0");
  const kb = (bytes) => (Math.round((bytes / 1024) * 10) / 10).toFixed(1).replace(/\.0$/, "") + " KB";
  const log = (message, kind) => { if (typeof pushLog === "function") pushLog("> apps: " + message, kind); };
  const busy = () => !!A.inst;

  /* ---- line icons (the stroke style of the updater) ---- */
  const P = {
    up: '<polyline points="18 15 12 9 6 15"/>',
    down: '<polyline points="6 9 12 15 18 9"/>',
    gear: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/>',
    trash: '<polyline points="3 6 5 6 21 6"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>',
    upload: '<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="17 8 12 3 7 8"/><line x1="12" y1="3" x2="12" y2="15"/>',
    bt: '<path d="m7 7 10 10-5 5V2l5 5L7 17"/>',
    x: '<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>',
    check: '<polyline points="20 6 9 17 4 12"/>',
    alert: '<circle cx="12" cy="12" r="10"/><line x1="12" y1="8" x2="12" y2="12"/><line x1="12" y1="16" x2="12.01" y2="16"/>',
    lock: '<rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/>',
    grip: '<circle cx="9" cy="6" r="1"/><circle cx="15" cy="6" r="1"/><circle cx="9" cy="12" r="1"/><circle cx="15" cy="12" r="1"/><circle cx="9" cy="18" r="1"/><circle cx="15" cy="18" r="1"/>',
    sound: '<polygon points="11 5 6 9 2 9 2 15 6 15 11 19 11 5"/><path d="M15.54 8.46a5 5 0 0 1 0 7.07"/>',
    counters: '<circle cx="12" cy="12" r="7"/><line x1="12" y1="2" x2="12" y2="6"/><line x1="12" y1="18" x2="12" y2="22"/><line x1="2" y1="12" x2="6" y2="12"/><line x1="18" y1="12" x2="22" y2="12"/>',
  };
  const icon = (name, size) => `<svg width="${size || 18}" height="${size || 18}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${P[name]}</svg>`;
  const perms = (list) => list.map((p) => `<span class="perm">${icon(p in P ? p : "alert", 14)}${esc(PERMISSION_TEXT[p] || p)}</span>`).join("");

  let toastTimer = 0;
  let watchTimer = 0;       // watchOpened()
  function toast(message) {
    let t = $(".apps-toast");
    if (!t) {
      t = document.createElement("div");
      t.className = "apps-toast";
      t.setAttribute("role", "status");
      document.body.appendChild(t);
    }
    t.textContent = message;
    t.style.opacity = 1;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.style.opacity = 0; }, 1400);
  }

  /* ---- data ---------------------------------------------------------------------------- */

  const free = () => (A.info ? A.info.free : 0);
  const usedBytes = () => MAX_BYTES - free();
  const appById = (id) => A.list.find((a) => a.id === id);
  const shownCount = () => A.list.filter((a) => !a.hidden).length;
  const canHide = (app) => app.hidden || shownCount() > 1;
  const knownOf = (app) => A.known[hex(app.id)] || null;
  const pictureOf = (app) => AppPictures.app(app.id, app.name, knownOf(app) && knownOf(app).picture);
  function settingsOf(app) {
    const known = knownOf(app);
    return known && known.settings && known.settings.length === app.settingCount && app.settingCount > 0 ? known.settings : null;
  }

  /* Can this app go on the device? null = yes, else the text in place of Install */
  function roomFor(item) {
    const old = A.list.find((a) => a.id === item.id);
    if (!old && A.info && A.info.count >= A.info.maxApps) return { text: `${A.info.maxApps} of ${A.info.maxApps} apps` };
    if (item.size > free() + (old ? old.size : 0)) return { text: "Needs " + kb(item.size), small: kb(free()) + " free" };
    return null;
  }

  async function loadCatalog() {
    if (A.catalog) return;
    A.catalog = [];
    try {
      const response = await fetch(CATALOG_URL, { cache: "no-cache" });
      const catalog = await response.json();
      if (!catalog || catalog.format !== "stattrak-apps" || !Array.isArray(catalog.apps)) throw new Error("not a catalog");
      A.catalog = catalog.apps
        .filter((a) => /^[0-9a-f]{8}$/.test(a.id) && typeof a.name === "string" && typeof a.file === "string" && /^[\w.-]+\.stapp$/.test(a.file))
        .map((a) => ({
          key: a.id, id: parseInt(a.id, 16), name: a.name, desc: a.description || "", size: a.size,
          version: a.version, permissions: a.permissions || [], picture: a.picture, settings: a.settings || [],
          file: "apps/" + a.file,
        }));
      for (const item of A.catalog) A.known[item.key] = { picture: item.picture, settings: item.settings };
    } catch (error) {
      log("no app catalog: " + error.message, "warn");
    }
    render();
  }

  /* ---- views --------------------------------------------------------------------------- */

  const banner = (kind, title, body, extra) =>
    `<div class="banner banner--${kind}" role="alert"><span class="banner__icon">${icon(kind === "info" ? "check" : "alert", 20)}</span><div><div class="banner__title">${esc(title)}</div>` +
    (body ? `<div class="banner__body">${esc(body)}</div>` : "") + `</div>${extra || ""}</div>`;

  const device = (screen, alt, pressTwo) =>
    `<div class="dev${pressTwo ? " dev--b2" : ""}"><div class="dev__screen"><img alt="${esc(alt)}" src="${screen}"></div>` +
    '<i class="dev__mock"></i><i class="dev__mock dev__mock--2"></i><div class="dev__btn dev__btn--1"><span>1</span></div><div class="dev__btn dev__btn--2"><span>2</span></div></div>';

  function vConnect() {
    const supported = typeof detectBleSupport !== "function" || detectBleSupport().supported;
    return (A.banner ? banner(A.banner.kind, A.banner.title, A.banner.body) : "") +
      '<div class="connect">' + device(AppPictures.screen("idle"), "StatTrak screen: APP SYNC") +
      '<div class="crumbs">On StatTrak: <span class="crumb">Settings</span><span class="crumb-sep">›</span><span class="crumb">Apps: SYNC</span><span class="crumb-sep">›</span><span class="crumb">hold button 2</span></div>' +
      `<button class="btn btn--primary btn--lg" data-act="connect" type="button"${A.conn === "connecting" || !supported ? " disabled" : ""}>` +
      icon("bt", 16) + (A.conn === "connecting" ? "Connecting…" : "Connect") + "</button></div>";
  }

  function vStatus() {
    const used = usedBytes(), pct = Math.min(100, (used / MAX_BYTES) * 100);
    return `<div class="status"><span class="status__dev"><span class="dot"></span>StatTrak <small>${esc(A.fw)}</small></span>` +
      `<div class="store${pct > 75 ? " store--full" : ""}"><span>${kb(used)} / ${kb(MAX_BYTES)}</span><div class="store__bar"><div class="store__fill" style="width:${pct}%"></div></div></div>` +
      '<button class="btn btn--ghost btn--sm" data-act="disconnect" type="button">Disconnect</button></div>';
  }

  function meta(app) {
    if (A.fresh === app.id) return `<span class="tag new">${icon("check", 13)}Installed · ${kb(app.size)}</span>`;
    let h = `<span class="tag">${app.builtin ? "Built-in" : "Installed · " + kb(app.size)}</span>`;
    if (app.error) h += '<span class="tag tag--err">App error</span>';
    return h;
  }

  function vRow(app, i, last) {
    const n = esc(app.name), del = A.del === app.id, edit = A.edit === app.id, settings = settingsOf(app);
    let h = `<div class="row${app.hidden ? " row--off" : ""}${del ? " row--del" : ""}${A.fresh === app.id ? " row--new" : ""}" data-id="${app.id}">` +
      `<div class="row__main"><button class="grip" data-grip type="button" aria-label="Drag to move ${n}. Arrow keys move it.">${icon("grip")}</button>` +
      `<img class="pic${app.hidden ? " pic--off" : ""}" alt="" src="${pictureOf(app)}">` +
      `<div class="row__info"><div class="row__name">${n}</div><div class="row__meta">${meta(app)}</div></div>` +
      '<div class="row__acts">' +
      `<button class="sw" role="switch" type="button" aria-checked="${!app.hidden}" aria-label="Show ${n} in the menu"${canHide(app) ? "" : ' aria-disabled="true"'} data-act="toggle"><span class="sw__track"></span><span class="lbl">${app.hidden ? "Hidden" : "Shown"}</span></button>` +
      `<span class="mv"><button class="icon-btn" type="button" data-act="up" aria-label="Move up"${i === 0 ? " disabled" : ""}>${icon("up")}</button>` +
      `<button class="icon-btn" type="button" data-act="down" aria-label="Move down"${i === last ? " disabled" : ""}>${icon("down")}</button></span>` +
      (settings ? `<button class="icon-btn${edit ? " icon-btn--on" : ""}" type="button" data-act="edit" aria-label="Settings" aria-expanded="${edit}">${icon("gear")}</button>` : '<span class="ph"></span>') +
      (app.builtin ? '<span class="ph"></span>' : `<button class="icon-btn icon-btn--danger" type="button" data-act="del" aria-label="Delete ${n}">${icon("trash")}</button>`) +
      "</div></div>";
    if (A.hint === app.id) h += '<div class="hint">One app must stay shown.</div>';
    if (A.fresh === app.id && A.opened && A.opened.id === app.id) {
      h += '<div class="row__panel" id="donepanel"><div class="inst">' +
        device(AppPictures.screen("installed", app.name), "StatTrak screen: APP INSTALLED, SYNC MORE, OPEN") +
        `<div class="inst__text"><div class="inst__big inst__big--ok">${icon("check", 20)} Installed</div>` +
        '<div class="inst__hint">On StatTrak: hold button 1 = SYNC MORE, hold button 2 = OPEN. OPEN closes this connection.</div></div></div></div>';
    }
    if (edit && settings) {
      const values = A.values[app.id];
      h += '<div class="row__panel">' + settings.map((s, k) =>
        `<div class="setting"><span class="setting__label">${esc(s.name)}</span><div class="seg" role="group" aria-label="${esc(s.name)}">` +
        s.values.map((v, j) => `<button type="button" data-act="set" data-k="${k}" data-v="${j}" aria-pressed="${!!values && values[k] === j}"${values ? "" : " disabled"}>${esc(v)}</button>`).join("") +
        "</div></div>").join("") + "</div>";
    }
    if (del) {
      h += `<div class="row__panel"><div class="msg">Delete <strong>${n}</strong> and its saved values?</div>` +
        `<div class="btns"><button class="btn btn--ghost btn--sm" type="button" data-act="cancel">Cancel</button><button class="btn btn--danger btn--sm" type="button" data-act="really-del">${icon("trash", 14)}Delete</button></div></div>`;
    }
    return h + "</div>";
  }

  function vSettingsRow() {
    return `<div class="row row--fixed"><div class="row__main"><span class="grip grip--none"></span><img class="pic" alt="" src="${AppPictures.settings()}">` +
      `<div class="row__info"><div class="row__name">SETTINGS</div><div class="row__meta"><span class="tag">${icon("lock", 12)}Always last</span></div></div></div></div>`;
  }

  function vCat(item) {
    const inst = A.inst && A.inst.key === item.key, err = A.err && A.err.key === item.key;
    if (!item.fromFile && A.list.some((a) => a.id === item.id) && !inst && !err) return "";
    if (busy() && !inst) return "";
    let act = "";
    if (!inst && !err) {
      const room = roomFor(item);
      act = room ? `<div class="need">${esc(room.text)}${room.small ? `<small>· ${esc(room.small)}</small>` : ""}</div>`
        : '<button class="btn btn--primary btn--sm" type="button" data-act="install">Install</button>';
    }
    let h = `<div class="row cat" data-key="${item.key}"><div class="row__main"><img class="pic" alt="" src="${AppPictures.app(item.id, item.name, item.picture)}">` +
      `<div class="row__info"><div class="row__name">${esc(item.name)}</div>${item.desc ? `<div class="row__desc">${esc(item.desc)}</div>` : ""}` +
      `<div class="row__meta"><span class="tag">${kb(item.size)}</span>${perms(item.permissions)}</div></div>` +
      `<div class="row__acts">${act}</div></div>`;
    if (inst && A.inst.phase === "start") {
      h += '<div class="row__panel" id="instpanel"><div class="inst__line"><span>Asking StatTrak…</span></div></div>';
    }
    if (inst && A.inst.phase === "ask") {
      h += '<div class="row__panel" id="instpanel"><div class="inst">' +
        device(AppPictures.screen("question", item.name), `StatTrak screen: INSTALL ${item.name}?`, true) +
        '<div class="inst__text"><div class="inst__big"><span class="dot dot--wait"></span> Answer on StatTrak</div>' +
        '<div class="inst__hint">Hold button 2 = yes, hold button 1 = no.</div></div></div></div>';
    }
    if (inst && A.inst.phase === "transfer") {
      const pct = Math.round(A.inst.pct);
      h += `<div class="row__panel" id="instpanel"><div class="inst__line"><span>Installing</span><span class="pct">${pct}%</span></div>` +
        `<div class="prog"><div class="prog__fill" style="width:${pct}%"></div></div></div>`;
    }
    if (err) {
      h += `<div class="row__panel"><div class="res res--err">${icon("alert", 20)}<div><b>Not installed</b><span>${esc(A.err.text)}</span></div></div>` +
        '<div class="btns"><button class="btn btn--ghost btn--sm" type="button" data-act="dismiss">Close</button><button class="btn btn--primary btn--sm" type="button" data-act="install">Try again</button></div></div>';
    }
    return h + "</div>";
  }

  function vList() {
    const last = A.list.length - 1;
    let h = vStatus();
    h += `<section><div class="sec-head"><h2 class="h2">On your StatTrak</h2></div><div class="list${busy() ? " list--busy" : ""}" id="appsList">` +
      A.list.map((app, i) => vRow(app, i, last)).join("") + vSettingsRow() + "</div></section>";
    const items = (A.catalog || []).concat(A.fileItem ? [A.fileItem] : []);
    h += `<section><div class="sec-head"><h2 class="h2">Add apps</h2><button class="btn btn--ghost btn--sm" type="button" data-act="file"${busy() ? " disabled" : ""}>${icon("upload", 14)}From file</button></div>` +
      (A.fileErr ? `<div class="banner banner--error" role="alert"><span class="banner__icon">${icon("alert", 20)}</span>` +
        `<div><div class="banner__title">Not a StatTrak app</div><div class="banner__body">${esc(A.fileErr)}</div>` +
        '<button class="btn btn--ghost btn--sm banner__more" type="button" data-act="file">Pick another file</button></div>' +
        `<button class="icon-btn banner__close" type="button" data-act="dismiss" aria-label="Close">${icon("x")}</button></div>` : "") +
      `<div class="list">${items.map(vCat).join("")}</div></section>`;
    return h + vName();
  }

  /* The Bluetooth name of StatTrak in "Apps: SYNC" (DEVICE_NAME) */
  function vName() {
    if (!A.devName) return "";
    const now = A.devName, off = busy() ? " disabled" : "";
    return '<section><div class="sec-head"><h2 class="h2">Bluetooth name</h2></div><div class="row namebox"><div class="row__panel">' +
      `<div class="name__now">StatTrak shows the name <strong>${esc(now.name)}</strong> in Apps: SYNC${now.custom ? "" : " (the standard name)"}.</div>` +
      '<form class="name__form" data-form="name" novalidate><label class="name__label" for="appsName">New name</label>' +
      `<span class="name__prefix" aria-hidden="true">${esc(StatTrakApps.NAME_PREFIX)}</span>` +
      `<input class="name__input" id="appsName" type="text" maxlength="${StatTrakApps.NAME_MAX}" autocomplete="off" spellcheck="false" ` +
      `value="${esc(A.nameDraft)}" aria-describedby="appsNameMsg appsNameHelp"${A.nameMsg && A.nameMsg.kind === "err" ? ' aria-invalid="true"' : ""}${off}>` +
      `<button class="btn btn--primary btn--sm" type="submit" data-act="name-save"${off}>Save</button>` +
      (now.custom ? `<button class="btn btn--ghost btn--sm" type="button" data-act="name-reset"${off}>Back to StatTrak</button>` : "") + "</form>" +
      `<div class="name__msg${A.nameMsg ? " name__msg--" + A.nameMsg.kind : ""}" id="appsNameMsg" role="status">${A.nameMsg ? esc(A.nameMsg.text) : ""}</div>` +
      `<div class="row__desc" id="appsNameHelp">1 to ${StatTrakApps.NAME_MAX} characters after "${esc(StatTrakApps.NAME_PREFIX)}". A phone or computer finds StatTrak by this name. The firmware update always uses "StatTrak".</div>` +
      "</div></div></section>";
  }

  function vOldFw() {
    return banner("warn", "Update the firmware first", `Firmware ${A.fw || "of this StatTrak"} has no apps.`,
      '<a class="btn btn--primary btn--sm banner__act" href="#firmware">Update firmware</a>');
  }

  function render() {
    renderTabs();
    const main = $("#appsMain");
    if (!main) return;
    if (A.conn === "on") main.innerHTML = vList();
    else if (A.conn === "oldfw") main.innerHTML = vOldFw();
    else main.innerHTML = vConnect();
  }

  function renderProgress() {
    const panel = $("#instpanel");
    if (!panel || !$("#instpanel .prog__fill")) return render();
    const pct = Math.round(A.inst.pct);
    panel.querySelector(".pct").textContent = pct + "%";
    panel.querySelector(".prog__fill").style.width = pct + "%";
  }

  const reveal = (selector) => {
    const el = $(selector);
    if (el && el.scrollIntoView) el.scrollIntoView({ block: "nearest", behavior: "smooth" });
  };

  /* ---- tabs ---------------------------------------------------------------------------- */

  // The firmware tab: a transfer or the wait for the confirmation runs (js/main.js)
  const firmwareBusy = () => typeof state !== "undefined" &&
    (state.flashState === "flashing" || state.flashState === "verifying" || state.connState === "scanning" || state.confirmOpen);

  function renderTabs() {
    const fwTab = $("#tabFirmware"), appsTab = $("#tabApps");
    if (!fwTab || !appsTab) return;
    const blocked = { firmware: A.tab === "apps" && busy(), apps: A.tab === "firmware" && firmwareBusy() };
    for (const [name, el] of [["firmware", fwTab], ["apps", appsTab]]) {
      el.classList.toggle("tab--on", A.tab === name);
      if (A.tab === name) el.setAttribute("aria-current", "page"); else el.removeAttribute("aria-current");
      if (blocked[name]) el.setAttribute("aria-disabled", "true"); else el.removeAttribute("aria-disabled");
    }
    $("#firmwareView").hidden = A.tab !== "firmware";
    $("#appsView").hidden = A.tab !== "apps";
  }

  function showTab(name) {
    if (name === A.tab) return render();
    if ((name === "apps" && firmwareBusy()) || (name === "firmware" && busy())) {
      history.replaceState(null, "", A.tab === "apps" ? "#apps" : "#firmware");
      return renderTabs();
    }
    if (name === "apps") {
      // The device cannot be in the update screen and in "Apps: SYNC": the firmware tab lets go of it
      if (typeof state !== "undefined" && (state.device || (typeof bleOta !== "undefined" && bleOta && bleOta.isConnected()))) {
        const button = document.getElementById("disconnectBtn");
        if (button) button.click();
      }
      A.tab = "apps";
      loadCatalog();
    } else {
      disconnect(null);
      A.tab = "firmware";
    }
    render();
    if (typeof window.scrollTo === "function") window.scrollTo(0, 0);
  }

  /* ---- connection ---------------------------------------------------------------------- */

  function disconnect(newBanner) {
    const ota = A.ota;
    A.ota = null;
    A.apps = null;
    if (ota) {
      ota.removeEventListener("bleConnected", onConnected);
      ota.removeEventListener("bleDisconnected", onDisconnected);
      ota.removeEventListener("bleConnectFailed", onConnectFailed);
      try { ota.disconnect(); } catch (_) {}
    }
    A.conn = "off";
    A.banner = newBanner;
    A.inst = null;
    A.edit = A.del = A.hint = A.fresh = A.err = null;
    A.opened = A.devName = A.nameMsg = null;
    A.nameDraft = "";
    clearTimeout(watchTimer);
    A.fileErr = null;
    A.fileItem = null;
    A.values = {};
  }

  const LOST = { kind: "error", title: "Connection lost", body: "Turn on Apps: SYNC again, then connect." };
  // The person pressed OPEN after an install: StatTrak opened the app and turned Bluetooth off
  const OPENED = (name) => ({ kind: "info", title: `${name} is installed`,
    body: "The connection ended. OPEN on StatTrak opens the app and closes the connection. For more apps, open Apps: SYNC on StatTrak again, then connect." });
  const NOT_IN_SYNC = { kind: "warn", title: "StatTrak is not in Apps: SYNC", body: "Open Apps: SYNC on StatTrak, then connect." };

  async function connect() {
    if (A.conn === "connecting") return;
    disconnect(null);
    A.conn = "connecting";
    render();
    let ota;
    try {
      // Also a StatTrak with an other Bluetooth name (DEVICE_NAME): by its service UUID
      ota = await StatTrakApps.pair();
    } catch (error) {
      log("pairing failed: " + error.message, "warn");
      A.conn = "off";
      return render();
    }
    A.ota = ota;
    ota.addEventListener("bleConnected", onConnected);
    ota.addEventListener("bleDisconnected", onDisconnected);
    ota.addEventListener("bleConnectFailed", onConnectFailed);
  }

  async function onConnected(event) {
    const ota = event.target;
    if (ota !== A.ota) return;
    // One connection: when it is lost, the person connects again (the device needs "Apps: SYNC")
    ota.wantConnection = false;
    A.apps = new StatTrakApps(ota);
    try {
      A.fw = ((await ota.getVersion()) || "").replace(/[\s\x00]+$/g, "");
      if (ota !== A.ota) return;
      await refresh();
      if (ota !== A.ota) return;
      A.devName = await A.apps.deviceName();
      if (ota !== A.ota) return;
      A.conn = "on";
      log(`connected, firmware ${A.fw}, ${A.list.length} apps, ${A.info.free} bytes free`, "ok");
    } catch (error) {
      if (ota !== A.ota) return;
      // Firmware 1.0.1 does not answer user commands at all
      if (error.kind === "no answer" && /^1\.0\./.test(A.fw) && ota.isConnected()) error.kind = "old firmware";
      return fail(error);
    }
    render();
  }

  function onDisconnected(event) {
    if (event.target !== A.ota) return;
    const opened = A.opened && !busy() ? A.opened : null;
    log(opened ? "disconnected after the install of " + opened.name : "disconnected", opened ? "ok" : "warn");
    disconnect(opened ? OPENED(opened.name) : LOST);
    render();
  }

  /* After an install the device shows APP INSTALLED: OPEN there opens the app and closes the
     connection (a calm message, not "Connection lost"). STATUS tells when the person pressed
     SYNC MORE (or an other install began): then a lost connection is lost again. */
  const OPEN_WATCH_MS = 1000;
  const OPEN_WATCH_LIMIT_MS = 60000;   // Each command keeps the device awake: not for ever
  function watchOpened(started) {
    clearTimeout(watchTimer);
    const apps = A.apps;
    if (!A.opened || !apps) return;
    if (started === undefined) started = Date.now();
    if (Date.now() - started >= OPEN_WATCH_LIMIT_MS) {
      A.opened = null;
      return render();
    }
    watchTimer = setTimeout(async () => {
      if (apps !== A.apps || !A.opened) return;
      if (!busy()) {
        try {
          const state = await apps.status();
          if (apps !== A.apps) return;
          if (state !== StatTrakApps.STATE__INSTALLED) {
            A.opened = null;
            return render();
          }
        } catch (_) {
          return;   // The connection is lost: onDisconnected() shows it
        }
      }
      watchOpened(started);
    }, OPEN_WATCH_MS);
  }

  function onConnectFailed(event) {
    if (event.target !== A.ota) return;
    disconnect({ kind: "warn", title: "Could not connect", body: "Turn on Apps: SYNC on StatTrak, then connect." });
    render();
  }

  async function refresh() {
    A.info = await A.apps.info();
    A.list = await A.apps.list();
  }

  /* An error of a command: the views for "old firmware", "not in Apps: SYNC" and a lost
     connection. Other errors: the list is read again. */
  async function fail(error) {
    log((error.kind || "error") + ": " + error.message, "warn");
    if (error.kind === "old firmware") {
      disconnect(null);
      A.conn = "oldfw";
      return render();
    }
    if (error.kind === "not in sync") {
      disconnect(NOT_IN_SYNC);
      return render();
    }
    if (error.kind === "waiting") {
      toast("Answer StatTrak first");
    } else if (error.kind === "status") {
      toast("Not saved");
    } else {
      disconnect(LOST);
      return render();
    }
    try {
      await refresh();
    } catch (again) {
      if (again.kind !== "waiting" && again.kind !== "status") disconnect(LOST);
    }
    render();
  }

  /* ---- actions ------------------------------------------------------------------------- */

  async function change(task, done) {
    try {
      await task();
      if (done !== false) toast(done || "Saved");
    } catch (error) {
      if (error.kind === "status" && error.status === StatTrakApps.STATUS__LAST_SHOWN) {
        await refresh().catch(() => {});
        render();
        return;
      }
      await fail(error);
    }
  }

  async function move(id, step) {
    const i = A.list.findIndex((a) => a.id === id), j = i + step;
    if (i < 0 || j < 0 || j >= A.list.length) return false;
    [A.list[i], A.list[j]] = [A.list[j], A.list[i]];
    render();
    await saveOrder();
    return true;
  }

  function saveOrder() {
    return change(() => A.apps.setOrder(A.list.map((a) => a.id)));
  }

  async function toggle(app) {
    if (!canHide(app)) {
      A.hint = app.id;
      return render();
    }
    A.hint = null;
    app.hidden = !app.hidden;
    if (!app.hidden) app.error = false;   // shown again: the device sets the error count to 0
    render();
    await change(() => A.apps.setHidden(app.id, app.hidden));
  }

  async function openSettings(app) {
    A.edit = A.edit === app.id ? null : app.id;
    A.del = null;
    render();
    if (A.edit !== app.id || A.values[app.id]) return;
    try {
      const values = [];
      for (let k = 0; k < app.settingCount; k++) values.push(await A.apps.getSetting(app.id, k));
      A.values[app.id] = values;
    } catch (error) {
      return fail(error);
    }
    render();
  }

  async function setSetting(app, k, value) {
    const values = A.values[app.id];
    if (!values || values[k] === value) return;
    values[k] = value;
    render();
    await change(() => A.apps.setSetting(app.id, k, value));
  }

  async function reallyDelete(app) {
    A.del = null;
    try {
      await A.apps.delete(app.id);
      await refresh();
      toast(app.name + " deleted");
    } catch (error) {
      return fail(error);
    }
    render();
  }

  const INSTALL_TEXT = {
    [StatTrakApps.STATUS__NO_SPACE]: "Not enough space on StatTrak.",
    [StatTrakApps.STATUS__TOO_LARGE]: "The app is larger than 4 KB.",
    [StatTrakApps.STATUS__TOO_MANY]: "8 apps are installed. Delete one first.",
    [StatTrakApps.STATUS__BAD_CRC]: "The data was damaged. Try again.",
    [StatTrakApps.STATUS__ERROR]: "StatTrak refused the app.",
  };
  const NEWER_FORMAT = "Update the firmware first: this app needs a newer StatTrak firmware.";

  async function startInstall(item) {
    if (busy() || !A.apps) return;
    A.err = null;
    A.fresh = null;
    A.opened = null;
    A.fileErr = null;
    A.edit = A.del = A.hint = null;
    if (roomFor(item)) return render();

    let bytes = item.bytes;
    if (!bytes) {
      try {
        const response = await fetch(item.file, { cache: "no-cache" });
        if (!response.ok) throw new Error("HTTP " + response.status);
        bytes = new Uint8Array(await response.arrayBuffer());
        const file = StappFile.inspect(bytes);
        if (!file.ok || file.id !== item.id || file.size !== item.size) throw new Error("the file does not match the catalog");
      } catch (error) {
        log("catalog file: " + error.message, "warn");
        A.err = { key: item.key, text: "The app file could not be loaded." };
        return render();
      }
    }

    // First the device asks "INSTALL <NAME>?", after "yes" the data goes
    A.inst = { key: item.key, phase: "start", pct: 0 };
    log(`install ${item.name} (${bytes.length} bytes)`);
    render();
    reveal("#instpanel");
    let result;
    try {
      result = await A.apps.install(bytes, (fraction) => {
        if (!A.inst) return;
        A.inst.pct = fraction * 100;
        if (A.inst.phase !== "transfer") {
          A.inst.phase = "transfer";
          return render();
        }
        renderProgress();
      }, () => {
        if (!A.inst) return;
        A.inst.phase = "ask";
        render();
        reveal("#instpanel");
      });
    } catch (error) {
      if (!A.inst) return;   // the connection was lost: the connect view is there
      A.inst = null;
      if (error.kind === "file") {
        A.err = { key: item.key, text: error.problem === "newer format" ? NEWER_FORMAT : "The app file is damaged." };
        log("install not sent: " + error.message, "warn");
        return render();
      }
      if (error.kind === "status" && (error.status in INSTALL_TEXT || error.status === StatTrakApps.STATUS__BAD_PACKAGE)) {
        A.err = { key: item.key, text: error.status === StatTrakApps.STATUS__BAD_PACKAGE ? `StatTrak refused the app (${error.reason}).` : INSTALL_TEXT[error.status] };
        log("install refused: status " + error.status, "warn");
        await refresh().catch(() => {});
        return render();
      }
      return fail(error);
    }
    if (!A.inst) return;
    A.inst = null;
    if (result !== "installed") {
      log("install " + result, "warn");
      A.err = { key: item.key, text: result === "stopped" ? "The transfer stopped on StatTrak. Try again." : "Declined on StatTrak or timed out." };
      render();
      return reveal(`.cat[data-key="${item.key}"]`);
    }
    try {
      await refresh();
    } catch (error) {
      return fail(error);
    }
    A.known[hex(item.id)] = { picture: item.picture, settings: item.settings };
    A.fresh = item.id;
    A.opened = { id: item.id, name: item.name };
    watchOpened();
    if (A.fileItem && A.fileItem.key === item.key) A.fileItem = null;
    log(item.name + " installed", "ok");
    render();
    toast(item.name + " installed");
    const row = $(`#appsList [data-id="${item.id}"]`);
    if (row && row.scrollIntoView) row.scrollIntoView({ block: "center", behavior: "smooth" });
  }

  const FILE_TEXT = {
    "not an app": (name) => `${name} is not a .stapp file.`,
    "too large": (name) => `${name} is larger than 4 KB.`,
    "damaged": (name) => `${name} is damaged.`,
    "newer format": (name) => `${name} needs a newer firmware. Update the firmware first.`,
  };

  async function onFile(file) {
    if (!file || busy()) return;
    A.fileErr = null;
    A.err = null;
    let problem = "";
    let bytes = null;
    if (!/\.stapp$/i.test(file.name)) problem = "not an app";
    else if (file.size > StappFile.MAX_SIZE) problem = "too large";
    else {
      bytes = new Uint8Array(await file.arrayBuffer());
      const info = StappFile.inspect(bytes);
      if (!info.ok) problem = info.problem;
      else if (A.info && (info.engine > A.info.engine || info.format > A.info.format)) problem = "newer format";
      else {
        const item = {
          key: "file-" + info.idHex, id: info.id, name: info.name, desc: file.name, size: info.size,
          permissions: info.permissions, picture: info.picture, settings: info.settings, bytes, fromFile: true,
        };
        A.fileItem = item;
        log(`file ${file.name}: ${info.name}, ${info.size} bytes`);
        return startInstall(item);
      }
    }
    log(`file ${file.name} refused: ${problem}`, "warn");
    A.fileErr = FILE_TEXT[problem](file.name);
    render();
    reveal("#appsView .banner--error");
  }

  /* The Bluetooth name: "" = back to "StatTrak" */
  async function saveName(name, reset) {
    if (busy() || !A.apps) return;
    const problem = name === "" && !reset ? "Type a name first." : StatTrakApps.nameProblem(name);
    if (problem) {
      A.nameMsg = { kind: "err", text: problem };
      render();
      const input = $("#appsName");
      if (input) input.focus();
      return;
    }
    try {
      A.devName = await A.apps.setDeviceName(name);
    } catch (error) {
      if (error.kind === "status") {
        A.nameMsg = { kind: "err", text: "StatTrak refused the name." };
        return render();
      }
      A.nameMsg = null;
      return fail(error);
    }
    log("Bluetooth name: " + A.devName.name, "ok");
    A.nameDraft = "";
    A.nameMsg = null;
    render();
    showNameNotice();
  }

  // After a new name: a popup with OK. The device uses the name from its next start of Apps: SYNC.
  const NAME_NOTICE = "Name change will take place next time you open apps sync on device.";
  function showNameNotice() {
    const root = document.getElementById("modalRoot");
    if (!root) return;
    root.innerHTML =
      '<div class="modal-backdrop" data-dismiss><div class="modal-panel modal-panel--info" role="dialog" ' +
      `aria-modal="true" aria-describedby="nameNotice"><p class="modal__text" id="nameNotice">${esc(NAME_NOTICE)}</p>` +
      '<div class="modal__actions"><button class="btn btn--primary" data-ok type="button">OK</button></div></div></div>';
    root.hidden = false;
    const close = () => { root.innerHTML = ""; root.hidden = true; };
    const ok = root.querySelector("[data-ok]");
    ok.addEventListener("click", close);
    root.querySelector(".modal-backdrop").addEventListener("click", (e) => { if (e.target.matches("[data-dismiss]")) close(); });
    ok.focus();
  }

  // While the person types: the problem of the name at once (no new render: the field keeps the focus)
  function onNameInput(input) {
    A.nameDraft = input.value;
    const problem = StatTrakApps.nameProblem(input.value);
    A.nameMsg = problem ? { kind: "err", text: problem } : null;
    const msg = $("#appsNameMsg");
    if (msg) {
      msg.textContent = problem;
      msg.className = "name__msg" + (problem ? " name__msg--err" : "");
    }
    if (problem) input.setAttribute("aria-invalid", "true"); else input.removeAttribute("aria-invalid");
  }

  /* ---- events -------------------------------------------------------------------------- */

  function onClick(event) {
    const b = event.target.closest("[data-act]");
    if (!b || b.disabled) return;
    const act = b.dataset.act;
    const row = b.closest("[data-id]"), cat = b.closest("[data-key]");
    const app = row ? appById(Number(row.dataset.id)) : null;
    if (act !== "set") A.fresh = null;

    if (act === "connect") return connect();
    if (act === "disconnect") { disconnect(null); return render(); }
    if (act === "file") { A.fileErr = null; return $("#appsFile").click(); }
    if (act === "dismiss") { A.err = null; A.fileErr = null; if (A.fileItem && !busy()) A.fileItem = null; return render(); }
    if (act === "name-reset") return saveName("", true);
    if (act === "install" && cat) {
      const item = (A.catalog || []).concat(A.fileItem ? [A.fileItem] : []).find((i) => i.key === cat.dataset.key);
      if (item) startInstall(item);
      return;
    }
    if (!app || busy()) return;
    if (act === "toggle") return toggle(app);
    if (act === "up") return move(app.id, -1);
    if (act === "down") return move(app.id, 1);
    if (act === "edit") return openSettings(app);
    if (act === "set") return setSetting(app, Number(b.dataset.k), Number(b.dataset.v));
    if (act === "del") {
      if (!canHide(app)) { A.hint = app.id; return render(); }
      A.del = app.id; A.edit = null; A.hint = null;
      return render();
    }
    if (act === "cancel") { A.del = null; return render(); }
    if (act === "really-del") return reallyDelete(app);
  }

  // Drag to sort (mouse and touch). Arrow keys on the handle.
  let drag = null;
  function onPointerDown(event) {
    const grip = event.target.closest && event.target.closest("[data-grip]");
    if (!grip || busy()) return;
    const row = grip.closest(".row");
    drag = { row, list: row.parentNode, start: A.list.map((a) => a.id).join() };
    row.classList.add("drag");
    if (grip.setPointerCapture) grip.setPointerCapture(event.pointerId);
    event.preventDefault();
  }
  function onPointerMove(event) {
    if (!drag) return;
    const rows = [...drag.list.querySelectorAll(".row[data-id]")].filter((r) => r !== drag.row);
    const before = rows.find((r) => { const box = r.getBoundingClientRect(); return event.clientY < box.top + box.height / 2; }) || drag.list.querySelector(".row--fixed");
    if (before !== drag.row.nextSibling) drag.list.insertBefore(drag.row, before);
  }
  function onPointerUp() {
    if (!drag) return;
    const ids = [...drag.list.querySelectorAll(".row[data-id]")].map((r) => Number(r.dataset.id));
    const start = drag.start;
    drag = null;
    A.list = ids.map(appById);
    render();
    if (ids.join() !== start) saveOrder();
  }
  async function onKeyDown(event) {
    const grip = event.target.closest && event.target.closest("[data-grip]");
    if (!grip || busy() || (event.key !== "ArrowUp" && event.key !== "ArrowDown")) return;
    event.preventDefault();
    const id = Number(grip.closest("[data-id]").dataset.id);
    const moved = move(id, event.key === "ArrowUp" ? -1 : 1);
    const focus = () => { const g = $(`#appsList [data-id="${id}"] [data-grip]`); if (g) g.focus(); };
    focus();
    await moved;
    focus();
  }

  function init() {
    const main = $("#appsMain");
    if (!main) return;
    main.addEventListener("click", onClick);
    main.addEventListener("submit", (event) => {
      if (!event.target.matches('[data-form="name"]')) return;
      event.preventDefault();
      A.fresh = null;
      const input = $("#appsName");
      saveName(input ? input.value : "");
    });
    main.addEventListener("input", (event) => { if (event.target.id === "appsName") onNameInput(event.target); });
    main.addEventListener("keydown", onKeyDown);
    main.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("pointermove", onPointerMove);
    document.addEventListener("pointerup", onPointerUp);
    for (const id of ["tabFirmware", "tabApps"]) {
      document.getElementById(id).addEventListener("click", (event) => {
        if (event.currentTarget.getAttribute("aria-disabled") === "true") event.preventDefault();
      });
    }
    $("#appsFile").addEventListener("change", (event) => {
      const file = event.target.files && event.target.files[0];
      event.target.value = "";
      onFile(file);
    });
    const fromHash = () => showTab(location.hash === "#apps" ? "apps" : "firmware");
    window.addEventListener("hashchange", fromHash);
    window.addEventListener("beforeunload", (event) => { if (busy()) event.preventDefault(); });
    fromHash();
  }

  return { init, render, renderTabs, state: A, onFile };
})();

function renderTabs() { AppsTab.renderTabs(); }

document.addEventListener("DOMContentLoaded", () => AppsTab.init());
