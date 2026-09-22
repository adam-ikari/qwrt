/* qzjs example: gRPC 四形态自连（unary / 服务端流式 / 客户端流式 / 双向流式）
 *
 * 单个进程里 serve() 起 gRPC 服务端（h2c 明文，免证书），再用客户端连自己，
 * 把四种 RPC 形态各跑一遍并核对结果：
 *
 *   unary         Echo     一个请求 → 一个响应
 *   服务端流式    CountUp  一个请求 → 连续 N 个响应
 *   客户端流式    Collect  连续 N 个请求 → 一个响应
 *   双向流式      Chat     连续 N 个请求 → 连续 N 个响应
 *
 * 运行（仓库根；需要 QZ_WITH_GRPC=ON 构建，example 本身不编入默认构建）：
 *   ./build_grpc/qzjs examples/grpc-hello/grpc-hello.js
 *
 * 依赖的能力（qzjs 内置）：
 *   grpc.loadProto(text)          — 解析 .proto → 方法注册表
 *   grpc.createServer()/addService — gRPC 服务端（四形态 handler）
 *   grpc.createInsecureChannel()  — 明文客户端
 *   invoke / invokeStream / invokeClientStream / invokeBidi
 */

/* 最小 proto：一个 service 四条 RPC，正好覆盖四形态 */
var PROTO = `
syntax = "proto3";
package grpchello;

message EchoRequest  { string text = 1; }
message EchoReply    { string text = 1; }
message CountRequest { int32 to = 1; }        // 数到几
message CountReply   { int32 n = 1; }         // 当前数到的值
message SumRequest   { int32 n = 1; }         // 一个加数
message SumReply     { int32 total = 1; }     // 累计和
message ChatMessage  { string text = 1; int32 seq = 2; }

service Hello {
  rpc Echo   (EchoRequest)               returns (EchoReply) {}
  rpc CountUp(CountRequest)              returns (stream CountReply) {}
  rpc Collect(stream SumRequest)         returns (SumReply) {}
  rpc Chat   (stream ChatMessage)        returns (stream ChatMessage) {}
}
`;

var PORT = 18090;

/* ── 1) 解析 proto，得到方法注册表 ── */
var reg = grpc.loadProto(PROTO);
var svc = reg.service('grpchello.Hello');
var Echo    = svc.method('Echo');
var CountUp = svc.method('CountUp');
var Collect = svc.method('Collect');
var Chat    = svc.method('Chat');

/* ── 2) 服务端：四个 handler，形态对应 addService 的四种契约 ── */
var server = grpc.createServer();
server.addService(reg, {
  /* unary：接收一个请求对象，返回一个响应对象 */
  Echo: function (call) {
    return { text: 'echo: ' + call.request.text };
  },
  /* 服务端流式：async generator，yield 的每一项是一个响应消息 */
  CountUp: async function* (call) {
    for (var i = 1; i <= call.request.to; i++) {
      yield { n: i };
    }
  },
  /* 客户端流式：call.request 是收全的请求数组，返回单个响应 */
  Collect: function (call) {
    var total = 0;
    for (var i = 0; i < call.request.length; i++) total += call.request[i].n;
    return { total: total };
  },
  /* 双向流式：收全请求数组，返回整个响应数组 */
  Chat: function (call) {
    var out = [];
    for (var i = 0; i < call.request.length; i++) {
      out.push({ text: 'reply-' + call.request[i].text, seq: call.request[i].seq });
    }
    return out;
  },
});

/* ── 3) 启动服务端 + 连自己 ── */
var srv = serve({ port: PORT, grpc: server }, function () { return 'not-grpc'; });
var ch = grpc.createInsecureChannel('127.0.0.1:' + PORT);

var pass = 0, fail = 0;
function check(label, got, want) {
  var ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) pass++; else fail++;
  console.log('[grpc] ' + (ok ? 'ok  ' : 'FAIL') + ' ' + label + '  →  ' + JSON.stringify(got));
}

/* ── 4) 四形态各跑一遍并核对 ── */
(async function () {
  /* unary：Echo */
  var echo = await ch.invoke(Echo, { text: 'hi qzjs' });
  check('unary Echo', echo, { text: 'echo: hi qzjs' });

  /* 服务端流式：CountUp，invokeStream 一次收全，期望 3 帧 1..3 */
  var up = (await ch.invokeStream(CountUp, { to: 3 })).map(function (m) { return m.n; });
  check('server-stream CountUp', up, [1, 2, 3]);

  /* 客户端流式：Collect，期望 1+2+3 = 6 */
  var sum = await ch.invokeClientStream(Collect, [{ n: 1 }, { n: 2 }, { n: 3 }]);
  check('client-stream Collect', sum, { total: 6 });

  /* 双向流式：Chat，invokeBidi 一次收全，期望 3 个请求 ↔ 3 个响应 */
  var chat = await ch.invokeBidi(Chat, [
    { text: 'a', seq: 1 }, { text: 'b', seq: 2 }, { text: 'c', seq: 3 },
  ]);
  check('bidi Chat', chat, [
    { text: 'reply-a', seq: 1 },
    { text: 'reply-b', seq: 2 },
    { text: 'reply-c', seq: 3 },
  ]);

  srv.close();
  await ch.close();
  console.log('[grpc] 四形态完成：' + pass + ' 通过 / ' + fail + ' 失败');
})();
