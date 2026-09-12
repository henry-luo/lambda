# Tune27: stop re-proving typed contracts per value

- **Date:** 2026-09-13.
- **Status:** **PROPOSED — evidence collected, no implementation.** §2 and §3
  are measured; §4 states tracks and exit evidence, not results.
- **User requirement:** the slow typed rows must stop paying a per-value
  contract re-proof. A declared annotation should establish its proof once at
  the boundary that reifies it, and hot code should consume that proof.
- **Predecessors:** [Tune22](<Lambda_Impl_Tune22 (done).md>),
  [Tune25](<Lambda_Impl_Tune25 (done).md>), [Tune26](Lambda_Impl_Tune26.md).
  Tune26 attacked the *array* admission scan; Tune27 attacks what remains on
  the mandatory per-iteration path once that scan is gone.
- **Authority:** [formal design](../../doc/Lambda_Formal_Design.md) **D3.2.1**
  (three distinct subtype operations), **D3.2.2** (the validator is the runtime
  enforcer, deep, **on first crossing**), **D3.2.4v3** (a named map crossing is
  a reification; elide only on a proven physical layout, reached through the
  non-null arm), **D3.3.4** (representation follows the full inferred
  contract), **D4.4.2/D4.4.4** (one-level COW, RMW place borrow),
  **D5.3.4** (precise roots), **D8.2.6**, **D8.4.3v2**;
  [formal semantics](../../doc/Lambda_Formal_Semantics.md) **S11.4.1v3** (an
  implementation may reuse an admitted representation proof only while the
  actual carrier matches it), **S7.1.3v2** (checked writes), **S9.1.2**.
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

**Problem:** M2. `cd`, `splay` and deltablue are outside D4.4.4's shape.

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

G5 and G6 are non-negotiable; a track that meets its own gate and breaks G5
does not land.

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

**Dynamic — `sample <binary> <secs> 1 -wait -f out`,** self time aggregated
under the `run_script_mir` subtree; JIT frames appear as `???`, so named frames
are exactly the non-native lane. Unreliable below ~100 ms of workload — the
attach races the process — so hashmap, nbody, crypto_sha1, matmul and primes
rest on the static census only. `COW_EXEC_PROFILE=1` with
`COW_EXEC_PROFILE_OUT=<file>` supplies the admission and COW counters; verify
the binary actually emits the counter you intend to read before quoting it.

## 7. Frozen row set

The fourteen rows of §2, plus the four out-of-scope native rows carried as
regression guards (`triangl`, `text_search`, `primes`, `matmul`). Each entry
freezes: suite, benchmark, typed source path and hash, transitive imports,
input files, expected output, and the T27-0 baseline timing and census.

## 8. Status ledger

| Track | Mechanism | Status |
|---|---|---|
| T27-0 | census refresh + M1 counter attribution | not started |
| T27-1 | memoize contract proof incl. disproof | not started |
| T27-2 | elide identical-contract re-crossings | not started |
| T27-3 | native stores for `var T[]` parameters | not started |
| T27-4 | RMW place borrow for uncovered rows | not started |
| T27-5 | `cd` generic-operator case study | blocked on T27-1..4 |
