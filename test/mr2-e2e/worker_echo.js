// M-R2 composition e2e worker fixture (PROCESS backend): echo received message.
onmessage = function (e) {
  postMessage(e.data);
};
