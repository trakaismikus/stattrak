/* StatTrak updater, Apps tab: the parts under the app list (docs/DATA.md): the start picture, the
 * backup, the app data and the device log. js/apps.js shows them after the connection; the
 * commands are in js/data-protocol.js.
 *
 * Only when the device answers CONFIG_INFO: an older firmware (status 1) gets one line in place of
 * these parts. While a transfer or an install runs, all controls are locked (host.busy()).
 */

const SyncParts = (() => {
  const S = {
    host: null,             // js/apps.js: { apps(), list(), fw(), busy(), render(), fail(error), toast(text), log(text, kind) }
    data: null,             // StatTrakData
    supported: null,        // null = not read yet, true, false = an older firmware
    info: null,             // CONFIG_INFO
    work: null,             // { kind, id, phase, pct, text }: what runs now (locks the controls)
    pic: null,              // the chosen picture: { name, scaled, dark, rgba, pixels, still }
    picErr: "",             // the text of a picture that could not be used
    picMsg: "",             // the text after "Send to device"
    devPic: null,           // the start picture of the device: { state, pixels, url }
    picAsk: false,          // the page asks "Delete the start picture?"
    dataApps: [],           // [{ id, name, info }]: the apps with an app data store
    dataMsg: {},            // id -> { kind, text }
    log: null,              // the device log, newest first
    backupMsg: null,        // { kind, text }
    restore: null,          // { file, device, rows, missing, parts, phase: table | ask | send | done, result, skipped }
  };
  const DARK_DEFAULT = 50;

  const $ = (selector) => document.querySelector(selector);
  const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
  const busy = () => !!S.work;
  const off = () => (S.host && S.host.busy() ? " disabled" : "");
  const render = () => S.host && S.host.render();
  const log = (text, kind) => S.host && S.host.log(text, kind);

  /* ---- files ---------------------------------------------------------------------------- */

  function save(name, text, type) {
    const blob = new Blob([text], { type: type || "text/plain" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  }

  // 128 x 32 pixels as a picture for an <img> (white = on)
  function pixelsUrl(pixels) {
    const canvas = document.createElement("canvas");
    canvas.width = 128;
    canvas.height = 32;
    const ctx = canvas.getContext("2d");
    const image = ctx.createImageData(128, 32);
    for (let i = 0; i < pixels.length; i++) {
      const v = pixels[i] ? 255 : 0;
      image.data.set([v, v, v, 255], i * 4);
    }
    ctx.putImageData(image, 0, 0);
    return canvas.toDataURL("image/png");
  }

  /* A PNG or JPG file -> the RGBA bytes of 128 x 32 (scaled to fit, centered, on black) */
  async function readPicture(file) {
    let source;
    if (typeof createImageBitmap === "function") {
      source = await createImageBitmap(file);
    } else {
      source = await new Promise((resolve, reject) => {
        const img = new Image();
        img.onload = () => resolve(img);
        img.onerror = () => reject(new Error("not a picture"));
        img.src = URL.createObjectURL(file);
      });
    }
    const width = source.width, height = source.height;
    if (!width || !height) throw new Error("not a picture");
    const place = FrameBuffer.fit(width, height);
    const canvas = document.createElement("canvas");
    canvas.width = 128;
    canvas.height = 32;
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, 128, 32);
    ctx.imageSmoothingEnabled = place.scaled;
    ctx.imageSmoothingQuality = "high";
    ctx.drawImage(source, place.x, place.y, place.w, place.h);
    if (source.close) source.close();
    return { rgba: ctx.getImageData(0, 0, 128, 32).data, scaled: place.scaled, width, height };
  }

  /* ---- after the connection ------------------------------------------------------------- */

  function reset() {
    S.data = null;
    S.supported = null;
    S.info = null;
    S.work = null;
    S.pic = null;
    S.picErr = S.picMsg = "";
    S.devPic = null;
    S.picAsk = false;
    S.dataApps = [];
    S.dataMsg = {};
    S.log = null;
    S.backupMsg = null;
    S.restore = null;
  }

  /* After the app list is read: CONFIG_INFO, then the start picture, the app data and the log */
  async function connected(apps) {
    reset();
    S.data = new StatTrakData(apps);
    const data = S.data;
    try {
      const info = await data.info();
      if (data !== S.data) return;
      S.supported = !!info;
      S.info = info;
      render();
      if (!info) { log("no backup and data commands (older firmware)", "warn"); return; }
      await loadPicture();
      await loadDataApps();
      await loadLog();
    } catch (error) {
      if (data === S.data) S.host.fail(error);
      return;
    }
    if (data === S.data) render();
  }

  async function loadInfo() {
    S.info = await S.data.info();
  }

  async function loadPicture() {
    const state = S.info ? S.info.picture : 0;
    if (!state) { S.devPic = null; return; }
    const pixels = await S.data.readStartPicture();
    S.devPic = pixels ? { state, pixels, url: pixelsUrl(FrameBuffer.toPixels(pixels)) } : null;
  }

  async function loadDataApps() {
    const out = [];
    for (const app of S.host.list()) {
      const info = await S.data.dataInfo(app.id);
      if (info && info.columns > 0) out.push({ id: app.id, name: app.name, info });
    }
    S.dataApps = out;
  }

  async function loadLog() {
    S.log = S.info && S.info.logCount > 0 ? await S.data.deviceLog() : [];
  }

  /* The app list changed (an install or a delete): read the app data stores again */
  let listTimer = 0;
  function listChanged() {
    if (!S.supported || !S.data) return;
    clearTimeout(listTimer);
    const data = S.data;
    listTimer = setTimeout(async () => {
      if (data !== S.data || busy()) return;
      try {
        await loadInfo();
        await loadDataApps();
      } catch (_) {
        return;   // the error shows at the next command
      }
      if (data === S.data) render();
    }, 50);
  }

  /* A command failed: the texts of this part, else the host shows it (a lost connection, ...) */
  function failed(error, show) {
    if (error && error.kind === "waiting") { S.host.toast("Answer StatTrak first"); return true; }
    if (error && error.kind === "status") {
      const texts = {
        9: "The data was damaged. Try again.",
        10: `StatTrak refused the backup (${error.reason || "bad backup"}).`,
        2: "StatTrak refused it.",
      };
      show(texts[error.status] || `StatTrak answered status ${error.status}.`);
      return true;
    }
    S.work = null;
    S.host.fail(error);
    return false;
  }

  /* Runs a task that locks the controls */
  async function run(work, task) {
    if (S.host.busy() || !S.data) return;
    S.work = work;
    render();
    try {
      await task();
    } finally {
      S.work = null;
      if (S.data) render();
    }
  }

  function progress(pct) {
    if (!S.work) return;
    S.work.pct = pct;
    const el = $("#syncBusy");
    if (el && el.querySelector(".prog__fill")) {
      el.querySelector(".pct").textContent = Math.round(pct) + "%";
      el.querySelector(".prog__fill").style.width = Math.round(pct) + "%";
    } else {
      render();
    }
  }

  /* ---- start picture -------------------------------------------------------------------- */

  async function onPicFile(file) {
    if (!file || S.host.busy()) return;
    S.picErr = S.picMsg = "";
    if (!/^image\/(png|jpeg)$/.test(file.type) && !/\.(png|jpe?g)$/i.test(file.name)) {
      S.picErr = `${file.name} is not a PNG or JPG picture.`;
      return render();
    }
    try {
      const read = await readPicture(file);
      const dark = S.pic ? S.pic.dark : DARK_DEFAULT;
      const pixels = FrameBuffer.fromRgba(read.rgba, dark);
      S.pic = { name: file.name, scaled: read.scaled, width: read.width, height: read.height, dark, rgba: read.rgba, pixels,
        still: StDevice.still(pixels, S.pic && S.pic.still) };
      log(`start picture ${file.name}: ${read.width} x ${read.height}`);
    } catch (error) {
      S.picErr = `${file.name} could not be read as a picture.`;
    }
    render();
  }

  // The slider: a new picture at once, with no new render (the slider keeps the focus)
  function onDark(input) {
    if (!S.pic) return;
    S.pic.dark = Number(input.value);
    S.pic.pixels = FrameBuffer.fromRgba(S.pic.rgba, S.pic.dark);
    S.pic.still = StDevice.still(S.pic.pixels, S.pic.still);
    const dev = $("#syncPicDevice");
    if (dev) dev.setAttribute("arg", S.pic.still);
    const out = $("#syncDarkValue");
    if (out) out.textContent = S.pic.dark + "%";
  }

  function sendPicture() {
    if (!S.pic) return;
    const bytes = FrameBuffer.fromPixels(S.pic.pixels);
    S.picMsg = "";
    return run({ kind: "pic", pct: 0 }, async () => {
      let result;
      try {
        result = await S.data.sendStartPicture(bytes, (fraction) => progress(fraction * 100));
      } catch (error) {
        failed(error, (text) => { S.picMsg = text; });
        return;
      }
      if (result.result !== "done") {
        S.picMsg = "The transfer stopped on StatTrak. Try again.";
        return;
      }
      log("start picture sent", "ok");
      S.pic = null;
      S.picMsg = "Sent. StatTrak shows it for 2 s.";
      S.devPic = { state: StatTrakData.PIC_STATE__ON, pixels: bytes, url: pixelsUrl(FrameBuffer.toPixels(bytes)) };
      S.host.toast("Start picture sent");
      try { await loadInfo(); } catch (error) { failed(error, () => {}); }
    });
  }

  function togglePicture() {
    if (!S.devPic) return;
    const on = S.devPic.state !== StatTrakData.PIC_STATE__ON;
    return run({ kind: "pic-switch" }, async () => {
      try {
        S.devPic.state = await S.data.setStartPictureShown(on);
        S.host.toast(on ? "Shown at power-on" : "Not shown at power-on");
      } catch (error) {
        failed(error, (text) => { S.picMsg = text; });
      }
    });
  }

  function deletePicture() {
    S.picAsk = false;
    return run({ kind: "pic-delete" }, async () => {
      try {
        await S.data.deleteStartPicture();
        S.devPic = null;
        S.host.toast("Start picture deleted");
        log("start picture deleted", "ok");
      } catch (error) {
        failed(error, (text) => { S.picMsg = text; });
      }
    });
  }

  /* ---- backup and restore --------------------------------------------------------------- */

  function downloadBackup() {
    S.backupMsg = null;
    S.restore = null;
    return run({ kind: "backup", text: "Reading StatTrak…" }, async () => {
      let read;
      try {
        read = await S.data.readAll({ firmware: S.host.fw(), onProgress: (text) => { if (S.work) { S.work.text = text + "…"; render(); } } });
      } catch (error) {
        failed(error, (text) => { S.backupMsg = { kind: "err", text }; });
        return;
      }
      const file = BackupFile.make(read);
      save(BackupFile.fileName(), JSON.stringify(file, null, 2) + "\n", "application/json");
      S.backupMsg = { kind: "ok", text: `Saved ${BackupFile.fileName()}.` };
      log("backup saved", "ok");
    });
  }

  async function onBackupFile(file) {
    if (!file || S.host.busy()) return;
    S.backupMsg = null;
    S.restore = null;
    const parsed = BackupFile.parse(await file.text());
    if (parsed.problem) {
      S.backupMsg = { kind: "err", text: `${file.name}: ${parsed.problem}` };
      return render();
    }
    await run({ kind: "backup", text: "Reading StatTrak…" }, async () => {
      let device;
      try {
        device = await S.data.readAll({ log: false });
      } catch (error) {
        failed(error, (text) => { S.backupMsg = { kind: "err", text }; });
        return;
      }
      const parts = {};
      for (const part of BackupFile.PARTS) parts[part] = true;
      S.restore = { name: file.name, file: parsed.file, device, rows: BackupFile.compare(parsed.file, device),
        missing: BackupFile.missingApps(parsed.file, device), parts, phase: "table" };
    });
    const el = $("#syncRestore");
    if (el && el.scrollIntoView) el.scrollIntoView({ block: "nearest", behavior: "smooth" });
  }

  function startRestore() {
    const r = S.restore;
    if (!r || r.phase !== "table" || !BackupFile.PARTS.some((p) => r.parts[p])) return;
    let bytes;
    try {
      bytes = BackupFile.restoreData(r.file, r.device, r.parts);
    } catch (error) {
      S.backupMsg = { kind: "err", text: error.message };
      return render();
    }
    log(`restore ${r.name} (${bytes.length} bytes)`);
    return run({ kind: "restore" }, async () => {
      r.phase = "ask";
      render();
      let result;
      try {
        result = await S.data.restore(bytes, (fraction) => { r.phase = "send"; progress(fraction * 100); }, () => { r.phase = "ask"; render(); });
      } catch (error) {
        r.phase = "table";
        failed(error, (text) => { S.backupMsg = { kind: "err", text }; });
        return;
      }
      if (result.result !== "done") {
        r.phase = "table";
        S.backupMsg = { kind: "err", text: result.result === "stopped" ? "The transfer stopped on StatTrak. Try again." : "Declined on StatTrak or timed out." };
        log("restore " + result.result, "warn");
        return;
      }
      r.result = result;
      if (S.work) S.work.text = "Reading StatTrak again…";
      render();
      try {
        await S.host.refresh();
        const after = await S.data.readAll({ log: false });
        r.skipped = BackupFile.skipped(r.file, after, r.parts);
        S.info = after.info;
        await loadPicture();
        await loadDataApps();
      } catch (error) {
        failed(error, (text) => { S.backupMsg = { kind: "err", text }; });
        return;
      }
      r.phase = "done";
      log(`restore done: ${result.written === undefined ? "?" : result.written} written, ${r.skipped.length} not the same`, "ok");
      S.host.toast("Backup restored");
    });
  }

  /* ---- app data and the device log ------------------------------------------------------ */

  function downloadData(app) {
    S.dataMsg[app.id] = null;
    return run({ kind: "csv", id: app.id, pct: 0 }, async () => {
      try {
        const info = await S.data.dataInfo(app.id);
        if (!info || info.columns === 0) { S.dataMsg[app.id] = { kind: "err", text: "This app has no data now." }; return; }
        app.info = info;
        const names = await S.data.dataColumns(app.id, info.columns);
        const rows = await S.data.dataRows(app.id, info.columns, info.rows, (fraction) => progress(fraction * 100));
        save(DataCsv.appFileName(app.name), DataCsv.appData(names, rows), "text/csv");
        S.dataMsg[app.id] = { kind: "ok", text: `Saved ${rows.length} rows.` };
        log(`${app.name} data: ${rows.length} rows saved`, "ok");
      } catch (error) {
        failed(error, (text) => { S.dataMsg[app.id] = { kind: "err", text }; });
      }
    });
  }

  function clearData(app) {
    S.dataMsg[app.id] = null;
    return run({ kind: "clear", id: app.id, phase: "start" }, async () => {
      let result;
      try {
        result = await S.data.dataClear(app.id, () => { if (S.work) { S.work.phase = "ask"; render(); } });
        app.info = (await S.data.dataInfo(app.id)) || app.info;
        await loadInfo();
      } catch (error) {
        failed(error, (text) => { S.dataMsg[app.id] = { kind: "err", text }; });
        return;
      }
      S.dataMsg[app.id] = result === "deleted" ? { kind: "ok", text: "Data deleted." } : { kind: "err", text: "Declined on StatTrak or timed out." };
      log(`${app.name} data: ${result}`, result === "deleted" ? "ok" : "warn");
    });
  }

  function downloadLog() {
    if (!S.log) return;
    save(`stattrak-log-${DataCsv.day()}.csv`, DataCsv.deviceLog(S.log), "text/csv");
  }

  /* ---- views ---------------------------------------------------------------------------- */

  const section = (title, body, act) =>
    `<section class="sync"><div class="sec-head"><h2 class="h2">${esc(title)}</h2>${act || ""}</div>${body}</section>`;
  const panel = (body, extra) => `<div class="row${extra ? " " + extra : ""}"><div class="row__panel">${body}</div></div>`;
  const msg = (m) => (m ? `<div class="sync__msg sync__msg--${m.kind}" role="status">${esc(m.text)}</div>` : "");
  const bar = (pct, label) => `<div id="syncBusy"><div class="inst__line"><span>${esc(label)}</span><span class="pct">${Math.round(pct || 0)}%</span></div>` +
    `<div class="prog"><div class="prog__fill" style="width:${Math.round(pct || 0)}%"></div></div></div>`;
  const ask = (question) => '<div class="inst__text"><div class="inst__big"><span class="dot dot--wait"></span> Answer on StatTrak</div>' +
    `<div class="inst__hint">StatTrak asks ${esc(question)} Hold button 2 = yes, hold button 1 = no.</div></div>`;

  function vPicture() {
    const w = S.work && S.work.kind.startsWith("pic") ? S.work : null;
    let h = "";
    if (S.devPic) {
      const on = S.devPic.state === StatTrakData.PIC_STATE__ON;
      h += '<div class="spic__now">' +
        `<img class="spic__img" alt="The start picture on StatTrak" src="${S.devPic.url}">` +
        '<div class="spic__acts">' +
        `<button class="sw" role="switch" type="button" aria-checked="${on}" data-act="sync-pic-show"${off()}><span class="sw__track"></span><span class="lbl">Show at power-on</span></button>` +
        `<button class="btn btn--ghost btn--sm" type="button" data-act="sync-pic-del"${off()}>Delete start picture</button></div></div>`;
      if (S.picAsk) {
        h += '<div class="sync__ask"><div class="msg">Delete the start picture on StatTrak?</div><div class="btns">' +
          `<button class="btn btn--ghost btn--sm" type="button" data-act="sync-pic-cancel">Cancel</button>` +
          `<button class="btn btn--danger btn--sm" type="button" data-act="sync-pic-really-del"${off()}>Delete</button></div></div>`;
      }
    } else {
      h += '<div class="row__desc">No start picture on StatTrak. It starts with the first app.</div>';
    }
    h += '<div class="spic__new"><div class="btns">' +
      `<button class="btn btn--ghost btn--sm" type="button" data-act="sync-pic-file"${off()}>Choose a PNG or JPG</button></div>` +
      '<a class="sync__link" href="designer/index.html">Make one in the Screen Designer, then use Download PNG there</a>';
    if (S.picErr) h += `<div class="sync__msg sync__msg--err" role="alert">${esc(S.picErr)}</div>`;
    if (S.pic) {
      h += '<div class="spic__edit">' +
        `<st-device id="syncPicDevice" anim="still" arg="${esc(S.pic.still)}" label="StatTrak screen: the new start picture"></st-device>` +
        '<div class="spic__ctl">' +
        `<div class="row__desc">${esc(S.pic.name)}${S.pic.scaled ? ` · ${S.pic.width} x ${S.pic.height}, scaled to fit` : " · 128 x 32"}</div>` +
        `<label class="spic__dark" for="syncDark">How dark is on <output id="syncDarkValue">${S.pic.dark}%</output></label>` +
        `<input id="syncDark" type="range" min="0" max="100" step="1" value="${S.pic.dark}"${off()}>` +
        (w && w.kind === "pic" ? bar(w.pct, "Sending") :
          `<div class="btns"><button class="btn btn--primary btn--sm" type="button" data-act="sync-pic-send"${off()}>Send to device</button></div>`) +
        "</div></div>";
    }
    if (S.picMsg) h += `<div class="sync__msg${/^Sent/.test(S.picMsg) ? " sync__msg--ok" : " sync__msg--err"}" role="status">${esc(S.picMsg)}</div>`;
    h += "</div>";
    return section("Start picture", panel(h, "spic"));
  }

  function vRestore() {
    const r = S.restore;
    if (!r) return "";
    if (r.phase === "ask") {
      return '<div class="sync__restore" id="syncRestore"><div class="inst">' + ask("RESTORE BACKUP?") + "</div></div>";
    }
    if (r.phase === "send") return '<div class="sync__restore" id="syncRestore">' + bar(S.work ? S.work.pct : 0, "Restoring") + "</div>";
    if (r.phase === "done") {
      const written = r.result.written === undefined ? "" : ` ${r.result.written} values written.`;
      let h = `<div class="sync__restore" id="syncRestore"><div class="res res--ok">${esc("Backup restored." + written)}</div>`;
      if (S.work) return h + `<div class="row__desc">${esc(S.work.text || "")}</div></div>`;
      h += r.skipped.length === 0 ? '<div class="row__desc">StatTrak now has the values of the file.</div>' :
        '<div class="sync__skipped"><b>Not restored: StatTrak skipped these</b><ul>' +
        r.skipped.map((s) => `<li>${esc(s.label)}: on StatTrak ${esc(s.device)}, in the file ${esc(s.file)}</li>`).join("") + "</ul></div>";
      return h + `<div class="btns"><button class="btn btn--ghost btn--sm" type="button" data-act="sync-restore-close">Close</button></div></div>`;
    }
    // The table: on the device now / in the file, with a tick for each part
    let h = `<div class="sync__restore" id="syncRestore"><div class="row__desc">${esc(r.name)}${r.file.created ? " · made " + esc(r.file.created.slice(0, 10)) : ""}${r.file.firmware ? " · firmware " + esc(r.file.firmware) : ""}</div>`;
    if (r.missing.length) {
      h += `<div class="sync__msg sync__msg--warn" role="alert">Install these apps first, or restore without them: ${esc(r.missing.map((a) => a.name).join(", "))}.</div>`;
    }
    h += '<div class="bk__wrap"><table class="bk"><thead><tr><th scope="col">Part</th><th scope="col">On the device now</th><th scope="col">In the file</th></tr></thead><tbody>';
    for (const part of BackupFile.PARTS) {
      const rows = r.rows.filter((row) => row.part === part);
      h += `<tr class="bk__part"><th colspan="3" scope="rowgroup"><label class="chk"><input type="checkbox" data-part="${part}"${r.parts[part] ? " checked" : ""}${off()}> ${esc(BackupFile.PART_LABELS[part])}</label></th></tr>`;
      for (const row of rows) {
        h += `<tr class="${row.same ? "" : "bk__diff"}${r.parts[part] ? "" : " bk__off"}"><td>${esc(row.label)}</td><td>${esc(row.device)}</td><td>${esc(row.file)}</td></tr>`;
      }
    }
    h += "</tbody></table></div>";
    const any = BackupFile.PARTS.some((p) => r.parts[p]);
    h += '<div class="btns">' +
      `<button class="btn btn--ghost btn--sm" type="button" data-act="sync-restore-close"${S.work ? " disabled" : ""}>Cancel</button>` +
      `<button class="btn btn--primary btn--sm" type="button" data-act="sync-restore-go"${any ? off() : " disabled"}>Restore</button></div>`;
    return h + "</div>";
  }

  function vBackup() {
    const w = S.work && (S.work.kind === "backup") ? S.work : null;
    let h = '<div class="row__desc">A backup file has your stats, settings, the setup of your apps, the Bluetooth name, the start picture and the device log. The apps themselves are not in it.</div>' +
      '<div class="btns">' +
      `<button class="btn btn--primary btn--sm" type="button" data-act="sync-backup"${off()}>Download backup</button>` +
      `<button class="btn btn--ghost btn--sm" type="button" data-act="sync-restore"${off()}>Restore from file</button></div>`;
    if (w) h += `<div class="inst__line" id="syncBusy"><span>${esc(w.text || "Reading StatTrak…")}</span></div>`;
    h += msg(S.backupMsg) + vRestore();
    return section("Backup", panel(h));
  }

  function vDataApp(app) {
    const w = S.work && S.work.id === app.id ? S.work : null;
    const i = app.info;
    let tags = `<span class="tag">${i.rows} / ${i.fit} rows</span>`;
    if (i.full) tags += '<span class="tag tag--err">Full</span>';
    if (i.otherColumns) tags += '<span class="tag tag--err">Old columns: delete the data</span>';
    const none = i.rows === 0 ? " disabled" : off();
    let h = `<div class="row" data-data-id="${app.id}"><div class="row__main sync__row"><div class="row__info"><div class="row__name">${esc(app.name)}</div><div class="row__meta">${tags}</div></div>` +
      '<div class="row__acts btns">' +
      `<button class="btn btn--ghost btn--sm" type="button" data-act="sync-csv"${none}>Download CSV</button>` +
      `<button class="btn btn--ghost btn--sm" type="button" data-act="sync-clear"${none}>Delete data</button></div></div>`;
    if (w && w.kind === "csv") h += `<div class="row__panel">${bar(w.pct, "Reading")}</div>`;
    if (w && w.kind === "clear") h += `<div class="row__panel">${w.phase === "ask" ? '<div class="inst">' + ask(`DELETE ${app.name} DATA?`) + "</div>" : '<div class="inst__line"><span>Asking StatTrak…</span></div>'}</div>`;
    if (S.dataMsg[app.id]) h += `<div class="row__panel">${msg(S.dataMsg[app.id])}</div>`;
    return h + "</div>";
  }

  function vData() {
    const body = S.dataApps.length ? `<div class="list">${S.dataApps.map(vDataApp).join("")}</div>` :
      panel('<div class="row__desc">No app on StatTrak keeps data.</div>');
    return section("App data", body);
  }

  function vLog() {
    const entries = S.log || [];
    const act = `<button class="btn btn--ghost btn--sm" type="button" data-act="sync-log-csv"${entries.length ? "" : " disabled"}>Download CSV</button>`;
    const body = !S.log ? panel('<div class="row__desc">Reading…</div>') : !entries.length ? panel('<div class="row__desc">The log is empty.</div>') :
      panel('<div class="row__desc">One line for each start, newest first. The reason tells how the run before it ended.</div>' +
        '<ol class="dlog">' + entries.map((e) =>
          `<li><span class="dlog__boot">Start ${e.boot}</span><span class="dlog__why">${esc(DataCsv.reasonText(e.reason))}</span>` +
          `<span class="dlog__bat">${esc(DataCsv.batteryText(e.battery))}</span>${e.firmwareChanged ? '<span class="tag new">New firmware</span>' : ""}</li>`).join("") + "</ol>");
    return section("Device log", body, act);
  }

  function view() {
    if (!S.data) return "";
    if (S.supported === null) return section("Backup and data", panel('<div class="row__desc">Reading StatTrak…</div>'));
    if (S.supported === false) return section("Backup and data", panel('<div class="row__desc sync__old">Update the firmware to use this.</div>'));
    return vPicture() + vBackup() + vData() + vLog();
  }

  /* ---- events (from js/apps.js) --------------------------------------------------------- */

  function onClick(b) {
    const act = b.dataset.act;
    if (act === "sync-pic-cancel") { S.picAsk = false; return render(); }
    if (act === "sync-restore-close") { if (!S.work) { S.restore = null; S.backupMsg = null; } return render(); }
    if (S.host.busy()) return;
    if (act === "sync-pic-file") { S.picErr = ""; return $("#syncPicFile").click(); }
    if (act === "sync-pic-send") return sendPicture();
    if (act === "sync-pic-show") return togglePicture();
    if (act === "sync-pic-del") { S.picAsk = true; return render(); }
    if (act === "sync-pic-really-del") return deletePicture();
    if (act === "sync-backup") return downloadBackup();
    if (act === "sync-restore") { S.backupMsg = null; return $("#syncBackupFile").click(); }
    if (act === "sync-restore-go") return startRestore();
    if (act === "sync-log-csv") return downloadLog();
    const row = b.closest("[data-data-id]");
    const app = row ? S.dataApps.find((a) => a.id === Number(row.dataset.dataId)) : null;
    if (app && act === "sync-csv") return downloadData(app);
    if (app && act === "sync-clear") return clearData(app);
  }

  function onInput(target) {
    if (target.id === "syncDark") onDark(target);
  }

  function onChange(target) {
    if (target.dataset.part && S.restore && S.restore.phase === "table" && !S.host.busy()) {
      S.restore.parts[target.dataset.part] = target.checked;
      render();
    }
  }

  function init(host) {
    S.host = host;
    const pick = (id, fn) => {
      const input = document.getElementById(id);
      if (!input) return;
      input.addEventListener("change", (event) => {
        const file = event.target.files && event.target.files[0];
        event.target.value = "";
        fn(file);
      });
    };
    pick("syncPicFile", onPicFile);
    pick("syncBackupFile", onBackupFile);
  }

  return { init, reset, connected, listChanged, view, busy, onClick, onInput, onChange, state: S };
})();
