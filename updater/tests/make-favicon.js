/* Makes the picture files of the favicon from favicon.svg, with the Chrome of the tests:
     favicon.ico            16, 32 and 48 px (PNG inside), for browsers that do not take an SVG
     apple-touch-icon.png   180 px, a full square (a phone cuts the corners itself)
   Run it after a change of favicon.svg:   node tests/make-favicon.js   (in updater/) */
"use strict";
const fs = require("node:fs");
const path = require("node:path");
const { Browser, serve } = require("./browser.js");

const SITE = path.join(__dirname, "..");
const SIZES = [16, 32, 48];

async function main() {
  const site = await serve(SITE);
  const browser = await Browser.launch();
  async function png(size, square) {
    const page = await browser.newPage();
    await page.send("Emulation.setDeviceMetricsOverride", { width: size, height: size, deviceScaleFactor: 1, mobile: false });
    await page.send("Emulation.setDefaultBackgroundColorOverride", { color: { r: 0, g: 0, b: 0, a: 0 } });
    await page.goto(site.url + "/favicon.svg");
    if (square) await page.evaluate('document.querySelector("rect").removeAttribute("rx")');
    const shot = await page.send("Page.captureScreenshot", { format: "png", clip: { x: 0, y: 0, width: size, height: size, scale: 1 } });
    await page.close();
    return Buffer.from(shot.data, "base64");
  }
  const images = [];
  for (const size of SIZES) images.push(await png(size, false));
  // An .ico file: a header of 6 bytes, an entry of 16 bytes for each picture, then the PNG files
  const head = Buffer.alloc(6 + 16 * images.length);
  head.writeUInt16LE(1, 2);
  head.writeUInt16LE(images.length, 4);
  let at = head.length;
  images.forEach((image, i) => {
    const entry = 6 + 16 * i;
    head[entry] = SIZES[i];
    head[entry + 1] = SIZES[i];
    head.writeUInt16LE(1, entry + 4);
    head.writeUInt16LE(32, entry + 6);
    head.writeUInt32LE(image.length, entry + 8);
    head.writeUInt32LE(at, entry + 12);
    at += image.length;
  });
  fs.writeFileSync(path.join(SITE, "favicon.ico"), Buffer.concat([head, ...images]));
  fs.writeFileSync(path.join(SITE, "apple-touch-icon.png"), await png(180, true));
  await browser.close();
  await site.close();
  for (const file of ["favicon.ico", "apple-touch-icon.png"]) console.log(file, fs.statSync(path.join(SITE, file)).size, "bytes");
}

main().catch((error) => { console.error(error); process.exit(1); });
