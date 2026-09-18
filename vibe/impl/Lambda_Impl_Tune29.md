# Lambda Impl Proposal: Tune29 — Handles, Facts, and the Structural Gap to C2MIR

- **Date:** 2026-09-17
- **Status:** CLOSED as a round 2026-09-17; the three §19.1 follow-ups
  were then implemented (§20: cd2 typed, field-place array admission, handle
  and flow-join work, with one more JIT alias defect fixed, LR12-17). Landed: P0 (§11.1), T29-2 (§11), T29-1 one-level handles
  (§12), T29-3 null split and per-operand tests (§13), T29-4 (§13), T29-5
  items 1/3/4 (§14), T29-7b frame trimming (§16.7), boxed-scalar admission
  (§18). Already in place before the round: T29-6 (§15). Tried and reverted:
  T29-7a (§16.6). Closed without implementation: T29-3 finiteness fact,
  T29-5 item 2, T29-7c (§19.2). The time profile that decided the disposition
  is §17. Two pre-existing JIT correctness defects were found and fixed on
  the way (§11.2, §11.3); three more were ledgered (LR12-14..16).
- **Formal authority:** **D4.4.4v4** (a handle carries four facts, valid while
  the place is unchanged; synthesized handles), **D3.2.4v4** (the layout proof
  is transitive through a verified carrier), **D3.2.6 / S11.4.10** (verified
  once, valid while unchanged; a required field is a required field; native
  lane layout is the ABI), D2.5.3 (flow-sensitive use of an index read's
  payload), D4.3.1 (headers never move; the data zone compacts), S4.1.2 /
  S4.2.3 (int saturation; nan is unequal), S9.1.2 / S9.1.3 / S9.2.2 (values
  never alias; plain params snapshot; a mutable borrow un-shares first),
  S11.4.1v3 (proof reuse only while the carrier matches), D5 (precise
  rooting through the side stack).
- **Vibe authority:** `Lambda_Design_Runtime_COW.md` §11.14 (CW37) and
  Appendix D; `Lambda_Design_Type_Enforcement.md` TE-19;
  `impl/Lambda_Impl_Tune28.md` §9.6 (why the certificate hoist failed),
  §9.7 (null-sentinel receiver arm), §9.17 (emission diet and its residue).
- **ID series:** `T29-#` tracks. Mechanism names reuse Tune28's N1–N5 where
  the same mechanism is meant.

## 1. Objective

Result46 measures MIR (typed) at **4.26x C2MIR** geomean over 63 rows and
0.75x Node. Tune28 took the emission-diet levers (module constants, band
proofs, null-receiver arms, index proofs, `split`) and left the residue it
could not reach by trimming: per-access record and array layout proofs,
COW ownership tests on every write, and the calling convention. This round
is about that residue. It is built on four rulings ratified 2026-09-17 that
turn per-access re-proof into per-handle proof, and it adds the three
implementation-only levers the same evidence exposed (typed local stores,
certified construction, in-place string append).

Gate for the round: **typed/C2MIR ≤ 2.5x** on the 63-row population, with
the two port artefacts of §2.3 repaired first so the geomean measures the
compiler; deltablue ≤ 8x, cube3d ≤ 6x, base64 ≤ 6x; typed ≤ untyped on every
row; outputs identical on all benchmark scripts across interp/jit/auto.

## 2. Result46 read

### 2.1 The run is load-inflated

Every engine slowed against Result44: v46/v44 geomean untyped 1.13, typed
1.08, **C2MIR 1.12, Node 1.24**, QuickJS 1.17. The same C code and the same
Node got slower, so the machine was loaded; the JetStream and late Text rows,
run last, sit at 1.00. Absolute v46 numbers are not comparable across runs.
Same-run ratios (typed/untyped, typed/C2MIR) are valid. Tune28's wins are
visible through the noise: matmul 0.39, three_way_merge 0.62, queens 0.64,
log_pipeline 0.72, nbody 0.75, crypto_sha1 0.85.

### 2.2 Rows where typed is slower than untyped

Five are structural; the rest are within the same-run noise band and were at
parity in Result44. Each was proven by an A/B variant (`temp/r46/var/`,
additive from the untyped file — stripping downward hits E207):

| Row | typed/untyped | one change that explains it | measured |
|---|---:|---|---|
| bounce | 1.50 | `var bx: int[] = fill(..)` on 4 locals | 0.064 → 0.134 ms |
| nqueens | 1.25 | `int[]` on parameters | params-only 2.13, scalars-only 1.25, untyped 1.32 ms |
| mbrot | 1.37 | matrix built by `[for .. fill(n,0)]` | comprehension 0.838, loop-built 0.621 ms |
| fasta | 1.23 | `var seed_arr: int[]` param | typed 0.812, untyped 0.684 ms |
| sieve | 1.16 | `bool[]` admission walk | one 5,000-element walk, ~3 µs |

Mechanisms: (bounce) a typed local store whose value is not statically a
native int is lowered as an unconditional `lambda_array_set_checked_lane`
call (`transpile-mir.cpp:30457` `native_write_witness`), with no inline tag
test in front of it; (nqueens) `runtime_type_admit_array_env`
(`lambda-eval.cpp:10610`) walks a plain `fill()` array on every call because
it carries no certificate; (mbrot) the comprehension build routes every
nested write through `fn_index → cow_prepare_write → cow_path_set_raw`
(5,625 unique-mutation tests in the census, none for the loop build; the
mechanism is not yet resolved); (fasta) the out-of-range null arm of
`seed_arr[0]` infects the following `*`, `+`, `%` with two sentinel compares
and a null arm each; (sieve) `ensure_typed_array`
(`lambda-data-runtime.cpp:3533`) has no bool lane.

### 2.3 Two headline rows are port artefacts

- **r7rs/nqueens** — the C port is a fixed `int rows[8]` nested loop with no
  allocation; the `.ls` mirrors the Scheme list-building version (a `fill()`
  per `solve` call). The 14.7x row compares two algorithms.
- **awfy/cd** — `find_intersection(m1: array, m2: array) any` is untyped in
  the *typed* file: 74 of its operations are generic `fn_mul`/`fn_sub`/`fn_add`
  calls. richards has the same `tasks: array` substitution (Tune28 §6
  deferred list).

Both are repaired in P0 (§6) before any track is measured against them.

## 3. The MIR side by side

### 3.1 Static volume tracks timing

`lambda/mir/c2m -Dmain=c2mir_bench_body -S <port>.c` writes the C port's
MIR; `classify.py` buckets each instruction of a function by mechanism.

| Hot function | Lambda typed | c2m | static ratio | timing ratio |
|---|---:|---:|---:|---:|
| deltablue `c_choose_method` | 1,997 | 57 | 35x | 31.7x |
| cube3d `run_cube` | 9,655 | 531 | 18x | 16.7x |
| base64 `encode` | 1,125 | 158 | 7x | 16.5x |
| cd (module) | 15,036 | 1,367 | 11x | 37x |

Buckets of the Lambda excess: spill/reload around calls 18–26%; branch plus
label fragmentation from guard arms 25–38%; int53 band tests 2–8%;
null-sentinel arms 3–6%; box/unbox 4–7%; certificate/layout guards up to 3%.
`c_choose_method` declares 938 MIR locals; MIR's register allocator and the
instruction cache pay for the volume a second time.

### 3.2 Cost per source operation, fast arm only

Counted in the listings (`temp/r46/deltablue2.mir`, `nqueens2.mir`,
`cube3d2.mir`, `base642.mir` against `c2m/*.mir`); cold arms excluded.

| Source operation | Lambda | C | what the instructions are |
|---|---:|---:|---|
| `w.cons[cid].kind` | ~38 | 1 | unbox `w`; `cons` slot load + zero-slot null arm; array null; certificate load; kind mask `1535`/`1281`; leaf `Type*` identity; lane `262`; length; two bounds tests; index; slot null arm; unbox element; data pointer; field load |
| `w.cons[cid].satisfied = 1` | ~42 | 1 | the above, plus a COW shared-bit test (`u8:4 & 1`) on `w`, on `cons` and on the element, two shape-identity compares, then `lambda_map_path_set_checked_fixed` on the cold arm |
| one `ok()` iteration (all `int`, tail call lowered to a loop) | ~50 | ~12 | band-test pair per `+`/`-` with slow-call arm; two `ne x, INT_LANE_NAN` per `==` (S4.2.3); TCO depth counter; bounds pair per read |
| `var r: float[] = fill(4, 0.0)` | 2 calls + heap | 0 | box `4`, box `0.0`, `fn_fill`, `lambda_array_admit_numeric_contract`; `run_cube` performs 102 `array_float_new` per call where C has one `alloca` |
| a call | ~14 spill/reload + probe + witness | 1 | every live Item spilled to the side stack and reloaded; `_array_witness` bit tests; `lambda_type_check` per unwitnessed array/record argument; boxed Item return |
| `result ++ chunk` per 3 input bytes | 1 allocation + copy | 4 stores | `fn_join`; 11 join sites in `b64_encode` |

Every access to `w.cons[cid]` re-navigates from `w` because a record has no
interior handle: C hoists `Constraint* c` once. That single fact is the
deltablue/havlak/richards/splay/cd family.

## 4. Mechanisms

- **N6 — no interior handle.** Value semantics (S9.1.2) surfaces per access:
  identity, layout, presence and ownership are all re-proven at every
  spelling of the same path. Tune28 §9.6 tried to hoist the certificate
  guard and found the proof is carried in a flag after a join, so nothing
  can CSE it. D4.4.4v4 makes the proof a property of a handle instead.
- **N7 — presence re-proof.** Every field read of an admitted record tests
  the packed slot for a zero word and materializes `ItemNull`
  (`transpile-mir.cpp:20838` `skip_null_guard = false` and the empty-slot arm
  in `emit_mir_direct_field_read` at :20861). Tune28 §9.17 left it because no
  ruling said a required field is present. D3.2.6 now does.
- **N8 — layout re-proof through a verified carrier.** Element reads compare
  the element's shape pointer against a baked `TypeMap*`, and every typed
  store re-runs `emit_array_rep_cert_guard` (:22104). D3.2.4v4 rules the proof
  transitive through the carrier.
- **N9 — nullable and finite lanes.** An unproven index read infers `T?`
  (D2.5.3) and the null arm follows the value into arithmetic; `==` on an
  `int` lane excludes nan on both operands (S4.2.3) even when both are
  provably finite; band tests (S4.1.2) run on every `+`/`-` even when an
  interval is proven (`mir_int_lane_interval` :11244 knows literals and
  loop extents only).
- **N10 — construction without a certificate.** `fill()` and literals build a
  generic carrier, then a separate admission call certifies it; `bool[]` has
  no lane at all. Freshly built values that only escape by return are still
  heap-allocated and boxed across the return.
- **N11 — the calling convention.** Precise rooting through the side stack
  (D5) spills every live Item around every call, plus a probe, witness tests
  and boundary checks. This is the one mechanism this round does not touch
  (T29-7 is a design track, not an implementation track).
- **N12 — immutable strings.** `result ++ x` allocates and copies on every
  append even when `result` is COW-unique.

## 5. What the rulings license

| Ruling | What it removes | Track |
|---|---|---|
| D4.4.4v4: a handle carries identity, layout, presence, uniqueness, valid while the place and its ownership are unchanged; compiler-synthesized handles | re-navigation per access; per-write COW tests on `w`, `cons`, element | T29-1 |
| D3.2.6 / S11.4.10: verified once, valid while unchanged; required field present; lane layout is the ABI | empty-slot arm and `ItemNull` materialization on required fields; `bool[]` element walks | T29-2, T29-5 |
| D3.2.4v4 corollary: proof transitive through a verified carrier | per-element shape-identity compare; certificate re-check on typed stores | T29-2 |
| D2.5.3 (existing): flow-sensitive payload use | null arm carried through arithmetic after an index read | T29-3 |
| design notes (no ruling): finiteness fact; inline tag test before a checked store; in-place append on unique strings | nan tests on proven-finite operands; unconditional checked-store calls; per-append allocation | T29-3, T29-4, T29-6 |

The rulings fix *what may be assumed*; the concrete invalidation table is an
implementation record (COW record Appendix D) and may change under the
principle.

## 6. Tracks

Ordered by dependency, then by gain. Every track keeps the standing gates:
`make test-lambda-baseline` at its current pass set, all benchmark outputs
byte-identical across interp/jit/auto, the MT7 ratchet
(`test/test_mir_ratchet_gtest.cpp`), the tier-matrix gtest, and forced GC
(`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`) plus
`LAMBDA_ROOT_WITNESS=1` on every fixture a track adds (rule 15).

### P0 — Port hygiene (no language change)

Re-port `r7rs/c2mir/nqueens.c` to the `.ls` algorithm (candidate lists
built per call), or replace both ports with one algorithm and regenerate the
golden. Type `awfy/cd2.ls` `find_intersection` (`Vec3`/`Motion` records or
`float[]` parameters) and `awfy/richards2.ls` `tasks`; keep the workload and
the goldens. Re-run those rows on the v46 binary before any track is
measured, so the geomean gate of §1 measures the compiler.

### T29-2 — Required fields present; proof transitive through the carrier (N7, N8)

Do this first: T29-1's handles rely on presence and layout being facts.

1. **Drop the empty-slot arm on required fields.** In
   `emit_mir_direct_field_read` (`transpile-mir.cpp:20861`) a read of a
   non-optional field from an admitted receiver reads the slot by offset;
   the zero-word test and `ItemNull` materialization stay only for `T?`
   fields and for receivers that are not admitted. Retire the hardcoded
   `skip_null_guard = false` (:20838) in favour of the admission fact
   (`mir_receiver_binding_non_null` :20840 already carries it for a named
   binding; extend it to any value reached through an admitted receiver —
   the chained-read hazard Tune28 §9.7 named is exactly what D3.2.6 closes,
   because an admitted `a.b` is present).
2. **Drop the per-element shape-identity compare.** An element read from a
   certified `T[]` (array certificate proven once at admission) carries `T`'s
   declared-prefix layout; the `bne …, <TypeMap*>` after the element load
   and the `is_trusted_contract` recheck (:3760, :5930) go. Open extension
   (S2.1.4, OB16) never disturbs the prefix, so offset reads remain valid on
   a grown instance.
3. **Certificate guard once per carrier fact, not per store.** The typed
   store path (:30457 onward) and `emit_array_rep_cert_guard` (:22104) take a
   carrier-fact argument; a carrier admitted in the body, or reached through
   a handle (T29-1), stores by offset. Writes into the carrier still admit
   their *value* (S11.4.1v3); what disappears is re-proving the *owner*.
4. **Runtime writers honour the ABI.** `lambda_map_path_set_checked_fixed`
   (`lambda-eval.cpp:9374`) and `item_at` (`lambda-data-runtime.cpp:3078`)
   must never publish a required field as a zero word; audit the partial-
   construction paths (record literals, `fill`, host `MarkBuilder` entry) so
   that a value acquires its layout proof only when every required field is
   written. Host-built values stay outside D3.2.6 until they admit.

- Pilot: deltablue2 `c_choose_method` 1,997 → ≤ 1,300 insns (107 remaining
  `ItemNull` materializations, 76 guard instructions); cube3d2 `run_cube`
  9,655 → ≤ 7,500.
- Gate: fixture `tune29_required_field_read` (record with required and
  optional fields; optional keeps its arm, required has none; grown instance
  reads through the prefix); `tune29_carrier_proof` (certified `T[]` element
  read with no `TypeMap*` compare); probe of a *non-admitted* receiver
  keeps both arms.
- Risk: medium. A wrong presence assumption is a malformed Item read
  (D3.2.4). Item 4 is the safety net and lands before items 1–3.

### T29-1 — Handles that carry facts (N6; D4.4.4v4, CW37)

1. **Static shape.** Extend `lambda_ast_lower_rmw_borrows`
   (`build_ast.cpp:11286`) and `RmwBorrowCtx` so that, besides the named
   `var h = root.path` binds of CW34–CW36, a path spelled more than once in
   one statement list on a `var` root is lowered to a synthesized handle
   bound at the first spelling. The kill set is the COW record's Appendix D
   table, applied conservatively: any statement not in the keep rows kills.
   Loops: a handle bound outside a loop dies at the back edge if any
   iteration contains a killing event; a handle synthesized inside the body
   is bound afresh each iteration. Decided once in
   `lambda_ast_finalize_script` (:7815), shared by both tiers; T0 keeps
   per-access navigation (observably identical).
2. **Bind.** `cow_bind_rmw_handle` (`lambda-eval.cpp:8691`) runs the spine
   test and, for a *writing* handle, un-shares a shared leaf into its place
   (S9.2.2) so no later write through any aliasing spelling can detach. The
   handle register holds the leaf's **header** pointer only (D4.3.1); packed
   field and array buffer pointers are reloaded after every allocation
   point.
3. **Access.** Field reads and writes through the handle are offset loads
   and stores under T29-2's facts; a write through a handle whose
   uniqueness fact died runs one shared-bit test on the header and
   detaches with store-back (v3's obligation, per handle).
4. **Kill events.** Root or prefix passed as `var`: kill both facts. Handle
   passed as `var`: reload the pointer from the handle's home after the call
   (CW33 address-of-home), kill uniqueness. Plain pass, place copy, return,
   store or capture of root, prefix or handle: kill uniqueness only.
   Write to `P` or a strict prefix through another spelling, growth or
   reorder on a prefix, root rebind: kill both.

- Pilot: deltablue2 `c_choose_method` → ≤ 600 insns (five `w.cons[cid]`
  spellings per branch become one navigation); havlak2, richards2 (3.5M COW
  ownership tests in the Result45 census), splay2 (683k map copies after
  CW36; the rotation residue is a call-site move convention and stays open).
- Gate: fixtures `tune29_handle_synth` (repeated spelling, one navigation in
  the `.mir-check`), `tune29_handle_kill` (one probe per Appendix D row,
  golden shows the re-navigation or the copy), `tune29_handle_alias`
  (`w.cons[i]` and `w.cons[j]` with `i == j` at runtime, write through one,
  read through the other, both tiers), all under forced GC and
  `LAMBDA_ROOT_WITNESS=1`; `COW_EXEC_PROFILE` unique-mutation tests on
  richards2 −80%.
- Risk: high, and the reason the kill set is ratified rather than inferred.
  Default is kill; a keep row needs a fixture.

### T29-3 — Non-null and finite lane facts (N9)

1. **Split the null arm at the read** (D2.5.3). `a[i]` with an unproven index
   branches once: the in-bounds arm feeds a plain `T` lane into the rest of
   the expression, the miss arm materializes null and joins at the
   expression's result. Today the `T?` lane is threaded through every
   operator with a sentinel test each (fasta2's `* IA`, `+ IC`, `% IM`).
2. **Finiteness fact.** A value is proven finite when it is a literal, a loop
   counter with a proven interval, a `len`, an in-bounds index, or the fast
   arm result of in-band `+ - *`; only `/`, `%` by an unproven divisor,
   ingestion and explicit poison produce nan/inf. `mir_int_lane_interval`
   (:11244) gains a finite bit; `INT_LANE_NAN` exclusion (:11787, :11848) and
   the band-test pair are skipped when both operands carry it. S4.2.3 and
   S4.1.2 are unchanged: this is proof, not semantics.

- Pilot: fasta2 typed ≤ untyped (0.81 → ≤ 0.68 ms); nqueens2 `ok()` 179 →
  ≤ 100 insns; collatz, mandelbrot, pnpoly (band-test share 5–8%).
- Gate: fixture `tune29_finite_lane` (a proven-finite compare has no
  `INT_LANE_NAN` test; a value from `/` keeps it; a nan literal still
  compares unequal to itself on both tiers); `tune29_index_null_split`
  (out-of-range read still yields null end-to-end, S7.1.1v3).
- Risk: low–medium. Every skipped test must have a producer-side proof; the
  fixture enumerates the producers.

### T29-4 — Typed local stores: tag test first, checked call cold (N-bounce)

`native_write_witness` (:30457) requires the value statically proven native.
When it is not, emit the same shape the `var`-parameter path already uses
(`_random_next_68` in `temp/r46/bounce2.mir`): an in-band tag test on the
boxed value, a direct lane store on the hit, and `emit_checked_array_store`
(:28594) only on the miss. `mir_int_array_store_value_proven` (:11205) stays
as the static fast path.

- Pilot: bounce2 0.094 → ≤ 0.064 ms (untyped); every `T[]` local in the
  corpus (cube3d2 25, nbody2 7, nqueens2 6, fasta2 6, hashmap2 5, bounce2 5).
- Gate: `tune29_local_store_tag_test` (`bx[i] = f(x) % 500` shows one band
  test and a direct store; the checked helper appears only in a cold arm);
  outputs identical; a non-int value at runtime still raises E207 at the
  store.
- Risk: low.

### T29-5 — Certified construction and native `bool[]` (N10)

1. `fill(N, v)` and a scalar literal array, when the destination contract is
   known (`var r: float[] = fill(4, 0.0)`, a typed parameter, a typed field),
   allocate the certified lane directly (`fn_fill`, `lambda-vector.cpp:1654`,
   gains a contract argument; `runtime_array_admit_primitive_contract`
   :9450 is then a pointer compare). Two calls and two boxings become one
   call.
2. **Escape-analysed return-only locals** (`var r: T[] = fill(..); … ;
   return r`): allocate in caller-provided storage when the caller's
   destination is a known-shape local, the same shape as the region-producer
   path Tune-COW gave to self-recursive map builders
   (`mir_region_producer_candidate`); the `var`/`let` in the body must not
   disqualify the function as it does today (typed-benchmark-annotation-rules,
   2026-07-29).
3. **`bool[]` lane** in `ensure_typed_array` (`lambda-data-runtime.cpp:3533`)
   so admission is O(1) and `flags[k] = false` is a byte store.
4. **Comprehension build** (mbrot): resolve why `[for .. fill(n,0)]` rows
   take `cow_prepare_write` on every nested write while loop-built rows do
   not (census: `array[num] 0 5625 0 0` vs no row), and make the fresh
   comprehension result own its children (S9.3.1 already says construction
   captures by value; the result is unique).

- Pilot: cube3d2 `run_cube` 102 `array_float_new` → ≤ 20; nqueens2 with
  `int[]` parameters ≤ scalars-only (1.25 ms); sieve2 ≤ sieve; brainfuck2
  (`fn_fill` 71% of samples in Result45); mbrot2 ≤ 0.62 ms.
- Gate: `tune29_certified_fill` (one call, certificate installed, admission
  is a pointer compare in the `.mir-check`); `tune29_bool_lane`;
  `tune29_comprehension_owner` (COW census shows no per-write test).
- Risk: medium for item 2 (lifetime of caller storage across an error exit
  must follow S7.7's declaration-boundary skip); low for the rest.

### T29-6 — In-place append on COW-unique strings (N12)

`result = result ++ x` where `result` is a `var` local whose value is
COW-unique appends into the existing buffer (amortized growth) instead of
allocating a new string; the operation is unobservable under S9.1.2 for
exactly the reason a unique array's `push` is. `fn_join` keeps its allocating
path for shared or `let`-bound sources. The static shape is the assignment
whose RHS is `lhs ++ e` with `lhs` the assigned name; the runtime test is the
shared bit on the string header (strings are containers in the object zone,
D4.3.2v2).

- Pilot: base642 `b64_encode` (11 join sites) 10.6 → ≤ 4 ms; hyphen2,
  microdiff2, prettier_ast2, three_way_merge2, log_pipeline2.
- Gate: `tune29_string_append_unique` (unique: no allocation per append in
  the `COW_EXEC_PROFILE` string rows; `let` alias: copy); outputs identical;
  `LAMBDA_GC_FORCE_EVERY=1` on the growth path.
- Risk: low–medium (string interning and the name pool must never see an
  in-place-grown string: only heap strings owned by a `var` qualify).

### T29-7 — Stack-map rooting (N11) — design track only

Spill only pointer-typed live values, only at call sites that can allocate,
using a per-call-site map instead of spilling every live Item to the side
stack. This is a D5 change and the largest engineering item in the residue;
it is gated on T29-1..T29-6 landing first, because handles and finite lanes
remove most of the *Items* that are live across calls today (a handle is one
header pointer; a finite int is not a root). Deliverable for this round: a
design record with the call-site census after T29-1/T29-3, not code.

## 7. Measurement procedure

- Reference binary for every A/B: `test/benchmark/exe/lambda-v46-9697f43375`.
  Build the control from the **same HEAD** as the change (Tune28 §9.5).
  Never time a debug build; `make test-lambda-baseline` overwrites
  `lambda.exe` and `make release` deletes `test/*.exe`.
- One benchmark process at a time; `LAMBDA_TIER=jit`; interleaved min of 7
  (`temp/r46/ab.py`, `N=9` for any sub-3% call). Check the C2MIR column moves
  < 5% between runs before reading any row (§2.1).
- Static census per track: `LAMBDA_MIR_DUMP_PATH`, then
  `temp/r46/classify.py <dump> <func>` on the pilot's hot function and
  `mircensus.py` on the module. The side-by-side reference is
  `temp/r46/c2m/<row>.mir` from `lambda/mir/c2m -S`. The per-track numbers
  are instruction counts on the fast arm of one source operation (§3.2), not
  module totals.
- Dynamic census: `COW_EXEC_PROFILE=1 COW_EXEC_PROFILE_OUT=<tsv>` for
  unique-mutation, copy and string rows. `sample` returns zero samples on
  sub-second rows even with `-wait`; use the A/B variants instead.
- Correctness: outputs of all benchmark scripts on all three tiers; the
  tier-matrix gtest; every new fixture under forced GC, poison and
  `LAMBDA_ROOT_WITNESS=1`.

## 8. Expected outcome

| Track | Rows | expected typed/C2MIR after |
|---|---|---|
| P0 | nqueens, cd, richards | comparable rows; no compiler claim |
| T29-2 | deltablue 31.7x, cube3d 16.7x, havlak 26.7x, splay 15.8x | −25–35% on each |
| T29-1 | deltablue, havlak, richards 14.4x, splay, cd, hashmap | deltablue ≤ 8x, havlak ≤ 10x, richards ≤ 6x, splay ≤ 8x |
| T29-3 | fasta, nqueens, collatz 1.22x, mandelbrot 1.28x, pnpoly 7.1x | typed ≤ untyped; scalar rows ≤ 1.1x |
| T29-4 | bounce 4.6x, every `T[]`-local row | bounce ≤ 2.5x; no typed row slower than untyped |
| T29-5 | cube3d, nbody 7.6x, raytrace3d 7.7x, sieve, brainfuck 7x, mbrot | cube3d ≤ 6x, nbody ≤ 4x |
| T29-6 | base64 16.5x, hyphen 54.9x, microdiff 24.1x, prettier 16.4x, text rows | base64 ≤ 6x, hyphen ≤ 20x |

Together these are the 63-row target of ≤ 2.5x. The residue after them is
N11 (the calling convention, T29-7) and GC on allocation-heavy rows; both are
design work, not tuning.

## 9. Order and dependencies

```
P0 ──► (measure) ──► T29-2 ──► T29-1 ──► T29-7 (design)
                       │
                       └──► T29-5
T29-3, T29-4, T29-6: independent, any time after P0
```

T29-2 before T29-1 because a handle's presence and layout facts are T29-2's
facts. T29-5 after T29-2 because a certified construction is only useful if
the reader trusts the certificate. T29-3, T29-4 and T29-6 touch disjoint
emitters and can proceed in parallel.

## 10. Obligations carried into implementation

1. A wrong presence or layout assumption reads a malformed Item (D3.2.4);
   T29-2 item 4 (runtime writers honour the ABI) lands before any arm is
   removed, and every removal has a fixture that shows the arm *kept* on a
   non-admitted receiver.
2. A missing handle-kill row is a silent wrong write; the default is kill,
   every keep row in Appendix D has a fixture, and `tune29_handle_alias`
   runs on both tiers under forced GC.
3. Handles cache header pointers only; any track that keeps a data-zone
   pointer live across an allocation point is a GC defect, checked by
   `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` on every fixture.
4. Every skipped nan or band test names its producer-side proof in the
   fixture; S4.1.2/S4.2.3 results are byte-identical on all tiers.
5. Nothing in this round changes an observable result; the tier-parity
   harness is the gate, and a divergence is a bug in the track, never a
   semantics change (D3.3.1v2).

## 11. Implementation evidence

Build under test: debug `make build` on HEAD `dc93c49c0` plus this round for
the gates; release `make release` (archived as `temp/t29/lambda-t29.exe`) for
timing. Instruction counts are MT7 counts (`temp/r46/mircensus.py`); the v46
column is the archived release binary's dump of the same script.

### 11.1 P0 — port hygiene (LANDED)

- **r7rs/nqueens.** `c2mir/nqueens.c`, `c2mir/nqueens_double.c` and the Go
  port (`go/internal/bench/bench.go`) now run the `.ls` algorithm: fresh
  candidate and rest lists per call, `placed` shared across frames as in
  `nqueens2.js`. All three print `nqueens: PASS`. The C2MIR row moves from
  0.141 ms (Result46, fixed board) to ~0.37 ms, so the typed/C2MIR gap on this
  row is about a third of the 14.7x Result46 reported.
- **awfy/cd2.** `Motion` is now a record (`{cs, p1x, p1y, p1z, p2x, p2y,
  p2z}`), matching the C port's struct; `find_intersection(m1: Motion, m2:
  Motion)` and its caller read fields. Output identical to the golden on all
  three tiers. `find_intersection` 2,341 → 454 instructions (74 generic
  `fn_mul`/`fn_sub`/`fn_add` calls gone); module 15,036 → 12,182 (with T29-2).
- **awfy/richards2 (shared core `lambda/benchmark/richards2_core.ls`).**
  `tasks: TaskControlBlock?[]`, `pkts: Packet?[]`. `datas` stays `array`: it
  holds four record kinds (the C port's `void *handle`), and a union element
  would stay boxed under D2.5.3. Output identical on all tiers for both the
  AWFY and JetStream wrappers.

Typing `tasks` exposed §11.2's first defect: the JIT runs of richards2 looped
forever (the interpreter was correct).

### 11.2 Defect: a nullable bool field lane published as an Item (FIXED)

`w.tasks[tid].th` reads a `bool` field through a `TaskControlBlock?` element,
so the member emitter unboxes into the nullable bool lane (byte 0/1/2,
D2.5.2v3). It then published that register with
`mir_expr_semantic_type(node)` -- the `bool?` wrapper, whose TypeId maps to
the Item representation -- so `mir_value_carrier_type` reported `any` for a raw
register. Two consumers trusted it:

- the declared-return boundary rebuilt the value from that TypeId, skipped
  boxing, and passed the raw byte to `lambda_type_check` (an Item word of 0 or 1
  reads as a container pointer: SIGSEGV, exit 139 on v46 too);
- a local declaration bound the raw byte into a binding described as boxed, so
  `if (x)` called `is_truthy` on it and `x == true` compared wrongly (the
  richards infinite loop).

Fixes (`transpile-mir.cpp`): the member emitter's `publish` now stamps the
lane's own TypeId whenever it returns a native lane (the contract still rides
along for nullable boxing, as the T21-1c index load already did); and a new
`emit_checked_value_boundary()` boxes from the producer's descriptor
(`em_require_rep`), replacing the TypeId re-derivation at the return and
tail-call-argument boundaries (rule 13: one helper, two callers). Regression:
`test/lambda/proc/typed_record_bool_lane.ls` + gtest
`InterpWalker.NullableBoolFieldLaneAgreesAcrossAllTiers` (interp, eager JIT,
default); v46 segfaults on it.

### 11.3 T29-2 item 4 — the required-field ABI audit (LANDED, one hole fixed)

Probes `temp/t29/abi/*.ls` cover every Lambda-side writer of a record slot:
literal construction (static null, dynamic null, wrong type, per field kind),
typed member store, deep path store, `var`-parameter store, parameter / return
/ declaration admission, JSON input admission, and element binding. All
rejected null into a required field on both tiers except one family:

**A contract-constructed literal in the JIT accepted `null` in a required
field** -- statically for array fields (`{cells: null}`) and dynamically for
every field kind (`{k: nothing()}`) -- at declaration and return sites, and
stored a zero word in the slot. The interpreter rejected it (E201). Root cause:
`mir_map_field_contract_compatible` decided "nullable" by `base !=
expected`, but a plain field's contract arrives as its declaration wrapper,
which unwraps to a different pointer, so a null-typed proof passed for every
required field and `mir_map_literal_matches_contract` sent the literal down
the unchecked shaped path. This is the D3.2.4 non-null-arm trap. Fix: a null
proof is compatible exactly when `lambda_type_accepts_null(expected)`.
Regressions: `test/lambda/negative/runtime/typed_literal_required_{array,dynamic}_null.ls`
+ gtest `NegativeScriptTest.RequiredLiteralFieldRejectsNullOnEveryTier`
(interp, jit, auto). The site label still differs by tier ("at field 'k'" vs
"at declaration 'c'"), as it already did for wrong-typed values on v46; both
tiers fail with E201 and never establish the binding.

Host-built values are outside D3.2.6 and were not audited.

### 11.4 T29-2 item 1 — required fields have no empty-slot arm (LANDED)

- `mir_shape_field_required(shape, field)`: trusted contract and a field
  contract that does not accept null. Inferred literal shapes never qualify.
- `emit_mir_direct_field_read(..., field_required)`: the container arm returns
  the raw `Container*` with no zero test. Used by the trusted-contract member
  read and by the exact-record fallback extraction; the guarded inferred-shape
  read keeps its arm.
- `mir_receiver_non_null()` replaces `mir_receiver_binding_non_null()`: besides
  a named non-optional record binding, a member read of a required container
  field through a non-null receiver is non-null (Tune28 §9.7's chained-read
  hazard is exactly what D3.2.6 closes). Index reads stay out (an out-of-bounds
  read is null, S7.1.1v3). A `T[]` contract is recognised through
  `lambda_array_contract_canonical` (it is an occurrence wrapper).
- The receiver probe of `emit_checked_index_load`
  (`MirIndexLoadPolicy::receiver_non_null`) and the null test of
  `emit_generic_pointer_array_load` are skipped for such receivers.

`b.main.k` is now two field loads and no branch.

### 11.5 T29-2 item 2 — layout proof through the carrier (LANDED)

`mir_emit_typed_path_store`: the root of a declared non-optional record binding
gets no null test and no shape-word compare; below it every step's parent is
proven on the fast arm, so no step compares a shape pointer, and a step is
null-tested only when its contract admits null. `w.cons[cid].satisfied = 1`
goes from ~42 fast-arm instructions to ~30; what remains is three COW bits and
the certificate guard (T29-1), the bounds pair, and the optional element's
null test.

### 11.6 T29-2 item 3 — moved to T29-1

Re-proving the certificate once per carrier needs a carrier that cannot change
between accesses. `w.cons` can be replaced by any write to `w.cons` or by a
`var` call on `w`, which is exactly the handle invalidation of CW37; Tune28's
census found no other benchmark emitting the guard. Item 3 is therefore part
of T29-1, not T29-2.

### 11.7 Results and gates

| function | v46 | T29-2 |
|---|---:|---:|
| deltablue2 `c_choose_method` | 1,997 | 1,861 |
| deltablue2 `c_recalculate` | 2,011 | 1,855 |
| deltablue2 `c_execute` | 1,618 | 1,498 |
| deltablue2 module | 24,703 | 23,751 |
| cube3d2 module | 14,487 | 14,487 (its hot loop reads no record field) |

This is the per-access part of the record gap only; the navigation and COW
tests per access are T29-1's.

**Timing** (release, `LAMBDA_TIER=jit`, interleaved, 7 runs, ms; "old" is the
HEAD script, "new" the P0-typed one):

| row | v46 | T29-2 same script | T29-2 new script |
|---|---:|---:|---:|
| deltablue2 | 33.7 | 33.0 | |
| havlak2 | 61.7 | 61.4 | |
| splay2 | 300.6 | 300.9 | |
| hashmap2 | 39.1 | 39.3 | |
| cube3d2 | 8.58 | 8.57 | |
| nqueens2 | 1.72 | 1.71 | |
| richards2 | 381.4 | 384.9 | 339.9 |
| cd2 | 476.5 | 477.2 | 464.6 |

T29-2's compiler change is **timing-neutral** (deltablue −2%, the rest within
noise). That is the expected size: it removes one to three predictable
branches per record access out of ~30–40, and the per-access navigation,
certificate and COW tests it leaves are T29-1's. The gains are P0's honest
typing: richards2 −11%, cd2 −2.5%. With the corrected port, r7rs/nqueens reads
~4.6x C2MIR (1.71 / 0.37 ms) instead of Result46's 14.7x.

Corpus gate: all 157 benchmark scripts give identical JIT output (timing lines
excluded) on the new build and on v46, except the two richards2 wrappers, where
v46 times out on the typed core (§11.2's defect) and the new build passes.

Gates on the debug build: `make test-lambda-baseline` 5,565 / 5,569 after the
sidecar updates below, the 4 failures being two pre-existing JS failures
(`test_js_mir_emission_gtest` `tune12_array_access`, `test_mir_ratchet_gtest`
`js_corpus_side_stack_frame_gc`; both fail identically with this round's
transpiler change stashed) and the two Lambda sidecars fixed here;
`test_mir_emission_gtest` 145/145; `test_lambda_gtest` 925/925;
`test_mir_gc_stress_gtest` 192/192; the four new or changed fixtures produce
identical output under `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1
LAMBDA_ROOT_WITNESS=1` on both tiers with no witness report.

Fixtures: new `tune29_required_field_read` (required read has no branch; an
optional field and a nullable receiver keep their arms) and
`tune29_carrier_proof` (the path store keeps exactly the certificate's three
compares and two null tests). `typed_path_store` pinned the root shape-word
load that D3.2.4v4 removes by design; its sequence drops that step and pins the
remaining compare count.

## 12. T29-1 implementation evidence

### 12.1 What landed

- **Plan (`build_ast.cpp`, `lambda_ast_plan_place_handles`, run at script
  finalize after the CW34 lowering, so both tiers see one decision).** In a
  `pn` body, a record place `P = root.s1[.s2]` on a declared non-optional
  record `var` local or `var` parameter is annotated on its `AstFieldNode`
  (`handle_slot`, `handle_role`). The first statement that spells P exactly
  once, as an unconditional `P.f` read, **binds**; later statements that spell
  P only as `P.f` reads or `P.f... = v` writes **use**. A handle with any write
  use binds as a writing handle. Everything the walk does not recognise as an
  Appendix D keep row ends the handle: a bare P or a bare prefix, any other
  spelling under P's first member, a store to P or a prefix, a root rebind or a
  bare root (a call argument, a copy), a key reassigned or passed to a call, a
  root-rooted call argument, nested functions, handlers, `raise`, `start`,
  `try`, `^`, and loops inside expressions. Sibling members of the root are
  kept. `if` arms scope their binds and a handle survives only if both arms
  keep it; condition loops keep an outer handle only if nothing in the loop
  ends it, and a bind in the body is redone each iteration; `for` statements
  are opaque. Roots with a CW34-36 named borrow are excluded, because those
  borrows write the place in place under a name the walk does not track.
- **Bind (`transpile-mir.cpp`, dispatcher hook in
  `transpile_structural_value`).** A read bind is the ordinary read,
  remembered in a register. A writing bind
  (`mir_emit_place_handle_write_bind`) walks the spine on the fast arm --
  COW bit per level, no shape compares (T29-2), certificate and bounds on array
  steps -- and on any miss detaches the root (a local; a `var` parameter's root
  is its caller's, CW33) and calls the existing `cow_place_leaf_fixed`, which
  un-shares every link and returns the installed leaf or null (S9.2.2). No new
  runtime helper was needed. The handle holds the record header only; field
  and buffer pointers are reloaded per access (D4.3.1).
- **Use.** A read use returns the handle register; the member emitter reads
  the field by offset (with the optional-element null test). A write use in
  `mir_emit_typed_path_store` is `beq record, 0 -> checked arm` plus one
  offset store, with no walk. The registers are keyed by the MIR function, so
  a body emitted twice or a nested function starts from an empty table; a use
  with no bind emitted in this function navigates normally (always correct).
- **Bool leaf.** The typed path store now also stores a plain `bool` field from
  a proven non-null bool value (its native word, as the direct field write
  does), on both the handle and the walk arm.

### 12.2 Evidence

| function | T29-2 | T29-1 |
|---|---:|---:|
| deltablue2 `c_choose_method` | 1,861 | 1,200 |
| deltablue2 `c_recalculate` | 1,855 | 1,212 |
| deltablue2 `c_execute` | 1,498 | 963 |
| deltablue2 module | 23,751 | 20,538 |
| richards2 core module | 9,263 | 8,935 |
| splay2 module | 5,211 | 5,211 |

A later `w.cons[cid].out` read is now four instructions (unbox, null test,
data load, field load) against ~38 at Result46. `c_choose_method` misses the
≤ 600 target: what remains per branch is the handle's own null tests, the
int53/null-sentinel arms on the values, spills around `s_stronger`, and 14
`lambda_type_check` calls (N9, N11). havlak2, cd2, hashmap2 and cube3d2 plan no
handles (their roots or paths do not qualify); splay2 plans six but its cost
is copies (683k map copies, unchanged), not navigation.

**Timing** (release, JIT, interleaved, 7 runs, ms):

| row | T29-2 | T29-1 |
|---|---:|---:|
| deltablue2 | 32.9 | 31.3 (−5%) |
| richards2 | 339.5 | 321.7 (−5%) |
| splay2 | 300.1 | 301.1 |
| havlak2 | 61.8 | 61.5 |
| cd2 | 463.5 | 464.5 |
| cube3d2 | 8.69 | 8.57 |

The −35% instruction cut on deltablue's hot functions buys 5%: the removed
navigation was well-predicted branches. **The proposal's §8 targets for these
rows are not met**, and the richards gate (COW unique-mutation tests −80%)
rested on a wrong attribution. The census is unchanged (array 1.34M, map
1.44M): the map tests are the `var`-parameter boundary's `cow_prepare_write`
in every `_b` wrapper (one per call, CW33), and the array tests are writes
through the untyped `datas` store, which takes the generic path. Neither is a
per-access navigation cost, so handles cannot remove them.

**Correctness.** `test/lambda/proc/place_handle_semantics.ls` has one probe per
Appendix D row that matters here -- aliasing write through another index,
element replaced, container grown, root rebound, root to a `var` callee and to
a plain callee, key reassigned, sibling write, place copy before a write,
shared local root, shared leaf, kept and killed loops, bind and kill in `if`
arms, absent elements -- and gives identical output on interp, jit and auto
(the interpreter never uses handles, so it is the reference).
`test/mir/lambda/tune29_handle_synth` pins one navigation for a function with
several spellings and two for one that replaces the container between them.
Forced GC (`LAMBDA_GC_FORCE_EVERY=1`, poisoning, `LAMBDA_ROOT_WITNESS=1`) is
clean on the fixtures, deltablue2 and richards2. `make test-lambda-baseline`:
all Lambda tests pass; the only failures are the two pre-existing JS ones
(§11.7). The bool leaf grew the `lambda_corpus_deltablue` ratchet probe
(jetstream deltablue2: module +96, `_constraint_choose_method_#` +173) and
was re-baselined in `test/mir/mir_budgets.json`: that row runs 25.4 → 23.9 ms
(release, 9 interleaved runs; v46 25.2), which outweighs a 1.1% larger module. All 157 benchmark scripts give v46's JIT output except the two
richards2 wrappers (§11.2).

**Found, not fixed (pre-existing on v46, both tiers):** a copy of a `var`
parameter taken inside the callee (`var saved = b`) observes later nested
writes through the parameter, because `var`-parameter stores publish in place
on the assumption that the caller detached the root. The writing bind makes
the same assumption, so it neither causes nor changes this. Filed as a
separate task; the fixture uses a local root for the shared-root probe.

### 12.3 What this says about the gap

T29-1 removes the per-access navigation the MIR comparison pointed at, and the
time barely moves. The instruction volume was real but cheap; the remaining
cost on the record rows is calls and their boundaries (spills, `_b` wrappers
with a type check and a root prepare per call, boxed returns) and value
representation (int53/null arms, boxing). That is N11 and N9, i.e. T29-7's
design and T29-3, and the next measurement should profile those rows by time,
not by instruction count.

## 13. T29-3 and T29-4 implementation evidence

### 13.1 T29-4 — typed local stores test the value's tag first (LANDED)

A typed `int[]` store whose value is not statically a native int (an
Item-returning call such as `fn_mod` in the untyped `random_next`) was one
unconditional `lambda_array_set_checked_lane` call.
`mir_emit_tag_tested_int_array_store` now handles it:
- it evaluates the value, then the key, then the owner, as the checked arm did;
- it tests the boxed value's tag against `LMD_TYPE_INT`;
- on a hit, it sign-extends the payload into the same
  `emit_array_num_direct_store` a proven value takes, with that store's own
  band, certificate, COW and bounds guards;
- on a miss, it calls `emit_array_num_store_fallback`, the checked setter.

The shape applies to a plain (non-nullable) `int` lane on an `ArrayNum` root
with an integral key. A non-int value is still rejected by the checked setter.
A string, `2.5`, and an `int64` beyond the band all give E201 on every tier,
matching the pre-change binary.

- Fixture: `test/mir/lambda/tune29_local_store_tag_test`. Its sidecar requires
  the tag branch before the checked call, and exactly two checked calls, both
  in cold arms.
- Timing (release, JIT, 9 interleaved runs, ms, against the post-T29-1
  build): Result46's `bB_ballarr` variant (untyped bounce plus `int[]` on four
  locals) went 0.145 → 0.109 (−25%). Untyped bounce is 0.069, so the variant
  still pays about 1.6x. What remains is the untyped `random_next` itself:
  `fn_mod` and its boxed return.
- The typed bounce2 was already on the direct path, so it is flat
  (0.100 → 0.098).

### 13.2 T29-3 — null and finiteness facts (LANDED except the fast-arm finiteness fact)

1. **Per-operand null tests.** `emit_int_lane_arith` and
   `emit_int_lane_divmod` take one nullability flag per operand
   (`emit_int_lane_null_branches`). A literal or a declared `int` operand is
   never compared with the null sentinel; the old pair test compared both.
2. **Proven divisors.** `emit_int_lane_divmod` skips the band test of a
   proven in-band operand and the zero test of a proven nonzero divisor, so
   `% 139968` carries no guard of its own.
3. **Null split at the read (D2.5.3).** `mir_null_split_candidate` applies to
   an int tree in which:
   - every operator is `+ - * div %`, is lowered by the checked arith or
     div/mod arm, and is not a u32, wide or compact-loop form;
   - there are at least two operators;
   - every leaf is pure: a literal, a binding already in the int lane, or a
     typed element read whose object is a binding and whose key is pure and
     non-null.

   Such a tree is lowered once with one null exit. Each nullable leaf is
   tested once (`beq lane, NULL, exit`), and the operators then see non-null
   operands. The result equals the old one because every operator on the way
   up tests its operands for null before anything else, and the skipped
   leaves are pure. fasta2's `(seed_arr[0] * IA + IC) % IM` went from three
   pair tests and three null arms to one branch.
4. **One-sided equality nan test.** A non-null int `==`/`!=` tests nan on one
   side only: equal lanes are both nan or neither, and a lone nan already
   compares unequal (S4.1.2). `tune26_dynamic_equality_compare`'s sidecar was
   updated to this form.
5. **Finiteness beyond intervals: not implemented.** The pilot rows have no
   finite producer that the existing interval proof
   (`mir_int_lane_operand_proven_in_band`) misses.
   - collatz's `n` and nqueens' `row`/`dist` are `int` parameters, which may
     hold nan or inf.
   - fasta's seed is an element read.
   - The one producer the proposal names that intervals cannot give, "the fast
     arm result of `+ - *`", needs the continuation after the slow arm to be
     duplicated; that is a larger change and is left open.

- Fixtures: `test/mir/lambda/tune29_index_null_split` and
  `tune29_finite_lane`.
  - `tune29_index_null_split` covers out-of-range reads, negative indices, two
    nullable reads, a nullable binding, nan/inf equality, and nan and
    infinity poisoning through a split tree.
  - `tune29_finite_lane` covers literal divisors (no zero test), a single nan
    test for the counter equality, `div` keeping its test, `x % 0`,
    `nan % 3` and `7 % inf`.

  Both give identical output on interp, jit and the pre-change binary, and
  both are clean under forced GC.
- Ratchet: deltablue module −16 and `choose_method` −12; prettier −11.
  Re-baselined in `test/mir/mir_budgets.json`.
- Timing (same method, ms): fasta2 0.857 → 0.862, nqueens2 1.703 → 1.703,
  collatz2 273.5 → 269.6, mandelbrot2 17.2 → 17.2, pnpoly2 12.45 → 12.37,
  deltablue2 31.3 → 31.3. **The §8 targets are not met.**
  - fasta2 is still 1.26x untyped (0.684 in Result46). The removed branches
    were cheap and well predicted, the same lesson as T29-1 (§12.3).
  - nqueens2's residue is the `int[]` parameter admission walk (§2.2). That
    belongs to T29-5, not to lane facts.

### 13.3 Gates

- `make test-lambda-baseline`: only the two pre-existing JS failures remain.
- The JIT outputs of all 157 benchmark scripts match v46, except the two
  richards2 wrappers (§11.2).
- havlak2 is 60.7 → 63.4 ms against the post-T29-1 build. That is LR12-11's
  accepted copy cost (issue ledger), not this round.

## 14. T29-5 implementation evidence

### 14.1 Item 4 — the comprehension matrix (LANDED, cause corrected)

The Result46 census was right that mbrot2's nested writes take the generic
path, but the loop-built variant takes the same path today; that is not the
difference. The comprehension `[for (i in 0 to n - 1) fill(n, 0)]` packs its
equal numeric rows into **one two-dimensional ArrayNum**. Each
`matrix[x][y] = v` then ran `fn_index`, which materializes a row view (an
allocation), `cow_prepare_write`, and `cow_path_set_raw` through that view.
A sampled run spent its time in `array_num_init_derived_view`, the allocator
and the collector; the loop-built matrix is an Array of separate rows and
allocates nothing per write.

**Fix.** `cow_path_set_packed_index` (runtime; `cow_packed_index_store`
behind it) writes the leaf at the full coordinate when the owner is an owned
N-D ArrayNum of the path's rank and the value is admissible to the lane. It
returns 0 with no effect for anything else — a rank mismatch, a view, an
out-of-range coordinate, a value outside the lane — and the caller's per-link
walk keeps its semantics and diagnostics. The certificate rule matches
`fn_array_set`: it is kept only when the value proves the certified leaf
element.
- MIR tries it first in `mir_emit_cow_path_set` for an all-index path of two
  or three keys. The root is already prepared, or is a caller-detached `var`
  root.
- T0's `cow_path_set_impl` tries it after preparing the root.

Fixtures: `test/lambda/proc/packed_nested_store` (a `var` borrow, locals, a
snapshot, 3-D, a non-int value, a plain-parameter snapshot, `bool[]`) and
`test/mir/lambda/tune29_packed_nested_store` (the packed attempt precedes the
`fn_index` walk). Both give identical output on interp, jit, auto and the
pre-change binary, and both are clean under forced GC.

**Found, not fixed (pre-existing, both tiers, any matrix):**
- LR12-14: `var row = m[0]` aliases the matrix both ways.
- LR12-15: an out-of-range nested store is logged, not raised.

### 14.2 Item 1 — certified construction (LANDED as a cheaper admission)

nqueens2's typed cost was the admission of every `var x: int[] = fill(..)`
(three per `solve` call): `runtime_array_admit_primitive_contract`
re-derived the contract's lane and rank (`lambda_array_contract_info`, the
lane descriptor, the element type) on every call before interning the
certificate.
- **Fix.** It now interns the certificate first, which is a pointer-cache hit
  on a warm boundary, and checks the live carrier against the certificate's
  resolved lane and rank (`lambda_array_num_matches_cert`, shared with
  `array_representation_matches_cert`). Every caller of the primitive
  admission benefits, not only `fill`.
- **Not done.** The proposal's fused one-call `fill` with a contract argument
  was not built: after this change the admission is a few loads and compares
  next to the fill's own allocation.
- **`bool[]`.** The declaration-time fill proof now also accepts `bool[]`.
  `fn_fill` already built a packed bool lane.

### 14.3 The plain-parameter snapshot store (LANDED)

nqueens2's `placed[placed_len] = row` writes a plain `int[]` parameter, which
CW29 marks at entry. Every call therefore reaches the direct store's cold arm
once to detach. For `int` lanes that arm always took the full checked setter:
the non-null flag passed to `emit_array_num_store_fallback` covered only
`bool` and non-null `float` values.
- **Fix.** A non-null `int` value now takes the compact
  `array_num_set_cow_idx` too. A lane sentinel boxes as an int poison Item,
  which the admitted store accepts; only null still needs the checked setter.
- **Gating.** The helper still applies only where it already did: a
  statically proven contract on an ArrayNum root.
- **Out-of-range stores.** They raise the same `fn_array_set` bounds error as
  before (compared on both tiers against the pre-change binary).
- **Sidecars.** `tune13_array_lane`, `tune17_typed_store` and
  `tune25_array_bounds` pinned the checked call in these cold arms and were
  updated.

**Found, not fixed:** LR12-16. The JIT does not return that bounds error from
a plain-parameter callee, while T0 does. This predates the round.

### 14.4 Item 3 — `bool[]` (already in place)

`fn_fill` builds a packed `ELEM_BOOL` lane, `ensure_typed_array` passes it
through, and admission is the representation check. sieve2 is 0.032 → 0.030
ms, which is noise.

### 14.5 Item 2 — caller-provided storage (NOT implemented)

cube3d2's allocation share is about a quarter of its samples (`fn_fill`, the
object and data zone allocators, zeroing, frame setup). Returning into
caller-provided storage needs two things the tree does not have:
- **A buffer-reuse proof.** The destination (`var new_v: float[] = vmulti(..)`
  in a loop) must be dead, not merely unescaped, at each iteration end.
- **A hidden destination parameter** threaded through the native, boxed and
  public-wrapper ABIs.

The existing region producer (`mir_region_producer_candidate`) covers
self-recursive map builders consumed within one call and does not
generalize. This remains a design item; see §16.4 for its relation to T29-7.

### 14.6 Results

Release, JIT, 7 interleaved runs, ms, against the post-T29-3/4 build. The
machine was about 8% slower than during §13, so compare only within a row.

| row | before | after | untyped (same run) |
|---|---:|---:|---:|
| mbrot2 | 0.917 | 0.556 | 0.721 (mbrot) |
| mbrot2 loop-built variant | 0.679 | 0.695 | |
| nqueens2 | 1.764 | 1.383 | 1.445 (nqueens) |
| cube3d2 | 8.79 | 7.81 | |
| sieve2 | 0.032 | 0.030 | |
| brainfuck2 | 192.4 | 192.0 | |
| nbody2, havlak2, deltablue2 | flat | flat | |

- mbrot2 and nqueens2 now run faster than their untyped versions, which closes
  the two T29-5 rows of §2.2.
- cube3d2 is −11%; its §8 target (≤ 6x C2MIR) needs item 2.

Gates: `make test-lambda-baseline` has only the two pre-existing JS failures;
the corpus diff is in §16.5.

## 15. T29-6 — in-place append on COW-unique strings (already in place)

The mechanism predates this round:
- `publish_var_binding` marks a `var` string that is only ever rebound to
  `self ++ …` as a may-own buffer (`string_buffer_owned`).
- `fn_strcat` / `fn_strcat_many` grow an owned buffer geometrically and
  append in place.
- A read that publishes the value freezes it (`fn_string_freeze`), so an
  alias never sees a later append.

The gate fixtures exist as `LambdaOptStrings` in `test/test_lambda_opt_gtest.cpp`
(`string_builder_observer`, `string_builder_tail`).

Census on the §6 pilots (`COW_EXEC_PROFILE`):

| row | appends | in place | growth copies | generic joins |
|---|---:|---:|---:|---:|
| base642 | 333,406 | 332,400 | 1,006 | 6 |
| hyphen2 | 71,086 | 53,229 | 17,857 | 3,964 |
| log_pipeline2 | 213,002 | 0 | 213,002 | 201,000 |
| three_way_merge2 | 1,287 | 0 | 1,287 | 517 |
| microdiff2 | 4 | 0 | 4 | 4 |

- **base642.** 99.7% of appends are in place, and the row is still 9.0 ms
  (untyped 16.7). A 40,000-iteration sample puts about 50% of the time in JIT
  code and 40% in `fn_strcat_many`: five single-character pieces per append,
  passed through varargs. The remaining cost is the append ABI and the
  per-character table reads, not ownership.
- **log_pipeline2 and three_way_merge2.** Their joins are one-shot
  multi-piece expressions, or a first append to a borrowed element, where a
  copy is required. The self-append shape does not apply; their generic joins
  are the Tune22 text-row work.

No code change was made for T29-6.

## 16. T29-7 — stack-map rooting: design record and call-site census

### 16.1 Census method

`temp/t29/census/spill_census.py` reads `LAMBDA_MIR_DUMP` output and, per call
site, counts:
- the stores into the function's side-stack frame immediately before the
  call (spills);
- the loads from it immediately after (reloads).

The frame base is the register loaded from `runtime+80`, and the frame size is
the `add` that follows. Dumps are from the post-T29-5 debug build, which emits
the same MIR as release. The counts are static; a cold-arm call counts the
same as a hot one.

### 16.2 Census

| row | functions | calls | spills | reloads | frame slots | user calls (spills/reloads) |
|---|---:|---:|---:|---:|---:|---|
| deltablue2 | 99 | 1,379 | 1,622 | 1,366 | 1,250 | 240 (371/559) |
| havlak2 | 115 | 1,565 | 1,517 | 4,239 | 263 | 304 (369/1,412) |
| cd2 | 65 | 1,020 | 892 | 1,902 | 94 | 123 (109/348) |
| splay2 | 39 | 418 | 394 | 419 | 192 | 63 (57/70) |
| cube3d2 | 31 | 674 | 984 | 1,593 | 192 | 95 (152/320) |
| fasta2 | 9 | 183 | 206 | 207 | 98 | 9 (27/6) |
| nqueens2 | 11 | 163 | 195 | 205 | 121 | 12 (22/12) |

(Corrected 2026-09-17: the first count missed slot 0, which MIR prints as
`i64:(%r..)` with no displacement digits.)

richards2's dump holds only the wrapper script; its core module compiles into
a separate image.

Top spill sites (spills + reloads per call):
- **`lambda_type_check` leads every row** (deltablue 171 sites, havlak 186,
  cd 139). Almost all of these are boundary checks on cold or error-lane arms.
- **cube3d2.** `array_float_new` has 111 sites with 638 reloads (7.1 per
  call): every float literal array reloads the frame after allocating. User
  calls such as `_draw_line` carry 6.7 per call.
- **havlak2.** Accessor calls carry 6–10 per call (`_bvec_raw_get` 10.1,
  `_iarr_set` 9.6). `fn_member_by_id` reloads 8.1 per call.
- **deltablue2.** `lambda_map_path_set_checked_fixed` spills 4.4 per call on
  the path store's cold arm.

### 16.3 What the census says

1. **Spills sit where they belong.** They are emitted immediately before the
   call they protect, so a cold-arm call costs its spills only when taken.
   The static totals overstate hot traffic. The hot cost is on user calls and
   allocation calls that run every iteration.
2. **Reloads dominate the rows that allocate.** In cube3d2 and havlak2,
   reloads exceed spills by 1.7x and 1.4x. Under D4.3.1 container headers
   never move; only the data zone compacts. A reloaded register that holds a
   **header** pointer therefore gets back the value it already had.
   - Only data-zone pointers (items and data buffers, which the emitter
     already reloads from the header after a call) and values a callee may
     republish (CW33 homes, which have their own reload) change across a
     call.
   - The first measurable design point is to **spill header-pointer and
     scalar Items for liveness, but not reload them.** It needs no stack map.
3. **A per-call-site stack map** — the proposal's N11 — replaces "spill every
   live Item" with "record which frame slots and registers hold pointers at
   this call".
   - It needs MIR to preserve callee-saved registers across the runtime call,
     or it needs the map to name the slots the values already live in.
   - Its payoff is bounded by item 2: after T29-1 and T29-3 the live Items
     across a hot call are mostly header pointers, which item 2 would make
     free without a map.

### 16.4 Proposed sequence (for ratification; no ruling changes yet)

1. **T29-7a — reload elision for header pointers.** A register whose value is
   an untagged container pointer, or a scalar Item with no heap payload,
   skips its post-call reload. The spill stays as the root. Proof obligations:
   - D4.3.1 holds for every container kind, including views and ArrayNum
     shapes;
   - no runtime helper replaces a spilled slot's value, except CW33 homes and
     the documented republish paths;
   - forced GC with `LAMBDA_GC_POISON_FREED=1` stays clean on the corpus.

   Pilot: cube3d2's `array_float_new` sites and havlak2's accessors.
2. **T29-7b — side-stack frame trimming.** deltablue2 reserves 1,250 slots
   across 99 functions. Size each frame to the maximum simultaneous live
   Items (a liveness walk over the emitted MIR) instead of one slot per spilled
   register.
3. **T29-7c — stack maps.** Only if a time profile after 7a/7b still shows
   spill traffic on hot calls. This is the D5 change the proposal named, and
   it needs a D5 ruling (precise rooting stays; conservative scanning stays
   retired, CLAUDE.md rule 15).
4. **T29-5 item 2** (caller-provided storage) is independent of 7a–7c. It
   removes allocations rather than their bookkeeping, and should be designed
   with the buffer-reuse proof of §14.5.

### 16.5 Gates for this round

`make test-lambda-baseline`: only the two pre-existing JS failures remain. The
JIT outputs of all 157 benchmark scripts match v46, except the two richards2
wrappers (§11.2).

### 16.6 T29-7a — reload elision: implemented, measured, reverted

**What was built.** The rooting pass (`em_finalize_semantic_root_write_back`)
took an opt-in flag, which Lambda passed for non-async bodies. It dropped the
post-call reload of any candidate whose frame slot no callee could address.
- **Exposed slots.** A slot counts as exposed when the function computes
  `frame_base + disp` for it; those are CW33 homes and record output addresses,
  which are pinned home slots and reload explicitly anyway.
- **Safety net.** Any other register use of the base — beyond comparisons and
  the watermark stores the frame emits itself — kept every reload in that
  function.
- **Why it is sound.** The collector only reads root slots (D4.3.1: objects
  never move), and MIR preserves pseudo registers across calls.

**Correctness held:**
- static reloads fell sharply (cube3d2 1,585 → 0, havlak2 1,944 → 152,
  deltablue2 575 → 147, nqueens2 202 → 0);
- the module ratchet shrank (cube3d −1,593 instructions, prettier −1,481,
  deltablue −497);
- all 157 corpus outputs were unchanged, and the baseline passed;
- forced GC with poisoning and the root witness was clean on deltablue2,
  richards2, nqueens2, mbrot2, bounce2, cube3d2, fasta2 and cd2.

**Time got worse.** Release build, interleaved against the post-T29-5
binary:
- nqueens2 1.297/1.321 → 1.373 ms (15-run minimums, +4–5%);
- cube3d2 7.63 → 7.73 ms (+1.5%);
- the other ten rows were flat within noise.

**Why.** A reload is a fresh definition of the register, so the register is
not live across the call. Without it the register is live across the call,
and MIR's allocator must preserve it: a callee-saved hard register while
those last, then its own stack save and restore. That is on top of the
side-stack spill, which still has to happen for rooting. The reload was the
cheaper way to carry the value across the call.

**Consequences for T29-7:**
- The remaining lever is on the spill side: never materialize the value in
  a MIR register across the call. That is exactly what a stack map (T29-7c)
  does by naming the slot the value already lives in.
- T29-7b (frame trimming) is unaffected by this result.
- The second negative result of this round on "remove instructions, gain
  time" (after T29-1, §12.3). Profile by time before the next rooting change.

The change was reverted; the budgets and emitter are back to the post-T29-5
state.

### 16.7 T29-7b — frame trimming (LANDED, timing-neutral)

**Where the frames came from.** Ordinary root stores are recorded as
candidates and placed by the write-back pass. A home the lowering also reads
or writes explicitly — a CW33 transport, a record output address, a value
merge — keeps its eager store and is *pinned*. Pinned homes kept their
lowering-time offsets, and the other homes were laid out around them. A frame
therefore spanned its highest pinned home however few homes were pinned:
`c_recalculate` reserved 157 slots and touched 69.

**Fix** (`em_finalize_semantic_root_write_back`,
`em_root_frame_refs_compactable` / `em_root_frame_refs_remap`):
- Pinned homes are renumbered densely into the frame's prefix, and every
  other stable home follows them without gaps.
- The pinned homes' explicit operands — memory operands on the frame base and
  `frame_base + disp` address computations — are rewritten after the oracle
  stores are removed and before the write-back inserts its own stores, which
  already use the dense numbering.
- **Fallback.** Compaction applies only when every frame reference is such a
  pinned slot and the base appears otherwise only in the frame's own
  watermark store. A fixed prerooted suffix (JavaScript) also keeps the old
  layout.

**Evidence** (static, post-T29-5 debug MIR; spills and reloads are
unchanged):

| row | frame slots before | after |
|---|---:|---:|
| deltablue2 | 1,250 | 812 |
| havlak2 | 263 | 234 |
| splay2 | 192 | 161 |
| cd2, cube3d2, fasta2, nqueens2 | unchanged | |

**Gates:**
- `make test-lambda-baseline`: only the two pre-existing JS failures.
- The JIT outputs of all 157 benchmark scripts match v46, except the two
  richards2 wrappers.
- Forced GC with poisoning and the root witness is clean on deltablue2,
  richards2, nqueens2, mbrot2, bounce2, cube3d2, fasta2 and cd2, and on a
  reduced splay2 (600 nodes, 3 rounds; identical to the pre-7b binary). The
  full splay2 exceeds 25 minutes under forced GC on either binary.

**Timing** (release, 9 interleaved runs, against the post-T29-5 build):
deltablue2, jetstream deltablue2, havlak2, splay2, cd2, nqueens2, cube3d2 and
richards2 are all within ±1%. The trimmed words were never touched on the hot
path. The gains are a shorter collector scan per activation and fewer stale
words a frame can keep alive, neither of which these rows measure.

## 17. Time profile of the record rows (2026-09-17)

Method: `make build-release-profile` (ThinLTO, local symbols kept: the
shipping release strips them and LTO internalizes statics, so `sample` showed
30% as `???`); each row's driver loop lengthened to run for 10 s or more; `sample`
for 8 s; `LAMBDA_MIR_LOG_CODE_ADDR=1` (new, `mir.c`) names the JIT frames.
Scripts: `temp/t29/prof/attribute.py` (helper inclusive/self, what each helper
does inside) and `temp/t29/prof/jit_attr.py` (per Lambda function, and the
reverse map helper -> callers). Shares are of all samples.

### 17.1 deltablue2 (6,140 samples; JIT code self 24%)

| source operation | runtime cost | share |
|---|---|---:|
| `vec_at(var v: Vec, i) int { return v[i] }`: entry admission of the unproven `int[]` argument (the `_array_witness` bit is clear because callers pass a field place `w.vars[x].constraints`), then the `int` return contract on the boxed element | `lambda_type_check` x2 per call | 11.6 |
| `c_execute`: three `int`-contract checks on boxed field reads (`w.cons[cid].kind/direction`, ...) | `lambda_type_check` -> numeric admission ladder | 7.9 |
| `vec_new() Vec { return [] }`: the empty literal admitted to `int[]` (reified to an ArrayNum) | `lambda_type_check` | 6.9 |
| `push(v, item)` on `var v: Vec` in `vec_add`, `con_alloc`, `var_new` | `lambda_array_push_checked_impl` (reserve, admit, store) | 10.4 |
| `c_satisfy`'s nested record stores that miss the T29-1 handle arm | `lambda_map_path_set_checked_fixed` -> `cow_path_set_impl` | 9.6 |
| `var` borrows of shared `int[]` places (`planner_add_constraints_consuming_to`, `var_add_constraint`) | `cow_prepare_write` -> `clone_mutable_array_num` | 7.1 |
| GC | `gc_collect_with_root_region` self | 3.9 |

Inside the 38.5% under `lambda_type_check`: `runtime_type_admit_value_env`
10% self, `lambda_numeric_boundary_admit` 7.2%, `contract_numeric_admit_signed`
5.7%, `runtime_contract_uses_binder` 4.6% (called at the top of every
admission; the program has no binders, so this is either the walk running
because the process-wide flag is set, or plain call overhead -- verify),
`lambda_numeric_runtime_part` 2.5%. Almost all of it is *a boxed int checked
against the plain `int` contract*: one tag compare's worth of work done by a
seven-level runtime ladder.

### 17.2 havlak2 (891 samples; JIT code self 6%)

| source operation | runtime cost | share |
|---|---|---:|
| `arr_set`: `var l0 = a.l0; var c1 = l0[i0]; var c2 = c1[i1]; c2[i2] = v; c1[i1] = c2; l0[i0] = c1` -- the nested place copies are share-marked, so each level's store clones its array (S9.1.2) | `member_set_cow` -> `cow_prepare_write` -> clone | 21.8 |
| `push(v.data, x)` in `bvec_add`, `loop_add_node`: the place borrow un-shares a shared `data` array, i.e. clones it, per push | `cow_path_borrow_impl` -> `cow_prepare_write` | 16.0 |
| record constructors and typed reads (`iset_new`, `bb_new`, `hlf_do_dfs`, `iarr_get`) | `lambda_type_check` | 15.3 |
| `cfg_add_edge`, `hlf_process_edges` detaches | `cow_prepare_write` | 9.7 |
| GC of the copies above | `gc_collect_with_root_region` + payload destroy | 28.6 |

Copies plus their collection are about 75% of havlak2. The census confirms it:
65,675 array copies and 132,341 map copies per run. This is the D4.4.4v4
"nested handles" case, which the implementation does not cover, plus the
place-borrow-of-a-shared-child case.

### 17.3 cd2 (677 samples; JIT code self 7.5%)

| source operation | runtime cost | share |
|---|---|---:|
| `rbt_find_node`: `keys[i] == key` with `keys: array` (untyped) | `fn_eq_depth` -> `lambda_numeric_compare` -> `lambda_numeric_runtime_part` | 28.2 |
| `rbt_find_node`: `keys[i]`, `len(keys)` on the untyped array | `fn_index`, `fn_len` | 30.0 |
| `rbt_put`: `push(keys, key)` on the place copy `var keys = tree.keys` (marked, so cloned per push) | `pn_push_cow` -> `cow_prepare_write` | 12.1 |
| `recurse_draw` list building | `array_push`, `lambda_type_check` | 8.6 |

cd2's red-black table is untyped (`RbtTable = {keys: array, vals: array}`),
so 58% of the row is generic equality and indexing on `any`. P0 typed only
`find_intersection`.

### 17.4 What the profile says

1. **Boxed-scalar admission is the largest single cost on the typed rows**
   (deltablue 38%, havlak 15%): `lambda_type_check(Item, int)` and friends run
   the full admission ladder for what is one tag compare. T28-7 inlined the
   *lane* case (`mir_emit_int_lane_null_admission`); the *boxed Item* case
   still calls out. An inline `tag == INT` test with the call on the miss arm
   is the T29-4 shape applied to every scalar boundary.
2. **Unproven typed-array arguments are admitted per call** (`vec_at`,
   `vec_new`): a field place passed as `var` clears the witness bit, so the
   callee walks the contract on every call. The T29-5 admission fix made the
   ArrayNum case a certificate compare, but a `Vec` built by `vec_new`'s `[]`
   is reified at each boundary.
3. **COW copies from nested place copies and place borrows** are havlak2's
   whole cost and a third of deltablue2's. Every one is a copy the semantics
   permit and T29-1's nested-handle design was meant to remove.
4. **Guards were never the cost.** JIT self time is 24% (deltablue), 6%
   (havlak), 7.5% (cd). The T29-1/T29-2/T29-3 tracks worked inside that
   slice.
5. **cd2 needs typing before tuning**: its remaining cost is generic operations
   on `any`.

## 18. Boxed-scalar admission (LANDED, from §17)

`emit_checked_boundary` lowered every boxed value against a scalar contract
as one `lambda_type_check` call, and §17 measured that call at 38% of
deltablue2. For a plain `int`, `bool` or `string` contract (simple kind, not a
literal, not nullable, no binder) a value whose tag byte *is* the contract's
TypeId is admitted unchanged by every arm of the runtime ladder: a compact int
is in band by construction, and `string` admits a STRING item as is (T28-5).
The emitter now compares the tag inline and calls the runtime only on a miss
(null, poison, a widened numeric, a symbol, an error), so every diagnostic
and conversion is unchanged. Doubles are excluded: inline doubles carry no
plain top-byte tag.

**Timing** (release, 7 interleaved runs, ms, against the post-T29-7b build):

| row | before | after | |
|---|---:|---:|---|
| awfy deltablue2 | 32.0 | 28.3 | −12% |
| text prettier_ast2 | 701 | 603 | −14% |
| r7rs nqueens2 | 1.36 | 1.24 | −9% |
| awfy havlak2 | 66.4 | 62.3 | −6% |
| jetstream deltablue2 | 24.1 | 22.9 | −5% |
| richards2, cd2, splay2, fasta2, cube3d2 | | | within ±1% |

**Gates.** JIT outputs of all 157 benchmark scripts unchanged; baseline: only
the two pre-existing JS failures; five Lambda size probes grew by the inline
test (deltablue +249, prettier +207, cube3d +12, typed_array_guard +24,
tail_forward +7) and were re-baselined with the reason.

This is the round's largest single gain, from a seven-instruction change,
which is the §13.2/§16.6 lesson in the other direction: removing a *call*
moves time; removing branches does not. The rest of the 38% is the miss arm
on values that are not tagged as the contract (nqueens2's `int[]` argument
admission, deltablue's `vec_new` reification) and is §17.4 item 2.

## 19. Disposition (2026-09-17)

The §8 targets were set from a static instruction census, and §13.2, §16.6
and §18 established that instruction volume was not the cost: the tracks that
removed guards were timing-neutral or slower; the ones that removed a call or
an allocation moved time (T29-4 −25%, T29-5 −22..−39%, §18 −12..−14%). The
disposition below follows the §17 time profile, not §8.

### 19.1 Three items worth doing, in this order

**1. Nested handles and shared-child borrows** (D4.4.4v4; the unbuilt half
of T29-1). havlak2 spends 38% cloning arrays -- `arr_set`'s three-level
place-copy chain (`var l0 = a.l0; var c1 = l0[i0]; var c2 = c1[i1]`, each
level cloned on its store) and `push(v.data, x)` borrows that un-share a
shared child by copying it -- and 29% collecting those copies; deltablue2
spends 7% and cd2 12% on the same shapes (§17.2, §17.1, §17.3). The ruling
already covers nested handles; the implementation stopped at one level and
excludes roots with a named CW34 borrow. Deliverables: handles through a
chain of place copies on one root; a writing borrow of a shared child that
un-shares in place instead of cloning; the two gate fixtures the proposal
required and §12 never wrote (`tune29_handle_kill`, `tune29_handle_alias`).
Measure with `temp/t29/prof/jit_attr.py` on havlak2: the `member_set_cow` and
`cow_path_borrow_impl` rows are the target.

**2. Proven typed-array arguments through field places** (§17.4 item 2).
`vec_at(var v: Vec, i)` re-admits its `int[]` argument on every call because
callers pass the field place `w.vars[x].constraints`, which clears the
`_array_witness` bit; `vec_new`'s `return []` reifies the empty literal to an
ArrayNum at every boundary. Together 18% of deltablue2, and most of what §18's
tag test left in the miss arm. Two mechanisms: a declared record field of
array contract carries its certificate to the borrow (the layout fact of
D3.2.4v4 is exactly this), and an empty-literal return against `T[]`
allocates the certified ArrayNum directly instead of reifying through the
checker. Compiler-only; no semantic change.

**3. Type cd2's red-black table.** 58% of cd2 is generic equality and
indexing on `keys: array` (§17.3). This is the P0 port-hygiene issue
(`find_intersection` was typed; `RbtTable` was not) and is a benchmark edit,
not compiler work. It is a precondition for any compiler change showing on
that row, and it removes cd2's 37x headline gap from the comparison until the
port is honest.

Cheap and owed alongside: the effect audit (`cow_mark_shared` and
`cow_capture_value` only set a bit; classing them NO_GC removes a safepoint
per call, the one spill lever that has paid, §16.4).

### 19.2 Closed without implementation

- **T29-3 finiteness fact** (§13.2 item 5): no pilot row has a producer the
  interval proof misses; the one named source (the fast arm of `+ - *`)
  needs continuation duplication for a test that costs nothing when
  predicted. Closed.
- **T29-5 item 2, caller-provided storage** (§14.5): needs a hidden
  destination parameter through three ABIs and a dead-destination proof, for
  one row (cube3d2, allocation ≈ 25% of samples). Closed for this round;
  reopen only if a later profile of cube3d2 shows allocation as the residue
  after item 1.
- **T29-7c stack maps** (§16.3–16.4): needs a vendored-MIR patch and a D5
  ruling, and T29-7a showed the value stays in the frame either way; the
  cost it would address is not where the time is (§17.4 item 4). Closed.
- **T29-1's `c_choose_method` ≤ 600 target and the §8 ratio targets**: guard
  volume is not the cost; retired as goals.

### 19.3 Where the rows stand at close

Release, against Result46 typed and R46's C2MIR column (R46 was ~13%
load-inflated, so the ratios are indicative):

| row | R46 typed | close | / C2MIR |
|---|---:|---:|---:|
| deltablue2 | 41.2 | 28.3 | ~22x |
| havlak2 | 74.9 | 62.3 | ~22x |
| richards2 | 483 | 325 | ~10x |
| prettier_ast2 | 703 (R46 wrapper) | 603 | |
| nqueens2 | 2.07 | 1.24 | port changed |
| cube3d2 | 8.65 | 7.6 | ~15x |
| cd2 | 652 | 467 | ~27x (untyped rbt) |
| mbrot2 / bounce variant | 0.84 / 0.145 | 0.56 / 0.109 | faster than untyped |


## 20. The three §19.1 items (2026-09-17)

All three were implemented. Item 1's investigation found a JIT
correctness defect, and fixing it came first (§20.3). Timings are release
min-of-7, interleaved against the §18 binary (`temp/t29/lambda-bsa.exe`); the
new binary is `temp/t29/lambda-items2.exe`.

| row | §18 binary | now | change |
|---|---:|---:|---:|
| deltablue2 | 28.8 | 25.3 | −12% |
| havlak2 | 64.1 | 62.3 | −3% |
| cd2 (typed `RbtTable`, both binaries) | 200.7 | 183.8 | −8% |
| cd2 (untyped rbt on §18 → typed now) | 461.8 | 183.8 | −60% |
| richards2 | 326.1 | 326.6 | neutral |
| json2 | 2.55 | 2.56 | neutral |

### 20.1 Item 3 — cd2's red-black table is typed

`type RbtTable = {keys: int[], vals: array}`; `rbt_new` declares
`var keys: int[] = []`. Keys are always ints (`rbt_put` takes `key: int`), so
lookups compare native ints; values stay open because callers store both ints
and entry arrays. Output is identical on every tier and matches the corpus
reference. The original port is kept at `temp/t29/cd2_before.ls`.

### 20.2 Item 2 — typed-array arguments through field places

The profile's first claim was already stale: `vec_at`'s callers pass the
witness bit (the declaration of `cs` proves the argument). The cost had moved
to the declaration itself, `var cs: Vec = w.vars[i].constraints`, which ran
the full `lambda_type_check` because a static relation proves only element
compatibility, not the live certificate (D3.3.3v3).

- **Carrier-proven declaration admission.** D3.2.4v4's corollary says a field
  read from an admitted record carries the field's proof: every write into
  the record admitted its value. `mir_member_read_carries_contract` (split out
  of `mir_member_read_proves_contract`, which adds the null condition) marks
  such a declaration. `emit_checked_boundary` then admits any container Item
  (tag byte 0, nonzero) inline and keeps the call only for null (an absent
  carrier, `w.vars[99]`) and every other tag. Negative fixture:
  `test/lambda/negative/runtime/typed_array_field_missing_carrier.ls`.
- **Certified empty literal.** `[]` crossing a `T[]` declaration or `return`
  called `array()` and then the admission ladder. The emitter now marks the
  literal (`mt->empty_array_boundary_node`), the literal arm calls
  `lambda_array_empty_for_contract` (a certified empty ArrayNum for a
  primitive lane, the checked generic array otherwise), and the boundary is
  skipped. `runtime_array_new_empty_numeric` is shared with
  `lambda_array_admit_numeric_contract`'s empty-array branch.

Fixture `test/lambda/proc/typed_array_admission.ls` (int/float/bool/string/
nested/optional returns, typed declarations, field reads through a record
array; all tiers identical to the §18 binary).

### 20.3 Item 1 — what the investigation found

**A JIT alias leak (fixed first; LR12-17).** `arr_set`'s three handles were
already bound. The trace showed the chain's stores were raw
`fn_array_set` calls even when the spine was shared. MIR Direct picks the
store form at compile time from `cow_marked` / `cow_children_may_be_shared`,
and these "may be shared" facts were updated in emission order only:

- a detach or a fresh rebind inside one `if` arm cleared the fact for the
  path that skipped the arm (`if (c1 == null) { c1 = null2() }`);
- a share made late in a loop body never reached the stores emitted earlier
  in the body.

Eight probes in `test/lambda/proc/cow_flow_join.ls` leaked on the §18 binary,
while T0 was correct. The fix has three parts:

- `MirCowJoin` starts every `if` and `match` arm from the entry facts and
  leaves the join with the union of the arm-end facts. An arm that always
  returns is excluded.
- The loop pre-scan (`mir_premark_loop_cow_bindings`, formerly plain-parameter
  call arguments only) now marks at loop entry every outer binding the body
  may hand to a second observer. Any name in a value position counts; the
  exceptions are operands, conditions, access/store objects and keys, `var`
  borrows, and plain arguments the callee neither mutates nor retains. An
  escaping access shares its root's children.
- `MirCowLoopJoin` joins the entry and body-end facts at loop exits
  (`while` and `for`).

**The havlak2 cascade was a snapshot bind, not missing nesting.**
`cfg_create_node` binds `var bbm = cfg.bbMap` and stores it back only inside
`if (node == null)`. CW36 refused the region because `bbm` is also passed to
`arr_get` as a plain argument. The snapshot bind marked `bbMap` shared on
every call. The next path borrow `arr_set(cfg.bbMap, …)` cloned it, the clone
marked `l0`, and `arr_set` then cloned all three levels.

Appendix D's row for a plain argument is "keep identity, kill uniqueness".
The CW36 use model now admits a bare handle passed as a plain argument to a
`pn` whose parameter does not keep the leaf itself
(`!cow_param_root_retained`):
- a returned part is share-marked at the call site on both tiers (LR12-11);
- a part stored elsewhere is captured (S9.3.1);
- named arguments stay refused.

Fixture `test/mir/lambda/tune29_handle_kill.ls` pins three decisions: the
admitted shape, a root-returning callee (borrow kept, result mark, detach),
and a `var` writer on the root (snapshot bind).

**Two precision fixes that the flow join made visible** (cd2 was +4.5%
before them, −8% after):

- `cow_param_root_returned` separates "the result may be this parameter's
  root" from "a part of it". The call-site rule (LR12-11) now marks the
  argument root itself only in the first case, and always marks its
  children. `rbt_put` returns an old value, not the tree, so its callers no
  longer pay `cow_prepare_write` per call.
- Retention: a local bound to a part of a plain parameter
  (`let keys = tree.keys`) now counts as retaining it only if the local
  escapes in turn. The pure-scalar-builtin exemption (`len`) applies to that
  recursion.

**Copies (havlak2, one run).**
- §18 binary: 137k, understated because the leaked writes skipped their
  copies.
- Flow join alone: 164k, the correct count.
- With the handle admission: 111k.

**"Un-share a shared child in place" is closed as unsound for these
rows.** The children are shared because a live owner still references them.
`loop_add_node` stores `loop` into `lsg.loops` while its caller keeps
`live_loop`, and `hlf_process_edges` does `bp = arr_get(...)`,
`vec_add(bp, v)`, `arr_set(..., bp)`. Both are real two-owner states under
S9.1.2. Removing those copies needs a get-modify-put across calls (a
moved-element convention), not a runtime shortcut. The remaining 111k copies
are almost all these shapes.

Fixture `test/mir/lambda/tune29_handle_alias.ls`:
- `w.cons[i]` and `w.cons[j]` with `i == j` at runtime;
- the `arr_set` store chain, rebound in one arm, must not leak into a held
  copy (the §18 binary printed the leak).

### 20.4 Effect audit

`cow_mark_shared` and `cow_capture_value` set one header bit and return their
argument. Both are now NO_GC, REENTRY_NO and BOXED_ITEM rows, and are listed
in `jit_import_validate_no_gc_allowlist`.

### 20.5 Gates

- `make test-lambda-baseline`: only the two JS failures that were already
  there (`tune12_array_access`, and the ratchet `js_corpus_side_stack_frame_gc`).
- `lambda_corpus_cube3d` re-baselined for the flow join: 14499→14574 module
  instructions, `run_cube` 9655→9730. cube3d2 time is unchanged (7.78 vs
  7.79 ms).
- The `typed_path_store` sidecar counts §18's extra inline tag test.
- Corpus JIT output against v46: 155 same, plus the two `richards2` rows
  where v46 times out.
- Forced GC (`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1
  LAMBDA_ROOT_WITNESS=1`) is clean on both tiers for `cow_flow_join`,
  `typed_array_admission`, `call_result_alias`, `tune29_handle_kill` and
  `tune29_handle_alias`.
- `lambda.exe` is currently the release build; the test executables must be
  rebuilt before the next gtest run.
