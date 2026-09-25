# wasm — 在 qzjs 里执行 WebAssembly

qzjs 是 JS + Wasm 双引擎。示例把一个手写的最小 WASM 模块（导出
`add(i32, i32) -> i32`）喂给 `WebAssembly.instantiate` 并调用它。

## 运行

```bash
./build/qzjs examples/wasm/wasm.js
```

## 期望输出

```
WebAssembly 可用: true
wasm add(2, 3) = 5
wasm add(-1, 1) = 0
reinstantiate add(40, 2) = 42
```

## 要点

- 字节数组就是完整的 `.wasm` 二进制——`\0asm` 魔数 + version 1，后跟
  type / func / export / code 四个 section。读一遍能建立对 WASM 二进制的直觉。
- 两种实例化形式都演示了：`instantiate(bytes)`（ArrayBuffer 视图）与
  `instantiate(bytes.buffer)`（裸 ArrayBuffer）。
- 从网络拉模块同理：`fetch(url)` → `arrayBuffer()` → `instantiate`；流式
  版本是 `instantiateStreaming`（见 [guide/wasm](/guide/build-options) 的引擎开关）。
- 引擎在构建期选择：WAMR（默认，Fast Interp + AOT，懒加载）或 wasm3，
  互斥编译。

## 相关文档

- [Build Options](/guide/build-options) — `QZ_WITH_WASM3` 等引擎开关
