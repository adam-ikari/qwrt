// Qwrt worker 脚本：把收到的消息原样回传给父线程。
onmessage = function (e) {
  postMessage(e.data);
};
