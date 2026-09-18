# Lambda Implementation Plan: Shared AST Interpreter Tuning

**Date:** 2026-09-18  
**Status:** IN PROGRESS — Phases 1–3 have landed their scoped interpreter
slices; Phase 4 has scoped JS and Lambda activation-window convergence slices.
Measurement and the remaining cross-profile slices are pending.
**Source baseline:** `12b507e51`; measurements captured on 2026-09-18.  
**Scope:** Lambda's boxed AST interpreter and the existing Item-based LambdaJS
AST backend, pinned to interpretation. Not the separate JS MVP ABI/backend.

**Formal authority:** [Lambda Formal Design](../../doc/Lambda_Formal_Design.md)
**D1.3v3, D1.4v4, D1.5v2, D4.6.1v3–D4.6.2v2, D5.3.2–D5.3.6,
D6.2.2v2–D6.2.3v2, D8.1.1v9, D8.1.3v11, D8.2.1–D8.2.5v2,
D8.4.1v2–D8.4.3v2**; [Lambda Formal Semantics](../../doc/Lambda_Formal_Semantics.md)
**S1.6, S4.4–S4.5, S9.1.2–S9.1.4, S9.2.2**.

**Related work:** [JS interpreter design](../Lambda_Design_JS_Interpreter.md),
especially JSI26's common activation service; [JS interpreter implementation
record](Lambda_Impl_JS_Interpreter.md); [Lambda interpreter implementation
record](<Lambda_Impl_Ast_Interp (done).md>). Existing MIR tuning rounds are
separate work; their native-code timings are not this plan's baseline.

This document is an informative implementation plan, not a new semantic ruling.
Phase numbers are work sequencing, not a new design-ledger ID series. Any
missing ruling or conflict discovered during implementation requires a decision
before proceeding; a ratified change updates both the formal specification and
its corresponding working design document.

## 1. Objective and non-goals

Reduce avoidable work in both interpreters by sharing stable execution facts,
name/index handling, activation ownership and frame resource planning. Preserve
separate semantic evaluators and measure improvements independently for each
language. A JS-only sparse-array win must not conceal a Lambda regression or
be presented as evidence that the shared interpreter machinery improved.

The initial priority order is:

1. Close Lambda's bare-literal decoding gap and reduce JS literal rematerialization.
2. Resolve names once and preserve integer-index lanes through runtime access.
3. Plan binding access, call shape and stable contract facts once.
4. Share activation bookkeeping and planned scratch/argument root windows.
5. Move eligible JS locals into rooted frame slots using the same machinery.
6. Specialize only the remaining measured scalar/access bottlenecks.

Non-goals:

- No auto-tiering, hot-function MIR compilation, bytecode, second executable
  IR, or new unboxed interpreter value stack.
- No mutable per-site inline caches, feedback vectors, receiver/callee caches
  or runtime AST rewriting (**D8.4.1v2, D8.2.2**).
- No language-semantic merger: JS coercion, TDZ, mutable lexical cells,
  completions and iterator closing remain JS-owned. Lambda snapshot/COW,
  error/absence and numeric rules remain Lambda-owned.
- No JS tail-call optimization or change in stack-limit behavior. The failing
  `tco.js` workload is not authority to transplant Lambda's tail-frame reuse.
- No new supported JS syntax, suspension model, default backend, runtime,
  heap, scheduler, module registry or allocator framework.
- No conservative GC scanning, vendor modifications, benchmark-specific
  shortcuts, golden-output changes to hide failures, or legacy C2MIR reliance.

## 2. Evidence and baseline

The [profiling report](../../temp/runtime_compare/ast_analysis/report.md),
[manifest](../../temp/runtime_compare/ast_analysis/manifest.json),
[measurements](../../temp/runtime_compare/ast_analysis/measurements.jsonl), and
[probe script](../../temp/runtime_compare/ast_analysis/probe.py) are local
artifacts under `temp/`, not guaranteed tracked files. Preserve the evidence
before cleaning that directory; rerun the baseline if artifacts are missing.

### 2.1 Measured slow workloads

Whole-process wall seconds, median of five fresh-process release runs after a
discovery/warm-up sweep. Lambda paths are under `test/benchmark/`; JS paths are
under `test/js/`. These are script fixtures, not all standalone GTests.

| Rank | Lambda fixture | Median s | JS fixture | Median s |
|---:|---|---:|---|---:|
| 1 | awfy/richards2.ls | 4.238 | sparse_reduce.js | 6.245 |
| 2 | beng/mandelbrot2.ls | 3.947 | sparse_find.js | 4.727 |
| 3 | awfy/richards.ls | 3.599 | sparse_slice_splice.js | 3.751 |
| 4 | kostya/matmul2.ls | 2.603 | lib_fast_diff.js | 3.499 |
| 5 | awfy/deltablue2.ls | 1.470 | sparse_mutate.js | 3.268 |
| 6 | larceny/pnpoly2.ls | 1.465 | sparse_concat_flat.js | 3.099 |
| 7 | r7rs/ack2.ls | 1.422 | sparse_includes.js | 2.731 |
| 8 | awfy/deltablue.ls | 1.274 | tco.js — fails | 2.499 |
| 9 | beng/nbody2.ls | 1.061 | hljs_highlight.js | 1.886 |
| 10 | kostya/levenshtein2.ls | 0.866 | collection_gc_retention.js | 0.726 |

The original capture discovered 892 Lambda and 428 JS fixtures; all Lambda
fixtures passed, while JS passed 426/428. The other JS failure was
`regex_bt_legacy_octal_assertion.js`. Standalone GTests were not independently
ranked. All Lambda top-ten samples reported zero MIR fallbacks; all JS top-ten
phase probes emitted zero MIR instructions. Nine other Lambda fixtures did
fall back despite `LAMBDA_TIER=interp`; preserve their identities in the
coverage/fallback report, not the strict-AST performance population.

Release executable SHA256:
`a1ed8836a1504688fda059a05e9712b4887bbe5926821fe888dde7d7311c67ec`.
The earlier AST-versus-MIR suite comparison did not preserve its executable
and has uncertain release provenance. Do not derive targets or speedup claims
from its ratios.

The six sparse JS fixtures contribute 23.820 s, or 73.5% of the JS top-ten
summed medians. Several JS runs varied by approximately 2×; the report retains
ranges. These observations motivate work, not precise speedup predictions.

### 2.2 Measured causes versus hypotheses

| Evidence | Confirmed hot work | Implementation hypothesis to test |
|---|---|---|
| Richards2 native sample | Name hashing/lookup, member access, call/contract handling | Reuse linked names and static call/binding facts |
| Mandelbrot2 sample | AST dispatch, type unwrapping/admission, numeric dispatch, integer parsing | Predecode literals and contract shape; then guard scalar operations |
| Ackermann2 sample | Calls, argument/declaration formatting, contracts | Preplan call resources; build error descriptions only on failure |
| Sparse reduce sample | HasProperty → HasOwn → ToPropertyKey → NamePool | Preserve numeric indices; avoid repeated canonicalization |
| Sparse find sample | Name lookup plus callback/root machinery | Improve the same index route and shared activation costs |
| Fast-diff sample | JS binding/eval-journal checks and root reservations | Planned binding access and fixed scratch/argument windows |
| Highlight.js sample | Repeated regex alias rewriting and byte comparisons | Separate token-aware regex preprocessing optimization |
| Collection retention sample | Allocation, property/name lookup, roots and GC | Measure allocation/root deltas; no single-cause claim yet |

For scale, sparse reduce had 812 self samples in NamePool lookup and 251 in
hashing, against 1,727 main-thread samples. These are partial-run statistical
samples, not whole-test attribution or an additive speedup estimate. Exclude
the idle background thread's wait samples from interpretation.

JS phase probes attributed 99.16–99.97% of their pipeline time to execution,
including library initialization. Lambda's procedural `run ... main()` probes
reported zero `interp_exec` despite multi-second execution: repair or supplement
that instrumentation before using it as an execution metric.

## 3. Required architecture and ownership

```text
Existing indexed AST, bindings, captures and function effects
                           |
          Shared interpreter resource/fact planning
                           |
          Shared activation and root/argument windows
                 /                         \
       Lambda evaluator                 JS evaluator
       snapshot/COW rules               cells/TDZ/completions
                 \                         /
       Existing storage, name, property and call substrate
            with language-specific semantic adapters
```

Profile selection occurs at activation entry. Do not add a language test to
every core node or introduce a general virtual-dispatch layer merely to share
code. Equivalent structural helpers are extracted after both clients work
(**D8.2.1–D8.2.3**; JSI26).

| Concern | Shared owner/mechanism | Retained profile responsibility |
|---|---|---|
| AST facts | Existing node/scope/binding/function owners and pass schedule | Name resolution and semantic effect rules |
| Resources | Frame sizing, scratch overlap, exact roots, watermark protocol | Which operands/completions remain live and when |
| Names | Existing NamePool, PropertyKeySpec and per-context NameId linking | Observable ToPropertyKey, symbols/private keys, exotic traps |
| Local storage | Rooted Item slots and existing owned-scalar/environment storage | Lambda captures by snapshot; JS captures by mutable cell |
| Calls | Existing call entries and rooted argument ownership | JS call/construct/this/newTarget; Lambda signatures and borrows |
| Control flow | Entry/exit/source ownership helpers | JS finally/IteratorClose and Lambda signal/error semantics |

Fact placement follows **D8.2.4–D8.2.5v2**: effective types remain on
`AstNode.type`, declarations own source contracts, and function/scope plans own
their resource shape. Do not add an ID-keyed per-node fact database or put
use-specific metadata on shared `TYPE_ANY`/literal type singletons. A capture
offset is relative to a particular function; it cannot be a universal offset
on a `NameEntry` shared by several closures.

Runtime name IDs and GC values remain context/realm-owned, not embedded as
arbitrary process-specific values in reusable AST images (**D4.6.2v2**).
Data-pointer caching across allocation is forbidden: object headers and
movable backing storage have different lifetimes (**D4.3.1**).

## 4. Phases and dependencies

| Phase | Deliverable | Depends on | Initial scope |
|---|---|---|---|
| 0 | Reproducible strict-AST baseline and causal counters | — | Both languages |
| 1 | Lambda bare-literal decoding and JS literal rematerialization | 0 | Both literal evaluators and constant ownership |
| 2 | Linked names and canonical index/property paths | 0, 1 | Shared substrate plus profile adapters |
| 3 | Immutable interpreter facts and removal of repeated preparation | 0, 1 | Both planners/walkers |
| 4 | Common activation and planned root/argument windows | 3 | Both walkers |
| 5 | Proven JS frame-local storage | 3, 4 | JS client of common storage |
| 6 | Guarded scalar/access specialization, if still justified | 2, 3; reprofile after 4 | Both profiles where semantics coincide |
| 7 | Sparse algorithms and regex preprocessing | 0; sparse work builds on 2 | Separate JS-runtime work |
| 8 | Full validation, before/after report and cleanup | Landed phases | Both languages |

Phase 1 is the first optimization delivery after baseline preparation. Its two
language slices should be measured separately and together. Afterward, Phases 2
and 3 may be developed independently in non-overlapping changes. Timing runs
must remain serialized. Phase 7 is not a prerequisite for calling the shared-core
work complete and cannot substitute for its acceptance gates.

## 5. Phase 0 — Freeze a trustworthy baseline

### Work

1. Build release with the project release workflow (`make release`); preserve
   the executable, hash, source revision and dirty-tree patch description
   under `temp/interp_tune/<run-id>/`. Build both before/after candidates before
   measuring either. Record test-runner build configuration independently.
2. Promote the useful measurement behavior into the existing `test/interp/`
   tooling. Reuse `lambda_process.py` for isolated processes, timeouts and
   descendant cleanup; extend existing measurement/discovery helpers instead
   of creating a third subprocess runner. The temporary `measure.py` is a
   prototype, not an additional permanent framework.
3. Freeze a manifest from actual GTest discovery. Preserve invocation kind,
   document files, module paths, permission options, platform goldens and
   fixture hashes. Audit differences from the 892/428 baseline rather than
   silently using a changed population.
4. Set `LAMBDA_TIER=interp` and `JS_EXECUTION_BACKEND=ast` explicitly for every
   measured child. Sanitize inherited tier, GC-stress and diagnostic controls;
   hold logging fixed without editing `log.conf`. A missing selector is not
   equivalent to pinned AST.
5. Record actual execution. Lambda needs executed/fallback/excluded counts;
   JS needs backend identity and aggregate MIR-generation evidence, including
   nested dynamic-code/import boundaries. Missing evidence is unknown, not zero.
6. Keep three populations: strict-AST successful performance fixtures, the
   complete correctness manifest, and explicit mixed/fallback/error records.
   No fixture disappears from correctness coverage to make a performance claim.
7. Separate process wall time, parse/build/plan time, execution, and cleanup.
   Audit the procedural timing gap in `runner.cpp`/CLI invocation; instrument
   the actual entry invocation and avoid double-counting nested imports or
   callbacks. Until fixed, whole-process wall time remains the comparable metric.

### Diagnostic counters

Add only counters needed to test a phase's cause, using existing diagnostic
facilities where possible. Proposed counter fields are not new public APIs:

- Name lookups, segment probes, new interned names and key materializations.
- Binding access categories and slow-path reasons, including eval/with/imports.
- Runtime argument-list/scope scans and literal decodes.
- Root-window reservations, peak live roots, scratch high-water mark and copies.
- JS environment allocation count/bytes; allocated versus frame-local bindings.
- Contract-shape analysis and diagnostic-string formatting counts.

Do not increment expensive counters or format per-node logs in timed release
runs. Use a diagnostic build/configuration of the same release optimizations
and/or existing runtime-controlled aggregation separately. Diagnostics must not
change tier selection, semantic coverage or scheduling.

### Exit

- One warm-up and at least five measured samples per selected fixture, with
  exact goldens/status and backend evidence retained.
- Before/after execution interleaved by fixture; reverse pair order between
  rounds. No simultaneous build, profiler or benchmark initiated by the runner.
- Immutable binaries and a machine-readable manifest/results schema exist.
- Existing failures/fallbacks are enumerated by identity, not only totals.
- Phase timing is either verified or explicitly unavailable; never inferred
  from a zero/missing column.

## 6. Phase 1 — Decode Lambda literals once; reuse JS immutable literal values

This phase addresses two specific gaps, not a new general constant-folding
pass. Preserve existing folds and close the literal paths that still do work
on each evaluation (**D8.2.4–D8.2.5v2, D8.1.3v11**).

### 6.0 Landed implementation (2026-09-18)

- Lambda now retains admitted compact integer and boolean payloads on each
  `AstPrimaryNode`; `eval_literal` loads those values and retains the former
  source-span decoder only as a defensive compatibility fallback. The existing
  folded-constant pool and MIR contracts are unchanged.
- JS assigns immutable string and BigInt literal occurrences dense AST slots.
  On first evaluation, the interpreter materializes the value into a
  realm-owned `RootVector` cache keyed by the immutable AST image and reuses it
  for subsequent evaluations. The AST holds no runtime pointer, so a cached
  AST shared by scripts cannot cross a realm boundary (**D5.1.1v2, D5.4.2,
  D8.1.3v11**).
- The JS regression exercises repeated use, forced collection, and
  `runtime_reset_heap` followed by cached-AST reuse. Lambda AST-mode literal
  smoke tests cover hexadecimal integers and booleans. A TypeScript enum
  regression confirms parser-synthesized string literals have no cache slot.
  The instrumentation and controlled runtime measurement exit gates in §6.4
  remain outstanding.

### 6.1 Current behavior and scope

- Lambda's `eval_expr` already calls `interp_const_folded_value` before normal
  dispatch. Eligible folded expressions and constant-binding reads load their
  published pool values; do not replace or duplicate this mechanism.
- The folding pass skips `AST_NODE_PRIMARY`. The ordinary `eval_literal` path
  reparses compact `LIT_INT` literals from source using `parse_int_literal_span`
  and reads booleans from source using `parse_bool_literal_span`. Floats already
  carry decoded values, and many other literals already use the constant pool.
  Specialized integer-loop paths also have literal reuse; preserve that benefit.
- JS Number/boolean literals are already decoded in the AST. String escapes
  are decoded during AST construction, but evaluation calls `js_make_string_len`
  to copy the bytes into a runtime string. BigInt evaluation calls
  `bigint_from_string` on its stored normalized spelling each time.
- General JS folded-expression consumption is outside this phase. Do not
  apply Lambda's folder to JS operators without a separate profile-correct
  design. Reading an initialized `const` binding is also distinct from
  reevaluating a literal expression.

### 6.2 Close Lambda's bare-literal decoding gap

Starting points: `build_ast.cpp::build_literal_type_from_span`,
`ast.hpp::{parse_int_literal_span,parse_bool_literal_span}`,
`interp.cpp::{eval_literal,interp_const_folded_value,interp_const_fold_script}`,
and existing constant-pool publication/loading helpers.

1. Retain the already validated compact integer value during AST preparation
   instead of discarding it and preserving only `LIT_INT` plus a source span.
   Store the boolean value at preparation too. Reuse the existing literal/
   constant owner and loader; do not introduce another literal-value table.
2. Preserve effective literal types and source spans for inference and
   diagnostics. Never place one occurrence's value on shared `LIT_INT`,
   `LIT_BOOL` or other type singletons (**D8.2.5v2**).
3. Make ordinary bare-literal evaluation load the prepared value, including
   literals inside expressions that cannot fold, such as a mutable local plus
   `1`. This must work with `LAMBDA_CONST_FOLD=0`; literal decoding is source
   admission, not optional expression folding.
4. Reuse the same prepared literal in eligible specialized integer-loop paths
   where practical; do not leave a second source-decoding implementation or
   duplicate immutable payload solely for that path. Keep MIR consumers
   compatible without changing the selected execution backend.
5. Preserve the accepted spelling/range rules, separators, radix prefixes,
   integer exponents, signed-expression handling and invalid-literal errors.
   Reuse admission's exact value rather than inventing a new numeric parser.

**Exit:** diagnostic counts show zero source decodes attributable to compact
integer/boolean literal evaluation after preparation, both with folding enabled
and disabled. Existing folded expressions remain pool loads; floats and already
pooled literals do not regress. Explicit user string-to-number conversions are
not subject to this zero-decode requirement.

### 6.3 Reduce JS literal rematerialization

Starting points: `js_c_ast_helpers.cpp::build_js_literal_from_source`,
`js_interp.cpp::js_interp_eval`, `js_runtime_value.cpp::js_make_string_len`,
`bigint_from_string`, and retained `JsScript`/realm constant ownership.

1. Keep the current predecoded Number/boolean path. Identify immutable string
   and BigInt literal occurrences that currently copy/parse on every evaluation.
2. Prepare an owned runtime value once per applicable script/context lifetime,
   or lazily on first evaluation when eager preparation would allocate unused
   literals or alter failure timing. Select the policy using cold-start,
   retained-memory and semantic evidence; measure first use separately.
3. Reuse existing constant slots and ownership-qualified loading. Reusable ASTs
   carry stable literal facts/handles, not foreign heap pointers. Register/trace
   materialized values through the existing owner before any further MAY_GC
   operation, and retire them with that owner (**D5.3.3, D8.1.3v11**).
4. Do not substitute a NamePool property-name identity for an ordinary runtime
   string to avoid an allocation. Preserve the property/value boundary and
   cross-context lifetime rules (**D4.6.1v3–D4.6.2v2**).
5. Reuse only immutable primitive values; verify that arithmetic/conversion
   helpers cannot mutate a retained BigInt payload. Keep array/object/regex
   instances fresh and leave template-site identity rules unchanged. No
   process-global literal cache or mutable per-site dispatch cache is introduced.
6. Share publication/loading/ownership mechanics with Lambda where equivalent,
   while keeping decoding, value representation and lifetime adapters specific
   to each profile. Extract after both clients work (**D8.2.3**).

**Exit:** repeated evaluation of an admitted literal performs no repeated
string-byte copying or BigInt text parsing after its first materialization in
the same live owner. Fresh contexts receive correctly owned values; teardown
releases retained values and repeated script/context lifecycles do not grow
memory without bound. Non-admitted cases and their reasons are reported.

### 6.4 Phase-specific tests and measurements

- Lambda: bare integers/booleans inside non-foldable loops; folded expressions
  versus bare literals; folding on/off; distinct literals sharing a base type;
  supported radix/separator/exponent and boundary spellings; invalid input;
  repeated execution and imported-script ownership.
- JS: repeated escaped/Unicode/embedded-NUL strings; large/radix BigInts;
  primitive equality and repeated BigInt arithmetic without operand mutation;
  forced GC between literal uses; retained values across callbacks; fresh
  realms/context teardown and reusing an AST with a new owner; untaken branches
  containing large literals; mutable literal freshness counterexamples.
- Record source-decode count, string materializations/bytes and BigInt parses
  by literal path in separate diagnostics. Record preparation/first-use time,
  steady-state wall time, retained bytes and peak RSS in release comparisons.
- Rerun Lambda's arithmetic/call cohort and JS Fast-diff/other literal-heavy
  libraries. Add focused probes only as supplementary evidence; do not infer
  a whole-suite gain from a literal-only loop. Add matching expected results
  for every new Lambda script.

## 7. Phase 2 — Resolve names once; keep indices numeric

### 7.0 Landed implementation (2026-09-18)

- Lambda dotted-member reads and COW-path member segments now use one
  activation-local map from immutable `AstIdentNode` occurrence to the active
  `EvalContext`'s `NamePool` `String`. The first access performs the existing
  pool lookup; later accesses reuse that context's name identity. The map is
  freed on normal runner teardown and the standalone activation guard. The
  shared AST stores no runtime `String*` (**D4.6.2v2**).
- The JavaScript static-member path already carries its parser-owned spelling
  through the existing canonical property-lane APIs; it did not repeat source
  spelling interning. The audit also confirmed that `js_get` and `js_set`
  preserve an admitted numeric lane through ordinary dense-element paths.
  No speculative sparse `HasProperty` shortcut landed: holes, prototypes,
  proxies, accessors, typed arrays and generic array-likes still require the
  existing semantic kernel.
- `InterpWalker.RepeatedStaticMemberAccess`, release AST smoke runs including
  procedural COW, and the existing JS literal-cache regressions pass. Counter
  and before/after timing gates remain pending, so this is not a speedup claim.

### 7.1 Static member names

Starting points: `interp.cpp::eval_expr` member-expression arm,
`js_interp.cpp` member-key evaluation, `name_pool.cpp`, and existing
`PropertyKeySpec`/NameId linking.

1. Trace static member spelling from AST admission to context linkage.
2. Reuse the existing linked key/NameId owner. Add a missing link once during
   preparation, not at each access; keep serialized/static identity separate
   from context-specific linked identity.
3. Route Lambda dotted reads/writes and eligible JS named references through
   existing keyed lookup/store helpers. Promote a suitable `static` helper to
   its module header if both clients need it; do not copy lookup logic.
4. Preserve path-specific Lambda behavior and JS receiver/prototype/exotic
   semantics. Linking a name proves its identity, not a receiver shape or slot.

**Exit:** repeated ordinary access to a linked static member performs no
re-interning after preparation. Richards and a JS named-access/library case
exercise the same identity route. Cross-context AST reuse does not retain a
foreign NameId or realm-owned object.

### 7.2 Numeric-index and canonical-key routes

Starting points: `js_runtime.cpp::js_array_index_key`,
`js_array_method_property_key`, `js_has_property_status`, and
`js_globals.cpp::js_has_own_property`.

1. Audit the existing property-lane APIs before adding any overload. Name the
   distinction between an unconverted user key, canonical property key, and
   an admitted integer array index; do not encode it with ambiguous booleans.
2. Preserve integer keys through ordinary dense/sparse own-element Get/Has/Set
   and prototype traversal where admissible. Avoid numeric → string → interned
   name → parsed index round trips.
3. Reuse the same canonical key for a semantic operation. Arbitrary object-key
   coercion must not be duplicated or moved across other observable work.
4. Materialize the required observable key at proxy/reflection/exotic boundaries.
   A guard miss reaches the existing kernel with the original operation,
   receiver and completion behavior; it must not replay an already-run getter
   or conversion.
5. Cover array-index limits, generic array-like indices through the existing
   supported integer range, negative zero/canonical numeric strings,
   symbol/private identity, numeric companions, accessors and typed arrays.

**Exit:** the ordinary sparse-hole diagnostic case does not intern a new name
for each absent index. Proxy/accessor cases retain exact operation order and
key spelling. Both AST and MIR consumers of changed shared kernels retain
correctness (**D4.6.1v3, D8.4.1v2–D8.4.3v2**).

### 7.3 NamePool segment lookup, conditional on residual evidence

`name_pool_lookup_local_strview` searches individual overflow segments after
a miss. Reprofile after 7.1/7.2: eliminating unnecessary interning may remove
most of this cost without a pool change.

If it remains material, evolve the existing NamePool spelling index to serve
the complete identity scope, or reuse a precomputed hash through segment probes.
Compare alternatives for lookup cost, memory, publication, rollback and teardown.
Preserve parent-first lookup, first-definer identity, sealed static roots,
dynamic-child ownership and ID exhaustion behavior (**D4.6.2v2**). Do not add
a competing identity registry or a per-access-site cache.

**Exit:** large multi-segment tests preserve identical identity resolution and
show the intended probe/hash reduction without unbounded memory retention.
Choose no pool-layout change if the measured benefit does not justify it.

## 8. Phase 3 — Immutable interpreter preparation

### 8.0 Landed implementation (2026-09-18)

- The existing Lambda frame-plan pass now links a capture-read occurrence to
  its immutable owning `AstFuncNode` and dense closure-environment slot. The
  walker consumes that link only when the active frame has the same owner;
  view overlays, imports, object fields, ordinary locals, and any unlinked
  occurrence retain the generic binding route. This removes the repeated
  `FnCapture` list scan without placing a universal capture index on a shared
  `NameEntry` (**D6.2.3, D8.2.4–D8.2.5v2**).
- The same plan records source argument count and named-argument presence on
  each representable Lambda call. `eval_call` reuses the fact, while unplanned
  calls retain the old scan and malformed oversized calls retain the existing
  runtime arity diagnostic. It does not cache a callee, receiver, resolved
  argument value, or dynamic dispatch result (**D6.2.2v2**).
- `InterpWalker.RepeatedCaptureRead` exercises snapshot capture reads in a
  repeated-call loop. Focused interpreter differential tests cover captures,
  static members, named arguments, literals and type binders. JS lexical-cell
  slots are now sealed by direct-scope construction before the AST can be
  shared by executions; `js_interp_env_create` consumes the prepared count and
  no longer rewrites `NameEntry::slot` per activation. Lazy synthetic class
  field-initializer functions likewise seal their known zero-slot scope at
  construction in the execution overlay (**D8.2.4–D8.2.5v2**).
  AST callable definitions now retain their source-owned parameter shape and
  resolved formal length in the shared `JsCallableCode`; closures, mapped
  `arguments`, and class constructors consume that immutable record instead
  of rescanning parameters. The same parameter-facts pass now supplies the
  callable's total arity, removing the immediately preceding independent list
  count (**D8.2.4, D8.2.5v2**).
  `AstCallNode` now has one shared preparation routine for representable
  source arity, named operands, and spread presence. Lambda's plan and every
  JS call constructor use it; the JS walker can select its direct rooted-call
  path without another argument-list scan, while oversized lists retain the
  existing scan and diagnostics (**D8.2.4, D8.2.5v2**).
  Lazy parameter-diagnostic formatting remains separate work: the current
  contract checker receives an already-built diagnostic string, so a lazy
  descriptor boundary needs a dedicated ownership/error-format design.

Starting points: `ast-core.hpp::{NameEntry,NameScope,FnAnalysis,FnFramePlan,
AstCallNode}`, `compiler_pass.cpp`, `interp_plan.cpp`, and both walkers.
Use the indexed compilation unit and common child enumeration; no independent
core-child traversal or alternate pass schedule (**D8.2.4–D8.2.5v2**).

### 8.1 Binding access plans

- Classify each binding use as frame-local, Lambda snapshot capture, JS lexical
  cell, module/live import, global, object/with lookup or unresolved/dynamic.
  Preserve existing authoritative identities and store only missing facts.
- Assign JS scope slot counts once; stop rewriting `NameEntry::slot` on every
  environment creation/initialization.
- Replace Lambda's repeated capture-list searches with function-relative
  planned capture access. Retain method-field and view-binding behavior.
- Reuse `FnAnalysis` facts such as direct-eval, with, arguments and suspension
  effects. Prove which slow paths are unnecessary for each use; do not infer
  safety solely from an absent token in the current function body.
- For JS, model actual allocated environment records, including omitted empty
  scopes and per-iteration records. A syntactic scope depth is not automatically
  a valid runtime environment depth.
- Keep TDZ/const checks, mapped-arguments aliases, live imports and eval-created
  bindings where semantically required. Resolve dynamic binding values afresh.

**Exit:** a proven ordinary local read/write performs no name lookup,
capture-list scan, eval-journal search or unrelated import scan. The dynamic
counterexamples still use the generic semantic path and pass their tests.

### 8.2 Call shape and argument preparation

- Precompute ordinary source arity, positional/named/spread classification,
  default/rest requirements and call-root demand on the existing call/function
  owners. Reuse immutable argument mappings only where binding facts prove them.
- Do not cache a mutable binding's callee value, method target or receiver.
  Direct-call admission must remain valid when bindings can be reassigned.
- Preserve left-to-right evaluation, optional-call short circuit, receiver,
  `newTarget`, super, direct eval, borrow handles and error routing.
- JS ordinary calls already use `RootSpan` instead of allocating a JS argument
  array. Retain that improvement; Phase 4 removes repeated reservation/scanning,
  not an argument-array allocation that is already gone.
- Dynamic spread and other unbounded adapter demands retain the existing
  precisely rooted dynamic span. Never impose an optimization-specific arity cap.

**Exit:** repeated ordinary calls perform no source argument-list classification
scan. Existing `fn->invoke`/`fn->construct` and Lambda dynamic-call entries remain
the sole dispatch authorities (**D6.2.2v2**).

### 8.3 Contracts and lazy diagnostics

Literal decoding/materialization belongs to Phase 1 (§6), not a second
implementation in this phase. Build on its prepared values and ownership.

- Normalize stable contract structure and binder-presence classification once.
  Separate these from value-dependent admission, binder environments, COW and
  mutation-sensitive proofs; the latter cannot be skipped merely because a
  declaration is typed (**S1.6, S9.1.2–S9.1.4, S9.2.2**).
- Replace eagerly formatted declaration/argument boundary messages with source
  descriptors passed to existing error construction. Format only if the
  operation fails, retaining the same diagnostic content and source location.

**Exit:** successful ordinary declarations/calls do not format failure
descriptions or repeat stable contract-shape analysis. Numeric edge cases,
typed admission failures and cold-start preparation/memory costs are checked.

## 9. Phase 4 — Common activation and planned root windows

Starting points: `interp.cpp::{InterpFrameGuard,Scratch}`,
`js_interp.cpp::JsInterpFrame`, `runtime-state.{h,cpp}`,
`lambda-root-frame.hpp`, `side_stack.c`, `FnFramePlan`.

### 9.0 Landed implementation (2026-09-18)

- Ordinary JS calls and generator/async-generator activation setup now reserve
  one exact one-slot root window for the complete parameter-binding sequence,
  rather than opening and closing an identical window for every parameter.
  The window exists only when a function has parameters. Each completed
  binding is owned by the traced function environment, so the reusable slot
  roots only the parameter currently crossing allocation-capable binding work
  and is cleared before a later rest-array allocation (**D5.3.3**). This
  matches the activation-scoped reservation discipline
  already used by Lambda's `InterpFrameGuard`, without falsely merging JS
  suspension/control-frame semantics with Lambda frames.
- Focused JS coverage exercises defaults and rest parameters in both ordinary
  and suspended activation setup. This is a client convergence slice, not yet
  a common abstraction: the audit found JS `yield`/`await`, direct eval and
  re-entrant property/call behavior need distinct lifetime plans before a
  shared activation type is justified.
- Lambda's bounded ordinary dynamic-call arguments now borrow a contiguous
  span from their planned activation root window. The planner reserves every
  source argument plus a possible pipe injection and retains the callee root
  while nested arguments run. Named and special call shapes, and any
  unplanned/oversized activation, retain the established dynamic `RootSpan`
  route (**D5.3.3, D8.2.5v2**).

### 9.1 Converge ownership before extraction

1. Inventory existing entry scopes and responsibilities: canonical context,
   current script/module, active slab, source position, stack/depth limits,
   root/number marks and result publication. Reuse `RuntimeExecutionScope` and
   its helpers; do not build a second owner with overlapping teardown.
2. Bring both clients to an equivalent minimal entry/exit contract.
3. Extract common activation mechanics only after tests exercise both clients.
   Keep profile frame payloads and internal completions separate. A new common
   file/header is justified only by this real two-client extraction.
4. Preserve existing limit accounting and source/stack-trace behavior. Sharing
   counter ownership is not permission to remove observability or safety checks.

### 9.2 Resource plan and reservation

Extend the existing resource plan to describe named roots, profile-required
completion roots, scratch capacity and a fixed ordinary-argument suffix.
These are resource facts, not executable instructions.

- Calculate maximum simultaneously live roots following actual evaluation
  order. Nested arguments add to still-live outer arguments; taking only the
  maximum arity of any single call is incorrect (**D5.3.5**).
- Account for receiver/callee/key lifetimes, optional chains, defaults,
  destructuring, named arguments and exceptional exits. The planner's lifetime
  model and the walker's slot acquisition order must agree.
- Check arithmetic and representability before narrowing to the current
  `uint16_t` plan fields. An oversized/unproven plan keeps an existing safe
  rooted path or fails admission before user effects; never silently underreserve.
- Reserve one nested activation window on the existing side-root stack.
  Borrow bounded scratch/argument subranges; clear dead slots to avoid retaining
  unrelated objects. Preserve a safe dynamic span for unbounded adapters.
- Keep root and number regions distinct. A pointer into a completed activation's
  slots or number homes may not escape into a closure, module or suspended job.

### 9.3 Entry, call and return protocol

1. Validate/obtain the existing runtime context and plan; preserve outer marks.
2. Reserve/check the activation window before publishing its roots.
3. Populate named/profile roots and enter through the selected profile evaluator.
4. Evaluate operands into planned rooted homes before any MAY_GC operation.
5. Nested/native/cross-language callbacks reserve their own windows, without
   overwriting the caller's live operands or argument suffix.
6. Publish the result using existing ownership-qualified return adapters.
   Wide scalar payloads must not alias a popped scratch/number home. Keep a
   result rooted across any cleanup that can allocate.
7. Run cleanup, restore module/source state and restore watermarks in the
   established order. Every explicit error/completion takes the same cleanup
   obligations; do not introduce non-local language unwinding.

A return may use the existing no-safepoint interval between frame pop and
caller store, but only with the precise lifetime proof required by
**D5.3.3**. Any cleanup before that handoff remains part of the proof.
Native recovery checkpoints must remain consistent with **D5.3.6**.

**Exit:** bounded expression temporaries and ordinary call operands stop opening
per-node/per-call root windows in both clients. Root reservations scale with
activations plus documented dynamic adapters. Forced GC, recursion, reentrant
callbacks, errors and cross-language calls pass; peak live roots do not grow
with completed loop iterations. No conservative-root fallback exists.

## 10. Phase 5 — Eligible JS locals in the common frame storage

This is deliberately later and higher risk than binding-plan lookup removal.
It optimizes the JS client while exercising a genuinely shared storage service.
Reuse `GcEnvironmentStorage` and owned scalar-slot helpers; do not add a new
heap family or copy Lambda's capture semantics (**D6.2.3v2, D8.1.3v11**).

### Admission

Start conservatively with synchronous, non-escaping function-local bindings
whose identities cannot be observed through direct eval, captured with state,
mapped arguments, nested closures or suspension. Treat uncertainty as requiring
an environment cell. Do not add runtime stack-to-heap deoptimization/promotion
to broaden the first slice.

### Work

- Produce a binding storage decision using existing capture/effect facts.
  A scope still owns an environment if another binding or lexical `this`,
  `new.target`, private/class state or an escaping environment edge requires it.
- Keep TDZ/const and initialization-order checks for frame-local bindings.
- Retain environment cells for captured or dynamically visible bindings;
  do not let closures point at popped frame slots. Preserve arrow lexical
  bindings and per-iteration closure identity.
- Maintain parameter-default and body-scope separation, duplicate/sloppy
  parameter behavior, named-function-expression binding and declaration hoisting.
- Elide an environment allocation only after proving no remaining semantic
  state or outer link needs that record. Avoid replacing one environment with
  an equally costly per-local allocation.

**Exit:** an admitted plain function allocates no environment solely for its
uncaptured locals, and mixed functions allocate only required records.
Closures/eval/arguments/iteration tests and forced GC agree with baseline.
Environment allocation/bytes decrease on the admitted cohort without increased
root retention or stale wide scalar homes.

## 11. Phase 6 — Guarded primitive specialization

Reprofile first. Only implement this phase where dispatch/representation work
remains material after names, preparation and frame costs are removed.

- Reuse existing scalar classification, checked arithmetic and element-access
  helpers. A fast arm has an exact admission predicate, operation, result
  representation, effect classification and generic miss path.
- Keep boxed `Item` interpreter boundaries. Temporary native arithmetic inside
  a proven helper does not authorize a second unboxed interpreter lane.
- Share mechanics only where profile semantics coincide. Lambda numeric tower,
  saturation, division/modulo and absence differ from JS coercion, Number,
  BigInt, string addition and equality (**S4.4–S4.5, D1.3v3**).
- Preserve NaN, signed zero, infinities, out-of-band scalar ownership, null/
  undefined and invalid-operand behavior. A guard miss must not repeat operand
  evaluation, user conversion or property access.
- NO_GC is a mechanically verified transitive property, not a name given to a
  seemingly simple helper (**D5.3.2**). Do not remove roots across an unaudited call.
- Use immutable compile-predicted facts and guards; no feedback mutation or
  per-site cache installation (**D8.4.1v2**).

**Exit:** the measured residual cost decreases on Lambda compute cases and a
JS numeric/library cohort. Edge-case/forced-GC results remain identical. Drop
specializations that only add branches or code size without repeatable benefit.

## 12. Phase 7 — Separate JS-runtime tracks

### 12.1 Sparse-array traversal

Build on Phase 2 and existing `JsArraySparseKeyCursor`, own-element traversal,
prototype guards and generic array kernels in `js_runtime.cpp`.

- For reduction and other HasProperty-based algorithms, skip provably absent
  ranges only when ordinary receiver/prototype/descriptor facts make each
  skipped observation unobservable.
- Refresh/revalidate after callbacks, accessors, species construction or any
  user code that can mutate elements, lengths, prototypes or descriptors.
  Preserve the algorithm's original length snapshot and current-index behavior.
- `find`/`findIndex`/reverse variants must still call predicates for holes.
  `includes` observes holes as undefined; a sparse search shortcut must preserve
  that behavior and inherited values.
- Treat slice, splice, concat, reverse and sort as different semantic clients
  of a shared traversal mechanism, not interchangeable loops. Preserve
  observable Get/Has/Set/Delete order and partial mutations on failure.
- Keep generic proxy/accessor/exotic paths. No blind snapshot of sparse keys
  may hide insertions/deletions made by callbacks.

**Exit:** eligible hole-skipping algorithms scale with present entries plus
guard work, demonstrated across several array lengths/densities, while
hole-observing algorithms retain their required callback count. All sparse,
mutation, accessor/proxy and species regressions remain covered.

### 12.2 Regex preprocessing

Starting point: `js_regexp_compile.cpp::js_regexp_replace_all_owned` and its
property-alias callers; this is Lambda-owned code, not the vendored engine.

- Measure pattern scans/bytes and rewrite passes in Highlight.js.
- Reuse existing regex lexical knowledge or extract a token-aware normalization
  pass; avoid repeatedly scanning the full pattern for every possible alias.
- Consider a cheap no-rewrite admission check only if it respects escaping,
  character classes, flags and the supported regex grammar.
- Preserve diagnostics, named groups/backreferences, Unicode properties and
  legacy escape behavior. Do not hide the existing legacy-octal failure.
- Do not add an unbounded global regex/pattern cache to obtain a benchmark win.

**Exit:** rewrite passes/bytes decrease and Highlight.js improves without regex
semantic regressions, changed errors or vendor edits.

## 13. Validation matrix

Reuse existing cases first; add focused regressions for uncovered boundaries.
Every new Lambda `*.ls` fixture includes its expected `*.txt` result. The table
names representative tests, not an exhaustive replacement for baseline suites.

| Change | Required coverage | Existing starting points |
|---|---|---|
| Lambda literal decoding | Bare versus folded values, folding on/off, non-foldable loops, spelling/range errors, shared-type separation | `test_interp_gtest.cpp`; Lambda literal/numeric fixtures; Phase 1 probes |
| JS literal reuse | String bytes/escapes, BigInt immutability, forced GC, owner teardown/recreation, unused literals, mutable literal freshness | JS string/BigInt fixtures; `collection_gc_retention.js`; Phase 1 probes |
| Name linkage/pool | Equal spellings, distinct symbols/private names, parent precedence, segment overflow, context teardown/reuse | `test_name_pool_gtest.cpp`; `test/js/props/dynamic_string_no_name_id_exhaustion.js` |
| Index lanes | Dense/hole/sparse, numeric companions, prototype indices, accessors, proxies, typed arrays, large array-like indices | `test/js/sparse_*.js`; JS property suite |
| Bindings | Lambda snapshot captures/view bindings; JS TDZ, const, eval, with, live imports, arrows | `test_interp_gtest.cpp`; `eval_local_var_nested.js`; `jscu29_eval_*`; `interp_esm/` |
| Calls/plans | Nested arguments, spread/defaults/rest, optional calls, method receivers, constructors/super, rebinding | `arguments_rest_default.js`; `direct_method_arguments_forwarding.js`; interpreter GTests |
| Root windows | Collection during later operands, callbacks and returns; frame overflow; nested language entries | `regression_side_stack_frame_gc.js`; `array_callback_gc_roots.js` |
| Scalar lifetime | Returns/arguments/closures containing wide/out-of-band scalars across cleanup | `stack_api_scalar_homes.js`; `regression_ast_call_scalar_home.js` |
| JS local storage | Escaping closures, per-iteration cells, mapped arguments, parameter defaults, eval journals | `arguments_wide_formal_alias.js`; `jscu29_eval_bridge_journals.js`; interpreter GTests |
| Completion cleanup | Throws/errors through nested calls, catch/finally and iterator closing; no skipped cleanup | Existing JS exception/iterator cases and Lambda error/handler cases |
| Numeric guards | Boundary integers, infinities, NaN, signed zero, mixed domains, invalid operands | Lambda numeric suite; `regression_self_tag_float_samevaluezero.js` |
| Runtime-library tracks | Callback mutations/proxy traces, GC retention, regex semantics | `sparse_gc_survival.js`; `collection_gc_retention.js`; regex and Highlight.js fixtures |

Run forced-GC checks separately from performance tests using the existing
`LAMBDA_GC_FORCE_EVERY` and seeded `LAMBDA_GC_FORCE_SEED` /
`LAMBDA_GC_FORCE_ONE_IN` mechanisms. Confirm supported values in
`lambda-mem.cpp` when executing; do not invent a new stress control. Use
sanitizers for correctness as appropriate, never for the reported timing run.

### Suite commands and interpretation

After building the corresponding test runners in release configuration:

```sh
env LAMBDA_TIER=interp ./test/test_lambda_gtest.exe --gtest_brief=1
env JS_GTEST_MODE=mir JS_EXECUTION_BACKEND=ast \
  ./test/test_js_gtest.exe --gtest_brief=1
./test/test_interp_gtest.exe
./test/test_name_pool_gtest.exe
make test-lambda-baseline
```

The counterintuitive `JS_GTEST_MODE=mir` setting disables the harness's mixed
AST/MIR fixture-list overrides; `JS_EXECUTION_BACKEND=ast` chooses the actual
backend. Recheck `parse_js_gtest_options` and child environment handling before
use. Some standalone GTests deliberately select other tiers: these are
correctness coverage, not strict-AST timing evidence.

Run relevant Test262 baseline partitions whenever JS semantic/property/call
kernels change, preserving the exact baseline failure set. Never modify
`test_js_test262_gtest` to mask failures, crashes or timeouts. Recheck supported
MIR paths and cross-tier calls when changing shared helpers; native code and
auto mode are compatibility coverage only, not performance evidence for this
round. Run Radiant baseline when a shared runtime/property change reaches
document/DOM consumers.

## 14. Performance acceptance and reporting

### Frozen reporting cohorts

1. Lambda top ten from §2, with typed and untyped pairs kept separately visible.
2. JS interpreter/shared-cost cohort: `lib_fast_diff`,
   `collection_gc_retention`, `stack_api_scalar_homes`, `lib_handlebars`,
   `lib_joi`, `lib_moment`, `lib_marked`, plus representative small scripts.
3. JS sparse cohort: the six §2 sparse fixtures, reported separately.
4. Highlight.js/regex cohort: reported separately.
5. Full correctness population, including known failures and fallback cases.

Add focused common-mechanism probes only as supplementary diagnostics; do not
replace real workloads or reduce input sizes. Existing failed partial runs
such as `tco.js` never enter the successful-performance aggregate.

### Gates

- **Correctness:** no new failing identity, timeout, crash, output mismatch or
  AST fallback. Existing failures remain visible; if fixed, record the fix and
  establish a completed-work baseline before including its speed in aggregates.
- **Causality:** each optimization demonstrates its intended counter/profiler
  reduction, not just a lower wall-time sample. No claim based on empty/missing
  metrics, skipped work or a changed manifest.
- **Both languages:** shared work must show repeatable benefit in at least one
  relevant real workload in each language, with no material regression in its
  separate cohort. A neutral structural extraction may land as a measured
  prerequisite, but is not itself called a speedup.
- **Regression investigation:** a per-fixture median slowdown exceeding 5% and
  5 ms triggers a new interleaved capture and attribution; it is not silently
  averaged away. Persistent regressions require fixing, dropping the slice or
  explicit approval of a documented tradeoff. These thresholds are engineering
  triage choices, not formal semantic/performance rulings.
- **Memory/cold start:** report peak RSS, preparation time, environment bytes
  and root high-water marks. Every material increase needs attribution and an
  explicit tradeoff; unbounded growth across repeated execution is a failure.
- **Noise:** retain every measured sample/status and report median, range and
  dispersion. Rerun a noisy complete pair rather than selectively deleting
  slow samples. Extend the sample count when the apparent win is within noise.

Report per-fixture ratios, median complete-cohort elapsed times, and language-
specific aggregates. Do not confuse sum-of-fixture medians, median-of-cohort
times and parallel GTest wall time. Use native samples only for attribution;
profile runs and diagnostic-counter runs do not enter timing medians.

The one-warm-up/five-sample discipline follows **D8.6.4v2**, but its compiler-
time and LOC ratchets are a different gate. This plan does not claim those
ratchets using interpreter runtime or code movement. No numerical speedup is
promised before implementation and controlled measurement.

## 15. Delivery, rollback and completion

Keep each semantic-preserving optimization reviewable and reversible in its
own change. Separate any required correctness repair from its tuning consumer;
establish equivalent before/after correctness before attributing performance.
Do not retain a second production implementation as a permanent comparison
backend; preserve baseline binaries instead.

| Change group | Required artifact | Status |
|---|---|---|
| Baseline/tooling | Frozen manifest, execution evidence, raw samples and failure ledger | Not started |
| Phase 1: Lambda literal decoding | Per-primary prepared payloads; AST literal smoke tests pass; decode-counter and folding-on/off matrix remain | Implemented; measurement gate pending |
| Phase 1: JS literal reuse | Realm-owned immutable cache; repeated-use, GC and heap-replacement tests pass; materialization/parse deltas remain | Implemented; measurement gate pending |
| Linked names/index route | Activation-local Lambda member links; existing JS property-lane audit; identity/order tests and name-work deltas | Partial; sparse/index counter gate pending |
| Static interpreter facts | Lambda capture/read and call-shape facts; JS lexical slots, callable parameter facts and shared call shapes | Partial; lazy diagnostics, local classification and measurement remain |
| Common activation/windows | JS parameter and Lambda ordinary-call window reuse; lifetime protocol, resource-bound and forced-GC results remain | Partial; common extraction pending |
| JS local slots | Eligibility proof/tests, environment allocation deltas | Not started |
| Residual primitive guards | Profile evidence, exact guards, semantic edge-case tests | Conditional |
| Sparse/regex runtime tracks | Separate algorithm proofs and performance tables | Separate follow-ups |
| Closeout | Full correctness delta, per-language performance/memory report, final source map | Not started |

Before marking complete:

- Both clients use the extracted shared mechanism; delete superseded duplicate
  bookkeeping rather than leaving unused abstractions or a third variant.
- Remaining exclusions and intentionally dynamic paths are explicit.
- The full correctness/fallback ledger and release before/after artifacts exist.
- Runtime gains are not credited to tier promotion, altered test coverage,
  reduced diagnostics semantics or weakened rooting.
- Update this implementation record with actual commits, results and abandoned
  experiments. Update formal/working rulings only if a separately approved
  ruling changed; otherwise this is an implementation-only closeout.
- Keep all generated captures under `./temp/`. Any build integration change
  goes through `build_lambda_config.json`, never hand-edited generated Lua.
  Use project C++17/lib conventions and existing logging facilities.

## 16. Source map for implementation

Symbols were checked against the baseline tree; relocate by symbol before
editing rather than relying on line numbers from the profiling report.

| Area | Files and starting symbols |
|---|---|
| Lambda literal preparation | [build_ast.cpp](../../lambda/runtime/build_ast.cpp): `build_literal_type_from_span`; [ast.hpp](../../lambda/runtime/ast.hpp): `parse_int_literal_span`, `parse_bool_literal_span`; `interp.cpp`: `eval_literal`, `interp_const_folded_value`, `interp_const_fold_script` |
| JS literal preparation | [js_c_ast_helpers.cpp](../../lambda/js/js_c_ast_helpers.cpp): `build_js_literal_from_source`; `js_interp.cpp`: `js_interp_eval`; [js_runtime_value.cpp](../../lambda/js/js_runtime_value.cpp): `js_make_string_len`; `bigint_from_string` |
| Lambda walker | [interp.cpp](../../lambda/runtime/interp.cpp): `eval_expr`, `eval_literal`, `interp_read_identifier`, `interp_capture_index`, `interp_static_member_name_item`, `eval_call`, `interp_bind_declared_value`, `InterpFrameGuard`, `Scratch` |
| JS walker | [js_interp.cpp](../../lambda/js/js_interp.cpp): `js_interp_read_binding`, `js_interp_scope_slot_count`, `js_interp_env_create`, `js_interp_eval_call_chain`, `js_interp_call_function` |
| Planning/owners | [interp_plan.cpp](../../lambda/runtime/interp_plan.cpp): `plan_link_capture_identifier`, `plan_link_call_shape`; [ast-core.hpp](../../lambda/runtime/ast-core.hpp): `FnFramePlan`, `FnAnalysis`, `NameScope`, `NameEntry`, `AstCallNode`; [compiler_pass.cpp](../../lambda/runtime/compiler_pass.cpp) |
| Activation | [runtime-state.h](../../lambda/runtime/runtime-state.h): `RuntimeExecutionScope`; [runtime-state.cpp](../../lambda/runtime/runtime-state.cpp) |
| Precise roots | [lambda-root-frame.hpp](../../lambda/runtime/lambda-root-frame.hpp), [side_stack.c](../../lambda/runtime/side_stack.c) |
| Durable cells | [gc_environment.h](../../lambda/runtime/gc_environment.h), [js_interp_env.h](../../lambda/js/js_interp_env.h) |
| Names | [name_pool.cpp](../../lambda/core/name_pool.cpp): `name_pool_lookup_local_strview`, `name_pool_create_strview`; [name_pool.hpp](../../lambda/core/name_pool.hpp) |
| JS property/array kernels | [js_runtime.cpp](../../lambda/js/js_runtime.cpp): `js_has_property_status`, `js_array_reduce_get_element_status`, sparse cursors/index-key helpers; [js_globals.cpp](../../lambda/js/js_globals.cpp): `js_has_own_property` |
| Regex preprocessing | [js_regexp_compile.cpp](../../lambda/js/js_regexp_compile.cpp): `js_regexp_replace_all_owned` and alias normalization callers |
| Timing/stress | [runner.cpp](../../lambda/runtime/runner.cpp), [main.cpp](../../lambda/main.cpp), [lambda-mem.cpp](../../lambda/runtime/lambda-mem.cpp) |
| Measurement tooling | [lambda_process.py](../../test/interp/lambda_process.py), [run_bench.py](../../test/interp/run_bench.py), [tier_sweep.py](../../test/interp/tier_sweep.py) |
