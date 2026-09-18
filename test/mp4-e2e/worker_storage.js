/* M-P4 fixture：PROCESS worker 内的 localStorage/sessionStorage 代理（§10.2
 * 单所有者）。所有操作都是同步 API——经 pal.storageSync 阻塞往返主RT 所有者
 * 执行；storageDomain 区分路由目标（localStorage / sessionStorage 独立区）。 */
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
  /* sessionStorage：独立域跨进程代理——同 key 与 localStorage 隔离 */
  sessionStorage.setItem('skey', 'sval');
  r.push('ss-get=' + sessionStorage.getItem('skey'));
  r.push('ss-len=' + sessionStorage.length);
  r.push('ss-isolated=' + sessionStorage.getItem('wkey'));  /* localStorage 的 key → null */
  try {
    sessionStorage.setItem('big', 'x'.repeat(6 * 1024 * 1024));
    r.push('ss-quota=no-throw');
  } catch (ex) { r.push('ss-quota=' + ex.name); }
  postMessage(r.join('|'));
};
