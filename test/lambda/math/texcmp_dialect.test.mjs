import assert from 'node:assert/strict';
import test from 'node:test';
import { prepare_reference } from './texcmp_dialect.mjs';

test('hex colors expand shorthand and preserve named and explicit-model colors', () => {
  const prepared = prepare_reference(String.raw`\color{#f0a}x\textcolor{#Ab12Ef}{y}\color{red}z\color[rgb]{1,0,0}a`);
  assert.equal(prepared.tex, String.raw`\color[HTML]{FF00AA}x\textcolor[HTML]{AB12EF}{y}\color{red}z\color[rgb]{1,0,0}a`);
  assert.deepEqual(prepared.changes.map((c) => c.kind), ['hex-color', 'hex-color']);
});

test('verbatim protects TeX syntax, extensions and comments without enabling features', () => {
  const prepared = prepare_reference(String.raw`\verb|\oiint & % # --|`);
  assert.deepEqual(prepared.features, []);
  assert.equal(prepared.changes.length, 1);
  assert.match(prepared.tex, /\\char38\{\}.*\\char37\{\}.*\\char35\{\}/);
  assert.match(prepared.tex, /\\char45\{\}\\char45\{\}/);
  assert.equal(prepare_reference(String.raw`\verb ab `).tex,
    String.raw`\text{\ttfamily \char97{}\char98{}}`);
  assert.match(prepare_reference(String.raw`\verb*|a b|`).tex, /\\textvisiblespace\{\}/);
  assert.throws(() => prepare_reference(String.raw`\verb|unterminated`), /unterminated/);
});

test('comments and escaped controls stay opaque to feature detection', () => {
  const source = String.raw`x % \oiint \color{#abc}` + '\n' + String.raw`\\oiint \% y`;
  const prepared = prepare_reference(source);
  assert.equal(prepared.tex, source);
  assert.deepEqual(prepared.features, []);
  assert.deepEqual(prepared.changes, []);
});

test('text accents and multilingual fonts respect nested math mode', () => {
  const prepared = prepare_reference('\\text{e\u030B БГ 여 私 $a+\\oiint$}');
  assert.match(prepared.tex, /\\H\{e\}/);
  assert.match(prepared.tex, /\\fontencoding\{T2A\}\\selectfont БГ/);
  assert.match(prepared.tex, /\\begin\{CJK\}\{UTF8\}\{nanummj\}여/);
  assert.match(prepared.tex, /\\begin\{CJK\}\{UTF8\}\{min\}私/);
  assert.deepEqual(prepared.features, ['cyrillic', 'integrals', 'japanese', 'korean']);
  assert.equal(prepare_reference('e\u030B').tex, 'e\u030B');
});

test('Unicode math alphabets retain weight and shape even inside text and other alphabets', () => {
  const prepared = prepare_reference(String.raw`𝐀𝑎𝑨𝔅𝔸𝒜\text{𝗔𝘢𝙰}\mathrm{𝖺𝟶}`);
  for (const command of ['mathbf{A}', 'mathit{a}', 'ReferenceBoldItalic{A}', 'mathfrak{B}', 'mathbb{A}',
    'mathscr{A}', 'ReferenceUnicode{cmss}{bx}{n}{A}', 'ReferenceUnicode{cmss}{m}{sl}{a}', 'mathtt{A}'])
    assert.ok(prepared.tex.includes('\\' + command), command);
  assert.equal(prepared.changes.length, 11);
});

test('audit entries point back to exact original formula or macro text', () => {
  const tex = String.raw`\color{#123}x\rule{18mu}{1mu}`;
  const definitions = String.raw`\def\reaction{\xrightequilibrium{a}}`;
  const prepared = prepare_reference(tex, definitions);
  assert.deepEqual(prepared.features, ['reactions']);
  for (const change of prepared.changes)
    assert.equal((change.source === 'formula' ? tex : definitions).slice(change.start, change.end), change.input);
  assert.equal(prepared.definitions, definitions);
  assert.match(prepared.tex, /\\ReferenceRule\{18mu\}\{1mu\}/);
});
