#!/usr/bin/env node
/**
 * Post-build script: generate sitemap.xml and robots.txt from VitePress dist/ output.
 */
import { readdirSync, statSync, writeFileSync, existsSync } from 'node:fs';
import { join } from 'node:path';

const BASE = 'https://adam-ikari.github.io/qwrt';
const DIST = '.vitepress/dist';

function walk(dir, base = '') {
  const results = [];
  if (!existsSync(dir)) return results;
  const entries = readdirSync(dir, { withFileTypes: true });
  for (const entry of entries) {
    const fullPath = join(dir, entry.name);
    const relPath = base ? `${base}/${entry.name}` : entry.name;
    if (entry.isDirectory() && !entry.name.startsWith('.')) {
      results.push(...walk(fullPath, relPath));
    } else if (entry.name.endsWith('.html')) {
      const url = relPath
        .replace(/\.html$/, '')
        .replace(/\/index$/, '')
        .replace(/^index$/, '');
      results.push('/' + url);
    }
  }
  return results;
}

const urls = walk(DIST);
const today = new Date().toISOString().split('T')[0];

const xml = `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9"
        xmlns:xhtml="http://www.w3.org/1999/xhtml">
${urls.map(url => {
  const isZh = url.startsWith('/zh/');
  const enUrl = isZh ? url.replace(/^\/zh/, '') : url;
  const zhUrl = '/zh' + (enUrl === '/' ? '/' : enUrl);
  const enFull = enUrl === '/' ? '' : enUrl;
  return `  <url>
    <loc>${BASE}${url}</loc>
    <lastmod>${today}</lastmod>
    <changefreq>monthly</changefreq>
    <priority>${url === '/' || url === '/zh/' ? '1.0' : url.startsWith('/guide') || url.startsWith('/zh/guide') ? '0.8' : '0.6'}</priority>
    <xhtml:link rel="alternate" hreflang="en" href="${BASE}${enFull}"/>
    <xhtml:link rel="alternate" hreflang="zh" href="${BASE}${zhUrl}"/>
  </url>`;
}).join('\n')}
</urlset>`;

writeFileSync(join(DIST, 'sitemap.xml'), xml);
console.log(`  sitemap.xml generated with ${urls.length} URLs`);

// robots.txt
writeFileSync(join(DIST, 'robots.txt'),
  `User-agent: *\nAllow: /\nSitemap: ${BASE}/sitemap.xml\n`);
console.log(`  robots.txt generated`);