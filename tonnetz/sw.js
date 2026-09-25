'use strict';

/* Bump when the shell file list changes; content edits reach users via the revalidation below without a bump. */
const CACHE = 'tonnetz-v3';
const SHELL = ['./', 'index.html', 'lib/theory.js', 'lib/geometry.js', 'lib/harmony.js', 'lib/synth.js', 'lib/sequencer.js', 'lib/player.js', 'lib/presets.js', 'lib/improv.js','manifest.webmanifest', 'icon-192.png', 'icon-512.png', 'icon-maskable-512.png', 'apple-touch-icon.png'];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
      .then(() => self.clients.claim()),
  );
});

const announceUpdate = () => self.clients.matchAll().then(cs => cs.forEach(c => c.postMessage('updated')));

/* Cached copy answers instantly so the app starts offline; the network fetch refreshes it for the next launch
   and tells open pages when the content changed, so they can offer a reload instead of running stale code. */
self.addEventListener('fetch', e => {
  const req = e.request;
  if (req.method !== 'GET' || new URL(req.url).origin !== location.origin) return;
  e.respondWith(
    caches.open(CACHE).then(async cache => {
      const hit = await cache.match(req, { ignoreSearch: true });
      const fresh = fetch(req).then(async res => {
        if (res.ok) {
          const changed = hit && res.headers.get('etag') !== hit.headers.get('etag');
          await cache.put(req, res.clone());
          if (changed) announceUpdate();
        }
        return res;
      }).catch(() => null);
      if (hit) { e.waitUntil(fresh); return hit; }
      return (await fresh) || (req.mode === 'navigate' ? cache.match('index.html') : Response.error());
    }),
  );
});
