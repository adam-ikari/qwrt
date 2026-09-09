/**
 * qwrt polyfill: lazy installation primitives
 *
 * 惰性注册机制（设计文档 §3.1/§3.2）：installLazy 在 globalThis 上定义
 * configurable+enumerable 的 getter。首次访问时单元 ensure 先 `delete` 该属性
 * （清掉 accessor 位，让原 setup 内部的 `globalThis.x = v` 在严格模式下正常落
 * 数据属性），再跑原有 setupXxx 原样执行；此后属性稳定为数据属性，descriptor
 * 与 eager 安装结果逐位一致。JS 消费者无法观察到 lazy 与 eager 的差异。
 *
 * 关键设计点：
 *   - getter 不负责 delete 自身——由单元 ensure 统一清整个单元的 accessor 面。
 *     否则多名单元（fetch/streams/caches…）里"第二名的二次访问"会把 setup
 *     产出的数据属性误删（见 §3.1 单位多面）。
 *   - setter 语义对齐 eager 赋值：strict 模式下对"无 setter accessor"赋值会抛
 *     TypeError，eager 的 data property 赋值则不抛。setter 先 ensure() 把单元
 *     物化为数据属性，再落值——monkey-patch（`globalThis.fetch = custom`）行为
 *     与 eager 一致；对 non-writable 属性（localStorage）赋值照样抛 TypeError，
 *     也与 eager 一致。
 *   - once 的 done 标志在 setup 前置位：级联 setup 中途重入同单元 getter 时
 *     直接短路，不二次 setup、不死循环。
 *
 * 单元（lazyUnit）：一组全局名 + 一组宿主对象子属性（[[obj, prop], …]）共享
 * 同一 ensure 与 done 标志。首次触发先把整个单元的 accessor 面清掉（含未触发
 * 的兄弟名），再跑 setup，保证 setup 内对兄弟名的赋值直接落数据属性。
 */

/* 单元 ensure：首次调用执行 setup；重复调用短路。done 在 setup 前置位——
 * 级联 setup 中途重入同单元 getter 时直接短路，不会二次 setup 或死循环。 */
function once(fn) {
  var done = false;
  return function () {
    if (done) return;
    done = true;
    fn();
  };
}

/* 顶层全局惰性 getter。 */
export function installLazy(name, ensure) {
  Object.defineProperty(globalThis, name, {
    configurable: true,
    enumerable: true,
    get() {
      ensure();
      return globalThis[name];
    },
    set(v) {
      ensure();
      globalThis[name] = v;
    },
  });
}

/* 宿主对象子属性惰性 getter（qwrt.fs / navigator.serviceWorker / crypto.subtle）。 */
export function installLazyProp(obj, prop, ensure) {
  Object.defineProperty(obj, prop, {
    configurable: true,
    enumerable: true,
    get() {
      ensure();
      return obj[prop];
    },
    set(v) {
      ensure();
      obj[prop] = v;
    },
  });
}

/* 单元注册器：globalNames 顶层全局 + props [[obj, prop], ...] 子属性共享 ensure。
 * 首次触发：清掉整单元 accessor 面 → setup 原样执行。返回 ensure 供其他单元
 * 级联（如 fetch→streams/blob、worker→message-channel）。 */
export function lazyUnit(globalNames, props, setup) {
  var ensure = once(function () {
    for (var i = 0; i < globalNames.length; i++) delete globalThis[globalNames[i]];
    for (var j = 0; j < props.length; j++) delete props[j][0][props[j][1]];
    setup();
  });
  for (var k = 0; k < globalNames.length; k++) installLazy(globalNames[k], ensure);
  for (var m = 0; m < props.length; m++) installLazyProp(props[m][0], props[m][1], ensure);
  return ensure;
}
