// docs 配下の Markdown から ```mermaid ブロックを抜き出し、mermaid.parse で構文検証する
import fs from 'node:fs';
import path from 'node:path';
import { JSDOM } from 'jsdom';

const dom = new JSDOM('<!doctype html><html><body></body></html>');
globalThis.window = dom.window;
globalThis.document = dom.window.document;
// Node 24 では globalThis.navigator が getter のみのため defineProperty で上書きする
Object.defineProperty(globalThis, 'navigator', {
  value: dom.window.navigator,
  configurable: true,
});
globalThis.DOMPurify = { addHook() {}, sanitize: (s) => s, setConfig() {} };

const mermaid = (await import('mermaid')).default;
mermaid.initialize({ startOnLoad: false, securityLevel: 'loose' });

const root = process.argv[2];
const files = [];
(function walk(dir) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) {
      if (e.name === 'build' || e.name === '.git' || e.name === 'node_modules') continue;
      walk(p);
    } else if (e.name.endsWith('.md')) {
      files.push(p);
    }
  }
})(root);

let total = 0;
let failed = 0;
for (const f of files) {
  const text = fs.readFileSync(f, 'utf8');
  const blocks = [...text.matchAll(/```mermaid\r?\n([\s\S]*?)```/g)];
  for (let i = 0; i < blocks.length; i++) {
    total++;
    const code = blocks[i][1];
    const kind = code.trim().split('\n')[0].trim();
    try {
      await mermaid.parse(code);
      console.log(`  OK   ${path.relative(root, f)} [${i + 1}] ${kind}`);
    } catch (err) {
      failed++;
      console.log(`  FAIL ${path.relative(root, f)} [${i + 1}] ${kind}`);
      console.log(`       ${String(err.message).split('\n').slice(0, 4).join('\n       ')}`);
    }
  }
}
console.log(`\n合計 ${total} ブロック / 失敗 ${failed}`);
process.exit(failed === 0 ? 0 : 1);
