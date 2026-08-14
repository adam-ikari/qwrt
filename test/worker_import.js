// Task 2 fixture: receives a file:// URL via message, loads it synchronously
// with importScripts, then reports the EXTRA global defined by that script.
onmessage = function (e) {
  importScripts(e.data);
  postMessage(EXTRA);
};
