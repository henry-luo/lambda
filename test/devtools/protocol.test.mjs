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

function target() {
  const transport = new InspectorTransport(root, fixture);
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

async function withTarget(callback, hello = true) {
  const active = target();
  try {
    if (hello) await active.client.request('hello', { version: 1 });
    await callback(active);
  } catch (error) {
    error.message += active.output() ? `\nTarget output:\n${active.output()}` : '';
    throw error;
  } finally { await active.close(); }
}

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
