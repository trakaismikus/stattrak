/* Tests of api/firmware.js (the file of an official release through our own address) in Node.js,
 * with a stand-in for GitHub.
 * Run: node --test "tests/*.test.js"
 */
const test = require("node:test");
const assert = require("node:assert/strict");
const path = require("node:path");
const { pathToFileURL } = require("node:url");

const load = () => import(pathToFileURL(path.join(__dirname, "..", "api", "firmware.js")).href);

async function call(query, github) {
  const { default: handler } = await load();
  const asked = [];
  const real = global.fetch;
  global.fetch = async (url) => { asked.push(String(url)); return github(String(url)); };
  try {
    const res = await handler(new Request("https://site.test/api/firmware" + query));
    return { res, asked };
  } finally {
    global.fetch = real;
  }
}

test("a release file: from the official repo, the bytes unchanged, kept at the edge", async () => {
  const { REPO } = await load();
  const bytes = new Uint8Array([0xe9, 1, 2, 3]);
  const { res, asked } = await call("?tag=v1.0.1-added-rcr123a&file=stattrak.bin", () => new Response(bytes, { status: 200 }));
  assert.equal(res.status, 200);
  assert.deepEqual(asked, [`https://github.com/${REPO}/releases/download/v1.0.1-added-rcr123a/stattrak.bin`]);
  assert.deepEqual(new Uint8Array(await res.arrayBuffer()), bytes);
  assert.match(res.headers.get("cache-control"), /s-maxage=86400/);
});

test("only .bin files with plain names; nothing else is asked from GitHub", async () => {
  for (const q of ["?tag=v1&file=stattrak.exe", "?tag=../x&file=a.bin", "?tag=v1&file=../../a.bin", "?tag=&file=a.bin", "?tag=v1", "?tag=v1/x&file=a.bin"]) {
    const { res, asked } = await call(q, () => new Response("x"));
    assert.equal(res.status, 404, q);
    assert.deepEqual(asked, [], q);
  }
});

test("a missing release is 404; a file over 1 MB is refused", async () => {
  assert.equal((await call("?tag=v9&file=stattrak.bin", () => new Response("no", { status: 404 }))).res.status, 404);
  const big = new Uint8Array(1024 * 1024 + 1);
  assert.equal((await call("?tag=v1&file=stattrak.bin", () => new Response(big, { status: 200 }))).res.status, 413);
});
