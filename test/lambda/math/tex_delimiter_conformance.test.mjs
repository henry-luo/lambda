// TeX82 var_delimiter oracle: actual TeX boxes and shipped DVI component baselines.
import test from 'node:test';
import { tex_geometry, assert_tex_geometry, missing_tex_tools } from './tex_geometry_oracle.mjs';

test('bundled delimiters agree with TeX boxes and shipped components', {
  skip: missing_tex_tools.length ? `missing tools: ${missing_tex_tools.join(', ')}` : false,
}, async (t) => {
  const cases = [];
  const delimiters = ['(', ')', '[', ']', '\\lfloor', '\\rfloor', '\\lceil', '\\rceil',
    '\\{', '\\}', '\\langle', '\\rangle', '|', '\\Vert', '/', '\\backslash',
    '\\uparrow', '\\downarrow', '\\updownarrow', '\\Uparrow', '\\Downarrow', '\\Updownarrow',
    '\\lgroup', '\\rgroup', '\\lmoustache', '\\rmoustache'];
  for (const delimiter of delimiters) for (const style of ['display', 'text', 'script', 'scriptscript']) {
    for (const size of ['big', 'Big', 'bigg', 'Bigg'])
      cases.push({ delimiter, source: `\\${style}style\\${size}l${delimiter}`, name: `${style} ${size} ${delimiter}` });
    for (const height of [0.05, 1.3, 6])
      cases.push({ delimiter, source: `\\${style}style\\left${delimiter}\\vphantom{\\rule{0pt}{${height * 10}pt}}\\right.`,
        name: `${style} automatic ${delimiter}, ${height}em` });
  }
  for (const delimiter of ['(', '[', '\\{', '\\langle', '\\Vert', '\\uparrow'])
    for (const style of ['display', 'text', 'script', 'scriptscript']) {
      const body = '\\vphantom{\\rule{0pt}{60pt}}';
      for (const [kind, expression] of [
        ['cramped', `\\overline{\\left${delimiter}${body}\\right.}`],
        ['middle', `\\left${delimiter}${body}\\middle|\\right.`],
        ['nested', `\\left${delimiter}\\left[${body}\\right]\\right.`],
      ]) cases.push({ delimiter, source: `\\${style}style${expression}`, name: `${style} ${kind} ${delimiter}` });
    }
  for (const style of ['display', 'text', 'script', 'scriptscript'])
    for (const source of [String.raw`\left.\right.`, String.raw`\left(\middle|\right)`,
      String.raw`\left(\scriptstyle\vphantom{\rule{0pt}{60pt}}\middle|\right)`,
      String.raw`\left(\scriptstyle\vphantom{\rule{0pt}{60pt}}\middle|\vphantom{\rule{0pt}{60pt}}\right)`,
      String.raw`\left(\scriptstyle1\vphantom{\rule{0pt}{60pt}}\middle|1\right)`])
      cases.push({ source: `\\${style}style${source}`, name: `${style} boundary ${source}` });
  const result = tex_geometry(cases, 'math-delimiter-oracle-', ['test/lambda/math/tex_delimiter_conformance.test.mjs']);
  t.diagnostic(`artifacts: ${result.dir}`);
  for (const [i, c] of cases.entries()) await t.test(c.name, () =>
    assert_tex_geometry(result.lambda[i], result.dimensions.get(i), result.dvi.pages[i]));
});
