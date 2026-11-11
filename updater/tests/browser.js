/* A small driver for a headless Chrome, for the tests of the full page.
 * It uses the DevTools protocol directly: no packages to install.
 * Needs Node.js 22 or later (global WebSocket) and Chrome, Chromium or Edge.
 */

const { spawn } = require("node:child_process");
const fs = require("node:fs");
const http = require("node:http");
const os = require("node:os");
const path = require("node:path");

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".mp4": "video/mp4",
  ".ttf": "font/ttf",
  ".woff2": "font/woff2",
};

function findChrome() {
  const candidates = [
    process.env.CHROME,
    "C:/Program Files/Google/Chrome/Application/chrome.exe",
    "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe",
    "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  ];
  return candidates.find((candidate) => candidate && fs.existsSync(candidate)) || null;
}

/* Serves the files of `root` on http://127.0.0.1:<port>. Returns { url, close }. */
function serve(root) {
  const server = http.createServer((request, response) => {
    let name = decodeURIComponent(new URL(request.url, "http://x").pathname);
    if (name.endsWith("/")) name += "index.html";
    const file = path.join(root, name);
    if (!file.startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
      response.writeHead(404).end("not found");
      return;
    }
    response.writeHead(200, { "content-type": TYPES[path.extname(file)] || "application/octet-stream" });
    fs.createReadStream(file).pipe(response);
  });
  return new Promise((resolve) => {
    server.listen(0, "127.0.0.1", () => {
      resolve({
        url: `http://127.0.0.1:${server.address().port}`,
        close: () => new Promise((done) => { server.closeAllConnections(); server.close(done); }),
      });
    });
  });
}

class Browser {
  static async launch() {
    const executable = findChrome();
    if (!executable) return null;

    const profile = fs.mkdtempSync(path.join(os.tmpdir(), "stattrak-web-test-"));
    const child = spawn(executable, [
      "--headless=new",
      "--remote-debugging-port=0",
      `--user-data-dir=${profile}`,
      "--no-first-run",
      "--no-sandbox",
      "--disable-gpu",
      // A page that is not in front must run at full speed
      "--disable-background-timer-throttling",
      "--disable-backgrounding-occluded-windows",
      "--disable-renderer-backgrounding",
      "--window-size=1280,1100",
      "about:blank",
    ], { stdio: ["ignore", "ignore", "pipe"] });

    const endpoint = await new Promise((resolve, reject) => {
      let text = "";
      const timer = setTimeout(() => reject(new Error("Chrome did not start: " + text)), 20000);
      child.stderr.on("data", (chunk) => {
        text += chunk;
        const match = text.match(/DevTools listening on (ws:\/\/\S+)/);
        if (match) { clearTimeout(timer); resolve(match[1]); }
      });
      child.on("exit", () => { clearTimeout(timer); reject(new Error("Chrome stopped: " + text)); });
    });

    const browser = new Browser(child, profile);
    await browser.connect(endpoint);
    return browser;
  }

  constructor(child, profile) {
    this.child = child;
    this.profile = profile;
    this.nextId = 1;
    this.waiting = new Map();
    this.listeners = [];
  }

  connect(endpoint) {
    this.socket = new WebSocket(endpoint);
    this.socket.addEventListener("message", (event) => {
      const message = JSON.parse(event.data);
      if (message.id) {
        const entry = this.waiting.get(message.id);
        this.waiting.delete(message.id);
        if (!entry) return;
        if (message.error) entry.reject(new Error(`${entry.method}: ${message.error.message}`));
        else entry.resolve(message.result);
        return;
      }
      for (const listener of this.listeners.slice()) listener(message);
    });
    return new Promise((resolve, reject) => {
      this.socket.addEventListener("open", resolve, { once: true });
      this.socket.addEventListener("error", () => reject(new Error("no connection to Chrome")), { once: true });
    });
  }

  send(method, params, sessionId) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.waiting.set(id, { resolve, reject, method });
      this.socket.send(JSON.stringify({ id, method, params: params || {}, sessionId }));
    });
  }

  /* A page in its own browser context: its storage is not shared with other tests. */
  async newPage() {
    const { browserContextId } = await this.send("Target.createBrowserContext");
    const { targetId } = await this.send("Target.createTarget", { url: "about:blank", browserContextId });
    const { sessionId } = await this.send("Target.attachToTarget", { targetId, flatten: true });
    const page = new Page(this, sessionId, browserContextId);
    await page.send("Page.enable");
    await page.send("Runtime.enable");
    return page;
  }

  async close() {
    try { this.socket.close(); } catch (_) {}
    await new Promise((resolve) => {
      this.child.once("exit", resolve);
      this.child.kill();
      setTimeout(resolve, 3000);
    });
    try { fs.rmSync(this.profile, { recursive: true, force: true, maxRetries: 5 }); } catch (_) {}
  }
}

class Page {
  constructor(browser, sessionId, browserContextId) {
    this.browser = browser;
    this.sessionId = sessionId;
    this.browserContextId = browserContextId;
    this.initScript = null;
    this.errors = [];
    browser.listeners.push((message) => {
      if (message.sessionId !== sessionId) return;
      if (message.method === "Runtime.exceptionThrown") {
        const details = message.params.exceptionDetails;
        this.errors.push((details.exception && details.exception.description) || details.text);
      }
    });
  }

  send(method, params) {
    return this.browser.send(method, params, this.sessionId);
  }

  /* A script that runs in the page before the scripts of the page, at each load. */
  async setInitScript(source) {
    if (this.initScript) await this.send("Page.removeScriptToEvaluateOnNewDocument", { identifier: this.initScript });
    const { identifier } = await this.send("Page.addScriptToEvaluateOnNewDocument", { source });
    this.initScript = identifier;
  }

  /* Loads a page and waits until it is complete. The check is made in the page itself:
     a "load" event of the page before (about:blank, or the page before a reload) can
     arrive late, and then a test would click before the page is ready. */
  async goto(url) {
    const mark = "load" + this.browser.nextId;
    await this.send("Page.navigate", { url: url + "?" + mark });
    const start = Date.now();
    for (;;) {
      try {
        if (await this.evaluate(`document.readyState === "complete" && location.search === "?${mark}"`)) return;
      } catch (_) {
        // The old page goes away during the load
      }
      if (Date.now() - start > 20000) throw new Error("the page did not load: " + url);
      await new Promise((resolve) => setTimeout(resolve, 25));
    }
  }

  async evaluate(expression) {
    const result = await this.send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
    if (result.exceptionDetails) {
      const details = result.exceptionDetails;
      throw new Error("in the page: " + ((details.exception && details.exception.description) || details.text));
    }
    return result.result.value;
  }

  async waitFor(expression, timeoutMs = 10000) {
    const start = Date.now();
    for (;;) {
      if (await this.evaluate(`!!(${expression})`)) return;
      if (Date.now() - start > timeoutMs) {
        let details = "";
        try { if (this.diagnose) details = " " + await this.diagnose(); } catch (_) {}
        throw new Error("timeout, not true in the page: " + expression + details);
      }
      await new Promise((resolve) => setTimeout(resolve, 25));
    }
  }

  async screenshot(file) {
    const { data } = await this.send("Page.captureScreenshot", { format: "png" });
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, Buffer.from(data, "base64"));
  }

  async close() {
    await this.browser.send("Target.disposeBrowserContext", { browserContextId: this.browserContextId });
  }
}

module.exports = { Browser, serve, findChrome };
