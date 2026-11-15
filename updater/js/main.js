/* StatTrak Firmware Updater — UI controller
 * Vanilla JS. Keeps the BLE contract from ble.js intact: listens for
 * bleConnected / bleDisconnected / progress CustomEvents, drives the
 * 3-step DOM from a single state object + render().
 */

const GH_OWNER = "janiszah";
const GH_REPO  = "stattrak-fw";

// --- State ------------------------------------------------------------
const state = {
  connState: "idle",        // idle | scanning | connected
  device: null,             // { name, id, fw }
  fwTab: "upload",          // upload (default) | hosted
  releases: [],             // [{ id, name, tag, size, date, desc, htmlUrl }]
  releasesStatus: "loading",// loading | ready | error
  releaseAssets: [],        // [{ release, digest }] of all official .bin files
  uploadedFile: null,       // { name, size, bytes: Uint8Array, version, hash }
  step1Confirmed: false,
  step2Confirmed: false,
  flashState: "ready",      // ready | flashing | verifying | done | error
  progress: 0,
  log: [],
  notice: null,             // { kind: error | warn | ok, title, body } shown above the steps
  doneInfo: null,           // { version, device } that the device reported after the update
  consoleOpen: false,
  confirmOpen: false,
};

let bleOta = null;

/* After the transfer the device checks the file and restarts with the new firmware.
   The new firmware shows "Verifying…" until this page confirms it. Without the
   confirmation the device goes back to the old firmware at its next start.
   Thus "transfer complete" is not "update complete". */
const TIMING = {
  restartMs: 30 * 1000,     // time for the device to check the file and restart
  confirmMs: 6 * 60 * 1000, // the longest wait for the confirmation of the new firmware
  retryMs: 3 * 1000,        // wait before a confirmation with no answer is sent again
};
let restartTimer = null;
let confirmTimer = null;
let retryTimer = null;
let flashRun = 0;             // number of the transfer, to ignore late answers of an earlier one
let inRestartWindow = false;  // the device possibly did not restart yet
let sawDisconnect = false;    // the device went away after the transfer started
let acceptUnanswered = false; // a confirmation command got no answer: the device possibly did it
let readingDeviceInfo = false; // the page reads the version and the ID after a connection

/* A transfer that was not confirmed is kept here, thus a page that was closed
   or loaded again can confirm the firmware at the next pairing. */
const PENDING_KEY = "stattrak.pendingAccept";
const PENDING_MAX_AGE_MS = 24 * 60 * 60 * 1000;

/* One confirmation command at a time. A second caller waits for the same answer:
   two commands at the same time would both read the first answer that arrives. */
let acceptRequest = null;     // { ota, promise }
function askAccept(ota) {
  if (!acceptRequest || acceptRequest.ota !== ota) {
    const request = { ota, promise: null };
    request.promise = ota.acceptFirmware(true).finally(() => {
      if (acceptRequest === request) acceptRequest = null;
    });
    acceptRequest = request;
  }
  return acceptRequest.promise;
}

// The part of a Bluetooth name after "StatTrak | " ("" = the standard name "StatTrak")
function bleNamePart(name) {
  const m = /^StatTrak \| (.+)$/.exec(name || "");
  return m ? m[1] : "";
}

function setPendingAccept(info) {
  try {
    localStorage.setItem(PENDING_KEY, JSON.stringify({ uid: info.uid, version: info.version, time: Date.now() }));
  } catch (_) {}
}
function getPendingAccept() {
  try {
    const pending = JSON.parse(localStorage.getItem(PENDING_KEY) || "null");
    if (pending && typeof pending.time === "number" && Date.now() - pending.time < PENDING_MAX_AGE_MS) return pending;
  } catch (_) {}
  return null;
}
function clearPendingAccept() {
  try { localStorage.removeItem(PENDING_KEY); } catch (_) {}
}

// --- DOM refs ---------------------------------------------------------
const $ = (id) => document.getElementById(id);

// --- Helpers ----------------------------------------------------------
function fmtBytes(n) {
  if (n == null) return "—";
  if (n < 1024) return `${n} B`;
  if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
  return `${(n / (1024 * 1024)).toFixed(2)} MB`;
}

function nowHHMMSS() {
  const d = new Date();
  return `${String(d.getHours()).padStart(2,"0")}:${String(d.getMinutes()).padStart(2,"0")}:${String(d.getSeconds()).padStart(2,"0")}`;
}

function detectBleSupport() {
  const ua = navigator.userAgent || "";
  const hasBle = !!(navigator.bluetooth && typeof navigator.bluetooth.requestDevice === "function");
  let name = "This browser";
  if (/Firefox\//.test(ua)) name = "Firefox";
  else if (/Edg\//.test(ua)) name = "Edge";
  else if (/OPR\//.test(ua) || /Opera\//.test(ua)) name = "Opera";
  else if (/Chrome\//.test(ua)) name = "Chrome";
  else if (/Safari\//.test(ua)) name = "Safari";
  return { supported: hasBle, name };
}

function pushLog(message, kind = "info") {
  state.log.push({ t: nowHHMMSS(), k: kind, m: message });
  if (state.log.length > 200) state.log.splice(0, state.log.length - 200);
  renderLogCount();
  if (state.consoleOpen) renderConsoleModalBody();
}

function renderLogCount() {
  $("logCount").textContent = String(state.log.length);
}

// --- Derived selectors -----------------------------------------------
function selectedFirmware() {
  if (state.uploadedFile) {
    return {
      source: "local",
      name: state.uploadedFile.name,
      version: state.uploadedFile.version || "custom",
      size: state.uploadedFile.size,
      bytes: state.uploadedFile.bytes,
    };
  }
  return null;
}

/* Is the selected file one of the official releases? official | unofficial | unknown */
function officialStatus() {
  if (!state.uploadedFile || state.releasesStatus !== "ready") return { status: "unknown" };
  return FirmwareFile.matchRelease(state.uploadedFile.hash, state.releaseAssets);
}

function activeStepNumber() {
  // The device is away while it restarts. The page stays on step 3 and waits.
  if (state.flashState === "done" || state.flashState === "verifying") return 3;
  if (!state.step1Confirmed) return 1;        // stay on step 1 until Next, even if paired
  if (!state.step2Confirmed) return 2;
  return 3;
}

// --- Render -----------------------------------------------------------
function render() {
  if (typeof renderTabs === "function") renderTabs(); // js/apps.js: no tab change during an update
  if (typeof DevBar !== "undefined") DevBar.render();   // js/devbar.js: the StatTrak button in the header
  renderBanner();
  renderNotice();
  renderSteps();
  renderStep1();
  renderStep2();
  renderStep3();
}

function renderBanner() {
  const { supported, name } = detectBleSupport();
  const banner = $("compatBanner");
  banner.hidden = supported;
  $("browserName").textContent = name;
}

function renderNotice() {
  const banner = $("noticeBanner");
  const notice = state.notice;
  banner.hidden = !notice;
  if (!notice) return;
  banner.className = `banner banner--${notice.kind}`;
  $("noticeTitle").textContent = notice.title;
  $("noticeBody").textContent = notice.body;
}

function renderSteps() {
  const active = activeStepNumber();
  [1, 2, 3].forEach((n) => {
    const section = $(`step${n}`);
    const done = stepIsDone(n);
    const isActive = n === active;
    const locked = !done && !isActive;
    // A step expands when it's active, or when it's step 3 and done
    // (so the success panel can render).
    const expanded = isActive || (n === 3 && done);
    section.classList.toggle("step--active", isActive);
    section.classList.toggle("step--done", done);
    section.classList.toggle("step--locked", locked);
    section.classList.toggle("step--collapsed", !expanded);

    const badge = $(`step${n}Badge`);
    if (done) {
      badge.innerHTML = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"/></svg>';
    } else {
      badge.textContent = String(n);
    }
  });

  // step statuses
  setStatus("step1Status", step1StatusText(), step1StatusClass(), step1StatusWithDot());
  setStatus("step2Status", step2StatusText(), step2StatusClass());
  setStatus("step3Status", step3StatusText(), step3StatusClass(), state.flashState === "flashing" || state.flashState === "verifying");

  // edit buttons — only when the step is done AND not currently active
  $("step1EditBtn").hidden = !(stepIsDone(1) && 1 !== active);
  $("step2EditBtn").hidden = !(stepIsDone(2) && 2 !== active);
}

function stepIsDone(n) {
  if (n === 1) return !!state.device;                            // paired = done visually
  if (n === 2) return !!selectedFirmware() && state.step2Confirmed;
  if (n === 3) return state.flashState === "done";
  return false;
}

function setStatus(id, text, cls, withDot = false) {
  const el = $(id);
  el.className = `step__status ${cls || ""}`.trim();
  if (withDot) {
    el.innerHTML = `<span class="step__status-dot"></span>${escapeHtml(text)}`;
  } else {
    el.textContent = text;
  }
}

function step1StatusText() {
  if (state.device) return "Connected";
  if (state.connState === "scanning") return "Scanning";
  return "Ready";
}
function step1StatusClass() {
  if (state.device) return "step__status--live";               // green pulsing dot
  if (state.connState === "scanning") return "step__status--scanning"; // yellow pulsing dot
  if (activeStepNumber() === 1) return "step__status--active";
  return "";
}
function step1StatusWithDot() {
  return !!state.device || state.connState === "scanning";
}
function step2StatusText() {
  if (stepIsDone(2)) {
    const fw = selectedFirmware();
    return fw ? (fw.version === "custom" ? "Custom file" : fw.version) : "Pick one";
  }
  return "Pick one";
}
function step2StatusClass() {
  if (stepIsDone(2)) return "step__status--done";
  if (activeStepNumber() === 2) return "step__status--active";
  return "";
}
function step3StatusText() {
  if (state.flashState === "done") return "Complete";
  if (state.flashState === "flashing") return "Writing";
  if (state.flashState === "verifying") return "Verifying";
  return "Ready";
}
function step3StatusClass() {
  if (stepIsDone(3)) return "step__status--done";
  if (state.flashState === "flashing" || state.flashState === "verifying") return "step__status--live";
  if (activeStepNumber() === 3) return "step__status--active";
  return "";
}

function renderStep1() {
  const paired = !!state.device;
  $("step1Before").hidden = paired;
  $("step1After").hidden = !paired;

  $("pairBtnLabel").textContent = state.connState === "scanning" ? "Scanning…" : "Pair device";
  $("pairBtn").disabled = !detectBleSupport().supported || state.connState === "scanning";

  if (paired) {
    $("deviceName").textContent = state.device.name || "—";
    $("deviceIdVal").textContent = state.device.id || "—";
    $("deviceFw").textContent = state.device.fw || "—";
  }
}

function renderStep2() {
  // tabs
  document.querySelectorAll(".fw-tab").forEach((btn) => {
    const on = btn.dataset.tab === state.fwTab;
    btn.classList.toggle("fw-tab--active", on);
    btn.setAttribute("aria-selected", on ? "true" : "false");
  });
  $("hostedPane").hidden = state.fwTab !== "hosted";
  $("uploadPane").hidden = state.fwTab !== "upload";

  // release list — hosted tab shows GitHub release links; selection
  // happens in the Upload tab after the user downloads a .bin manually.
  const list = $("releaseList");
  if (state.releasesStatus === "loading") {
    list.innerHTML = '<div class="release-empty">Loading releases from GitHub…</div>';
  } else if (state.releasesStatus === "error") {
    list.innerHTML = '<div class="release-empty">Couldn\u2019t reach GitHub. Check your connection or use the Upload tab.</div>';
  } else if (state.releases.length === 0) {
    list.innerHTML = '<div class="release-empty">No releases published yet.</div>';
  } else {
    const dlIcon = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.75" stroke-linecap="round" stroke-linejoin="round"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>';
    list.innerHTML = state.releases
      .map(
        (r) => `
      <a class="release" href="${escapeHtml(r.htmlUrl || "#")}" target="_blank" rel="noreferrer">
        <div class="release__main">
          <h3 class="release__name">
            ${escapeHtml(r.name)}
            <span class="chip chip--${r.tag}">${r.tag}</span>
          </h3>
          <p class="release__desc">${escapeHtml(r.desc || "")}</p>
        </div>
        <div class="release__meta">${r.size != null ? fmtBytes(r.size) : "—"}</div>
        <div class="release__meta">${r.date}</div>
        <div class="release__download">${dlIcon} Download</div>
      </a>`
      )
      .join("");
  }

  // file card (lives outside the tab panes so it stays visible after
  // the user swaps tabs to download another release)
  $("dropzone").hidden = !!state.uploadedFile;
  $("fileCard").hidden = !state.uploadedFile;
  if (state.uploadedFile) {
    $("fileCardName").textContent = state.uploadedFile.name;
    const official = officialStatus();
    let officialText = "";
    if (official.status === "official") officialText = ` · official release <strong>${escapeHtml(official.release)}</strong>`;
    else if (official.status === "unofficial") officialText = " · <strong>not an official release</strong>";
    $("fileCardMeta").innerHTML = `${fmtBytes(state.uploadedFile.size)} · StatTrak firmware · version <strong>${escapeHtml(
      state.uploadedFile.version || "custom"
    )}</strong>${officialText}`;
  }

  // next button
  $("step2NextRow").hidden = !selectedFirmware() || state.step2Confirmed;
}

function renderStep3() {
  const fw = selectedFirmware();
  const canShowSummary = stepIsDone(1) && stepIsDone(2);

  if (canShowSummary) {
    $("sumDeviceName").textContent = state.device?.name || "—";
    $("sumDeviceFw").textContent = state.device?.fw || "—";
    $("sumDeviceId").textContent = state.device?.id || "—";
    $("sumFwName").textContent = fw?.name || "—";
    $("sumFwVersion").textContent = fw?.version || "—";
    $("sumFwSize").textContent = fw?.size != null ? fmtBytes(fw.size) : "—";
  }

  // progress
  const flashing = state.flashState === "flashing" || state.flashState === "verifying";
  $("flashProgress").hidden = !flashing;
  if (flashing) {
    $("flashLabel").textContent = state.flashState === "verifying" ? "Waiting for the device" : "Writing firmware";
    $("flashNote").textContent = state.flashState === "verifying" ? "Keep this page open" : "Do not disconnect";
    $("flashPct").textContent = `${Math.floor(state.progress)}%`;
    $("flashFill").style.width = `${state.progress}%`;
    const totalBytes = fw?.size || 0;
    $("flashBytes").textContent = `${fmtBytes(Math.floor((totalBytes * state.progress) / 100))} / ${fmtBytes(totalBytes)}`;
  }
  $("flashHint").hidden = state.flashState !== "verifying";

  // flash button
  const canFlash = stepIsDone(1) && stepIsDone(2) && state.flashState !== "flashing" && state.flashState !== "verifying" && state.flashState !== "done";
  const flashBtn = $("flashBtn");
  flashBtn.disabled = !canFlash;
  $("flashBtnLabel").textContent =
    state.flashState === "flashing" ? "Flashing…" : state.flashState === "verifying" ? "Waiting…" : "Flash firmware";

  // success panel
  $("step3Pre").hidden = state.flashState === "done";
  $("successPanel").hidden = state.flashState !== "done";
  if (state.flashState === "done") {
    // The version that the device reported after its restart
    $("successVersion").textContent = state.doneInfo?.version || "—";
    $("successSize").textContent = fw?.size != null ? fmtBytes(fw.size) : "—";
    $("successDevice").textContent = state.doneInfo?.device || "—";
  }
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

// --- GitHub release fetch --------------------------------------------
async function loadReleases() {
  state.releasesStatus = "loading";
  render();
  try {
    const res = await fetch(`https://api.github.com/repos/${GH_OWNER}/${GH_REPO}/releases`);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const releases = await res.json();

    let firstStable = true;
    const mapped = releases
      .filter((r) => !r.draft)
      .slice(0, 4)
      .map((r) => {
        const binAsset = r.assets?.find((a) => /\.bin$/i.test(a.name));
        let tag;
        if (r.prerelease) tag = "beta";
        else if (firstStable) { tag = "latest"; firstStable = false; }
        else tag = "stable";
        return {
          id: r.id.toString(),
          name: r.tag_name || r.name,
          tag,
          size: binAsset?.size ?? null,
          htmlUrl: r.html_url,
          date: (r.published_at || "").slice(0, 10),
          desc: (r.body || "").split(/\r?\n/)[0] || "No description.",
        };
      });

    state.releases = mapped;
    state.releaseAssets = releases
      .filter((r) => !r.draft)
      .flatMap((r) =>
        (r.assets || [])
          .filter((a) => /\.bin$/i.test(a.name))
          .map((a) => ({ release: r.tag_name || r.name, digest: a.digest || null }))
      );
    state.releasesStatus = "ready";
    pushLog(`> fetched ${mapped.length} release${mapped.length === 1 ? "" : "s"} from GitHub.`, "ok");
  } catch (err) {
    console.error("Release fetch failed:", err);
    state.releasesStatus = "error";
    pushLog(`> release fetch failed: ${err.message}`, "warn");
  }
  render();
}

// --- BLE wiring -------------------------------------------------------
async function onPairClick() {
  if (state.connState === "scanning") return;
  try {
    state.connState = "scanning";
    pushLog("> requesting navigator.bluetooth device…");
    render();

    // Each pairing starts fresh: the user selects the device in the browser dialog
    if (bleOta) {
      bleOta.removeEventListener("bleConnected", onBleConnected);
      bleOta.removeEventListener("bleDisconnected", onBleDisconnected);
      bleOta.removeEventListener("bleConnectFailed", onBleConnectFailed);
      bleOta.removeEventListener("progress", onBleProgress);
      bleOta.disconnect();
      bleOta = null;
    }

    const created = await BleOta.create();
    created.addEventListener("bleConnected", onBleConnected);
    created.addEventListener("bleDisconnected", onBleDisconnected);
    created.addEventListener("bleConnectFailed", onBleConnectFailed);
    created.addEventListener("progress", onBleProgress);
    bleOta = created;
  } catch (err) {
    console.error(err);
    pushLog(`> pairing failed: ${err.message}`, "warn");
    state.connState = "idle";
    render();
  }
}

async function onBleConnected() {
  pushLog("> GATT connected.", "ok");
  state.connState = "connected";
  readingDeviceInfo = true;
  try {
    const version = await bleOta.getVersion();
    const uid = await bleOta.getUid();
    if (!uid) throw new Error("the device did not send its ID");
    const uidHex = Array.from(uid).map((b) => b.toString(16).padStart(2, "0").toUpperCase()).join("");
    state.device = {
      name: bleOta.device?.name || "StatTrak",
      id: uidHex,
      fw: (version || "unknown").replace(/[\s\x00]+$/g, ""),
    };
    pushLog(`> device info: fw ${state.device.fw}, uid ${uidHex}`, "ok");
    SiteEvents.emitDevice({ event: "connected", serial: uidHex, fw: state.device.fw, name: bleNamePart(state.device.name) });
  } catch (err) {
    console.error(err);
    pushLog(`> device info read failed: ${err.message}`, "warn");
    render();
    return;
  } finally {
    readingDeviceInfo = false;
  }

  if (state.flashState === "verifying") {
    // The device is back after the transfer of this page
    await tryConfirmAfterTransfer();
  } else {
    // A transfer of an earlier page session waits for its confirmation
    const pending = getPendingAccept();
    if (pending && pending.uid === state.device.id) await confirmWaitingFirmware(false, pending);
  }
  render();
}

function onBleDisconnected() {
  pushLog("> disconnected from device.", "warn");
  state.connState = "idle";
  if (state.device) SiteEvents.emitDevice({ event: "disconnected", serial: state.device.id });
  state.device = null;
  state.step1Confirmed = false;
  if (state.flashState === "flashing" || state.flashState === "verifying") sawDisconnect = true;
  if (state.flashState === "verifying") pushLog("> waiting for the device to come back…");
  // keep firmware selection so user can retry after reconnect
  render();
}

function onBleConnectFailed() {
  pushLog("> could not connect to the device.", "warn");
  state.connState = "idle";
  if (state.flashState === "verifying") {
    noAnswerAfterTransfer();
    return;
  }
  state.notice = {
    kind: "warn",
    title: "Could not connect",
    body: "Make sure that the device shows the update screen and is near the computer, then pair again.",
  };
  render();
}

function onBleProgress(event) {
  state.progress = event.detail?.progress ?? 0;
  render();
}

/* The device did not confirm the new firmware in time. The mark of the open
   transfer stays: the next pairing confirms the firmware. */
function noAnswerAfterTransfer() {
  if (state.flashState !== "verifying") return;
  // Back to step 1, where the user can pair again
  try {
    if (bleOta) bleOta.disconnect();
  } catch (_) {}
  failFlash(
    "No answer from the device",
    "Switch the device on. If its display shows “Verifying…”, click “Pair device”: this page then confirms the new firmware. " +
      "If the device shows the counter, the update did not work and the device has its old firmware: start again."
  );
}

/* The update did not complete. The device keeps or gets back its old firmware. */
function failFlash(title, body, kind = "error") {
  clearTimeout(restartTimer);
  clearTimeout(confirmTimer);
  clearTimeout(retryTimer);
  inRestartWindow = false;
  state.flashState = "error";
  state.notice = { kind, title, body };
  pushLog(`> ${title.toLowerCase()}.`, "warn");
  render();
}

async function onFlashConfirm() {
  const fw = selectedFirmware();
  if (!fw || !bleOta) return;
  const bytes = fw.bytes;
  if (!bytes) {
    pushLog("> no firmware bytes available.", "warn");
    return;
  }

  state.flashState = "flashing";
  state.flashFromFw = state.device?.fw || "";
  state.progress = 0;
  state.notice = null;
  sawDisconnect = false;
  acceptUnanswered = false;
  flashRun++;
  pushLog(`> starting OTA: ${fw.name} (${fmtBytes(bytes.length)})`);
  render();

  /* A firmware of an earlier transfer that still waits is confirmed first.
     After this, a "yes" to the confirmation can only come from the firmware
     of this transfer. */
  try {
    if (await askAccept(bleOta)) {
      clearPendingAccept();
      pushLog("> an earlier firmware waited for its confirmation. it is confirmed now.", "ok");
    }
  } catch (err) {
    console.error(err);
    failFlash(
      "The device did not answer",
      "On the device, leave the update screen and open it again. Then pair the device and start again."
    );
    return;
  }

  // An earlier success switched this off. The page must find the device again after its restart.
  bleOta.wantConnection = true;

  let sent = false;
  try {
    sent = await bleOta.update(bytes);
  } catch (err) {
    console.error(err);
    pushLog(`> OTA error: ${err.message}`, "warn");
  }

  if (!sent) {
    failFlash(
      "The transfer stopped",
      "The device keeps its old firmware. On the device, leave the update screen and open it again. Then pair the device and start again."
    );
    return;
  }

  /* All data is on the device. This is not the end: the device now checks the
     file and restarts. The update is complete when the new firmware answers
     and this page confirms it (see onBleConnected). */
  pushLog("> transfer complete. waiting for the device to restart…", "ok");
  state.flashState = "verifying";
  state.progress = 100;
  setPendingAccept({ uid: state.device?.id || "", version: fw.version });
  inRestartWindow = true;
  clearTimeout(restartTimer);
  restartTimer = setTimeout(onRestartWindowEnd, TIMING.restartMs);
  clearTimeout(confirmTimer);
  confirmTimer = setTimeout(noAnswerAfterTransfer, TIMING.confirmMs);
  render();
}

/* The time for the restart is over. A device that is connected now and did not
   restart refused the file (its display shows "Error"). */
async function onRestartWindowEnd() {
  inRestartWindow = false;
  if (state.flashState !== "verifying") return;
  if (bleOta && bleOta.ready) {
    await tryConfirmAfterTransfer();
    render();
  }
}

/* Asks the device to keep the new firmware. Only a new firmware that waits
   ("Verifying…" on the display) answers "yes". */
async function tryConfirmAfterTransfer() {
  const ota = bleOta;
  const run = flashRun;
  // True for an answer that arrives after this connection or this transfer ended
  const late = () => ota !== bleOta || run !== flashRun || state.flashState !== "verifying";

  let accepted;
  try {
    accepted = await askAccept(ota);
  } catch (err) {
    if (late()) return;
    console.error(err);
    pushLog(`> no answer to the confirmation: ${err.message}`, "warn");
    acceptUnanswered = true;
    // Ask again, also without a new connection
    clearTimeout(retryTimer);
    retryTimer = setTimeout(async () => {
      // After a new connection, onBleConnected asks (two commands at the same time mix their answers)
      if (late() || !ota.ready || readingDeviceInfo) return;
      await tryConfirmAfterTransfer();
      render();
    }, TIMING.retryMs);
    return;
  }
  if (late()) return;

  if (accepted) {
    clearTimeout(restartTimer);
    clearTimeout(confirmTimer);
    clearTimeout(retryTimer);
    inRestartWindow = false;
    clearPendingAccept();
    state.flashState = "done";
    state.doneInfo = { version: state.device?.fw || "—", device: state.device?.name || "—" };
    state.notice = null;
    // The work is done. Do not connect again when the device leaves the update screen.
    bleOta.wantConnection = false;
    pushLog(`> the device confirmed the new firmware (${state.device?.fw || "?"}).`, "ok");
    SiteEvents.emitDevice({ event: "flashed", serial: state.device?.id || "", fw: state.device?.fw || "",
      previousFw: state.flashFromFw || "", name: bleNamePart(state.device?.name) });
    return;
  }

  if (inRestartWindow) {
    // This is the old firmware, before its restart
    pushLog("> the device did not restart yet.");
    return;
  }

  if (acceptUnanswered) {
    /* An earlier confirmation got no answer. The device possibly did it, and
       then it has nothing more to confirm. The page cannot know. */
    failFlash(
      "Look at the device display",
      `The device did not answer one confirmation, and now it has nothing to confirm. It runs firmware ${state.device?.fw || "?"}. ` +
        "If its display shows “Complete”, the update worked. If it shows “Verifying…”, pair the device again.",
      "warn"
    );
    return;
  }

  clearPendingAccept();
  if (sawDisconnect) {
    failFlash(
      "The new firmware did not start",
      `The device went back to the firmware that it had before (${state.device?.fw || "version not known"}). ` +
        "The file is possibly damaged or not made for this device."
    );
  } else {
    failFlash(
      "The device refused the file",
      "The device did not restart. If its display shows “Error”, the file is not a valid firmware. The device keeps its old firmware."
    );
  }
}

function transferRuns() {
  return state.flashState === "flashing" || state.flashState === "verifying";
}

/* Confirms a firmware that waits on the device and that this page session did
   not transfer: the page was closed or loaded again after the transfer, or the
   user clicked the button. */
async function confirmWaitingFirmware(manual, pending) {
  const ota = bleOta;
  let accepted;
  try {
    accepted = await askAccept(ota);
  } catch (err) {
    if (ota !== bleOta || transferRuns()) return;
    console.error(err);
    pushLog(`> no answer to the confirmation: ${err.message}`, "warn");
    if (manual) {
      state.notice = { kind: "warn", title: "No answer", body: "The device did not answer. Pair the device again." };
      render();
    }
    return;
  }
  // A transfer that started in the meantime asked the same question, and it uses the answer
  if (ota !== bleOta || transferRuns()) return;

  clearPendingAccept();
  if (accepted) {
    pushLog(`> the device confirmed the new firmware (${state.device?.fw || "?"}).`, "ok");
    if (state.flashState === "error") state.flashState = "ready";
    // The device leaves when the user presses its button. Do not connect again then.
    bleOta.wantConnection = false;
    state.notice = {
      kind: "ok",
      title: "Firmware confirmed",
      body: `The device now keeps firmware ${state.device?.fw || ""}. Its display shows “Complete”.`,
    };
  } else {
    pushLog("> no firmware waits for a confirmation.");
    if (manual) {
      state.notice = {
        kind: "warn",
        title: "Nothing to confirm",
        body: "This device has no new firmware that waits for a confirmation.",
      };
    } else {
      // An earlier transfer was not confirmed, and now nothing waits on the device
      state.notice = {
        kind: "warn",
        title: "No new firmware on the device",
        body: `The device runs firmware ${state.device?.fw || "?"}. No new firmware waits for a confirmation. ` +
          `If this is not the version that you sent (${pending?.version || "?"}), start the update again.`,
      };
    }
  }
  render();
}

// --- Modals -----------------------------------------------------------
function openConsole() {
  state.consoleOpen = true;
  mountConsoleModal();
}
function closeConsole() {
  state.consoleOpen = false;
  unmountModal();
}
function mountConsoleModal() {
  const root = $("modalRoot");
  const tpl = $("consoleModalTpl");
  root.innerHTML = "";
  root.appendChild(tpl.content.cloneNode(true));
  root.hidden = false;

  root.querySelector("[data-close]").addEventListener("click", closeConsole);
  root.querySelector("[data-clear]").addEventListener("click", () => {
    state.log.length = 0;
    renderLogCount();
    renderConsoleModalBody();
  });
  root.querySelector(".modal-backdrop").addEventListener("click", (e) => {
    if (e.target.matches("[data-dismiss]")) closeConsole();
  });
  renderConsoleModalBody();
}
function renderConsoleModalBody() {
  const root = $("modalRoot");
  const body = root.querySelector("[data-log-body]");
  if (!body) return;
  body.innerHTML = state.log
    .map((l) => `<div class="log__line log__line--${l.k}"><span class="log__time">${l.t}</span>${escapeHtml(l.m)}</div>`)
    .join("");
  body.scrollTop = body.scrollHeight;
  const count = root.querySelector("[data-log-count]");
  if (count) count.textContent = String(state.log.length);
}

function openConfirm() {
  const fw = selectedFirmware();
  if (!fw) return;
  state.confirmOpen = true;
  const root = $("modalRoot");
  const tpl = $("confirmModalTpl");
  root.innerHTML = "";
  root.appendChild(tpl.content.cloneNode(true));
  root.hidden = false;

  root.querySelector("[data-fw-name]").textContent = fw.name;
  root.querySelector("[data-fw-size]").textContent = fmtBytes(fw.size || 0);
  root.querySelector("[data-device-name]").textContent = state.device?.name || "device";
  root.querySelector("[data-unofficial]").hidden = officialStatus().status === "official";
  renderTabs();   // The menu links are locked while this question is open (js/apps.js)

  root.querySelector("[data-cancel]").addEventListener("click", closeConfirm);
  root.querySelector("[data-confirm]").addEventListener("click", async () => {
    closeConfirm();
    await onFlashConfirm();
  });
  root.querySelector(".modal-backdrop").addEventListener("click", (e) => {
    if (e.target.matches("[data-dismiss]")) closeConfirm();
  });
}
function closeConfirm() {
  state.confirmOpen = false;
  unmountModal();
  renderTabs();
}

function unmountModal() {
  const root = $("modalRoot");
  root.innerHTML = "";
  root.hidden = true;
}

// --- File import ------------------------------------------------------
function handleFile(file) {
  if (!file) return;
  if (file.size > FirmwareFile.MAX_SIZE) {
    // Do not read a very large file into memory
    state.uploadedFile = null;
    state.step2Confirmed = false;
    state.notice = {
      kind: "error",
      title: "This file cannot be used",
      body: `${file.name}: The file has ${file.size} bytes. The largest firmware is ${FirmwareFile.MAX_SIZE} bytes.`,
    };
    pushLog(`> refused local file: ${file.name} is too large.`, "warn");
    render();
    return;
  }
  const reader = new FileReader();
  reader.onload = async (ev) => {
    const bytes = new Uint8Array(ev.target.result);

    // A wrong file is refused here, not after some minutes of transfer
    const info = FirmwareFile.inspect(bytes);
    if (!info.ok) {
      state.uploadedFile = null;
      state.step2Confirmed = false;
      state.notice = { kind: "error", title: "This file cannot be used", body: `${file.name}: ${info.problem}` };
      pushLog(`> refused local file: ${file.name}. ${info.problem}`, "warn");
      render();
      return;
    }

    const uploaded = {
      name: file.name,
      size: bytes.length,
      bytes,
      version: info.version || "custom",
      hash: null,
    };
    state.uploadedFile = uploaded;
    state.notice = null;
    state.fwTab = "upload";
    pushLog(`> loaded local file: ${file.name} (${fmtBytes(bytes.length)})`, "ok");
    render();

    // The hash shows if the file is one of the official releases
    try {
      uploaded.hash = await FirmwareFile.sha256Hex(bytes);
    } catch (err) {
      console.error(err);
    }
    if (state.uploadedFile === uploaded) render();
  };
  reader.readAsArrayBuffer(file);
}

// --- Wiring -----------------------------------------------------------
function wireEvents() {
  // topbar
  $("consoleBtn").addEventListener("click", openConsole);

  // step 1
  $("pairBtn").addEventListener("click", onPairClick);
  $("step1NextBtn").addEventListener("click", () => {
    state.step1Confirmed = true;
    render();
  });
  $("confirmFwBtn").addEventListener("click", () => {
    if (bleOta && bleOta.ready) confirmWaitingFirmware(true);
  });
  $("disconnectBtn").addEventListener("click", () => {
    try {
      if (bleOta) bleOta.disconnect();
    } catch (_) {}
    if (state.device) SiteEvents.emitDevice({ event: "disconnected", serial: state.device.id });
    state.device = null;
    state.step1Confirmed = false;
    state.connState = "idle";
    pushLog("> user disconnected.", "warn");
    render();
  });
  $("step1EditBtn").addEventListener("click", () => {
    state.step1Confirmed = false;
    render();
  });

  // step 2 tabs
  document.querySelectorAll(".fw-tab").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.fwTab = btn.dataset.tab;
      state.step2Confirmed = false;
      render();
    });
  });

  // dropzone
  const dz = $("dropzone");
  const fileInput = $("fileInput");
  dz.addEventListener("click", () => fileInput.click());
  dz.addEventListener("keydown", (e) => {
    if (e.key === "Enter" || e.key === " ") { e.preventDefault(); fileInput.click(); }
  });
  fileInput.addEventListener("change", (e) => {
    handleFile(e.target.files?.[0]);
    fileInput.value = ""; // the same file can be selected again
  });
  dz.addEventListener("dragenter", (e) => { e.preventDefault(); dz.classList.add("dropzone--hover"); });
  dz.addEventListener("dragover", (e) => { e.preventDefault(); });
  dz.addEventListener("dragleave", () => dz.classList.remove("dropzone--hover"));
  dz.addEventListener("drop", (e) => {
    e.preventDefault();
    dz.classList.remove("dropzone--hover");
    handleFile(e.dataTransfer?.files?.[0]);
  });
  // also accept drops anywhere on the upload pane
  document.addEventListener("dragover", (e) => e.preventDefault());

  $("fileCardRemove").addEventListener("click", () => {
    state.uploadedFile = null;
    state.step2Confirmed = false;
    render();
  });

  $("step2NextBtn").addEventListener("click", () => {
    state.step2Confirmed = true;
    render();
  });
  $("step2EditBtn").addEventListener("click", () => {
    state.step2Confirmed = false;
    render();
  });

  // step 3
  $("flashBtn").addEventListener("click", openConfirm);
  $("flashAgainBtn").addEventListener("click", () => {
    state.flashState = "ready";
    state.progress = 0;
    state.step2Confirmed = false;
    render();
  });
  $("doneBtn").addEventListener("click", () => {
    try {
      if (bleOta) bleOta.disconnect();
    } catch (_) {}
    if (state.device) SiteEvents.emitDevice({ event: "disconnected", serial: state.device.id });
    state.notice = null;
    state.flashState = "ready";
    state.progress = 0;
    state.step1Confirmed = false;
    state.step2Confirmed = false;
    state.device = null;
    state.uploadedFile = null;
    state.connState = "idle";
    render();
  });

  // Escape closes modals
  document.addEventListener("keydown", (e) => {
    if (e.key !== "Escape") return;
    if (state.confirmOpen) closeConfirm();
    else if (state.consoleOpen) closeConsole();
  });
}

// --- Boot -------------------------------------------------------------
document.addEventListener("DOMContentLoaded", () => {
  pushLog("> StatTrak Updater ready.");
  const ble = detectBleSupport();
  pushLog(`> runtime: ${ble.name} · BLE ${ble.supported ? "available" : "unavailable"}.`, ble.supported ? "ok" : "warn");

  wireEvents();
  render();
  loadReleases();
});
