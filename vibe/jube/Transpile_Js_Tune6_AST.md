# Transpile JS Tune6 — AST Build and Lazy MIR Tuning Proposal

Date: 2026-06-06
Status: proposal
Scope: JavaScript transpile latency, generated MIR volume, and cold third-party library startup

## Goal

Recent Radiant web-template testing exposed a different performance profile from
the normal LambdaJS execution benchmarks. For large browser libraries, the slow
part is not reading the source file or running layout. It is compiling a large
amount of JavaScript that the page often never calls.

One representative debug-mode measurement for local `jquery-1.8.2.min.js`
showed:

| Phase | Time |
| --- | ---: |
| source load | 0.5 ms |
| Tree-sitter parse | 26.7 ms |
| AST build | 1,759 ms |
| MIR lowering | 1,109 ms |
| MIR link/JIT | 549 ms |
| JS execute | 222 ms |

This proposal targets three areas:

1. Reduce AST building time.
2. Reduce generated MIR code, which should reduce MIR JIT time and speed up
   execution by keeping cold library internals out of the hot startup path.
3. Implement lazy function MIR so function bodies are lowered and linked only
   when they are actually called.

Before tuning any of those areas, add a focused JS transpiling GTest benchmark so
changes can be measured on repeatable workloads without running full layout
suites.

---

## 1. Benchmark First: Focused JS Transpiling GTest

### 1.1 Why this is needed

Current timing signals are spread across:

- `lambda.exe js <file>` benchmark runs, which mix compile and execution policy.
- Radiant `layout` timing, which includes HTML parse, DOM/CSS work, layout,
  rendering output, script loading policy, and page-level lifecycle behavior.
- Ad-hoc `RADIANT_JS_TASK_TIMING=1` logs, which are useful for diagnosis but not
  a stable regression benchmark.

Tune6 should start by adding a focused GTest binary that measures JavaScript
frontend and MIR phases directly:

```text
read source
tree-sitter parse
AST build
MIR lowering
MIR link/JIT
top-level execute
cleanup
```

The benchmark should report per-file timing and aggregate totals. It should not
decide pass/fail from fixed timing thresholds, because debug and release timing
vary by machine. The GTest should fail only on compile/runtime correctness
errors. Timing output is diagnostic data.

### 1.2 Proposed test binary

Add:

```text
test/test_js_transpile_timing_gtest.cpp
test/js/transpile_timing/
```

Build integration:

- Edit `build_lambda_config.json`.
- Run `make` to regenerate build files.
- Do not edit generated `.lua` files manually.

Suggested executable:

```text
./test/test_js_transpile_timing_gtest.exe
./test/test_js_transpile_timing_gtest.exe --gtest_filter=JSTranspileTiming.jquery_1_8_2_min
```

The test should use existing JS runtime entry points:

- `transpile_js_to_mir_len(...)`
- `js_mir_get_last_phase_timing(...)`
- `js_mir_reset_last_phase_timing(...)`

Those are already declared through the JS runtime/transpiler headers. The timing
struct currently covers parse, AST, MIR, link, execute, cleanup, and total
timing. If the current struct does not expose enough detail for AST-specific
work, extend it rather than adding one-off log parsing.

### 1.3 Benchmark corpus

Use a mixed corpus so Tune6 does not optimize only jQuery:

| Category | Candidate files | Reason |
| --- | --- | --- |
| browser/vendor | `test/js/dom_jquery_lib.js` | large DOM/jQuery-oriented library input already in `test/js` |
| schema libraries | `test/js/lib_joi.js`, `test/js/lib_validator.js`, `test/js/lib_ajv.js`, `test/js/lib_yup.js` | large object/function-heavy libraries |
| parser/compiler libraries | `test/js/lib_acorn.js`, `test/benchmark/octane/typescript-compiler.js`, `test/benchmark/octane/typescript-input.js` | large source with deep function/class structures |
| utility libraries | `test/js/ramda_src.js`, `test/js/ramda_src_min.js`, `test/js/lib_lodash.js`, `test/js/underscore_lib.js` | many closures and higher-order functions |
| web-template vendor JS | selected jQuery, Bootstrap, Raphael, DataTables, Morris files from `test/layout/data/web-tmpl` | mirrors the Radiant bottleneck |
| execution-heavy small benchmarks | `test/benchmark/awfy/*_bundle.js`, `test/benchmark/jetstream/*.js` | checks that compile tuning does not regress normal JS benchmarks |

For tidiness, copy a small curated set of web-template vendor scripts into:

```text
test/js/transpile_timing/vendor/
```

Suggested initial vendor set:

```text
jquery-1.8.2.min.js
jquery-1.10.2.js
jquery-1.11.0.min.js
bootstrap.min.js
raphael-2.1.0.min.js
morris.js
jquery.datatables.js
modernizr-latest.js
```

The copied files should be exact snapshots from `test/layout/data/web-tmpl`, with
a README noting the source path and byte size. The reason to copy is not to fork
the fixtures semantically; it is to avoid benchmark tests depending on the
web-template directory layout.

### 1.4 Output format

Use a plain tabular diagnostic line per file:

```text
JS_TRANSPILE_TIMING file=jquery-1.8.2.min.js bytes=93435 parse_ms=26.7 ast_ms=1759.4 mir_ms=1108.7 link_ms=549.0 exec_ms=222.2 total_ms=3892.3
```

For machine-readable tracking, optionally support:

```text
JS_TRANSPILE_TIMING_JSON=./temp/js_transpile_timing.jsonl
```

Temporary output must go under `./temp/`, not `/tmp`.

### 1.5 Useful counters to add

Timing alone will not explain why AST build is slow. Add optional counters gated
by an env var such as `JS_AST_TIMING=1`:

| Counter | Why |
| --- | --- |
| total AST nodes built | normalizes timing by tree size |
| count by Tree-sitter symbol | shows dominant syntax nodes |
| identifier count | identifies name-pool/scope pressure |
| string literal decode count and bytes | catches heavy unescape/copy paths |
| `ts_node_type()` calls | measures string-based classification overhead |
| field lookup calls | measures `ts_node_child_by_field_name` pressure |
| scope lookups during AST build | tests whether binding lookup is dominating |
| AST allocation bytes | detects allocation shape and node bloat |
| functions declared vs functions called | baseline for lazy MIR |
| MIR functions emitted | direct measure of generated-code reduction |

---

## 2. Area One: Reduce AST Build Time

### 2.1 Current shape

The JavaScript AST builder is `lambda/js/build_js_ast.cpp`. It consumes a
Tree-sitter CST and creates a LambdaJS-specific AST. This is useful for semantic
lowering, but the current build path pays several costs repeatedly:

- Many builders classify nodes with `strcmp(ts_node_type(...), "...")`.
- Many builders fetch common children with
  `ts_node_child_by_field_name(..., strlen("field"))`.
- `build_js_identifier()` decodes and interns identifier names, then performs
  scope lookup immediately.
- `build_js_literal()` copies and decodes literals.
- Some constructs build duplicate AST nodes for syntax that can be represented
  more compactly, such as object shorthand properties.
- Later MIR analysis walks the complete AST again to collect functions, classes,
  imports, hoists, captures, implicit globals, and scope metadata.

For minified libraries, the AST is large and dense. Even small per-node costs
become seconds in debug builds.

### 2.2 Replace node-type strings with numeric symbols

Use `ts_node_symbol(node)` and switch on generated Tree-sitter symbols instead of
calling `ts_node_type()` and comparing strings.

Current pattern:

```c
const char* node_type = ts_node_type(node);
if (strcmp(node_type, "identifier") == 0) { ... }
```

Proposed pattern:

```c
TSSymbol sym = ts_node_symbol(node);
switch (sym) {
    case sym_identifier:
        ...
        break;
}
```

The file already has generated `sym_*` defines, so this is mostly mechanical.

Expected effect:

- Less string work.
- Better branch shape for hot builders.
- Cleaner profiling because node categories become explicit.

Risk:

- Tree-sitter grammar changes can alter symbol ids. The existing generated
  defines should remain the source of truth.

### 2.3 Cache field ids instead of field-name lookup

Replace repeated field-name lookups with cached field ids.

Current pattern:

```c
ts_node_child_by_field_name(node, "left", strlen("left"));
ts_node_child_by_field_name(node, "right", strlen("right"));
```

Proposed approach:

```c
static TSFieldId field_left;
static TSFieldId field_right;
static TSFieldId field_function;
static TSFieldId field_arguments;
```

Initialize once from the JS language:

```c
field_left = ts_language_field_id_for_name(lang, "left", 4);
```

Then use field-id child access where available.

Expected effect:

- Reduces repeated string hashing/search in Tree-sitter.
- Helps common nodes: binary expressions, calls, members, classes, functions,
  imports, exports, destructuring, and assignments.

### 2.4 Fast path identifier decoding

Most identifiers in large libraries do not contain escapes. Add a fast path:

1. Get source slice.
2. Scan for `'\\'`.
3. If absent, intern the slice directly.
4. Only call `js_decode_identifier_name()` when an escape is present.

This should apply to:

- identifiers,
- private names where applicable,
- property names that are syntactic identifiers,
- shorthand object keys.

Expected effect:

- Large reduction in copy/decode work.
- Less transient allocation.

Correctness requirement:

- Escaped identifiers must continue to canonicalize to the same name as the
  unescaped spelling.

### 2.5 Defer scope lookup out of AST construction

`build_js_identifier()` currently does syntax construction and binding lookup in
one step. That makes AST build pay semantic costs for every identifier, including
global identifiers in third-party libraries.

Proposed change:

- AST build records identifier name and source range.
- A later semantic pass resolves bindings.
- Undefined/global reporting happens in that semantic pass.
- Debug logging for missing identifiers is gated behind a specific env var.

Expected effect:

- Less work during AST build.
- Cleaner separation of syntax and semantics.
- Enables lazy function-body AST/MIR because binding resolution can become
  function-scoped.

Risk:

- Some existing lowerers may assume `identifier->symbol` or equivalent binding
  fields are populated immediately. The migration should first add lazy accessors
  or preserve the field with a sentinel state.

### 2.6 Compact common AST nodes

Large minified libraries create many identifiers, member expressions, call
expressions, literals, and binary expressions. Tune6 should reduce the memory and
initialization work for these hot nodes.

Candidates:

- Store source offsets/lengths instead of full `TSNode` where location is enough.
- Use compact node-specific structs for identifiers/literals.
- Avoid duplicate nodes for object shorthand.
- Avoid storing both raw source and decoded name when one can be recovered or
  interned.

This is not the first change to make, because correctness bugs in AST shape can
be subtle. Do it after counters show node allocation is a real bottleneck.

### 2.7 Combine collection passes with AST build where safe

The MIR path performs several whole-AST scans for declarations, hoists, imports,
classes, captures, and implicit globals. Some metadata can be collected while
building the AST:

- top-level function declarations,
- top-level class declarations,
- top-level `var`/`let`/`const` declarations,
- imports/exports,
- static field declarations,
- function body source ranges,
- direct child function declarations.

Do not try to solve capture propagation inside AST build immediately. That still
needs semantic context. The short-term win is to avoid rediscovering simple
top-level metadata repeatedly.

---

## 3. Area Two: Reduce MIR Code

### 3.1 Current issue

The current eager pipeline tends to lower whole scripts and their functions into
MIR before top-level execution. For application code this is acceptable. For
third-party browser libraries it is expensive because:

- libraries declare hundreds or thousands of functions,
- layout tests often need only top-level initialization plus a tiny fraction of
  DOM-ready/plugin code,
- many functions are feature branches for browsers Radiant does not emulate,
- much generated MIR is linked but never executed.

Reducing MIR code helps in three ways:

1. MIR lowering does less work.
2. MIR link/JIT has fewer functions and smaller modules.
3. Execution has less startup initialization and less generated-code pressure.

### 3.2 Track generated MIR volume

Before changing lowering policy, expose counters:

| Counter | Why |
| --- | --- |
| functions discovered | potential lazy compile population |
| functions lowered eagerly | codegen volume |
| function bodies skipped/deferred | lazy MIR benefit |
| MIR instructions emitted | lower/link work proxy |
| MIR functions linked | JIT cost proxy |
| top-level calls executed | call graph seed |
| first-call lazy compiles | lazy MIR runtime cost |

Add those counters to the focused GTest timing output.

### 3.3 Move common operations to native C helpers

Generated MIR should describe program structure, not reimplement common runtime
algorithms in generated code. When repeated JS patterns are already well-defined
runtime operations, lower them to precompiled C helpers.

Candidate native helpers:

| Pattern | Native helper direction |
| --- | --- |
| object extend/merge loops | `js_object_assign_like(...)` variants |
| property descriptor definition | `js_define_property_runtime(...)` |
| array-like iteration for common library patterns | native iterator helper with callback only when needed |
| class/style DOM manipulation | `js_dom_add_class`, `js_dom_remove_class`, `js_dom_set_style` |
| event registration | `js_dom_on`, `js_dom_off`, `js_dom_ready` |
| selector wrapper creation | native query/select wrapper |
| feature detection stubs | native no-op/known-answer helpers for Radiant environment |

The rule is: semantics live in C runtime helpers when the operation is generic
and reusable. The transpiler emits a call. This keeps generated MIR smaller and
keeps tuning in one native implementation.

### 3.4 Native compatibility installers for known browser libraries

For Radiant layout tests, the fastest way to avoid compiling huge vendor JS is to
not compile known libraries at all when a native compatibility layer is adequate.

Add optional script recognition:

```text
script source path/hash -> native installer
```

Examples:

| Library | Installer |
| --- | --- |
| jQuery 1.x/2.x/3.x | `js_install_jquery_compat(...)` |
| Bootstrap 2/3 plugins | `js_install_bootstrap_compat(...)` |
| Modernizr/respond/html5shiv | `js_install_feature_detect_compat(...)` |
| Raphael/Morris chart setup | `js_install_chart_compat(...)` |
| DataTables | `js_install_datatables_compat(...)` |

This should be opt-in at first:

```text
RADIANT_JS_NATIVE_VENDOR=1
```

The installer must leave observable globals in the document realm:

```text
window.jQuery
window.$
window.Modernizr
...
```

For layout, a partial jQuery layer can support the common chainable operations:

```text
$(fn)
$(selector)
.ready(fn)
.each(fn)
.addClass(name)
.removeClass(name)
.toggleClass(name)
.css(name/value)
.attr(name/value)
.prop(name/value)
.width()
.height()
.on(type, fn)
.off(type, fn)
.trigger(type)
.find(selector)
.parent()
.children()
.append(...)
```

This is deliberately not a replacement for general LambdaJS correctness tests.
It is a Radiant policy for known vendor scripts under browser-template workloads.
Normal `lambda.exe js` and js262 should continue to compile and execute source.

### 3.5 Hash/cache repeated scripts

The web-template corpus repeats the same vendor files many times. Even if native
installers are not used, the pipeline should cache by content hash:

```text
source bytes + runtime version + feature flags -> parse/AST/MIR artifact
```

Cache levels:

1. Source hash and metadata only.
2. Tree-sitter parse tree or compact syntax summary.
3. AST template.
4. MIR template.
5. Linked code, only after lifetime and realm safety are audited.

The safe early cache is AST/MIR metadata that does not own realm-specific heap
objects. Linked-code caching requires special care because current MIR/runtime
state can reference pools, name pools, runtime globals, and document objects.

---

## 4. Area Three: Lazy Function MIR

### 4.1 Desired behavior

Compile the top-level script eagerly, but do not lower every function body to MIR
up front. Function declarations and expressions become function objects with lazy
compile records.

At script load time:

```text
parse source
build top-level AST/metadata
create global bindings
create function objects for declarations
compile/link top-level body
execute top-level body
```

On first call to a lazy function:

```text
check function object compile state
if uncompiled:
    build/finish body AST if needed
    resolve local/captured bindings
    lower body to MIR
    link function
    patch function object entry point
call compiled entry point
```

Subsequent calls jump directly to the compiled entry point.

### 4.2 Lazy function record

Add a runtime/transpiler-side record conceptually like:

```c
typedef struct JsLazyFunction {
    const char* source;
    size_t source_len;
    uint32_t body_start;
    uint32_t body_end;
    uint32_t params_start;
    uint32_t params_end;
    JsAstNode* body_ast;
    JsFunctionMeta* meta;
    JsScopeSnapshot* scope;
    void* compiled_entry;
    uint8_t state;
} JsLazyFunction;
```

Actual implementation should use project allocation and project container types,
not C++ STL containers.

The record needs enough data to compile later without relying on freed parser
state:

- source pointer and lifetime owner,
- byte ranges for function body and parameters,
- function kind: normal, arrow, method, constructor, generator, async,
- strict-mode status,
- lexical outer scope/capture metadata,
- realm/runtime pointer,
- module/global environment reference,
- original filename/source-map location.

### 4.3 Function object state machine

Use explicit states:

```text
JS_FUNC_LAZY_UNCOMPILED
JS_FUNC_LAZY_COMPILING
JS_FUNC_LAZY_COMPILED
JS_FUNC_LAZY_FAILED
```

`COMPILING` prevents recursive re-entry from trying to compile the same function
twice. `FAILED` stores the compile exception so repeated calls observe the same
failure behavior.

### 4.4 What remains eager

Some code still needs eager work:

- top-level statements,
- imports/exports/module linking,
- hoisted declaration registration,
- class declaration shape,
- function names and arity,
- parameter list metadata,
- capture classification sufficient to create closures,
- direct eval hazards,
- syntax errors.

Important semantic point: JavaScript reports syntax errors in function bodies
when the script is parsed, not when the function is first called. Therefore lazy
MIR must not mean lazy parsing for syntax validity. We can defer AST body
materialization and MIR lowering, but the source must already have been parsed
successfully by Tree-sitter.

### 4.5 Capture and scope handling

Lazy MIR is only safe if closure environments are represented independently from
generated code.

At eager load time:

1. Build enough metadata to know which outer bindings a function may capture.
2. Allocate or reference the correct lexical environment.
3. Store that scope snapshot on the function object.

At first compile:

1. Rehydrate or finish the function body AST.
2. Resolve identifiers against the stored scope snapshot and local function
   scope.
3. Generate MIR using the same capture slots the eager path would have used.

This means capture analysis likely remains eager at first. A later phase can make
capture analysis itself more incremental, but Tune6 should avoid changing too
many semantic layers at once.

### 4.6 Direct eval and dynamic hazards

Direct `eval` complicates lazy function MIR because eval can introduce or observe
bindings in the current lexical environment. Conservative policy:

- If a function or ancestor scope contains direct eval, compile that function
  eagerly.
- If the script uses `with`, compile affected scopes eagerly.
- If arguments/parameter aliasing creates special environment behavior, use the
  existing eager path until explicitly supported.

This still leaves most vendor library functions eligible for lazy MIR.

### 4.7 Top-level function calls

A lazy system must avoid making startup slower by compiling tiny functions one at
a time when top-level code immediately calls them.

Add a small policy:

- Compile top-level body eagerly.
- If top-level code calls a locally declared function directly, allow first-call
  lazy compilation.
- Optionally batch compile functions called during top-level execution after the
  first N lazy compiles, to reduce repeated MIR link overhead.

Initial implementation should prioritize correctness and measurement:

```text
lazy compile one function on first call
record timing
record whether link cost is too fragmented
```

If link fragmentation is expensive, introduce a lazy compile batch:

```text
collect pending lazy functions reached during top-level execution
lower/link them in one MIR module
patch all entry points
resume execution
```

### 4.8 Interaction with native vendor installers

Lazy MIR and native vendor installers are complementary:

- Native installer: best for recognized browser libraries in Radiant.
- Lazy MIR: generic improvement for all large JS inputs.

Policy:

1. If native vendor mode recognizes a script, install native compatibility layer
   and skip source compile.
2. Otherwise, compile top-level eagerly and lazily compile function bodies.
3. If lazy compilation is disabled, use the current eager path.

Feature flags:

```text
JS_LAZY_MIR=1
RADIANT_JS_NATIVE_VENDOR=1
```

Make them opt-in until js262, benchmark, and layout coverage are stable.

---

## 5. Implementation Phases

### Phase A: Measurement Infrastructure

Tasks:

1. Add `test/test_js_transpile_timing_gtest.cpp`.
2. Add curated fixtures under `test/js/transpile_timing/`.
3. Expose phase timing and generated-code counters through JS runtime APIs.
4. Add optional JSONL output under `./temp/`.
5. Add build entry in `build_lambda_config.json` and regenerate build files with
   `make`.

Validation:

```text
make build-test
./test/test_js_transpile_timing_gtest.exe
```

Expected result:

- Correctness pass/fail is stable.
- Timing output lists parse, AST, MIR, link, execute, cleanup, total.
- Large known fixtures reproduce the Radiant bottleneck outside layout.

### Phase B: AST Builder Low-Risk Speedups

Tasks:

1. Add AST counters gated by `JS_AST_TIMING=1`.
2. Convert hot `ts_node_type()` comparisons to `ts_node_symbol()`.
3. Cache Tree-sitter field ids.
4. Add identifier no-escape fast path.
5. Defer or gate per-identifier undefined/global logging.
6. Remove duplicate shorthand identifier construction where safe.

Validation:

```text
./test/test_js_transpile_timing_gtest.exe
make test-js-baseline
make layout suite=web-tmpl
```

Expected result:

- AST time drops on large library fixtures.
- No js baseline regression.
- No layout behavior change.

### Phase C: Generated MIR Counters and Native Helpers

Tasks:

1. Count functions discovered/lowered/linked.
2. Count MIR instructions emitted.
3. Identify repeated JS helper patterns from the benchmark corpus.
4. Move safe generic operations into native C helpers.
5. Lower recognized patterns to helper calls.

Validation:

```text
./test/test_js_transpile_timing_gtest.exe
make test-js-baseline
make test-radiant-baseline
```

Expected result:

- MIR instruction/function count decreases.
- MIR lowering and link time decrease.
- Execution time improves or stays neutral.

### Phase D: Lazy Function MIR Prototype

Tasks:

1. Add lazy function record and function object state.
2. Create lazy function objects for eligible function declarations/expressions.
3. Eagerly compile top-level code only.
4. Compile function body on first call.
5. Patch entry point after successful compile.
6. Fall back to eager compilation for eval/with/unsupported function kinds.
7. Add counters for lazy compile count and lazy compile time.

Validation:

```text
JS_LAZY_MIR=1 ./test/test_js_transpile_timing_gtest.exe
JS_LAZY_MIR=1 make test-js-baseline
JS_LAZY_MIR=1 make layout suite=web-tmpl
```

Expected result:

- Large vendor files show much lower initial MIR lowering/link time.
- Some execution time may move to first-call lazy compile.
- Total page load improves when most library functions are cold.

### Phase E: Radiant Native Vendor Installers

Tasks:

1. Add script hash/path recognition in the Radiant script loader.
2. Implement native jQuery compatibility installer first.
3. Add Bootstrap/Modernizr no-op or partial compatibility installers.
4. Gate with `RADIANT_JS_NATIVE_VENDOR=1`.
5. Compare layout diffs and timing against source-compiled mode.

Validation:

```text
RADIANT_JS_NATIVE_VENDOR=1 make layout suite=web-tmpl
```

Expected result:

- Recognized vendor scripts have near-zero transpile time.
- DOM mutation behavior remains sufficient for layout baselines.
- Unknown scripts still use normal JavaScript frontend.

---

## 6. Risks and Guardrails

### 6.1 Correctness risks

| Risk | Guardrail |
| --- | --- |
| lazy MIR changes closure semantics | keep capture metadata eager first |
| syntax errors become lazy | always parse full source eagerly |
| direct eval observes wrong environment | eager compile eval/with scopes |
| native jQuery layer misses observable behavior | opt-in Radiant-only mode first |
| AST builder refactor changes source locations | keep source range tests and error reporting checks |
| cached artifacts retain realm-specific pointers | start with metadata/AST cache only |

### 6.2 Performance measurement risks

Debug builds are useful for zooming into relative phase costs while developing,
especially because the user-facing issue is visible in debug-mode layout runs.
For final performance claims, record both:

- debug-mode numbers for developer workflow impact,
- release-mode numbers for production throughput.

The benchmark GTest should print build mode, platform, fixture byte size, and
whether lazy/native flags are enabled.

### 6.3 Non-goals

Tune6 should not:

- replace the JS frontend with ad-hoc string interpretation,
- skip parsing unknown scripts,
- hard-code layout test expected output,
- manually edit generated build files,
- make native vendor installers the default before they are proven against
  layout baselines.

---

## 7. Success Criteria

Measurement success:

- A focused GTest reports stable per-phase timing for large JS fixtures.
- AST counters identify the top AST build cost centers.
- Generated MIR counters quantify code-volume reduction.

AST success:

- AST build time improves on large library fixtures.
- No correctness regression in JS baseline tests.

MIR volume success:

- Number of eagerly lowered functions drops significantly for large libraries.
- MIR lowering/link time drops proportionally.

Lazy MIR success:

- For large vendor libraries, initial compile/link time drops because cold
  function bodies are not lowered.
- First-call compilation is observable in counters and does not break closure,
  class, method, or strict-mode behavior.

Radiant success:

- `make layout suite=web-tmpl` is materially faster.
- Auto-close debug-mode runs no longer spend seconds compiling unused vendor
  functions per test.
- Native vendor mode can be enabled experimentally without changing the default
  JS correctness path.

---

## 8. Recommended First Patch

The first patch should be small and measurement-only:

1. Add `test/test_js_transpile_timing_gtest.cpp`.
2. Add 5-10 curated fixture entries.
3. Print phase timing through existing `JsMirPhaseTiming`.
4. Add generated MIR/function counters only if they are easy to expose without
   changing lowering behavior.
5. Wire the test into `build_lambda_config.json`.

Only after that benchmark exists should Tune6 start changing AST builder or MIR
lowering behavior. Otherwise, the optimization work will keep depending on
one-off layout probes, and it will be too easy to improve one page while
regressing the broader JS corpus.
