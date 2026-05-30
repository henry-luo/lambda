# Transpile_Js52 - Fix 4 Engine Gaps Surfaced By Library Suite

Date: 2026-05-30

Status: 4/4 gaps root-caused; 3/4 fixed cleanly; marked/ajv still have additional downstream bugs deferred to Js53.

## Implementation Status

| Phase | Status | Result |
|---|---|---|
| P0 — Baseline | ✅ landed | 39,258 / 39,258 passing, 137.7s runtime captured |
| P1 — ESM aliased exports | ✅ landed | js262 clean, 131.6s (−4.4%); chalk@5 native ESM loads; new `test/js/module_aliased_exports.js` added |
| P2 — Regex `<!--`/`-->` content | ✅ landed | js262 clean, 135.4s (−1.7%); root cause was `js_normalize_source_for_parser` in `lambda/js/js_scope.cpp:629` (rewrote `<!--` → `//--` unconditionally; now skips inside regex literals via ST_REGEX state); new `test/js/regex_html_comment_chars.js` added |
| P3+P4 — Class constructor closure captures | ✅ root cause landed | js262 clean, 138.7s (+0.7%). Single root cause for both P3 and P4: `jm_transpile_new_expr` in `lambda/js/js_mir_statement_lowering.cpp:2981` rebuilt the constructor closure at the `new` call site, looking up captures in the CURRENT scope. When `new C()` was called from outside the class's defining scope, captured names (e.g. `exports` from an IIFE) resolved to nothing, so the constructor body read `undefined` for any closure variable. Fix: when `capture_count > 0`, fetch the pre-built `__ctor__` from the class object (which has captures already bound from class-def time) instead of rebuilding. Methods already worked correctly because their function value was stored on the prototype at def time. Marked's `_Lexer.lex` and ajv's `new Ajv()` first-hop both unblocked; subsequent hops still have additional bugs (§5b). |
| P5 — lib suite tightening | ✅ partial | `lib_chalk.js` converted to native ESM `import chalk from './chalk_src.js'` (P1 unblocked); js262 stays at 39,258 / 0 / 0. markdown-it / marked / ajv lib tests remain blocked by §5b bugs. |

Js52 fixes four distinct engine gaps surfaced while authoring the new
`lib_*.js` tests under `test/js/` (chalk, immer, snarkdown, tv4, acorn, rxjs,
bn.js). Each gap is independently verifiable from a self-contained repro and
each maps to a small, isolatable change area. The plan deliberately orders
phases from lowest blast radius to highest, and each phase ends with a full
js262 run so any correctness or performance regression is caught at the
phase boundary instead of compounding across phases.

## 1. Goals And Non-Goals

Goals:

- Make ESM aliased exports (`export { x as y }`, `export { x as default }`)
  work as a third-party bundle target expects.
- Fix regex-literal tokenization for patterns that contain the byte sequence
  `-->`, so widely shipped UMD bundles parse.
- Eliminate the segfault in `marked.parse()` and make it reachable through
  the JS API.
- Make `new Function`-based codegen validators (ajv, jsen) executable so
  schema validators that generate function bodies dynamically run end-to-end.

Non-goals:

- No new language features. The four gaps are pre-existing semantics that
  third-party libraries already assume.
- No engine refactor beyond what each fix requires.
- No changes to the test262 baseline scope. The baseline acceptance gate is
  unchanged: 0 failures, 0 regressions, 0 non-fully-passing tests, and the
  release runtime must stay within a small drift band of the current
  baseline runtime.
- No new `lib_*.js` test additions in this proposal. The existing seven new
  lib tests are the change signal; markdown-it / marked / ajv / jsen lib
  tests are written and confirmed-passing only after their respective phases
  land.

## 2. Starting Baseline

Current checked-in release baseline (from Js51, file
`vibe/jube/Transpile_Js51_Es2022_CrossRealm.md`):

```text
# Scope: ES2023 (skip ES2024+ features)
# Total passing: 39258
# Total tests: 42295  Skipped: 3037  Batched: 39258  Passed: 39258  Failed: 0
# Runtime: 147.5s total (prep 0.0s + exec 147.4s)
# Batch size: batched 50 tests/process; async 50 tests/process
```

The Js52 acceptance bar is:

- Passing count stays `>= 39258` after every phase.
- Regressions count is `0` at every phase boundary.
- `t262_partial.txt` stays empty.
- Total runtime stays within `+5%` of the current baseline (147.5s). Any
  phase that exceeds this is paused and re-scoped before continuing.

## 3. The Four Gaps

Each gap below has a self-contained repro under `temp/js52_repros/`. The
repros are the contracts Js52 commits to fix; library tests are the
downstream confirmation.

### Gap A - ESM aliased exports drop the alias name

Repro (`temp/js52_repros/A_export_alias.js`):

```js
const x = 42;
export default x;
export const y = 'hello';
export { x as renamed };
```

And consumer:

```js
import def, { y, renamed } from './A_export_alias.js';
console.log('default:', def);     // 42       — works
console.log('y:', y);             // hello    — works
console.log('renamed:', renamed); // undefined — BUG, expected 42
```

Likely root cause area: `lambda/js/build_js_ast.cpp:2438`
`build_js_export_statement`. The export-specifier loop at
`build_js_ast.cpp:2510` reads only the `name` field of each
`export_specifier` Tree-sitter node and ignores its `alias` field. The
resulting `JsExportNode.specifiers` chain therefore stores the local
identifier under its own name, not under the requested alias, so the module
binding is published under the wrong external name.

Real-world impact:

- `export { x as default }` is the dominant default-export pattern in
  modern esm.sh / esbuild / rollup output. With the alias dropped, every
  such bundle imports as `undefined`.
- `chalk@5.3.0` native ESM is unloadable today only because of this; Js52's
  test for chalk currently uses a stripped-bundle workaround. After Gap A
  is fixed, the workaround can be removed in a follow-up.

### Gap B - Regex-literal tokenization fails on patterns containing `-->`

Repro (`temp/js52_repros/B_regex_arrow.js`):

```js
const arr = [
  /^<!--/,
  /-->/,
  true
];
console.log(arr.length); // expected 3; currently SyntaxError before reaching this line
```

Lambda reports `SyntaxError: Invalid left-hand side in prefix/postfix
operation` while reading the second regex literal. The pattern body
`-->` is being lexed as JavaScript tokens `--` followed by `>`, which
implies the regex-vs-division disambiguation either:

- mis-identifies the leading `/` as a division operator in this position, or
- treats `-->` as the HTML-style single-line-comment terminator that V8
  accepts at start-of-line in script-source mode, then re-enters expression
  parsing inside the regex body.

Likely root cause area: the lexer regex-vs-division state machine. The
production is in `lambda/tree-sitter-typescript/grammar.js` plus the
generated `parser.c` external scanner; the regex literal external-scanner
hook is what decides whether `/` opens a regex. The scanner needs to commit
to regex mode when the previous significant token is a `,` or `[` or `(`
(it already does for many cases), and once committed it must not interpret
`-->` as an HTML comment terminator.

Real-world impact:

- `markdown-it@13` and `@14` both have HTML-block tables containing
  `[ /^<!--/, /-->/, true ]`. Both versions currently fail to load.
- Any code generator that emits HTML-tokenizer regex tables hits this.

### Gap C - `marked.parse()` segfaults at runtime

Repro (`temp/js52_repros/C_marked_parse.js`): load
`marked@12.0.2/lib/marked.umd.js` via the standard CJS shim, then call
`marked.parse('hello world')`. Exit code is 139 (SIGSEGV) with no stack
emitted; even a wrapping `try/catch` cannot intercept it. The bundle loads
cleanly — `Object.keys(module.exports)` returns the full API surface and
`typeof marked.parse` is `'function'` — so the crash is inside the parser
walk, not at module init.

Likely root cause area: the segfault is a native-code crash, so it is in
runtime code, not in transpiled user code. Candidates, in order of prior
probability based on Lambda's code base:

1. Deep recursion in the marked tokenizer overflowing the C stack (marked
   uses recursive descent through inline and block lexers; large or deeply
   nested input is a known hot path).
2. A GC nursery promotion path triggered by repeated string concatenation
   inside marked's renderer; a mishandled `Item` may be promoted while a
   raw pointer is held on the stack.
3. A specific Lambda string-builder primitive used by marked's
   `Tokenizer.prototype.inlineText` that does not handle a particular
   sentinel value emitted by tokens.

Diagnostic plan in P3 below. The fix may be a single missing null/empty
check or a GC root pin; the proposal does not pre-commit to a specific
shape until P3 reduces the crash to a minimal in-engine repro.

Real-world impact:

- `marked` is one of the top-3 markdown libraries on npm; today Lambda
  cannot use it at all.
- A segfault that even `try/catch` cannot catch is the worst class of
  engine bug because it cannot be paved around in user space.

### Gap D - `new Function`-generated bodies fail to resolve names

Repro (`temp/js52_repros/D_newfn_codegen.js`):

```js
// Minimal codegen-style usage:
const refs = { check: (x) => typeof x === 'string' };
const body = "return refs.check(data);";
const fn = new Function('refs', 'data', body);
console.log(fn(refs, 'hi'));  // expected true
console.log(fn(refs, 42));    // expected false
```

The basic `new Function('a','b','return a+b')` form already works
(verified). The failure is restricted to bodies that:

- declare local helpers (`function _validate0(data) { ... }`) and then call
  them from a top-level `return` expression, or
- reference identifiers expected to come from arguments while the body uses
  destructuring or `function`-declaration hoisting inside the generated
  source.

Concrete observed failure modes:

- `ajv@6.12.6`: `new A()` itself throws `Uncaught TypeError: is not a
  function` during `new Function`-driven validator-compiler construction.
  The thrown error has no callee name.
- `jsen@0.6.6`: validator compiles, but invoking it raises
  `Uncaught ReferenceError: data is not defined`, indicating the generated
  function body cannot see its declared parameter.

Likely root cause area: `new Function` constructor in `lambda/js/js_globals.cpp`
and/or the MIR lowering path used for the resulting function body
(`lambda/js/js_mir_function_class_lowering.cpp`). Two failure patterns to
chase:

1. The constructor parses the body as an expression rather than a
   FunctionBody (the spec requires the body to be parsed as a
   `FunctionBody`, with its own lexical environment and parameter
   bindings).
2. Parameter names are correctly registered but the generated function
   loses them when the body itself contains a `function` declaration that
   would hoist over the parameter slot.

Real-world impact:

- All JSON-schema validators that emit code (ajv, jsen, fast-json-stringify)
  are blocked.
- Any template engine that compiles to a function (e.g. handlebars'
  pre-compile path) is at risk of the same bug.

## 4. Phase Plan

The phases are ordered by blast radius — the smaller and more localized the
change, the earlier it lands. Every phase ends with the same js262 guard
(commands in §5).

### P0 - Baseline Capture

Goal: establish the exact pre-Js52 numbers everything is measured against,
so any regression in any later phase is unambiguous.

Work:

- Commit the repros under `temp/js52_repros/A_export_alias.js`,
  `B_regex_arrow.js`, `C_marked_parse.js`, `D_newfn_codegen.js`. These are
  the contracts Js52 fixes.
- Run release `test_js_test262_gtest.exe` with `--run-async` against the
  current baseline; capture failure tsv, pass count, and total runtime.
- Run debug `test_js_gtest.exe` over the existing `test/js/lib_*.js` suite
  to capture the current 7-lib green baseline (chalk via globals workaround,
  bn.js, immer, snarkdown, tv4, acorn, rxjs).

Acceptance:

- `temp/js52_p0_release_guard.tsv` exists with `Failed: 0`,
  `Regressions: 0`, `Passing: 39258 (+/- 0)`.
- Recorded baseline runtime number drops into `vibe/jube/Transpile_Js52.md`
  for use as the +5% ceiling in later phases.

No engine changes in P0.

### P1 - Gap A: ESM Aliased Exports

Risk class: smallest. Single AST-build site. Pure ESM code path.

Work:

1. In `build_js_ast.cpp:build_js_export_statement`, extend the export-clause
   loop to also read field `alias` from each `export_specifier`. Store both
   the local name and the alias on the specifier node. A fresh node type or
   a `JsAstNode` shape extension is acceptable as long as the change is
   confined to ESM export construction.
2. Update the module emit path (search for where `JsExportNode.specifiers`
   is consumed) to publish the binding under the alias name when present,
   under the local name otherwise.
3. Apply the same change to ES-re-exports (`export { a as b } from '...'`).
   The current AST already records `source`; only the specifier loop needs
   the alias-aware version.
4. Verify with the existing `temp/probe_export.js` repro and with a fresh
   `test/js/module_export_alias.js` + `.txt` pair that imports an
   aliased export and prints both forms.

Risk controls:

- Do not change the CJS shim path used by `lib_bn.js` / `lib_tv4.js` etc.
- Do not change behavior when no alias is present.
- Stage with a debug-build run of `test/js/module_*.js` before touching
  release.

Acceptance:

- Repro A prints `42` for `renamed`.
- Repro for `export { x as default }` lets `import def from '...'` resolve
  to the underlying value.
- All existing `test/js/module_*.js` tests still pass byte-for-byte.
- js262 release guard run is clean (see §5).

### P2 - Gap B: Regex-Literal Tokenization

Risk class: small-but-careful. The regex/division disambiguation is one of
the most regression-prone lex sites in any JS engine.

Work:

1. Reproduce with a one-line repro: `var r = /-->/;`. If this alone fails,
   the issue is independent of position; if only the bracketed form fails,
   the prior-token state machine is the bug.
2. Find the regex-literal external-scanner hook in
   `lambda/tree-sitter-typescript/` (or whichever scanner Lambda's TS
   grammar pulls in). The decision point is: when the previous significant
   token allows a regex (open-paren, open-bracket, comma, operator, etc.),
   commit to regex mode and consume to the unescaped closing `/`.
3. Specifically, inside the regex body the scanner must not treat `-->` as
   the V8 HTML-comment-end production. The HTML-comment-end production
   only applies at top level of Script source, not inside a regex literal.
4. Add direct grammar-level tests in `test/lambda/` covering:
   - `[ /^<!--/, /-->/, true ]` parses to a 3-element array.
   - `var x = /-->/.test('abc-->def');` returns `true`.
   - `var y = 1; var z = y --> 0;` still rejects (or accepts as the legacy
     HTML comment-out, whichever the existing baseline says).
5. Run `make generate-grammar` per the CLAUDE.md rule (no manual
   `parser.c` edits).

Risk controls:

- Compare full pre- and post- parses of every `regex_*` test in
  `test/js/regex_*.js` to make sure no regex literal changes shape.
- Run the focused js262 regex chapter manually before the full guard:
  `--batch-file=test/js262/regex.txt` (if present) or
  `--gtest_filter='*RegExp*'` on `test_js_gtest.exe`.

Acceptance:

- Repro B prints `3`.
- markdown-it@13 minified bundle loads to `typeof module.exports === 'function'`
  without parse errors.
- All existing `test/js/regex_*.js` tests pass byte-for-byte.
- js262 release guard run is clean.

### P3 - Gap C: marked.parse Segfault

Risk class: medium. Native crash; root-cause analysis comes first.

Work, in order:

1. **Reduce the repro.** Start from `temp/js52_repros/C_marked_parse.js`.
   Bisect marked's call graph by replacing `marked.parse` arms with no-ops
   until the crash disappears, then re-add one piece at a time. Goal: a
   pure-JS file under 200 lines that crashes Lambda with no marked
   bundle.
2. **Decide crash class.** Run the minimal repro under:
   - Debug build with ASAN (`make build` already enables ASAN for tests).
     ASAN will name the offending allocation or use-after-free.
   - LLDB with `bt full` on SIGSEGV — quickest path to the call site.
3. **Map to subsystem.** Expected suspects, in order:
   - GC nursery promotion during deep recursive evaluation. If ASAN points
     at a `Container` pointer, an Item lacked a GC root in a temporary
     slot. Fix: pin the relevant temporaries through the affected
     interpreter loop.
   - Stack overflow on recursive descent. Visible as a `__stack_chk_fail`
     symbol or large frame just before the fault. Fix: lift the recursion
     limit guard (`lambda/js/js_eval.cpp` style) to detect and throw
     `RangeError` instead of crashing.
   - String-builder primitive (`lib/strbuf.h`) overrun on a specific
     control-character input from markdown text.
4. **Fix at root cause.** No bypass that just makes `marked` not crash;
   the fix must apply to the class of input that triggered it.
5. **Lock the fix.** Add a focused C++ unit test in `test/test_js_gtest.cpp`
   covering the reduced repro, and a `test/js/lib_marked.js` + `.txt`
   pair once `marked.parse` runs through. The lib test does not need to
   match marked's full output across versions; it needs only to confirm
   that representative inputs produce non-empty HTML containing the
   expected tag names.

Risk controls:

- Do not rebase the GC, do not rework the recursion model. The change is a
  surgical fix at the call site identified by ASAN/LLDB.
- If the fix turns out to require a larger interpreter change, **stop**,
  write the finding to this proposal under §6, and split P3 into its own
  proposal Js53.

Acceptance:

- Reduced repro no longer crashes.
- `marked.parse('hello world').trim()` returns `<p>hello world</p>` or the
  equivalent shape marked actually emits.
- A new `test/js/lib_marked.js` is added with at least the
  paragraph/heading/list/code-block/link/blockquote assertions used in the
  failed Js51-era attempt.
- js262 release guard run is clean. Special attention to stack-recursive
  test files (eval depth, deeply nested expressions): the fix must not
  reduce the depth that test262 expects.

### P4 - Gap D: new Function Codegen

Risk class: medium. Touches Function constructor + body lowering.

Work, in order:

1. **Reduce to engine repros.** From the public lib failures:
   - Repro D1: `new Function('refs', 'data', 'return refs.check(data)')`
     — confirms parameter-name resolution.
   - Repro D2: `new Function('data', 'function _v(d) { return typeof d === "string"; } return _v(data)')`
     — confirms hoisted inner declaration plus param binding.
   - Repro D3 (from ajv): `new Function('"use strict"; return function vv(data) { return typeof data === "string"; }')`
     — confirms IIFE-shape pattern (the one Ajv actually emits).
   Run each repro and record which one fails. The fix surface is whichever
   set of repros fails today.
2. **Find the entry point.** Function constructor implementation in
   `lambda/js/js_globals.cpp` (look for `js_function_constructor` or
   similar). The body should be parsed as a `FunctionBody` (15.2.1
   FunctionBody) with the supplied formal parameters, in strict-or-sloppy
   mode matching the call site's directive prologue, with `[[Scope]]` set
   to the global environment.
3. **Find the lowering path.** If parsing is correct but the MIR lowering
   for the generated body drops parameter bindings, the bug is in
   `lambda/js/js_mir_function_class_lowering.cpp`. The likely shape: the
   generated function uses a different code path than user `function`
   declarations and that path skips the formal-parameter installation
   step.
4. **Fix at root cause.** Reuse the same FunctionBody parse + lowering
   path as user-source function declarations. The Function constructor is
   spec-required to produce an indistinguishable function value; any
   shortcut that says "build a synthetic shape" is the wrong shape.
5. **Lock in.** Add C++-level unit tests for the three repros above and
   ship `test/js/lib_ajv.js` + `lib_jsen.js` only after their probes pass
   end-to-end.

Risk controls:

- Do not change `new Function` arity validation, `length`, or `prototype`
  shape. Only the body lowering changes.
- Strict-mode propagation matters: ajv-generated bodies start with
  `"use strict"`. Make sure the directive prologue is honored.
- Pre-flight js262's `built-ins/Function/*` and
  `built-ins/Function/prototype/*` chapters on debug before the full guard.

Acceptance:

- Repros D1, D2, D3 all run and produce the expected outputs.
- `ajv@6.12.6` constructs (`new Ajv()` succeeds) and compiles a `{type:'string'}`
  schema to a validator that accepts `'hi'` and rejects `42`.
- `jsen@0.6.6` compiled validator accepts/rejects the same inputs.
- js262 release guard run is clean. Special attention to:
  - `built-ins/Function/*` chapter — must be byte-identical to baseline.
  - `language/expressions/function/*` and `.../arrow-function/*` — must
    be byte-identical to baseline.

### P5 - Library Suite Tightening

Goal: after all four root causes land, remove the workarounds the Js51-era
`lib_*.js` tests added to dodge each gap.

Work:

- After P1: rewrite `test/js/lib_chalk.js` to use the native ESM bundle
  with `import chalk from './chalk_src.js'` instead of the
  global-attachment workaround. Update `.txt` if output changes.
- After P2: add `test/js/lib_markdown_it.js` using the standard CJS shim.
  Replace `lib_snarkdown.js` only if markdown-it covers the same
  assertions; otherwise keep both.
- After P3: add `test/js/lib_marked.js`.
- After P4: add `test/js/lib_ajv.js` and `lib_jsen.js`. Replace
  `lib_tv4.js` only if ajv covers the same assertions; otherwise keep
  both for orthogonal coverage (interpreted vs. codegen schema paths).

Risk controls:

- Each replaced test ships with a captured `.txt` expected-output file
  from the current engine, just like the existing lib tests.
- Do not delete the workaround test until the replacement is green for
  at least one full guard cycle.

Acceptance:

- Final lib-tests inventory under `test/js/lib_*.js` after P5 covers all
  seven original target libraries plus the substitutes added during
  Js52 development that prove distinct in category.
- `test_js_gtest.exe` runs the new lib tests with 0 failures.
- js262 release guard run is clean.

## 5. Per-Phase Guard Commands

Run after every phase boundary (P1 through P5). The guard is the contract;
if any clause fails, the phase is reverted before the next phase starts.

Pre-flight (debug build) — catches the obvious cases fast:

```bash
make build && make build-test
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/lib_*'
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/regex_*'
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/JsFileTest.Run/module_*'
```

Release js262 guard — the binding acceptance bar:

```bash
make release
ASAN_OPTIONS=detect_container_overflow=0 \
  ./test/test_js_test262_gtest.exe --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt \
  --async-chunk-size=50 \
  --write-failures=temp/js52_pN_release_guard.tsv \
  --gtest_brief=1
```

(Replace `pN` with the phase letter.)

The guard tsv must report:

- `Failed: 0`
- `Regressions: 0`
- `Passing >= 39258`
- `Skipped <= 3037`
- Total runtime within `+5%` of the P0-captured baseline runtime.

If the runtime drifts by more than `+5%`, treat that as a regression even
if pass/fail counts are clean: stop, profile, and resolve before opening
the next phase.

## 5b. Residual marked/ajv bugs after P3+P4 fix

The constructor closure capture fix unblocks the first hop, but minimal repros
isolate further bugs that should be addressed in Js53:

**Bug R1 — IIFE parameter scope-analysis confusion under nested `new`**

Repro (`temp/js52/probe_nested_new.js`):

```js
!function(t, e) { e(exports); }(this, function(exports) {
  class B {
    constructor() { this.v = exports.foo; }   // captures exports
  }
  class A {
    constructor() {
      this.b = new B();   // <-- inside A's ctor body, exports resolves to FUNCTION
    }
  }
  exports.foo = "FOO";
  exports.A = A;
});
new module.exports.A();
```

Inside A's constructor, `typeof exports === 'function'` — it resolves to the
factory function `e`, not the inner factory's `exports` parameter. Likely
candidate: the scope analyzer mishandles a free variable lookup when the same
name (`exports`) is shadowed at multiple scope levels AND the lookup site is
inside one class definition that itself sits next to another class
definition. Single-class repros (`probe_iife2.js`) work correctly. The
failure pattern requires two classes in the same IIFE plus the second class's
constructor invoking `new` on the first.

**Bug R2 — ajv constructor opaque "is not a function"**

After R1's class-ctor first-hop fix, `new Ajv()` still throws "TypeError: is
not a function" with no callee name in the trace. The Ajv constructor calls
into a chain of helper functions; one of them is unresolved at the call site.
Diagnosis needs LLDB-level stack capture against the debug build — the
error-emission path in `js_call_function` does have a `log_error` with the
last-traced function name, but that log path was suppressed by `--no-log` in
my smoke runs. Re-run without `--no-log` and capture stderr to surface the
likely culprit.

## 5a. P3 Findings — Deferred to Js53

P3 root-cause analysis hit the proposal's own kill switch ("If the fix turns
out to require a larger interpreter change, stop"). Documenting state for
the Js53 follow-up:

**Confirmed observations:**

- `marked@12.0.2` UMD bundle loads cleanly. `Object.keys(module.exports)`
  returns the full API; `typeof module.exports.parseInline === 'function'`.
- Calling `m.parseInline("hi")` produces a hard SIGSEGV (exit 139). No
  output reaches stderr/stdout before the crash; `try/catch` cannot
  intercept it.
- Debug build does not surface a useful trace in normal run mode.
- Injecting `console.log` markers into marked's `#parseMarkdown` arrow:
  - Markers up to `const opt = { ...this.defaults, ...origOpt };` print
    cleanly — `this` IS the Marked instance at that point.
  - Adding any marker between that line and
    `const throwError = this.#onError(!!opt.silent, !!opt.async);` changes
    the crash signature: original is segfault, injection produces either
    a different segfault, an `Uncaught TypeError: Cannot read properties
    of undefined (reading 'defaults')` propagated through marked's own
    `throwError` wrap, or different garbage depending on the exact line.
- Calling `m.lexer("hello")` (a different entry point) also produces the
  `Cannot read properties of undefined (reading 'defaults')` error — so
  the failure is not parseInline-specific but a shared internal path.

**Negative findings (ruled out as immediate cause):**

- Class field arrow `this` capture works for minimal repros — even when
  the bound method is reassigned (`const f = a.method`) and called via a
  different receiver (`b.f()`).
- Private method chaining (`this.#a()` returning an arrow that calls
  `this.#b()`) works in minimal repros.
- Passing static methods from another class as args to a private method
  works in minimal repros.
- The minimal D-class structure of marked (defaults field, arrow field
  initialized from private method, called via a remote reference) all
  works standalone.

**Suspected actual cause:**

Some combination of marked-specific patterns triggers a runtime corruption
that:

- Survives the loading of the bundle (no crash at module init).
- Triggers only when the parseMarkdown closure is called from a re-exposed
  reference (exports.parseInline → marked.parseInline → markedInstance.parseInline).
- Sensitive to instruction sequence near `this.#onError(...)` — injecting
  bytecode-affecting code at that exact site shifts the crash, suggesting
  a JIT codegen / GC root issue, not a pure JS semantic bug.

Likely candidates for Js53 investigation, in order of prior probability:

1. JIT GC nursery promotion in the middle of a closure access path —
   private method calls through `this` may share a temp register with the
   arrow's captured `this`, and a GC at exactly the wrong instruction
   stomps it.
2. Closure-environment layout difference between Marked's class field
   arrow (initialized in field declaration) and ordinary closures.
3. Specific opt-tier (P5/P6) pattern that mis-types the result of
   `#parseMarkdown` and assumes a stale shape on later property reads.

**Why this is a Js53 task, not a Js52 task:**

- LLDB-level analysis is required to identify the exact instruction the
  crash happens at.
- Minimal repros don't reproduce; full marked source needs to be the
  test vehicle, which makes systematic bisection slow.
- Any fix likely touches JIT GC roots or codegen, both of which carry
  larger regression risk than P1/P2's local edits.
- Js52's scope cap is local fixes that keep the js262 baseline at
  39,258. P3 is beyond that scope.

**Hand-off contract for Js53:**

- Reduced repro: `m.lexer("hello")` after loading
  `marked@12.0.2/lib/marked.umd.js` via standard CJS shim. Smallest
  reliable trigger.
- Diagnostic checkpoint: marked's `#parseMarkdown` arrow runs successfully
  up to and including `const opt = { ...this.defaults, ...origOpt };` —
  the failure is between that and `const throwError = this.#onError(...)`.
- Js52 commits the snarkdown lib test as the markdown-parser coverage
  representative; replacing it with `lib_marked.js` is a Js53 P5 step.

## 6. Risk Register

| Risk | Impact | Mitigation |
|---|---|---|
| P1 export-alias fix affects re-exports (`export { a as b } from 'm'`) in a way that breaks an existing module test | high — silent wrong-binding | Add a dedicated re-export test alongside the named-alias test in P1; run all `module_*.js` tests before js262 guard. |
| P2 regex-vs-division retune breaks a regex elsewhere | high — silent wrong tokens | Diff parse trees of every existing `test/js/regex_*.js` before/after with a one-shot AST dump tool; treat any diff as a regression. |
| P3 root cause turns out to need a GC redesign | very high — overscope | Split P3 into Js53 as soon as the root cause is identified as systemic. Js52 ships P1, P2, P4, P5 even if P3 is deferred. |
| P4 Function constructor change breaks `Function.prototype.toString` snapshots | medium | Run `built-ins/Function/prototype/toString/*` chapter on debug before release guard. |
| Cumulative drift hides a regression | medium | The +5% runtime band is enforced *per phase*, not just at the end. Each guard is the previous-guard's contract. |
| New lib tests bake in current engine quirks as "expected output" | low-to-medium | Each `.txt` is regenerated from the latest engine after the corresponding fix lands; lib tests are regression catches, not correctness oracles. |

## 7. Completion Criteria

Js52 is complete when:

- All four repros under `temp/js52_repros/` produce the expected output
  documented in §3.
- `chalk` ESM bundle loads without a stripped-bundle workaround.
- `markdown-it@13` minified bundle loads and `markdownit().render('# t')`
  returns the expected `<h1>` HTML.
- `marked@12.0.2/lib/marked.umd.js` loads and `marked.parse('hello')`
  returns `<p>hello</p>` (or marked's equivalent).
- `ajv@6.12.6` compiles `{ type: 'string' }` to a working validator.
- The release js262 baseline reports `>= 39258` passing, `0` regressions,
  `0` failures, `0` non-fully-passing tests.
- Total runtime stays within `+5%` of the P0-captured number.
- The seven Js51-era `lib_*.js` tests still pass; new `lib_*.js` tests
  added during P5 also pass.

## 8. Out Of Scope

- ESM `import.meta` semantics, dynamic `import()`, top-level `await` in
  modules — all out of scope.
- Full `ShadowRealm` support — out of scope (Js51 already capped this).
- Generic recursion-depth tuning beyond what P3's root cause requires.
- Adding new lib tests beyond the seven from the Js51-era set and the
  P5-released markdown-it/marked/ajv/jsen replacements.
- Removing `cross-realm`, `IsHTMLDDA`, or other intentional test262
  exceptions.

## 9. Open Questions

1. P2: does the current Tree-sitter TS grammar source live in
   `tree-sitter-typescript/grammar.js` or in a Lambda-vendored
   `define-grammar.js`? Either way the fix is the same, but the file path
   changes the diff size.
2. P3: if ASAN attributes the crash to a third-party `arena_alloc` site
   that is also exercised by many test262 tests, the fix may surface
   secondary regressions in pre-Js52 tests that just happened to escape
   notice. In that case those tests are pre-existing latent bugs, not Js52
   regressions; document them in this file and either fix or `t262_partial`
   them separately.
3. P4: a literal reading of the spec requires a fresh parse of the body
   text on every `new Function(...)` call. Today's engine may cache by
   source text; the fix may need to invalidate that cache. Confirm during
   the work.
