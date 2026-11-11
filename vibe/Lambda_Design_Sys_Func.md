# Lambda System Function Return-Value Convention

- **Status:** design proposal — return-value convention; RF7 (native
  procedures with `var` parameters) RATIFIED by USER 2026-11-11 as D7.6.3*,
  D7.6.4*, S9.1.3v4 and S9.2.1v2
- **Date:** 2026-08-05
- **Scope:** Lambda system functions and procedures that search, select,
  transform, or mutate collections and strings
- **Semantic authority:** [`doc/Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md), especially §3 (truthiness), §4.7 (numeric and vector totality), §5.1 (total equality), §6.1 (magnitude comparison vs total order), §7.1 (total reads and null propagation), §7.5 (aggregation), §7.6 (error participation), §7.7 (broad input and in-domain results), §8.1 (iteration and length), and §10.2 (vectorization).

---

## 1. Decision summary

The return value follows the cardinality and meaning of the operation:

| Operation contract | Empty or missing outcome | Non-admissive invalid input/operation |
|---|---|---|
| **Zero-to-many** | `[]` | `error` |
| **Zero-or-one** | `null` | `error` |
| **String-valued result with no string content** | `""` | `error` |
| **ArrayNum-producing vector operation** | typed numeric `[]` | `error` for an invalid whole operation |
| **In-place mutation** | updated owner, or consistently `null`/unit for the whole mutator family | `error` |

Admissive invalid type/value cases are an additional operation-level outcome:
they use the empty/missing result in the middle column when the function's
contract explicitly defines that graceful behavior.

The last column is refined by the operation's admission policy. A system
function may classify a bad type/value as **admissive** when the operation has
a meaningful absence result and continuing composition is more useful than a
diagnostic. It then returns the result-domain absence (`null`, `[]`, or `""`).
A **non-admissive** failure is one where returning absence would hide a
malformed source, an unreadable resource, or a contract violation that the
caller needs explained; it returns `error` (or raises through its declared
`T^E` channel). This is a function-level semantic choice, not a universal
rule based only on whether the argument has the right static type.

In short:

1. **Zero-to-many result → `[]`.**
2. **Zero-or-one result → `null`.**
3. **Non-admissive invalid input or operation → `error`.**
4. **String-valued result with no content → `""`.** This is the string
   result convention; it does not change collection `[]` or general scalar
   `null` conventions.
5. **ArrayNum vector operation → accept numeric `[...]` and, for the array
   overload, always return numeric `[...]`, including a typed empty `[]`.**
6. **In-place mutation → return the updated array, or consistently return
   `null`/unit. Never use `[]` as a generic mutation-success sentinel.**
7. **Admission policy is explicit per function.** Admissive misses return the
   appropriate absence value; non-admissive failures return a detailed error.
8. **No in-band sentinels.** Absence is never encoded as `-1`, an unchanged input, or another value a caller could mistake for — or compute with as — a success (RF3A).

The result shape must not depend on cardinality. A function whose successful
result is a collection returns a collection whether it contains ten values, one
value, or no values.

---

## 2. The semantic states

These states are distinct and must remain distinguishable:

| State | Meaning | Representation |
|---|---|---|
| Empty success | The operation ran successfully and found/produced no members | `[]` |
| Absent value | The operation ran successfully but no single value exists | `null` |
| Empty string result | The operation successfully produced no string content | `""` |
| Failure | The operation could not validly run or complete | `error` |
| Admissive miss | The operation cannot produce a result, but its contract permits graceful composition | `null`, `[]`, or `""` by result domain |
| Mutation completion | The owner was updated, possibly with no structural change | updated owner or `null`/unit, according to the family contract |

Lambda cannot treat `null`, `""`, and `[]` as interchangeable. `null` and
`""` are falsy, while every container, including `[]`, is truthy. They also
carry different type tags and communicate different result contracts. `""`
remains a real string value; it is the canonical no-content result only for
string-valued system functions.

### RF1 — Result-shape invariance

For every successful call, the outer result shape is fixed by the function's
contract, not by the number of values produced.

```lambda
find("a1b2", \d)     // [{value: "1", index: 1}, {value: "2", index: 3}]
find("abc", \d)      // [] — still a collection result
```

A caller can therefore iterate, concatenate, map, or inspect the result without
first branching on `null`.

### RF2 — Zero-to-many returns `[]`

Use `[]` when the function conceptually returns a sequence of answers:

- all matches;
- filtered or selected members;
- slices and removed-member collections;
- tokenization or parsing records;
- keys, values, groups, rows, or children.

Examples include `find`, `filter`-shaped operations, `split`, collection
`slice`, and a hypothetical `removed_items` operation. No matches or members
is a successful empty collection, not absence and not failure. The string
overload of `slice` follows RF3B and returns `""` when it produces no
characters.

### RF3 — Zero-or-one returns `null`

Use `null` when the successful result has at most one value:

```lambda
find_first(items, pred)     // item | null
lookup_optional(map, key)   // value | null
reduce([], fn)              // null when no identity/initial value exists
```

This says that the operation succeeded but there is no value to return. A
plural/all-matches function must not use this convention.

Naming should expose the distinction where ambiguity is likely:

- `find` or `find_first` for zero-or-one;
- `find_all`, `matches`, or an explicitly documented all-match `find` for
  zero-to-many.

Lambda's existing string-pattern `find` is an all-match operation, so its
no-match result is `[]`.

### RF3C — Admissive misses use the result-domain absence

An admissive operation treats a particular invalid type/value or empty domain
as “no answer” when that interpretation is safe and useful in expression and
data composition. The result still follows the operation's cardinality:

```lambda
arr[-1]          // null — admissive out-of-range read
argmin([])       // null — no minimum exists
argmax([])       // null — no maximum exists
varg(1.5)        // null — invalid variadic position is not a result
```

Admissive does not mean that every bad argument is silently accepted. It is a
documented contract of the individual function, and existing error operands
remain errors. In particular, a valid operation that produces no single value
uses `null`; a collection-producing operation uses `[]`; and a string-valued
operation uses `""`.

### RF3A — Scalar absence is `null`, not a numeric sentinel

Some older system functions such as `index_of`, `last_index_of`, and `ord`
historically returned `-1` when they had no answer. This follows the C-family
convention used by C/C++, Java, JavaScript, and similar APIs: indices and
Unicode code points are non-negative, so `-1` is outside the success domain;
it is easy to test with `>= 0`, and the ABI can remain a plain `int` rather
than `int | null`.

That convention is useful at a low-level boundary, but it is not the Lambda
surface convention. These functions now return `int | null`:

```lambda
index_of("abc", "b")       // 1
index_of("abc", "z")       // null
last_index_of("abca", "z") // null
ord("")                    // null

index_of(text, needle) or len(text)  // value-or-default
index_of("abc", "a") or 99           // 0 — a position-0 match survives or-coalescing; 0 is truthy (§3)
```

Migration from the old `-1` sentinel is intentionally asymmetric. `index_of(text, needle) >= 0` remains a valid “found a non-negative index” test: an ordered comparison absorbs `null` (formal semantics §6.1), so on a miss the expression evaluates to `null`, which is falsy. Every old *not-found* test, however, breaks silently, and each breaks toward the found branch: `idx < 0` likewise evaluates to falsy `null`; `idx == -1` is always false under total equality (§5.1 — `null` is cross-family unequal to `-1`, and a found index is never negative); `idx != -1` is always true. Write `index_of(text, needle) is null`; the same applies to `last_index_of` and `ord`. Because a valid index or code point is never negative, comparing these results against a negative constant is *always* a bug and is statically detectable — a lint rule should flag it, converting the silent migration hazard into a loud one.

The Lambda choice is deliberate, and most of the argument is that `null` is the value the rest of Lambda's semantics was already built around:

1. **Uniformity.** `null` is the one absence value for every zero-or-one result: out-of-range `arr[i]`, missing map lookup, `find_first`, `reduce([])`, `argmin([])`, `min([])`. An `index_of` returning `-1` would be the lone numeric sentinel on the entire surface — one more convention to memorize, in exchange for nothing.
2. **A sentinel is only a sentinel at rest; `null` survives computation.** `-1` stops meaning “not found” the moment it flows: `index_of(s, c) + 1` yields `0`, a valid-looking index, and a `-1` reaching a slice offset selects real characters — from the end under the current from-the-end handling, a plausible prefix under a clamping rule — so a missed search silently returns text either way (the classic JavaScript `s.slice(s.indexOf("z"))` footgun). `null` is a fixed point of the same pipeline: `null + 1` is `null` (§7.1), a `null` offset propagates (RF3B), and the miss arrives at the end of the pipeline still recognizable as a miss, to be rescued exactly once with `or`. Under §7.1's set-processing rationale — a 10,000-record transform should produce 10,000 results — `null` is the only return value that participates correctly in the machinery.
3. **Truthy-`0` makes the coalescing idiom actually safe — a dividend `-1` would refuse.** In JavaScript or Python, `s.indexOf(x) || dflt` is broken because a match at position 0 is falsy. Lambda chose truthy-`0` (§3) precisely to keep `or` a safe coalescing operator, so `index_of(text, needle) or len(text)` is correct even for a match at position 0. A `-1` result is a real, truthy integer and cannot participate in the idiom at all.
4. **The expressiveness is asymmetric in `null`'s favor.** Where a boundary genuinely wants the sentinel — dense numeric encodings, foreign ABIs — `index_of(s, c) or -1` recovers it in one expression, safe again because `0` is truthy. Recovering absence from a `-1`-returning primitive takes a conditional at every call site. The composable value is the correct primitive; the sentinel is a derived encoding a caller opts into at a boundary.
5. **The former performance objection no longer applies.** Lambda's nullable native lane represents `int | null` in the same native lane as `int`, so the public type does not require boxing merely because absence is possible.

The cross-language record points the same way: every language that acquired first-class absence with a cheap representation moved its index search onto it (Swift, Rust, Haskell), while the `-1` holdouts keep the sentinel for compatibility with their own legacy `indexOf` (§5).

Valid positions and code points remain `int`. No match, an empty input to
`ord`, or another successful broad-input operation with no scalar answer is
`null`; an error operand remains `error` according to the normal error
boundary rules. These three APIs intentionally retain their broad-input
contract, so a non-applicable non-error value is treated as “no answer,” not
as a second absence encoding.

#### Known friction, and the idioms that answer it

Naming where `null` will chafe keeps the ruling honest:

- **Dense index arrays.** Collecting `for (row in rows) index_of(row, key)` with misses produces `int | null` content, which cannot pack into an ArrayNum, so vectorized index pipelines degrade to generic lists. Where dense encoding matters, opt back into the sentinel explicitly — `index_of(row, key) or -1` — and the result packs again. Pandas walked the same path (NaN holes, then the nullable `Int64` dtype); a nullable ArrayNum lane is the eventual clean answer if this bites often.
- **Aggregation under strict null propagation (§7.5).** The earliest-of-several-needles pattern nulls out when any needle misses; the idiom is explicit skipping: `min([index_of(s, ","), index_of(s, ";")][!null]) or len(s)`. `-1` is no better here — `min` would happily select the sentinel — it merely fails differently.
- **Emission boundaries.** CSV integer columns, packed binary layouts, and JSON consumers that require a number take `or -1` (or a domain default) at the edge, where the encoding choice is explicit and local.
- **Mixed Lambda/JS codebases.** The same programmer writes `s.indexOf(x) === -1` in a JavaScript file and `index_of(s, x) is null` in a Lambda file. Web compatibility fixes the JS side; the raw/public adapter split below is the seam.

The classic scan loop is *not* on this list — `idx >= 0` still works as a found test — and enumerate-all-occurrences workflows belong to the all-match `find` under the zero-to-many `[]` convention anyway. In practice `index_of` is genuinely zero-or-one, which is exactly the cardinality `null` serves.

The runtime keeps the compatibility sentinel one layer down: `fn_index_of_raw` and `fn_last_index_of_raw` retain the `-1` ABI for the C/JavaScript adapters (`String.prototype.indexOf` requires it), and the public wrappers map a negative raw result to `null`. Lambda-side code must never call a `_raw` helper directly: the raw path conflates an error operand with `-1`, so the public wrappers guard errors first precisely to keep “operand was an error” distinct from “no match.”

### RF3B — String-valued results use `""` for no content

The general zero-or-one convention is `null`, but a string-valued system
function uses the empty string when its contract has no characters to return.
Within that string result contract, `""` is the absence/no-content
representation. It remains a genuine string value and is not equal to `null`:

```lambda
chr(65)              // "A"
chr(null)            // ""
chr(-1)              // ""
slice("", 0, 1)      // ""
slice("abc", 2, 2)   // ""
slice(null, 0, 1)      // null — no source value to slice
slice("abc", null, 1)  // null — a missing offset makes the selection absent (§7.1)
```

This is a representation convention for string-valued results, not a new
universal absence value. Collection-producing overloads still return `[]`,
scalar zero-or-one functions still return `null`, and error operands or other
invalid operations still return `error` according to their contracts. In
particular, the overloaded `slice` keeps the result convention of its input
domain: string slices return strings (possibly `""`), while array/list slices
return arrays/lists (possibly `[]`).

Both `slice` overloads take their offset contract from RF3D, which is what makes the `null` rows above absent rather than empty.

*Rationale.* JavaScript, Python, JSON, and XML all distinguish empty text from
absence and preserve `""` as a real string value. Returning `null` from every
no-content string operation would make the empty-string result convention
unobservable and make `""` needlessly redundant at the system-function
boundary.

### RF3D — Slice offsets: a negative offset clamps, a null offset is absence

Offsets into a sequence obey two rules, both inherited from the formal semantics rather than invented here.

**A negative offset carries no meaning and clamps.** Per §7.4 a negative index is out of range *exactly like* an over-length one — "both failure directions of a computed index are symmetric absence" — and per §7.1 slices clamp to bounds. Combining them, a negative offset clamps to `0` just as an over-length offset clamps to `len`. It never wraps from the end:

```lambda
slice([1, 2, 3, 4], -2)      // [1, 2, 3, 4] — start clamps to 0
slice([1, 2, 3, 4], -2, 2)   // [1, 2]
slice("hello", -2, 3)        // "hel"
slice("hello", -5, -1)       // ""  — both offsets clamp to 0, empty interval
[1, 2, 3][-1]                // null — indexing is absence, not clamping
```

Reaching from the end is the separate `last` keyword in a subscript (`s[last - 2 to last]`), or an explicit `len(s) - n` in a call position, where §7.4 makes `last` unavailable. Function offsets are positions, never signed directions — the same corollary that keeps `take`/`drop` counts unsigned.

**A null offset makes the whole selection absent.** A missing scalar position is not position `0`, so `slice(s, null, k)` is `null` — never a silent read from the start, and never an `error`. This is what keeps a missed search composable end-to-end, and it is the rule that discharges RF3A's most important obligation:

```lambda
slice(s, index_of(s, c), k)  // null when the search missed
```

A non-null, non-integral offset remains a non-admissive `error` (RF4).

*Rationale.* Python-style wrapping is precisely what made the old `-1` sentinel dangerous, and the danger outlives the sentinel: under wrapping, *any* underflowed computed index silently selects real members from the far end of the sequence instead of yielding absence. `arr[i - 1]` at `i = 0` returns the last element; a leaked not-found `-1` slices the tail. Clamping makes the two out-of-range directions behave alike, so an index bug degrades to an empty or absent result the caller can see, rather than to plausible wrong data. The cost is that Lambda gives up a familiar shorthand; §7.4 already paid that price for indexing, and a `slice` that wrapped while `arr[-1]` returned `null` would be the worse outcome — one concept with two contradictory spellings.

### RF4 — Non-admissive invalid input or operation returns `error`

Non-admissive failures must not be hidden by `null` or `[]`. Examples include:

- malformed parser input or an unsupported parse format;
- failed file/URL loading or other I/O failure;
- an invalid pattern or option where no safe absence interpretation exists;
- mutating a read-only or unsupported view;
- allocation failure or another operation that cannot produce its contract's
  result.

An explicit conversion or a declared/deferred type-enforcement boundary is
also non-admissive: a value that cannot satisfy the requested contract must
produce a detailed type/value error, never a silent `null`.

This preserves the distinction between:

```lambda
find("abc", \d)   // []: valid search, no matches
parse("{broken", 'json') // error: malformed source with a diagnostic
```

An existing error operand propagates according to the error-participation rules
in the formal semantics; a function must not convert it into an empty or absent
success value.

The line between admissive and non-admissive is necessarily contractual rather
than syntactic. Use the following test: if the caller can safely continue with
the result-domain absence and the absence itself carries the intended meaning,
choose admissive; if absence would leave the caller unable to explain a bad
source or failed operation, choose non-admissive. Every non-admissive path has
an implementation obligation to publish a detailed, case-specific diagnostic;
anonymous `ItemError` or a bare log line is implementation debt, not a
conforming final behavior.

---

## 3. ArrayNum vector operations

### RF5 — Array overloads are array-in/array-out

`ArrayNum` is a transparent runtime representation of a numeric `[...]` value,
not a separate source-level calling convention. A vectorized sys func must
accept an ordinary numeric array wherever it accepts `ArrayNum`; callers must
not need to construct, cast, or pass a raw `ArrayNum*`.

When the array overload of an ArrayNum-producing operation is selected, every
successful result is another numeric array:

```lambda
math.sqrt([1, 4, 9])    // [1, 2, 3]
math.sqrt([4])          // [2] — do not collapse one lane to a scalar
math.sqrt([])           // []  — typed empty numeric array, not null
[1, 2, 3] + 10          // [11, 12, 13]
```

At the language surface both generic arrays and packed numeric arrays use
`[...]`. Internally, an ArrayNum-producing result is returned as a boxed
`Item.array_num`, not a raw pointer and not a generic `Item.array`. Its element
type follows the operation's promotion rule, and its shape follows the
operation's shape-preservation or broadcasting rule.

This makes the vector operation total over its declared numeric domain in the
NumPy style:

- zero lanes produce a typed zero-lane result with a deterministic element
  type;
- one lane still produces a one-lane array;
- numeric lane exceptions produce the domain's defined lane value, such as
  `nan` or `inf`, rather than changing the whole result's shape;
- incompatible shapes, a non-numeric operand, an invalid axis, or another
  invalid whole operation returns `error`.

“Always return `[...]`” applies to an operation whose declared successful result
is ArrayNum. It does not change the contract of an ArrayNum **consumer** that is
explicitly scalar- or metadata-producing: indexing returns one item, `sum`
returns a scalar reduction, `any`/`all` return booleans, and `shape` returns its
documented dimension value. The function's declared result shape remains the
authority.

---

## 4. In-place mutation

There are two coherent public conventions for mutators:

### Model A — return the updated owner

```lambda
arr2 = push(arr, value)
arr3 = splice(arr2, start, count)
```

Advantages:

- supports chaining and expression-oriented code;
- naturally carries a detached replacement under copy-on-write;
- returns the actual result state, including `[]` when the updated array itself
  is empty.

### Model B — return `null`/unit

```lambda
push(arr, value)                // mutation statement
splice(arr, start, count)       // mutation statement
```

Advantages:

- makes side-effect-only intent explicit;
- prevents callers from depending on a redundant alias of the mutated owner;
- matches statement-like mutator APIs in several languages.

### RF6 — Do not mix mutation models accidentally

A mutator family must choose one public convention. In particular:

- `[]` must not mean merely "mutation succeeded";
- a no-op such as `splice(arr, 0, 0)` has the same return shape as a splice that
  removes members;
- invalid mutation returns `error`, not the unchanged input, `null`, or `[]`.

The current Lambda runtime follows **Model A internally**: `pn_push` and
`pn_splice` return the owner `Item`, and the MIR copy-on-write path uses that
return value to write a detached replacement back to the owning variable. If
the language surface adopts Model B, the implementation must keep an internal
owner-returning helper and separately produce the user-visible `null`/unit
result. The COW writeback value and the language-level expression result are
different concerns.

`splice` also needs an explicit semantic choice distinct from its mutation
result:

- Lambda's runtime helper currently returns the updated owner for COW
  writeback;
- the procedural-runtime design treats the mutator call as a statement with no
  value, so the public expression contract still needs an explicit ruling;
- JavaScript's `splice` returns the removed members;
- a future removed-members API would be zero-to-many and therefore return `[]`
  when nothing was removed.

### RF7 — Native procedures with `var` parameters (RATIFIED, USER 2026-11-11)

**Status:** ratified by USER on 2026-11-11 for paint M1h
(`Lambda_Pkg_Paint.md` PT7), with option (a) of RF7-Q1. Formal text:
D7.6.3* and D7.6.4* (design spec 66.1.0), S9.1.3v4 and S9.2.1v2 (semantics
spec 89.0.0). Not implemented yet.

**Problem.** PT7 ruled that native paint kernels write in place through a
`var` borrow: `paint.stroke(var layer, …)`, `paint.feed(var session, …)`,
`paint.wet_step(var sheet, …)`. Today no general native form exists:

- Only three natives mutate a `var` argument: `push`, `splice` and map `set`
  (`SYSPROC_PUSH`, `SYSPROC_SPLICE`, `SYSPROC_VMAP_SET`). Each one is an enum
  special case in `build_ast.cpp`, `interp.cpp` and `mvp_mir.cpp`. Each takes
  the owner by value and returns a replacement owner, and the caller writes
  that back (Model A above).
- `SysFuncInfo` has no parameter list. `resolve_sys_func_value` builds a
  `TypeFunc` with no params, so the call validator's `var` arm
  (`lambda_ast_validate_call_arguments`) never runs for a sys func.
- The Jube signature scans (`build_ast.cpp`, `jube_signature_*`) skip a
  `var` marker silently, so a host `pn(var x: map)` is accepted today with
  value semantics. The write would be lost.
- The only home transport is `Context::mir_var_homes` plus
  `LAMBDA_DYNAMIC_CALL_BORROWED`. It serves Lambda callees only. CW33
  schedules the cells for deletion when the pointer ABI lands.
- Current idiom: a `pn` wraps a functional native and rebinds
  (`image.write_region`). The native sees a shared value and forks every
  level it touches, on every call. PT7 rules this out for live painting: a
  forked 1-megapixel, 12-plane wet sheet is about 48 MB per step.

**The form.** A native procedure joins the existing `var` protocol
(S9.1.3v3, D5.4.6). Only the delivery of the home is new.

1. **Registration names the `var` positions.**
   - Every row carries a `var_mask` bitmask and optional `var_bounds` on
     `SysFuncInfo` (a new `ValueRep` would ripple through every switch over
     that enum). Built-in rows can set them; host-module rows get them from
     the signature.
   - A host-module function spells it in both signatures:
     `"pn(var layer: map, dabs: any, options: map|null) -> map"` and
     `"Item fn_radiant_paint_stroke(Item* layer, Item dabs, Item options)"`.
   - Registration rejects: `var` in an `fn` signature (S12.1.1v3); `var` on
     a variadic position; `var` on an async native, which completes after it
     returns, when the home is gone (CW33 found the same limit for a
     suspending callee); and a `var` position whose native type is not
     `Item*`. The Jube
     scans must parse `var`, never skip it.
2. **The call uses the Lambda `pn` checks.** The sys func gets the parameter
   list that its signature declares. Then `lambda_ast_validate_call_arguments`
   applies the S9.1.3v3 checks unchanged: a mutable root, writer
   exclusivity, the place-copy analysis
   (`direct_note_place_copy_var_borrow`) and the type rule (RF7-Q1). There is
   no first-class reference to such a procedure, so a dynamic call stays
   rejected (S12.3.2, D7.2.2).
3. **The native entry receives `Item*` per `var` position.** It is the
   address of the caller-owned, rooted home of D5.4.6: the T0 frame slot or
   place leaf, or the MVP deferred `argument_span` slot that
   `em_publish_var_home` writes. The home is always a boxed `Item`, also for
   a typed binding: the caller boxes the binding into the home and reloads it
   after the call, as the boxed `_b` edge does for typed `var` positions
   (D7.3.4; CW33 "typed half on the boxed edges"). The address is not a value. The caller never roots it, boxes it
   or puts it in a value span; the argument descriptor carries a pointer rep
   so the emitter neither roots nor spills it (CW33 blocker 2). The return
   value is the expression result, not the write-back value (D7.6.2).
4. **The callee writes in a fixed order** (PT7 steps 2–4, generalized):
   1. Check every argument and reserve every resource (for paint, the work
      budget) before the first write.
   2. Read `*home`. Un-share one level at a time down to the storage that
      the call writes. Copy a level only when its shared bit is set (D5.4.1,
      D5.4.2).
   3. Store each replacement before the next allocation point: the root into
      the home, a child into its unique parent. The value stays rooted, and
      an error exit publishes a well-formed value.
   4. Take data-zone pointers after the last allocation point. Lend them for
      the work of the call only (D5.4.4, D5.5.2). A call back into Lambda code
      counts as an allocation point.
   5. Keep the kind of the value: change members, never the container kind,
      the element kind or the map shape (RF7-Q1). A write that changes the
      kind is outside this form.
   6. Return. Keep neither the home address nor any data-zone pointer.
5. **The caller writes back on every return, also on an error** (D5.4.6).
   T0 reuses `interp_borrow_place_leaf` and `publish_places`. MVP reuses the
   `direct_call` borrow machinery: home publish, post-call reload,
   `write_place`, and the array-witness refresh. `mvp_parameter_storage_stable`
   answers false for a native callee, because its proof reads the callee's
   AST.
6. **Effects.** A `var` native is MAY_GC, because un-sharing allocates. A
   native `var` call ends the facts of place handles on that place, as a
   Lambda `var` call does (D5.4.4 invalidation table). The write scan
   (`ast_body_may_write_entry`) now counts every root argument of a non-pure
   sys call as a write. With `var` positions known, it can count only those.
   This is a later optimization.
7. **`push`, `splice` and map `set` stay outside the form.** They are its
   precedent, not instances. They change the kind of an untyped place: `push`
   widens an `ArrayNum` lane (`pn_push_cow`), and `set` adds a key, which
   changes the map shape. Step 4.5 forbids both. To stay safe on a typed
   place, their lowerings pass the place's declared contract to the native
   (`lambda_array_push_checked`), which the general form does not carry. They
   also skip the write-back on an error, which is observably the same,
   because they fail before the place changes value. Their lowerings stay as
   they are. A general kind-changing form would need the declared contract in
   the ABI; nothing needs it yet.

**Partial writes.** Step 4.1 makes "a failed call leaves the place
unchanged" the normal case. If a native still fails after its first write,
the home holds a well-formed value, and the partial writes reach the caller,
as S9.1.3v3 permits. PT7's stronger promise is a paint-engine property: the
engine charges its whole budget first.

**RF7-Q1 — the type rule (USER 2026-11-11: option (a)).** S9.1.3v3 makes `var` parameters
invariant (S9.2.1): the argument's declared type must equal the parameter's.
A host module cannot name a package or user type. `procart.watercolor`
declares `var s: Sheet`, so M3's `paint.wet_step(var sheet)` cannot match
any type that Radiant can register. Options:

| Option | Rule | Cost |
|---|---|---|
| **(a) Bounded parameter** (recommended) | The registered type is a bound. The argument's declared type `T` must conform to it, and the native keeps the value in `T` (step 4.5). This is invariance at `T`, as for a generic `pn f<T: map>(var x: T)`. For a typed binding, the caller checks the published value's kind before it reloads it: the type tag, the element kind of an `ArrayNum`, and the shape of a map. A mismatch is a defect error. | A semantics addition, S9.1.3v4. A few compares per typed `var` argument. |
| (b) Exact invariance | Natives declare untyped `var` positions. A typed caller copies into an untyped `var` and back. | The copy shares the value, so the native forks it every call. This defeats PT7 for M3. |
| (c) Registered nominal types | A host module registers the package types it accepts. | Radiant would depend on package types. This breaks the D9.1.1v4 ownership split. Rejected. |

Precedent for (a): `push(var xs: int[], v)` already accepts any array
place and keeps the place's declared contract by checking `v` against it
before the write. S9.2.1 forbids covariant `var` passing because a callee
could store a value outside `T` (the covariant-array hole). Under (a) the
native cannot: step 4.5 and the caller's check close the hole.

Why check in all builds under (a): MVP reloads a typed binding with its raw
descriptor. A native that broke the type would corrupt memory without a
trace, and Jube modules can come from outside the runtime. The check costs a
few compares per call.

Proposed semantics text for (a), as a sentence added to S9.1.3 (S9.1.3v4):
*"A system procedure's `var` parameter declares a bound: the argument's
declared type `T` must conform to it, and the procedure keeps the value in
`T`. This is invariance at `T`."* S9.2.1 then gains a cross-reference
(S9.2.1v2): its compile error applies to a `pn` written in Lambda; a system
procedure's bound is not covariance (S9.1.3v4).

**Alternatives for the native ABI (rejected):**

- *Owner in, replacement out* (Model A, as for `push`): it supports one `var`
  position only, and the replacement uses up the result channel, which
  D7.6.2 keeps separate.
- *The `Context::mir_var_homes` cells*: a side channel that CW33 schedules
  for deletion. Both tiers would fill it for natives, and natives would
  read context state.
- *Raw `Container**` or lane homes*: these belong to the native direct
  entries of Lambda functions (CW33 full stage). A public sys func uses
  boxed Items at the boundary (D7.6.1).
- *The rebinding wrapper*: forks every touched level on every call (see
  Problem).

**As implemented (2026-11-11, M1h).**

- *Registration:* `jube_signature_var_params` (`build_ast.cpp`) parses `var`
  in both signatures and fails closed.
- *Checks:* `validate_native_var_arguments` shares `validate_var_argument_root`
  with the Lambda `pn` validator and applies the bound with
  `lambda_static_boundary_relation`. It also sets
  `AstCallNode::var_alias_capture_mask`: plain arguments that read a borrowed
  root, or call a definition that might return one, are marked shared before
  the borrow.
- *T0:* `interp_call_native_var_proc` (`interp.cpp`) evaluates arguments and
  place keys in source order (`interp_eval_place_keys` is shared with
  `interp_borrow_place_leaf`), then borrows. It passes home addresses in a C
  stack array that is never rooted, and writes back after
  `lambda_var_home_check_kind`.
- *MVP:* `native_var_call` (`mvp_mir.cpp`) uses `place_path`,
  `prepare_place_owner`, `cow_path_borrow_keys`, slots of the rooted argument
  span as homes, and `write_place`/`publish_place` for the write-back.
  `jit_import_get_metadata` classifies the `var` positions of a dynamic
  import as `JIT_VALUE_RAW_NON_GC_POINTER`. This is the fix for CW33's
  blocker 2: the emitter never roots the address.
- *Kind check:* the caller checks every native home, typed or not, because
  D7.6.4 step 5 binds every native.
- *First kernel:* `radiant.paint_stroke` (`radiant_paint_adapter.cpp`) shares
  its batch reader and renderer with `radiant.paint_dabs`. `lambda.paint`'s
  `stroke(var layer, …)` and `feed(var session, …)` call it on their own place
  (Lambda_Pkg_Paint PT7). A Lambda `pn` that receives a member place beside
  container parameters snapshots the leaf, so the in-place write must not pass
  through such a hop.
- *Tests:* `test/lambda/proc/native_var_proc.ls` covers binding and place
  homes, snapshots, parity with the pure form, a typed binding and a
  rejected budget, on three tiers and under forced GC.
  `NegativeScriptTest.SemanticError_NativeVarArguments` covers the static
  rejections.
- *Known residue:* the JIT tier marks a bare binding shared when it passes
  the binding to an unaudited import, as a capture. The next in-place write
  then copies the touched levels. Place arguments (`s.layer`) and the T0
  tier are not affected. Borrow metadata for Jube functions would remove
  this.

**Implementation map** (survey 2026-11-11; not normative):
`sys_func_registry.h` (`SysFuncArgDesc`, `SysFuncInfo`), `value_rep.h`;
the Jube scans and `register_jube_sys_funcs` in `build_ast.cpp`;
`resolve_sys_func_value` and the push/splice arm (`build_ast.cpp`); T0
`eval_call` borrow and publish (`interp.cpp`) and the planner caps
(`interp_plan.cpp`, Item-only and arity ≤ 5); MVP `direct_call` and
`mutate_collection` (`mvp_mir.cpp`), plan admission and effects
(`mvp_plan.cpp`); JIT import metadata (`jit_import_*`).

**Formal text as proposed** (entered unchanged into
`doc/Lambda_Formal_Design.md` §D7.6 on 2026-11-11, with the S9.1.3 citation
updated to S9.1.3v4):

> - **D7.6.3* — A native procedure can take `var` parameters.** A system
>   procedure or a host-module `pn` can declare `var` positions. Registration
>   rejects a `var` position in an `fn`, on a variadic position, or on an
>   async procedure. A call uses the checks of S9.1.3 and the
>   caller-owned home of D5.4.6. The native entry receives the address of each
>   home as an `Item*`. This address is not a value: the caller does not root
>   it, box it or put it in a value span. A typed binding goes through a boxed
>   home, and the caller checks the kind of the published value before it
>   reloads it. The caller writes back each home on every return, also on an
>   error result. A dynamic call to such a procedure stays rejected (D7.2.2).
>   `push`, `splice` and map `set` keep their own lowering, because they can
>   change the kind of an untyped place. [Sys_Func RF7; PT7]
> - **D7.6.4* — A native procedure writes through its home in a fixed
>   order.** The native must do these steps:
>   1. Check the arguments and reserve the resources before the first write.
>   2. Un-share one level at a time, down to the storage that it writes
>      (D5.4.1, D5.4.2).
>   3. Store each replacement in the home, or in its unique parent, before
>      the next allocation point.
>   4. Take data-zone pointers after the last allocation point, and lend
>      them for the work of the call only (D5.4.4, D5.5.2). A call into
>      Lambda code is an allocation point.
>   5. Keep the kind of the value. Change members, never the container kind,
>      the element kind or the map shape.
>
>   The native keeps neither the home address nor a data-zone pointer after
>   it returns. Its return value is the expression result, not the write-back
>   value (D7.6.2). [Sys_Func RF7]

Ratification (2026-11-11): design spec 66.0.0 → 66.1.0 (rulings added
compatibly); semantics spec 88.0.0 → 89.0.0 (S9.1.3 and S9.2.1 revised in
place); Appendix A rows added to both; Appendix C rows point at RF7.

---

## 5. Cross-language convention

The common pattern across languages follows result cardinality rather than one
universal empty sentinel:

| Language/API shape | No-result convention |
|---|---|
| JavaScript `find` | `undefined` — zero-or-one |
| JavaScript `filter` | `[]` — zero-to-many |
| JavaScript `splice` | `[]` when no members were removed |
| JavaScript `indexOf` / ES2015 `findIndex` | `-1` — C-family sentinel, kept for consistency with legacy `indexOf` |
| Python optional search | value or `None` — zero-or-one |
| Python comprehension / collected filter | `[]` — zero-to-many |
| Python in-place list mutators | `None`/unit-like |
| Rust `Iterator::find` | `Option::None` — zero-or-one |
| Rust `Iterator::position` | `Option::None` — the zero-or-one index search |
| Rust collected filtering | empty `Vec` — zero-to-many |
| Rust `Vec::splice` | iterator over removed members, possibly empty |
| Swift `firstIndex(of:)` | `nil` — the zero-or-one index search |
| NumPy ufunc over array-like input | `ndarray`, including a zero-length result; broadcasting determines shape |

The transferable rule is not "always return null" or "always return an empty
array." It is: preserve the successful result's cardinality, and keep failure
on a separate channel.

The `-1` holdouts are instructive rather than contradictory. Languages that acquired first-class absence with a cheap representation moved the index search onto it: Swift's `firstIndex(of:)` returns `Index?`, Rust's `Iterator::position` returns `Option<usize>`, Haskell's `elemIndex` returns `Maybe Int`. The APIs that still return `-1` — Java, C#, Kotlin, Go, and JavaScript's ES2015 `findIndex` — keep it for ecosystem compatibility or consistency with their own legacy `indexOf`, not on the merits. The lesson is that uniformity *within* a language is the binding constraint, and Lambda's uniform absence value is `null` (RF3A).

---

## 6. Current Lambda audit

| Function/operation | Current behavior | Convention |
|---|---|---|
| `find(source, pattern)` | returns all match maps; no matches returns `[]` | conforms to zero-to-many |
| `find(null, pattern)` | returns `[]` | conforms because Lambda treats `null` as empty iterable content |
| `find(non_text, pattern)` | returns `error` | conforms |
| `split` | returns a collection of parts, including `[]` for accepted empty cases | conforms to zero-to-many |
| `slice` / `take` / `drop` on collections | return an empty collection when the selected interval is empty | conforms to zero-to-many |
| string `slice` / `chr` | return `""` when the successful string result has no characters | conforms to RF3B |
| `reduce([])` | returns `null` because no single reduced value exists without an identity | conforms to zero-or-one |
| `math.sqrt`, `math.sin`, `clip`, and similar vector overloads | accept numeric `[...]` and return ArrayNum, including typed empty results | conform to ArrayNum array-in/array-out |
| zero-length scalar/vector arithmetic | `vec_scalar_op` currently returns a generic Array/List before preserving the ArrayNum carrier | does **not** conform for an ArrayNum operand; should return a typed empty ArrayNum |
| `push` / `splice` | runtime helper returns the updated owner; procedural design treats calls as statement-like | internal COW behavior is coherent; public convention needs an explicit decision |
| invalid `push` / `splice` | currently logs and returns the original operand in several branches | does **not** conform; should become `error` |

### 6.1 Confirmed cardinality and error-channel violations

The following are implementation mismatches, independent of the unresolved public
choice between mutation Model A and Model B:

| Function/operation | Current behavior | Required behavior |
|---|---|---|
| `argmin([])`, `argmax([])` | return `null` for an empty collection | conforms to the admissive zero-or-one convention |
| `index_of`, `last_index_of` | public result is `int | null`; private raw adapter keeps `-1` | `null` for no match or non-applicable broad input; `error` operands remain `error` |
| `ord("")` / invalid UTF-8 / non-text | public result is `null`; private raw adapter keeps `-1` | `null` for no first code point or non-applicable broad input; `error` operands remain `error` |
| `slice` / `substring` / `subview` with a `null` offset | now returns `null` | conforms to RF3D; non-null non-integral offsets remain `error` |
| `slice` / `substring` / `subview` with a negative offset | now clamps to `0` | conforms to RF3D; previously wrapped from the end, contradicting §7.4 |
| `chr(null)` / invalid code point | returns `""` | conforms to RF3B; error operands and wrong argument shapes remain `error` |
| `input` invalid target/options or failed input | returns an error-bearing `RetItem` and publishes the runtime diagnostic | conforms to the effectful `T^E` contract |
| `parse` failed parse | returns an error-bearing `RetItem` with the parser diagnostic | conforms to RF4 non-admissive failure |
| `parse_html_fragment` invalid source or parser/allocation failure | returns `error` with a specific diagnostic | conforms to RF4 non-admissive failure |
| `query(data, non_type)` | logs an error and returns `null` | `error` |
| `index(value, invalid_index)` / error item | converts the case to `null` | propagate `error`; keep `null` for valid out-of-range/missing lookup |
| `url_resolve` malformed base/relative URL | returns `null` | `error` |
| `varg(non_integer)` | returns `null` | conforms to the admissive invalid-position convention; missing/out-of-range positions also return `null` |
| `pdf_parse_content_stream` invalid input/no runtime pool | returns `null` | `error`; valid empty content returns `[]` |
| `pdf_register_svg_image_resolver` invalid SVG/PDF operands | returns the unchanged SVG item and may silently do nothing | `error`; otherwise return the documented updated owner or unit |

The source locations for the main cases are `argmin`/`argmax`
([`lambda-vector.cpp`](../lambda/runtime/lambda-vector.cpp#L1440)), the index
sentinel pair ([`lambda-eval.cpp`](../lambda/runtime/lambda-eval.cpp#L4901)),
`ord`/`chr` ([`lambda-eval.cpp`](../lambda/runtime/lambda-eval.cpp#L5653)),
input/parse ([`lambda-eval.cpp`](../lambda/runtime/lambda-eval.cpp#L3725)),
generic indexing ([`lambda-eval.cpp`](../lambda/runtime/lambda-eval.cpp#L4162)),
and the PDF tokenizer ([`input-pdf.cpp`](../lambda/input/input-pdf.cpp#L324)).

Two slice-path defects surfaced in the same audit and are now fixed under RF3D. Both were absence violations of exactly the kind RF3A describes, one layer below `index_of` itself: a `null` offset failed integral conversion and became `error`, and a negative offset wrapped Python-style from the end, so a leaked `-1` selected real tail members instead of yielding absence. The offset contract now lives in two shared helpers (`lambda_slice_offsets`, `lambda_clamp_slice_range` in [`lambda-number-runtime.hpp`](../lambda/runtime/lambda-number-runtime.hpp)) used by all three call sites — collection `slice` and `subview` ([`lambda-vector.cpp`](../lambda/runtime/lambda-vector.cpp#L3002)) and string `substring` ([`lambda-eval.cpp`](../lambda/runtime/lambda-eval.cpp#L4735)) — so the rule cannot drift between the string, collection, and view overloads. Regression coverage is [`test/lambda/slice_negative_null_offsets.ls`](../test/lambda/slice_negative_null_offsets.ls).

The JavaScript engine is deliberately unaffected: `String.prototype.slice` and friends keep Web-compatible from-the-end semantics in their own `lambda/js/` implementations, which never route through these helpers.

`index_of`, `last_index_of`, and `ord` were a documented-policy conflict: the
old formal/library documentation and regression tests preserved the `-1`
sentinel. The migration changes the public registry rows to boxed `Item`
results with nullable `int` success types. Private raw helpers retain `-1` only
for JavaScript compatibility and other C-family adapters.

### 6.2 ArrayNum representation violations

These functions have the right visible `[...]` cardinality but do not preserve
the required typed numeric carrier on successful numeric-array paths:

| Function/operation | Current behavior | Required behavior |
|---|---|---|
| binary scalar/vector arithmetic with an empty operand | generic `Array`/`List` | typed empty `ArrayNum` |
| `math.cumsum([])` / `math.cumprod([])` | generic empty `Array` | typed empty `ArrayNum` |
| `fill(0, numeric)` | generic empty `Array` | typed empty numeric `ArrayNum` |
| `reverse`, `sort`, `sort2`, `unique` on `ArrayNum` | generic `Array`/`List` in empty and/or normal paths | same numeric dtype family, including zero lanes |
| `take`, `drop`, `slice` on `ArrayNum` | `ELEM_INT` paths deliberately fall back to generic content arrays | typed numeric arrays under the ArrayNum rule |

The relevant implementation points are the empty binary paths
([`lambda-vector.cpp`](../lambda/runtime/lambda-vector.cpp#L254)), cumulative
operations ([`lambda-vector.cpp`](../lambda/runtime/lambda-vector.cpp#L1399)),
`fill` ([`lambda-vector.cpp`](../lambda/runtime/lambda-vector.cpp#L1504)), and
the manipulation family ([`lambda-vector.cpp`](../lambda/runtime/lambda-vector.cpp#L2458)).
These are representation-level violations, not `null`/`[]` cardinality errors,
but they matter for dtype, chaining, and NumPy-style totality.

### 6.3 Conforming or intentional exceptions

The following patterns are consistent with the convention after accounting for
their declared result cardinality:

- `find` and `split` return collections, including empty collections.
- `reduce`, `min`, `max`, `avg`, `median`, `variance`, `deviation`, and
  `quantile` return `null` when a valid call has no single result.
- `sum`/`prod`, `all`/`any`, `dot`, and `norm` return exact-one identity values
  on empty input; these are scalar identity contracts, not zero-to-many results.
- `clip`, unary vector math, and mask selection already produce typed empty
  ArrayNum results on their numeric-array paths.
- Valid out-of-range indexing and missing member lookup return `null`; that is
  absence, not invalid input. Invalid index kinds and error operands must remain
  distinct from it.

The mutation discrepancy should be fixed at the source of the invalid operation,
while preserving the owner-returning COW ABI. Returning the original operand can
be mistaken for successful mutation and therefore violates the formal
semantics' "cannot be mistaken for success" test.

---

## 7. Runtime and registry requirements

1. Public sys funcs return boxed `Item` values at the runtime/JIT boundary.
   Collection results use `Item.array` or `Item.array_num`; they do not require a
   raw `Array*` return convention.
2. `SysFuncInfo.return_type` should describe the stable successful result shape
   as narrowly as the type system permits. `TYPE_ANY` must not justify changing
   between `null`, array, and error ad hoc.
3. Empty collection construction must preserve the intended container mode.
   In particular, an ArrayNum-producing vector overload returns a typed empty
   `Item.array_num`, not a generic array/list merely because its length is zero.
4. Invalid operands must propagate or construct `ItemError`; they must not be
   converted to empty collections or unchanged values.
5. Internal helpers may return owner pointers or boxed owners for rooting and
   COW writeback even when a future public mutation surface returns null/unit.
6. Tests should cover the three-way distinction for every collection API:
   non-empty success, empty/absent success, and invalid/error.
7. Raw `-1` helpers (`fn_index_of_raw`, `fn_last_index_of_raw`) are adapter-only surface for C/JavaScript compatibility. Public wrappers must guard error operands first and normalize the negative sentinel to `null`; no Lambda-surface path may call a `_raw` helper directly.
8. (RF7, D7.6.3.) A native procedure that writes a `var` argument in place
   declares the position at registration and receives the caller's home as
   `Item*`; it never returns the replacement owner as its result.

---

## 8. Review checklist for new sys funcs

Before registering a collection-related sys func, answer:

1. Is the successful result zero-to-many, zero-or-one, or exactly one?
2. What is the result for a valid empty input?
3. What is the result when no member matches?
4. Which inputs are invalid, and how is `error` returned or propagated?
5. If it produces ArrayNum, does numeric `[...]` input always produce numeric
   `[...]`, including zero- and one-lane cases, with explicit dtype, shape, and
   lane-poison rules?
6. If it mutates, does the whole family return the updated owner or null/unit?
7. Does COW require a distinct internal owner-returning helper?
8. Can any failure result be mistaken for a valid empty or absent result?
9. Does the contract smuggle an in-band sentinel — `-1` for absence, `0` or `-1` for "unlimited", an unchanged input for failure? RF3A and the named-option rule of formal-semantics §7.4 forbid all three.
10. (RF7, D7.6.3–D7.6.4.) If it writes an argument in place: is that position a
    registered `var` position, is the procedure a `pn` that is not async,
    and does it follow the RF7 write order (check first, un-share level by
    level, store replacements before the next allocation, pointers after the
    last allocation, keep the type)?

If those answers are explicit, `null`, `[]`, and `error` retain one meaning each
throughout Lambda's system-function surface.

## 9. Scientific math kernels

The first scientific batch adds `math.erf`, `erfc`, `fft`, `ifft`, `rfft`,
`irfft`, `solve`, `lstsq`, `orient2d`, `orient3d`, and `incircle`. The public
contracts and examples are in [Lambda_Sys_Func.md](../doc/Lambda_Sys_Func.md#error-functions).
These are pure native numerical primitives; chart assembly, spectral
estimators, integration policies, physical constants, and units remain
separate package concerns (S12.1.1v2, D7.6.1).

### Contracts and representation

- **S7.10.1v3, S7.10.5v3:** FFT results are ordinary arrays of boxed complex
  values, including `[]` and singletons. `irfft`, `solve`, and `lstsq` produce
  float ArrayNum values with stable vector/matrix rank, including empty arrays.
  Error functions reuse the existing real scalar/vector math kernel.
- **S7.10.2:** malformed shapes, non-finite scientific inputs, invalid inverse
  lengths, and singular/rank-deficient solve domains produce errors. Whole
  null operands propagate absence, with errors taking precedence. Fourier
  arithmetic itself may overflow to IEEE infinities/NaNs.
- **D7.6.1, D7.6.2:** each function uses a boxed `Item` boundary and registered
  successful result shape. The new SysFunc IDs are appended. The two libm
  scalar leaves also enter the shared native-call/GC-effect audit list.
- **D6.3.3:** sequence readers root their source and current row across boxing;
  output builders root the complex array across allocations. Algorithm
  scratch buffers contain only native numbers, never unrooted Lambda values.
- **S13.4.1:** QR inner products and norms use fixed balanced reduction trees.
  No tier-dependent BLAS reductions or parallel reduction order is introduced.

### Numerical choices

FFT uses an iterative radix-two kernel and Bluestein convolution for other
lengths. Forward transforms use the negative exponential sign; inverse
transforms normalize by the original sample count. `irfft(half, n)` requires
the original count so odd-length data round-trips without an implicit even
assumption. Allocation products and convolution-length growth are checked.
Windows, zero padding, PSD normalization, and frequency grids are explicit
higher-level operations.

`solve` uses partial-pivot LU. `lstsq` uses column-pivoted Householder QR,
following the stable orthogonal-factorization approach described in the
[LAPACK least-squares guide](https://www.netlib.org/lapack/lug/node40.html).
The initial domain is real square systems and full-column-rank problems
with `m >= n`; no normal-equation or silent pseudoinverse fallback is used.
Both accept multiple right-hand sides. Rank-deficient/underdetermined
minimum-norm solutions need a separate complete orthogonal/SVD design.

The geometry API returns a sign. Each finite binary64 coordinate is scaled
by `2^1074` to an exact integer. Existing libmpdec operations then evaluate
the translated 2×2 or 3×3 determinant, with squared lifts for `incircle`.
The degree-four determinant needs at most `4*2099+4` binary digits; a derived
decimal precision covers every intermediate. A local context isolates this
calculation from user decimal precision and traps. This follows the exact-sign
requirement discussed in [Shewchuk's robust-predicates paper and overview](https://www.cs.cmu.edu/~quake/robust.html),
using Lambda's existing arithmetic engine rather than an adaptive expansion
implementation. Decimal and wide-integer input conversion is explicitly
binary64 before this guarantee applies.

The kernels live in `lambda/runtime/lambda-eval-science.cpp`; exact integer
determinants remain behind `lambda/core/lambda-decimal.hpp`, preserving its
libmpdec encapsulation. Both regular and headless runtime source lists include
the scientific unit. Existing complex decoding and shaped-array allocation
helpers are shared, not duplicated. The latter now rejects shape-product
overflow and failed shape allocation instead of returning a flattened result.

Regression fixtures `test/lambda/math_science_{fft,linear,geometry}.ls` and
their goldens cover an independent direct DFT oracle, arbitrary transform
lengths, real round trips, residuals, pivoting, views, multiple right-hand
sides, empty/error/null distinctions, exact degeneracy, cancellation, extreme
exponents, and libm native/boxed paths. Run them under both interpreter and
JIT, including forced GC and freed-memory poisoning (D8.3.1, D6.3.3).
