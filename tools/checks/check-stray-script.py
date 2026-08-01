"""この案件で使うはずのない文字体系の混入を検出する。

過去に日本語コメントへキリル文字やハングルが紛れ込んだことがある。
CJK の中では見た目で気付けないため、使わない文字体系ごと弾く。
"""
import pathlib
import sys
import unicodedata

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                    else pathlib.Path(__file__).resolve().parents[2])
EXTS = {'.cpp', '.h', '.hlsl', '.md', '.svg', '.json', '.ps1', '.vcxproj', '.filters'}

# Unicode names begin with the script name; these must never show up here.
FORBIDDEN = (
    'CYRILLIC', 'HANGUL', 'ARABIC', 'HEBREW', 'THAI', 'DEVANAGARI',
    'ARMENIAN', 'GEORGIAN', 'BENGALI', 'TAMIL', 'MYANMAR', 'KHMER',
    'LAO', 'TIBETAN', 'ETHIOPIC', 'CHEROKEE', 'MONGOLIAN', 'SYRIAC',
    'BOPOMOFO', 'YI ', 'VAI ', 'COPTIC', 'GLAGOLITIC',
)

hits = 0
scanned = 0
for path in sorted(ROOT.rglob('*')):
    if not path.is_file() or path.suffix not in EXTS:
        continue
    if {'node_modules', '.git', 'build'} & set(path.parts):
        continue
    scanned += 1
    try:
        text = path.read_text(encoding='utf-8')
    except UnicodeDecodeError:
        print(f'NOT-UTF8  {path.relative_to(ROOT)}')
        hits += 1
        continue

    for lineno, line in enumerate(text.splitlines(), 1):
        for ch in line:
            if ord(ch) < 128:
                continue
            try:
                name = unicodedata.name(ch)
            except ValueError:
                continue
            if name.startswith(FORBIDDEN):
                print(f'STRAY  {path.relative_to(ROOT)}:{lineno}  '
                      f'U+{ord(ch):04X} {name}  |  {line.strip()[:70]}')
                hits += 1

print(f'\n{scanned} ファイルを走査 / 混入 {hits} 件')
sys.exit(1 if hits else 0)
