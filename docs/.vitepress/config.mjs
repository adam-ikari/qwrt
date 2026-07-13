import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'

// Shared sidebar/nav structure. The zh locale reuses these links (pages stay
// English until translated) but localizes the UI chrome (labels, nav text).
// To translate page content later, create docs/zh/<path>.md — VitePress will
// serve it at /zh/<path> automatically.
const sidebar = {
  guide: [
    {
      text: 'Getting Started',
      items: [
        { text: 'Overview', link: '/guide/' },
        { text: 'Quick Start', link: '/guide/quickstart' },
        { text: 'Building', link: '/guide/building' },
      ],
    },
    {
      text: 'Core Concepts',
      items: [
        { text: 'Runtime Lifecycle', link: '/guide/lifecycle' },
        { text: 'JS Execution', link: '/guide/execution' },
        { text: 'Multi-Context', link: '/guide/multi-context' },
        { text: 'Extensions', link: '/guide/extensions' },
        { text: 'Event Loop', link: '/guide/event-loop' },
      ],
    },
    {
      text: 'Advanced',
      items: [
        { text: 'Bytecode Compilation', link: '/guide/bytecode' },
        { text: 'Build Options', link: '/guide/build-options' },
        { text: 'Embedding Patterns', link: '/guide/embedding' },
      ],
    },
  ],
  pal: [
    {
      text: 'PAL Documentation',
      items: [
        { text: 'Overview', link: '/pal/' },
        { text: 'Interface Reference', link: '/pal/interface' },
        { text: 'Error Codes', link: '/pal/errors' },
        { text: 'Callback Types', link: '/pal/callbacks' },
      ],
    },
    {
      text: 'Implementing a PAL',
      items: [
        { text: 'Step-by-Step Guide', link: '/pal/implementing' },
        { text: 'Async Operations', link: '/pal/async' },
        { text: 'Streaming HTTP', link: '/pal/streaming' },
        { text: 'Shared Helpers', link: '/pal/common-helpers' },
      ],
    },
    {
      text: 'Built-in Backends',
      items: [
        { text: 'pal_uv (libuv)', link: '/pal/pal-uv' },
        { text: 'pal_mock (Testing)', link: '/pal/pal-mock' },
        { text: 'pal_freertos (ESP32)', link: '/pal/pal-freertos' },
      ],
    },
  ],
  jsApi: [
    {
      text: 'JS API Reference',
      items: [
        { text: 'Overview', link: '/js-api/' },
        { text: 'PAL Injection', link: '/js-api/pal-injection' },
      ],
    },
    {
      text: 'Web APIs',
      items: [
        { text: 'fetch', link: '/js-api/fetch' },
        { text: 'console', link: '/js-api/console' },
        { text: 'crypto', link: '/js-api/crypto' },
        { text: 'streams', link: '/js-api/streams' },
        { text: 'timers', link: '/js-api/timers' },
        { text: 'URL', link: '/js-api/url' },
        { text: 'TextEncoder / TextDecoder', link: '/js-api/encoding' },
        { text: 'AbortController', link: '/js-api/abort' },
        { text: 'performance', link: '/js-api/performance' },
      ],
    },
    {
      text: 'Platform APIs',
      items: [
        { text: 'fs (Filesystem)', link: '/js-api/fs' },
        { text: 'storage', link: '/js-api/storage' },
        { text: 'navigator', link: '/js-api/navigator' },
      ],
    },
    {
      text: 'Data & Events',
      items: [
        { text: 'Blob / File / FormData', link: '/js-api/blob' },
        { text: 'EventTarget / Event', link: '/js-api/events' },
        { text: 'MessageChannel', link: '/js-api/message-channel' },
        { text: 'structuredClone', link: '/js-api/structured-clone' },
      ],
    },
  ],
}

const nav = [
  { text: 'Guide', link: '/guide/' },
  { text: 'PAL', link: '/pal/' },
  { text: 'JS API', link: '/js-api/' },
  { text: 'GitHub', link: 'https://github.com/adam-ikari/qwrt' },
]

export default withMermaid(
defineConfig({
  title: 'Qwrt.js',
  description: 'Embeddable QuickJS-ng Runtime — C99, WinterCG-compatible, Platform Abstraction Layer',
  base: '/qwrt/',
  lastUpdated: true,
  cleanUrls: true,

  head: [
    ['link', { rel: 'icon', href: '/qwrt/favicon.ico' }],
    ['meta', { name: 'theme-color', content: '#58a6ff' }],
    ['meta', { name: 'og:type', content: 'website' }],
    ['meta', { name: 'og:title', content: 'Qwrt.js — Embeddable QuickJS Runtime' }],
    ['meta', { name: 'og:description', content: 'Embeddable QuickJS-ng runtime in C99 — WinterCG-compatible, with a Platform Abstraction Layer' }],
  ],

  locales: {
    root: {
      label: 'English',
      lang: 'en',
      themeConfig: {
        nav,
        sidebar: {
          '/guide/': sidebar.guide,
          '/pal/': sidebar.pal,
          '/js-api/': sidebar.jsApi,
        },
        outline: { level: [2, 3], label: 'On this page' },
        editLink: {
          pattern: 'https://github.com/adam-ikari/qwrt/edit/master/docs/:path',
        },
        footer: {
          message: 'MIT Licensed',
          copyright: 'Qwrt.js — Embeddable QuickJS-ng Runtime',
        },
      },
    },
    zh: {
      label: '简体中文',
      lang: 'zh',
      link: '/zh/',
      // zh page content (docs/zh/**) is not yet translated — the locale provides
      // the language switcher + localized UI chrome. Until docs/zh/ pages exist,
      // zh links fall back to the English pages above (no 404). Add docs/zh/
      // pages incrementally to translate.
      themeConfig: {
        nav,
        sidebar: {
          '/zh/guide/': sidebar.guide,
          '/zh/pal/': sidebar.pal,
          '/zh/js-api/': sidebar.jsApi,
        },
        outline: { level: [2, 3], label: '本页目录' },
        editLink: {
          pattern: 'https://github.com/adam-ikari/qwrt/edit/master/docs/:path',
        },
        footer: {
          message: 'MIT 许可证',
          copyright: 'Qwrt.js — 可嵌入的 QuickJS-ng 运行时',
        },
        lastUpdatedText: '最后更新',
        docFooter: { prev: '上一页', next: '下一页' },
        darkModeSwitchLabel: '主题',
        sidebarMenuLabel: '菜单',
        returnToTopLabel: '回到顶部',
      },
    },
  },

  themeConfig: {
    logo: false,
    siteTitle: 'Qwrt.js',
    socialLinks: [
      { icon: 'github', link: 'https://github.com/adam-ikari/qwrt' },
    ],
    search: {
      provider: 'local',
    },
  },

  markdown: {
    theme: {
      light: 'github-light',
      dark: 'github-dark',
    },
    lineNumbers: true,
  },
})
)
