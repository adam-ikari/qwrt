import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'qwrt',
  description: 'Embeddable QuickJS-ng Runtime — C99, WinterCG polyfill, Platform Abstraction Layer',
  lang: 'en-US',
  base: '/qwrt/',
  lastUpdated: true,
  cleanUrls: true,

  head: [
    ['link', { rel: 'icon', href: '/qwrt/favicon.ico' }],
    ['meta', { name: 'theme-color', content: '#58a6ff' }],
    ['meta', { name: 'og:type', content: 'website' }],
    ['meta', { name: 'og:title', content: 'qwrt — Embeddable QuickJS Runtime' }],
    ['meta', { name: 'og:description', content: 'Embeddable QuickJS-ng runtime wrapper in C99 with WinterCG polyfill and Platform Abstraction Layer' }],
  ],

  themeConfig: {
    logo: false,
    siteTitle: 'qwrt',

    nav: [
      { text: 'Guide', link: '/guide/' },
      { text: 'PAL', link: '/pal/' },
      { text: 'JS API', link: '/js-api/' },
      { text: 'GitHub', link: 'https://github.com/adam-ikari/qwrt' },
    ],

    sidebar: {
      '/guide/': [
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
      '/pal/': [
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
      '/js-api/': [
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
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/adam-ikari/qwrt' },
    ],

    search: {
      provider: 'local',
    },

    footer: {
      message: 'MIT Licensed',
      copyright: 'qwrt — Embeddable QuickJS-ng Runtime',
    },

    editLink: {
      pattern: 'https://github.com/adam-ikari/qwrt/edit/master/docs/:path',
    },

    outline: {
      level: [2, 3],
      label: 'On this page',
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
