// Canonicalize KaTeX input for pdfLaTeX without changing the imported corpus.
const TEXT_COMMANDS = new Set(['text', 'textrm', 'textsf', 'texttt', 'textbf',
  'textit', 'textnormal', 'mbox', 'reflectbox']);
const ACCENTS = new Map(Object.entries({ '\u0300': '`', '\u0301': "'", '\u0302': '^',
  '\u0303': '~', '\u0304': '=', '\u0306': 'u', '\u0307': '.', '\u0308': '"',
  '\u030A': 'r', '\u030B': 'H', '\u030C': 'v', '\u0327': 'c', '\u0328': 'k' }));

// Unicode Mathematical Alphanumeric Symbols: each range retains its actual font style.
const ALPHABETS = [
  [0x1d400, 0x1d433, 'mathbf'], [0x1d434, 0x1d467, 'mathit'],
  [0x1d468, 0x1d49b, 'ReferenceBoldItalic'], [0x1d49c, 0x1d4cf, 'mathscr'],
  [0x1d4d0, 0x1d503, 'ReferenceBoldScript'], [0x1d504, 0x1d537, 'mathfrak'],
  [0x1d538, 0x1d56b, 'mathbb'], [0x1d56c, 0x1d59f, 'ReferenceBoldFraktur'],
  [0x1d5a0, 0x1d5d3, 'mathsf'], [0x1d5d4, 0x1d607, 'mathsfbf'],
  [0x1d608, 0x1d63b, 'mathsfit'], [0x1d63c, 0x1d66f, 'mathsfbfit'],
  [0x1d670, 0x1d6a3, 'mathtt'],
  [0x1d7ce, 0x1d7d7, 'mathbf'], [0x1d7d8, 0x1d7e1, 'mathbb'],
  [0x1d7e2, 0x1d7eb, 'mathsf'], [0x1d7ec, 0x1d7f5, 'mathsfbf'],
  [0x1d7f6, 0x1d7ff, 'mathtt'],
];
const EXTRA_COMMANDS = {
  integrals: new Set(['oiint', 'oiiint']),
  accents: new Set(['overgroup', 'undergroup', 'widecheck', 'Overrightarrow',
    'overlinesegment', 'underlinesegment', 'overleftharpoon', 'overrightharpoon', 'utilde']),
  mathabx: new Set(['overgroup', 'undergroup', 'widecheck']),
  reactions: new Set(['xrightleftarrows', 'xrightequilibrium', 'xleftequilibrium']),
};
const TEXT_ALPHABETS = { mathsfbf: ['cmss', 'bx', 'n'], mathsfit: ['cmss', 'm', 'sl'],
  mathsfbfit: ['lmss', 'bx', 'sl'] }; // CM has no bold oblique sans shape; Latin Modern does.

export function prepare_reference(tex, definitions = '') {
  if (typeof tex !== 'string') throw new Error('reference formula must be a string');
  const changes = [], features = new Set();

  function transform(source, sourceName) {
    let i = 0;
    function replace(start, end, value, kind) {
      changes.push({ source: sourceName, start, end, kind, input: source.slice(start, end), output: value });
      return value;
    }
    function walk(initialMode, grouped = false) {
      let output = '', mode = initialMode;
      while (i < source.length) {
        const start = i, ch = String.fromCodePoint(source.codePointAt(i));
        if (ch === '}' && grouped) { i++; return output; }
        if (ch === '%') {
          const end = source.indexOf('\n', i);
          i = end === -1 ? source.length : end + 1;
          output += source.slice(start, i);
          continue;
        }
        if (ch === '{') { i++; output += '{' + walk(mode, true) + '}'; continue; }
        if (ch === '$') { mode = mode === 'text' ? 'math' : 'text'; output += ch; i++; continue; }
        if (ch === '\\') {
          const match = source.slice(i).match(/^\\([A-Za-z]+|[^\n])/);
          if (!match) { output += ch; i++; continue; }
          const name = match[1]; i += match[0].length;
          if (name === 'verb') {
            const star = source[i] === '*'; if (star) i++;
            const delimiter = source[i++], end = source.indexOf(delimiter, i);
            if (!delimiter || delimiter === '\n' || end < 0 || source.slice(i, end).includes('\n'))
              throw new Error('unterminated verbatim input');
            const text = source.slice(i, end); i = end + 1;
            // Explicit glyphs survive array tokenization and prevent TeX ligatures.
            const body = [...text].map((c) => c === ' '
              ? (star ? '\\textvisiblespace{}' : '\\ ')
              : c.codePointAt(0) < 128 ? `\\char${c.codePointAt(0)}{}` : c).join('');
            output += replace(start, i, `\\text{\\ttfamily ${body}}`, 'verbatim');
            continue;
          }
          for (const [feature, commands] of Object.entries(EXTRA_COMMANDS))
            if (commands.has(name)) features.add(feature);
          if (name === 'color' || name === 'textcolor') {
            const color = source.slice(i).match(/^\s*\{#([0-9a-fA-F]{3}|[0-9a-fA-F]{6})\}/);
            if (color) {
              i += color[0].length;
              const hex = color[1].length === 3 ? [...color[1]].map((c) => c + c).join('') : color[1];
              output += replace(start, i, `\\${name}[HTML]{${hex.toUpperCase()}}`, 'hex-color');
              continue;
            }
          }
          if (name === 'rule') {
            output += replace(start, i, '\\ReferenceRule', 'rule-units');
            continue;
          }
          output += match[0];
          if (TEXT_COMMANDS.has(name)) {
            const whitespace = source.slice(i).match(/^\s*/)[0];
            output += whitespace; i += whitespace.length;
            if (source[i] === '{') { i++; output += '{' + walk('text', true) + '}'; }
          }
          continue;
        }
        const alphabet = ALPHABETS.find(([first, last]) => ch.codePointAt(0) >= first && ch.codePointAt(0) <= last);
        const plain = ch.normalize('NFKD');
        if (alphabet && /^[A-Za-z0-9]$/.test(plain)) {
          i += ch.length;
          // Text-backed Latin variants avoid pdfTeX's sixteen math-family limit.
          const textFont = TEXT_ALPHABETS[alphabet[2]];
          const glyph = textFont ? `\\ReferenceUnicode${textFont.map((part) => `{${part}}`).join('')}{${plain}}`
            : `\\${alphabet[2]}{${plain}}`;
          output += replace(start, i, `\\ensuremath{${glyph}}`, 'unicode-alphabet');
          continue;
        }
        if (mode === 'text' && /\p{L}/u.test(ch) && ACCENTS.has(source[i + ch.length])) {
          let accented = ch; i += ch.length;
          while (ACCENTS.has(source[i])) accented = `\\${ACCENTS.get(source[i++])}{${accented}}`;
          output += replace(start, i, accented, 'combining-text-accent');
          continue;
        }
        const script = mode !== 'text' ? null : /[\u0400-\u04ff]/u.test(ch) ? 'cyrillic'
          : /[\u1100-\u11ff\u3130-\u318f\uac00-\ud7af]/u.test(ch) ? 'korean'
            : /[\u3040-\u30ff\u3400-\u9fff]/u.test(ch) ? 'japanese' : null;
        if (script) {
          const range = script === 'cyrillic' ? /^[\u0400-\u04ff]+/u
            : script === 'korean' ? /^[\u1100-\u11ff\u3130-\u318f\uac00-\ud7af]+/u
              : /^[\u3040-\u30ff\u3400-\u9fff]+/u;
          const text = source.slice(i).match(range)[0]; i += text.length;
          features.add(script);
          output += replace(start, i, script === 'cyrillic'
            ? `{\\fontencoding{T2A}\\selectfont ${text}}`
            : `\\begin{CJK}{UTF8}{${script === 'korean' ? 'nanummj' : 'min'}}${text}\\end{CJK}`, 'unicode-text');
          continue;
        }
        output += ch; i += ch.length;
      }
      if (grouped) throw new Error('unclosed reference group');
      return output;
    }
    return walk('math');
  }
  const normalizedDefinitions = transform(definitions, 'definitions');
  const normalizedTex = transform(tex, 'formula');
  return { tex: normalizedTex, definitions: normalizedDefinitions, changes, features: [...features].sort() };
}
