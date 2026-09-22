---
title: BroadcastChannel
description: Amoib.js 的 BroadcastChannel API —— 单一运行时内跨上下文的消息广播。
---

# BroadcastChannel

一个带名字的消息通道，向所有同名 `BroadcastChannel` 实例广播。
用来协调同一个 amoib 运行时里多个派生上下文的工作。

## 全局

| Global | 类型 |
|--------|------|
| `BroadcastChannel` | `class` |

## API

```js
const a = new BroadcastChannel('work');
const b = new BroadcastChannel('work');

a.onmessage = (ev) => console.log('a got', ev.data);
b.postMessage({ task: 'sync' });   // a 会收到
```

| 成员 | 说明 |
|--------|------|
| `new BroadcastChannel(name)` | 订阅命名通道 |
| `postMessage(data)` | 向同名其它订阅者广播 |
| `onmessage` | 收到广播时调用，参数 `{ data }` |
| `close()` | 退订；之后不再投递消息 |

## 说明

- 广播不会回环给发送者。
- 消息需可 JSON 序列化；函数与代理对象跨通道不受支持。
