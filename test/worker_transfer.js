// transferable fixture: on request, build an ArrayBuffer, transfer it to the
// parent, then report the detached length plus the (pre-transfer) contents via
// a second message. Errors are reported back so gtest can distinguish a thrown
// DataCloneError from a routing failure.
onmessage = function (e) {
  try {
    var ab = new ArrayBuffer(4);
    var u8 = new Uint8Array(ab);
    u8.set([7, 8, 9, 10]);
    var contents = Array.prototype.join.call(u8, ',');
    postMessage({ buf: ab }, [ab]);
    postMessage({ after: ab.byteLength, contents: contents });
  } catch (err) {
    postMessage({ err: String(err && err.name || err) });
  }
};
