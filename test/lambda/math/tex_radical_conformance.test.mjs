// TeX82 radical/accent construction and LaTeX's default indexed-root macro.
import assert from 'node:assert/strict';
import test from 'node:test';
import { tex_geometry, assert_tex_geometry, missing_tex_tools } from './tex_geometry_oracle.mjs';

// CMEX slots in the font generator's unchanged encoding, independently checked in native PNGs.
const cmexGlyphs = new Map([
  ...[112,113,114,115].map((slot, i) => [slot, ['√', `KaTeX_Size${i + 1}`]]),
  [116,['⎷','KaTeX_Size4']], [117,['\uE000','KaTeX_Size4']], [118,['\uE001','KaTeX_Size4']],
  ...[98,99,100].map((slot, i) => [slot, ['ˆ', `KaTeX_Size${i + 1}`]]),
  ...[101,102,103].map((slot, i) => [slot, ['˜', `KaTeX_Size${i + 1}`]]),
]);

test('bundled radicals and wide accents agree with independently shipped TeX geometry', {
  skip: missing_tex_tools.length ? `missing tools: ${missing_tex_tools.join(', ')}` : false,
}, async (t) => {
  const cases = [];
  for (const style of ['display','text','script','scriptscript']) {
    for (const height of [0,0.5,4,7,10,13,18,24,30,60])
      for (const depth of [0,4]) {
        const body = `\\rule[-${depth}pt]{10pt}{${height + depth}pt}`;
        for (const index of [null, '', String.raw`\phantom{\rule{3pt}{2pt}}`,
          String.raw`\phantom{\rule[-1pt]{3pt}{2pt}}`])
          cases.push({source:`\\${style}style\\sqrt${index === null ? '' : `[${index}]`}{${body}}`,
            name:`${style} root ${height}pt/${depth}pt ${index === null ? 'ordinary' : index || 'empty degree'}`});
      }
    for (const body of [String.raw`\sqrt{\rule{10pt}{10pt}}`, String.raw`\frac{\rule{8pt}{9pt}}{\rule{4pt}{3pt}}`])
      cases.push({source:`\\${style}style\\sqrt{${body}}`,name:`${style} nested ${body}`});
    for (const command of ['widehat','widetilde'])
      for (const width of [0,3,5.5,5.6,9.9,10.1,14.4,14.5,30])
        for (const height of [2,8]) {
          const body = `\\rule{${width}pt}{${height}pt}`;
          cases.push({source:`\\${style}style\\${command}{${body}}`,name:`${style} ${command} ${width}pt/${height}pt`});
        }
  }
  const result = tex_geometry(cases, 'math-radical-oracle-', ['test/lambda/math/tex_radical_conformance.test.mjs']);
  t.diagnostic(`artifacts: ${result.dir}`);
  for (const [i, c] of cases.entries()) await t.test(c.name, () => {
    assert_tex_geometry(result.lambda[i], result.dimensions.get(i), result.dvi.pages[i], true);
    for (const [j, expected] of result.dvi.pages[i].glyphs.entries()) {
      const identity = expected.font === 'cmex10' ? cmexGlyphs.get(expected.slot) : ['√', 'KaTeX_Main'];
      assert.ok(identity, `unmapped reference font/slot ${expected.font}/${expected.slot}`);
      assert.deepEqual([result.lambda[i].glyphs[j].text,result.lambda[i].glyphs[j].family],identity);
    }
  });
});
