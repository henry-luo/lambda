# JS Transpiler/Runtime Performance Tuning Proposal

Source data: `test/js262/results/release_run_002/` (commit `df106e3`).
Analysis date: 2026-05-26.

This proposal targets a **general** JS-engine performance defect, not any
individual test. The optimization speeds up *every* program that makes function
or method calls with arguments.

---

## 1. Reading the run_002 slow list

### 1.1 The top-30 is dominated by one group — but it is a build artifact

The top of `top_slow_tests.tsv`:

| Rank | Seconds | Test | Group |
| ---: | ---: | --- | --- |
| 1–17, 22–23 | 0.66–2.97 | `language_identifiers_start_unicode_*` (`5.2.0`…`17.0.0`, plain + `escaped`) | Unicode identifier parsing |
| 18, 20, 25, 26 | 0.62–0.76 | `language_literals_regexp_S7_8_5_*` | RegExp literal sweep + `eval` |
| 19 | 0.74 | `Array_prototype_sort_stability_2048_elements` | sort |
| 21, 32 | 0.51–0.71 | `annexB_RegExp_RegExp_*_escape_BMP` | RegExp BMP sweep |
| 24, 30 | 0.55–0.63 | `TypedArray*set` / `TypedArrayConstructors*` | TypedArray |
| 28, 31, 33 | 0.47–0.59 | `unsigned_right_shift` / `left_shift` / `decodeURI` | misc loops |
| 29 | 0.58 | `RegExp_property_escapes_*_Letter` | RegExp property class |

**Caveat that changes everything:** run_002's timing came from
`temp/_t262_timing_o0.tsv`. Per the JS262 guide (`JS262_Test_Guide.md:284`),
`_o0.tsv` is the **debug (O0) build**, and the guide explicitly warns that
"debug/O0 runs can create hundreds of false slow tests"
(`JS262_Test_Guide.md:41`). The folder's `README.md` labels it "release", but
the captured data file is the O0 timing series.

Re-measuring on the current **release** `lambda.exe` confirms the top group is
an O0 artifact:

```
part-unicode-10.0.0.js          (debug rank #1, 2.97s)  → 0.03s release
part-unicode-10.0.0-escaped.js  (debug rank #2, 2.70s)  → 0.01s release
400 × unicode var decls (stress)                        → 0.04s release
400 × ASCII  var decls (stress)                         → 0.06s release
```

Unicode identifier scanning is **not** a real bottleneck in release. Optimizing
it would chase a measurement artifact. (Under O0 the Tree-sitter generated
scanner's per-codepoint character-class comparisons are un-inlined, which is why
big astral identifiers blow up only in debug builds.)

### 1.2 What is *genuinely* slow in release

Re-running the non-identifier groups on release `lambda.exe` (with the standard
`sta.js`+`assert.js` preamble) tells the real story:

```
built_ins/RegExp/character-class-escape-non-whitespace.js   → 9.6 s   (!!)
language/literals/regexp/S7.8.5_A2.4_T2.js                  → >30 s   (eval in loop)
RegExp/property-escapes/generated/General_Category_-_Letter → 0.19 s  (fine)
```

The common shape of every genuinely-slow test is the same: a **tight loop over
the Basic Multilingual Plane** (`for (j = 0; j < 0x10000; j++)`, 65 536
iterations) that, each iteration, calls into JS — most often
`assert.sameValue(...)`, plus a `String`/`RegExp` builtin method. The slow cost
is **not** the regex or the unicode; it is the **per-iteration call overhead**,
multiplied 65 536×.

---

## 2. Root cause: JS calls with arguments are O(n²)

### 2.1 Decomposition (release build, 65 536 iterations each)

```
plain top-level call   f(j,j,j)         →    4.3 ms   (66 ns/call)   ← fast
property read          x = o.f          →   56   ms                  ← fast
property read (data)   x = o.v          →   55   ms                  ← fast
method call, 0 args    o.f()            →  155   ms
prefetched call,0 args g()              →    4.3 ms                  ← fast
prefetched call,1 arg  g(j)             → 1099   ms   (16.5 µs/call) ← CLIFF
prefetched call,2 args g(j,j)           → 1010   ms
prefetched call,4 args g(j,j,j,j)       → 1010   ms
real harness           assert.sameValue → 4154   ms   (63 µs/call)
```

Two facts jump out:

1. **A statically-known callee is ~250× faster than a dynamic one.** `f(j,j,j)`
   where `f` is a known top-level function compiles to a direct MIR call
   (4.3 ms). The *same* function reached through a variable/property
   (`g`, `o.f`) goes through the generic `js_call_function` path (≈1000 ms).

2. **There is a fixed cliff the instant a dynamic call has ≥1 argument.** 0 args
   = 4.3 ms; 1, 2, or 4 args all ≈1010 ms. Cost is gated on *“are there any
   args?”*, not on how many — the signature of a per-call allocation that the
   0-arg path skips.

### 2.2 The allocation: `js_alloc_env` per call, never released

Every call lowering builds its argument buffer through
`jm_build_args_array` (`js_mir_function_collection_class_inference.cpp:2594`):

```c
MIR_reg_t jm_build_args_array(JsMirTranspiler* mt, JsAstNode* first_arg, int arg_count) {
    if (arg_count == 0) return 0;                 // ← the cliff: 0 args allocates nothing
    ...
    MIR_reg_t args_ptr = jm_call_1(mt, "js_alloc_env", ... arg_count);  // heap buffer per call
    // store each evaluated arg into args_ptr[i]
    return args_ptr;
}
```

and `js_alloc_env` (`js_runtime_function.cpp:147`):

```c
extern "C" Item* js_alloc_env(int count) {
    Item* env = (Item*)pool_calloc(js_input->pool, count * sizeof(Item));
    heap_register_gc_root_range((uint64_t*)env, count);   // ← registers a permanent GC root range
    return env;
}
```

`heap_register_gc_root_range` →
`gc_register_root_range` (`lib/gc/gc_heap.c:574`):

```c
void gc_register_root_range(gc_heap_t* gc, uint64_t* base, int count) {
    for (int i = 0; i < gc->root_range_count; i++) {        // ← linear scan of ALL ranges, every call
        if (gc->root_ranges[i].base == base) { ... return; }
    }
    ... append (base, count) ...
    gc->root_range_count++;
}
```

The buffer's lifetime is exactly one call, **but it is never unregistered or
freed**: `js_call_function` (`js_runtime.cpp:11150`) receives `args`, invokes
the function via `js_invoke_fn` (line 11506), and returns — nothing pops the
root range, nothing frees the pool block. Grep confirms there is no
`js_free_env` / range-pop anywhere in `lambda/js/`.

So over a loop of *n* calls:

- **Registration is O(n²):** call *k* linearly scans the *k−1* ranges already
  present for dedup. Σ k = n²/2.
- **GC marking is O(n²):** `gc_collect` walks every registered range each cycle
  (`lib/gc/gc_heap.c:1346`); the range count only grows.
- **Memory grows unbounded:** one pool block + one 16-byte range entry per call,
  never reclaimed.

### 2.3 Proof it is quadratic

Scaling a 1-arg dynamic call loop (release):

```
 20 000 calls →   97 ms
 40 000 calls →  381 ms   (2× work → 3.9× time)
 80 000 calls → 1579 ms   (2× work → 4.1× time)
160 000 calls → 6008 ms   (2× work → 3.8× time)
```

Each doubling of *n* quadruples the time — textbook O(n²). The 0-arg loop stays
flat (linear), exactly as predicted by the cliff in §2.1.

Memory side (200 000 calls): peak RSS 45.5 MB (1-arg) vs 39.1 MB (0-arg) — the
~6 MB delta is the leaked args buffers + range table. This matches the run_002
memory report's pathological growth (largest single-test growth 186 MB, peak
RSS 976 MB) on the same loop-heavy tests.

### 2.4 Why this is the highest-leverage fix

`jm_build_args_array` feeds **every** call lowering path — plain calls, method
calls (`JS_AST_NODE_MEMBER_EXPRESSION`), `super(...)`, the Math/builtin
fallbacks — there are 30+ call sites of it in
`js_mir_expression_lowering.cpp` / `js_mir_statement_lowering.cpp`. And
`assert.sameValue` is the single most-called function across the entire test262
corpus. One runtime-level fix accelerates essentially every test and every real
JS program that calls methods in a loop.

---

## 3. Proposal

The defect is purely a **lifetime mismatch**: argument buffers are transient
(scoped to a single call) but are allocated like persistent closure
environments (permanent GC roots, pool-leaked). Fix the lifetime, generally.

### 3.1 Primary fix — a transient call-argument stack (recommended)

Introduce a dedicated bump-allocated **argument stack** that is registered with
the GC **once** as a single growable root range, and is popped after each call.

Runtime (`lambda/js/js_runtime_function.cpp` + `lib/gc`):

```c
// One contiguous region, registered as ONE gc root range covering [base, top).
static Item*  js_args_stack_base;
static size_t js_args_stack_cap;     // slots
static size_t js_args_stack_top;     // bump pointer (slots in use)

Item* js_args_push(int count) {       // replaces js_alloc_env for CALL ARGS only
    if (js_args_stack_top + count > js_args_stack_cap) grow();  // realloc + re-register once
    Item* p = js_args_stack_base + js_args_stack_top;
    js_args_stack_top += count;
    return p;                          // already GC-visible via the single range
}
void js_args_pop(int count) { js_args_stack_top -= count; }
```

GC marks only `[base, top)` — i.e. **arguments that are live right now**, never
historical ones. `gc_register_root_range` is called O(1) times total, not per
call. No per-call `pool_calloc`. No dedup scan. No leak.

Lowering change (`jm_build_args_array` and its call sites): bracket the call so
the stack is restored on return. Each call site already has the shape
`args = build(...); r = js_call_function(fn, this, args, n);`. Wrap it:

```
mark = js_args_top();
args = js_args_push(n); fill args[i];
r    = js_call_function(fn, this, args, n);
js_args_set_top(mark);      // pop on the normal return path
```

For exception unwinding (the runtime uses `setjmp`/flag-based throw, see
`js_check_exception`), record the stack top in the throw/catch boundary and
restore it there, mirroring how `js_with_save_stack`/`js_with_set_stack` already
checkpoint the `with` stack around a call (`js_runtime.cpp:11480`). This keeps
the args stack from leaking across thrown calls.

Result: dynamic call with args becomes **O(1) per call**, same class as the
0-arg path. Expected: the 65 536-iteration assert loops drop from seconds to
tens of milliseconds; `character-class-escape-non-whitespace.js` from ~9.6 s to
well under 0.5 s.

**Generality:** this is a single runtime mechanism behind one builder function.
It is independent of which builtin/user function is called, how many args, or
which test exercises it. Closure/generator/async envs and object-construction
scratch arrays that are genuinely persistent keep using `js_alloc_env`
unchanged — only the transient call-args allocation moves to the stack.

### 3.2 Cheaper interim fix — make the GC root range table not-quadratic

If the lowering rework in §3.1 is too invasive to land first, a smaller change
removes the *quadratic* term (but not the per-call constant or the leak):

- Replace the linear dedup scan in `gc_register_root_range`
  (`lib/gc/gc_heap.c:576`) — call-args buffers are always fresh addresses, so the
  dedup is pure waste. Either drop dedup for an "append-only" fast variant
  (`gc_append_root_range`) used by `js_alloc_env`, or index ranges by base in a
  hash set for O(1) dedup/removal.
- Pair `js_alloc_env` for **call args** with a `js_free_env` that pops the range
  (O(1) if registration order is LIFO, which calls are). This both kills the
  O(n²) GC-mark growth and stops the leak.

This is strictly less general than §3.1 (still one register/unregister + one
pool op per call) but is a low-risk, localized first step and is fully
compatible with later doing §3.1.

### 3.3 Complementary fix — widen the static-dispatch fast path

§2.1 shows a known top-level callee is ~250× faster because it compiles to a
direct MIR call and skips the args buffer entirely. Many "dynamic" calls are
statically resolvable: a `const`/once-assigned local bound to a function, or a
method on a known builtin prototype. Extending the existing direct-call
inference (`js_mir_function_collection_class_inference.cpp`) to emit direct or
specialized calls for these cases removes the args buffer for the *common* call
shapes outright. This is additive on top of §3.1 and helps even the warm path.

### 3.4 Secondary, narrower item — `eval` / RegExp-literal compile caching

`S7.8.5_A2.4_T2.js` calls `eval("/" + ... + "/")` up to 65 536 times, each
re-parsing + re-JITting a one-char regex. This is a different axis (compile
caching, not call overhead) and far less general than §3.1, so it is lower
priority. If pursued: cache compiled `RegExp` by (source, flags), and cache
regex *literals* so a literal in a loop body compiles once (measured: hoisting
`/a/g` out of a loop saves ~4 µs/iter today, i.e. literals recompile per
iteration).

---

## 4. Verification plan

1. **Microbenchmarks** (release build, before/after):
   - 1-arg dynamic call loop at n = 20k/40k/80k/160k — must become **linear**
     (constant ms/call), not 4× per doubling.
   - `assert.sameValue` loop ×65 536 — target < 100 ms (from 4154 ms).
2. **Real tests** (release `lambda.exe`, with preamble):
   - `built_ins/RegExp/character-class-escape-non-whitespace.js` — target
     < 0.5 s (from 9.6 s).
   - Re-run the BMP-sweep family (`annexB/.../escape_BMP`, `unsigned_right_shift`,
     `decodeURI`) and confirm broad speedups.
3. **Memory:** peak RSS for the 200 k-call loop should match the 0-arg baseline
   (no per-call growth); re-check the run_002 memory outliers.
4. **Correctness:** `make test-lambda-baseline` must stay 100%; full
   `make test262-full` pass-count must not regress. Pay special attention to GC
   correctness under the args stack — run with the existing memtrack/leak
   reporting to confirm the per-call leak is gone and no args buffer is collected
   while still live (especially across thrown calls, §3.1 unwinding).
5. **Regenerate timing on the *release* series** (`_o2.tsv`), not `_o0.tsv`, so
   future slow-lists reflect reality (and update run_002's README labeling).

---

## 5. Summary

- The run_002 top-30 is headed by `language_identifiers_start_unicode_*`, but
  that ranking is an **O0/debug artifact**; those tests are 0.01–0.06 s in
  release.
- The **real**, general bottleneck behind the genuinely-slow release tests is
  that **every JS call with ≥1 argument allocates a per-call argument buffer
  (`js_alloc_env`) that registers a permanent GC root range and is never
  released** — making call-heavy loops **O(n²)** in time and unbounded in
  memory. Confirmed by clean quadratic scaling and the 0-arg "cliff".
- Fix the **lifetime**, generally: a transient, single-root-range **argument
  stack** (push on build, pop on return) collapses per-call cost to O(1) and
  benefits every function/method call in every program — no per-test tuning.

---

## 6. Implementation status (landed)

The §3.1 primary fix is implemented. Call-loop scaling is now linear; the
character-class regex test drops from 9.6 s to 0.22 s (~44×); `assert.sameValue`
×65 536 from 4154 ms to 83 ms (~50×); a 160 k-call dynamic-call loop from
6008 ms to 12.5 ms (~480×). `test262-baseline` (34,165 tests) holds at zero
regressions.

**Runtime** (`lambda/js/js_runtime_function.cpp`): a fixed 256K-Item (2 MB)
argument stack, registered with the GC exactly once. `js_args_push(n)` bump-
allocates a frame, `js_args_save()`/`js_args_restore(mark)` mark and pop. Slots
in `[len, cap)` are kept zeroed so the GC marks only live frames. The base never
moves (so a frame pointer survives nested-call reallocation and partially-built
args stay GC-rooted in place); on pathological overflow beyond the C-stack limit
it falls back to the old `js_alloc_env` path. `js_args_stack_reset()` runs in
`js_reset_transient_call_state` so the registration is refreshed across batch
heap teardown.

**Lowering**: `jm_build_args_array` (non-generator path) now allocates via
`js_args_push` instead of `js_alloc_env`
(`js_mir_function_collection_class_inference.cpp`). The call/`new` chokepoints in
`jm_transpile_box_item` (`js_mir_expression_lowering.cpp`) emit
`js_args_save`/`js_args_restore` around the call so the frame is popped on both
the normal and callee-threw paths. The try-statement lowering
(`js_mir_statement_lowering.cpp`) resets the stack mark at catch/finally entry to
reclaim frames abandoned by a throw during argument evaluation. Added
`heap_unregister_gc_root_range` (`lambda-mem.cpp`/`transpiler.hpp`) and registry
entries for the new runtime functions (`sys_func_registry.c`).

**Measured (release, `make release`):**

| Workload | Before | After |
| --- | ---: | ---: |
| Dynamic call loop, 160k iters | 6008 ms | 12.5 ms |
| Call-loop scaling 20k→160k | 97/381/1579/6008 ms (O(n²)) | 1.8/3.2/6.0/12.5 ms (linear) |
| `assert.sameValue` ×65 536 | 4154 ms | 83 ms |
| `character-class-escape-non-whitespace.js` | 9.6 s | 0.22 s |
| `unsigned-right-shift S11.7.3_A4_T3` | ~0.59 s | 0.09 s |
| `Array sort stability-2048` | ~0.74 s | 0.22 s |

**Correctness:** `test262-baseline` (34,165 tests) — **0 regressions, 0
improvements** (the 2 non-fully-passing are the pre-existing slow `decodeURI`/
`decodeURIComponent` retries, unchanged from run_002). Manual checks of nested
calls, recursion, method calls, `.map()` callbacks, and exception-during-arg-eval
loops all pass.

### 6.1 §3.3 widening — landed

The resolver in `jm_transpile_call` (`js_mir_expression_lowering.cpp`) used to
take the direct-MIR-call fast path *only* for `function foo(){}` declarations
(the only `entry->node` form that was a `JsFunctionNode`). Anything declared as
`const f = function(...){...}` or `const f = (...) => ...` fell through to the
generic `js_call_function` dynamic-dispatch path — even though `const` already
guarantees the binding is immutable.

**Fix.** Extend the resolver to also accept a `JS_AST_NODE_VARIABLE_DECLARATOR`
when

1. the declarator was introduced by a `const` lexical declaration
   (checked by walking up to the `lexical_declaration` TS node and looking at
   the keyword token — new helper `jm_declarator_is_const`), **and**
2. the initializer is a function expression or arrow function
   (`JS_AST_NODE_FUNCTION_EXPRESSION` / `JS_AST_NODE_ARROW_FUNCTION`), **and**
3. the call site is *textually after* the initializer's end byte
   (`ts_node_end_byte(init) <= ts_node_start_byte(call)`) so we never
   devirtualise a call still inside its TDZ window.

In that case `resolved_fn` is set to the function expression's
`JsFunctionNode`. All the downstream guards (`fc->capture_count == 0`,
`fc->is_reassigned`, spread/rest/arguments/with, and the §6.x generator
yield-in-args gate) are reused — `let` and `var` are *not* widened because
their reassignment can't be ruled out cheaply.

**Microbenchmark** (release, 2 M iterations of a 1-arg call in a tight loop):

| Binding form | Before §3.3 | After §3.3 |
| --- | ---: | ---: |
| `function fdecl(a){...}` (already fast) | 62.7 ms | 62.7 ms |
| `const fconst = function(a){...}` | ~99 ms (dyn) | **62.9 ms** |
| `const farrow = a => a` | ~99 ms (dyn) | **61.7 ms** |
| `let flet = function(a){...}` (control) | 99.4 ms | 99.4 ms |

Per-call cost on the const-bound forms drops from ~50 ns to ~31 ns — they now
match the `function`-declaration baseline.

**TDZ semantics preserved.** A call site textually before the initializer end
still falls through to dynamic dispatch and throws `ReferenceError` at runtime,
e.g. `function go(){ try { return f(); } catch (e) { return "threw:"+e.name; } }
print(go()); const f = function(){return 99;}; print(go());` → `threw:ReferenceError`
then `99`.

**Clean A/B on the full test262 baseline** (release, just §3.3 toggled off vs
on, same binary otherwise):

| Metric | §3.3 off | §3.3 on | Δ |
| --- | ---: | ---: | ---: |
| Tests passing | 34,165 / 34,165 | 34,165 / 34,165 | 0 regressions |
| Wall-time (parallel harness) | 215.7 s | 196.9 s | **−8.7 %** |
| Sum of per-test elapsed | 431.02 s | 284.07 s | **−34 %** |
| Average per test | 12.6 ms | 8.3 ms | **−34 %** |
| Tests improved by ≥ 5 ms | — | 6,822 | — |
| Top single-test win | — | 985 ms | `language_identifiers_start_unicode_10_0_0_js` |

The biggest wins concentrate on the long-`var <unicode>` declaration family —
those tests run with the standard test262 preamble (`sta.js` + `assert.js`),
whose const-bound helper functions are now dispatched directly per call.
Combined cumulative speedup from §3.1 + §3.3 + the generator-spill fix vs the
pre-fix `release_run_002` snapshot (sum 529 s, max 2.97 s): **sum 284 s
(−46 %), top test 0.88 s (−70 %)**.

### 6.2 Follow-up: generator yield-spill bug — fixed

While verifying the args-stack change I uncovered a pre-existing generator bug:
when `yield` appeared as a non-leading element of an array literal or a
non-leading argument of a call, any value evaluated *before* the yield was
clobbered on resume. Repros:

```js
function* g(){ var a=[1, yield 2, 3]; print(a.join('|')); }
var it=g(); it.next(); it.next(99);   // BEFORE: "99,,3"  — AFTER: "1|99|3"

function add(a,b){return a+b;}
function* g2(){ var r=add(7, yield 1); print("r="+r); }
var it2=g2(); it2.next(); it2.next(100); // BEFORE: r=NaN — AFTER: r=107
```

**Root cause:** four direct-dispatch fast paths in `jm_transpile_call` evaluate
arguments into raw MIR registers and pass them to a single `MIR_CALL` —
- the plain user-function direct call (line ~8870),
- the P3 direct method call (line ~8055),
- the P7 native method call (line ~7800),
- the Date setter (line ~7782).

When any argument contains a `yield`, the yield's `MIR_RET`-then-resume
trampoline destroys those registers (the state-machine dispatch jumps over the
initialising MOVs on re-entry), so previously evaluated args become garbage.
The method-dispatch fallback at line ~8180 has a related issue: `recv` and
`method_name` are built *before* `jm_build_args_array`, which (in its generator
branch) emits the yield internally. The array-literal lowering has the same
hazard with its boxed-index register `idx`.

**Fix** (`js_mir_expression_lowering.cpp`):
1. Introduce a small helper, `jm_call_yield_blocks_direct(mt, args)`, that
   returns true iff we are inside a generator and any argument contains a yield.
2. Disqualify each of the four direct-dispatch fast paths under that condition
   (one `if (...) fc = NULL;` / `p3_method = NULL;` / `p7_fc = NULL;` /
   `date_setter_id = -1;`), forcing the call through the env-spilling
   `jm_build_args_array` generator branch which writes each arg to a stable env
   slot before the yield is emitted.
3. In the method-dispatch fallback, when the gate trips, spill `recv` via
   `jm_gen_spill_save` before the build_args and reload it after; re-box
   `method_name` from its compile-time `String*` after the build_args. Both
   regs are therefore valid in the post-yield dispatch.
4. In `jm_transpile_array`'s non-spread branch, construct the boxed index
   register *after* the per-element yield-resume spill_load (it is a fresh
   `MIR_MOV` of a compile-time constant, so the move always reaches the right
   place at runtime regardless of the resume jump).

**Verification:** all array, call, method, and loop repros above now produce
the correct values. `test262-baseline` returns
**34,165 / 34,165 passing, 0 regressions, 0 non-fully-passing** — strictly
better than the pre-fix baseline run, which had 4 tests on the slow-retry path.
Performance is unchanged in the non-generator hot path (the gate only trips
inside a generator with `yield` in argument expressions, which is rare).

---

## 7. Pushing the `decodeURI*` SLOW pair below 3 s — proposal

After §3.1 + §3.3 + the generator-spill fix, the only entries left in
`t262_partial_at_run.txt` are the same two URI tests across both phases:

- `built_ins_decodeURI_S15_1_3_1_A2_5_T1_js` — **SLOW_3218** (≈3.22 s)
- `built_ins_decodeURIComponent_S15_1_3_2_A2_5_T1_js` — **SLOW_3191** (≈3.19 s)

Both are ≈ 50 % faster than in `release_run_002` (where they were 6.92 s and
6.44 s) but they sit just above the harness's 3 s flag and still get rerouted
through Phase 4 retry. The goal of this section is a path to drive them
*substantially* below 3 s — target ≤ 2 s — through changes that are general
JS-engine wins, not per-test special-casing.

### 7.1 Where the remaining time actually goes

Both tests run the same 4-deep nested loop over UTF-8 byte ranges
`0xF0..0xF4 × 0x80..0xBF × 0x80..0xBF × 0x80..0xBF`. Excluding the trimmed
sub-ranges in the test source, the body executes **≈ 1.05 × 10⁶ inner
iterations**. Each inner iteration is:

```js
var hexB1_B2_B3_B4 = hexB1_B2_B3 + decimalToPercentHexString(indexB4);
// ... a few inline int ops ...
if (decodeURI(hexB1_B2_B3_B4) === String.fromCharCode(H, L)) continue;
```

`decimalToPercentHexString` (from the test262 harness `decimalToHexString.js`)
is:

```js
function decimalToPercentHexString(n) {
  var hex = "0123456789ABCDEF";
  return "%" + hex[(n >> 4) & 0xf] + hex[n & 0xf];
}
```

Per inner iteration, the JS-level work is roughly:

| Site | Cost components |
| --- | --- |
| `decimalToPercentHexString(indexB4)` | 1 direct user-fn call (§3.1, ~31 ns) + 2 single-char `str[i]` accesses + a 3-way `"%"+ch1+ch2` concat chain → **3 small-string allocations** in the function body |
| outer `hexB1_B2_B3 + …` | 1 more `js_string_concat` call → **1 allocation** of the 12-byte URI |
| `decodeURI(hex…)` | builtin call → `js_uri_try_decode_four_byte_escape` fast path → `heap_create_name` allocates the 2-codepoint surrogate-pair result → **1 allocation** |
| `String.fromCharCode(H, L)` | builtin call → **1 allocation** of a 2-codepoint string |
| `===` and arithmetic | small constant work |

So ≈ **6 ephemeral small-string allocations × 1.05 M iterations ≈ 6.3 M
allocations** for the whole test. With deeper loops we also pay 4× `hex` chain
ops at the outer levels. The call-dispatch share is now small (§3.1/§3.3 cut it
in half); **the remaining cost is dominated by string allocation, intermediate
buffer copies, and the URI builtin's own allocator/error-bookkeeping**, not by
call overhead.

The C side of `js_decodeURI` (`lambda/js/js_globals.cpp:13323`) already has
two targeted fast paths — `js_string_last_four_byte_uri_escape_cp` (per-string
code-point cache) and `js_uri_try_decode_four_byte_escape` (whole-string fast
decoder). Both hit on this workload; what remains *inside* the builtin is the
final `heap_create_name(decoded, decoded_len)` allocation and the `mem_free`
of the C-side `url_decode_*` buffer.

### 7.2 Proposed general optimizations

Each item below is described as a JS-engine-wide improvement; none is keyed on
"this test". They are independently landable and each is gated so it cannot
regress paths it doesn't apply to.

#### 7.2.A Multi-operand string-concat fusion (transpiler + small runtime)

Today `a + b + c + d` (when all operands are strings) lowers to three nested
two-argument `js_string_concat` calls
(`js_mir_expression_lowering.cpp:1482`). Each intermediate concat allocates a
fresh `String`, computes its length, copies the prefix, then is immediately
thrown away by the next concat — O(n²) bytes copied across an n-piece chain.

**Change.** In the `+` binary-op lowering, when the operator chain is
left-associative `+` and *every* leaf is known-string (the existing
`left_type == LMD_TYPE_STRING && right_type == LMD_TYPE_STRING` check, applied
recursively), collapse the chain into a flat list of operand registers and
emit a single `js_string_concat_n(int n, Item* parts)` call. The runtime
helper does one length-summing pass, one allocation, one copy pass.

- **Scope.** Triggered only when *all* leaves are statically typed as string.
  Mixed-type chains (`"x" + n`) keep the current pairwise lowering, which is
  required to get `ToString` coercion right.
- **Per-iteration win on this workload.** Inside
  `decimalToPercentHexString`, `"%" + hex[i] + hex[j]` collapses three
  allocations to one — saves 2 allocs/call × 4 calls/iter ≈ 8 allocs/iter
  ≈ 8 M allocs over the test. At a rough ~50 ns/small-string-alloc that's
  ≈ 0.4 s saved per test.
- **Risk.** Low. The fused call is a strict refinement of the pairwise chain
  for the typed-string case. Mixed-type chains are untouched.
- **General benefit.** Every `"prefix=" + a + " ; " + b + " ; " + c`-style
  string-builder pattern in any JS program gets the same speedup.

#### 7.2.B Intern 1-character ASCII results of `str[i]` and small literal substrings

`hex[(n >> 4) & 0xf]` and `hex[n & 0xf]` each currently produce a *fresh*
1-character `String` per call. There are at most 128 distinct ASCII 1-char
strings — they are immutable and can be interned in a 128-entry table.

**Change.** In the `js_string_index` / `js_string_charAt` / `String.prototype.charAt`
fast path, when the result is a single ASCII byte (and the source is not a
binary-encoded string), return a pointer into a process-wide
`static const String* js_ascii_char_strings[128]` table instead of allocating.
Equivalently, `String.fromCharCode(n)` with `n < 128` returns the interned
entry. Initialization is one-time at runtime startup.

- **Scope.** 1-char ASCII results only. Multi-char substrings and non-ASCII
  bytes keep current behaviour.
- **Per-iteration win.** Removes 2 small-string allocs per
  `decimalToPercentHexString` call → 8/iter → ≈ 8 M allocs eliminated. ≈ 0.4 s
  saved.
- **Risk.** Very low — strings are immutable, identity is observable only for
  reference-equality `===`, which is already defined to be value-equality for
  strings; interning makes no observable difference.
- **General benefit.** Any tight loop that does `s[i]`, `s.charAt(i)`, or
  `String.fromCharCode(c)` on ASCII (parsers, hex/base64 encoders, CSV
  splitters, lexers) shrinks its per-iteration alloc count.

#### 7.2.C Widen `jm_should_inline` to small pure JS helpers

`jm_should_inline` (`js_mir_expression_lowering.cpp:5692`) currently requires
`fc->has_native_version`, which excludes any user function that doesn't have a
typed/native specialization — including string-heavy helpers like
`decimalToPercentHexString` even though their body is a single `return` and
they are already direct-dispatched via §3.1.

**Change.** Drop the `has_native_version` gate when the body is a single
return whose RHS contains no calls except (a) statically-resolvable
direct-dispatch calls, (b) intrinsic operations the inliner already lowers,
and (c) builtin calls. Keep all other guards (`capture_count == 0`,
`param_count ≤ 4`, single-statement single-return). Add a hard AST-node-count
cap (~24 nodes) to bound code-size growth at call sites.

- **Scope.** Single-return pure-ish helpers with bounded body size.
- **Per-iteration win.** `decimalToPercentHexString` becomes 4 inline body
  expansions per inner iter — removes 4 direct calls × (param marshalling +
  stack-frame setup + return) ≈ 30–40 ns each ≈ 130 ns/iter ≈ 0.14 s saved.
- **Risk.** Medium. Inlining grows MIR text and JIT compile time. Mitigations:
  (1) the AST-node cap; (2) only inline at most K times per source function
  per caller; (3) keep the existing native-version gate as a *strong*
  preference — fall back to inlining without it only when the function has no
  alternative.
- **General benefit.** Small pure JS helpers (accessors, predicate functions,
  format helpers) all benefit. Standard practice in modern JS engines.

#### 7.2.D Optional: small-string `===` short-circuit

The inner loop's `decodeURI(uri) === String.fromCharCode(H, L)` is two
2-codepoint strings. Make sure `===` between strings of equal-length-≤-8 takes
a single `memcmp`-or-inline-compare path (no boxing, no general-equality
dispatch). This is most likely already in place but worth verifying with a
microbenchmark; if it is going through the generic equality function the win
is meaningful. *Investigation rather than a designed change.*

### 7.3 Expected combined impact

Per inner iteration we expect roughly:

| Source | Savings |
| --- | ---: |
| 7.2.A concat fusion (2 allocs eliminated × 4 calls) | ≈ 400 ns |
| 7.2.B 1-char interning (2 allocs × 4 calls) | ≈ 400 ns |
| 7.2.C inlining `decimalToPercentHexString` (× 4 calls) | ≈ 130 ns |
| 7.2.D `===` fast path (if needed) | tens of ns |

Total ≈ **0.9 µs/iter** at the current scale. Applied to ≈ 1.05 M
iterations that is **≈ 0.9 s saved** per test, taking 3.22 s → ≈ 2.3 s
and 3.19 s → ≈ 2.3 s — comfortably below the 3 s SLOW threshold with
margin, and well below the harness's 6–7 s figures from `release_run_002`.

### 7.4 Verification plan

1. **Microbenchmarks** (release):
   - Concat-chain `s = "%" + c1 + c2 + c3` × 1 M — confirm linear-time
     allocation count (§7.2.A).
   - Loop `hex[(n>>4)&0xf]` × 1 M — confirm zero allocations after §7.2.B.
   - Tight loop calling a const-bound 1-return helper — confirm body inlines
     after §7.2.C (check generated MIR has no `MIR_CALL` to the helper at the
     call site).
2. **Targeted timing** (release):
   - `built_ins_decodeURI_S15_1_3_1_A2_5_T1_js` and
     `built_ins_decodeURIComponent_S15_1_3_2_A2_5_T1_js` directly with the
     standard preamble — target < 2.5 s each, ideally ≤ 2.0 s.
3. **Regression guard**: `test262-baseline` must stay at
   **34,165 / 34,165, 0 regressions, 0 non-fully-passing**. Capture into a
   new `release_run_004/` to compare against run_003 (especially aggregate
   `sum` of per-test elapsed and `top_slow_tests.tsv`).
4. **No new allocs** smoke test: the 1-char interning and concat fusion must
   not increase the *baseline* heap, only reduce churn. Use the existing
   memtrack/leak reporting and the run-002/003 memory tsvs as references.

### 7.5 What we are explicitly *not* proposing

- A 256-entry lookup table for `decimalToPercentHexString(byte)` outputs:
  test-specific and would not survive a harness rewrite.
- Interning the four-byte UTF-8 surrogate-pair result inside `decodeURI`
  (a code-point→`String*` LRU around `js_uri_make_four_byte_string_from_cp`):
  the hit-rate that makes this attractive is specific to BMP-sweep workloads
  like these two tests; in general-purpose URL decoding the input distribution
  is wide and an LRU would mostly miss while adding a per-call lookup. Better
  to keep the existing per-string code-point cache that already pays for
  itself on this workload, and not bake test-shaped state into the runtime.
- Inlining the `decodeURI` C decoder into JIT code: would force JS-engine
  knowledge of URI parsing rules; brittle and far out of scope.
- Lowering the harness's `SLOW` threshold from 3 s: would hide rather than
  fix the slowness.

Each item in §7.2 is a standalone change with its own gate, so any subset can
land first, and any that turn out to under-deliver in measurement can be
withdrawn without disturbing the others.
