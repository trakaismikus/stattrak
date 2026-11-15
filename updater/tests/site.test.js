/* The StatTrak web site: one folder (updater/) with three pages that share one menu.
 *   index.html           Firmware and My device (the web updater)
 *   start/index.html     Get started
 *   designer/index.html  the Screen Designer
 * Each page must have the same menu, and each local link and file must exist, also when the page
 * is opened from a disk (file://): so the links name index.html, not a folder. */
"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");

const SITE = path.join(__dirname, "..");
const PAGES = ["index.html", "start/index.html", "designer/index.html"];
const MENU = ["Get started", "Firmware", "My device", "Designer"];
// Where each menu entry goes, seen from the site root
const TARGETS = ["start/index.html", "index.html#firmware", "index.html#device", "designer/index.html"];

const read = (page) => fs.readFileSync(path.join(SITE, page), "utf8");
const strip = (html) => html.replace(/<[^>]+>/g, "").replace(/\s+/g, " ").trim();

function menuOf(page) {
  const html = read(page);
  const nav = html.match(/<nav class="site-nav" aria-label="StatTrak tools">([\s\S]*?)<\/nav>/);
  assert.ok(nav, `${page}: no site menu`);
  return [...nav[1].matchAll(/<a\b([^>]*)>([\s\S]*?)<\/a>/g)].map((m) => ({
    href: m[1].match(/href="([^"]*)"/)[1],
    text: strip(m[2]),
    current: /aria-current="page"/.test(m[1]),
  }));
}

// A link seen from the site root: "../index.html#device" on start/index.html -> "index.html#device"
function fromRoot(page, href) {
  const [file, hash] = href.split("#");
  const resolved = file ? path.posix.normalize(path.posix.join(path.posix.dirname(page), file)) : page;
  return hash ? `${resolved}#${hash}` : resolved;
}

test("every page has the same menu, and it marks the page itself", () => {
  const current = { "index.html": "Firmware", "start/index.html": "Get started", "designer/index.html": "Designer" };
  for (const page of PAGES) {
    const menu = menuOf(page);
    assert.deepEqual(menu.map((m) => m.text), MENU, page);
    assert.deepEqual(menu.map((m) => fromRoot(page, m.href)), TARGETS, page);
    assert.deepEqual(menu.filter((m) => m.current).map((m) => m.text), [current[page]], page);
  }
});

test("every local link, picture, script and style of each page exists", () => {
  for (const page of PAGES) {
    const html = read(page);
    const refs = [...html.matchAll(/\b(?:href|src)="([^"]+)"/g)].map((m) => m[1])
      .concat([...html.matchAll(/url\(\s*['"]?([^'")]+)['"]?\s*\)/g)].map((m) => m[1]));
    for (const ref of refs) {
      if (/^(https?:|mailto:|data:|#)/.test(ref) || ref.includes("${")) continue;
      const file = fromRoot(page, ref).split("#")[0];
      assert.ok(fs.existsSync(path.join(SITE, file)), `${page}: ${ref} does not exist`);
    }
  }
});

test("every page has the same header: the logo, then the menu (css/site.css)", () => {
  for (const page of PAGES) {
    const html = read(page);
    assert.match(html, /<link rel="stylesheet" href="(?:\.\.\/)?css\/site\.css"/, page);
    const head = html.match(/<header class="site-header">\s*<div class="site-header__in">\s*<a class="site-header__brand"(?: id="siteLogo")? href="([^"]+)"><img src="([^"]+)"/);
    assert.ok(head, `${page}: no site header`);
    assert.equal(fromRoot(page, head[1]), "index.html", page);
    assert.equal(fromRoot(page, head[2]), "assets/stattrak-wordmark-white.svg", page);
  }
});

test("the links inside the Get started page go to its own sections", () => {
  const html = read("start/index.html");
  const ids = new Set([...html.matchAll(/\bid="([^"]+)"/g)].map((m) => m[1]));
  for (const m of html.matchAll(/href="#([^"]+)"/g)) assert.ok(ids.has(m[1]), `#${m[1]} has no section`);
});

test("the published site leaves out the tests and the notes for developers", () => {
  const ignore = fs.readFileSync(path.join(SITE, ".vercelignore"), "utf8").split(/\r?\n/);
  for (const name of ["tests", "package.json", "README.md"]) assert.ok(ignore.includes(name), name);
});

/* A site that adds its own parts (an extra tab, a sign-in button, its styles and scripts) puts them
   at these places. The site works without them: they are comments. */
test("every page has the places for added parts, and the StatTrak button or a Connect link", () => {
  for (const page of PAGES) {
    const html = read(page);
    for (const mark of ["site:head", "site:nav", "site:meta", "site:body"]) {
      assert.equal(html.split(`<!-- ${mark} -->`).length, 2, `${page}: one <!-- ${mark} -->`);
    }
    assert.ok(html.indexOf("<!-- site:head -->") < html.indexOf("</head>"), page);
    assert.ok(html.indexOf("<!-- site:body -->") > html.lastIndexOf("<script"), `${page}: site:body after the scripts`);
    // site:nav is in the menu, right after My device
    const nav = html.match(/<nav class="site-nav"[^>]*>([\s\S]*?)<\/nav>/)[1];
    assert.match(nav, />My device<\/a>\s*<!-- site:nav -->/, page);
    if (page === "index.html") {
      assert.match(html, /<div class="devbar" id="devBar"><\/div>/);
      assert.equal(html.split("<!-- site:views -->").length, 2);
    } else {
      assert.match(html, /<a class="devbtn devbtn--off" href="\.\.\/index\.html#device"/, page);
    }
  }
});
