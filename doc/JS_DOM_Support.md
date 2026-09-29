# LambdaJS — JavaScript and DOM Support

LambdaJS is Lambda's embedded JavaScript engine. It shares the Lambda runtime's value representation, garbage collector, AST interpreter and MIR JIT, and it runs page scripts inside the Radiant viewer as well as standalone Node-style programs.

> **Status:** usable and measured, still labelled experimental. Conformance is tracked against TC39 test262 (ES2024 scope), the Node.js test suite and Web Platform Tests subsets. The figures below are the committed baselines, which the test suites regenerate; the design is documented in [doc/dev/js/JS_00_Overview.md](dev/js/JS_00_Overview.md).

## Usage

```bash
lambda script.js                       # run a .js / .mjs / .cjs / .ts / .tsx file
lambda js script.js                    # the explicit subcommand
lambda js -e "console.log(1 + 1)"      # evaluate a snippet (-p prints the result)
lambda js app.js --document page.html  # load an HTML document for DOM access
lambda ts app.ts                       # TypeScript
lambda view page.html                  # page scripts run inside the Radiant viewer
```

Other `js` options: `--input-type=module` (read a module from stdin), `--unhandled-rejections=strict|none`, `--stack-size=KB`, `--diagnose`, `--opt-level=N`. `lambda js` with no file does nothing and exits 0. See [Lambda_CLI.md](Lambda_CLI.md#js--javascript).

## Conformance

| Suite | Baseline | Where |
|---|---|---|
| TC39 test262, ES2024 scope | **40,261 passing** of 42,889 (2,628 skipped as ES2025+ or out of scope), 0 failing | `test/js262/test262_baseline.txt` |
| Node.js official tests | 3,450 passing | `test/node/official_baseline.txt` |
| Web Platform Tests | 9 baselines: DOM nodes, DOM ranges, CSSOM view, CSS transitions, forms, HTML reflection, input events, intersection observer, resize observer | `test/wpt/*_baseline.txt` |
| Lambda's own JS scripts | 416 script/expected-output pairs | `test/test_js_gtest.cpp` |

### What the test262 baseline covers

Everything in the ES2015–ES2024 language and standard library that is in scope passes: classes with private members, generators and async generators, `async`/`await` and Promises with a real microtask queue, ES modules and dynamic `import()`, `Proxy`/`Reflect`, `Symbol`, `WeakMap`/`WeakSet`/`WeakRef`/`FinalizationRegistry`, `BigInt`, typed arrays with `DataView`, `Atomics` and `SharedArrayBuffer`, `RegExp` including lookbehind and backreferences (a backtracking matcher sits beside the RE2 fast path), `JSON` with reviver/replacer/space, `structuredClone`, timers, and spec-compliant `delete`, property attributes and prototype semantics.

Not covered yet: `Intl` (the object exists; the intl402 suite is not in the baseline), and ES2025-or-later features, which the baseline skips.

## DOM and Web Platform

The DOM lives in `lambda/dom/` and is realm-neutral: the same nodes are driven by JavaScript (`el.querySelector(...)`) and by Lambda (`import dom`, and the `radiant.*` behaviours the `dom` package uses). The catalogue is 351 operations plus events, CSSOM, canvas, XHR/fetch, `FormData`, observers, selection and clipboard. Highlights:

- **Nodes and traversal**: the `Node`/`Element`/`Document` APIs, `querySelector[All]`, `matches`/`closest`, `cloneNode`, `insertAdjacent*`, fragments, comments, `importNode`/`adoptNode`, `createElementNS` with the namespace recorded.
- **Events**: dispatch with capture and bubbling, `addEventListener` options, `preventDefault`, input/keyboard/pointer events, `MutationObserver`, `IntersectionObserver`, `ResizeObserver`.
- **CSSOM and layout**: `element.style` get/set with camelCase mapping, `getComputedStyle` over the full cascade, stylesheet objects, `classList` and `dataset`, `getBoundingClientRect`, `offsetWidth`/`clientHeight` and `scrollTop`/`scrollLeft` computed from a lazy layout pass.
- **Document**: `document.cookie`, `readyState`, `title`, `URL`/`location`, `write`, `innerHTML`/`outerHTML` (parsing HTML fragments), forms and form controls, selection and ranges, clipboard.

The full surface is described in [doc/dev/js/JS_13_Web_DOM.md](dev/js/JS_13_Web_DOM.md).

## Node.js Compatibility

A Node.js compatibility layer provides CommonJS `require()` and ES `import` with Node resolution (`node_modules`, `package.json` exports), 25+ core modules implemented natively (`fs`, `path`, `os`, `crypto`, `zlib`, `net`, `http`, `events`, `stream`, `buffer`, `url`, `util`, `child_process`, …), and the `process` object. The `node-core`, `node-fs`, `node-net`, `node-crypto` and `node-zlib` modules ship as native modules beside the executable. `node:vm` is not provided. Design: [doc/dev/js/JS_14_Node_Compat.md](dev/js/JS_14_Node_Compat.md); tests: `test/node/` (231 scripts) and the official baseline above.

## Performance

Benchmarks are re-run on every release under `test/benchmark/`; the current report is `Overall_Result49.md` (2026-09-24, Apple Silicon, Node.js v22.13.0, 63 benchmarks in 7 suites, median of 3 runs of each benchmark's self-reported time).

| Engine | Geometric mean vs Node.js (V8) | Note |
|---|---:|---|
| LambdaJS, pinned MIR JIT | 7.80× slower | faster than Node on 7 of 63 rows |
| LambdaJS, default `auto` tier | 8.66× slower | the shipped configuration |
| Lambda Script, MIR typed | 0.63× (faster) | the same workloads written in Lambda |

By suite, LambdaJS is close to Node on the recursion-heavy R7RS set (1.07×) and far behind on JetStream (25.8×) and the text-processing rows (24.9×). V8 wins where its optimizing compiler and generational GC matter most: numeric loops, class-heavy code with hot property access, allocation-heavy workloads and string processing. LambdaJS wins where it can hand work to Lambda's native engines (regular expressions, JSON, some string kernels). Use JavaScript on LambdaJS for compatibility with existing code and page scripts; write performance-sensitive code in Lambda Script.

## Architecture

```
JavaScript / TypeScript source
        │
        ▼
  first-party C parser (lexer + recursive-descent/Pratt)  →  unified AST
        │
        ├── AST interpreter (default tier)
        └── MIR lowering (hot functions, or --tier=jit) → native code
        │
        ▼
  JS runtime (lambda/js: objects, prototypes, builtins, RegExp, event loop, modules)
        │
        ▼
  DOM and web APIs (lambda/dom, shared with Lambda) → Radiant (layout, paint, events)
```

`lambda/js` is about 145K lines of C++ and `lambda/dom` about 34K. Values are Lambda `Item`s, so a page script and a Lambda transformation operate on literally the same objects; the compilation pipeline is described in [JS_01_Compilation_Pipeline.md](dev/js/JS_01_Compilation_Pipeline.md) and the value model in [JS_03_Value_Model.md](dev/js/JS_03_Value_Model.md).
