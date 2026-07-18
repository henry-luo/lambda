# Lambda Stack Rooting: safepoint-current canonical slots

**Status:** IMPLEMENTATION IN PROGRESS — S0 metadata/diagnostic foundation landed; CQ2 and CQ4 settled

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
| `vibe/Lambda_Stack_Rooting_Schemes.md` | Scheme survey; §12 supplies the stable-home foundation, refined here with safepoint-current write-back; §13 records stack maps as KIV |
| `vibe/Lambda_Design_Stack_Frame.md` | SF1–SF20 lifetime architecture (side stacks, number watermarks, scalar re-homing, env ownership) — **unchanged by this proposal**; this doc only replaces *how live values are published as roots* |

### Implementation checkpoint — 2026-07-17

The first behavior-neutral S0 slice is implemented:

- `JitImport` now carries explicit GC effect, re-entry, return-value class,
  and per-argument value-class metadata. Unknown and ordinary two-field table
  entries default to `MAY_GC` with unknown representation.
- `jit_import_get_metadata()` is the conservative lookup API, dynamic imports
  are explicitly `MAY_GC` and re-entrant, and the shared `MirEmitter` import
  cache retains the resolved metadata.
- LambdaJS call emission now consumes the GC-effect classification. Because no
  helper has yet passed the required native-helper audit, every current entry
  remains `MAY_GC`; therefore this checkpoint deliberately preserves existing
  rooting behavior.
- Optional `LAMBDA_MIR_LOG_FRAME_SLOTS=1` diagnostics now report root-slot
  stores and classified `MAY_GC`/`NO_GC` call counts per generated JS function.
  Calls emitted outside the effect-aware wrapper are counted and treated as
  conservative `MAY_GC` calls.

Not yet implemented: GC use/definition records, per-safepoint liveness/root-set
analysis, stable binding homes, scratch coloring, precise-only/shadow collector
modes, forced-GC hooks, or deletion of broad/post-instruction publication.
The current side-root behavior is intentionally retained until those pieces
make S1 independently provable.

An early attempt to reuse the current register-keyed binding slots as stable
canonical homes was rejected rather than landed. MIR register identities can
be overwritten across representation changes and temporary lifetimes; the
attempt exposed a typed `double`/`int64` move mismatch and a later runtime
crash. Stable homes must therefore be assigned to semantic bindings by the S1
use/definition layer, not inferred from physical register identity.

Verification for this checkpoint:

- debug `make build`: pass;
- focused `regression_side_stack_frame_gc` and `v20_tdz`: 2/2 pass;
- `make test-lambda-baseline`: 3439/3439 pass (2105 input + 1334 runtime,
  including JS 332/332 and Node preliminary 110/110);
- diagnostic sample for `js_main` in the forced-GC frame regression:
  `roots=729`, `root_stores=21244`, `may_gc_calls=848`, `no_gc_calls=0`.
  The zero `NO_GC` count confirms that this checkpoint has not activated an
  unaudited fast path.

The standalone `test/node/dns_resolve_records.js` process still exits 139
after its first six lines. An untouched Git `HEAD` control build reproduces
the identical exit and output, while the official JS baseline remains green;
this is a pre-existing standalone/debug-runtime issue, not a rooting-metadata
regression.

## 1. Goal and scope

The primary design goal is **precise rooting with performance close to the
pre-stack baseline**. Correctness is non-negotiable, but a scheme that restores
correctness by adding work after every instruction, definition, or allocation
does not meet the goal. Rooting work must be proportional to the live managed
state that actually crosses GC safepoints.

Adopt **safepoint-current canonical-slot frames** as the generated-code GC
rooting contract for every precise execution tier in the Lambda runtime:

1. **Generated code** — Lambda MIR-Direct, LambdaJS, Python and every future
   guest language, emitted through the shared `MirEmitter`;
2. **The MIR interpreter** — which executes the same emitted frames;
3. **C/C++ runtime helpers** — via slot-backed handle types over the same
   side-root stack.

Legacy C2MIR is an explicit exception: it remains unchanged and continues to
use the existing conservative compatibility mode. It is not a target of this
rooting migration and is never admitted to a precise-only context (CQ2).

The central generated-code rule is: each rootable binding has a stable logical
home and each protected temporary has a scratch home, but those slots are
required to be current only when execution enters a `MAY_GC` call. Between
safepoints, a MIR register may hold a newer value and the compiler tracks the
home as dirty. Native helpers use the same slots with a simpler always-current,
write-through `Rooted<T>` discipline because C/C++ does not have the emitter's
root-liveness analysis.

End state for Lambda MIR-Direct, LambdaJS, the MIR interpreter, and migrated
guest/helper paths: the conservative native-stack scan is **fully retired
from precise execution contexts**. A build that retains legacy C2MIR also
retains its isolated conservative compatibility path; a build that deletes
the scanner cannot enable C2MIR. No C2MIR code is changed. For precise
contexts, retirement means removing the collector's dependence on native
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

Non-goals: moving/compacting or generational collection (this proposal creates
exact mutable root locations that such a collector would require, but does not
by itself unlock one); changes to the number stack, scalar return donation, or
closure env ownership (all settled, SF1–SF20); write barriers.

## 2. Key design pillars

The rooting design is the combination of the following contracts. None of the
first seven can be removed independently: generated frames cover JIT values,
native handles cover helper locals, ownership rules cover values that outlive
an activation, unwind restoration covers paths that skip normal cleanup, and
precise-only stress proves that the conservative scan is no longer carrying
correctness accidentally.

### 2.1 The seven essential contracts

1. **Canonical generated frames.** Generated Lambda/LambdaJS values use
   safepoint-current canonical homes: stable logical homes for rootable
   bindings and reusable scratch homes for protected arguments/temporaries.
   Registers may be authoritative between safepoints, but every required home
   is current before `MAY_GC` execution.
2. **Semantic GC representation classes.** Rootability follows
   `BOXED_ITEM`, `RAW_GC_POINTER`, `NON_GC_SCALAR`, and
   `RAW_NON_GC_POINTER` metadata, never the physical `MIR_T_I64`/`MIR_T_P`
   carrier alone.
3. **Conservative `MAY_GC` classification.** Unknown calls, public GC
   allocators, indirect native calls, and generated-code re-entry default to
   `MAY_GC`; `NO_GC` is an explicitly claimed and mechanically verified
   property. `can_reenter` is tracked separately for aliasable binding-cache
   invalidation.
4. **Exact native handles.** C/C++ helpers use always-current
   `RootFrame`/`Rooted<T>` homes, borrowed handles, and persistent handles over
   the same side-root stack. Native locals are never assumed visible merely
   because they probably spilled to the machine stack.
5. **Persistent/async ownership rules.** A value that outlives its current
   activation is transferred continuously into a traced heap owner,
   persistent registered root, or stable registered range before the producer
   frame pops. Suspension never leaves a native side-root frame alive.
6. **Non-local-unwind restoration.** Every `longjmp`/`siglongjmp` recovery
   boundary snapshots and restores root, number, and other runtime-owned
   dynamic watermarks because generated epilogues and C++ destructors can be
   bypassed.
7. **Precise-only forced-GC verification.** The design is not complete until
   precise-only mode, forced collection at every legal safepoint, poisoning,
   sanitizers, and write-through-vs-write-back differential testing pass
   without conservative discovery.

### 2.2 Additional foundational contracts

8. **Performance-proportional publication.** Root stores are proportional to
   dirty live stable homes plus live scratch values at `MAY_GC` boundaries —
   never to all instructions, all lexical bindings, all definitions, or all
   allocation fast paths. The pre-stack release performance budget in §11 is
   a design gate, not a post-implementation optimization target.
9. **Frozen safepoint-only collection.** GC may begin only within a classified
   `MAY_GC` call. Asynchronous collection at arbitrary PCs is prohibited unless
   a future stack-map design replaces this contract.
10. **One shared generated-code policy.** `MirEmitter` owns semantic root
    events, liveness/dirty analysis, scratch coloring, frame sizing, and final
    root-store insertion. Language transpilers report semantics; they do not
    implement independent rooting dialects or emit ad-hoc root stores.
11. **Capability-gated migration and scan retirement.** Conservative scanning
    remains sticky per context while any active execution tier, native module,
    or helper path still depends on it. Precise contexts may disable/delete
    that path only after every tier they admit satisfies the precise contract
    and the complete verification matrix. Unchanged C2MIR is permanently
    confined to compatibility contexts or omitted from scanner-free builds.

Together these pillars define one safepoint invariant across optimized
generated code, write-through native helpers, persistent state, and recovery
paths while allowing each layer to use the cheapest sound publication method.

## 3. Why safepoint-current canonical slots

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
| Liveness-pruned anonymous shadow/scratch slots | dirty live stores per safepoint | exact at safepoints | none | no (helpers) |
| Always-current canonical slots | ~1 store per rooted definition | exact per slot | none | yes, with §7 helper contract |
| **Safepoint-current canonical slots (this proposal)** | dirty live-home/scratch stores at `MAY_GC` calls | exact at safepoints | none | **yes — with §7 helper contract** |
| Safepoint stack maps | 0 | exact | fork MIR backend | only with helper contract too; KIV per schemes doc §13.5 |

The proposal retains the most useful canonical-slot property: **every rootable
binding has a stable side-root home**. It changes when that home must be
updated. Generated code treats the register as a write-back cache between
safepoints and flushes only dirty, live homes before a `MAY_GC` call. Unhomed
temporaries and call arguments use reusable scratch homes. A million no-GC
assignments followed by one allocating call therefore require one root store,
not a million write-through stores.

This scheme needs compiler GC-liveness, dirty-state, and scratch-slot analysis,
but no MIR backend changes. The analysis replaces the current broad dynamic
approximation (full-scope publication, representation-blind call rooting, and
post-instruction binding scans) with precomputed per-safepoint root sets.

Native C/C++ code cannot rely on that compiler analysis, so helpers use an
always-current slot-backed RAII local. Both forms present the same collector
invariant at a safepoint: every live managed value is in an exact slot.

**CR1 — Safepoint-current canonical-slot frames are adopted as the generated-
code rooting scheme**, emitted through shared `MirEmitter` primitives.

**CR2 — C/C++ helpers use always-current slot-backed handles** over the same
side-root stack (§7), replacing conservative discovery of native locals. One
stack, one safepoint invariant, and one collector interface; generated code
uses write-back emission while native code uses RAII write-through.

**CR3 — Stable homes plus liveness-pruned write-back are adopted.** Broad
shadow-copy publication and per-instruction synchronization are rejected;
stack maps remain KIV per the schemes doc §13.5 preconditions.

## 4. The core contract (normative)

The required safety invariant, global at every legal GC point:

> Every live GC-managed object must be reachable from a registered root slot
> or range, a side-root slot (generated frame or helper frame), an
> async/persistent activation root, or another already-reachable GC object.

A value does not need its own root if it is already reachable through a
rooted owner: an array element is protected by the rooted array *after* the
store; before it, a fresh element may need a temporary home. Raw backing-data
pointers are never roots — the owning GC object is.

### 4.1 Safepoints

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

### 4.2 Representation classes

Every value the emitter or a helper handles carries one of four representation
classes, assigned at creation, never inferred from `MIR_T_I64`/`uint64_t`
alone (blocker B10, §8):

```text
NON_GC_SCALAR       bools, counters, source IDs, args-stack marks, flags
BOXED_ITEM          a Lambda Item that may reference managed storage
RAW_GC_POINTER      direct pointer to a GC allocation (Array*, Map*, env ptr)
RAW_NON_GC_POINTER  args-stack pointers, arena/pool pointers, Context, buffers
```

Only `BOXED_ITEM` and `RAW_GC_POINTER` are ever root candidates. The lexical
predicate already exists in both transpilers (`jm_should_gc_root_var()`,
`js_mir_hashmap_scope_utils.cpp:213`); the gap is helper call **results**,
which get a class column on the import tables (§6).

The side-root marker continues to accept both boxed Items and raw GC pointers
in slots (precise *region*, per-word candidate decode) — unchanged.

### 4.3 GC-effect classification

Every helper callable from generated code, and every internal helper, is
classified:

```text
NO_GC    cannot allocate GC memory, collect, invoke unknown native code,
         or re-enter generated code
MAY_GC   everything else (the default)
```

`MAY_GC` is the safe default; `NO_GC` is claimed explicitly and verified
(§7.4). Anything that can re-enter generated JS/Lambda (dynamic calls,
getters, Proxy traps, `valueOf`/`toString` coercion, iterator protocol,
callbacks) is `MAY_GC` by definition. The JS transpiler imports **473 distinct
helpers**; the hot `NO_GC` set (`js_check_exception`, `js_is_truthy`,
`js_args_save`/`js_args_restore`, TDZ/debug/source-id checks) is small and
carries most call volume.

Track `can_reenter` separately from `can_gc`: every re-entrant call is
`MAY_GC`, but many allocating helpers cannot invoke user code. The extra bit
lets generated code invalidate/reload aliasable captured or shared binding
caches only around calls that can actually mutate them.

All public GC-heap allocators (`heap_alloc`, `heap_calloc`, class/env variants,
and their wrappers) are `MAY_GC`, even if a current fast path happens not to
collect. A future optimization may introduce an explicitly named
`try_alloc_no_gc` operation that is mechanically guaranteed never to collect
and returns failure; only its slow fallback is a safepoint. Existing allocators
must not be classified `NO_GC` from incidental implementation behavior.

### 4.4 The homing and publication rule

**A GC-class value whose root obligation intersects a `MAY_GC` call has a
stable or scratch home in the current side-root frame, and that home must be
current before the call executes.** Concretely:

1. **Stable variable homes.** A GC-class parameter/local that can be live at a
   safepoint receives one slot for its binding. Definitions update the MIR
   register and mark the home dirty; they do not automatically emit a store.
2. **Arguments live through the call.** Every GC-class argument passed to a
   `MAY_GC` helper is root-obligated for the helper's entire dynamic execution,
   even when ordinary compiler liveness says it is dead immediately after the
   call. A current stable home needs no store; a dirty stable home is flushed;
   an unhomed argument is stored in a reusable scratch slot.
3. **Other temporaries.** Call results, loads, and expression intermediates
   receive scratch homes only when live across a later `MAY_GC` call. Values
   whose last use precedes the next safepoint emit no root traffic.
4. **Minimal safepoint publication.** Before a `MAY_GC` call, emit stores only
   for dirty live stable homes and required scratch values. A current unchanged
   home is not republished. `NO_GC` calls emit no rooting MIR.
5. **Ownership transfer.** Storing a value into a rooted, traced owner may end
   its separate root obligation only after the store is complete. Do not apply
   this elision when re-entrant code could remove the value from the owner while
   a helper still holds a borrowed copy.
6. **Static frame, reusable physical slots.** Stable homes are logical
   per-binding identities, not per-register identities. Bindings with proven
   non-overlapping lifetimes may share a physical slot; scratch slots are
   colored from non-overlapping safepoint live ranges. Frame size is the
   maximum simultaneous stable-plus-scratch demand, finalized by the existing
   late-sized prologue.

The first correctness implementation may conservatively allocate unique slots
and keep homes current by write-through. The production performance path must
use the safepoint liveness/dirty rules above; write-through remains a useful
debug/reference mode for differential testing.

### 4.5 Safepoint-current write-back caching

Lambda's collector is non-moving mark-sweep and, under CR4, cannot run between
safepoints. A MIR register may therefore be authoritative while its stable home
is dirty. The compiler carries a dirty/current abstract state through control
flow and makes every required home current before `MAY_GC` execution. No
runtime dirty bits or binding scans are needed.

At branches and loops, the analysis must merge dirty state conservatively. It
may flush on predecessors, at a join, or immediately before the next
safepoint. A no-GC loop can keep a repeatedly assigned value only in a
register and issue one store after the loop; a loop containing a safepoint
flushes values changed in each iteration only when that safepoint requires
them.

Because the heap is non-moving, current register copies need no reload after a
collection. If a moving collector is ever adopted, both generated register
caches and raw native argument copies must reload through mutable homes/handles
after safepoints, or the backend needs stack maps. Exact homes are necessary
infrastructure for that future, not a complete moving-GC design.

Non-moving pointer stability does not imply language-binding coherence. A
captured, module, or otherwise shared binding that can be mutated by re-entrant
code must keep its existing environment/cell as the semantic home; a cached
register is invalidated and reloaded after a `can_reenter` call. Side-root
homes protect object lifetime and must not become an independent stale copy of
an aliasable language binding.

### 4.6 Frame hygiene

- **Zero-initialize the reserved slot range in the prologue.** The collector
  scans the whole reserved range, so no slot may hold garbage at any
  safepoint. Zeroing makes the invariant local and retires the current
  store-before-init hazard (JS review R8). Slot counts under §4.4 are small.
- **Frame elision:** a function with zero stable/scratch demand and no
  number-stack use emits no frame at all (JS review R9).
- **Dead-home clearing:** stale values in dead stable homes are safe but can
  cause false retention. Clear them before a later safepoint when liveness
  proves death and retention measurements justify the store; scratch coloring
  naturally overwrites reused slots. Clearing is a memory-retention
  optimization, not part of the safety proof.
- Prologue/epilogue, overflow handling, number-watermark save/restore,
  scalar-return donation, and owned-env re-homing are unchanged from the
  current frame design.

### 4.7 Collector end state

```text
mark registered root slots
mark registered root ranges
mark [side_root_base, side_root_top)      // generated frames + helper frames
mark explicit extra roots
trace the marked object graph
```

No `setjmp()` flush, no native stack bounds, no raw-word stack scan. During
migration the scan remains, governed by the capability mode of §9.1.

## 5. Generated code: shared emitter and per-tier adoption

### 5.1 The shared MirEmitter owns the scheme

Frame lifecycle, home identity, dirty state, and safepoint publication become
`em_*` primitives in
`lambda/mir_emitter_shared.hpp` (`MirEmitter`), which both transpilers already
partially use (`em_store_frame_slot`, `em_store_frame_top`) and which the
unified-AST plan designates as the common emission layer:

```text
em_frame_begin(em, ...)            anchor + late-sized prologue, zero-init
em_stable_home(em, binding, class) fixed slot identity for a binding
em_note_definition(em, home, reg)  records current register; marks home dirty
em_note_temp(em, reg, class)       records a possible scratch-root obligation
em_safepoint_call(em, import, ...) for MAY_GC, flushes the precomputed dirty
                                   live-home/scratch set, then emits the call;
                                   for NO_GC, emits only the call
em_frame_finish(em, ...)           epilogue: env re-homing, scalar donation,
                                   watermark restores, overflow block
```

The shared layer owns the representation/effect metadata, root-use/definition
records, backward safepoint liveness, dirty-state dataflow, scratch-slot
coloring, and final store insertion. Per-language transpilers identify binding
definitions, values, ownership transfers, and calls through these primitives;
they do not scan scopes or emit ad-hoc root stores. This resolves the JS
review's R7 structurally and gives Python and future guests the same policy at
port time.

**CR5 — Rooting policy and final root-store insertion live only in
`MirEmitter`; per-language emitters provide semantic events and may not emit
root stores directly.**

### 5.2 LambdaJS: replace broad publication with safepoint root sets

The current JS implementation already has useful pieces — stable frame
watermarks, late slot sizing, and `jm_set_var()` as a binding-definition path —
but its slots remain shadow copies maintained by broad interception. Migration:

**Delete**:
- `jm_root_live_scope_vars()` — republication before every helper; called
  from `jm_call_with_args`, `jm_call_void_with_args`
  (`js_mir_calls_boxing_types.cpp:142/170`) and the `jm_emit()` call
  interception (`js_mir_hashmap_scope_utils.cpp:547`);
- `jm_root_call_insn_regs()` — representation-blind `I64`/`P` operand/result
  rooting (`:550/:554`, predicate at `:291`);
- `jm_emit_root_updates()` — the post-instruction output-register × binding
  scan (`js_mir_hashmap_scope_utils.cpp:264`);
- duplicate argument/result rooting in `jm_call_with_args()` and
  `jm_call_void_with_args()`.

**Change**:
- slot-per-register → stable slot-per-binding in `jm_set_var()` (kills TDZ
  double-slots, R4), with each definition reported to `MirEmitter` as dirty;
- helper calls route through one effect-aware call path with an explicit
  safepoint root set;
- prologue zero-init; frame elision.

**Add**:
- `can_gc`, `can_reenter`, argument-class, and result-class columns on the
  473-helper import table;
- GC-class use/definition metadata for bindings, call operands, and results;
- backward safepoint liveness, dirty-home tracking, and reusable scratch-slot
  allocation in the shared layer;
- a debug/reference mode that emits always-current write-through homes and is
  differentially tested against optimized write-back emission.

Estimated effect on the review probe is 440 → ~130 MIR instructions; this is a
measurement hypothesis, not an acceptance substitute. The
remaining delta over the 58-instruction pre-stack body is
prologue/epilogue/classifier, separately reducible via return-mode inference
(R10) and elision (R9).

### 5.3 Lambda MIR-Direct: converge onto the same primitives

The core transpiler already has anchor/late-prologue frames and static slot
counts (`transpile-mir.cpp:547–744`) and emits through `em_*` stores. Work:
align its rooting predicate with §4.2, its safepoint root sets with §4.4, and
route frame lifecycle/use-definition metadata through the §5.1 primitives so
the two transpilers share one implementation. Audit for any shadow-copy-style
republication and remove it. Baseline contract: Lambda gtest and
`test-lambda-baseline` results remain identical; emitted MIR may change only in
the reviewed frame/rooting shapes.

### 5.4 Python and future guests

`transpile_py_mir` currently emits **no rooting at all** — its locals survive
on the conservative scan only, and its envs/generator frames are pool-pinned
(Stack_Frame_Python PO2/PO8). Python adopts safepoint-current slots by
construction when it ports onto the unified AST + `MirEmitter`
(`Lambda_Unified_AST_Impl_Plan.md`; `Lambda_Impl_Stack_Frame_Py.md` P1
already routes its frames through the shared em pair). Until then, an active
Python module forces the compatibility mode of §9.1.

**CR6 — A guest language is precise iff it emits through the §5.1 primitives.
No per-guest rooting dialects.**

### 5.5 MIR interpreter

The MIR interpreter executes the same emitted instructions, so frame
prologues, home stores, and epilogues run identically under interpretation —
no separate scheme is needed for interpreted MIR. Its native helper calls are
covered by §7 like any other caller. It gets its own precise-only test lane
(§10.3).

### 5.6 C2MIR

Blocker B1 (§8) stands: current C2MIR emits no precise frames, and the
settled position (OS6) is all-or-nothing — a partial patch would preserve
hidden conservative dependence while hiding it from the gates.

**CR7 — C2MIR remains unchanged and permanently gates the *mode*.** No C2MIR
emission, frame, rooting, or runtime-support code is changed by this proposal.
Executing a C2MIR-compiled module forces the context into compatibility mode
(§9.1), where the existing conservative native-stack scan remains active.
Precise-only contexts and scanner-free builds reject or omit C2MIR. Its current
test suite remains a compatibility regression lane, not a precise-rooting
acceptance lane. This resolves CQ2.

## 6. Import tables as the single source of classification

The helper classification (§4.2 result classes + §4.3 effects) attaches to the
existing import/prototype tables the transpilers already maintain:

```text
{ name, proto, import, can_gc, can_reenter, arg_classes, ret_class }
```

One table serves three consumers: `em_safepoint_call` (whether a root set is
required), representation-aware liveness (`ret_class` and `arg_classes`),
aliasable-binding cache invalidation (`can_reenter`), and the §7.4 audit
tooling (the same effect bits are what the static checker verifies against the
call graph). Classification is data, not code: **no helper
implementation changes are needed for the generated-code migration** —
verified by scan: no runtime helper reads the side-root region (the only
side-stack touch points outside the transpilers are `js_env_rehome_scalars`
and the number-stack range check, `js_runtime_function.cpp:311/330`, both
number-stack machinery), so helpers are indifferent to the caller's
write-back implementation.

The conservative scan may mask a misclassification during migration, but it is
not guaranteed to discover a value retained only in an optimized caller-saved
register. Compatibility mode is therefore a safety fallback, not validation;
per-safepoint root-set inspection, write-through differential mode, and the
precise-only stress gates must catch classification errors before scan
retirement.

## 7. C/C++ runtime helper adaptation

This is what makes retiring the scan from precise execution honest. Helpers
hold raw `Item`s and GC pointers in C locals across allocating calls; today
only the conservative scan protects them (blocker B2). The adaptation applies
the same safepoint invariant through a deliberately simpler **always-current
write-through** C++ form: the local's storage becomes a side-root slot, wrapped
in an RAII type. Native helpers do not implement generated-code dirty/liveness
analysis. Scale to be honest about: `js_runtime.cpp` alone is ~39k lines; plus
`lambda-eval.cpp` (~6.6k), validator paths, and the polyglot runtimes.
Input/format pool/arena paths are excluded by CQ4.

### 7.1 API

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
    ~RootFrame();                       // exact restore on structured exits
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
// collection occurs while alive. Verifies the §4.3 claim dynamically.
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
structured normal/error path; recovery-checkpoint restoration after non-local
jumps; non-copyable RAII in C++; lint-enforced begin/end structure in C;
mutable rooted values; a clear temporary-vs-persistent distinction. Helper
frames and generated frames interleave on the same side-root stack; the
collector scans one interval and cannot tell them apart.

### 7.2 The rules (normative)

- **RH1 — Locals.** Any `BOXED_ITEM`/`RAW_GC_POINTER` local live across a
  `MAY_GC` call must be a `Rooted<T>` (its storage is the home). Locals
  consumed before the next `MAY_GC` call need nothing.
- **RH2 — Arguments are caller-rooted borrows.** Every GC-class argument to a
  `MAY_GC` helper is live for the entire dynamic call, regardless of ordinary
  live-after-call analysis. Generated callers make an existing stable home
  current or publish an unhomed argument to a scratch slot before entry;
  native callers pass `Handle`s backed by their own `Rooted` slots. The callee
  may then use the incoming raw value without duplicating the root under the
  current non-moving collector. This resolves blocker B4 in the cheap
  direction — a helper roots only what it *materializes*, not what it
  receives. The contract becomes load-bearing only at the precise-only flip,
  by which point all caller classes are migrated; until then the conservative
  scan covers any unmigrated caller. Exception: extending an argument's
  lifetime beyond the activation (RH5/RH6).
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
- **RH8 — Non-local recovery.** `longjmp`/`siglongjmp` bypass generated
  epilogues and C++ destructors. Every recovery boundary snapshots
  `side_root_top`, `side_number_top`, and any other runtime-owned dynamic
  watermark before `setjmp`/`sigsetjmp`, then restores them at the landing
  point before allocation, collection, or continued execution. Stack-overflow,
  timeout, MIR-error, batch-crash, JS entrypoint, and event-loop recovery paths
  all use the same checkpoint helper.

### 7.3 What adaptation looks like in practice

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

The per-function audit recipe (applied bottom-up in §7.5's order):

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

### 7.4 Enforcement: classification must be verified, not trusted

Convention alone cannot carry a 39k-line file (blocker B5). Three layers:

1. **Generated effect call graph.** A script over the compile database
   computes transitive reachability to every public GC allocator,
   `heap_gc_collect`, unknown/indirect native calls, and generated-code
   re-entry; emits the classification report; and diffs it against the import
   table's `can_gc`/`can_reenter` bits. Rebuilt in CI; a helper silently
   becoming `MAY_GC` because a callee started allocating, or becoming
   re-entrant because a callee started invoking user code, flips the relevant
   bit and fails the diff. An explicitly verified `try_alloc_no_gc` is the
   only allocator-like exception. (This is SpiderMonkey's hazard analysis
   reduced to the effect half.)
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
3. **Dynamic gates** (§10): shadow-scan reporting, precise-only mode, and
   forced-GC stress with heap poisoning and ASan.

### 7.5 Audit order and cost containment

Bottom-up, leaf allocators first, then callers:

1. core object/container constructors and mutators;
2. shared string, map, array, element, VMap, error, and function helpers;
3. evaluator/interpreter and procedural runtime (`lambda-eval.cpp`);
4. JS runtime helpers (the 473 imported ones by call frequency, transitives
   after);
5. async, scheduler, event-loop, promise, and callback helpers;
6. Python/Ruby/Bash bridges and validator paths that allocate GC memory;
7. Jube/native module boundary (§9, S4);
8. remaining platform and optional-module helpers.

Input and format paths are intentionally outside this GC-rooting audit. Their
ownership model is subsystem-local pools/arenas, not the GC heap (CQ4). Values
allocated from those pools/arenas are governed by their owning builder/parser/
formatter lifetime and must not be treated as GC objects or added to the GC
root stack. This is a design invariant: if a future input/format change starts
allocating GC-heap objects or retains GC-owned values across a `MAY_GC`
boundary, that new boundary must be declared and audited before landing.

Cost is contained by three facts: RH2 removes duplicate **callee-side**
argument roots (the generated caller may still flush a dirty home or use one
scratch store); §7.3 shapes 1–2 (most helpers) need classification only; and
the shadow diagnostics (§10.1) point the audit at frames that actually matter
instead of requiring a blind full sweep.

## 8. Blockers to scan retirement (absorbed ledger)

Inherited from the retired `Lambda_Design_Stack_Frame2.md`; each blocker with
its resolution under this proposal.

| ID | Blocker | Resolution here |
|---|---|---|
| B1 | C2MIR is conservative-scan-only; partial patches invalid | CR7: leave C2MIR unchanged; permanently confine it to compatibility contexts or omit it from scanner-free builds (CQ2 resolved) |
| B2 | Native helpers have no complete local-root contract | §7 API + RH1–RH8 |
| B3 | `LambdaRootGuard` is a pilot, not a discipline | §7.1 upgrades native helpers to always-current slot homes; C ABI added |
| B4 | Helpers can't assume callers rooted arguments | §4.4/RH2 make arguments live through the dynamic call; generated callers flush stable/scratch homes and native callers pass handles |
| B5 | No enforced may-GC/no-GC effect system | §4.3 classification + §7.4.1 CI call-graph diff + §6 single-source table |
| B6 | Evaluator and non-MIR paths use native locals | In audit scope as helpers (§7.5 item 3; CQ1) |
| B7 | Broad persistent roots ≠ temporary-root proof | Persistent storage audited separately (RH5 range rules) from transient locals (RH1/RH3) |
| B8 | Jube modules lack a mandatory handle ABI | §7.1 C ABI; versioned rooting-capable host ABI; old-ABI modules force compatibility mode; module-level forced-GC tests (S4) |
| B9 | Async/callback handoff intervals | RH6 + suspension coverage per SF20 (CQ3) |
| B10 | Raw pointers and boxed Items share machine words | §4.2 representation classes carried as metadata, never inferred from machine type |
| B11 | Tests pass through accidental conservative roots | §10.1 precise-only mode + forced-GC stress as mandatory gates |
| B12 | Mixed activation chains defeat per-entrypoint switches | §9.1 sticky per-context capability mode; per-frame/per-entrypoint switching prohibited |

## 9. Migration plan

Every stage keeps all baselines green. Because performance is the primary
design goal after correctness, S1 is not complete when broad rooting is merely
replaced by a correct slower form; it is complete only when the §11 release
performance gate passes. S1–S2 are independently shippable wins even if later
scan-retirement stages stall.

- **S0 — Tables, analysis scaffolding, and diagnostics.** Import-table
  `can_gc`/`can_reenter`/argument/`ret_class` columns + generation script
  (§6, §7.4.1);
  shared GC use/definition records and per-safepoint root-set dump; compile-time
  counters for stable/scratch slots and inserted root stores; shadow-scan
  reporting and precise-only mode in the collector (§10.1); forced-GC stress
  hooks (§10.2). No behavior change.
  Deliverables also include: the frozen list of GC entry points (CR4), the
  inventory of execution engines/entrypoints, and the list of persistent
  root stores/ranges with lifetimes. Do not begin by deleting
  `gc_scan_stack()`.
- **S1 — LambdaJS safepoint-current slots** (§5.2). First build an
  always-current write-through mode as a correctness oracle, then make
  liveness/dirty write-back + scratch coloring the production mode. Delete all
  broad publication and post-instruction scans. Gates: probe MIR golden/count
  test, differential write-through-vs-write-back execution,
  `regression_side_stack_frame_gc.js` under forced GC + ASan, JS gtests, full
  Test262 timed against the pre-stack release binary, and §11's performance
  budget. The write-through oracle remains debug-only.
- **S2 — Lambda MIR-Direct convergence** (§5.1/§5.3). Lambda MIR-Direct adopts
  the frame, semantic-event, safepoint-analysis, and store-insertion machinery
  already established in the shared `MirEmitter` for S1. Aligns with
  unified-AST Phase 0; Python inherits at its port (P1 of the Py impl plan).
- **S3 — Helper API + core helpers** (§7.1–§7.3). Land
  `RootFrame`/`Rooted`/`Handle`/`PersistentRooted`/`AutoAssertNoGC` + C ABI;
  land the shared RH8 recovery-checkpoint helper and audit all existing
  `setjmp`/`longjmp` boundaries; migrate §7.5 items 1–3; lint ratchet on
  migrated modules.
- **S4 — Subsystem audits** (§7.5 items 4–8) driven by scan-exclusive
  candidate reports (§10.1); JO-ledger de-pinning (RH5); persistent/async
  ownership audit (RH5 range rules, RH6 handoffs, context-reuse resets);
  Jube ABI versioning and module forced-GC tests; verify that unchanged C2MIR
  can only select compatibility mode (CR7/CQ2); Python port lands via
  unified-AST plan. Input/format pool/arena code is excluded by CQ4's ownership
  invariant; only a future explicit GC-heap boundary would enter this audit.
- **S5 — Precise-only as default.** All §10.3 suites run precise-only in CI;
  then precise-only becomes the default in development builds, then in
  release builds with a guarded diagnostic fallback for one transition
  period. Rollout never toggles scanning per-entrypoint (B12) — only via the
  §9.1 context mode.
- **S6 — Retire the scan from precise execution.** In builds without C2MIR,
  remove `setjmp()` register flushing, stop
  reading/passing `stack_current`/GC `stack_base`, drop those parameters from
  `gc_collect_with_root_region()`/`gc_collect()`, remove `gc_scan_stack()`
  and its ASan poison helpers and profiling counters; retain stack-bound
  initialization needed for stack-overflow protection; rerun the complete
  §10 acceptance matrix; measure per §11. Builds that continue to ship
  unchanged C2MIR retain the current scanner only for sticky compatibility
  contexts; it is not a fallback for a failed precise context. What remains
  for precise execution is the **debug shadow-verify mode** (§10.1) as a
  regression diagnostic, where the build includes the scanner for testing.

### 9.1 Compatibility mode (sound transition, B12)

A per-context capability flag, not a per-entrypoint switch: the context runs
precise-only iff **no conservative-dependent tier is active in it** — no
C2MIR module loaded, no unported guest (Python pre-port), no old-ABI Jube
module, no unaudited path flagged by the loader. Loading any such tier sets
`conservative_mode` on the context; the collector then adds the native-stack
scan for that context exactly as today. The flag is sticky per context (mixed
native/generated activation chains make dynamic per-frame switching unsound);
precise-only test lanes assert it stays clear. C2MIR's compatibility status is
permanent under CR7, not a migration backlog item; a scanner-free build must
reject or omit C2MIR before execution begins.

## 10. Verification

### 10.1 Collector diagnostic modes (built at S0, kept forever)

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

### 10.2 Forced-GC stress (legal safepoints only)

Collect at aggressively varied **legal** `MAY_GC` boundaries — never at
invented mid-allocator points the real runtime cannot produce. Schedules:
before every public GC allocation/slow allocation path; every Nth allocation
for several N; randomized deterministic schedules with logged seeds; at callback/async
handoff boundaries; during exception/error propagation; under deep recursion
and large frames. Run both shadow-scan and precise-only variants, with heap
poisoning and ASan.

### 10.3 Per-engine verification and the gate matrix

Per engine: Lambda MIR-Direct (predicates, allocation/call results across
safepoints, dirty-state merges across branches/loops/errors/cleanup, arguments
live through calls, stable/scratch slot lifetime and reuse, elision correctness,
async spill/suspension handoff, main/handlers/view functions);
LambdaJS (same, plus methods, constructors, generators, async functions,
module entrypoints, `js_main`, module loading, promises, events, typed
arrays, Node paths); MIR interpreter (own precise-only lane). Unchanged C2MIR
has a separate compatibility regression lane and is excluded from the
precise-only matrix.

Precise-only suites required before the flip:

```text
Lambda core baseline + extended/unit tests
MIR interpreter mode
JS/Test262 baseline, zero failures, zero retries
Node compatibility baseline
async/concurrency + worker-reuse tests
Python/Ruby/Bash/Jube tests
document/layout paths that execute Lambda/JS (Radiant re-entry, CQ5)
forced-GC stress variants; ASan/UBSan variants
release smoke + performance comparison
```

When the build retains C2MIR, run its existing suite separately with
compatibility mode and conservative scanning asserted active. That suite
guards against regressions but does not gate precise-scan retirement.

Targeted stress must cover: values held across one and several nested
allocations; raw container pointers; fresh values before container insertion;
dirty variables with many no-GC definitions before one safepoint; unchanged
variables across repeated safepoints; scratch-slot reuse; arguments and
returns; errors and cleanup; closures/captures; async
suspension/resumption; callback/event-loop handoff; persistent range
resize/destruction; deep recursion and stack-overflow recovery; worker/context
reuse; GC during module initialization.

**Acceptance criteria for S6:** (1) every scan-exclusive candidate observed
under targeted stress is classified or eliminated; (2) all suites pass
precise-only; (3) the static hazard analysis reports no may-GC hazards;
(4) no execution engine admitted to a precise context remains documented as
conservative-only; and (5) unchanged C2MIR cannot enter a precise context.

Condensed checklist:

- [ ] Lambda MIR-Direct, LambdaJS, MIR-interpreter exact-root audits complete
- [ ] C2MIR compatibility isolation enforced without changing C2MIR code
- [ ] Evaluator/procedural/fallback paths migrated
- [ ] Helper root API complete for C and C++; Jube handle ABI enforced
- [ ] Async + persistent handoffs audited; ranges have stable-address proof
- [ ] `NO_GC`/`MAY_GC` contract enforced transitively (CI diff green)
- [ ] Shadow reporting, precise-only mode, forced-GC schedules, static
      analysis all active in CI; no unexplained scan-exclusive roots
- [ ] Full §10.3 suite matrix green precise-only
- [ ] Scan retired from precise contexts per S6; any retained copy is isolated
      to C2MIR compatibility/debug verification; stack-overflow bounds remain
      functional; docs updated

## 11. Performance is the primary design gate

After correctness, recovering the pre-stack execution profile is the main
reason to adopt this scheme. S1 must remove the measured 7.6x MIR / 2x
Test262 regression while keeping precise rooting; conservative scan retirement
is not allowed to postpone this performance result.

The generated-code cost model is:

```text
root stores = dirty live stable homes at MAY_GC boundaries
            + unhomed live arguments/temporaries published to scratch homes

not proportional to total MIR instructions
not proportional to all lexical bindings
not proportional to definitions in no-GC regions
not proportional to every allocation fast path
```

Structural performance requirements:

- no post-instruction binding scan;
- no full-scope publication before calls;
- no representation-blind `I64`/`P` rooting;
- no republishing a current unchanged home;
- no rooting MIR around `NO_GC` calls;
- stable physical-slot count reflects the maximum overlapping rootable binding
  lifetimes, and scratch count reflects the maximum overlapping safepoint live
  set rather than total bindings/temporaries;
- prologue zeroing uses unrolled stores for small frames and a verified
  no-GC bulk clear for larger frames, selected by measurement;
- side-stack capacity is ensured once per frame, not once per root store.

Suggested release budget, measured on the same machine and harness against a
preserved pre-stack release binary:

```text
target      Test262 median wall time within 5% of pre-stack
hard gate   no more than 10% slower without a separately approved,
            measured GC benefit
correctness zero failures and zero unexpected retries
```

Use repeated runs and report sync, async, compilation, execution, GC, and
retry components separately; a single wall-time sample is not an acceptance
measurement.

- **Performance now** (S1): recover generated MIR/code size and execution
  time through safepoint-current write-back.
- **At retirement from precise execution** (S6): per-collection work
  proportional to native stack depth (`O(words between SP and stack_base)`)
  eliminated; no `setjmp` flush;
  no false retention from stale stack words (less mark/survivor work); ASan
  without poison workarounds; GC correctness decoupled from optimizer spill
  choices (the JS review's P1 fragility). Retirement does *not* by itself
  reduce generated-code cost — that is S1's job; the two improvements are
  independent and complementary.
- **Option value:** exact, mutable root slots provide the root locations a
  moving or generational collector would need. Register reloads, native handle
  ABI changes, derived-pointer metadata, field rewriting, and barriers remain
  separate prerequisites.

Measure (release builds only), before/after each stage and at S6: total
execution time; GC count and total pause; native-stack bytes formerly
scanned; side-root slots scanned; objects marked only by the conservative
pass; live bytes after collection; peak/steady RSS; generated MIR/code size;
inserted/executed root stores; stable/scratch slots per function; Test262
sync/async/compile/execute wall time.

## 12. Explicitly unsafe actions

Do not, at any stage:

- comment out `gc_scan_stack()` and rely on normal tests (B11);
- assume all MIR locals are spilled to the native stack, or that `setjmp`
  captures every caller-saved live value;
- assume a MIR caller roots every native helper argument before that caller
  has migrated to §4.4/RH2 (the end-state contract is not a current fact);
- treat registered globals as coverage for local temporaries;
- root raw backing-data pointers instead of their owning GC object;
- disable the scan per-entrypoint while unaudited helpers can be active (B12);
- keep current C2MIR enabled under a precise-only collector (B1);
- use ad-hoc `volatile` locals as a rooting mechanism;
- add more conservative pointer heuristics as a substitute for explicit
  roots;
- classify an existing GC allocator `NO_GC` merely because its current fast
  path happens not to collect;
- keep full-scope/call-operand publication or the post-instruction binding
  scan under new names;
- ship always-current generated-code write-through as the production mode if
  it misses the §11 performance budget;
- delete `_lambda_stack_base` without preserving stack-overflow handling;
- declare completion before the forced-GC precise-only matrix passes.

## 13. Risks

- **The §7 audit is the schedule risk** — it is the multi-quarter tail
  (SpiderMonkey/V8 precedent). Mitigations: RH2 (arguments free), §7.3 shapes
  1–2 (most helpers are classification-only), scan-exclusive candidates as a
  worklist, per-context compatibility mode so value ships incrementally
  (S1/S2 pay for themselves regardless of whether S6 is ever reached).
- **A missed hazard after the flip is a use-after-free.** Mitigations: the
  three §7.4 layers, poisoning + forced-GC in CI permanently, the shadow
  collector mode retained in debug builds, staged rollout with a guarded
  fallback period (S5).
- **Contract drift**: a new helper or trigger violating CR4/RH rules.
  Mitigations: the CI call-graph diff (§7.4.1) and a frozen-trigger assertion
  (debug check in `gc_data_alloc`/collect entry that a `MAY_GC` region is
  active).
- **Generated root-analysis error**: an incorrect liveness, dirty-state merge,
  or scratch-slot reuse can omit a live root. Mitigations: conservative merges,
  unique-slot/write-through reference mode, per-safepoint root-set dumps,
  differential execution, and forced-GC precise-only tests across loops and
  exceptional control flow.
- **Helper-frame overhead**: one `RootFrame` bind + restore per adapted
  helper activation. §7.3 shapes 1–2 should pay nothing, and caller-rooted
  arguments avoid duplicate slots, but overhead is measured rather than
  assumed negligible. Benchmark gates (deltablue, havlak+push, AWFY, Test262
  phase timing) ride every stage.

## 14. Decision ledger

| ID | Decision | Status |
|---|---|---|
| CR1 | Safepoint-current canonical slots = generated-code rooting scheme | proposed |
| CR2 | C/C++ helpers use always-current slot-backed handles on the same stack | proposed |
| CR3 | Stable homes + liveness/dirty write-back adopted; broad shadow publication rejected; stack maps KIV | proposed |
| CR4 | Safepoint contract frozen: GC begins only inside `MAY_GC` calls; async collection prohibited | proposed |
| CR5 | Root analysis/store insertion implemented once in `MirEmitter`; transpilers report semantic events | proposed |
| CR6 | A guest language is precise iff it emits through the shared primitives | proposed |
| CR7 | Leave C2MIR code unchanged; it permanently forces compatibility mode and is unavailable in precise contexts/scanner-free builds | **accepted (CQ2)** |
| CR8 | Input/format allocations remain pool/arena-owned and outside GC rooting; any future GC-heap boundary requires an explicit audit | **accepted (CQ4)** |

## 15. Clarifications and open questions

- **CQ1 — Interpreter/evaluator locals.** `lambda-eval.cpp` and the
  procedural runtime hold Items in C++ locals like any helper; confirm they
  are covered by the §7 audit scope (they are "helpers" for this purpose) and
  identify any evaluator-specific activation state needing persistent roots
  (B6).
- **CQ2 — RESOLVED: C2MIR compatibility tier** (CR7). Leave all C2MIR code
  unchanged. C2MIR always uses conservative compatibility mode and cannot run
  in a precise-only context or scanner-free build.
- **CQ3 — Generator/async resume homes.** Verify suspended state is fully
  owned by traced activation objects (`gen_env`, LambdaAsyncFrame per SF20)
  and homes are re-established per resume activation; add a forced-GC test at
  every suspension boundary.
- **CQ4 — RESOLVED: Input/format ownership** (CR8). Input and format paths are
  designed to allocate through their own pools/arenas rather than the GC heap,
  so they are outside the GC-root audit. Introducing a GC allocation or a
  retained GC-owned value there is an ownership-model change and requires an
  explicit boundary audit.
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
- **CQ8 — Root-analysis placement.** Whether use/definition collection and
  backward safepoint liveness run over MIR before final insertion or over a
  compact side IR owned by `MirEmitter`. The choice must preserve semantic GC
  classes through control-flow joins and keep per-language emitters free of
  rooting policy.
- **CQ9 — Scratch coloring threshold.** Whether S1 ships unique scratch slots
  first and enables coloring after profiling, or colors immediately. Either
  way, precise-only correctness cannot depend on aggressive reuse; conservative
  fallback is a fresh zero-initialized slot.

## 16. Source map

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
