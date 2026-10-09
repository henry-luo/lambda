// Decode the retained reference corpus; HTML snapshots are historical evidence.
import fs from 'node:fs';
import { fileURLToPath } from 'node:url';

const fixtures = {
  mathlive: new URL('../mathlive/__snapshots__/markup.test.ts.snap', import.meta.url),
  'lambda-input': new URL('../mathlive/__snapshots__/lambda_input_markup.snap', import.meta.url),
};

export function readCorpus(source = 'all') {
  const selected = source === 'all' ? Object.values(fixtures) : [fixtures[source]];
  if (selected.some((file) => !file)) throw new Error(`unknown fixture source: ${source}`);
  const cases = selected.flatMap((file) => buildCases(parseSnapshots(fs.readFileSync(file, 'utf8'))));
  return { cases, snapshots: selected.map(fileURLToPath) };
}

function parseSnapshots(snapshotText) {
  const entries = [];
  const re = /exports\[`([^`]+)`\] = `\n([\s\S]*?)\n`;/g;
  let match;
  while ((match = re.exec(snapshotText)) !== null) {
    const key = match[1];
    const body = match[2];
    const parsed = parseSnapshotBody(body);
    if (!parsed) continue;
    entries.push({ key, ...parsed });
  }
  return entries;
}

function parseSnapshotBody(body) {
  const lines = body.split('\n');
  const htmlStart = lines.findIndex((line) => line.startsWith('  "'));
  const errorIndex = lines.findLastIndex((line) => /^  "[^"]*",$/.test(line));
  if (htmlStart < 0 || errorIndex < 0 || htmlStart >= errorIndex) return null;

  return {
    expectedHtml: lines.slice(htmlStart, errorIndex).join('\n').slice(3, -2),
    expectedError: lines[errorIndex].slice(3, -2),
  };
}

function buildCases(snapshotEntries) {
  const cases = [];
  for (const entry of snapshotEntries) {
    const rawFormula = formulaFromSnapshotKey(entry.key);
    const normalized = rawFormula == null ? null : normalizeFormula(rawFormula);
    if (normalized == null) continue;
    cases.push({
      category: categoryFromKey(entry.key),
      key: entry.key,
      formula: normalized.formula,
      display: normalized.display,
      expectedError: entry.expectedError,
    });
  }
  return cases;
}

function normalizeFormula(formula) {
  const trimmed = formula.trim();
  if (trimmed.startsWith('\\[') && trimmed.endsWith('\\]')) {
    return { formula: trimmed.slice(2, -2).trim(), display: true };
  }
  return { formula, display: false };
}

function categoryFromKey(key) {
  const categories = [
    'DELIMITER SIZING COMMANDS',
    'SUPERSCRIPT/SUBSCRIPT',
    'RULE AND DIMENSIONS',
    'SPACING AND KERN',
    'BINARY OPERATORS',
    'OVER/UNDERLINE',
    'SIZING COMMANDS',
    'MODE SHIFT',
    'LEFT/RIGHT',
    'ENVIRONMENTS',
    'EXTENSIONS',
    'FRACTIONS',
    'COMMANDS',
    'ACCENTS',
    'COLORS',
    'FONTS',
    'SURDS',
    'BOX',
    'NOT',
  ];
  return categories.find((category) => key.startsWith(category)) ?? key.split(' ')[0];
}

function formulaFromSnapshotKey(key) {
  const accent = key.match(/^ACCENTS \d+\/ (\\+[A-Za-z]+) renders correctly 1$/);
  if (accent) {
    const cmd = unescapeSnapshotText(accent[1]);
    return `${cmd}{x}${cmd}{x + 1}${cmd}`;
  }

  const colorFormat = key.match(/^COLORS \d+\/ color format "([\s\S]*)" renders correctly 1$/);
  if (colorFormat) {
    return `a\\textcolor{${unescapeSnapshotText(colorFormat[1])}}{x}b`;
  }

  if (key === 'COMMANDS Commands  1') {
    return "\\!\\#\\%\\&\\$\\_\\{\\}\\text{\\'{a}\\\"{a}\\.{a}\\`{a}\\={a}\\~{a}\\^{a}}";
  }

  const delimiterSize = key.match(
    /^DELIMITER SIZING COMMANDS \d+\/ sizing command ([\s\S]*) 1$/
  );
  if (delimiterSize) {
    const parts = delimiterSize[1].split(', ').map(unescapeSnapshotText);
    if (parts.length === 4) {
      const [left, right, middle, plain] = parts;
      return `${left}(x${middle}|y${right}) = x+${plain}x${plain}+`;
    }
  }

  const leftRight = key.match(/^LEFT\/RIGHT Delimiters ([\s\S]*) ([0-9]+)$/);
  if (leftRight) {
    const body = leftRight[1];
    const index = Number(leftRight[2]);
    const [open, close] = splitDelimiterPair(body).map(unescapeSnapshotText);
    if (!open || !close) return null;
    if (index % 2 === 1) return `\\left${open} x + 1\\right${close}`;
    return `\\left${open} x \\frac{\\frac34}{\\frac57}\\right${close}`;
  }

  const middle = key.match(/^LEFT\/RIGHT middle delimiters ([123])$/);
  if (middle) {
    return [
      '\\left(a\\middle|b\\right)',
      '\\left(a\\middle xb\\right)',
      '\\left(a\\color{red}\\middle|b\\right)',
    ][Number(middle[1]) - 1];
  }

  const script = key.match(/^SUPERSCRIPT\/SUBSCRIPT ([\s\S]*) 1$/);
  if (script) return unescapeSnapshotText(script[1]);

  const direct = key.match(/^[^ ]+ \d+\/ ([\s\S]*) renders (?:correctly|corectly) 1$/);
  if (direct) return unescapeSnapshotText(direct[1]);

  const multiWordDirect = key.match(
    /^(MODE SHIFT|BINARY OPERATORS|RULE AND DIMENSIONS|SPACING AND KERN|SIZING COMMANDS|OVER\/UNDERLINE|ENVIRONMENTS|EXTENSIONS|SURDS|COLORS|BOX|NOT) \d+\/ ([\s\S]*) renders (?:correctly|corectly) 1$/
  );
  if (multiWordDirect) return unescapeSnapshotText(multiWordDirect[2]);

  return null;
}

function splitDelimiterPair(body) {
  const parts = body.split(' ');
  if (parts.length === 2) return parts;
  if (parts.length > 2) {
    return [parts.slice(0, -1).join(' '), parts[parts.length - 1]];
  }
  return [body, ''];
}

function unescapeSnapshotText(text) {
  return text.replace(/\\\\n(?=\s|$)/g, ' ').replace(/\\\\/g, '\\');
}
