# Lambda MIR-Direct stack-frame and rooting review

**Status:** historical implementation review through `9402b169`, updated with
the post-implementation safepoint write-back result

**Reviewed:** 2026-07-16

**Post-implementation profile:** 2026-07-19 at `638e11c93`

**Stack-frame implementation commit:** `0ee5814fa72afb6b813a4988aeca8de571d9dcd4`

**Historical side-stack reviewed tree:** `9402b16980e819dbd2feae1763e2cade5d327244`

**Exact pre-change baseline:** `5717d36fb3bd80c9eaaea805d6bf2b19d616de79`, the direct parent of the stack-frame change

**Companion report:** [Lambda_Stack_JS_MIR.md](./Lambda_Stack_JS_MIR.md)

This report documents how the **core Lambda MIR-Direct runtime** represents a
function frame, protects GC-managed values, owns wide scalar payloads, performs
calls, and returns values. Its original body compares the exact implementation
immediately before commit `0ee5814f` with the side-stack implementation at
`9402b169`. Section 0 adds the latest implemented rooting result. Unless a
later section explicitly says post-implementation, “Current” in the original
review means `9402b169`, not repository `HEAD`.

The word *frame* is overloaded in this area. This report distinguishes:

1. the native/MIR activation record managed by MIR and the platform ABI;
2. the GC root frame, which holds live `Item` values for the collector;
3. the wide-scalar storage frame, which owns out-of-line `int64`, `float`, and
   `DateTime` payloads;
4. asynchronous spill/activation storage, whose lifetime can exceed the native
   call that created it.

The new stack-frame change primarily replaces items 2 and 3. It does not replace
MIR's native activation record.

---

## 0. Post-implementation result — 2026-07-19

The latest production implementation uses safepoint-current canonical slots:
Lambda MIR-Direct reports semantic root candidates and classified call sites
to the shared `MirEmitter`, which performs CFG liveness, dirty write-back,
scratch coloring, compact slot assignment, and final store insertion. Ordinary
production lowering no longer constructs the eager rooting oracle.

For the identical `_frame_review_0` probe used throughout this report:

| Metric | Pre-side-stack `5717d36f` | Reviewed side-stack `9402b169` | Post-implementation write-back `638e11c93` | Reviewed → post change |
|---|---:|---:|---:|---:|
| Executable MIR instructions | 90 | 138 | **101** | **−37 (−26.8%)** |
| MIR body entries, including labels | 96 | 155 | **118** | **−37 (−23.9%)** |
| MIR locals | 60 | 80 | **47** | **−33 (−41.3%)** |
| MIR calls | 45 | 10 | **11** | +1 |
| Root slots | 16 | 16 | **12** | **−4 (−25.0%)** |

The post-implementation function is only 11 instructions (12.2%) above the
90-instruction pre-side-stack version. The latter was not root-free: it made
36 heap-root management calls. The latest implementation keeps precise direct
side-root publication without those calls.

The same current compiler also provides a write-through correctness oracle:

| Current `638e11c93` mode | Executable instructions | Body entries | Locals | Calls | Root slots |
|---|---:|---:|---:|---:|---:|
| Write-through oracle | 141 | 158 | 65 | 11 | 34 |
| Production write-back | **101** | **118** | **47** | 11 | **12** |
| Reduction | **−40 (−28.4%)** | −40 | −18 | unchanged | **−22 (−64.7%)** |

This same-compiler comparison isolates publication policy. The current
write-through oracle is not byte-for-byte the historical `9402b169` emitter;
it shares the new semantic-candidate and canonical-slot infrastructure and
changes only the publication mode.

Collector status has also changed since the original review. Scanner-
independent Lambda/LambdaJS release builds now use precise roots without
native-stack discovery or `gc_scan_stack()`. Scanner-capable debug builds and
builds containing unchanged C2MIR retain the conservative scanner only for
shadow verification or sticky compatibility contexts.

Captured evidence:

- production MIR: `temp/lambda_mir_latest_writeback.txt`;
- write-through MIR: `temp/lambda_mir_latest_writethrough.txt`;
- slot diagnostics: `temp/lambda_mir_latest_writeback.log` and
  `temp/lambda_mir_latest_writethrough.log`;
- full cross-tier comparison and measurement rules:
  `vibe/Lambda_Design_Stack_Rooting.md` §12.

The current release Test262 baseline completes all 42,889 tests in 160.6s with
40,261/40,261 fully passing, zero non-fully-passing tests, zero failures, zero
regressions, and zero retry time. This is current acceptance evidence; it is
not used to attribute runtime change solely to this MIR reduction because the
historical source and harness snapshots differ.

---

## 1. Executive conclusion

The core Lambda change is not the same MIR expansion seen in the JS compiler.
Core Lambda already emitted explicit root stores and root reloads before the
change. The implementation changed their backing mechanism:

```text
before: MIR call -> heap_jit_gc_root_frame_set/get -> heap block
after:  MIR mov  -> [root_frame + constant offset] -> side root stack
```

For the representative function used in this review:

| Historical metric for `_frame_review_0` | Before | Reviewed `9402b169` | Change |
|---|---:|---:|---:|
| Executable MIR instructions | 90 | 138 | +48, or +53.3% |
| MIR body entries, including labels | 96 | 155 | +59, or +61.5% |
| MIR locals | 60 | 80 | +20 |
| All MIR `call` instructions | 45 | 10 | -35, or -77.8% |
| Semantic/runtime calls on the normal path | 9 | 9 | unchanged |
| Root slots | 16 | 16 | unchanged |
| Root-management calls | 36 | 0 | removed |
| Cold side-stack overflow calls in the function body | 0 | 1 | added |

The additional MIR instructions are mainly:

- a checked, exact-size side-root prologue;
- a unified epilogue;
- boxed-scalar return ownership classification and frame-slot donation;
- explicit names/registers around direct root loads.

They are **not** caused by publishing every live root after every instruction.
The core Lambda emitter rooted the same temporary values before the new frame.
The reviewed `9402b169` implementation retains the 16 root slots in this
example, but turns 36 hot C helper calls into direct memory operations.

### Migration boundary: removed old mechanisms versus retained old mechanisms

> **[REMOVED OLD FRAME ROOTING] The heap-backed `JitGcRootFrame` implementation
> is completely removed. It is not running alongside the side root stack.**

| Removed old mechanism | Previous role | Reviewed replacement |
|---|---|---|
| Thread-local linked `JitGcRootFrame` stack and frame cache | Represented nested JIT root frames | Root/number watermarks in `Context` |
| Heap-allocated 64-slot `JitGcRootBlock` chains | Stored each function's rooted values | Exact contiguous slots in the side root stack |
| `heap_jit_gc_root_frame_enter()` / `exit()` | Pushed and popped heap root frames | Save/reserve/restore the side-root watermark in MIR |
| `heap_jit_gc_root_frame_set()` / `get()` | Published and reloaded individual roots through C calls | Direct MIR loads/stores at `[root_frame + constant offset]` |
| Per-collection JIT-root block registration and contiguous snapshot | Copied active heap-frame roots into collector inputs | Direct scan of `[side_root_base, side_root_top)` |
| Evaluation-lifetime `gc_nursery` numeric storage | Owned out-of-line `int64`, float, and `DateTime` payloads | Scoped number side stack plus return-slot donation |
| Broad scalar-inclusive root predicate | Mixed GC reachability with numeric payload lifetime | Separate GC-root and number-frame ownership rules |

There are no current `heap_jit_gc_root_frame_*` calls or heap JIT root blocks.
Consequently, the normal MIR path does not pay both the old helper-call rooting
cost and the new direct side-root-store cost.

> **[HISTORICAL AT `9402b169`: RETAINED OLD ROOTING] The reviewed collector
> still used registered roots and conservatively scanned the native stack in
> addition to scanning the new precise side root stack. This is no longer the
> normal scanner-independent release configuration; see §0.**

| Retained old mechanism | Reviewed use | Expected status at `9402b169` |
|---|---|---|
| Registered individual root slots | Stable runtime roots such as `heap->result_root` | Retained precise mechanism; not inherently transitional |
| Registered root ranges | Closure environments and other stable contiguous `Item` storage | Retained precise mechanism; not inherently transitional |
| `setjmp` register flush before collection | Makes register-held candidates visible to the native-stack scan | Transitional with conservative scanning |
| Native stack-bound discovery | Defines the range passed to `gc_scan_stack()` | Transitional with conservative scanning |
| `gc_scan_stack()` conservative scan | Protects C2MIR and host/native values not yet covered by a complete precise-root contract | **Still active on every collection; eventual retirement target** |

The reviewed `9402b169` collector root equation was therefore:

```text
reviewed roots
    = registered slots/ranges       # retained precise roots
    + [side_root_base, top)         # new precise MIR roots
    + conservative native stack     # retained old compatibility roots
```

Thus the reviewed runtime was hybrid at the **collector boundary**, but not at
the old JIT-frame implementation boundary. The same object could be discovered
through a precise root and the conservative stack; marking it twice was safe,
but the conservative pass cost scan time and could retain false-positive
objects. In the latest implementation this hybrid path is confined to
scanner-capable debug/compatibility builds; precise release contexts use the
scanner-independent root equation documented in §0.

The historical review claimed no controlled release-build performance A/B; its
MIR captures were debug-build inspection artifacts. Section 0 now records the
post-implementation structural counts and current release acceptance result,
while deliberately avoiding a causal timing comparison across changed source
and harness snapshots.

---

## 2. Review probe and notation

The same Lambda source was compiled at both revisions:

```lambda
fn frame_review(a: string, b: string) {
    let combined = a ++ b
    let holder = {value: combined}
    let values = [holder.value, combined]
    join(values, ":")
}

frame_review("left", "right")
```

This small function intentionally includes:

- pointer arguments;
- pointer-to-`Item` boxing;
- a function call returning a GC-managed string;
- `let` bindings;
- map allocation and fill;
- member access;
- array construction and mutation;
- a final boxed return;
- a generated typed wrapper; and
- the module `main` function.

Both executions produced:

```text
"leftright:leftright"
```

The exact captured dumps are:

- `temp/lambda_mir_pre.txt`
- `temp/lambda_mir_current.txt`

The listings below use these labels:

- **[OLD: HEAP-ROOT]** — removed heap-backed root-frame behavior;
- **[NEW: ROOT-PROLOGUE]** — new precise side-root reservation;
- **[NEW: INLINE-ROOT]** — new direct side-root load/store;
- **[NEW: NUMBER-FRAME]** — new scoped wide-scalar storage;
- **[NEW: RETURN]** — new return-lane and scalar-ownership handling;
- **[RETAINED: CONSERVATIVE]** — old conservative native-stack scan still active;
- **[BASE]** — semantics common to both versions.

For compact lossless listings, these macros are used:

```text
OLD_ROOT_STORE(slot, value):
    mov  root_bits, value
    call heap_jit_gc_root_frame_set(slot, root_bits)

OLD_ROOT_LOAD(dst, slot):
    call heap_jit_gc_root_frame_get(dst, slot)

NEW_ROOT_STORE(slot, value):
    mov  root_bits, value
    mov  i64:(slot * 8)(root_frame), root_bits

NEW_ROOT_LOAD(dst, slot):
    mov  dst, i64:(slot * 8)(root_frame)
```

The current dump happens to show concrete `Context` byte offsets such as `80`,
`96`, and `112`. The emitter does not hard-code those numbers: it uses
`offsetof(Context, ...)`. They must be read as field accesses, not as a stable
public ABI.

---

## 3. Before the stack-frame change

### 3.1 Native activation versus GC root frame

MIR already created a native activation record for registers, spills, return
addresses, and ABI state. GC protection was separate. Each ordinary lowered
Lambda function body and module entry using the frame emitter emitted:

```text
call heap_jit_gc_root_frame_enter()
...
call heap_jit_gc_root_frame_exit()
ret result
```

This happened even when such a body eventually used no root slot. Small
generated conversion wrappers were emitted by a separate path and did not
enter this frame machinery.

The runtime maintained a thread-local linked stack of:

```c
JitGcRootFrame {
    JitGcRootFrame* prev;
    JitGcRootBlock* blocks;
    int64_t depth;
}

JitGcRootBlock {
    int64_t block_index;
    uint64_t slots[64];
    JitGcRootBlock* next;
}
```

Each block held 64 `Item`-sized slots. A frame was obtained from a thread-local
cache or allocated. Blocks were allocated lazily on the first write into each
64-slot interval. Looking up a slot walked the frame's linked block list.

Consequences:

- every generated function paid `enter` and `exit` C calls;
- every root write paid a `set` C call;
- every root read paid a `get` C call;
- the first write in a block could allocate memory;
- each block registered all 64 entries even if only one was used;
- block lookup was a linked-list walk;
- exit unregistered and freed blocks, while frame headers were cached up to 256.

### 3.2 Old root predicate

Before `0ee5814f`, the root predicate was effectively:

```text
root if MIR type is MIR_T_P
or if TypeId >= LMD_TYPE_INT64 and TypeId != LMD_TYPE_FLOAT
```

That broad rule mixed two different concepts:

- GC reachability: an `Item` or pointer may refer to a GC object; and
- scalar ownership: an `Item` may refer to out-of-line numeric payload storage.

It therefore treated some wide scalar forms as GC roots even though their
payload memory was owned by the numeric nursery rather than traced by the GC.

### 3.3 Old numeric nursery

Wide numeric payloads used `gc_nursery`:

- 32 KiB linked allocation blocks;
- bump allocation for `int64`, `double`, and `DateTime` payloads;
- stable addresses;
- no function watermark and no frame reset;
- payloads lived until `gc_nursery_destroy()`.

This avoided dangling payload pointers, but retained every wide numeric
temporary for the entire evaluation lifetime. It also provided no direct model
for transferring a callee-created scalar payload to its caller.

### 3.4 Old collection sequence

At collection time, `heap_gc_collect()` did all of the following:

1. used `setjmp` to spill callee-saved registers;
2. found the native-stack bounds;
3. registered every live heap root block as a 64-slot range;
4. copied every live block into a temporary contiguous `jit_roots` snapshot;
5. passed that snapshot as explicit extra roots;
6. marked registered root slots;
7. marked registered root ranges;
8. marked the copied extra roots;
9. conservatively scanned the native stack;
10. traced the object graph.

The JIT root blocks were consequently visible through registered ranges and a
second snapshot. The native stack was scanned in addition.

### 3.5 Problems in the old design

| ID | Problem | Effect |
|---|---|---|
| OLD-R1 | Four C helpers implemented frame/root traffic | Hot call overhead and ABI traffic |
| OLD-R2 | Rootless frame-enabled bodies still entered/exited | Fixed per-call cost |
| OLD-R3 | Roots allocated in 64-slot heap blocks | Allocation, registration, zeroing, and unused scanning |
| OLD-R4 | Block lookup walked a list | Cost increased with slot block index |
| OLD-R5 | Collection copied active roots | Per-GC allocation and copying |
| OLD-R6 | Broad predicate mixed GC roots and scalar payloads | Imprecise model and unnecessary roots |
| OLD-N1 | Numeric nursery had evaluation lifetime | Wide-scalar temporaries accumulated |
| OLD-N2 | No caller/callee ownership transfer | Return lifetime was implicit rather than frame-scoped |
| OLD-C1 | Conservative native scan remained authoritative | False retention and dependence on machine representations |

The old design was safe largely because it layered multiple protections. That
made it expensive and made it hard to determine which protection was actually
required for a particular value.

---

## 4. Reviewed `9402b169` frame and rooting design

### 4.1 Two side stacks

Each runtime `Context` now exposes two watermark-based regions:

```text
side_root_base          side_number_base
side_root_top           side_number_top
side_root_commit_limit  side_number_commit_limit
side_root_limit         side_number_limit
```

The backing regions are thread-local virtual-memory reservations:

| Region | Reservation | Contents | Scanned by GC? |
|---|---:|---|---|
| Side root stack | 16 MiB | Exact live `Item` roots | Yes, `[base, top)` |
| Side number stack | 64 MiB | Raw wide-scalar payload words | No |

On POSIX, `mmap` supplies demand-paged read/write memory. On Windows, address
space is reserved first and pages are committed as a watermark advances. After
GC, unused pages above the live watermarks may be decommitted/advised away.

### 4.2 Function watermark discipline

A generated function conceptually performs:

```text
root_base   = context.side_root_top
number_base = context.side_number_top

reserve exact root slots
reserve fixed error-return scratch if required

execute body

restore context.side_root_top = root_base
transfer or restore number-frame ownership
return
```

The emitter does not know its final root count when it starts lowering the
function. It emits an anchor, allocates monotonically numbered slots while
lowering, then inserts the checked prologue at the anchor after the final count
is known.

If the final count is zero, no side-root reservation is inserted.

### 4.3 Reviewed `9402b169` root predicate

The precise root predicate is now:

```text
MIR_T_P
or TypeId in {
    decimal, symbol, string, binary, path, range,
    array_num, array, map, vmap, element, object,
    type, func, any, error
}
```

`int64`, `float`, and `DateTime` no longer enter the GC root stack merely
because their `Item` contains a pointer-shaped payload. They belong to the
number-frame ownership model. This is the important semantic separation:

```text
GC root stack     = values that keep traced GC objects reachable
number side stack = untraced storage that keeps wide scalar payload bytes alive
```

### 4.4 Reviewed `9402b169` collector root order

At `9402b169`, `heap_gc_collect()` supplied the collector with:

1. registered individual roots such as stable result slots;
2. registered ranges such as closure environment arrays;
3. the dense exact side-root region `[side_root_base, side_root_top)`;
4. optional explicit extra roots;
5. **[RETAINED: CONSERVATIVE] the native stack range**;
6. recursively traced children of marked objects.

The removed and retained mechanisms are therefore:

| Mechanism | Before | Reviewed `9402b169` |
|---|---:|---:|
| Heap `JitGcRootFrame` stack | Yes | **Removed** |
| 64-slot JIT root blocks | Yes | **Removed** |
| `heap_jit_gc_root_frame_enter/set/get/exit` | Yes | **Removed** |
| Per-GC JIT-root snapshot | Yes | **Removed** |
| Precise side-root range | No | **New** |
| Registered stable root slots/ranges | Yes | Retained |
| Conservative native-stack scan | Yes | **Retained** |

The reviewed implementation therefore did **not** keep both the old heap root
frame and the new side root stack. It kept the new side root stack and the
older conservative scan.

---

## 5. Reviewed `9402b169` function prologue

For `_frame_review_0`, lowering allocated exactly 16 root slots. The reviewed
prologue is:

```text
# [BASE] runtime/module setup
mov runtime, i64:(_lambda_rt)
mov consts, i64:(_mod_consts_ptr)
mov type_list, i64:(_mod_type_list_ptr)
mov heap_ptr, i64:offsetof(EvalContext, heap)(runtime)
mov gc_ptr, i64:offsetof(Heap, gc)(heap_ptr)

# [NEW: ROOT-PROLOGUE] reserve 16 * 8 bytes
mov root_frame, i64:offsetof(Context, side_root_top)(runtime)
add root_top, root_frame, 128
mov root_limit, i64:offsetof(Context, side_root_limit)(runtime)
ugt root_overflow, root_top, root_limit
bt  root_overflow_label, root_overflow
mov i64:offsetof(Context, side_root_top)(runtime), root_top

# [NEW: NUMBER-FRAME] save the entry watermark
mov number_frame, i64:offsetof(Context, side_number_top)(runtime)
```

The side-root overflow edge is cold:

```text
root_overflow_label:
    call lambda_stack_overflow_error("side-stack")
    ret ITEM_ERROR
```

On Windows, the inserted prologue additionally checks the commit watermark and
calls `lambda_side_stack_ensure()` only when newly reserved pages must be
committed.

The current prologue has these useful properties:

- one load of the old root watermark;
- one exact-size reservation per function activation;
- one bounds check;
- no heap allocation on the normal path;
- no per-slot registration;
- no `enter` helper call;
- complete elision when the function allocates zero root slots.

---

## 6. What occupies the representative function's 16 root slots

The before and after versions allocate the same logical slots:

| Slot | Byte offset | Protected value | Reason |
|---:|---:|---|---|
| 0 | 0 | raw pointer argument `a` | string pointer must survive calls |
| 1 | 8 | raw pointer argument `b` | string pointer must survive calls |
| 2 | 16 | boxed `a` | argument to `fn_join` |
| 3 | 24 | boxed `b` | argument to `fn_join` |
| 4 | 32 | result of `fn_join` | newly allocated string `Item` |
| 5 | 40 | `combined` binding | live across map/array calls |
| 6 | 48 | map field value temporary | live across map allocation/fill |
| 7 | 56 | new map | map must survive fill |
| 8 | 64 | `holder` binding | map used by member access |
| 9 | 72 | array builder | live across pushes and finalization |
| 10 | 80 | member object temporary | argument to `fn_member` |
| 11 | 88 | member key `value` | argument to `fn_member` |
| 12 | 96 | member result | newly returned `Item` |
| 13 | 104 | array item temporary | first `array_push` argument |
| 14 | 112 | `combined` array item temporary | second `array_push` argument |
| 15 | 120 | finalized `values` array | argument to final `join` |

The slot allocator is monotonic within a function. It does not currently reuse
slot 0 after argument `a` becomes dead, for example. That makes the frame layout
simple and statically addressable, but it means the reservation reflects the
number of rooted definitions/temporaries rather than peak simultaneously-live
roots.

---

## 7. Root stores and reloads before and after

### 7.1 Pointer argument

Before:

```text
mov  root_bits, _a
call heap_jit_gc_root_frame_set(0, root_bits)
```

Reviewed `9402b169`:

```text
mov root_bits, _a
mov i64:0(root_frame), root_bits
```

### 7.2 Reloading before boxing

Before:

```text
call heap_jit_gc_root_frame_get(var_live, 0)
bt   non_null, var_live
...
```

Reviewed `9402b169`:

```text
mov var_live, i64:0(root_frame)
bt  non_null, var_live
...
```

### 7.3 Rooting a returned object

Before:

```text
call fn_join(result, left, right)
mov  root_bits, result
call heap_jit_gc_root_frame_set(4, root_bits)
call heap_jit_gc_root_frame_get(join_rv, 4)
```

Reviewed `9402b169`:

```text
call fn_join(result, left, right)
mov  root_bits, result
mov  i64:32(root_frame), root_bits
mov  join_rv, i64:32(root_frame)
```

The number of explicit root stores/reloads is approximately unchanged. Their
runtime cost is not: a direct base-plus-constant memory operation replaces each
helper call and its frame/block lookup.

### 7.4 This is not "scan root bindings after instructions"

The generated function does not execute a separate loop that scans all local
bindings after each instruction. Instead, lowering assigns a fixed slot when a
particular value needs protection and emits a direct store at that point.

At GC time, the collector scans the dense live range once:

```text
for slot in [context.side_root_base, context.side_root_top):
    gc_mark_item(*slot)
```

The phrase *root bindings after instructions* should therefore be read as
"lowering emits root stores beside the instructions that produce live GC
values," not as "the runtime rescans all bindings after every MIR instruction."

---

## 8. Calls, arguments, and call results

Core Lambda MIR calls retain the standard shape:

```text
call <prototype>, <import/function>, <optional result>, <arguments...>
```

The frame change does not introduce a generic pre-call publication pass. Each
lowering path protects operands according to its own lifetime needs, then loads
the call operands from fixed root slots when necessary.

For example, concatenation is emitted as:

```text
NEW_ROOT_LOAD(join_l, 2)
NEW_ROOT_LOAD(join_r, 3)
call fn_join(result, join_l, join_r)
NEW_ROOT_STORE(4, result)
```

Map construction is:

```text
NEW_ROOT_STORE(6, combined)
call map_with_tl(map, 0, type_list)
NEW_ROOT_STORE(7, map)
NEW_ROOT_LOAD(map_value, 6)
call map_fill(filled, map, map_value)
NEW_ROOT_STORE(8, filled)
```

Array construction is:

```text
call array(builder)
NEW_ROOT_STORE(9, builder)

NEW_ROOT_LOAD(item0, 13)
NEW_ROOT_LOAD(builder0, 9)
call array_push(builder0, item0)

NEW_ROOT_LOAD(item1, 14)
NEW_ROOT_LOAD(builder1, 9)
call array_push(builder1, item1)

NEW_ROOT_LOAD(builder2, 9)
call array_end(values, builder2)
NEW_ROOT_STORE(15, values)
```

The root slot protects an object across any nested allocation/collection within
the called runtime helper. The MIR register is still used to pass the immediate
ABI argument.

### Call-count accounting for the probe

Before the change, `_frame_review_0` emitted:

```text
 1 root-frame enter
16 root stores
18 root loads
 1 root-frame exit
 9 semantic/runtime calls
--
45 calls total
```

Reviewed `9402b169`:

```text
 0 root-management calls
 9 semantic/runtime calls on the normal path
 1 cold overflow-reporting call
--
10 calls present in MIR
```

Thus the current function has more MIR instructions but dramatically fewer
runtime calls.

---

## 9. Variables, assignment, and rooting policy

### 9.1 A MIR local is not automatically a GC root

`new_reg()` creates a MIR virtual register and records it for async spill
analysis when relevant. It does not by itself make that register visible to the
collector.

`set_var()` and expression-specific lowering decide whether to allocate/store a
root slot by calling `should_gc_root_var()` or a related explicit root helper.
The key cases are:

| Value representation | Root side stack? | Number side stack? |
|---|---:|---:|
| Packed null/bool/small integer | No | No |
| Raw pointer to GC object (`MIR_T_P`) | Yes | No |
| Boxed string/map/array/etc. `Item` | Yes | No |
| Boxed `int64`/float/DateTime with out-of-line payload | No | Yes |
| Native MIR integer/double | No | No, unless later boxed out-of-line |
| Async-owned value | Stored in async activation | Avoid redundant side root |

### 9.2 Bindings can copy between root slots

The representative function roots the call result in slot 4 and then the
`combined` binding in slot 5:

```text
NEW_ROOT_STORE(4, fn_join_result)
NEW_ROOT_LOAD(join_rv, 4)
mov letv, join_rv
NEW_ROOT_STORE(5, letv)
```

This is semantically safe but exposes an optimization opportunity: a liveness-
aware allocator could let the binding own the existing slot rather than allocate
a second monotonically numbered slot. Such reuse must be proven against every
safepoint and alias/lifetime transition; it should not be implemented as an
ad-hoc removal of stores.

### 9.3 Async variables

An async function may suspend after its native activation returns. Values that
must survive suspension are written to the async activation/spill structure.
When a variable has an `async_slot`, the current emitter avoids also treating
the side-root slot as its durable owner. Async lifetime is controlled by the
activation object, not by a native function watermark that is restored on
return/suspension.

---

## 10. Wide scalar ownership before and after

### 10.1 Why wide scalars need storage

An `Item` is 64 bits. Some scalar values fit inline; others encode a type tag and
a pointer to an 8-byte payload. The payload is not a GC object merely because
the `Item` contains a pointer-shaped bit pattern.

Before the change, out-of-line payloads accumulated in the numeric nursery.
Current MIR-Direct code uses the number side stack so their lifetime follows
function watermarks.

### 10.2 Reviewed `9402b169` number frame

At entry, a function saves:

```text
number_frame = context.side_number_top
```

Wide scalar boxing allocates from `side_number_top` and advances it. On ordinary
exit the function restores the entry watermark unless it deliberately donates a
return payload slot to the caller.

The number side stack is **not** scanned by GC. Its words are raw payload bytes,
not candidate `Item` roots.

### 10.3 Boxed scalar return donation

A boxed/dynamic Lambda function may return any `Item`, so the epilogue must
classify the value:

1. detect whether the `Item` is an inline value, a GC object, or an out-of-line
   scalar type;
2. extract the scalar payload address when applicable;
3. test whether the payload lies in the callee's number extent
   `[number_frame, current_number_top)`;
4. if it does, copy the raw 8-byte payload into the callee's entry slot;
5. retag the `Item` to point at that entry slot;
6. set `side_number_top = number_frame + 1`, donating exactly one word to the
   caller;
7. otherwise restore `side_number_top = number_frame` unchanged.

This gives the caller ownership without a heap allocation and without retaining
all of the callee's other numeric temporaries.

The dynamic classifier recognizes the relevant boxed scalar encodings,
including compact/inline cases that require no donation. In the representative
function this classifier accounts for roughly fifty MIR instructions and most
of the observed code-size increase.

### 10.4 Native return lane

When type inference gives a native MIR return (`MIR_T_I64`, `MIR_T_D`, or a raw
pointer) and no raised-error lane is needed, the function uses `RETURN_LANE_NONE`:

- no dynamic boxed-scalar classifier;
- restore the number watermark;
- return the native value.

The generated `_frame_review_b_0` typed wrapper in the probe is only four MIR
instructions and remains identical across the two revisions:

```text
call it2s(a_ptr, a_item)
call it2s(b_ptr, b_item)
call _frame_review_0(result, a_ptr, b_ptr)
ret result
```

It has no side-root frame because conversion wrappers are emitted outside the
ordinary function-frame path. Separately, ordinary current functions that use
that path but finish with zero root slots are elided by
`finalize_side_root_frame()`.

---

## 11. Unified return and error ABI

All body returns now branch to one epilogue. The emitter records one of three
return-lane kinds:

| Lane | Used for | Epilogue behavior |
|---|---|---|
| `RETURN_LANE_NONE` | Native return without raised error | restore frames, return native value |
| `RETURN_LANE_SCALAR` | Dynamic/boxed `Item` return | restore roots, re-home/donate scalar payload, return `Item` |
| `RETURN_LANE_ERROR` | Native `T^E` result | stage native value, restore frames, publish error `Item` in `Context::mir_return_lane` |

### 11.1 Why one epilogue matters

Before the change, a normal tail looked like:

```text
call heap_jit_gc_root_frame_exit()
ret value
```

With two watermarks and return ownership, cleanup order is part of correctness.
One epilogue ensures that every normal/error branch observes the same order:

1. preserve return lanes;
2. restore the root watermark;
3. transfer or restore the number watermark;
4. publish the error lane when applicable;
5. return.

The root watermark is restored only after any cleanup that could collect while
the return value still needs protection.

### 11.2 Native `T^E` return

MIR's ARM64 multi-result lowering can lose the first lane when a nested call
feeds it. The current implementation therefore reserves one number-frame scratch
word for `RETURN_LANE_ERROR`:

```text
number_frame[0] = primary_native_result
restore root frame
restore number top
primary = number_frame[0]
context.mir_return_lane = error_item
ret primary
```

The scratch word is raw number-frame storage, not a GC root.

### 11.3 Boxed return in the probe

The probe's body ends as:

```text
call fn_join2(result, values, colon)
mov return_value, result
jmp unified_epilogue
```

The epilogue then:

```text
context.side_root_top = root_frame
classify/re-home return_value against number_frame
ret scalar_result
```

The returned string is a GC-managed object rather than a wide scalar, so the
classifier restores the number watermark and returns the original `Item`.

---

## 12. Complete normalized MIR: before

This is a lossless normalized listing of `_frame_review_0`; only register
suffixes and the root helper expansions have been compacted.

```text
func _frame_review_0(a: i64, b: i64) -> i64
    # [BASE] module/runtime state
    runtime   = *_lambda_rt
    consts    = *_mod_consts_ptr
    type_list = *_mod_type_list_ptr
    heap      = runtime.heap
    gc        = heap.gc

    # [OLD: HEAP-ROOT]
    call heap_jit_gc_root_frame_enter()

    OLD_ROOT_STORE(0, a)
    OLD_ROOT_STORE(1, b)

    OLD_ROOT_LOAD(a_live, 0)
    a_item = a_live ? (STRING_TAG | a_live) : EMPTY_STRING_ITEM
    OLD_ROOT_STORE(2, a_item)

    OLD_ROOT_LOAD(b_live, 1)
    b_item = b_live ? (STRING_TAG | b_live) : EMPTY_STRING_ITEM
    OLD_ROOT_STORE(3, b_item)

    OLD_ROOT_LOAD(join_l, 2)
    OLD_ROOT_LOAD(join_r, 3)
    call fn_join(join_result, join_l, join_r)
    OLD_ROOT_STORE(4, join_result)

    OLD_ROOT_LOAD(join_rv, 4)
    combined = join_rv
    OLD_ROOT_STORE(5, combined)

    OLD_ROOT_LOAD(map_value, 5)
    OLD_ROOT_STORE(6, map_value)
    call map_with_tl(map, 0, type_list)
    OLD_ROOT_STORE(7, map)
    OLD_ROOT_LOAD(map_value, 6)
    call map_fill(holder_value, map, map_value)
    holder = holder_value
    OLD_ROOT_STORE(8, holder)

    call array(array_builder)
    OLD_ROOT_STORE(9, array_builder)

    OLD_ROOT_LOAD(holder_live, 8)
    member_object = holder_live
    member_key = interned("value")
    OLD_ROOT_STORE(10, member_object)
    OLD_ROOT_STORE(11, member_key)
    OLD_ROOT_LOAD(member_object, 10)
    OLD_ROOT_LOAD(member_key, 11)
    call fn_member(member_result, member_object, member_key)
    OLD_ROOT_STORE(12, member_result)
    OLD_ROOT_LOAD(member_result, 12)
    OLD_ROOT_STORE(13, member_result)

    OLD_ROOT_LOAD(item0, 13)
    OLD_ROOT_LOAD(array_builder, 9)
    call array_push(array_builder, item0)

    OLD_ROOT_LOAD(combined_live, 5)
    OLD_ROOT_STORE(14, combined_live)
    OLD_ROOT_LOAD(item1, 14)
    OLD_ROOT_LOAD(array_builder, 9)
    call array_push(array_builder, item1)

    OLD_ROOT_LOAD(array_builder, 9)
    call array_end(values_result, array_builder)
    values = values_result
    OLD_ROOT_STORE(15, values)

    OLD_ROOT_LOAD(values_live, 15)
    colon_ptr = consts[0]
    colon_item = colon_ptr ? (STRING_TAG | colon_ptr) : EMPTY_STRING_ITEM
    call fn_join2(result, values_live, colon_item)

    # [OLD: HEAP-ROOT]
    call heap_jit_gc_root_frame_exit()
    ret result
endfunc
```

The old function has no frame-scoped numeric cleanup and no return donation.
The numeric nursery, if used by called scalar boxing paths, outlives the call.

---

## 13. Complete normalized MIR: reviewed `9402b169`

```text
func _frame_review_0(a: i64, b: i64) -> i64
    # [BASE] module/runtime state
    runtime   = *_lambda_rt
    consts    = *_mod_consts_ptr
    type_list = *_mod_type_list_ptr
    heap      = runtime.heap
    gc        = heap.gc

    # [NEW: ROOT-PROLOGUE] exact reservation, 16 slots
    root_frame = runtime.side_root_top
    root_top = root_frame + 128
    if root_top > runtime.side_root_limit: goto root_overflow
    runtime.side_root_top = root_top

    # [NEW: NUMBER-FRAME]
    number_frame = runtime.side_number_top

    NEW_ROOT_STORE(0, a)
    NEW_ROOT_STORE(1, b)

    NEW_ROOT_LOAD(a_live, 0)
    a_item = a_live ? (STRING_TAG | a_live) : EMPTY_STRING_ITEM
    NEW_ROOT_STORE(2, a_item)

    NEW_ROOT_LOAD(b_live, 1)
    b_item = b_live ? (STRING_TAG | b_live) : EMPTY_STRING_ITEM
    NEW_ROOT_STORE(3, b_item)

    NEW_ROOT_LOAD(join_l, 2)
    NEW_ROOT_LOAD(join_r, 3)
    call fn_join(join_result, join_l, join_r)
    NEW_ROOT_STORE(4, join_result)

    NEW_ROOT_LOAD(join_rv, 4)
    combined = join_rv
    NEW_ROOT_STORE(5, combined)

    NEW_ROOT_LOAD(map_value, 5)
    NEW_ROOT_STORE(6, map_value)
    call map_with_tl(map, 0, type_list)
    NEW_ROOT_STORE(7, map)
    NEW_ROOT_LOAD(map_value, 6)
    call map_fill(holder_value, map, map_value)
    holder = holder_value
    NEW_ROOT_STORE(8, holder)

    call array(array_builder)
    NEW_ROOT_STORE(9, array_builder)

    NEW_ROOT_LOAD(holder_live, 8)
    member_object = holder_live
    member_key = interned("value")
    NEW_ROOT_STORE(10, member_object)
    NEW_ROOT_STORE(11, member_key)
    NEW_ROOT_LOAD(member_object, 10)
    NEW_ROOT_LOAD(member_key, 11)
    call fn_member(member_result, member_object, member_key)
    NEW_ROOT_STORE(12, member_result)
    NEW_ROOT_LOAD(member_result, 12)
    NEW_ROOT_STORE(13, member_result)

    NEW_ROOT_LOAD(item0, 13)
    NEW_ROOT_LOAD(array_builder, 9)
    call array_push(array_builder, item0)

    NEW_ROOT_LOAD(combined_live, 5)
    NEW_ROOT_STORE(14, combined_live)
    NEW_ROOT_LOAD(item1, 14)
    NEW_ROOT_LOAD(array_builder, 9)
    call array_push(array_builder, item1)

    NEW_ROOT_LOAD(array_builder, 9)
    call array_end(values_result, array_builder)
    values = values_result
    NEW_ROOT_STORE(15, values)

    NEW_ROOT_LOAD(values_live, 15)
    colon_ptr = consts[0]
    colon_item = colon_ptr ? (STRING_TAG | colon_ptr) : EMPTY_STRING_ITEM
    call fn_join2(result, values_live, colon_item)
    return_value = result
    goto epilogue

epilogue:
    # [NEW: ROOT-PROLOGUE] pop exact root extent
    runtime.side_root_top = root_frame

    # [NEW: RETURN] dynamic boxed-scalar ownership
    scalar_top = runtime.side_number_top
    scalar_payload = classify_out_of_line_scalar_payload(return_value)
    if scalar_payload >= number_frame && scalar_payload < scalar_top:
        raw = *scalar_payload
        *number_frame = raw
        scalar_result = retag(return_value, number_frame)
        runtime.side_number_top = number_frame + 8
    else:
        scalar_result = return_value
        runtime.side_number_top = number_frame
    ret scalar_result

root_overflow:
    call lambda_stack_overflow_error("side-stack")
    ret ITEM_ERROR
endfunc
```

In actual MIR the classifier is expanded into tag masks, comparisons, and
branches. It is shown as one semantic operation above only to keep the complete
body readable; the exact expansion is documented next.

### 13.1 Exact dynamic scalar return classifier shape

```text
scalar_top     = runtime.side_number_top
scalar_payload = 0
scalar_result  = return_value

if return_value has an inline-double payload:
    goto payload_done

tag = return_value >> 56
if tag == INT64:
    if compact-inline-int64: goto payload_done
    goto extract_payload
if tag == FLOAT or tag == FLOAT64:
    if canonical inline zero form: goto payload_done
    goto extract_payload
if tag == DATETIME:
    goto extract_payload
goto payload_done

extract_payload:
    scalar_payload = return_value & ITEM_PAYLOAD_MASK

payload_done:
if scalar_payload >= number_frame && scalar_payload < scalar_top:
    scalar_raw = *scalar_payload
    *number_frame = scalar_raw
    scalar_result = (return_value & ITEM_TAG_MASK) | number_frame
    runtime.side_number_top = number_frame + 8
else:
    runtime.side_number_top = number_frame
ret scalar_result
```

The apparent `+ 8` above is byte-oriented pseudocode. In C fields,
`side_number_top` is a `uint64_t*`, so donation advances by one slot.

---

## 14. Module `main` and wrapper comparison

The frame design applies to generated module entry points as well as user
functions.

### 14.1 Before `main`

```text
call heap_jit_gc_root_frame_enter()
result = NULL_ITEM
call _frame_review_0(call_result, consts[1], consts[2])
OLD_ROOT_STORE(0, call_result)
OLD_ROOT_LOAD(result, 0)
call heap_jit_gc_root_frame_exit()
ret result
```

Metrics: 19 executable instructions, 19 body entries, 13 locals, 5 calls.

### 14.2 Reviewed `9402b169` `main`

```text
root_frame = runtime.side_root_top
root_top = root_frame + 8
if root_top > runtime.side_root_limit: goto root_overflow
runtime.side_root_top = root_top

result = NULL_ITEM
call _frame_review_0(call_result, consts[1], consts[2])
NEW_ROOT_STORE(0, call_result)
NEW_ROOT_LOAD(result, 0)
return_value = result
goto epilogue

epilogue:
    runtime.side_root_top = root_frame
    ret return_value
```

Metrics: 28 executable instructions, 31 body entries including labels, 18
locals, and 2 calls. One call is the real function call; one is present only on
the cold overflow edge.

`main` uses `RETURN_LANE_NONE`, so it does not emit the boxed-scalar donation
classifier.

### 14.3 Typed wrapper

The wrapper `_frame_review_b_0` is unchanged at four executable instructions,
three calls, and three locals. It remains outside the ordinary frame-emission
path in both revisions, so it does not itself measure the new zero-slot
finalization optimization.

---

## 15. MIR growth analysis

Executable instructions grew from 90 to 138 (body entries including labels grew
from 96 to 155) even though calls fell from 45 to 10. This is not contradictory:

1. An old root store was two MIR instructions (`mov` + `call`); a current root
   store is also two (`mov` + memory `mov`). Instruction count stays similar,
   but the expensive call disappears.
2. An old root load was one `call`; a current root load is one memory `mov`.
   Again, count stays similar while execution cost changes.
3. The current checked root prologue adds six normal-path instructions and a
   cold error block.
4. The unified return path adds moves and a branch even for a single source
   return.
5. The boxed return lane adds the dynamic scalar classifier and donation path.
6. More descriptive intermediate registers raise the local count but do not by
   themselves imply memory traffic after MIR register allocation.

### What is design cost versus implementation cost?

| Source of growth | Design requirement? | Could be specialized? |
|---|---:|---:|
| Exact side-root bounds check | Yes | Hoist/amortize only with a different reservation design |
| Direct root stores/loads | Yes for precise roots | Slot liveness and redundant-copy analysis can reduce them |
| Unified cleanup | Yes | Straight-line single-return functions may be simplified safely |
| Scalar payload ownership transfer | Yes | Classifier can be specialized from proven return type |
| Cold overflow block | Yes | May be outlined/shared by backend |
| Monotonic non-reused root slots | No | Liveness-based slot reuse is possible |
| Retained conservative native scan | Transitional | Remove only after all root producers are audited |

The largest probe-specific growth is the generic scalar return classifier. That
is a consequence of supporting a dynamic boxed return safely, but emitting the
fully generic classifier is an implementation choice. If return analysis proves
"GC string only," "inline integer only," or a particular wide scalar type, a
specialized epilogue could be substantially smaller.

### Runtime interpretation

MIR instruction count alone is a poor proxy for runtime here. The current
normal path adds simple arithmetic, comparisons, and memory accesses while
removing 36 C calls with TLS state, linked-list lookup, registration, and
possible allocation behind them. A release-build benchmark is required to
measure the net effect.

---

## 16. Historical review findings and latest disposition

The findings below describe the `9402b169` review. F1, F3, F5, F7, F8, and F9
remain valid. F2 is resolved for precise release contexts and retained only as
an explicit compatibility/debug capability. F4 is addressed by shared CFG
liveness, dirty write-back, scratch coloring, and compact slot assignment. F6
remains a separate return-mode specialization opportunity.

### F1. The core root side stack replaces the old heap root frame cleanly

No `heap_jit_gc_root_frame_*` references remain in current core/runtime code.
The probe confirms that root traffic is base-plus-constant memory traffic.

### F2. The reviewed GC still had dual precise/conservative coverage

The side-root range was precise, but `gc_scan_stack()` still ran on every
collection. This was not duplicate MIR rooting; it was duplicate collector
coverage needed by C2MIR and then-unmigrated host/native paths. Section 0
records its retirement from precise release contexts.

### F3. Core Lambda did not acquire JS-style post-instruction full publication

The exact before/after comparison has 16 root slots in both versions. The frame
change did not create the root definitions responsible for this probe. It
changed how they are stored and collected.

### F4. Reviewed root slot allocation was exact but not liveness-minimal

The reviewed prologue reserved exactly the number of slots assigned by
lowering, but assignment was monotonic. The result was exact relative to the
emitter's slot plan, not peak live roots. The current shared finalizer computes
compact stable/scratch slots from safepoint liveness.

### F5. Wide scalar lifetime is now structurally bounded

Replacing evaluation-lifetime nursery allocation with number-frame watermarks
prevents dead scalar temporaries from accumulating indefinitely. Return donation
defines caller ownership explicitly.

### F6. Dynamic scalar return handling is the main MIR code-size concern

The classifier is correct for a dynamic `Item` lane but large. Return-type
specialization is the safest high-value code-size optimization to investigate.

### F7. Rootless side-root prologue elision works

`finalize_side_root_frame()` returns without inserting a prologue when the final
slot count is zero. This is established directly by the emitter's early-return
path; the probe's typed wrapper is separately frame-free in both revisions.

### F8. Frame cleanup is centralized

Normal and raised-error exits converge on one epilogue. This is a correctness
improvement because the relative lifetime of root slots, number slots, and
return values is explicit.

### F9. Platform behavior differs only at virtual-memory commit

POSIX normal execution uses the reserved/demand-paged region directly. Windows
may call `lambda_side_stack_ensure()` when an activation crosses the committed
watermark. This must remain part of cross-platform MIR review even when macOS
dumps show no commit call.

---

## 17. Historical recommended optimization order

This was the optimization order recommended by the `9402b169` review. The
latest status is: semantic root liveness/reuse, dirty write-back, redundant
publication removal, classified calls, release measurement, precise native
producer migration, and scanner retirement for precise contexts are
implemented. Return-mode specialization remains an independent code-size
opportunity. The historical order was:

1. **Specialize boxed return epilogues from proven return type.** Keep the
   generic classifier only for genuinely dynamic `Item` returns.
2. **Add root-slot liveness/reuse analysis.** Compute peak live rooted values,
   assign stable offsets, and reserve that peak rather than total definitions.
3. **Eliminate redundant root-to-root copies.** Transfer slot ownership for
   bindings/results when the source slot is dead and the safepoint model proves
   equivalence.
4. **Audit reloads around calls.** Preserve stores required for GC reachability,
   while letting MIR registers carry values when ABI/liveness proves it safe.
5. **Measure release builds.** Separate compilation time, generated code size,
   normal execution, GC time, and recursion-heavy workloads.
6. **Migrate remaining conservative-only producers.** Add precise guards for
   C2MIR and host/native activation state.
7. **Retire conservative native scanning only after the migration gate is
   complete.** Until then it is a correctness dependency, not dead code.

The first four items are emitter optimizations. The last two are runtime-wide
rooting architecture changes and require broader tests than the core MIR probe.

---

## 18. Source map

| Concern | Source |
|---|---|
| Root predicate and function-frame emission | `lambda/transpile-mir.cpp` |
| Direct frame-slot load/store helpers | `lambda/mir_emitter_shared.hpp` |
| Boxed scalar return re-homing | `lambda/mir_emitter_shared.hpp` |
| Runtime context watermarks | `lambda/lambda.h` |
| Side-stack reservation/commit/decommit | `lib/side_stack.c`, `lib/side_stack.h` |
| Collector entry and side-root range | `lambda/lambda-mem.cpp` |
| Registered roots, side-root scan, compatibility/debug conservative scan | `lib/gc/gc_heap.c` |
| Stack overflow recovery | `lambda/lambda-stack.cpp`, `lambda/lambda-stack.h` |
| Implementation checklist/history | `vibe/Lambda_Impl_Stack_Frame.md` |
| Design and migration rationale | `vibe/Lambda_Design_Stack_Frame.md` |
| JavaScript MIR companion review | `vibe/Lambda_Stack_JS_MIR.md` |
| Latest rooting design and cross-tier MIR profile | `vibe/Lambda_Design_Stack_Rooting.md` §12 |

Historical sources at `5717d36f` additionally include:

- `JitGcRootFrame` and helpers in `lambda/lambda-mem.cpp`;
- old root helper emission in `lambda/transpile-mir.cpp`;
- `lib/gc/gc_nursery.c` and `lib/gc/gc_nursery.h`.

---

## 19. Historical verification record

The comparison used:

- the exact parent of the implementation commit, not a loosely named older
  branch;
- identical Lambda source at both revisions;
- isolated historical source for the old transpiler/runtime;
- `9402b169` source for the reviewed transpiler/runtime;
- exact MIR dump inspection;
- instruction/local/call counts for `_frame_review_0`, its typed wrapper, and
  `main`;
- source inspection of both collector paths and both scalar-storage designs.

The historical debug build required supplying unrelated generated/dependency
artifacts that are no longer self-contained at that revision. The core
`transpile-mir.cpp`, `lambda-mem.cpp`, and numeric nursery sources used for the
comparison are the exact historical files. Debug builds were used only to emit
MIR. No performance conclusion was drawn from them.

The most important review invariant is:

> Every GC-managed value live across a possible collection must be reachable
> through a precise registered/side root or through the retained conservative
> compatibility path; every out-of-line scalar payload must remain within an
> owned number extent or be re-homed before that extent is restored.
