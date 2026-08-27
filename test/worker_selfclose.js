// worker_selfclose.js — 回报一条消息后调用 close() 终止自身线程。
// 覆盖 worker 自关路径（pal.workerClose → qwrt_worker_terminate）：父侧
// w.onmessage 收到 'before-close' 后，worker 线程退出；父 runtime 不受影响。
postMessage('before-close');
close();
