/* qzjs example: fs — 文件系统读写
 *
 * 演示 qzjs.fs（libuv 后端）：写文件、读回、检查存在、列目录、删除。
 * 注意 API 名为 readFile / writeFile / exists / readdir / unlink。
 *
 * 运行：
 *   ./build/qzjs examples/fs/fs.js
 */
(async () => {
  var base = '/tmp/qzfs_';

  // 准备目录
  try { await qzjs.fs.unlink(base + 'a.txt'); } catch (e) {}
  try { await qzjs.fs.unlink(base + 'b.log'); } catch (e) {}

  // 1) 写文件
  await qzjs.fs.writeFile(base + 'a.txt', 'hello qzjs fs\n');
  await qzjs.fs.writeFile(base + 'b.log', 'log line\nsecond line\n');
  console.log('写了 2 个文件');

  // 2) 读回
  console.log('read a.txt:', JSON.stringify(await qzjs.fs.readFile(base + 'a.txt')));

  // 3) exists
  console.log('a.txt exists:', await qzjs.fs.exists(base + 'a.txt'));
  console.log('missing exists:', await qzjs.fs.exists(dir + '/nope.txt'));

  // 4) readdir 列目录
  var entries = await qzjs.fs.readdir('/tmp');
  const ours = entries.filter(n => n.startsWith('qzfs_'));
  console.log('我们的文件:', ours.join(', '));

  // 5) 删除 + 确认
  await qzjs.fs.unlink(base + 'b.log');
  console.log('删 b.log 后(tmp qzfs 残留):', (await qzjs.fs.readdir('/tmp')).join(', '));
})();
