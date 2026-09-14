/* M-P4 fixture：长驻 worker（供 kill -9 崩溃注入）。 */
postMessage('ready');
setInterval(function () {}, 100);
