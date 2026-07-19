# LambdaJS MIR emission tuning proposal

**Status:** post-rooting implementation analysis and proposed work

**Profile date:** 2026-07-19

**Profiled revision:** `79c4070826cccf433cfd48c4b8320a61ce762318`

**Scope:** LambdaJS MIR-Direct emission. This document does not propose changes
to C2MIR.

**Companion reports:**
[Lambda_Design_Stack_Rooting.md](../Lambda_Design_Stack_Rooting.md),
[Lambda_Stack_JS_MIR.md](../Lambda_Stack_JS_MIR.md), and
[Lambda_Stack_MIR_Locals.md](../Lambda_Stack_MIR_Locals.md).

## 1. Conclusion

The current safepoint-current rooting design has removed the historical broad
root-copy explosion. It is not the main reason that current JS MIR remains
large. The remaining expansion is primarily caused by generic JS correctness
machinery being emitted when a function or operation does not need it, plus
incomplete effect and representation metadata for runtime helpers.

The highest-value work is:

1. complete `NO_GC` and semantic value metadata for audited leaf helpers;
2. add exception effects and coalesce exception polls;
3. omit eval-local frame machinery from functions without direct `eval`;
4. stop wrapping direct/native calls in unused argument-stack save/restore
   pairs;
5. stop boxing native numeric locals when the boxed value has no consumer;
6. specialize dynamic return cleanup when the returned representation is
   statically known;
7. eliminate provably unnecessary TDZ/capture checks;
8. introduce guarded fast ABIs for direct calls and fresh literals.

The measured corpus contains 242 generated functions and 98,482 executable
pre-backend MIR instructions. Its most important structural findings are:

| Finding | Measured evidence | Interpretation |
|---|---:|---|
| Exception polling | 6,412 `js_check_exception` calls; 25.9% of all MIR calls | Largest repeated hot-path mechanism |
| Poll call plus branch | 12,824 instructions; 13.0% of the corpus | Upper structural footprint, not all removable |
| Proven duplicate polls | 627 back-to-back polls | At least 1,254 instructions and 627 locals are removable without effect inference |
| Conservative helper effects | 14,786 calls classified `MAY_GC`; 11,302 root stores | Missing metadata creates false safepoints and extra publication |
| Auditable leaf helpers | 1,139 calls to selected state/metadata helpers currently left conservative | Immediate metadata-audit candidates |
| Eval cleanup without eval | 588 pop calls and 1,992 eval-frame instructions in sources containing no `eval(...)` | Entire measured eval scaffold is unnecessary |
| Argument-stack watermarks | 828 save/restore pairs | Every `CallExpression` is wrapped, including direct/native calls |
| Pairs with no emitted push | 170 pairs, or 340 calls | Proven lower bound for removable watermark calls |
| Dynamic scalar return | 5,955 instructions and 2,475 locals in 180 functions | Correct mechanism, applied more broadly than necessary |
| Float boxing helpers | 568 `lambda_mir_double_bits` plus 568 cold `js_profiled_push_d` call sites | Includes unused boxing of native locals |
| TDZ checks | 452 `js_check_tdz` calls | Includes reads/assignments proven initialized by local control flow |
| Checked root binding | 242 ensure call sites and 242 cold overflow call sites | One copy is emitted in every generated function, including zero-root native bodies |

These categories overlap. Their percentages must not be added as if they were
independent savings.

The recommended order is deliberately conservative: fix metadata and
function-level eligibility first, then add exception-state analysis, then
consider ABI redesign. This preserves the precise rooting invariant while
removing unnecessary work around it.

## 2. What was measured

### 2.1 Measurement boundary

The counts in this report are from the textual MIR emitted by a debug build
before backend register allocation and machine-code optimization. An
“instruction” excludes declarations, prototypes, imports, comments, labels,
and `endfunc`.

This boundary is useful for finding emitter duplication, excessive virtual
locals, helper calls, safepoints, and root stores. It is not a machine-code
instruction count:

- MIR locals are virtual registers and may be coalesced;
- constant-false branches may be removed later;
- `lambda_stack_overflow_error` and `js_profiled_push_d` call sites are often
  cold;
- a static MIR call-site count is not a runtime invocation count.

No debug-build timing is used as performance evidence. Runtime validation must
use `make release`.

### 2.2 Probe corpus

Four focused temporary programs were written to isolate common lowering
features:

| Temporary source | Features |
|---|---|
| `temp/js_mir_tune_basics.js` | Empty/constant returns, numeric specialization, dynamic add, bindings, branches, logical and ternary expressions |
| `temp/js_mir_tune_objects_calls.js` | Object and array literals, property read/write, callback and method calls, destructuring, spread and optional access |
| `temp/js_mir_tune_control_exceptions.js` | `for`, `while`, `for..of`, `switch`, try/catch, try/finally and throw |
| `temp/js_mir_tune_advanced.js` | Closures, defaults/rest, class methods, generator and async state machines, chained built-ins |

Five existing programs supplied less synthetic evidence:

| Existing source | Role |
|---|---|
| `test/fuzzy/js/corpus/property_storm.js` | Repeated property operations |
| `test/fuzzy/js/corpus/closures_scope.js` | Closures and shared scope |
| `test/fuzzy/js/corpus/destructuring.js` | Binding/destructuring paths |
| `test/js/lib_marked.js` | Large production-style library, 2,484 source lines |
| `test/benchmark/jetstream/richards.js` | Object-oriented benchmark body |

Together these sources contain 3,479 lines. All nine selected programs
compiled and executed successfully. Two broader feature fixtures were not
included because they failed in parsing/lowering before a complete comparable
MIR module was produced; partial dumps would bias the totals.

The working analyzer is `temp/analyze_js_mir_tune.py`. It parses function
summaries, executable opcodes, call targets, generated locals, frame-slot logs,
root stores, and the instruction immediately preceding each exception poll.
The temporary probes and analyzer are analysis artifacts, not permanent
regression tests. Phase 0 below proposes a durable version.

### 2.3 Per-file results

| MIR module | Functions | Instructions | Locals | Calls | Exception polls | Eval pops | Root slots | Root stores | `MAY_GC` | `NO_GC` |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Focus: basics | 11 | 1,176 | 479 | 221 | 20 | 14 | 28 | 116 | 156 | 53 |
| Focus: objects/calls | 12 | 2,019 | 772 | 434 | 64 | 28 | 64 | 249 | 311 | 110 |
| Focus: control/exceptions | 12 | 1,837 | 762 | 353 | 42 | 18 | 39 | 138 | 229 | 111 |
| Focus: advanced | 14 | 2,177 | 841 | 474 | 71 | 25 | 63 | 245 | 338 | 115 |
| Property storm | 4 | 3,321 | 1,503 | 1,080 | 318 | 6 | 15 | 622 | 639 | 436 |
| Closures/scope | 12 | 2,022 | 806 | 437 | 95 | 30 | 46 | 205 | 266 | 150 |
| Destructuring | 5 | 1,935 | 821 | 570 | 131 | 8 | 20 | 295 | 385 | 179 |
| Marked | 132 | 74,475 | 33,085 | 18,374 | 4,938 | 360 | 1,108 | 8,214 | 10,665 | 7,444 |
| Richards | 40 | 9,520 | 4,021 | 2,775 | 733 | 99 | 204 | 1,218 | 1,797 | 932 |
| **Total** | **242** | **98,482** | **43,090** | **24,718** | **6,412** | **588** | **1,587** | **11,302** | **14,786** | **9,530** |

The top opcodes are `mov` at 39.9%, `call` at 25.1%, `bt` at 13.6%,
`jmp` at 6.8%, and `eq` at 5.1%. This confirms that helper protocols,
state publication and branches dominate textual MIR more than core arithmetic
does.

### 2.4 Focused function anatomy

| Function | Instructions | Locals | Calls | Exception polls | Eval pops | Root slots | Root stores |
|---|---:|---:|---:|---:|---:|---:|---:|
| Empty return | 85 | 31 | 7 | 1 | 3 | 2 | 3 |
| Constant number return | 74 | 30 | 6 | 0 | 2 | 1 | 1 |
| Native numeric body | 54 | 32 | 6 | 0 | 0 | 0 | 0 |
| Dynamic add | 76 | 27 | 5 | 0 | 2 | 3 | 3 |
| Property read/write | 103 | 40 | 15 | 6 | 3 | 4 | 5 |
| Array destructuring | 173 | 61 | 39 | 8 | 2 | 5 | 22 |
| `for..of` | 202 | 84 | 33 | 6 | 3 | 4 | 10 |
| try/finally | 222 | 91 | 39 | 9 | 2 | 4 | 12 |
| Generator state machine | 224 | 76 | 47 | 16 | 5 | 5 | 26 |
| Async state machine | 153 | 60 | 26 | 8 | 0 | 4 | 13 |
| Chained `map().filter()` | 192 | 75 | 41 | 6 | 3 | 6 | 13 |

The four focused modules total only 7,209 instructions, but their shared
scalar-return instructions account for 1,352 instructions (18.8%), and
root/frame-related instructions account for 1,783 (24.7%). Those categories
overlap with body root stores and should be read as attribution signals, not
independent removable totals.

Large real functions magnify the same patterns. The largest generated Marked
function contains 18,185 instructions, 8,515 locals, 3,623 calls, 608 exception
polls and 1,927 root stores. This is not a new broad-rooting transform; it is a
large body accumulating the per-operation protocols measured above.

## 3. Major inefficiency 1: incomplete helper effect metadata

### 3.1 Current behavior

`JitImportMetadata` currently describes:

- GC effect (`MAY_GC` or `NO_GC`);
- re-entry effect;
- return semantic representation;
- argument semantic representations.

Unannotated imports default conservatively. This is required for correctness,
but many simple runtime state helpers are still unannotated. Each such call is
treated as a safepoint, and an `i64` result with unknown representation can be
treated as a possible GC value.

The direct-call sequence provides a concrete example. `js_with_save_depth()`
only reads and returns an integer, yet current MIR contains sequences such as:

```text
call  js_with_save_depth, js_with_save_depth_N
mov   i64:slot(js_root_frame), js_with_save_depth_N
```

The integer scope depth has no GC meaning. It is published because the import
has neither `NO_GC` nor `NON_GC_SCALAR` metadata.

The following selected helpers have straightforward leaf bodies and account
for 1,139 call sites in the corpus:

| Helper family | Call sites | Required classification after audit |
|---|---:|---|
| `js_get_new_target` | 84 | `NO_GC`, boxed Item result |
| `js_set_this` | 114 | `NO_GC`, boxed Item argument |
| `js_set_direct_new_target` | 90 | `NO_GC`, boxed Item argument |
| `js_with_save_depth` | 51 | `NO_GC`, non-GC scalar result |
| `js_with_restore_depth` | 51 | `NO_GC`, non-GC scalar argument |
| `js_set_function_source` | 271 | `NO_GC`, boxed Item arguments |
| `js_mark_strict_func` | 82 | `NO_GC`, boxed Item argument |
| `js_set_module_var` | 195 | `NO_GC`, scalar index plus boxed Item |
| `js_get_module_var` | 201 | `NO_GC`, scalar index plus boxed Item result |
| **Total** | **1,139** | |

This is an audit list, not permission to bulk-mark helpers by name. Each body
must be checked for allocation, callbacks, proxy/accessor dispatch, error
creation and re-entry. In particular, `js_get_global_this` is not a `NO_GC`
helper because its lazy initialization allocates and populates the global
object. `js_set_function_name` is also not equivalent to
`js_set_function_source`: private-name normalization can allocate.

### 3.2 Proposal

1. Audit and annotate the leaf helpers above in `lambda/sys_func_registry.c`.
2. Add registry validation tests for both effect and semantic value classes.
3. Split mixed fast/slow helpers rather than incorrectly marking them
   `NO_GC`. A fast helper must assert its invariant in debug builds and never
   fall back to an allocating path.
4. Add a MIR diagnostic listing the top unannotated helpers by emitted call
   count and root stores caused at their call sites.

One important split candidate is
`js_array_define_dense_element_direct`. The corpus emits 551 calls. Its common
fresh-array arm is a direct owned store, but its fallback calls general
`js_array_set` and may allocate or invoke JS semantics. A separate
`js_array_store_fresh_no_gc` helper can be `NO_GC` only when lowering has just
allocated an array with proven capacity; all other cases retain the current
helper.

This phase improves the rooting implementation rather than bypassing it. The
root analysis remains conservative for unaudited helpers and exact for
classified values.

## 4. Major inefficiency 2: exception poll proliferation

### 4.1 Current behavior

`jm_emit_pending_exception_check()` always creates a fresh local, calls
`js_check_exception`, and branches to the active completion target. The
runtime helper only reads `js_runtime_state.exception_pending` under a
no-GC assertion.

The corpus emits 6,412 polls:

- 25.9% of all MIR call instructions are exception polls;
- the call plus `bt` pair occupies 12,824 instructions, 13.0% of all emitted
  instructions;
- every poll creates a distinct `js_check_exception_N` local;
- 627 polls immediately follow the branch of another poll, with no operation
  between them.

Those 627 second polls are provably redundant. Removing them alone deletes
627 calls, 627 branches and 627 locals. This is a lower bound. A poll following
`js_args_restore` is not automatically redundant: the earlier user call may
have thrown and the restore is intentionally placed before the poll. Effect
state must track the potentially throwing operation, not merely inspect the
immediately preceding instruction.

The main source of duplicates is layered lowering: a property/call/coercion
subroutine emits a check, then the enclosing expression dispatcher emits
another unconditional check.

### 4.2 Proposal: exception effects and clean-state propagation

Extend import metadata with an exception effect independent of GC effect:

```text
JIT_EXCEPTION_MAY_SET
JIT_EXCEPTION_PRESERVES
JIT_EXCEPTION_CLEARS
```

`NO_GC` does not imply `PRESERVES`: a helper can throw without collecting,
and an allocating helper can complete without changing the pending exception.
The dimensions must remain separate.

Track an emitter state on normal control-flow edges:

```text
CLEAN    a poll has established that no exception is pending, and only
         PRESERVES operations have executed since
UNKNOWN  an operation may have set the pending exception
```

Rules:

1. a potentially throwing call changes the state to `UNKNOWN`;
2. a preserving call/instruction leaves it unchanged;
3. a generated check is omitted when the state is `CLEAN`;
4. normal fallthrough after a check is `CLEAN`;
5. a label is `CLEAN` only if all incoming normal edges are `CLEAN`;
6. unknown joins, backedges and exception edges remain conservative.

Implementation can be staged:

- first, remove adjacent duplicate checks and reuse one status virtual
  register per function;
- next, propagate state within a basic block;
- finally, compute the two-state fixed point over the MIR CFG.

Reusing one status register reduces textual locals but not runtime work. Poll
elimination is the important optimization.

### 4.3 Longer-term ABI option

Once effect metadata is reliable, hot throwing helpers can return a dedicated
internal exception sentinel or a value/status pair. The caller branches on the
returned status rather than making a second C call to read global state.
Property access, calls and coercions are candidates.

This ABI is invasive and must remain a later phase. The sentinel must not
collide with any observable JS `Item`, and scalar/void helpers need explicit
forms. A direct load of `exception_pending` is another option only after JS
runtime state has an ABI-stable address or is made context-local; the current
C accessor should not be bypassed by hard-coded global layout assumptions.

## 5. Major inefficiency 3: eval-local frames in non-eval functions

### 5.1 Current behavior

Ordinary functions and generator state machines allocate an
`eval_local_frame` register and initialize it to zero unconditionally. Unified
return, exception and cleanup paths call
`jm_emit_eval_local_pop_if_needed()`, producing:

```text
bf    done, eval_local_frame
call  js_eval_local_pop_frame
mov   eval_local_frame, 0
```

The actual push/note paths already test `current_fc->has_direct_eval` in key
places, but frame allocation and cleanup emission do not.

None of the nine measured source programs contains `eval(...)`. Nevertheless,
their MIR has:

- 588 `js_eval_local_pop_frame` call sites;
- 1,992 instructions mentioning eval-frame state;
- cleanup in 222 of 242 generated functions.

The pop call is normally behind a constant-false branch and may not execute,
but it increases MIR size, imports, CFG edges, compilation work and conservative
call/root analysis before backend simplification.

### 5.2 Proposal

Create eval-local frame state only when the generated body can execute direct
eval or must expose local bindings to a direct-eval descendant under the
implemented environment model.

The eligibility decision must be computed once during function collection and
used consistently by:

- ordinary function lowering;
- generator and async state-machine lowering;
- every early return/throw/cleanup path;
- eval binding note/writeback helpers.

For a function declared ineligible:

- `eval_local_frame_reg` remains zero;
- no initialization, push, note, writeback or pop MIR is emitted;
- cleanup helpers become compile-time no-ops.

The acceptance target for the current no-eval corpus is exactly zero eval
push/pop calls and zero eval-frame locals. Direct-eval fixtures must retain
their current semantics, including strict eval, lexical bindings, closures,
try/finally and non-local completion.

## 6. Major inefficiency 4: call-site argument and dynamic-state protocols

### 6.1 Argument-stack save/restore

The expression dispatcher wraps every `CallExpression` in
`js_args_save`/`js_args_restore`. This is correct for fallback calls that push
arguments onto the transient runtime argument stack, but direct MIR calls and
native-specialized calls pass operands in their MIR call signature and often
emit no `js_args_push` at all.

Measured evidence:

- 828 save calls and 828 restore calls;
- 170 matched pairs contain no emitted `js_args_push` between them;
- those 170 pairs are a proven lower bound of 340 removable hot helper calls.

Nested calls make a simple textual interval conservative: an outer direct call
may contain inner argument pushes while still requiring no watermark of its
own. Therefore 170 is a lower bound, not the total opportunity.

Move watermark ownership into the lowering paths that actually use the
transient stack. A call result should carry whether a mark was opened, or the
fallback/apply/constructor paths should manage the mark internally. Direct
fixed-arity and native calls should not open one.

### 6.2 Direct-call global state

A direct local function call currently saves and restores process/runtime
state around the MIR call:

1. lexical `this`;
2. `new.target`;
3. argument watermark;
4. current `with` depth;
5. callee `this` and direct `new.target` installation;
6. restoration of all state;
7. exception polling.

The corpus contains 51 `js_with_save_depth`/restore pairs, and the focused
module initializers make the cost obvious. This protocol is executed on the
hot direct-call path, unlike the cold overflow label.

Near-term improvements:

- apply correct `NO_GC`/value metadata to all state helpers;
- omit argument marks for direct calls;
- avoid a second exception poll when the call dispatcher has already checked;
- use per-function flags to skip `new.target`, `this`, `arguments`, or `with`
  state that the callee cannot observe, including through direct eval.

Longer-term, generated functions should have two entry layers:

```text
generic JsFunction wrapper
    establishes dynamic JS call state and validates Context binding
    -> direct generated body ABI

direct generated body ABI
    receives hidden this/new.target/environment operands as needed
    -> no global save/install/restore protocol at each direct call
```

The wrapper is used by external/native/dynamic calls. Statically resolved
generated-to-generated calls target the body ABI. The body still reserves and
restores its own per-activation root/number extent; only the Context-binding
check is inherited. This is the cleanest way to move binding checks out of
every trusted internal call without sharing root frames across activations.

The `with` stack needs its own invariant before removing restoration. Prefer
restoring a function's own entry depth in its unified epilogue only when the
function can manipulate `with`, rather than making every caller defend against
every callee.

## 7. Major inefficiency 5: numeric boxing and scalar-return cleanup

### 7.1 Unused native-local boxing

The native version of:

```javascript
function tuneNumeric(a, b) {
    let sum = a + b;
    let product = sum * 3;
    return product - 1;
}
```

has zero precise root slots but still contains two complete float-boxing
diamonds, one after assigning `sum` and one after assigning `product`:

```text
call lambda_mir_double_bits
and / branches / zero handling
call js_profiled_push_d       # cold out-of-line case
```

Neither boxed result is consumed. In
`lambda/js/js_mir_statement_lowering.cpp`, a native `let`/`const` value is
boxed before calling
`jm_declare_evalscript_global_lexical_if_needed()`. That callee immediately
returns unless the compilation is a direct eval script. Eligibility is tested
after the expensive value has already been produced.

Move the `is_eval_direct` and declaration-kind decision before boxing. Apply
the same rule to integer native locals. Boxing remains required for an env
writeback, capture, global property, boxed call argument or boxed return.

This is a root-cause fix, not dead-code cleanup: the cold allocation call makes
the unused boxing diamond effectful enough that generic backend DCE cannot be
relied upon to remove it.

### 7.2 Dynamic scalar-return classification

The shared scalar-return epilogue correctly prevents returned wide scalar
payloads from pointing into a callee-owned number-stack extent. In dynamic
mode it classifies inline doubles and the `INT64`, `FLOAT`, `FLOAT64`, and
datetime tags, then either restores the watermark or donates one payload slot.

Across the corpus this accounts for 5,955 instructions and 2,475 locals in
180 functions. In the focused corpus it is 18.8% of all instructions.

The mechanism must stay for truly dynamic Item returns. It should not be used
when return analysis proves one of these modes:

- no out-of-line scalar can be returned;
- float only;
- int64 only;
- datetime only;
- native MIR scalar return.

`em_scalar_return_mode_for_type()` already supports these modes. The tuning
work is to improve JS return-type inference and preserve the mode through
wrapper/state-machine generation. Constant `undefined`, Boolean, null, object,
function and inline-integer returns can use `NONE`.

Number literals should also use a constant Item encoding when representable,
rather than always emitting a runtime bit-reinterpretation and boxing diamond.
Out-of-line or non-representable values keep the current path.

## 8. Major inefficiency 6: unresolved-name and TDZ checks

### 8.1 Constant `undefined`

The focused empty function's `return undefined` is lowered through
`js_get_with_binding_or_fallback`, an exception poll, root publication and a
dynamic scalar-return classifier. When lexical analysis proves there is no
shadowing parameter/local/capture, no active `with` environment and no direct
eval that can introduce or resolve the name differently, the compiler can
emit `ITEM_JS_UNDEFINED` directly.

This must be a proof-based optimization. `undefined` may be shadowed by a
parameter or lexical binding, and the current dynamic environment model must
be honored.

### 8.2 Definite initialization

The corpus contains 452 `js_check_tdz` calls. Some are required, especially for
closures, loops, classes, cyclic lexical initialization and resumed state
machines. Others are locally provable.

For example, the binding probe initializes `third`, then checks it for TDZ
before a later assignment even though no control-flow path reaches the check
uninitialized. Generator lowering repeatedly checks restored locals such as
`index` and `limit` after the state machine has established their initialized
state.

Add a per-binding definite-initialization analysis:

- lattice: `TDZ`, `INITIALIZED`, `MAYBE_TDZ`;
- assignment/initializer transfers to `INITIALIZED`;
- control-flow join preserves `INITIALIZED` only if every predecessor does;
- closure escape, direct eval, unresolved capture and resume edges widen
  conservatively;
- loops use a fixed point;
- state-machine resume labels carry explicit initialized-slot facts.

Emit `js_check_tdz` only for `MAYBE_TDZ`. A successful check can refine the
normal fallthrough to `INITIALIZED` until an operation that can invalidate the
proof.

Source-binding version locals such as `_js_holder_6`/`_js_holder_10` are not by
themselves a runtime defect: MIR virtual registers can be coalesced. The
optimization target is repeated checks, boxing and root publication across
those versions, not simply textual renaming.

## 9. Major inefficiency 7: literals, destructuring and iteration

The measured call totals include:

- 551 `js_array_define_dense_element_direct` calls;
- 397 `js_create_data_property` calls;
- 2,500 property-related calls in total;
- 594 `js_require_object_coercible` calls;
- iterator setup/step/close protocols in destructuring and `for..of`.

These operations are semantically rich. Getters, proxies, modified
prototypes, custom iterators, `IteratorClose`, computed property keys and
abrupt completion prevent unconditional replacement with raw loads/stores.

Safe specializations are still possible:

1. use the audited fresh-array no-GC store after an exact-capacity allocation;
2. prebuild shapes for object literals containing only static data keys, then
   fill owned slots in source evaluation order;
3. guard dense-array destructuring and `for..of` on the built-in iterator and
   relevant prototype/epoch, with the current iterator protocol as fallback;
4. combine helper result and exception status on guarded fast paths so the
   common arm does not perform an independent poll;
5. preserve IteratorClose and abrupt-completion behavior on every fallback.

The 173-instruction array-destructuring probe and 202-instruction `for..of`
probe show the opportunity, but this work belongs after effect, eval and call
protocol cleanup because those improvements also reduce the generic fallback.

## 10. Major inefficiency 8: module/function initialization

The four focused `js_main` functions range from 466 to 787 instructions and
161 to 271 calls. Top-level function setup repeatedly emits:

- `js_new_function`;
- separate boxed strings for name and source;
- `js_set_function_name`;
- `js_set_function_source`;
- strict/eval-initializer flags;
- `js_set_module_var`;
- global function-property definition;
- root stores around conservatively classified calls.

The corpus contains 122 new-function calls, 182 name setters, 271 source
setters, 82 strict markers, 195 module-variable setters and 114 global-property
definition calls.

First annotate the non-allocating setters correctly. Then consider a fused
function-construction helper or descriptor table that supplies arity, flags,
name and source when the function object is allocated. The source text cannot
simply be dropped or made observably lazy because
`Function.prototype.toString` requires it. Global declaration instantiation
order and function identity must also remain unchanged.

A fused allocator can remove setter calls and repeated root transitions while
preserving one allocation and the existing publication order. Batch global
definition should be considered only after proving descriptor and redeclaration
semantics.

## 11. Major inefficiency 9: fixed checked-frame scaffold

Every generated function currently emits:

- runtime/context resolution;
- a root-top binding check;
- a conditional `lambda_side_stack_ensure` call;
- a number watermark load;
- root capacity/zeroing when slots exist;
- watermark restoration;
- a cold `lambda_stack_overflow_error` label.

There are exactly 242 ensure and 242 overflow call sites for 242 corpus
functions. The ensure call normally runs only when the Context is not bound,
and the overflow call is cold. Still, zero-root native bodies retain 11–12
frame-related instructions. `_js_tuneNumeric_316_n` has zero root slots and
54 instructions; `_js_tuneBranch_609_n` has zero slots and 28 instructions.

Do not remove the checked frame from generic entry points. Native code,
callbacks and different Contexts can enter a generated function. Instead use
the wrapper/body split proposed in section 6:

- generic wrappers bind/validate the Context before entering a body;
- every body that needs slots still reserves/restores its own activation
  extent;
- trusted internal body calls inherit a known bound Context and omit only the
  binding path;
- a zero-root, native-scalar body can omit root-top reservation entirely;
- number watermark handling remains when the body can allocate or return an
  out-of-line scalar.

This is a medium-risk ABI optimization. It should follow the simpler emitter
fixes and must keep forced-GC precise-only testing.

## 12. Recommended implementation plan

### Phase 0 — make MIR size a repeatable metric

1. Move a reduced probe set and analyzer into a stable test/tool location.
2. Record per-function instructions, locals, calls, polls, root slots, root
   stores, `MAY_GC` calls and no-push argument marks.
3. Keep the metric diagnostic-only at first; do not create brittle full-text
   MIR goldens.
4. Add selected structural assertions, such as no eval frame in a no-eval
   function and no boxed native-local value without a consumer.

### Phase 1 — low-risk emitter and metadata fixes

1. Audit the 1,139 selected leaf-helper call sites through registry metadata.
2. Add `NO_GC`/non-GC-scalar metadata for the `with` depth helpers first and
   assert that their results never receive a root slot.
3. Gate eval-frame allocation and cleanup on direct-eval eligibility.
4. Test evalscript eligibility before boxing native `let`/`const` values.
5. Move argument watermarks into paths that emit argument-stack pushes.
6. Remove immediately adjacent duplicate exception polls and reuse one poll
   result register.

Expected structural acceptance on this corpus:

- eval-frame calls and locals become zero;
- at least 340 argument watermark calls disappear;
- at least 627 exception calls and branches disappear;
- no `js_with_save_depth` result is classified or stored as a GC root;
- the native numeric probe loses both unused boxing diamonds;
- no precise-root or wide-scalar correctness test regresses.

### Phase 2 — effect-aware lowering

1. Add exception-effect metadata.
2. Implement basic-block then CFG exception-state propagation.
3. Improve return-mode inference and constant Item emission.
4. Add TDZ definite-initialization analysis, including generator/async resume
   facts.
5. Split fresh-array no-GC store from the general dense-element helper.

### Phase 3 — call and entry ABI

1. Add generic wrapper and trusted direct-body entry points.
2. Pass hidden `this`, `new.target` and environment values only when required.
3. Make `with` cleanup a callee invariant for functions that manipulate it.
4. Omit the root-frame prologue from proven zero-root, native-scalar trusted
   bodies.

### Phase 4 — guarded high-level specialization

1. Dense built-in array iterator/destructuring fast path with epoch guards.
2. Pre-shaped static object literal construction.
3. Fused function allocation/name/source/flag initialization.
4. Optional exception-sentinel or multi-result ABI for hot throwing helpers.

## 13. Verification gates

Each phase must be measured in three separate dimensions.

### 13.1 Structural MIR

- rerun the focused and real-world corpus;
- compare instructions, calls, locals, polls, root slots and root stores;
- inspect representative MIR, not only aggregate totals;
- separate hot fallthrough from cold labels;
- ensure a lower MIR count was not obtained by moving the same work into an
  unmeasured generic helper without a runtime benefit.

### 13.2 Correctness and GC safety

- `make build-test`;
- focused JS transpiler and Node preliminary tests;
- direct/indirect eval, strict eval and eval lexical fixtures;
- exception propagation through nested calls, expressions, loops,
  try/catch/finally, generators and async;
- `this`, `new.target`, `arguments`, `with`, closures and re-entry;
- forced GC at helper safepoints;
- precise-only execution with conservative native-stack scanning disabled;
- root-slot poison/validation and write-through-oracle differential runs;
- `make test262-baseline` with zero failures and zero retry-only tests;
- Node baseline with zero regressions.

### 13.3 Performance

Build with `make release`. Measure separately:

- compile/JIT time for Marked and other large modules;
- code size or generated machine-code bytes where available;
- direct-call microbenchmarks;
- numeric loops;
- property-heavy and iterator-heavy programs;
- full Test262 wall time with prep/batch/retry breakdown.

Static MIR reduction is a means, not the final acceptance criterion. A cold
label reduction may improve compilation and code size without affecting hot
runtime, while removing exception accessor calls or false safepoints can
improve both.

## 14. Correctness constraints and non-goals

The tuning work must not:

- weaken safepoint-current canonical rooting;
- treat physical `MIR_T_I64` as sufficient GC representation metadata;
- mark a helper `NO_GC` because its common branch happens not to allocate;
- infer `NO_THROW` from `NO_GC`;
- remove a poll merely because its immediately preceding instruction is a
  no-throw cleanup operation;
- constant-fold `undefined` when lexical/dynamic shadowing is possible;
- bypass proxy, getter, iterator or abrupt-completion semantics;
- remove scalar-return rehoming for a dynamic/wide-scalar return;
- use debug-build timings as performance evidence;
- modify C2MIR as part of this plan.

The goal is not the smallest possible textual MIR. The goal is the smallest
hot and compile-time protocol that preserves exact JS semantics and precise GC
safety.

## 15. Source map

| Area | Current source |
|---|---|
| Exception poll emission | `lambda/js/js_mir_completion.cpp` |
| General call-expression wrapper | `lambda/js/js_mir_expression_lowering.cpp` |
| Direct/native call lowering | `lambda/js/js_mir_expression_lowering.cpp` |
| Eval-local push/pop helpers | `lambda/js/js_mir_expression_lowering.cpp` |
| Per-function eval-frame creation | `lambda/js/js_mir_function_class_lowering.cpp` |
| Native variable declaration/boxing | `lambda/js/js_mir_statement_lowering.cpp` |
| JS helper call/root effect handling | `lambda/js/js_mir_calls_boxing_types.cpp` |
| Root/number frame prologue and epilogue | `lambda/js/js_mir_hashmap_scope_utils.cpp` |
| Shared scalar-return rehoming | `lambda/mir_emitter_shared.hpp` |
| Module/function initialization | `lambda/js/js_mir_module_batch_lowering.cpp` |
| Runtime import effects/value classes | `lambda/sys_func_registry.c`, `lambda/sys_func_registry.h` |
| Runtime exception state | `lambda/js/js_runtime_state.cpp`, `lambda/js/js_runtime_state.hpp` |
| Argument stack runtime | `lambda/js/js_runtime_function.cpp` |
| `with` and eval frame runtime | `lambda/js/js_globals.cpp` |
