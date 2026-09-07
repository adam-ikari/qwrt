// M-P1 process-backend e2e fixture: echo received message back to parent.
onmessage = function (e) {
  postMessage(e.data);
};
