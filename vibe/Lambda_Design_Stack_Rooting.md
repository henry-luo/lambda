# Lambda Stack Rooting: canonical-slot frames as the universal precise rooting scheme

**Status:** PROPOSAL — for review; no decision taken, no implementation started

**Date:** 2026-07-17

**Supersedes:** `vibe/Lambda_Design_Stack_Frame2.md` ("Removing conservative
native-stack rooting from Lambda", 2026-07-16) — that document's blockers
(§7), procedures, gates (§9), and unsafe-actions list (§11) are absorbed here
and the file is removed. This document is the single authority on rooting
scheme and conservative-scan retirement.

**Companion documents:**

| Document | Role relative to this proposal |
|---|---|
| `vibe/Lambda_Stack_JS_MIR.md` | Measured failure of the current shadow-copy rooting (7.59x MIR, findings R0–R10) — the motivating evidence |
| `vibe/Lambda_Stack_MIR.md` | Lambda-side runtime stack/rooting review |
| `vibe/Lambda_Stack_Rooting_Schemes.md` | Scheme survey; §12 is the canonical-slot analysis this proposal adopts, §13 records stack maps as KIV |
| `vibe/Lambda_Design_Stack_Frame.md` | SF1–SF20 lifetime architecture (side stacks, number watermarks, scalar re-homing, env ownership) — **unchanged by this proposal**; this doc only replaces *how live values are published as roots* |

## 1. Goal and scope

Adopt **canonical-slot (Henderson) frames** as the single GC rooting contract
for every execution tier in the Lambda runtime:

1. **Generated code** — Lambda MIR-Direct, LambdaJS, Python and every future
   guest language, emitted through the shared `MirEmitter`;
2. **The MIR interpreter** — which executes the same emitted frames;
3. **C/C++ runtime helpers** — via slot-backed handle types over the same
   side-root stack.

End state: the conservative native-stack scan is **fully retired**.
Concretely, retirement means deleting the collector's dependence on native
stack contents:

```text
remove setjmp used only for GC register flushing
remove SP-to-stack-base root scanning
remove gc_scan_stack()
remove stack_base/stack_current parameters from collector root APIs
remove ASan poisoned-word scan handling
```

It does **not** mean: removing the platform call stack; removing
`_lambda_stack_base` where stack-overflow protection uses it; removing
registered roots/ranges (they remain a supported *precise* mechanism);
removing the side stacks; scanning the number stack; changing MIR's register
allocator; or changing the non-moving GC-object policy.

Non-goals: moving/compacting or generational collection (this proposal
*unlocks* the option by making roots exact and mutable-in-place, but does not
design it); changes to the number stack, scalar return donation, or closure
env ownership (all settled, SF1–SF20); write barriers.

## 2. Why canonical slots

The current LambdaJS implementation is a *shadow-copy* scheme: rooted values
live in MIR registers and the transpiler emits synchronizing stores to keep
side-root copies current — full live-scope republication before every helper
call, representation-blind rooting of every `I64`/`P` call operand and result,
and a post-instruction binding scan. Measured result (`Lambda_Stack_JS_MIR.md`):
7.59x MIR for the probe function, 84.3% of added instructions being root
copy/store pairs, and Test262 regressing 195.7s → 349.4s — while the
conservative scan is still retained underneath (dual rooting, no payoff).

The scheme options were surveyed in `Lambda_Stack_Rooting_Schemes.md`:

| Scheme | Mutator cost | Precision | Backend changes | Can retire the scan |
|---|---|---|---|---|
| Conservative-primary | ~0 | over-approximate | none | no — it *is* the scan |
| Shadow-copy (current) | 7.59x measured | exact but redundant | none | no (helpers) |
| Liveness-pruned shadow slots | dirty-flush per safepoint | exact | none | no (helpers) |
| **Canonical slots (this proposal)** | ~1 store per rooted definition | exact per slot | none | **yes — when combined with §6 helper contract** |
| Safepoint stack maps | 0 | exact | fork MIR backend | only with helper contract too; KIV per schemes doc §13.5 |

Canonical slots invert the shadow-copy representation: **the side-root slot is
the value's canonical home**; registers hold at most a cached copy. A write to
a rooted value is one store to its home; a safepoint requires no publication
because there is no second copy to fall out of sync. The scheme needs no MIR
backend changes, no liveness/dirty-tracking analyses for correctness, and —
decisively for this proposal — **it is directly expressible in C++**: a
slot-backed RAII local gives helpers the *same* contract as generated code.
(Henderson's paper is titled "Accurate garbage collection in an uncooperative
environment" — the uncooperative environment is exactly C/C++.)

**CR1 — Canonical-slot frames are adopted as the single rooting scheme for all
generated code**, emitted through shared `MirEmitter` primitives.

**CR2 — The same scheme extends to C/C++ helpers** as slot-backed handle types
over the same side-root stack (§6), replacing conservative discovery of native
locals. One stack, one frame discipline, one collector interface, two front
ends (emitter and RAII).

**CR3 — Liveness-pruned shadow slots (the JS review's §15 direction) are
rejected**; stack maps remain KIV per the schemes doc §13.5 preconditions.

## 3. The core contract (normative)

The required safety invariant, global at every legal GC point:

> Every live GC-managed object must be reachable from a registered root slot
> or range, a side-root slot (generated frame or helper frame), an
> async/persistent activation root, or another already-reachable GC object.

A value does not need its own root if it is already reachable through a
rooted owner: an array element is protected by the rooted array *after* the
store; before it, a fresh element may need a temporary home. Raw backing-data
pointers are never roots — the owning GC object is.

### 3.1 Safepoints

GC may begin only inside a call classified `MAY_GC`. This is currently *true
by construction* and this proposal freezes it as an API contract:

- the only automatic trigger is the data-zone allocation threshold check —
  the single `collect_callback` invocation at `gc_data_alloc()`
  (`lib/gc/gc_heap.c:624`);
- the only explicit trigger is the `gc()` builtin
  (`lambda/js/js_globals.cpp:15763`), itself a helper call;
- `gc_should_collect()` has no callers; there is no signal-, timer-, or
  background-thread-driven collection.

**CR4 — Frozen safepoint contract:** any future GC trigger must preserve "GC
begins only inside a `MAY_GC` call." Asynchronous collection (from signals,
watchdogs, or other threads suspending a mutator at an arbitrary PC) is
incompatible with every precise scheme that lacks stack maps and is prohibited
under this design. Between safepoints, a GC-managed value may legally live
only in a register with no protection.

### 3.2 Representation classes

Every value the emitter or a helper handles carries one of four representation
classes, assigned at creation, never inferred from `MIR_T_I64`/`uint64_t`
alone (blocker B10, §7):

```text
NON_GC_SCALAR       bools, counters, source IDs, args-stack marks, flags
BOXED_ITEM          a Lambda Item that may reference managed storage
RAW_GC_POINTER      direct pointer to a GC allocation (Array*, Map*, env ptr)
RAW_NON_GC_POINTER  args-stack pointers, arena/pool pointers, Context, buffers
```

Only `BOXED_ITEM` and `RAW_GC_POINTER` are ever root candidates. The lexical
predicate already exists in both transpilers (`jm_should_gc_root_var()`,
`js_mir_hashmap_scope_utils.cpp:213`); the gap is helper call **results**,
which get a class column on the import tables (§5).

The side-root marker continues to accept both boxed Items and raw GC pointers
in slots (precise *region*, per-word candidate decode) — unchanged.

### 3.3 GC-effect classification

Every helper callable from generated code, and every internal helper, is
classified:

```text
NO_GC    cannot directly or transitively reach gc_data_alloc/heap_gc_collect
         and cannot re-enter generated code
MAY_GC   everything else (the default)
```

`MAY_GC` is the safe default; `NO_GC` is claimed explicitly and verified
(§6.4). Anything that can re-enter generated JS/Lambda (dynamic calls,
getters, Proxy traps, `valueOf`/`toString` coercion, iterator protocol,
callbacks) is `MAY_GC` by definition. The JS transpiler imports **473 distinct
helpers**; the hot `NO_GC` set (`js_check_exception`, `js_is_truthy`,
`js_args_save`/`js_args_restore`, TDZ/debug/source-id checks) is small and
carries most call volume.

### 3.4 The homing rule

**A GC-class value whose live range crosses a `MAY_GC` call has a canonical
home: a slot in the current side-root frame.** Concretely:

1. **Variables** (parameters, locals) of GC class are homed: one store at each
   definition (declaration, assignment). The home is the root; the register
   is a cache.
2. **Temporaries** — call results, loaded values, snapshots — are homed iff
   live across at least one `MAY_GC` call. Detection is local: the emitter
   (or helper author) knows what it is still holding when the next `MAY_GC`
   call is emitted. When in doubt, home it — the cost is one store.
3. **Safepoints emit nothing.** No publication, no republication, no operand
   or result rooting. Homes are current by construction.
4. **Values consumed before the next safepoint need nothing.** Ownership
   transfers also end the homing obligation: once a value is stored into a
   rooted owner (container field, env slot, the registered args stack), its
   register copy needs no home for later safepoints.
5. **Slots are per-variable, not per-register**, reused across disjoint
   lexical scopes; frame slot counts are static (late-sized prologue, as
   today).

### 3.5 Write-through register caching

Lambda's collector is non-moving mark-sweep. Therefore a register copy of a
homed value never needs reloading after a safepoint, and the scheme runs as
**write-through**: reads use the register exactly as pre-stack code did;
writes add one store to the home. The marginal mutator cost versus rootless
code is one store per definition of a homed value, plus the existing
prologue/epilogue.

If a moving collector is ever adopted, write-through becomes unsound (cached
registers would stale after relocation); reads would then reload from homes
after safepoints, or the backend needs stack maps. That future decision does
not change this proposal's slot layout — homes are already the mutable,
exact locations a moving collector would rewrite (this is also why the helper
API in §6 uses mutable slots rather than copied roots).

### 3.6 Frame hygiene

- **Zero-initialize the reserved slot range in the prologue.** The collector
  scans the whole reserved range, so no slot may hold garbage at any
  safepoint. Zeroing makes the invariant local and retires the current
  store-before-init hazard (JS review R8). Slot counts under §3.4 are small.
- **Frame elision:** a function with zero homed slots and no number-stack use
  emits no frame at all (JS review R9).
- Prologue/epilogue, overflow handling, number-watermark save/restore,
  scalar-return donation, and owned-env re-homing are unchanged from the
  current frame design.

### 3.7 Collector end state

```text
mark registered root slots
mark registered root ranges
mark [side_root_base, side_root_top)      // generated frames + helper frames
mark explicit extra roots
trace the marked object graph
```

No `setjmp()` flush, no native stack bounds, no raw-word stack scan. During
migration the scan remains, governed by the capability mode of §8.2.

## 4. Generated code: shared emitter and per-tier adoption

### 4.1 The shared MirEmitter owns the scheme

Frame lifecycle and homing become `em_*` primitives in
`lambda/mir_emitter_shared.hpp` (`MirEmitter`), which both transpilers already
partially use (`em_store_frame_slot`, `em_store_frame_top`) and which the
unified-AST plan designates as the common emission layer:

```text
em_frame_begin(em, ...)            anchor + late-sized prologue, zero-init
em_home_alloc(em, class)           static slot for a variable/temp
em_home_store(em, slot, reg)       the one store: write-through at definition
em_safepoint_call(em, import, ...) checks import.can_gc; homes pending
                                   GC-class temps the lowering still holds;
                                   emits the call; NO publication
em_frame_finish(em, ...)           epilogue: env re-homing, scalar donation,
                                   watermark restores, overflow block
```

Per-language transpilers keep only their language-specific lowering; the
rooting policy exists in exactly one place. This resolves the JS review's R7
(policy duplicated across two emission paths) structurally and gives Python
and future guests the scheme for free at port time.

**CR5 — Rooting policy lives only in `MirEmitter`; per-language emitters may
not emit root stores directly.**

### 4.2 LambdaJS: migration is mostly deletion

The current JS implementation is already canonical-slot-shaped for lexical
variables — `jm_emit_root_updates()` (`js_mir_hashmap_scope_utils.cpp:264`)
already stores to a variable's slot on every write (write-through), and
`jm_set_var()` homes at creation. The blow-up comes from the two shadow-copy
mechanisms layered on top. Migration:

**Delete** (5 call sites, 2 functions):
- `jm_root_live_scope_vars()` — republication before every helper; called
  from `jm_call_with_args`, `jm_call_void_with_args`
  (`js_mir_calls_boxing_types.cpp:142/170`) and the `jm_emit()` call
  interception (`js_mir_hashmap_scope_utils.cpp:547`);
- `jm_root_call_insn_regs()` — representation-blind `I64`/`P` operand/result
  rooting (`:550/:554`, predicate at `:291`).

**Change**:
- slot-per-register → slot-per-variable in `jm_set_var()` (kills TDZ
  double-slots, R4);
- `jm_create_gc_root_slot()` lookup-hit no longer re-stores;
- prologue zero-init; frame elision.

**Add**:
- `can_gc` + result representation-class columns on the 473-helper import
  table;
- pending-temp homing in `em_safepoint_call` (first cut may eagerly home
  every GC-class call result — still ~10x below today's traffic).

Expected effect on the review probe: 440 → ~130 MIR instructions; the
remaining delta over the 58-instruction pre-stack body is
prologue/epilogue/classifier, separately reducible via return-mode inference
(R10) and elision (R9).

### 4.3 Lambda MIR-Direct: converge onto the same primitives

The core transpiler already has anchor/late-prologue frames and static slot
counts (`transpile-mir.cpp:547–744`) and emits through `em_*` stores. Work:
align its rooting predicate with §3.2, its temp rule with §3.4, and route
frame lifecycle through the §4.1 primitives so the two transpilers share one
implementation. Audit for any shadow-copy-style republication and remove it.
Baseline contract: Lambda gtest + `test-lambda-baseline` byte-identical.

### 4.4 Python and future guests

`transpile_py_mir` currently emits **no rooting at all** — its locals survive
on the conservative scan only, and its envs/generator frames are pool-pinned
(Stack_Frame_Python PO2/PO8). Python adopts canonical slots by construction
when it ports onto the unified AST + `MirEmitter`
(`Lambda_Unified_AST_Impl_Plan.md`; `Lambda_Impl_Stack_Frame_Py.md` P1
already routes its frames through the shared em pair). Until then, an active
Python module forces the compatibility mode of §8.2.

**CR6 — A guest language is precise iff it emits through the §4.1 primitives.
No per-guest rooting dialects.**

### 4.5 MIR interpreter

The MIR interpreter executes the same emitted instructions, so frame
prologues, home stores, and epilogues run identically under interpretation —
no separate scheme is needed for interpreted MIR. Its native helper calls are
covered by §6 like any other caller. It gets its own precise-only test lane
(§9.3).

### 4.6 C2MIR

Blocker B1 (§7) stands: current C2MIR emits no precise frames, and the
settled position (OS6) is all-or-nothing — no partial patch, since a partial
patch preserves hidden conservative dependence while hiding it from the
gates. Under this proposal:

**CR7 — C2MIR does not block the scheme; it gates the *mode*.** Executing any
C2MIR-compiled module forces the context into compatibility mode (§8.2). The
long-term choice — upgrade C2MIR wholesale to §4.1 emission (frame
reservation, homing, may-GC boundaries, return/error ownership, unwind
integration, precise-only regression coverage), or exclude it from
precise-only builds — is deferred until the rest of the migration is
complete, when its remaining value can be judged. (CQ2.)

## 5. Import tables as the single source of classification

The helper classification (§3.2 result classes + §3.3 effects) attaches to the
existing import/prototype tables the transpilers already maintain:

```text
{ name, proto, import, can_gc, ret_class }
```

One table serves three consumers: `em_safepoint_call` (skip homing around
`NO_GC` helpers), the result-homing rule (`ret_class`), and the §6.4 audit
tooling (the same `can_gc` bit is what the static checker verifies against
the call graph). Classification is data, not code: **no helper implementation
changes are needed for the generated-code migration** — verified by scan: no
runtime helper reads the side-root region (the only side-stack touch points
outside the transpilers are `js_env_rehome_scalars` and the number-stack range
check, `js_runtime_function.cpp:311/330`, both number-stack machinery), so
helpers are indifferent to whether callers republish or maintain homes.

Misclassification is soft while the conservative scan remains (the scan still
discovers an unhomed temp — pre-stack-level protection) and is caught by the
precise-only stress gates before the scan retires.

## 6. C/C++ runtime helper adaptation

This is what makes "fully retire the scan" honest. Helpers hold raw `Item`s
and GC pointers in C locals across allocating calls; today only the
conservative scan protects them (blocker B2). The adaptation applies the
*same canonical-slot idea* to C++: the local's storage becomes a side-root
slot, wrapped in an RAII type. Scale to be honest about: `js_runtime.cpp`
alone is ~39k lines; plus `lambda-eval.cpp` (~6.6k), input/format/validator
paths, and the polyglot runtimes.

### 6.1 API

C+ convention (no `std::` types), building on the existing `LambdaRootGuard`
(`lambda/lambda.hpp:329`) but upgrading it from *snapshot-push* to *canonical
slots* — the pushed copy in the current guard does not track later mutation
of the local; a slot-backed local does, and keeps the contract valid for a
future moving collector:

```cpp
// reserves n zeroed slots above the watermark; dtor restores the watermark.
// same overflow discipline as generated prologues (lambda_side_stack_ensure).
class RootFrame {
public:
    RootFrame(Context* rt, int n_slots);
    ~RootFrame();                       // exact restore on every path
    uint64_t* slot(int i);              // canonical storage
    // non-copyable, non-movable
};

// a GC-class local whose storage IS a frame slot (canonical home).
// T is Item or a raw GC pointer type (Array*, Map*, ...).
template <typename T> class Rooted {
public:
    Rooted(RootFrame& rf, T init);
    T get() const;                      // read the slot
    void set(T v);                      // write the slot — the one store
    operator T() const { return get(); }
};

// borrowed views of an already-rooted slot; callee never re-roots.
template <typename T> class Handle       { const uint64_t* slot_; ... };
template <typename T> class MutableHandle{ uint64_t* slot_; ... };

// registered root with explicit lifetime, for statics/caches (wraps
// gc_register_root / unregister). Replaces raw Items in globals.
template <typename T> class PersistentRooted;

// debug-only assertion scope for NO_GC leaves: aborts if any allocation or
// collection occurs while alive. Verifies the §3.3 claim dynamically.
class AutoAssertNoGC;
```

C equivalent for C modules and the Jube host ABI (blocker B8):

```c
LambdaRootFrame rf;  lambda_root_frame_begin(ctx, &rf, n);
uint64_t* home = lambda_root_frame_slot(&rf, i);   // canonical storage
lambda_root_frame_end(&rf);                        // watermark restore
```

Required properties (any API bikeshedding must preserve them): no heap
allocation on root push; bounds checking consistent with generated frames;
correct LIFO nesting with generated calls; exact restoration on every
normal/error path; non-copyable RAII in C++; explicit begin/end structure in
C that cannot silently skip restoration; mutable rooted values; a clear
temporary-vs-persistent distinction. Helper frames and generated frames
interleave on the same side-root stack; the collector scans one interval and
cannot tell them apart.

### 6.2 The rules (normative)

- **RH1 — Locals.** Any `BOXED_ITEM`/`RAW_GC_POINTER` local live across a
  `MAY_GC` call must be a `Rooted<T>` (its storage is the home). Locals
  consumed before the next `MAY_GC` call need nothing.
- **RH2 — Arguments are caller-rooted borrows.** A helper may use incoming
  GC-class arguments across `MAY_GC` calls *without re-rooting them*, because
  every caller class roots them first: generated code homes them in its frame
  (§3.4), and native callers pass `Handle`s backed by their own `Rooted`
  slots. This resolves blocker B4 in the cheap direction — a helper roots
  only what it *materializes*, not what it receives. The contract becomes
  load-bearing only at the precise-only flip, by which point all caller
  classes are migrated; until then the conservative scan covers any
  unmigrated caller. Exception: extending an argument's lifetime beyond the
  activation (RH5/RH6).
- **RH3 — Fresh values.** A newly allocated or loaded GC-class value must be
  in a `Rooted` slot (or stored into a rooted owner) before the next `MAY_GC`
  call. The allocation→root window is safe because it contains no safepoint.
- **RH4 — Returns.** Returning a raw `Item`/pointer is legal: the helper's
  `RootFrame` pops at return, and the window between that pop and the
  caller's home store (or next use) contains no `MAY_GC` call. A helper must
  not, however, make a `MAY_GC` call after unrooting the value it is about to
  return.
- **RH5 — Statics and caches.** No raw GC value in static/global storage;
  use `PersistentRooted`, a registered range, or make the cache a GC-owned
  traced object. (This is the existing JO-ledger de-pinning doctrine; the
  audit will surface violations — timers, DOM wrapper cache, module vars.)
  Registered ranges must additionally prove: stable backing address while
  registered; correct count after growth; unregister before storage
  destruction/movement; unused entries initialized to non-pointer values.
- **RH6 — Handoffs.** A value crossing into async/callback/queue storage must
  be continuously rooted until the persistent owner holds it (blocker B9):
  root in the producer frame, store into the registered/traced owner with
  write ordering that makes it visible before the frame pops, then pop.
- **RH7 — `NO_GC` leaves.** Helpers classified `NO_GC` need no frame and no
  handles; they should carry `AutoAssertNoGC` in debug builds.

### 6.3 What adaptation looks like in practice

Most helpers fall into three shapes, in decreasing frequency:

1. **Leaf / no allocation** (`js_is_truthy`, flag reads, arithmetic): classify
   `NO_GC`, add debug assert, zero code change.
2. **Single-alloc-and-return** (`js_new_object`, most constructors): the
   result is returned immediately with no intervening `MAY_GC` call — RH4
   applies, zero code change beyond classification.
3. **Multi-step builders** (create container → allocate members → insert;
   coercion loops; iterator pumps; anything calling back into user code):
   these are the real audit targets. Adaptation is a `RootFrame` + `Rooted`
   for each value held across the internal `MAY_GC` calls:

```cpp
Item js_pair_helper(Context* rt, Item input) {
    RootFrame rf(rt, 2);
    Rooted<Array*> arr(rf, array_new(rt, 2));   // RH3: held across next call
    Rooted<Item>   val(rf, js_coerce(rt, input)); // MAY_GC: may re-enter JS
    array_set(arr.get(), 0, input);             // RH2: input is caller-rooted
    array_set(arr.get(), 1, val.get());
    return array_to_item(arr.get());            // RH4: no MAY_GC after this
}
```

The per-function audit recipe (applied bottom-up in §6.5's order):

1. identify every GC-class argument and local;
2. identify first definition and last use of each;
3. identify every intervening `MAY_GC` call;
4. leave arguments alone (RH2); `Rooted` each local crossing such a call;
5. root fresh results before the next such call (RH3);
6. note ownership transfers into rooted containers/activations (obligation
   ends there);
7. check error/cleanup paths restore the frame (RAII does this; C code must
   not `return` past `lambda_root_frame_end`);
8. run the function's tests under forced GC + precise-only mode.

### 6.4 Enforcement: classification must be verified, not trusted

Convention alone cannot carry a 39k-line file (blocker B5). Three layers:

1. **Generated `MAY_GC` call graph.** A script over the compile database
   computes transitive reachability to `gc_data_alloc`/`heap_gc_collect`/
   generated-code re-entry, emits the classification report, and diffs it
   against the import-table `can_gc` bits. Rebuilt in CI; a helper silently
   becoming `MAY_GC` because a callee started allocating flips the bit and
   fails the diff. (This is SpiderMonkey's hazard analysis reduced to the
   effect half.)
2. **Rooting-hazard lint** (incremental start, AST-based endgame): flag the
   pattern *GC-class local defined + `MAY_GC` call before last use + no
   active `Rooted`/handle*. It must understand: `Item` and raw GC pointer
   types; owner/backing-pointer relationships; `RootFrame`/`Rooted` scopes;
   persistent registrations; transitive may-GC; ownership transfer into
   rooted containers; async handoff; return escape. High-value warnings:
   raw `Container*` live across `heap_calloc()`; fresh allocation followed
   by another allocation before rooting; rooted local overwritten without
   updating its slot (impossible with `Rooted`, possible in C); root frame
   destroyed before cleanup/error calls; registered range whose backing can
   move; callback retaining a borrowed handle past its scope. Start as a
   `make lint` ratchet on migrated modules (the no-int-cast-radiant model);
   static analysis is a required CI gate before final deletion.
3. **Dynamic gates** (§9): shadow-scan reporting, precise-only mode, and
   forced-GC stress with heap poisoning and ASan.

### 6.5 Audit order and cost containment

Bottom-up, leaf allocators first, then callers:

1. core object/container constructors and mutators;
2. shared string, map, array, element, VMap, error, and function helpers;
3. evaluator/interpreter and procedural runtime (`lambda-eval.cpp`);
4. JS runtime helpers (the 473 imported ones by call frequency, transitives
   after);
5. async, scheduler, event-loop, promise, and callback helpers;
6. Python/Ruby/Bash bridges; input/format/validator paths that allocate GC
   memory (CQ4);
7. Jube/native module boundary (§8, S4);
8. remaining platform and optional-module helpers.

Cost is contained by three facts: RH2 removes all argument-rooting work (the
majority of GC-class values a helper touches are arguments); §6.3 shapes 1–2
(most helpers) need classification only; and the shadow diagnostics (§9.1)
point the audit at frames that actually matter instead of requiring a blind
full sweep.

## 7. Blockers to scan retirement (absorbed ledger)

Inherited from the retired `Lambda_Design_Stack_Frame2.md`; each blocker with
its resolution under this proposal.

| ID | Blocker | Resolution here |
|---|---|---|
| B1 | C2MIR is conservative-scan-only; partial patches invalid | CR7: gates the per-context mode (§8.2); wholesale upgrade-or-exclude decided at S4 (CQ2) |
| B2 | Native helpers have no complete local-root contract | §6 API + RH1–RH7 |
| B3 | `LambdaRootGuard` is a pilot, not a discipline | §6.1 upgrades it to canonical slots and makes it universal; C ABI added |
| B4 | Helpers can't assume callers rooted arguments | RH2 makes caller-rooting the contract for every caller class; load-bearing only at the precise-only flip |
| B5 | No enforced may-GC/no-GC effect system | §3.3 classification + §6.4.1 CI call-graph diff + §5 single-source table |
| B6 | Evaluator and non-MIR paths use native locals | In audit scope as helpers (§6.5 item 3; CQ1) |
| B7 | Broad persistent roots ≠ temporary-root proof | Persistent storage audited separately (RH5 range rules) from transient locals (RH1/RH3) |
| B8 | Jube modules lack a mandatory handle ABI | §6.1 C ABI; versioned rooting-capable host ABI; old-ABI modules force compatibility mode; module-level forced-GC tests (S4) |
| B9 | Async/callback handoff intervals | RH6 + suspension coverage per SF20 (CQ3) |
| B10 | Raw pointers and boxed Items share machine words | §3.2 representation classes carried as metadata, never inferred from machine type |
| B11 | Tests pass through accidental conservative roots | §9.1 precise-only mode + forced-GC stress as mandatory gates |
| B12 | Mixed activation chains defeat per-entrypoint switches | §8.2 sticky per-context capability mode; per-frame/per-entrypoint switching prohibited |

## 8. Migration plan

Every stage keeps all baselines green; S1–S2 are each independently shippable
perf/correctness wins even if later stages stall.

- **S0 — Tables and diagnostics.** Import-table `can_gc`/`ret_class` columns +
  generation script (§5, §6.4.1); shadow-scan reporting and precise-only mode
  in the collector (§9.1); forced-GC stress hooks (§9.2). No behavior change.
  Deliverables also include: the frozen list of GC entry points (CR4), the
  inventory of execution engines/entrypoints, and the list of persistent
  root stores/ranges with lifetimes. Do not begin by deleting
  `gc_scan_stack()`.
- **S1 — LambdaJS canonical slots** (§4.2). Recovers the Test262 regression.
  Gates: probe MIR golden/count test, `regression_side_stack_frame_gc.js`
  under forced GC + ASan, JS gtests, full Test262 timed against the pre-stack
  release binary, Lambda baselines byte-identical.
- **S2 — Emitter unification** (§4.1/§4.3). Frame + homing primitives move
  into `MirEmitter`; Lambda MIR-Direct converges. Aligns with unified-AST
  Phase 0; Python inherits at its port (P1 of the Py impl plan).
- **S3 — Helper API + core helpers** (§6.1–§6.3). Land
  `RootFrame`/`Rooted`/`Handle`/`PersistentRooted`/`AutoAssertNoGC` + C ABI;
  migrate §6.5 items 1–3; lint ratchet on migrated modules.
- **S4 — Subsystem audits** (§6.5 items 4–8) driven by scan-exclusive
  candidate reports (§9.1); JO-ledger de-pinning (RH5); persistent/async
  ownership audit (RH5 range rules, RH6 handoffs, context-reuse resets);
  Jube ABI versioning and module forced-GC tests; C2MIR decision point
  (CR7/CQ2); Python port lands via unified-AST plan.
- **S5 — Precise-only as default.** All §9.3 suites run precise-only in CI;
  then precise-only becomes the default in development builds, then in
  release builds with a guarded diagnostic fallback for one transition
  period. Rollout never toggles scanning per-entrypoint (B12) — only via the
  §8.2 context mode.
- **S6 — Delete the scan.** Remove `setjmp()` register flushing, stop
  reading/passing `stack_current`/GC `stack_base`, drop those parameters from
  `gc_collect_with_root_region()`/`gc_collect()`, remove `gc_scan_stack()`
  and its ASan poison helpers and profiling counters; retain stack-bound
  initialization needed for stack-overflow protection; rerun the complete
  §9 acceptance matrix; measure per §10. The scan is *not* kept as a
  permanent production fallback (it would add cost and false retention
  without required coverage); what remains permanently is the **debug
  shadow-verify mode** (§9.1) as a regression diagnostic.

### 8.2 Compatibility mode (sound transition, B12)

A per-context capability flag, not a per-entrypoint switch: the context runs
precise-only iff **no conservative-dependent tier is active in it** — no
C2MIR module loaded, no unported guest (Python pre-port), no old-ABI Jube
module, no unaudited path flagged by the loader. Loading any such tier sets
`conservative_mode` on the context; the collector then adds the native-stack
scan for that context exactly as today. The flag is sticky per context (mixed
native/generated activation chains make dynamic per-frame switching unsound);
precise-only test lanes assert it stays clear.

## 9. Verification

### 9.1 Collector diagnostic modes (built at S0, kept forever)

**Shadow conservative-root reporting.** During root marking: mark all precise
roots first; then run the conservative scan; count and log objects that were
unmarked before it ("**scan-exclusive candidates**") with type/address and
collection context; still mark them (safety preserved). Counters:

```text
gc_precise_root_count
gc_conservative_candidate_words
gc_conservative_new_objects(_by_type)
gc_conservative_scan_bytes
```

A scan-exclusive candidate is not automatically a bug (stale words are
legitimate false positives) — it is the audit worklist and the migration's
primary progress metric. Logging via `log_debug()` with a distinctive prefix.

**Precise-only mode.** A test/debug switch that skips conservative marking
entirely, usable per harness and engine, visibly reported in logs/test
summaries, never silently falling back after a failure.

### 9.2 Forced-GC stress (legal safepoints only)

Collect at aggressively varied **legal** `MAY_GC` boundaries — never at
invented mid-allocator points the real runtime cannot produce. Schedules:
before every eligible allocation; every Nth allocation for several N;
randomized deterministic schedules with logged seeds; at callback/async
handoff boundaries; during exception/error propagation; under deep recursion
and large frames. Run both shadow-scan and precise-only variants, with heap
poisoning and ASan.

### 9.3 Per-engine verification and the gate matrix

Per engine: Lambda MIR-Direct (predicates, allocation/call results across
safepoints, slot lifetime across branches/loops/errors/cleanup, elision
correctness, async spill/suspension handoff, main/handlers/view functions);
LambdaJS (same, plus methods, constructors, generators, async functions,
module entrypoints, `js_main`, module loading, promises, events, typed
arrays, Node paths); MIR interpreter (own precise-only lane); C2MIR (only if
upgraded).

Precise-only suites required before the flip:

```text
Lambda core baseline + extended/unit tests
MIR interpreter mode
C2MIR suite (if retained)
JS/Test262 baseline, zero failures, zero retries
Node compatibility baseline
async/concurrency + worker-reuse tests
Python/Ruby/Bash/Jube tests
document/layout paths that execute Lambda/JS (Radiant re-entry, CQ5)
forced-GC stress variants; ASan/UBSan variants
release smoke + performance comparison
```

Targeted stress must cover: values held across one and several nested
allocations; raw container pointers; fresh values before container insertion;
arguments and returns; errors and cleanup; closures/captures; async
suspension/resumption; callback/event-loop handoff; persistent range
resize/destruction; deep recursion and stack-overflow recovery; worker/context
reuse; GC during module initialization.

**Acceptance criteria for S6:** (1) every scan-exclusive candidate observed
under targeted stress is classified or eliminated; (2) all suites pass
precise-only; (3) the static hazard analysis reports no may-GC hazards;
(4) no execution engine remains documented as conservative-only.

Condensed checklist:

- [ ] Lambda MIR-Direct, LambdaJS, MIR-interpreter exact-root audits complete
- [ ] C2MIR upgraded completely or excluded from precise-only builds
- [ ] Evaluator/procedural/fallback paths migrated
- [ ] Helper root API complete for C and C++; Jube handle ABI enforced
- [ ] Async + persistent handoffs audited; ranges have stable-address proof
- [ ] `NO_GC`/`MAY_GC` contract enforced transitively (CI diff green)
- [ ] Shadow reporting, precise-only mode, forced-GC schedules, static
      analysis all active in CI; no unexplained scan-exclusive roots
- [ ] Full §9.3 suite matrix green precise-only
- [ ] Scan deleted per S6; stack-overflow bounds still functional; docs updated

## 10. What this buys, and how to measure it

- **Performance now** (S1): the measured 7.6x MIR / 2x Test262 regression
  recovered while keeping precise rooting, instead of rolling it back.
- **At retirement** (S6): per-collection work proportional to native stack
  depth (`O(words between SP and stack_base)`) eliminated; no `setjmp` flush;
  no false retention from stale stack words (less mark/survivor work); ASan
  without poison workarounds; GC correctness decoupled from optimizer spill
  choices (the JS review's P1 fragility). Retirement does *not* by itself
  reduce generated-code cost — that is S1's job; the two improvements are
  independent and complementary.
- **Option value:** exact, mutable root slots are precisely what a moving or
  generational collector needs from its roots; canonical slots leave that
  door open (with the §3.5 read-reload caveat), where write-through shadow
  copies and conservative scanning both close it.

Measure (release builds only), before/after each stage and at S6: total
execution time; GC count and total pause; native-stack bytes formerly
scanned; side-root slots scanned; objects marked only by the conservative
pass; live bytes after collection; peak/steady RSS; generated MIR/code size;
Test262 sync/async wall time.

## 11. Explicitly unsafe actions

Do not, at any stage:

- comment out `gc_scan_stack()` and rely on normal tests (B11);
- assume all MIR locals are spilled to the native stack, or that `setjmp`
  captures every caller-saved live value;
- assume a MIR caller roots every native helper argument *before S6's
  contract flip* (RH2 is an end-state contract, not a current fact);
- treat registered globals as coverage for local temporaries;
- root raw backing-data pointers instead of their owning GC object;
- disable the scan per-entrypoint while unaudited helpers can be active (B12);
- keep current C2MIR enabled under a precise-only collector (B1);
- use ad-hoc `volatile` locals as a rooting mechanism;
- add more conservative pointer heuristics as a substitute for explicit
  roots;
- delete `_lambda_stack_base` without preserving stack-overflow handling;
- declare completion before the forced-GC precise-only matrix passes.

## 12. Risks

- **The §6 audit is the schedule risk** — it is the multi-quarter tail
  (SpiderMonkey/V8 precedent). Mitigations: RH2 (arguments free), §6.3 shapes
  1–2 (most helpers are classification-only), scan-exclusive candidates as a
  worklist, per-context compatibility mode so value ships incrementally
  (S1/S2 pay for themselves regardless of whether S6 is ever reached).
- **A missed hazard after the flip is a use-after-free.** Mitigations: the
  three §6.4 layers, poisoning + forced-GC in CI permanently, the shadow
  collector mode retained in debug builds, staged rollout with a guarded
  fallback period (S5).
- **Contract drift**: a new helper or trigger violating CR4/RH rules.
  Mitigations: the CI call-graph diff (§6.4.1) and a frozen-trigger assertion
  (debug check in `gc_data_alloc`/collect entry that a `MAY_GC` region is
  active).
- **Helper-frame overhead**: one `RootFrame` bind + restore per adapted
  helper activation. Negligible against helper call cost; §6.3 shapes 1–2
  (the vast majority) pay nothing. Benchmark gates (deltablue, havlak+push,
  AWFY, Test262 phase timing) ride every stage.

## 13. Decision ledger

| ID | Decision | Status |
|---|---|---|
| CR1 | Canonical-slot frames = the single rooting scheme for all generated code | proposed |
| CR2 | Same scheme extends to C/C++ helpers as slot-backed handles on the same stack | proposed |
| CR3 | Liveness-pruned shadow slots rejected; stack maps stay KIV | proposed |
| CR4 | Safepoint contract frozen: GC begins only inside `MAY_GC` calls; async collection prohibited | proposed |
| CR5 | Rooting policy implemented once, in `MirEmitter`; transpilers may not emit root stores directly | proposed |
| CR6 | A guest language is precise iff it emits through the shared primitives | proposed |
| CR7 | C2MIR gates the per-context mode, not the scheme; upgrade-or-exclude decision deferred to S4 | proposed |

## 14. Open questions

- **CQ1 — Interpreter/evaluator locals.** `lambda-eval.cpp` and the
  procedural runtime hold Items in C++ locals like any helper; confirm they
  are covered by the §6 audit scope (they are "helpers" for this purpose) and
  identify any evaluator-specific activation state needing persistent roots
  (B6).
- **CQ2 — C2MIR endgame** (CR7): wholesale upgrade vs exclusion from
  precise-only builds. Decide at S4 with usage data.
- **CQ3 — Generator/async resume homes.** Verify suspended state is fully
  owned by traced activation objects (`gen_env`, LambdaAsyncFrame per SF20)
  and homes are re-established per resume activation; add a forced-GC test at
  every suspension boundary.
- **CQ4 — Input/format paths.** Determine which parser/formatter paths
  allocate GC-heap memory (vs arena/pool only) and therefore enter the audit
  scope; MarkBuilder's arena discipline may exempt large regions.
- **CQ5 — Radiant re-entry.** Radiant event/script callbacks re-enter JS;
  audit the C++ frames between Radiant and the JS boundary under the same RH
  rules.
- **CQ6 — Slot metadata.** Whether side-root slot words ever need a tag
  distinguishing boxed Item vs raw pointer (not needed for non-moving
  marking; would matter for a moving collector).
- **CQ7 — `Rooted` ergonomics for hot helpers.** Whether write-through
  register caching is worth mirroring in C++ (`Rooted::get()` is a load;
  helpers in tight loops may want a cached raw copy between safepoints —
  legal under CR4, needs a documented idiom rather than ad-hoc use).

## 15. Source map

| Concern | Source |
|---|---|
| GC driver, register flush, stack bounds | `lambda/lambda-mem.cpp` (`heap_gc_collect`) |
| Conservative scan, root marking, trigger | `lib/gc/gc_heap.c` (`gc_data_alloc:624`, `gc_collect_with_root_region`) |
| Collector root API | `lib/gc/gc_heap.h` |
| Side-stack reservation and watermarks | `lib/side_stack.c`, `lib/side_stack.h` |
| Runtime `Context` side-stack fields | `lambda/lambda.h` |
| C++ host-helper guard (current pilot) | `lambda/lambda.hpp:329`, use in `lambda/lambda-data-runtime.cpp` |
| Shared emitter primitives | `lambda/mir_emitter_shared.hpp` |
| Lambda MIR root emission | `lambda/transpile-mir.cpp:547–744` |
| JS MIR root emission | `lambda/js/js_mir_hashmap_scope_utils.cpp`, `lambda/js/js_mir_calls_boxing_types.cpp` |
| C2MIR generation | `lambda/transpile.cpp` |
| Persistent root registration | `lambda/lambda-mem.cpp`, language runtime state files |
| Async task roots | `lambda/concurrency.cpp`, JS event-loop/runtime files |
| Jube host API | `lambda/jube/jube_registry.cpp` + public Jube API |
| Lifetime architecture | `vibe/Lambda_Design_Stack_Frame.md` (SF1–SF20) |
| Regression evidence | `vibe/Lambda_Stack_JS_MIR.md`, `vibe/Lambda_Stack_MIR.md` |
