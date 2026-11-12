# LambdaJS — JavaScript and DOM Support

LambdaJS is Lambda's embedded JavaScript engine. It shares the Lambda runtime's value representation, garbage collector, AST interpreter and MIR JIT, and it runs page scripts inside the Radiant viewer as well as standalone Node-style programs.

> **Status:** usable and measured, still labelled experimental. Conformance is tracked against TC39 test262 (ES2024 scope) and Web Platform Tests subsets. The figures below are the committed baselines, which the test suites regenerate; the design rulings are in [Lambda_Formal_Design.md](Lambda_Formal_Design.md) D8.4.

## Usage

```bash
lambda js script.js                    # run a .js / .mjs / .cjs file
lambda js -e "console.log(1 + 1)"      # evaluate a snippet (-p prints the result)
lambda js app.js --document page.html  # load an HTML document for DOM access
lambda ts app.ts                       # TypeScript: annotations are stripped, not checked
lambda view page.html                  # page scripts run inside the Radiant viewer
```

The `js` or `ts` subcommand is required: a bare `lambda script.js` is read as a Lambda script and fails to parse. JSX is not supported.

Other `js` options: `--input-type=module` (read a module from stdin), `--unhandled-rejections=strict|none`, `--stack-size=KB`, `--diagnose`, `--opt-level=N`. `lambda js` with no file does nothing and exits 0. See [Lambda_CLI.md](Lambda_CLI.md#js--javascript).

## Conformance

| Suite | Baseline | Where |
|---|---|---|
| TC39 test262, ES2024 scope | **40,261 passing** of 42,889 (2,628 skipped as ES2025+ or out of scope), 0 failing | `test/js262/test262_baseline.txt` |
| Node.js official tests | no current figure: the committed baseline (3,450 entries) was recorded in July 2026, before the built-in module set was reduced (see [Node.js Compatibility](#nodejs-compatibility)) | `test/node/official_baseline.txt` |
| Web Platform Tests | 9 baselines: DOM nodes, DOM ranges, CSSOM view, CSS transitions, forms, HTML reflection, input events, intersection observer, resize observer | `test/wpt/*_baseline.txt` |
| Lambda's own JS scripts | 416 script/expected-output pairs | `test/test_js_gtest.cpp` |

### What the test262 baseline covers

Everything in the ES2015–ES2024 language and standard library that is in scope passes: classes with private members, generators and async generators, `async`/`await` and Promises with a real microtask queue, ES modules and dynamic `import()`, `Proxy`/`Reflect`, `Symbol`, `WeakMap`/`WeakSet`/`WeakRef`/`FinalizationRegistry`, `BigInt`, typed arrays with `DataView`, `Atomics` and `SharedArrayBuffer`, `RegExp` including lookbehind and backreferences (a backtracking matcher sits beside the RE2 fast path), `JSON` with reviver/replacer/space, `structuredClone`, timers, and spec-compliant `delete`, property attributes and prototype semantics.

Not covered yet: `Intl` (the object exists but is not locale-aware, and the intl402 suite is not in the baseline), proper tail calls, `Temporal`, and ES2025-or-later features such as iterator helpers, which the baseline skips.

## DOM and Web Platform

The DOM lives in `lambda/dom/` and is realm-neutral: the same nodes are driven by JavaScript (`el.querySelector(...)`) and by Lambda (`import dom`, and the `radiant.*` behaviours the `dom` package uses). The catalogue is 351 operations plus events, CSSOM, canvas, XHR/fetch, `FormData`, observers, selection and clipboard. Highlights:

- **Nodes and traversal**: the `Node`/`Element`/`Document` APIs, `querySelector[All]`, `matches`/`closest`, `cloneNode`, `insertAdjacent*`, fragments, comments, `importNode`/`adoptNode`, `createElementNS` with the namespace recorded.
- **Events**: dispatch with capture and bubbling, `addEventListener` options, `preventDefault`, input/keyboard/pointer events, `MutationObserver`, `IntersectionObserver`, `ResizeObserver`.
- **Initial geometry observation**: standalone exports obtain the first layout through the shared DOM checkpoint even when page scripts never read geometry. Host-driven commit guards and the selected virtual frame remain authoritative (**D9.1.1v4 / D9.1.3v2 / D10.1.7**); this does not claim complete observer conformance.
- **CSSOM and layout**: `element.style` get/set with camelCase mapping, `getComputedStyle` over the full cascade, stylesheet objects, `classList` and `dataset`, `getBoundingClientRect`, `offsetWidth`/`clientHeight` and `scrollTop`/`scrollLeft` computed from a lazy layout pass.
- **Document**: `document.cookie`, `readyState`, `title`, `URL`/`location`, `write`, `innerHTML`/`outerHTML` (parsing HTML fragments), forms and form controls, selection and ranges, clipboard.

The design record is [Lambda_Design_DOM_API.md](../vibe/Lambda_Design_DOM_API.md).

## Node.js Compatibility

`lambda js` runs a script the way `node` would for a core subset of Node.js. The built-ins ship as native modules beside the executable (`node-core`, `node-fs`, `node-net` and `node-crypto` in the standard set).

- **Module loading**: CommonJS `require()` and ES `import` of relative and absolute files, JSON, and directories with a `package.json` `main` or an `index.js`; `module.exports`, `__filename`, `__dirname`, and the `node:` prefix. Bare package names are not resolved: there is no `node_modules` lookup, no `package.json` `exports`, and no package installer.
- **Built-in modules that load**: `fs` and `fs/promises`, `path`, `os`, `url`, `querystring`, `string_decoder`, `punycode`, `buffer`, `crypto`, `timers` and `timers/promises`, `module`, `constants`, `perf_hooks`, `v8`, `tty`, `worker_threads`, and a small `util` (`promisify`, `format`, `inspect`, `inherits`, `types`).
- **Globals**: `process` (reports `v20.0.0`), `console`, `Buffer`, timers and `setImmediate`, `performance`, `fetch`, `URL`, `TextEncoder`/`TextDecoder`, `AbortController`, `structuredClone`.
- **Not available**: `events`, `stream`, `http`, `https`, `net`, `tls`, `zlib`, `child_process`, `assert`, `readline`, `cluster`, `dns`, `vm`, `async_hooks` and `node:test`. `require()` of any of them throws "Cannot find module". An earlier, wider layer covered these; it was removed in September 2026.

Tests: `test/node/` (231 scripts).

## Performance

Benchmarks are re-run on every release under `test/benchmark/`; the current report is `Overall_Result50.md` (2026-09-30, Apple Silicon, Node.js v22.13.0, 63 benchmarks in 7 suites, median of 3 runs).

| Engine | Geometric mean vs Node.js (V8) | Note |
|---|---:|---|
| LambdaJS, pinned MIR JIT | 7.43× slower | each benchmark's self-reported workload time; faster than Node on 6 of 63 rows; QuickJS is 7.24× on the same rows |
| LambdaJS, default `auto` tier | 8.86× slower | the shipped configuration, timed end to end (start-up and compilation included; 62 of 63 rows); QuickJS is 1.26× on this measure |
| Lambda Script, MIR typed | 0.54× (faster) | the same workloads written in Lambda, self-reported workload time |

By suite, LambdaJS is level with Node on the recursion-heavy R7RS set (0.99×) and far behind on JetStream (26.7×) and the text-processing rows (23.4×). V8 wins where its optimizing compiler and generational GC matter most: class-heavy code with hot property access, allocation-heavy workloads and string processing. The six rows where LambdaJS is ahead are small recursive and numeric kernels (`tak`, `cpstak`, `sum`, `sumfp`, `sieve`) and `pidigits`. Use JavaScript on LambdaJS for compatibility with existing code and page scripts; write performance-sensitive code in Lambda Script.

## Architecture

```
JavaScript / TypeScript source
        │
        ▼
  first-party C parser (lexer + recursive-descent/Pratt)  →  unified AST
        │
        ├── AST interpreter (default tier)
        └── MIR lowering (hot functions, or JS_EXEC_BACKEND=mir) → native code
        │
        ▼
  JS runtime (lambda/js: objects, prototypes, builtins, RegExp, event loop, modules)
        │
        ▼
  DOM and web APIs (lambda/dom, shared with Lambda) → Radiant (layout, paint, events)
```

`lambda/js` is about 150K lines (131K of C++; the rest is the C parser, the builtin catalogue and generated Unicode tables) and `lambda/dom` about 34K. Values are Lambda `Item`s, so a page script and a Lambda transformation operate on literally the same objects; the compilation tiers are in [Lambda_Formal_Design.md](Lambda_Formal_Design.md) D8.4 and the value model in D2.
