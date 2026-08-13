// Task 4 worker fixture: echo received message back to the parent.
onmessage = function (e) {
  postMessage(e.data);
};
