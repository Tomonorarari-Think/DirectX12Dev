"""表示幅 96 桁を超える行を報告する（全角は 2 桁）。

コーディング規約 5 節の「1 行は表示幅 96 桁以内」を機械で確かめる。
桁数は文字数ではないので、East Asian Width を見て数える。
"""
import pathlib
import sys
import unicodedata

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                    else pathlib.Path(__file__).resolve().parents[2])
LIMIT = 96
EXTS = {'.cpp', '.h', '.hlsl'}


def width(line):
    return sum(2 if unicodedata.east_asian_width(c) in 'WF' else 1 for c in line)


over = 0
for path in sorted(ROOT.rglob('*')):
    if not path.is_file() or path.suffix not in EXTS:
        continue
    if {'build', '.git'} & set(path.parts):
        continue
    for n, line in enumerate(path.read_text(encoding='utf-8').splitlines(), 1):
        w = width(line.rstrip())
        if w > LIMIT:
            over += 1
            print(f'{path.relative_to(ROOT)}:{n}  {w} 桁  {line.strip()[:60]}')

print(f'\n96 桁超過 {over} 件')
sys.exit(1 if over else 0)
