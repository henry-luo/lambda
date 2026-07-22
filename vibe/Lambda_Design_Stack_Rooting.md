# Lambda Stack Rooting: safepoint-current canonical slots

**Status:** IMPLEMENTED — S0–S6 are complete for exact-rooted
Lambda/LambdaJS builds. Every build physically omits native-stack root
discovery. C2MIR and unported guest tiers are disabled until each publishes an
exact-root ABI. The current release Test262 baseline is
fully green with zero retries. Any remaining project-wide Radiant-refactor
baseline debt is outside this design and is not an open rooting implementation
item.

**Date:** 2026-07-19

**Supersedes:** `vibe/Lambda_Design_Stack_Frame2.md` ("Removing conservative
native-stack rooting from Lambda", 2026-07-16) — that document's blockers
(§7), procedures, gates (§9), and unsafe-actions list (§11) are absorbed here
and the file is removed. This document is the single authority on rooting
scheme and conservative-scan retirement.

**Companion documents:**

| Document | Role relative to this design |
|---|---|
| `vibe/Lambda_Stack_JS_MIR.md` | Historical measurement of the superseded shadow-copy rooting (7.59x MIR, findings R0–R10) — the motivating evidence; its “current” column is commit `9402b169`, not the post-implementation state |
| `vibe/Lambda_Stack_MIR.md` | Historical Lambda-side runtime stack/rooting review through commit `9402b169`; post-implementation counts are in §12 of this document |
| `vibe/Lambda_Stack_Rooting_Schemes.md` | Scheme survey; §12 supplies the stable-home foundation, refined here with safepoint-current write-back; §13 records stack maps as KIV |
| `vibe/Lambda_Design_Stack_Frame.md` | SF1–SF20 lifetime architecture (side stacks, number watermarks, scalar re-homing, env ownership) — **unchanged by this design**; this doc replaces only *how live values are published as roots* |

The implementation checkpoints below are chronological records. Statements in
an earlier checkpoint that a later stage was incomplete describe that point in
the implementation history; the **final implementation checkpoint** is the
current status. The historical root-mode and scan diagnostics described below
were removed in the final retirement; they are not available in any build.

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

At this checkpoint, GC use/definition records, per-safepoint
liveness/root-set analysis, stable binding homes, scratch coloring,
precise-only/shadow collector modes, forced-GC hooks, and deletion of
broad/post-instruction publication were not yet implemented. The current
side-root behavior was intentionally retained until those pieces could make
S1 independently provable.

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

### Implementation checkpoint — 2026-07-18

The next behavior-neutral S0 slice adds the first shared semantic GC event
stream:

- `MirEmitter` now owns diagnostic definition, use, and safepoint event
  records. Each record carries a semantic home ID, MIR register, GC
  representation class, call effect where applicable, source label, and
  sequence number.
- LambdaJS assigns one-based home IDs to semantic bindings in `jm_set_var()`.
  A reassignment of the same binding retains its home ID; a lexical shadow
  receives a fresh ID. This deliberately avoids the rejected register-keyed
  home model described above.
- Ordinary local-identifier reads emit use events. Effect-aware helper calls
  emit safepoint events from their `JitImportMetadata`; any raw call that
  bypasses that wrapper is conservatively recorded as `MAY_GC`.
- `LAMBDA_MIR_LOG_GC_EVENTS=1` emits one summary per generated function;
  `=2` also emits the individual ordered events. Event storage is allocated
  only when this diagnostic is enabled, so normal compilation does not pay
  for an event buffer.

This slice still emits the existing side-root MIR. It does not yet use the
event stream to size canonical slots, calculate liveness, insert stores, or
remove broad publication. Local identifier reads, call operands, and call
results are now recorded in program order: argument temporary uses precede
the call safepoint and result temporary definitions follow it. Effect-aware
calls preserve their semantic argument/result classes; raw bypass calls use
the conservative `UNKNOWN` class. Control-flow joins and special binding
paths still need explicit coverage before backward root-set analysis can be
trusted. S0 is therefore **partially implemented**, not complete.

The collector-side S0 diagnostics and deterministic stress hook were recorded
at this historical checkpoint:

- every heap defaults to `compatibility`; `LAMBDA_GC_ROOT_MODE=shadow-verify`
  retains the native-stack scan and reports scan-exclusive objects, while
  `LAMBDA_GC_ROOT_MODE=precise-only` skips both `setjmp()` register discovery
  and native-stack marking without a fallback;
- the collector records direct precise-root count, conservative candidate
  words, newly discovered objects, scan bytes, poisoned words, and
  scan-exclusive counts by type;
- the precise object graph is traced to closure before the shadow scan. This
  ordering is required: otherwise a child reachable from a precise parent
  would be falsely classified as stack-exclusive merely because its parent
  had not been traced yet;
- `LAMBDA_GC_FORCE_EVERY=N` forces collection before every Nth public object,
  class-specialized, JIT bump, or data-zone allocation. The existing
  `collecting` guard prevents recursive forced collection, and the default
  interval is zero, so production behavior is unchanged unless explicitly
  enabled.

Verification for this checkpoint:

- debug `make build`: pass with zero warnings/errors;
- focused `regression_side_stack_frame_gc` and `v20_tdz`: 2/2 pass;
- complete GC heap unit suite: 47/47 pass, including compatibility-stack,
  shadow-classification, precise-only-no-fallback, and all-allocation-path
  forced-schedule tests;
- `regression_side_stack_frame_gc.js` passes in precise-only mode with forced
  collection both every 50 allocations and every allocation (`N=1`);
- diagnostic sample for `js_main` in the forced-GC frame regression:
  `homes=30`, `events=893`, `definitions=30`, `uses=15`, `may_gc=848`,
  `no_gc=0`;
- verbose TDZ output shows stable, distinct semantic homes for the tested
  `let` and `const` bindings and ordered safepoint records.

The semantic argument/result filter and the first audited leaf-only `NO_GC`
set are now landed. Root publication treats `BOXED_ITEM` and
`RAW_GC_POINTER` as roots, rejects `NON_GC_SCALAR` and
`RAW_NON_GC_POINTER`, and preserves the old physical `I64`/`P` fallback for
`UNKNOWN`. The audited leaves are `memset`, `memcpy`, `fmod`, the double-bit
conversion helpers, `js_is_truthy`, `js_args_save`, `js_args_restore`, and
`js_check_exception`. Each has no Lambda-GC allocation, unknown native call,
or generated-code re-entry; every unaudited helper remains `MAY_GC`.

On the forced-GC `js_main` probe this reduces roots from 729 to 513,
root stores from 1189 to 909, and `MAY_GC` calls from 848 to 576, with 272
calls classified `NO_GC`. The post-merge full JS gate remains independently
red at the exact control result of 351/367, with the same 16 failures seen on
untouched `HEAD`; focused rooting regressions and precise-only forced-GC at
`N=1` remain green. These leaf classifications are retained because they are
individually audited and their rooting behavior is covered independently of
that pre-existing gate debt.

### Implementation checkpoint — 2026-07-18 (safepoint write-back and native roots)

The implementation has advanced beyond the S0-only checkpoints above:

- LambdaJS now solves CFG liveness, assigns stable binding homes and colored
  scratch homes, propagates conservative `MAY_GC` call sites, and inserts only
  dirty live roots immediately before safepoints. `LAMBDA_MIR_ROOT_MODE=write-through`
  remains the differential correctness oracle; write-back is the default.
- Production write-back no longer constructs eager root stores and deletes them
  after analysis. Candidate registration is metadata-only until the final
  safepoint pass. Full-scope and post-instruction publication remain reachable
  only from the explicit write-through oracle.
- Every direct LambdaJS scope insertion now goes through one fresh-binding
  installation path. This fixes a precise-root hole where memset-initialized
  generator, capture, parameter, lexical-`this`, and synthetic loop entries had
  `root_slot == 0` without owning either slot zero or a semantic home.
- `RootFrame`, `Rooted`, `LambdaHandle`, `LambdaMutableHandle`,
  `PersistentRooted`, `AutoAssertNoGC`, the corresponding C frame API, and
  non-local-recovery checkpoints are implemented. High-risk promise/event-loop,
  closure/bind, async-drive, evaluator, container, regex, VMap, and Radiant/Jube
  paths have begun migration to exact native roots.
- Compatibility, shadow-verify, and precise-only collector modes; deterministic
  and seeded randomized forced-GC schedules; root statistics; and the C2MIR
  precise-context rejection policy are implemented. C2MIR source remains
  unchanged and is built only in the Jube configuration.

Current focused verification is green in precise-only mode with forced
collection before every public allocation (`N=1`): the 17-case side-stack
regression, async promise/timer resumption probe, generator baseline, and the
write-through/write-back differential all pass. The GC heap suite passes 52/52.
For the side-stack regression, the current debug MIR dump is 6,876 lines in
write-back mode versus 7,600 in the write-through oracle; release performance,
not this debug structural count, remains the S1 acceptance measurement.

The synchronized remote `HEAD` currently has an independent JavaScript baseline
debt. A clean archive-built `HEAD` executable reproduces the same class-field,
crypto, DNS, Ramda, Highlight.js, and runner-test-id failures as the rooting
build. These are not attributed to rooting and do not relax the final zero-debt
gate. Remaining design work is the shared-emitter S2 convergence, complete
native/Jube/async/persistent audits and static enforcement (S3/S4), precise-only
suite rollout (S5), and scanner retirement from precise contexts (S6).

### Implementation checkpoint — 2026-07-18 (shared convergence and release gate)

The shared-emitter boundary and failure behavior have advanced further:

- semantic root-candidate registration, register-index growth, candidate
  propagation through `MIR_MOV`, call-site effect registration, CFG successor
  discovery, bit-set traversal, argument detection, and the correct
  write-through fallback after analysis allocation failure now live in
  `mir_emitter_shared.hpp`;
- analysis-metadata allocation failure can no longer silently produce unrooted
  precise code. Candidate metadata is fail-stop because omitting a candidate is
  unsound; late liveness/dirty-analysis allocation failure reconstructs the
  write-through oracle before compilation continues;
- the obsolete native `LambdaRootGuard` pilot has been removed. The last three
  array-view constructors now use `RootFrame`/`Rooted`, so there is no second
  C++ root-frame abstraction left alongside the canonical API;
- nested root-frame watermark restoration and recovery-checkpoint restoration
  of both side stacks have direct unit coverage;
- native `RootFrame` reservation failure is fail-closed. It reports the
  overflow and leaves through the armed execution recovery point; without a
  valid recovery target it aborts instead of letting `Rooted` silently become
  a null, non-rooting home. Oversized C-frame rejection is covered without
  advancing the watermark.

CR5's policy-placement requirement is now implemented: advanced liveness,
scratch coloring, dirty-state propagation, OOM fallback, and final store
insertion live in `em_finalize_semantic_root_write_back()`; the LambdaJS
finalizer is a thin adapter that passes semantic candidates, call sites, frame
anchors, and receives slot/store counts. S2 is still incomplete because Lambda
MIR-Direct feeds the older shared eager-oracle finalizer rather than reporting
the same semantic candidate stream. The two shared finalizers still need to
converge on the semantic-event path before Lambda MIR-Direct has the same
stable/scratch/dirty behavior.

Lambda MIR-Direct now delegates that eager correctness oracle to the same
semantic finalizer. It records helper call sites with the shared import-table
`NO_GC`/`MAY_GC` effect, compacts independent homes with CFG liveness and dirty
write-back, and retains eager stores only for a home that is also program-
semantic value-merge memory. In a mixed function those retained offsets alone
are reserved; optimized neighboring homes compact around them. This is an
intentional transitional invariant: removing a merge store before Lambda's
join lowering stops reading the root frame would change program behavior.
Consequently S2 is functionally converged at the finalizer and call-effect
layers, but remains incomplete until Lambda emits semantic candidates directly
and no longer constructs the eager oracle during lowering.

Verification after this slice:

- debug `make build`: pass (repository-wide pre-existing warnings only);
- GC heap/side-stack unit suite: 55/55 pass;
- LambdaJS precise-only + collection before every public allocation: all 17
  side-stack regression cases pass;
- Lambda MIR-Direct `proc_var` passes precise-only with collection before every
  allocation, and `proc_var_type_widen` is byte-identical between production
  write-back and the write-through oracle;
- the representative `comp_expr` function now reports 338 classified call
  sites and emits 332 final root stores instead of the former 676 eager stores,
  with a compact 332-slot frame;
- the audited leaf set now also covers Lambda representation readers
  (`is_truthy`, `item_type_id`, and `it2*`). A debug-startup allowlist validator
  rejects a new or malformed `NO_GC` table entry unless the frozen audited set
  is updated explicitly;
- the post-change full baseline has Lambda 555/555 green; JavaScript 362/375
  and Node preliminary 106/110 remain in the same synchronized-`HEAD` control
  debt set described above;
- precise-only async promise/timer resumption probe at the same forced-GC
  schedule: pass;
- normal `test-lambda-baseline`: input 2105/2105, Lambda 555/555, JS 362/375,
  Node preliminary 107/110. A synchronized clean `HEAD` control reproduces the
  representative semantic failures; the DNS batch timeout is load-sensitive;
- precise-only `test-lambda-baseline` has the same semantic debt. Its additional
  Lambda `latex_m7` and Node DNS failures are load timeouts; isolated
  `latex_m7` passes in 22.4s and direct DNS completes.

The first same-machine, same-harness release comparison is:

```text
build                         total    sync     async    non-batched
synchronized clean HEAD      326.9s   266.9s    58.1s      1.2s
current safepoint write-back  332.2s   275.7s    54.5s      1.3s
delta                           +1.6%    +3.3%    -6.2%
```

This first pair is inside §11's 5% target and 10% hard gate. It does **not**
complete the performance acceptance criterion: the required repeated-run
median and GC/MIR structural counters still have to be collected. For
historical context, the separately preserved older pre-stack release takes
512.0s under the current harness, so it is not a valid source-isolated control
after the remote synchronization; the synchronized clean `HEAD` build above is
the relevant patch-isolation control.

### Implementation checkpoint — 2026-07-19 (direct Lambda candidates and ABI audit)

Lambda MIR-Direct no longer constructs the eager rooting oracle for ordinary
bindings or temporaries in production write-back mode. Every GC-class value is
registered directly in the shared semantic candidate stream. Binding
definitions are buffered as metadata; only if lowering actually changes a
binding's physical MIR register are the buffered stores materialized, because
that slot is then genuine program-semantic control-flow merge memory. The
write-through oracle remains available through `LAMBDA_MIR_ROOT_MODE`, and
analysis failure still reconstructs a complete correctness oracle. This closes
the remaining S2 lowering gap without changing Lambda's join semantics.

The native/Jube audit also advanced:

- the Jube host API is now rooting ABI v2 and exposes scoped C root-frame
  begin/take/end operations in addition to persistent root registration;
- v1 modules remain loadable, but admission sets the Runtime's compatibility
  requirement permanently, so they cannot execute under a precise-only heap;
- fixed JS root ranges and caches now re-register by JS heap epoch rather than
  a process-lifetime boolean. DNS, TLS, Atomics waiters, eval-source stacks,
  test262 agents, diagnostics-channel state, async hooks, assertion state,
  Blob URLs, drag state, net active objects, and mock-scheduler waits therefore
  remain exact after heap/context reuse;
- iterator prototype caches are persistent exact roots and use construction
  frames; array/string/typed-array iterator constructors root unpublished
  sources and results; zlib constructors, prototypes, constants, and codes use
  exact construction roots; fs, crypto, util, and OS namespaces use persistent
  roots plus exact construction frames.

Verification at this checkpoint:

- debug build: pass with zero errors/warnings;
- normal full baseline: input 2105/2105, Lambda 555/555, JS 363/375,
  Node preliminary 107/110; the same 15 failures are the synchronized-`HEAD`
  control set already documented above;
- Lambda MIR-Direct precise-only suite: 555/555;
- LambdaJS JIT and MIR-interpreter 17-case side-stack regression: pass under
  precise-only forced GC with freed-object poisoning;
- collection before every public allocation (`N=1`) with poisoning: all 35
  stressed built-in Node namespaces load and survive an explicit GC; the Jube
  host-object import probe also passes;
- `proc_var` passes precise-only at `N=1`; `proc_var_type_widen` remains
  identical between write-through and direct write-back; representative
  `comp_expr` remains 332 stores/slots for 338 candidates while lowering no
  longer emits ordinary eager stores;
- the release Test262 baseline currently has 19 semantic failures. A
  synchronized clean-`HEAD` binary reproduces the isolated failures with
  byte-identical messages, so they are baseline/control debt rather than
  rooting regressions. The first fully recorded current run completes the
  40,261-test execution phase in 240.4s (sync 195.5s + async 43.4s +
  non-batched 0.9s).

### Implementation checkpoint — 2026-07-19 (effect gate and precise default)

The exceptional `NO_GC` classification is now statically enforced. The new
`utils/check_gc_effects.py` reads the actual import table and its independently
frozen audited list, builds a conservative source call graph, follows project
callees transitively, and fails on a Lambda allocator, collector entry,
generated-code re-entry, unknown call, unknown member call, or list drift. It
currently verifies all 18 `NO_GC` imports through 22 project call-graph nodes.
The check is registered as the mandatory `gc-effects` structural lint and
passes through `make lint ARGS='--rule ^gc-effects$'`.

`make test-gc-rooting` is now the permanent focused exact-root lane. It runs
the 17-case LambdaJS regression through both native JIT and the MIR interpreter,
runs Lambda MIR-Direct `proc_var`, forces collection before every public
allocation, poisons freed objects, compares exact output goldens, and runs the
transitive effect audit. The complete lane passes.

S5's development flip is implemented for scanner-independent builds: absent an
explicit environment override, builds without C2MIR create precise-only heaps.
A build that includes unchanged C2MIR remains compatibility-default. Invalid
root-mode overrides fail toward compatibility rather than accidentally
selecting precise execution. The mode remains sticky per heap and a
conservative-only tier still cannot enter an explicitly precise context.

Verification after the flip:

- default-mode full baseline: input 2105/2105, Lambda 555/555, JS 363/375,
  Node preliminary 107/110; exactly the same 15 synchronized-control failures,
  with no new precise-root failure;
- Jube rooting ABI v2 host-object probe: pass in both its C2MIR-capable build's
  compatibility default and explicit precise-only N=1 + poisoning;
- shadow-verify N=1 executes all 17 regression cases. Its scan-exclusive set
  consists of stale native-stack words: the byte-identical precise-only N=1 +
  poison run frees them without changing any observed result;
- three recorded current release Test262 execution phases are 240.4s, 240.3s,
  and 244.7s, for a 240.4s median. Each has the same 19 control failures and
  zero retries. This completes the repeated current-build timing side of the
  §11 measurement; the synchronized clean-HEAD comparison remains the earlier
  326.9s control measurement.

### Final implementation checkpoint — scanner retirement and audit closure

The rooting implementation is complete for Lambda MIR-Direct, LambdaJS JIT,
and the MIR interpreter:

- every build creates exact-root heaps. The collector ABI no longer takes a
  native stack region, and objects/binaries contain no native-stack root
  discovery, register-flushing `setjmp`, or scan-exclusive implementation;
- C2MIR and legacy Jube modules are disabled. Python, Ruby, and Bash entry
  paths fail closed until their generated frames implement the same
  canonical-slot protocol;
- automatic native locals were migrated to `RootFrame`/`Rooted`; durable
  ownership uses `PersistentRooted` or stable registered ranges. Static JS
  registries re-register by heap epoch rather than process-lifetime booleans,
  so context reuse cannot leave a new heap depending on an old registration;
- the new `gc-root-hazards` lint scans all 12,081 migrated native functions and
  rejects automatic-local addresses registered as roots, transient
  register/unregister guards, and process-lifetime gates for heap-local
  registries. Stable-storage handoffs and explicitly resettable registries are
  narrow, reviewed exceptions;
- the generated-code effect checker verifies the frozen 18-member `NO_GC`
  allowlist transitively through 22 project call-graph nodes. All other or
  unknown calls remain `MAY_GC`;
- native recovery checkpoints restore both object-root and number-stack
  watermarks after non-local recovery. The shared guest-exit helper transfers a
  standalone guest heap/name pool/type list to its `Runtime` for normal
  cleanup, or destroys it directly when no Runtime owns it; this removes the
  per-invocation Heap-wrapper leak in the Python/Bash/Ruby bridges;
- GC-free input DLLs use explicit fallback homes at their GC boundary. The
  input/format parsers themselves retain their intentional pool/arena ownership
  model and are not conservatively reclassified as GC-heap code.

Permanent verification now includes `make test-gc-rooting`: native JIT and MIR
interpreter execution of all 17 rooting cases with collection before every
public allocation, a seeded randomized schedule, freed-object poisoning,
Lambda MIR-Direct `proc_var`, exact output comparisons, the effect checker, and
the native-root hazard checker. The complete GC heap/side-stack suite is 57/57.

Final gate results:

- focused precise-root gate: pass in full;
- Lambda input baseline: 2105/2105; Lambda MIR-Direct: 555/555;
- Ruby compatibility lane: 22/22; Bash compatibility lane: 37/37;
- rebuilt Jube precise-only forced-GC JS probe: 17/17; explicit Python
  precise-only admission is rejected with the required capability error;
- repeated release Test262 execution median: 240.4s, with the same 19 semantic
  failures as synchronized clean `HEAD` and zero retries in all three accepted
  runs. A later 345.8s run under heavy host contention is excluded from the
  performance sample; its isolated semantic failures reproduce the same
  control debt and its additional regex timeout is load-sensitive;
- final default Lambda baseline: input 2105/2105, Lambda 555/555, JavaScript
  362/375, Node preliminary 106/110. The 17 red cases are exactly the recorded
  synchronized-`HEAD` control set (class field, crypto/DNS, selected library,
  and runner-test-id behavior);
- Radiant re-entry coverage passes its page snapshot (41/41), headless
  page/Markdown (104/104), fuzzy-crash (24/24), and CSS syntax (38/38) lanes.
  The complete Radiant baseline remains red in layout, Markdown-view, and
  render goldens owned by the separate active DOM/view refactor.

Therefore implementation closure is distinct from repository-wide baseline
closure: the precise rooting scheme and scanner retirement have no remaining
code item. Any unrelated Radiant-refactor failures remain visible and are not
waived or hard-coded around.

### Post-closure verification addendum — 2026-07-19

The final rooting implementation is now followed by a fully green release
Test262 baseline at commit `638e11c93`:

- all 42,889 tests completed in 160.6s;
- all 40,261 admitted tests fully passed;
- zero non-fully-passing tests, zero failures, zero regressions, and zero retry
  time.

This supersedes the JavaScript/Test262 failure counts in the chronological
checkpoints above as a statement of current repository status. Those older
counts remain in place because they record the controls and intermediate
conditions under which the rooting implementation was reviewed. The
post-implementation MIR/code-size evidence is recorded separately in §12.

## 1. Goal and scope

The primary design goal is **precise rooting with performance close to the
pre-stack baseline**. Correctness is non-negotiable, but a scheme that restores
correctness by adding work after every instruction, definition, or allocation
does not meet the goal. Rooting work must be proportional to the live managed
state that actually crosses GC safepoints.

The implemented generated-code GC rooting contract for every precise execution
tier in the Lambda runtime is **safepoint-current canonical-slot frames**:

1. **Generated code** — Lambda MIR-Direct and LambdaJS today; Python and every
   future guest only after it emits through the shared `MirEmitter` and can be
   admitted to a precise context;
2. **The MIR interpreter** — which executes the same emitted frames;
3. **C/C++ runtime helpers** — via slot-backed handle types over the same
   side-root stack.

Legacy C2MIR is disabled: it remains frozen until its emitter publishes the
same exact-root protocol. It is not a target of this rooting migration (CQ2).

The central generated-code rule is: each rootable binding has a stable logical
home and each protected temporary has a scratch home, but those slots are
required to be current only when execution enters a `MAY_GC` call. Between
safepoints, a MIR register may hold a newer value and the compiler tracks the
home as dirty. Native helpers use the same slots with a simpler always-current,
write-through `Rooted<T>` discipline because C/C++ does not have the emitter's
root-liveness analysis.

End state for Lambda MIR-Direct, LambdaJS, the MIR interpreter, and migrated
guest/helper paths: native-stack rooting is **fully retired from every
execution context**. C2MIR is unavailable until it implements exact roots.
Retirement means removing the collector's dependence on native stack contents:

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

Non-goals: moving/compacting or generational collection (this design creates
exact mutable root locations that such a collector would require, but does not
by itself unlock one); changes to the number stack, scalar return donation, or
closure env ownership (all settled, SF1–SF20); write barriers.

## 2. Key design pillars

The rooting design is the combination of the following contracts. None of the
first seven can be removed independently: generated frames cover JIT values,
native handles cover helper locals, ownership rules cover values that outlive
an activation, unwind restoration covers paths that skip normal cleanup, and
exact-root stress proves that native-stack discovery is no longer carrying
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

The superseded LambdaJS implementation was a *shadow-copy* scheme: rooted
values lived in MIR registers and the transpiler emitted synchronizing stores
to keep side-root copies current — full live-scope republication before every
helper call, representation-blind rooting of every `I64`/`P` call operand and
result, and a post-instruction binding scan. Its measured result
(`Lambda_Stack_JS_MIR.md`) was:
7.59x MIR for the probe function, 84.3% of added instructions being root
copy/store pairs, and Test262 regressing 195.7s → 349.4s — while the
conservative scan was still retained underneath (dual rooting, no payoff).

The scheme options were surveyed in `Lambda_Stack_Rooting_Schemes.md`:

| Scheme | Mutator cost | Precision | Backend changes | Can retire the scan |
|---|---|---|---|---|
| Conservative-primary | ~0 | over-approximate | none | no — it *is* the scan |
| Shadow-copy (superseded) | 7.59x measured | exact but redundant | none | no (helpers) |
| Liveness-pruned anonymous shadow/scratch slots | dirty live stores per safepoint | exact at safepoints | none | no (helpers) |
| Always-current canonical slots | ~1 store per rooted definition | exact per slot | none | yes, with §7 helper contract |
| **Safepoint-current canonical slots (implemented)** | dirty live-home/scratch stores at `MAY_GC` calls | exact at safepoints | none | **yes — with §7 helper contract** |
| Safepoint stack maps | 0 | exact | fork MIR backend | only with helper contract too; KIV per schemes doc §13.5 |

The implemented scheme retains the most useful canonical-slot property: **every rootable
binding has a stable side-root home**. It changes when that home must be
updated. Generated code treats the register as a write-back cache between
safepoints and flushes only dirty, live homes before a `MAY_GC` call. Unhomed
temporaries and call arguments use reusable scratch homes. A million no-GC
assignments followed by one allocating call therefore require one root store,
not a million write-through stores.

This scheme uses compiler GC-liveness, dirty-state, and scratch-slot analysis,
but no MIR backend changes. The analysis replaced the broad dynamic
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
by construction* and this design freezes it as an API contract:

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

No `setjmp()` flush, no native stack bounds, no raw-word stack scan. This is
the implemented collector path in every build.

## 5. Generated code: shared emitter and per-tier adoption

### 5.1 The shared MirEmitter owns the scheme

Frame lifecycle, home identity, dirty state, and safepoint publication are
`em_*` primitives in
`lambda/mir_emitter_shared.hpp` (`MirEmitter`). Lambda MIR-Direct and LambdaJS
both report semantic root candidates and call sites to this common emission
layer:

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
they do not scan scopes or emit ad-hoc production root stores. This resolves
the JS review's R7 structurally and gives Python and future guests the same
policy when they are ported.

**CR5 — Rooting policy and final root-store insertion live only in
`MirEmitter`; per-language emitters provide semantic events and may not emit
root stores directly.**

### 5.2 LambdaJS: broad publication replaced by safepoint root sets

LambdaJS now implements the shared policy:

- `jm_set_var()` assigns stable semantic binding homes and reports definitions
  to `MirEmitter`; a lexical shadow receives a different home while a
  reassignment retains its logical identity;
- helper calls flow through effect-aware emission. Only `MAY_GC` call sites
  participate in root publication, and representation metadata excludes known
  non-GC values;
- `em_finalize_semantic_root_write_back()` performs CFG liveness, dirty-state
  propagation, scratch coloring, late slot sizing, and final store insertion;
- production write-back performs neither full-scope republication nor a
  post-instruction binding scan. The eager call/register and output-update
  behavior remains only in the explicit write-through correctness oracle and
  fail-closed analysis fallback;
- frame slots are zero-initialized before publication, and zero-root frames can
  elide root-stack reservation;
- `LAMBDA_MIR_ROOT_MODE=write-through` remains the differentially tested
  always-current reference mode.

The design estimate was 440 → approximately 130 executable MIR instructions.
The implemented result is **440 → 136** on the same review function, with
root publication reduced from 161 copy/store pairs to 11 direct stores. The
complete post-implementation measurement and its limitations are in §12.

### 5.3 Lambda MIR-Direct: converged onto the same primitives

Lambda MIR-Direct now reports GC-class binding and temporary candidates plus
classified call sites directly to the shared emitter. It no longer constructs
the eager oracle during ordinary production lowering. The shared finalizer
owns liveness, dirty write-back, compact slot assignment, and OOM restoration
of the correctness oracle for both Lambda and LambdaJS. Program-semantic merge
memory remains separate from rooting memory and is retained only where Lambda
join lowering actually reads it. On the review probe this convergence reduces
the side-stack implementation from 138 to 101 executable MIR instructions;
see §12.

### 5.4 Python and future guests

`transpile_py_mir` currently emits **no exact rooting** — its envs/generator
frames are pool-pinned
(Stack_Frame_Python PO2/PO8). Python adopts safepoint-current slots by
construction when it ports onto the unified AST + `MirEmitter`
(`Lambda_Unified_AST_Impl_Plan.md`; `Lambda_Impl_Stack_Frame_Py.md` P1
already routes its frames through the shared em pair). Until then, the Python
entry path is disabled.

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

**CR7 — C2MIR remains unchanged and disabled.** No C2MIR emission, frame,
rooting, or runtime-support code is changed by this design. Re-enable it only
after its emitter publishes exact roots at every `MAY_GC` boundary. This
resolves CQ2.

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

The conservative scan in a compatibility/debug build may mask a
misclassification and is not guaranteed to discover a value retained only in
an optimized caller-saved register. Compatibility mode is therefore tier
support, not validation; per-safepoint root-set inspection, write-through
differential mode, and the precise-only stress gates validate classification
without conservative discovery.

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

C+ convention (no `std::` types), implemented directly over the side-stack C
API. The former `LambdaRootGuard` snapshot-push pilot has been removed: its
pushed copy did not track later mutation of the local, whereas a slot-backed
local does and keeps the contract valid for a future moving collector:

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
// Lambda prefix avoids collision with the macOS Carbon `Handle` typedef.
template <typename T> class LambdaHandle       { const uint64_t* slot_; ... };
template <typename T> class LambdaMutableHandle{ uint64_t* slot_; ... };

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

Inherited from the retired `Lambda_Design_Stack_Frame2.md`; each blocker is
listed with its implemented resolution.

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

## 9. Implemented migration sequence

S0–S6 are complete. The sequence is retained here as the implementation and
review record. Performance was the primary design goal after correctness, so
S1 was not considered complete when broad rooting was merely replaced by a
correct slower form; it closed only after the §11 structural and release gates.

- [x] **S0 — Tables, analysis scaffolding, and diagnostics — complete.** Import-table
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
- [x] **S1 — LambdaJS safepoint-current slots — complete** (§5.2). First build an
  always-current write-through mode as a correctness oracle, then make
  liveness/dirty write-back + scratch coloring the production mode. Delete all
  broad publication and post-instruction scans. Gates: probe MIR golden/count
  test, differential write-through-vs-write-back execution,
  `regression_side_stack_frame_gc.js` under forced GC + ASan, JS gtests, full
  Test262 timed against the pre-stack release binary, and §11's performance
  budget. The write-through oracle remains debug-only.
- [x] **S2 — Lambda MIR-Direct convergence — complete** (§5.1/§5.3). Lambda MIR-Direct adopts
  the frame, semantic-event, safepoint-analysis, and store-insertion machinery
  already established in the shared `MirEmitter` for S1. Aligns with
  unified-AST Phase 0; Python inherits at its port (P1 of the Py impl plan).
- [x] **S3 — Helper API + core helpers — complete** (§7.1–§7.3). Land
  `RootFrame`/`Rooted`/`Handle`/`PersistentRooted`/`AutoAssertNoGC` + C ABI;
  land the shared RH8 recovery-checkpoint helper and audit all existing
  `setjmp`/`longjmp` boundaries; migrate §7.5 items 1–3; lint ratchet on
  migrated modules.
- [x] **S4 — Subsystem audits — complete** (§7.5 items 4–8), driven by scan-exclusive
  candidate reports (§10.1); JO-ledger de-pinning (RH5); persistent/async
  ownership audit (RH5 range rules, RH6 handoffs, context-reuse resets);
  Jube ABI versioning and module forced-GC tests; verify that unchanged C2MIR
  can only select compatibility mode (CR7/CQ2). Unported Python/Ruby/Bash
  guests remain capability-gated compatibility tiers rather than pretending
  to satisfy the precise contract. Input/format pool/arena code is excluded by
  CQ4's ownership invariant; only a future explicit GC-heap boundary would
  enter this audit.
- [x] **S5 — Precise-only as default — complete.** Scanner-independent
  development and release builds default to precise-only. Rollout never
  toggles scanning per entrypoint (B12); compatibility is admitted only via
  the §9.1 context/build capability.
- [x] **S6 — Retire the scan from precise execution — complete.**
  Scanner-independent release builds omit GC-only `setjmp()` register
  flushing, `stack_current`/GC `stack_base` collector parameters,
  `gc_scan_stack()`, and its ASan scan helpers and profiling counters. Stack
  bounds needed for overflow protection remain. Builds that ship unchanged
  C2MIR retain the scanner only for sticky compatibility contexts; it is not a
  fallback for a failed precise context. Scanner-capable debug builds retain
  **shadow-verify mode** (§10.1) as a regression diagnostic.

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

### 10.1 Collector diagnostic modes (implemented at S0, kept forever)

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

Current switches and APIs:

```text
LAMBDA_GC_ROOT_MODE=compatibility   precise roots + native scan
LAMBDA_GC_ROOT_MODE=shadow-verify   same safety + scan-exclusive reporting
LAMBDA_GC_ROOT_MODE=precise-only    precise roots only; no setjmp/stack scan

gc_set_root_mode()
gc_get_last_root_stats()
gc_get_last_conservative_type_count()
```

The modes are sticky for the heap created by `heap_init()` or
`heap_init_with_pool()`. The setter exists for collector unit tests; production
entrypoints configure the mode once when the heap is created. Scanner-
independent development builds default to precise-only; a build containing
unchanged C2MIR defaults to compatibility. An explicit valid environment mode
overrides that build default, subject to the sticky §9.1 capability policy.

### 10.2 Forced-GC stress (legal safepoints only; deterministic interval implemented)

Collect at aggressively varied **legal** `MAY_GC` boundaries — never at
invented mid-allocator points the real runtime cannot produce. Schedules:
before every public GC allocation/slow allocation path; every Nth allocation
for several N; randomized deterministic schedules with logged seeds; at callback/async
handoff boundaries; during exception/error propagation; under deep recursion
and large frames. Run both shadow-scan and precise-only variants, with heap
poisoning and ASan.

`LAMBDA_GC_FORCE_EVERY=N` and `gc_set_force_collect_interval()` provide the
deterministic every-N schedule across all public GC allocation paths.
`LAMBDA_GC_FORCE_SEED=S` plus `LAMBDA_GC_FORCE_ONE_IN=N` (or
`gc_set_force_collect_random()`) provides a reproducible pseudo-random
one-in-N schedule and logs the seed with every forced collection. Dedicated
async promise/timer, generator, module-initialization, and context-reuse probes
execute their real callback/handoff paths under the every-allocation schedule;
a synthetic mid-callback injection point is neither needed nor legal because
collection remains confined to public allocation safepoints. The permanent
focused CI command is `make test-gc-rooting`. `LAMBDA_GC_POISON_FREED=1` (or
`gc_set_poison_freed()`) now overwrites
dead object payloads with `0xDD` after finalization and before object-zone
reuse; allocation zeroes recycled slots. Combined with precise-only forced GC,
this makes stale references into retained bump blocks and slabs observably
invalid instead of accidentally readable.

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

- [x] Lambda MIR-Direct, LambdaJS, MIR-interpreter exact-root audits complete
- [x] C2MIR compatibility isolation enforced without changing C2MIR code
- [x] Evaluator/procedural/fallback paths migrated
- [x] Helper root API complete for C and C++; Jube handle ABI enforced
- [x] Async + persistent handoffs audited; ranges have stable-address proof
- [x] `NO_GC`/`MAY_GC` contract enforced transitively (CI diff green)
- [x] Shadow reporting, precise-only mode, forced-GC schedules, static
      analysis all active in CI; no unexplained scan-exclusive roots
- [x] Rooting-specific §10.3 precise-only matrix green, including the current
      zero-failure/zero-retry Test262 gate; unrelated Radiant-refactor goldens
      remain a separate project baseline and do not leave a rooting item open
- [x] Scan retired from precise contexts per S6; any retained copy is isolated
      to C2MIR compatibility/debug verification; stack-overflow bounds remain
      functional; docs updated

## 11. Performance is the primary design gate

After correctness, recovering the pre-stack execution profile is the main
reason for this scheme. S1 removed the measured 7.6x MIR expansion while
keeping precise rooting; conservative scan retirement was not used to postpone
the generated-code performance result. The post-implementation structural
measurement is §12.

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

- **Implemented generated-code result** (S1): generated MIR/code size is
  recovered through safepoint-current write-back, and the release execution
  measurements stayed within the accepted gate.
- **Implemented precise-execution retirement** (S6): per-collection work
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

## 12. Post-implementation MIR instruction profiling and comparison

### 12.1 Method and comparison boundaries

The post-implementation profile was captured at commit `638e11c93` with the
same representative source functions used by `Lambda_Stack_MIR.md` and
`Lambda_Stack_JS_MIR.md`:

- Lambda: `_frame_review_0` from `temp/lambda_stack_mir_probe.ls`;
- LambdaJS: `_js_frameReview_149` from
  `temp/stack_mir_review_probe.js`.

Captured artifacts:

| Mode | Lambda | LambdaJS |
|---|---|---|
| Production write-back MIR | `temp/lambda_mir_latest_writeback.txt` | `temp/js_mir_latest_writeback.txt` |
| Write-through oracle MIR | `temp/lambda_mir_latest_writethrough.txt` | `temp/js_mir_latest_writethrough.txt` |
| Slot/store diagnostics | `temp/lambda_mir_latest_writeback.log`, `temp/lambda_mir_latest_writethrough.log` | `temp/js_mir_latest_writeback.log`, `temp/js_mir_latest_writethrough.log` |
| Historical MIR/evidence | `temp/lambda_mir_pre.txt`, `temp/lambda_mir_current.txt`, `vibe/Lambda_Stack_MIR.md` | `vibe/Lambda_Stack_JS_MIR.md` |

An executable MIR instruction is an opcode entry between the function's
`func` and `endfunc`. Declarations, `local` lines, comments, blank lines,
labels, prototypes, imports, and `endfunc` are excluded. Debug builds are used
only because they expose the pre-backend MIR dump; no debug timing is treated
as a performance result. Runtime timing comes only from release builds.

There are two distinct comparisons:

1. **Implementation generations** compare the exact historical dumps with the
   post-implementation dump. This answers whether the original MIR blow-up was
   removed, but it also spans intervening compiler changes.
2. **Current write-through versus write-back** compiles the same source and
   current compiler twice. This isolates the production publication policy
   from the retained correctness oracle.

The historical points are:

| Tier | Pre-change point | Reviewed side-stack point |
|---|---|---|
| Lambda MIR-Direct | `5717d36f`, before the core side-stack frame; it already used heap-root set/get helper calls | `9402b169`, direct side-root loads/stores and scalar donation |
| LambdaJS | `5043b956`, before per-function side-root frames and still dependent on conservative native-stack discovery | `9402b169`, broad always-current shadow publication |

### 12.2 Lambda MIR-Direct result

| `_frame_review_0` metric | Pre-side-stack | Reviewed side-stack | Post-implementation write-back | Side-stack → post change |
|---|---:|---:|---:|---:|
| Executable MIR instructions | 90 | 138 | **101** | **−37 (−26.8%)** |
| MIR locals | 60 | 80 | **47** | **−33 (−41.3%)** |
| MIR calls | 45 | 10 | **11** | +1 |
| Root slots | 16 | 16 | **12** | **−4 (−25.0%)** |

The latest function is 11 instructions, or 12.2%, above the 90-instruction
pre-side-stack implementation. That baseline was not root-free: 36 of its 45
calls were heap-root frame management calls. The post-implementation function
keeps precise direct side-root publication while remaining much closer to that
baseline and avoiding those helper calls.

### 12.3 LambdaJS result

| `_js_frameReview_149` metric | Pre-stack | Broad side-stack | Post-implementation write-back | Broad → post change |
|---|---:|---:|---:|---:|
| Executable MIR instructions | 58 | 440 | **136** | **−304 (−69.1%)** |
| MIR locals | 29 | 214 | **53** | **−161 (−75.2%)** |
| MIR calls | 24 | 26 | **26** | unchanged |
| Per-function side-root slots | 0 | 28 | **7** | **−21 (−75.0%)** |
| Runtime root publication | none | 161 copy/store pairs = 322 instructions | **11 direct stores** | broad publication removed |

The original 7.59x JS expansion is therefore no longer the current design.
The production function is 30.9% of the broad side-stack instruction count,
and the measured 136 instructions closely match the design estimate of about
130. It remains 78 instructions above the 58-instruction pre-stack body, or
2.34x that body. The residual includes checked side-stack binding/reservation,
slot initialization, precise stores at safepoints, unified cleanup, boxed
scalar return classification/donation, watermark restoration, and a cold
overflow path.

### 12.4 Same-compiler publication-policy isolation

| Current `638e11c93` probe | Write-through oracle | Production write-back | Change |
|---|---:|---:|---:|
| Lambda executable instructions | 141 | **101** | **−40 (−28.4%)** |
| Lambda locals | 65 | **47** | −18 |
| Lambda root slots | 34 | **12** | **−22 (−64.7%)** |
| JS executable instructions | 150 | **136** | **−14 (−9.3%)** |
| JS locals | 54 | **53** | −1 |
| JS root slots | 15 | **7** | **−8 (−53.3%)** |
| JS root stores | 31 | **11** | **−20 (−64.5%)** |
| JS classified calls (`MAY_GC` / `NO_GC`) | 13 / 12 | **13 / 12** | unchanged |

The current write-through oracle is not the historical 440-instruction
compiler. It shares semantic candidates, canonical homes, import effects, and
the consolidated emitter with production; it changes publication policy so
that correctness can be compared differentially. The much larger historical
440 → 136 reduction also includes deletion of production full-scope
publication, post-instruction binding scanning, and two-instruction shadow
copy/store pairs.

### 12.5 Conclusion

Per-frame MIR count is not a constant: it depends on control flow, values live
across `MAY_GC` calls, representation classes, scratch overlap, and return
mode. For the stable review probes, however, the result is unambiguous:

- **Lambda:** reduced from 138 to 101 relative to the reviewed side-stack
  implementation; still 12.2% above its helper-root pre-side-stack count.
- **LambdaJS:** reduced from 440 to 136; the original blow-up is largely
  removed, although the exact-frame/scalar machinery remains above the
  conservative pre-stack body.
- **Publication policy:** the same-compiler oracle comparison reduces root
  slots by 64.7% for Lambda and root stores by 64.5% for JS.

The latest release Test262 gate completes all 42,889 tests in 160.6s with
40,261/40,261 fully passing, zero non-fully-passing tests, zero failures, zero
regressions, and zero retry time. This is current acceptance evidence, not a
source-isolated attribution against the historical 195.7s/349.4s measurements:
the intervening repository and harness changes make that runtime comparison
non-causal. The MIR comparison above is the structural evidence attributable
to the implemented frame/rooting generations.

## 13. Explicitly unsafe actions

Do not, at any stage:

- comment out `gc_scan_stack()` and rely on normal tests (B11);
- assume all MIR locals are spilled to the native stack, or that `setjmp`
  captures every caller-saved live value;
- assume a caller roots every native helper argument unless that caller is an
  admitted precise tier emitted through the §4.4/RH2 call path;
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

## 14. Risks

- **The §7 audit was the schedule risk.** It is now closed by exact native
  homes plus the `gc-root-hazards` ratchet. New native helpers remain subject
  to RH1–RH8 and the static check.
- **A missed hazard after the flip is a use-after-free.** Mitigations: the
  three §7.4 layers, poisoning + forced-GC in CI permanently, the shadow
  collector mode retained in debug builds, and fail-stop admission when a tier
  requires compatibility in a scanner-free build.
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

## 15. Decision ledger

| ID | Decision | Status |
|---|---|---|
| CR1 | Safepoint-current canonical slots = generated-code rooting scheme | **implemented** |
| CR2 | C/C++ helpers use always-current slot-backed handles on the same stack | **implemented** |
| CR3 | Stable homes + liveness/dirty write-back adopted; broad shadow publication rejected; stack maps KIV | **implemented** |
| CR4 | Safepoint contract frozen: GC begins only inside `MAY_GC` calls; async collection prohibited | **implemented** |
| CR5 | Root analysis/store insertion implemented once in `MirEmitter`; transpilers report semantic events | **implemented** |
| CR6 | A guest language is precise iff it emits through the shared primitives | **accepted; Lambda/JS implemented** |
| CR7 | Leave C2MIR code unchanged; it permanently forces compatibility mode and is unavailable in precise contexts/scanner-free builds | **accepted (CQ2)** |
| CR8 | Input/format allocations remain pool/arena-owned and outside GC rooting; any future GC-heap boundary requires an explicit audit | **accepted (CQ4)** |

## 16. Clarifications, resolved choices, and non-blocking KIV items

- **CQ1 — RESOLVED: Interpreter/evaluator locals.** `lambda-eval.cpp` and the
  procedural runtime are native helpers for RH1–RH8. Their live GC values use
  exact frame homes; state surviving an activation uses traced/persistent
  ownership.
- **CQ2 — RESOLVED: C2MIR compatibility tier** (CR7). Leave all C2MIR code
  unchanged. C2MIR always uses conservative compatibility mode and cannot run
  in a precise-only context or scanner-free build.
- **CQ3 — RESOLVED: Generator/async resume homes.** Suspended state is owned by
  traced activation objects (`gen_env`, LambdaAsyncFrame per SF20), and exact
  homes are re-established on resume. Promise/timer and generator probes run
  under precise-only collection before every allocation.
- **CQ4 — RESOLVED: Input/format ownership** (CR8). Input and format paths are
  designed to allocate through their own pools/arenas rather than the GC heap,
  so they are outside the GC-root audit. Introducing a GC allocation or a
  retained GC-owned value there is an ownership-model change and requires an
  explicit boundary audit.
- **CQ5 — RESOLVED: Radiant re-entry.** C++ frames between Radiant and JS obey
  the same RH rules and are included in the native hazard scan. The precise
  root re-entry probes pass; current layout/view golden failures belong to the
  separate DOM/view refactor.
- **CQ6 — Slot metadata.** Whether side-root slot words ever need a tag
  distinguishing boxed Item vs raw pointer (not needed for non-moving
  marking; would matter for a moving collector).
- **CQ7 — `Rooted` ergonomics for hot helpers.** Whether write-through
  register caching is worth mirroring in C++ (`Rooted::get()` is a load;
  helpers in tight loops may want a cached raw copy between safepoints —
  legal under CR4, needs a documented idiom rather than ad-hoc use).
- **CQ8 — RESOLVED: Root-analysis placement.** `MirEmitter` owns compact
  semantic candidates and call-site metadata, then performs CFG liveness,
  dirty propagation, scratch coloring, and final stores after lowering.
  Per-language emitters report semantics without owning rooting policy.
- **CQ9 — RESOLVED: Scratch coloring threshold.** Production colors scratch
  homes immediately; write-through/unique-home behavior remains the
  correctness oracle and the fail-closed fallback after analysis allocation
  failure.

## 17. Source map

| Concern | Source |
|---|---|
| GC driver, precise/compatibility admission, stack bounds | `lambda/lambda-mem.cpp` (`heap_gc_collect`) |
| Root marking, forced schedules, debug compatibility/shadow scan | `lib/gc/gc_heap.c` |
| Collector root API | `lib/gc/gc_heap.h` |
| Side-stack reservation and watermarks | `lib/side_stack.c`, `lib/side_stack.h` |
| Runtime `Context` side-stack fields | `lambda/lambda.h` |
| C/C++ exact helper frames and handles | `lambda/lambda.hpp`, `lambda/lambda-mem.cpp` |
| Shared emitter primitives | `lambda/mir_emitter_shared.hpp` |
| Lambda MIR root emission | `lambda/transpile-mir.cpp` |
| JS MIR root emission | `lambda/js/js_mir_hashmap_scope_utils.cpp`, `lambda/js/js_mir_calls_boxing_types.cpp` |
| C2MIR generation | `lambda/transpile.cpp` |
| Persistent root registration | `lambda/lambda-mem.cpp`, language runtime state files |
| Async task roots | `lambda/concurrency.cpp`, JS event-loop/runtime files |
| Jube host API | `lambda/jube/jube_registry.cpp` + public Jube API |
| Guest heap/context ownership handoff | `lambda/lambda-mem.cpp`, guest `transpile_*_mir.cpp` entrypoints |
| Transitive `NO_GC` enforcement | `utils/check_gc_effects.py` |
| Native local/range-root enforcement | `utils/check_gc_root_hazards.py` |
| Lifetime architecture | `vibe/Lambda_Design_Stack_Frame.md` (SF1–SF20) |
| Historical regression evidence | `vibe/Lambda_Stack_JS_MIR.md`, `vibe/Lambda_Stack_MIR.md` |
| Post-implementation MIR profile | §12 and its captured `temp/*mir_latest*` artifacts |
