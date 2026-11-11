/* Tests of the files of the page: fonts and other sites. Run: node --test "tests/*.test.js" */

const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { ROOT } = require("./helpers.js");

const read = (name) => fs.readFileSync(path.join(ROOT, name), "utf8");
const cssFiles = fs.readdirSync(path.join(ROOT, "css")).map((name) => "css/" + name);

test("the page loads no style, script, font or picture from an other site", () => {
  for (const name of cssFiles) {
    const css = read(name);
    assert.doesNotMatch(css, /@import/, name + " imports an other style file");
    assert.doesNotMatch(css, /url\(\s*['"]?(https?:)?\/\//, name + " loads a file from an other site");
  }
  const html = read("index.html");
  // Links for the user (<a href>) are not loaded by the page
  const loaded = html.match(/<(script|link|img|video|source|iframe)\b[^>]*>/g) || [];
  for (const tag of loaded) {
    assert.doesNotMatch(tag, /(src|href)\s*=\s*['"](https?:)?\/\//, "loaded from an other site: " + tag);
  }
});

test("each font file that the styles name is in the fonts folder", () => {
  let count = 0;
  for (const name of cssFiles) {
    for (const match of read(name).matchAll(/url\(\s*['"]?([^'")]+)['"]?\s*\)/g)) {
      const file = path.join(ROOT, "css", match[1]);
      assert.ok(fs.existsSync(file), name + " names a file that is not there: " + match[1]);
      count++;
    }
  }
  assert.ok(count >= 7, "the font declarations are missing");
});

test("the paid font is not in the page", () => {
  const fonts = fs.readdirSync(path.join(ROOT, "fonts"));
  assert.deepEqual(fonts.filter((name) => /lakes/i.test(name) || /\.(ttf|otf)$/i.test(name)), []);
  for (const name of [...cssFiles, "index.html"]) {
    assert.doesNotMatch(read(name), /TT ?Lakes/i, name);
  }
  // The license texts of the free fonts go with the font files
  assert.ok(fonts.includes("OFL-BarlowCondensed.txt") && fonts.includes("OFL-JetBrainsMono.txt"));
});
