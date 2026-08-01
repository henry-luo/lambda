# Result18: Typed Lambda vs C2MIR — Where the 9.5x Goes

> **Status: ANALYSIS (2026-08-01).** Companion to the C2MIR/Go static-ceiling
> columns added to `test/benchmark/Overall_Result18.md` (MIR-typed / C2MIR
> geomean **9.48x** over 44 rows). Method: side-by-side reading of the MIR both
> compilers emit for the same workload — Lambda's from the debug build's
> `temp/mir_dump.txt`, C2MIR's from `mac-deps/mir/c2m -S`. Since both feed the
> same MIR backend, every difference is front-end attributable by construction.
> Related: [`Lambda_Design_Dual_Func_Compiling.md`](Lambda_Design_Dual_Func_Compiling.md)
> (DF2/DF3/DF8, O1, O3), [`Lambda_Issue_Type_Support.md`](Lambda_Issue_Type_Support.md)
> (TS-1, TS-3), the Result18 flexint-poisoning finding (navier-stokes),
> [`Lambda_Impl_Tune8_Result15_Bottlenecks.md`](Lambda_Impl_Tune8_Result15_Bottlenecks.md).

---

## 1. The four exhibits

Four rows were dissected, chosen to span the gap distribution. All Lambda
sources are the typed (`*2.ls`) variants.

| Row | MIR-typed | C2MIR | Gap | Class |
|---|---:|---:|---:|---|
| r7rs/fib | 7.79 ms | 1.10 ms | **7.1x** | recursion / call boundary |
| awfy/sieve | 0.518 ms | 0.017 ms | **30x** | int loop + array |
| awfy/nbody | 82.1 ms | 1.57 ms | **52x** | float array |
| awfy/mandelbrot | 53.5 ms | 32.7 ms | **1.6x** | float scalar loop (control) |

Mandelbrot is the control: its inner loop compiles to native `dmul`/`dadd`/
`dsub` on `d`-registers with **zero per-iteration runtime calls**. At 1.6x it
shows the backend and the scalar-float lane are already near the ceiling. The
gap elsewhere is therefore not codegen quality — it is **whether a boxed value
enters the hot path**. Once one does, every operation touching it degrades to a
runtime call.

### 1.1 fib — the call boundary (7.1x)

C2MIR `fib`: 7 instructions, 6 locals, zero memory traffic:

```
	bges	L2, i0_n, 2
L1:	ret	i0_n
L2:	subs	i_2, i0_n, 1
	call	proto0, fib, i_1, i_2
	subs	i_4, i0_n, 2
	call	proto0, fib, i_3, i_4
	adds	i_5, i_1, i_3
	ret	i_5
```

Lambda `_fib_161` (the **native** body, param declared `n: int`): 92 locals,
~8 runtime calls per invocation. Per self-call, `n - 1` does:

1. `sub` + biased INT53 range check (`add`/`ule`/`bf`) — fine, 4 instrs;
2. **box** the result into a tagged Item (second range check, mask/or, 2-way
   branch — ~9 instrs), with a cold overflow-to-double lane
   (`push_d` + `lambda_item_adopt_scalar_home` + `lambda_restore_number_frame_top`,
   3 calls);
3. spill Item + boundary-type + type-string to the root frame (3 stores);
4. **call `lambda_type_check`** against the declared `int` — on a value that
   was provably `int` at transpile time (declared param minus int literal);
5. tag-test the result for error (ursh/eq/bf);
6. **call `it2i`** to unbox the Item back to the i64 it started as;
7. pass it as a native i64 argument.

The recursive results come back as **boxed Items** — the native body has native
params but a boxed return — so `fib(n-1) + fib(n-2)` is a **call to `fn_add`**,
full dynamic dispatch on two values whose type the transpiler chose itself.

Per-entry ceremony on top: `lambda_side_stack_ensure_for` call, number-frame
reserve (load/add/load/cmp/branch/store), root-frame reserve + **80-byte
`memset` call**, and on *every* return two more calls
(`lambda_item_adopt_scalar_home`, `lambda_restore_number_frame_top`) even when
returning an inline tagged int that touched neither. Dead code rides along:
`type_list_1`, `heap_ptr_2`, `gc_ptr_3` are loaded and never used;
`direct_result_47` is stored to the root frame twice (offsets 32, then 32
again).

Net: C ~1.3 ns/call, Lambda ~9.4 ns/call across fib(27)'s 832,040 calls.

### 1.2 sieve — the loop protocol (30x)

C2MIR inner loop: 4 instructions (`subs`/`ext32`/store/`adds`/`bles`).

Lambda, `for i in 2 to sz` (both endpoints statically int):

- **Range materialization**: `fn_to` call allocates a Range object; then
  `item_keys` + `iter_len` calls; then **one `iter_val_at` call per
  iteration** to produce the induction variable — *as a boxed Item*.
- The boxed `i` poisons the body: `i - 1` → `fn_sub` call, `flags[i-1]` →
  `fn_index` + `is_truthy` calls, `k + i` → `fn_add` call. Four dynamic
  dispatches per iteration on statically-int values.
- Declared locals round-trip per assignment: `prime_count: int` and `k: int`
  each do box → `lambda_type_check` (statically true) → `it2i` on every
  `x = x + 1`. The annotation *adds* work — TS-1/TS-3 in its purest form.
  Notably `k` itself is held unboxed (`letv_77` is a raw i64; the `k <= sz`
  compare is a native `le`) — the round-trip is pure decoration at the
  assignment, which is exactly why eliding it is safe.
- `flags[k-1] = false` → `fn_array_set` call (runtime type switch per store).
- **Dead comprehension**: the statement-position for-loop still builds its
  result — `array_spreadable` + `array_push_spread` call per outer iteration +
  `array_end`, building a 4,999-null list that is immediately discarded.

### 1.3 nbody — typed arrays that decline the type (52x)

`advance(bx: float[], by: float[], ...)` — the annotation is present, the
boundary runs `ensure_typed_array` (7 calls, fine), and then the body ignores
the proof it just established. The single `_advance_186` body contains **131
runtime calls**:

| Call | Count | C equivalent |
|---|---:|---|
| `item_at` | 24 | `ldd d, (base, idx, 8)` |
| `it2d` | 25 | — (the same load) |
| `push_d` / adopt / restore | 41 | — (value stays in a d-register) |
| `fn_array_set` | 9 | `std d, (base, idx, 8)` |
| `ensure_typed_array` | 7 | — |

Every `bx[i]` on a **declared `float[]`** is two native calls
(`item_at` runs a full TypeId switch — [lambda-data-runtime.cpp:2609](../lambda/runtime/lambda-data-runtime.cpp:2609) —
then `array_num_get` boxes the double into an Item; `it2d` immediately unboxes
it). Doubles that don't fit the inline-double tag take the number-stack detour
(`push_d` + adopt + restore = 3 calls) per intermediate.

Worse: `has_typed_array_param` is a **disqualifier** in the `generate_native`
gate ([transpile-mir.cpp:15286](../lambda/runtime/transpile-mir.cpp:15286)) —
declaring `float[]` params actively *disables* the native body for exactly the
functions that need it most. The annotation is a net pessimization end to end.

---

## 2. Mechanism catalog

| # | Mechanism | Evidence | Cost shape |
|---|---|---|---|
| M1 | Statically-true boundary checks at direct call sites: box → `lambda_type_check` → `it2i` when the argument's static type already ⊑ the declared param type | fib ×2/call; sieve per assignment | 2 calls + ~12 instrs + 3 root stores per arg |
| M2 | Flexint lane *boxes* declared-int arithmetic results (the INT53 check is cheap; the boxing + M1 round-trip after it is not) | fib `n-1`; sieve `k+i` | ~9–20 instrs + potential 3-call double detour per op |
| M3 | Native bodies have native params but **boxed returns** → results of typed calls feed `fn_add`/`fn_eq` dynamic dispatch | fib `fn_add`; mandelbrot `fn_add`/`fn_eq` residue | 1 dispatch call per use of a call result |
| M4 | `for i in a to b` (int endpoints) runs the full iterator protocol: Range alloc + `item_keys` + `iter_len` + per-iter `iter_val_at`, boxed induction var | sieve outer loop | 3 calls + alloc setup; 1 call/iter; poisons body → M5 |
| M5 | A boxed value in a loop degrades every op touching it to `fn_sub`/`fn_index`/`fn_add`/`is_truthy` dispatch | sieve body (4 calls/iter) | 1 call per op |
| M6 | Statement-position for-loops build and discard their comprehension (`array_push_spread`/iter + `array_end`) | sieve: 4,999-null list | 1 call + amortized alloc per iter |
| M7 | Typed-array element ops are runtime calls (`item_at`+`it2d`, `fn_array_set`) despite `ensure_typed_array` proof; `has_typed_array_param` disables `generate_native` | nbody: 131 calls in one body | 2 calls per read, 1 per write vs 1 instruction |
| M8 | Per-call ceremony: `side_stack_ensure` call, root-frame `memset` (80–200 B), number-frame reserve, unconditional 2-call epilogue; dead prologue loads; duplicate root stores | all functions | ~2–4 calls + ~30 instrs per invocation |

Attribution by row class:

| Class (Result18 rows) | Dominant mechanisms |
|---|---|
| micro loops: sieve 30x, permute 31x*, queens 27x, towers 26x, bounce 31x | M4+M5+M6, M1/M2, M8 |
| float arrays: nbody 52x, spectralnorm 48.5x, fft 39.4x | M7, M2 (float via number stack), M8 |
| recursion: fib 7.1x, ack, tak, cpstak | M1+M2+M3, M8 |
| strings/data: base64 88.6x, levenshtein 37x | M5+M7 analogues on strbuf/array (not yet dissected) |
| near ceiling: mandelbrot 1.6x, regexredux | — (control; library-bound) |

\* permute/bounce measured against their own C ports; gap ratios from Result18.

---

## 3. Tuning proposals

Ordered by measured leverage. A, B, C are independent; each is separately
land-able and gate-able. The unifying principle is the one the prior-art
section (§11 of the dual-func doc) named: **a type must pin representation,
not add a check**. The DLS 2019 transient result (avg overhead 2.21x → 6%
purely by deleting statically-redundant checks) is this exact shape.

### T-A. Annotations pin representation (kills M1, M2, M3)

- **A1 — Elide statically-true boundaries. LANDED 2026-08-01.** At a typed
  declaration, assignment, or call argument, when the boundary can neither
  reject nor convert, emit no `lambda_type_check` and no error branch.
  This is the static complement of DF8 (which handles the *dynamic*-entry case
  in the callee); DF2 already states the principle ("after that check the
  representation is known… not emitted").

  **Two things the implementation had to get right, both found by the gate:**

  1. **Proven ≠ redundant.** `lambda_type_check` returns the Item that
     `runtime_type_admit_value` produced — it *widens* `int` to `float` and
     re-packs map shapes. Eliding on `STATIC_BOUNDARY_PROVEN` alone skipped
     the widening in `var x: float = <int>` and made mbrot/permute/nqueens
     compute wrong answers. Redundancy therefore also requires that admission
     be the identity: both sides unadorned global scalar carriers of the same
     TypeId (`lambda_boundary_is_redundant`, build_ast.cpp — kept next to the
     relation it refines rather than re-derived in the transpiler).
  2. **The boundary was also the unboxing point.** At a *declaration* it is
     where a boxed initializer acquires the binding's declared carrier
     (`checked_declaration_boundary` gates the `emit_unbox`). Dropping check
     and unbox together wrote an Item into a native lane — precisely the
     failure the `9 - len(s)` comment in that function already describes.
     The elided path keeps the unbox and drops only the call. At *assignment*
     and *call-argument* sites no repair is needed: the checked path also left
     `val_tid` as ANY, so downstream sees the same representation either way.

  **Measured (MIR-typed, median of 3, release):**

  | row | v18 | master pre-A1 | post-A1 |
  |---|---:|---:|---:|
  | r7rs/sum | 4.39 | 21.5 | **4.13** |
  | r7rs/mbrot | 0.961 | 2.52 | **0.883** |
  | r7rs/fib | 7.79 | 17.3 | 9.03 |
  | r7rs/ack | 24.5 | 45.5 | 30.2 |
  | r7rs/cpstak | 1.24 | 3.87 | 2.69 |
  | r7rs/nqueens | 1.60 | 6.13 | 5.53 |
  | awfy/nbody | 82.1 | 30.0 | 21.0 |

  With A2 (below) the R7RS typed geomean is **0.588x of pre-fix master**
  (41% faster); distance to the v18 snapshot closes **2.36x → 1.39x**, i.e.
  **62% of the `274625d56` regression recovered**. `sum` (21.5 → 3.99) and
  `mbrot` (2.52 → 0.855) are fully back, both now ahead of v18.
  Gate: `make test-lambda-baseline` 3709 passed / 1 failed, the single failure
  being `MirRatchetTest/js_tune6_exact_collection`, proven pre-existing by
  re-running it with these changes stashed.
- **A2 — Checked-unboxed int arithmetic. PARTIALLY LANDED 2026-08-01.** Keep
  the biased INT53 overflow check (2 instrs, well-predicted — this is *not*
  the cost); on the fast path keep the result as raw i64. Box only in the cold
  overflow branch.

  Implemented by parameterizing the existing flex-int lowering on a
  `native_int_out` flag (`transpile_binary_out`) rather than duplicating it,
  and wiring one consumer: a flex-int `int` argument passed to a **native int
  parameter** (`f(n - 1)`). The promote lane deliberately keeps
  box-float-then-`it2i` — byte for byte what the consumer would have applied
  to the Item it used to receive — so out-of-INT53 behaviour is unchanged and
  **O1 stays untouched**.

  Gain is real but small: ~2–3% (fib 9.03 → 8.78, sum 4.13 → 3.99,
  nqueens 5.53 → 5.39). The reason matters: for `fib` the residual is **M3,
  not M2** — the native body still has a *boxed return*, so
  `fib(n-1) + fib(n-2)` is still an `fn_add` dispatch. **A3 is now the
  binding constraint for the recursion class, not A2.**

  Remaining A2 consumers not yet wired: declaration/assignment initializers
  into a native int local, and `for`-range bounds (the latter subsumed by
  T-B1).
- **A3 — Native returns for native bodies.** `NativeFuncInfo.return_mir`
  already exists ([transpile-mir.cpp:15318](../lambda/runtime/transpile-mir.cpp:15318));
  make the native body actually return the native representation so
  `fib(n-1) + fib(n-2)` compiles to `adds`, not `fn_add`. This is dual-func
  O3/DF9 territory — the entry-equivalence test (§8 of that doc) is the gate.

### T-B. Loop lowering (kills M4, M5, M6) — **largest single lever**

- **B1 — Native counted `for`.** `for i in a to b` with int-typed endpoints →
  i64 induction variable, native compare/branch, no Range object, no iterator
  calls. This alone de-poisons the body: with `i` unboxed, existing inference
  keeps `i-1`, `flags[i-1]`, `k+i` in native lanes and M5 evaporates for free.
  The whole AWFY micro cluster (sieve/permute/queens/towers/bounce, 26–31x) is
  this one pattern.
- **B2 — No dead comprehensions.** In statement position, emit no
  `array_spreadable`/`array_push_spread`/`array_end`. (The prior for-stam fix
  addressed void-body boxing; the spreadable-list scaffolding is still
  emitted.)

### T-C. Typed-array direct addressing (kills M7)

- **C1 — Inline element ops on proven ArrayNum.** After `ensure_typed_array`
  (or when the static type says `float[]`/`int[]`), lower `a[i]` to bounds
  check + `d:(base, idx, 8)` load into a d-register, and `a[i] = x` to the
  store. Read: 2 calls → ~3 instrs. Write: 1 call → ~3 instrs. Floats stay in
  registers, which also removes the `push_d`/adopt/restore stack detours (41
  calls in `advance`).
- **C2 — Remove `has_typed_array_param` from the native-gate disqualifiers**
  once C1 exists (the exclusion is currently rational: the native body
  couldn't do anything useful with the array anyway).
- Together these target the top of the gap table: nbody 52x, spectralnorm
  48.5x, fft 39.4x, plus TS-3's measured "annotations make it slower".

### T-D. Call-ceremony diet (shrinks M8)

- **D1a — Inline the side-stack ensure.** The prologue already contains the
  fast path; the call is redundant with it. Today every function emits
  `call lambda_side_stack_ensure_for(runtime, R, N)` *followed by* inline
  top+size vs limit compares ([side_stack.c:111](../lambda/runtime/side_stack.c:111)).
  On POSIX the region is `mmap`ed demand-paged at reserve, so
  `committed == limit` from first bind and the call does nothing the inline
  compares don't ([side_stack.c:52](../lambda/runtime/side_stack.c:52)); it is
  load-bearing only for (a) first-call binding and (b) Windows
  `VirtualAlloc(MEM_COMMIT)` watermark advance. The recipe:
  1. switch the inline compares from `side_*_limit` (Context offsets 96/128)
     to `side_*_commit_limit` (88/120) — same instruction count, different
     load offset;
  2. drop the unconditional call;
  3. the overflow branch goes to a cold block: call the existing ensure (now
     a `grow` slow path — binds on first use, commits on Windows, `false` on
     true overflow), retry the inline check on success, fall into the
     existing `lambda_stack_overflow_error` exit on failure;
  4. first-call binding needs no special case: an unbound Context has
     `top == commit_limit == NULL`, so `top + slots > commit_limit` routes the
     first call through the slow path naturally.
  Fast path grows by zero instructions and loses one FFI call per function
  invocation (~10–20 cycles + branch/I-cache pressure).
- **D1b** — Skip the root-frame `memset` when every slot is provably stored
  before the first GC point (fib stores all live slots before any call), or
  zero only the live-across-call subset. For an 80-byte frame the `memset`
  *call* costs more than the 10 inline zero-stores it performs.
- **D2** — Conditional epilogue: skip `adopt_scalar_home`/`restore_number_frame_top`
  when the returned Item is self-contained (tagged int/bool/inline double) and
  the number frame provably didn't grow.
- **D3** — Drop dead prologue loads (`type_list`, `heap_ptr`, `gc_ptr` when
  unused) and duplicate root stores. Cheap; also shrinks MIR budgets.

### Expected recovery (order-of-magnitude, per class)

| Class | Now | With | Expected |
|---|---:|---|---:|
| fib-class | 7.1x | A1+A2+A3+D | ~1.5–2x |
| sieve-class | 26–31x | B1+B2+A2 (+C1 for generic arrays later) | ~3–5x |
| nbody-class | 39–52x | C1+C2+A2 | ~4–8x |
| mandelbrot-class | 1.6x | D | ~1.3x |

If those land, the 9.48x geomean should compress to roughly **2–3x**, which is
about where mature statically-typed-subset compilers sit relative to C on
mixed suites (cf. mypyc, Static Hermes typed paths).

### What NOT to do

- **No inline caches in Lambda script — DECIDED 2026-08-01, recorded as
  [`Lambda_Design_Compiling.md`](Lambda_Design_Compiling.md) LC1.** Stated in
  lane/site terms: specialized lowerings (declared, or inferred behind a
  DF-guard) have no dynamic sites by construction; open sites in Lambda —
  untyped functions, partially typed functions' open sites, and boxed
  fallback bodies alike — get plain runtime dispatch now and multi-version /
  guard hoisting later (dual-func §10, DF16), never ICs; LJS keeps its ICs
  (no type system to specialize against). Accepted cost: shape dispatch is
  pushed onto the type system, which makes the TS-3 fix (T-A/T-C above)
  load-bearing. Full rationale (guard-amortization, closed dispatch space,
  immutable-code/module-cache, union completeness, Julia precedent) in LC1.
- Don't add more declared types to benchmark sources to compensate — until
  T-A/T-C land, annotations are net-negative (TS-3), and the `*2.ls` corpus
  already demonstrates it.
- Don't touch the INT53 overflow *semantics* to buy speed (O1 stays open; A2
  doesn't need it resolved).

---

## 4. Gates

- `make test-lambda-baseline` 100% per CLAUDE, per phase.
- MIR emission budgets (`test/mir/mir_budgets.json`) re-baselined with
  per-gate justification — T-B and T-D should *shrink* budgets materially.
- Entry-equivalence corpus from the dual-func doc §8 once A3 changes return
  representations.
- Re-run the Result18 native columns (`run_benchmarks.py -e mir,c2mir,go
  --typed`) after each pillar; the per-class table above is the scorecard.
- P3 forced-GC sweep after D1 (the memset elision is a GC-precision change —
  a root slot scanned before first store would read garbage; the
  store-before-scan proof is the invariant).

## 5. Reproduction

```sh
# Lambda MIR for a typed benchmark (debug build dumps automatically)
./lambda.exe run test/benchmark/r7rs/fib2.ls && cp temp/mir_dump.txt temp/fib_lambda.mir
# C2MIR's MIR for the matching C port
./mac-deps/mir/c2m -S test/benchmark/r7rs/c2mir/fib.c -o temp/fib_c2mir.mir
```

Dumps studied for this analysis: `temp/{fib,sieve,nbody,mandelbrot}_lambda.mir`
against `temp/{fib,sieve}_c2mir.mir` (nbody/mandelbrot C ports read from
source, `test/benchmark/awfy/c2mir/*.c`).
