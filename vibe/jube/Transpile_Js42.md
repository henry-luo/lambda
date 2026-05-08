# Transpile_Js42 - Structural Plan To Retire The Remaining js262 Failures

Date: 2026-05-08

This proposal follows Js41. Js41 made the JavaScript implementation safer to
change: the monolithic MIR transpiler is now split into real `.cpp` files, the
runtime has a `JsRuntimeState` capsule, ordinary property kernels exist in
`js_props`, and the test262 baseline was refreshed to:

```text
Fully passed: 30026 / 34169  (87.9%)
Failed:        4143
Skipped:       8068
Regressions:      0
Batch-lost:       0
Crash-exits:      0
```

The next step should not be another round of local fixes. The remaining 4,143
known failures cluster around missing spec structure: execution contexts,
lexical environments, property descriptors, RegExp Unicode semantics, built-in
algorithm families, and async/job scheduling. Js42 should make those structures
explicit and then use them to reduce failures family by family.

## 1. Current Code And Test Snapshot

### 1.1 Current split state

The Js41 split is real, but a few files still carry too much semantic weight:

| File | Lines | Main remaining risk |
|---|---:|---|
| `lambda/js/js_mir_expression_lowering.cpp` | 10,018 | expressions, calls, class/super/new/eval special cases still interleaved |
| `lambda/js/js_mir_module_batch_lowering.cpp` | 4,567 | module, preamble, batch, lifecycle, and dynamic context concerns mixed |
| `lambda/js/js_mir_statement_lowering.cpp` | 4,009 | statements, lexical scopes, loop/switch/try completion, Annex B pressure |
| `lambda/js/js_mir_function_class_lowering.cpp` | 2,441 | function/class/generator lowering still bundled |
| `lambda/js/js_runtime.cpp` | 20,920 | large dispatch surface for object, function, collection, promise, generator, RegExp, and batch behavior |
| `lambda/js/js_globals.cpp` | 12,864 | global objects, constructors, Reflect/Object/Date/JSON/test262 host helpers, and host APIs |
| `lambda/js/js_runtime_builtin_registry.cpp` | 1,018 | method registry exists, but descriptor/accessor/constructor metadata is not yet the single source of truth |

Useful foundations already exist:

- `js_runtime_state.{hpp,cpp}` centralizes many former runtime globals.
- `js_props.{h,cpp}` contains ordinary get/has/set descriptor kernels.
- `js_property_attrs.{h,cpp}` contains shape-entry descriptor/accessor flags.
- `js_runtime_builtin_registry.cpp` gives method-table installation a shared
  pattern.
- `js_mir_context.hpp` and `js_mir_internal.hpp` make the split transpiler
  tractable, but they still expose many phase-specific declarations in one
  broad header.

### 1.2 Failure taxonomy

The exact runner result is 4,143 failures. A metadata/baseline comparison
grouped by test path shows the major remaining pressure points:

| Area | Approx failures | Structural theme |
|---|---:|---|
| `language/expressions` | 800+ | class/super, compound assignment references, dynamic import, assignment, object literal, yield/new/call |
| `language/statements` | 680+ | class declarations, for-of/for-in, `with`, function declarations, try/switch/let/const |
| `built-ins/RegExp` | 516 | Unicode property escapes, named groups, lookbehind, Annex B regex grammar |
| `built-ins/Array` | 245 | array methods, species, descriptor/index edge cases, huge length stress |
| `built-ins/Object` | 222 | defineProperty/defineProperties, prototype methods, assign/freeze/seal/preventExtensions |
| `built-ins/Atomics` + `SharedArrayBuffer` | 190+ | shared memory model and typed-array backed atomics |
| `built-ins/Promise` | 156 | job queue, thenable assimilation, combinators, species, async edge behavior |
| `built-ins/TypedArrayConstructors` + `TypedArray` | 235+ | integer-indexed exotic semantics, detached buffers, BigInt typed arrays, descriptors |
| `annexB/language/eval-code`, `global-code`, `function-code` | 260+ | sloppy eval, global/function declaration instantiation, Annex B hoisting |
| `built-ins/String`, `Date`, `Set`, `Map`, `Function`, `BigInt` | 300+ | algorithm conformance and descriptors |
| `built-ins/Proxy`, `Reflect`, `Symbol`, iterator prototypes | 100+ | trap invariants and well-known symbol dispatch |

Feature tags reinforce the same story:

- `regexp-unicode-property-escapes`: about 430 failures.
- `class`: about 420 failures, plus class fields/private/static-block tags.
- `TypedArray`: about 380 tagged failures.
- `BigInt`: about 230 tagged failures.
- `Symbol`, `Symbol.iterator`, `Symbol.toPrimitive`, `Symbol.species`: about
  400 combined tagged failures.
- `Proxy` and `Reflect`: about 170 combined tagged failures.
- `generators`: about 190 tagged failures.

## 2. Diagnosis

### 2.1 The transpiler still lacks a spec-level environment model

The current MIR lowering tracks locals, module constants, captures, TDZ, scope
envs, and module vars, but those are implementation structures rather than
ECMA execution-context structures. This shows up in the failure taxonomy:

- class declaration/expression and private-name semantics are spread across
  analysis, collection, and lowering passes;
- `with`, direct eval, global/function code, and Annex B hoisting need object
  environment records and declarative environment records;
- compound assignment, update expressions, destructuring, and super/property
  references need a shared Reference Record model;
- for-of/for-in and generator/yield lowering need centralized iterator closing
  and abrupt-completion handling.

The missing abstraction is not more helper functions. It is a small runtime and
lowering model for:

- Execution Context
- LexicalEnvironment / VariableEnvironment
- Declarative, object, global, and function environment records
- Reference Records
- Completion Records
- Iterator Records

The transpiler can still optimize common cases, but it should lower rare or
complex cases through these explicit operations.

### 2.2 Property descriptors are close, but not yet mandatory

`js_props` and `js_property_attrs` are strong foundations, yet much runtime code
still has alternate paths:

- `js_property_get` and `js_property_set` still contain large inline branches.
- `Object.defineProperty`, `Reflect.set`, Proxy, typed arrays, arrays, and
  string wrappers still partially reimplement descriptor rules.
- Built-in installation tables install methods, but descriptor synthesis,
  accessor metadata, constructor/prototype properties, `name`, and `length` are
  not yet all generated from one registry.

This explains the Object/Array/TypedArray/Proxy/Reflect failures. The next
property phase must make the abstract operations unavoidable.

### 2.3 RegExp is a dedicated subsystem, not a normal built-in

RegExp failures dominate the built-in side. The largest cluster is generated
Unicode property escape tests. RE2 helps, but JavaScript RegExp has grammar and
semantic features that need a JS-specific front-end:

- `\p{...}` / `\P{...}` Unicode property escapes and Script_Extensions aliases;
- `/u` mode identity/escape restrictions;
- named groups and backreference validation;
- lookbehind handling and fixed-length validation;
- Annex B regex grammar differences;
- `lastIndex`, sticky/global, split/match/search/replace protocol behavior.

Treating RegExp as "strings passed into RE2" will continue to leak failures.
LambdaJS needs a `js_regexp_compile` front-end that validates and translates
ECMA RegExp patterns into the supported backend representation.

### 2.4 Async behavior and jobs need one queue

Promise and async/generator failures are not just missing methods. The engine
needs one spec-visible job/microtask queue model shared by:

- `Promise.prototype.then`,
- Promise combinators,
- async functions,
- async generators,
- dynamic import,
- host event-loop integration,
- test262 `$DONE` and async harness paths when enabled later.

Without this, Promise methods can appear to work in direct cases but diverge on
thenable assimilation, ordering, abrupt completions, species, and iterator
closing.

### 2.5 The test runner needs better failure artifacts

The refreshed baseline is trustworthy, but the current runner prints the
failure list to the terminal. For a 4,143-failure campaign, that is not enough.
Js42 should add machine-readable failure manifests under `temp/` so every phase
can start from a stable list:

- failing test name,
- path,
- category,
- feature tags,
- includes,
- failure kind: parse/runtime/timeout/assert/missing/crash,
- first line of diagnostic.

This is infrastructure, but it is what keeps a large compliance push honest.

## 3. Js42 Target Architecture

### 3.1 MIR lowering around spec records

Add a small set of lowering/runtime helpers that mirror ECMA records:

| New unit | Purpose |
|---|---|
| `js_mir_reference.{hpp,cpp}` | lower identifier/property/super/private references into get/set/delete/update operations |
| `js_mir_environment.{hpp,cpp}` | lower lexical/global/function/with/eval environment operations |
| `js_mir_completion.{hpp,cpp}` | statement completion values, abrupt completion, finally propagation |
| `js_mir_iterator.{hpp,cpp}` | IteratorRecord lowering, iterator close, for-of/destructuring spread behavior |
| `js_mir_class_semantics.{hpp,cpp}` | class heritage, private names, fields, static blocks, super constructor rules |

These files should not merely move code. They should define the boundary that
statement/expression lowering must use for complex semantics.

### 3.2 Runtime abstract-operation spine

Add or complete these runtime operations and require public ABI wrappers to use
them:

| Operation family | Required functions |
|---|---|
| Property keys/descriptors | `js_to_property_key`, `js_to_property_descriptor`, `js_from_property_descriptor`, `js_validate_and_apply_property_descriptor` |
| Ordinary objects | `js_ordinary_get`, `js_ordinary_set`, `js_ordinary_define_own_property`, `js_ordinary_delete`, `js_ordinary_own_property_keys` |
| Exotics | `js_proxy_get/set/has/define/delete/own_keys`, `js_ta_get/set/define/delete/own_keys`, array/string/arguments/module namespace hooks |
| References | `js_get_value`, `js_put_value`, `js_delete_reference`, `js_resolve_binding` |
| Environment records | `js_env_has_binding`, `js_env_create_mutable_binding`, `js_env_initialize_binding`, `js_env_get_binding_value`, `js_env_set_mutable_binding` |
| Iteration | `js_get_iterator`, `js_iterator_next`, `js_iterator_complete`, `js_iterator_value`, `js_iterator_close` |
| Jobs | `js_enqueue_promise_job`, `js_run_microtasks`, `js_promise_resolve_thenable_job` |

Compatibility wrappers such as `js_property_get`, `js_property_set`, and
`js_in` can remain, but they should delegate to this spine instead of carrying
parallel logic.

### 3.3 Built-in registry as source of truth

Expand `JsBuiltinMethodSpec` into a true intrinsic registry:

```c
struct JsBuiltinPropertySpec {
    const char* owner;
    const char* name;
    int name_len;
    int builtin_id;
    int length;
    int flags;
    ItemKind kind;  // data method, accessor get, accessor set, intrinsic object
};
```

The registry should install and answer descriptor queries for:

- constructor own properties,
- prototype methods,
- prototype accessors,
- well-known symbol properties,
- `name`, `length`, `prototype`, `constructor`,
- default attributes.

This directly targets Object/Function/Array/String/Date/Promise descriptor
failures and prevents future drift between "method exists" and "descriptor is
right".

## 4. Proposed Phases

### J42-1 - Failure Inventory And Runner Artifacts

Goal: make the 4,143 failures queryable without rerunning the full suite for
every question.

Work:

- Add `--write-failures=temp/js262_failures.tsv` to
  `test/test_js_test262_gtest.cpp`.
- Emit test name, path, status, message, category, features, includes, native
  harness flag, elapsed time, and RSS delta.
- Add `--feature-summary` or a small runner post-pass that writes
  `temp/js262_failures_by_feature.tsv` and
  `temp/js262_failures_by_path.tsv`.
- Keep all generated files under `temp/`.

Gate:

- Full `--run-partial` still reports `0` regressions, `0` batch-lost, `0`
  crash-exits.
- `temp/js262_failures.tsv` contains exactly the failed tests reported by the
  summary.

Expected impact:

- No pass-count change by itself.
- Makes every later phase measurable and repeatable.

### J42-2 - Reference Record And Assignment Lowering

Goal: remove duplicated assignment/update/call/super/with paths from expression
lowering.

Why this phase is high yield:

- `language/expressions/compound-assignment`: about 100 failures.
- `language/expressions/assignment`: about 80 failures.
- `language/expressions/super`, `new`, `call`, `in`, update expressions, and
  object-literal side effects are all Reference Record problems.
- Recent `with`/Proxy fixes were local; the generalized model should prevent
  repeating that class of bug.

Work:

- Add `JsMirReference` with kinds:
  `unresolvable`, `environment`, `property`, `super_property`,
  `private_name`, `with_binding`.
- Add shared lowering:
  `jm_emit_reference`, `jm_emit_get_value`, `jm_emit_put_value`,
  `jm_emit_delete_reference`, `jm_emit_update_reference`.
- Route simple assignment, compound assignment, postfix/prefix update,
  optional-chain assignment rejection, `delete`, `in`, and member calls through
  the same reference path.
- Keep fast paths only after the shared semantic path is correct.

Gate:

- Focused batches:
  `language/expressions/assignment`,
  `language/expressions/compound-assignment`,
  `language/expressions/super`,
  `language/statements/with`,
  `built-ins/Proxy`.
- Full baseline update with zero regressions.

Expected impact:

- 150-300 tests, mostly expression/reference and Proxy/Reflect edge cases.

### J42-3 - Environment Records, Eval, And Annex B Instantiation

Goal: model JS binding environments explicitly enough for eval/global/function
code and Annex B semantics.

Why this phase matters:

- `annexB/language/eval-code`: about 120 failures.
- `annexB/language/global-code`: about 75 failures.
- `annexB/language/function-code`: about 65 failures.
- `language/statements/function`, `let`, `const`, `switch`, `try`, and
  `identifier-resolution` failures share the same binding root.

Work:

- Add runtime environment-record operations:
  declarative, object, global, and function environment records.
- Represent `with` scopes as object environment records with unscopables.
- Represent direct eval as a new execution context linked to caller lexical and
  variable environments.
- Replace eval string rewriting and ad-hoc var export with script evaluation
  that returns a Completion Record.
- Implement GlobalDeclarationInstantiation and FunctionDeclarationInstantiation
  paths for top-level scripts, functions, and direct eval.
- Centralize Annex B B.3.3 block-level function declaration propagation.

Gate:

- Focused batches:
  `annexB/language/eval-code`,
  `annexB/language/global-code`,
  `annexB/language/function-code`,
  `language/global-code`,
  `language/identifier-resolution`,
  `language/statements/function`.
- Baseline update with zero regressions.

Expected impact:

- 250-450 tests.

### J42-4 - Class And Private-Name Semantic Layer

Goal: move class semantics out of scattered lowering branches and into a
class-specific semantic layer.

Why this phase matters:

- `language/statements/class`: about 350 failures.
- `language/expressions/class`: about 200 failures.
- Feature tags include `class`, public/private fields, private methods, static
  fields, and static blocks.

Work:

- Add `js_mir_class_semantics.{hpp,cpp}`.
- Separate class phases:
  class name binding, heritage evaluation, constructor creation, method
  definition, field initializer closures, static element evaluation, private
  environment creation.
- Add runtime private-name records instead of relying only on property-key
  conventions.
- Centralize `super()` and `super.prop` restrictions, derived constructor
  `this` initialization, and `new.target` propagation.
- Route static blocks through normal completion/finally semantics.

Gate:

- Focused batches:
  `language/expressions/class`,
  `language/statements/class`,
  `language/computed-property-names/class`,
  private-field and static-block feature subsets.
- Baseline update with zero regressions.

Expected impact:

- 400-700 tests.

### J42-5 - Descriptor And Exotic Object Convergence

Goal: make Object/Reflect/Proxy/Array/TypedArray/String descriptor behavior use
one descriptor engine.

Why this phase matters:

- `built-ins/Object`: about 220 failures.
- `built-ins/Array`: about 245 failures.
- `built-ins/TypedArray*`: about 235 failures.
- `built-ins/Proxy` and `Reflect`: about 45 direct failures, with many
  indirect descriptor failures.

Work:

- Complete `js_validate_and_apply_property_descriptor`.
- Make `Object.defineProperty`, `Object.defineProperties`,
  `Reflect.defineProperty`, Proxy `defineProperty`, array `length`, typed-array
  integer-indexed properties, and string-indexed properties call the descriptor
  engine.
- Add explicit exotic operation tables:
  `JsObjectOps ordinary_ops`, `array_ops`, `typed_array_ops`, `proxy_ops`,
  `string_object_ops`, `arguments_ops`.
- Move `js_property_get/set/delete/has/own_keys` to dispatch through these
  operation tables.
- Expand the built-in registry so descriptor queries and installation share
  one source of truth.

Gate:

- Focused batches:
  `built-ins/Object/defineProperty`,
  `built-ins/Object/defineProperties`,
  `built-ins/Object/freeze`,
  `built-ins/Object/seal`,
  `built-ins/Object/preventExtensions`,
  `built-ins/Array/length`,
  `built-ins/TypedArrayConstructors`,
  `built-ins/Proxy`,
  `built-ins/Reflect`.
- Existing `test_js_props_gtest` and a new descriptor invariant gtest.

Expected impact:

- 400-800 tests.

### J42-6 - Iterator, Completion, For-Of, And Generator Semantics

Goal: make loops, destructuring, spread, generators, and abrupt completion use
the same IteratorRecord and CompletionRecord machinery.

Why this phase matters:

- `language/statements/for-of`: about 90 failures.
- generator-related tags: about 190 failures.
- iterator prototype failures appear across Array, Map, Set, String, RegExp,
  and Generator prototypes.

Work:

- Add `js_mir_iterator` lowering helpers.
- Implement runtime IteratorRecord helpers and `IteratorClose`.
- Route for-of, array/object destructuring, spread, yield-star, Promise
  combinators over iterables, Map/Set constructors, and Array.from through the
  same iterator operations.
- Add CompletionRecord propagation for `break`, `continue`, `return`, `throw`,
  and `finally`.
- Rework generator state machines to use the completion/iterator helpers for
  `yield*`, `throw`, and `return`.

Gate:

- Focused batches:
  `language/statements/for-of`,
  `language/destructuring`,
  `language/expressions/yield`,
  `language/statements/generators`,
  `built-ins/GeneratorPrototype`,
  iterator prototype built-ins.

Expected impact:

- 200-400 tests.

### J42-7 - RegExp Front-End And Unicode Property Tables

Goal: add an ECMA RegExp compiler front-end before the RE2 backend.

Why this phase matters:

- `built-ins/RegExp/property-escapes`: about 430 failures by itself.
- Additional failures cover named groups, lookbehind, Annex B regex grammar,
  string/RegExp protocol methods, and `Symbol.match` family behavior.

Work:

- Add `js_regexp_compile.{h,cpp}` as the only path from JS pattern/flags to
  backend regex.
- Parse flags and pattern under ECMA rules before invoking RE2.
- Add Unicode property alias tables for General_Category, Script, and
  Script_Extensions.
- Translate supported property escapes into backend-compatible character
  classes or precomputed predicates.
- Validate named groups, duplicate group rules, backreferences, and lookbehind
  restrictions.
- Centralize RegExp protocol methods:
  `@@match`, `@@matchAll`, `@@replace`, `@@search`, `@@split`,
  `exec`, `test`, `lastIndex`.

Gate:

- Focused batches:
  `built-ins/RegExp/property-escapes`,
  `built-ins/RegExp/named-groups`,
  `built-ins/RegExp/lookBehind`,
  `annexB/language/literals`,
  `language/literals/regexp`.
- Watch slow-list pressure carefully; property-escape tests can be correct but
  too slow.

Expected impact:

- 450-650 tests.

### J42-8 - Promise Jobs And Async Integration

Goal: implement one microtask/job queue model shared by Promises, async
functions, dynamic import, and the host loop.

Why this phase matters:

- `built-ins/Promise`: about 150 failures.
- async/generator/iterator failures depend on predictable job ordering.

Work:

- Add `js_job_queue.{h,cpp}` for PromiseJobs.
- Implement Promise reaction jobs and thenable jobs per spec.
- Route `Promise.all`, `allSettled`, `any`, `race`, `resolve`, and `reject`
  through common capability/resolve-element helpers.
- Connect async function lowering to the Promise job queue.
- Add test262 async harness enablement as a later gate once the queue is stable.

Gate:

- Focused batches:
  `built-ins/Promise/all`,
  `built-ins/Promise/allSettled`,
  `built-ins/Promise/any`,
  `built-ins/Promise/race`,
  `built-ins/Promise/prototype`,
  async function/generator subsets that are currently in scope.

Expected impact:

- 150-300 tests immediately, more after async test flags can be enabled.

### J42-9 - Shared Memory, Atomics, And BigInt Typed Arrays

Goal: stop treating Atomics/SAB/BigInt typed arrays as ordinary typed-array
variants.

Why this phase matters:

- `built-ins/Atomics`: about 150 failures.
- `built-ins/SharedArrayBuffer`: about 40 failures.
- BigInt and typed arrays overlap heavily.

Work:

- Add explicit SharedArrayBuffer storage and typed-array view tracking.
- Add Atomics validation helpers:
  `ValidateSharedIntegerTypedArray`, `ValidateAtomicAccess`, element-size and
  index checks.
- Implement atomic read-modify-write operations using platform atomics where
  safe, with single-threaded fallback only for tests that do not need real
  concurrency.
- Separate Number typed-array conversion from BigInt typed-array conversion in
  one conversion helper.

Gate:

- Focused batches:
  `built-ins/Atomics`,
  `built-ins/SharedArrayBuffer`,
  BigInt typed-array constructor subsets.

Expected impact:

- 150-250 tests.

### J42-10 - Built-In Algorithm Families

Goal: clean up the remaining medium-sized built-in clusters after the structural
spine is in place.

Families:

- Array methods and species paths.
- String prototype algorithms.
- Date edge cases.
- BigInt prototype/static algorithms.
- Map/Set iteration and constructor edge cases.
- JSON parse/stringify details.
- Function prototype `call`/`apply`/`bind`/`toString`.

Work pattern:

- For each family, make a focused failure manifest.
- Identify whether failure is descriptor, conversion, iterator, species,
  abrupt completion, or algorithm body.
- Implement against the shared abstract operations from earlier phases.
- Update baseline only with zero regressions.

Expected impact:

- 500-900 tests across several smaller phases.

## 5. Execution Rules

1. Keep every phase complete: no half-phase tranche that leaves a new semantic
   abstraction unused.
2. Every phase must start with a failure manifest and end with a baseline gate.
3. Prefer spec-operation helpers over local patches.
4. Keep fast paths after correctness, not before it.
5. Do not add new global state; add fields to `JsRuntimeState` or a scoped
   execution context.
6. Do not add more marker-string semantics where shape flags, descriptors, or
   private-name records are the right model.
7. Do not use runner skips to hide in-scope ES2020 failures.

## 6. Recommended First Phase

Start with J42-1 and J42-2 together as one complete phase:

- J42-1 gives us durable failure artifacts.
- J42-2 attacks Reference Record semantics, which affects many categories but
  is still narrow enough to finish without rewriting the whole runtime.

Then do J42-3 and J42-4. Those two address the largest language clusters:
environment/eval/Annex B and class/private-name semantics.

Recommended first verification set:

```bash
make build-test
./test/test_js_test262_gtest.exe --batch-file=temp/js42_reference_batch.txt --js-timeout=30
./test/test_js_test262_gtest.exe --run-partial --update-baseline --js-timeout=30
```

The expected near-term target after J42-1 through J42-4 is not 100% test262.
A realistic target is to move from `30026` passing to the `30800-31500` range
while reducing the most fragile language-semantics failures. After J42-5 and
J42-7, the project should be positioned for a larger jump toward `32000+`.

