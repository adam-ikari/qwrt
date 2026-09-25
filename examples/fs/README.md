# fs — 文件系统读写

演示 `qzjs.fs`（libuv 后端）：写文件、读回、检查存在、列目录、删除。

## 运行

```bash
./build/qzjs examples/fs/fs.js
```

## 期望输出

```
写了 2 个文件
read a.txt: "hello qzjs fs\n"
a.txt exists: true
missing exists: false
我们的文件: qzfs_a.txt, qzfs_b.log
删 b.log 后(tmp qzfs 残留): qzfs_a.txt
```

## 要点

- API 名为 `qzjs.fs.readFile` / `writeFile` / `exists` / `readdir` / `unlink`
  ——**不是** Node 的 `fs` 模块，也不存在 `require('fs')`。
- 全部 Promise 风格，可 `await`。
- 示例把文件写到 `/tmp/qzfs_*`，跑完自清理；`readdir('/tmp')` 后按前缀
  过滤，避免打印系统文件。
- 读取缺失路径会 reject，示例用 `try/catch` 吞掉首次清理。
- 二进制大文件用 `fsReadBinary`（零拷贝直写 ArrayBuffer），见
  [JS API: fs](/js-api/fs)。

## 相关文档

- [JS API: fs](/js-api/fs) — 完整签名与错误语义
