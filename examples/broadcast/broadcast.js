/* qzjs example: broadcast — BroadcastChannel 跨上下文消息
 *
 * 演示 BroadcastChannel：同名的不同实例间广播（发送者实例不回投自己）。
 * 在真实场景里，实例可来自隔离的 worker / 上下文。
 *
 * 运行：
 *   ./build/qzjs examples/broadcast/broadcast.js
 */
const channel = new BroadcastChannel('jobs');

// 一个"消费者"实例：收消息
channel.onmessage = (e) => {
  console.log('消费者收到:', JSON.stringify(e.data));
  if (e.data.kind === 'shutdown') {
    console.log('消费者关闭');
    channel.close();
  }
};

// 两个"生产者"实例：发消息（不投给自己）
const p1 = new BroadcastChannel('jobs');
const p2 = new BroadcastChannel('jobs');

p1.postMessage({ kind: 'job', id: 1, payload: 'do-something' });
p2.postMessage({ kind: 'job', id: 2, payload: 'do-other' });
p1.postMessage({ kind: 'shutdown' });

// 收尾（让消息事件派发）
setTimeout(() => {
  p1.close();
  p2.close();
  console.log('done');
}, 50);
