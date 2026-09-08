/* R3/R4 fixture: echo the received value straight back.
 * Structured-clone direct pass-through — no transform, no allocation. */
onmessage = function (e) {
  postMessage(e.data);
};
