// AMS arrowfill@/ext@arrow and over/under-arrow boxes, independently shipped by TeX.
import assert from 'node:assert/strict';
import test from 'node:test';
import { tex_geometry, assert_tex_geometry, missing_tex_tools } from './tex_geometry_oracle.mjs';

const identities = new Map([[0,'−'],[32,'←'],[33,'→'],[40,'⇐'],[41,'⇒'],[112,'√']]);
const rule = (width, height = 3, depth = 0) => `\\rule[-${depth}pt]{${width}pt}{${height + depth}pt}`;

test('bundled AMS horizontal arrows agree with independently shipped TeX geometry', {
  skip: missing_tex_tools.length ? `missing tools: ${missing_tex_tools.join(', ')}` : false,
}, async (t) => {
  const cases = [];
  const add = (style, formula, name) => cases.push({ source:`\\${style}style${formula}`, name:`${style} ${name}` });
  for (const style of ['display','text','script','scriptscript']) {
    for (const command of ['xrightarrow','xleftarrow','xleftrightarrow','xRightarrow','xLeftarrow','xLeftrightarrow']) {
      for (const [upper,lower] of [['',null], ['', ''], [rule(2),null], [rule(25),null],
        ['',rule(20,4,2)], [rule(18,2,3),rule(28,5,1)]])
        add(style, `\\${command}${lower === null ? '' : `[{${lower}}]`}{${upper}}`,
          `${command} upper=${upper} lower=${lower}`);
      add(style, `\\${command}[]{}`, `${command} absent optional labels`);
      add(style, `\\${command}[{{}}]{{}}`, `${command} nonempty zero-width labels`);
    }
    for (const command of ['overrightarrow','overleftarrow','overleftrightarrow',
      'underrightarrow','underleftarrow','underleftrightarrow']) {
      // Widths on either side of leader-count boundaries, and lowered/body-empty cases.
      for (const width of [0,3,10,10.555,10.556,13.333,13.334,14.444,14.445,16.666,16.667,20,20.001,30])
        add(style, `\\${command}{${rule(width,4,2)}}`, `${command} width ${width}`);
      add(style, `\\${command}{}`, `${command} empty`);
      add(style, `\\${command}{${rule(12)}}^{${rule(3,2)}}_{${rule(4,1,1)}}`, `${command} compound scripts`);
      add(style, `{\\${command}{${rule(12)}}}^{${rule(3,2)}}_{${rule(4,1,1)}}`, `${command} braced compound scripts`);
    }
    add(style, `\\sqrt{\\overrightarrow{${rule(18)}}}`, 'cramped over-arrow');
    add(style, `\\frac{${rule(4)}}{\\underrightarrow{${rule(18)}}}`, 'cramped under-arrow');
    add(style, `\\xrightarrow[{${rule(9)}}]{\\xleftarrow{${rule(8)}}}`, 'nested labelled arrows');
    add(style, `\\overrightarrow{\\underleftarrow{${rule(18)}}}`, 'nested arrow marks');
  }
  // AMS ex@ is nonlinear in physical text size; isolate it with the matched scaled fonts.
  for (const point_size of [1,5,6,7,8,8.4,8.5,8.7,9,10,11,12,14.4,17.28,19.5,20,20.01,20.74,24.88])
    for (const style of ['display','text','script','scriptscript'])
      cases.push({source:`\\${style}style\\underrightarrow{${rule(6,2,1)}}`,point_size,
        name:`${point_size}pt ${style} AMS under-arrow clearance`});
  const result = tex_geometry(cases, 'math-arrow-oracle-',
    ['test/lambda/math/tex_arrow_conformance.test.mjs'], ['mathtools']);
  t.diagnostic(`artifacts: ${result.dir}`);
  for (const [i,c] of cases.entries()) await t.test(c.name, () => {
    // Stacking may paint in a different order; compare spatially ordered components.
    const actual = {...result.lambda[i], glyphs:result.lambda[i].glyphs.toSorted((a,b) => a.x - b.x || a.y - b.y)};
    const page = {...result.dvi.pages[i], glyphs:result.dvi.pages[i].glyphs.toSorted((a,b) => a.x - b.x || a.y - b.y)};
    assert_tex_geometry(actual, result.dimensions.get(i), page, true);
    for (const [j,g] of page.glyphs.entries()) {
      const text = g.font.startsWith('cmsy') ? identities.get(g.slot) : g.font === 'cmr10' && g.slot === 61 ? '=' : null;
      assert.ok(text, `unmapped font/slot ${g.font}/${g.slot}`);
      assert.deepEqual([actual.glyphs[j].text,actual.glyphs[j].family], [text,'KaTeX_Main']);
    }
  });
});
