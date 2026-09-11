---
id: decision-principles
title: "决策三原则：苏格拉底式提问、第一性原理、奥卡姆剃刀"
category: decision
status: active
tags: [decision, principles, methodology]
created: "2026-09-11T00:13:33"
updated: "2026-09-11T00:13:33"
---

<!-- compiled_truth -->
## 决策三原则（用户拍板 2026-09-11，项目级约束）

任何设计/方案/取舍讨论，必须先满足这三条约束，缺一不推进：

1. **苏格拉底式提问（先问再答）**
   - 方案提出前，先以提问收敛问题本质：场景是什么、约束是什么、目标量级是多少、可接受边界在哪。
   - 用"已知事实 vs 假设"区分证据与猜测；对未实证的假设先设计探针/测量，再谈方案。
   - 例：polyfill 启动加速——先问"加速哪个场景（首启/spawn）""2ms 里哪些可省"再落方案。

2. **第一性原理（从事实推导，不搬套路）**
   - 拆到不可再分的物理/引擎事实再重建：QuickJS 字节码 realm 绑定、ReadObject 为一次性反序列化、lazy 只省 setup 执行。
   - 禁止"业界都是这么做的""别人用了 X"式论证；每个主张须可实测或可证伪。
   - 收益必须量化到具体数值（ms/字节），不接收"更规范/更优雅"的抽象收益。

3. **奥卡姆剃刀（最小可行，删掉复杂性）**
   - 两个方案同收益取实现更小的；无法实证收益的方案直接砍。
   - 复杂度=负债：预热池（跨 realm 不可共享、驻留内存、槽位迁移）即因过复杂被否决，改为保留已实现的属性级懒加载。
   - 不建无消费者验证的抽象层，不预埋"将来可能用到"的机制。

## 适用

- 特性/架构取舍、性能优化、依赖引入、接口设计的前置讨论。
- 产出物：先提问清单 → 实测数据 → 最小方案；拒绝未经测量的方案陈述。


## Timeline

- time: 2026-09-11T00:13:33
  kind: decision
  summary: "Created this page: 决策三原则：苏格拉底式提问、第一性原理、奥卡姆剃刀"
  source: "2026-09-11 用户拍板"
  affects: [decision-principles]

- time: 2026-09-11T00:13:33
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [decision-principles]
