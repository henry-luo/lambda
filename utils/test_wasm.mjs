import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import createLambda from '../build/wasm/lambda-wasm.mjs';

// exercise the same session contracts in Node and a real browser instance.
async function checkSession(create, wasmBinary) {
  const diagnostics = [];
  const module = await create({wasmBinary, locateFile: () => 'host-supplied.wasm',
    print: line => diagnostics.push(line), printErr: line => diagnostics.push(line)});
  let checks = 0;
  const equal = (actual, expected, source) => {
    if (actual !== expected) throw new Error(`${source}: expected ${expected}, got ${actual}`);
    checks++;
  };
  const evaluate = source => module.ccall('lambda_wasm_eval', 'string', ['string'], [source]);
  equal(module.ccall('lambda_wasm_init', 'number', [], []), 1, 'init');
  const cases = [
    ['1 + 2', '3'],
    ['let x = 41', 'null'], ['x + 1', '42'],
    ['fn twice(a) => a * 2', 'null'], ['twice(x)', '82'],
    ['[1, 2, 3]', '[1, 2, 3]'],
    ['0.1m + 0.2m', '0.3'],
    ['9223372036854775806n + 1n', '9223372036854775807'],
    ['"héllo 🌍"', '"héllo 🌍"'],
    ['("5" is \\(d))', 'true'],
    ['(parse("{\\"a\\": 1}", "json")^).a', '1'],
    ['date(2026, 10, 6)', "t'2026-10-06'"],
    ['datetime(0).unix', '0'], ['datetime(-1).unix', '-1'],
    ['datetime(-2203977600000).unix', '-2203977600000'],
    ['datetime(951782400000).unix', '951782400000'],
    ['let held = {a: [1, 2, 3], s: "kept"}', 'null'],
    ['let kept = (for (i in 1 to 10) {i * 2})', 'null'],
    ['fn make_adder(n) { fn inner(a) => a + n; inner }', 'null'],
    ['let add10 = make_adder(10)', 'null'], ['add10(5)', '15'],
  ];
  for (const [source, expected] of cases) equal(evaluate(source), expected, source);
  // repeated allocation crosses collection pressure while REPL bindings remain rooted.
  for (let i = 0; i < 80; i++) {
    equal(evaluate('sum(for (i in 1 to 4000) {i * 2})'), '16004000', 'allocation pressure');
  }
  equal(evaluate('held.a[2]'), '3', 'retained container');
  equal(evaluate('kept[2]'), '6', 'retained allocated array');
  equal(evaluate('add10(7)'), '17', 'retained closure');
  for (const source of ['datetime()', 'date()', 'justnow()',
    'load("a.png")', 'input("file:///etc/passwd")^',
    'import "https://example.com/a.ls"', 'import lambda.latex',
    'parse("x", "pdf")^', 'parse("x", "latex")^', 'fetch("https://example.com")^',
    'pn no_tasks() { sleep(1) }', 'pn no_clock() { now() }']) {
    equal(evaluate(source), 'error', source);
    equal(evaluate('twice(x)'), '82', 'session after rejection');
  }
  equal(module.ccall('lambda_wasm_reset', 'number', [], []), 1, 'reset');
  equal(evaluate('x'), 'error', 'old binding after reset');
  equal(evaluate('1 + 2'), '3', 'evaluation after reset');
  module.ccall('lambda_wasm_shutdown', null, [], []);
  equal(module.ccall('lambda_wasm_init', 'number', [], []), 1, 'reinitialize');
  equal(evaluate('1 + 2'), '3', 'new session');
  module.ccall('lambda_wasm_shutdown', null, [], []);
  return {checks, diagnostics: diagnostics.length};
}

const wasmBinary = await readFile(new URL('../build/wasm/lambda-wasm.wasm', import.meta.url));
const glue = await readFile(new URL('../build/wasm/lambda-wasm.mjs', import.meta.url), 'utf8');
// provider implementations must be absent; explicit denial imports may remain.
for (const provider of ['new Date', 'Date.now', 'Intl.DateTimeFormat', 'navigator.language',
  'process.env', 'getRandomValues', 'performance.now', '__setitimer_js__deps']) {
  assert.ok(!glue.includes(provider), `unexpected ambient provider: ${provider}`);
}
console.log('wasm-test: Node', await checkSession(createLambda, wasmBinary));

if (process.argv.includes('--browser')) {
  const {default: puppeteer} = await import('puppeteer');
  assert.ok(process.env.CHROME_HEADLESS_SHELL, 'set CHROME_HEADLESS_SHELL for browser tests');
  const browser = await puppeteer.launch({executablePath: process.env.CHROME_HEADLESS_SHELL,
    headless: true});
  try {
    const page = await browser.newPage();
    const result = await page.evaluate(async ({loader, bytes, check}) => {
      const unavailable = () => { throw new Error('ambient provider accessed'); };
      globalThis.fetch = unavailable;
      globalThis.XMLHttpRequest = unavailable;
      globalThis.WebSocket = unavailable;
      globalThis.Date = unavailable;
      Intl.DateTimeFormat = unavailable;
      performance.now = unavailable;
      crypto.getRandomValues = unavailable;
      const {default: create} = await import(loader);
      const binary = Uint8Array.from(atob(bytes), c => c.charCodeAt(0));
      return await (0, eval)(`(${check})`)(create, binary);
    }, {loader: 'data:text/javascript;base64,' + Buffer.from(glue).toString('base64'),
      bytes: wasmBinary.toString('base64'), check: checkSession.toString()});
    console.log('wasm-test: Chromium', result);
  } finally {
    await browser.close();
  }
}
