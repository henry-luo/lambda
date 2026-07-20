# LambdaJS-specific MIR tuning plan

**Status:** COMPLETE

**Revision date:** 2026-07-20

**Scope:** LambdaJS semantic analysis and MIR-Direct lowering only. C2MIR and
the common generated-frame implementation are out of scope.

**Stack API authority:**
[Lambda_Design_Stack_API.md](../Lambda_Design_Stack_API.md)

## 1. Decision and boundary

The common Stack API is implemented. LambdaJS tuning must build on it rather
than recreate any part of it.

The following work has therefore been removed from this tuning roadmap:

- separate JS root/number-frame ownership or frame arrays;
- JS-specific root coloring, root publication, prologue, epilogue or overflow
  handling;
- introducing public wrappers and internal generated bodies;
- designing or exposing the hidden caller-owned scalar-home ABI;
- callee-frame donation or JS-specific scalar return rehoming;
- a second import/direct-call metadata path;
- JS-local safepoint recording or raw MIR call emission;
- write-through/eager root modes and the retired GC event telemetry;
- generic zero-root frame elimination and common frame verification.

Those mechanisms now belong to `FnVariantAnalysis`, `JitCallMetadata`,
`MirValue`, `MirFunctionPlan`, `MirEmitter` and the common finalizer/verifier.
They are invariants consumed by JS lowering, not JS tuning projects.

This document contains only work whose policy is specific to JavaScript:

1. helper exception/ownership classification for JS runtime operations;
2. exception-state propagation and poll elimination;
3. direct-eval eligibility and eval-local state;
4. transient JS argument-stack ownership;
5. `this`, `new.target`, `arguments`, `with` and environment call state;
6. JS representation demand, native-local boxing and return inference;
7. unresolved-name and TDZ analysis;
8. JS literal, destructuring and iterator specialization;
9. JS function/module initialization.

## 2. Current post-Stack-API baseline

The measurements previously recorded in this file were taken at revision
`79c4070826cccf433cfd48c4b8320a61ce762318`, before the common Stack API was
implemented. Their frame, root-store, scalar-return and total MIR counts are
retired and must not be used as current acceptance numbers.

The implementation closes the JS-specific opportunities as follows:

| Opportunity | Delivered implementation |
|---|---|
| Exception polls | Normalized exception effects feed a full CFG `CLEAN`/`UNKNOWN` fixed point; proven-redundant poll calls and branches are removed before frame finalization |
| Eval-local state | Function collection's direct-eval fact gates all eval-local frame creation and cleanup; the no-eval corpus emits zero eval operations |
| Argument watermarks | Marks are created only by paths that emit `js_args_push`; exceptional argument evaluation restores the oldest active nested mark |
| JS state helpers | Audited `this`, `new.target`, `with`, function-metadata and module-variable rows now carry independent GC, re-entry, exception and argument-store effects |
| Native-local boxing | Eval publication is tested before native boxing; canonical inline constants avoid runtime float boxing when representable |
| Return precision | Physical parameter facts and boxed return scalar classes populate `FnVariantAnalysis` for public, boxed-body and native variants |
| TDZ and names | Structured statement dominance removes proven TDZ checks; switch, environment, eval, closure and resume cases remain conservative; `undefined` is constant only after static shadow checks |
| Literal/iterator protocols | Exact-capacity fresh arrays and guarded built-in iterator paths are retained and structurally asserted; static data-key object literals use prebuilt shapes and owned slot stores |
| Function setup | `js_finalize_function` atomically installs name, source, arity and flags before publication for generated functions and closures |

The Phase 0 profile below replaces those stale totals and separates public,
boxed-body, native-body and resume costs.

### 2.1 Phase 0 baseline (2026-07-20)

The durable probes are `test/js/mir_tune_*.js`; `utils/profile_js_mir.sh`
generates dumps and `utils/analyze_js_mir.py` reports JS mechanisms separately
from common frame metrics.

| Entry group | Functions | MIR instructions | Locals | Calls | Exception polls | Eval ops | Argument ops | TDZ checks | Conversions |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Public wrappers / `js_main` | 43 | 3,914 | 1,766 | 1,086 | 137 | 0 | 72 | 0 | 156 |
| Boxed internal bodies | 39 | 2,969 | 1,283 | 566 | 72 | 80 | 25 | 8 | 151 |
| Native internal bodies | 4 | 436 | 233 | 62 | 3 | 0 | 0 | 2 | 47 |
| Resume bodies | 2 | 327 | 125 | 78 | 24 | 5 | 0 | 7 | 9 |
| **Total** | **88** | **7,646** | **3,407** | **1,792** | **236** | **85** | **97** | **17** | **363** |

Common Stack API regression constraints for the same corpus are 248 root
slots, 842 root stores, 140 scalar homes and 1,164 safepoints. These are not JS
tuning targets, but later phases must not increase them without a demonstrated
semantic requirement.

### 2.2 Final structural profile (2026-07-20)

The fixed 88-function corpus is unchanged. Semantic assertions live in the
separate `test/js/mir_tune_semantics.js` fixture so additions to correctness
coverage cannot silently change the structural denominator.

| Entry group | Functions | Baseline instructions | Final instructions | Reduction | Baseline calls | Final calls |
|---|---:|---:|---:|---:|---:|---:|
| Public wrappers / `js_main` | 43 | 3,914 | 2,665 | 31.9% | 1,086 | 653 |
| Boxed internal bodies | 39 | 2,969 | 2,185 | 26.4% | 566 | 405 |
| Native internal bodies | 4 | 436 | 305 | 30.0% | 62 | 39 |
| Resume bodies | 2 | 327 | 264 | 19.3% | 78 | 67 |
| **Total** | **88** | **7,646** | **5,419** | **29.1%** | **1,792** | **1,164** |

Average MIR instructions per generated frame fell from 86.9 to 61.6. Every
entry group improved; no result depends on moving work into an unmeasured
generated helper.

| JS mechanism | Baseline | Final | Reduction |
|---|---:|---:|---:|
| Locals | 3,407 | 2,592 | 23.9% |
| Exception polls | 236 | 190 | 19.5% |
| Eval operations | 85 | 0 | 100.0% |
| Argument-stack operations | 97 | 42 | 56.7% |
| TDZ checks | 17 | 14 | 17.6% |
| Representation conversions | 363 | 186 | 48.8% |

The common Stack API constraints also improved: roots 248 to 219, root stores
842 to 487, scalar homes 140 to 98, and safepoints 1,164 to 647. These remain
regression evidence, not JS-owned planning state.

## 3. How JS tuning must use the Stack API

### 3.1 Function and variant analysis

JavaScript facts are computed during function collection and stored in the
existing analysis boundary:

- `FnVariantAnalysis.entry` for public, body, native and resume entry facts;
- `FnVariantAnalysis.effects` for conservative function effects;
- `FnVariantAnalysis.result` for normal/error representation and scalar-home
  demand;
- `FnVariantAnalysis.params` for any specialized body operands;
- the JS-owned function-analysis extension for semantics such as lexical
  `this`, direct eval, `arguments`, `new.target`, `with`, suspension and
  generator state.

JS lowering must not infer a second mutable frame plan. A JS fact that changes
the physical call ABI must first become immutable variant analysis, then be
materialized through the common direct-call metadata.

### 3.2 Values and representation demand

JS expression and binding lowering should carry `MirValue` at representation
boundaries and call `em_require_rep()` only when a consumer requires a boxed
`Item`, native integer/float, or managed pointer representation.

Examples of real boxed consumers are:

- capture or environment storage;
- module/global publication;
- a boxed runtime-helper argument;
- a public wrapper result;
- a dynamic property or call operation whose metadata requires `ITEM`.

A local declaration, copy, comparison or native direct argument is not by
itself a reason to box. The Stack API owns any scalar home created when a
conversion actually produces an activation-backed wide scalar.

### 3.3 Calls and effects

Every JS runtime or generated call must continue through the common normalized
call path. JS tuning supplies facts; it does not emit around the contract.

The relevant metadata dimensions are independent:

- GC effect;
- re-entry effect;
- exception effect;
- number-stack effect;
- ABI representation and semantic value class;
- per-argument borrow/capture/write/persistent-store effect;
- normal and error return lanes.

Unknown JS helpers remain conservative. A fast path that has different effects
from its fallback needs a distinct helper or generated direct edge, not a
conditional lie in one metadata row.

### 3.4 Frame and ownership invariants

JS tuning may observe common frame metrics but must not mutate or mirror their
state. In particular:

- public/indirect calls target checked wrappers;
- proven generated direct calls target internal bodies;
- only internal bodies may receive the caller-owned scalar home;
- activation-backed arguments are rehomed according to argument effects;
- normal and exceptional results obey the common ownership contract;
- root and number watermarks are finalized by `MirEmitter`.

## 4. Priority 1: classify JS helper contracts

### 4.1 Problem

Conservative JS helper rows make calls appear to collect, re-enter, throw,
allocate number-stack storage or retain arguments even when an audited body
does none of those things. The Stack API then correctly emits the conservative
rooting and ownership protocol. The fix is better JS evidence, not bypassing
that protocol.

Initial audit candidates include:

- `js_get_new_target`;
- `js_set_this` and `js_set_direct_new_target`;
- `js_with_save_depth` and `js_with_restore_depth`;
- `js_set_function_source` and `js_mark_strict_func`;
- `js_set_module_var` and `js_get_module_var`.

This is an audit list, not a bulk classification. Each implementation must be
checked for allocation, error creation, callbacks, proxy/accessor dispatch,
persistent storage and re-entry. For example, lazy global-object creation and
private-name normalization must remain on conservative paths when they can
allocate.

### 4.2 Stack-API integration

For each proven helper:

1. populate the existing normalized GC, re-entry, exception and number-stack
   effects;
2. classify every ABI argument and result by representation and value class;
3. mark borrowing only when the callee cannot retain or publish the value;
4. mark persistent-store arguments so activation-backed scalars are rehomed;
5. keep unaudited dimensions conservative;
6. extend `utils/check_gc_effects.py` or a JS-specific companion audit so an
   asserted leaf contract is checked transitively.

If the compact registry row cannot express a normalized field, extend its
normalization input once. Do not add a JS-only import cache or call emitter.

### 4.3 Mixed fast/slow helpers

`js_array_define_dense_element_direct` illustrates the required split. A store
into a newly allocated exact-capacity array can be a non-reentrant leaf; the
general fallback can allocate and execute JS semantics. Lowering should select
a separately audited fresh-array helper only while ownership/capacity proofs
hold. All other cases retain the general helper and its conservative metadata.

## 5. Priority 2: propagate JS exception state

### 5.1 Problem

`jm_emit_pending_exception_check()` remains the semantic request point: it
creates a status local, calls `js_check_exception`, and branches to the active
completion target. The finalizer removes requests proven redundant when no
operation changed the pending-exception state.

GC and exception effects must remain independent. A no-GC helper may throw;
an allocating helper may preserve the exception state.

### 5.2 Analysis

Track one JS-specific state on normal CFG edges:

```text
CLEAN    a poll proved no pending exception and all later operations preserve it
UNKNOWN  an operation may have set or cleared the pending exception
```

Transfer rules:

1. read each call's normalized Stack API exception effect;
2. `MAY_SET` changes normal state to `UNKNOWN`;
3. `PRESERVES` leaves the state unchanged;
4. `CLEARS` establishes `CLEAN` when that is the helper contract;
5. normal fallthrough after a poll is `CLEAN`;
6. a join is `CLEAN` only when every normal predecessor is `CLEAN`;
7. exception edges, unknown calls, backedges without a fixed point, re-entry
   and suspension remain conservative.

Start with adjacent duplicates, then basic-block propagation, then a CFG fixed
point. Reusing one status local reduces virtual locals, but eliminating the
call and branch is the material improvement.

### 5.3 Later fast ABI

No value/status ABI was added. The CFG fixed point removes redundant status
reads without adding a second return convention, so this optional mechanism is
not needed by the measured corpus.

## 6. Priority 3: emit eval-local state only when eligible

Ordinary and generator bodies now allocate `eval_local_frame_reg` only when
function collection reports direct eval. No-eval variants omit initialization,
writeback and completion cleanup entirely.

The immutable JS eligibility fact covers:

- syntactic direct eval in the function;
- local bindings that must be exposed to direct eval;
- generator/async resume paths;
- any descendant case that shares the current environment under LambdaJS's
  implemented eval model.

The physical consequence is represented through the existing variant entry fact
(`has_dynamic_scope`) or a JS-owned extension. For an ineligible variant:

- leave `eval_local_frame_reg` absent;
- emit no initialization, push, note, writeback or pop MIR;
- make all eval cleanup helpers compile-time no-ops.

Direct-eval tests must retain strict/sloppy eval behavior, lexical bindings,
closures, try/finally and non-local completion.

## 7. Priority 4: narrow JS argument and dynamic-call state

### 7.1 Transient argument stack

The runtime `js_args_*` stack is needed by dynamic fallback, apply/spread and
constructor paths that physically push arguments. A generated direct body or
native direct call passes operands in its verified MIR ABI and must not open an
unused runtime argument watermark.

Watermark ownership resides in the lowering operation that actually emits
`js_args_push`. The call plan distinguishes:

- generated direct body;
- native fixed-arity direct call;
- generic function call;
- apply/spread call;
- dynamic constructor call.

Nested calls own independent marks only for the levels that push. The
common Stack API continues to own root/scalar argument lifetime; the JS
argument watermark is solely a JavaScript runtime calling-convention detail.

### 7.2 `this`, `new.target`, `arguments` and `with`

The public-wrapper/internal-body split already exists. Use it to reduce JS
dynamic-state traffic rather than proposing another entry layer.

Function collection computes whether each internal body can observe:

- lexical or dynamic `this`;
- `new.target`;
- the `arguments` object;
- a `with` environment;
- direct eval capable of observing any of the above;
- closure or resume environment state.

Collection records immutable observability facts. Direct lowering uses them to
omit save/set/restore traffic for `this`, `new.target`, and `with` when the
callee cannot observe that state. No new hidden operand was required; checked
public wrappers and all indirect edges retain the existing runtime ABI.

Functions that manipulate `with` restore their own entry depth in their unified
JS epilogue. Callers omit defensive save/restore traffic around callees proven
unable to manipulate it.

## 8. Priority 5: preserve native representations until real demand

### 8.1 Native local declarations

Native `let`/`const` lowering can box a value before
`jm_declare_evalscript_global_lexical_if_needed()` discovers that the
declaration is not a direct-eval global lexical. Move the eligibility decision
before conversion.

Keep integer/float locals in their native `MirValue` representation unless a
later consumer requires an Item. The same rule applies to copies, loop-carried
values and native direct-call arguments. Capture, environment/global storage
and boxed runtime calls still use `em_require_rep()`.

This is a lowering fix, not backend dead-code cleanup: out-of-line float
boxing can call an allocator, so an unused boxing diamond remains effectful.

### 8.2 Constant Items

Emit canonical constant `Item` encodings for representable numbers,
`undefined`, null and Booleans. Use the runtime boxing path only for values
whose payload cannot be represented inline. Float encodings must follow
`Lambda_Type_Double_Boxing.md`; JavaScript lowering must not invent another
discriminator.

### 8.3 Return and state-machine inference

Improve JS return analysis so `FnVariantAnalysis.result` records the narrowest
legal normal/error representation for:

- constant/inline-only Item returns;
- object/function/container-only returns;
- int64, float and datetime returns;
- native scalar bodies;
- generator and async resume unions;
- thrown/rejected completion lanes.

The common Stack API then selects no scalar home, a typed scalar home or the
dynamic lane and performs caller-home adoption. JS code must not emit a custom
return classifier or manually restore/donate number-stack storage.

Public wrappers remain boxed and persistent. Internal specialization is valid
only when every direct caller uses the matching verified variant.

## 9. Priority 6: unresolved names and TDZ

### 9.1 Constant `undefined`

Emit `ITEM_JS_UNDEFINED` directly only when lexical analysis proves:

- no parameter/local/capture shadows the name;
- no active `with` environment can resolve it;
- no direct eval can alter or observe resolution;
- the applicable global semantics do not require a dynamic lookup.

Otherwise retain `js_get_with_binding_or_fallback` and its normalized call
effects.

### 9.2 Definite initialization

The structured lowering uses the per-binding lattice:

```text
TDZ
INITIALIZED
MAYBE_TDZ
```

Initializers and assignments transfer to `INITIALIZED`. A declaration in the
same statement block dominates every later sibling statement, which is the
additional proof needed after block TDZ setup in structured lowering. Switch
clauses, closure/environment storage, direct eval, `with`, and unknown resume
state widen conservatively instead of claiming cross-edge dominance.

Emit `js_check_tdz` only for `MAYBE_TDZ`. A successful check refines normal
fallthrough until an operation invalidates the proof. The goal is fewer JS
checks and conversions, not merely fewer textual holder-register names.

## 10. Priority 7: guarded literal, destructuring and iterator paths

General JavaScript operations must preserve getters, proxies, computed keys,
prototype mutation, custom iterators, `IteratorClose` and abrupt completion.
Safe specialized paths are nevertheless available:

1. use the audited fresh-array store after exact-capacity allocation;
2. prebuild shapes for object literals containing static data keys, then fill
   owned slots in source evaluation order;
3. guard dense-array destructuring and `for..of` on the built-in iterator plus
   the relevant prototype/epoch;
4. preserve the existing iterator protocol and completion cleanup as fallback;
5. describe fast/fallback effects separately through normalized call metadata;
6. merge exception status with a helper result only through a verified
   normal/error return contract.

These optimizations follow metadata, eval, argument and exception cleanup so
the generic fallback benefits from the earlier work too.

## 11. Priority 8: compact JS function/module initialization

Top-level function creation combines JavaScript-observable steps:

- function allocation;
- name and source assignment;
- strict/eval/constructor flags;
- module-variable publication;
- global declaration/property definition.

The audited `js_finalize_function` helper carries arity, flags, name and source
as one allocation-free, non-reentrant initialization transaction immediately
after allocation. `Function.prototype.toString`, function identity, global
declaration-instantiation order and redeclaration behavior remain unchanged.

Generated functions and closures are published only after all internal fields
are valid. Stored name/source arguments use the Stack API's persistent-store
ownership contract. Batch global definition was not added because declaration
ordering was not a measured bottleneck.

## 12. Implementation plan

### Phase 0 — establish a post-Stack-API JS baseline — COMPLETE

1. Move a reduced probe set and analyzer into a stable tool/test location.
2. Count public wrappers and implementation bodies separately.
3. Record per-function MIR instructions, locals, calls, exception polls,
   eval-frame operations, `js_args_*` operations, TDZ checks and representation
   conversions.
4. Record common frame metrics only as regression constraints, not JS tuning
   targets.
5. Add structural assertions instead of brittle full-text MIR goldens.

### Phase 1 — low-risk JS lowering cleanup — COMPLETE

1. Audit selected JS helper metadata through the normalized Stack API.
2. Remove adjacent duplicate exception polls.
3. Gate eval-local frame creation and cleanup on immutable eligibility.
4. Test eval/global eligibility before boxing native declarations.
5. Move argument watermarks into paths that actually push arguments.
6. Emit canonical constant Items where representation is proven.

Acceptance:

- no eval-frame local or call in a no-eval implementation body;
- no argument mark around a generated/native direct call that emits no push;
- no unused native-local boxing diamond;
- no adjacent duplicate exception poll;
- no JS helper receives a stronger effect than its audited implementation;
- representative common frame counts do not regress.

### Phase 2 — JS dataflow — COMPLETE

1. Populate JS exception effects and implement basic-block/CFG clean-state
   propagation.
2. Improve normal/error return representation inference, including
   generator/async unions.
3. Implement definite-initialization analysis for TDZ elimination.
4. Feed specialized parameter/return representations into
   `FnVariantAnalysis` and common direct calls.

### Phase 3 — JS semantic body ABI — COMPLETE

1. Record `this`, `new.target`, `arguments`, environment and `with`
   observability facts on collected functions.
2. Omit direct-call state traffic when a body cannot observe it; no additional
   hidden operand is needed.
3. Keep `with` restoration owned by lowering that manipulates it.
4. Verify every indirect edge still targets a public wrapper.

### Phase 4 — guarded high-level specialization — COMPLETE

1. Fresh-array and static-object-literal construction.
2. Dense built-in iterator/destructuring fast paths.
3. Fused function allocation/name/source/flag initialization.
4. Value/status returns were evaluated and rejected as unnecessary after the
   CFG exception fixed point removed redundant polls without a parallel ABI.

## 13. Verification gates

### 13.1 Structural MIR

- compare the new post-Stack-API baseline after every phase;
- report implementation bodies and public wrappers separately;
- compare calls, polls, conversions and locals as well as instructions;
- inspect representative MIR and hot fallthrough, not only aggregates;
- reject changes that merely move identical work into an unmeasured helper;
- ensure common root/scalar-home/frame metrics do not regress.

### 13.2 JavaScript correctness and memory safety

- `make build-test`;
- focused JavaScript transpiler and Node preliminary tests;
- direct/indirect and strict/sloppy eval fixtures;
- `this`, `new.target`, `arguments`, `with`, closures and re-entry;
- nested calls, spread/apply/construct and abrupt completion;
- try/catch/finally, generators, async and rejection paths;
- TDZ, shadowed `undefined`, classes and cyclic lexical initialization;
- proxy/getter/property and custom-iterator/`IteratorClose` fixtures;
- `make test-gc-rooting` in all supported precise/forced-GC modes;
- `make test262-baseline` with zero failures and zero retry-only results;
- Node baseline with zero regressions.

### 13.3 Release performance

Use `make release`. Measure separately:

- compile/JIT time and implementation-body MIR for large modules;
- generated code size where available;
- direct-call, numeric and exception-heavy microbenchmarks;
- property-heavy, iterator-heavy and module-initialization workloads;
- full Test262 wall time with prep/batch/retry breakdown.

Debug MIR is structural evidence only. Runtime conclusions require release
measurements.

### 13.4 Final acceptance results (2026-07-20)

All correctness gates were run against the final implementation:

| Gate | Final result |
|---|---|
| Release build | `make release` passed |
| Test build | `make build-test` passed |
| LambdaJS GTest | 385 / 385 passed |
| GC rooting | all precise, forced-GC, interpreter and MIR-Direct modes passed; 29 `NO_GC` imports and 12,145 migrated native functions audited with no hazards |
| Test262 baseline | 40,261 / 40,261 fully supported tests passed; 0 failures, 0 retry-only results, 2,628 expected skips; 298.3 s wall time |
| Node baseline | 3,528 / 3,528 executed tests passed; 0 failures, timeouts, crashes or regressions; 713.3 s wall time |

The Node gate was repeated in full after an isolated detached-child process
race in the first run. Ten independent focused reruns and the complete second
run passed, so no exception was added to the baseline.

Release runtime remained flat on the fixed microbenchmark set:

| Workload | Baseline median | Final median | Change |
|---|---:|---:|---:|
| Sieve | 51.254 ms | 50.574 ms | -1.3% |
| DeltaBlue | 751.200 ms | 761.835 ms | +1.4% |
| Richards | 1,183.904 ms | 1,199.068 ms | +1.3% |
| NQueens | 16.842 ms | 16.749 ms | -0.6% |

Release compile/JIT medians for large modules were:

| Module | Baseline median | Final median | Change |
|---|---:|---:|---:|
| Lodash | 1,900.063 ms | 1,991.779 ms | +4.8% |
| jQuery | 398.003 ms | 477.872 ms | +20.1% |

The exception fixed point is limited to poll-bearing functions and preindexes
call sites, poll-result uses and compact CFG edges. This removed the earlier
multi-fold analysis regression; the remaining jQuery increase is 79.9 ms and
is the measured cost of the JS exception dataflow. The release executable grew
from 17,726,440 to 17,842,312 bytes (+0.65%).

The structural target is met for LambdaJS: total MIR instructions fell 29.1%,
and average MIR instructions per generated frame fell from 86.9 to 61.6. The
common Stack API frame metrics also improved, as recorded in section 2.2.

**Open issues:** none.

## 14. Correctness constraints

JS tuning must not:

- bypass `em_call_import()`/`em_call_direct()` or emit an untracked raw call;
- mirror common frame, root, scalar-home or safepoint state in a JS context;
- expose an internal body ABI through a function object or indirect pointer;
- treat `MIR_T_I64` alone as proof of GC identity or Item representation;
- infer exception preservation from `NO_GC`;
- mark a mixed helper safe because only its common branch is safe;
- omit activation-to-persistent rehoming for captures, modules or other stores;
- remove eval, TDZ, proxy, getter, iterator or abrupt-completion semantics
  without the stated proof/guard;
- add another float encoding or scalar-return ownership mechanism;
- use debug timings as performance evidence;
- modify C2MIR as part of this plan.

The target is the smallest JavaScript-specific semantic protocol layered on
the common Stack API, not the smallest MIR at the expense of JS semantics.

## 15. Source map

| JS-specific area | Current source |
|---|---|
| Function/return/direct-eval analysis | `lambda/js/js_mir_function_collection_class_inference.cpp` |
| Exception completion and polling | `lambda/js/js_mir_completion.cpp` |
| General/direct calls, eval binding and name resolution | `lambda/js/js_mir_expression_lowering.cpp` |
| Generator/async and public/body lowering | `lambda/js/js_mir_function_class_lowering.cpp` |
| Native declarations and TDZ/control-flow lowering | `lambda/js/js_mir_statement_lowering.cpp` |
| JS call/boxing adapters to the Stack API | `lambda/js/js_mir_calls_boxing_types.cpp` |
| Module/function initialization | `lambda/js/js_mir_module_batch_lowering.cpp` |
| JS helper registry rows | `lambda/sys_func_registry.c` |
| Function metadata transaction | `lambda/js/js_runtime_function.cpp` |
| Static object-shape runtime | `lambda/js/js_runtime.cpp` |
| JS runtime exception state | `lambda/js/js_runtime_state.cpp`, `lambda/js/js_runtime_state.hpp` |
| Argument stack runtime | `lambda/js/js_runtime_function.cpp` |
| `with` and eval runtime | `lambda/js/js_globals.cpp` |

Common frame/call/representation implementation is intentionally omitted from
this source map. Its API and ownership remain defined by
`Lambda_Design_Stack_API.md`.
