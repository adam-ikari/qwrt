// Task: localStorage 只在父 runtime 挂载；worker 内不自动挂（Web Storage
// 保守默认——worker 无 DOM 场景，且每个 worker 是独立 JSRuntime）。
// 回报 worker 上下文里 localStorage 的类型。
postMessage({ ls: String(typeof localStorage) });
