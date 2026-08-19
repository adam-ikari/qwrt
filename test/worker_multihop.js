// transferable multihop fixture: receive a MessagePort transferred from the
// parent (event.ports[0]), then forward that same port back to the parent via
// postMessage(data, [port]) — a second hop (parent → worker → parent).
// The parent side must rebuild the same-thread entanglement with its local
// port2 so it can echo locally again. Errors are reported back so gtest can
// distinguish a routing/entanglement failure from a working multihop.
onmessage = function (e) {
  try {
    var port = e.ports && e.ports[0];
    if (!port) {
      postMessage({ err: 'no port in event.ports' });
      return;
    }
    postMessage('forwarding', [port]);
  } catch (err) {
    postMessage({ err: String(err && err.name || err) });
  }
};
