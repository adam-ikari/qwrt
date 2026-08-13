// test_host.h — 新宿主契约测试桩（gtest 用）
#pragma once
#include "qwrt/qwrt.h"
#ifdef QWRT_USE_MOCK_LIBUV
#include "qwrt_internal.h"   /* mock 构建下拿到完整 qwrt_t 布局（访问 h->rt->loop） */
#endif
#include "mock_libuv.h"
#include <gtest/gtest.h>
#include <string>
#include <deque>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <atomic>

// C 串 → JSON 字符串字面量（转义反斜杠、引号、控制字符）。
static inline std::string JSON_string(const char *s) {
    std::string out = "\"";
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c;
            }
        }
    }
    out += "\"";
    return out;
}

struct HostCtx {
    qwrt_t *rt = nullptr;
    uv_mutex_t m; uv_cond_t c;
    std::deque<std::string> inbox;   /* lock-guarded message FIFO (no overwrite loss) */
    long replies = 0;                /* lock-guarded message_cb count */
};

static inline void host_msg_cb(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt;
    auto *h = (HostCtx*)data;
    uv_mutex_lock(&h->m);
    h->inbox.emplace_back(json, len);
    h->replies++;
    uv_cond_signal(&h->c);
    uv_mutex_unlock(&h->m);
}

// 标准测试引导脚本：onmessage 命令通道（eval/echo）。
// 用间接 eval（(0, eval)(...)）在全局作用域求值：直接 eval 会把顶层 var
// 声明限定在 onmessage 函数作用域内，下一次消息就丢了 —— 跨 eval 的状态
// （异步测试里 setup 写入、poll 读取的全局）必须落在 globalThis 上。
static const char *kTestBootstrap = R"JS(
globalThis.onmessage = function (e) {
  var d = e.data;
  if (d && d.cmd === 'eval') {
    try { postMessage({ok: true, v: JSON.stringify((0, eval)(d.code))}); }
    catch (err) { postMessage({ok: false, e: String(err)}); }
  } else if (d && d.cmd === 'echo') {
    postMessage(d.data);
  }
};
)JS";

static inline HostCtx *host_create(const char *script = kTestBootstrap) {
    auto *h = new HostCtx();
    uv_mutex_init(&h->m); uv_cond_init(&h->c);
    qwrt_config_t cfg = {};
    cfg.initial_script = script;
    cfg.message_cb = host_msg_cb;
    cfg.host_data = h;
    h->rt = qwrt_create(&cfg);
    if (!h->rt) { delete h; return nullptr; }
    return h;
}

static inline void host_destroy(HostCtx *h) {
    if (!h) return;
    qwrt_destroy(h->rt);
    uv_cond_destroy(&h->c); uv_mutex_destroy(&h->m);
    delete h;
}

// 等待宿主收到一条消息；返回 true 并把内容写进 out。timeout_ms 内没到则 false。
static inline bool host_wait_msg(HostCtx *h, std::string *out, int timeout_ms = 5000) {
    uv_mutex_lock(&h->m);
    while (h->inbox.empty()) {
        if (uv_cond_timedwait(&h->c, &h->m, timeout_ms) != 0) { uv_mutex_unlock(&h->m); return false; }
    }
    *out = std::move(h->inbox.front());
    h->inbox.pop_front();
    uv_mutex_unlock(&h->m);
    return true;
}

// 宿主对 qwrt 求值（经命令通道）；返回 {ok, v|e} 的原始 JSON。
static inline bool host_eval(HostCtx *h, const char *code, std::string *out, int timeout_ms = 5000) {
    std::string payload = std::string("{\"cmd\":\"eval\",\"code\":") + JSON_string(code) + "}";
    EXPECT_EQ(0, qwrt_post_message(h->rt, payload.data(), payload.size()));
    return host_wait_msg(h, out, timeout_ms);
}

// 轮询直到表达式求值结果包含 expected_substring。每次 host_eval 都会跑一轮
// loop + 冲刷微任务，所以异步结果（promise/timer/storage）在下一次 eval 可见。
// 每次未命中先睡 25ms 再计 25ms 预算：让 timeout_ms 对应真实时间，否则一轮
// 轮询只有 ~0.1ms 真实耗时，100ms 的 timer 永远等不到触发预算就耗尽。
// 返回 true 并把最后一次求值结果写入 out；timeout_ms 内未满足则 false。
static inline void host_poll_sleep(void) {
    struct timespec ts = {0, 25 * 1000000L};
    nanosleep(&ts, NULL);
}

static inline bool host_poll_until(HostCtx *h, const char *expr,
                                   const char *expected_substring,
                                   std::string *out, int timeout_ms = 5000) {
    int waited = 0;
    std::string last;
    while (waited < timeout_ms) {
        if (!host_eval(h, expr, &last, timeout_ms)) return false;
        if (last.find(expected_substring) != std::string::npos) {
            *out = last;
            return true;
        }
        host_poll_sleep();
        waited += 25;
    }
    *out = last;
    return false;
}

// 解码一个 JSON 字符串字面量（含首尾引号，如 "\"abc\"" 或 "{\"a\":1}"）
// 的内容；格式不合法则返回 false。只处理 BMP \uXXXX。
static inline bool json_unescape(const std::string &s, std::string *out) {
    if (s.size() < 2 || s.front() != '"' || s.back() != '"') return false;
    std::string r;
    for (size_t i = 1; i + 1 < s.size(); i++) {
        char c = s[i];
        if (c != '\\') { r += c; continue; }
        if (i + 1 >= s.size()) return false;
        char e = s[++i];
        switch (e) {
        case 'n': r += '\n'; break;
        case 'r': r += '\r'; break;
        case 't': r += '\t'; break;
        case 'b': r += '\b'; break;
        case 'f': r += '\f'; break;
        case '/': r += '/'; break;
        case '"': r += '"'; break;
        case '\\': r += '\\'; break;
        case 'u': {
            if (i + 4 >= s.size()) return false;
            unsigned cp = 0;
            for (int k = 1; k <= 4; k++) {
                char h = s[i + k];
                cp <<= 4;
                cp |= (h >= '0' && h <= '9') ? (unsigned)(h - '0') :
                      (h >= 'a' && h <= 'f') ? (unsigned)(h - 'a' + 10) :
                      (h >= 'A' && h <= 'F') ? (unsigned)(h - 'A' + 10) : 0;
            }
            i += 4;
            if (cp < 0x80) r += (char)cp;
            else if (cp < 0x800) {
                r += (char)(0xC0 | (cp >> 6));
                r += (char)(0x80 | (cp & 0x3F));
            } else {
                r += (char)(0xE0 | (cp >> 12));
                r += (char)(0x80 | ((cp >> 6) & 0x3F));
                r += (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default: return false;   /* 未知转义 */
        }
    }
    *out = r;
    return true;
}

// 解析 host_eval 的原始 JSON 响应，把 v 字段解码成明文写进 out。
// v 字段是经 JSON.stringify 编码的字符串（bootstrap 先 stringify 一次、
// 桥接层序列化时再转义一次），所以字符串/对象需解码两次：
//   数字/布尔 → 裸值（"3"、"true"）；
//   字符串 → 如 "\"aGVsbG8=\"" 解码为 aGVsbG8=；
//   对象 → 如 "\"{\\\"status\\\":200}\"" 解码为 {"status":200}。
// ok:false 或无 v 字段则返回 false。
static inline bool host_value(HostCtx *h, const char *code, std::string *out,
                              int timeout_ms = 5000) {
    std::string raw;
    if (!host_eval(h, code, &raw, timeout_ms)) return false;
    size_t p = raw.find("\"v\":");
    if (p == std::string::npos) return false;
    p += 4;   /* 跳过 "v": */
    if (p >= raw.size()) return false;
    if (raw[p] != '"') {
        /* 裸值：数字/true/false/null，读到下一个 , 或 } */
        size_t e = raw.find_first_of(",}", p);
        *out = raw.substr(p, (e == std::string::npos ? raw.size() : e) - p);
        return true;
    }
    /* 引号：收集完整 JSON 字符串字面量（处理转义，找配对的闭合引号） */
    std::string lit;
    lit += raw[p++];
    bool in_esc = false;
    for (; p < raw.size(); p++) {
        char c = raw[p];
        lit += c;
        if (in_esc) { in_esc = false; continue; }
        if (c == '\\') { in_esc = true; continue; }
        if (c == '"') break;   /* 闭合引号 */
    }
    std::string t;
    if (!json_unescape(lit, &t)) return false;
    /* 解码一次后仍是带引号的 JSON 字符串（底层值是 string/object），再解一次 */
    if (!t.empty() && t.front() == '"' && t.back() == '"') {
        std::string t2;
        if (json_unescape(t, &t2)) { *out = t2; return true; }
    }
    *out = t;
    return true;
}

// 轮询直到表达式求值结果（解码后的 v 值）包含 expected_substring。
// 与 host_poll_until 一样，每次未命中睡 25ms，让预算对应真实时间。
static inline bool host_poll_until_value(HostCtx *h, const char *expr,
                                         const char *expected_substring,
                                         std::string *out, int timeout_ms = 5000) {
    int waited = 0;
    std::string last;
    while (waited < timeout_ms) {
        if (!host_value(h, expr, &last, timeout_ms)) return false;
        if (last.find(expected_substring) != std::string::npos) {
            *out = last;
            return true;
        }
        host_poll_sleep();
        waited += 25;
    }
    *out = last;
    return false;
}
