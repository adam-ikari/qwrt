// Task 1 fixture: registers self.onerror, then throws; self.onerror reports
// the ErrorEvent's message back to the parent.
self.onerror = function (e) {
  postMessage({ workerErr: e.message });
};
throw new Error('boom');
