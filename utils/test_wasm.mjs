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
    ['index_of("xxxxxxxxxxxxxxxx the the", "the")', '17'],
    ['contains("xxxxxxxxxxxxxxxx the the", "THE")', 'false'],
    ['replace("the the the", "the", "a")', '"a a a"'],
    ['contains("é 🌍 thé end", "thé")', 'true'],
    ['(parse("{\\"a\\": 1}", "json")^).a', '1'],
    ['parse_html_fragment("<p>Hello &amp; world</p>")', '<body\n  <p\n    "Hello & world">>'],
    ['parse("<p>Hello</p>", "html5")^', '<#document\n  <html\n    <head>\n    <body\n      <p\n        "Hello">>>>'],
    ['parse("<!doctype html><p>Hi</p>")^', '<#document\n  <#doctype name: "html">\n  <html\n    <head>\n    <body\n      <p\n        "Hi">>>>'],
    // HTML retains CSS text and resource URLs as data without parsing or acquisition.
    ['parse("<html><head><style>p { color: red }</style></head><body><p style=\'color: blue\'>Hi &amp; 🌍</p><img src=\'https://example.com/a.png\'></body></html>", "html")^',
      '<#document\n  <html\n    <head\n      <style\n        "p { color: red }">>\n    <body\n      <p style: "color: blue",\n        "Hi & 🌍">\n      <img src: "https://example.com/a.png">>>>'],
    // JS catalog spellings remain ordinary keys when its records are excluded.
    ['let names = {constructor: 1, prototype: 2, name: "n", toString: 3, valueOf: 4}', 'null'],
    ['[names.constructor, names.prototype, names.name, names.toString, names.valueOf]', '[1, 2, "n", 3, 4]'],
    ['(parse("{\\"Symbol.iterator\\": 7}", "json")^)["Symbol.iterator"]', '7'],
    ['date(2026, 10, 6)', "t'2026-10-06'"],
    ['datetime(0).unix', '0'], ['datetime(-1).unix', '-1'],
    ['datetime(-2203977600000).unix', '-2203977600000'],
    ['datetime(951782400000).unix', '951782400000'],
    ['let held = {a: [1, 2, 3], s: "kept"}', 'null'],
    ['let kept = (for (i in 1 to 10) {i * 2})', 'null'],
    ['fn make_adder(n) { fn inner(a) => a + n; inner }', 'null'],
    ['let add10 = make_adder(10)', 'null'], ['add10(5)', '15'],
    ['(parse(format({a: 1}, "json"), "json")^).a', '1'],
    ['reshape([1, 2, 3, 4], [2, 2])', '[[1, 2], [3, 4]]'],
    ['view int { ~ * 2 }', 'null'], ['apply(42)', '84'], ['apply(7, {})', '14'],
    ['view string { "hi:" ++ ~ }; apply("world")', '"hi:world"'],
    ['view <meter> state count: 5 { count }', 'null'],
    ['let meter_model = <meter>', 'null'], ['apply(meter_model)', '5'],
    ['view <meter2> state count: 7 { count }', 'null'], ['apply(<meter2>)', '7'],
  ];
  for (const [source, expected] of cases) equal(evaluate(source), expected, source);
  // repeated allocation crosses collection pressure while REPL bindings remain rooted.
  for (let i = 0; i < 80; i++) {
    equal(evaluate('sum(for (i in 1 to 4000) {i * 2})'), '16004000', 'allocation pressure');
  }
  equal(evaluate('held.a[2]'), '3', 'retained container');
  equal(evaluate('kept[2]'), '6', 'retained allocated array');
  equal(evaluate('add10(7)'), '17', 'retained closure');
  equal(evaluate('names.toString'), '3', 'retained ordinary key');
  equal(evaluate('apply(42)'), '84', 'retained view after allocation pressure');
  equal(evaluate('apply(meter_model)'), '5', 'retained template state after allocation pressure');
  equal(diagnostics.length, 0, 'successful evaluations produce no diagnostic dumps');
  for (const source of ['datetime()', 'date()', 'justnow()',
    'load("a.png")', 'input("file:///etc/passwd")^',
    'import "https://example.com/a.ls"', 'import lambda.latex',
    'parse("x", "pdf")^', 'parse("x", "latex")^',
    'parse("p { color: red }", "css")^', 'parse("p { color: red }", {type: "css"})^',
    'parse("digraph { a -> b }", "graph")^',
    'parse("digraph { a -> b }", {type: "graph", flavor: "dot"})^',
    'parse("graph TD; A-->B", {type: "graph", flavor: "mermaid"})^',
    'parse("a -> b", {type: "graph", flavor: "d2"})^',
    'parse("workspace {}", {type: "graph", flavor: "structurizr"})^',
    'parse("x+1", "math")^', 'parse("x+1", "math-ascii")^',
    'parse("x+1", "math-latex")^',
    'parse("x+1", {type: "math", flavor: "ascii"})^',
    'parse("x+1", {type: "math", flavor: "latex"})^',
    'format(<p "Hi">, "css")', 'format(<graph <node id: "a">>, "graph")',
    'format(<p "Hi">, "latex")',
    'format(<math "x+1">, "math")', 'format(<math "x+1">, "math-ascii")',
    ...['dot', 'mermaid', 'd2', 'structurizr'].map(flavor =>
      `format(<graph <node id: "a">>, {type: "graph", flavor: "${flavor}"})`),
    ...['latex', 'typst', 'ascii', 'mathml'].flatMap(flavor => [
      `format(<math "x+1">, {type: "math", flavor: "${flavor}"})`,
      `format(<math "x+1">, "math-${flavor}")`]),
    'undo()', 'redo()', 'edit_commit()', 'edit_commit("description")',
    'pn no_emit() { emit("event", {}) }',
    // uncalled wrappers must reject at admission, not merely fail on bad image arguments.
    ...['convolve', 'blur', 'erode', 'dilate', 'median_filter', 'maxpool', 'avgpool',
      'as_float', 'as_ubyte', 'invert', 'gamma', 'threshold', 'grayscale', 'flip',
      'rot90', 'crop', 'histogram', 'otsu', 'label', 'resize', 'rotate', 'affine_warp']
      .map(name => {
        const arity = ['as_float', 'as_ubyte', 'invert', 'grayscale', 'otsu', 'label'].includes(name)
          ? 1 : ['crop', 'resize'].includes(name) ? 3 : 2;
        return `fn excluded_${name}(a, b, c) => ${name}(${['a', 'b', 'c'].slice(0, arity).join(', ')})`;
      }),
    'fetch("https://example.com")^',
    'pn no_tasks() { sleep(1) }', 'pn no_clock() { now() }']) {
    equal(evaluate(source), 'error', source);
    equal(evaluate('twice(x)'), '82', 'session after rejection');
  }
  equal(evaluate('view int { ~ * 3 }; raise error("rejected")'), 'error', 'failed view fragment');
  equal(evaluate('apply(42)'), '84', 'prior template survives rollback');
  equal(evaluate('view bool { "uncommitted" }; raise error("rejected")'), 'error', 'failed new view kind');
  equal(evaluate('apply(true)'), 'true', 'failed template is not registered');
  equal(module.ccall('lambda_wasm_reset', 'number', [], []), 1, 'reset');
  const beforeSourceError = diagnostics.length;
  equal(evaluate('let ='), 'error', 'rejected syntax after reset');
  equal(diagnostics.length > beforeSourceError, true, 'source errors survive disabled logging');
  equal(evaluate('x'), 'error', 'old binding after reset');
  equal(evaluate('1 + 2'), '3', 'evaluation after reset');
  equal(evaluate('apply(42)'), '42', 'old templates after reset');
  equal(evaluate('view int { ~ + 1 }'), 'null', 'new template after reset');
  equal(evaluate('apply(42)'), '43', 'new template applies after reset');
  equal(evaluate('view <meter> state count: 9 { count }'), 'null', 'new stateful template after reset');
  equal(evaluate('apply(<meter>)'), '9', 'fresh template state after reset');
  module.ccall('lambda_wasm_shutdown', null, [], []);
  equal(module.ccall('lambda_wasm_init', 'number', [], []), 1, 'reinitialize');
  equal(evaluate('1 + 2'), '3', 'new session');
  module.ccall('lambda_wasm_shutdown', null, [], []);
  return {checks, diagnostics: diagnostics.length};
}

const wasmBinary = await readFile(new URL('../build/wasm/lambda-wasm.wasm', import.meta.url));
const glue = await readFile(new URL('../build/wasm/lambda-wasm.mjs', import.meta.url), 'utf8');
const {sources} = JSON.parse(await readFile(
  new URL('../build/wasm/lambda-wasm.size.json', import.meta.url), 'utf8'));
// rejection alone would miss disabled parsers accidentally compiled into the artifact.
for (const source of sources) {
  assert.ok(!/^lambda\/input\/input-(css|graph[.-])/.test(source), `excluded input: ${source}`);
  assert.ok(!/^lambda\/input\/css\/(css_(engine|font_face|paged_media|parser|tokenizer|value_parser)|selector_matcher)\.cpp$/.test(source),
    `excluded CSS parser dependency: ${source}`);
  assert.ok(!/^lambda\/format\/format-(css|graph|latex|math(?:-[^/]+)?)\.cpp$/.test(source),
    `excluded formatter: ${source}`);
  assert.ok(!/^lambda\/input\/input-math/.test(source), `excluded math parser: ${source}`);
  assert.notEqual(source, 'lambda/input/css/css_formatter.cpp', 'excluded CSS formatter dependency');
}
for (const unit of ['parser', 'token', 'tokenizer', 'tree_builder']) {
  assert.ok(sources.includes(`lambda/input/html5/html5_${unit}.cpp`), `missing HTML5 ${unit}`);
}
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
