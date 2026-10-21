# Lambda Typed Arrays (`ArrayNum`)

> **Status:** Consolidated design record, 2026-10-21. It merges the four earlier typed-array proposals — Typed Array 1 (one `ArrayNum` type with compact lanes, 2026-04), 2 (N-D arrays, views, vectorized math, masks, 2026-06), 3 (LambdaJS storage unification, 2026-06) and 4 (image processing, SIMD auto-vectorization, mutable views, 2026-06) — and brings them in line with the rulings ratified since: the number model, `T[]` contracts, copy-on-write values and views, and keyword element-wise comparison. The body states the current design only. Superseded wording is kept, struck through, in [Appendix S](#appendix-s--superseded-rulings). Updated the same day with two rulings by the user, now S9.3.2 (construction preserves each element's type and precision) and S10.2.4 (array arithmetic promotes as scalar arithmetic does). Implementation status and the few implementation notes are in [Appendix A](#appendix-a--implementation-notes-and-status).
>
> **Scope:** Lambda's homogeneous numeric array: element kinds, contracts, construction, indexing and selection, N-D shape, arithmetic, views and mutation, the byte-storage substrate shared with LambdaJS, the image-processing library, and the performance model. Non-numeric typed arrays (`string[]`, record arrays) share the contract model but are native-lane `Array`s, covered by the [impl plan](impl/Lambda_Impl_Typed_Array.md). The JS spec layer is covered by [JS_12_TypedArrays](../doc/dev/js/JS_12_TypedArrays.md).
>
> **Authority:** the formal specs win wherever they speak (`doc/Doc_Convention.md` §2). This record is normative only where no `S#`/`D#` ruling covers the point. Where the code disagrees with either, the code is the defect; Appendix A.7 lists the known gaps.

**Spec linkage**

| Topic | Rulings | Section |
|---|---|---|
| Three physical array forms; `ArrayNum` null-free and observably an `Array` | D2.6.1v3, D2.6.2, DI8, S5.3.1 | §1, §2 |
| `int` lane is the i64 lane with poison | D2.6.3, S4.4.1 | §2 |
| Lanes never hold errors | DI20, D2.8.1, S7.8.2 | §3.4 |
| `T[]`, `T[n]`, rank | S11.1.1v3 | §3.1 |
| Covariance, `var` invariance | S9.2.1 | §3.2 |
| Contract admission, value-aware numeric admission | S11.4.1v3, S11.4.5 | §3.3 |
| Representation certificate | D3.3.3v3, D3.1.1v5 | §3.3 |
| Unproven indexed read is `T?` | D2.5.3 | §3.5 |
| Invalid reads are `null`; no negative indices; `last` | S7.1.1v3, S7.2.1, S7.2.2 | §5 |
| Positional selection is NumPy's: range, mask, index array | S8.2.4v3 | §5 |
| Promotion lattice, division and modulo | S4.4.1–S4.4.4, S4.5.1–S4.5.3 | §7.2 |
| Vectorized arithmetic; kind preservation | S10.2.1, S2.5.7v4 | §7.1 |
| Element-wise arithmetic promotes as scalar arithmetic does | S10.2.4 | §7.2 |
| Element-wise comparison keywords; mask consumption | S10.2.2v2, S10.2.3 | §7.4 |
| Vectorized system functions | S7.10.5v3 | §7.5 |
| Construction preserves each element's type and precision | S9.3.2 | §4 |
| Read views are snapshots; write-through views are borrows | S9.2.2, S9.2.4v2, S9.3.1 | §8 |

**Related records:** [Number Model](Lambda_Semantics_Number_Model.md) (lane semantics, §3.3–§3.5), [Runtime COW](Lambda_Design_Runtime_COW.md) (CW16, views as borrows), [Type Enforcement](Lambda_Design_Type_Enforcement.md) (TE-1..TE-9), [Binary type §11](Lambda_Type_Binary.md) (`ByteStorage` / `ByteBufferHandle`), [Expr_Query §4.1](Lambda_Expr_Query.md) (subscript worlds), [DataFrame](Lambda_Design_DataFrame.md) (future consumer). Implementation records: [Lambda_Impl_Typed_Array.md](impl/Lambda_Impl_Typed_Array.md) (`T[]` contracts, certificates), [Lambda_Impl_Binary2.md](impl/Lambda_Impl_Binary2.md) (byte substrate). Detailed design: [LR_05 §4–§8](../doc/dev/lambda/LR_05_Strings_and_Vectors.md) (the `ArrayNum` engine), [LR_03 §4](../doc/dev/lambda/LR_03_Value_and_Type_Model.md) (layout), [LR_08](../doc/dev/lambda/LR_08_Memory_and_GC.md) (GC), [LR_11](../doc/dev/lambda/LR_11_Mark_Data_API.md) (readers).

---

## 0. Summary

- **A typed array is an array.** `ArrayNum` is one of three physical array forms. Equality, `type()`, iteration and formatting cannot tell it apart from a boxed `Array` holding the same values (D2.6.2, S5.3.1).
- **One container, many element kinds.** A single `ArrayNum` type carries a one-byte element kind: `int`, `float`, the sized integers and floats `i8`…`u64`, `f16`, `f32`, and `bool`, plus a JS-only clamped byte. Elements are packed at their natural width (1, 2, 4 or 8 bytes).
- **Contract first, representation second.** `T[]` is a homogeneous-array contract enforced at every boundary (S11.1.1v3, S11.4.1v3). The representation follows the admitted contract, and an enforced contract leaves a certificate that makes later boundaries O(1) (D3.3.3v3).
- **Lanes hold only values.** No lane holds a null (DI8) or an error (DI20). Admitting a null into a nullable contract demotes the array to a native `Array<T?>`.
- **The positional world is NumPy's.** N-D shape with strides, broadcasting, axis reductions, multi-axis subscripts `m[i, j]`, boolean masks, index arrays and zero-copy views (S8.2.4v3).
- **Arithmetic is the scalar lattice, applied lane by lane.** Same-lane `+ - *` wraps in the lane, mixed sized lanes meet in the smallest containing lane, and true division leaves the sized lanes (S4.4, S4.5).
- **Values stay values.** A read view behaves as a snapshot. Writing through to a base is legal only as a `var`-parameter borrow (S9.2.2). Masked and element writes are procedural statements on the owner.
- **Libraries, not types.** An image is a 2-D or 3-D `ArrayNum`. The image toolkit is a set of functions over it, built on one stencil engine.
- **One byte substrate with LambdaJS.** A JS typed array's storage is a generation-aware `ArrayNum` view over a non-moving `ByteBufferHandle`. JS spec semantics stay in the JS layer.

---

## 1. Principles

1. **Representation invariance.** `ArrayNum` is storage, not a language type. A program cannot observe whether `[1, 2, 3]` is a boxed `Array`, a native-lane `Array` or an `ArrayNum` — not through `==`, `type()`, `is`, iteration, printing or serialization (D2.6.2). Unboxed numeric arrays are representation only, and equality is never layout-based (S5.3.1).
2. **One numeric array type.** The original three types (`ARRAY_INT`, `ARRAY_INT64`, `ARRAY_FLOAT`) shared one layout and appeared in the same `switch` statements: about 420 triplicated dispatch points across 16 files. Adding a fourth element width would have added a fourth branch everywhere. One type ID with an element-kind byte collapses that to one branch, and the JIT still emits a single native load because it tracks the element kind statically.
3. **The contract is authoritative.** The element kind is a storage fact; it never proves a language contract. The full `Type*` graph does (D3.1.1v5). An annotation may drive representation only if its boundary is enforced (S11.4.1v3).
4. **Lanes are total.** Native lanes cannot encode null or error, so neither is ever stored. The ruled alternatives — an in-band error sentinel, a side table of errors — are rejected (D2.8.1).
5. **NumPy for positions, XPath for types.** A positional key (integer, range, mask, index array) selects a window whose shape follows the key; a type key walks content (S8.2.4v3). Typed arrays live in the positional world.
6. **Value semantics by construction.** Pure `fn` code has no assignment statement, so it cannot mutate. Procedural code mutates owners it holds as `var`; aliasing is admitted only as a scoped, checked borrow (S9.2.2).
7. **No hidden widening.** Construction never converts an element: it keeps each element's type and precision, and packs only elements that already share one exact type (S9.3.2). A declared contract admits a value only when it embeds exactly (S11.4.5). Arithmetic promotes as scalar arithmetic does, and a result that overflows a sized lane wraps in that lane, as Go does (S10.2.4, S4.4.4).
8. **Zero cost when unused.** A 1-D owned array pays nothing for N-D, views or JS sharing. The flags live in the existing 8-byte header, and the shape side-table is allocated only for N-D arrays and views.
9. **Libraries over types.** Images, and later data frames, are conventions and function sets over `ArrayNum`, not new containers. scikit-image and NumPy stand in the same relationship.

---

## 2. Element kinds

| Lambda element type | Lane tag | Bytes | Read back as | Notes |
|---|---|---|---|---|
| `int` | `ELEM_INT` | 8 | `int` | The i64 lane: finite values plus the int poison encodings (D2.6.3). Distinct from `i64` because `int ∥ i64` (S4.4.1): a finite `int` embeds in `i64`, poison does not. |
| `float` (= `f64`) | `ELEM_FLOAT64` | 8 | `float` | One tag per representation: `float[]` and `f64[]` are one array type (Number Model §3.5). |
| `i8` `i16` `i32` `i64` | `ELEM_INT8` … `ELEM_INT64` | 1 / 2 / 4 / 8 | the sized scalar | Two's-complement storage. |
| `u8` `u16` `u32` `u64` | `ELEM_UINT8` … `ELEM_UINT64` | 1 / 2 / 4 / 8 | the sized scalar | |
| `f16` `f32` | `ELEM_FLOAT16`, `ELEM_FLOAT32` | 2 / 4 | the sized scalar | IEEE half and single precision. |
| `bool` | `ELEM_BOOL` | 1 | `bool` | Masks. |
| — (JS `Uint8ClampedArray`) | `ELEM_UINT8_CLAMPED` | 1 | `u8` | JS storage kind only; it has no Lambda spelling. |

**Why the sized kinds exist.** `i8`…`f64` are fixed-width storage and interop types: wire formats, native-module struct fields, GPU and model weights, and JS and Arrow interop. They are not hidden tiers of `int` and `float`; size is semantic at the storage level (Number Model §3). A `u8[]` of 1000 elements takes 1 KB where a boxed array takes 8 KB:

| Lane width | Kinds | 1000 elements | vs. boxed `Array` |
|---|---|---|---|
| 1 byte | `i8` `u8` `bool` | 1 KB | 8× smaller |
| 2 bytes | `i16` `u16` `f16` | 2 KB | 4× smaller |
| 4 bytes | `i32` `u32` `f32` | 4 KB | 2× smaller |
| 8 bytes | `int` `float` `i64` `u64` | 8 KB | same |

**Why `bool` is its own kind and not `u8`.** `any`/`all` and `sum(mask)` have Boolean meaning (S10.2.3), formatters print `true`/`false`, and admission differs: only Boolean values enter a `bool[]`.

**Why the clamp is a kind and not a flag.** Clamping (NaN → 0, saturate to [0, 255], round half to even) is a write-coercion difference, not a storage or read difference. A flag would put a branch into every hot store loop and muddy width lookup. As a kind, the clamp lives in one place — the JS store path — and every other operation treats the lane exactly as `u8`.

**What is not a kind.** There are no nullable, `decimal`, `integer`, `complex`, string or union lanes. Their arrays are native-lane or boxed `Array`s, chosen by the representation table in §3.3.

---

## 3. Types and contracts

### 3.1 Spelling and rank

`T[]` is the homogeneous-array contract: every logical element satisfies `T`, at any length (S11.1.1v3). It applies to every element kind — `int[]`, `float[]`, `i8[]`, `u8[]`, `f32[]`, `bool[]` — and to non-numeric elements alike. `T[n]` fixes the length. Repeated postfixes preserve rank: `float[][]` is an array of `float[]`, and `int[2][3]` is three arrays of two. `[T]` is a structural pattern for exactly one `T`, never an alternative spelling of `T[]`.

A rectangular `int[][]` may be stored as one 2-D `ArrayNum`. That is a representation of `int[][]`, never a licence to treat a row as an `int`.

### 3.2 Relations

Value positions are covariant: `S[] ⊑ T[]` when every `S` embeds exactly into `T` (S9.2.1, S4.4.1). So `i8[] is int[]`, `u8[] is u16[]` and `f32[] is float[]` are true, while `i8[] is u8[]` and `float[] is f32[]` are false. This is sound because those channels copy, so the covariant-array hole cannot arise. Representation widening (i8 lanes to int lanes, or unboxed to boxed) happens lazily at copy time.

A `var` parameter is the only aliasing channel, so it is invariant: passing `var xs: i8[]` to `pn f(var a: int[])` is a compile error (S9.2.1). Invariance includes rank, nullability, sized kind and named-record layout.

### 3.3 Admission and representation

An annotation is a contract, with three outcomes and never a fourth: statically proven, statically rejected, or a deferred runtime check whose failure is a rich error naming the boundary, expected type, actual value and location (S11.4.1v3). A `T[]` boundary validates every logical element before it publishes the representation; failure never establishes the binding and never leaves a half-built array.

Numeric admission is value-aware (S11.4.5): a dynamic element that embeds exactly passes and is re-represented, and an inexact one fails. A computed `[1, 2]` crossing into `u8[]` becomes `u8` lanes; `[300]` is rejected, never wrapped to `[44]`. Static positions reject a class that does not embed, so sized literals are written with their suffix (`[1u8, 2u8]`).

The representation follows the admitted leaf contract:

| Contract | Representation |
|---|---|
| non-null `int`, `float`, `i8`…`u64`, `f16`, `f32`, `bool` | `ArrayNum` of the exact kind |
| nullable numeric or `bool` | native-lane `Array` with the ruled sentinel lane |
| nullable `i64?`, `u64?` | destination-owned `TypedItem` slots (D2.6.4v3) |
| rectangular nested numeric rank | N-D `ArrayNum`; nested arrays when ragged |
| union, `any`, error-admitting, heterogeneous | boxed `Array` |

A successful boundary attaches an immutable, interned **representation certificate** proving rank, leaf contract and physical carrier. It is reused only while both the target contract and the current carrier still match; a transform that does not materialize the matching carrier clears it, and the next checked boundary re-admits (D3.3.3v3). The first dynamic crossing costs O(n); repeated same-contract crossings cost O(1); nothing is checked per read. A certificate is never inferred from an element kind or byte width.

A typed empty array still has a typed carrier: `[]` under `u8[]` is an empty `u8` `ArrayNum`, and an unannotated `[]` stays an open boxed array.

### 3.4 Null and error

`ArrayNum` is strictly non-null (D2.6.2, DI8). Storing null into `int[]` is rejected. Storing null into `int?[]` atomically demotes the carrier to a native `Array<int?>` before the write. `nan` is a float value, not an error, so a float lane may hold it.

No lane holds an error (DI20). The type decides the representation: `[f(x) ^ { 0 } for x in xs]` is `int[]` with its lane preserved, while `[f(x) for x in xs]` is `(int | error)[]`, boxed, keeping each element's error (S7.8.2).

### 3.5 Indexed read types and reflection

An unproven scalar read `a[i]` from `T[]` has public type `T?` (D2.5.3); a bounds proof may use `T` privately in the emitter but never changes the public type. Reads are rank-aware: for `m: int[][]`, `m[i]` is `int[]?` and `m[i, j]` is `int?`. `flatten` and `ravel` return `int[]`, `transpose` keeps rank, a static `reshape(xs, [r, c])` gives `T[][]`, and a dynamic reshape gives the open `array` type plus a certificate.

`type(x)` is `array` for every physical form (D2.6.2). `x is u8[]` and `case u8[]:` test the structural contract, not the lane tag — and because of covariance a `u8[]` also matches `case int[]:`, so match arms go narrowest first.

---

## 4. Construction

### 4.1 Literals

Construction preserves each element's literal type and precision (S9.3.2, ruled by the user 2026-10-21). A literal is packed into an `ArrayNum` only when every element already has the same exact type; any mix stays a boxed `Array` holding the elements as written. Elements are never promoted to a common numeric type, even when the promotion would be exact, because the element's type is part of its value: `type([1, 2.5][0])` must be `int`, as it is in a boxed array (D2.6.2).

| Literal | Result |
|---|---|
| `[1, 2, 3]` | `int` lanes |
| `[1.0, 2.5]` | `float` lanes |
| `[1, 2.5]`, `[1, 2i64]` | boxed `Array`; `1` stays `int` |
| `[255u8, 0u8]`, `[0.5f32, 0.3f32]` | that sized lane |
| `[true, false]` | `bool` lanes |
| mixed sized kinds, or sized with plain numbers | boxed `Array` |
| `[2^53 + 1 as i64, 2.5]` | boxed `Array`; the `i64` keeps every digit |
| anything non-numeric | boxed `Array` |

An unannotated mixed literal has an open element type; nothing is inferred from a least upper bound. Under an annotation the literal is admitted by the target's contract (§3.3): `let xs: float[] = [1, 2.5]` stores `1.0`, because the declared contract asks for `float` and `1` embeds exactly (S11.4.5).

### 4.2 Nested literals are N-D

`[[1, 2], [3, 4]]` becomes one 2-D `ArrayNum`. There is no `tensor(...)` builtin and no separate syntax. Detection happens in two places:

- **Statically**, when the builder sees a rectangular nested literal of one numeric kind. It allocates one contiguous buffer and fills it directly; no row arrays are built.
- **Dynamically**, when an array is finished from runtime rows — `[r1, r2, r3]` where each row is an `ArrayNum` of one kind and equal length, or an N-D array of equal shape (giving N+1 dimensions). Promotion is eager: one new contiguous buffer, one copy per row.

Rows must have the same exact element type. Ragged rows, rows of different types (`int` with `float`, `i64` with `float`, `int` with `i64`) and rows mixed with scalars stay a nested `Array` whose rows keep their own types (S9.3.2). Either way the observable value is the same under indexing and iteration. A nested array is not a tensor, so `shape`, `ndim` and the whole-array reductions do not treat it as one (§13, Q7).

Dynamic promotion costs O(N) and briefly about twice the data size, until the old rows are collected. NumPy's `np.array([r1, r2])` has the same cost for the same reason: strides can only address one contiguous block. Static literals avoid it, and the downstream payoff — vectorized arithmetic, broadcasting and reductions over contiguous memory instead of per-row dispatch — is large. Lazy promotion (defer the copy until the first vectorized operation) is deferred.

### 4.3 Typed results from pipes and comprehensions

Functions and pipes return arrays for any sequence input (S2.5.7v4). When every produced element has the same exact numeric or Boolean type, the result is packed: `xs |> ~ * 2`, `[for (x in xs) x * 10]` and `[for (x in xs where x > 2) x]` all yield `ArrayNum`s that feed `sum`, `reshape` and broadcasting without re-boxing. The packing is done once, where every array builder finishes (Appendix A.4), so pipes, comprehensions and filters share it. A result of mixed types stays generic: `[for (x in [1, 2, 3]) if (x == 2) 2.5 else x]` holds two `int`s and a `float`. A bare `for` expression is a list and stays one, because it must splice where it lands (S2.5.2v2).

### 4.4 `fill` and planned construction functions

`fill(n, v)` builds an array of `n` copies of `v`; with a numeric or Boolean `v` it is typed by `v`'s lane.

NumPy-style constructors — `zeros(n, T)`, `ones(n, T)`, `arange(start, stop, step, T?)`, `linspace(start, stop, count, T?)` — and array forms of the sized conversions (`u8(xs)`, `f32(xs)`) were proposed but are neither built nor ruled. Their conversion semantics must be reconciled with value-aware admission first (§13, Q2).

---

## 5. Indexing and selection

- **Positions.** Indexing is 0-based. Negative indices carry no meaning: `a[-1]` is out of range and reads `null`, exactly like `a[len(a)]` (S7.2.1). Every invalid read is `null`; none raises (S7.1.1v3). `last` is `len − 1` of the innermost enclosing subscript's container, so `a[last]`, `a[last - 1]` and `a[5 to last]` work; on an N-D array `last` counts the leading axis (S7.2.2).
- **Multi-axis subscripts.** `m[i, j, k]` takes one key per axis and reads the element at `offset + Σ keyᵢ · strideᵢ` with no allocation. With fewer keys than axes, each key steps one leading axis, so `t[1, 3]` equals `t[1][3]` and is a view of rank `ndim − keys`. More keys than axes, or any out-of-range key, reads `null`. Multi-axis tail access uses `last` per axis or ordinary length arithmetic.
- **Ranges.** `a[i to j]` clamps to the array and is `[]` when empty (S8.2.4v3). It may share storage as a view, which is unobservable under S9.2.2.
- **Masks.** `a[mask]` with a `bool[]` of the same shape selects the true lanes in row-major order as a 1-D array. A 1-D mask whose length is `shape[0]` selects leading-axis rows, giving shape `(k, …trailing)`. The result keeps the source lane. A mask selection is always a copy, since a non-contiguous selection cannot alias.
- **Index arrays.** `a[[0, 2, 5]]` is a gather. The result has the index array's own length and shape (`a[[[0, 1], [2, 3]]]` is 2-D), and an out-of-range or negative position contributes `null` (S8.2.4v3). A gather with a `null` result cannot stay in a non-null lane (§3.4).
- **Positional selections always yield arrays**, whatever the count: shape follows the key, not the data (S8.2.4v3).

Why first-class `m[i, j, k]`. Chained `m[i][j][k]` on a 3-D array allocates two intermediate views per scalar read. `m[i, j, k]` is one offset computation. More importantly, it is the only way to write one element of an N-D owner (§8.3). NumPy, Julia, R, C#, Swift and — after 30 years of `[i][j][k]` — C++23 all have the form; the prior-art table is in [Appendix C](#appendix-c--prior-art).

---

## 6. N-dimensional arrays

### 6.1 Model

An N-D `ArrayNum` is a flat element buffer plus a shape side-table: `ndim` (at most 32), `shape[ndim]`, `strides[ndim]` in elements (not bytes), an element `offset`, and contiguity bits. `length` is the product of the shape. A 1-D owned array has no side-table and runs the unchanged 1-D path. The side-table costs `sizeof(header) + 16 · ndim` bytes, regardless of data size.

### 6.2 Iteration

`for x in m` walks the leading axis: each `x` is a view of rank `ndim − 1`, as in NumPy. Scalar iteration over an N-D array is `for x in flatten(m)`. A 1-D array yields scalars.

### 6.3 Structural operations

| Operation | Meaning |
|---|---|
| `shape(a)`, `ndim(a)` | shape as `int[]`; rank |
| `reshape(a, shape)` | same elements, new shape; total count must match; a view when the source is C-contiguous |
| `transpose(a)` | reversed axes, as a strided view; a permutation argument is designed (Appendix A.7) |
| `flatten(a)` | 1-D copy in row-major order |
| `ravel(a)` | 1-D view when contiguous, else a copy |
| `concat(a, b)` | join along axis 0 |
| `stack(a, b)` | join along a new leading axis |
| `split(a, n[, axis])` | `n` equal parts along an axis, which must divide evenly; dispatches on the first argument, so string `split` is unaffected |
| `matmul(a, b)`, `math.dot(a, b)` | matrix and vector products, result kind by §7.2 |

Variadic `concat`/`stack` and an explicit `axis` argument are deferred; until then the nested-literal promotion `[a, b, c]` serves as an n-ary stack. `expand_dims` and `squeeze` are deferred view operations.

### 6.4 Broadcasting

Every element-wise operation broadcasts with NumPy's rules: shapes align from the right, a size-1 axis stretches, and missing leading axes count as 1. A shape mismatch is an error, never a silent truncation. When both operands are 1-D, contiguous and of equal length, the operation takes the plain 1-D loop, so N-D support adds nothing to that case.

### 6.5 Axis reductions

`sum`, `avg`, `min`, `max`, `math.mean` and `math.prod` take an optional axis (`sum(m, 1)`, `min(m, axis: 0)`) and return an array with that axis removed. `math.cumsum` and `math.cumprod` run along the axis and keep the shape. Negative axes count from the end, and non-contiguous (transposed) sources work. The one-argument forms reduce the whole array. `min(a, b)` on two scalars keeps its pairwise meaning: an array first argument selects the axis form.

### 6.6 Output

Formatters see an N-D array as nested arrays, through the same reader interface as any other array (Appendix B). Serialization stays clean for round-tripping. A NumPy-style debug repr annotating kind, shape and view-ness (`<f32 shape=(2,3) …>`) is deferred: it is developer ergonomics only, and `shape()`/`ndim()` already expose the facts.

---

## 7. Arithmetic, comparison and math

### 7.1 Vectorized operators

`+ - * /` (and `div`, `%`, `**`) are element-wise with broadcasting; vector arithmetic is mathematics (S10.2.1). Operators keep the operand kind: a scalar operand leaves the kind alone, and an array or range operand gives an array (S2.5.7v4). `++` concatenates; `+` never appends.

### 7.2 Result element kind

Array arithmetic promotes exactly as scalar arithmetic does (S10.2.4, ruled by the user 2026-10-21): the type of each result element is the scalar result type of the same operation on that pair of elements, and arrays add no promotion rules of their own (S4.4, S4.5). A packed result follows when the result elements share one type; otherwise the result is generic, like any construction (S9.3.2): `[1, 2.5] + 1` is `[2, 3.5]` holding an `int` and a `float`.

| Operands | `+ - *`, bitwise, shifts | `/` | `div`, `%` |
|---|---|---|---|
| same sized lane | same lane; overflow wraps (`[255u8, 1u8] + [1u8, 2u8]` is `[0u8, 3u8]`) | leaves the lane: `i8`…`u32` → `float`; `i64`/`u64` → `decimal` | enters the non-sized domain: `i8`…`u32` → `int`; `i64`/`u64` → `integer` |
| different sized lanes | smallest containing lane: `i8 + u8 → i16`, `i32 + u32 → i64`, `i64 + u64 → u64`; it then wraps | as above | as above |
| sized with non-sized | leaves the machine domain: `i8`…`u32` → `int`; `i64`/`u64` → `integer` | domain rule | domain rule |
| bounded integer with `float` | `float` (S4.4.3) | `float` | `float` |

Consequences worth stating: `i16[] * i16[]` stays `i16[]` and wraps rather than widening to avoid overflow — at run time; a constant expression built only from sized literals that leaves its lane (`200u8 + 100u8`, `-1u8`) is a compile error, as `256u8` is (S4.1.5); `int[] / int[]` is `float[]`; `u8[] / u8[]` is `float[]`. A computed zero divisor never raises: `/` follows IEEE, and `div`/`%` yield the domain's poison (S4.5.1–S4.5.3). When the result type has no lane (`decimal`, `integer`), the result is a boxed array — representation follows the contract.

### 7.3 Equality

`==` on arrays is deep, element-wise numeric equality, exact across lanes (`[1u8] == [1]`), with N-D shape compared structurally. It is never layout-based (S5.3.1, D2.6.2).

### 7.4 Element-wise comparison and masks

The bare comparisons `< <= > >=` are scalar-only. Element-wise comparison has its own keyword operators, **`eq ne lt le gt ge`**, which broadcast and yield `bool[]` masks; `nan` lanes compare false (S10.2.2v2). The reason: a mask is a container, containers are truthy, and `if (a > 0)` must not silently mean "always".

```lambda
let a = [1, -2, 3, -4, 5]
a[a gt 0]          // [1, 3, 5] — the filter idiom
sum(a lt 0)        // 2 — sum of a mask counts true lanes
all(a ne 0)        // true
```

Masks are consumed explicitly: `sum(mask)` counts, `a[mask]` selects, and `any`/`all` reduce. `and`/`or` stay scalar short-circuit operators and never combine masks (S10.2.3).

### 7.5 Vectorized functions

Unary numeric system functions accept arrays and ranges and return arrays (S7.10.5v3). An array or list in gives an array out; one lane never collapses to a scalar, zero lanes give a typed `[]`, `null` in gives `null` out, and a lane exception produces a lane value (`nan`, `inf`), never a change of shape.

- **Float-valued** (`sqrt`, `cbrt`, `log`, `log2`, `log10`, `log1p`, `exp`, `exp2`, `expm1`, trigonometric and hyperbolic functions) return `float[]`.
- **Kind-preserving** (`abs`, `sign`, `floor`, `ceil`, `round`, `trunc`, and unary `-`) keep the input lane.
- **`clip(a, lo, hi)`** clamps element-wise; `lo` and `hi` broadcast as scalars or arrays.
- **Binary** float functions (`pow`, `atan2`, `hypot`, `fmod`) broadcast like operators.

### 7.6 Reductions

`sum`, `avg`, `min`, `max`, `math.prod`, `math.mean`, `math.variance`, `math.deviation`, `math.norm`, `argmin`, `argmax`, `all` and `any` reduce to a scalar; `math.cumsum`/`math.cumprod` return arrays. `argmin([])` and `argmax([])` are `null`. Reductions widen rather than wrap (S10.2.4, ruled by the user 2026-10-21, after `avg([200u8, 100u8])` was seen to return `22.0`). `sum`, `math.prod`, `math.cumsum` and `math.cumprod` accumulate a narrow sized integer lane (`i8`…`u32`) in `int`, and an `i64`/`u64` lane in itself, whole-array or along an axis: `sum([200u8, 100u8])` is `300` and `math.cumsum` of it is `[200, 300]`. `avg`, `math.mean`, `math.variance` and `math.deviation` accumulate every bounded number in `float`, so `avg([200u8, 100u8])` is `150.0` and an `i64` mean is a `float`, not a `decimal`. `min` and `max` keep the lane. `sum([1, 2.5])` is `3.5` (`int + float`), and float lanes keep their own type (`sum` of `f32[]` is `f32`).

Why widening, when element-wise operators wrap: this is the line NumPy, Julia and R all draw. An element-wise operation on a `u8` image is the machine operation the user asked for, and its result is still one byte per pixel; a reduction produces a new number whose magnitude grows with the array length, and no reader of `sum(pixels)` means a byte. Go is no counter-example: it has no `sum` at all, and its idiom declares a wider accumulator (`var s int`). R goes furthest, giving its byte type `raw` no arithmetic at all. `math.dot` and `matmul` follow NumPy instead (user ruling 2026-10-21): every product and sum is element arithmetic in the operands' common lane, so `math.dot([200u8, 1u8], [2u8, 1u8])` wraps to `145u8`, `u8 @ u8` is a wrapped `u8` matrix, `int @ int` is `int`, `f32 @ f32` stays `f32`, and `int @ float` is `float`. NumPy, Julia and PyTorch all compute products in the element type; only R converts to `double`. To multiply without wrapping, widen an operand first (`matmul(a |> int(~), b)`).

Float reductions may reassociate (pairwise or SIMD order), so results can differ from a left fold in the last ULP, as NumPy's do.

---

## 8. Views and mutation

### 8.1 What a view is

A view is an `ArrayNum` whose elements alias another array's storage. It has the base's element kind and a shape side-table carrying its offset, shape, strides and a reference to its base, which keeps the base alive. No element is copied. Views come from `subview(a, start, end)`, `reshape` of a contiguous source, `transpose`, `ravel` of a contiguous source, a partial N-D subscript (`m[i]`), and possibly a range slice.

A view is a storage fact, like the element kind: it is never observable as a distinct type.

### 8.2 Read views are snapshots

A read view is a first-class value with snapshot semantics: it behaves as a copy taken at creation, so the zero-copy slice is an optimization nobody can observe (S9.2.2). Binding a view and then writing through that binding detaches the binding into an owned array; the base never changes. Writing the base after a view was taken does not show through the view.

### 8.3 Writing in place

Procedural code needs in-place updates: a 4K RGBA frame is about 33 MB, and copying it per operation is ruinous. Lambda allows in-place writes in three forms, all procedural statements:

| Form | Meaning |
|---|---|
| `a[i] = v`, `m[i, j] = v` | element write on an owner held as `var` |
| `a[mask] = v`, `a[mask] = values` | masked write: a scalar fill, or one value per true lane in row-major order |
| `img[r0 to r1, c0 to c1] = patch` | region write: a scalar fill or a shape-matched block (designed, not yet built) |

Every form admits its right-hand side under the destination contract and preflights all positions before writing anything; a later failure leaves the owner unchanged (S11.4.1v3). The right-hand side is a value — a range or view on the right is a snapshot — so overlapping region copies (`a[1 to 3] = a[2 to 4]`) behave as if the source were copied first, which is `memmove` semantics.

**Write-through views are borrows, never values** (S9.2.2). A view that writes through to its base is legal only as an argument in `var`-parameter position. The borrow is exclusivity-checked (overlapping mutable borrows are rejected before the call; whole-base rejection is acceptable until a disjoint-range proof exists), it cannot escape, be returned, be captured or be stored, and creating it over shared storage un-shares first. A view-state `var` may not itself be passed as a `var` argument (S9.2.4v2).

```lambda
pn write_row(var row) {
    row[0] = 99          // writes m[1][0]
}

pn main() {
    var m = [[1, 2, 3], [4, 5, 6]]
    write_row(m[1])      // the row is borrowed; no copy
}
```

**The purity boundary holds by construction.** `fn` code has no assignment statement, so no write path is reachable from functional code. Mutability belongs to the context (a `var` owner, a `var`-parameter borrow), never to the array value.

### 8.4 Lifetime

The view's shape side-table holds its base as a garbage-collector edge, so a live view keeps its base alive. A view over GC-owned data does not pin the base. When compaction moves the base's data, the collector recomputes the view's data pointer from the base and the element offset. Views over non-moving byte storage resolve or retain that storage instead (§9).

A view over an arena-backed array (parser output, `is_heap == 0`) is rejected with an explicit error. The input arena dies independently of the collector, and silently turning `view` into `copy` would hide the cost and break the "views share storage" model.

---

## 9. Storage backings and LambdaJS

### 9.1 Backings

An owned `ArrayNum` keeps its elements in the collector's data zone, which compaction moves. This is the measured fast path, and it stays the default. A view's side-table records which kind of storage it aliases; ownership is never inferred from the base pointer.

| Backing | Bytes live in | Lifetime | Used by |
|---|---|---|---|
| GC owned | the GC data zone (moving) | the array | every owned array |
| GC view | another array's GC data | base edge; the data pointer is rebased after compaction | Lambda views |
| External borrowed | a stable, non-moving raw pointer | owned by `base` | reserved for FFI |
| Buffer handle | a replaceable `ByteBufferHandle` | handle generation; reads re-resolve when it changes | JS typed arrays |
| Byte storage | a refcounted `ByteStorage` | the view holds a reference | FFI, mmap, cross-isolate (tests only so far) |

`ByteStorage` and `ByteBufferHandle` are the shared byte substrate designed in [Binary type §11](Lambda_Type_Binary.md): a refcounted allocation, and a stable identity over it whose generation advances on copy-on-write, resize, detach and transfer.

### 9.2 Why JS bytes do not live in the GC data zone

JS hands raw element pointers to JIT code and bulk `memcpy`/`memmove` paths, and those consumers are not updated by the collector. Bytes in the moving data zone would dangle after the first compaction. So the substrate is the non-moving byte buffer, and `ArrayNum` is a typed window over it — the roles reversed from the naive "JS typed array wraps an `ArrayNum` that owns its bytes". Pinning data-zone buffers instead was rejected: every JS-backed array would fragment the zone permanently.

### 9.3 The JS typed array

A JS `ArrayBuffer` is a `ByteBufferHandle`. Each JS typed array is a separate `ArrayNum` view, with its own element kind and byte offset, over that handle; one buffer seen as a `Float64Array`, a `Uint8Array` and a `DataView` is N views sharing one base. Element kinds map one to one:

| JS | `ArrayNum` | Note |
|---|---|---|
| `Int8Array` … `Uint32Array` | `ELEM_INT8` … `ELEM_UINT32` | exact |
| `Float32Array`, `Float64Array` | `ELEM_FLOAT32`, `ELEM_FLOAT64` | exact |
| `BigInt64Array`, `BigUint64Array` | `ELEM_INT64`, `ELEM_UINT64` | storage only; see below |
| `Uint8ClampedArray` | `ELEM_UINT8_CLAMPED` | clamp on store |

What `ArrayNum` provides: element typing and sizing, raw indexed load and store, and the byte mechanics of `set`, `copyWithin`, `fill`, `slice`, `subarray` and `reverse`. Same-type copies are raw byte copies, which preserve NaN payloads and signed zeros. What stays in the JS layer is every spec-observable behavior: write coercion (wrap, clamp, `ToNumber`, `ToBigInt`, BigInt rejecting Number), detach, transfer, resizable and length-tracking buffers, out-of-bounds rules (typed arrays read `undefined` and ignore writes; `DataView` throws), `DataView` endianness and heterogeneous access, Atomics and `SharedArrayBuffer`, Node `Buffer`, the exotic property gate, and `.buffer` identity. The JS wrapper owns mutable buffer state and refreshes the cheap view descriptor per operation; `ArrayNum` owns element typing and the byte poke. `ArrayNumShape` gains no resize concept; the buffer stays the source of truth.

**BigInt lanes are lossless.** A `BigInt64Array` element is, by ECMAScript definition, exactly a 64-bit lane; a wider BigInt is wrapped on store by `BigInt.asIntN(64, v)`. The value layer is different: a JS BigInt in LambdaJS is a Lambda `decimal`. The conversion stays at the JS boundary, and loading a `BigUint64Array` lane must be unsigned-aware — a lane above `INT64_MAX` reconstructs the full unsigned value, not a negative one.

### 9.4 Lambda ↔ JS interop

Sharing the substrate makes interop expressible, with different costs in each direction:

- **JS → Lambda** is naturally zero-copy: a JS typed array already is an `ArrayNum` view over non-moving bytes. Lambda receives it read-only, respecting JS ownership, and Lambda's arithmetic, math, broadcasting and reshape run on it directly.
- **Lambda → JS** copies by default into a fresh non-moving buffer: O(n), safe and simple. A Lambda array's bytes live in the moving zone, so a zero-copy route needs storage that never moves (§13, Q3).

The asymmetry is part of the design and is documented, not hidden. The bridge entry points themselves are not built yet (Appendix A.7).

### 9.5 Other external buffers

The byte-storage backing — a refcounted allocation with a release callback — is the intended form for FFI buffers, memory-mapped files and Arrow columns. It exists and is tested, but no producer uses it yet. The DataFrame proposal is the expected first consumer.

---

## 10. Image processing

### 10.1 An image is an array

There is no image type. A grayscale image is a 2-D `ArrayNum` `(H, W)`; a colour image is 3-D `(H, W, C)` with the channel as the last axis (HWC, as in scikit-image, Pillow and OpenCV) and `C` 3 or 4. Rows are contiguous, which is the axis SIMD likes. `u8` is the storage kind (values 0–255); `float` or `f32` is the processing kind (0–1). `as_float` and `as_ubyte` convert, scaling and clipping. Functions take and return arrays, and the shape and kind convention is validated at the boundary.

### 10.2 What is free

Much of image processing is ordinary array code:

| Operation | Expressed as |
|---|---|
| threshold | `img gt t` → mask |
| brightness, contrast, gamma, invert | `img * a + b`, `img ** g`, `255 - img` |
| clip | `clip(img, lo, hi)` |
| crop | `img[r0 to r1]`, multi-axis subscripts |
| channel statistics, naive gray | axis reductions over axis 2 |
| transpose, rot90 | `transpose` |
| masked selection and update | `img[mask]`, `img[mask] = 0` |

### 10.3 The stencil engine

Almost every other operation is a windowed neighbourhood computation: at each output position, gather a `Kh × Kw` window and reduce it. One N-D stencil primitive covers the family, with a reducer (dot product, min, max, median or mean), a border mode (constant, edge, reflect or wrap) and an output stride:

| Operation | Stencil |
|---|---|
| `convolve`, `blur` (box, Gaussian), sharpen, edge kernels | dot with the kernel |
| `median_filter` | median |
| `erode`, `dilate`, and open/close/top-hat as compositions | min / max over the structuring element |
| `maxpool`, `avgpool` | max / mean with stride > 1 |
| adaptive threshold | mean, then compare |

This one primitive carries convolution, morphology, rank filters and pooling, and its contiguous inner loop is exactly what the vector kernels accelerate (§11).

### 10.4 The rest of the toolkit

`histogram` (a scatter-add, the basis of `otsu`, equalization and channel statistics); `otsu`; `label` (connected components by union-find, the basis of region measurement); `resize` (nearest and bilinear); `flip`, `rot90`, `rotate`, `affine_warp` (coordinate gathers); `grayscale` and other colour-space transforms (channel-axis `matmul` or per-pixel maps); `crop`, `invert`, `gamma`, `threshold`. `load` and `save` bridge to Radiant's raster codecs, yielding and accepting `(H, W, C)` `u8` arrays.

The functions are global system functions over `ArrayNum`. The `lmd/package/image.ls` package is a separate, layered raster-document package and not part of this toolkit.

### 10.5 Non-goals

Not a computer-vision suite (no SIFT, ORB, optical flow or neural networks — those belong in bindings). Eager and in-memory, not a libvips-style streaming tile pipeline; gigapixel and bounded-memory streaming are future work. No ICC colour management, EXIF or animation. Canny, corner detection, watershed and registration are later additions on the same primitives.

---

## 11. Performance model

- **Static element kind, single load.** When the compiler knows an array's carrier from a certificate, an element read is one native load at the right width plus packing, with no call and no boxing. The certificate guard is the only per-boundary check (D3.3.3v3).
- **Kernels the compiler can vectorize.** Contiguous same-kind kernels are written so auto-vectorization succeeds: the operator switch is hoisted out of the loop (one straight-line loop per operator and element type, generated from one template), pointers are `__restrict`, loops carry vectorize pragmas, and non-vectorizable operators (`%`, `pow`, `fmod`) run in separate loops so they cannot poison the hot ones. `u8` and `float` are the priority types: they are the common image kinds and the widest SIMD lanes.
- **One general fallback.** Mixed kinds, broadcasting and non-contiguous views go through a strided walker that is correct for every case. Fast kernels are gated on "contiguous, same kind, vectorizable operator".
- **Built from source.** Release builds use `-O3` and the host architecture, so auto-vectorization targets the running machine's best instruction set with no runtime dispatch. Verification uses the compiler's vectorization reports; a loop that quietly falls back to scalar is a regression. Performance is measured on release builds only.
- **Deferred.** A portable SIMD library (Highway, xsimd) with runtime ISA dispatch, adopted only with measurements and mainly if prebuilt binaries ship. Native `f16` SIMD (hardware support is uneven; `f16` widens to `f32`). JIT-emitted SIMD (MIR is a lightweight IR; SIMD belongs in the ahead-of-time kernels).
- **Allocation.** `m[i, j]` allocates nothing; chained `m[i][j]` allocates intermediate views. Static N-D literals allocate once; dynamic promotion copies once (§4.2).

---

## 12. Non-goals and deferred work

Non-goals: lazy or fused evaluation graphs (numexpr, JAX); GPU and accelerator interop; sparse arrays; a general mutable-array model or mutable views as values; dependent-shape types and rank-polymorphic signatures; N-D typed arrays in JS; streaming image pipelines.

Deferred, designed but not built: region assignment (§8.3); `transpose` with a permutation; `expand_dims` and `squeeze`; variadic `concat`/`stack` with an `axis` argument; strided slicing as `slice(a, range, step)` (a view whose stride is greater than one — the view machinery already supports it, and no grammar change is needed); views over generic `Array`s, reusing the same side-table (its `base` is typed as a generic container for this reason); lazy N-D promotion; a debug repr with shape; complex element kinds (they compose with this design but need their own proposal); a first-class memory-mapped file API (it falls out of byte-storage views); `DataView` subsumed into an `ArrayNum` byte reader; a Lambda primitive for `SharedArrayBuffer`/Atomics.

---

## 13. Open questions

1. ~~`dot` and `matmul` over sized lanes.~~ Resolved 2026-10-21: they follow NumPy and compute in the operands' common lane (S10.2.4, §7.6).
2. **Construction and conversion functions.** Are `zeros`/`ones`/`arange`/`linspace` and array-form sized conversions (`u8(xs)`) wanted, and what do the conversions do with inexact elements? Value-aware admission (S11.4.5) rejects; a conversion function could truncate, wrap or clamp. The original proposal itself was inconsistent (truncate-and-clamp in one place, truncate-and-wrap in another). Related: whether `u8(binary)` should be a zero-copy byte-storage view and `binary(xs)` a raw lane-byte reinterpretation.
3. **Zero-copy Lambda → JS.** The original opt-in was to pin the Lambda array's data; pinning is retired. The remaining route is a Lambda array whose bytes live in `ByteStorage`. Whether that is worth a second owned representation needs evidence.
4. **`f32` input to float-valued functions.** They return `float[]` for predictability; staying in `f32` would halve memory for image pipelines. Unruled.
5. **`abs` and unary `-` at the lane minimum** (`abs(-128i8)`), and unary `-` on unsigned lanes. Unruled for arrays; the scalar rule should decide.
6. **Stencil API surface.** Whether border modes and named kernels (`sobel`, `gaussian`) are exposed to scripts, and in what form.
7. **Tensor functions on nested arrays.** A rectangular nested array whose rows have different types (`[[1, 2, 3], [4.5, 5.5, 6.5]]`) stays nested under S9.3.2, so today `ndim` returns `0`, `shape` errors and `sum` errors, where the widened tensor used to answer `2`, `[2, 3]` and `22.5`. Under D2.6.2 these functions arguably should answer by value — treating any rectangular nested numeric array as N-D, with arithmetic per S10.2.4 — rather than by storage. Unruled.

---

## Appendix A — Implementation notes and status

Detailed design lives in [LR_05](../doc/dev/lambda/LR_05_Strings_and_Vectors.md), [LR_03 §4](../doc/dev/lambda/LR_03_Value_and_Type_Model.md) and [LR_08](../doc/dev/lambda/LR_08_Memory_and_GC.md). These notes cover only what the design above depends on.

### A.1 Layout

`ArrayNum` shares the 64-byte `List` layout (D2.6.6v2): the 8-byte container header, the map face, then `items`/`float_items`/`data` (one union over the element buffer), `length`, `extra`, `capacity` and `rep_cert`. In the header, byte 1 holds lifecycle flags (including `is_heap`), byte 2 (`array_flags`) holds bit 0 `is_ndim`, bit 1 `is_view`, bit 2 the retired `is_pinned`, bit 3 `is_mutable_view` and bit 4 `is_native_lane_array` — bits 5–7 belong to LambdaJS's elements kind and are not free — and byte 3, the map-kind byte, holds the element kind. Element kinds are high-nibble tags; `ELEM_TYPE_SIZE[kind >> 4]` gives the width. `0xC0` is the retired duplicate double tag and stays unavailable.

### A.2 Shape side-table

`ArrayNumShape`, pointed to by `extra` when `is_ndim` or `is_view` is set: `ndim`, contiguity bits, `backing_kind`, element `offset`, `base` (the GC edge), `backing` (the non-GC `ByteStorage`/`ByteBufferHandle`), `resolved_generation`, then `shape[ndim]` and `strides[ndim]`. Derived views inherit the source's backing; a byte-storage child takes its own reference.

### A.3 GC

Marking traces `shape->base` for views; `backing` is not a GC edge. Compaction promotes an N-D side-table out of the nursery and, for a GC view, promotes the base's data and recomputes `view->data = base->data + offset · width`. Nothing sets or reads `is_pinned`. Byte-storage views release their reference in the sweep finalizer. The collector's own byte offsets are pinned by static asserts in both `lambda.hpp` and the C mirror in `lambda.h`.

### A.4 Construction choke points

Every array builder finishes in `array_end`, which tries N-D promotion of rows that share one element type and length, and then 1-D packing of scalars that share one exact type (`uniform_scalar_elem_type`); nothing is widened (S9.3.2). Spreadable (list) results are never packed. Admission goes through `runtime_type_admit_array` (`type_contract.cpp`), which roots every intermediate, admits transactionally and installs the interned certificate. Exact element stores go through `array_num_store_admitted`, which has no catch-all arm and never coerces.

### A.5 Kernels and views

Element-wise arithmetic classifies each operation with the scalar numeric classifier (`lambda_numeric_classify`), so arrays cannot drift from scalar semantics. The vectorizable kernels are templates over element type and operator with `__restrict` and an `LMD_VEC_LOOP` pragma. The view constructors (`subview`, `reshape`, `transpose`, `ravel`, the N-D row view) share `array_num_init_derived_view`. The image functions and the stencil engine are in `lambda-vector.cpp`.

### A.6 Source map

| Area | Files |
|---|---|
| Structs, element kinds, shape, backings | `lambda/lambda.h`, `lambda/lambda.hpp` |
| Construction, `array_end`, views, N-D subscripts | `lambda/runtime/lambda-data-runtime.cpp` |
| Kernels, N-D operations, masks, stencil, image toolkit | `lambda/runtime/lambda-vector.cpp` |
| Contracts, admission, certificates | `lambda/runtime/type_contract.{hpp,cpp}` |
| Numeric classifier | `lambda/runtime/lambda-number.hpp` |
| GC tracing and compaction | `lambda/runtime/gc/gc_heap.c`, `lambda/runtime/lambda-mem.cpp` |
| Byte substrate | `lib/byte_storage.{h,c}` |
| JS typed arrays | `lambda/js/js_typed_array.{h,cpp}` |
| Readers and formatters | `lambda/core/mark_reader.{hpp,cpp}` |
| System-function registry | `lambda/runtime/sys_func_registry.c` |
| Tests | `test/lambda/typed_array_*.ls`, `test/lambda/compact_typed_arrays.ls`, `test/lambda/proc/*typed*`, `proc_view_mutable.ls`, `proc_mask_assign.ls`, `proc_ndim_index_write.ls`, `array_view_*.ls`, `test/js/typed_arrays.js` |

### A.7 Status (2026-10-21)

| Area | Status |
|---|---|
| Unified `ArrayNum`, 14 element kinds, compact lanes, `float`/`f64` merge | built |
| `T[]` contracts, value-aware admission, certificates, typed empty carriers (§3) | built 2026-09-09/10 ([impl plan §12–§14](impl/Lambda_Impl_Typed_Array.md)) |
| Exact-lane literal and pipe packing (§4.1, §4.3) | built |
| Exact construction (§4.1–§4.3, S9.3.2) | built 2026-10-21: 1-D packing, generic-row detection and N-D row promotion require one exact element type; mixed `int`/`float`/`i64` rows stay nested (`typed_array_construction_exact.ls`) |
| Scalar type on read (S9.3.2) | fixed 2026-10-21: reading an in-band `i64`/`u64` from a generic array, list, closure capture or frame returned an `int` (a JS-motivated shortcut in `scalar_storage_read`, added 2026-08-13); Lambda reads now keep the stored type, and the JS call sites use `js_scalar_storage_read` |
| `fill` typing (§4.4) | `int`, `u64`, `float`, `bool` only; a `u8` fill value gives a boxed array |
| `zeros`/`ones`/`arange`/`linspace`, array-form `u8(xs)`, `u8(binary)` views, `.bytes`/`.elem_type` | not built (§13, Q2) |
| `binary(xs)` | copies element values in 0–255; does not reinterpret lane bytes |
| Multi-axis subscripts, partial-index views, `last` (§5) | built (LR07-35 fixed 2026-09-26) |
| Masks and index-array gathers (§5) | built |
| Structural operations (§6.3) | built; **missing:** `transpose` permutation, `expand_dims`, `squeeze`, variadic `concat`/`stack`, `copy()` (which several error messages tell the user to call) |
| Broadcasting, axis reductions (§6.4–§6.5) | built |
| Lattice-driven arithmetic (§7.2) | built, including sized `div`/`%` entering the non-sized domain per S4.5.3 (Number Model §3.3 synced 2026-10-21); reductions widen per S10.2.4 since 2026-10-21: `sum`/`prod`/`cumsum`/`cumprod` (whole and axis) accumulate narrow lanes in `int`, `avg`/`mean` in `float` (`lambda_reduction_element`, `lambda_statistic_element`); `dot` wraps in the common lane as ruled; `matmul` realigned 2026-10-21 from the legacy `i64`/`float` result and `double` accumulation to the same per-cell lane rule, with native kernels for machine lanes (`matmul_store_cell`), so `i64` products are exact (`typed_array_construction_exact.ls`, `typed_array_reduce.ls`) |
| Constant overflow (S4.1.5) | enforced 2026-10-21: a sized constant expression that leaves its lane (`200u8 + 100u8`, `-1u8`, `127i8 + 1i8`) is E108, and two literal holes are closed — a bare `128i8` (read as -128) and decimal `i64` literals above 2^63 (read as raw bits); run-time values still wrap (`negative/semantic/sized_constant_*.ls`, `sized_literal_*.ls`) |
| `ArrayNum ==` (§7.3) | fixed (Number Model ledger row 8) |
| Keyword comparisons `eq … ge`, scalar-only `<` (§7.4) | built (`typed_array_mask.ls`); formal-spec status row synced 2026-10-21 (S10.2.3 complete; S10.2.2v2 keeps its mark for SO50 error shapes) |
| Vectorized unary functions, `clip`, `all`/`any` (§7.5) | built; `all`/`any`/`argmin`/`argmax` take no axis |
| Read views as snapshots (§8.2) | **not enforced.** The view constructors set `is_mutable_view` unconditionally, and `proc_view_mutable.ls` asserts write-through via a `var`-bound `subview`. View-borrow confinement is the open Stage 2 item of S9.2.2 |
| `var`-parameter view borrows (§8.3) | built for leading-axis rows |
| Element and masked writes (§8.3) | built (`proc_ndim_index_write.ls`, `proc_mask_assign.ls`) |
| Region writes (§8.3) | not built; rejected with "range slice-assignment is not supported" |
| Pinning (§8.4) | retired; the collector rebases views (LR_03, LR_05, the d05 diagram, [Binary §11.2](Lambda_Type_Binary.md), [DataFrame](Lambda_Design_DataFrame.md), Concurrency, Runtime_Structs and code comments synced 2026-10-21) |
| Arena-backed views (§8.4) | rejected, as designed (`ravel` copies instead) |
| JS substrate (§9.3) | built: `JsArrayBuffer` is a `ByteBufferHandle`; JS typed arrays are buffer-handle views; the old duplicated storage fields are deleted (test262 typed-array pages green at the 2026-06-30 cut) |
| Lambda ↔ JS bridge (§9.4) | not built; `new Float64Array(lambdaArray)` copies |
| External-borrowed and byte-storage backings (§9.5) | built; tests only |
| Image toolkit and stencil engine (§10) | built as global system functions; no `sobel`/`gaussian`; every wrapper uses the edge border mode |
| Vectorizable kernels (§11) | built: templated `__restrict` kernels with `LMD_VEC_LOOP`; release `-O3` and native architecture |

---

## Appendix B — Mark Reader guidelines

The readers (`ItemReader`, `ArrayReader`, `MapReader`, `ElementReader`) are how formatters and validators see typed arrays. The guidelines were first recorded in Typed Array 2 and should move to a Mark Reader design record when one exists; [LR_11](../doc/dev/lambda/LR_11_Mark_Data_API.md) describes the readers as built.

- **G1 — Four logical shapes only.** Consumers see arrays, maps, items and scalars. A typed `ArrayNum` (1-D, N-D, compact, or a view) answers `isArray()`/`asArray()` like any array; its elements arrive as ordinary scalars or, for N-D, nested arrays. No element kind, shape or stride leaks into consumer code, so one code path serves every representation, and new representations need no consumer changes.
- **G2 — Traversal allocates nothing.** Readers are small stack values; an N-D sub-tensor is an inline `{base, offset, axis}` cursor, not a heap view. Leaf scalars are packed inline or point into the live buffer. The one exception is an explicit request to materialize a sub-tensor as a standalone value, which is documented as leaving the zero-allocation path; formatters never make it.
- **G3 — The tree is static during a read.** A reader may hand out items that point into a live buffer because the tree does not move, mutate or get freed mid-traversal. Concurrent mutation of a tree being read is a usage error.

Conformance: the element-kind dispatch lives in one place in the reader; the per-formatter copies (JSON, YAML, TOML) were deleted; every `isArray()` call site iterates through the reader interface. `print.cpp` keeps its own fast walker for logging.

---

## Appendix C — Prior art

| Source | Taken | Not taken |
|---|---|---|
| JS TypedArray | type-specific element kinds; one buffer, many typed views; `subarray` views vs `slice` copies | mutation everywhere; `byteOffset` arithmetic in user code |
| NumPy `ndarray` | `dtype`; shape and strides; broadcasting; axis reductions; masks and index arrays; views with a base reference; pairwise float sums | mutable aliasing everywhere (the pandas `SettingWithCopyWarning` problem) |
| scikit-image | an image is an array; HWC; `img_as_float`/`img_as_ubyte`; a function library over the array type | — |
| Rust `Vec<T>` | contiguous storage at native width | ownership-based lifetime (Lambda arrays are GC-managed) |
| Go | wrapping fixed-width integer arithmetic | — |
| R `data.table` | opt-in reference semantics confined to a scoped channel | — |

Multi-axis subscript syntax across languages:

| Language | Syntax |
|---|---|
| Python + NumPy, Julia, R, C#, Swift, Kotlin, D | `a[i, j, k]` |
| C++23 (P2128) | `a[i, j, k]`, after decades of `a[i][j][k]` |
| MATLAB, Octave, Fortran | `a(i, j, k)` |
| Mathematica | `a[[i, j, k]]` |
| Rust `ndarray` | `a[(i, j, k)]` |

---

## Appendix S — Superseded rulings

Wording from the four earlier records that the body above no longer holds, with what replaced it.

**Representation and layout**

- ~~`ELEM_FLOAT` (`0x10`) and `ELEM_FLOAT64` (`0xC0`) are distinct types with the same storage.~~ → One double tag, `ELEM_FLOAT64` at `0x10`; `ELEM_FLOAT` is a source alias; `0xC0` is retired (Number Model §3.5, W8).
- ~~`elem_type` lives in the upper nibble of `Container.flags`.~~ then ~~the view, N-D and pin bits live in the upper nibble of `flags`.~~ → The element kind is the map-kind byte (byte 3); the layout bits are the separate `array_flags` byte (byte 2) (Appendix A.1).
- ~~`ELEM_INT` stores int56-as-int64.~~ → The i64 lane with poison encodings (D2.6.3).
- ~~Lambda typed arrays are one-dimensional only; multi-dimensional data uses nested arrays.~~ → N-D `ArrayNum` (§6).

**Types and admission**

- ~~`type(arr)` returns `u8[]`.~~ → `type()` is `array` for every physical form (D2.6.2).
- ~~An annotation coerces elements to the annotated type (`let x: i16[] = [1, 2]`); mixed sizes fall back to a generic array.~~ → Admission is a contract with value-aware numeric embedding; inexact values are rejected, never coerced (S11.4.1v3, S11.4.5).
- ~~`ensure_typed_array` casts an `ArrayNum` of another kind to the annotated kind.~~ → Certificate-backed admission; lossy casts are not admission (D3.3.3v3).

**Indexing**

- ~~Negative N-D indices wrap per axis (`< 0 ? len + idx : idx`), matching 1-D `arr[i]`.~~ → Negative indices read `null` on every axis; `last` reaches from the end (S7.2.1, S7.2.2).

**Arithmetic**

- ~~Accumulating reductions are the repeated scalar operation: `sum([200u8, 100u8])` is `44u8` (user ruling earlier on 2026-10-21).~~ → Reductions widen: narrow lanes accumulate in `int`, statistical reductions in `float` (S10.2.4 as revised the same day, after `avg([200u8, 100u8])` returned `22.0`).
- ~~Wrap for `+ - %` but promote for `*` to prevent silent loss (Typed Array 1, Q1).~~ → `+ - *` all stay in the selected sized lane and wrap (S4.4.4).
- ~~Mixed-type compact arithmetic (`i8[] + i16[]`) falls back to the float path and produces `float[]`.~~ → The smallest containing sized lane (`i16[]`) (S4.4.4).
- ~~Result element type is the wider of the two operands, with int → float when mixed.~~ → The scalar lattice per lane (§7.2).
- ~~`dot`/`matmul`: `int · int → int64`, otherwise float.~~ → The scalar classifier per multiply and add (§7.2).
- ~~Full-width integer with float → decimal domain.~~ → Every bounded integer type, `i64`/`u64` included, meets `float` in `float`; only `integer` meets `float` in `decimal` (S4.4.3).
- ~~`a / b` on `i16[]` produces `f32[]`.~~ → `float[]`: compact lanes enter `int`, then `/` gives `float` (S4.5.2).

**Comparison**

- ~~Vectorized ordering comparisons `a > 0` produce masks; `a[a > 0]` is the filter idiom; a bare `a > 0` statement needs parentheses.~~ → `< <= > >=` are scalar-only; masks come from `eq ne lt le gt ge`; the idiom is `a[a gt 0]` (S10.2.2v2).

**Views and mutation**

- ~~Views are read-only, period; mutation through a view raises.~~ and ~~success criterion: writes to the source are visible through the view.~~ → Read views are snapshot values; write-through is a `var`-parameter borrow (S9.2.2).
- ~~A view is created mutable iff it is produced inside a procedural function over a mutable base; `let v = img[2]; v[0] = 9` mutates row 2 of `img`.~~ → Binding a view gives a snapshot; only a `var`-parameter borrow writes through, exclusivity-checked and non-escaping (S9.2.2).
- ~~Pin the base with `is_pinned` when a view is first created; never clear it; the compactor skips view and pinned data.~~ → Pinning retired; compaction rebases GC views from their base (§8.4).
- ~~Lambda → JS zero-copy by opt-in pin-on-cross.~~ → Pinning is retired; the zero-copy route is open (§13, Q3).
- ~~JS typed arrays carry `data`, `length`, `byte_length` and `byte_offset`, and standalone typed arrays own their bytes.~~ → Every JS typed array is a buffer-handle view; `JsArrayBuffer` is a `ByteBufferHandle` (§9.3).

**Construction**

- ~~1-D packing of a numeric result widens to `float` if any element is a float, else to `i64`.~~ then ~~packing only into a lane that holds every element exactly; `[1, 2.5]` packs into `float` lanes (`int ⊑ float`); an unannotated literal takes the normalized least upper bound of its elements.~~ → Packing only when every element already has one exact type; no promotion at construction (S9.3.2, user ruling 2026-10-21).
- ~~Mixed-kind rows of an N-D literal widen to `float` if any row is float, else to `i64`.~~ → Rows of different types are never widened; they stay nested (S9.3.2, §4.2).
- ~~Image functions live in an `image.` namespace (`image.blur(sigma: 2.0)`).~~ → Global system functions over `ArrayNum` (§10.4).
