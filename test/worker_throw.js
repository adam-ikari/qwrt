// Task 1 fixture: registers an echo handler, then throws at the top level.
onmessage = function (e) {
  postMessage(e.data);
};
throw new Error('boom');
