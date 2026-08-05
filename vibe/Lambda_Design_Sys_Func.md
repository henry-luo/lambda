# Lambda System Function Return-Value Convention

- **Status:** design proposal — return-value convention
- **Date:** 2026-08-05
- **Scope:** Lambda system functions and procedures that search, select,
  transform, or mutate collections
- **Semantic authority:**
  [`doc/Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md), especially
  §3 (truthiness), §4.7 (numeric and vector totality), §7.6 (error
  participation), §7.7 (broad input and in-domain results), §8.1 (iteration
  and length), and §10.2 (vectorization).

---

## 1. Decision summary

The return value follows the cardinality and meaning of the operation:

| Operation contract | Empty or missing outcome | Invalid input/operation |
|---|---|---|
| **Zero-to-many** | `[]` | `error` |
| **Zero-or-one** | `null` | `error` |
| **ArrayNum-producing vector operation** | typed numeric `[]` | `error` for an invalid whole operation |
| **In-place mutation** | updated owner, or consistently `null`/unit for the whole mutator family | `error` |

In short:

1. **Zero-to-many result → `[]`.**
2. **Zero-or-one result → `null`.**
3. **Invalid input or operation → `error`.**
4. **ArrayNum vector operation → accept numeric `[...]` and, for the array
   overload, always return numeric `[...]`, including a typed empty `[]`.**
5. **In-place mutation → return the updated array, or consistently return
   `null`/unit. Never use `[]` as a generic mutation-success sentinel.**

The result shape must not depend on cardinality. A function whose successful
result is a collection returns a collection whether it contains ten values, one
value, or no values.

---

## 2. The four semantic states

These states are distinct and must remain distinguishable:

| State | Meaning | Representation |
|---|---|---|
| Empty success | The operation ran successfully and found/produced no members | `[]` |
| Absent value | The operation ran successfully but no single value exists | `null` |
| Failure | The operation could not validly run or complete | `error` |
| Mutation completion | The owner was updated, possibly with no structural change | updated owner or `null`/unit, according to the family contract |

Lambda cannot treat `null` and `[]` as interchangeable. `null` is falsy, while
every container, including `[]`, is truthy. They also carry different type tags
and communicate different result cardinalities.

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

Examples include `find`, `filter`-shaped operations, `split`, `slice`, and a
hypothetical `removed_items` operation. No matches or members is a successful
empty collection, not absence and not failure.

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

### RF4 — Invalid input or operation returns `error`

Neither `null` nor `[]` may hide a failed operation. Examples include:

- calling an array-only operation on a non-array;
- supplying an invalid pattern or option;
- mutating a read-only or unsupported view;
- passing a non-integral index/count where an integer is required;
- allocation, parsing, or I/O failure.

This preserves the distinction between:

```lambda
find("abc", \d)   // []: valid search, no matches
find(42, \d)      // error: invalid source type
```

An existing error operand propagates according to the error-participation rules
in the formal semantics; a function must not convert it into an empty or absent
success value.

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
| Python optional search | value or `None` — zero-or-one |
| Python comprehension / collected filter | `[]` — zero-to-many |
| Python in-place list mutators | `None`/unit-like |
| Rust `Iterator::find` | `Option::None` — zero-or-one |
| Rust collected filtering | empty `Vec` — zero-to-many |
| Rust `Vec::splice` | iterator over removed members, possibly empty |
| NumPy ufunc over array-like input | `ndarray`, including a zero-length result; broadcasting determines shape |

The transferable rule is not "always return null" or "always return an empty
array." It is: preserve the successful result's cardinality, and keep failure
on a separate channel.

---

## 6. Current Lambda audit

| Function/operation | Current behavior | Convention |
|---|---|---|
| `find(source, pattern)` | returns all match maps; no matches returns `[]` | conforms to zero-to-many |
| `find(null, pattern)` | returns `[]` | conforms because Lambda treats `null` as empty iterable content |
| `find(non_text, pattern)` | returns `error` | conforms |
| `split` | returns a collection of parts, including `[]` for accepted empty cases | conforms to zero-to-many |
| `slice` / `take` / `drop` | return an empty collection when the selected interval is empty | conforms to zero-to-many |
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
| `argmin([])`, `argmax([])` | returns `error` | `null`: the valid operation has no single index |
| `index_of`, `last_index_of` | returns raw `-1` for no match and also for invalid operands | `null` for no match; `error` for invalid operands |
| `ord("")` / invalid UTF-8 / non-text | returns raw `-1` | `null` for no first code point; `error` for invalid input/encoding |
| `chr(null)` / invalid code point | returns an empty string | choose an explicit null-input rule; invalid code points must be `error` |
| `input` invalid target/options or failed input | returns successful `null` through a raising `RetItem` path | `error` |
| `parse` failed parse | returns successful `null` through a raising `RetItem` path | `error` |
| `parse_html_fragment` invalid source or parser/allocation failure | returns `null` | `error`; reserve `null` only for a documented valid absence |
| `query(data, non_type)` | logs an error and returns `null` | `error` |
| `index(value, invalid_index)` / error item | converts the case to `null` | propagate `error`; keep `null` for valid out-of-range/missing lookup |
| `url_resolve` malformed base/relative URL | returns `null` | `error` |
| `varg(non_integer)` | returns `null` | `error`; retain `null` for missing/out-of-range variadic position |
| `pdf_parse_content_stream` invalid input/no runtime pool | returns `null` | `error`; valid empty content returns `[]` |
| `pdf_register_svg_image_resolver` invalid SVG/PDF operands | returns the unchanged SVG item and may silently do nothing | `error`; otherwise return the documented updated owner or unit |

The source locations for the main cases are `argmin`/`argmax`
([`lambda-vector.cpp`](../lambda/runtime/lambda-vector.cpp#L1440)), the index
sentinel pair ([`lambda-eval.cpp`](../lambda/runtime/lambda-eval.cpp#L4901)),
`ord`/`chr` ([`lambda-eval.cpp`](../lambda/runtime/lambda-eval.cpp#L5653)),
input/parse ([`lambda-eval.cpp`](../lambda/runtime/lambda-eval.cpp#L3725)),
generic indexing ([`lambda-eval.cpp`](../lambda/runtime/lambda-eval.cpp#L4162)),
and the PDF tokenizer ([`input-pdf.cpp`](../lambda/input/input-pdf.cpp#L324)).

`index_of`, `last_index_of`, and `ord` are also a documented-policy conflict:
the current formal/library documentation and regression tests explicitly preserve
the `-1` sentinel. Adopting the convention in this document therefore requires
an API/type migration, not merely changing a return statement. Their registry
rows currently use raw `C_RET_INT64` ([`sys_func_registry.c`](../lambda/runtime/sys_func_registry.c#L555)).

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

If those answers are explicit, `null`, `[]`, and `error` retain one meaning each
throughout Lambda's system-function surface.
