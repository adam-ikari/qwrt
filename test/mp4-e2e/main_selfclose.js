var keep = setInterval(function () {}, 50);
var w = new Worker('file://__ROOT__/test/mp4-e2e/worker_selfclose.js');
w.onerror = function (e) { console.log('SPURIOUS-ONERROR:' + (e && e.message)); };
w.onmessage = function () { console.log('WORKER-READY'); };
setTimeout(function () { console.log('SELFCLOSE-DONE'); clearInterval(keep); }, 800);
