/* SW-0 e2e service worker — lifecycle logs + message echo（所有场景共用） */
self.addEventListener('install', function () {
  console.log('SW install');
});
self.addEventListener('activate', function () {
  console.log('SW activate');
});
self.addEventListener('message', function (e) {
  self.postMessage('echo:' + e.data);
});
