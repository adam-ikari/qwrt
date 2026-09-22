/*
 * Qzjs.js — crypto: WebCrypto crypto.subtle 加解密 + 摘要
 *
 * 演示 WinterTC 兼容的 crypto.subtle（mbedTLS 后端）：
 *   - SHA-256 摘要
 *   - AES-GCM 对称加解密（随机 key + IV）
 *
 * 运行：
 *   ./build/qzjs examples/crypto/crypto.js
 */
(async () => {
const enc = new TextEncoder();

// 1) SHA-256 摘要
const digest = await crypto.subtle.digest('SHA-256', enc.encode('hello qzjs'));
console.log('SHA-256(hello qzjs) =', [...new Uint8Array(digest)]
  .map(b => b.toString(16).padStart(2, '0')).join('').slice(0, 32) + '…');

// 2) AES-GCM 对称加解密
const key = await crypto.subtle.generateKey(
  { name: 'AES-GCM', length: 256 }, true, ['encrypt', 'decrypt']);
const iv = crypto.getRandomValues(new Uint8Array(12));
const plaintext = enc.encode('secret message from qzjs');

const cipher = await crypto.subtle.encrypt(
  { name: 'AES-GCM', iv }, key, plaintext);
console.log('AES-GCM 密文长度:', cipher.byteLength, 'bytes');

const decrypted = await crypto.subtle.decrypt(
  { name: 'AES-GCM', iv }, key, cipher);
console.log('解密回文:', new TextDecoder().decode(decrypted));

// 3) 导出/导入 key（可序列化到宿主）
const exported = await crypto.subtle.exportKey('raw', key);
console.log('key 导出长度:', exported.byteLength, 'bytes');
})();
