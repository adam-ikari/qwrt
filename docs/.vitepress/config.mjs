import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'
import { writeFileSync, mkdirSync, existsSync } from 'node:fs'
import { join, dirname } from 'node:path'

// Sitemap generation — called after each page build
const SITE_URL = 'https://adam-ikari.github.io/qwrt'
const sitemapUrls = new Set()

function generateSitemap(outDir) {
  const today = new Date().toISOString().split('T')[0]
  const sorted = [...sitemapUrls].sort()
  const xml = `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"
        xmlns:xhtml="http://www.w3.org/1999/xhtml">
${sorted.map(url => {
  const isZh = url.startsWith('/zh/')
  const enUrl = isZh ? url.replace('/zh/', '/') : url
  const zhUrl = isZh ? url : '/zh' + url
  return `  <url>
    <loc>${SITE_URL}${url}</loc>
    <lastmod>${today}</lastmod>
    <changefreq>monthly</changefreq>
    <priority>${url === '/' || url === '/zh/' ? '1.0' : url.startsWith('/guide') || url.startsWith('/zh/guide') ? '0.8' : '0.6'}</priority>
    <xhtml:link rel="alternate" hreflang="en" href="${SITE_URL}${enUrl === '/' ? '' : enUrl}"/>
    <xhtml:link rel="alternate" hreflang="zh" href="${SITE_URL}${zhUrl}"/>
  </url>`;
}).join('\n')}
</urlset>`
  writeFileSync(join(outDir, 'sitemap.xml'), xml)
  writeFileSync(join(outDir, 'robots.txt'), `User-agent: *\nAllow: /\nSitemap: ${SITE_URL}/sitemap.xml\n`)
  console.log(`  sitemap.xml generated (${sorted.length} URLs)`)
}

// Schema.org WebApplication JSON-LD for homepage
const SCHEMA_LD = JSON.stringify({
  '@context': 'https://schema.org',
  '@type': 'SoftwareApplication',
  name: 'Qwrt.js',
  applicationCategory: 'DeveloperApplication',
  description: 'Embeddable QuickJS-ng runtime in C99 — WinterCG-compatible, with a Platform Abstraction Layer',
  url: SITE_URL,
  license: 'https://opensource.org/licenses/MIT',
  operatingSystem: 'Linux, macOS, ESP32-S3',
  programmingLanguage: 'C99, JavaScript (ES2023)',
  author: { '@type': 'Organization', name: 'Qwrt.js' },
})

// Navigation structure
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
        { text: 'Debugging', link: '/dev/debugging' },
      ],
    },
  ],
  cApi: [
    {
      text: 'C API Reference',
      items: [
        { text: 'Overview', link: '/c-api/' },
        { text: 'Runtime Lifecycle', link: '/c-api/runtime' },
        { text: 'JS Evaluation', link: '/c-api/eval' },
        { text: 'PAL Interface', link: '/c-api/pal' },
        { text: 'Extensions', link: '/c-api/extensions' },
        { text: 'Error Codes', link: '/c-api/errors' },
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
  { text: 'C API', link: '/c-api/' },
  { text: 'PAL', link: '/pal/' },
  { text: 'JS API', link: '/js-api/' },
  { text: 'Playground', link: '/playground' },
  { text: 'GitHub', link: 'https://github.com/adam-ikari/qwrt' },
]

// Chinese sidebar with translated labels
const zhSidebar = {
  guide: [
    {
      text: '快速开始',
      items: [
        { text: '概览', link: '/zh/guide/' },
        { text: '快速上手', link: '/zh/guide/quickstart' },
        { text: '构建', link: '/zh/guide/building' },
      ],
    },
    {
      text: '核心概念',
      items: [
        { text: '运行时生命周期', link: '/zh/guide/lifecycle' },
        { text: 'JS 执行', link: '/zh/guide/execution' },
        { text: '多上下文', link: '/zh/guide/multi-context' },
        { text: '扩展', link: '/zh/guide/extensions' },
        { text: '事件循环', link: '/zh/guide/event-loop' },
      ],
    },
    {
      text: '高级主题',
      items: [
        { text: '字节码编译', link: '/zh/guide/bytecode' },
        { text: '构建选项', link: '/zh/guide/build-options' },
        { text: '嵌入模式', link: '/zh/guide/embedding' },
        { text: '调试', link: '/zh/dev/debugging' },
      ],
    },
  ],
  cApi: [
    {
      text: 'C API 参考',
      items: [
        { text: '概览', link: '/zh/c-api/' },
        { text: '运行时生命周期', link: '/zh/c-api/runtime' },
        { text: 'JS 求值', link: '/zh/c-api/eval' },
        { text: 'PAL 接口', link: '/zh/c-api/pal' },
        { text: '扩展', link: '/zh/c-api/extensions' },
        { text: '错误码', link: '/zh/c-api/errors' },
      ],
    },
  ],
  pal: [
    {
      text: 'PAL 文档',
      items: [
        { text: '概览', link: '/zh/pal/' },
        { text: '接口参考', link: '/zh/pal/interface' },
        { text: '错误码', link: '/zh/pal/errors' },
        { text: '回调类型', link: '/zh/pal/callbacks' },
      ],
    },
    {
      text: '实现 PAL',
      items: [
        { text: '分步指南', link: '/zh/pal/implementing' },
        { text: '异步操作', link: '/zh/pal/async' },
        { text: '流式 HTTP', link: '/zh/pal/streaming' },
        { text: '共享辅助函数', link: '/zh/pal/common-helpers' },
      ],
    },
    {
      text: '内置后端',
      items: [
        { text: 'pal_uv (libuv)', link: '/zh/pal/pal-uv' },
        { text: 'pal_mock (测试)', link: '/zh/pal/pal-mock' },
        { text: 'pal_freertos (ESP32)', link: '/zh/pal/pal-freertos' },
      ],
    },
  ],
  jsApi: [
    {
      text: 'JS API 参考',
      items: [
        { text: '概览', link: '/zh/js-api/' },
        { text: 'PAL 注入', link: '/zh/js-api/pal-injection' },
      ],
    },
    {
      text: 'Web API',
      items: [
        { text: 'fetch', link: '/zh/js-api/fetch' },
        { text: 'console', link: '/zh/js-api/console' },
        { text: 'crypto', link: '/zh/js-api/crypto' },
        { text: 'streams', link: '/zh/js-api/streams' },
        { text: 'timers', link: '/zh/js-api/timers' },
        { text: 'URL', link: '/zh/js-api/url' },
        { text: 'TextEncoder / TextDecoder', link: '/zh/js-api/encoding' },
        { text: 'AbortController', link: '/zh/js-api/abort' },
        { text: 'performance', link: '/zh/js-api/performance' },
      ],
    },
    {
      text: '平台 API',
      items: [
        { text: 'fs (文件系统)', link: '/zh/js-api/fs' },
        { text: 'storage', link: '/zh/js-api/storage' },
        { text: 'navigator', link: '/zh/js-api/navigator' },
      ],
    },
    {
      text: '数据与事件',
      items: [
        { text: 'Blob / File / FormData', link: '/zh/js-api/blob' },
        { text: 'EventTarget / Event', link: '/zh/js-api/events' },
        { text: 'MessageChannel', link: '/zh/js-api/message-channel' },
        { text: 'structuredClone', link: '/zh/js-api/structured-clone' },
      ],
    },
  ],
}

const zhNav = [
  { text: '指南', link: '/zh/guide/' },
  { text: 'C API', link: '/zh/c-api/' },
  { text: 'PAL', link: '/zh/pal/' },
  { text: 'JS API', link: '/zh/js-api/' },
  { text: '演练场', link: '/zh/playground' },
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
    // Open Graph
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:title', content: 'Qwrt.js — Embeddable QuickJS Runtime' }],
    ['meta', { property: 'og:description', content: 'Embeddable QuickJS-ng runtime in C99 — WinterCG-compatible, with a Platform Abstraction Layer. Build once, run anywhere.' }],
    ['meta', { property: 'og:url', content: SITE_URL }],
    ['meta', { property: 'og:locale', content: 'en_US' }],
    ['meta', { property: 'og:locale:alternate', content: 'zh_CN' }],
    // Twitter Card
    ['meta', { name: 'twitter:card', content: 'summary' }],
    ['meta', { name: 'twitter:title', content: 'Qwrt.js — Embeddable QuickJS Runtime' }],
    ['meta', { name: 'twitter:description', content: 'Embeddable QuickJS-ng runtime in C99 — WinterCG-compatible, with a Platform Abstraction Layer' }],
  ],

  // Collect URLs for sitemap during build
  transformHtml(code, id) {
    if (id.endsWith('.html')) {
      const path = id
        .replace(/\.html$/, '')
        .replace(/index$/, '')
        .replace(new RegExp(`^.*?\\.vitepress/dist`), '')
      sitemapUrls.add(path || '/')
    }
  },

  locales: {
    root: {
      label: 'English',
      lang: 'en',
      themeConfig: {
        nav,
        sidebar: {
          '/guide/': sidebar.guide,
          '/c-api/': sidebar.cApi,
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
      themeConfig: {
        nav: zhNav,
        sidebar: {
          '/zh/guide/': zhSidebar.guide,
          '/zh/c-api/': zhSidebar.cApi,
          '/zh/pal/': zhSidebar.pal,
          '/zh/js-api/': zhSidebar.jsApi,
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
        selectLanguageText: '语言',
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
      options: {
        locales: {
          zh: {
            translations: {
              button: { buttonText: '搜索', buttonAriaLabel: '搜索' },
              modal: {
                displayDetails: '显示详情',
                resetButtonTitle: '重置',
                backButtonTitle: '返回',
                noResultsText: '无结果',
                footer: { selectText: '选择', navigateText: '切换' },
              },
            },
          },
        },
      },
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