/* M-P4 fixture：PROCESS worker 内的 localStorage 代理（§10.2 单所有者）。
 * 所有操作都是同步 API——经 pal.storageSync 阻塞往返主RT 所有者执行。 */
onmessage = function () {
  var r = [];
  r.push('get=' + localStorage.getItem('k1'));        /* 主RT 先写的值 */
  localStorage.setItem('wkey', 'wval');                /* worker 写 → 主RT 应可见 */
  r.push('len=' + localStorage.length);
  r.push('key0=' + localStorage.key(0));
  r.push('missing=' + localStorage.getItem('nope'));
  try {
    localStorage.setItem('big', 'x'.repeat(6 * 1024 * 1024));
    r.push('quota=no-throw');
  } catch (ex) { r.push('quota=' + ex.name); }
  localStorage.removeItem('k1');
  r.push('after-remove=' + localStorage.getItem('k1'));
  postMessage(r.join('|'));
};
