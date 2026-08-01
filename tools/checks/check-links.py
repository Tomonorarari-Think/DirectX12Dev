"""Markdown の相対リンクを全て解決し、切れているものを報告する。

資料はコードと同じ成果物として扱うため、ファイル名を変えたときの
リンク切れをビルドと同じ扱いで落とす。
"""
import pathlib
import re
import sys
from urllib.parse import unquote

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                    else pathlib.Path(__file__).resolve().parents[2])
LINK = re.compile(r'\[[^\]]*\]\(([^)\s]+)(?:\s+"[^"]*")?\)')

total = dead = 0
for md in sorted(ROOT.rglob('*.md')):
    if 'node_modules' in md.parts or '.git' in md.parts:
        continue
    text = md.read_text(encoding='utf-8')
    for target in LINK.findall(text):
        if target.startswith(('http://', 'https://', 'mailto:', '#')):
            continue
        total += 1
        path, _, anchor = target.partition('#')
        if not path:
            continue
        resolved = (md.parent / unquote(path)).resolve()
        if not resolved.exists():
            dead += 1
            print(f'DEAD  {md.relative_to(ROOT)}  ->  {target}')

print(f'\n相対リンク {total} 件 / 解決できないもの {dead} 件')
sys.exit(1 if dead else 0)
