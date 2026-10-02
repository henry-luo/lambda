# Lambda Implementation Plan: AST Interpreter Tuning, Round 2

**Date:** 2026-10-02
**Status:** IN PROGRESS — Items 1–3 implemented and Items 5–6 partly, on
branch `worktree-interp-tune2` (§12); Item 4 not started. Loop-head handoff,
its threshold and the retirement of the first-entry trigger were ruled by the
user on 2026-10-02 (§4) and are recorded as **D8.1.1v14** and AI23.
**Source baseline:** `c886322fd` (source reading and profile attribution);
archived release executable `test/benchmark/exe/lambda-v50-a9489bc329`
(timings). `interp.cpp` differs by 11 lines between the two.
**Scope:** Lambda's boxed AST interpreter (T0) in `lambda/runtime/interp.cpp`
and `interp_plan.cpp`, and the AUTO promotion edge that decides when a
definition leaves it. The LambdaJS walker is out of scope for this round.

**Formal authority:** [Lambda Formal Design](../../doc/Lambda_Formal_Design.md)
**D8.1.1v14** (tiering, asynchronous promotion *v12*, loop-head handoff
*v14*), **D8.4.1v2** (no inline caches), **D8.2.4v2, D8.2.5v3** (fact
placement, pass manager), **D5.3.3** (precise roots), **D6.2.2v2** (dynamic
call dispatch), **D3.3.1v2** (result identity under type erasure), **DO25**
(interpreter-tier opens); [Lambda Formal Semantics](../../doc/Lambda_Formal_Semantics.md)
**S1.6**, **S7.7.2**, **S9.1.2**.

**Related work:** [Round 1 plan](Lambda_Impl_Interp_Tune.md) (shared
Lambda + LambdaJS, in progress); [interpreter design](../Lambda_Design_Ast_Interpreter.md)
(AI1–AI23, AIO1–AIO13); [COW design](../Lambda_Design_Runtime_COW.md) (CW33,
the `var`-parameter home ABI); [interpreter implementation record](<Lambda_Impl_Ast_Interp (done).md>).

This is an informative implementation plan, not a ruling. Item numbers are
work sequencing, not a design-ledger series. Open design questions are cited by
their existing IDs (AIO6, AIO10, AIO13, DO25).

## 1. Summary

Two separate problems limit T0 today, and they need different fixes.

1. **AUTO does not rescue once-called loop owners.** `pn main()` with the
   workload's loops inline is compiled at its first entry in about 16 ms, but
   the image is published only at a later function entry. The activation that
   triggered it runs to completion in T0. `mandelbrot2` and `matmul2` therefore
   run at interpreter speed under the default tier, about 250× slower than JIT.
   Forcing publication at the trigger (`LAMBDA_SATELLITE_SYNC=1`) removes the
   gap entirely (§2.3). The fix ruled for this is loop-head handoff: the
   running activation transfers to its compiled image at a loop head (§4).
2. **T0 repeats static work on every node.** Formatting diagnostic strings
   that are read only on failure costs 15–29% of interpreter time. Contract
   classification, call-shape dispatch and identifier classification are
   likewise re-derived per evaluation. Typed code is 16–46% *slower* than
   untyped code in T0 as a result.

Items 2–6 are expected to make T0 roughly 2–3× faster on the measured
kernels. That figure is a projection from profile shares, not a measurement.
Item 1 is worth more than all of them for the workloads it covers.

## 2. Evidence

### 2.1 Method and limits

- Timings are user-CPU seconds from `/usr/bin/time -p`, one sample each, taken
  on a machine under heavy unrelated load (load average 25–90). User-CPU is
  less sensitive to load than wall time but is still inflated: `ack2` measured
  3.62 s here against 1.42 s wall in the idle Round 1 capture. Ratios between
  tiers are the usable result. Absolute numbers are not a baseline.
- Profiles are 6 s `sample` captures at 1 ms on the main checkout's debug
  configuration, which is `-O3 -g` with frame pointers and logging disabled by
  `--no-log`. Its interpreter user-CPU is within about 15% of the release
  binary. Local symbols are stripped, so all `interp.cpp` static functions
  appear as one "walker self" bucket.
- Every child set `LAMBDA_TIER` explicitly and `LAMBDA_NO_LOG=1`.
- Before any item lands, Round 1's Phase 0 protocol applies: idle machine, one
  warm-up and five samples, preserved before/after release binaries
  ([Round 1 §5](Lambda_Impl_Interp_Tune.md#5-phase-0--freeze-a-trustworthy-baseline)).

### 2.2 Tier comparison

User-CPU seconds, `lambda.exe run <fixture>` under `test/benchmark/`.

| Fixture | `interp` | `auto` | `jit` | interp / jit |
|---|---:|---:|---:|---:|
| r7rs/ack2 | 3.62 | 0.02 | 0.03 | 121× |
| beng/mandelbrot2 | 11.14 | **10.96** | 0.04 | 279× |
| kostya/matmul2 | 7.41 | **7.06** | 0.03 | 247× |
| awfy/richards2 | 8.10 | 0.68 | 0.23 | 35× |
| awfy/deltablue2 | 2.95 | 0.65 | 0.26 | 11× |
| larceny/pnpoly2 | 3.52 | 0.15 | 0.02 | 176× |
| beng/nbody2 | 2.40 | 0.17 | 0.09 | 27× |
| kostya/levenshtein2 | 1.92 | 0.48 | 0.03 | 64× |

### 2.3 AUTO with asynchronous and synchronous publication

Second capture, same binary, load average about 25. `sync` sets
`LAMBDA_SATELLITE_SYNC=1`, the existing test hook that waits for the queued
image and publishes it at the triggering promotion.

| Fixture | `auto` (shipped) | `auto` + sync |
|---|---:|---:|
| beng/mandelbrot2 | 7.19 | 0.02 |
| kostya/matmul2 | 4.58 | 0.02 |
| kostya/levenshtein2 | 0.30 | 0.05 |
| larceny/pnpoly2 | 0.10 | 0.03 |
| beng/nbody2 | 0.12 | 0.06 |
| awfy/deltablue2 | 0.44 | 0.29 |
| awfy/richards2 | 0.43 | 0.38 |
| r7rs/ack2 | 0.01 | 0.01 |

The run log for `mandelbrot2` under the shipped policy shows the promotion
happening and going unused:

```text
interp-tier: queued satellite function='main' image=1 pool_workers=4
interp-tier: satellite image function='main' members=1 compile_ms=16.2
```

No `published queued satellite` line appeared in the 6 s the run was observed.
`interp_satellite_publish_ready` runs only at an interpreted function entry,
and `main` is not entered again.

### 2.4 Typed and untyped variants in T0

| Fixture pair | Untyped | Typed | Typed cost |
|---|---:|---:|---:|
| r7rs/ack → ack2 | 2.78 | 3.24 | +17% |
| beng/mandelbrot → mandelbrot2 | 6.44 | 9.09 | +41% |
| kostya/matmul → matmul2 | 4.19 | 6.12 | +46% |
| awfy/richards → richards2 | 5.51 | 6.57 | +19% |

Annotations make JIT code faster and T0 slower. Each declared binding adds a
formatted boundary string and a full contract classification per evaluation.

### 2.5 Where T0 time goes

Share of interpreter-thread samples. Inclusive unless marked self.

| Cost | ack2 | mandelbrot2 | matmul2 | richards2 |
|---|---:|---:|---:|---:|
| `snprintf` of boundary strings | 18.8% | 28.6% | 20.2% | 15.6% |
| Walker self (`interp.cpp` statics) | 36% | 45% | 48% | 38% |
| `lambda_type_check_env` | 9.0% | 7.4% | 7.5% | 4.2% |
| `unwrap_simple_type_type` (self) | 2.9% | 3.4% | 2.5% | 1.0% |
| `lambda_array_contract_info` (self) | 1.2% | 1.5% | 1.4% | 1.6% |
| `malloc`/`free` on the call path | ~8% | — | — | ~7% |
| Arithmetic and compare helpers | ~2% | ~10% | ~13% | — |
| GC | 1.8% | 0.7% | 1.6% | 1.0% |
| `map_get` | — | — | — | 7.4% |
| `cow_path_set` + key-array growth | — | — | — | ~10% |

Native frame sizes on arm64, read from `build/obj/lambda/native/debug/interp.o`:

| Function | Frame | Notes |
|---|---:|---|
| `eval_expr` | 848 B | 12 callee-saved registers and a stack-protector canary on every node |
| `eval_call` | 992 B | |
| `interp_call_internal` | 720 B | |
| `eval_binary` | 160 B | |

One Lambda call nests `eval_expr` → `eval_call` → `fn_call_into` →
`lambda_dynamic_call` → `interp_call` → `interp_call_internal` → `eval_expr`.
`ack(3, 8)` makes about 2.8 million calls, so the idle 1.42 s figure is about
0.5 µs per call. That is a derived estimate.

## 3. Findings

**F1 — First-entry promotion is defeated by asynchronous publication.**
`interp_promote_function` computes `loop_first_entry` for a loop-bodied
procedure and promotes at its first entry, as **D8.1.1v13** *(v7)* required.
Its own comment names mandelbrot and matmul as the reason. Since `4f304cd83`
(2026-09-22, *v12*) promotion enqueues the target and returns `false`, and
`interp_satellite_publish_ready` adopts an image only at a later function
entry. For a once-called loop owner there is no later entry.

**F2 — Boundary strings are formatted on success.** Three sites build a
string that only a failed check reads:

- `interp_bind_declared_value` formats `declaration '<name>'` for every
  `let`/`var`, typed or not.
- The `AST_NODE_ASSIGN_STAM` arm formats `assignment to '<name>'` for every
  assignment to a declared binding.
- `interp_format_parameter_boundary` runs per parameter per call, for untyped
  parameters too. It allocates and frees a `StrBuf`, calls `write_fn_name`,
  then `snprintf`. Its two caller loops recompute the parameter index by
  walking the list from the head, which is quadratic in parameter count.

**F3 — Contract shape is re-derived per bind.** `interp_coerce_declared_binding`
runs `interp_type_uses_binder` (a recursive type walk),
`lambda_array_contract_info`, `ast_declared_type_is_map`,
`unwrap_simple_type_type`, `interp_declared_optional_array` and
`lambda_numeric_kind_from_type` before it reaches `lambda_type_check`. All of
these depend only on the declared type.

**F4 — Every call takes the general path.** `eval_call` tests its special
cases in order on each evaluation: self-tail call, named arguments, type
conversion callee, `print`, system function, mutating system function, method
member. An ordinary direct call then evaluates the callee to a `Function`
value, fills an argument span, and enters through `lambda_dynamic_call`.
`interp_call_internal` then checks the satellite queue, scans for rejected
parameters, zeroes a `source_was_error` array, opens the frame, and makes
three passes over the parameter list.

**F5 — Per-node overhead is high for trivial nodes.** `eval_expr` is one
function holding every node kind, so a literal or a local read pays the
848-byte frame, the register saves and the canary. Before dispatch it also
writes `f->cur`, increments `node_count`, tests the evaluation mode and runs
the `interp_const_folded_value` kind switch. A parenthesised expression is a
`PRIMARY` node that dispatches twice. An identifier read tests binder, type
definition, object type and pattern declarations, then `CONST_FOLD` mode, and
only then reaches `interp_read_binding_at_capture_slot`, which walks the view
binding list and tests import, object field, storage class and capture before
the slot load.

**F6 — Nested member writes allocate a key array.** The compound-assignment
arm builds an `array_plain()` and pushes each path key for every
`a.b.c = v`, then calls `cow_path_set`. The path length is static and bounded
by `AST_COW_PATH_MAX`.

**F7 — Boxed arithmetic is not the main cost.** The arithmetic and compare
helpers total 2–13%. GC is under 2%. An existing integer loop fast path
(`interp_fast_int_while`) already covers the pure-`int` case.

## 4. Ruling: loop-head handoff

**Ruled by the user on 2026-10-02: option (b).** A T0 activation that is
already running may transfer to compiled code at a loop-head test. This
reversed the interpreter design's "no general OSR" non-goal (n2) and the
**D8.1.1** *(v12)* clause "no active interpreter frame is replaced".

The question arose because **D8.1.1v13** carried two statements that disagreed
for a once-called loop owner: *(v7)* "a procedure whose body owns a loop
statement promotes at its **first** entry", and *(v12)* "the triggering call
and every call before publication stay T0". The implementation follows *v12*,
so the *v7* trigger compiles code the triggering activation cannot use (F1).

| Option | Behavior | Outcome |
|---|---|---|
| (a) Wait at the loop-bodied first entry | The entering call blocks for its compile, then enters native code. | Not chosen. It compiles every loop-bodied procedure whether or not its loop turns out hot. |
| **(b) Loop-head handoff** | The running activation transfers at a loop-head test once its image is published. | **Chosen.** |
| (c) Leave as is | Once-called loop owners need `LAMBDA_TIER=jit`. | Not chosen. |

### 4.1 What was ruled, and where it is recorded

All three points were ruled by the user on 2026-10-02:

- Handoff of a running activation at a loop head is permitted.
- The loop threshold defaults to 10,000 back-edges of one loop site (§4.3).
- The loop-owner first-entry trigger is retired (§4.4).

They are recorded, and not restated here, in:

- [Formal Design](../../doc/Lambda_Formal_Design.md) 16.0.0: **D8.1.1v14**,
  its Appendix A row (not implemented), and **DO25**, which now names AIO13
  in place of AIO11.
- [Interpreter design](../Lambda_Design_Ast_Interpreter.md) §5.1.1: AI23, the
  options and the reasoning; AI8v2; AIO13; superseded wording in its
  Appendix S.

### 4.2 Shape: whole-function continuation, not an outlined loop

Two shapes fit the ruling. This plan takes the first.

| | Whole-function continuation | Outlined loop |
|---|---|---|
| Compiled unit | The definition, with one extra entry at the loop head | The loop statement as a synthetic function over the frame's locals |
| After the loop ends | Compiled code runs the rest of the activation, enclosing loops included | Control returns to T0 |
| Transfers per activation | One | One per entry of the loop |
| Exit protocol | A result value; T0 signals `RETURNED` | Status code for normal exit, `break`, `return`, error; write-back of every modified local |
| Enclosing constructs | Must hold no T0 state outside named slots | Any |

The continuation shape is chosen for three reasons:

- In a loop nest the innermost loop gets hot first. With an outlined inner
  loop, `mandelbrot2`'s per-pixel statements and both outer loops stay in T0,
  and each outer level later compiles its own copy of the inner loops. The
  continuation carries the whole nest in one image.
- It needs no exit protocol and no write-back. `return`, `raise` and
  declaration-boundary skips are lowered as in any function body.
- It reuses function lowering, including the admission rules P2 already has.

Its cost is that a loop-triggered image lowers the body twice, once behind the
normal entry and once behind the loop entry.

### 4.3 Trigger threshold

**Ruled default: 10,000 back-edges of one loop**, counted per loop site and
accumulated across activations, under the existing `LAMBDA_JIT_BACKEDGE` knob
(shipped today as 1024, counted per definition). The user set it on
2026-10-02, asked for the break-even to be measured, and the measurement
below supports it.

**Break-even** is the rent-or-buy point: the loop length at which interpreting
the whole loop costs the same as compiling the definition and running it
natively. Triggering there bounds the total at twice the optimum.

**Method.** A once-called `pn main()` whose loop runs N times, N from 0 to
500,000, run two ways on the v50 release binary: `LAMBDA_TIER=interp`, and
`LAMBDA_TIER=auto LAMBDA_SATELLITE_SYNC=1`, which compiles `main` at its first
entry. The metric is total process CPU (user + system, all threads, so worker
compile time is included), minimum of 7 runs, fitted linearly in N. Load
average was 12–20 during the capture; it inflates both sides alike.

| Probe body | T0 per iteration | Native per iteration | Compile overhead | Crossover N | Crossover if compile doubles |
|---|---:|---:|---:|---:|---:|
| A: typed `float[]` inner product (33 nodes) | 0.74 µs | ~0 | 4.4 ms | 6,000 | 12,000 |
| B: typed float recurrence, 8 statements (55 nodes) | 1.41 µs | ~0 | 4.4 ms | 3,200 | 6,300 |
| C: untyped integer counter (19 nodes) | 0.28 µs | ~0 | 1.4 ms | 5,000 | 9,900 |
| D: map member updates and a call (48 nodes) | 1.51 µs | 0.13 µs | 12.8 ms | 9,300 | 18,500 |

Compile overhead for the real benchmark loop owners, measured the same way
with their size constant set to 0 so the loops do not run:

| Script | Definitions compiled | Compile overhead |
|---|---:|---:|
| `mandelbrot2` | 1 (`main`, 60 lines) | 8.3 ms |
| `matmul2` | 3 | 15.2 ms |

Reading:

- Measured crossover is 3,000–9,300 back-edges with one body lowering. A
  loop-triggered image lowers the body twice, so its crossover lies between
  that and the doubled column, 6,300–18,500. **10,000 is inside that range.**
- 50,000, the estimate this plan first gave, is wrong. It used an idle
  per-iteration figure of 0.33 µs, which fits only the untyped counter, and
  compile times of 9–40 ms taken from the debug-configuration binary under
  heavy load. Typed bodies cost 0.7–1.5 µs per iteration in T0, and release
  compiles of these definitions cost 1.4–15 ms.
- 1,024 is below every measured crossover.
- The crossover moves when T0 gets faster. Items 2–5 target a 1.3–2× lower
  per-iteration cost for typed bodies, which raises the crossover by the same
  factor. Re-run this measurement after they land.

The probes are small definitions. A long definition costs more to compile for
the same loop, so its crossover is higher; the Phase 1.1 census covers that
with real corpus data. Two refinements are recorded as AIO13 and adopted only
if the census shows a need:

- **Work-weighted counting.** A back-edge of a 300-node body is not the cost
  of a back-edge of a 20-node body. Probe B crosses at half the count of
  probe A for the same compile cost. Adding the loop body's static node count
  per back-edge and thresholding on node visits evens that out.
- **A per-Script cap** on loop-triggered images, since each is a private MIR
  context (§5).

### 4.4 The loop trigger replaces the first-entry trigger

`interp_promote_function` treats a definition as hot at its first entry when
`interp_body_has_loop` finds an `AST_NODE_LOOP`, `AST_NODE_FOR_EXPR`,
`AST_NODE_FOR_OF_STAM` or `AST_NODE_FOR_IN_STAM` in its body. Every such
definition is queued the first time it runs, however short its loop. With
handoff available, that trigger has no remaining purpose and is the main
source of unneeded images. Ruled by the user on 2026-10-02:

- Remove the first-entry trigger and the per-definition 1,024 next-entry
  marking.
- A definition is queued by the call threshold (5), the self-tail threshold
  (5), or one of its loops reaching the loop threshold.
- A loop-triggered image publishes the normal boxed entry too, so later calls
  enter natively.

This changes shipped AUTO behavior for definitions called a few times with
short loops: they stay in T0 until the fifth call.

## 5. How satellites are managed today

Read from `interp.cpp` and `transpile-mir.cpp` at the source baseline.

- **One private MIR context per image.** `compile_ast_function_satellite_image`
  calls `jit_init` for every image, so each has its own `MIR_context_t` and
  code generator. The owning `InterpSatelliteImage` records the context, the
  module-layout BSS, the target definition and the member entry pointers.
- **Queued images are snapshots of one definition.** A worker gets a private
  pool, name pool and cloned const and type lists, and lowers only the target.
  Direct-callee clusters (up to `INTERP_SATELLITE_CLUSTER_CAP`, 64) are built
  only on the synchronous path, which today serves task-backed procedures.
  An image whose lowering grew the const or type list is rejected and its
  definition pinned to T0.
- **Workers are process-wide; queues are per Script.** The pool has 4
  low-priority threads (`LAMBDA_SATELLITE_THREADS`). Each Script has an
  `InterpSatelliteQueue` with a generation number, so teardown cancels work
  in flight. `MirNativeCodegenLock` holds one global mutex for the whole image
  compile, so native images are compiled one at a time regardless of pool
  size.
- **Publication happens on the evaluator thread.** `interp_satellite_publish_ready`
  runs at interpreted function entries. It binds the image to the Script's
  module state, appends its property keys, retains it in
  `script->interp_satellite_images`, and writes the `_b` entry into the
  promotion cell, from which `Function` values pick it up.
- **Lifetime is the Script's.** There is no eviction and no cap on image
  count. A failed, stale or cancelled image is destroyed with its context.

Observed image counts under AUTO, debug-configuration binary, loaded machine:

| Fixture | Images (contexts) | Compile ms: median / max / total |
|---|---:|---|
| awfy/richards2 | 20 | 4.6 / 20.9 / 113 |
| awfy/deltablue2 | 48 | 4.5 / 22.6 / 332 |
| kostya/matmul2 | 3 | 9.0 / 9.3 / 19 |

The memory cost of one context has not been measured. It is a Phase 1.1
deliverable, because it decides whether the per-Script cap in §4.3 is needed.

## 6. Work items

Order is by measured effect per unit of risk. Items 2–6 do not depend on
Item 1.

### Item 1 — Loop-head handoff

Covers F1. Implements the §4 ruling in the §4.2 shape.

#### Contract

- **Trigger.** A loop site's back-edge counter reaches the loop threshold
  while its definition is still interpreted and unqueued. The definition is
  queued once, with that loop as its handoff loop.
- **Image.** The worker lowers the definition twice into one image: the
  normal body behind `_b`, and a loop-entry variant
  `Item <fn>_l(Context*, Item* slots, bool* accepted)`.
- **Loop entry.** Its prologue loads every local that is live at the loop
  head from `slots`, converts each to the representation the body uses, and
  jumps to the loop-head test. If any conversion guard fails it sets
  `*accepted = false` and returns before any effect. Otherwise it runs the
  activation to completion and returns the function result.
- **Handoff.** At a head test of the handoff loop, once the image is
  published and the frame guards hold, T0 calls the loop entry through the
  boxed-call result protocol. On acceptance it signals `RETURNED` with the
  result, and every enclosing walker arm unwinds as for a `return` statement.
  On refusal it marks the loop declined and continues interpreting.
- **One-way.** Compiled code never re-enters the interpreter mid-activation.
  The T0 frame stays on the stack, rooted and unused, until the loop entry
  returns.

#### Eligibility, decided once in the frame-plan pass

A loop is a handoff candidate only if all of these hold. Anything else stays
in T0, as do the loops of a definition P2 refuses.

- The definition passes `interp_satellite_refusal`.
- The loop is an `AST_NODE_LOOP` in test-first form. `for` statements and
  `for` expressions hold iterator and accumulator state outside named slots.
- Every ancestor between the loop and the function body is a statement block,
  an `if` statement or another test-first `AST_NODE_LOOP`. This excludes
  pipes, `~` contexts, handlers, match arms and any expression position.
- It is among the first 8 such loops of the definition, in source order. The
  cap bounds the per-definition counter array.

Run-time guards at the handoff point, each a field compare:

- The frame's scratch window is empty (`scratch_top == scratch_base`).
- No signal is pending and the evaluation mode is `RUNTIME`.
- `var_marked_mask == 0`: the body has not shared a `var` parameter that a
  later write must detach (LR12-10).
- The runtime is not in UI mode, matching the existing promotion pin.

#### Phases

**1.0 Rulings into the specs.** Done 2026-10-02 (§4.1).

**1.1 Census and baseline.**

- Add diagnostic counters, off in timed runs: back-edges per loop site at
  script exit, images queued and published, and resident-memory delta per
  published image.
- Run the `test/lambda` corpus and the benchmark set. Report how many loop
  sites cross 1,024, 10,000, 50,000 and 100,000, and how many definitions the
  first-entry trigger queues today.
- Exit: the crossing counts and the per-image memory figure exist, with a
  recommendation to keep or change the 10,000 default.

**1.2 Plan facts** (`interp_plan.cpp`, `ast-core.hpp`).

- Assign each eligible loop a `handoff_ordinal` on its `AstLoopControlNode`;
  ineligible loops get none. These are immutable AST facts (**D8.2.4v2**).
- Record, per eligible loop, the set of named slots declared on any path
  before the loop head. A slot in the set is loaded by the loop entry; the
  rest start as the body's own declarations leave them.
- Extend `FnPromotionCell` with `loop_backedges[8]`, the handoff loop's
  ordinal, the loop-entry pointer and a per-loop declined bit. The cell is
  per-execution state, including its overlay for cached ASTs.

**1.3 Lowering the loop-entry variant** (`transpile-mir.cpp`). This is the
largest and least certain phase. Start with a spike that answers the
questions below before committing to the design.

- Emit a second MIR function for the target with the same body lowering, a
  prologue that fills the live-in locals from `slots`, and an initial jump to
  the handoff loop's head label. Code that only the normal entry reaches is
  left to MIR's unreachable-block removal.
- Each live-in local is converted with the same admission the `_b` wrapper
  applies to a boxed argument: the declared contract, then the exact-shape
  guard for any lane the body infers. A miss refuses the handoff.
- **Static facts at the loop head must be joined with "unknown".** MIR Direct
  carries flow-sensitive facts about locals, notably COW ownership and
  uniqueness, and may omit a `cow_prepare_write` it has proven unnecessary
  along its own paths. T0 keeps the same program correct by a different
  route: it marks a value shared and detaches at the first write
  (**S9.1.2**). A container local arriving from T0 may therefore be shared
  where compiled code would have copied earlier. At the handoff loop's head,
  every live-in container local takes the facts of a value parameter arriving
  from an unknown caller. Getting this wrong writes through an alias, so it is
  the phase's primary correctness risk.
- `var` parameters: pass `&slots[i]` as the CW33 home, so the compiled
  epilogue stores the final value where T0's own epilogue publishes from.
  A typed `var` parameter is written in place, as in any satellite.
- Wide scalars in `slots` point at number-stack payloads the T0 frame owns.
  They stay valid because that frame outlives the call. The result returns
  through the existing `result_home` protocol.

Spike questions:

- Can one body lowering be entered at an interior label, or does the
  semantic-root write-back and liveness pass assume a single entry?
- Where are per-local COW and lane facts joined at loop heads, and can a
  third incoming state be added there?
- Does lowering the body twice in one module collide on symbols, const
  slots or property keys? A snapshot image is rejected on const or type
  growth today.
- How much does the second lowering add to compile time and code size?

**1.4 Image and publication** (`transpiler.hpp`, `transpile-mir.cpp`,
`interp.cpp`).

- `InterpSatelliteJob` and `InterpSatelliteImage` carry the handoff loop and
  its entry pointer.
- `interp_satellite_publish_image` writes the loop entry into the cell beside
  the boxed entry. An image whose loop entry failed to link still publishes
  its boxed entry.
- Enforce the per-Script cap here if Phase 1.1 calls for it.

**1.5 Handoff in the walker** (`interp.cpp`, the `AST_NODE_LOOP` arm).

- `interp_note_backedge` increments the loop's own counter. Cache the cell
  pointer in the frame; today each back-edge calls `interp_promotion_cell`,
  which for a cached AST is a linear overlay search.
- When the counter crosses the threshold, queue the definition with this loop
  as the handoff loop.
- At the head test, if the definition is queued or compiled with a loop entry
  for this loop: poll for a ready image without taking the queue mutex on
  every iteration (an atomic ready count, or every 256 back-edges), publish,
  check the guards and hand off.
- `interp_fast_int_while` runs its own loop and must return to the general
  loop when a loop entry becomes ready, or a once-entered integer loop never
  hands off.
- `interp_call_internal`'s epilogue then runs unchanged. Its return-contract
  check re-admits a value the compiled body already admitted; confirm that is
  idempotent for every contract class, or skip it for a handed-off frame.

**1.6 Retire the first-entry trigger.** Remove `loop_first_entry`,
`loop_bodied` and `interp_body_has_loop`, and the per-definition 1,024
marking, per §4.4.

**1.7 Stress mode and tests.**

- `LAMBDA_JIT_BACKEDGE=1` with `LAMBDA_SATELLITE_SYNC=1` makes every eligible
  loop hand off at its second head test, deterministically. The whole
  `test_lambda_gtest` corpus runs in this mode as a differential against the
  interpreter goldens. This is the gate for **S1.6** and **D3.3.1v2**.
- Focused tests: a container aliased before the loop and written inside it; a
  local whose value misses its inferred lane; `return`, `raise`, `break` and
  `continue` from nested loops after handoff; a `var` parameter written after
  handoff and read by the caller; a loop nested in a `for` expression (must
  stay in T0); handoff under forced GC; Script teardown with a handoff image
  in flight.

#### Slices

| Slice | Admits | Proves |
|---|---|---|
| 1 | Definitions without `var` parameters | `mandelbrot2` |
| 2 | Typed and untyped `var` parameters | `matmul2`, whose loop nest sits in `pn matmul(a, b, var c, n)` |

**Exit:**

- `mandelbrot2`, `matmul2` and `levenshtein2` under default AUTO are within
  2× of their `LAMBDA_SATELLITE_SYNC=1` times in §2.3. The remainder is the
  interpreted run-up to the threshold plus the compile.
- The stress-mode corpus run has no output difference from `LAMBDA_TIER=interp`.
- `test_lambda_gtest` wall time under default AUTO does not regress beyond
  Round 1's 5% and 5 ms triage threshold, and the count of images created
  across the corpus is lower than before Phase 1.6.
- `make test-lambda-baseline` and `test_interp_gtest` keep their current
  pass/fail sets, including under `LAMBDA_GC_FORCE_EVERY`.

#### Risks

| Risk | Handling |
|---|---|
| Compiled code assumes a container is unique when T0 left it shared | Fact join at the handoff head (1.3); the aliasing tests and the stress corpus (1.7) |
| The body cannot be entered at an interior label without restructuring lowering | The 1.3 spike decides this before any other phase starts; the outlined-loop shape in §4.2 is the fallback |
| Lowering twice doubles compile time and image size | Measured in the spike; the work runs on a worker |
| The 10,000 default rests on four small probes and two benchmarks | The 1.1 census over the corpus; re-measure after Items 2–5; `LAMBDA_JIT_BACKEDGE` is one constant to change |
| Private MIR contexts accumulate | Fewer triggers after 1.6; the per-Script cap |
| A cold loop crosses the threshold first and its entry is never used | Per-loop counters; a second handoff image per definition is left as an open question for after slice 2 |

### Item 2 — Lazy boundary diagnostics

Covers F2. This is the unlanded
[Round 1 §8.3](Lambda_Impl_Interp_Tune.md#83-contracts-and-lazy-diagnostics)
item.

- Replace the formatted `const char* boundary` at the three T0 sites with a
  small descriptor: kind (declaration, assignment, argument), name pointer and
  length, argument index, and the `AstFuncNode`.
- Format inside the failure path only. The message text, source location and
  error code must be byte-identical to today's, since goldens pin them and
  **S1.6** requires both tiers to word a diagnostic the same way.
- `lambda_type_check` and `lambda_type_check_env` are shared with generated
  code, which passes static strings. Add the descriptor form beside the string
  form in the same module; do not fork the checker.
- Compute each parameter's index once while walking the list.

**Exit:** a successful declaration, assignment or call performs no
`snprintf`, `strbuf` allocation or `write_fn_name`. A sampled profile of the
§2.5 fixtures shows the `snprintf` row gone. Negative-contract tests and
error-message goldens are unchanged.

**Expected effect:** 15–29% of T0 time on the measured fixtures, plus the
`malloc`/`free` share on call-heavy code.

### Item 3 — Per-binding contract plan

Covers F3.

- In the frame-plan pass, classify each declarator, parameter and assignment
  target once: untyped; exact scalar tag; numeric lane; array contract; map
  contract; binder site; general. Store the result on the node that owns the
  declaration, per **D8.2.4v2** and **D8.2.5v3**. Do not attach it to a shared
  `Type` singleton.
- At bind time, the untyped class skips the boundary entirely. The exact
  scalar class admits on a tag compare and falls through to the existing
  `lambda_type_check` on a miss. Every other class calls the same checker it
  calls today, without the classification prelude.
- The plan records only what the declared type determines. Value-dependent
  admission, binder environments and COW marking stay at run time
  (**S7.7.2**, **S9.1.2**).

**Exit:** an `x: int` binding that receives an `int` performs no call into
the type checker. The typed fixtures in §2.4 are no slower than their untyped
pairs in T0. Admission failures produce the same errors as before.

**Not in this item:** skipping a check because the initializer's static type
already proves it. That is an inference-soundness claim under **D3.3.1v2** and
would have to reuse the predicate MIR Direct uses for the same site. It is
unverified here.

### Item 4 — Planned call sites and a direct T0 entry

Covers F4. Extends the call-shape fact Round 1 added
(`interp_call_shape_planned`).

- Classify each call node once: self-tail; system function (with the mutating
  and `print` sub-cases); type conversion; method member; direct Lambda call;
  dynamic. `eval_call` switches on the class instead of testing each case.
- For a direct Lambda call that is positional, exact-arity, non-variadic, and
  has no `var` parameter, binder or method receiver: consult the promotion
  cell, and if the callee is still interpreted, open the callee frame and
  evaluate arguments straight into its parameter slots. This is the internal
  fast path [design §4.5](../Lambda_Design_Ast_Interpreter.md#45-functions-closures-and-the-tier-boundary)
  describes. It must keep the promotion count, the recursion-depth budget, the
  rejected-parameter rule and the return-contract check.
- All other shapes keep `lambda_dynamic_call` as the single dispatch
  authority (**D6.2.2v2**). No callee value, receiver or dispatch result is
  cached (**D8.4.1v2**).
- Arguments evaluated into a not-yet-entered callee window must be rooted
  while later arguments run, and a nested call in an argument must not
  overlap that window (**D5.3.3**). Round 1's planned argument span already
  solves the same lifetime problem for the caller side.

**Exit:** a planned simple direct call makes one pass over its parameters and
does not enter `lambda_dynamic_call`. Forced-GC runs
(`LAMBDA_GC_FORCE_EVERY`) of the call-heavy fixtures pass. Promotion counts
per definition are unchanged against the baseline log.

### Item 5 — Slim the walker's hot path

Covers F5. This is AIO10.

- Split `eval_expr` so that literals, identifier reads, unary, binary, `if`
  and call dispatch live in a small function, and the long arms (paths,
  navigation, compound assignment, patterns) are out-of-line. Verify the
  result by reading the prologue, as in §2.5.
- Record an access kind on each identifier occurrence in the frame-plan pass:
  frame slot, capture slot (already linked by Round 1), module slot, or
  special (binder, type name, pattern, object field, cross-language import).
  A frame-slot read becomes one bounds-checked load. Entries that a view can
  overlay are marked so the view-binding walk runs only for them.
- Resolve pass-through `PRIMARY` wrappers once, so a parenthesised operand
  does not dispatch twice.
- Test `const_kind` before the node-kind switch in
  `interp_const_folded_value`.
- Keep `f->cur` and the mode/fuel check. `f->cur` feeds diagnostics, and the
  fuel check is the purity gate for constant folding.

**Exit:** the walker-self share on `mandelbrot2` and `matmul2` falls, with
identical output. The `eval_expr` hot function's frame is under 200 bytes.
Drop any sub-change that does not show a repeatable gain.

### Item 6 — Static path writes without a key array

Covers F6.

- Hold the keys of a nested member or index assignment in a fixed frame span
  of at most `AST_COW_PATH_MAX` slots and pass pointer and count to the COW
  path writer. Promote a span-taking entry to the module header beside
  `cow_path_set`; do not copy its body.
- The keys stay rooted in frame slots while the owner spine is detached
  (**D5.3.3**).

**Exit:** a nested member assignment performs no heap allocation for its
path. `richards2` and `deltablue2` output is unchanged, including under
forced GC.

### Item 7 — Guarded scalar operations, only if still measured

Covers F7. This is
[Round 1 Phase 6](Lambda_Impl_Interp_Tune.md#11-phase-6--guarded-primitive-specialization),
unchanged. Reprofile after Items 2–5. Implement only where arithmetic or
compare helpers remain a material share, with an exact guard and the existing
helper on a miss.

## 7. Ruled out

- **Bytecode or any second executable form** — AI22.
- **Mutable per-site caches**, including shape or callee caches for member
  access and calls — **D8.4.1v2**. Items 3–5 use immutable facts computed
  before execution and an inline guard with the shared kernel on a miss, which
  is the sanctioned form.
- **An unboxed interpreter lane** — AI3.
- **Conservative stack scanning** to drop `Scratch` rooting — retired.
- **Removing contract checks to recover the §2.4 gap.** Item 3 makes the
  check cheap; it does not remove it.

## 8. Validation

Round 1's matrix and gates apply unchanged
([§13](Lambda_Impl_Interp_Tune.md#13-validation-matrix),
[§14](Lambda_Impl_Interp_Tune.md#14-performance-acceptance-and-reporting)).
Additions specific to this round:

| Item | Extra coverage |
|---|---|
| 1 | The stress-mode corpus differential and the focused tests of Phase 1.7. An ineligible loop and a refused definition complete in T0. `LAMBDA_TIER=interp` never counts, queues or hands off. |
| 2 | Every boundary kind fails with byte-identical text under `interp` and `jit`: declaration, assignment, each argument position, binder sites. |
| 3 | Each contract class admits and rejects the same values as before, including `T?`, sized numerics, `u64`, arrays with counted axes, typed maps, unions. |
| 4 | Arity mismatch, optional and rest parameters, named arguments, `var` borrows, methods and recursion-depth exhaustion still take the general path and behave identically. |
| 5 | View overlays, captures, imports and object-field identifiers read and write through their existing routes. |
| 6 | Nested writes through a shared owner still detach; a failing key expression leaves the owner unchanged. |

Commands, after building release test runners:

```sh
env LAMBDA_TIER=interp ./test/test_lambda_gtest.exe --gtest_brief=1
./test/test_lambda_gtest.exe --gtest_brief=1          # default AUTO
./test/test_interp_gtest.exe
make test-lambda-baseline
```

`test_interp_gtest` had 19 failures unrelated to this work when last checked
on 2026-09-24. Classify any new failure against that set first.

## 9. Reproducing the evidence

```sh
# user-CPU by tier
LAMBDA_TIER=interp LAMBDA_NO_LOG=1 /usr/bin/time -p \
  test/benchmark/exe/lambda-v50-a9489bc329 run --no-log test/benchmark/beng/mandelbrot2.ls

# AUTO with synchronous publication (§2.3)
LAMBDA_TIER=auto LAMBDA_SATELLITE_SYNC=1 LAMBDA_NO_LOG=1 /usr/bin/time -p \
  test/benchmark/exe/lambda-v50-a9489bc329 run --no-log test/benchmark/beng/mandelbrot2.ls

# break-even sweep (§4.3): probes A–D and the fit
(cd temp/interp_perf/be && python3 sweep.py 7 && python3 fit.py && python3 real.py)

# profile: start the run, then
sample <pid> 6 1 -file temp/interp_perf/<name>.sample.txt
```

The interpreter runs on its own thread (`interp_run_on_large_stack`), so read
that thread's call graph, not the main thread's. Helper scripts used for this
capture are in `temp/interp_perf/` (`cpu.sh`, `prof.sh`, `agg.py`,
`callers.py`); that directory is not tracked.

## 10. Source map

| Area | Files and symbols |
|---|---|
| Promotion edge | [interp.cpp](../../lambda/runtime/interp.cpp): `interp_promote_function`, `interp_note_backedge`, `interp_satellite_enqueue`, `interp_satellite_publish_ready`, `interp_satellite_sync_enabled`, `interp_body_has_loop`; [ast-core.hpp](../../lambda/runtime/ast-core.hpp): `FnPromotionCell` |
| Satellite images | [transpile-mir.cpp](../../lambda/runtime/transpile-mir.cpp): `compile_ast_function_satellite_image`, `interp_satellite_image_prepare`, `interp_satellite_image_destroy`, `MirNativeCodegenLock`; [transpiler.hpp](../../lambda/runtime/transpiler.hpp): `InterpSatelliteImage`; `interp_plan.cpp`: `interp_satellite_refusal` |
| Loop walker | `interp.cpp`: the `AST_NODE_LOOP` arm of `eval_expr`, `interp_fast_int_while`, `interp_signal` |
| Boundary strings | `interp.cpp`: `interp_bind_declared_value`, the `AST_NODE_ASSIGN_STAM` arm of `eval_expr`, `interp_format_parameter_boundary` |
| Contracts | `interp.cpp`: `interp_coerce_declared_binding`, `interp_coerce_parameter_binding`, `interp_type_uses_binder`; `lambda-eval.cpp`: `lambda_type_check`, `lambda_type_check_env` |
| Calls | `interp.cpp`: `eval_call`, `interp_call_internal`, `InterpFrameGuard`; [lambda-eval.cpp](../../lambda/runtime/lambda-eval.cpp): `lambda_dynamic_call`, `fn_call_into` |
| Walker | `interp.cpp`: `eval_expr`, `interp_const_folded_value`, `interp_read_identifier`, `interp_read_binding_at_capture_slot`, `Scratch` |
| Planning | [interp_plan.cpp](../../lambda/runtime/interp_plan.cpp): `plan_link_capture_identifier`, `plan_link_call_shape`; [ast-core.hpp](../../lambda/runtime/ast-core.hpp): `FnFramePlan`, `AstCallNode`, `NameEntry` |
| Path writes | `interp.cpp`: the `AST_NODE_INDEX_ASSIGN_STAM` / `AST_NODE_MEMBER_ASSIGN_STAM` arm; `lambda-eval.cpp`: `cow_path_set` |

## 11. Status

| Item | Status |
|---|---|
| 1 Loop-head handoff | Implemented 2026-10-02 (§12.2), narrower than the §6 plan; census done (§12.5) |
| 2 Lazy boundary diagnostics | Implemented 2026-10-02 (§12.1) |
| 3 Contract plan | Implemented 2026-10-02 as a tag fast path, not a stored plan (§12.1) |
| 4 Planned call sites, direct entry | Declined on the reprofile (§12.4): the layer it removes is 3–5% |
| 5 Walker hot path | Implemented (§12.3, §12.4) |
| 6 Static path writes | Partial: key array sized once; key-span form not used (§12.3) |
| 7 Guarded scalar operations | Implemented for int/float `+ - *` in the shared helper (§12.4) |

## 12. Implementation record (2026-10-02)

Branch `worktree-interp-tune2`, based on `800ff396d`. Timings are user+system
CPU of the debug configuration (`-O3 -g`), minimum of 3 interleaved runs on a
machine with load average 20–60, so only ratios are meaningful. Correctness
gates are differentials over the 1,092 golden-tested scripts under
`test/lambda`, run with `temp/edits/diffrun.py` (not tracked).

### 12.1 Items 2 and 3 — commit `a20db7803`

- `LambdaBoundary` (lambda.h) carries a label's parts; `lambda_type_check_lazy`
  formats it only on failure. T0's declaration, assignment and argument sites
  pass descriptors; the parameter loops stop recomputing indices.
- `unwrap_simple_type_type` delegates to `type_field_unwrap_simple_decl`. Under
  `type_id == LMD_TYPE_TYPE` only the four meta-types are compact globals, so
  the ~35-compare `is_global_simple_type` chain was redundant; it was 7.9% of
  T0 time on `mandelbrot2` after Item 2.
- Item 3 became a tag fast path in `interp_coerce_declared_binding`: a plain
  `int`, `float` or `string` contract admits a value of its own kind
  unchanged, which is what admission computes (an int Item is int53 by
  construction). With the unwrap fix, classification is three pointer
  compares, so the stored per-binding plan of §6 was not needed.
- Interpreter CPU against the baseline: `ack2` 0.59×, `mandelbrot2` 0.47×,
  `matmul2` 0.60×, `richards2` 0.65×, `ack` 0.54×, `mandelbrot` 0.79×.
- Gates: interpreter and JIT corpus differentials 1,092/1,092 identical; the
  29 goldens that pin boundary texts identical in both tiers.

### 12.2 Item 1 — loop-head handoff

**Shape changed from the §6 plan.** §6 entered a second lowering of the whole
definition at an interior loop label. An inventory of the transpiler's flow
facts ruled that out: facts proven only at one program point (counter sign,
a descending-sum accumulator interval with no runtime guard), loops lowered
in several copies (fast and generic siblings, nested copies inheriting outer
guards), and root write-back that requires every GC register to be defined on
the new entry path. The implementation instead synthesizes the continuation
as its own procedure, whose parameters are the live-in locals. Entering it
through its `_b` wrapper gives each live-in the admission an unknown caller's
argument gets, with the slow body on a shape miss.

- **Eligibility.** A `while` that is a direct statement of a `pn` body, at
  most 8 per body (`interp_handoff_ordinal`, frame-plan pass). Back-edges of
  its nested loops, including fast integer loops, count toward it. The
  definition must have no captures, binders, variadic or suspension state,
  the block's last value expression may not precede the loop, at most 16
  live-ins, and the continuation must pass `interp_satellite_refusal`.
  Anything else pins the loop to T0 with a logged reason. A loop nested in an
  `if`, or a `for`, is never a handoff loop.
- **Continuation.** `interp_build_loop_continuation`: the body's statements
  from the loop onward (shared nodes, not copied), parameters reusing the
  live-ins' binding entries so the shared identifiers already name them, a
  copied signature returning boxed `any` (T0's epilogue keeps the declared
  return check), a fresh zeroed analysis record, and
  `AstFuncNode::is_loop_continuation`. Its nodes live in a pool the image
  owns.
- **MIR changes for continuations only.** Parameters are snapshot-marked at
  entry and treated as owners (`cow_owned`), since a live-in container may be
  shared with other T0 bindings. An untyped parameter passed to an untyped
  `var` parameter stays boxed and rooted, the rule declarations already
  follow.
- **Handoff.** At a head test of the handoff loop, if its continuation is
  published and the frame is at a statement boundary (no scratch, signal,
  method receiver, binder slots, view bindings or marked `var` parameter),
  T0 calls the continuation's `_b` entry with the live-in slots. A
  definition's own `var` parameters travel as CW33 homes pointing at the T0
  frame slots. T0 then signals `RETURNED` with the result.
- **Publication.** Loop jobs use the existing worker queue; a finished image
  is adopted at interpreted function entries and every 256 back-edges of the
  queued loop.
- **Retired:** the v7 loop-owner first-entry trigger and the per-definition
  back-edge marking. `LAMBDA_JIT_BACKEDGE` defaults to 10,000.
- **Not done from §6:** the Phase 1.1 census, and carrying the definition's
  own boxed entry in the loop-triggered image (it still promotes by its call
  and self-tail thresholds).

Measured under default AUTO against the baseline: `mandelbrot2` 12.04 →
0.10 s, `matmul2` 6.33 → 0.11 s, `levenshtein2` 0.60 → 0.14 s, `pnpoly2`
0.20 → 0.09 s, `nbody2` 0.21 → 0.17 s, `deltablue2` 0.83 → 0.78 s;
`richards2` and `ack2` unchanged. Eager JIT on the same binary:
`mandelbrot2` 0.06 s, `matmul2` 0.05 s.

**Gates.** AUTO corpus differential against the baseline 1,092/1,092
identical. Stress differential (`LAMBDA_JIT_BACKEDGE=1
LAMBDA_SATELLITE_SYNC=1`, every eligible loop hands off at its second head
test) against the interpreter: 1,091/1,092, the exception being pre-existing
(defect 3 below). New regression `test/lambda/proc/interp_loop_handoff.ls`
covers aliasing, `return`/`break`/`continue`, a `var` parameter, the block
value and repeated activations; it matches under `interp`, `jit` and the
stress mode, and the log confirms each function handed off.

**Defects found by the stress differential:**

1. Fixed — an alias of a live-in container (`keep = c`) skipped its COW mark,
   so a later write showed through the alias (`cow_flow_join`). Continuation
   parameters are now owners.
2. Pre-existing on eager JIT, at first fixed for continuations only and then
   in the shared inference (§12.5) — an untyped
   parameter that the body uses arithmetically and passes to an untyped `var`
   parameter gets a native lane with no CW33 home, and the callee's writes
   are lost (`pn f(x) { x = x + 10; bump(x) ... }` returns 160 on `jit`
   against 166 on `interp`). Applying the declaration rule in the callee's own
   lowering is not a general fix: direct native callers choose the argument
   lane elsewhere, and the result became `inf`. It belongs in the shared
   parameter inference.
3. Pre-existing, fixed in §12.5 — `tune23_record_constructor.ls` aborts under
   `LAMBDA_SATELLITE_SYNC=1` alone, on the baseline binary too ("mir-value:
   unavailable representation transition 1 -> 2 in `_forward_column_150`").
   Default AUTO and eager JIT pass.

### 12.3 Items 5 and 6, partial

- Binder-contract walks are skipped when the frame has no binder slots: a
  binder or bound reference only occurs in a generic function's own
  contracts, and with no slots the environment is NULL, so the general path
  reaches the same checker. It was 5.5% of `richards2`.
- `InterpBorrowedScratch` makes one allocation instead of three per `var`
  call.
- Nested-path key arrays are sized once (`array_reserve_append_slots`). A
  key-span `cow_path_set` was not used: `richards2`'s writes take the typed-map
  arm, whose span form needs a compiler-resolved leaf contract T0 lacks.
- Interpreter CPU against the Item 1 binary: `richards2` 0.85×,
  `deltablue2` 0.92×, `ack2` 0.97×, `nbody2` 0.99×.
- Recording a procedure block's last value expression in the plan was first
  deferred over in-place node morphing; §12.4 checked that no node is morphed
  into a list and landed it.

### 12.4 Second round: walker and scalar operations

Reprofiled after §12.2–12.3. Interpreter CPU against the §12.3 binary:
`mandelbrot2` 0.73×, `ack2` 0.76×, `richards2` 0.87×, `matmul2` 0.88×,
`deltablue2` 0.88×, `nbody2` 0.90×; the scalar fast path then adds
`mandelbrot2` 0.90×, `matmul2` 0.88×, `nbody2` 0.80× (`ack2` neutral).

- **Empty block value.** A procedural block with no value expression
  allocated an empty list only to finish it as `null` (or the item-position
  marker); it now returns that constant. One allocation per loop-body
  iteration.
- **Block shape in the plan.** `AstListNode` records the block's procedural
  last value and counts (`plan_scan_proc_block`); `eval_content` falls back to
  the live scan for a block the plan never reached. No code converts another
  node kind into a list, and every list allocation is `sizeof(AstListNode)`.
- **Planned local reads.** `AstIdentNode::interp_frame_slot_read`, set by the
  capture-link pass, marks an occurrence that reads a plain frame slot of the
  function it was planned in. `eval_expr` loads it directly when the node has
  no const fact, the mode is `RUNTIME` and no view binding is active. The flag
  sits in the node's tail padding (a `static_assert` pins the size) because
  identifiers are also produced by morphing other nodes in place.
- **Smaller `eval_expr` frame.** The member/index assignment arm and the N-D
  index read moved to `noinline` helpers; their coordinate arrays and COW path
  descriptor had made every node evaluation reserve 752 bytes beyond the
  saved registers. It is now 192.
- **Scalar operations (Item 7).** `fn_numeric_binary` answers int and float
  `+ - *` of two same-kind operands before its null, complex, vector and
  classification probes. These are exactly the classifier's int and float
  cells, computed with the same helpers, so both tiers' boxed paths gain and
  nothing is duplicated. Comparisons were left alone: their total order and
  merged-poison rules are not a plain IEEE compare.
- **Item 4 declined.** In `richards2`, `lambda_dynamic_call`'s signature
  check and root span, `interp_call` and frame setup together are 3–5%; a
  direct entry would need its own argument-rooting protocol for that.
- Remaining large costs: member reads by name (`map_get_for_owner_keyed`,
  ~10% of `richards2`, a runtime lookup that D8.4.1v2 keeps uncached) and
  `eval_expr`'s own dispatch.

### 12.5 Open items: defects, census, release timings

**Defect 2 fixed generally.** `infer_param_types_batched` keeps an untyped,
non-`var` parameter boxed when the body passes it to an untyped `var`
parameter, the rule `var` declarations already follow. Because all three
callers of the shared inference see it, direct native callers agree on the
argument lane (the callee-only attempt had printed `inf`). It replaces the
continuation-only override of §12.2. A `var` parameter is excluded: it
already owns its caller's home, and forwarding it keeps its inferred array
witness (`test/mir/lambda/tune21_var_witness_forward` pins that). Regression
`test/lambda/proc/var_arg_param_lane.ls`: base JIT printed `160 2.5`, all
tiers now print `166 15`.

**Defect 3 fixed, and it was not sync-mode-only.** A satellite of a native-int
function whose body ends in a direct call to a function outside its image
received an `any` Item from the dynamic edge and asked `em_require_rep` for
the int lane, which aborts by design. With 20,000 iterations plain AUTO
aborted on the base binary every time. `mir_require_native_return` applies the
declared-return admission instead (checked boundary, error exit, unbox) when
the produced Item's contract does not cover the lane, at the content tail and
the body-result conversion; every value the lane covers keeps the plain
conversion, and the MIR emission suite is unchanged (235/235). Regression
`test/lambda/proc/satellite_open_call_return.ls`.

**Handoff eligibility.** A body ending in an unconditional `return` or
`raise` never yields its block value, so the "value before the loop" pin no
longer applies to it. That was 10 of the 13 pins in the census
(`nbody`'s `benchmark`, `fasta`, `revcomp`, `havlak`). The remaining pins are
an object-field identifier (`hyphen`) and a variadic signature.

**Phase 1.1 census** (`LAMBDA_LOOP_CENSUS=1`, 1,250 scripts: the golden
corpus plus `test/benchmark`, before the eligibility change):

| Measure | Value |
|---|---:|
| Handoff loop sites executed | 639 |
| … reaching 1,024 / 10,000 / 50,000 / 100,000 back-edges | 251 / 147 / 14 / 6 |
| Loop states at exit: published / still compiling / pinned | 96 / 53 / 17 |
| Definitions that handed off | 81 |
| Satellite images queued, base → new | 22,216 → 17,210 (−23%) |

The 53 still compiling at exit are continuations the script finished before
using: worker CPU spent, never the execution thread's time. The image count
falls because the first-entry trigger is gone.

**Release timings** (both binaries `build-release-compile`, minimum of 3
interleaved runs, load average 37–57):

| Fixture | interp base → new | AUTO base → new | JIT base → new |
|---|---|---|---|
| ack2 | 1.75 → 0.59 s | 0.022 → 0.022 s | 0.021 → 0.021 s |
| mandelbrot2 | 6.31 → 2.35 s | 6.35 → 0.053 s | 0.030 → 0.030 s |
| matmul2 | 4.05 → 2.09 s | 4.07 → 0.064 s | 0.024 → 0.024 s |
| richards2 | 4.35 → 2.25 s | 0.388 → 0.382 s | 0.129 → 0.129 s |
| deltablue2 | 1.57 → 0.84 s | 0.417 → 0.402 s | 0.137 → 0.137 s |
| nbody2 | 1.41 → 0.89 s | 0.115 → 0.099 s | 0.051 → 0.051 s |
| pnpoly2 | 2.13 → 0.76 s | 0.104 → 0.043 s | 0.019 → 0.019 s |
| levenshtein2 | 1.13 → 0.42 s | 0.287 → 0.069 s | 0.025 → 0.024 s |

Eager JIT is unchanged, as it should be: the shared-helper and lowering
changes alter only paths it reaches through boxed calls.
