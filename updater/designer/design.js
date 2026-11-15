/*
 * StatTrak screen designs: the file format, the drawing and the text for an AI tool.
 *
 * The same rules as tools/design.py. The test tests/test_designer.py runs this file with Node.js
 * and compares the results with tools/design.py, which is compared with the firmware draw code.
 * So a screen in the Screen Designer has the same pixels as on the device.
 *
 * Works in a browser (window.StatTrakDesign) and in Node.js (require).
 */
(function (root, factory) {
  if (typeof module === "object" && module.exports) {
    module.exports = factory();
  } else {
    root.StatTrakDesign = factory();
  }
})(typeof self !== "undefined" ? self : this, function () {
  "use strict";

  var FORMAT_NAME = "stattrak-design";
  var FORMAT_VERSION = 1;
  var WIDTH = 128;
  var HEIGHT = 32;
  var ELEMENT_TYPES = ["text", "rect", "line", "circle", "image", "pixels"];
  var MAX_SCALE = 8;
  var MAX_BORDER = 4;           // a cut-out border of 0 to 4 pixels around an element
  var MAX_CIRCLE_RADIUS = 127;
  var COORD_LIMIT = 1000;
  var ALIGNS = ["left", "center", "right"];

  /* --- Fonts and pictures ---------------------------------------------------------------------- */

  function prepareAssets(assets) {
    if (!assets.fontByName) {
      assets.fontByName = {};
      assets.fonts.forEach(function (font) { assets.fontByName[font.name] = font; });
      assets.imageByName = {};
      assets.images.forEach(function (image) { assets.imageByName[image.name] = image; });
    }
    return assets;
  }

  /* Width in pixels of a text at scale 1, as the firmware calculates it */
  function textWidth(font, text) {
    var width = 0;
    for (var i = 0; i < text.length; i++) {
      width += font.chars[text[i]].w + font.space;
    }
    return width - font.space;
  }

  /* --- Check a design ---------------------------------------------------------------------------- */

  function isInt(value) {
    return typeof value === "number" && isFinite(value) && Math.floor(value) === value;
  }

  /* A text value. A value of an other kind (number, null, list) is not used. */
  function str(value, fallback) {
    return (typeof value === "string") ? value : fallback;
  }

  /* Returns { design, problems, warnings }. With problems the design is not usable. */
  function normalize(design, assets) {
    prepareAssets(assets);
    var problems = [];
    var warnings = [];

    if (design === null || typeof design !== "object" || Array.isArray(design)) {
      return { design: null, problems: ["The file must contain one JSON object."], warnings: warnings };
    }
    if (design.format !== FORMAT_NAME) problems.push('"format" must be "' + FORMAT_NAME + '".');
    if (design.version !== FORMAT_VERSION) problems.push('"version" must be ' + FORMAT_VERSION + ".");

    var screens = design.screens;
    if (!Array.isArray(screens) || screens.length === 0) {
      problems.push('"screens" must be a list with one screen or more.');
      screens = [];
    }

    var clean = { format: FORMAT_NAME, version: FORMAT_VERSION, name: str(design.name, "Design"), screens: [] };
    var usedIds = [];

    function number(element, key, where, fallback, low, high) {
      var value = (element[key] === undefined) ? fallback : element[key];
      if (low === undefined) low = -COORD_LIMIT;
      if (high === undefined) high = COORD_LIMIT;
      if (!isInt(value)) {
        problems.push(where + ': "' + key + '" must be a whole number.');
        return 0;
      }
      if (value < low || value > high) {
        problems.push(where + ': "' + key + '" is ' + value + ". It must be from " + low + " to " + high + ".");
        return 0;
      }
      return value;
    }

    screens.forEach(function (screen, index) {
      var label = "Screen " + (index + 1);
      if (screen === null || typeof screen !== "object" || Array.isArray(screen)) {
        problems.push(label + " must be an object.");
        return;
      }
      var id = str(screen.id, "screen" + (index + 1));
      if (!/^[A-Za-z][A-Za-z0-9_]{0,30}$/.test(id)) {
        problems.push(label + ': "id" must start with a letter and have letters, digits and _ only (31 characters at most).');
      }
      if (usedIds.indexOf(id) >= 0) problems.push(label + ": the id " + id + " is used two times.");
      usedIds.push(id);

      var elements = (screen.elements === undefined) ? [] : screen.elements;
      if (!Array.isArray(elements)) {
        problems.push(label + ': "elements" must be a list.');
        elements = [];
      }

      var cleanScreen = { id: id, name: str(screen.name, id), note: str(screen.note, ""), elements: [] };

      elements.forEach(function (element, elementIndex) {
        var where = label + ", element " + (elementIndex + 1);
        if (element === null || typeof element !== "object" || ELEMENT_TYPES.indexOf(element.type) < 0) {
          problems.push(where + ': "type" must be one of: ' + ELEMENT_TYPES.join(", ") + ".");
          return;
        }
        var kind = element.type;
        where += " (" + kind + ")";

        var colorName = (element.color === undefined) ? "white" : element.color;
        if (colorName !== "white" && colorName !== "black") {
          problems.push(where + ': "color" must be "white" (pixel on) or "black" (pixel off).');
          colorName = "white";
        }
        var item = { type: kind, color: (colorName === "white") ? 1 : 0, note: str(element.note, ""),
                     border: number(element, "border", where, 0, 0, MAX_BORDER) };

        if (kind === "text") {
          var fontName = (element.font === undefined) ? "5x7" : element.font;
          var font = (typeof fontName === "string" && Object.prototype.hasOwnProperty.call(assets.fontByName, fontName))
            ? assets.fontByName[fontName] : null;
          var text = (element.text === undefined) ? "" : element.text;
          if (!font) {
            problems.push(where + ": the font " + JSON.stringify(fontName) + " does not exist.");
            return;
          }
          if (typeof text !== "string" || text === "") {
            problems.push(where + ': "text" must not be empty.');
            return;
          }
          for (var i = 0; i < text.length; i++) {
            if (!Object.prototype.hasOwnProperty.call(font.chars, text[i])) {
              problems.push(where + ": the firmware fonts do not have the character " + JSON.stringify(text[i]) +
                            ". Use the characters from space to ~ only.");
              return;
            }
          }
          item.x = number(element, "x", where, 0);
          item.y = number(element, "y", where, 0);
          item.text = text;
          item.font = font.name;
          item.align = (element.align === undefined) ? "left" : element.align;
          item.scale = number(element, "scale", where, 1, 1, MAX_SCALE);
          item.gap = element.gap === true;
          item.invert = element.invert === true;
          if (ALIGNS.indexOf(item.align) < 0) {
            problems.push(where + ': "align" must be left, center or right.');
            item.align = "left";
          }
          if (item.invert && item.scale !== 1) problems.push(where + ': "invert" works with scale 1 only.');
          if (item.gap && item.scale < 2) {
            warnings.push(where + ': "gap" has no effect at scale 1.');
            item.gap = false;
          }
          if (font.name === "numbers" && /[^0-9-]/.test(text)) {
            warnings.push(where + ': the font "numbers" has the digits and "-" only. The firmware draws a 0 for each other character.');
          }
          if (["5x5", "3x5", "big"].indexOf(font.name) >= 0 && /[a-z]/.test(text)) {
            warnings.push(where + ': the font "' + font.name + '" has capital letters only. Small letters are drawn as capitals.');
          }
        } else if (kind === "rect") {
          item.x = number(element, "x", where, 0);
          item.y = number(element, "y", where, 0);
          item.w = number(element, "w", where, null, 1);
          item.h = number(element, "h", where, null, 1);
          item.fill = element.fill === true;
          item.radius = number(element, "radius", where, 0, 0, COORD_LIMIT);
          if (item.w && item.h) {
            var largest = Math.floor((Math.min(item.w, item.h) - 1) / 2);
            if (item.radius > largest) {
              problems.push(where + ': "radius" ' + item.radius + " is too large for " + item.w + " x " + item.h +
                            " pixels. The largest is " + largest + ".");
            }
          }
        } else if (kind === "line") {
          ["x1", "y1", "x2", "y2"].forEach(function (key) { item[key] = number(element, key, where, null); });
        } else if (kind === "circle") {
          item.x = number(element, "x", where, 0);
          item.y = number(element, "y", where, 0);
          item.r = number(element, "r", where, null, 0, MAX_CIRCLE_RADIUS);
          item.fill = element.fill === true;
        } else if (kind === "image") {
          var image = (typeof element.name === "string" && Object.prototype.hasOwnProperty.call(assets.imageByName, element.name))
            ? assets.imageByName[element.name] : null;
          if (!image) {
            problems.push(where + ": the picture " + JSON.stringify(element.name) + " does not exist.");
            return;
          }
          item.name = image.name;
          item.x = number(element, "x", where, 0);
          item.y = number(element, "y", where, 0);
        } else if (kind === "pixels") {
          var rows = element.rows;
          var good = Array.isArray(rows) && rows.length >= 1 && rows.length <= HEIGHT;
          for (var r = 0; good && r < rows.length; r++) {
            if (typeof rows[r] !== "string" || !/^[#.]+$/.test(rows[r])) good = false;
          }
          for (r = 0; good && r < rows.length; r++) {
            if (rows[r].length !== rows[0].length) good = false;
          }
          if (good && rows[0].length > WIDTH) good = false;
          if (!good) {
            problems.push(where + ': "rows" must be 1 to ' + HEIGHT + " lines of the same length (" + WIDTH +
                          " at most), with # for a pixel that is drawn and . for a pixel that is not.");
            return;
          }
          item.x = number(element, "x", where, 0);
          item.y = number(element, "y", where, 0);
          item.rows = rows.slice();
        }

        cleanScreen.elements.push(item);
      });

      clean.screens.push(cleanScreen);
    });

    return { design: clean, problems: problems, warnings: warnings };
  }

  /* The design in the form of the file: colors as words, default values left out */
  function toFile(clean) {
    return {
      format: FORMAT_NAME,
      version: FORMAT_VERSION,
      name: clean.name,
      screens: clean.screens.map(function (screen) {
        return {
          id: screen.id,
          name: screen.name,
          note: screen.note,
          elements: screen.elements.map(function (element) {
            var out = { type: element.type };
            if (element.type === "text") {
              out.x = element.x; out.y = element.y; out.text = element.text; out.font = element.font;
              out.align = element.align;
              if (element.scale !== 1) out.scale = element.scale;
              if (element.gap) out.gap = true;
              if (element.invert) out.invert = true;
            } else if (element.type === "rect") {
              out.x = element.x; out.y = element.y; out.w = element.w; out.h = element.h;
              out.fill = element.fill;
              if (element.radius) out.radius = element.radius;
            } else if (element.type === "line") {
              out.x1 = element.x1; out.y1 = element.y1; out.x2 = element.x2; out.y2 = element.y2;
            } else if (element.type === "circle") {
              out.x = element.x; out.y = element.y; out.r = element.r; out.fill = element.fill;
            } else if (element.type === "image") {
              out.name = element.name; out.x = element.x; out.y = element.y;
            } else {
              out.x = element.x; out.y = element.y; out.rows = element.rows.slice();
            }
            if (!element.color) out.color = "black";
            if (element.border) out.border = element.border;
            if (element.note) out.note = element.note;
            return out;
          })
        };
      })
    };
  }

  /* --- Drawing: the same steps as components/gfx/gfx.c ------------------------------------------- */

  function Frame(assets) {
    this.assets = prepareAssets(assets);
    this.pixels = new Uint8Array(WIDTH * HEIGHT);
  }

  Frame.prototype.pixel = function (x, y, color) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) this.pixels[y * WIDTH + x] = color;
  };

  Frame.prototype.invert = function (x0, y0, x1, y1) {
    for (var y = Math.max(y0, 0); y < Math.min(y1, HEIGHT); y++) {
      for (var x = Math.max(x0, 0); x < Math.min(x1, WIDTH); x++) this.pixels[y * WIDTH + x] ^= 1;
    }
  };

  Frame.prototype.fill = function (x, y, w, h, color) {
    for (var ix = x; ix < x + w; ix++) {
      for (var iy = y; iy < y + h; iy++) this.pixel(ix, iy, color);
    }
  };

  Frame.prototype.line = function (x0, y0, x1, y1, color) {
    var steep = Math.abs(y1 - y0) > Math.abs(x1 - x0);
    var t;
    if (steep) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    var dx = x1 - x0;
    var dy = Math.abs(y1 - y0);
    var err = Math.trunc(dx / 2);
    var ystep = (y0 < y1) ? 1 : -1;
    for (; x0 <= x1; x0++) {
      if (steep) this.pixel(y0, x0, color); else this.pixel(x0, y0, color);
      err -= dy;
      if (err < 0) { y0 += ystep; err += dx; }
    }
  };

  Frame.prototype.hline = function (x, y, width, color) { this.line(x, y, x + width - 1, y, color); };
  Frame.prototype.vline = function (x, y, height, color) { this.line(x, y, x, y + height - 1, color); };

  function cornerSteps(radius) {
    var steps = [];
    var f = 1 - radius, ddfX = 1, ddfY = -2 * radius, x = 0, y = radius;
    while (x < y) {
      if (f >= 0) { y--; ddfY += 2; f += ddfY; }
      x++; ddfX += 2; f += ddfX;
      steps.push([x, y]);
    }
    return steps;
  }

  Frame.prototype.roundRect = function (x, y, w, h, r, color) {
    this.hline(x + r, y, w - 2 * r, color);
    this.hline(x + r, y + h - 1, w - 2 * r, color);
    this.vline(x, y + r, h - 2 * r, color);
    this.vline(x + w - 1, y + r, h - 2 * r, color);
    var corners = [[x + r, y + r, -1, -1], [x + w - r - 1, y + r, 1, -1],
                   [x + w - r - 1, y + h - r - 1, 1, 1], [x + r, y + h - r - 1, -1, 1]];
    var steps = cornerSteps(r);
    for (var c = 0; c < corners.length; c++) {
      for (var s = 0; s < steps.length; s++) {
        this.pixel(corners[c][0] + corners[c][2] * steps[s][0], corners[c][1] + corners[c][3] * steps[s][1], color);
        this.pixel(corners[c][0] + corners[c][2] * steps[s][1], corners[c][1] + corners[c][3] * steps[s][0], color);
      }
    }
  };

  Frame.prototype.fillRoundRect = function (x, y, w, h, r, color) {
    this.fill(x + r, y, w - 2 * r, h, color);
    var delta = h - 2 * r - 1;
    var sides = [[x + w - r - 1, 1], [x + r, -1]];
    var steps = cornerSteps(r);
    for (var i = 0; i < sides.length; i++) {
      var cx = sides[i][0], sign = sides[i][1], cy = y + r;
      for (var s = 0; s < steps.length; s++) {
        this.vline(cx + sign * steps[s][0], cy - steps[s][1], 2 * steps[s][1] + 1 + delta, color);
        this.vline(cx + sign * steps[s][1], cy - steps[s][0], 2 * steps[s][0] + 1 + delta, color);
      }
    }
  };

  Frame.prototype.circle = function (x0, y0, r, color) {
    this.pixel(x0, y0 + r, color); this.pixel(x0, y0 - r, color);
    this.pixel(x0 + r, y0, color); this.pixel(x0 - r, y0, color);
    var steps = cornerSteps(r);
    for (var s = 0; s < steps.length; s++) {
      var x = steps[s][0], y = steps[s][1];
      this.pixel(x0 + x, y0 + y, color); this.pixel(x0 - x, y0 + y, color);
      this.pixel(x0 + x, y0 - y, color); this.pixel(x0 - x, y0 - y, color);
      this.pixel(x0 + y, y0 + x, color); this.pixel(x0 - y, y0 + x, color);
      this.pixel(x0 + y, y0 - x, color); this.pixel(x0 - y, y0 - x, color);
    }
  };

  Frame.prototype.fillCircle = function (x0, y0, r, color) {
    this.vline(x0, y0 - r, 2 * r + 1, color);
    var steps = cornerSteps(r);
    for (var s = 0; s < steps.length; s++) {
      var x = steps[s][0], y = steps[s][1];
      this.vline(x0 + x, y0 - y, 2 * y + 1, color);
      this.vline(x0 + y, y0 - x, 2 * x + 1, color);
      this.vline(x0 - x, y0 - y, 2 * y + 1, color);
      this.vline(x0 - y, y0 - x, 2 * x + 1, color);
    }
  };

  Frame.prototype.rows = function (x, y, rows, color, scale, gap) {
    scale = scale || 1;
    var size = gap ? scale - 1 : scale;
    for (var iy = 0; iy < rows.length; iy++) {
      var row = rows[iy];
      for (var ix = 0; ix < row.length; ix++) {
        if (row[ix] !== "#") continue;
        if (scale === 1) this.pixel(x + ix, y + iy, color);
        else this.fill(x + ix * scale, y + iy * scale, size, size, color);
      }
    }
  };

  Frame.prototype.text = function (element) {
    var font = this.assets.fontByName[element.font];
    var scale = element.scale;
    var width = textWidth(font, element.text);
    var x = element.x;
    if (element.align === "center") x -= Math.floor(width / 2) * scale;
    else if (element.align === "right") x -= width * scale;
    var start = x;
    for (var i = 0; i < element.text.length; i++) {
      var glyph = font.chars[element.text[i]];
      this.rows(x, element.y, glyph.rows, element.color, scale, element.gap);
      x += (glyph.w + font.space) * scale;
    }
    if (element.invert && scale === 1) {
      this.invert(start - 1, element.y - 1, start + width + 1, element.y + font.height + 1);
    }
  };

  /* An element, moved by dx, dy, in an other color (for the cut-out border) */
  function shifted(element, dx, dy, color) {
    var copy = {};
    for (var key in element) copy[key] = element[key];
    if (element.type === "line") { copy.x1 += dx; copy.x2 += dx; copy.y1 += dy; copy.y2 += dy; }
    else { copy.x += dx; copy.y += dy; }
    copy.color = color;
    copy.border = 0;
    return copy;
  }

  /* The cut-out border: all pixels up to `border` pixels around the element are switched off
     (the element is drawn black at each place from -border to +border). An inverted text: its
     white box grows. tools/design.py draws it the same way. */
  Frame.prototype.border = function (element) {
    var n = element.border;
    if (!n) return;
    if (element.type === "text" && element.invert) {
      var font = this.assets.fontByName[element.font];
      var width = textWidth(font, element.text);
      var x = element.x;
      if (element.align === "center") x -= Math.floor(width / 2);
      else if (element.align === "right") x -= width;
      this.fill(x - 1 - n, element.y - 1 - n, width + 2 + 2 * n, font.height + 2 + 2 * n, 0);
      return;
    }
    for (var dy = -n; dy <= n; dy++) {
      for (var dx = -n; dx <= n; dx++) this.element(shifted(element, dx, dy, 0));
    }
  };

  Frame.prototype.element = function (element) {
    var color = element.color;
    if (element.type === "text") {
      this.text(element);
    } else if (element.type === "rect") {
      if (element.fill || element.w === 1 || element.h === 1) {
        if (element.radius === 0) this.fill(element.x, element.y, element.w, element.h, color);
        else this.fillRoundRect(element.x, element.y, element.w, element.h, element.radius, color);
      } else {
        this.roundRect(element.x, element.y, element.w, element.h, element.radius, color);
      }
    } else if (element.type === "line") {
      this.line(element.x1, element.y1, element.x2, element.y2, color);
    } else if (element.type === "circle") {
      if (element.fill) this.fillCircle(element.x, element.y, element.r, color);
      else this.circle(element.x, element.y, element.r, color);
    } else if (element.type === "image") {
      this.rows(element.x, element.y, this.assets.imageByName[element.name].rows, color);
    } else if (element.type === "pixels") {
      this.rows(element.x, element.y, element.rows, color);
    }
  };

  Frame.prototype.lines = function () {
    var out = [];
    for (var y = 0; y < HEIGHT; y++) {
      var row = "";
      for (var x = 0; x < WIDTH; x++) row += this.pixels[y * WIDTH + x] ? "#" : ".";
      out.push(row);
    }
    return out;
  };

  /* Draws a screen of a clean design. Returns a Frame (frame.pixels: 128 x 32 values, 1 = on). */
  function drawScreen(screen, assets) {
    var frame = new Frame(assets);
    screen.elements.forEach(function (element) { frame.border(element); frame.element(element); });
    return frame;
  }

  /* The rectangle that an element covers: { x, y, w, h } */
  function bounds(element, assets) {
    prepareAssets(assets);
    if (element.type === "text") {
      var font = assets.fontByName[element.font];
      var width = textWidth(font, element.text) * element.scale;
      var x = element.x;
      if (element.align === "center") x -= Math.floor(textWidth(font, element.text) / 2) * element.scale;
      else if (element.align === "right") x -= width;
      return { x: x, y: element.y, w: Math.max(width, 1), h: font.height * element.scale };
    }
    if (element.type === "rect") return { x: element.x, y: element.y, w: element.w, h: element.h };
    if (element.type === "circle") {
      return { x: element.x - element.r, y: element.y - element.r, w: 2 * element.r + 1, h: 2 * element.r + 1 };
    }
    if (element.type === "line") {
      return { x: Math.min(element.x1, element.x2), y: Math.min(element.y1, element.y2),
               w: Math.abs(element.x2 - element.x1) + 1, h: Math.abs(element.y2 - element.y1) + 1 };
    }
    if (element.type === "image") {
      var image = assets.imageByName[element.name];
      return { x: element.x, y: element.y, w: image.w, h: image.h };
    }
    return { x: element.x, y: element.y, w: element.rows[0].length, h: element.rows.length };
  }

  /* --- Text for an AI tool ----------------------------------------------------------------------- */

  function elementText(element, assets) {
    var color = element.color ? "" : ", black (pixels off)";
    var note = element.note ? "  [" + element.note + "]" : "";
    if (element.border) {
      color += ", cut-out border " + element.border + " px (pixels up to " + element.border + " px around it are off)";
    }
    if (element.type === "circle") {
      var size = 2 * element.r + 1;
      return (element.fill ? "filled circle" : "circle outline") + " center x=" + element.x + " y=" + element.y +
             " radius " + element.r + " (" + size + " x " + size + " pixels)" + color + note;
    }
    if (element.type === "text") {
      var font = assets.fontByName[element.font];
      var width = textWidth(font, element.text) * element.scale;
      var height = font.height * element.scale;
      var edge = { left: "x is the left edge", center: "x is the center", right: "x is the right edge" };
      var extra = "";
      if (element.scale > 1) extra += ", scale " + element.scale + (element.gap ? " with a gap between the dots" : "");
      if (element.invert) extra += ", inverted (black text in a white box)";
      return "text " + JSON.stringify(element.text) + ', font "' + element.font + '"' + extra + ", x=" + element.x +
             " y=" + element.y + " (" + edge[element.align] + "), " + width + " x " + height + " pixels" + color + note;
    }
    if (element.type === "rect") {
      return (element.fill ? "filled rectangle" : "rectangle outline") + " x=" + element.x + " y=" + element.y +
             " w=" + element.w + " h=" + element.h + (element.radius ? ", corner radius " + element.radius : "") + color + note;
    }
    if (element.type === "line") {
      return "line from x=" + element.x1 + " y=" + element.y1 + " to x=" + element.x2 + " y=" + element.y2 + color + note;
    }
    if (element.type === "image") {
      var image = assets.imageByName[element.name];
      return 'firmware picture "' + element.name + '" (' + image.w + " x " + image.h + ") at x=" + element.x +
             " y=" + element.y + color + note;
    }
    return "free pixels, " + element.rows[0].length + " x " + element.rows.length + ", at x=" + element.x +
           " y=" + element.y + color + note;
  }

  function screenLines(screen, index, count, assets, pictures) {
    var out = [];
    out.push('=== Screen ' + (index + 1) + " of " + count + ': "' + screen.name + '" (id: ' + screen.id + ") ===");
    out.push("What it does: " + (screen.note || "(no note)"));
    if (screen.elements.length) {
      out.push("Elements, in the order they are drawn (a later element is on top):");
      screen.elements.forEach(function (element, number) {
        out.push("  " + (number + 1) + ". " + elementText(element, assets));
      });
    } else {
      out.push("Elements: none (an empty screen).");
    }
    if (pictures) {
      out.push("Picture ('#' = pixel on):");
      drawScreen(screen, assets).lines().forEach(function (row) { out.push(row); });
    }
    return out;
  }

  /* The same text as "python tools/design.py describe" */
  function describe(clean, assets, source) {
    prepareAssets(assets);
    var count = clean.screens.length;
    var out = [];
    out.push('StatTrak screen design "' + clean.name + '": ' + count + " screen" + (count === 1 ? "" : "s") + ".");
    out.push("The display has " + WIDTH + " x " + HEIGHT + " pixels. A pixel is on (white) or off (black). " +
             "x goes to the right, y goes down, the top left pixel is x=0 y=0.");
    if (source) out.push("Draw code for the firmware: python tools/design.py code " + source);
    clean.screens.forEach(function (screen, index) {
      out.push("");
      screenLines(screen, index, count, assets, true).forEach(function (line) { out.push(line); });
    });
    return out.join("\n") + "\n";
  }

  /* A file name for the design: small letters, digits and - only */
  function fileName(name) {
    var slug = String(name).toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 40);
    return (slug || "my-screens") + ".json";
  }

  /*
   * The text that a person pastes into an AI coding tool.
   *
   * It has three parts: what the AI tool must do, the screens in words (so a person and an AI
   * can read them), and the design file itself (the exact data). The AI tool saves the file and
   * gets pictures and draw code from tools/design.py. options.request: what the person wants.
   * options.pictures: add each screen as a picture of # and . characters, for an AI tool that
   * cannot run the tools of the firmware project.
   */
  function buildPrompt(clean, assets, options) {
    prepareAssets(assets);
    options = options || {};
    var count = clean.screens.length;
    var file = "designs/" + fileName(clean.name);
    var out = [];

    out.push('StatTrak screen design "' + clean.name + '": ' + count + " screen" + (count === 1 ? "" : "s") + ".");
    out.push("");
    out.push("I made " + (count === 1 ? "this screen" : "these screens") + " with the StatTrak Screen Designer.");
    out.push("What I want: " + (String(options.request || "").trim() ||
             "Build " + (count === 1 ? "this screen" : "these screens") + " into my StatTrak firmware."));
    out.push("");
    out.push("Instructions for the AI tool:");
    out.push("1. Work in the StatTrak firmware folder. Read AGENTS.md there first, if you did not do that yet.");
    out.push("2. Save the design data from the end of this message as the file " + file + ". Copy it exactly.");
    out.push("3. Run: python tools/design.py check " + file);
    out.push("4. Run: python tools/design.py describe " + file + "   (it prints each screen as a picture of pixels)");
    out.push("5. Follow AGENTS.md, section 5. Build exactly what the design shows. Ask me before you change the layout.");
    out.push("If you cannot run these commands, use the lists below. All positions and sizes are exact.");
    out.push("");
    out.push("The display has " + WIDTH + " x " + HEIGHT + " pixels. A pixel is on (white) or off (black). " +
             "x goes to the right, y goes down, the top left pixel is x=0 y=0.");

    clean.screens.forEach(function (screen, index) {
      out.push("");
      screenLines(screen, index, count, assets, !!options.pictures).forEach(function (line) { out.push(line); });
    });

    out.push("");
    out.push("=== Design data (save as " + file + ") ===");
    out.push("```json");
    out.push(JSON.stringify(toFile(clean), null, 1));
    out.push("```");
    return out.join("\n") + "\n";
  }

  return {
    FORMAT_NAME: FORMAT_NAME,
    FORMAT_VERSION: FORMAT_VERSION,
    WIDTH: WIDTH,
    HEIGHT: HEIGHT,
    ELEMENT_TYPES: ELEMENT_TYPES,
    MAX_SCALE: MAX_SCALE,
    MAX_BORDER: MAX_BORDER,
    MAX_CIRCLE_RADIUS: MAX_CIRCLE_RADIUS,
    prepareAssets: prepareAssets,
    textWidth: textWidth,
    normalize: normalize,
    toFile: toFile,
    drawScreen: drawScreen,
    bounds: bounds,
    describe: describe,
    fileName: fileName,
    buildPrompt: buildPrompt
  };
});
