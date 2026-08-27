onmessage = function (e) {
  var task = e.data;
  try {
    if (task.type === 'countPrimes') {
      postMessage({ type: 'countPrimes', lo: task.lo, hi: task.hi, count: countPrimes(task.lo, task.hi) });
    } else {
      postMessage({ type: 'error', error: 'unknown task type: ' + task.type });
    }
  } catch (err) {
    postMessage({ type: 'error', error: String(err) + ' stack=' + (err.stack || '') });
  }
};

function countPrimes(lo, hi) {
  var n = hi;
  var sieve = new Uint8Array(n + 1);
  var count = 0;
  for (var i = 2; i <= n; i++) {
    if (!sieve[i]) {
      if (i >= lo) count++;
      for (var j = i * i; j <= n; j += i) sieve[j] = 1;
    }
  }
  return count;
}
