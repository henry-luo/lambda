# LambdaJS Implementation Plan: AST Interpreter Tuning, Hot-Loop Elevation, and Alignment with Lambda T0

**Date:** 2026-10-03
**Status:** IN PROGRESS — J1, J3, J5 (with a dense-index read), E1 and E3
implemented on branch `fix-continuation-param-inference` (§11); J2, J4, J6–J8,
E2, E4 and track A not started. The three policy points were ruled by the user
on 2026-10-03 (§8) and are recorded as **D8.1.3v22** and JSI18v2.
**Source baseline:** `72cc6de76` (source reading); release executable built
from `551d9cd3e` (`temp/bench_auto/lambda-fix-rel`) for timings and profiles.
**Scope:** the LambdaJS boxed AST walker (`lambda/js/js_interp.cpp`), its P2
promotion edge, and the parts of Lambda's T0 (`lambda/runtime/interp.cpp`,
`interp_plan.cpp`) that the two walkers could own jointly. The JS MVP backend
(`lambda/js/mvp/`) is out of scope.

**Formal authority:** [Lambda Formal Design](../../doc/Lambda_Formal_Design.md)
**D8.1.3v22** (LambdaJS tiers; hotness-only promotion and loop-head handoff,
*v22*), **D8.1.1v14** (Lambda loop-head handoff),
**D8.4.1v2** (no inline caches), **D8.2.3** (extract after two working
clients), **D8.2.4v2–D8.2.5v3** (fact placement, pass manager), **D8.5.1v6–v7**
(satellite work keying, bounded workers), **D5.3.3** (precise roots),
**D1.3** (shared substrate, profile-owned semantics).

**Related work:** [Lambda T0 round 2](Lambda_Impl_Interp_Tune2.md) (the items
and the loop handoff this plan carries over; §12.6 threshold evidence);
[shared round 1](Lambda_Impl_Interp_Tune.md) (Phases 1–5, partly landed; this
plan continues its Phases 3–5 for JS); [JS interpreter design](../Lambda_Design_JS_Interpreter.md)
(JSI1–JSI36; §9 promotion, §9.5 shared environment prerequisite);
[JS interpreter record](Lambda_Impl_JS_Interpreter.md).

This is an informative plan, not a ruling. Item letters are work sequencing.

## 1. Summary

1. **JS AUTO is, in effect, the AST interpreter.** On the benchmark rows
   measured, AUTO and forced AST differ only on two micro rows (§2.2). P2
   promotion admits only closed functions that make no call and read no
   global or module binding, and it triggers only on the fifth *call*. A
   loop in a function called once is never promoted; neither is `fib`.
2. **The JS walker repeats static work on every node, like Lambda T0 did
   before round 2 — but more of it.** A plain counting loop costs about
   650 ns per iteration in JS T0 against about 210 ns in Lambda T0 (§2.3).
   The largest single cost is an AST subtree walk executed on *every
   expression evaluation* to answer a static question (18–31% of T0 time).
3. **One promotion re-parses the whole source.** The satellite compiler
   builds an isolated parser clone per promoted function, synchronously on
   the running thread: about 4 ms for a tiny file, 17 ms at 17 KB, 59 ms at
   87 KB (§2.4). Lambda lowers the retained AST on a worker.
4. **The two interpreters already share data shapes but not the machinery
   that acts on them.** `FnPromotionCell`, `AstLoopControlNode`,
   `FnFramePlan`, `NameEntry` and `Script` are common types. The promotion
   driver, satellite queue, plan pass, activation chain and continuation
   builder exist once per language or only for Lambda (§6).

The plan has three tracks: tune the walker (§4, items J1–J8, no ruling
needed); make P2 useful and add loop-head handoff (§5, items E1–E4, ruled in
§8); and move the tiering machinery into one kernel both walkers call
(§6, items A1–A6).

## 2. Evidence

### 2.1 Method and limits

Release binary, `lambda.exe js <file>`, backend pinned with
`JS_EXEC_BACKEND=mir|ast` or left unset (AUTO). "exec" is the script's
own `__TIMING__` line; "wall" is process wall time and includes parse and
compile. Minimum of two runs, 30 s timeout.

The machine was shared: load average was 5–10 during the rows below and rose
above 60 afterwards, when the run was stopped. Read these as coarse ratios,
not as a benchmark record. A quiet-machine rerun is queued
(`temp/bench_auto/js_quiet.sh`); it should replace this table before any
number here is quoted elsewhere. Profiles are `sample` at 1 ms on 20 M
iteration probes; static functions are stripped in the release binary, so
attribution is by exported runtime helper and everything else is "walker".

### 2.2 Tier comparison

exec ms (wall ms in parentheses where it differs materially):

| Row | MIR | AST | AUTO | AST/MIR |
|---|---:|---:|---:|---:|
| awfy/mandelbrot2 | 58 | 12,229 | 12,966 | 211× |
| awfy/nbody2 | 595 | 5,064 | 5,911 | 8.5× |
| awfy/cd2 | 5,198 | 21,855 | 28,042 | 4.2× |
| awfy/towers2 | 24 | 117 | 134 | 4.9× |
| awfy/richards2 | 19,732 | timeout | timeout | — |
| awfy/havlak2 | 18,060 | timeout | timeout | — |
| awfy/bounce2 | 5 (248) | 22 (49) | 24 (50) | 4.4× |
| awfy/storage2 | 18 (566) | 44 (85) | 56 (116) | 2.4× |
| r7rs/fib2 | 5.8 | 1,114 | 996 | 192× |
| js_micro/named (wall) | 426 | 13,008 | 12,331 | 31× |
| js_micro/index (wall) | 548 | 9,704 | 8,571 | 18× |
| js_micro/args_ctl (wall) | 239 | 3,102 | 646 | 13× |
| jetstream/crypto-md5, load only (wall) | 7,668 | 48 | 54 | 0.006× |
| jetstream/3d-cube, load only (wall) | 610 | 82 | 78 | 0.13× |

Readings:

- AUTO tracks AST on every row except `args_ctl`/`args_fp`. P2 is nearly
  inert on this corpus.
- Arithmetic kernels are 200× off (mandelbrot2, fib2). Object-heavy rows are
  4–9× off because both tiers spend their time in the same property kernels.
- The interpreter's reason to exist is visible in the wall column: loading
  without running costs 0.2–7.7 s through whole-module MIR and 40–80 ms
  through the AST tier. The goal is to keep that and recover the kernels.
- Side finding: `awfy/deltablue2.js` and `awfy/json2.js` fail on the MIR
  backend (`TypeError: is not a function`) and pass on AST. The archived v50
  binary fails the same way, so this is not recent. Not investigated here.

### 2.3 Per-iteration cost in T0

One-million-iteration probes (`temp/js_interp/`), exec ms:

| Probe | JS MIR | JS AST | JS AUTO | Lambda T0 | Lambda JIT |
|---|---:|---:|---:|---:|---:|
| `for (let i…) s = (s + i*3) % M` in a function | 15 | 650 | 622 | 209 | 4 |
| same with `var`/`while` | 47 | 675 | 674 | | |
| loop calling `add(s, i)` | 31 | 987 | 614 | | |
| loop over `p.x = (p.x + a[i&7] + p.y) % M` | 62 | 1,867 | 1,855 | | |
| same counting loop at script top level | 396 | 3,004 | 3,297 | | |

JS T0 is about 3× slower per iteration than Lambda T0 on the same loop.
Top-level loops are 5× slower again: every `var` there is a global-object
property.

### 2.4 Where JS T0 time goes

Inclusive share of the interpreter thread, by exported helper:

| Cost | counting loop | object loop | call loop |
|---|---:|---:|---:|
| `ast_visit_core_children` (run-time AST walk) | 31% | 28% | 18% |
| `lambda_side_root_alloc_n_for` (root windows) | 6% | 5% | 6% |
| `getenv` under `js_interp_promote_function_if_hot` | — | — | 7.5% |
| GC allocation and collection (per-call environments) | — | 1% | 9% |
| name-pool lookup of static property names | — | 6% | — |
| `js_to_property_key` + canonicalisation | — | 9% | — |
| property get/set kernels | — | 30% | — |

Causes, from the source:

- **Run-time AST walk.** `js_interp_eval` wraps every expression evaluation
  and calls `js_interp_expression_may_suspend(node)`, a recursive subtree
  search for `yield`/`await`, before it checks whether the frame even has a
  suspended activation ([js_interp.cpp:4254](../../lambda/js/js_interp.cpp)).
  The walk repeats at every level of a nested expression, so its cost is
  quadratic in expression depth. Ordinary functions never use the answer.
  `js_interp_loop_needs_per_iteration_env` walks the whole loop subtree at
  each loop entry for the same kind of static fact.
- **Selector read per call.** `js_execution_auto_requested()` calls
  `getenv("JS_EXEC_BACKEND")` on every interpreted call.
- **Environments.** Every call, and every block or loop scope with a
  binding, allocates a GC `JsInterpEnv` and registers it as a root. An
  identifier read then walks the environment chain comparing scope pointers
  (`js_interp_find_env`).
- **Static property names** travel as `String` Items; the property kernel
  converts and looks them up in the name pool on each access. Round 1 §7.0
  recorded this as already avoided; the profile shows it is not.
- **Frame copies.** Each loop iteration, block and label copies the whole
  `JsInterpFrame` (about 200 bytes, mostly generator/async fields) once or
  twice.

### 2.5 Promotion cost

One promotion, measured as the wall-time difference between four and six
calls of a small function, with the file padded by unrelated functions:

| Source size | AUTO, no promotion | AUTO, one promotion | Promotion cost |
|---:|---:|---:|---:|
| 0.2 KB | 23 ms | 27 ms | 4 ms |
| 17 KB | 34 ms | 51 ms | 17 ms |
| 87 KB | 184 ms | 243 ms | 59 ms |
| 354 KB | 4,221 ms | 5,286 ms | 1,066 ms |

`js_mir_compile_function_satellite` re-parses and re-binds the entire source
to find one function by `FunctionId`, because JS lowering mutates the AST and
keeps its analysis facts in the lowering session (JS design §9.4, JSI3). The
cost therefore scales with the file, not the function. (The 354 KB row also
shows a superlinear front-end cost unrelated to promotion: 4.2 s to load
4,000 top-level functions.)

## 3. Findings

| # | Finding | Evidence | Addressed by |
|---|---|---|---|
| F1 | T0 walks the AST at run time to answer static questions | 18–31% of T0 | J1, J2 |
| F2 | Tier selector is read from the environment per call | 7.5% of a call loop | J3 |
| F3 | Locals live in GC environment records, resolved by chain walk | 9% GC on calls; hop walk per identifier | J6 |
| F4 | Static property names are re-keyed per access | 6–15% of object code | J5 |
| F5 | Hot frame carries cold suspension state and is copied per iteration | source | J4 |
| F6 | P2 admits no call, no global read, no capture | AUTO ≈ AST | E1 |
| F7 | P2 triggers on call count only | once-called loop owners stay in T0 | E3 |
| F8 | A promotion re-parses the file, synchronously | 4–59 ms, scales with source | E2 |
| F9 | Tiering machinery is duplicated or Lambda-only | §6 | A1–A6 |

## 4. Track J — tune the JS walker

These need no ruling: they are fact placement under **D8.2.4v2** and keep
**D8.4.1v2** (plan-time facts on immutable nodes, no run-time caches).

**J1 — Suspension fact at plan time.** Record "this subtree contains a
`yield`/`await` outside a nested callable" once per node when the script is
prepared, and consult the frame's `suspended_activation` first so ordinary
frames skip the question entirely. Removes the largest single cost.
Expected: 18–31% of T0.

**J2 — Loop facts at plan time.** Store "needs a per-iteration environment"
on `AstLoopControlNode` beside `interp_handoff_ordinal`.

**J3 — Resolve the selector once.** Read `JS_EXEC_BACKEND` and
`JS_FUNC_JIT_THRESHOLD` once per runtime, as `lambda_tier_selected()` does.
Expected: 7% on call-heavy code.

**J4 — Split the frame.** Keep script, environment, `this` home and flags in
the hot frame; move generator, async, replay and label state to a side
record referenced by pointer. Loop iterations then stop copying it. This is
the JS counterpart of round 2 Item 5 (Lambda's `eval_expr` frame went from
752 to 192 bytes).

**J5 — Static property keys planned once.** Give each static member name a
key slot in the script's property-key image, the same image MIR sites use
(JSI19), and pass the resolved key to the property kernel. This is a
plan-time name table, not a per-site cache. Expected: 6–15% of object code.

**J6 — Uncaptured locals in frame slots.** Continue round 1 Phase 5: a
binding that no closure, direct eval, `with`, mapped `arguments` or
suspension can observe gets a slot in a `FnFramePlan` root window; only
captured bindings keep an environment cell. An admitted function then
allocates no environment and an identifier read is one indexed load. This is
also step 2 of the shared environment ABI (JS design §9.5), so it is not
throwaway work.

**J7 — Planned root windows.** Reserve one root window per activation from
a planned scratch depth instead of opening a `RootFrame` in individual node
arms (67 sites today). Round 1 Phase 4 started this.

**J8 — Number fast path.** After J1–J7, re-profile; if `js_add`,
`js_compare` and number boxing are then the top entries, add the guarded
Number/Number path in the walker's binary arm, as round 2 did in
`fn_numeric_binary`.

Order: J1, J3, J2 first (small, independent, about a third of T0 together),
then J4, J5, then J6/J7, which change frame layout and need the forced-GC
gates.

Projection: J1–J5 should bring the counting loop from about 650 ns to
300–350 ns per iteration, and J6–J7 toward Lambda T0's 200 ns. These are
estimates from profile shares, not measurements.

## 5. Track E — make elevation work

**E1 — Widen P2 admission to calls and outer bindings.** The scan rejects
every `CALL_EXPR`/`NEW_EXPR`, every unresolved global name and every binding
owned outside the function. Admit them by lowering each as T0 already
evaluates it:

- a call goes through the ordinary `fn->invoke` kernel, so the callee may be
  AST or MIR (JSI9);
- a script top-level lexical reads its module slot;
- a global name goes through the global-environment helper by name;
- a direct self-call becomes a direct call to the satellite's own entry.

Captured bindings, nested callables and classes stay excluded until the
shared environment ABI (JS design §9.5, P4). This alone admits `fib`, `tak`,
and most leaf-with-helper functions. It depends on the clone assigning the
same module slots as the retained script, which must be asserted, not
assumed — or on E2.

**E2 — Compile from the retained script, off-thread.** Lower the selected
definition from the retained `JsScript` instead of a re-parsed clone, on the
satellite worker pool Lambda already has, and publish at a safe point. The
prerequisite is JSI3: the facts lowering needs are computed once and kept
with the script, and lowering stops mutating shared nodes (or works on an
execution overlay, as Lambda's satellites do under **D8.5.1v6**). This is the
largest item in the plan. Until it lands, an interim step is to cache one
clone per script generation rather than re-parsing per function.

**E3 — Loop-head handoff for JS (ruled, §8 R1).** Carry
**D8.1.1v14** over unchanged in shape: a loop that is a direct statement of
a function body counts back-edges in the definition's `FnPromotionCell`; at
the threshold, the statements from that loop to the end of the body are
compiled as a continuation function whose parameters are the live-in
bindings; at the next loop head, T0 calls it and returns its result as the
activation's result. No interpreter state is written back.

JS-specific conditions, each decided once in the plan pass:

| Condition | Reason |
|---|---|
| `while`, `do-while` and `for(;;)`; not `for-in`/`for-of` at first | iterator state and IteratorClose are live across the head |
| loop is a direct statement of the function body | no enclosing `try`/`finally`, label or block scope to reconstruct |
| function is synchronous and closed (E1's rule) | a captured binding lives in an environment cell the continuation cannot own before P4 |
| no `arguments`, direct eval or `with` in the function | their bindings are not named live-ins |
| `this`, `new.target` and the home object pass through the call activation | the continuation is invoked under the caller's activation, so a constructor's return rule still runs in T0's epilogue |
| `let`/`const` declared before the loop are live-ins and keep their const-ness; those declared after start in their TDZ inside the continuation | the continuation reuses the same `NameEntry` records |

Script top-level loops are a separate, later case: their `var`s are global
properties, so there are no live-ins, but the continuation must return the
script's completion value and the rest of the script must not run twice.

**E4 — Threshold.** Start with `JS_LOOP_JIT_THRESHOLD` at 10,000 and
`JS_FUNC_JIT_THRESHOLD` at 1000 (§8) and re-derive both for JS after E2. Today's
break-even is `promotion cost ÷ (T0 − T1 cost per iteration)`, about
6,000 iterations for a tiny file and about 90,000 at 87 KB, because the cost
scales with source size. That dependence disappears with E2; the JS value
should then be measured the way Tune2 §12.6 did, with the break-even probes
and a threshold sweep over the suites. Tuning the walker (track J) raises
the break-even, so the sweep comes last.

Sequencing: E1 before E3, since a continuation that cannot contain a call
elevates almost no real loop. E3's first slice can ship on the clone path
(the continuation is identified by function id and loop ordinal, both stable
across a re-parse), but its threshold is only meaningful after E2.

## 6. Track A — one tiering kernel for both walkers

What is shared today, and what is not:

| Concern | Lambda T0 | JS T0 | Shared? |
|---|---|---|---|
| AST nodes, `AstIndex`, `NameEntry`, `Script` | yes | yes (`JsScript : Script`) | yes |
| `FnPromotionCell` (counts, state, loop fields) | all fields | `call_count`, `state`, `boxed_entry` | type only |
| Promotion driver | `interp_promote_function_if_hot`, worker pool, queue, publish at safe point, cancel | `js_interp_promote_function_if_hot`, synchronous | no |
| Tier selector and knobs | `LAMBDA_EXEC_BACKEND`, `LAMBDA_FUNC_JIT_THRESHOLD`, `LAMBDA_LOOP_JIT_THRESHOLD`, `LAMBDA_SATELLITE_SYNC`, cached | `JS_EXEC_BACKEND`, `JS_FUNC_JIT_THRESHOLD`, `getenv` per call | no |
| Plan pass | `interp_plan.cpp` (frame plan, block shape, handoff ordinals, call shape) | support scan plus run-time walks | call-shape prep only |
| Frame storage | `FnFramePlan` slot window | GC `JsInterpEnv` per scope | storage helper only |
| Activation chain | `InterpState::top` | `JsInterpFrame` + `JsCallActivation` | no (JSI26 unbuilt) |
| Loop continuation builder | `interp_build_loop_continuation`, live-in analysis | none | no |
| Census and stress knobs | `LAMBDA_LOOP_CENSUS`, back-edge 1 + sync differential | none | no |
| Satellite compile unit | retained AST, private MIR context, worker | re-parsed clone, private MIR context, caller thread | context retention only |

Semantics stay profile-owned (**D1.3**, JSI1). What can be common is the
machinery that does not look at values:

**A1 — Promotion kernel.** Extract the driver that owns counting, the state
machine on `FnPromotionCell`, thresholds, the job queue and worker pool,
publication at a safe point, cancellation at script teardown, and the
diagnostic lines. Each profile supplies three callbacks: admission (reason
string or null), compile (definition → boxed entry) and publish (install the
entry on its function representation). JS gets asynchronous compilation and
script-teardown cancellation without writing them again. **D8.2.3** is
satisfied: both clients exist and work.

**A2 — One policy record, two sets of names.** One cached policy record per
runtime and profile: tier, call threshold, back-edge threshold, synchronous
publication, census. Each profile fills it from its own knobs (§8 R2), so the
kernel never reads the environment. The stress differential that found every
round-2 defect (back-edge 1 with synchronous publication, against forced
interpretation) then runs over `test/js/` with the JS knob names.

**A3 — JS plan pass under the pass manager.** Give the JS walker a plan pass
registered like Lambda's (**D8.2.5v3**) that writes J1, J2, J5, J6 facts and
the handoff ordinals. The handoff-eligibility rule ("direct statement of the
body, admitted loop form, ordinal ≤ `INTERP_HANDOFF_LOOP_MAX`") is the same
code with a per-profile form predicate.

**A4 — Common frame plan.** J6 puts JS uncaptured locals in the same
`FnFramePlan` window Lambda uses: parameters, locals, signal slot, scratch.
The root-window reservation, scratch discipline and frame guard become one
implementation (round 1 Phase 4/5, JSI25).

**A5 — Common continuation builder.** Live-in analysis and the synthesized
function are structural: collect the bindings declared before the loop and
referenced from it onward, then build a function node over the shared
statements with those bindings as parameters. Parameterise the existing
builder by child visitor and by the profile's parameter-node constructor; JS
supplies its own admission rules from E3's table.

**A6 — Common activation record.** Put the fields the kernel needs —
caller link, current node, `handoff_loop`, `promotion_cell` — in one base
activation both frames embed, so depth limits, backtraces and the back-edge
counter have one owner (JSI26, JSI33). JS completion records and Lambda
signals stay in their own frame payloads.

A1 and A2 are independent of the rest and can go first. A3 carries track J.
A4–A6 follow J6 and E3.

## 7. Order of work

| Step | Items | Gate |
|---|---|---|
| 1 | J1, J2, J3 | `test_js_gtest`, test262 baseline, AST/MIR differential; T0 probe timings |
| 2 | A1, A2 | Lambda baseline unchanged; JS promotion asynchronous; stress differential over `test/js/` |
| 3 | E1 | tier-crossing matrix (JS design §14.3); `fib2` AUTO within 2× of MIR exec |
| 4 | J4, J5, A3 | forced-GC gates; object probe |
| 5 | E3 on the clone path, A5 | stress differential at back-edge 1; mandelbrot2 AUTO within 2× of MIR exec |
| 6 | J6, J7, A4, A6 | forced-GC and poisoned-GC gates; environment allocation counters |
| 7 | E2 | promotion cost independent of source size |
| 8 | E4, J8 | quiet-machine suite sweep; record as Tune2 §12.6 did |

## 8. Rulings (USER, 2026-10-03)

Recorded as **D8.1.3v22** (formal design, spec 19.0.0) and JSI18v2 / §9.3 of
the JS interpreter design.

**R1 — Loop-head handoff for JS: allowed.** A running AST activation may hand
off at a loop-head test in the **D8.1.1v14** shape. This replaces the v13
sentence that no active AST frame transfers to MIR, and JSI18's entry-only
rule. E3 is unblocked; it still depends on E1 to be useful.

**R2 — JS keeps its own names.** `JS_EXEC_BACKEND` stays. The call and
loop thresholds are `JS_FUNC_JIT_THRESHOLD` and `JS_LOOP_JIT_THRESHOLD`
(renamed by the user from `JS_JIT_THRESHOLD` and `JS_JIT_BACKEDGE`). The shared
kernel of A1/A2 takes its policy values from the profile, so each language
reads its own knobs; the stress differential for JS is
`JS_FUNC_JIT_THRESHOLD=1 JS_LOOP_JIT_THRESHOLD=1` with synchronous publication.

**R3 — Hotness-only promotion; call threshold 1000, then 100, then 5.** A JS
definition is compiled only on a hot hit: 1000 calls (five before; 100 since
D8.1.3v23, §11.5; **5**, the same as Lambda, since **D8.1.3v24**, §11.6) or a
loop reaching the
back-edge threshold. Nothing is compiled at first entry or because a
definition owns a loop. Both thresholds are provisional and are to be revised
from release profiling (E4). The default is changed in
`js_interp_p2_threshold`; the P2 tests set the threshold explicitly and are
unaffected.

Consequence for §2: with 1000, the two rows where AUTO beat AST
(`args_ctl`, `args_fp`) still promote, about 1000 interpreted calls later.
With today's narrow admission the higher threshold costs little, and it
avoids paying a whole-source re-parse (§2.5) for functions called a handful
of times.

## 9. Reproducing the evidence

```bash
# three backends over the JS suites (wall and exec ms)
python3 temp/bench_auto/js_tiers.py temp/bench_auto/lambda-fix-rel out.json r7rs,awfy,js_micro
# per-iteration probes
cd temp/js_interp; JS_EXEC_BACKEND=ast ../bench_auto/lambda-fix-rel js loop.js
# profile attribution by exported helper
python3 temp/js_interp/incl.py temp/js_interp/loop.sample.txt
```

## 11. Implementation record (2026-10-03)

Commits on `fix-continuation-param-inference`: `8349baa75` (J1, J3, J5),
`841b337ce` (E1), `87b781771` and `286c909d2` (E3). Every step passed
`test_js_gtest` 484/484 and `test_js_script_gtest` 191/191, plus the
differentials below. Timings are release builds on a shared machine (load 8–50
during the session), interleaved per row, so they are ratios, not a record.

### 11.1 Walker (J1, J3, J5)

- **J1** — `js_interp_eval` asks the may-suspend question only when the frame
  has a suspended activation; no plan-time fact was needed, because ordinary
  frames never record replay values.
- **J3** — the promotion policy is resolved once per `Runtime`
  (`js_promotion_policy_resolved`, `js_promotion_auto`, call and back-edge
  thresholds), at the first interpreted call that asks.
- **J5** — the parser gives each static, non-private member name a slot in
  the realm literal cache (`AstIdentNode::js_property_key_slot`, in tail
  padding); the walker stores the canonical key there once per realm.
  `js_canonical_property_string` now returns a key whose NameId resolves to
  itself in the context pool instead of re-hashing it. The profile also showed
  `a[i]` reads converting the Number index to a string key; a plain computed
  read of an existing own dense array element now stops at the element, as
  MIR's `js_get_reference` does.

AST differential over `test/js/` (old vs new binary, forced AST): no
divergence. Probes (exec ms, `JS_EXEC_BACKEND=ast`):

| Probe | before | after |
|---|---:|---:|
| counting loop | 600 | 334 |
| call loop | 866 | 624 |
| object/array loop | 1,536 | 929 |
| top-level loop | 2,628 | 2,335 |

### 11.2 E1 — wider P2 admission

Calls and `new` go through the ordinary kernels; unresolved names are global
reads by name; script top-level bindings are admitted. Three lowering changes
make that correct in a satellite:

1. The clone numbers module vars in its own order. Every module var the
   selected body names is pointed at the retained script's slab slot for the
   same binding (`js_p2_remap_module_vars`), and its initial-type guess is
   dropped. Without it a top-level `let` read returned another slot's value.
2. Direct-callee resolution answers only the satellite's own function
   (`JsMirTranspiler::p2_satellite_node`), set before analysis so no inferred
   type assumes a callee the satellite does not define.
3. A function-declaration read never lazily materializes another
   definition's MIR item: the item is an undefined forward, and `MIR_link`
   looped on it.

Stress differential (`JS_FUNC_JIT_THRESHOLD=2` AUTO vs forced AST over
`test/js/`): no divergence.

### 11.3 E3 — loop-head handoff

As designed in §5, with these specifics:

- **Continuation source.** The satellite clone parses the retained source
  plus one appended top-level function `__lambda_loop_<fid>_<ordinal>`. Its
  fresh parameters are copied into same-named `let` locals (`var` for a name
  the region redeclares with `var`), and its body is the source text from the
  head test to the end of the function body. A `for` resumes as
  `for (;test;update)`. A never-executed call `if (0) __lambda_loop_…(…)` with
  literals of the live values' types gives parameter-type evidence.
- **Why `let` locals.** Measured on whole-module MIR, a numeric loop over
  reassigned parameters or `var` locals is untyped and runs 10–20× slower than
  the same loop over `let` locals (diviter's continuation: ~15 ns → ~2 ns per
  iteration).
- **Entry.** T0 calls the continuation through `js_call_from_ast` with the
  frame's `this`; the wrapper gets the compiled-context ABI flag (and strict,
  if the original is strict). The result is the activation's `return`.
- **Refusals** pin the loop (`FN_LOOP_HANDOFF_PINNED`): P2 admission,
  named function expressions, >32 or shadowed live-ins, an assigned `const`
  live-in, a continuation that fails to compile. A live-in still in its TDZ
  declines the handoff and stays T0.

Stress differential (`JS_FUNC_JIT_THRESHOLD=2 JS_LOOP_JIT_THRESHOLD=1`): no divergence;
31 handoffs in 19 of 471 files. Refusals are dominated by nested definitions
(272 "closure capture"), the shared-environment work of JS design §9.5.

AUTO exec ms on a quiet machine (load 3.7–8, 2026-10-03 16:16–16:25, min of
three): pre-round binary (`551d9cd3e`, call threshold 5, no handoff) vs this
round (`286c909d2`: threshold 1000 — 100 since §11.5 — and handoff), with whole-module MIR from the
same binary. Geomean AUTO after/before over the 29 rows: **0.218**.

| Row | AUTO before | AUTO after | MIR |
|---|---:|---:|---:|
| r7rs/ack2 | 2,963 | 16.4 | 13.7 |
| r7rs/fib2 | 271 | 3.7 | 1.9 |
| r7rs/tak2 | 43.9 | 3.0 | 0.3 |
| r7rs/cpstak2 | 83.7 | 3.4 | 0.7 |
| r7rs/sum2 | 14.9 | 4.6 | 0.7 |
| r7rs/sumfp2 | 31.6 | 4.0 | 0.1 |
| r7rs/nqueens2 | 102 | 49.9 | 28.0 |
| larceny/triangl | 60,664 | 6,932 | 5,537 |
| larceny/array1 | 691 | 45.5 | 13.2 |
| larceny/divrec | 722 | 21.2 | 16.1 |
| larceny/diviter | 1,358 | 619 | 614 |
| larceny/gcbench | 4,436 | 1,025 | 525 |
| larceny/puzzle | 832 | 86.5 | 49.8 |
| larceny/deriv | 403 | 107 | 87.2 |
| larceny/ray | 357 | 89.3 | 6.5 |
| kostya/matmul | 13,142 | 614 | 187 |
| kostya/brainfuck | 10,854 | 1,894 | 1,748 |
| kostya/base64 | 2,109 | 540 | 467 |
| kostya/primes | 3,334 | 1,683 | 50.9 |
| kostya/collatz | 1,984 | 1,644 | 1,587 |
| larceny/primes | 3,952 | 3,629 | 98.6 |
| kostya/levenshtein | 1,283 | 1,037 | 76.6 |
| **regressions** | | | |
| r7rs/mbrot2 | 9.8 | 24.3 | 2.2 |
| r7rs/fft2 | 91.5 | 101 | 3.3 |
| larceny/paraffins | 91.7 | 155 | 2.3 |
| larceny/quicksort | 91.1 | 300 | 25.8 |

The regressions are rows whose hot functions are called fewer than 1000 times
but more than five, with loops short of 10,000 back-edges per definition, or
(quicksort) a recursive hot function that now runs 1000 interpreted calls
before promoting. They are the expected cost of the provisional thresholds and
the input for E4. `larceny/primes`, `kostya/primes` and `levenshtein` stay
far from MIR: their hot code is not a direct-statement loop of an admitted
function. Raw data: `temp/bench_auto/js_e3_time.json` (main checkout).

### 11.5 Threshold sweep and the 100-call default (D8.1.3v23)

Release AUTO exec ms, min of three, load 2–4, binary `286c909d2`; columns are
call threshold / back-edge threshold.

| Row | 5/10k | 100/10k | 1000/10k | 1000/1k | 100/1k | 1000/100k |
|---|---:|---:|---:|---:|---:|---:|
| r7rs/mbrot2 | 8.0 | 12.6 | 22.8 | 18.9 | 16.1 | 55.5 |
| larceny/paraffins | 49.5 | 62.1 | 82.4 | 71.4 | 84.2 | 57.4 |
| r7rs/fft2 | 97.1 | 97.8 | 97.7 | 58.4 | 57.8 | 75.3 |
| r7rs/nqueens2 | 43.7 | 44.2 | 50.3 | 50.3 | 44.4 | 50.4 |
| larceny/puzzle | 41.4 | 42.1 | 50.6 | 57.5 | 42.1 | 50.7 |
| larceny/ray | 43.7 | 43.4 | 45.4 | 36.6 | 34.9 | 62.8 |
| kostya/base64 | 582 | 631 | 570 | 527 | 526 | 913 |
| larceny/quicksort | 142 | 152 | 153 | 149 | 145 | 214 |
| fib2, array1, deriv, gcbench, matmul | flat | flat | flat | flat | flat | ≥ others |

- **Calls.** No row is slower at 100 than at 1000; 100 recovers most of the
  gap to 5. Five is not chosen yet because each promotion still re-parses the
  whole source (§2.5), a cost these small files do not show. The user set the
  default to 100 (**D8.1.3v23**).
- **Back-edges.** 100,000 is worse almost everywhere. 1,000 helps fft2, ray
  and base64 but hurts puzzle; 10,000 is kept until E2 removes the
  size-dependent compile cost, then re-derived by break-even as Lambda's was.
- **quicksort** is 142–214 ms under every setting against 91 ms before this
  round, so its regression is not a threshold effect. Probable cause: E1 now
  promotes its recursive function, whose calls go through the generic call
  kernel. Not yet investigated.

### 11.6 Function-threshold sweep for both languages (D8.1.3v24)

Release `a51bab468`, AUTO, loop threshold 10000, values interleaved per
script, min of three, load 3–6 (2026-10-03 18:16–19:45). Script:
`temp/bench_auto/func_sweep.py`; data `func_sweep_{lambda,js}.json` (main
checkout). Geomean end-to-end wall time, lower is better:

| Threshold | 1 | 2 | 5 | 10 | 25 | 100 | 500 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Lambda, 126 scripts, vs 5 | 1.040 | 1.005 | **1.000** | 1.014 | 1.033 | 1.069 | 1.113 |
| JS, 73 rows, vs 100 | 0.875 | 0.983 | **0.978** | 0.984 | 0.990 | 1.000 | 1.008 |

- **Lambda:** 5 is best (2 ties within noise; exec time 0.987 vs 1.000). 1
  costs 4% by compiling once-called functions; above 10 cost grows steadily
  (diviter 458 ms at 5 → 11.3 s at 500).
- **JS:** among real thresholds 5 is best, on wall and on exec time (0.955 vs
  100). The default is set to 5 (**D8.1.3v24**).
- **JS at 1** compiles every function at its first call, which hotness-only
  promotion excludes. Its gain is concentrated in rows whose hot function is
  called once and so reaches MIR only through loop handoff: larceny/primes
  75 vs 1,798 ms, kostya/primes 76 vs 1,740, matmul 222 vs 636, fft2 48 vs
  121, array1 34 vs 63, ray 40 vs 61, triangl 5.5 vs 6.9 s. The continuation
  runs up to 24× slower than the whole-function satellite there — a
  continuation-quality defect (likely: locals built before the loop lose
  their native types as parameters), not a threshold question. Next item.
- **Threshold 2** shows isolated pathologies in both languages (JS triangl
  12.8 s vs 6.9 s; Lambda paraffins 47 vs 25 ms), not investigated.
- Not usable: octane rows, `run_octane.js`, `prettier_ast_preprocess.js` and
  `havlak2.js` error or time out at every value; two text rows were not
  reached before the run's time limit.

### 11.4 Not done

J2 (loop facts at plan time), J4 (frame split), J6/J7 (frame-slot locals,
planned root windows), J8, E2 (compile from the retained script, off-thread),
E4 (threshold derivation), and track A. AWFY rows need P2 for class methods
and CommonJS modules, which E1 does not cover.

## 10. Source map

| Topic | Location |
|---|---|
| Expression wrapper and the suspension walk | `js_interp.cpp` `js_interp_eval`, `js_interp_expression_may_suspend` |
| Loop execution, per-iteration environment probe | `js_interp.cpp` `case AST_NODE_LOOP`, `js_interp_loop_needs_per_iteration_env` |
| Environments and binding reads | `js_interp_env.h`; `js_interp_env_create`, `js_interp_find_env`, `js_interp_read_binding` |
| Call entry | `js_interp_call_function` |
| P2 admission and promotion | `js_interp_p2_scan_node`, `js_interp_p2_admission_reason`, `js_interp_promote_function_if_hot` |
| Satellite compile | `js_mir_module_batch_lowering.cpp` `js_mir_compile_satellite_unit`, `js_p2_remap_module_vars` |
| Loop handoff (E3) | `js_interp.cpp` `js_interp_loop_handoff_begin`, `js_interp_loop_continuation_compile`, `js_interp_loop_handoff_try` |
| Lambda promotion driver and queue | `interp.cpp` `interp_satellite_*`, `interp_promote_function_if_hot` |
| Lambda handoff plan and continuation | `interp_plan.cpp` `plan_mark_handoff_loops`; `interp.cpp` `interp_build_loop_continuation` |
| Shared cell and plan types | `ast-core.hpp` `FnPromotionCell`, `FnFramePlan`, `AstLoopControlNode` |
