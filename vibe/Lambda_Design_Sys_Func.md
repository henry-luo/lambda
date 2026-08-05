# Lambda System Function Return-Value Convention

- **Status:** design proposal — return-value convention
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

If those answers are explicit, `null`, `[]`, and `error` retain one meaning each
throughout Lambda's system-function surface.
