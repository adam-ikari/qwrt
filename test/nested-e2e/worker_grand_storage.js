/* 嵌套 storage e2e：孙 worker。经两级中继使用 localStorage（owner = 主RT）。 */
onmessage = function () {
  var r = [];
  r.push('g-get=' + localStorage.getItem('gk1'));   /* 主RT owner 先写 */
  localStorage.setItem('gkey', 'gval');             /* 孙写 → 主RT 应可见 */
  r.push('g-len=' + localStorage.length);
  postMessage(r.join('|'));
};
