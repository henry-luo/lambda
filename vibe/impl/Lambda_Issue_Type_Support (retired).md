# Lambda — Type Support Issues  ·  RETIRED 2026-08-25

> **Retired. Do not add to it.** Reviewed in full on 2026-08-25; every item was
> re-tested except the two performance claims, and the live residue moved to
> `vibe/Lambda_Issue_Ledger.md` §14 (IDs unchanged — `TS-3`, `TS-4`, `TS-6`,
> `TS-8`).
>
> | Item | Outcome |
> |---|---|
> | TS-1 | **FIXED** — `var s: int = "abc"` now raises `E201` at the declaration |
> | TS-2 | **FIXED** — `LMD_TYPE_STRING` admitted to the native scalar return set |
> | TS-3 | Open, **needs re-measurement** — the cited cause is gone from the source, the benchmark was not re-run → ledger §14 |
> | TS-4 | Open, **not re-verified** (perf + a correctness half) → ledger §14 |
> | TS-5 | Dead-code half **FIXED** — the `if (false && …)` guard is gone |
> | TS-6 | **Open**, structurally unchanged → ledger §14 |
> | TS-7 | **FIXED** — `bool[]` and `string[]` work |
> | TS-8 | **Not a defect** — ruled intended (S12.3.6, C19) |
> | TS-9 | **RESOLVED** by C16, and the implementation has since landed |
>
> Retained as the evidence and code-site record behind those findings.

**Status:** open ledger. Items are TS-1…TS-9; each records what is wrong, the evidence, and the
code site. Nothing here is fixed unless marked.
**Date:** 2026-07-29 · **verification pass 2026-08-25** — TS-1, TS-2 and TS-7 re-tested and found
**FIXED**; TS-8 re-tested and still open; TS-9's C16 implementation has since landed. TS-3…TS-6
are performance claims and were **not** re-measured in that pass.
**Scope:** the Lambda type surface — declared types on bindings, parameters, and returns — and how
the MIR-Direct emitter treats them. Guest-language type mapping is out of scope
(`vibe/Lambda_Semantics_Number_Model.md` owns the numeric tower; `Lambda_Issue_Ledger.md`
§15 OI-5 owns the broader MIR value-representation contract, of which several items here are
instances).
**Line references** were re-checked against the tree on 2026-07-29 and will drift.

---

## 0. The invariant behind most of this

The emitter assumes **a value's static TypeId implies its runtime representation**. That
assumption is unenforced and unprobeable: `type_to_mir` maps everything except `float` to
`MIR_T_I64`, so a raw `String*`, a raw `int64`, and a boxed `Item` are the same register class.
`MIR_reg_type` cannot distinguish them.

Consequently any path that produces a value whose representation disagrees with its recorded type
corrupts every consumer downstream, silently, and no assertion can catch it. TS-1, TS-2, TS-3 and
TS-4 are all instances. Two already-fixed bugs from the same family (a `string` return type
segfaulting `len()`, and `var x: int = <decimal expr>` yielding `<error>`) are recorded in
`doc/dev/lambda/LR_07` territory and in the session memory; they are cited below only as evidence
that the class is live rather than theoretical.

---

## 1. Checking gaps

### TS-1 — A declaration's initializer is never type-checked *(user-reported)* — **FIXED (verified 2026-08-25)**

> **Fixed.** The declaration boundary now checks. `var s: int = "abc"` and
> `var m: int = {a: 1}` both raise `error[E201]: type check at declared assignment
> binding failed: expected int, got string 'abc'` — the same diagnostic the
> re-assignment path emits — while `var n: int = 5` still compiles. The
> declaration/re-assignment asymmetry described below is gone.


`var x: int = "abc"` is not rejected at the declaration. `build_var_stam`
(`lambda/runtime/build_ast.cpp:7830`) records `has_type_annotation` and builds the assignment, but
performs no compatibility check between the initializer's type and the declared type. The only
such check lives at `build_ast.cpp:8017` and runs exclusively on **re-assignment** to an
already-declared var.

The asymmetry is directly observable: in `jetstream/crypto_sha1.ls`, `total_len = x_len + 20`
(a reassignment) produced a clean `cannot assign decimal value to var 'total_len' of type int`,
while `var pad: int = 9 - len(s)` (a declaration) compiled and produced `<error>` at runtime plus
a non-terminating loop.

The numeric half of that has since been fixed by adding a DECIMAL → INT/INT64/FLOAT coercion at
the declaration, but **the checking gap itself is untouched** — a genuinely incompatible
initializer (`var s: int = "abc"`, `var m: int = {a: 1}`) still passes the front end and relies on
the emitter to do something sensible.

*Fix shape:* run the `types_compatible` check from `:8017` at declaration time too, with the same
ANY/NULL escape hatches. Decide deliberately whether a narrowing initializer (`int64` → `int`) is
an error or an implicit coercion — today it is an implicit coercion, and that is defensible, but
it should be stated rather than emergent.

### TS-2 — Declared return type and native-return classification disagree — **FIXED (verified 2026-08-25)**

> **Fixed by the first of the two suggested shapes**: the native-return set was
> widened rather than the recorded type narrowed. `mir_is_native_scalar_value_type`
> (`transpile-mir.cpp:2524`) now admits `LMD_TYPE_STRING`, `LMD_TYPE_SYMBOL` and
> `LMD_TYPE_BINARY`, with the comment stating the rule: "String is a pointer-lane
> scalar; zero remains its nullable lane". `pn f() string` consumed by `len`,
> `ord` and `starts_with` returns `5 104 true` — type and ABI now agree, so a new
> consumer no longer inherits the mismatch.


`infer_return_type` accepts a declared return type only if
`mir_is_native_scalar_value_type` (`transpile-mir.cpp:1485`) admits it — INT/INT64/UINT64/FLOAT/BOOL.
`string` is excluded, so `pn f() string` returns a **boxed Item** while the call expression's
static type remains `STRING`. Every consumer that treats `STRING` as a raw `String*` then
dereferences a tagged Item.

That was a hard SIGSEGV in `len`/`ord`/`starts_with`/`ends_with` (fixed 2026-07-29 by unboxing
STRING at the boxed-return path), but the fix patches the *symptom at one boundary*. The
disagreement between "what the type says" and "what the ABI returns" is still there, and any new
consumer of a STRING-typed call result inherits it.

*Fix shape:* make the two agree — either widen the native-return set to carry `string` in a raw
lane, or have the call expression's recorded type reflect the ABI (ANY) when the return is boxed.
The second is cheaper and matches what `get_effective_type` already does for boxed bitwise calls.

---

## 2. Annotations that make code slower

These are the counter-intuitive ones: writing a *more precise* type produces *worse* code. All
three were measured on release builds.

### TS-3 — `int[]` / `float[]` on a **local** is a 3–5x regression *(user-reported)* — **needs re-measurement**

> Partial signal (2026-08-25): the cited cause has moved — the
> `var_tid = LMD_TYPE_ANY` assignment and its *"result is a pointer (stored as
> I64), treat as ANY"* comment are no longer in `transpile-mir.cpp`. Whether the
> 3–5x local-annotation regression itself survives was **not** re-measured; that
> needs a release build and the typed benchmark column, so the entry stays open
> pending measurement rather than being marked either way.


On a `pn` **parameter** a typed-array annotation is a large win — `larceny/quicksort` went
21.9 ms → 4.3 ms, and dropping just that annotation costs 18 ms. On a **local** it is a heavy loss:

```lambda
var arr = fill(10000, 3)          //  4.31 ms
var arr: int[] = fill(10000, 3)   // 18.35 ms   — 4.24x slower, same program
```

Cause: after the `ensure_typed_array` coercion the declaration path sets
`var_tid = LMD_TYPE_ANY` (`transpile-mir.cpp:6598`, comment *"result is a pointer (stored as I64),
treat as ANY"*). That discards the `ARRAY_NUM` type and with it every indexed fast path.
`fill(n, int)` and numeric array literals are **already** inferred as `ARRAY_NUM`, so the
annotation can only ever downgrade them.

Reads are what suffer; writes are close to neutral. This still handicaps shipped code:
`awfy/bounce2.ls` is 3x faster (1.44 ms → 0.45 ms) with its local `int[]` annotations removed, and
several other `*2.ls` typed benchmarks carry the same pattern.

*Fix shape:* set `var_tid = LMD_TYPE_ARRAY_NUM` (recording the element type) instead of ANY when
the coercion target is int/float/int64/uint64. This is the single highest-leverage item in this
document — it would improve the typed benchmark column across the board.

### TS-4 — A named map type on a **local** is a COW value root, not a borrow *(user-reported)*

`var x: SomeMapType = expr` makes the local a fresh COW value root rather than a borrow. Two
distinct failure modes, both observed:

- **Performance.** `jetstream/raytrace3d2.ls` did not finish in 120 s with nine map-typed locals;
  stripping them (keeping the *parameter* annotations, which are worth ~2x) runs in **80 ms**.
  Each `var tri: Triangle = (scene.triangles)[i]` deep-copied a map per intersect test.
- **Correctness.** `jetstream/splay2.ls` had replaced `splay.ls`'s retained-header idiom with
  direct `var left: SplayNode = dummy` bindings. The rotations then mutated copies, and the tree
  collapsed to **1 node instead of 8000** — while the benchmark harness happily timed it, because
  the runner checks exit status and `__TIMING__`, never output.

Note this is **not** uniform, which is why it needs a real fix rather than a blanket rule: a
blanket strip of map-typed locals across ten benchmark files made `richards2` **0.68x** (slower)
and broke `cd2` outright. Whatever the mechanism is, it helps in some shapes and is catastrophic
in others.

### TS-5 — Named map types currently buy nothing — **the dead-code half is FIXED (verified 2026-08-25)**

> The `if (false && …)` guard is gone; the direct byte-offset field read is live
> (`transpile-mir.cpp:14611`, condition now `(ast_obj_tid == LMD_TYPE_MAP ||
> ast_obj_tid == LMD_TYPE_OBJECT) && …`). Whether typed map *parameters* still
> register as `LMD_TYPE_ANY` — the second half of this entry — was not
> re-measured.


The Phase 3 direct byte-offset field read is **dead code**:

```c
if (false && (ast_obj_tid == LMD_TYPE_MAP || ast_obj_tid == LMD_TYPE_OBJECT) && ...
```

`transpile-mir.cpp:8273`. Every `x.field` goes through `fn_member_ic` regardless. Typed map
*parameters* also register as `LMD_TYPE_ANY`.

So `type Node = {...}` is documentation only — while still carrying TS-4's cost when it lands on a
local. The comments in `splay2.ls`, `hashmap2.ls` and `deriv2.ls` claiming the annotation "enables
direct byte-offset field access" are stale and misleading.

*Decide:* re-enable the path (and find out why it was disabled), or remove it and correct the
documentation so nobody adds map annotations expecting a speedup.

### TS-6 — Binding a map literal to a local kills region allocation — **still OPEN (re-verified 2026-08-25)**

> Structurally unchanged, only relocated: `mir_region_producer_candidate` is now
> `transpile-mir.cpp:888` and delegates to `mir_region_producer_node`, whose
> switch still handles only CONTENT/LIST/BLOCK, IF_EXPR, RETURN_STAM and MAP —
> there is no `AST_NODE_VAR`/`AST_NODE_LET` case, so a `var`/`let` in the body
> still falls to `default:` and disqualifies the function.


A `pn` that builds a map and self-recurses receives a caller-passed `_region` and allocates via
`map_with_region_tl` (bump-allocation into the caller's region). `mir_region_producer_candidate`
(`transpile-mir.cpp:543`) accepts a body of **only** blocks, if-expressions, returns and map
literals — any `var`/`let` hits `default: return false` and disqualifies the function, dropping it
to the general-heap `map_with`.

```lambda
return {left: mk(d-1), right: mk(d-1)}              // region producer
var n: Node = {left: mk(d-1), right: mk(d-1)}      // not — ~1.6x slower
return n
```

Measured on `larceny/gcbench`: 285 ms untyped, 354 ms as written with the local binding (1.24x
*slower*), 213 ms with direct returns (**1.34x faster**) — a 1.66x swing from the idiom alone.
Verify with `LAMBDA_MIR_DUMP_PATH=… <exe> run f.ls` and grep for `p:_region` in the function
signature; that env var works on release builds.

Note the cost is **not** the GC root slot — an `int` local, which is never rooted, costs the same.
It is purely the region-producer gate.

---

## 3. Type-surface gaps

### TS-7 — Only int/float/int64/uint64 have typed arrays — **FIXED (verified 2026-08-25)**

> **Fixed as the entry recommends** — valid element types without an `ArrayNum`
> lane now use checked generic-array storage instead of being rejected.
> `pn f(v: bool[])` called with `[true, false]` and `pn f(v: string[])` called
> with `["a", "b"]` both return 2; the `cannot coerce array to bool[]` runtime
> rejection is gone.


`ensure_typed_array` (`lambda/runtime/lambda-data-runtime.cpp:3125`) supports exactly
`LMD_TYPE_INT`, `LMD_TYPE_FLOAT`, `LMD_TYPE_INT64`, `LMD_TYPE_UINT64` (plus an `any[]` widening
path). `bool[]` and `string[]` are accepted by the grammar and the type system but fail at runtime
with `cannot coerce array to bool[]`.

That is a legitimate packed-representation limit (only those four have `ArrayNum` lanes), not a
semantic type limit. `bool[]`, `string[]`, and other valid element types should use checked
generic-array storage; the current runtime rejection incorrectly exposes a backend optimization
constraint as a language restriction. See `Lambda_Design_Type_Enforcement.md` TE-7/P4.

### TS-8 — No arity overloading for user definitions — **RESOLVED 2026-08-25 (not a defect)**

> Ruled intended: `Lambda_Formal_Semantics.md` **S12.3.6** (v15.2.0), decision
> record **C19**. A name binds to exactly one function, following ECMAScript per
> S1.11. The asymmetry this entry reported is not real — the builtin registry's
> `(name, arity)` key is a **dispatch optimization**, not a language rule, so
> builtins are not overloadable in source either. And nothing is lost:
> `pn f(a)` and `pn f(a, b)` are one `pn f(a, b?)`, verified to accept both
> arities and reject anything past its declared slots.


`pn f(a)` and `pn f(a, b)` in the same scope is `duplicate definition of 'f' in the same scope`,
while the *builtin* registry is keyed on name **and** arity. The asymmetry is what made the
recently-fixed shadowing bug so quiet: a user `pn emit(a, b)` collided with the arity-2 `emit`
sysproc and was silently discarded, whereas a 1-arg `pn emit(v)` worked, because the lookup missed
the builtin on arity. The shadowing side is fixed; the asymmetry between how user and builtin
names are keyed is worth an explicit decision.

### TS-9 — `int` overflow silently becomes `float`

`int` arithmetic that leaves ±(2⁵³−1) promotes to `float` with no diagnostic:

```
9007199254740991 + 9007199254740991   →  1.80144e+16   type=float
```

This is deliberate (`LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT`, and `INT53_MAX` is exactly the binary64
safe-integer band) and documented in `Lambda_Formal_Semantics.md` §4.1. It is listed here only
because it is a *silent* representation change of the same family as everything above: a value
whose static type said `int` is now a `float`, and nothing in the source says so. Worth confirming
this stays the intended behaviour as the numeric tower settles.

**RESOLVED 2026-08-01 by C16** (`vibe/Lambda_Semantics_Formal2.md`; spec §4.1/§4.2/§4.6/§4.7
rewritten): flex `int` is redefined as the float64-representable integers — a distinct runtime
type, closed and total under `+ - * div %` with `int.inf`/`int.nan`, no overflow promotion.
The silent int→float flip ceases to exist semantically; `LAMBDA_NUM_OVERFLOW_INT_TO_FLOAT` and
the flexint dual-lane lowering become implementation back-work.

**Implementation has since landed** (verified 2026-08-25):
`9007199254740991 + 9007199254740991` now evaluates to `inf` with `type() == int`
— saturation stays in the int lane, with no promotion to float.

---

## Priority

1. **TS-3** — one-line-ish change, measurable across the whole typed benchmark column, and it also
   un-handicaps existing `*2.ls` files.
2. **TS-5 / TS-4** — decide the map-annotation story: they are the same question (does a named map
   type mean anything?) and today the honest answer is "cost without benefit".
3. **TS-6** — narrow but a 1.66x swing where it applies, and invisible to anyone reading the source.
4. **TS-8** — the only remaining correctness/consistency item; "make the diagnostic match the rule".

*(TS-1, TS-2, TS-7 fixed; TS-9 resolved by C16 and implemented — all verified 2026-08-25.)*
