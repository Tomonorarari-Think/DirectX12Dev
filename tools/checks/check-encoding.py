"""PowerShell スクリプトが UTF-8 BOM 付きで保存されているか確かめる。

Windows PowerShell 5.1 は BOM の無いファイルを CP932 として読む。
日本語コメントが化けるだけでなく、その行以降が構文エラーになって
「なぜか動かない」状態になる。何度も踏んだので機械で止める。
"""
import pathlib
import sys

ROOT = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                    else pathlib.Path(__file__).resolve().parents[2])

BOM = b'\xef\xbb\xbf'

checked = bad = 0
for path in sorted(ROOT.rglob('*.ps1')):
    if {'node_modules', '.git', 'build'} & set(path.parts):
        continue

    checked += 1
    head = path.open('rb').read(3)

    if head != BOM:
        bad += 1
        print(f'NO-BOM  {path.relative_to(ROOT)}')

print(f'\nPowerShell {checked} ファイル / BOM 無し {bad} 件')
sys.exit(1 if bad else 0)
