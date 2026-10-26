import test from 'node:test';
import assert from 'node:assert/strict';
import { once } from 'node:events';
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { InspectorTransport, MAX_BYTES } from '../../utils/devtool/inspector-transport.cjs';
import { InspectorClient } from '../../utils/devtool/src/inspector/client.mjs';
import { decodeMark, encodeMark, MarkElement, MarkSymbol, MarkScalar, MarkBinary, symbol } from '../../utils/devtool/src/inspector/mark.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const fixture = 'test/devtools/document.html';
const require = createRequire(new URL('../render/package.json', import.meta.url));
const { PNG } = require('pngjs');

function target({ document = fixture, ...options } = {}) {
  const transport = new InspectorTransport(root, document, options);
  const client = new InspectorClient(transport);
  let output = '';
  transport.on('output', bytes => { output = (output + bytes).slice(-10000); });
  async function close() {
    client.disconnect();
    const exited = transport.child.exitCode !== null || transport.child.signalCode !== null;
    const exit = exited ? Promise.resolve() : once(transport.child, 'exit');
    transport.close();
    const timeout = setTimeout(() => transport.stop(), 2000);
    await exit;
    clearTimeout(timeout);
  }
  return { transport, client, close, output: () => output };
}

async function withTarget(callback, hello = true, options = {}) {
  const active = target(options);
  try {
    if (hello) await active.client.request('hello', { version: 1 });
    await callback(active);
  } catch (error) {
    error.message += active.output() ? `\nTarget output:\n${active.output()}` : '';
    throw error;
  } finally { await active.close(); }
}

const withScratch = callback => withTarget(callback, true, { scratch: true });

test('Lambda scratch advertises native REPL operations and excludes document and JS evaluation', async () => {
  await withScratch(async ({ client }) => {
    const hello = await client.request('hello', { version: 1 });
    assert.deepEqual(hello.value.ops.map(op => op.name), ['hello', 'eval', 'bindings', 'inspect', 'release', 'gc', 'source']);
    await assert.rejects(client.request('tree'), error => error.code === 'unsupported');
    await assert.rejects(client.request('eval', { source: 'var x = 1; window.x' }));
    assert.equal((await client.request('eval', { source: '6 * 7' })).value.result.value, 42);
  });
});

test('Lambda scratch persists declarations, rejects entries transactionally and lists live bindings', async () => {
  await withScratch(async ({ client }) => {
    assert.equal((await client.request('eval', { source: 'let answer = 40' })).value.declarations, true);
    assert.equal((await client.request('eval', { source: 'answer + 2' })).value.result.value, 42);
    await assert.rejects(client.request('eval', { source: 'unknown_variable + 1' }));
    assert.equal((await client.request('eval', { source: 'answer' })).value.result.value, 40);
    const bindings = await client.request('bindings');
    assert.equal(bindings.value.entries.find(row => row.name === 'answer').value.value, 40);
    assert.equal((await client.request('bindings', { offset: 999 })).value.entries.length, 0);
    const source = (await client.request('source')).value;
    assert.equal(source.kind.name, 'scratch_session');
    assert(source.text.includes('let answer = 40'));
    assert(!source.text.includes('unknown_variable'));
  });
});

test('Lambda results preserve exact scalar kinds and passive previews do not call functions', async () => {
  await withScratch(async ({ client }) => {
    for (const source of ["'ready'", '9007199254740993n', '12.50m', '1.0', '-0.0', '"Unicode λ"', "b'\\x00FF'"]) {
      const reply = await client.request('eval', { source });
      assert.deepEqual(reply.value.result.value, decodeMark(source));
      if (source.endsWith('n')) assert.equal(reply.value.result.type.name, 'integer');
      if (source.endsWith('m')) assert.equal(reply.value.result.type.name, 'decimal');
    }
    await client.request('eval', { source: 'fn never_call() => 1 / 0' });
    const fn = (await client.request('eval', { source: 'never_call' })).value.result;
    assert.equal(fn.opaque, true);
    assert.equal(fn.type.name, 'function');
  });
});

test('Lambda values page maps, arrays, elements and ranges with native keys and stable aliases', async () => {
  await withScratch(async ({ client }) => {
    await client.request('eval', { source: 'let shared = {label: "λ", values: [1, 2, 3]}' });
    const result = (await client.request('eval', { source: '[shared, shared]' })).value.result;
    const first = (await client.request('inspect', { ref: result.ref, limit: 1 })).value;
    const second = (await client.request('inspect', { ref: result.ref, offset: first.next, limit: 1 })).value;
    assert.equal(first.entries[0].value.ref, second.entries[0].value.ref);
    const fields = (await client.request('inspect', { ref: first.entries[0].value.ref })).value.entries;
    assert.equal(fields.find(row => row.key === 'label').value.value, 'λ');
    const array = fields.find(row => row.key === 'values').value;
    const items = (await client.request('inspect', { ref: array.ref })).value.entries;
    assert.deepEqual(items.map(row => row.value.value), [1, 2, 3]);
    const element = (await client.request('eval', { source: '<section title: "test", "hello"; <span "child">>' })).value.result;
    const content = (await client.request('inspect', { ref: element.ref })).value;
    assert.equal(content.entries[0].kind.name, 'attribute');
    assert.equal(content.entries[0].key, 'title');
    assert.equal(content.entries[1].value.value, 'hello');
    assert.equal(content.entries[2].value.tag, 'span');
    const range = (await client.request('eval', { source: '1 to 10000' })).value.result;
    const page = (await client.request('inspect', { ref: range.ref, offset: 9998 })).value;
    assert.equal(page.count, 10000);
    assert.deepEqual(page.entries.map(row => row.value.value), [9999, 10000]);
    const chars = (await client.request('eval', { source: '"α" to "ω"' })).value.result;
    await client.request('gc');
    const letters = (await client.request('inspect', { ref: chars.ref })).value.entries;
    assert.deepEqual(letters.slice(0, 2).map(row => row.value.value), ['α', 'β']);
    assert.equal(letters.at(-1).value.value, 'ω');
  });
});

test('retained Lambda values survive forced GC, release precisely and never reuse handles', async () => {
  await withScratch(async ({ client }) => {
    const value = (await client.request('eval', { source: '{temporary: ["kept", 42]}' })).value.result;
    assert((await client.request('gc')).value.retained > 0);
    const nested = (await client.request('inspect', { ref: value.ref })).value.entries[0].value;
    await client.request('gc');
    assert.equal((await client.request('inspect', { ref: nested.ref })).value.entries[0].value.value, 'kept');
    await client.request('release', { refs: [value.ref, nested.ref, value.ref] });
    assert.equal((await client.request('gc')).value.retained, 0);
    await assert.rejects(client.request('inspect', { ref: value.ref }), error => error.code === 'stale');
    const next = (await client.request('eval', { source: '[99, 100]' })).value.result;
    assert(next.ref > nested.ref);
    for (const fields of [{ limit: 0 }, { limit: 257 }, { offset: -1 }])
      await assert.rejects(client.request('inspect', { ref: next.ref, ...fields }), error => error.code === 'invalid');
  });
});

test('Lambda long text is paged at UTF-8 boundaries and scratch sessions stay independent', async () => {
  await withScratch(async a => withScratch(async b => {
    await a.client.request('eval', { source: 'let private_value = 7' });
    await assert.rejects(b.client.request('eval', { source: 'private_value' }));
    const text = 'λ'.repeat(3000);
    const result = (await a.client.request('eval', { source: encodeMark(text) })).value.result;
    assert.equal(result.count, 6000);
    const first = (await a.client.request('inspect', { ref: result.ref, limit: 4095 })).value;
    const last = (await a.client.request('inspect', { ref: result.ref, offset: first.next })).value;
    assert.equal(first.value + last.value, text);
    await assert.rejects(a.client.request('inspect', { ref: result.ref, offset: 1 }), error => error.code === 'invalid');
    assert.equal((await b.client.request('bindings')).value.count, 0);
  }));
});

test('Lambda source, binary and typed array inspection preserve bounded pages and strided order', async () => {
  await withScratch(async ({ client }) => {
    const typed = (await client.request('eval', { source: 'transpose([[1, 2, 3], [4, 5, 6]])' })).value.result;
    assert.deepEqual(typed.shape, [3, 2]);
    const page = (await client.request('inspect', { ref: typed.ref })).value;
    assert.deepEqual(page.entries.map(row => row.value.value), [1, 4, 2, 5, 3, 6]);
    const bytes = new Uint8Array(5000); bytes[4999] = 255;
    const binary = (await client.request('eval', { source: encodeMark(new MarkBinary(bytes)) })).value.result;
    const tail = (await client.request('inspect', { ref: binary.ref, offset: 4998 })).value;
    assert.deepEqual([...tail.value.bytes], [0, 255]);
    const first = (await client.request('source')).value;
    assert(first.count > 4096);
    assert(first.next <= 4096);
    const next = (await client.request('source', { offset: first.next })).value;
    assert(next.text.length > 0);
    for (const fields of [{ source: 1 }, { source: 'x'.repeat(65537) }])
      await assert.rejects(client.request('eval', fields), error => error.code === 'invalid');
  });
});

test('Radiant ancestor paths reveal nodes beyond the first child page', async () => {
  const directory = await fs.mkdtemp(path.join(root, 'temp/devtools-path-'));
  try {
    const document = path.join(directory, 'page.html');
    await fs.writeFile(document, '<html><body><ul>' + Array.from({ length: 150 }, (_, i) => `<li id="row-${i}">${i}</li>`).join('') + '</ul></body></html>');
    await withTarget(async ({ client }) => {
      const root = (await client.request('tree')).value;
      const body = root.children.find(row => row.name === 'body');
      const ul = (await client.request('tree', { node: body.node })).value.children[0];
      const page = (await client.request('tree', { node: ul.node, offset: 100 })).value;
      const row = page.children[25];
      const path = (await client.request('path', { node: row.node })).value;
      assert.deepEqual(path.map(part => part.node.name), ['html', 'body', 'ul', 'li']);
      assert.equal(path.at(-1).offset, 125);
      assert.equal(path.at(-1).node.value.attrs.id, 'row-125');
      assert.equal(path.at(-1).node.node, row.node);
    }, true, { document });
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});

test('Lambda retained-value limits recover by releasing all values without losing bindings', async () => {
  await withScratch(async ({ client }) => {
    await client.request('eval', { source: 'let large = (for (i in 1 to 5000) {index: i})' });
    const root = (await client.request('eval', { source: 'large' })).value.result;
    let limited = false;
    for (let offset = 0; offset < 5000; offset += 256) {
      try { await client.request('inspect', { ref: root.ref, offset }); }
      catch (error) { assert.equal(error.code, 'limit'); limited = true; break; }
    }
    assert(limited);
    await client.request('release', { all: true });
    assert.equal((await client.request('gc')).value.retained, 0);
    assert.equal((await client.request('eval', { source: 'len(large)' })).value.result.value, 5000);
    await assert.rejects(client.request('release', { all: true, refs: [] }), error => error.code === 'invalid');
  });
});

test('large exact Lambda numbers stay bounded without recursively formatting their digits', async () => {
  await withScratch(async ({ client }) => {
    const value = (await client.request('eval', { source: '9'.repeat(5000) + 'n' })).value.result;
    assert.equal(value.opaque, true);
    assert.equal(value.digits, 5000);
    assert(!Object.hasOwn(value, 'value'));
    assert.equal((await client.request('eval', { source: '42' })).value.result.value, 42);
  });
});

async function card(client) {
  const tree = await client.request('tree');
  const body = tree.value.children.find(row => row.name === 'body');
  const children = await client.request('tree', { node: body.node });
  return children.value.children.find(row => row.value instanceof MarkElement && row.value.attrs.id === 'card');
}

test('Mark codec preserves data kinds, Unicode, exact numbers, binary and elements', () => {
  const source = String.raw`{op: 'style', text: "quotes \" / λ / \u0000", values: [null, true, false, -0.0, nan, inf, 9007199254740993n, 12.50m, t'2026-10-26', b'\x00FF'], node: <div class: "card"; "hello"; <span; 'ready'>>}`;
  const original = decodeMark(source);
  assert(original.op instanceof MarkSymbol);
  assert(original.node instanceof MarkElement);
  assert.equal(original.values[6].text, '9007199254740993n');
  assert.equal(original.values[7].kind, 'decimal');
  assert(original.values[9] instanceof MarkBinary);
  assert.deepEqual([...original.values[9].bytes], [0, 255]);
  assert.deepEqual(decodeMark(encodeMark(original)), original);
  assert.equal(Object.getPrototypeOf(original), null);
});

test('Mark reader rejects malformed/trailing/deep data and encoder refuses live cycles', () => {
  for (const text of ['{id: 1', '[1,', '<div', '{x: 1, x: 2}', '{x: "\\uZZZZ"}', '{id: 1} trailing'])
    assert.throws(() => decodeMark(text), SyntaxError);
  assert.throws(() => decodeMark('['.repeat(70) + '0' + ']'.repeat(70)));
  const cycle = {}; cycle.self = cycle;
  assert.throws(() => encodeMark(cycle), /handles/);
  const data = decodeMark('{"__proto__": {safe: true}}');
  assert.equal(data.__proto__.safe, true);
  assert.equal({}.safe, undefined);
});

test('frontend Mark control/data literals are accepted by the native Mark decoder', async () => {
  const directory = await fs.mkdtemp(path.join(root, 'temp/devtools-codec-'));
  try {
    const literals = ["'operation'", '9007199254740993n', '12.50m', "t'2026-10-26'", "b'\\x00FF'", '<div class: "card"; "λ">', '[-0.0, nan, inf, -inf]', encodeMark('quotes " and\nUnicode λ\0')];
    for (const literal of literals) {
      const source = path.join(directory, 'source.mark'), output = path.join(directory, 'output.mark');
      await fs.writeFile(source, encodeMark(decodeMark(literal)));
      const result = spawnSync(process.env.LAMBDA_DEVTOOLS_EXE || path.join(root, 'lambda.exe'),
        ['--no-log', 'convert', source, '-f', 'mark', '-t', 'mark', '-o', output], { cwd: root, encoding: 'utf8' });
      assert.equal(result.status, 0, result.stderr);
      const decoded = decodeMark(await fs.readFile(output, 'utf8'));
      assert.deepEqual(decoded, decodeMark(literal));
    }
  } finally { await fs.rm(directory, { recursive: true, force: true }); }
});

test('hello uses native symbols and reports only implemented operations', async () => {
  await withTarget(async ({ client }) => {
    const reply = await client.request('hello', { version: 1 });
    assert.equal(reply.value.version, 1);
    assert(reply.value.ops.every(op => op instanceof MarkSymbol));
    assert(reply.value.ops.some(op => op.name === 'set_style'));
    assert(!reply.value.ops.some(op => op.name === 'pause'));
    await assert.rejects(client.request('pause'), error => error.code === 'unsupported');
  });
});

test('handshake, version, envelope and paging errors leave valid sessions usable', async () => {
  await withTarget(async ({ client }) => {
    await assert.rejects(client.request('tree'), error => error.code === 'hello');
    await assert.rejects(client.request('hello', { version: 2 }), error => error.code === 'version');
    await client.request('hello', { version: 1 });
  }, false);
  await withTarget(async ({ client }) => {
    for (const fields of [{ limit: 0 }, { limit: 501 }, { offset: -1 }, { limit: 'bad' }])
      await assert.rejects(client.request('tree', fields), error => error.code === 'invalid');
    assert((await client.request('tree')).value.children.length > 0);
  });
});

test('partial headers/payloads and coalesced frames produce distinct complete replies', async () => {
  const { transport, client, close } = target();
  client.disconnect();
  const messages = [];
  transport.on('message', text => messages.push(decodeMark(text)));
  const frame = text => {
    const body = Buffer.from(text), header = Buffer.alloc(4);
    header.writeUInt32BE(body.length);
    return Buffer.concat([header, body]);
  };
  try {
    const hello = frame("{id: 1, op: 'hello', version: 1}");
    for (let offset = 0; offset < hello.length; offset += 3) {
      transport.channel.write(hello.subarray(offset, offset + 3));
      await new Promise(resolve => setTimeout(resolve, 1));
    }
    transport.channel.write(Buffer.concat([frame("{id: 2, op: 'tree'}"), frame("{id: 3, op: 'style'}"), frame('{id: 4, op: "tree"}')]));
    const deadline = Date.now() + 10000;
    while (messages.length < 4 && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 10));
    assert.deepEqual(messages.map(message => message.id), [1, 2, 3, 4]);
    assert(messages[1].value.node.value instanceof MarkElement);
    assert.equal(messages[3].error.code.name, 'invalid');
  } finally { await close(); }
});

test('live tree pages preserve elements, attributes and fractional layout geometry', async () => {
  await withTarget(async ({ client }) => {
    const row = await card(client);
    assert(row.value instanceof MarkElement);
    assert.match(row.value.attrs['data-note'], /λ/);
    const style = await client.request('style', { node: row.node });
    assert.equal(style.value.computed.display, 'block');
    assert.equal(Number(style.value.box.width), 228);
    const partial = await client.request('tree', { offset: 1, limit: 1 });
    assert.equal(partial.value.children.length, 1);
    assert.equal(partial.value.children[0].name, 'body');
  });
});

test('inline edits commit geometry, publish changes and undo absent declarations', async () => {
  await withTarget(async ({ client }) => {
    const row = await card(client);
    const before = await client.request('style', { node: row.node });
    const events = [];
    client.onEvent(event => events.push(event));
    const edit = await client.request('set_style', { node: row.node, rev: before.rev, name: 'width', value: '300px' });
    assert.equal(edit.value.value, '');
    const after = await client.request('style', { node: row.node });
    assert.equal(Number(after.value.box.width) - Number(before.value.box.width), 100);
    assert(after.rev > before.rev);
    const rendered = await client.request('screenshot');
    const png = PNG.sync.read(Buffer.from(rendered.value.pixels.bytes));
    const edgeX = Math.floor(Number(after.value.box.x) + Number(after.value.box.width) - 1);
    const edgeY = Math.floor(Number(after.value.box.y) + 20);
    const edge = (edgeY * png.width + edgeX) * 4;
    assert.deepEqual([...png.data.subarray(edge, edge + 3)], [36, 89, 166]);
    assert(events.some(event => event.event.name === 'changed' && event.rev === after.rev));
    await assert.rejects(client.request('set_style', { node: row.node, rev: before.rev, name: 'width', value: '400px' }), error => error.code === 'conflict');
    await client.request('set_style', { node: row.node, rev: after.rev, ...edit.value });
    assert.equal(Number((await client.request('style', { node: row.node })).value.box.width), Number(before.value.box.width));
  });
});

test('CSS priority and invalid edits preserve authored state and undo', async () => {
  await withTarget(async ({ client }) => {
    const row = await card(client);
    let state = await client.request('style', { node: row.node });
    await client.request('set_style', { node: row.node, rev: state.rev, name: 'width', value: '250px', priority: 'important' });
    state = await client.request('style', { node: row.node });
    assert.match(state.value.inline, /important/);
    const edit = await client.request('set_style', { node: row.node, rev: state.rev, name: 'width', value: '280px' });
    assert.equal(edit.value.priority, 'important');
    state = await client.request('style', { node: row.node });
    await assert.rejects(client.request('set_style', { node: row.node, rev: state.rev, name: 'width', value: 'nonsense' }), error => error.code === 'invalid');
    assert.equal((await client.request('style', { node: row.node })).rev, state.rev);
    await client.request('set_style', { node: row.node, rev: state.rev, ...edit.value });
    assert.match((await client.request('style', { node: row.node })).value.inline, /important/);
  });
});

test('picker uses native transformed hit geometry and highlight matches committed bounds', async () => {
  await withTarget(async ({ client }) => {
    const tree = await client.request('tree');
    const body = tree.value.children.find(row => row.name === 'body');
    const children = await client.request('tree', { node: body.node });
    const transformed = children.value.children.find(row => row.value?.attrs?.id === 'transformed');
    const box = (await client.request('highlight', { node: transformed.node })).value;
    assert.equal(Number(box.x) % 1, 0.5);
    const hit = await client.request('pick', { x: Number(box.x) + Number(box.width) / 2, y: Number(box.y) + Number(box.height) / 2 });
    assert.equal(hit.value.node, transformed.node);
  });
});

test('screenshots contain native PNG bytes and causal CSS edits change pixels', async () => {
  await withTarget(async ({ client }) => {
    const row = await card(client);
    const before = await client.request('screenshot');
    assert(before.value.pixels instanceof MarkBinary);
    const pngBefore = PNG.sync.read(Buffer.from(before.value.pixels.bytes));
    let state = await client.request('style', { node: row.node });
    await client.request('set_style', { node: row.node, rev: state.rev, name: 'background-color', value: 'rgb(255, 0, 0)' });
    const after = await client.request('screenshot');
    const pngAfter = PNG.sync.read(Buffer.from(after.value.pixels.bytes));
    const x = Math.floor(Number(row.box.x) + 5), y = Math.floor(Number(row.box.y) + 5);
    const pixel = (y * pngAfter.width + x) * 4;
    assert.notDeepEqual([...pngBefore.data.subarray(pixel, pixel + 3)], [...pngAfter.data.subarray(pixel, pixel + 3)]);
    assert.deepEqual([...pngAfter.data.subarray(pixel, pixel + 3)], [255, 0, 0]);
    assert.equal(pngBefore.width, pngAfter.width);
  });
});

test('released weak handles are stale and are never reassigned', async () => {
  await withTarget(async ({ client }) => {
    const row = await card(client);
    await client.request('release', { refs: [row.node] });
    await assert.rejects(client.request('style', { node: row.node }), error => error.code === 'stale');
    const newRow = await card(client);
    assert(newRow.node > row.node);
    await client.request('release', { refs: [row.node, row.node] });
  });
});

test('independent targets do not share document revisions or style edits', async () => {
  await withTarget(async a => withTarget(async b => {
    const [one, two] = await Promise.all([card(a.client), card(b.client)]);
    const state = await a.client.request('style', { node: one.node });
    await a.client.request('set_style', { node: one.node, rev: state.rev, name: 'width', value: '350px' });
    assert.equal(Number((await b.client.request('style', { node: two.node })).value.box.width), 228);
    assert.equal((await b.client.request('style', { node: two.node })).rev, 1);
  }));
});

test('malformed, oversized, zero and invalid UTF-8 frames close cleanly', async () => {
  for (const payload of [Buffer.from('not_a_map'), Buffer.from('{id: 0}'), Buffer.from([0xff]), Buffer.from('abc\0def'), null, false]) {
    const active = target();
    active.client.disconnect();
    try {
      const closed = once(active.transport, 'closed');
      const header = Buffer.alloc(4);
      header.writeUInt32BE(payload === null ? MAX_BYTES + 1 : payload === false ? 0 : payload.length);
      active.transport.channel.write(header);
      if (payload) active.transport.channel.write(payload);
      const timeout = setTimeout(() => active.transport.stop(), 10000);
      await closed;
      clearTimeout(timeout);
      assert(active.transport.ended);
    } finally { await active.close(); }
  }
});

test('disconnect rejects outstanding requests and target death leaves the client responsive', async () => {
  await withTarget(async ({ client, transport }) => {
    const pending = client.request('tree');
    const rejected = assert.rejects(pending, /detached|exited|disconnected/);
    transport.stop();
    await rejected;
    await assert.rejects(client.request('tree'), /disconnected/);
  });
});
