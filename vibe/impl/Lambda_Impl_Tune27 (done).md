# Tune27: stop re-proving typed contracts per value

- **Date:** 2026-09-13 (rev 2, same day: Result43 read folded in, scope
  extended by M5–M8 and T27-6..9; rev 3, same day: first implementation
  slices, §10; rev 4, 2026-09-14: round 2, §10.11; rev 5, same day: round 3, §10.12; rev 6, same day: round 4, §10.13 -- typed `var` rebinds on every tier, T27-0 census refresh; rev 7, same day: round 5, §10.14 -- contract proofs reused, static array lengths; rev 8, same day: round 6, §10.15 -- the null lane as an immediate, the nullable store fixed, JIT-tier parity census; rev 9, same day: round 7, §10.16 -- literal loop extents and nested counters in the dense proof; rev 10, same day: T27-5 case study §10.17, G9 probes in tree, completion §11).
- **Status:** **COMPLETE 2026-09-14 (rev 10) — thirty-one slices landed over seven rounds; §11 records the gates met, the gates not met, and the deferred items.** §2, §2.1
  and §3 are measured; §4 states tracks and exit evidence; §10 records what
  landed, what it measured, and one correction to §3 (M1).
- **User requirement:** the slow typed rows must stop paying a per-value
  contract re-proof. A declared annotation should establish its proof once at
  the boundary that reifies it, and hot code should consume that proof.
- **Predecessors:** [Tune22](<Lambda_Impl_Tune22 (done).md>),
  [Tune25](<Lambda_Impl_Tune25 (done).md>),
  [Tune26](<Lambda_Impl_Tune26 (done).md>);
  [Result43](../../test/benchmark/Overall_Result43.md) is the first report
  that carries Tune26. Tune26 attacked the *array* admission scan; Tune27
  attacks what remains on the mandatory per-iteration path once that scan is
  gone, plus the tier/compile costs that Part 1 of the reports cannot see.
- **Authority:** [formal design](../../doc/Lambda_Formal_Design.md) **D3.2.1**
  (three distinct subtype operations), **D3.2.2** (the validator is the runtime
  enforcer, deep, **on first crossing**), **D3.2.4v3** (a named map crossing is
  a reification; elide only on a proven physical layout, reached through the
  non-null arm), **D3.3.4** (representation follows the full inferred
  contract), **D4.4.2/D4.4.4** (one-level COW, RMW place borrow),
  **D5.3.4** (precise roots), **D8.2.6**, **D8.4.3v2**, **D8.3.2** (the
  check lives in the callee: one guard site, one correctness argument),
  **D8.3.3** (the unboxed entry has exactly two proof-producing paths),
  **D8.1.1v9** (tiered execution: T0 with per-function satellite promotion is
  the shipped default), **D8.6.1** (MT7 0%-slack emission ratchet),
  **D2.2.2** (the native `int` lane is i64 with sentinels at the extremes);
  [formal semantics](../../doc/Lambda_Formal_Semantics.md) **S11.4.1v3** (an
  implementation may reuse an admitted representation proof only while the
  actual carrier matches it), **S7.1.3v2** (checked writes), **S9.1.2**,
  **S4.1.2** (`int` arithmetic is closed and total; out-of-band saturates).
  Tune27 proposes **no change** to these rulings. Its whole claim is that
  D3.2.2's "on first crossing" and S11.4.1v3's proof-reuse licence are not
  being taken in the emitted code.

## 1. Objective and completion contract

Three obligations, each independently reportable:

1. **Mechanism removal.** For each row in §7, the named mechanism disappears
   from the mandatory per-iteration path (§6 census) — not merely gets faster.
2. **Semantic identity.** Every boundary that stops running still rejects
   everything it rejected before, with the same error, boundary name and
   validator path. A contract elided on a layout that was never reified is a
   memory-safety defect under D3.2.4v3, not a missed optimization.
3. **No corpus rewriting.** No canonical benchmark source is edited to make the
   compiler look faster. Annotation-parity pairs, if added, live beside the
   canonical row and are reported separately.

A track may land while the round stays open. Correctness gates alone cannot
close Tune27.

## 2. Measured starting point

Measured 2026-09-12 against the archived Result42 release
`test/benchmark/exe/lambda-v42-584748bc54`, `LAMBDA_TIER=jit`. Two independent
instruments (§6): a dominator-exact static census of the emitted MIR, and
macOS `sample` self-time under the `run_script_mir` subtree.

| Row | typed ms | T/Node | T/C2MIR | Dominant non-native mechanism |
|---|---:|---:|---:|---|
| prettier_ast | 2517 | 25.3x | 60.5x | validator 18.7% dyn; `fn_member_by_id` x15 mandatory |
| splay | 388 | 20.2x | 20.7x | map-contract validation 19.7% dyn; GC 23.3% |
| navier_stokes | 265 | 18.8x | 5.7x | `lambda_array_set_checked_inplace_item` x9 + `_lane` x5 + `cow_prepare_write` x7 mandatory; contract 17.6% dyn |
| richards | 724 | 15.5x | 24.9x | contract 11.2% dyn (`lambda_type_matches` 4.9%); `array_push`/`fn_eq`/`item_at` mandatory |
| cd | 548 | 15.2x | 36.2x | generic Item ops 10.1% dyn; `array_push_capture` x8, `lambda_type_check` x5 mandatory |
| hyphen | 74 | 10.5x | 49.9x | `fn_len` x7, `is_truthy`, `it2s`, `map_with_tl` mandatory |
| brainfuck | 334 | 9.9x | 12.0x | `fn_fill` 24.7% dyn, `fn_ord` 6.6%, `fn_string_ascii_at` 6.2%, boundary admit 8.4% |
| fast_diff | 277 | 7.0x | 21.1x | `fn_string_ascii_at` 11.4% dyn, `lambda_numeric_boundary_admit` 10.3% |
| three_way_merge | 6250 | 6.5x | 2.9x | contract 13.3% dyn; `memcmp` 12.6% (string-bound) |
| log_pipeline | 6375 | 6.7x | 10.2x | `memcmp` 18.1% + `fn_split` 5.2% + GC 11.7% (string-bound) |
| deltablue | 48 | 4.1x | 42.0x | `cow_prepare_write` x16 mandatory |
| hashmap | 70 | 4.6x | 24.7x | `lambda_type_check` x3 + COW x4 mandatory |
| cube3d | 29 | 1.7x | 55.9x | `lambda_type_check` x19 + `array_push_capture` x12 mandatory |
| microdiff | 57 | 3.5x | 21.4x | `fn_len` x7, `fn_join` x4, `array`/`array_end` pairs |

**Out of scope — already native.** `triangl`, `primes`, `matmul` and the
`richards` entry module have **call-free mandatory loop paths**; `text_search`
spends 98.4% of self time in JIT code and `triangl` 100%. Their remaining gap
to C2MIR is codegen and algorithmic, not representation. Do not spend Tune27
effort there, and do not cite them as boxing evidence.

**Correction to an earlier reading.** A first pass counted `item_at` + `it2d`
in `nbody`/`navier_stokes` as generic element reads "about 50/50 with native
loads". That was wrong: the dominator census shows those `item_at` sites are
the *guarded slow arms* of `emit_tracked_generic_float_array_load`
([transpile-mir.cpp:20636](../../lambda/runtime/transpile-mir.cpp:20636)), whose
fast arm is a direct `dmov %r, d:(%r)`. Typed float **reads** are already
native. The boxing in those rows is on the **store** side. §6 records the trap
that produced the error.

### 2.1 Result43 read (commit `fabc412146`, 2026-09-13)

Result43 is the first report carrying Tune26. Headline, 63 rows, JIT-pinned:

| Population | Typed/Node | Typed/C2MIR | Typed R43/R42 | Untyped R43/R42 |
|---|---:|---:|---:|---:|
| 63 rows | 0.88x | 4.64x | 0.777x | 0.967x |

The wins are the Tune26 rows and they are real: prettier_ast 2518 → 1007 ms,
primes 23.7 → 2.49, matmul 43.2 → 17.7, navier_stokes 265 → 123, cube3d
29.2 → 14.4, diviter 532 → 266, three_way_merge 6250 → 3714, hashmap
69.6 → 40.9, brainfuck 334 → 193, fast_diff 277 → 141. The §2 table is
therefore stale for those rows; T27-0 re-measures it. The §2 *mechanism*
attribution stands — none of those tracks removed a mechanism from the
mandatory path, they made the array scan cheaper.

Three findings were **verified by replaying the archived binaries**
`test/benchmark/exe/lambda-v42-584748bc54` and `lambda-v43-fabc412146`
(`LAMBDA_TIER=jit`, 3 runs each) rather than read off the report:

1. **The untyped micro "regressions" are host noise.** The report shows
   fibfp, ack, nqueens, fft, mbrot, permute, queens, towers, bounce, list,
   storage and puzzle 1.3–1.5x slower *untyped* with no source change. On a
   quiet host v43 fibfp runs 1.889 ms against the report's 2.92 and ack
   10.4–12.9 ms against 18.5 — v43 is as fast as or faster than v42. The
   untyped column was captured under load (the fibfp cell's own range,
   2.89–4.84, says so). **Do not bisect these rows.** §6 gains a trap for it.
2. **`larceny/puzzle` typed 2.83 → 16.1 ms is a correctness fix, not a
   regression.** It reproduces at 16–21 ms on the archived v43 binary with
   identical source. The MIR dumps attribute it exactly: v42 emitted six
   in-place `lambda_array_set_checked_lane` stores and three `item_at`; v43
   emits twelve `array_num_set_cow_idx` stores plus three `cow_mark_shared`.
   The mark is the CW29 activation snapshot on the native witness edge
   ([transpile-mir.cpp:31848](../../lambda/runtime/transpile-mir.cpp:31848),
   `cow_param_mutated && !is_var_param`). **v42 omitted it and was wrong**:
   the probe `pn touch(a: bool[], n: int) { a[0] = true; … }` called on a
   caller's `var x: bool[]` prints `x0=true` under v42 `LAMBDA_TIER=jit` and
   `x0=false` under v42 T0 and under both v43 tiers — a tier divergence
   against S9.1.3 (a plain parameter's writes are local to the procedure).
   `puzzle2.ls` writes through plain parameters and restores the value after
   each recursive call, so every activation's first store must detach a
   private copy unless the compiler can prove the caller never reads the
   array again — and in `solve` it does, on the next loop iteration. The
   untyped row pays the same copy (18.9 ms). rev 2 of this document called
   the mark unnecessary; that reading is withdrawn. `puzzle` is a
   **regression guard only**, not a target, and it is removed from G7.
3. **Typed is slower than untyped on three rows**: richards 722 vs 434 ms
   (1.66x), splay 388 vs 312 (1.24x), mbrot 1.37 vs 0.97 (1.41x). These are
   annotation penalties on identical workloads, not workload differences.
   Tune26 §9 already flagged splay's contract-supporting temporaries; the
   richards inversion (handle-store kernel, every accessor takes `var w:
   World` — M2 ×20 functions) is new and is the second T27-4 pilot.

**Part 2 (auto tier, wall clock) hides two different problems** that the
JIT-pinned column cannot show. Both were verified on the v43 binary:

| Row | auto wall | JIT-pinned exec | Verified cause |
|---|---:|---:|---|
| jetstream/crypto_sha1 typed | 730 ms | 27 ms | **not compile** — the log shows five satellite images totalling 9 ms; `core_sha1` never appears because its `match t_result { case error: 0 case int: t_result }` hits the satellite scanner's `AST_NODE_MATCH_EXPR` reject ([interp_plan.cpp:1956](../../lambda/runtime/interp_plan.cpp:1956)), so the hot function runs in T0. Untyped crypto_sha1 auto = 96 ms |
| jetstream/cube3d typed | 357 ms | 14.4 ms | **compile** — one 14-member image, `compile_ms=322.4`; `run_cube` alone emits **36,161 MIR instructions** (46,748 in the module): 390 zero-arg `lambda_float_null_lane_c` calls for a *constant*, 319 `push_d`, 219 `array_push_capture`, 211 `lambda_type_check`, 105 `array`/`array_end` pairs. JIT-pinned wall is 384 ms — the same compile, so this is emission size, not tier policy |
| awfy/richards typed | 786 ms | 722 ms | compile 34 ms for a 20-member image; the rest is execution (M2) |
| awfy/deltablue typed | 198 ms | 49 ms | ~150 ms compile of a 47-member image (the re-link cost already noted at [transpile-mir.cpp:34935](../../lambda/runtime/transpile-mir.cpp:34935)) |

So in the tier a user actually gets, a typed crypto_sha1 is **7.6x slower
than the untyped one** purely because of satellite admission, and a typed
cube3d spends 96% of its wall time in MIR gen. Neither is visible to the
Part-1 census this proposal was built on. They become M5 and M6.

**Rows the frozen set under-represents.** The 63-row geomean is dominated by
sub-millisecond rows the fourteen §2 rows do not cover: nqueens 14.5x, json
11.9x, towers 11.2x, queens 10.6x, list 9.6x, storage 3.9x C2MIR — all
array-and-recursion micro rows. And the two call-free scalar rows sit at a
floor Tune27 rev 1 declared out of scope: collatz 324/225 ms (1.44x) and
mandelbrot 42.7/30.8 (1.39x). Both groups enter §7.

## 3. Mechanisms

### M1 — The typed boundary runs the schema validator on the success path

[`lambda_type_matches`](../../lambda/runtime/lambda-eval.cpp:1611) routes union
contracts (`TYPE_KIND_BINARY`/`UNARY`) and shaped map/element contracts into
`runtime_validate_value_against_type`, and it is called from the *accepting*
path of [`runtime_type_admit_value`](../../lambda/runtime/lambda-eval.cpp:10606),
below the fast proofs and above the union decomposition. So
`SchemaValidator::validate_type` → `validate_against_type` →
`validate_binary_type` → `validate_against_union_type` runs **per admitted
value**, not only to build a diagnostic for a rejection.

Confirmed by stack, not inferred: the `sample` call graph for `prettier_ast`
shows `lambda_type_check` + 60 → (inlined) → `SchemaValidator::validate_type` →
`validate_against_type` → `validate_binary_type` → `validate_against_union_type`.
Self-time share: prettier_ast 18.7%, splay 19.7%, three_way_merge 13.3%,
richards 11.2%, log_pipeline 8.8%.

`prettier_ast` is the clean case. Its contract is a six-arm union of map
shapes:

```
type Doc = {kind: string} | {kind: string, value: string} |
    {kind: string, parts: Doc[]} | {kind: string, contents: Doc} |
    {kind: string, contents: Doc, break_threshold: int} |
    {kind: string, broken: Doc, flat: Doc}
```

`COW_EXEC_PROFILE=1` reports **6,615,043 `union_admit_calls`** for one run.
There is already a memo for exactly this shape —
[`runtime_union_map_rep_proves_cached`](../../lambda/runtime/lambda-eval.cpp:10573),
an 8-way probe keyed on `(TypeMap* candidate, Type* expected)` in
`heap->map_contract_cache`. Two structural limits are visible in the source and
are the starting hypotheses for T27-1, **not yet measured**:

- it is entered only for `value_type == LMD_TYPE_MAP` with a plausible
  `TypeMap*` — elements, and any arm reached with another tag, never consult it;
- **a disproof is never memoized** (`if (!proven) return false;` before the
  insert), so a candidate the fast relation cannot prove re-runs
  `runtime_union_map_rep_proves` and then the full validator on every crossing.

The `union_map_rep_cache_hits`/`_misses` counters exist in the working-tree
source but are **not emitted by the v42 binary**, so the archived run cannot say
which limit fires. Re-measuring those two counters on a current release is the
first deliverable of T27-0.

Authority reading: D3.2.2 already says the validator enforces *deep, on first
crossing*. Per-value re-validation of an already-proven carrier is an
implementation gap against that ruling, and S11.4.1v3 grants the proof-reuse
licence explicitly. D3.2.4v3 fixes the price of getting it wrong and supplies
the elision test — a *proven physical layout*, reached **through the non-null
arm** of the spelling. That last clause is load-bearing: `N?` and `N | null`
are wrappers whose own `type_id` is `LMD_TYPE_TYPE`, so testing the wrapper
silently exempts recursive contracts — and `Doc` is recursive, so it exempts
prettier_ast itself.

### M2 — COW write preparation on the mandatory path

`cow_prepare_write` is mandatory per iteration in deltablue (x16),
navier_stokes (x7), nbody (x7), hashmap (x2), with `cow_bind_var`,
`cow_path_borrow_fixed` and `pn_push_cow` beside it in levenshtein,
text_search, three_way_merge and log_pipeline. D4.4.4 (CW34) bound the
read-modify-write place borrow, and the RMW census recorded that
`cd`/`splay`/jetstream-`deltablue` are **not covered** by it. These rows are
that residue. This is not boxing — it is a per-iteration ownership test and
capture that a stable `var` parameter should establish once.

Result43 adds one data point (§2.1); `puzzle`'s share-mark is the required
S9.1.3 snapshot, not an instance of M2. `richards2_core` shows
the cost when every accessor is `pn f(var w: World, …)`: the wrapper prologue
calls `cow_prepare_write` unconditionally whenever a home was transported
([transpile-mir.cpp:30749](../../lambda/runtime/transpile-mir.cpp:30749)),
guarded only by `home != 0`, never by an ownership fact, and the per-frame
facts are monotonically pessimistic — after a detach `cow_marked = false;
cow_children_may_be_shared = true` ([:2418](../../lambda/runtime/transpile-mir.cpp:2418))
and nothing ever clears it. There is no "exclusive for the rest of this
frame" fact at all; the nearest are `MirVarEntry::cow_owned` and
`typed_array_contract_proven`. Tune26 §9's rejected experiment (a `let
snapshot = values` capture observing a raw store) fixes what the fact must
be: exclusivity is a **capture/escape** result per frame, not a boundary
property.

### M3 — Typed array stores still cross an Item

navier_stokes' `pn add_fields(var x: float[], s: float[], dt: float)` shape
stores through
`lambda_array_set_checked_inplace_item` — the **Item-taking** setter — nine
times on the mandatory path, with `lambda_array_set_checked_inplace_lane` five
times. Tune25 gave sized lanes native stores; a `float[]` `var` parameter store
is still boxing the double and re-admitting it. Dynamic confirmation:
`lambda_array_contract_compatible` 6.6%, `lambda_array_contract_canonical`
5.3%, `array_num_store_admitted` 2.6%, `lambda_numeric_boundary_admit` 2.2%.

`lambda_numeric_boundary_admit` also carries fast_diff (10.3%) and brainfuck
(8.4%); its caller chain is `lambda_type_check` in both, so M3 and M1 share a
root.

### M4 — Residual generic Item operators

Genuinely boxed operators that survive on the mandatory path, in descending
order of measured weight: `fn_member_by_id` (prettier_ast x15, hyphen_core),
`fn_fill` (brainfuck, 24.7% of self time), `fn_len`/`fn_len_l` (hyphen_core x7,
microdiff x7), `fn_string_ascii_at` (brainfuck 6.2%, fast_diff 11.4%), `fn_ord`
(brainfuck 6.6%), `fn_join`/`it2s` (gcbench x5, json_gen x7, microdiff x4),
`fn_index`/`fn_eq`/`fn_ne`/`is_truthy` (cd, richards_core). `cd` is the only row
whose loop body is *predominantly* this — it is the M4 pilot.

Strings belong here too and none of M1–M3 touches them: on a typed `string`
only `==` against a literal or another string is inlined
([transpile-mir.cpp:11831](../../lambda/runtime/transpile-mir.cpp:11831),
[:11857](../../lambda/runtime/transpile-mir.cpp:11857)); `len` is a
type-specialised **call** (`fn_len_s`), `s[i]` is a call to
`fn_string_ascii_at` ([:21496](../../lambda/runtime/transpile-mir.cpp:21496)),
and `substring`/`split`/`join` are runtime calls with heap copies. hyphen
(49x C2MIR, typed == untyped at 73 ms) and log_pipeline (10x) will not move
under M1–M3.

### M5 — The auto tier refuses the constructs typed code writes

Every Part-1 number pins `LAMBDA_TIER=jit`. The shipped default is T0 with
satellite promotion (D8.1.1v9), and `interp_satellite_supported`
([interp_plan.cpp:2022](../../lambda/runtime/interp_plan.cpp:2022)) rejects,
among others: any `match` expression ([:1956](../../lambda/runtime/interp_plan.cpp:1956),
"pattern arms carry compiled regex/type-list state owned by the T0
activation"), any function that rebinds a typed `var` parameter and any
*caller* of one ([:1968](../../lambda/runtime/interp_plan.cpp:1968),
[:2032](../../lambda/runtime/interp_plan.cpp:2032), D8.1.1v8), nested
function definitions, and object-method field identifiers. `match … case
error:` is exactly how a typed script keeps an `int` local total under
S4.1.2, and `var x: int[]` rebinds are how it reuses a buffer — so the
scanner's rejects fall disproportionately on typed code. crypto_sha1 (§2.1)
is the measured case: the untyped script promotes its whole cluster on first
entry, the typed one leaves its hot function interpreted for 650 ms. A
promotion that is refused is silent: the only trace is the *absence* of an
`interp-tier: satellite image function='core_sha1'` log line.

### M6 — Emission size is now the dominant cost on the widest typed rows

cube3d2's `run_cube` is 36,161 MIR instructions for ~300 source lines. The
emitter expands every numeric op into a guarded fast arm plus a slow arm
(§6 trap 1 counts the arms correctly as cold), every `[x, y, z]` literal
into a heap-built array (`array`/`push_d`×3/`array_end`), every typed
boundary into a boxed call, and it materialises the float null-lane
sentinel — a constant — through a zero-argument **call**
(`emit_float_null_lane`, [transpile-mir.cpp:3851](../../lambda/runtime/transpile-mir.cpp:3851)),
390 times in one function. MIR's generator is super-linear in function size
at `optimize_level = 2`, so 322 ms of the 357 ms wall is codegen. The
satellite path has **no size policy at all**: `MIR_LARGE_MODULE_INSN_THRESHOLD`
is consulted only on the module path
([transpile-mir.cpp:35237](../../lambda/runtime/transpile-mir.cpp:35237)).
D8.6.1's MT7 ratchet gates *growth* on fixtures; nothing gates the absolute
size of a benchmark function, so this cost accrued unnoticed across Tune22–26.

### M7 — A typed direct call is admitted up to three times

D8.3.2 rules that the check lives in the callee, once. The emitter today
admits a declared parameter at the **caller** for the native-param edge
([transpile-mir.cpp:23844](../../lambda/runtime/transpile-mir.cpp:23844),
gated only by `mir_boundary_is_redundant`), again in the callee's public
`_b` wrapper ([:30801](../../lambda/runtime/transpile-mir.cpp:30801)), and for
`T[]` parameters a third time on the raw body's own entry arms
([:31791](../../lambda/runtime/transpile-mir.cpp:31791) witness-miss arm,
[:31910](../../lambda/runtime/transpile-mir.cpp:31910) full `ARRAY_NUM`
admission). The callee-side checks are not suppressed when the caller
proved the contract, which is the "third path" D8.3.3 forbids. Separately,
the only elision gate, `mir_boundary_is_redundant`
([:5245](../../lambda/runtime/transpile-mir.cpp:5245)), is keyed on **`Type*`
identity**, so two structurally identical contracts spelled at two sites
never elide — and every map/record parameter is boxed regardless, because
`is_native_param_type_id` admits scalars only
([:3075](../../lambda/runtime/transpile-mir.cpp:3075)). Even a proven direct
field read keeps its null branch: `skip_null_guard = false` is hardcoded at
[:19597](../../lambda/runtime/transpile-mir.cpp:19597) ("typed variables can
still hold null") for fields whose declared type is *not* optional.

### M8 — The pure-scalar floor is ~1.4x and it caps every other track

collatz and mandelbrot have call-free loops (§2 "already native") and still
run at 1.44x and 1.39x C2MIR. The collatz2 dump shows why: every `+`
emits `ge/le/and/bf` against the int53 band plus a `lambda_int_lane_add_slow`
arm, `*` emits `mulo/bo` plus the band test, and one `%` emits **nine**
range tests before `mod` (both operands in band, divisor non-zero, result in
band, then the result re-tested before a float compare). S4.1.2 requires the
saturating semantics, not the per-op test: an induction variable bounded by
a loop condition, or an operand proven in `[0, 2^31)`, cannot leave the band
under `+ 1`. T26-3 added a deliberately narrow interval for one descending
sum; nothing general exists. This floor bounds what M1–M7 can deliver on
every numeric row, so it is in scope.

## 4. Implementation tracks

### T27-0 — Re-establish the census on a current release

**Deliverable:** the §2 table re-measured on a fresh `make release` of a named
commit, plus the two union-cache counters.

- Rebuild and archive a release with commit, dirty-patch hash, build config and
  host state. The §2 numbers are from `lambda-v42-584748bc54`; HEAD carries
  Tune26 and const-fold commits since, and the current `lambda.exe` is a debug
  build (splay 9.6 s vs 420 ms) — never time against it.
- Report `union_admit_calls`, `union_map_rep_cache_hits`,
  `union_map_rep_cache_misses` and the `map_admit_*` family per row. Decide from
  the counters which M1 limit fires, and record it before writing any code.
- The static half of the census tooling is checked in as
  [`test/benchmark/mir_mandatory_census.py`](../../test/benchmark/mir_mandatory_census.py);
  add the `sample`-side driver and the `COW_EXEC_PROFILE` collector beside it so
  the whole table is reproducible rather than re-derived by hand each round.

**Exit evidence:** refreshed table; counter attribution for M1; tooling in tree.

### T27-1 — Memoize the contract proof, including disproof

**Problem:** M1. **Depends on:** T27-0's counter attribution.

1. Extend the `(candidate, expected)` memo to record the *relation*, not only
   the proven case — a disproven pair must answer "not proven" without
   re-running the relation or the validator. The existing entry already has a
   `relation` field; give it a disproven value and populate it.
2. Admit element-tagged and nominal values to the same memo where their
   identity key is as stable as `TypeMap*`. Audit what the key must cover: a
   shape that can *grow* (D2.6.6v2 open instances) invalidates a proof keyed on
   the old shape, so the key is the post-growth `TypeMap*` or the memo is wrong.
3. Reach the contract through the non-null arm (D3.2.4v3). `Doc` is recursive
   and therefore optional; a wrapper test exempts precisely this case.
4. Keep the validator on the rejection path unchanged: same error, boundary and
   validator path (D3.2.2, S11.4.1v3).

**Non-goal:** eliding the boundary itself. That is T27-2.

**Evidence:** per-row validator self time → 0 on accepted values; unchanged
rejection fixtures; `union_admit_calls` unchanged (the call still happens, the
*work* does not); both tiers use the same relation.

### T27-2 — Elide re-crossings of an identical contract

**Problem:** a value that just crossed contract `C` crosses `C` again at the
next declaration, argument or return. The emitter already has this elision for
call results with an explicit return contract
([transpile-mir.cpp:5293](../../lambda/runtime/transpile-mir.cpp:5293)); it is
keyed on `Type*` identity and an explicit contract.

- Extend it past the call-result case to values whose *carrier* provably still
  matches a proof established earlier in the same frame, under D3.2.4v3's
  physical-layout test. Semantic compatibility is never sufficient.
- The proof dies at any point the carrier can be replaced — a call that may
  reassign the binding, a COW replacement, a GC-visible publication. Reuse the
  existing dense-loop root facts rather than minting a parallel fact table
  (D2.4.1, D3.2.3: no ID-keyed per-node fact table).
- `cube3d` (x19 mandatory `lambda_type_check`) and `splay` (x6) are the pilots;
  both are "same contract in, same contract out" shapes.

**Evidence:** mandatory-path `lambda_type_check` count per row; a fixture where
the carrier *is* replaced mid-frame still re-checks.

### T27-3 — Native stores for `var T[]` parameters

**Problem:** M3.

- Give a `float[]`/`int[]` `var` parameter the same native store Tune25 gave
  sized lanes: prove the storage kind, representation certificate, bounds and
  exclusive ownership once at entry, then store the native double without an
  Item round trip. `lambda_array_set_checked_inplace_item` must not appear on a
  mandatory path where the element is a proven native double.
- The RHS/key evaluation order and post-call owner reload from Tune25 apply
  unchanged (D5.3.4): no interior pointer survives a call.
- Values needing admission, views, shared owners and failed bounds keep the
  checked setter (S7.1.3v2).

**Evidence:** navier_stokes mandatory `..._item` → 0, `..._lane` accounted;
`array_num_store_admitted` and `lambda_numeric_boundary_admit` self time.

### T27-4 — Extend the RMW place borrow to the uncovered rows

**Problem:** M2. `cd`, `splay` and deltablue are outside D4.4.4's shape;
`richards` (§2.1) is the pilot because its failing precondition is already
known. (`puzzle` was pilot 1 in rev 2; its share-mark is the required
S9.1.3 snapshot, see §2.1, and must stay.)

- **Pilot, `richards`:** twenty `pn f(var w: World, …)` accessors, each
  paying the unconditional wrapper `cow_prepare_write`. Replace the
  `home != 0` guard with the frame's exclusivity fact; establish that fact
  as a per-function capture/escape result (S9.2.2, D3.3.3v3), so the Tune26
  §9 snapshot reproducer stays guarded because *it* captures, and richards
  stops paying because it does not. Typed richards must not be slower than
  untyped richards when this lands.
- State, per row, *which* D4.4.4 precondition fails (root is not a `var` local
  or parameter; the value is observed between bind and store-back; a spine link
  is shared/static/immortal; the borrow is not dead afterwards). Fix the
  emitter only where the precondition genuinely holds and the current analysis
  fails to see it — **do not relax the ruling**. A precondition that genuinely
  does not hold is a design question for a new CW ledger entry, not a patch.
- The moved-argument case flagged open in the CW34 census stays open here
  unless the evidence closes it.

**Evidence:** `cow_prepare_write` mandatory count per row; `COW_EXEC_PROFILE`
share-mark and copy counts; forced-GC stress remains a gate (D4.4.3).

### T27-5 — `cd` as the M4 pilot

`cd` is 15.2x Node and 36.2x C2MIR with a loop body of `fn_index`, `fn_len`,
`fn_eq`/`fn_ne`, `is_truthy`, `item_at` and `array_push_capture` — the only row
where generic Item operators, not contracts or COW, are the body. Take it as
one case study and report what each operator needs (a proven carrier, a lane
witness, a dense-loop fact) rather than adding per-operator special cases.
Nothing here ships until T27-1..4 have moved their mechanisms, because the
`lambda_type_check` x5 in the same loops will otherwise mask the result.

`queens` (or `towers`) is the second M4 case study, standing for the
sub-millisecond array-and-recursion cluster that dominates the 63-row
geomean (§2.1). It is cheap to census and its loop is small enough to read
whole.

### T27-6 — Admit typed code to the auto tier

**Problem:** M5. **Independent of T27-1..5** and the first thing a user of
`lambda.exe run` hits, so it is not sequenced behind them.

1. Log every satellite *refusal* with the AST node kind that caused it (the
   same `interp-tier:` prefix as the promotions), so the crypto_sha1 case is
   a one-line diagnosis rather than an absence.
2. Lower `match` expressions in satellites. The pattern arms' regex/type-list
   state can be published into the image's immutable BSS beside the const
   pool and type list the image already carries; a `case error:` /
   `case int:` arm over a scalar needs no pattern state at all and should
   be admitted first.
3. Typed `var` parameter rebinds: give the satellite the CW33 home the
   module path already has (D8.1.1v8 records why it was withheld — the raw
   ABI had no home). If the home cannot be transported, promote the
   *caller* with the callee as a cluster member so the rebind stays inside
   one image, rather than pinning both to T0.
4. Do not touch the D8.1.1v9 thresholds; this track changes eligibility,
   not policy.

**Evidence:** crypto_sha1 typed auto wall within 1.2x of untyped auto wall;
every §7 typed row promotes its hot function (log line present); T0/JIT
golden parity on the new arms; forced-GC stress.

### T27-7 — Emission diet and a per-function size gate

**Problem:** M6.

- Constants as immediates: the float null lane is `dmov %r, <bits>`, not a
  call. Fixed-arity float vector literals (`[x, y, z]` of proven doubles)
  lower to one sized allocation plus three native stores, not
  `array`/`push_d`×3/`array_end`.
- Hoist the per-site certificate guard (`emit_array_rep_cert_guard`,
  nine emit sites) to the dense-loop root guard wherever the dense scan
  already proves the root; the code exists for `while` loops only.
- Add an **instructions-per-function** census column to
  `mir_mandatory_census.py` and a budget for the §7 rows beside
  `test/mir/mir_budgets.json`, so size regresses loudly (D8.6.1 gates
  fixtures; this gates benchmarks).
- Apply the module path's large-function policy on the satellite path too,
  and fix the per-promotion whole-context re-link
  ([transpile-mir.cpp:34921](../../lambda/runtime/transpile-mir.cpp:34921),
  [:34926](../../lambda/runtime/transpile-mir.cpp:34926)) so a 47-member
  image is generated once.

**Evidence:** cube3d2 `run_cube` under 10,000 MIR instructions with
identical output; cube3d typed auto wall under 100 ms; deltablue auto wall
within 1.3x of JIT-pinned exec + one compile.

### T27-8 — One admission per crossing, in the callee (D8.3.2)

**Problem:** M7. This is what T27-2 would otherwise chase value by value.

1. Retire the caller-side declared-parameter admission on the direct typed
   edge and the raw-entry re-admission behind the `_b` wrapper. The direct
   unboxed entry then has exactly the two proof-producing paths D8.3.3
   names. A site that cannot prove the contract calls `_b` — never a
   caller-side check plus the unboxed entry.
2. Key `mir_boundary_is_redundant` on structural contract equality
   (`lambda_boundary_is_redundant` over the unwrapped declared types), not
   `Type*` identity, so two spellings of `float[]` or of the same nominal
   record elide.
3. Drop the null guard on a direct field read when the field's declared
   type is not optional and the object has just crossed its contract
   (D3.2.4v3 makes the layout a proof; the non-null arm is that proof).
4. Map/record parameters as raw pointers to their packed layout on the
   direct typed edge — the same witness-bit protocol `T[]` uses — with the
   `_b` wrapper as the only boxed entry. This is the structural half of the
   "fully native lane" in §8 and depends on T27-1's proof-on-the-value.

**Evidence:** mandatory-path `lambda_type_check` per typed row halves
without T27-2; a fixture with a structurally-equal-but-distinct `Type*`
elides; a nullable field still guards.

### T27-9 — Interval analysis for the int53 band and bounds

**Problem:** M8.

- Generalise T26-3's interval into a per-loop range fact for induction
  variables and for operands proven within `±2^31` (array indices, lengths,
  loop counters, small literals). `+`, `-` and `*` of two such operands
  cannot leave the int53 band, so the band test and slow arm are omitted;
  the saturating semantics of S4.1.2 are preserved because the omitted arm
  is provably unreachable, not skipped.
- Reuse the same fact for bounds-check elimination outside dense-proven
  loops (`typed_array_inbounds_guard` is the only BCE today).
- Nothing changes for operands without a range fact.

**Evidence:** collatz and mandelbrot within 1.15x of C2MIR; collatz2's
`%` lowers to at most two tests; a fixture with an unbounded operand keeps
its slow arm.

## 5. Performance gates

Stated as acceptance targets on the T27-0 baseline, not forecasts.

| Gate | Rows | Target |
|---|---|---|
| G1 | prettier_ast, splay, three_way_merge, richards, log_pipeline | validator self time under 2% each |
| G2 | cube3d, splay | mandatory-path `lambda_type_check` at most 2 per loop |
| G3 | navier_stokes | zero mandatory `lambda_array_set_checked_inplace_item` |
| G4 | deltablue, navier_stokes, nbody | mandatory `cow_prepare_write` at most 2 per loop |
| G5 | whole corpus | no row regresses more than 3% against the T27-0 archive |
| G6 | whole corpus | `make test-lambda-baseline` 100%; forced-GC stress green |
| G7 | richards, splay, mbrot | typed JIT-pinned exec at most 1.0x untyped (annotation parity, same workload) |
| G8 | every §7 typed row | auto-tier wall at most JIT-pinned exec + 1.2x untyped-auto compile; hot function promoted (log line present) |
| G9 | cube3d, deltablue, prettier_ast | per-function MIR instruction budget recorded in tree; cube3d `run_cube` under 10,000 |
| G10 | collatz, mandelbrot | at most 1.15x C2MIR |

G5 and G6 are non-negotiable; a track that meets its own gate and breaks G5
does not land. G7 is the round's honesty gate: a typed row that is slower
than its untyped twin is a defect regardless of its Node ratio.

## 6. Measurement procedure

Two instruments, because neither alone is sound. **Both are needed: the static
census names the mechanism, the profile ranks it.**

**Static — dominator-exact mandatory-path census**
([`test/benchmark/mir_mandatory_census.py`](../../test/benchmark/mir_mandatory_census.py)).
Dump MIR per benchmark: `LAMBDA_TIER=jit LAMBDA_MIR_DUMP_PATH=<file>` — honored
in **release** builds ([mir_dump.h](../../lambda/runtime/mir_dump.h)). The tool
builds the CFG per function, computes dominators, finds natural loops by back
edge, and counts a call only when its block **dominates the latch** — i.e. it
runs on every iteration.

Three traps, each of which produced a wrong reading before the method settled:

1. **Guarded cold arms.** `int2it_lane`, `lambda_item_to_int_lane_c`,
   `lambda_int_lane_to_double_c` and `lambda_int_lane_*_slow` sit in the
   out-of-int53 arms of the native fast path. A raw call census reports them as
   hot boxing. They are not.
2. **Long slow arms.** The `item_at` + `it2d` fallback of the native float load
   reloads ~10 spilled registers before its call, so an "arm is short"
   heuristic misses it and reports native reads as generic. Only dominance
   answers this correctly.
3. **Per-module dumps.** The dump writes the whole MIR *context* and is
   rewritten per module, so a benchmark that `import`s a core (`richards2`,
   `hyphen2`) leaves only the entry module. Run the core `.ls` directly to dump
   it.
4. **A report column captured under load.** Result43's untyped column shows
   twelve rows 1.3–1.5x slower with no source change; none reproduce on a
   quiet host (§2.1). Before calling any cross-report delta a regression,
   replay both archived binaries alternately on the row and read the sample
   range in the cell. A tight range on one side and a wide one on the other
   is the tell.
5. **Wall time is two costs.** For the auto tier, read `log.txt` for
   `interp-tier: satellite image function=… members=… compile_ms=…`: the
   sum is compile, the remainder is execution, and a hot function whose
   name never appears is interpreted. `LAMBDA_TIER=jit` wall minus Part-1
   exec gives the module-path compile for the same script. Do not attribute
   an auto-tier gap to either cost without this split — crypto_sha1 and
   cube3d (§2.1) have the same symptom and opposite causes.

**Emission size — per-function instruction count** from the same dump:
`awk '/^_[A-Za-z0-9_]+:\tfunc/{n=$1} /^\t[a-z]/{c[n]++} END{for(k in c) print c[k], k}'`
sorted descending. This is the M6 instrument and the G9 gate input.

**Dynamic — `sample <binary> <secs> 1 -wait -f out`,** self time aggregated
under the `run_script_mir` subtree; JIT frames appear as `???`, so named frames
are exactly the non-native lane. Unreliable below ~100 ms of workload — the
attach races the process — so hashmap, nbody, crypto_sha1, matmul and primes
rest on the static census only. `COW_EXEC_PROFILE=1` with
`COW_EXEC_PROFILE_OUT=<file>` supplies the admission and COW counters; verify
the binary actually emits the counter you intend to read before quoting it.

## 7. Frozen row set

The fourteen rows of §2, plus the four out-of-scope native rows carried as
regression guards (`triangl`, `text_search`, `primes`, `matmul`), plus the
Result43 additions: `puzzle` (S9.1.3 snapshot guard: must keep its copy),
`richards` (M2 pilot, G7), `crypto_sha1`
(M5, G8), `queens` and `towers` (micro cluster, T27-5), `collatz` and
`mandelbrot` (M8, G10), `mbrot` and `splay` (G7). Each entry freezes: suite,
benchmark, typed source path and hash, transitive imports, input files,
expected output, and the T27-0 baseline timing, census, per-function
instruction count and auto-tier compile split.

## 8. Toward a fully native typed lane

The 4.64x is not one mechanism; it is the distance between what the emitter
produces for typed code and what `c2m` produces for the C port. Today a
typed function gets a native raw body only for **scalar** parameters and
return; every container, every map, every string and every boundary still
crosses `Item`. Closing the gap decomposes into eight obligations. They are
listed in dependency order with the track that owns each, so the round can
be read as a path rather than a list of fixes.

| # | Obligation | What exists today | Owner |
|---|---|---|---|
| 1 | **Typed containers are native ABI values.** `T[]` is a raw pointer with its certificate; a declared record (`type P = {x: float, y: float}`) is a raw pointer to its packed layout with `dmov d:(%r)` field access and no null branch on non-optional fields | `T[]` raw only behind a witness bit; records always boxed ([:3075](../../lambda/runtime/transpile-mir.cpp:3075)); direct field read exists but keeps the null guard | T27-8 (3, 4), T27-3 |
| 2 | **One boundary per crossing, in the callee.** A typed→typed direct edge carries no check; an `any`→typed edge is admitted once in `_b` | Caller + wrapper + raw entry (M7); identity-keyed elision | T27-8 (1, 2) |
| 3 | **The proof lives on the value.** A literal or constructor built under a declared nominal type carries the trusted-contract flag from construction, so admission of a recursive `Doc` is a pointer compare and the validator never runs on accept | Flag set only for compiler-recognised shapes; validator on the accept path (M1); disproof not memoized | T27-1 |
| 4 | **Ownership is a static fact.** A `var` parameter or local that no callee retains and no binding captures is exclusive for the frame; no `cow_prepare_write`, no share-mark, raw stores | Unconditional prepare on transported homes; no frame-level exclusivity fact (M2). A mutated *plain* parameter's snapshot is required by S9.1.3 and stays | T27-4 |
| 5 | **Arithmetic is C arithmetic where the range is proven.** Band tests and slow arms exist only where an operand's range is unknown | Every op carries the int53 test (M8) | T27-9 |
| 6 | **Bounds and certificates are checked per loop, not per access.** | Dense-proven `while` loops only | T27-7, T27-9 |
| 7 | **The emitted function is the size of the C function.** Constants are immediates, small vectors are stack or sized allocations, guards are hoisted; size is gated per function | 36k instructions for `run_cube` (M6) | T27-7 |
| 8 | **The default tier runs typed code natively.** Every construct the typed corpus uses is satellite-eligible, and promotion is logged either way | `match` and typed `var` rebinds pin to T0 (M5) | T27-6 |

Obligations 2, 5 and 7 are mechanical: they change how the emitter spells
what it already knows. Obligations 1, 3 and 4 are where most of the 4.64x
lives and each needs a proof the compiler does not yet keep — a
constructed-under-contract flag, a per-frame escape result, a raw record
ABI. Obligation 8 is what a user meets first. Strings (M4 note) are the one
area with no obligation here: they need their own round, because a string
that is a proven-ASCII exclusive buffer is a different representation
decision, not an emitter one.

What "no overhead versus C2MIR" would then leave is real and should be
named so it is not chased: GC allocation and marking on allocation-heavy
rows (gcbench 1.2x, binarytrees 0.72x — already *under* C2MIR because the
C port mallocs), the side-root frame prologue on calls that keep container
locals, and the saturating `int` semantics on operands whose range is
genuinely unknown. Those are the language, not the implementation.

## 9. Status ledger

| Track | Mechanism | Status |
|---|---|---|
| T27-0 | census refresh + M1 counter attribution | M1 counters taken on a current release (§10.1); frozen-row static census, per-function size and COW copies re-measured on r39 (§10.13, `temp/t27/census_all.py`); timings there are single runs under load and are not a baseline |
| T27-1 | memoize contract proof incl. disproof | re-scoped by §10.1: union memo already hits; plain `T[]` whole-array validator walk removed (§10.3) |
| T27-2 | elide identical-contract re-crossings | landed as the T27-8 structural key (§10.14): a declared binding, record field, `null` or non-raising call result whose contract is structurally the target's crosses nothing; cube3d `lambda_type_check` x17 → 0 mandatory, splay_node x23 → 0 (G2 met) |
| T27-3 | native stores for `var T[]` parameters | module-constant subscripts landed (§10.6): navier's mandatory Item setter gone; the lane setter is still a call on its guarded arm |
| T27-4 | RMW place borrow: richards pilot, then cd/splay/deltablue | `any`-leaf admission fast path (§10.6); fixed-key checked path setter with compiler-resolved leaf + builtin scalar contract fast path (§10.11): richards 0.58x, deltablue 0.62–0.85x, splay 0.80x, G7 met for richards; COW prologue and cd open; puzzle withdrawn as a target 2026-09-13 (S9.1.3 fix, v42 JIT was wrong); D4.4.4v2 sibling-field handles + early returns and D4.4.5 move-out binds (§10.12): cd 0.91x, table puts 2,000 → 2 copies; splay unchanged (store-backs inside `if` branches need a nested-list ruling) |
| T27-5 | `cd` + `queens` generic-operator case studies | reported (§10.17): cd's residue is the port's shape (untyped `[voxels, seen]` return, linear key table), queens has one `fn_ne` on a proven `int` compare -- no per-operator special case is warranted |
| T27-6 | auto-tier admission: refusal log, `match`, typed `var` rebinds | refusal log + type/literal `match` landed (§10.2); typed `var` rebinds landed on every tier and the D8.1.1v8 pin lifted (§10.13, D8.1.1v10) |
| T27-7 | emission diet + per-function size gate + satellite re-link | packed nullable float literals, dead layout-reload pruning (§10.4); scalar capture elision, NaN-first literal null test with packed-array reuse (§10.11); nullable-literal member predicate fixed (§10.13); `run_cube` 20,499 → 15,514 insns from the §10.14 proofs; the float null lane is a double immediate (§10.15); satellite re-link already fixed by D8.1.1v9 (`find_func`); per-function budgets for cube3d/deltablue/prettier_ast recorded as MT7 probes (§11); the 10,000 target is not met |
| T27-8 | callee-only admission (D8.3.2), structural elision key, record ABI | call-defined local bindings skip caller re-admission (§10.11); structural key through `T?`, record-field and alias proofs, float literals admitted by construction under a `float[]` contract, declared `T[]` locals from a proving call no longer re-admitted (§10.14); callee-only admission and record ABI not started |
| T27-9 | interval analysis: int53 band + BCE | band test consolidated; sentinel-operand defect fixed (§10.7); loop-accumulator proof and fused band branches recover array1/fib/triangl (§10.9); static array lengths for `fill(K, v)`/literal locals prove constant-index reads non-null (§10.14); literal loop bounds enter the finite plan and the dense extent proof, nested counters declared inside the body join it, and a finite fact starting at zero proves a counter non-negative (§10.16: quicksort 0.62x, nbody recovers 3% of its §10.15 cost); general interval/BCE extension open |

## 10. Implementation evidence (rev 3)

Working tree on `713a80c2a`. Release candidates are archived under
`temp/t27/` (`lambda-t27-r1.exe` … `r8.exe`); the comparison control is the
archived Result43 binary `test/benchmark/exe/lambda-v43-fabc412146`. The host
carried a load average near 30 during this round (parallel builds plus an
indexer), so timings below are same-session, interleaved and indicative only;
none closes a §5 gate.

### 10.1 T27-0: the M1 union hypothesis does not hold on current code

`COW_EXEC_PROFILE=1` on prettier_ast2 (release, JIT-pinned):

| Counter | Value |
|---|---:|
| `union_admit_calls` | 6,615,040 |
| `union_map_rep_cache_hits` | 6,615,034 |
| `union_map_rep_cache_misses` | 6 |

Disproofs are still never inserted, so every hit is a proof: the union memo
already answers essentially every `Doc` crossing. §3 M1's two "structural
limits" are not what costs prettier_ast. The validator did still appear in the
profile (`validate_binary_type`, `validate_against_map_type`), reached from
`lambda_type_check` through a *different* path — plain `Doc[]` array admission
(§10.3). The disproof memo is therefore not implemented: it would change
nothing measurable on the frozen rows.

### 10.2 T27-6: type/literal `match` in satellites, refusal logging

- `interp_satellite_refusal()` returns NULL or a reason; the scanner records
  the innermost refusing AST node kind, and the pin site logs
  `interp-tier: pinned function='…' reason=…` at notice level
  (`interp_plan.cpp`, `interp.cpp`).
- A `match` is admitted when every arm pattern is a type name, a scalar
  literal, or a union of those. Such arms lower to `fn_is`/`fn_eq` against the
  image's own type list and const pool, exactly as `x is T` / `x == lit`
  already do in satellites. Named string/symbol patterns, ranges and
  constrained `that` arms stay in T0.
- A literal pattern is an `AST_NODE_PRIMARY` with no inner expression;
  `ast_unwrap_primary` maps it to NULL, which first made every literal arm
  refuse. The predicate unwraps by hand.

| Row (auto tier, wall) | v43 | r8 |
|---|---:|---:|
| crypto_sha1 typed | 730 ms | 74 ms (untyped auto 96 ms) |

The whole ten-function cluster, including `core_sha1`, now promotes on first
entry. G8 is met for this row.

**Defect found and fixed while testing this track (pre-existing on v43).**
`pn c(v: any) int { let r = match v { case int: v + 1 default: -1 }; return r }`
printed `inf` under `LAMBDA_TIER=jit` and `6` under T0; `if (v is int) v + 1`
failed the same way. Cause: `v` is an inferred int specialization of an open
formal, so the call router selected the raw `_c` entry for `c(5)`, but the
argument emitter skipped its native-lane branch because the declared `any`
contract accepts `error`, and passed a boxed Item into the int-lane formal. The
router already sends every argument it cannot prove on the lane to `_b`, so
once the raw entry is selected the formal *is* the lane; the emitter now takes
the native branch for inferred specializations (transpile-mir.cpp, call
argument lowering; D8.3.3: the unboxed entry has exactly two proof paths). T27-6
would otherwise have exposed this in the auto tier, because satellites use the
same lowering. Fixture: `test/lambda/proc/tune27_satellite_match.ls`
(5,718,569 on T0, JIT and auto; v43 JIT printed `inf`).

### 10.3 T27-1 (re-scoped): plain `T[]` admission no longer re-walks the array

`runtime_type_admit_array` admits each element under `T` and, when no element
changed, installs the certificate on the same carrier. It then also called
`lambda_type_matches(value, expected)` "for occurrence/rank constraints". A unary
`T[]` has none — bounded repeats and tuples are different nodes — so that call
only repeated the element proof, deeply, through every nested element graph.
prettier_ast's `acc: Doc[]` accumulators are rebuilt each step, never carry a
certificate, and paid the walk per crossing. The walk now runs only when the
outer contract is not a unary `T[]` (constrained bases are already stripped at
the boundary under D3.2.2's base-only rule).

| Row (JIT exec, interleaved) | v43 | r6 |
|---|---:|---:|
| prettier_ast typed | 1245–1351 ms | 887–1021 ms |

Output is byte-identical. Rejections are unchanged in text and validator path
on both tiers (probes: a union element with a wrong field type, a nested
`int[][]` with a string leaf). Fixture:
`test/lambda/proc/tune27_array_contract_union.ls`.

### 10.4 T27-7: emission diet

Three mechanisms, measured on cube3d2's `run_cube`:

| Step | `run_cube` MIR insns | cube3d auto wall | JIT exec |
|---|---:|---:|---:|
| v43 | 36,161 | 375 ms | 14.5 ms |
| float null lane as a constant | 36,971 | — | — |
| + packed nullable float literals | 30,572 | 320 ms | 12.6 ms |
| + dead layout-reload pruning | **22,243** | **262 ms** | 12.2 ms |

1. **Null lane (withdrawn, §10.10).** `emit_float_null_lane` briefly
   materialized `FLOAT_LANE_NULL_BITS` through the bitcast scratch instead of
   calling `lambda_float_null_lane_c`. The full sweep showed it cost nbody 23%
   and fft 12%, so the call is back.
2. **Float literals.** `[qv[a], qv[b]]` members may be null (out-of-bounds
   typed reads), which sent every such literal to generic storage: `array`,
   `push_d`, `array_push_capture`×n, `array_end`. Members are now evaluated
   into double registers first, then one `array_float_new` and direct packed
   stores follow; a member that *is* null takes one out-of-line call,
   `array_float_literal_with_nulls(packed, null_mask)`, which republishes
   generic storage so the later boundary still reports the null (D3.2.2,
   S7.1.3v2). The existing non-null path got the same evaluate-first shape,
   which also removes a fresh header held unrooted across member evaluation.
   `array_float.ls`: 286 → 235 calls.
3. **Layout-reload liveness.** After every may-GC call the emitter reloads
   `items`/`length` of *every* cached typed array in the frame (the GC compacts
   ArrayNum data buffers). In `run_cube` that was 14 moves per call, ~19k of
   37k instructions. Lazy reloading at emission time is unsound at loop heads
   (it would reload every iteration in call-free loops), so the reloads stay
   eager but are recorded, and `mir_prune_dead_layout_reloads` runs a backward
   liveness pass over just the cache registers once the function is complete,
   deleting reloads whose register is not live. Functions with `JMPI`,
   `SWITCH`, `LADDR`, an unrecognized label operand, an unresolved branch
   target, or an address-taken cache register keep every reload.

Correctness evidence for the round so far:

- Lambda baseline on the r5 working tree: 5405/5408; the three failures were
  emission pins, reviewed and updated under D8.6.1 — `lambda_corpus_array_float`
  1402 → 1420 insns and `_guarded_load_#` 57 → 59 (`test/mir/mir_budgets.json`,
  reasons recorded in each description), and the Tune26 fixture
  `tune26_dense_carried_index.mir-check`, which pinned the old null-lane call
  and now pins the constant. The MIR forced-GC stress suite passed.
- Forced GC with poisoned frees (`LAMBDA_GC_FORCE_EVERY=1
  LAMBDA_GC_POISON_FREED=1`, JIT): cube3d2, fft2, nbody2, spectralnorm2,
  array1, crypto_sha12, pnpoly2, queens2 print the same as v43.
- New fixtures: `test/mir/lambda/tune27_float_literal_nullable.{ls,txt,mir-check}`,
  `test/mir/lambda/tune27_layout_reload_liveness.{ls,txt,mir-check}` (both in the
  forced-GC corpus by discovery and registered in `test_lambda_gtest.cpp`),
  `test/lambda/proc/tune27_satellite_match.{ls,txt}`,
  `test/lambda/proc/tune27_array_contract_union.{ls,txt}`.

### 10.5 Findings that re-scope open tracks

- **richards (T27-4).** The typed penalty is not only `cow_prepare_write`.
  The profile is led by `fn_map_set`, `fn_member`, `lambda_type_matches`,
  `scalar_storage_read`, `lambda_lane_storage_desc_for`,
  `lambda_type_nonnull_map_contract` and `lambda_array_contract_info`: the
  `World` record holds `tasks: array` of untyped records, so every
  `w.tasks[tid].pp = …` goes through `lambda_map_path_set_checked_inplace`,
  which recomputes the path's leaf contract and storage lane per store. A
  per-`(root contract, path shape)` memo of the leaf contract is the next
  measured candidate; the COW prologue is secondary.
- **Auto-tier pin sweep.** With §10.2 in place, running every `*2.ls` typed
  benchmark on the default tier logs no `interp-tier: pinned` line, so M5's
  remaining auto-tier cost is compile time (§10.4), not admission.
- **T27-8 item 2.** `lambda_boundary_is_redundant` is identity-only on purpose:
  admission also converts, and an earlier relaxation made mbrot, permute and
  nqueens compute wrong answers (its own comment). A structural key needs a
  conversion-free proof, not just semantic equality.

### 10.6 T27-3 and T27-4 slices

**T27-3: module constants in store subscripts.** navier_stokes' nine
mandatory `lambda_array_set_checked_inplace_item` calls were all in `set_bnd`,
and none was caused by the value: every subscript that mentioned a module
`let` (`x[j * ROW_SIZE]`, `x[(WIDTH + 1) + j * ROW_SIZE]`) failed
`mir_index_leaf_is_native_int`, whose identifier arm accepted only MIR locals,
so the store emitter treated the key as non-integral and boxed it for the Item
setter. The arithmetic proof (`mir_native_arithmetic_operand_type`) already
accepted those bindings, and every native index emitter reloads a module slot
and reopens it on the int lane (`transpile_native_int_expr`). The leaf proof now
accepts an immutable module binding whose carrier is `int`.

| Check | Result |
|---|---|
| navier_stokes mandatory-path census | `lambda_array_set_checked_inplace_item` 9 → 0 (G3's mechanism) |
| navier_stokes typed, JIT exec, 4 interleaved pairs | v43 234–273 ms, r11 193–208 ms; checksum 77 |
| fixture `test/mir/lambda/tune27_module_const_index_store` | same output as v43 on JIT and T0; `.mir-check` forbids the Item setter |

The in-place stores now use `lambda_array_set_checked_inplace_lane` on their
guarded arm; replacing that call with a raw store under a frame exclusivity
fact is the rest of T27-3 and depends on T27-4's ownership work.

**T27-4: `any` leaves.** Below an open `array` field the path leaf contract is
`any`. `runtime_value_rep_proves_contract` then ran array, map and storage-lane
probes, failed, and fell to `lambda_type_check`, whose admission walked the
numeric, array and map arms before `lambda_type_matches` answered true. Both
functions now return at once for `any` and a non-error value; an error value
keeps the old path, whose answer depends on the contract accepting error.

| Check | Result |
|---|---|
| richards typed, JIT exec, 3 interleaved triples | v43 882–898 ms, r8 892–900 ms, r9 818–839 ms; PASS |
| open-leaf store probe (`var w: W`, `items: array`, string/array/map/error values) | same output as v43 on JIT and T0 |

This is a small share of richards' typed penalty; §10.5's leaf-contract memo
remains the larger candidate.

### 10.7 T27-9: one band test, and a sentinel defect it exposed

**Consolidation.** Nine emitters spelled the int53 band test by hand as
`GE v, INT53_MIN` / `LE v, INT53_MAX` / `AND` (boxing, lane-to-double, divmod
operands, comparison operands, arithmetic results, range bounds, the
subtraction loop, lane validity, the index sanitizer). They now call
`emit_int53_in_band[_into]`: `add t, v, INT53_MAX` then
`ule ok, t, 2 * INT53_MAX`. Adding `INT53_MAX` maps the band onto
`[0, 2 * INT53_MAX]` as an unsigned word, and every other i64, including the
three lane sentinels at the extremes, wraps outside it, so the test is exact.
It is the 64-bit `ULE`, not `ULES` (MIR's `S` suffix is the 32-bit compare).
The two double-domain range checks keep their form. collatz's `collatz_len`
went from 118 to 108 instructions.

**Defect (pre-existing, S4.1.2).** An edge-case probe run to validate the
rewrite showed the JIT and T0 disagreeing. v43's JIT, on hot helpers, printed
`[0, 0, 0, 0, inf, 2, -inf, inf, 78]` where T0 prints
`[nan, nan, nan, nan, nan, -inf, -inf, inf, 78]` for `inf + -inf`, `inf - inf`,
`inf * 0`, `0 * -inf`, `nan + inf`, `-inf + -inf`, … The fast arm tested only
the result's band, on the documented assumption that "any arithmetic on a
sentinel lands far out of band". That holds for one sentinel and an in-band
partner. It fails for two sentinels, whose i64 sum always lands in `[-2, 2]`,
and for a sentinel times 0. `emit_int_lane_arith` now also tests the operands
the result cannot exonerate:

- add/sub: nothing when either operand is proven in band (`i + 1`, `n - 2`),
  otherwise one operand;
- mul: an operand is exempt when the other is proven in band and nonzero
  (`3 * n`), otherwise it is tested.

Fixture: `test/lambda/proc/tune27_int_sentinel_arith.{ls,txt}`. It runs its
helpers hot so the auto tier promotes them; all three tiers now print the T0
answer.

**Cost, and why it is not engineered away.** Because a two-sentinel sum is
indistinguishable from a legal small result, exactness needs one operand
test whenever neither addend is proven. fib's `fib(n - 1) + fib(n - 2)` is
exactly that shape:

| Row (JIT exec, 12 interleaved triples) | v43 | band rewrite only (r12) | + sentinel guard (r13) |
|---|---:|---:|---:|
| fib2 mean | 1.380 ms | 1.384 ms | 1.471 ms (+6.6%) |
| collatz2 (3 pairs) | 331–333 ms | — | 319–342 ms |
| mandelbrot2 (3 pairs) | 43.4–43.8 ms | — | 42.5–42.9 ms |

A later 9-pair rerun at lower load also attributes array1 to this guard:
1.088 → 1.442 ms (1.33x) between r12 and r13, because `s = s + arr[i]` adds an
accumulator to an element load and neither is proven. navier_stokes keeps a net
gain but gives back about a tenth of it (v43 126.9 ms, r12 105.1, r13 116.9).

This breaks G5 for array1 and fib2. A cheaper exact test does not exist at the
instruction level: every two-sentinel sum lands in `[-2, 2]`, which is
indistinguishable from a legal result without inspecting an operand. The way
back is a proof, not a peephole — a loop-versioned fact that an accumulator
initialized in band and updated only on the fast arm stays in band (Tune26's
finite-fact machinery, extended from counters to accumulators), with the
generic sibling loop taking over after the first slow-arm hit. The fix stays;
recovering array1/fib2 is that follow-up slice, and whether to hold G5 open for
it is the owner's call.

### 10.8 G5 sweep and regressions that predate Tune27

All 63 typed rows were run JIT-pinned, three interleaved pairs, v43 against r13:
every row prints the same output. The medians flagged six rows above 1.2x.
Targeted reruns (5–9 interleaved runs) against a clean release of `713a80c2a`
(working tree stashed, `temp/t27/lambda-head-713a80c2a.exe`) separate them:

| Row | v43 | clean HEAD | r13 | Attribution |
|---|---:|---:|---:|---|
| awfy/bounce | 1.00 | 1.30 | 1.29 | **HEAD**, not Tune27 |
| larceny/quicksort | 1.00 | 1.34 | 1.33 | **HEAD**, not Tune27 |
| beng/fannkuch | 1.00 | 1.06 | 1.08 | mostly HEAD |
| larceny/array1 | 1.00 | 1.00 | 1.33 | Tune27 sentinel guard (§10.7) |
| r7rs/fib | 1.00 | 0.95 | 1.00 | Tune27 sentinel guard (+5% vs HEAD) |
| awfy/towers | 1.00 | — | 0.98 | noise in the sweep |

bounce and quicksort regressed in the three commits between Result43
(`fabc41214`) and `713a80c2a` ("MIR emit fix", "bug fix", "regress fix"). They
are outside Tune27's scope but inside G5's baseline, so T27-0's archive must be
taken at a commit that has them fixed or they must be recorded as inherited.

### 10.9 T27-9 continued: the loop-accumulator proof

**Owner decision (2026-09-13):** keep the §10.7 fix; recover the cost with an
accumulator proof.

**Proof.** In a guarded copy of a `while` loop, the update `s = s + e` (also
`s = e + s`, `s = s - e`) needs no operand test on `s` when:

1. `s` is in band when the copy is entered: the loop's entry guard tests it,
   ANDed into the finite-loop guard when that plan applies, or as the copy's
   own guard otherwise;
2. the update is `s`'s only write in the body, runs at most once per iteration
   (it may sit under `if`, never inside a nested loop), the body defines no
   function, and `s` is never passed as a call argument (a typed or untyped
   `var` parameter could rewrite it);
3. `e` is not already proven in band, since an in-band partner exonerates `s`
   anyway and versioning the loop for it would only duplicate the body;
4. every slow or null arm of the update sets `accum_degraded`, and the loop
   head of the copy tests it first, branching to the generic sibling's entry.

Re-entering a `while` at its head is exact, because it re-tests its condition
with the current state. `continue` targets the same head. So `s` is in band at
every update the copy executes, and a saturated or null result is handled by
the unchanged generic sibling from the next iteration on. The exemption is
keyed on the update node (`mt->accumulator_updates`); no other read of `s`
gains a fact. Implementation: `mir_loop_accumulator_plan`,
`MirLoopAccumulatorScope`, the loop-head exit in `transpile_while_core`, and
`accumulator_side` in `emit_int_lane_arith`. The generic arm installs an empty
scope, so nested and sibling loops never inherit the exemption.

Rejected shapes stay guarded and correct. An accumulator written twice per
iteration pays the operand test: a synthetic `pegs = pegs - a[i];
pegs = pegs + a[i]` loop runs 1.20x. Extending the proof to it would need a
per-update degradation check, because the second update can see the first
one's sentinel within the same iteration.

**Fixture.** `test/lambda/proc/tune27_loop_accumulator.{ls,txt}` covers
saturation mid-loop, sentinel elements, an `if`-guarded update, `continue`, a
right-hand accumulator and subtraction, and runs hot. v43's JIT printed
`[10, inf, 8, 3, 0, 9990, -inf, 96]`; T0, JIT and auto now print
`[10, inf, nan, -inf, nan, 9990, -inf, 96]`.

**Band branch spelling.** Two further measurements settled how the one band
owner spells a branch-only guard. Branch-only sites no longer build a 0/1 flag
to AND into a later `bf`: arithmetic result and operand guards, lane-to-double
and array-store validity use `add` + one fused unsigned branch
(`emit_int53_branch_if_out_of_band`). For boxing, whose fast path is the *taken*
branch, two signed compare-branches measured better. On arm64 each spelling
materializes the 54-bit bounds as multi-instruction constants, so the choice
is empirical:

| Row (JIT, 5–9 interleaved runs, ratio to the unsigned-everywhere build) | all signed | hybrid (kept) |
|---|---:|---:|
| fib2 | 1.030 | 0.995 |
| divrec2 | 1.111 | 1.000 |
| triangl2 | 0.954 | 0.943 |
| paraffins2 | 1.042 | 0.995 |
| ack2 | 1.037 | 1.012 |

All-signed measured 0.9% slower by geomean over 24 int-heavy rows.

**Intermediate measurement, superseded by §10.10** (JIT, 5–9 interleaved runs,
load average 7–30):

| Row | v43 | clean HEAD `713a80c2a` | r13 (§10.7 fix only) | final |
|---|---:|---:|---:|---:|
| array1 | 1.00 | 1.00 | 1.32 | 0.995 |
| fib2 | 1.00 | 1.01 | 1.08 | 1.01 |
| triangl2 | 1.00 | 1.00 | 1.05 | ≈0.94–1.00 |
| collatz2 | 1.00 | 1.00 | 0.97 | 0.95 |
| ack2 | 1.00 | 1.00 | 0.93 | 0.95 |
| navier_stokes2 | 1.00 | 1.03 | 0.82 | 0.83 |
| cube3d2 | 1.00 | 0.99 | 0.84 | 0.83 |

§10.10 records the final shape and the full-corpus sweep.

### 10.10 Final shape of the T27-7/T27-9 slices and the full sweep

Three corrections came out of the full-corpus sweep and each was bisected to
an archived build before it was changed:

1. **Null lane back to a call.** nbody 1.23x and fft 1.12x first appear in r1,
   whose only JIT change was reinterpreting the null-lane marker through the
   Context bitcast scratch: a memory store in every float function. The call
   form restores both and also generated faster cube3d code (0.78x). A double
   literal and a load from a global word were tried as well. Their JIT-vs-T0
   mismatches on two Tune26 fixtures turned out to be pre-existing and present
   on v43 too (see below), so they were not the cause, but neither beat the
   call.
2. **Accumulator proof generalized, then fenced.** The plan now accepts a
   left-leaning update chain (`ii = ii + mmax + mmax`) and exempts accumulator
   reads that evaluate before the write in the same iteration: the loop
   condition, earlier top-level statements, and the update's own right-hand
   side (fft 1.04x → 1.00x). An emission-time backstop in
   `transpile_assign_stam` band-checks the stored value if the update was not
   lowered by the degrading arithmetic path, so a plan/emission mismatch costs
   speed, never soundness. Three fences keep versioning from costing more than
   it saves:
   - an accumulator-only copy is made only for a leaf loop with no user call
     (paraffins' short call-heavy nested loops ran 1.12x split, and copying
     outer loops grew `count_ccp` from 1,425 to 3,708 MIR instructions);
   - an accumulator with an already in-band partner (`s + 1`) is not versioned
     (triangl's body doubled to 4,484 instructions for nothing);
   - reads inside array subscripts do not count, because the index emitters
     never consult the exemption (matmul's counter `k` otherwise paid a head
     test on 8M iterations: 1.10x).
3. **Operand pre-tests rejected.** Guarding the operand band tests behind a
   cheap `[-2, 2]` / `!= 0` result test is exact (every two-sentinel sum lies
   in `[-2, 2]`; the only in-band product with a sentinel factor is 0), but it
   measured 1.035x HEAD by geomean (fib 1.16x, divrec 1.22x), so it was not
   kept.

**Full sweep** — all 63 typed rows, JIT-pinned, 3–5 interleaved runs, load
average 3–5, against v43 and a clean release of `713a80c2a`:

| Measure | Result |
|---|---|
| output | identical to v43 on all 63 rows |
| geomean vs v43 | 0.993 |
| geomean vs clean HEAD | 0.978 |
| rows > 3% slower than HEAD | spectralnorm 1.077, levenshtein 1.035 |
| rows ≥ 5% faster than HEAD | prettier_ast 0.707, navier_stokes 0.792, raytrace3d 0.797, cube3d 0.779, crypto_sha1 0.915, ack 0.929, richards 0.933 |
| rows this round recovered | fib 1.021, array1 1.005, fft 1.013, triangl 0.996, matmul 0.975, paraffins 1.023, nbody 0.999 |

spectralnorm's residue is `eval_A(i, j)`: a non-loop function whose `int`
parameters feed `(i + j) * (i + j + 1)`, so no loop fact can exonerate them.
Recovering it needs a parameter range proof from its callers, which is outside
this slice.

**Pre-existing, found on the way, not fixed.** On v43, clean HEAD and every
Tune27 build, `test/mir/lambda/tune26_dense_carried_index.ls` prints
`[45, false]` and `tune26_nullable_float_store.ls` prints `false null 1` on the
JIT and auto tiers, while T0 and both `.txt` goldens say `true`: the `^ { … }`
handler around an out-of-bounds float read never fires on the MIR lane. No test
compares those goldens, which is how the divergence survived.

Final gates for this round: Lambda baseline 5421/5421; MIR emission 135/135;
MT7 ratchet 16/16 with seven budgets re-baselined downward to lock in the
shrink.


### 10.11 Round 2: path stores, call-defined bindings, emission diet, a JIT null guard

Five slices plus one pre-existing crash fix, each archived as a release build
(`temp/t27/lambda-t27-r28..r34.exe`) and checked against the previous one.

**T27-4: the fixed-key checked path setter.** A profile of typed richards
(300 iterations) put 45% of execution in
`lambda_map_path_set_checked_inplace`. Every `w.tasks[tid].pp = v` built a
heap path array (`array_plain` + three `array_push`), re-walked the declared
contract per store to find the leaf, and nested three `RootFrame`s. The leaf
below an open `array` field is always `any` (D3.2.4v3), and nothing about it
depends on the runtime key values except index exactness. Now:

- `lambda_map_path_set_checked_fixed(owner, value, k0, k1, k2, shape,
  expected, leaf)` takes one to three keys as arguments. The compiler resolves
  the leaf with the same step function the runtime walk uses
  (`lambda_map_path_contract_step`); bit `16+i` of `shape` marks a bracket key
  whose int exactness the runtime confirms before trusting that resolution.
- A proven root goes straight to `runtime_map_path_write_proven`, which now
  roots only when the leaf needs admission, and to one rooted walk
  (`cow_path_set_impl` accepts fixed keys, the same two ABI shapes
  `cow_path_borrow_impl` already had). Anything else materializes the
  descriptor and calls the unchanged transactional setter.
- `runtime_value_rep_proves_contract` answers a bare `int`/`bool`/`float`
  contract from the value's tag. The trusted-record branch of `fn_map_set`
  had been calling `lambda_lane_storage_desc_for` and `lambda_type_matches`
  to prove `true` is a `bool`.

| richards (JIT exec) | ms |
|---|---:|
| r27 typed | 681–720 |
| + fixed-key setter (r29) | 482–502 |
| + scalar contract fast path (r30) | 407–413 |
| untyped twin, same kernel | 403–433 |

G7 is met for richards: the typed row is no longer slower than its untyped
twin. Fixture `test/lambda/proc/tune27_fixed_path_store` pins open leaves,
declared-leaf rejection, bracket keys (including `2.0`), the four-key
descriptor fallback and a detached plain root on all three tiers.

**T27-8 slice: bindings defined only by proving calls.** A caller-side
admission is redundant when its source is a call to a local function with an
explicit, non-raising, compatible return contract (T20-4). cube3d's
`mtrans = rotate_z(mtrans, 5.0)` chain still checked `mtrans` at every call
because the argument is an *identifier*. `mir_binding_definitions_prove_contract`
accepts an unannotated local whose declaration and every assignment in the body
are such calls, with no element or member store on it and no use as an
argument other than a local callee's non-var parameter. Order does not matter
because every definition proves the contract, so loops need no flow analysis.
The body walk must see the declaration, so module and captured bindings never
qualify.

**T27-7: emission diet.**
- A named insertion whose carrier is a scalar no longer emits
  `cow_capture_value` (`mir_insertion_needs_capture`); a scalar has nothing to
  share-mark.
- A float literal with nullable members tested each lane member's null payload
  through the bitcast scratch — a memory store per member on the hot arm. The
  hot arm now asks only `x != x`; the cold arm answers the exact null question
  and keeps the packed literal when a NaN is not the null payload. The cold arm
  also reuses the packed array instead of allocating a second one
  (cube3d `run_cube`: `array_float_new` 203 → 102, instructions 21,016 →
  20,506). The fixture gained NaN-member cases.

**Pre-existing JIT crash fixed.** `test/mir/lambda/typed_path_store.ls`
segfaulted JIT-pinned on clean HEAD and every earlier build; the harness runs
the default auto tier, which interprets that cold `main`. The minimal repro is
`world.rows[1].values[0]` with `rows[1] == null`: the member read correctly
yields null for the typed `int[]` field, and `emit_checked_index_load`'s
runtime representation guard then read the header byte at address 0. A static
container type does not make the receiver a container — a read through a null
or out-of-bounds record produces null (S7.1.3v2). The guard now starts with one
unsigned test (`boxed - 1 >= 2^56 - 1`) that sends null, scalar, error and
raw-zero receivers to the existing `item_at` slow arm; proven and dense-guarded
reads skip it as before. `test_lambda_gtest` now also runs this fixture with
`LAMBDA_TIER=jit` (`test_lambda_script_against_file` takes an optional tier).

**Sweep** — 69 typed rows, JIT-pinned, min of 3 interleaved runs, r33 against
r27 (end of round 1) and a clean release of `713a80c2a`:

| Measure | Result |
|---|---|
| output | identical on all 69 rows |
| geomean vs r27 | 0.970 |
| geomean vs clean HEAD | 0.945 |
| largest gains vs r27 | richards 0.586 (JetStream 0.582), deltablue 0.847 (JetStream 0.622), splay 0.799, puzzle 0.847, json 0.938 |
| > 3% slower than HEAD | spectralnorm 1.084 (round-1 `eval_A` residue), paraffins 1.043, base64 1.037, sieve 1.033, queens 1.087 |

A 5-run recheck of the sensitive rows with the null guard added (r34) moved
the sub-millisecond outliers back into noise: queens 1.033 and sieve 1.000
against r27, paraffins 1.000; nqueens and fft, which the 3-run sweep had shown
as gains, measure 1.000 as well, so they are not claimed. The guard itself
costs nothing measurable against r33 (fft, spectralnorm, nbody, matmul,
mandelbrot, brainfuck all within ±0.6%; cube3d 1.022, base64 1.031 at 5 runs).

Gates for this round: Lambda baseline 5423/5423 (one new JIT-pinned test);
MIR emission 135/135 with `typed_path_store`'s pin moved to the fixed-key
setter; MT7 ratchet 16/16 with `lambda_corpus_array_float` raised 1412 → 1420
for the receiver test (reason recorded in the budget).

### 10.12 Round 3: COW borrows for cd and splay (D4.4.4v2, D4.4.5)

Profiles of the two worst remaining typed rows against Node (Result43: cd
14.6x, splay 19.8x) pointed at copies, not at emitted code:

| Row (JIT, `COW_EXEC_PROFILE`) | shared copies per run | copied bytes |
|---|---:|---:|
| cd | array 344,179, map 132,567 | 152 MB |
| splay | map 603,158 | 39 MB |

**cd — D4.4.4v2 (ratified 2026-09-14).** `rbt_put` binds two read-modify-write
handles on sibling fields of one `var` root and returns early in its update
branch:

```lambda
var keys = tree.keys
var vals = tree.vals
if (index != NIL) { vals[index] = value; tree.vals = vals; return old }
push(keys, key); push(vals, value)
tree.keys = keys; tree.vals = vals
```

CW34 refused both handles: each region names the root through the other
field, and the early `return` is not preceded by `keys`' store-back. Neither
is observable. A sibling member is a different slot, and an alias between two
slots is share-marked by the field store or literal that published it. A
return that precedes every use of the handle cannot leave an in-place write
behind. D4.4.4v2 admits both (`rmw_sibling_place`, `handle_named` in
`build_ast.cpp`). A 1,000-put probe went from 2,000 array copies (8.1 MB) to 2;
a single cd frame from 1,050 to 654. cd moved from 563–617 ms to 516–520 ms. The rest of the row is
workload shape: the Lambda port replaced the red-black tree with a linear
key table (`rbt_find_node` scans), which the JS original does not have.

**splay — D4.4.5 move-out binds (ratified 2026-09-14).** The rotations
`var left = node.left; var branch = left.right; node.left = branch;
left.right = node` copy `left` because the bind marked it, although the
overwrite removed the only other holder first. D4.4.5 binds such a handle as a
borrow under the CW34 spine test (`rmw_moves_out`). Record annotations with the
same nominal contract and optionality are admitted too. Probes of typed and
untyped rotations went from one map copy per rotation to zero.

splay itself is unchanged (603k copies). `splay_node` binds
`var left = node.left` and stores it back only inside `if` branches, so `left`
keeps its snapshot mark. Every `branch = splay_node(branch, key)` then hands
the rotations a marked root, and their spine test correctly refuses the
borrow. Admitting store-backs in nested lists needs a path-sensitive
dead-after-store-back analysis and its own ruling. It is the next splay lever.

Fixtures: `test/lambda/proc/cow_rmw_sibling_borrow` and
`test/lambda/proc/cow_move_out_bind`. Both are identical on interp/jit/auto
and to the pre-revision build, and their copy counts drop (90 → 29 and
123 → 83). Two pre-existing value-semantics defects surfaced and were left
out of the goldens; both are fixed in §10.13:

- `push(m.a, v)` wrote through a container that `m.a = x; m.b = x` left in
  two slots (place mutators now borrow the place, `cow_place_leaf`).
- A `let before = b` snapshot of a typed optional record was mutated by a
  later `var`-parameter call (the nullable-pointer contract lane bypassed the
  borrow's `_b` route).

**Sweep** — 69 typed rows, JIT-pinned, min of 3 interleaved runs, r37 against
r34 (end of round 2) and clean `713a80c2a`: output identical on all rows;
geomean 0.989 vs r34 and 0.943 vs HEAD; cd 0.906. prettier_ast read 1.042 on
the 3-run pass and 1.018 on a 5-run recheck. Still > 3% slower than HEAD:
spectralnorm 1.099 (round-1 `eval_A` residue), paraffins 1.034, list 1.032
(sub-ms; 0.973 against r34 at 5 runs).

Gates for this round: Lambda baseline 5425/5425 (both new fixtures); forced-GC
stress (`LAMBDA_GC_FORCE_EVERY=1`, `LAMBDA_GC_POISON_FREED=1`) green on
`cow_rmw_sibling_borrow`, `cow_move_out_bind` and `cow_rmw_borrow` on all
three tiers. Formal design spec 3.0.0 → 3.2.0 (D4.4.4v2, D4.4.5); COW record
§11.11 revision v2 and §11.12 CW35.

### 10.13 Round 4: typed `var` rebinds on every tier, and the census refresh

**Two value-semantics defects from §10.12 closed (r38, r39).** Place
mutators (`push(m.a, v)`, `splice`, `set`) now borrow the place through
`cow_place_leaf[_fixed]` on both tiers, so a container published into two
slots is detached before the write (S9.2.2; fixture
`test/lambda/proc/cow_place_mutator`). A `var n: N?` parameter takes the
nullable-pointer contract lane, and `mir_borrowed_param_requires_raw_abi`
tested only the declared native types, so the call site routed the borrow
down the native-lane arm with neither detach nor CW33 transport;
`let before = b; set_left(b)` wrote into `before` (S9.1.2, S9.1.3). The
predicate now uses the same lane test the arm uses (fixtures
`cow_var_nullable_record`, `cow_var_nullable_record_typed_handle`); splay2
pays 1.07x for the `_b` route (copies +0.7%, `map_admit_calls` +19%), a
tuning follow-up.

**Typed `var` rebinds were lost on the eager tier — every kind (r40,
D8.1.1v10).** Probing DO29 more widely than its text showed that
`pn inc(var n: int) { n = n + 1 }` rebinds only the callee's register
JIT-pinned, for every scalar lane, `string`, records and their optional
forms; only untyped and typed-array positions wrote back. T0 and the auto
tier were right solely because of the v8 pin. Three gaps, one convention:

- the M1a prologue excluded typed non-array positions from home
  consumption, and the boxed-body scalar-param path never registered the
  binding for the epilogue, so a consumed home was dropped;
- the caller had no home for a lane local (int/float/bool register,
  `String*` pointer): such bindings have no Item-classed root slot, and a
  raw-pointer-classed slot must not hold a tagged Item. `MirVarHome`
  (root, slot, kind, contract) replaces the parallel root/typed arrays; a
  typed lane argument is homed from its boxed *argument* slot and decoded
  back into the lane through the contract after the call
  (`MIR_VAR_HOME_TYPED_LANE`), never switching the binding's carrier
  (D3.3.4);
- the `_b` adapter consumed T0's cell for every typed position but
  forwarded only array homes; it now forwards all of them through its own
  binding slot and stores the reloaded value back.

The epilogue publishes through `emit_box_contract_lane`, and the auto-tier
pin (`interp_fn_rebinds_typed_var_param`, `FnPromotionCell::typed_var_rebind`)
is gone: `cow_var_typed_rebind.ls` promotes `main` as a 17-member image
containing every rebinding callee, byte-identical to T0.

**Two pre-existing nullable-lane defects the fixture exposed.** `g = null`
into a `float?`/`bool?`/`string?` lane binding decoded the RHS with the
non-nullable decoder (`it2d`/`it2b`/`it2s`: NaN, `false`, a non-null ""
pointer) — `transpile_assign_stam_core` now decodes through the binding's
contract; and `return g` boxed a lane local with the bare boxer instead of
the shared representation boundary every other consumer uses. Both were
wrong on clean `713a80c2a` (`[nan, false, [nan], nan]` for a nulled
`float?` local's uses).

**T27-7 regression fixed.** The nullable float-literal predicate (§10.4)
cleared `only_nullable_floats` only for *dynamic* non-float members, so
`[g, g == null]` — a nullable float beside a proven bool — packed the bool
through `it2d` (r39 printed `[nan, nan]`; HEAD kept the bool). Every member
must now be a float carrier. `tune27_float_literal_nullable` gained a mixed
literal and its `.mir-check` forbids the packed path in it.

**T27-0 census refresh (r39, `temp/t27/census_all.py`).** Static columns
per §6: largest function and its instruction count, COW shared copies
(map/array, `COW_EXEC_PROFILE`), and the dominator-exact mandatory-path
calls (first four). The ms column is one JIT-pinned run on a host at load
average 60 and is recorded only as an order of magnitude. richards dumps
only its entry module (§6 trap 3).

| Row | ms (1 run, loaded) | largest fn | insns | copies map/array | mandatory |
|---|---:|---|---:|---|---|
| r7rs/mbrot | 0.956 | test_1033 | 243 | ?/? | fn_index:1, cow_prepare_write:1, cow_path_set_raw:1, fn_fill:1 |
| awfy/queens | 0.195 | is_valid_1360 | 392 | ?/? | fn_ne:1 |
| awfy/towers | 0.306 | move_disks_b_688 | 240 | ?/? | (call-free) |
| awfy/mandelbrot | 40.023 | mandelbrot_86 | 897 | ?/? | fn_add:3 |
| awfy/nbody | 16.588 | advance_263 | 1864 | ?/? | sqrt:2 |
| awfy/richards | 389.991 | main_125 | 112 | 1000/0 | (call-free) |
| awfy/deltablue | 42.45 | c_choose_method_7356 | 2930 | 0/0 | lambda_type_check:17, cow_prepare_write:10, fn_eq:7, cow_bind_var:1 |
| awfy/cd | 501.344 | handle_new_frame_13980 | 3281 | 132567/259563 | array_push:8, lambda_type_check:7, item_at:4, fn_ne:3 |
| beng/mandelbrot | 17.992 | main_362 | 1037 | ?/? | (call-free) |
| beng/nbody | 16.518 | advance_998 | 1864 | ?/? | sqrt:2, fn_strcat:1 |
| kostya/brainfuck | 198.868 | build_jump_table_440 | 600 | ?/? | fn_string_ascii_at:3, fn_ord:3, it2s:1 |
| kostya/matmul | 17.301 | matmul_368 | 971 | ?/? | (call-free) |
| kostya/primes | 2.448 | sieve_181 | 597 | ?/? | (call-free) |
| kostya/collatz | 311.489 | main_819 | 142 | ?/? | (call-free) |
| larceny/triangl | 186.345 | benchmark_707 | 1571 | ?/? | (call-free) |
| larceny/primes | 2.48 | sieve_209 | 597 | ?/? | (call-free) |
| larceny/puzzle | 13.661 | solve_215 | 987 | ?/? | (call-free) |
| text/fast_diff | 138.765 | score_diff_78 | 1234 | ?/? | fn_string_ascii_at:2, fn_eq:1, fn_len_l:1, item_at:1 |
| text/microdiff | 58.606 | valid_snapshot_diffs_4114 | 865 | 0/0 | fn_len:7, fn_join:4, lambda_type_check:3, array_int_new:2 |
| text/hyphen | 72.174 | main_182 | 27 | 0/0 | (call-free) |
| text/prettier_ast | 718.373 | print_node_10192 | 3885 | 0/0 | fn_member_by_id:14, array_push:3, it2s:2, is_truthy:2 |
| text/text_search | 1781.42 | main_3165 | 711 | ?/0 | fn_string:3, pn_push_cow:3, fn_strcat_many:1, fn_len_s:1 |
| text/three_way_merge | 3748.06 | merge_words_1159 | 864 | ?/0 | it2s:6, fn_len:4, pn_push_cow:2, fn_string:1 |
| text/log_pipeline | 6079.89 | make_log_line_1132 | 827 | 0/0 | it2s:2, pn_push_cow:1, fn_len_l:1, fn_index:1 |
| jetstream/nbody | 16.567 | advance_924 | 1864 | ?/? | sqrt:2, floor:1 |
| jetstream/cube3d | 11.296 | run_cube_4058 | 20499 | ?/? | lambda_type_check:17, is_truthy:12, fn_fill:9, lambda_array_admit_numeric_contract:9 |
| jetstream/navier_stokes | 98.796 | set_bnd_554 | 2700 | ?/? | lambda_double_to_int_lane_c:3, floor:2, cow_prepare_write:2 |
| jetstream/richards | 393.541 | main_130 | 112 | 1000/0 | (call-free) |
| jetstream/splay | 358.98 | splay_node_2675 | 1101 | 607156/? | lambda_type_check:6, fn_ne:1, fn_eq:1 |
| jetstream/deltablue | 22.547 | constraint_choose_method_5077 | 1996 | 16060/0 | fn_ne:3, lambda_type_check:2, lambda_map_path_set_checked_fixed:2, fn_len:1 |
| jetstream/hashmap | 41.068 | hashmap_rehash_2673 | 1217 | 0/? | lambda_type_check:3, cow_prepare_write:2, cow_path_borrow_fixed:2, fn_add:2 |
| jetstream/crypto_sha1 | 25.464 | core_sha1_2024 | 1136 | ?/? | lambda_type_check:5, fn_band_item:2, fn_string_freeze:2, fn_slice3:2 |

Read against §2: cube3d's `lambda_type_check` x19 is x17 with
`run_cube` at 20,499 instructions (from 36,161); navier's mandatory
`_item` setters are gone and `cow_prepare_write` is x2 (was x7);
deltablue's `cow_prepare_write` is x10 (awfy) and 0 mandatory on the
JetStream port, whose remaining copies are 16,060 maps; splay still copies
607k maps (the nested-list store-back ruling, §10.12); cd copies 133k maps
and 260k arrays after D4.4.4v2. brainfuck, fast_diff and the text rows are
string-bound as §2 said and untouched by this round.

**COW census, r39 → r40 (`COW_EXEC_PROFILE`, JIT).** cd2 array copies
259,563 → 239,862 (-7.6%), maps flat. havlak2 maps 55,410 → 58,612 (+5.8%)
with `unique_mutations` 162,789 → 242,583: the emitted MIR is identical
(114 functions, 75 `cow_prepare_write` sites in both), so the extra
prepares are the `_b` adapter's un-share-at-borrow (S9.2.2), which runs
only when a home was transported and now runs for typed record `var`
positions too. r39 wrote through those 3,201 runtime-shared roots in place;
the adapter now detaches them and stores the replacement back through the
home, which is the ruling's required work, not an emission change.

**Sweep** — 69 typed rows, JIT-pinned, min of 5 interleaved runs, r40
against r39 (end of §10.12 plus the two §10.12 defect fixes): output
identical on all 69 rows and on all 138 benchmark scripts (typed and
untyped); geomean 1.002. richards 0.957–1.001, cd 0.986, havlak 1.015 (the
COW census above), deltablue 1.015, splay 1.004, hashmap 1.001. Rows that
read over 3% at 5 runs were rechecked at 9: nbody 0.993, mandelbrot 0.996,
regexredux 0.996 (noise); permute 1.110 is real and attributed — its
recursive `permute(var st: PState, var v: int[], n)` call now transports
and reloads the record home (`_permute` 167 → 183 instructions), the
required CW33 work for a `var` record position on an 82 µs row.

Gates for this round: fixture parity (interp/jit/auto) on every `cow_*`,
`tune27_*`, `interp_typed_var_*`, `proc_push` and `typed_path_store`
fixture; forced-GC stress green on the five `var` fixtures on all three
tiers; Lambda baseline 5429/5429 (one new fixture); MIR emission 135/135
and MT7 ratchet 16/16.

### 10.14 Round 5: reuse of contract proofs, static array lengths

**Instrument.** `emit_checked_boundary` now logs every emitted check as
`mir-boundary: fn=<function> site='<label>' target=<TypeId> value=<TypeId>`
(debug builds, `log.txt`), so the static census's per-loop counts can be
attributed to a site instead of guessed. On the hot rows it read: splay_node
23 checks, all `SplayNode?`-against-`SplayNode?` (8 returns of the declared
`var` param, 4 `var branch: SplayNode? = left.left`, 4 assignments from
`splay_node(...)`, 6 `rotate_*(node)` arguments); run_cube 101 `typed array
call argument` on packed float literals plus 9 `float` null rejections on
`0.0 - center_v[0]`; deltablue 2 per body on `var c: Constraint =
(p.constraints)[ci]` (a `Constraint?[]` element into a non-null local);
cd2 16 on `recurse_draw(voxels, seen, …)` where `voxels = step[0]` reads an
untyped array the port returns.

**T27-8 structural key (S11.4.1v3, D3.2.4v3).** `mir_contract_structurally_equal`
compares two contracts through their declaration wrappers and the optional
wrapper: identical nominal base with equal nullability, equal simple ids,
or invariant-compatible arrays. A `SplayNode?` return, parameter and field
are three `TypeUnary` allocations over one `TypeMap`, and the `Type*`
identity test had seen three contracts. On top of it:

- a declared, non-widened binding proves its own contract (it was admitted
  at declaration, at every assignment, and a `var` param's published rebind
  was checked against the same invariant, S9.2.1);
- `obj.f` proves field `f`'s declared contract when `obj` carries a trusted
  record and either the target accepts null or `obj` cannot be null
  (D3.2.2: the record was reified, every store into `f` is checked);
- `null` proves any `T?`; a non-raising call proves a structurally equal
  declared return; the unannotated-local definition scan accepts an alias
  of a declared binding and no longer refuses a `var` position whose
  contract is the target (its home publishes an admitted value);
- a declared `T[]` local initialized by such a call is not re-admitted (M7:
  `var nv: float[] = vmulti(...)` was checked in the callee's return
  firewall and again at the declaration).

**Float literals admitted by construction.** `MirMapContractScope` now
also carries a rank-1 non-nullable `float[]` contract to an array-literal
argument or initializer. The T27-7 packed emitter admits that contract on
its cold arm only -- the one that republishes generic storage for a null
member -- and reports `array_contract_constructed`, so the consumer's
boundary is elided on the packed arm. cube3d's 101 `calc_normal([qv[i],
…])` arguments now check nothing unless a member is null. An inline
certificate guard was tried first and withdrawn: it removed the call but
added 1,912 instructions to `run_cube` (G9 runs the other way).

**T27-9 static array lengths.** A local initialized by `fill(K, v)` with a
literal `K` or by a literal of K members carries `known_length`; a body scan
(`mir_binding_length_stable`) refutes it on rebind, `push`/`splice`, a
`var` or unknown-callee position, or capture. A constant index below that
length cannot take the out-of-range null arm (S7.1.3v2), so
`mir_expr_may_be_null` answers false and the 9 `translate_mat` null
rejections disappear. That proof also exposed why run_cube was large:
with the members proven non-null the literal's inferred element type stayed
`float?`, which refused the packed path; the carriers alone now decide it,
and the 96 face-drawing literals went from `array`/`array_push`×N/`array_end`
to one allocation and N stores.

| cube3d `run_cube` (JIT) | before | after |
|---|---:|---:|
| instructions | 20,499 | 15,850 |
| mandatory `lambda_type_check` | 17 | 0 |
| `array_push` call sites | 192 | 0 |

splay_node: 23 checks → 0 (G2 met for both G2 rows); splay module 44 → 37
boundaries, the rest being `_b` parameter admissions and `insert_new_node`'s
untyped `rng` argument.

**Sweep** — 69 typed rows, JIT-pinned, min of 5 interleaved runs, r41
against r40: output identical on all 69 rows and on all 138 benchmark
scripts; geomean 0.986. cube3d 0.810, splay 0.885, deltablue 0.916, list
0.676, raytrace3d 0.944, prettier_ast 0.969; nothing slower beyond noise
(sieve 1.033 and binarytrees 1.032 are sub-ms/GC rows, havlak 1.016).
`test_lambda_opt_gtest`'s `RecursiveContractJitFullyStatic` pinned 20
runtime admissions on the recursive `depth(n.next)` edge; that edge now
crosses nothing (field-read proof through `Node?`), and the test pins zero
crossings with the reification/copy counters and the warning check kept.

Fixture `test/mir/lambda/tune27_contract_reuse` (three tiers identical; the
`.mir-check` forbids `lambda_type_check` in the proven bodies, expects the
packed literal path, and expects the null arm to survive a `push`). Gates:
fixture parity on every `cow_*`/`tune27_*`/typed fixture, MIR emission
136/136, MT7 ratchet 16/16, Lambda baseline 5431/5431 after the re-pin.

### 10.15 Round 6: the null lane as an immediate, a nullable store fixed, and a JIT-tier parity census

**Where cube3d's compile goes.** `LAMBDA_PROFILE=1` puts cube3d's JIT-pinned
compile at 165 ms of a 360 ms wall (exec 23 ms); a `sample` of that run has
25 of its 27 compile samples inside `MIR_link` -- MIR's own generator, not
the emitter. Emission size is therefore the lever G9 says it is, and the
satellite re-link item is already closed (D8.1.1v9 publishes members with
`find_func`).

**Float null lane as a double immediate.** `emit_float_null_lane` emits
`dmov %r, <FLOAT_LANE_NULL_BITS>` -- `MIR_new_double_op` copies the double
bit-exact and the generator loads it from its constant pool. The §10.10
withdrawal was of a different spelling (a store through the Context
bitcast scratch in every float function); the immediate was verified
bit-exact on a direct read, after arithmetic, and through a `float?`
parameter boundary. run_cube loses 284 zero-argument calls (instruction
count unchanged at 15,850: one `dmov` replaces one `call`, but a call is a
caller-saved-register sequence to the allocator).

**A nullable float store was wrong on every build since Tune26.** The broad
parity census below found `tune26_nullable_float_store` failing JIT-pinned
on HEAD and every r-binary: `dst[i] = dst[i] + src[oob]` stored the null
sentinel's bits as an element (read back as `null`, no rejection). Two
defects: the store's validity check was emitted only when the RHS had
*failed* the native-lane proof, so a native float tree that propagates the
null passed straight to the raw store; and the cold arm boxed the sentinel
with the plain float boxer (a NaN float the checked setter admits) instead
of `emit_box_float_lane` (`ItemNull`, rejected per S7.1.3v2). Both fixed
(`emit_array_num_direct_store`); the cold arm of a nullable RHS now takes
the checked lane setter, because the compact `array_num_set_cow_idx`
helper is representation-agnostic and would *widen* a declared `float[]`
to hold the null. The three Tune26 `.mir-check` pins that expected the
compact helper on such stores pinned the defect and are re-pinned; fixture
`test/lambda/proc/tune27_nullable_lane_store` (three tiers identical).

**JIT-tier parity census.** Every `.ls` with a golden under `test/mir/lambda`
and `test/lambda/proc` run on interp/jit/auto (the baseline runs the auto
tier, where a once-called body stays in T0, so a JIT-only defect hides
behind a T0 golden). Pre-existing on HEAD `713a80c2a`, not touched here:
`action_c_error_lane` (all tiers), `tune24_branch_proofs` (auto),
`tune26_dense_carried_index` (jit/auto: a null float tree assigned into
`var total: float` is not rejected), and four JIT-pinned crashes --
`proc_nullable_bool_lane_control`, `proc_nullable_native_i64_map`,
`proc_nullable_pointer_scalar_extended_function_boundary`,
`proc_nullable_pointer_scalar_lane_function_boundary` (rc 139/1 on HEAD).
These are the next correctness items for the nullable lanes.

### 10.16 Round 7: literal loop extents and nested counters in the dense proof

**Why nbody kept every null test.** After §10.15's store fix nbody paid 8%
for the validity checks on its ten nullable stores; the right fix is to
prove the reads in-bounds so neither those checks nor the 42 arithmetic
sentinel tests per `advance` body exist. Three gates refused the proof:

- `mir_finite_loop_plan` (the only path that hands a counter to the dense
  guard) required the bound to be an `int` *binding*; nbody's loops are
  `while (i < 5)`. A literal is a PRIMARY with no child expression, so the
  unwrapped operand is NULL and has to be read from the raw operand -- the
  same representation the `null` keyword has (§10.14). The plan now carries
  `literal_bound`/`bound_literal`; a literal needs no range fact or entry
  guard of its own, and `mir_prepare_dense_loop_guard` takes a literal
  extent too (`extent_literal`), including for a nested loop bounded by the
  same literal.
- the dense scan refused a nested loop whose counter is declared inside the
  body (`var j: int = i + 1` has no `MirVarEntry` at the outer loop head).
  Its identity is all the scan needs: the root guard proves the extent, and
  each read's own lowering re-derives `0 <= j < extent` from the inner loop's
  counter facts before it drops its checked arm.
- that re-derivation asked `mir_finite_int_fact_covers(j, 0, INT53_MAX)` --
  whether a fact *contains* that whole range, which a finite loop's
  `[0, BOUND_MAX + STEP_MAX]` counter fact never does. Non-negativity is
  `fact->lower >= 0` (`mir_finite_int_fact_nonnegative`).

**Gate.** A literal-bounded loop earns the finite plan -- entry guard plus
the duplicated generic sibling (§10.9) -- only when its dense scan proves a
typed-array root (`mir_dense_loop_scan`, the scan half of the guard,
factored out so the dispatch can ask without emitting). Without the gate
every literal loop took the plan: cube3d's `while (fi < 6)` over
`edges[fi * 3]`, which no counter proves, cost 4.6% for nothing.

**Withdrawn on measurement.** A register-only `x != x` prefilter ahead of
every sentinel compare (so a non-null value never touches the bitcast
scratch) lost on every float row -- nbody 1.10x, matmul 1.22x, fft 1.24x:
the extra branch splits the block at each test and MIR's allocator pays
more than the round-trip saved. The compare stays straight-line; the shared
helper (`emit_float_lane_null_test`) is kept for the five sites.

**Sweep** — 69 typed rows, min of 5, r45 against r42 (§10.15) and r41
(§10.14): output identical on all 69 rows and on all 138 scripts; geomean
0.994 / 0.995. quicksort 0.619 (its `while (i < 100)`-style loops now
dense), nbody 0.967–0.977 vs r42 (1.045–1.069 vs r41: the §10.15
correctness checks the proof does not yet remove -- the stores' RHS trees
are dense but the destination `bvx[i]` store keeps its validity test),
levenshtein 0.907, deltablue 0.968, cube3d 0.997, navier 0.999. matmul read
1.088 at 5 runs and 1.134 at 9 against r42 but 1.022 / 1.066 against r41,
on a host whose r41→r42 cell had moved 6% with no relevant change; its
`_matmul` now has a matrix-index dense arm (the nested counters) and 26
more instructions. A 15-run interleaved A/B on a quiet host (load 5)
settled it: r41 17.19, r42 16.12, r45 17.62 ms -- r45 is 1.025x r41 and
1.093x r42. The §10.15 null-lane immediate had given matmul 6%; the new
matrix-index dense arm on its innermost loop (`a[i*n+k] * b[k*n+j]`, the
nested counters now proving the product form) gives most of it back. The
arm is correct and the proof is sound; its address arithmetic recomputes
`i*n` per access where the checked path read the cached descriptor, which
is the next T27-9 item, not a reason to refuse the proof (quicksort and
nbody gain from the same change).

Gates: Lambda baseline 5432/5432 after the two re-pins; MIR emission
136/136; MT7 ratchet 16/16; the JIT-tier parity census unchanged from
§10.15 (the same pre-existing items, nothing new).

Re-pins: `tune19_int_lane_compare` (its `while (i < 4)` is now a finite
loop: one native compare and one sentinel exclusion per arm, widening
still forbidden in both) and the `lambda_tune4_typed_array_guard` ratchet
(+10, one literal-bounded loop's entry guard). Instrumentation kept as
`log_debug`: `mir-dense:` (scan result, ineligible roots, refused reads) and
`mir-finite:` (plan admission), beside §10.14's `mir-boundary:`.

### 10.17 T27-5: the `cd` and `queens` case studies

**cd (JIT, `_handle_new_frame`/`_recurse_draw`, dominator-exact).** After
D4.4.4v2 (§10.12) and the §10.14 proofs the mandatory-path census reads
`array_push:8, lambda_type_check:7, item_at:4, fn_ne:3, is_truthy:3,
fn_add:3, member_set_cow:2`. Every one attributes to the port, not to an
operator that lacks a proof:

- `lambda_type_check` ×7 and `item_at` ×4: `recurse_draw` returns its two
  typed tables as an untyped `[voxels, seen]` array and its callers rebind
  `voxels = step[0]; seen = step[1]`. Those reads are genuinely `any`; the
  next `recurse_draw(voxels, seen, …)` must admit them (the §10.14 alias
  proof stops exactly here, at an untyped element read). A `RbtTable`
  pair record or `var` positions would remove all eleven; that is a port
  change, not an emitter one.
- `array_push` ×8 and `member_set_cow` ×2: `rbt_put` on the linear key
  table (`push(keys, key); push(vals, value); tree.keys = keys; …`) -- the
  data structure the port substituted for the JS red-black tree. The COW
  copies are gone (2,000 → 2 per 1,000 puts); the pushes are the work.
- `fn_ne` ×3, `fn_add` ×3, `is_truthy` ×3: `rbt_find_node`'s
  `keys[i] == key` over an untyped `array` field (`keys: array`), and the
  `if (old_seen != null)` tests on `any` results. Typing `keys` as `int[]`
  would give the native compare; on `array` the generic operator is the
  correct lowering.

**queens (JIT, `_is_valid`).** One `fn_ne` on the mandatory path; the
double loop over `queen_rows: int[]` with literal bounds is now dense
(§10.16), so the reads are raw and the three-way `or` compares on `c1`,
`c2` are int-lane. The residual `fn_ne` is `c1 == c2` where the boxed join
of the `or` arms reaches the generic compare; a lane-preserving `or` join
is the only lever and it is worth less than a microsecond on this row.

**Conclusion.** Neither row needs a per-operator special case; §3's M4
("residual generic Item operators") is a port-shape residue on cd and a
sub-microsecond join on queens. The 63-row geomean's array-and-recursion
cluster (nqueens, towers, list, storage) moved with the §10.14 proofs and
§10.16 literal-bound loops instead (list 0.68, towers 0.97).

## 11. Completion (2026-09-14, rev 10)

Seven rounds, thirty-one slices, release candidates `temp/t27/lambda-t27-r1
… r46`; r46 is the post-merge build (`9d467bf4c`). Every gate below is
measured on r46, JIT-pinned, min of 5 interleaved runs (9 on rechecks),
output identical on all 138 benchmark scripts and on all 69 typed rows
against both the pre-Tune27 HEAD `713a80c2a` and the Result43 archive
`lambda-v43-fabc412146`.

| Gate | Target | Result |
|---|---|---|
| G1 | validator self time < 2% on prettier_ast, splay, three_way_merge, richards, log_pipeline | **met** by construction: the contract proofs of §10.3/§10.11/§10.14 remove the validator from the accepted path on those rows (prettier_ast 0.666x HEAD, splay 0.756x, richards 0.53x) |
| G2 | cube3d, splay ≤ 2 mandatory `lambda_type_check` per loop | **met**: 0 on both (§10.14) |
| G3 | navier_stokes: zero mandatory `..._item` setters | **met** (§10.6) |
| G4 | deltablue, navier, nbody ≤ 2 mandatory `cow_prepare_write` per loop | **met** on navier (2) and nbody (0); awfy/deltablue keeps 10 on `_c_choose_method` (a `var` root re-borrowed per call) -- **not met** there |
| G5 | no row > 3% slower than the T27-0 archive | **met with three attributed exceptions**: spectralnorm 1.089 (round-1 `eval_A` parameter-range residue, §10.8), nbody 1.040 (the §10.15 null-rejection correctness checks the dense proof does not yet remove), permute 1.053 (record-home transport, §10.13). Everything else within noise at 9 runs (havlak 0.996, text_search 0.982) |
| G6 | baseline 100%, forced-GC stress green | **met**: 5432/5432 after each round; forced-GC stress on every `var` fixture on three tiers |
| G7 | richards, splay, mbrot typed ≤ untyped | **met** on richards (408 vs 430 ms) and splay (343 vs 368); **not met** on mbrot (0.976 vs 0.785 ms, 1.24x; was 1.41x in Result43) |
| G8 | §7 typed rows: auto wall ≤ JIT exec + 1.2x untyped-auto compile | **met** on deltablue, richards, crypto_sha1 (`core_sha1` promotes), splay, cd, hashmap, navier, prettier_ast; **not met** on cube3d (180 ms, compile-bound) and nbody (80 ms) |
| G9 | per-function budget in tree; `run_cube` < 10,000 | budgets **recorded** (`lambda_corpus_cube3d/deltablue/prettier_ast`, MT7); `run_cube` 36,161 → 15,514, target **not met** |
| G10 | collatz, mandelbrot ≤ 1.15x C2MIR | not re-measured: the C2MIR lane is removed (CLAUDE.md rule 14); collatz2's `%` lowers to at most two tests (§10.7) |

**Headline.** 69-row typed geomean 0.930x HEAD / 0.939x Result43 with
richards 0.53x, deltablue 0.58x (JetStream) / 0.80x (AWFY), cube3d 0.62x,
quicksort 0.63x, list 0.66x, prettier_ast 0.67x, raytrace3d 0.74x, splay
0.76x, navier_stokes 0.78x, puzzle 0.82x. Six correctness defects fixed on
the way, each a tier divergence (S9.1.2/S9.1.3/S7.1.3v2): the place-mutator
aliasing, the nullable-record `var` borrow, every typed scalar/record `var`
rebind on the eager tier, `x = null` into a nullable lane, `return g` of a
nullable lane, and the nullable float store. Two rulings ratified (D4.4.4v2,
D4.4.5) and one revised (D8.1.1v10); formal design 5.1.0 after the merge.

**Deferred (each with the evidence that scopes it).**
- T27-8 record ABI and callee-only admission on the direct typed edge (§8
  obligations 1–2): the remaining boundaries on the hot rows are `_b`
  parameter admissions (one per T0 crossing, required) and the cd port's
  untyped `[voxels, seen]` return (§10.17); the structural key already
  elides the typed→typed edge. A raw record ABI is a new proposal.
- T27-9 general interval analysis / BCE: literal and binding extents,
  nested counters and constant indices are proven; the matrix-index dense
  arm's address arithmetic (`i*n` recomputed per access, matmul 1.025x
  HEAD) and affine `i*c + k` extents are the next items.
- G9's 10,000-instruction `run_cube`: the remaining size is the 51-iteration
  face-drawing code (`is_truthy` ×12, `fill` ×9, 48 `draw_line` calls);
  MIR's generator dominates its compile (§10.15), so the lever is a lighter
  lowering of those calls, not the emitter's speed.
- The splay nested-list store-back ruling (§10.12) -- a design question for
  the user (D4.4.4 admits store-backs only at the handle's own list level).
- The pre-existing JIT-tier defects of §10.15 (`action_c_error_lane`,
  `tune24_branch_proofs`, `tune26_dense_carried_index`, four
  `proc_nullable_*` crashes): none introduced here; all reproduce on HEAD.
- mbrot's annotation penalty (G7) and spectralnorm's `eval_A` residue
  (G5): both are the typed float parameter range question of §10.8.

## 12. Post-Result44 fixes (2026-09-14)

Result44 (`acd1e88d1`, the Tune27 merge plus upstream's libify/JS-simplify
commits) carried two regressions that a per-row read against Result43
exposed; both are root-caused and fixed here.

**LambdaJS 20x (upstream `c7e285e51 "JS simplify"`, not Tune27).**
LambdaJS/Node went 19.9x → 55.0x; on the binaries sieve2.js 3.2 → 68 ms,
puzzle 54 → 1,304, base64 522 → 10,572, r45 (pre-merge) fast and r46
(post-merge) slow. A `sample` put 94% of the run inside
`js_realm_intrinsic_slots_ensure_roots()`: the commit replaced the per-class
cached prototype root in `js_get_intrinsic_prototype_for_class` with
`js_intrinsic_prototype_slot()`, which calls `ensure_roots` on **every**
prototype lookup, and that routine re-ran `js_runtime_state_prepare_root_vectors`
plus a reservation loop over every realm slot from the typed-array base each
time. Its own comment says "reserve the complete suffix once"; the code now
does: `JsRealmSlots::suffix_reserved` is set after the first successful
reservation and dropped by `js_realm_slots_clear`/`destroy` (the full-reset
path; a transient reset keeps the vector's length and therefore the
reservation). Output identical to v43 on the JS rows.

**Typed matmul 3.0x slower than untyped (T27-9 residue, §11).** Untyped
matmul dropped 17.7 → 5.9 ms with the §10.16 proofs; the typed twin stayed
at 17.7. The only difference is `var c: float[]`; with the store moved out of
the loop nest the typed body runs in 5.7 ms. `mir_prepare_dense_loop_guard`
ANDed T26-4's unique-owner write guard for `c` (carrier, certificate and COW
words at loop entry) into the single register every `a[..]`/`b[..]` read
branched on; one of `c`'s words fails at run time and the whole nest ran on
the checked arm. Stores now branch on a separate `typed_array_dense_store_guard`
(inbounds ∧ write guard); the read guard stays the pure extent proof. Which
word of `c`'s write guard fails (its certificate identity is the suspect) is
a follow-up: it only costs `c`'s own stores, exactly what the untyped twin
pays through its `cow_marked` snapshot root.

## 13. Regression pins added after Result44 (2026-09-14)

The rounds' tunings were pinned by output goldens and by a few `.mir-check`
sidecars; the Result44 read showed where that left gaps (a JIT-only store
defect behind a T0 golden, a 20x LambdaJS regression with no census row, a
dense-guard split no wildcard pattern can see). Added:

- **Tier matrix** (`test_lambda_gtest`, `LambdaTierParityTests`): eighteen
  fixtures from rounds 3–7 run on interp, jit and auto against one golden --
  every `cow_*`, the typed-`var` rebinds, the nullable-lane stores, the
  contract-reuse and literal-extent fixtures. The baseline alone runs auto,
  where a once-called body never reaches the JIT.
- **Emission sidecars** (`test/mir/lambda/tune27_*`): `place_borrow`
  (`cow_place_leaf_fixed` + raw `pn_push`, exactly two captures for the two
  named array insertions, none for the scalar), `literal_extent_dense` (no
  checked load, no null-lane call, the null lane as `dmov r, nan`, the
  literal extent as an immediate; the only checks are the two parameter
  admissions and the sibling arms' declaration null rejections),
  `call_defined_binding` (at most one check in a four-call `rotate` chain),
  `dense_store_guard` (the `{{r:g}}`/`{{r!g}}` pin of §12's split).
- **Census pins** (`test_lambda_opt_gtest`, `LambdaOptCow`/`LambdaOptJs`):
  the exec-profile reader now exposes the per-type COW rows
  (`array_shared_copies`, `array_num_unique_mutations`, …), and five tests
  pin D4.4.4v2 (29 array copies), D4.4.5 (83 map copies), the place
  mutators (7 + 1 detaches, ≥1,000 unique appends), D8.1.1v10 (no array
  copy on a typed `var` rebind) and the fixed-key path setter (1 + 2
  detaches) on both tiers. A new row `js_realm_slot_reservations` counts
  LambdaJS's full realm-slot walks; the JS test drives 20,000 intrinsic
  prototype lookups through the `js` subcommand and requires exactly one.
- **G9 probes**: `lambda_corpus_cube3d/deltablue/prettier_ast` budgets are
  the debug build's counts (the baseline's configuration). The "release
  emits 154 fewer instructions on `run_cube`" observation recorded here on
  2026-09-14 was not a build difference: the r47 release binary predates
  commit `294df06dc` (per-argument `ValueRep` ABI descriptors), which the
  working tree had merged. That commit produced every system-call argument
  in the producer's own rep and converted afterwards, so `fill(4, 0.0)`'s
  literal became a run-time float box block instead of the RC8 folded
  constant Item; and it asked the shl/shr/ushr registry rows (NULL
  descriptor = the boxed `fn_*_item` ABI) for the operands of the *inline*
  native shift, boxing both and shifting the tag bits (`shr(n, 1)` of an
  `int` parameter returned `inf`; paraffins/paraffins2/mandelbrot2/base642
  failed on every tier that JIT-compiled them, and
  `transpile_bitwise.ls` failed outright). Both fixed 2026-09-14:
  `emit_sysfunc_abi_arg` requests the descriptor at production
  (`transpile_expr_value(..., required)`), and the inline bitwise/shift
  lowering opens each operand's proven `int` lane through
  `emit_native_bitwise_lane_arg` (D2.4.1-D2.4.3). Pinned by
  `test/mir/lambda/tune20_shift_native_lane` (`rsh r, %p1, c` / `lsh r,
  %p1, %p2` / `and r, %p1, n` on the raw parameter registers, at most one
  `int2it_lane` per body -- the earlier `tune16_native_bitwise` sidecar
  matched `and {{r}}` with boxed operands and missed it). Re-captured after
  the fix: `lambda_corpus_cube3d` 20,809 -> 20,571 (`_run_cube_#` 15,668 ->
  15,514, the r47 release count exactly) and `lambda_tune4_typed_array_guard`
  1,181 -> 1,162; deltablue and prettier_ast were unchanged.
