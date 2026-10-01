## Lambda Script 0.3 (alpha)

**Released: October 2026** | [GitHub](https://github.com/henry-luo/lambda)

Lambda 0.3 alpha is the largest release so far: about 6,500 commits over the six months since 0.2 (March 2026):

- **Lambda, the language**, now has a normative specification, enforced type annotations, value semantics for mutable data, structured concurrency, and a new execution pipeline: a hand-written parser, an AST interpreter, and automatic tiering into the MIR JIT.
- **Radiant** grew from a layout engine into an interactive document and UI engine: events, selection, forms, rich-text editing, animation, page scripting, basic web browsing, and reactive UI written in Lambda.
- **LambdaJS** grew from an experimental transpiler into an ES2024 JavaScript engine that passes 40,261 Test262 tests and shares its runtime, DOM and event loop with Lambda.

Together, Lambda and Radiant are now one **parser-to-pixel, end-to-end pipeline**. Every input format is parsed into a single Lambda/Mark tree; Lambda transforms and validates it; Radiant styles, lays out, paints and makes interactive that same tree, with no separate DOM or render tree in between.

[![Lambda and Radiant document pipeline](https://raw.githubusercontent.com/henry-luo/lambda/master/doc/img/lambda-radiant-pipeline.svg)](https://github.com/henry-luo/lambda/blob/master/doc/Lambda_Doc_Pipeline.md)

> This is an alpha. Syntax and semantics changed substantially since 0.2 and may change again; 0.2 scripts need migration (see [Breaking changes since 0.2](#breaking-changes-since-02)).

---

### Lambda

#### Language

- **Pipes and queries**: `|>` maps, `|:` filters, `that` is a one-value proviso; for-expressions gain `group by … into` and joins.
  `users |: ~.age >= 18 |> ~.name`
- **Array operations**: element-wise comparison (`[1, 2, 3] gt 1`), mask and index-array indexing, the `last` index, and computed keys.
- **String patterns**: delimited `\(…)` patterns, usable wherever a type is.
  `type Email = \(w+ "@" w+ "." a{2,6})`
- **Error handlers**: `expr ^ { … }` handles a failure in place.
- **Path literals** redesigned for files, URLs and system data.

#### Type system

- **Enforced annotations**: each is proven statically, rejected statically, or checked at run time.
- **Typed arrays** `T[]`, `T[n]`, `T[][]` with unboxed storage.
- **New number model**: saturating 53-bit `int`, sized integers and floats (`255u8`), arbitrary-precision `integer` and `decimal`.
- **Constrained types** (`{age: int} that (age >= 18)`), type operators in expressions, and distinct `fn`/`pn` function types.

#### Procedures and concurrency

- **Mutable value semantics**: assignment copies (copy-on-write underneath); `var` parameters are the only write-through.
- **Pure functions**: an `fn` never calls a `pn`.
- **Structured concurrency**: tasks with `start`, `wait`, `select`, mailboxes and cancellation, without `async`/`await`.
- **Document updates (preview)**: `put`/`del` statements and transactions with `commit`/`rollback`.
- **Reactive UI**: `view`/`edit` templates with `state` and `on` handlers.

#### Compiler and runtime

- **Hand-written C parser** replaces Tree-sitter.
- **AST interpreter with automatic tiering** into the MIR JIT, now the default. MIR Direct is the only back end; `--c2mir` is removed.
- **Precise garbage collection** replaces conservative stack scanning.
- **Native value lanes** cut boxing in typed code, on an in-house pool and arena memory system.

#### Packages and tooling

- **Modules**: a `lambda.*` root, parent imports, and two-way imports with JavaScript.
- **New packages**: `graph` (Mermaid, Graphviz, D2, Structurizr), `pdf` (experimental), `dom`, `editor` and `edit`.
- **New inputs**: SQLite, Structurizr/C4, TikZ and TSV.
- **`lambda edit`**: rich-text editing for Markdown and HTML, drawing for SVG, with lossless saving.
- **Formal specifications** for semantics and design, and a ten-chapter tutorial with checked examples.

#### Breaking changes since 0.2

The changes most likely to break, or silently change, a 0.2 script:

- The pipe `|` is now `|>`, and the old `|>`/`|>>` file-output operators are replaced by `output()`.
- Filtering with `xs that p` or `where` is now `xs |: p`. Old `that` code still parses but means something different.
- Error destructuring (`let a^err = …`) is removed; use `expr ^ { … }`. `input()` and `parse()` raise and must be handled with `^`.
- Assignment copies; `let` containers are immutable; `var` exists only inside `pn`.
- Number literals: suffix `n` now means `integer` and `m` means `decimal`; negative indexes return null (use `last`).
- Elements separate attributes from content with `,` (was `;`); object literals are written `<Point x: 1>`.
- A line that starts with `(`, `[`, `-` or `<` after a complete statement needs a `;` before it.
- String patterns are written `type D = \(d+)` (was `string D = \d+`).

---

### Radiant

#### Layout

- **Multi-column layout** rewritten, with balancing, spanners and break control.
- **Text layout**: UAX #14 line breaking, CJK wrapping, hyphenation, `text-wrap: balance` and `line-clamp`.
- **Custom layout in Lambda**, which now drives diagram layout (Mermaid, Graphviz, D2, Structurizr).
- **Also new**: scroll-following sticky positioning, incremental relayout, Shadow DOM, `popover` and logical properties.
- **Conformance against Chrome**: Bootstrap 20/20, Tailwind 70/70, WPT CSS Display 145/145, Tables 57/57, Images 56/56.

#### Rendering

- **Paint IR and display list** shared by the raster, SVG and PDF outputs, with retained dirty-region repaint.
- **Animation**: a vsync frame clock, CSS `@keyframes`, a first set of transitions, `requestAnimationFrame`, GIF and Lottie.
- **Own SVG renderer and font engine**: FreeType is removed; WOFF, WOFF2 and colour glyphs are supported.
- **Effects and content**: blend modes, filters, `clip-path`, 3D transforms, a Canvas 2D subset, and experimental PDF viewing.

#### Interaction and editing

- **Unified event pipeline** across JavaScript listeners and Lambda handlers, with IME.
- **Selection, form controls and `contenteditable`**, with editing policy written in Lambda.
- **Third-party editors**: CodeMirror 6, ProseMirror and Editor.js run within tested capability sets.
- **UI automation**: JSON event scenarios, headless mode and `lambda replay`.

#### DOM and application shell

- **One tree**: the parsed Lambda element tree is the DOM, the layout tree and the render tree.
- **Shared DOM core** (351 operations) driven from both JavaScript and Lambda.
- **Page scripting** on LambdaJS: classic, deferred, async and module scripts.
- **Basic web browsing** in `lambda view`: http(s) pages, navigation history, parallel loading, cache and cookies.

---

### Lambda JS

#### Conformance

- **Test262: 40,261 tests pass, none fail**, at ES2024 scope (42,889 discovered; 2,628 skipped as out of scope). 0.2 had no Test262 run at all.
- **Out of scope for now**: ES2025+ additions, `intl402`, Temporal, decorators and proper tail calls.

#### Engine

- **Two tiers, automatic by default**: a new AST interpreter runs code first and promotes hot functions to the MIR JIT.
- **First-party parser** for JavaScript and TypeScript, replacing Tree-sitter.
- **TypeScript**: `lambda ts file.ts` runs TypeScript directly (annotations are stripped; there is no type checking).
- **One runtime with Lambda**: JS values are Lambda values, with the same precise GC, typed-array storage and event loop. Lambda can import `.js` modules and JavaScript can import `.ls` modules.
- **Headless DOM scripting outside the browser**: `lambda js app.js --document page.html` gives a script the real DOM, CSS cascade and layout metrics.
- **Web APIs**: events, observers, CSSOM, `fetch`, XMLHttpRequest, storage, history, selection and clipboard.
- **Pure-JS libraries in the test suite**: lodash, ramda, rxjs, immer, marked, handlebars, acorn, ajv, zod and others.
-  **Performance**: 7.4× Node.js time on the 63-benchmark suite, on par with QuickJS (7.2×) for compute. LambdaJS remains experimental and is not yet tuned for property-heavy or string-heavy code.


---

### Benchmarks

[![Lambda benchmark history](https://raw.githubusercontent.com/henry-luo/lambda/master/test/benchmark/benchmark_history.svg)](https://github.com/henry-luo/lambda/blob/master/test/benchmark/Overall_Result50.md)

The latest run (Result50, 2026-09-30) covers 63 benchmarks across the R7RS, AWFY, BENG, KOSTYA, LARCENY and JetStream suites plus Text, against Node.js v22.13.0. Ratios are geometric means of each engine's time over Node's; below 1.0× is faster than Node.

| Engine | Workload time | End to end, default tier |
|---|---:|---:|
| **Lambda, typed** | **0.54×** | 1.32× |
| Lambda, untyped | 1.05× | 1.41× |
| LambdaJS | 7.43× | 8.86× |
| QuickJS (reference) | 7.24× | 1.26× |

- **Workload time** is each benchmark's self-timed work on the pinned JIT. Typed Lambda beats Node on 42 of 63 benchmarks, untyped on 32.
- **End to end** is wall clock from launch to exit on the shipped auto tier, so start-up and compilation are included.
- The suite has grown since 0.2 (56 benchmarks then), so these figures are not directly comparable with the 0.2 release notes.

See the [full report](https://github.com/henry-luo/lambda/blob/master/test/benchmark/Overall_Result50.md) for per-benchmark results.
