/* Tests of the device picture with its live display (js/device.js) in Node.js.
   Run: node --test "tests/*.test.js" */

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { ROOT } = require("./helpers.js");

function load() {
  const context = vm.createContext({ console: { log() {}, warn() {} }, performance });
  context.window = context;
  for (const file of ["js/apps-pictures.js", "assets/update-path-1.0.1.js", "js/device.js"]) {
    vm.runInContext(fs.readFileSync(path.join(ROOT, file), "utf8"), context, { filename: file });
  }
  return { StDevice: vm.runInContext("StDevice", context), AppPictures: vm.runInContext("AppPictures", context), context };
}

const lit = (b) => b.d.reduce((sum, v) => sum + v, 0);
const text = (b) => Array.from({ length: b.h }, (_, y) => Array.from(b.d.slice(y * b.w, (y + 1) * b.w)).join("")).join("\n");

test("the 1.0.1 update path: well formed frames, from the counter to the update mode", async () => {
  const { StDevice, context } = load();
  const data = context.STATTRAK_UPDATE_PATH;
  assert.equal(data.firmware, "1.0.1");
  for (const f of data.frames) {
    assert.equal(f.rows.length, 32);
    for (const row of f.rows) assert.match(row, /^[0-9a-f]{32}$/);
    assert.ok(Number.isInteger(f.ms) && f.ms > 0);
    assert.ok(["-", "1", "2", "H2", "B"].includes(f.press), f.press);
  }
  const total = data.frames.reduce((sum, f) => sum + f.ms, 0);
  assert.ok(total > 10000 && total < 30000, "length " + total);
  // The steps of the update path: both buttons (to Settings), button 2 (down), hold button 2 (update mode)
  const presses = data.frames.map((f) => f.press).filter((p, i, all) => p !== "-" && p !== all[i - 1]);
  assert.deepEqual([...new Set(presses)], ["B", "2", "H2"]);
  const frames = await StDevice.framesOf("update-1.0.1").frames;
  // The same pixels and times as the file (a frame may be split in two for the button light)
  assert.equal(frames.reduce((sum, f) => sum + f.ms, 0), total);
  // A short press (0.1 s on the device) keeps its button lit for 0.4 s more, so a person sees it
  const litMs = frames.filter((f) => f.lit === "B").reduce((sum, f) => sum + f.ms, 0);
  const pressMs = data.frames.filter((f) => f.press === "B").reduce((sum, f) => sum + f.ms, 0);
  assert.equal(litMs, pressMs + 3 * 400, "3 presses of both buttons");
  assert.ok(frames.every((f) => lit(f.b) > 0), "a frame with no pixel");
  // The last frames: the update mode of 1.0.1 ("FIRMWARE UPDATE" title and the CANCEL button)
  const last = frames[frames.length - 1].b;
  assert.ok(lit(last) > 200);
  assert.equal(frames[frames.length - 1].lit, "");
});

test("the Apps screens of the device picture are the screens of AppPictures (tested against the firmware)", async () => {
  const { StDevice, AppPictures } = load();
  const idle = await StDevice.framesOf("sync-idle").frames;
  assert.deepEqual([...idle].map((f) => text(f.b)), [0, 1, 2, 3].map((dots) => text(AppPictures.syncScreen("idle", dots))));
  assert.ok(idle.every((f) => f.ms === 512 && f.press === ""), "the dots of the firmware change each 512 ms");

  const question = await StDevice.framesOf("sync-question", "REACTION").frames;
  assert.equal(text(question[0].b), text(AppPictures.syncScreen("question", "REACTION")));
  assert.equal(question[0].press, "");
  const held = question.slice(1);
  assert.ok(held.length > 5 && held.every((f) => f.press === "H2"), "button 2 is held while YES fills");
  assert.equal(text(held[held.length - 1].b), text(AppPictures.syncScreen("question", "REACTION", { no: 0, yes: 100 })));

  const installed = await StDevice.framesOf("sync-installed", "REACTION").frames;
  assert.equal(text(installed[0].b), text(AppPictures.syncScreen("installed", "REACTION")));
});

test("an unknown animation shows nothing and does not stop the page", async () => {
  const { StDevice } = load();
  assert.equal((await StDevice.framesOf("no-such-animation").frames).length, 0);
});

/* The display is at the same place in the picture for the page and the web simulator */
test("the display place is the same in js/device.js, css/main.css and the web simulator", () => {
  const { StDevice } = load();
  const s = StDevice.SCREEN;
  const css = fs.readFileSync(path.join(ROOT, "css", "main.css"), "utf8");
  const rule = css.match(/\.st-dev__screen\s*\{([^}]*)\}/)[1];
  for (const side of ["left", "top", "width", "height"]) {
    assert.match(rule, new RegExp("\\b" + side + ":\\s*" + String(s[side]).replace(".", "\\.") + "%"), "css " + side);
  }
  const shell = path.join(ROOT, "..", "sim", "web", "shell.html.in");
  if (!fs.existsSync(shell)) return;
  const sim = fs.readFileSync(shell, "utf8").match(/#screen-container canvas\s*\{([^}]*)\}/)[1];
  for (const side of ["left", "top", "width", "height"]) {
    assert.match(sim, new RegExp("\\b" + side + ":\\s*" + String(s[side]).replace(".", "\\.") + "%"), "simulator " + side);
  }
});
