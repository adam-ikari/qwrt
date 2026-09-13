---
id: code-quality-requirements
title: "代码质量硬性要求：可读性 + DRY（项目级）"
category: decision
status: active
tags: [code-quality, readability, dry, review]
created: "2026-09-13T12:41:26"
updated: "2026-09-13T12:41:42"
---

<!-- compiled_truth -->
## 代码质量硬性要求（项目级，用户 2026-09-13 追加，对全部产出生效）

两条要求是 DIRECTOR 的显式要求，不是可选项；适用于新增代码与顺手触及的既有代码。

### 1. 可读性

- **诊断/错误消息自解释**：打印或抛出时必须自带定位与差异上下文——文件/路径、期望 vs 实际、触发条件。禁止「校验失败」「invalid input」这类裸话。
- **禁魔法数字**：`-4`、`32`、`64`、`0xFFFFFFFF` 之类裸字面量改用命名常量/枚举/宏（编译期常量亦可），名字说明语义。
- **标识符自描述**：变量/函数名表达意图，不用单字符或含义不明的缩写。
- **注释解释「为什么」**：记录设计约束、踩过的坑、权衡取舍；不复述「这行做了什么」。
- **函数短、单一职责**：避免深嵌套；嵌套过深即拆分或提前返回。

### 2. DRY（Don't Repeat Yourself）

- **不 copy-paste**：重复出现的逻辑、常量、样板抽成公共 helper / 命名常量 / 宏，或复用仓库既有实现（错误码枚举、既有工具函数、既有测试隔离模式皆优先于新建平行实现）。
- **单一事实来源**：一处定义、多处引用；改了定义即处处生效。
- **拒绝过度抽象**：不得为「将来可能有重复」造层——只有一个调用点的包装、无实际重复的通用化属 YAGNI 反例；确需引入时说明理由，否则回退。

### 落地方式

- 提交前自查两条：有未命名的裸数字、裸诊断消息、复制粘贴的 setup/断言，即属未达标。
- 每轮代码交付报告包含「可读性自查 / DRY 自查」小节，列出为达标所做的调整（含「为何该处不抽象」的理由）。
- 与既有 `decision-principles`（苏格拉底式提问 / 第一性原理 / 奥卡姆剃刀）配合：奥卡姆剃刀管「要不要做」，本页管「做完的代码长什么样」。


## Timeline

- time: 2026-09-13T12:41:26
  kind: decision
  summary: "Created this page: 代码质量硬性要求：可读性 + DRY（项目级）"
  source: "2026-09-13 用户/DIRECTOR 项目级代码要求"
  affects: [code-quality-requirements]

- time: 2026-09-13T12:41:42
  kind: decision
  summary: "写入两条项目级代码硬性要求：可读性（自解释诊断 / 禁魔法数字 / 自描述标识符 / 注释讲为什么 / 函数短单一职责）与 DRY（抽公共件与单一事实来源，拒绝无实际重复的过度抽象）"
  source: "2026-09-13 用户/DIRECTOR 追加要求"
  affects: [code-quality-requirements]

- time: 2026-09-13T12:41:42
  kind: decision
  summary: "用户追加两条项目级代码质量硬性要求（可读性 + DRY），要求适用于全部产出并在报告中附自查小节；本轮 polyfill external 测试/收尾即按此标准产出（命名常量 kTamperFlipMask/kMinPlausibleBytecodeBytes、断言带路径与期望/实际、helper host_read_file/host_write_file 与 load_polyfill_at 抽公共件、删除 test_suspend_gtest 的 read_file_bytes 副本）"
  source: "2026-09-13 用户/DIRECTOR 追加要求"
  affects: [code-quality-requirements]
