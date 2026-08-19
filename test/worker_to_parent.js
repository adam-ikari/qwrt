// transferable fixture (worker→parent): on request, create a MessageChannel
// and transfer port1 to the parent via postMessage(data, [port1]); keep port2
// locally and echo any message received on it back with an 'echo:' prefix.
// Also report the detached state of the original port1 (transfer semantics).
// Errors are reported back so gtest can distinguish a thrown DataCloneError /
// routing failure from a working transfer.
onmessage = function (e) {
  try {
    var ch = new MessageChannel();
    ch.port2.onmessage = function (e2) {
      ch.port2.postMessage('echo:' + e2.data);
    };
    postMessage({ ready: true }, [ch.port1]);
    postMessage({ detached: ch.port1._detached ? true : false });
  } catch (err) {
    postMessage({ err: String(err && err.name || err) });
  }
};
