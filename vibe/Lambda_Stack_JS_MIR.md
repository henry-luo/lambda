# LambdaJS stack-frame MIR emission review

**Status:** historical review of the broad shadow-copy implementation, updated
with the implemented safepoint write-back result

**Historical review date:** 2026-07-16

**Post-implementation profile:** 2026-07-19 at `638e11c93`

**Historical side-stack commit reviewed:** `9402b16980e8`

**Exact pre-stack comparison point:** `5043b95690ff`, the direct parent of the first JS stack-frame commit `451962211`

Unless a section explicitly says post-implementation, “Current” in the
original review below means the historical `9402b169` implementation, not
repository `HEAD`.

## 0. Post-implementation result — 2026-07-19

LambdaJS now uses safepoint-current canonical slots in production. Semantic
binding homes and temporary candidates are reported to the shared
`MirEmitter`; classified `MAY_GC` call sites drive CFG liveness, dirty-state
propagation, scratch coloring, compact slot assignment, and final root-store
insertion. Production no longer republishes the full lexical scope or scans
root bindings after every instruction.

For the identical `frameReview` probe used by the historical review:

| Metric | Pre-stack `5043b956` | Broad side-stack `9402b169` | Post-implementation write-back `638e11c93` | Broad → post change |
|---|---:|---:|---:|---:|
| Executable MIR instructions | 58 | 440 | **136** | **−304 (−69.1%)** |
| MIR locals | 29 | 214 | **53** | **−161 (−75.2%)** |
| MIR calls | 24 | 26 | **26** | unchanged |
| Per-function side-root slots | 0 | 28 | **7** | **−21 (−75.0%)** |
| Runtime root publication | none | 161 copy/store pairs = 322 instructions | **11 direct stores** | broad publication removed |

The production function is now 30.9% of the broad side-stack instruction
count. The design estimate was approximately 130 instructions; the implemented
result is 136. It remains 78 instructions above the 58-instruction pre-stack
body (2.34x) because precise execution still needs checked frame
binding/reservation, slot initialization, safepoint stores, unified cleanup,
boxed-scalar return classification/donation, watermark restoration, and a cold
overflow path.

The retained write-through oracle provides a same-compiler comparison:

| Current `638e11c93` mode | Executable instructions | Body entries | Locals | Calls | Root slots | Root stores |
|---|---:|---:|---:|---:|---:|---:|
| Write-through oracle | 150 | 168 | 54 | 27 | 15 | 31 |
| Production write-back | **136** | **154** | **53** | **26** | **7** | **11** |
| Reduction | **−14 (−9.3%)** | −14 | −1 | −1 | **−8 (−53.3%)** | **−20 (−64.5%)** |

Both modes classify the probe identically at 13 `MAY_GC` calls and 12
`NO_GC` calls. The current oracle is not the historical 440-instruction
compiler: it shares semantic candidates, canonical homes, import effects, and
the consolidated emitter with production and changes the publication policy
for differential correctness testing.

The collector transition described by the historical review is also complete
for precise tiers. Scanner-independent Lambda/LambdaJS release builds omit
native-stack discovery and `gc_scan_stack()`. Scanner-capable debug builds and
builds containing unchanged C2MIR retain conservative scanning only for shadow
verification or sticky compatibility contexts.

Captured evidence:

- production MIR: `temp/js_mir_latest_writeback.txt`;
- write-through MIR: `temp/js_mir_latest_writethrough.txt`;
- slot/store diagnostics: `temp/js_mir_latest_writeback.log` and
  `temp/js_mir_latest_writethrough.log`;
- full Lambda/JS comparison and measurement rules:
  `vibe/Lambda_Design_Stack_Rooting.md` §12.

The current release Test262 baseline completes all 42,889 tests in 160.6s with
40,261/40,261 fully passing, zero non-fully-passing tests, zero failures, zero
regressions, and zero retry time. This is current acceptance evidence, not a
source-isolated attribution against the historical 195.7s/349.4s comparison;
the intervening source and harness changes make that runtime comparison
non-causal.

## 1. Executive summary

The reviewed `9402b169` JS stack-frame work did not merely add a small function
prologue and epilogue. It added a whole-function rooting transform around the
existing JS MIR:

1. every generated JS function opened root and number side-stack frames;
2. every variable judged heap-capable received a root slot;
3. every runtime helper call republished all live lexical variables;
4. every `I64`/pointer register passed to or returned from a call was rooted;
5. every emitted instruction was checked to see whether it overwrote a rooted
   register;
6. every return was redirected to one epilogue that re-homed scalar payloads
   and restored both watermarks.

The safety goal is valid: a collection during a helper call must see all live
JS objects, and no returned wide scalar may point into the reclaimed callee
number extent. The reviewed implementation, however, rooted substantially more
values and emitted substantially more stores than that invariant required.

> **[HISTORICAL AT `9402b169`: BOTH ROOTING MODELS WERE ACTIVE]**
>
> The new precise LambdaJS side-root stack had **not** replaced the old
> conservative native-stack rooting path. Every collection scanned the
> published side-root region **and then still scanned the native stack**. Old
> registered roots for globals, module variables, runtime singletons, and the JS
> argument stack also remained. The migration replaced permanent closure-env
> root ranges with traced GC ownership, but for ordinary live JS locals the new
> publication was additive to the old conservative discovery mechanism.
>
> Consequently, the runtime paid for the newly emitted root stores and
> root-region scan without receiving the potential payoff of removing the
> conservative native-stack scan. This does not make precise rooting useless,
> but the reviewed implementation was a dual-rooting transition state, not a
> completed replacement. Section 0 documents the completed precise-release
> state.

For the representative function in this report:

| Historical metric | Pre-stack | Reviewed `9402b169` | Change |
|---|---:|---:|---:|
| MIR instructions | 58 | 440 | +382, or 7.59x total |
| MIR locals | 29 | 214 | +185, or 7.38x total |
| Calls | 24 | 26 | side-stack bind/overflow calls added |
| Root copy/store pairs | 0 | 161 | 322 MIR instructions |
| Explicit `ret` instructions | 3 | 2 | returns unified; second return is overflow path |

The 322 root copy/store instructions account for **84.3% of all added MIR** in
this function. A simpler one-argument property-reader grows from 20 to 102 MIR
instructions. An 18-function probe grows from 1,408 to 11,738 MIR instructions.

The full release Test262 comparison using the same runner and suite revisions
was 195.7 seconds at the pre-stack commit versus 349.4 seconds at the reviewed
side-stack commit. The emitted-MIR expansion and the Test262 regression are
therefore consistent.

## 2. Design before the new LambdaJS stack frame

### 2.1 Historical boundary

The exact comparison point in this report is `5043b95690ff`, immediately before
the first LambdaJS stack-frame commit. At that point, the two MIR transpilers did
not use the same rooting model:

- the core Lambda MIR-Direct transpiler already emitted root/number side-stack
  frames for Lambda functions;
- the LambdaJS MIR transpiler emitted no per-function root frame and no
  per-function number watermark;
- both languages still ran over the same GC heap, registered-root tables, and
  conservative native-stack scanner.

Therefore, "pre-stack" in the rest of this report means **before LambdaJS was
migrated to the shared side-stack frame architecture**, not before side stacks
existed anywhere in the runtime.

### 2.2 Pre-change JS invocation frame

An ordinary generated JS function used only the native machine frame produced
by the MIR backend:

```text
native call entry
    MIR parameters arrive in ABI registers/stack locations
    JS locals and temporaries live in MIR virtual registers
    the backend assigns them to machine registers or spill slots
    helper calls use the native calling convention
    each JS return emits a direct MIR_RET
native call exit
```

There was no JS-visible frame descriptor, root-slot count, root watermark, or
number watermark. The transpiler did not emit stores that published JS locals
to the collector. Values live across a helper call survived in whatever
registers or native spill slots the MIR backend selected.

Out-of-band scalar payloads used the shared number-side-stack backing region,
but a JS function did not save and restore `side_number_top`. Such payloads were
effectively execution/context-lifetime temporaries rather than invocation-
lifetime temporaries. A direct return needed no common epilogue because there
was no per-function root or number extent to release.

### 2.3 Pre-change GC root set

Before the JS frame migration, collection discovered reachable JS values from
four principal sources:

| Root source | What it protected | Lifetime / discovery rule |
|---|---|---|
| Registered root slots | Runtime globals and singleton Items such as `js_current_this`, `js_new_target`, `js_exception_value`, and the heap result | Explicit stable addresses registered with `gc_register_root()` |
| Registered root ranges | Module-variable arrays, the JS call-argument stack, and closure environment arrays | Every Item in each registered range was marked |
| Conservative native-stack scan | JS function parameters, locals, and temporaries present in machine registers or native spill slots | Every aligned native stack word was tested as a possible managed Item/pointer |
| Precise heap tracing | Children of an already marked array, map, function, string owner, error, and other typed object | `gc_trace_object()` followed fields according to the allocation type |

The collector already supported caller-supplied extra roots as well, but they
were not the ordinary LambdaJS local-variable mechanism.

The shared collector also marked the dense active side-root region belonging
to any enclosing core Lambda MIR frames. That protected values explicitly
published by the Lambda transpiler, including cross-language caller state, but
the pre-change JS transpiler did not place its own parameters, locals, or
temporaries in that region.

The collection entry sequence was:

```text
heap_gc_collect()
    setjmp(regs)                    // preserve callee-saved register contents
    read native stack base and SP
    mark registered root slots
    mark every Item in registered root ranges
    mark the active core-Lambda side-root region, if any
    mark caller-supplied extra roots
    conservatively scan [SP, stack_base)
    precisely trace the object graph from everything marked above
    sweep unreachable objects
```

`setjmp()` and the native ABI made the conservative scan practical. A value
live across a call generally had to be in a callee-saved register or a spill
slot. Callee-saved register state was either preserved through nested runtime
calls or copied by `setjmp()`, while spill slots were already inside the scanned
native stack interval.

No extra MIR surrounded a normal helper call:

```text
[BASE] evaluate arguments into MIR registers
[BASE] call runtime_helper(...)
[BASE] consume the result
```

If `runtime_helper` caused a collection, the collector relied on registered
storage plus the conservative scan to rediscover all live values. This is why
the pre-stack representative function contains zero explicit root-copy/store
pairs.

### 2.4 Closure and argument rooting before the change

Two JS-owned storage areas avoided dependence on the conservative scan by
being registered explicitly.

The call-argument stack was one fixed 256K-Item region. It was registered once
as a root range. A call saved its logical top, reserved contiguous argument
slots, populated them, then restored the top and zeroed the released slots.
Because the root-range registry knew only the fixed capacity, every collection
walked the whole 2 MiB range, including the zeroed unused portion.

Closure environments used a heavier lifetime rule:

```text
js_alloc_env(count)
    pool-allocate Item[count]
    register Item[count] as a GC root range
    never unregister that environment range
```

This made every environment an independent permanent root. It protected all
captured values without requiring the collector to discover the owning closure,
but it also meant the environment and everything reachable only through it
could remain live until heap/context teardown.

### 2.5 Problems in the pre-change design

The old design was fast in emitted JS MIR because ordinary local rooting added
no instructions, but that low instruction count came with correctness,
retention, and lifetime costs.

#### P1 — Local rooting was conservative rather than compiler-proven

The JS transpiler did not tell the collector which values were semantically
boxed Items, raw GC pointers, or non-GC integers. Correctness depended on the
MIR backend, calling convention, register preservation, tagged-value decoding,
and stack scanner collectively leaving a recognizable copy of every live
value. This worked for the measured baseline, but it was not an explicit
per-safepoint invariant and was vulnerable to backend/optimization changes.

Raw GC pointers made the contract especially difficult to audit. The physical
MIR register type could be `I64` even when the semantic value was a pointer, so
machine type alone could not prove whether an arbitrary register word had to be
kept alive.

#### P2 — Conservative scanning could retain dead objects

The scanner examined all aligned words in active native frames. A stale spill,
integer bit pattern, or obsolete tagged value that happened to decode to a
managed allocation could keep that allocation and its reachable graph alive for
another collection. The collector was precise after finding a root, but the
initial local-root set was intentionally an over-approximation.

#### P3 — Closure environments were permanently pinned

Every `js_alloc_env()` range was registered and never unregistered. Dead
closures therefore did not imply dead environments. The registered-range list
grew with environment creation, collection spent more time walking it, and
objects reachable from obsolete captures could be retained for the heap's
entire lifetime.

This was also an ownership inversion: the root registry, rather than the live
`JsFunction`, owned the environment's reachability. It prevented normal tracing
from expressing "the closure owns its environment; the environment owns its
captured Items."

#### P4 — The argument root range was capacity-scanned

The argument bump stack correctly cleared released entries, but its complete
256K-Item capacity remained a registered root range. Each collection performed
the scan even when only a small prefix was live. This cost predates the new JS
stack frame and should not be attributed to the current MIR blow-up.

#### P5 — JS scalar temporaries had no invocation lifetime

Without a per-function number watermark, out-of-band doubles and polyglot wide
scalars allocated during a JS invocation were not reclaimed at that invocation's
return. Long-running callbacks could therefore grow the shared number extent
until a broader execution/context reset.

#### P6 — There was no unified cleanup edge

Direct returns were cheap, but there was no single epilogue at which JS could
restore root/number watermarks, re-home escaping scalar payloads, or finalize
other invocation-owned state. Adding invocation-lifetime resources required a
return funnel or equivalent cleanup discipline.

### 2.6 What the new design was intended to fix

The new LambdaJS frame design had three valid goals:

1. make live JS locals explicit, precise roots rather than relying solely on a
   conservative scan;
2. make closure environments GC-owned and traced through live functions, so
   dead closure graphs can be collected instead of permanently rooted;
3. give out-of-band scalar payloads invocation lifetime through a saved/restored
   number watermark and an escaping-return re-homing rule.

Those goals do not inherently require publishing every lexical variable before
every helper call or rooting every `I64` input/result. The current bottleneck is
the conservative lowering used to implement precise frames, not the existence
of a bounded side-root region itself. The target design should keep the three
ownership/lifetime improvements while publishing only GC-capable values that
are live across actual GC safepoints.

## 3. Scope and notation

This report covers the MIR emitted for an ordinary boxed JS function body and
then maps the same mechanism to native-specialized functions, `js_main`,
generators, async state machines, and closures.

The exact probe is:

```js
function frameReview(a, b, callback) {
    let sum = a + b;
    let holder = { value: sum };
    sum = callback(holder, sum);
    if (sum) {
        holder.value = sum;
    }
    return holder.value;
}

frameReview(1, 2, function readValue(object) {
    return object.value;
});
```

MIR register suffixes and label numbers are allocation artifacts. The semantic
instruction shapes are stable. Offsets such as `80`, `96`, and `112` below are
the values of `offsetof(Context, ...)` in the captured build; source code uses
`offsetof`, not hard-coded offsets.

New post-stack behavior is highlighted as:

- `[NEW:PROLOGUE]` — side-stack entry and capacity checks;
- `[NEW:ROOT]` — root-slot allocation, publication, or synchronization;
- `[NEW:RETURN]` — unified return and scalar re-homing;
- `[NEW:OVERFLOW]` — side-stack failure path;
- `[BASE]` — MIR already emitted at the pre-stack commit.

To keep the complete current listing reviewable, this report uses one lossless
abbreviation:

```text
ROOT(value, slot) :=
    mov js_root_bits_N, value
    mov i64:(slot * 8)(js_root_frame), js_root_bits_N
```

Thus `ROOT(sum, 6)` represents exactly two current MIR instructions. No
stack-frame operation is hidden by this notation.

## 4. Runtime frame model

`Context` owns four pointers for each side stack:

```text
root:   side_root_base -> side_root_top -> side_root_limit
number: side_number_base -> side_number_top -> side_number_limit
```

The root stack is a contiguous array of 64-bit candidate roots. At the reviewed
commit, a collection scanned `[side_root_base, side_root_top)` in addition to
the conservative native stack. Restoring `side_root_top` removed the callee's
roots in O(1).

**[HISTORICAL AT `9402b169`: DUAL ROOTING]** The collection sequence marked
both regions:

```text
mark registered root slots and ranges       // old, retained
mark [side_root_base, side_root_top)         // new precise JS publication
mark caller-provided extra roots            // old, retained
scan [native_SP, native_stack_base)          // old conservative scan, retained
trace the marked heap graph
```

The status of each pre-change mechanism is:

| Rooting mechanism | Pre-change JS | Reviewed `9402b169` JS | Migration status |
|---|---:|---:|---|
| Conservative native-stack discovery of JS locals | yes | yes | retained |
| Precise JS side-root publication | no | yes | added |
| Registered global/singleton slots | yes | yes | retained |
| Registered module-variable ranges | yes | yes | retained |
| Fixed-capacity registered argument stack | yes | yes | retained |
| Permanently registered closure-env ranges | yes | no | replaced by GC-owned, precisely traced envs |

A live local may therefore be discoverable twice: once from its explicit side-
root slot and once from a machine register or spill word found by the native-
stack scan. Marking the same object twice is functionally harmless because the
mark bit deduplicates tracing, but publishing and scanning both root sources
still costs instructions, memory traffic, code size, and collection work.

The number stack stores out-of-band scalar payload words. A function saves its
entry `side_number_top`. On return it normally restores that watermark. If the
returned Item points into the callee's number extent, the epilogue copies the
payload into the entry slot and donates that single slot to the caller.

The side stacks reserve virtual address space per thread:

- root stack: 16 MiB;
- number stack: 64 MiB.

`lambda_side_stack_bind()` initialized `Context` from the thread-local regions.
`lambda_side_stack_ensure()` bound on first use and checked/committed capacity.
The reviewed macOS/Linux prologue performed the common capacity check inline;
Windows additionally checked the committed-page watermark.

## 5. Function-lowering lifecycle

### 5.1 Pre-stack lifecycle

Before `451962211`, an ordinary boxed function was lowered as:

```text
create MIR function and parameter registers
push compiler lexical scope
emit JS body
emit direct return(s)
emit exception landing return when required
pop compiler lexical scope
finish MIR function
```

There was no generated side-stack prologue, no root-slot table, no unified
return, and no scalar-watermark restoration in the JS function.

### 5.2 Reviewed `9402b169` lifecycle

The reviewed boxed-function path was:

```text
create MIR function and parameter registers
[NEW] jm_begin_function_frame(...)
    load or remember the runtime Context register
    allocate frame-base, return, anchor, and epilogue registers/labels
    emit only an anchor initially
push compiler lexical scope
[NEW] register parameters/locals and discover root slots while lowering
emit JS body
[NEW] jm_emit() redirects every MIR_RET to the unified return label
emit exception landing return
pop compiler lexical scope
[NEW] jm_finish_function_frame(...)
    emit owned-env scalar re-homing calls
    emit returned-scalar re-homing
    restore number/root tops
    emit the single normal RET
    insert the now-sized prologue before the original anchor
finish MIR function
```

The prologue is inserted late because the static root-slot count is only known
after the whole function has been lowered. This is why the emitted prologue can
reserve exactly 28 slots for the sample even though `jm_begin_function_frame()`
ran before the body.

The same frame wrapper is used at four main entry classes:

| Function class | Return policy | Extra ownership work |
|---|---|---|
| Ordinary boxed JS function | inferred `NONE/FLOAT/INT64/DTIME/DYNAMIC` | closure/scope env re-homing when owned |
| Native-specialized JS function | native `I64` or `D`; scalar mode `NONE` | still opens root and number frames |
| Generator/async state machine | boxed `DYNAMIC` | roots and re-homes `gen_env` |
| `js_main` | boxed `DYNAMIC` | uses the `ctx` parameter directly; may own module scope env |

## 6. Complete pre-stack MIR for the representative body

This is the complete 58-instruction pre-stack function, with declarations and
import prototypes omitted but no executable instruction omitted:

```text
_js_frameReview(a, b, callback):
    mov eval_local_frame, 0
    mov holder_tdz, JS_TDZ
    mov sum_tdz, JS_TDZ

    call js_add, add_result, a, b
    mov sum, add_result

    call js_new_object, new_object
    mov value_key, "value"
    call js_create_data_property, create_result, new_object, value_key, sum
    mov holder, new_object

    call js_check_tdz, sum, source_id, source_offset
    call js_check_exception, exception
    bt exception_landing, exception

    call js_args_save, args_mark
    call js_debug_check_callee, checked_callee, callback, site_id
    call js_args_push, args_ptr, 2
    call js_check_exception, exception
    bt exception_landing, exception
    mov i64:(args_ptr), holder
    call js_check_exception, exception
    bt exception_landing, exception
    mov i64:8(args_ptr), sum

    mov undefined, JS_UNDEFINED
    call js_call_function, call_result, callback, undefined, args_ptr, 2
    call js_check_exception, exception
    bt exception_landing, exception
    call js_args_restore, args_mark
    call js_check_exception, exception
    bt exception_landing, exception
    mov sum, call_result

    call js_is_truthy, truthy, sum
    uext8 truthy, truthy
    bf if_else, truthy
    mov value_key, "value"
    call js_check_exception, exception
    bt exception_landing, exception
    call js_check_exception, exception
    bt exception_landing, exception
    call js_property_set_named_ic,
         set_result, holder, value_site, value_len, sum, strict_flag, cache_site
    jmp if_end
if_else:
if_end:

    call js_check_exception, exception
    bt exception_landing, exception
    call js_property_access_named_ic,
         property_result, holder, value_site, value_len, cache_site
    call js_check_exception, exception
    bt exception_landing, exception

    bf no_eval_pop_on_return, eval_local_frame
    call js_eval_local_pop_frame
    mov eval_local_frame, 0
no_eval_pop_on_return:
    ret property_result

    mov undefined, JS_UNDEFINED
    bf no_eval_pop_on_implicit, eval_local_frame
    call js_eval_local_pop_frame
    mov eval_local_frame, 0
no_eval_pop_on_implicit:
    ret undefined

exception_landing:
    mov null, NULL_ITEM
    bf no_eval_pop_on_exception, eval_local_frame
    call js_eval_local_pop_frame
    mov eval_local_frame, 0
no_eval_pop_on_exception:
    ret null
```

This baseline already contains significant JS semantic machinery:

- generic `a + b` dispatches through `js_add` because the values are boxed;
- object creation and property definition are runtime calls;
- calls use the separately registered `js_args` stack;
- exception propagation is explicit `js_check_exception` plus branches;
- truthiness and property access use runtime helpers/inline caches;
- normal, implicit, and exception returns are separate.

Those are existing JS-lowering costs, not stack-frame additions.

## 7. Reviewed `9402b169` prologue: complete emitted MIR

For the sample's 28 root slots, the current macOS/Linux prologue is:

```text
; [NEW:PROLOGUE] resolve the runtime Context
mov side_rt_addr, _lambda_rt
mov side_rt, i64:(side_rt_addr)

; [NEW:PROLOGUE] load current root watermark
mov root_frame, i64:offsetof(Context, side_root_top)(side_rt)
ne  root_bound, root_frame, 0
bt  already_bound, root_bound

; [NEW:PROLOGUE] first-use slow path
call lambda_side_stack_ensure, ensured, side_rt, 28, 0
bf   stack_overflow, ensured
mov  root_frame, i64:offsetof(Context, side_root_top)(side_rt)

already_bound:
; [NEW:PROLOGUE] save number watermark
mov number_frame, i64:offsetof(Context, side_number_top)(side_rt)

; [NEW:PROLOGUE] reserve 28 root words
add root_top, root_frame, 28 * 8
mov root_limit, i64:offsetof(Context, side_root_limit)(side_rt)
ugt root_overflow, root_top, root_limit
bt  stack_overflow, root_overflow
mov i64:offsetof(Context, side_root_top)(side_rt), root_top

body_anchor:
```

On Windows, a second inline comparison checks `side_root_commit_limit`; the
slow arm calls `lambda_side_stack_ensure()` to commit pages before publishing
the new top.

Even a function that discovers zero root slots still resolves the runtime,
checks/binds the side stack, saves `side_number_top`, creates the unified-return
machinery, and emits the overflow block. The root-top arithmetic/store is the
only part omitted when `side_root_next == 0`.

## 8. How variables become root bindings

### 8.1 Rooting predicate for lexical variables

`jm_should_gc_root_var()` roots:

- all `MIR_T_P` variables;
- legacy entries with `MIR_T_UNDEF`;
- pointer-backed scalar/container/function/error TypeIds;
- `LMD_TYPE_ANY`.

It does not root variables proven to be compact ints, bools, or native doubles.
This lexical-variable predicate has useful TypeId information.

`jm_set_var()` currently creates a fresh variable entry with `root_slot = -1`,
inserts it into the lexical-scope hashmap, and immediately calls
`jm_update_gc_root_slot()`. If rootable, the variable gets the next static slot
and its current register value is stored immediately.

Important consequence: slots are associated with MIR registers, not semantic
variables or live ranges. When `sum` changes from its TDZ register to its real
value register, the real register receives another slot; the TDZ register's
slot remains reserved until function return.

### 8.2 Root slot allocation

`jm_create_gc_root_slot(reg)` performs a linear search over all existing
bindings:

```text
if reg already has a slot:
    ROOT(reg, existing_slot)       ; stores again even on a lookup hit
else:
    slot = side_root_next++
    ROOT(reg, slot)
    append {reg, slot} to side_root_bindings
```

There is no slot release or reuse inside a function. Every rooted temporary
extends the frame's static slot count even after that temporary is dead.

### 8.3 The sample's 28 slots

The captured sample reserves these classes of slots:

| Slots | Values | Review |
|---:|---|---|
| 0-2 | `a`, `b`, `callback` | boxed `ANY`; potentially heap-bearing |
| 3-4 | TDZ/predeclaration registers for `holder`, `sum` | superseded but retained |
| 5-6 | `js_add` result and assigned `sum` | duplicate live value in two registers |
| 7, 10 | new-object result and assigned `holder` | duplicate object in two registers |
| 8 | immortal string literal `"value"` | does not need frame lifetime protection |
| 9 | `js_create_data_property` result | short-lived/unused helper result |
| 11, 15, 16, 19, 20, 22, 23, 25, 27 | exception-check results | boolean/control values, not GC references |
| 12 | `js_args_save` result | args-stack mark, not a GC Item |
| 13 | `js_debug_check_callee` result | debug/control result |
| 14 | `js_args_push` result | raw pointer into separately rooted args stack |
| 17 | boxed `undefined` constant | immediate, not heap-bearing |
| 18 | callback result | boxed Item; may need rooting while live |
| 21 | `js_is_truthy` result | boolean/control value |
| 24 | property-set result | short-lived/unused result |
| 26 | property-get result | boxed Item returned from the function |

Only a minority of the 28 slots are clearly required. The broad call-register
predicate, rather than the lexical-variable predicate, creates most of the
obviously non-GC slots.

### 8.4 Initialization ordering

The sample begins with:

```text
ROOT(holder_tdz_reg, 3)       ; register not initialized yet
mov holder_tdz_reg, JS_TDZ
ROOT(sum_tdz_reg, 4)          ; register not initialized yet
mov sum_tdz_reg, JS_TDZ
```

The slots are corrected by the full live-scope publication before the first
runtime helper call. This is currently safe only under the invariant that GC
can occur at helper-call safepoints, not between ordinary MIR instructions.
The initial garbage stores are nevertheless unnecessary and make that
safepoint assumption correctness-critical. Initialization should precede first
publication, or root slots should be initialized to a known non-pointer value.

## 9. Function calls and their new rooting envelope

### 9.1 Runtime helper calls: existing MIR

`jm_call_0` through `jm_call_6` build a prototype/import and emit a MIR call:

```text
call helper_proto, helper_import, result, arg0, arg1, ...
```

Void helpers omit the result. This mechanism existed before stack frames.

### 9.2 Runtime helper calls: new rooting

`jm_call_with_args()` and `jm_call_void_with_args()` now do this before calling
the shared emitter:

```text
[NEW] publish every rootable variable in every active lexical scope
[NEW] root every register argument whose MIR type is I64 or P
[BASE] emit helper call
[NEW] root an I64/P result register
```

The argument phase does not know whether an `I64` is a boxed Item, an inline
integer, a boolean, a source ID, an args-stack mark, or a raw non-GC pointer.
It roots them all.

There is also no GC-safepoint classification. Calls such as
`js_check_exception`, `js_is_truthy`, `js_args_save`, and debug checks receive
the same full publication envelope as allocation/collection-capable helpers.

### 9.3 Exact `js_add` example

The pre-stack body emitted:

```text
call js_add, add_result, a, b
```

The current body emits:

```text
; [NEW:ROOT] full lexical-scope publication
ROOT(b, 1)
ROOT(a, 0)
ROOT(holder_tdz, 3)
ROOT(callback, 2)
ROOT(sum_tdz, 4)

; [NEW:ROOT] call operands are rooted again
ROOT(a, 0)
ROOT(b, 1)

; [BASE]
call js_add, add_result, a, b

; [NEW:ROOT] result gets a function-lifetime slot
ROOT(add_result, 5)
```

This illustrates three amplification sources:

1. variables not used by `js_add` are still republished;
2. `a` and `b` are written twice because they are both live lexical variables
   and call operands;
3. the result gets a permanent slot even though it is immediately copied into
   `sum`, which gets a second slot.

### 9.4 Dynamic JS function call: existing body

For `callback(holder, sum)`, the pre-stack lowering is:

```text
call js_args_save, args_mark
call js_debug_check_callee, checked, callback, site_id
call js_args_push, args_ptr, 2
call js_check_exception, exception
bt exception_landing, exception
mov i64:(args_ptr), holder
call js_check_exception, exception
bt exception_landing, exception
mov i64:8(args_ptr), sum
mov undefined, JS_UNDEFINED
call js_call_function, result, callback, undefined, args_ptr, 2
call js_check_exception, exception
bt exception_landing, exception
call js_args_restore, args_mark
call js_check_exception, exception
bt exception_landing, exception
```

The current lowering preserves every `[BASE]` instruction above, but inserts a
full live-scope publication before each helper call, explicit rooting for each
register operand, and a new slot for each I64/P result. The args buffer itself
already lives in `js_args_stack`, which is registered once with the GC, yet its
raw pointer and save mark still consume root slots.

### 9.5 Direct local/native calls

Direct local and native-specialized calls construct `MIR_CALL` manually and
pass it through `jm_emit()`. `jm_emit()` recognizes `MIR_CALL`/`MIR_JCALL` and
applies another call-rooting path:

```text
[NEW] jm_root_live_scope_vars()
[NEW] root all I64/P input registers found in MIR operands
[BASE] emit MIR_CALL or MIR_JCALL
[NEW] root all I64/P output registers found in MIR operands
```

Runtime-helper calls normally bypass this interception through the shared
emitter and therefore implement equivalent rooting manually. The root policy
is consequently duplicated across two emission paths, which creates drift and
audit risk.

## 10. Root bindings after ordinary instructions

After every instruction passed through `jm_emit()`, current lowering calls
`jm_emit_root_updates()`.

At MIR-generation time it performs:

```text
for each output register of the just-emitted instruction:
    for each side_root_binding in the function:
        if output_register == binding.reg:
            emit ROOT(output_register, binding.slot)
```

Example:

```text
; original assignment
mov sum, callback_result

; generated when sum is already bound to slot 6
mov js_root_bits_N, sum
mov i64:48(root_frame), js_root_bits_N
```

The scan itself runs in the C++ transpiler, not in generated code. Its lowering
cost is approximately:

```text
emitted instructions x instruction outputs x registered root bindings
```

Only a matching register emits runtime MIR. Nonmatching instructions still pay
the linear binding scan during compilation.

There is a second linear scan inside `jm_create_gc_root_slot()` whenever a call
operand/result or lexical variable is rooted. Therefore both slot lookup and
post-instruction synchronization become more expensive as the monotonically
growing binding array expands.

## 11. Branches, properties, exceptions, and assignments

The following table shows the full feature-level effect. “New envelope” is
additional to the existing MIR in the middle column.

| JS feature | Existing/pre-stack MIR | New stack-frame envelope |
|---|---|---|
| `let x` | TDZ/immediate initialization and a MIR register | register may get a root slot at compiler-scope insertion |
| `x = value` | `mov x, value` plus env/module writeback when applicable | post-instruction binding scan; matching rooted register emits `ROOT(x, slot)` |
| `a + b` with boxed operands | `call js_add` | publish all live scope vars; root `a`, `b`; root result |
| object literal | `js_new_object`, string literal, `js_create_data_property` | full publication at every helper; object, string, and helper results get slots |
| property read | named-IC helper plus exception check | full publication before property helper and exception helper; both results rooted |
| property write | named-IC setter | full publication; receiver/value rooted again; result rooted even if unused |
| `if (x)` | `js_is_truthy`, `uext8`, branch labels | publication before truthiness call; truthiness result gets a root slot |
| dynamic JS call | args save/push/store, `js_call_function`, restore, exception checks | publication/rooting around every one of those helper calls |
| direct local call | direct `MIR_CALL` | `jm_emit()` roots live scope, inputs, and outputs |
| exception propagation | `js_check_exception` plus branch | all live roots republished; boolean exception result rooted |
| explicit/implicit/error return | separate direct `ret` paths | value moved to shared return register and jumped to epilogue |

## 12. Reviewed `9402b169` return funnel and complete epilogue

### 12.1 Return rewriting

When `jm_emit()` sees a one-value `MIR_RET` inside an active frame, it replaces:

```text
ret value
```

with:

```text
; [NEW:RETURN]
mov js_return_value, value
jmp unified_epilogue
```

The sample's explicit return, implicit return, and exception return all use this
funnel. `try/finally`, generators, and async functions still perform their
existing semantic completion work before reaching the frame return.

### 12.2 Owned environment re-homing

Before restoring the number watermark, each environment registered through
`jm_register_owned_env()` is passed to:

```text
call js_env_rehome_scalars, owned_env
```

The purpose is to move any wide scalar payload referenced by a closure,
generator, async state, or scope environment out of the callee's reclaimed
number extent. The sample owns no environment, so this call is absent from its
dump.

### 12.3 Dynamic scalar-return classifier

For an ordinary boxed function whose inferred return type is `ANY`, the current
epilogue emits this complete normalized sequence:

```text
unified_epilogue:
; [NEW:RETURN] capture current top and default to unchanged Item
mov scalar_top, i64:offsetof(Context, side_number_top)(side_rt)
mov scalar_payload, 0
mov scalar_result, js_return_value

; [NEW:RETURN] inline IEEE doubles cannot point into the number stack
and scalar_double, js_return_value, ITEM_DBL_MASK
bt  classify_done, scalar_double

; [NEW:RETURN] classify tagged scalar representations
ursh scalar_tag, js_return_value, 56
eq scalar_is_type, scalar_tag, LMD_TYPE_INT64
bt int64_case, scalar_is_type
eq scalar_is_type, scalar_tag, LMD_TYPE_FLOAT
bt float_tag_case, scalar_is_type
eq scalar_is_type, scalar_tag, LMD_TYPE_FLOAT64
bt float_tag_case, scalar_is_type
eq scalar_is_type, scalar_tag, LMD_TYPE_DTIME
bt payload_case, scalar_is_type
jmp classify_done

float_case:
and scalar_inline_double, js_return_value, ITEM_DBL_MASK
bt classify_done, scalar_inline_double
float_tag_case:
eq scalar_zero, js_return_value, ITEM_FLOAT_P0
bt classify_done, scalar_zero
eq scalar_zero, js_return_value, ITEM_FLOAT_N0
bt classify_done, scalar_zero
jmp payload_case

int64_case:
and scalar_inline_int64, js_return_value, ITEM_INT64_INLINE_MARK
bt classify_done, scalar_inline_int64

payload_case:
and scalar_payload, js_return_value, ITEM_PAYLOAD_MASK

classify_done:
uge scalar_ge_base, scalar_payload, number_frame
bf restore_number_top, scalar_ge_base
ult scalar_lt_top, scalar_payload, scalar_top
bt donate_scalar_slot, scalar_lt_top
jmp restore_number_top

donate_scalar_slot:
mov scalar_raw, i64:(scalar_payload)
mov i64:(number_frame), scalar_raw
and scalar_tag_bits, js_return_value, ITEM_HIGH_BYTE_MASK
or scalar_result, scalar_tag_bits, number_frame
add donated_top, number_frame, 8
mov i64:offsetof(Context, side_number_top)(side_rt), donated_top
jmp number_done

restore_number_top:
mov i64:offsetof(Context, side_number_top)(side_rt), number_frame

number_done:
mov js_return_value, scalar_result
```

Specialized `FLOAT`, `INT64`, or `DTIME` modes skip most tag dispatch. `NONE`
only restores the number watermark. Native-return functions also use the
restore-only path.

### 12.4 Root restoration and overflow

After scalar/environment work:

```text
; [NEW:RETURN] pop all callee roots
mov i64:offsetof(Context, side_root_top)(side_rt), root_frame
ret js_return_value

; [NEW:OVERFLOW] target of bind/capacity failures
stack_overflow:
call lambda_stack_overflow_error, "js-side-stack"
ret NULL_ITEM
```

The root restoration is emitted only when the function discovered at least one
root slot. The overflow return bypasses the normal epilogue because the frame
top has not been successfully published.

## 13. Compact complete reviewed `9402b169` listing

Using the exact `ROOT` expansion defined in section 3, the current sample is:

```text
[NEW] PROLOGUE(root_slots = 28, save number_frame)

[BASE] mov eval_local_frame, 0
[NEW] ROOT(a, 0); ROOT(b, 1); ROOT(callback, 2)
[NEW] ROOT(holder_tdz, 3)
[BASE] mov holder_tdz, JS_TDZ
[NEW] ROOT(sum_tdz, 4)
[BASE] mov sum_tdz, JS_TDZ

[NEW] PUBLISH_LIVE(b, a, holder_tdz, callback, sum_tdz)
[NEW] ROOT(a, 0); ROOT(b, 1)
[BASE] call js_add, add_result, a, b
[NEW] ROOT(add_result, 5)
[BASE] mov sum, add_result
[NEW] ROOT(sum, 6)

[NEW] PUBLISH_LIVE(b, a, holder_tdz, callback, sum)
[BASE] call js_new_object, new_object
[NEW] ROOT(new_object, 7)
[BASE] mov value_key, "value"
[NEW] PUBLISH_LIVE(b, a, holder_tdz, callback, sum)
[NEW] ROOT(new_object, 7); ROOT(value_key, 8); ROOT(sum, 6)
[BASE] call js_create_data_property, create_result, new_object, value_key, sum
[NEW] ROOT(create_result, 9)
[BASE] mov holder, new_object
[NEW] ROOT(holder, 10)

[BASE+NEW] every remaining helper call from section 6 is preserved and wrapped:
    PUBLISH_LIVE(b, a, holder, callback, sum)
    ROOT(each I64/P register operand, its slot)
    call helper
    ROOT(each I64/P result, a permanent slot)

[BASE] all argument stores, exception branches, property operations, and if labels
[NEW] assignments to rooted registers are followed by ROOT(register, slot)
[NEW] each old ret becomes mov js_return_value + jmp unified_epilogue

[NEW] DYNAMIC_SCALAR_RETURN_EPILOGUE from section 12.3
[NEW] restore side_root_top = root_frame
[NEW] ret js_return_value
[NEW] OVERFLOW_BLOCK from section 12.4
```

`PUBLISH_LIVE(...)` expands to one `ROOT` pair per listed variable. This compact
listing accounts for all 440 instructions: 58 baseline-body instructions,
161 root pairs (322 instructions), and the remaining prologue, unified-return,
scalar, restoration, and overflow instructions.

## 14. Historical review findings and latest disposition

The findings below describe `9402b169`. In the post-implementation design:

- R0 is resolved for precise release contexts; conservative discovery remains
  only in scanner-capable compatibility/debug builds;
- R1–R8 are resolved by semantic representation classes, import-effect
  classification, shared call-site/candidate recording, CFG liveness, dirty
  write-back, compact stable/scratch homes, initialized frames, and a
  write-through-only correctness oracle;
- R9 is addressed by late frame sizing and rootless-frame elision;
- R10 remains a separate opportunity to specialize genuinely dynamic boxed
  scalar return epilogues.

### R0 — The reviewed migration retained both old and new local-root discovery

**Severity:** high architectural and performance significance.

The reviewed collector still performed the conservative native-stack scan
after marking the new precise side-root region. Therefore that stack-frame
change added a second discovery path for ordinary JS locals rather than
replacing the first. The largest measured regression signal was the MIR needed
to keep side-root slots synchronized. The dense-root scan added collection work
while the old scan cost and false-retention behavior remained.

This dual state was appropriate during migration because C/C++ helper frames
and not-yet-audited generated paths still depended on conservative discovery.
The review required one of two explicit outcomes:

1. keep the conservative scan as a documented backstop, but make precise JS
   publication cheap through representation, safepoint, liveness, dirty-root,
   and slot-reuse analysis; or
2. prove every generated and native helper root path precise, then retire the
   conservative scan separately.

The implementation selected outcome 2 for precise Lambda/LambdaJS contexts and
retains outcome 1 only for explicitly compatible/debug builds; see §0.

### R1 — Call rooting is representation-blind and is the largest inflation source

**Severity:** high performance cost; conservative for correctness.

Lexical variables use TypeId-aware rooting, but call operands/results use only
`MIR_T_I64 || MIR_T_P`. Consequently booleans, integer counters, source IDs,
args-stack marks, debug results, immediate constants, and raw pointers become
GC root slots.

**Required review:** every MIR register needs a representation class such as
`NON_GC_SCALAR`, `BOXED_ITEM_MAY_HEAP`, `RAW_GC_POINTER`, or
`RAW_NON_GC_POINTER`; MIR machine type alone is insufficient.

### R2 — Every helper is treated as a GC safepoint

**Severity:** high performance cost.

Full publication is emitted for helpers that cannot allocate or collect, such
as exception-flag reads and truthiness/control helpers. A call-effect table
should identify actual GC-capable calls. Roots only need synchronization before
real safepoints.

### R3 — Live-scope publication and operand rooting duplicate stores

**Severity:** high performance cost.

When a live variable is also a call argument, it is stored once by
`jm_root_live_scope_vars()` and again by operand rooting. Existing-slot lookup
always stores, so the second pass is not a no-op.

### R4 — Root slots follow registers, not liveness

**Severity:** high code-size/runtime cost.

Slots grow monotonically. Superseded TDZ registers, temporary helper results,
and duplicate copies retain slots through function exit. A liveness pass should
assign/reuse slots only for values live across a safepoint.

### R5 — All lexical roots are republished, not only dirty roots

**Severity:** high runtime-store cost.

Unchanged parameters and locals are written before every helper call. Once a
slot contains the current value, another store is unnecessary until the
register changes. The emitter can maintain compile-time dirty state and flush
only dirty, live roots at a safepoint.

### R6 — Root lookup and update scans are linear

**Severity:** medium-to-high MIR-generation cost.

`jm_create_gc_root_slot()` linearly searches bindings, and
`jm_emit_root_updates()` linearly searches them after every instruction output.
At minimum, register-to-slot lookup should be direct. A liveness/safepoint pass
would remove most per-instruction synchronization entirely.

### R7 — Root policy exists in two call-emission paths

**Severity:** medium correctness/audit risk.

Runtime helper calls root manually around the shared emitter. Direct calls rely
on `jm_emit()` intercepting `MIR_CALL`/`MIR_JCALL`. Any call appended through a
third path can bypass the policy, and future fixes must update both existing
paths consistently.

### R8 — Initial root publication can precede register initialization

**Severity:** medium correctness invariant; low direct runtime value.

TDZ/predeclared registers were stored before their first `mov`. Reviewed safety
depends on there being no GC safepoint before the first full publication. The
ordering should be explicit and locally safe rather than dependent on that
global assumption.

### R9 — Frame work is not elided for functions with no relevant state

**Severity:** medium fixed cost, especially for tiny functions.

Zero-root/native functions still resolve/bind the runtime, save the number
watermark, redirect returns, and emit overflow machinery. A late frame-analysis
decision could select:

- no frame;
- root-only frame;
- number-only frame;
- root + number frame.

### R10 — Dynamic scalar classification is substantial but secondary

**Severity:** medium code-size cost.

Boxed `ANY` returns receive the full classifier. Return inference should select
specialized or `NONE` modes wherever possible. Controlled comparison against
the exact pre-scalar parent showed that the recent inline scalar-return change
added about 7.9% MIR to the 18-function probe but did not cause the large
runtime regression. Root publication remains the dominant target.

## 15. Recommended target lowering — implemented

The post-implementation emitter implements this target: semantic
representation classes, classified safepoints, CFG liveness, compact stable
and scratch homes, dirty write-back, delayed result publication, and frame
elision are owned by the shared `MirEmitter`. The historical recommendation
was:

1. classify each MIR register by GC representation, not just machine type;
2. classify imported/direct calls by whether they are GC safepoints;
3. compute values live across each safepoint;
4. assign reusable root slots to those live intervals;
5. mark a root dirty only when its defining register changes;
6. flush only dirty live roots immediately before a real safepoint;
7. do not root call results until a later safepoint actually requires them;
8. elide unused root/number frames after whole-function analysis;
9. retain the unified epilogue only where number restoration, owned-env
   re-homing, or multiple-return cleanup requires it.

For the sample, the essential roots at the dynamic callback safepoint are
approximately `callback`, `holder`, and any boxed `sum` value whose runtime
representation may reference managed storage. Parameters `a`/`b` are no longer
live there. Args-buffer storage is independently rooted. Exception flags,
truthiness flags, source IDs, args marks, and immediate constants require no GC
slots.

## 16. Review and verification checklist — implemented

These invariants are now permanent verification requirements rather than an
unimplemented checklist:

- GC can only occur at a known set of generated-call safepoints, or document
  any asynchronous collection mechanism that invalidates this assumption.
- A boxed `ANY` value is rooted when live across a safepoint even if its current
  execution happens to contain an immediate scalar.
- Raw environment pointers are represented and traced correctly.
- The args stack remains independently registered and valid across nested calls.
- Exception, early-return, `try/finally`, generator, and async paths all restore
  watermarks exactly once.
- Returned wide scalars obey capture/donate-before-restore semantics.
- Owned closure/generator/async environments contain no pointer into a reclaimed
  number extent.
- Root-slot reuse never lets a stale slot keep an unrelated object alive or
  drop a still-live object.

Implemented validation gates:

1. focused MIR golden/count test for this probe;
2. forced GC at every declared safepoint;
3. `regression_side_stack_frame_gc.js` under ASan/forced GC;
4. Lambda baseline and JS gtests;
5. full Test262 baseline with phase timing;
6. release-only repeated performance comparison against the direct pre-change
   binary, including MIR instruction volume and per-phase compile/link time.

## 17. Source map

Pre-change design evidence at `5043b95690ff`:

- `lambda/lambda-mem.cpp`
  - `heap_gc_collect()` flushes callee-saved registers with `setjmp()`, obtains
    the native stack bounds, and invokes collection with the active shared
    side-root region;
- `lib/gc/gc_heap.c`
  - `gc_collect_with_root_region()` marks registered slots/ranges, the active
    side-root region, extra roots, and the conservative native stack before
    precise heap tracing;
- `lambda/js/js_runtime_function.cpp`
  - `js_args_push()` registers the fixed-capacity argument stack;
  - `js_alloc_env()` pool-allocates each closure environment and permanently
    registers it as a root range;
- `lambda/js/js_runtime_state.cpp`
  - module variables and runtime singleton Items are explicitly registered;
- `lambda/js/js_mir_*`
  - no per-function side-root prologue, root publication, number watermark, or
    unified cleanup epilogue was emitted.

Reviewed `9402b169` implementation entry points:

- `lambda/js/js_mir_hashmap_scope_utils.cpp`
  - root slot store/allocation: `jm_store_gc_root_slot`,
    `jm_create_gc_root_slot`;
  - lexical predicate/publication: `jm_should_gc_root_var`,
    `jm_root_live_scope_vars`;
  - after-instruction update scan: `jm_emit_root_updates`;
  - direct-call operand scan: `jm_root_call_insn_regs`;
  - frame lifecycle: `jm_begin_function_frame`,
    `jm_finalize_side_root_prologue`, `jm_finish_function_frame`;
  - return/call interception: `jm_emit`.
- `lambda/js/js_mir_calls_boxing_types.cpp`
  - runtime-helper rooting: `jm_call_with_args`,
    `jm_call_void_with_args`, `jm_call_0` through `jm_call_6`.
- `lambda/js/js_mir_function_class_lowering.cpp`
  - ordinary/native/generator/async function frame entry and finish.
- `lambda/js/js_mir_module_batch_lowering.cpp`
  - `js_main` frame entry and finish.
- `lambda/js/js_mir_function_collection_class_inference.cpp`
  - call argument-stack construction in `jm_build_args_array`.
- `lambda/js/js_mir_statement_lowering.cpp`
  - ordinary JS return lowering and exception propagation.
- `lambda/mir_emitter_shared.hpp`
  - root/top stores and scalar return re-homing.
- `lib/side_stack.c`, `lib/side_stack.h`
  - reservation, bind, ensure, snapshot, restore, and decommit.
- `lambda/lambda-mem.cpp`
  - GC collection passes the active side-root region to the collector.
- `lambda/lambda.h`
  - `Context` side-stack fields and Item representation helpers.

Post-implementation entry points at `638e11c93`:

- `lambda/mir_emitter_shared.hpp`
  - semantic root candidates and call sites;
  - CFG liveness, dirty-state propagation, scratch coloring, OOM fallback,
    and `em_finalize_semantic_root_write_back()`;
- `lambda/js/js_mir_hashmap_scope_utils.cpp`
  - stable semantic-home installation, effect-aware call-site recording,
    write-back finalization, frame sizing, and slot/store diagnostics;
  - the eager write-through behavior retained only as the correctness oracle;
- `lambda/js/js_mir_calls_boxing_types.cpp`
  - helper calls routed through import-effect and representation metadata;
- `lambda/js/js_mir_function_class_lowering.cpp` and
  `lambda/js/js_mir_module_batch_lowering.cpp`
  - function and module frame integration with the shared finalizer;
- `lambda/lambda-mem.cpp` and `lib/gc/gc_heap.c`
  - precise-only admission and scanner-independent release collection, with
    conservative scanning confined to compatible/debug builds;
- `utils/check_gc_effects.py` and `utils/check_gc_root_hazards.py`
  - transitive `NO_GC` enforcement and native-root hazard enforcement;
- `vibe/Lambda_Design_Stack_Rooting.md` §12
  - cross-tier post-implementation measurements and artifact list.

## 18. Historical reproduction record

The reviewed side-stack and pre-stack MIR were generated from the same source
probe with debug binaries solely because detailed MIR dumping is debug-gated:

```bash
JS_MIR_DUMP=1 LAMBDA_DISABLE_MIR_CACHE=1 \
    ./lambda.exe js ./temp/stack_mir_review_probe.js
```

The pre-stack dump was built from detached commit `5043b95690ff`. The main
workspace release binary was preserved before debug dumping and restored
byte-for-byte afterward. Performance figures in this report came only from
release builds; debug builds were not used for timing.
