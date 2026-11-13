/*
 * Runs updater/designer/design.js (the code of the Screen Designer) in Node.js for the tests.
 * tests/test_designer.py sends a list of jobs as JSON on standard input and reads the results.
 */
"use strict";

const fs = require("fs");
const path = require("path");

const designerDir = path.join(__dirname, "..", "updater", "designer");
const StatTrakDesign = require(path.join(designerDir, "design.js"));

// assets.js is made for a browser: "window.STATTRAK_ASSETS = {...};"
const assetsText = fs.readFileSync(path.join(designerDir, "assets.js"), "utf8");
const assets = JSON.parse(assetsText.slice(assetsText.indexOf("{"), assetsText.lastIndexOf("}") + 1));

const jobs = JSON.parse(fs.readFileSync(0, "utf8"));
const results = jobs.map(function (job) {
  const checked = StatTrakDesign.normalize(job.design, assets);
  const result = { problems: checked.problems, warnings: checked.warnings, clean: checked.design };
  if (checked.problems.length > 0 || !checked.design) {
    return result;
  }
  if (job.op === "draw") {
    result.frames = checked.design.screens.map(function (screen) {
      return StatTrakDesign.drawScreen(screen, assets).lines();
    });
    result.bounds = checked.design.screens.map(function (screen) {
      return screen.elements.map(function (element) { return StatTrakDesign.bounds(element, assets); });
    });
  } else if (job.op === "describe") {
    result.text = StatTrakDesign.describe(checked.design, assets, job.source || "");
  } else if (job.op === "prompt") {
    result.text = StatTrakDesign.buildPrompt(checked.design, assets, job.options || {});
    result.file = StatTrakDesign.toFile(checked.design);
    result.fileName = StatTrakDesign.fileName(checked.design.name);
  }
  return result;
});

process.stdout.write(JSON.stringify(results));
