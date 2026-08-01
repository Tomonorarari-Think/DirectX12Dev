"""SVG の文字色と背景色のコントラスト比を検査する。

図の文字と背景のコントラスト比は 7:1 以上と決めている（コーディング規約 5 節）。
CSS は後に定義した規則が優先される点と、fill-opacity の合成を考慮する。

使い方:
    python check-contrast.py <SVG のあるフォルダ> [出力先のテキスト]
"""
import io
import re
import sys
import pathlib
import xml.etree.ElementTree as ET
from collections import Counter

# 標準出力を UTF-8 に固定する。
#   ★ Windows の既定は環境によって cp932 や cp1252 になる。
#     日本語を出した瞬間に UnicodeEncodeError で落ちるため、ここで揃える。
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

NS = '{http://www.w3.org/2000/svg}'
PAGE = '#f6f8fa'


def styles_of(root):
    st, order = {}, []
    for s in root.iter(NS + 'style'):
        for m in re.finditer(r'\.([\w-]+)\s*\{([^}]*)\}', s.text or ''):
            name = m.group(1)
            order.append(name)
            e = st.setdefault(name, {})
            for prop in m.group(2).split(';'):
                if ':' in prop:
                    k, v = prop.split(':', 1)
                    e[k.strip()] = v.strip()
    return st, order


def res(el, st, order, prop):
    if el.get(prop):
        return el.get(prop)
    cands = [c for c in (el.get('class') or '').split() if c in st and prop in st[c]]
    if not cands:
        return None
    cands.sort(key=lambda c: order.index(c))
    return st[cands[-1]][prop]


def rgb(c):
    if not c or not c.startswith('#'):
        return None
    c = c[1:]
    if len(c) == 3:
        c = ''.join(x * 2 for x in c)
    return tuple(int(c[i:i + 2], 16) for i in (0, 2, 4)) if len(c) == 6 else None


def lum(v):
    def ch(x):
        x /= 255
        return x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(i) for i in v)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(a, b):
    l1, l2 = lum(a), lum(b)
    if l1 < l2:
        l1, l2 = l2, l1
    return (l1 + 0.05) / (l2 + 0.05)


def over(fg, a, bg):
    return tuple(round(fg[i] * a + bg[i] * (1 - a)) for i in range(3))


def poly_in(pts, x, y):
    ins = False
    n = len(pts)
    for i in range(n):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            if x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
                ins = not ins
    return ins


def collect(folder):
    rows = []
    for p in sorted(pathlib.Path(folder).glob('*.svg')):
        root = ET.parse(p).getroot()
        st, order = styles_of(root)
        shapes = []
        for el in root.iter():
            tag = el.tag.replace(NS, '')
            f = res(el, st, order, 'fill')
            if not f:
                continue
            op = res(el, st, order, 'fill-opacity')
            op = float(op) if op else 1.0
            if tag == 'rect':
                try:
                    x = float(el.get('x', 0)); y = float(el.get('y', 0))
                    w = float(el.get('width')); h = float(el.get('height'))
                except (TypeError, ValueError):
                    continue
                shapes.append(('r', (x, y, w, h), f, op))
            elif tag == 'polygon':
                pts = [tuple(map(float, q.split(',')))
                       for q in (el.get('points') or '').split() if ',' in q]
                if pts:
                    shapes.append(('p', pts, f, op))

        for el in root.iter(NS + 'text'):
            fgc = res(el, st, order, 'fill')
            fg = rgb(fgc)
            if not fg:
                continue
            try:
                x = float(el.get('x')); y = float(el.get('y'))
            except (TypeError, ValueError):
                continue
            size = float(re.sub(r'[^\d.]', '', res(el, st, order, 'font-size') or '12px') or 12)
            w = int(re.sub(r'[^\d]', '', res(el, st, order, 'font-weight') or '400') or 400)
            need = 3.0 if (size >= 18 or (size >= 14 and w >= 600)) else 4.5
            probe = (x + 2, y - size * 0.35)
            bg, bgn = rgb(PAGE), '(page)'
            for kind, geom, f, op in shapes:
                if kind == 'r':
                    hit = (geom[0] <= probe[0] <= geom[0] + geom[2]
                           and geom[1] <= probe[1] <= geom[1] + geom[3])
                else:
                    hit = poly_in(geom, *probe)
                if hit:
                    c = rgb(f)
                    if c:
                        bg = over(c, op, bg)
                        bgn = f + ('' if op == 1 else ' @%.2f' % op)
            rows.append((ratio(fg, bg), need, p.name, fgc, bgn,
                         ''.join(el.itertext())[:26]))
    return rows


# 引数が無ければリポジトリの docs/assets を見る。
folder = sys.argv[1] if len(sys.argv) > 1 else str(
    pathlib.Path(__file__).resolve().parents[2] / 'docs' / 'assets')

rows = sorted(collect(folder))
out = io.StringIO()
out.write('=== コントラスト比 低い順 20 件 ===\n')
for r, need, name, fg, bg, t in rows[:20]:
    mark = 'NG' if r < need else ('..' if r < 7.0 else '  ')
    out.write('%s %5.2f (>=%.1f) %-30s %-8s on %-16s %s\n' % (mark, r, need, name, fg, bg, t))

ng = [x for x in rows if x[0] < x[1]]
low = [x for x in rows if x[1] <= x[0] < 7.0]
out.write('\n基準未達(AA) %d 件 / 4.5〜7.0 の低め %d 件 / 全 %d 件\n' % (len(ng), len(low), len(rows)))
if ng:
    out.write('\n=== 未達のファイル別 ===\n')
    for k, v in Counter(x[2] for x in ng).most_common():
        out.write('  %-32s %d\n' % (k, v))
if len(sys.argv) > 2:
    io.open(sys.argv[2], 'w', encoding='utf-8').write(out.getvalue())
    print('詳細を書き出しました: ' + sys.argv[2])

# 最低でも要約は必ず標準出力へ出す。
print(out.getvalue().rstrip())

# ★ このプロジェクトの基準は 7:1。WCAG AA(4.5) ではなく 7.0 で落とす。
sys.exit(1 if (ng or low) else 0)
