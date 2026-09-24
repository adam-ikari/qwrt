---
id: brand-display-name
title: "品牌显示名 Qz.js 与内部标识 qzjs"
category: decision
status: active
tags: [brand, docs]
created: "2026-09-23T05:49:50"
updated: "2026-09-24T00:20:36"
---

<!-- compiled_truth -->
Qz.js 品牌显示名 = **Qz.js**（内部标识 qzjs）。**Quick + Zero** 双关：QuickJS 内核 + 零依赖/零配置定位。

## Logo / 标识

- **主标识：排印式字标**。从编程字体 Maple Mono ExtraBold 提取真实字形轮廓，非几何拼图。
  - Wordmark: `Qz.js` — Q 深蓝 #1a3a5c，z 亮蓝 #58a6ff（Zero 强调色），`.js` 深蓝。
  - Monogram: 紧排 `Qz`（favicon / 图标）。
  - Ligature 变体: z 作为 Q 的尾巴，单字形同时是 Q 与 z（备选，站点未用）。
- **设计约束（已否决的方向）**：不得使用「圆环 + 直柄」几何组合做 Q——必然读作放大镜/搜索图标。v1/v2 各 4 个此类概念均被否决。
- **双调色板（明/暗）**：navy 半#1a3a5c 在暗背景不可读（1.6:1 对 #0d1117），故每个 mark 都有明暗两版。
  - light: Q #1a3a5c (11.6:1 对白) + z #58a6ff
  - dark:  Q #c9d9ec (13.2:1 对 #0d1117) + z #58a6ff
  - VitePress logo/hero 用 `{ light, dark }` 切换；favicon 用 `prefers-color-scheme` 双 link。
- **源头生成**：单一生成器 `logo/gen.py`（fontTools 实时提取字形，非手抄 path），配 FONT_CANDIDATES 探测字体。
- **交付物（单一来源源在 logo/，站点副本在 docs/public/）**：
  - `logo/`：gen.py + logo*.svg（wordmark/mono/square × light+dark）+ icon-logo-{16..512}.png ×2 + favicon.ico/favicon-dark.ico + og-image.png（1200x630 社交卡）
  - `docs/public/`：站点实际引用的 logo.svg / logo-dark.svg / favicon.ico / favicon-dark.ico / icon-logo-{32,256}.png / og-image.png
  - `logo-design/`：早期原型目录（含 ligature 变体与 preview.html），作设计历史保留，非源头。
  - 站点接入：themeConfig.logo、两语言首页 hero.image、README <picture> 双 palette、head OG/twitter/image + 双主题色。


## Timeline

- time: 2026-09-23T05:49:50
  kind: decision
  summary: "Created this page: 品牌显示名 Qz.js 与内部标识 qzjs"
  source: created via brain create-page
  affects: [brand-display-name]

- time: 2026-09-23T08:20:28
  kind: decision
  summary: "追加 logo/标识决策：排印式字标 + 否决放大镜感几何方案"
  source: logo design session
  affects: [brand-display-name]

- time: 2026-09-24T00:20:36
  kind: decision
  summary: "Logo 交付物迁至 logo/ 生成器 + 新增暗色调色板与社交卡"
  source: logo site integration
  affects: [brand-display-name]
