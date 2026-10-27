// Mark is data: this codec never evaluates source or flattens Lambda value kinds.
export class MarkSymbol {
  constructor(name) { this.name = name; }
}
export class MarkScalar {
  constructor(kind, text) { this.kind = kind; this.text = text; }
  valueOf() {
    if (this.text === 'inf') return Infinity;
    if (this.text === '-inf') return -Infinity;
    return Number(this.text.replace(/[nm]$/, ''));
  }
}
export class MarkElement {
  constructor(name, attrs = Object.create(null), children = []) {
    this.name = name;
    this.attrs = attrs;
    this.children = children;
  }
}
export class MarkBinary {
  constructor(bytes) { this.bytes = bytes; }
}
export const symbol = name => new MarkSymbol(name);

export function decodeMark(text) {
  let cursor = 0;
  const fail = message => { throw new SyntaxError(`${message} at byte/character ${cursor}`); };
  function space() {
    for (;;) {
      while (/\s/.test(text[cursor] || '') && cursor < text.length) cursor++;
      if (text.startsWith('//', cursor)) {
        const end = text.indexOf('\n', cursor + 2);
        cursor = end < 0 ? text.length : end + 1;
      } else if (text.startsWith('/*', cursor)) {
        const end = text.indexOf('*/', cursor + 2);
        if (end < 0) fail('Unclosed comment');
        cursor = end + 2;
      } else return;
    }
  }
  function quoted() {
    const quote = text[cursor++];
    let value = '';
    while (cursor < text.length) {
      let ch = text[cursor++];
      if (ch === quote) return value;
      if (ch === '\\') {
        ch = text[cursor++];
        const escapes = { n: '\n', r: '\r', t: '\t', b: '\b', f: '\f', v: '\v', '0': '\0' };
        if (ch === 'u') {
          const digits = text.slice(cursor, cursor + 4);
          if (!/^[\da-f]{4}$/i.test(digits)) fail('Invalid Unicode escape');
          value += String.fromCharCode(parseInt(digits, 16));
          cursor += 4;
        } else if (ch in escapes) value += escapes[ch];
        else if (ch === quote || ch === '\\' || ch === '/') value += ch;
        else fail('Invalid escape');
      } else value += ch;
    }
    fail('Unclosed quoted value');
  }
  function name() {
    space();
    if (text[cursor] === '"' || text[cursor] === "'") return quoted();
    const start = cursor;
    while (cursor < text.length && !/[\s,:;<>\[\]{}()]/.test(text[cursor])) cursor++;
    if (cursor === start) fail('Expected a name');
    return text.slice(start, cursor);
  }
  function value(depth = 0) {
    if (depth > 64) fail('Mark nesting limit');
    space();
    const ch = text[cursor];
    if (ch === '"') return quoted();
    if (ch === "'") return symbol(quoted());
    if (ch === '{') {
      cursor++;
      const map = Object.create(null);
      space();
      while (text[cursor] !== '}') {
        const key = name();
        space();
        if (text[cursor++] !== ':') fail('Expected colon');
        if (Object.hasOwn(map, key)) fail('Duplicate map field');
        map[key] = value(depth + 1);
        space();
        if (text[cursor] === '}') break;
        if (text[cursor++] !== ',') fail('Expected comma');
        space();
      }
      cursor++;
      return map;
    }
    if (ch === '[') {
      cursor++;
      const items = [];
      space();
      while (text[cursor] !== ']') {
        items.push(value(depth + 1));
        space();
        if (text[cursor] === ']') break;
        if (text[cursor++] !== ',') fail('Expected comma');
        space();
      }
      cursor++;
      return items;
    }
    if (ch === '<') {
      cursor++;
      const element = new MarkElement(name());
      let content = false;
      space();
      while (text[cursor] !== '>') {
        if (cursor >= text.length) fail('Unclosed element');
        if (text[cursor] === ';' || text[cursor] === ',') { cursor++; space(); continue; }
        const start = cursor;
        if (!content && !'<[{('.includes(text[cursor])) {
          const key = name();
          space();
          if (text[cursor] === ':') {
            cursor++;
            element.attrs[key] = value(depth + 1);
            space();
            continue;
          }
          cursor = start;
        }
        content = true;
        element.children.push(value(depth + 1));
        space();
      }
      cursor++;
      return element;
    }
    if ((ch === 'b' || ch === 't') && text[cursor + 1] === "'") {
      cursor += 2;
      const end = text.indexOf("'", cursor);
      if (end < 0) fail('Unclosed binary/datetime');
      const raw = text.slice(cursor, end);
      cursor = end + 1;
      if (ch === 't') return new MarkScalar('datetime', raw);
      const compact = raw.replace(/\s/g, '');
      if (compact.startsWith('\\64')) {
        const decoded = atob(compact.slice(3));
        return new MarkBinary(Uint8Array.from(decoded, byte => byte.charCodeAt(0)));
      }
      const hex = compact.replace(/^\\x/, '');
      if (hex.length % 2 || !/^[\da-f]*$/i.test(hex)) fail('Invalid binary payload');
      return new MarkBinary(Uint8Array.from(hex.match(/../g) || [], byte => parseInt(byte, 16)));
    }
    const token = name();
    if (token === 'null') return null;
    if (token === 'true' || token === 'false') return token === 'true';
    if (/^[+-]?(?:inf|nan)$/.test(token)) return new MarkScalar('float', token);
    if (/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[nm]?$/.test(token)) {
      if (token.endsWith('n')) return new MarkScalar('integer', token);
      if (token.endsWith('m')) return new MarkScalar('decimal', token);
      if (/[.eE]/.test(token) || token === '-0') return new MarkScalar('float', token);
      const number = Number(token);
      return Number.isSafeInteger(number) ? number : new MarkScalar('integer', token);
    }
    return symbol(token);
  }
  const result = value();
  space();
  if (cursor !== text.length) fail('Unexpected trailing input');
  return result;
}

export function encodeMark(value) {
  const seen = new Set();
  const quoted = text => JSON.stringify(String(text));
  const key = name => /^[A-Za-z_][A-Za-z_\d-]*$/.test(name) ? name : quoted(name);
  function encode(item, depth = 0) {
    if (depth > 64) throw new RangeError('Mark nesting limit');
    if (item === null) return 'null';
    if (typeof item === 'string') return quoted(item);
    if (typeof item === 'boolean') return String(item);
    if (typeof item === 'number') {
      if (Number.isNaN(item)) return 'nan';
      if (!Number.isFinite(item)) return item < 0 ? '-inf' : 'inf';
      return Object.is(item, -0) ? '-0.0' : String(item);
    }
    if (item instanceof MarkSymbol) {
      return "'" + item.name.replace(/\\/g, '\\\\').replace(/'/g, "\\'")
        .replace(/\n/g, '\\n').replace(/\r/g, '\\r').replace(/\t/g, '\\t') + "'";
    }
    if (item instanceof MarkScalar) {
      if (item.kind === 'datetime') {
        if (/[\x00-\x1f'\\]/.test(item.text)) throw new TypeError('Invalid datetime literal');
        return `t'${item.text}'`;
      }
      if (!/^[+-]?(?:(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?[nm]?|inf|nan)$/.test(item.text))
        throw new TypeError('Invalid numeric literal');
      return item.text;
    }
    if (item instanceof MarkBinary) {
      const hex = Array.from(item.bytes, byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
      return `b'\\x${hex}'`;
    }
    if (!item || typeof item !== 'object') throw new TypeError('Expected a Lambda data value');
    if (seen.has(item)) throw new TypeError('Live cycles require inspector handles');
    seen.add(item);
    let result;
    if (item instanceof MarkElement) {
      const attrs = Object.entries(item.attrs).map(([name, child]) => `${key(name)}: ${encode(child, depth + 1)}`);
      const children = item.children.map(child => encode(child, depth + 1));
      result = `<${key(item.name)}${attrs.length ? ' ' + attrs.join(', ') : ''}${children.length ? '; ' + children.join('; ') : ''}>`;
    } else if (Array.isArray(item)) result = `[${item.map(child => encode(child, depth + 1)).join(', ')}]`;
    else result = `{${Object.entries(item).map(([name, child]) => `${key(name)}: ${encode(child, depth + 1)}`).join(', ')}}`;
    seen.delete(item);
    return result;
  }
  return encode(value);
}
