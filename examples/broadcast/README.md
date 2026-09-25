# broadcast — BroadcastChannel 跨上下文消息

演示 `BroadcastChannel`：同名频道的不同实例间广播，**发送者实例不回投自己**。
真实场景里这些实例可来自隔离的 worker 或上下文。

## 运行

```bash
./build/qzjs examples/broadcast/broadcast.js
```

## 期望输出

```
消费者收到: {"kind":"job","id":1,"payload":"do-something"}
消费者收到: {"kind":"job","id":2,"payload":"do-other"}
消费者收到: {"kind":"shutdown"}
消费者关闭
done
```

## 要点

- 一个"消费者"实例挂 `onmessage`，两个"生产者"实例 `postMessage`——
  消费者收到两条 job，而生产者自己不回投，所以只出现两行 job。
- `channel.close()` 后不再收消息；示例在 `shutdown` 时关闭。
- 消息走结构化克隆，按值传递。
- 跨实例（含 worker）通信也用它；同进程内是共享内存传递，无序列化开销。

## 相关文档

- [JS API: BroadcastChannel](/js-api/broadcast-channel)
- [JS API: MessageChannel](/js-api/message-channel) — 点对点通道
