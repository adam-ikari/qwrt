#!/usr/bin/env python3
"""Generate the Qz.js brand marks from real glyph outlines.

Typographic mark: glyphs are extracted from Maple Mono ExtraBold (not traced,
not geometrically reconstructed) so the logo shares the project's mono typeface.

Mark family
-----------
wordmark   "Qz.js"  - Q navy, z accent, ".js" navy. Site headers, README, slides.
monogram   "Qz"     - tight pair, no dot. Wide layouts where "Qz.js" is too long.
square     monogram centered in a square viewBox. Nav logo, avatars, OG cards.
favicon    square rasterized to 16/32/48 .ico plus PNG icon sizes.

Each mark ships in two palettes, because the navy half of the mark is
unreadable on dark backgrounds (1.6:1 against VitePress #0d1117):

  light   Q #1a3a5c (11.6:1 on white)   z #58a6ff
  dark    Q #c9d9ec (13.2:1 on #0d1117) z #58a6ff

Run from the repo root:  python3 logo/gen.py
Writes sources next to this script and the shipped copies into docs/public/.
"""

import os

from fontTools.misc.transform import Transform
from fontTools.pens.basePen import BasePen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
PUBLIC = os.path.join(HERE, os.pardir, 'docs', 'public')

FONT_CANDIDATES = [
    os.path.expanduser('~/.local/share/fonts/MapleMonoNL-NF-CN-ExtraBold.ttf'),
    '/usr/share/fonts/truetype/maple-mono/MapleMono-ExtraBold.ttf',
]

LIGHT = {'Q': '#1a3a5c', 'z': '#58a6ff'}
DARK = {'Q': '#c9d9ec', 'z': '#58a6ff'}

# Q->z tightened 8%: the default advance pair reads loose at display sizes.
KERN_QZ = 0.92
# Q->z tightened further for the monogram, which is read as one unit.
KERN_MONO = 0.90


class SVGPen(BasePen):
    """Minimal glyph pen emitting SVG path data."""

    def __init__(self, glyph_set):
        BasePen.__init__(self, glyph_set)
        self.d = []

    def _moveTo(self, p):
        self.d.append('M%.1f %.1f' % p)

    def _lineTo(self, p):
        self.d.append('L%.1f %.1f' % p)

    def _curveToOne(self, a, b, c):
        self.d.append('C%.1f %.1f %.1f %.1f %.1f %.1f' % (*a, *b, *c))

    def _qCurveToOne(self, a, b):
        self.d.append('Q%.1f %.1f %.1f %.1f' % (*a, *b))

    def _closePath(self):
        self.d.append('Z')


def load_font():
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            return TTFont(path), path
    raise SystemExit('Maple Mono ExtraBold not found; checked:\n  ' +
                     '\n  '.join(FONT_CANDIDATES))


def glyph(font, char, asc):
    """Return (path_data, advance_width, ink_bounds) in SVG orientation."""
    glyph_set = font.getGlyphSet()
    name = font.getBestCmap()[ord(char)]
    transform = Transform(1, 0, 0, -1, 0, asc)

    pen = SVGPen(glyph_set)
    glyph_set[name].draw(TransformPen(pen, transform))

    bounds_pen = BoundsPen(glyph_set)
    glyph_set[name].draw(TransformPen(bounds_pen, transform))

    return ' '.join(pen.d), glyph_set[name].width, bounds_pen.bounds


def compose(items, kern=None):
    """Lay glyphs left to right; return (body, width, ink_bounds).

    `kern` maps glyph index -> multiplier on that glyph's advance, so it controls
    the gap *after* that glyph. Bounds come from font outlines, never from
    re-parsing the emitted path data.
    """
    kern = kern or {}
    parts, x = [], 0.0
    x0 = y0 = float('inf')
    x1 = y1 = float('-inf')
    for i, (d, w, colour, bounds) in enumerate(items):
        parts.append('<g transform="translate(%.1f,0)"><path d="%s" fill="%s"/></g>'
                     % (x, d, colour))
        bx0, by0, bx1, by1 = bounds
        x0, y0 = min(x0, bx0 + x), min(y0, by0)
        x1, y1 = max(x1, bx1 + x), max(y1, by1)
        x += w * kern.get(i, 1.0)
    return ''.join(parts), x, (x0, y0, x1, y1)


def svg(view_box, body):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="%s %s %s %s" '
            'width="%s" height="%s">%s</svg>' % (*view_box, *view_box[2:], body))


def main():
    font, font_path = load_font()
    asc = font['hhea'].ascender
    height = asc + (-font['hhea'].descender)

    g = {c: glyph(font, c, asc) for c in 'Qz.js'}

    def ink(char, colour):
        """Glyph tuple in compose() order: (path_data, advance, colour, bounds)."""
        d, w, bounds = g[char]
        return d, w, colour, bounds

    def wordmark(palette):
        items = [ink('Q', palette['Q']), ink('z', palette['z']),
                 ink('.', palette['Q']), ink('j', palette['Q']),
                 ink('s', palette['Q'])]
        body, width, bounds = compose(items, {1: KERN_QZ})
        return svg((0, 0, round(width), height), body)

    def monogram(palette):
        items = [ink('Q', palette['Q']), ink('z', palette['z'])]
        return compose(items, {0: KERN_MONO})

    def square(palette):
        body, _, (x0, y0, x1, y1) = monogram(palette)
        # Square crop on the ink, +18% breathing room, so the mark optically
        # centers at small sizes instead of hugging the box.
        side = max(x1 - x0, y1 - y0) * 1.18
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        box = (round(cx - side / 2, 1), round(cy - side / 2, 1),
               round(side, 1), round(side, 1))
        return svg(box, body)

    marks = {
        'logo-wordmark.svg': wordmark(LIGHT),
        'logo-wordmark-dark.svg': wordmark(DARK),
        'logo-mono.svg': svg((0, 0, round(monogram(LIGHT)[1]), height),
                             monogram(LIGHT)[0]),
        'logo-mono-dark.svg': svg((0, 0, round(monogram(DARK)[1]), height),
                                  monogram(DARK)[0]),
        'logo.svg': square(LIGHT),
        'logo-dark.svg': square(DARK),
    }
    for name, content in marks.items():
        with open(os.path.join(HERE, name), 'w') as fh:
            fh.write(content)
        print('wrote %s (%d bytes)' % (name, len(content)))

    # Shipped copies. VitePress resolves themeConfig.logo against `base`, so the
    # config references these by bare filename.
    os.makedirs(PUBLIC, exist_ok=True)
    for name in ('logo.svg', 'logo-dark.svg'):
        with open(os.path.join(PUBLIC, name), 'w') as fh:
            fh.write(marks[name])

    rasterize(os.path.join(PUBLIC, 'logo.svg'), os.path.join(PUBLIC, 'logo-dark.svg'))
    social_card(os.path.join(HERE, 'logo-wordmark-dark.svg'))
    print('font: %s' % font_path)


def rasterize(square_svg, square_dark_svg):
    """Rasterize the square marks to the icon set + favicon.

    Favicons are transparent PNGs and ship in both palettes: browsers that
    honour `prefers-color-scheme` pick the readable one, the rest get light.
    """
    try:
        import cairosvg
        from PIL import Image
    except ImportError as exc:
        print('skipping icons: %s' % exc)
        return

    for name, src in (('logo', square_svg), ('logo-dark', square_dark_svg)):
        tmp = os.path.join(HERE, '.%s-512.png' % name)
        cairosvg.svg2png(url=src, write_to=tmp, output_width=512, output_height=512)
        base = Image.open(tmp).convert('RGBA')

        # PNG sizes stay next to this script (full icon set); the two the site
        # references are also copied into docs/public.
        for size in (16, 32, 64, 128, 256, 512):
            icon = base.resize((size, size), Image.LANCZOS)
            icon.save(os.path.join(HERE, 'icon-%s-%d.png' % (name, size)))
            if size in (32, 256):
                icon.save(os.path.join(PUBLIC, 'icon-%s-%d.png' % (name, size)))
        print('wrote icon-%s-{16,32,64,128,256,512}.png' % name)

        # Transparent padding so the ICO mark is not clipped by the OS mask.
        canvas = Image.new('RGBA', (512, 512), (0, 0, 0, 0))
        inset = base.resize((384, 384), Image.LANCZOS)
        canvas.paste(inset, (64, 64), inset)
        out = 'favicon.ico' if name == 'logo' else 'favicon-dark.ico'
        canvas.save(os.path.join(PUBLIC, out), sizes=[(16, 16), (32, 32), (48, 48)])
        print('wrote %s' % out)

        os.remove(tmp)


def social_card(wordmark_svg):
    """Render the 1200x630 Open Graph card.

    Social crawlers do not render SVG, so the card has to be a raster. The
    mark is centred on the dark canvas, matching the site's dark theme.
    """
    try:
        import cairosvg
        from PIL import Image
    except ImportError as exc:
        print('skipping social card: %s' % exc)
        return

    width, height = 1200, 630
    tmp = os.path.join(HERE, '.og-mark.png')
    cairosvg.svg2png(url=wordmark_svg, write_to=tmp, output_width=760)
    mark = Image.open(tmp).convert('RGBA')

    card = Image.new('RGBA', (width, height), (13, 17, 23, 255))
    card.paste(mark, ((width - mark.width) // 2, (height - mark.height) // 2), mark)
    card.convert('RGB').save(os.path.join(PUBLIC, 'og-image.png'), optimize=True)
    print('wrote og-image.png (%dx%d)' % (width, height))
    os.remove(tmp)


if __name__ == '__main__':
    main()
