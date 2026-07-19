---
layout: home

hero:
  name: "Qwrt.js"
  text: "可嵌入 QuickJS 运行时"
  tagline: C99 · WinterCG 兼容 · 平台抽象层 · 零系统依赖
  actions:
    - theme: brand
      text: 快速开始
      link: /zh/guide/
    - theme: alt
      text: JS API
      link: /zh/js-api/

features:
  - icon: ⚡
    title: 严格 C99
    details: 可嵌入任何 C99 代码库。无需 C99 之外的宿主编译器特性。
  - icon: 📦
    title: 零系统依赖
    details: QuickJS-ng、mbedTLS、miniz、libuv、WAMR — 全部通过 CMake 从源码构建。无需系统包。
  - icon: 🌐
    title: WinterCG 兼容
    details: WinterCG 兼容的 JavaScript 运行时 — 嵌入者期望的标准 Web API，预编译为字节码。
  - icon: 🔌
    title: 平台抽象层
    details: 通过精简的 PAL 合约（约 30 个函数指针）跨平台运行相同的 JS。无需修改核心即可实现自己的后端。
  - icon: 🧵
    title: 多上下文隔离
    details: 在一个运行时内创建、挂起和恢复隔离的 JS 上下文。每个上下文拥有独立的 PAL、权限和扩展状态。
  - icon: 🔒
    title: 无全局状态
    details: 零可变文件作用域状态。通过不透明的 qwrt_t 实现每运行时隔离 — 可安全地在同一进程中运行多个独立实例。
---



## 快速开始

```bash
# 克隆仓库及所有子模块
git clone --recursive https://github.com/adam-ikari/qwrt.git
cd qwrt

# 配置并构建
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // 执行 JavaScript
    char *result = NULL;
    qwrt_eval(rt, "1 + 1", &result);
    printf("1 + 1 = %s\n", result);  // "2"
    qwrt_free(result);

    // 驱动事件循环
    pal->run_cycle(pal, 100); qwrt_tick(rt);

    qwrt_destroy(rt);
    return 0;
}
```

## 架构

```mermaid
flowchart TB
    subgraph QWRT["Qwrt.js"]
        direction TB
        Core["qwrt.c (核心 API)"]
        Ctx["context.c (多上下文)"]
        Ext["extension.c (扩展注册表)"]
        Bridge["bridge.c — JS ↔ PAL 桥接"]
        Core --> Bridge
        Ctx --> Bridge
        Ext --> Bridge
        Bridge --> PAL["qwrt_pal_t (PAL 接口)"]
        PAL --> PalUV["pal_uv (libuv)"]
        PAL --> PalFR["pal_freertos (ESP-IDF)"]
        PAL --> PalMock["pal_mock (测试)"]
        JS["WinterCG 模块: fetch · console · crypto · streams · timers · …"]
        ExtList["扩展: compress · crypto · textcodec · wamr"]
        Bridge -.注入.-> JS
        Ext -.注册.-> ExtList
    end
```