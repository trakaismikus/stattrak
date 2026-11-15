/* StatTrak device picture with a live display: <st-device anim="NAME" arg="..." label="...">.
 *
 * The picture (assets/device.webp) is a render of the 3D model, front view: the device plus 2 mm
 * on each side (63.2 x 34.133 mm). The display is a canvas at the place of the real 128 x 32
 * pixels. The two round buttons under the body light up when an animation presses them.
 *
 * The screens come from the firmware, so a change of the device screens shows here:
 * - "sync-idle", "sync-question", "sync-installed": Settings > Apps: SYNC, drawn by
 *   AppPictures.syncScreen. tests/apps.test.js compares it with the firmware screens
 *   (tests/sync-screens.json), and tests/test_updater_screens.py (firmware checks) fails when the
 *   firmware draws them in an other way.
 * - "update-1.0.1": the way to the update mode on firmware 1.0.1 (assets/update-path-1.0.1.js,
 *   from the 1.0.1 simulator: tools/device_art/fw101/make_update_path.py). index.html loads it.
 * - "still": one picture of the page (the start picture of the Apps tab): arg = the id that
 *   StDevice.still(pixels) gives. A new arg on the element shows the new picture.
 *
 * The animations run on one clock: a list that the page draws again continues where it was.
 * With "reduce motion" on, the element shows one frame.
 */
const StDevice = (() => {
  // Places in % of the picture (front view, mm: see tools/device_art/README.md)
  const SCREEN = { left: 11.946, top: 22.969, width: 65.823, height: 30.469 };
  const LIT = "#f6f7ff";

  function fromRows(rows) {                       // 32 rows of 32 hex digits -> { w, h, d }
    const d = new Uint8Array(128 * 32);
    rows.forEach((row, y) => {
      for (let i = 0; i < 32; i++) {
        const n = parseInt(row[i], 16);
        for (let b = 0; b < 4; b++) d[y * 128 + i * 4 + b] = (n >> (3 - b)) & 1;
      }
    });
    return { w: 128, h: 32, d };
  }

  /* ---- animations: name -> function (arg) -> [{ b: pixels, ms, press }] (or a Promise of it) ---
     press: "" none, "1" / "2" a short press, "H1" / "H2" held, "B" both buttons */
  const HOLD_MS = 700;                             // a long press (main/app.h)
  const stills = new Map();                        // id -> 128 x 32 pixels (StDevice.still)
  let stillCount = 0;
  const anims = {
    "still": (id) => (stills.has(id) ? [{ b: { w: 128, h: 32, d: stills.get(id) }, ms: 1000, press: "" }] : []),
    "sync-idle": () => [0, 1, 2, 3].map((dots) => ({ b: AppPictures.syncScreen("idle", dots), ms: 512, press: "" })),
    "sync-question": (name) => {
      const frames = [{ b: AppPictures.syncScreen("question", name), ms: 1100, press: "" }];
      for (let t = 50; t <= HOLD_MS; t += 50) {
        frames.push({ b: AppPictures.syncScreen("question", name, { no: 0, yes: Math.round((t / HOLD_MS) * 100) }), ms: 50, press: "H2" });
      }
      frames.push({ b: AppPictures.syncScreen("question", name, { no: 0, yes: 100 }), ms: 500, press: "H2" });
      return frames;
    },
    "sync-installed": (name) => [{ b: AppPictures.syncScreen("installed", name), ms: 1000, press: "" }],
    "update-1.0.1": () => {
      const data = typeof window !== "undefined" && window.STATTRAK_UPDATE_PATH;   // assets/update-path-1.0.1.js
      if (!data) throw new Error("assets/update-path-1.0.1.js is not loaded");
      return data.frames.map((f) => ({ b: fromRows(f.rows), ms: f.ms, press: f.press === "-" ? "" : f.press }));
    },
  };

  /* A short press lasts 0.1 s on the device: too short to see. The button stays lit for at least
     LINGER_MS (frame.lit), the display keeps the real timing. */
  const LINGER_MS = 400;
  function linger(list) {
    const out = [];
    let last = "", until = -Infinity, t = 0;
    for (const f of list) {
      if (f.press) {
        last = f.press; until = t + f.ms + LINGER_MS;
        out.push(Object.assign({}, f, { lit: f.press }));
      } else if (t < until && t + f.ms > until) {          // split: lit until the end of the linger time
        out.push(Object.assign({}, f, { ms: until - t, lit: last }), Object.assign({}, f, { ms: t + f.ms - until, lit: "" }));
      } else {
        out.push(Object.assign({}, f, { lit: t < until ? last : "" }));
      }
      t += f.ms;
    }
    return out;
  }

  const cache = new Map();                         // name + arg -> Promise of frames
  const starts = new Map();                        // name + arg -> start time (one clock for each animation)
  function framesOf(name, arg) {
    const key = name + "\u0000" + (arg || "");
    if (!cache.has(key)) {
      const make = anims[name];
      cache.set(key, Promise.resolve().then(() => (make ? make(arg) : [])).then(linger).catch((error) => {
        if (typeof console !== "undefined") console.warn("StatTrak animation " + name + ": " + error.message);
        cache.delete(key);
        return [];
      }));
    }
    if (!starts.has(key)) starts.set(key, performance.now());
    return { key, frames: cache.get(key) };
  }

  const reduceMotion = () => typeof matchMedia === "function" && matchMedia("(prefers-reduced-motion: reduce)").matches;

  // (Node.js runs this file in the tests: no HTMLElement there)
  const Base = typeof HTMLElement !== "undefined" ? HTMLElement : class {};
  class Element extends Base {
    connectedCallback() {
      if (!this.built) {
        this.built = true;
        this.setAttribute("role", "img");
        this.innerHTML =
          '<img class="st-dev__pic" src="assets/device.webp" alt="" draggable="false">' +
          '<canvas class="st-dev__screen"></canvas><span class="st-dev__glass"></span>' +
          '<span class="st-dev__btn st-dev__btn--1"></span><span class="st-dev__btn st-dev__btn--2"></span>' +
          '<span class="st-dev__lbl st-dev__lbl--1">1</span><span class="st-dev__lbl st-dev__lbl--2">2</span>';
        this.canvas = this.querySelector("canvas");
      }
      if (this.getAttribute("label")) this.setAttribute("aria-label", this.getAttribute("label"));
      this.index = -1;
      this.start();
    }

    disconnectedCallback() { this.stop(); }

    // A new anim or arg (for example a new "still" picture): show it
    static get observedAttributes() { return ["anim", "arg"]; }
    attributeChangedCallback() {
      if (this.built && this.isConnected) { this.index = -1; this.start(); }
    }

    start() {
      this.stop();
      const { key, frames } = framesOf(this.getAttribute("anim"), this.getAttribute("arg"));
      this.key = key;
      frames.then((list) => {
        if (!this.isConnected || this.key !== key || !list.length) return;
        this.list = list;
        this.total = list.reduce((sum, f) => sum + f.ms, 0);
        const tick = () => {
          this.show(this.frameAt(performance.now() - starts.get(key)));
          if (!reduceMotion()) this.raf = requestAnimationFrame(tick);
        };
        tick();
      });
    }

    stop() { if (this.raf) cancelAnimationFrame(this.raf); this.raf = 0; }

    frameAt(elapsed) {
      if (reduceMotion()) return this.list.length - 1;      // the last frame: where the steps lead
      let t = elapsed % this.total;
      for (let i = 0; i < this.list.length; i++) { t -= this.list[i].ms; if (t < 0) return i; }
      return this.list.length - 1;
    }

    show(i) {
      const ratio = window.devicePixelRatio || 1;
      const width = Math.max(128, Math.round(this.canvas.clientWidth * ratio));
      const height = Math.max(32, Math.round(this.canvas.clientHeight * ratio));
      if (i === this.index && width === this.canvas.width && height === this.canvas.height) return;
      this.index = i;
      if (this.canvas.width !== width || this.canvas.height !== height) { this.canvas.width = width; this.canvas.height = height; }
      const frame = this.list[i];
      draw(this.canvas, frame.b);
      this.dataset.press = frame.lit;
      this.dataset.frame = String(i);
    }
  }

  /* The lit pixels as plain squares with no gaps: on the real display the gaps are almost
     invisible, and drawn gaps make a moire pattern at some sizes. Each edge is rounded to a whole
     canvas pixel, so the pixels meet and stay sharp at any size. */
  function draw(canvas, b) {
    const ctx = canvas.getContext("2d");
    const px = canvas.width / b.w, py = canvas.height / b.h;
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.fillStyle = LIT;
    for (let y = 0; y < b.h; y++) {
      const y0 = Math.round(y * py), y1 = Math.round((y + 1) * py);
      for (let x = 0; x < b.w; x++) {
        if (!b.d[y * b.w + x]) continue;
        let end = x;                                   // one rectangle for a row of lit pixels
        while (end + 1 < b.w && b.d[y * b.w + end + 1]) end++;
        const x0 = Math.round(x * px), x1 = Math.round((end + 1) * px);
        ctx.fillRect(x0, y0, x1 - x0, y1 - y0);
        x = end;
      }
    }
  }

  if (typeof customElements !== "undefined" && !customElements.get("st-device")) customElements.define("st-device", Element);

  /* A picture for anim="still": 128 x 32 pixels (0 / 1). Returns its id (the arg). `old` = an
     id that is not needed any more. */
  function still(pixels, old) {
    if (old) { stills.delete(old); cache.delete("still\u0000" + old); starts.delete("still\u0000" + old); }
    const id = "p" + (++stillCount);
    stills.set(id, Uint8Array.from(pixels));
    return id;
  }

  return { SCREEN, anims, fromRows, framesOf, draw, still };
})();
