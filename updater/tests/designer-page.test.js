/* The Screen Designer (designer/index.html) in a real headless Chrome: the start project, the
 * device picture, and the tools that need the mouse (Circle, Select, black parts, Trace, a screen
 * from an AI tool). The pixels themselves are checked by tests/test_designer.py (firmware CI).
 * Run: node --test "tests/*.test.js"
 */

const test = require("node:test");
const assert = require("node:assert/strict");
const { Browser, serve, findChrome } = require("./browser.js");
const { ROOT } = require("./helpers.js");

const SKIP = findChrome() ? false : "no Chrome, Chromium or Edge on this computer";

let browser = null;
let site = null;

test.before(async () => {
  if (SKIP) return;
  site = await serve(ROOT);
  browser = await Browser.launch();
});
test.after(async () => {
  if (browser) await browser.close();
  if (site) await site.close();
});

/* A new page with an empty browser storage, and helpers for the mouse on the pixel canvas */
async function openDesigner(t) {
  const page = await browser.newPage();
  t.after(() => page.close());
  await page.send("Emulation.setDeviceMetricsOverride", { width: 1440, height: 900, deviceScaleFactor: 1, mobile: false });
  await page.setInitScript("try { if (!sessionStorage.getItem('cleared')) { localStorage.clear(); sessionStorage.setItem('cleared', '1'); } } catch (e) {} window.__errors = []; addEventListener('error', e => window.__errors.push(e.message));");
  await page.goto(site.url + "/designer/index.html");
  await page.waitFor("window.StatTrakDesigner && document.querySelector('#cvOv').getBoundingClientRect().width > 0");
  const box = await page.evaluate("JSON.parse(JSON.stringify(document.querySelector('#cvOv').getBoundingClientRect()))");
  const zoom = box.width / 128;
  const at = (x, y) => ({ x: box.left + (x + 0.5) * zoom, y: box.top + (y + 0.5) * zoom });
  const mouse = (type, p) => page.send("Input.dispatchMouseEvent", { type, x: p.x, y: p.y, button: "left", buttons: type === "mouseReleased" ? 0 : 1, clickCount: 1 });
  const drag = async (a, b) => {
    await mouse("mousePressed", at(a.x, a.y));
    for (let k = 1; k <= 5; k++) await mouse("mouseMoved", at(a.x + ((b.x - a.x) * k) / 5, a.y + ((b.y - a.y) * k) / 5));
    await mouse("mouseReleased", at(b.x, b.y));
  };
  const click = (selector) => page.evaluate(`document.querySelector(${JSON.stringify(selector)}).click()`);
  const elements = () => page.evaluate("JSON.parse(JSON.stringify(StatTrakDesigner.project.screens[StatTrakDesigner.current].elements))");
  const newScreen = async () => { await click("#addScreen"); await click("[data-new=empty]"); };
  return { page, drag, click, elements, newScreen };
}

test("the designer opens with the 4 StatTrak screens and shows the screen in the device picture", { skip: SKIP }, async (t) => {
  const { page } = await openDesigner(t);
  const names = await page.evaluate("StatTrakDesigner.project.screens.map(s => s.name)");
  assert.deepEqual(names, ["Big counter", "Split counter", "Stats", "Settings"]);
  const lit = await page.evaluate(`(() => { const c = document.querySelector('#preview canvas');
    const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0;
    for (let i = 3; i < d.length; i += 4) if (d[i]) n++; return n; })()`);
  assert.ok(lit > 100, "the device picture shows the screen");
  assert.equal(await page.evaluate("document.querySelector('#preview img').getAttribute('src')"), "../assets/device.webp");
  assert.deepEqual(await page.evaluate("[...document.querySelectorAll('.tool__label')].map(x => x.textContent)"),
    ["Move", "Select", "Text", "Box", "Circle", "Line", "Draw", "Erase", "Trace"]);
  assert.deepEqual(await page.evaluate("window.__errors"), []);
});

test("Circle draws from the center, a black part is red while selected, the border is a setting", { skip: SKIP }, async (t) => {
  const { page, drag, click, elements, newScreen } = await openDesigner(t);
  await newScreen();
  await click("[data-tool=circle]");
  await drag({ x: 40, y: 15 }, { x: 48, y: 15 });
  let els = await elements();
  assert.equal(els.length, 1);
  assert.deepEqual([els[0].type, els[0].x, els[0].y, els[0].r], ["circle", 40, 15, 8]);
  await click("[data-act=color][data-v='0']");
  await click("[data-act=border][data-v='3']");
  els = await elements();
  assert.deepEqual([els[0].color, els[0].border], [0, 3]);
  // red on the overlay while it is selected, nothing red when it is not
  const red = `(() => { const c = document.querySelector('#cvOv'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    let n = 0; for (let i = 0; i < d.length; i += 4) if (d[i] > 200 && d[i + 1] < 90 && d[i + 3] > 200) n++; return n; })()`;
  assert.ok((await page.evaluate(red)) > 0, "a selected black part is red");
  await click("[data-act=deselect]");
  assert.equal(await page.evaluate(red), 0, "not red when it is not selected");
});

test("round corners are a number, and Round ends makes a pill", { skip: SKIP }, async (t) => {
  const { drag, click, elements, newScreen } = await openDesigner(t);
  await newScreen();
  await click("[data-tool=rect]");
  await drag({ x: 10, y: 5 }, { x: 70, y: 25 });
  await click("[data-act=pill]");
  const [box] = await elements();
  assert.equal(box.radius, 10);       // (21 - 1) / 2
});

test("Select takes part of a drawing and moves it as a group; undo puts it back", { skip: SKIP }, async (t) => {
  const { page, drag, click, elements, newScreen } = await openDesigner(t);
  await newScreen();
  await click("[data-tool=pencil]");
  await drag({ x: 10, y: 10 }, { x: 40, y: 10 });
  assert.equal((await elements()).length, 1);
  await click("[data-tool=marquee]");
  await drag({ x: 30, y: 8 }, { x: 50, y: 12 });
  assert.deepEqual(await page.evaluate("StatTrakDesigner.group.parts"), 1);
  await drag({ x: 35, y: 10 }, { x: 35, y: 20 });
  const els = await elements();
  assert.equal(els.length, 2, "the selected pixels are a drawing of their own");
  assert.equal(els[0].rows[0].length, 20);                 // x 10..29 stay
  assert.deepEqual([els[1].x, els[1].y, els[1].rows[0].length], [30, 20, 11]);   // x 30..40 moved down
  await page.evaluate("StatTrakDesigner.undo()");
  const back = await elements();
  assert.equal(back.length, 1);
  assert.deepEqual([back[0].x, back[0].y, back[0].rows[0].length], [10, 10, 31]);
});

test("a selection of more parts: border and color for all; a new part ends the selection", { skip: SKIP }, async (t) => {
  const { page, drag, click, elements, newScreen } = await openDesigner(t);
  await newScreen();
  for (const x of [20, 60]) {
    await click("[data-tool=circle]");
    await drag({ x, y: 15 }, { x: x + 5, y: 15 });
  }
  await click("[data-tool=marquee]");
  await drag({ x: 5, y: 2 }, { x: 80, y: 29 });
  assert.deepEqual((await page.evaluate("StatTrakDesigner.group")).els, [0, 1]);
  await click("[data-act=gborder][data-v='2']");
  await click("[data-act=gcolor][data-v='0']");
  assert.deepEqual((await elements()).map((e) => [e.border, e.color]), [[2, 0], [2, 0]]);
  await click("[data-tool=text]");
  await drag({ x: 100, y: 5 }, { x: 100, y: 5 });
  assert.equal(await page.evaluate("StatTrakDesigner.group"), null);
  assert.equal((await elements()).at(-1).type, "text");
  assert.equal(await page.evaluate("document.querySelector('.sec__title').textContent"), "Text");
});

test("New screen is a short menu; the AI help is a window of its own that fits a small screen", { skip: SKIP }, async (t) => {
  const { page, click } = await openDesigner(t);
  await page.send("Emulation.setDeviceMetricsOverride", { width: 1100, height: 640, deviceScaleFactor: 1, mobile: false });
  await click("#addScreen");
  const menu = await page.evaluate("JSON.parse(JSON.stringify(document.querySelector('#newPop').getBoundingClientRect()))");
  assert.ok(menu.top >= 0 && menu.bottom <= 640 && menu.height < 300, "a short menu");
  assert.equal(await page.evaluate("!!document.querySelector('#newPop #aiPaste')"), false, "no AI form in the menu");
  await click("[data-new=ai]");
  assert.equal(await page.evaluate("document.querySelector('#newPop').hidden && !document.querySelector('#aiModal').hidden"), true);
  await page.evaluate("document.querySelector('#aiPasteAdd').scrollIntoView()");
  const add = await page.evaluate("JSON.parse(JSON.stringify(document.querySelector('#aiPasteAdd').getBoundingClientRect()))");
  assert.ok(add.bottom <= 640 && add.top >= 0, "the Add button is in the window");
});

test("Trace keeps a picture under the screen, only in the browser", { skip: SKIP }, async (t) => {
  const { page, click } = await openDesigner(t);
  await page.evaluate(`(() => { const c = document.createElement('canvas'); c.width = 40; c.height = 20;
    const id = StatTrakDesigner.project.screens[0].id, t = {};
    t[id] = { src: c.toDataURL('image/png'), ratio: 0.5, w: 64, x: 32, y: 0, opacity: 0.5 };
    localStorage.setItem('stattrak-designer-trace-v1', JSON.stringify(t)); })()`);
  await page.goto(site.url + "/designer/index.html");
  await page.waitFor("window.StatTrakDesigner && !document.querySelector('#traceImg').hidden");
  await click("[data-tool=trace]");
  assert.equal(await page.evaluate("document.querySelector('.sec__title').textContent"), "Trace picture");
  // the size in %: 100 % = as high as the screen (this picture: 64 x 32 display pixels)
  assert.equal(await page.evaluate("document.querySelector('[data-tr=pct]').value"), "100");
  // a corner handle: drag the lower right corner 16 display pixels to the right -> 125 %
  const box = await page.evaluate("JSON.parse(JSON.stringify(document.querySelector('#cvOv').getBoundingClientRect()))");
  const zoom = box.width / 128;
  const at = (x, y) => ({ x: box.left + x * zoom, y: box.top + y * zoom });
  const mouse = (type, p) => page.send("Input.dispatchMouseEvent", { type, x: p.x, y: p.y, button: "left", buttons: type === "mouseReleased" ? 0 : 1, clickCount: 1 });
  await mouse("mousePressed", at(95.8, 31.8));
  await mouse("mouseMoved", at(104, 34));
  await mouse("mouseMoved", at(112, 36));
  await mouse("mouseReleased", at(112, 36));
  assert.equal(await page.evaluate("document.querySelector('[data-tr=pct]').value"), "125");
  await click("[data-act=tr-fith]");
  assert.equal(await page.evaluate("document.querySelector('[data-tr=pct]').value"), "100");
  // a new size in %: X and Y follow (the picture grows around its middle)
  await page.evaluate("const i = document.querySelector('[data-tr=pct]'); i.value = '300'; i.dispatchEvent(new Event('input', { bubbles: true }))");
  assert.equal(await page.evaluate("document.querySelector('[data-tr=x]').value"), "-32");   // 64 x 32 at x 32 -> 192 x 96 at x -32
  assert.equal(await page.evaluate("document.querySelector('[data-tr=y]').value"), "-32");
  // a corner outside the screen: its handle is at the edge, and a small drag does not jump the size
  await mouse("mousePressed", at(127.8, 31.8));
  await mouse("mouseMoved", at(127.9, 31.8));
  await mouse("mouseReleased", at(127.9, 31.8));
  assert.equal(await page.evaluate("document.querySelector('[data-tr=pct]').value"), "300");
  const text = await page.evaluate("StatTrakDesign.buildPrompt(StatTrakDesigner.project, window.STATTRAK_ASSETS, {})");
  assert.ok(!text.includes("data:image"), "the trace picture is not in the text for the AI");
});

test("New screen takes the answer of an AI tool", { skip: SKIP }, async (t) => {
  const { page, click } = await openDesigner(t);
  const answer = "Here it is:\n```json\n" + JSON.stringify({ format: "stattrak-design", version: 1, name: "X",
    screens: [{ id: "killFeed", name: "Kill feed", note: "", elements: [{ type: "circle", x: 64, y: 16, r: 9, fill: true }] }] }) + "\n```";
  await click("#addScreen");
  await click("[data-new=ai]");
  assert.match(await page.evaluate("document.querySelector('#aiAsk').textContent"), /stattrak-design/);
  await page.evaluate(`document.querySelector('#aiPaste').value = ${JSON.stringify(answer)}`);
  await click("#aiPasteAdd");
  const ids = await page.evaluate("StatTrakDesigner.project.screens.map(s => s.id)");
  assert.deepEqual(ids.slice(-1), ["killFeed"]);
  assert.equal(await page.evaluate("document.querySelector('#aiModal').hidden"), true);
  assert.equal(await page.evaluate("StatTrakDesigner.current"), 4);
  assert.match(String(await page.evaluate("StatTrakDesigner.designFromText('no data').error")), /no design data/);
});
