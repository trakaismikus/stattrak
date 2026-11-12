/* StatTrak apps: the pictures of the Apps tab, drawn as the device draws them.
 *
 * - The title picture of an app: 80 x 24 pixels in the Gfx_drawBitmapRaw format (3 pages of
 *   80 bytes, bit 0 = the top pixel of the page). Installed apps have it in their package
 *   (catalog.json has it as base64). The built-in apps have no package: their pictures are
 *   here, made with the pixel art of the firmware menu.
 * - The display of the device (128 x 32) in "Apps: SYNC", as main/views/app_sync_view.c
 *   draws it.
 * The fonts are the firmware fonts (only the characters of app names and of these screens).
 */

const AppPictures = (() => {
  // Built-in title pictures (from the pixel art of the firmware menu)
  const BUILTIN = {
    kills: "AAAAAAAAAAAAAAAAAAAAAIBAQAD4AEBAgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAABAQENYREBAAAJMAABAQEdYQEBAAAAAAAP7+EBD+7gCCgv7+goIA/v6AgICAAP7+gICAgABc3pKS9mQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAECBAQAPwAEBAIBAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    killdead: "AAAAAAAAAAAAAAAAAAAAAAAAgADgAIAAAAAAAAAAAAAAAAAAAAAAAKgAAAAAAAAAAAAAAAAAAICAwMDAwMDAwICAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEBBU1hERANcAERHWVBAQAAAAAAAAAAAAAAAAqgAAAAAAAAAAAAAAABw/f+Pj4/8//+Pj438/HAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAQMADwADAQAAAAAAAAAAAAAAAAAAAAAKAAAAAAAAAAAAAAAAAAAAAAcFBwUHBQcAAAAAAAAAAAAAAAAAAAAA",
    stats: "AAAAAAAAAAAAAAAAwAAAAAAAAAAAAAAAAICAgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD/AAAA4ODgAAD+/v4A////AAAAAAAAXN6SkvZkAAIC/v4CAgD8/hIS/vwAAgL+/gICAFzekpL2ZAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAcEBAQHBwcEBAcHBwQHBwcEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
    settings: "AAAAAAAAAAAAQMBAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAOBC7//7Hx8f+/7sQOAAAAAAAXN6SkvZkAP7+kpKCggACAv7+AgIAAgL+/gICAIKC/v6CggD+/gwY/v4AfP6CkvZ0AFzekpL2ZAAAAAAAAAAAAAEBAAUHBQABAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
  };

  // Firmware fonts: [width, row bits (the left pixel is the high bit)]
  const FONTS = {
    "5x5": { h: 5, sp: 1, c: {
      "0": [5,31,17,21,17,31], "1": [5,28,4,4,4,31], "2": [5,31,1,31,16,31], "3": [5,31,1,7,1,31], "4": [5,17,17,31,1,1], "5": [5,31,16,31,1,31],
      "6": [5,31,16,31,17,31], "7": [5,31,1,1,1,1], "8": [5,31,17,31,17,31], "9": [5,31,17,31,1,31], " ": [5,0,0,0,0,0], "!": [5,16,16,16,0,16],
      "+": [5,0,4,14,4,0], "-": [5,0,0,14,0,0], "%": [5,17,2,4,8,17], "/": [5,1,2,4,8,16], ".": [5,0,0,0,0,4], ":": [5,0,16,0,16,0],
      "?": [5,31,17,7,0,4], "A": [5,31,17,31,17,17], "B": [5,30,17,30,17,30], "C": [5,31,16,16,16,31], "D": [5,30,17,17,17,30], "E": [5,31,16,30,16,31],
      "F": [5,31,16,30,16,16], "G": [5,31,16,19,17,31], "H": [5,17,17,31,17,17], "I": [5,31,4,4,4,31], "J": [5,1,1,1,17,31], "K": [5,19,20,24,20,19],
      "L": [5,16,16,16,16,31], "M": [5,17,27,21,17,17], "N": [5,17,25,21,19,17], "O": [5,31,17,17,17,31], "P": [5,31,17,31,16,16], "Q": [5,31,17,17,31,2],
      "R": [5,31,17,31,20,19], "S": [5,31,16,31,1,31], "T": [5,31,4,4,4,4], "U": [5,17,17,17,17,31], "V": [5,17,17,17,10,4], "W": [5,17,17,17,21,10],
      "X": [5,17,10,4,10,17], "Y": [5,17,10,4,8,16], "Z": [5,31,2,4,8,31],
    } },
    "3x5": { h: 5, sp: 1, c: {
      "0": [3,7,5,5,5,7], "1": [3,6,2,2,2,7], "2": [3,7,1,7,4,7], "3": [3,7,1,3,1,7], "4": [3,5,5,7,1,1], "5": [3,7,4,7,1,7],
      "6": [3,7,4,7,5,7], "7": [3,7,1,1,1,1], "8": [3,7,5,7,5,7], "9": [3,7,5,7,1,7], " ": [3,0,0,0,0,0], "!": [3,4,4,4,0,4],
      "+": [3,0,2,7,2,0], "-": [3,0,0,7,0,0], "%": [3,5,1,2,4,5], "/": [3,2,0,7,0,2], ".": [3,0,0,0,0,2], ":": [3,0,2,0,2,0],
      "?": [3,7,0,3,0,2], "A": [3,7,5,7,5,5], "B": [3,7,5,7,5,7], "C": [3,7,4,4,4,7], "D": [3,6,5,5,5,6], "E": [3,7,4,6,4,7],
      "F": [3,7,4,6,4,4], "G": [3,7,4,7,5,7], "H": [3,5,5,7,5,5], "I": [3,7,2,2,2,7], "J": [3,7,1,1,5,7], "K": [3,5,5,6,5,5],
      "L": [3,4,4,4,4,7], "M": [3,5,7,7,5,5], "N": [3,5,7,7,7,5], "O": [3,7,5,5,5,7], "P": [3,7,5,7,4,4], "Q": [3,7,5,7,7,3],
      "R": [3,7,5,6,5,5], "S": [3,7,4,7,1,7], "T": [3,7,2,2,2,2], "U": [3,5,5,5,5,7], "V": [3,5,5,3,3,1], "W": [3,5,5,7,7,5],
      "X": [3,5,5,2,5,5], "Y": [3,5,5,7,2,2], "Z": [3,7,1,2,4,7],
    } },
    "big": { h: 7, sp: 1, c: {
      "0": [6,30,51,59,63,55,51,30], "1": [6,28,60,12,12,12,12,63], "2": [6,30,51,3,30,48,48,63], "3": [6,30,51,3,6,3,51,30], "4": [6,51,51,51,63,3,3,3], "5": [6,63,48,48,62,3,3,62],
      "6": [6,30,51,48,62,51,51,30], "7": [6,63,3,3,15,3,3,3], "8": [6,30,51,51,30,51,51,30], "9": [6,30,51,51,31,3,51,30], " ": [6,0,0,0,0,0,0,0], "!": [2,3,3,3,3,3,0,3],
      "+": [3,0,0,2,7,2,0,0], "-": [3,0,0,0,7,0,0,0], "%": [5,0,17,2,4,8,17,0], "/": [5,3,7,6,14,12,28,24], ".": [1,0,0,0,0,0,0,1], ":": [1,0,0,1,0,1,0,0],
      "?": [6,30,51,3,14,12,0,12], "A": [6,30,51,51,63,51,51,51], "B": [6,62,51,51,62,51,51,62], "C": [6,30,51,48,48,48,51,30], "D": [6,62,51,51,51,51,51,62], "E": [6,63,48,48,60,48,48,63],
      "F": [6,63,48,48,60,48,48,48], "G": [6,30,51,48,55,51,51,30], "H": [6,51,51,51,63,51,51,51], "I": [6,63,12,12,12,12,12,63], "J": [6,3,3,3,3,3,51,30], "K": [6,51,51,51,62,51,51,51],
      "L": [6,48,48,48,48,48,48,63], "M": [7,99,119,127,107,99,99,99], "N": [6,51,59,63,55,51,51,51], "O": [6,30,51,51,51,51,51,30], "P": [6,62,51,51,62,48,48,48], "Q": [6,30,51,51,51,55,54,31],
      "R": [6,62,51,51,62,51,51,51], "S": [6,30,51,48,62,3,51,30], "T": [6,63,12,12,12,12,12,12], "U": [6,51,51,51,51,51,51,30], "V": [7,99,99,99,99,119,62,28], "W": [7,99,99,99,107,127,119,99],
      "X": [7,99,119,62,28,62,119,99], "Y": [6,51,51,51,30,12,12,12], "Z": [6,63,7,14,28,56,48,63],
    } },
  };

  const W = 80, H = 24;

  function buffer(w, h) { return { w, h, d: new Uint8Array(w * h) }; }
  function px(b, x, y, c) { if (x >= 0 && x < b.w && y >= 0 && y < b.h) b.d[y * b.w + x] = c === 2 ? 1 - b.d[y * b.w + x] : c; }
  function hline(b, x, y, w, c) { for (let i = 0; i < w; i++) px(b, x + i, y, c); }
  function vline(b, x, y, h, c) { for (let j = 0; j < h; j++) px(b, x, y + j, c); }
  function invert(b, x, y, w, h) { for (let j = y; j < y + h; j++) for (let i = x; i < x + w; i++) px(b, i, j, 2); }

  function glyph(font, ch) {
    const f = FONTS[font];
    return f.c[ch] || f.c[ch.toUpperCase()] || f.c[" "];
  }
  function textWidth(s, font) {
    let w = 0;
    for (const ch of s) w += glyph(font, ch)[0] + FONTS[font].sp;
    return w ? w - FONTS[font].sp : 0;
  }
  function text(b, x, y, s, font, c) {
    for (const ch of s) {
      const g = glyph(font, ch), w = g[0];
      for (let j = 1; j < g.length; j++) for (let i = 0; i < w; i++) if (g[j] & (1 << (w - 1 - i))) px(b, x + i, y + j - 1, c);
      x += w + FONTS[font].sp;
    }
  }
  // The picture of the compiler for an app without a "picture" line (tools/app.py)
  function textCenter(b, y, s, font) { text(b, Math.floor((b.w - textWidth(s, font)) / 2), y, s, font, 1); }

  /* Ports of the firmware drawing (components/gfx/gfx.c, main/app_view.h). The fonts 5x5 and
     3x5 have one width for all characters. */
  // Gfx_drawString(): with invert, the box from 1 pixel before the text to 1 pixel after it
  function fwString(b, x, y, s, font, inverted) {
    text(b, x, y, s, font, 1);
    if (inverted && s.length) invert(b, x - 1, y - 1, textWidth(s, font) + 2, FONTS[font].h + 2);
  }
  // Gfx_drawStringAligned(GFX_ALIGN__CENTER): x - width / 2 (C integer division)
  function fwCenter(b, x, y, s, font) { text(b, x - Math.floor(textWidth(s, font) / 2), y, s, font, 1); }
  // GUI_DRAW_TITLE(x, y, len, str): the text inverted, a line on each side
  function fwTitle(b, x, y, len, s) {
    fwString(b, x, y, s, "5x5", true);
    vline(b, x - 2, y, 5, 1);
    vline(b, x + len * 6, y, 5, 1);
  }
  // GUI_DRAW_BTN(x, y, w, h, str, select): a frame without its corner pixels, the text in the middle
  function fwButton(b, x, y, w, h, s, select) {
    vline(b, x, y + 1, h - 2, 1); vline(b, x + w - 1, y + 1, h - 2, 1);
    hline(b, x + 1, y, w - 2, 1); hline(b, x + 1, y + h - 1, w - 2, 1);
    fwCenter(b, x + Math.floor(w / 2), y + Math.floor((h - 5) / 2), s, "5x5");
    if (select) invert(b, x + 1, y + 1, w - 2, h - 2);
  }
  // app_sync_view.c _drawButton(): 9 pixels high at y 21, text in font3x5. fill 0..100 inverts
  // the button from the left (the hold animation).
  function fwSmallButton(b, x, w, s, fill) {
    const y = 21, h = 9;
    vline(b, x, y + 1, h - 2, 1); vline(b, x + w - 1, y + 1, h - 2, 1);
    hline(b, x + 1, y, w - 2, 1); hline(b, x + 1, y + h - 1, w - 2, 1);
    fwCenter(b, x + Math.floor(w / 2), y + 2, s, "3x5");
    const width = Math.floor(((w - 2) * Math.max(0, Math.min(100, fill || 0)) + 50) / 100);
    if (width > 0) invert(b, x + 1, y + 1, width, h - 2);
  }
  // Gfx_drawCircleHelper()
  function fwCorner(b, x0, y0, r, corner) {
    let f = 1 - r, ddx = 1, ddy = -2 * r, x = 0, y = r;
    while (x < y) {
      if (f >= 0) { y--; ddy += 2; f += ddy; }
      x++; ddx += 2; f += ddx;
      if (corner & 4) { px(b, x0 + x, y0 + y, 1); px(b, x0 + y, y0 + x, 1); }
      if (corner & 2) { px(b, x0 + x, y0 - y, 1); px(b, x0 + y, y0 - x, 1); }
      if (corner & 8) { px(b, x0 - y, y0 + x, 1); px(b, x0 - x, y0 + y, 1); }
      if (corner & 1) { px(b, x0 - y, y0 - x, 1); px(b, x0 - x, y0 - y, 1); }
    }
  }
  // Gfx_drawRoundRect()
  function fwRoundRect(b, x, y, w, h, r) {
    hline(b, x + r, y, w - 2 * r, 1); hline(b, x + r, y + h - 1, w - 2 * r, 1);
    vline(b, x, y + r, h - 2 * r, 1); vline(b, x + w - 1, y + r, h - 2 * r, 1);
    fwCorner(b, x + r, y + r, r, 1); fwCorner(b, x + w - r - 1, y + r, r, 2);
    fwCorner(b, x + w - r - 1, y + h - r - 1, r, 4); fwCorner(b, x + r, y + h - r - 1, r, 8);
  }

  /* bytes (240) or base64 -> an 80 x 24 buffer */
  function fromBytes(bytes) {
    if (typeof bytes === "string") bytes = Uint8Array.from(atob(bytes), (c) => c.charCodeAt(0));
    const b = buffer(W, H);
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) if (bytes[(y >> 3) * W + x] & (1 << (y & 7))) b.d[y * W + x] = 1;
    return b;
  }

  /* An app without a known picture: its name in the middle, in the title font if it fits
     (the picture that the compiler makes for an app without a "picture" line) */
  function fromName(name) {
    const b = buffer(W, H);
    const font = textWidth(name, "big") <= W ? "big" : "5x5";
    textCenter(b, Math.floor((H - FONTS[font].h + 1) / 2), name, font);
    return b;
  }

  /* The display in "Apps: SYNC", as _draw() of main/views/app_sync_view.c.
     kind and arg:
       idle           arg = dots after "Waiting" (0..3, default 3)
       notinstalled   the last install stopped or failed after "yes"
       question       arg = app name; fill = { no, yes }: 0..100 of a hold of button 1 / 2
       receiving      arg = percent
       installed      arg = app name (SYNC MORE and OPEN)
       refused        "no", or no answer in 30 s */
  function syncScreen(kind, arg, fill) {
    const b = buffer(128, 32);
    if (kind === "installed") {
      fwTitle(b, (128 - 13 * 6) / 2, 1, 13, "APP INSTALLED");
      fwCenter(b, 64, 11, String(arg), "5x5");
      // Each button over its round button: button 1 is the left one, button 2 the right one
      fwSmallButton(b, 4, 40, "SYNC MORE", 0);
      fwSmallButton(b, 128 - 4 - 22, 22, "OPEN", 0);
      return b;
    }
    fwTitle(b, (128 - 8 * 6) / 2, 1, 8, "APP SYNC");
    if (kind === "receiving") {
      const pct = Math.max(0, Math.min(100, Math.floor(arg)));
      fwCenter(b, 64, 12, "Receiving", "5x5");
      fwCenter(b, 64, 23, pct + "%", "5x5");
      invert(b, 14, 22, pct, 7);
      fwRoundRect(b, 13, 21, 128 - 26, 9, 3);
    } else if (kind === "question") {
      const question = "INSTALL " + arg + "?";
      fwCenter(b, 64, 11, question, textWidth(question, "5x5") > 128 - 4 ? "3x5" : "5x5");
      // NO over button 1 (left), YES over button 2 (right). They fill while they are held.
      fwSmallButton(b, 4, 22, "NO", fill && fill.no);
      fwSmallButton(b, 128 - 4 - 22, 22, "YES", fill && fill.yes);
    } else if (kind === "refused") {
      fwCenter(b, 64, 12, "REFUSED", "5x5");
      fwButton(b, 12, 20, 42, 11, "BACK", false);
    } else {
      if (kind === "notinstalled") fwCenter(b, 64, 12, "NOT INSTALLED", "5x5");
      else text(b, (128 - 7 * 6) / 2, 12, "Waiting" + ".".repeat(arg === undefined ? 3 : arg & 3), "5x5", 1);
      fwButton(b, 12, 20, 42, 11, "BACK", false);
    }
    return b;
  }

  /* The OLED look: a dot for each pixel, unlit dots faint. Returns a data URL (PNG). */
  const cache = new Map();
  function paint(b, key) {
    if (key && cache.has(key)) return cache.get(key);
    const pitch = 4, dot = pitch - 1;
    const canvas = document.createElement("canvas");
    canvas.width = b.w * pitch;
    canvas.height = b.h * pitch;
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#050505";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    for (const [color, on] of [["rgba(255,255,255,0.05)", 0], ["#f6f7ff", 1]]) {
      ctx.fillStyle = color;
      for (let y = 0; y < b.h; y++) for (let x = 0; x < b.w; x++) if (b.d[y * b.w + x] === on) ctx.fillRect(x * pitch, y * pitch, dot, dot);
    }
    const url = canvas.toDataURL("image/png");
    if (key) cache.set(key, url);
    return url;
  }

  // Ids of the built-in apps (main/apps.h)
  const BUILTIN_IDS = { 1: "kills", 2: "killdead", 3: "stats" };

  return {
    /* The title picture of an app: picture = 240 bytes, base64, or null (then the name) */
    app(id, name, picture) {
      const builtin = BUILTIN_IDS[id];
      if (builtin) return paint(fromBytes(BUILTIN[builtin]), "b:" + builtin);
      if (picture) return paint(fromBytes(picture), "p:" + id + ":" + (typeof picture === "string" ? picture : Array.from(picture).join(",")));
      return paint(fromName(name), "n:" + name);
    },
    settings() { return paint(fromBytes(BUILTIN.settings), "b:settings"); },
    screen(kind, arg, fill) { return paint(syncScreen(kind, arg, fill), "s:" + kind + ":" + arg + ":" + (fill ? fill.no + "/" + fill.yes : "")); },
    // For the tests: the pixels as rows of '#' and '.'
    rows(b) {
      const out = [];
      for (let y = 0; y < b.h; y++) { let s = ""; for (let x = 0; x < b.w; x++) s += b.d[y * b.w + x] ? "#" : "."; out.push(s); }
      return out;
    },
    fromBytes, fromName, syncScreen, textWidth,
  };
})();
