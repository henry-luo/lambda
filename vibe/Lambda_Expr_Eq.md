# Lambda Equality Comparison: Design & Implementation

## 1. Problem Statement

Lambda currently supports value equality (`==`, `!=`) for scalar types (int, float, decimal, string, symbol, datetime, etc.) but returns an error for container types (list, array, map, element). The `fn_eq` function in `lambda-eval.cpp` has the comment:

```cpp
// other container types: fall through to error (structural equality not yet supported)
```

We need to decide:
1. **What kind of equality** should `==` perform on objects — reference or structural?
2. **How to handle type promotion** in nested comparisons — should `[1] == [1.0]`?
3. **Implementation details** for deep comparison of all container types.

---

## 2. Survey: Equality in Other Languages

### 2.1 Functional Languages (Default `==`)

| Language | `==` / default | Reference check | Notes |
|----------|---------------|-----------------|-------|
| **Haskell** | Structural (via `Eq` typeclass) | Not directly available | Derived `Eq` compares all fields recursively |
| **Erlang** | Structural (`==`) | N/A (immutable) | `=:=` for exact type match (no int→float) |
| **Elixir** | Structural (`==`) | N/A (immutable) | `===` for strict type (no int→float) |
| **Clojure** | Structural (`=`) | `identical?` | `=` does NOT promote numbers: `(= 1 1.0)` → `false` |
| **OCaml** | Structural (`=`) | Physical (`==`) | No implicit numeric promotion (type error) |
| **F#** | Structural (`=`) | `ReferenceEquals()` | Follows .NET equality |
| **Elm** | Structural (`==`) | Not available | No escape hatch to reference equality |
| **Scala** | Structural (`==`) | `eq` | `==` calls `.equals()` |
| **Racket** | Three-tiered | Three-tiered | `equal?` (deep), `eqv?` (shallow), `eq?` (identity) |

### 2.2 Non-Functional Languages (for contrast)

| Language | `==` on objects | Identity check | Numeric promotion in `==` |
|----------|----------------|----------------|---------------------------|
| **Python** | Structural (`__eq__`) | `is` | Yes: `1 == 1.0` → `True` |
| **JavaScript** | Reference | `===` (same) | Yes (with coercion): `1 == 1.0` → `true` |
| **Java** | Reference | Same `==` | Primitives yes, boxed no |
| **Ruby** | Structural (`==`) | `equal?` | Yes: `1 == 1.0` → `true` |

### 2.3 Key Observation

**Every major functional language uses structural/deep value equality as the default `==`.** Reference equality, when available at all, is a separate explicit operation used only for optimization.

---

## 3. Decision: Structural (Deep Value) Equality

**Lambda `==` will perform structural deep value equality.** Rationale:

1. **Functional identity**: In a pure functional language, values are defined by their structure, not their memory address. If two things look the same, they *are* the same.
2. **Immutability guarantee**: Lambda's immutable data makes structural equality well-defined and safe — no risk of mutation during traversal.
3. **Referential transparency**: Users expect `{a: 1, b: 2} == {a: 1, b: 2}` to be `true`.
4. **Industry consensus**: Java/JavaScript's choice of reference equality for `==` on objects is widely regarded as a mistake and a constant source of bugs.
5. **Compositionality**: If `1 == 1` is `true`, then `[1] == [1]` should also be `true`. Anything else is surprising.

**Reference equality** will be available as a separate system function (`ref_eq(a, b)` or `identical(a, b)`) for rare optimization cases. It is not exposed through an operator.

---

## 4. Type Promotion in Equality Comparison

### 4.1 Current Scalar Behavior

Lambda already promotes numeric types in `fn_eq`:

```cpp
// number promotion - only for int/float types
if (LMD_TYPE_INT <= a_item._type_id && a_item._type_id <= LMD_TYPE_NUMBER &&
    LMD_TYPE_INT <= b_item._type_id && b_item._type_id <= LMD_TYPE_NUMBER) {
    double a_val = it2d(a_item), b_val = it2d(b_item);
    return (a_val == b_val) ? BOOL_TRUE : BOOL_FALSE;
}
```

This means `1 == 1.0` → `true`, `1 == 1n` → `true` (all numeric types convertible to double).

### 4.2 The Container Question

Given scalar promotion, should containers follow suit?

| Expression | Question |
|-----------|----------|
| `[1] == [1.0]` | int vs float in list |
| `[1] == [1n]` | int vs decimal in list |
| `{a: 1} == {a: 1.0}` | int vs float in map value |

### 4.3 Survey: Container Promotion in Other Languages

| Language | `[1] == [1.0]` | Rule |
|----------|----------------|------|
| **Python** | `True` | Element-wise uses same `==` (which promotes) |
| **Elixir** | `true` | `==` promotes int→float, recursion inherits |
| **Erlang** | `true` | Same as Elixir |
| **Ruby** | `true` | Array `==` delegates to element `==` |
| **Clojure** | `false` | `=` does NOT promote numbers at all |
| **OCaml** | N/A | Type error — `[1]` and `[1.0]` have different types |
| **Haskell** | `True` | Numeric literals are polymorphic; both resolve to same type |

**Two camps:**
- **Compositional promotion** (Python, Erlang, Elixir, Ruby): If scalar `==` promotes, container `==` inherits that behavior through recursion.
- **Strict type** (Clojure, OCaml): No promotion at any level; types must match exactly.

### 4.4 Decision: Compositional Promotion

Lambda will use **compositional promotion**: structural equality recurses element-wise, and each element comparison follows the same `fn_eq` rules, including numeric promotion.

**Therefore:**
- `[1] == [1.0]` → **`true`** (element-wise: `1 == 1.0` → `true`)
- `[1] == [1n]` → **`true`** (element-wise: `1 == 1n` → `true` via numeric promotion)
- `{a: 1} == {a: 1.0}` → **`true`** (map value-wise: `1 == 1.0` → `true`)

**Rationale:**
1. **Least surprise**: Since `1 == 1.0` is already `true` in Lambda, it would be deeply confusing if wrapping in a list changed that.
2. **Compositional semantics**: Equality of a compound is defined by equality of its parts. No special container logic needed.
3. **Practical**: Data from JSON (`1.0`) and computed results (`1`) should compare equal when nested in structures.

### 4.5 Promotion Precision Caveats

Numeric promotion currently converts everything to `double` via `it2d()`. This has precision limits:

| Comparison | Result | Note |
|-----------|--------|------|
| `1 == 1.0` | `true` | Exact in double |
| `9007199254740993 == 9007199254740993.0` | **`true`** (imprecise) | int64 exceeds double precision (2^53+1) |
| `1n == 1.0` | `true` | Decimal-to-double may lose precision |

**Future consideration**: For int64 vs float comparisons near the precision boundary, we may want to special-case large int64 values to avoid false positives. For decimal vs float, converting decimal to double loses the whole point of decimal. A more precise path:
- **int64 vs float**: Convert float to int64 if it's a whole number; otherwise `false`.
- **decimal vs float**: Convert float to decimal, then compare with decimal arithmetic.
- **decimal vs int/int64**: Convert int to decimal, then compare with decimal arithmetic.

This is a targeted optimization for a later phase. The current `it2d` path is correct for the vast majority of real-world use.

---

## 5. Implementation Design

### 5.1 Recursive `fn_eq` for Container Types

Extend the existing `fn_eq` function in `lambda-eval.cpp` to handle container types by recursive element-wise comparison.

#### 5.1.1 List Equality

Two lists are equal if:
1. They have the same length.
2. Every corresponding element is `fn_eq`.

```
[1, "a", true] == [1, "a", true]    → true
[1, 2, 3] == [1, 2]                  → false  (different length)
[1, 2, 3] == [1, 2, 4]               → false  (element mismatch)
[] == []                              → true
```

#### 5.1.2 Array Equality

Typed arrays (`array[int]`, `array[float]`, etc.) and generic `array`:
1. Same length check.
2. Element-wise comparison with type promotion.

Cross-type array comparison (e.g., `array[int]` vs `array[float]`):
- Promote element-by-element via `fn_eq`. Since an `array[int]` element promoted to double equals an `array[float]` element, `[|1, 2, 3|] == [|1.0, 2.0, 3.0|]` → `true`.

Same-type fast paths:
- `array[int]` vs `array[int]`: `memcmp` on the backing buffer.
- `array[float]` vs `array[float]`: Element-wise double `==` (NaN != NaN).

#### 5.1.3 Map Equality

Two maps are equal if:
1. They have the same set of keys.
2. For every key, the corresponding values are `fn_eq`.

**Key order does NOT matter:**
```
{a: 1, b: 2} == {b: 2, a: 1}    → true
{a: 1, b: 2} == {a: 1}           → false  (missing key)
{a: 1} == {a: 1, b: null}        → false  (extra key, even if null)
```

##### Survey: Map Equality Across Languages

Every language treats map equality as order-independent:

| Language | `{a:1,b:2} == {b:2,a:1}` | Underlying structure | Notes |
|----------|---------------------------|---------------------|-------|
| **Clojure** | `true` | Hash array mapped trie | Maps are unordered by definition |
| **Elixir** | `true` | HAMT / hashmap | `%{a: 1, b: 2} == %{b: 2, a: 1}` |
| **Erlang** | `true` | Flat tuple / HAMT | Since OTP 18 |
| **Haskell** (`Data.Map`) | `true` | Balanced BST | Ordered by key internally, but equality ignores insertion order |
| **OCaml** (`Map`) | `true` via `equal` | AVL tree | `Map.equal (=) m1 m2` |
| **Scala** | `true` | Hash trie | `Map("a"->1,"b"->2) == Map("b"->2,"a"->1)` |
| **F#** | `true` | AVL tree | `Map` has structural equality |
| **Python** | `True` | Hash table | `{"a":1,"b":2} == {"b":2,"a":1}` |
| **Ruby** | `true` | Hash table | Insertion-ordered since 1.9, but `==` ignores order |

No language makes map `==` order-dependent. The mathematical definition of a map is a *set of key-value pairs* (or a function from keys to values), which has no inherent order.

##### Decision: Order-Independent Map Comparison

Lambda maps use **shapes** (ordered field layouts for efficient access). Two maps with the same keys inserted in different order have *different shapes*, but must still compare equal. The implementation compares by key lookup rather than by shape slot order:

1. Check that both maps have the same number of keys (fast reject).
2. For each key in map A, look up the same key in map B and compare values via `fn_eq`.
3. The key-count check in step 1 guarantees B has no extra keys.

**Optimization**: If two maps share the same shape pointer, their keys are identical in the same order — skip key lookup and compare values by slot index directly.

#### 5.1.4 Element Equality

Elements act as both lists and maps. Two elements are equal if:
1. They have the same tag name (symbol equality).
2. Their attribute maps are equal (map equality).
3. Their child lists are equal (list equality).

```
<div class="a">Hello</div> == <div class="a">Hello</div>    → true
<div class="a"> == <div class="b">                            → false
```

#### 5.1.5 Range Equality

Two ranges are equal if they have the same start, end, and step values.

#### 5.1.6 Cross-Type Sequence Equality

Ranges, lists, and all array variants (`array`, `array[int]`, `array[int64]`, `array[float]`) are treated as **sequences** for equality purposes. When two values belong to different sequence types, they are compared element-wise:

```
(1 to 3) == [1, 2, 3]            → true   (range vs list)
[1, 2, 3] == (1 to 3)            → true   (symmetric)
(1 to 3) == [1, 2, 4]            → false  (element mismatch)
(1 to 3) == [1, 2]               → false  (different length)
(1 to 1) == [1]                  → true   (single-element range vs list)
(0 to 0) == [0]                  → true
```

**Rationale:** A range is conceptually the sequence of values it generates. If `1 to 3` produces `1, 2, 3`, then it should be equal to any other sequence containing `[1, 2, 3]` — regardless of whether that sequence is stored as a list, a typed array, or a range. This follows the principle of **structural equality by value**, not by representation.

**Implementation:** Three helper functions provide uniform access to any sequence type:

- `seq_get_length(item, type_id)` — returns the number of elements for any sequence type (list length, array count, range length).
- `seq_get_element(item, type_id, index)` — extracts the element at a given index, converting typed array elements (int, int64, float) to `Item` values.
- `cross_seq_eq(a, a_tid, b, b_tid, depth)` — compares two sequences of potentially different types element-wise, delegating each element pair to `fn_eq_depth` (which applies numeric promotion, NaN handling, and nested structural comparison).

This covers all cross-type combinations: range↔list, range↔array, list↔array[int], array[float]↔array[int64], etc.

#### 5.1.7 Function Equality

Functions use **reference equality** — two function values are equal only if they are the same function object. This is universal across all languages (Haskell, OCaml, Elixir all make function equality either reference-based or an error).

```
let f = fn(x) x + 1
f == f                    → true
fn(x) x + 1 == fn(x) x + 1  → false (different closures)
```

### 5.2 Cycle Detection

Since Lambda is immutable and all data is constructed (not mutated to be cyclic), circular references should not occur. No cycle detection is needed in the initial implementation.

If Lambda ever supports lazy/thunked values that could form cycles, a visited-set (pointer identity) should be added.

### 5.3 Performance Considerations

1. **Pointer identity fast-path**: Before recursing, check `a.raw == b.raw`. If the pointers are identical, return `true` immediately. This is already done for strings and should be extended to all container types.
2. **Length/size check first**: Reject early if container sizes differ.
3. **Shape comparison for maps**: If two maps share the same shape (common in homogeneous data), key iteration can be skipped — just compare values in order.
4. **Typed array memcmp**: For `array[int]` vs `array[int]`, a single `memcmp` suffices.
5. **Stack depth**: Deep nesting could overflow the C stack. For the initial implementation, a reasonable recursion limit (e.g., 256 levels) with an error on overflow is sufficient.

### 5.4 NaN Handling

IEEE 754: `NaN != NaN`. Lambda should preserve this:
```
let x = 0.0 / 0.0    // NaN
x == x                 → false
[x] == [x]             → false  (element-wise: NaN != NaN)
```

### 5.5 Null and Error in Containers

```
[null] == [null]     → true   (null == null is true)
[null] == []         → false  (different length)
```

Error items in containers: `fn_eq` returns `BOOL_ERROR` when comparing error items. This propagates: if any element comparison returns `BOOL_ERROR`, the container comparison returns `BOOL_ERROR`.

---

## 6. Ordering Comparisons (`<`, `>`, `<=`, `>=`)

Structural ordering for containers is **not** proposed at this time. Comparisons like `[1, 2] < [1, 3]` (lexicographic) are useful but lower priority. The initial implementation focuses on `==` and `!=` only.

If lexicographic ordering is added later, it should follow the same compositional promotion rules.

---

## 7. Summary of Semantics

| Expression | Result | Rule |
|-----------|--------|------|
| `1 == 1.0` | `true` | Numeric promotion (existing) |
| `1 == 1n` | `true` | Numeric promotion (existing) |
| `"a" == "a"` | `true` | String value comparison (existing) |
| `[1, 2] == [1, 2]` | `true` | List structural equality (new) |
| `[1] == [1.0]` | `true` | Compositional promotion (new) |
| `[1] == [1n]` | `true` | Compositional promotion (new) |
| `{a: 1} == {a: 1}` | `true` | Map structural equality (new) |
| `{a: 1, b: 2} == {b: 2, a: 1}` | `true` | Key-order independent (new) |
| `{a: 1} == {a: 1.0}` | `true` | Compositional promotion (new) |
| `<x a=1/> == <x a=1/>` | `true` | Element structural equality (new) |
| `[] == []` | `true` | Empty containers equal (new) |
| `[1] == [1, 2]` | `false` | Different length (new) |
| `{a: 1} == {a: 1, b: 2}` | `false` | Different key set (new) |
| `f == f` | `true` | Function reference equality (new) |
| `fn(x) x == fn(x) x` | `false` | Different function objects (new) |
| `(1 to 3) == [1, 2, 3]` | `true` | Cross-type sequence equality (new) |
| `[1, 2, 3] == (1 to 3)` | `true` | Symmetric cross-type (new) |
| `(1 to 3) == [1, 2]` | `false` | Different length cross-type (new) |
| `NaN == NaN` | `false` | IEEE 754 (existing) |
| `[NaN] == [NaN]` | `false` | Compositional NaN (new) |
| `true == 1` | `error` | Type mismatch (existing) |
| `"1" == 1` | `error` | Type mismatch (existing) |

---

## 8. Implementation Checklist

- [ ] Extend `fn_eq` in `lambda-eval.cpp` for `LMD_TYPE_LIST`
- [ ] Extend `fn_eq` for `LMD_TYPE_ARRAY`, `LMD_TYPE_ARRAY_INT`, `LMD_TYPE_ARRAY_INT64`, `LMD_TYPE_ARRAY_FLOAT`
- [ ] Extend `fn_eq` for `LMD_TYPE_MAP`
- [ ] Extend `fn_eq` for `LMD_TYPE_ELEMENT`
- [ ] Extend `fn_eq` for `LMD_TYPE_RANGE`
- [ ] Extend `fn_eq` for `LMD_TYPE_FUNC` (reference equality)
- [ ] Add pointer-identity fast-path for all container types
- [ ] Add recursion depth limit
- [ ] Add unit tests: list equality, nested lists, mixed-type promotion
- [ ] Add unit tests: map equality, key-order independence
- [ ] Add unit tests: element equality
- [ ] Add unit tests: cross-type array comparison
- [ ] Add unit tests: NaN in containers
- [ ] Add unit tests: error propagation in containers
- [ ] (Future) Improve int64/decimal precision in cross-type numeric comparison
- [ ] (Future) Add `ref_eq()` system function
- [ ] (Future) Lexicographic ordering for containers
