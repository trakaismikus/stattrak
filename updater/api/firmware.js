// The firmware file of an official release, from our own address (a Vercel Edge Function).
//   GET /api/firmware?tag=<release tag>&file=<name>.bin
// GitHub sends release files from release-assets.githubusercontent.com, without the header that
// lets a web page read them (CORS). This function gets the file for the page, so that the person
// can pick a release on the Firmware tab without a download and a drop. It reads only .bin files
// of the official release repo (the same repo as GH_OWNER / GH_REPO in js/main.js), at most 1 MB.
// The page checks the file again (js/firmware.js: the image header, and the SHA-256 against the
// digest of the release). From a disk or a server without functions the page shows the manual way.
export const config = { runtime: 'edge' };

export const REPO = 'trakaismikus/stattrak';
const MAX = 1024 * 1024;   // as FirmwareFile.MAX_SIZE
const NAME = /^[A-Za-z0-9][A-Za-z0-9._-]{0,99}$/;

export default async function handler(request) {
  const url = new URL(request.url);
  const tag = url.searchParams.get('tag') || '';
  const file = url.searchParams.get('file') || '';
  if (request.method !== 'GET' || !NAME.test(tag) || !NAME.test(file) || !/\.bin$/i.test(file) || tag.includes('..') || file.includes('..')) {
    return new Response('not found', { status: 404 });
  }
  let res;
  try {
    res = await fetch(`https://github.com/${REPO}/releases/download/${encodeURIComponent(tag)}/${encodeURIComponent(file)}`, { redirect: 'follow' });
  } catch (_) {
    return new Response('GitHub did not answer', { status: 502 });
  }
  if (!res.ok) return new Response('not found', { status: res.status === 404 ? 404 : 502 });
  if (Number(res.headers.get('content-length') || 0) > MAX) return new Response('too large', { status: 413 });
  const bytes = await res.arrayBuffer();
  if (bytes.byteLength > MAX) return new Response('too large', { status: 413 });
  return new Response(bytes, {
    status: 200,
    headers: {
      'content-type': 'application/octet-stream',
      // A release file does not change: the edge keeps it for a day
      'cache-control': 'public, max-age=3600, s-maxage=86400',
    },
  });
}
