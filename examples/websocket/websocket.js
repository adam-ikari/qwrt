/* qzjs example: websocket — WebSocket echo 服务器
 *
 * 演示 serve() 的 ws 路由：/echo 升级为 WebSocket，onmessage 回显。
 * 纯 JS 应用层实现 WebSocket 协议（qzjs 只提供监听 + 升级 + 收发帧）。
 *
 * 运行（起服务器）：
 *   ./build/qzjs examples/websocket/websocket.js
 * 另开终端用 node 客户端连：
 *   node -e 'const W=require("ws");const w=new W("ws://127.0.0.1:19000/echo");w.on("open",()=>w.send("hello"));w.on("message",d=>{console.log("echo:",d.toString());process.exit(0)})'
 */
const PORT = 19000;

serve({
  port: PORT,
  ws: {
    '/echo': (conn) => {
      conn.onopen = () => console.log('client connected /echo');
      conn.onmessage = (ev) => {
        // text 帧数据是字符串；binary 帧是 Uint8Array
        const text = typeof ev.data === 'string' ? ev.data : 'binary(' + ev.data.byteLength + 'b)';
        console.log('  收到:', text);
        conn.send('echo: ' + text);
      };
      conn.onclose = () => console.log('client disconnected');
      conn.onerror = (e) => console.log('ws error');
    },
  },
}, (req) => 'HTTP 请求命中非 ws 路径');
console.log('WebSocket echo server @ ws://127.0.0.1:' + PORT + '/echo');
