/*
 * Qzjs.js — wasm: 在 qzjs 里执行 WebAssembly
 *
 * qzjs 的定位是 JS + Wasm 双引擎。本示例把一个手写的最小 WASM 模块
 * （导出 add(i32, i32) -> i32）喂给 WebAssembly.instantiate，然后调用它。
 *
 * 运行：
 *   ./build/qzjs examples/wasm/wasm.js
 * 或（REPL 等价）：
 *   ./build/qzjs -e 'WebAssembly'           # 应打印函数
 */
(async () => {
const bytes = new Uint8Array([
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, // \0asm  version 1
  0x01, 0x07, 0x01, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f, // type: (i32,i32)->i32
  0x03, 0x02, 0x01, 0x00,                             // func: 1, type 0
  0x07, 0x07, 0x01, 0x03, 0x61, 0x64, 0x64, 0x00, 0x00, // export "add" func 0
  0x0a, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01,
  0x6a, 0x0b, // code: local.get 0, local.get 1, i32.add, end
]);

console.log('WebAssembly 可用:', typeof WebAssembly !== 'undefined');

const { instance } = await WebAssembly.instantiate(bytes);
console.log('wasm add(2, 3) =', instance.exports.add(2, 3));
console.log('wasm add(-1, 1) =', instance.exports.add(-1, 1));

// 动态实例化（从服务端拉模块也同理：fetch → arrayBuffer → instantiate）
const again = await WebAssembly.instantiate(bytes.buffer);
console.log('reinstantiate add(40, 2) =', again.instance.exports.add(40, 2));
})();
