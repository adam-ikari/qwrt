---
title: MessageChannel
description: Qwrt.js 中的 MessageChannel API —— 结构化克隆、端口消息传递以及跨上下文通信。
---

# MessageChannel / MessagePort

WHATWG Channel Messaging API，用于执行上下文之间的通信。`MessageChannel` 创建两个连接的端口；`MessagePort` 发送和接收消息。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `MessageChannel` | 创建一对连接的 MessagePort |
| `MessagePort` | 用于发送/接收消息的端口 |

## MessageChannel

### 基本用法

```js
let channel = new MessageChannel();

// 端口 1 → 端口 2
channel.port1.addEventListener('message', (event) => {
    console.log('端口 1 收到:', event.data);
});

channel.port2.postMessage('来自端口 2 的问候！');
```

### 构造函数

```js
let channel = new MessageChannel();
// channel.port1 和 channel.port2 已连接
```

无选项——构造函数始终创建两个连接的端口。

## MessagePort

### postMessage(data)

向连接的端口发送消息：

```js
port.postMessage('简单字符串');
port.postMessage({ key: 'value' });
port.postMessage([1, 2, 3]);
```

消息被克隆（而非共享）。接收者获得一份副本。

### 事件：'message'

```js
port.addEventListener('message', (event) => {
    console.log('收到:', event.data);
    // event.data 是克隆后的消息
    // event.ports 是传输的 MessagePort 数组（在 qwrt 中始终为空）
});
```

### start()

启动端口。在 qwrt 中，端口在创建时自动启动——只有 `addEventListener` 在 `postMessage` 之后调用时才需要显式调用 `start()`。

```js
port.start();
```

### close()

关闭端口，阻止后续消息：

```js
port.close();
```

### onmessage

事件处理器属性替代方案：

```js
port.onmessage = (event) => {
    console.log('收到:', event.data);
};
```

### onmessageerror

当消息无法反序列化时调用：

```js
port.onmessageerror = (event) => {
    console.error('消息反序列化失败');
};
```

## 跨上下文通信

`MessageChannel` 的主要用例是 qwrt 上下文之间的通信（多上下文模式）：

```js
// 上下文 A
let channel = new MessageChannel();
contextB.postMessage({ port: channel.port2 }, [channel.port2]);

channel.port1.addEventListener('message', (event) => {
    console.log('来自上下文 B 的响应:', event.data);
});

// 上下文 B（在其自己的 qwrt 上下文中）
globalThis.addEventListener('message', (event) => {
    let port = event.ports[0];
    console.log('来自上下文 A 的请求:', event.data);
    port.postMessage('来自 B 的响应');
});
```

## 可转移对象

当使用 `postMessage` 的第二个参数（传输列表）时，`MessagePort` 对象的所有权被转移：

```js
// 将 port2 转移给接收者——发送者失去访问权
port1.postMessage('接收这个端口', [port2]);
// port2 现在已失效；发送者无法使用
```

只有 `MessagePort` 对象可以被转移。不支持 `ArrayBuffer` 转移。

## 注意事项

- 消息通过结构化克隆算法克隆（参见 [structuredClone](/js-api/structured-clone)）
- 不支持 `BroadcastChannel` —— 使用 `MessageChannel` 进行 1:1 通信，或构建自己的发布/订阅
- 端口在未被引用时被垃圾回收
- 收到的消息没有"origin"或"source"的概念
- `event.ports` 始终被填充（如果没有端口被转移则为空数组）