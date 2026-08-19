// transferable fixture: receive a MessagePort via event.ports[0], confirm it,
// then echo any message sent through it back with an 'echo:' prefix.
onmessage = function (e) {
  var port = e.ports && e.ports[0];
  if (!port) {
    postMessage({ err: 'no port in event.ports' });
    return;
  }
  port.onmessage = function (e2) {
    port.postMessage('echo:' + e2.data);
  };
  port.postMessage('ready');
};
