import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'
import { writeFileSync, mkdirSync, existsSync } from 'node:fs'
import { join, dirname } from 'node:path'

// Sitemap generation — called after each page build
const SITE_URL = 'https://adam-ikari.github.io/qzjs'
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
  name: 'Qzjs.js',
  applicationCategory: 'DeveloperApplication',
  description: 'Embeddable WinterTC runtime in C99 — WinterCG-compatible, libuv-native',
  url: SITE_URL,
  license: 'https://opensource.org/licenses/MIT',
  operatingSystem: 'Linux, macOS',
  programmingLanguage: 'C99, JavaScript (ES2023)',
  author: { '@type': 'Organization', name: 'Qzjs.js' },
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
        { text: 'Compatible Packages', link: '/guide/compatible-packages' },
        { text: 'Examples', link: '/guide/examples' },
        { text: 'Use Cases', link: '/guide/use-cases' },
      ],
    },
    {
      text: 'Host Integration',
      items: [
        { text: 'Host Integration', link: '/guide/host-integration' },
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
        { text: 'Performance Benchmarks', link: '/guide/benchmarking' },
        { text: 'Embedding Patterns', link: '/guide/embedding' },
        { text: 'Testing', link: '/dev/testing' },
        { text: 'Debugging', link: '/dev/debugging' },
        { text: 'Website Guidelines', link: '/dev/website-guidelines' },
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
        { text: 'Extensions', link: '/c-api/extensions' },
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
        { text: 'WebSocket', link: '/js-api/websocket' },
        { text: 'BroadcastChannel', link: '/js-api/broadcast-channel' },
        { text: 'EventSource', link: '/js-api/event-source' },
        { text: 'CacheStorage', link: '/js-api/cache-storage' },
        { text: 'Service Worker', link: '/js-api/service-worker' },
      ],
    },
    {
      text: 'Platform APIs',
      items: [
        { text: 'fs (Filesystem)', link: '/js-api/fs' },
        { text: 'storage', link: '/js-api/storage' },
        { text: 'navigator', link: '/js-api/navigator' },
        { text: 'serve (HTTP/WS/gRPC server)', link: '/js-api/serve' },
        { text: 'grpc', link: '/js-api/grpc' },
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

// C API reference pages exist for completeness but are NOT linked from
// navigation. C developers can find them via search or direct URL if needed.
// The website is user-facing; C API is for embedders who use the headers.

const nav = [
  { text: 'Guide', link: '/guide/' },
  { text: 'JS API', link: '/js-api/' },
  { text: 'GitHub', link: 'https://github.com/adam-ikari/qzjs' },
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
        { text: '兼容包', link: '/zh/guide/compatible-packages' },
        { text: '示例', link: '/zh/guide/examples' },
        { text: '用例', link: '/zh/guide/use-cases' },
      ],
    },
    {
      text: '主机集成',
      items: [
        { text: '主机集成', link: '/zh/guide/host-integration' },
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
        { text: '性能基准', link: '/zh/guide/benchmarking' },
        { text: '嵌入模式', link: '/zh/guide/embedding' },
        { text: '测试', link: '/zh/dev/testing' },
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
        { text: '扩展', link: '/zh/c-api/extensions' },
      ],
    },
  ],
  jsApi: [
    {
      text: 'JS API 参考',
      items: [
        { text: '概览', link: '/zh/js-api/' },
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
        { text: 'WebSocket', link: '/zh/js-api/websocket' },
        { text: 'BroadcastChannel', link: '/zh/js-api/broadcast-channel' },
        { text: 'EventSource', link: '/zh/js-api/event-source' },
        { text: 'CacheStorage', link: '/zh/js-api/cache-storage' },
        { text: 'Service Worker', link: '/zh/js-api/service-worker' },
      ],
    },
    {
      text: '平台 API',
      items: [
        { text: 'fs (文件系统)', link: '/zh/js-api/fs' },
        { text: 'storage', link: '/zh/js-api/storage' },
        { text: 'navigator', link: '/zh/js-api/navigator' },
        { text: 'serve（HTTP/WS/gRPC 服务器）', link: '/zh/js-api/serve' },
        { text: 'grpc', link: '/zh/js-api/grpc' },
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
  { text: 'JS API', link: '/zh/js-api/' },
  { text: 'GitHub', link: 'https://github.com/adam-ikari/qzjs' },
]

export default withMermaid(
defineConfig({
  title: 'Qzjs.js',
  description: 'Embeddable WinterTC Runtime — C99, WinterCG-compatible, libuv-native',
  base: '/qzjs/',
  lastUpdated: true,
  cleanUrls: true,

  head: [
    ['link', { rel: 'icon', href: '/qzjs/favicon.ico' }],
    ['meta', { name: 'theme-color', content: '#58a6ff' }],
    // Open Graph
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:title', content: 'Qzjs.js — Embeddable WinterTC Runtime' }],
    ['meta', { property: 'og:description', content: 'Embeddable WinterTC runtime in C99 — WinterCG-compatible, libuv-native' }],
    ['meta', { property: 'og:url', content: SITE_URL }],
    ['meta', { property: 'og:locale', content: 'en_US' }],
    ['meta', { property: 'og:locale:alternate', content: 'zh_CN' }],
    // Twitter Card
    ['meta', { name: 'twitter:card', content: 'summary' }],
    ['meta', { name: 'twitter:title', content: 'Qzjs.js — Embeddable WinterTC Runtime' }],
    ['meta', { name: 'twitter:description', content: 'Embeddable WinterTC runtime in C99 — WinterCG-compatible, libuv-native' }],
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
          '/js-api/': sidebar.jsApi,
        },
        outline: { level: [2, 3], label: 'On this page' },
        editLink: {
          pattern: 'https://github.com/adam-ikari/qzjs/edit/master/docs/:path',
        },
        footer: {
          message: 'MIT Licensed',
          copyright: 'Qzjs.js — Embeddable WinterTC Runtime',
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
          '/zh/js-api/': zhSidebar.jsApi,
        },
        outline: { level: [2, 3], label: '本页目录' },
        editLink: {
          pattern: 'https://github.com/adam-ikari/qzjs/edit/master/docs/:path',
        },
        footer: {
          message: 'MIT 许可证',
          copyright: 'Qzjs.js — 可嵌入的 WinterTC 运行时',
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
    siteTitle: 'Qzjs.js',
    socialLinks: [
      { icon: 'github', link: 'https://github.com/adam-ikari/qzjs' },
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