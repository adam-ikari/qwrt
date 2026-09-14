/* M-P4 fixture：主RT 长驻（供 kill -9 主RT：宿主应感知 message_cb error）。 */
var keep = setInterval(function () {}, 100);
var w = new Worker('file://__ROOT__/test/mp4-e2e/worker_keepalive.js');
w.onmessage = function () { console.log('WORKER-READY'); };
