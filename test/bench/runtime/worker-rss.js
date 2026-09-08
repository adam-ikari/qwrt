/* R5 fixture: report own peak RSS (VmHWM) from /proc/self/status to the
 * parent. Uses pal.fsRead (async, growable read loop) — fsReadSync sizes the
 * buffer via fstat/ftell which returns 0 for procfs virtual files. VmHWM is
 * the kernel high-water mark (monotonic peak RSS), so a single read suffices.
 * NOTE: for the THREAD backend the worker shares the parent process, so this
 * reads the whole process's HWM; for PROCESS it is the qwrt-rt child's own. */
onmessage = function (e) {
  __native__.fsRead('/proc/self/status').then(function (st) {
    var m = /VmHWM:\s+(\d+) kB/.exec(st);
    postMessage({ vmhwm_kb: m ? parseInt(m[1], 10) : -1 });
  }, function () {
    postMessage({ vmhwm_kb: -1 });
  });
};