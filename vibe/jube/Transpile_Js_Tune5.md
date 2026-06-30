# Transpile JS Tune5 — Array-Write Regression Diagnosis & Fix

Date: 2026-06-02
Status: closed (fix committed `352a1b4ae` "js performance regression fix")
Platform: Apple Silicon, macOS 15.7.1, release build (`make release`, `-O2`, stripped)

## Goal

Earlier Tune passes (Tune…Tune4) found broad cost centers and added narrow
generic fast paths. This pass is different: it started from the observation that
**current LambdaJS performance had regressed badly versus the last recorded
benchmark numbers**. The objective was to (1) confirm the regression against a
known-good baseline, (2) bisect it to a specific commit, (3) fix the root cause
without re-introducing the correctness behaviour the offending commit added, and
(4) prove zero js262 regression.

This pass deliberately avoids any test-specific shortcut: the fix is a generic
fast path gated on properties that the ES spec itself uses to short-circuit, with
a full fallback to the existing slow path.

---

## 1. How the Diagnosis Was Carried Out

### 1.1 Establishing the baseline to compare against

The LambdaJS benchmark driver is `test/benchmark/run_benchmarks.py`, engine key
`lambdajs` (runs `./lambda.exe js <file>`, median of 3, self-reported
`__TIMING__` exec time, excludes startup/JIT).

Two historical records exist:

- `test/benchmark/Overall_Result6.md` — the R6/R7 LambdaJS report (2026-03-24).
  Its absolute numbers use a different methodology (sub-millisecond figures that
  in several cases reflect partially-broken / early-exiting runs), so it is not a
  reliable absolute baseline.
- `test/benchmark/benchmark_results_v3.json` — a full saved run dated 2026-04-12,
  produced by the **same** `run_benchmarks.py` driver. This is the correct
  apples-to-apples "last recorded result".

### 1.2 First measurement vs. the v3 baseline

A clean LambdaJS run on current `master` (62222b8c3) showed a broad slowdown,
with extreme outliers. Crucially, before trusting it, two confounders were ruled
out:

- **CPU contention** — the first run was accidentally launched alongside a
  parallel `make build-test`; the contaminated numbers were discarded and the run
  was repeated on an idle machine.
- **Changed workloads** — `git` confirmed that no benchmark `.js` source under
  `test/benchmark/{r7rs,awfy,…}` had changed since the v3 run, so identical work
  was being measured on both sides.

### 1.3 Confirming the regression is real (not v3 noise)

To remove any doubt that the v3 JSON values were themselves valid, the Apr-12
commit `262176731` was checked out and rebuilt (`make release`). A six-benchmark
subset (`temp/measure_subset.sh`) was measured on the **same machine**, after
confirming via `git diff` that all six workloads were byte-identical between
Apr-12 and master:

| Benchmark | Apr-12 (`262176731`) | master (pre-fix) | Slowdown |
| --- | ---: | ---: | ---: |
| fib | 18.6 ms | 29.1 ms | 1.6× |
| tak | 1.41 ms | 2.84 ms | 2.0× |
| **sieve** | **0.27 ms** | **93.1 ms** | **≈350×** |
| base64 | 82 ms | 2,780 ms | 34× |
| deriv | 210 ms | 3,690 ms | 17.6× |
| gcbench | 2,343 ms | 41,530 ms | 18× |

This proved the regression was real, severe, and **not uniform** — array/loop
work (`sieve`) was hit hardest, allocation/object work (`gcbench`, `deriv`)
heavily, and pure recursion (`fib`) only mildly. That non-uniformity signalled
**more than one independent regression**.

### 1.4 Git bisect on `sieve`

`sieve` was chosen as the bisect signal: it is the most sensitive (≈350×) and the
fastest to measure (sub-ms when healthy). A `git bisect run` script
(`temp/bisect_sieve.sh`) built each candidate (`make build`, debug is sufficient
because the 350× cliff dwarfs the debug/release factor) and classified
`good` (<5 ms) / `bad` (≥5 ms) on the median of 3 runs.

Range: `262176731` (good) … `62222b8c3` (bad), 1572 commits, ~11 steps.

**First-bad commit: `060fa3251` "fixing es2020 tests" (2026-04-30).**

### 1.5 Root cause

`060fa3251` added an ES `OrdinarySet` inherited-accessor walk to the array-write
runtime helpers `js_array_set_int` and `js_property_set`. On **every** indexed
write it executed:

```
snprintf(idx_buf, …, index)            // stringify the index
js_get_prototype_of(array)             // allocates "Array"/"prototype" names,
                                       //   does property lookups — not cheap
js_find_accessor_pair_inheritable(...) // walk the prototype chain for a
                                       //   numeric-index accessor
```

For a tight array loop like `sieve` (`flags[k-1] = false` millions of times) this
is catastrophic. master had since added an `own_index_present` guard that skips
the *walk* for existing dense slots, but the `snprintf` and the per-write
`js_array_ta_proto_numeric_set()` call (which itself calls `js_has_own_property`
+ `js_get_prototype_of`) still ran on every write.

The change was also a **latent spec bug**: per ES `OrdinarySet`, when the index is
an existing **own writable data property**, the value is written directly and the
prototype chain is *not* consulted. The added code performed the inherited-accessor
walk before honouring the own data property, so an own element could be shadowed by
an inherited accessor.

---

## 2. What Was Changed / Tuned

Commit `352a1b4ae`, single file `lambda/js/js_runtime.cpp` (+29 lines).

A shared fast-path helper short-circuits the common case — writing an existing own
dense data element of a plain array — before any accessor / prototype / typed-array
work:

```c
// Fast path for writing an existing own dense data element of a plain Array.
// Per ES OrdinarySet, an own writable data property is written directly without
// consulting accessors, the prototype chain, or typed-array proto exotics.
// arr->extra==0 guarantees the array carries no indexed accessor / non-writable /
// sparse descriptors (those all live in the companion map), and is_content!=1
// excludes the arguments-exotic object, so every present dense slot is a plain
// writable data property.
static inline bool js_array_fast_own_dense_set(Item object, int64_t index, Item value) {
    if (get_type_id(object) != LMD_TYPE_ARRAY) return false;
    Array* arr = object.array;
    if (arr->extra != 0 || arr->is_content == 1) return false;
    if (index < 0 || index >= arr->length || index >= arr->capacity) return false;
    if (arr->items[index].item == JS_DELETED_SENTINEL_VAL) return false;
    arr->items[index] = value;
    return true;
}
```

Wired into both write entry points so every lowering path benefits:

- **`js_array_set_int`** — used by the non-strict `arr[i] = v` lowering. The
  helper is called immediately after `arr` is obtained, before
  `js_array_ta_proto_numeric_set` and the accessor walk.
- **`js_property_set`** — used by the strict-mode `[[Set]]` path (class-method
  bodies are strict, e.g. `sieve`'s inner loop). A guard at the very top handles a
  non-negative `LMD_TYPE_INT` key on a plain array, bypassing the
  symbol/marker/`js_ta_proto_chain_set` preamble, which is all no-op for such a
  key.

### Why it is correct

- `arr->extra == 0` ⇒ the array has **no** companion map, hence no indexed
  accessor, non-writable (`__nw_<i>`) or sparse descriptors. Every present dense
  slot is therefore a plain writable data property.
- `is_content != 1` excludes the arguments-exotic object (mapped-parameter
  aliasing keeps its own path).
- A present dense slot (`index < length`, `index < capacity`, not the deleted
  sentinel) is an **own data property**, which by `OrdinarySet` is written
  directly and never consults the prototype chain. So skipping the accessor/proto
  work is not merely an optimisation — it is what the spec requires, and it also
  removes the latent shadowing bug.
- Any case the fast path does not handle (sparse, accessors, length extension,
  typed arrays, holes, negative/symbol keys) returns `false` and falls through to
  the unchanged slow path.

---

## 3. Performance Result (Before → After)

Release build, median of 3, exec ms. "Apr-12" from `benchmark_results_v3.json`.

### Benchmarks the fix targets (array index writes)

| Benchmark | Apr-12 | master pre-fix | master + fix | Recovery |
| --- | ---: | ---: | ---: | --- |
| sieve | 0.17 ms | 93.1 ms | **0.82 ms** | **114× faster**, baseline restored |
| puzzle | 20.2 ms | 1,770 ms | **38.3 ms** | **46× faster** |
| permute | 37.1 ms | 222 ms | **51.6 ms** | **4.3× faster** |
| queens | 22.7 ms | 99.6 ms | **34.5 ms** | **2.9× faster** |
| towers | 69.1 ms | 238 ms | **96.6 ms** | **2.5× faster** |

(`sieve` does not fully return to 0.17 ms because the **read** path is now a
runtime `js_array_get_int` call rather than the inline load it had in April — a
separate, smaller delta — but the 350× write cliff is gone.)

### Benchmarks unaffected by this fix (confirm scope / no second cause touched)

| Benchmark | master pre-fix | master + fix |
| --- | ---: | ---: |
| base64 | 2,780 ms | 2,780 ms |
| deriv | 3,690 ms | 3,630 ms |
| gcbench | 42,030 ms | 41,530 ms |
| matmul | 4,480 ms | 4,510 ms |
| fib | 29.1 ms | 28.8 ms |

These are object/allocation/float-heavy and do not exercise dense array index
writes — they belong to the outstanding second regression (§5).

### Correctness — js262 baseline

`make`-equivalent run of `test/test_js_test262_gtest.exe --baseline-only
--batch-only --run-async`:

```
Fully passed: 39258 / 39258
Regressions:  0   (pass → fail)
Improvements: 0
```

Run twice (once on the verification build, once on the final committed binary);
both showed **0 regressions**. The single transient "non-fully-passing" entry
(`language_literals_regexp_S7_8_5_A2_1_T2_js`) is a pre-existing batch-timing
flake on a regexp-literal test that passed on retry and cannot be affected by an
array-write change.

---

## 4. Reproduction

```bash
# baseline rebuild for comparison
git checkout 262176731 && make release
bash temp/measure_subset.sh          # Apr-12 numbers

# current + fix
git checkout master && make release
python3 test/benchmark/run_benchmarks.py -e lambdajs -n 3 --no-save \
    -s r7rs,awfy,beng,kostya,larceny

# correctness
make -C build/premake config=debug_native test_js_test262_gtest -j8 \
    CC="ccache gcc" CXX="ccache g++"
./test/test_js_test262_gtest.exe --baseline-only --batch-only --run-async \
    --async-list=test/js262/test262_baseline.txt
```

---

## 5. Second Regression — object-literal `CreateDataProperty` (FIXED)

A **separate, independent** regression hit object/tree-allocation benchmarks. It is
**not** the array-write path (commit `060fa3251` did not touch it; the §2 fix left
these numbers unchanged):

| Benchmark | Apr-12 | pre-fix master | Slowdown |
| --- | ---: | ---: | ---: |
| gcbench | 1,957 ms | 41,530 ms | ~21× |
| deriv | 169 ms | 3,630 ms | ~17× |
| binarytrees | 86 ms | 1,730 ms | ~20× |

(`base64` ~34× and `matmul`/`collatz` ~1.5× are a *different* residual — `push` /
typed-array / float paths, not object literals — see §6.)

### Root cause (identified via profiling)

`sample` on a symbolicated debug build running `deriv` shows the hot path is
dominated by **string-key interning/hashing during object creation**:
`hash_fnv1a_32` (798) + `SIP64` (647, the name-pool hash) + `js_map_get_fast` (444)
+ `typemap_hash_lookup` (219), plus `__bzero`/`rpmalloc_heap_calloc` (object alloc).

The cause: **object-literal fields changed from `js_property_set` (Apr-12) to
`js_create_data_property` (master)**. The current `js_create_data_property` runs the
full `Object.defineProperty` machinery **per field**:

1. `js_new_object()` — allocates a throwaway descriptor object;
2. four `js_property_set(desc, "value"/"writable"/"enumerable"/"configurable", …)`
   — each `heap_create_name(...)` re-interns a constant string into the name pool
   (the `SIP64`/`find_string_by_content` cost);
3. `js_object_define_property(obj, name, desc)` — re-parses the descriptor.

So `{t:2, l:X, r:Y}` does 3 × (descriptor alloc + 4 interns + define-property). This
is the object-creation regression for `deriv`/`gcbench`/`binarytrees`.

The deriv regression is also **non-monotonic**: a clean-build bisect found a severe
~28 s spike at the May-9 merge `53fca1a10` (both parents ~640 ms — a genuine
cross-branch interaction, reproduced with clean builds, *not* a staleness artifact)
that was **later mostly fixed**, leaving the residual ~6–17× at master. A single
bisect over this range is therefore unreliable; the profiling root cause above is
the actionable lead.

### False start — `js_property_set` (reverted)

A first attempt delegated the common object-literal case to `js_property_set`. It
cut `deriv` 3.6 s → 1.2 s and `gcbench` 41.5 s → 13.5 s (≈3×) but **regressed 2704
test262 tests** and was reverted. Reason: `js_property_set` implements `[[Set]]`,
which is *not* equivalent to `CreateDataProperty`'s `[[DefineOwnProperty]]` —
`[[Set]]` honours inherited non-writable/accessor properties on the prototype chain,
whereas `CreateDataProperty` defines the own property unconditionally. The lesson:
the fast path must use a primitive that **does not consult the prototype at all**.

### The fix

A fast path in `js_create_data_property` (`lambda/js/js_globals.cpp`) that performs a
direct `[[DefineOwnProperty]]` via the **raw own-field store `map_put`** — the same
install the slow path ultimately performs for an ordinary default data descriptor,
but with no descriptor object, no attribute-name interning, and (crucially) **no
prototype consultation**:

```c
if (js_input && get_type_id(obj) == LMD_TYPE_MAP && get_type_id(name) == LMD_TYPE_STRING) {
    Map* m = obj.map;
    JsClass cls = js_class_id(obj);
    if (m && m->map_kind == MAP_KIND_PLAIN && (cls == JS_CLASS_NONE || cls == JS_CLASS_OBJECT)) {
        String* nm = it2s(name);
        if (nm && !(nm->len >= 2 && nm->chars[0] == '_' && nm->chars[1] == '_')) {
            bool key_exists = false;
            js_map_get_fast_ext(m, nm->chars, (int)nm->len, &key_exists);
            if (!key_exists && js_is_truthy(js_object_is_extensible(obj))) {
                map_put(m, nm, value, js_input);
                return obj;
            }
        }
    }
}
```

Guards keep it strictly equivalent to the slow descriptor path: ordinary plain
object (`MAP_KIND_PLAIN`, class `OBJECT`/`NONE`) excludes proxies / typed arrays /
String·Array·Date exotics; the `__`-prefix exclusion covers `__proto__`, symbol
(`__sym_*`), private (`__private_*`) and attribute-marker keys; `key_exists` (which
`js_map_get_fast_ext` reports even for deleted-sentinel entries) means `map_put`
never duplicates a shape entry and existing-property redefinition keeps its
spec-correct path; and the target must be extensible.

**Result (release, median of 3):**

| Benchmark | Apr-12 | pre-fix | with fix | Recovery |
| --- | ---: | ---: | ---: | --- |
| deriv | 169 ms | 3,630 ms | **194 ms** | **~18×**, back to baseline |
| gcbench | 1,957 ms | 41,530 ms | **2,219 ms** | **~19×**, back to baseline |
| binarytrees | 86 ms | 1,730 ms | (≈baseline) | recovered |

**js262: 39258 / 39258, 0 regressions.** (The earlier `js_property_set` attempt
showed 2704 regressions on this same gate — the contrast confirms the `map_put`
primitive is the semantically correct one.)

### Note: non-monotonic history

A clean-build bisect on `deriv` also surfaced a severe ~28 s spike at the May-9 merge
`53fca1a10` (both parents ~640 ms — a genuine cross-branch interaction, reproduced
with clean builds, not a staleness artifact) that was later mostly fixed. This made
single-benchmark bisecting unreliable; **profiling** (above) was the decisive tool
that located the actual cost.

## 6. Still outstanding

Ranked remaining regressions vs Apr-12 (current build, after §2 + §5 fixes):
`base64` 47×, `mandelbrot/awfy` 13.8×, `ack` 13.3×, plus a broad ~1.6–2.2× on
many benchmarks (`fib`, `tak`, `sum`, `mbrot`, `nqueens`, `primes`, `quicksort`…).

### 6a. `base64` — `.push()` method-dispatch override-check (root-caused)

**2026-06-30 update:** landed a guarded pristine-`Array.prototype.push` fast path
for statically-known array receivers, including a one-argument lowering helper
and tamper tracking for assignment/define/delete on `Array.prototype.push`.
The fast path is disabled for arrays with own companion properties and falls
back through the existing override-aware path. Focused coverage:
`test/js/array_push_override_fastpath.js`, `array_methods.js`, and the combined
JS fixture gate. The larger realm-scoped intrinsic-prototype cache remains
deferred.

Profiling `base64` (symbolicated debug `sample`) shows the hot path is **`parts.push()`
method dispatch**, *not* arithmetic/string work. A micro-benchmark isolates it:
`push` 1.3 M× = 9.3 s (debug) vs string-index 168 ms, typed-array-read 272 ms.

Cause: `arr.push(x)` lowers to `js_array_method_direct` (`lambda/js/js_runtime.cpp`),
which on **every call** runs an override-check before dispatching to the builtin:

```c
Item resolved = js_property_get(arr, "push");          // own + prototype-chain walk
Item builtin  = js_lookup_builtin_method(ARRAY,"push"); // builtin function Item
if (resolved != ItemNull && resolved != builtin) { ...call override... }
return js_array_method(arr, "push", args, argc);        // common case: builtin
```

In the common (no-override) case this resolves to the builtin, compares equal, and
falls through anyway — so the per-call `js_property_get` is wasted. Worse,
`js_property_get` → `js_get_prototype_of(arr)`, which **re-interns `"Array"` and
`"prototype"` into the name pool every call** (`heap_create_name` → `SIP64` /
`find_string_by_content` — exactly what dominates the profile), then hashes `"push"`
against `Array.prototype`'s large shape. The override-check was added in `a0849d62c`
("js262: fix array method global leak") for spec correctness (honoring user-overridden
array methods), so it cannot simply be removed — at Apr-12 `arr.push()` went straight
to `js_array_method`.

**Original safe fix plan:** skip the override-check
when no override is possible — i.e. `arr->extra == 0` (no own method override) **and**
`Array.prototype` methods are pristine. The latter needs an
`Array.prototype`-tamper flag set on the `js_property_set` *and*
`js_object_define_property` prototype-write paths (cf. the existing
`g_array_sym_iter_ever_set`). Missing any override path would silently break
correctness, so this must land with a full 0-regression js262 run. A secondary, broadly
useful win: cache the interned `"Array"`/`"prototype"`/`"constructor"` names in
`js_get_prototype_of` (with an epoch-reset hook like `js_reset_proto_key`) to kill the
per-call name re-interning that also feeds the broad ~2× regression.

**Attempt 2 — per-epoch prototype cache (reverted, broke js262):** caching the
resolved intrinsic prototypes (`Array.prototype`, `Object.prototype`, …) in
`js_get_prototype_of`, keyed by `js_get_heap_epoch()`, sped base64 ~1.5× (3.14 s →
2.06 s) but **regressed 169 test262 tests**. Cause: test262 creates **multiple realms**
(`$262.createRealm`) and the batch runner executes several test scripts per process, so
a *process-global* cache hands one realm's `Array.prototype` to another realm's objects;
the heap-epoch counter doesn't change on realm/context boundaries. **Lesson: any
intrinsic-prototype cache must be realm-scoped** (stored on the realm's intrinsics /
global object), not a file-static. That's the correct-but-larger design for this win.
Reverted to committed state.

### 6b. Float-compute (`mandelbrot` 13.8×, `matmul`, `nbody`, `spectralnorm`)

Float arithmetic in tight loops — almost certainly float boxing in the codegen
(V8 uses unboxed `Float64Array`; LambdaJS boxes floats). Needs a codegen/profiling pass.

**2026-06-30 update:** landed the safe typed-array read subset: known typed-array
computed reads in boxed/value position now use the existing inline typed-array
load helper instead of boxing the index and calling `js_typed_array_get`.
Focused probes cover `NaN`, int/float mixes, `-0`, out-of-bounds reads, and
string-concat boundaries. The broader unboxed float-compute plan is still open.

### 6c. Broad ~2× on recursion / numeric functions (`fib` 2.2×, `tak`/`cpstak` 1.9×, `ack` 13×) — ROOT-CAUSED

Clean-build bisect (with a 60s-timeout→skip guard — several merge commits in the window
*hang* `ack`, a separate transient bug) → **first-bad `eff6ccb9` "fix js262 tests"
(May 17)**. (Non-monotonic: an earlier regression was fixed by `66832b62b`
"fix regression", then `eff6ccb9` re-introduced it.)

Profiling `ack` shows boxed-arithmetic helpers dominating (`js_make_number`,
`js_get_number`, `js_subtract`, `js_strict_equal`, `js_numeric_operand`,
`js_is_symbol`/`js_is_bigint`) — `m-1`/`n-1`/`n===0` run through **boxed helpers
instead of native MIR integer ops**. Apr-12 ≈ 12 ns/call (native) → now ≈ 208 ns/call
(boxed).

Cause: `eff6ccb9` made `+` type-inference conservative for string-concat correctness
(`+` is overloaded numeric-add / string-concat in JS):

- `jm_infer_walk` (param typing): removed `JS_OP_ADD` from `is_arith`, so a param used
  in `x + y` is no longer inferred numeric;
- `jm_infer_return_type_walk`: the `JS_OP_ADD` else-branch changed `LMD_TYPE_INT` →
  `LMD_TYPE_ANY` ("param + param can still concatenate at runtime").

So additive/recursive numeric functions lose native-int typing (`fib(n-1)+fib(n-2)`
directly; `ack`/`tak`/`cpstak` via return-type → argument-type propagation through the
recursive call) and their arithmetic boxes. The change was correct (assuming INT for
`+` is unsound) but discarded INT inference for ADD *entirely*.

**2026-06-30 update:** landed bounded ADD inference recovery. P6 now infers
`JS_OP_ADD` as numeric only when both operands are proven numeric, and recursive
return inference seeds from concrete non-recursive returns before re-walking
self-recursive returns. This recovers recursive numeric cases such as
`fib(n - 1) + fib(n - 2)` without treating mixed/string additions as numeric.
Focused coverage: `test/js/tune5_p6_numeric_add_recursion.js` plus a 5-test
js262 addition slice.

**Original safe fix plan:** infer ADD as
numeric only when **both operands are provably non-string** (numeric literals, results
of `-`/`*`/`/`/`%`, or operands already inferred numeric), falling to `ANY` otherwise.
This recovers `a - b + c`-style chains without resurrecting the string-concat
unsoundness. It may not fully recover `fib`/`ack`, whose `+` operands are recursive
*calls* (return type unresolvable to INT under self-recursion); those also need
fixed-point return-type inference. Two js262 breaks this session (the `js_property_set`
and proto-cache attempts) underline that this must land behind a 0-regression gate.

### 6d. Array read path

`js_array_get_int` is now a runtime call where April used an inline bounds-checked
load; restoring a guarded inline read (skip when `arr->extra != 0`) would recover the
residual `sieve`/array-read delta.

**2026-06-30 update:** landed a guarded inline dense-array read for known JS
array computed reads. The inline helper pointer-checks the receiver before
reading container fields, requires `arr->extra == 0`, non-arguments dense
content, in-bounds index, and non-hole storage, then falls back to
`js_array_get_int` for sparse/accessor/prototype/deleted/non-array cases.
