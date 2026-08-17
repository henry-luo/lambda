# Compiling Nullable Values in Native Lanes

- **Status:** PARTIALLY IMPLEMENTED — core Array/Shape storage and the first nullable MIR
  scalar ABI slice landed 2026-08-05.
  `int?`, `bool?`, `float?`, widened `i8?`…`u32?`, pointer-backed optionals, and the typed-Item
  `i64?`/`u64?` fallback retain an explicit native-lane descriptor in eligible Lambda arrays and
  map shapes. `int?`, `float?`, `bool?`, `string?`, `symbol?`, `binary?`, `decimal?`,
  `datetime?`, `complex?`, and the supported nullable container pointers (`array?`, `map?`,
  `element?`, `object?`, `range?`, `path?`, `type?`, `function?`) preserve their lane through a
  direct raw ABI and convert only at the checked public adapter. LambdaJS dynamic values and
  named property ICs retain their boxed `Item` representation unless a future site has an exact
  Lambda `T | null` contract; mutable `ArrayNum` views and vector/N-D kernels remain pending,
  and those two cases are explicitly deferred in §10.
- **Scope:** Lambda MIR Direct, Lambda runtime storage, shapes/maps, and LambdaJS lowering.
- **Depends on:** [`Lambda_Semantics_Int_Type.md`](Lambda_Semantics_Int_Type.md) §5 (the v5
  `IntLane`), [`Lambda_Design_Type_Enforcement.md`](Lambda_Design_Type_Enforcement.md) TE-11
  (plain `T` excludes null), and [`Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md)
  §7.1 (reads are total and absence is `null`) and §9.2 (value-array covariance under COW).
- **Non-goal:** changing the language meaning of `null`, errors, optional types, or JavaScript
  `undefined`. This is a carrier and lowering design.

---

## 1. Decision to make

Every concrete Lambda type `T` has a native carrier (its **native lane**) `N(T)`. The compiler
must keep the optional form `T?` in a native carrier too:

```
T?  = T | null
N(T?) = N(T) | NULL_LANE(T)
```

`NULL_LANE(T)` is normally a lane-local distinguished value. It is **not** an ordinary boxed
`Item` flowing through a register merely because an expression might be absent. At a box/unbox
boundary it converts to/from the ordinary `ItemNull` value. The explicit `i64?`/`u64?` exception
selects `Item` itself as `N(T?)`, because no raw 64-bit null code exists; that is a typed Item
lane, not a request to erase the surrounding native storage contract.

This applies everywhere a compiler or runtime chooses a physical representation:

- locals, temporaries, phi/join results, parameters, returns, and closures;
- boxed Arrays, fixed-word native Arrays, and the null-free `ArrayNum` specialization;
- typed map fields, including fields packed by a `Shape`;
- direct native call ABIs and their checked boxed entry/exit adapters;
- LambdaJS's proven-native internal values and shaped property fast paths.

The rule is deliberately stronger than “nullable locals avoid boxing.” A declared map field
`score: int?` must use the nullable `IntLane` in its shape slot, not an 8-byte raw `Item` slot.
Otherwise `T?` would silently lose its native representation at the point where maps become
data-processing records. Likewise, an `int?[]` must use a native Array word containing an
`IntLane`, not an Array of boxed Items.

### 1.1 The result this is intended to unlock

```lambda
let values: int[] = [10, 20]
let x = values[index]
```

The semantic type of `x` is `int?`, because an index read can be absent. Its compiler carrier is
one nullable integer lane:

```text
in bounds:      x = IntLane(10 or 20)
out of bounds:  x = INT_LANE_NULL
```

It is not:

```text
in bounds:      x = raw int64
out of bounds:  x = boxed ItemNull
```

That old mixed-register convention is invalid: the consumer cannot know whether an `i64` holds a
numeric lane or a tagged `Item`. Nor is the general answer “box both outcomes”; that is correct
but gives up the native array/loop path for every in-bounds iteration.

---

## 2. Terms and invariants

### 2.1 Semantic type, lane, and Item are different things

| Term | Meaning |
|---|---|
| `T` / `T?` | Lambda semantic type. `T?` is exactly `T | null`. |
| native lane | Unboxed ABI/storage representation selected for a proven concrete type. |
| lane null | A reserved lane value or null pointer used only for `T?`. |
| boxed `Item` | The universal tagged representation at dynamic boundaries, and the native-lane fallback for a type with no spare unboxed null representation. `ItemNull` is its null member. |

A lane null may use the same *bit pattern* as `ItemNull` where that is safe, but it is still a
member of a lane-specific closed representation. It must never be accepted as an arbitrary boxed
Item by accident. The `i64?`/`u64?` Item lane is the explicit exception: it admits only
`ItemNull` or a validated Item for that exact full-width integer type.

### 2.2 Required invariants

1. **No semantic loss.** `box_T?(NULL_LANE(T)) == ItemNull`; every non-null lane value boxes to
   the same Item as the corresponding `T` value.
2. **No sentinel laundering.** A nullable lane sentinel must be tested before arithmetic,
   comparison, pointer dereference, map traversal, or typed-store conversion that expects `T`.
3. **Plain `T` stays clean.** A `T` carrier cannot hold a null sentinel. A dynamic null at a
   plain-`T` boundary remains a type error, as TE-11 requires.
4. **The representation follows the full type contract.** A `TypeId` alone is insufficient:
   `int` and `int?` have different lane rules despite sharing the same base `TypeId`.
5. **Array optionality follows the existing covariance rule.** Per the normative formal
   semantics §9.2, since `T <: T?`, ordinary value assignment permits `T[] <: T?[]`; the reverse
   assignment is rejected. The target has its own full `T?[]` descriptor even when COW initially
   shares storage with the source.
6. **Concrete array contracts do not widen.** `T[]` and `T?[]` are distinct semantic types and
   descriptors. A store of null into `T[]` is rejected statically when provable and dynamically
   otherwise; it never changes that array into `T?[]`.
7. **Storage transitions are representation-only.** Writing `null` to an existing `T?` field
   does not change its map shape. A nullable or generic `ArrayNum` may transition to its general
   Array representation when null is admitted, but the observable array contract is unchanged.
8. **GC sees only pointers.** A null pointer lane is not a root; numeric and bool nullable
   sentinels are never roots. Existing `Rooted`/`RootFrame` ownership rules remain unchanged.

---

## 3. Canonical lane table

The following table is the proposed canonical policy. “ItemNull” in the null column means that
boxing the sentinel produces `ItemNull`; it does **not** automatically mean the Item word can be
placed in an unrelated scalar lane.

| Semantic type | `N(T)` | `N(T?)` | null encoding | packed map field | native Array slot |
|---|---|---|---|---|---|
| `int` | `IntLane` / `int64_t`; int53 finite values plus `nan`, `-inf`, `+inf` lane sentinels | same `IntLane` | `INT_LANE_NULL`, a fourth value that boxes as `ItemNull` | 8-byte lane | one 64-bit `IntLane` word |
| `bool` | `uint8_t`: `0=false`, `1=true` | `uint8_t`: `0=false`, `1=true`, `2=null` | `BOOL_LANE_NULL = 2` | one byte | one 64-bit word; its lane value is 0, 1, or 2 |
| `i8`, `u8`, `i16`, `u16`, `i32`, `u32` | their ordinary width | widened `int64_t` lane | `SIZED_LANE_NULL`, outside the source domain and boxing as `ItemNull` | ordinary width for `T`; eight bytes for `T?` | one 64-bit word holding the ordinary or widened lane |
| `i64`, `u64` | all 64-bit values are valid | `Item` fallback lane; no spare in-band value | ordinary `ItemNull` | raw eight-byte value for `T`; Item for `T?` | one raw word for `T`; one typed `Item` word for `T?` |
| `float` / `f64` | canonical IEEE binary64 | same binary64 lane plus one reserved quiet-NaN marker | `FLOAT_LANE_NULL_BITS`, which boxes as `ItemNull` | 8-byte lane | one 64-bit float-lane word |
| pointer scalar or container: `string`, `binary`, `symbol`, `decimal`, `datetime`, `map`, `array`, `element`, `range`, `function`, `type`, etc. | pointer | same pointer lane | C/C++ `NULL` pointer | pointer-width slot | one 64-bit pointer-lane word |
| `null` | no payload lane | invalid | invalid | n/a | n/a |
| `error` | no native nullable lane | invalid | invalid | n/a | n/a |

### 3.1 `int?`

The v5 design reserves three `IntLane` values today:

```text
INT_LANE_NAN
INT_LANE_NEG_INF
INT_LANE_INF
```

This proposal adds exactly one:

```text
INT_LANE_NULL
```

`INT_LANE_NULL` is the numeric `int64_t` value of `ITEM_NULL`. It lies outside the finite int53
band and differs from all three poison sentinels. It is always spelled as the named lane constant,
never open-coded in arithmetic or array code. In an `IntLane` it is a lane sentinel, not a boxed
Item contract; its equality to the Item word is what makes the box/unbox conversion exact.

`SIZED_LANE_NULL` uses the same `int64_t` value. It is safely outside every `i8`…`u32` domain.
Both sentinels must be checked before the existing poison arithmetic helpers run.

The resulting lane domain is:

```text
int:   finite int53 | +inf | -inf | nan
int?:  finite int53 | +inf | -inf | nan | null
```

The common case remains one native i64 register and one 8-byte storage cell.

### 3.2 `bool?`

`bool?` stays one byte. The compiler must not use ordinary C truthiness for it:

```text
0 -> false
1 -> true
2 -> null
```

Consequently `if (b)` and `not b` first test for `2`, follow Lambda's existing null behavior,
then interpret only `0`/`1` as booleans. A direct conversion to C `bool` is valid only after the
null test.

### 3.3 Sized integer optionals

The narrow integer domains have unused values in an i64 carrier, so the optional form widens to
one i64 lane rather than an Item:

```text
i8?   : int64_t, valid i8 range or SIZED_LANE_NULL
u32?  : int64_t, valid u32 range or SIZED_LANE_NULL
```

The lane is widened because reserving an in-width `i8`/`u8` bit pattern would remove a valid
source value. The typed boundary validates range before producing the lane; it does not truncate
or wrap a dynamic source just to obtain the compact form.

`i64?` and `u64?` are the deliberate exception. Their domains occupy every bit pattern, so a
single raw 64-bit lane has no spare null code. Their selected `N(T?)` is therefore the ordinary
64-bit `Item` lane, using `ItemNull` directly. This is still a fixed-width native lane and may
be used by a typed native Array; it introduces no extra nullable sentinel, tagged pair, or
separate array representation.

This is an **accepted limitation of the lane scheme, not an open item** (D2.5.2). The practical
cost is real: a wide optional stays boxed, pays a scalar home per value, and is excluded from the
unboxed wide `+ - *` path, which admits only non-optional operands. Both ways to buy a null code
back are worse. Reserving one — say `INT64_MIN` — deletes a legal member from a type whose whole
point is the full domain, so `i64?` would no longer contain `i64`. A tagged pair adds a second
word to every wide optional, in arrays and packed fields as well as registers, to serve a bit of
information the `Item` lane already carries. Against that, the case is judged rare: wide integers
show up as exact identifiers, counters, hashes, timestamps and bit patterns, and those are
precisely the uses that do not model absence — an absent id is usually a sentinel `0` or a
separate presence flag in the surrounding shape, not an `i64?`. Reopen this only with evidence
that `i64?`/`u64?` are hot in real code, and reopen it as a measurement, not a symmetry argument.

### 3.4 Pointer-backed values

For pointer-backed scalar and container values, the nullable carrier is already available:

```text
string?  -> String*  (NULL means null)
Map?     -> Map*     (NULL means null)
array?   -> Array*   (NULL means null)
```

No tagged Item is needed in locals, parameters, returns, or typed map fields. The compiler must
retain the semantic base type alongside the pointer so a null `String*` is not confused with a
null `Map*`, and so boxing selects the right non-null tag.

### 3.5 Invalid optional forms and normalization

The type normalizer rejects `null?` and `error?`: neither denotes a useful new domain.

It canonicalizes:

```text
T??       => T?
T | null  => T?
```

`any`, abstract numeric types (`number`, `integer`), and non-optional heterogeneous unions do
not have one native lane and remain boxed. `any?` therefore canonicalizes according to the
existing `any`-contains-null relation; it does not create a new native carrier.

---

## 4. `float?`: a reserved IEEE NaN marker

The nullable float remains a one-word native lane. Its null value boxes as `ItemNull`, but the
literal `ITEM_NULL` word cannot be stored in a `double` register: `ITEM_NULL` is
`0x0100000000000000`, which is the valid finite binary64 value `2^-1007`.

Changing the global `ItemNull` encoding to a NaN is also invalid. The Item tag partition requires
`ItemNull` to remain outside raw-double space so the decoder can distinguish a tagged null Item
from a float Item.

The proposed native encoding is therefore:

```text
FLOAT_LANE_NAN_BITS  = 0x7ff8_0000_0000_0000  // ordinary Lambda float nan
FLOAT_LANE_NULL_BITS = 0x7ff8_0000_0000_0001  // float? null only
```

Both are quiet NaNs. The second payload is never a float value: it is the native-lane spelling
of null. The conversion funnel is exact:

```text
ItemNull                 -> FLOAT_LANE_NULL_BITS
float Item containing NaN -> FLOAT_LANE_NAN_BITS
FLOAT_LANE_NULL_BITS     -> ItemNull
FLOAT_LANE_NAN_BITS      -> canonical float NaN Item
finite / +/-inf          -> themselves
```

NaN payload identity is not a Lambda surface value. Lambda already treats NaN as one semantic
poison value (including SameValueZero collection behavior), so the native float entry and result
funnels must canonicalize every non-null NaN to `FLOAT_LANE_NAN_BITS`. A nullable operation first
compares the **bits** with `FLOAT_LANE_NULL_BITS`; it must never use IEEE `==` for this test.
Only after that check may it execute or classify the hardware double operation. If the result is
a NaN, it is canonicalized before it becomes a lane value.

`f16?` and `f32?` follow the same rule: reserve one quiet-NaN payload for lane null and
canonicalize all semantic NaNs to a different payload. The exact bit constants are format-sized,
but the Item boundary still maps their null marker to `ItemNull`.

---

## 5. Type inference and flow facts

### 5.1 Optionality is part of the effective type

The compiler needs an effective representation descriptor, conceptually:

```text
EffectiveValueType {
    semantic_type: Type*       // preserves T, T?, unions, named contracts
    lane_kind: NativeLaneKind  // IntLane, BoolLane, Ptr, Item, ...
    nullable: bool
    storage_width: uint8_t
}
```

`get_effective_type()` returning only a `TypeId` cannot express this. Returning `LMD_TYPE_ANY`
when an otherwise-known expression can be null is the current conservative escape hatch; this
proposal replaces that loss of information with `known int + nullable lane`.

### 5.2 Producers of `T?`

At minimum, inference must produce `T?` for:

| Expression | Inferred semantic result |
|---|---|
| `a[i]`, where `a: T[]` and the index is not statically proven in bounds | `T?` |
| an N-dimensional typed-array read | element type `T?` |
| `map.key` / `map[key]`, when the key can be absent | declared/value type `T?` |
| an optional shaped-map field read | field type `T?` |
| chained access from `T?` | selected member/index type made nullable |
| `if c then value_of_T else null` | `T?` |
| a system function declared to return `T?` | `T?` |

The important correction is the first row:

```lambda
let a: int[] = ...
a[index]             // int?, not int and not any
```

The emitted checked read writes `INT_LANE_NULL` on failure and an ordinary `IntLane` on success.
No per-read Item allocation or generic `fn_index` result is required.

### 5.3 Consumers and null propagation

This proposal preserves the normative behavior that null propagates through scalar arithmetic
and chained access. It only makes that behavior native:

```lambda
let x: int? = a[i]
x + 1                 // int?; null lane short-circuits to null lane
x.name                // member result made nullable; no pointer dereference on null
x or 0                // int; null lane is discharged before evaluating/choosing the fallback
```

For each native operation, lowering first checks its nullable inputs. If any is null, it emits
the result's lane-null value. Only non-null operands reach the existing `int` poison arithmetic,
float arithmetic, pointer dereference, or bool operation.

This makes the fast path explicit:

```text
if (int_lane_is_null(x)) return INT_LANE_NULL
return int_lane_add(x, 1)
```

The result is still one native lane. The null branch is cold for normal loop bodies, while the
in-bounds non-null branch no longer boxes just to remain semantically correct.

### 5.4 Flow-sensitive presence proofs

The static type of a general read remains `T?`. A dominating proof may let code use its payload
as `T` without another null check:

```lambda
let x = a[i]          // int?
if (x is int) {
    x + 1             // native IntLane, proven non-null in this branch
}
```

Likewise, a successful bounds test may justify a direct native element load inside its dominated
region. This is an optimization proof; it must not change the public inferred type of an
unproven `a[i]` expression.

### 5.5 Boundary checking

At a dynamic boundary:

```text
Item -> T? lane:
    if ItemNull: return NULL_LANE(T)
    validate/admit as T, then unbox as N(T)

T? lane -> Item:
    if lane is NULL_LANE(T): return ItemNull
    box as T
```

At a plain `T` boundary, `ItemNull` is rejected before native unboxing. At an annotated `T?`
boundary, it is admitted and converted to the lane null. This retains TE-11 exactly.

---

## 6. Storage and shape policy

### 6.1 Shapes need a lane-storage descriptor

Today shape layout is largely derived from `TypeId` and `type_info[type].byte_size`. That cannot
distinguish `int` from `int?`, nor `bool` from `bool?`, and it cannot express a widened `i8?`.

Each `ShapeEntry` must retain or derive an immutable `LaneStorageDesc` from its full `Type*`:

```text
LaneStorageDesc {
    semantic_contract: Type*
    storage_kind: INT_LANE | BOOL_LANE | SIZED_NULLABLE_I64 | POINTER | ITEM | ...
    byte_size: 1 | 8 | 16 ...
    nullable: bool
    native_array_word_kind: INT_LANE | BOOL_LANE | POINTER | ITEM | ...
}
```

All of these operations must use the descriptor rather than only `TypeId`:

- shape-size calculation and field offsets;
- shape transitions and packed-field rebuilding;
- raw field load/store and `map_field_to_item` conversion;
- copy-on-write cloning and shape repacking;
- map validation, typed map writes, and direct MIR field offsets;
- native Array allocation, element load/store, and ArrayNum-to-native transitions;
- JS shaped-object property fast paths.

### 6.2 Stable nullable shape slots

```lambda
type Row = { id: int, score: int?, enabled: bool?, title: string? }
```

Proposed storage:

| Field | Storage |
|---|---|
| `id` | 8-byte non-null `IntLane` |
| `score` | 8-byte nullable `IntLane` |
| `enabled` | one-byte `bool?` lane |
| `title` | `String*`, where `NULL` is absent |

Assigning `row.score = null` writes `INT_LANE_NULL`. Assigning `row.title = null` writes a C
null pointer. Neither action changes the field's representation, field offset, or map `Shape`.

Changing a field *contract* between `T` and `T?` may require a shape transition. It is a
descriptor transition, not merely a `TypeId` overwrite. In particular, `i8 -> i8?` widens from
one byte to eight bytes, so every following packed offset must be rebuilt; the fixed-slot prefix
must be preserved only when its descriptor and byte offset are unchanged.

### 6.3 Arrays have three physical forms

Array packing follows the same lane policy as map packing, but has a deliberately different
physical granularity. A map puts each field at its descriptor's minimum width; a native Array
uses one fixed 64-bit word per element, irrespective of whether the lane itself is a byte or a
pointer. The descriptor tells the reader how to interpret that word.

| Physical form | Element representation | Use |
|---|---|---|
| boxed Array | every element is a boxed `Item` | dynamic/abstract element contracts without a proven homogeneous lane descriptor |
| native Array | every element is one 64-bit word containing the unpacked `N(T)` or `N(T?)` lane; that lane is an `Item` only for the `i64?`/`u64?` fallback | proven concrete `T[]` and `T?[]` with a homogeneous lane descriptor |
| `ArrayNum` | its existing specialized numeric layout | fast, null-free numeric arrays, including a `T?[]` or generic array while its stored values remain non-null |

For example, `int?[]` is a native Array whose middle word may be `INT_LANE_NULL`, not an
`ItemNull` element:

```lambda
let xs: int?[] = [1, null, 3]
```

`bool?[]` likewise occupies three native lane values (0, 1, 2), even though each Array element
still reserves a full 64-bit word. Pointer-like `T?[]` values use a zero pointer word for null.
The widened 64-bit lane is used for nullable `i8` through `u32`. `i64?[]` and `u64?[]` remain
the explicit scalar exception: every possible raw word is already a valid value, so their native
array lane is the ordinary 64-bit `Item` word. `ItemNull` is its null value; no new nullable
sentinel or second array representation is introduced. The array remains a typed native Array,
not a dynamic boxed Array.

`ArrayNum` has a stricter invariant than the general native Array: it cannot contain *any* null
encoding. It therefore never contains `INT_LANE_NULL`, `BOOL_LANE_NULL`, a float-null NaN
payload, or a sized-integer null sentinel. Its semantic element contract may be `T?`, but its
stored numeric values must remain non-null while it uses the `ArrayNum` representation.

When a null is validly assigned to a nullable `ArrayNum` element, the runtime must perform this
single transition:

```text
ArrayNum<T?> + admitted null store
    -> native Array<T?>
       copy each existing value as its unpacked native lane into a 64-bit word
       write NULL_LANE(T) at the target word
       install the T? LaneStorageDesc
```

This is a demotion from the specialized fast numeric representation to the slower general
native Array. Subsequent `T?` values, including null, remain native lanes. The transition must
be atomic with respect to an observable array write: the target array is either still its
original `ArrayNum`, or it is the fully initialized native Array with the requested store
applied.

The source type contract decides whether that transition is reachable. A declared `int[]` store
of `null` fails its typed-store admission check before mutation and leaves the `ArrayNum`
unchanged. The compiler must reject that store when its value is statically known to be null. A
declared `int?[]` may use `ArrayNum` while all stored values are non-null; its first admitted null
store performs the native demotion above. `int[]` never widens to `int?[]` as a consequence of a
store.

A generic array may also use `ArrayNum` while its values permit that specialization. Its null
store demotes it transparently to the general Array representation while preserving its generic
semantic contract; its slots use the generic Item representation when no concrete lane descriptor
is available. This is not a concrete `T[] -> T?[]` type transition. Re-promotion from a general
Array to `ArrayNum` is explicitly deferred; the first implementation performs no automatic
re-scan or promotion.

### 6.4 Existing value-array covariance preserves the source contract

This proposal inherits, and must implement faithfully, the normative value-array covariance rule
in [`Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md) §9.2. Because `int <: int?`,
an array of the former is assignable to the latter on an ordinary value assignment:

```lambda
var source: int[] = [1, 2]
var target: int?[] = source       // allowed: int[] <: int?[]
target[0] = null                  // allowed: target COW-detaches and demotes if needed
// source is still an int[] containing [1, 2]

var rejected: int[] = target      // type error: int?[] is not a subtype of int[]
```

The assignment does not retag or widen `source`; it creates a target value whose semantic
descriptor is `int?[]`. The two values may initially share a null-free `ArrayNum` buffer under
copy-on-write. A null store through `target` must first detach that target and then perform its
`ArrayNum -> native Array<int?>` transition. `source` retains its `int[]` descriptor and its
null-free `ArrayNum` representation.

This covariance is for values, including ordinary local `var` assignment, whose sharing is
unobservable. A `pn` `var` parameter is an aliasing borrow and remains invariant: an `int[]`
cannot be passed as `var int?[]`, because that would permit the callee to store null into the
caller's non-null array.

---

## 7. Function, closure, and call ABI policy

### 7.1 Native direct calls

A direct native function specialization uses `N(T?)` for nullable scalar parameters and returns:

```lambda
fn increment_or_absent(x: int?) int? {
    x + 1
}
```

Its native ABI takes and returns an `IntLane`; the fourth sentinel carries absence. This avoids
forcing all callers through a boxed `Item` ABI merely because the signature is optional.

### 7.2 Checked boxed entry remains required

First-class calls, dynamic dispatch, imports, and any call site without a proven native
signature continue to use boxed `Item`s. The boxed entry adapter performs the §5.5 conversion,
then enters the native body. The native return is boxed only when returning to a dynamic caller.

This is compatible with the type-enforcement design: correctness still comes from one checked
boundary; native nullable lanes only determine the representation after admission succeeds.

### 7.3 Procedures and `var` parameters

`pn` and `var` parameters need a specific implementation plan because they expose caller-visible
write-back. The semantic local can be an `N(T?)` lane, but the write-back adapter must box the
final lane value to `ItemNull` or `T` before replacing the caller's rooted Item.

Until direct mutable-native ABI support is implemented, it is acceptable for a `pn` to retain a
boxed entry/exit adapter. It is not acceptable to erase its effective type to `any` just because
the parameter is nullable.

---

## 8. LambdaJS policy

LambdaJS has JavaScript's separate surface model:

- a JS Number is a binary64 number;
- JS `null` and JS `undefined` are distinct values;
- Lambda's `T?` means `T | null`, never `T | undefined`.

The existing LJS policy that JS-visible numbers box as float Items remains valid. This proposal
only covers compiler-internal proven-native values and typed storage selected by Lambda
contracts.

1. A Lambda `int?` crossing into JS becomes a JS Number or JS `null`; none of the `IntLane`
   sentinels may escape.
2. A JS `null` crossing into a Lambda `T?` boundary produces that type's `NULL_LANE(T)`.
3. JS `undefined` stays on its existing distinct JS-value path. It must not be silently encoded
   as the Lambda nullable sentinel or enter any native lane.
4. A concrete JS/Lambda contract of `T | null | undefined` remains boxed. Only the exact
   `T | null` contract is eligible for the nullable native lane; an explicit conversion or
   guard must remove `undefined` before a native boundary.
5. LJS named-load/store ICs and constructor-shape metadata must consume `LaneStorageDesc`, so a
   known nullable field uses its native nullable slot rather than widening the property to an
   Item on its first null assignment.
6. JavaScript code without a proven Lambda contract remains dynamically boxed. This proposal is
   not a new hidden JS static type system.

The first implementation enforces this boundary by keeping ordinary JS property reads, named IC
entries, and constructor-shape slots as boxed `Item`s. This preserves the observable distinction
between an own `null` property and a missing `undefined` property; no JS `undefined` value can be
mistaken for a Lambda lane null. A later exact-contract optimization may use `LaneStorageDesc` only
after it proves the value cannot include `undefined`.

---

## 9. Implementation order and verification

1. **Lock lane encodings.** Add `INT_LANE_NULL`, `SIZED_LANE_NULL`, `FLOAT_LANE_NULL_BITS`, and
   the equivalent `f16`/`f32` markers. Add representation assertions that no nullable sentinel
   collides with a valid source-domain value.
2. **Add full-type lane descriptors.** Introduce one shared descriptor resolver used by MIR,
   map layout, native Array layout, `ArrayNum` transitions, runtime field conversion, and LJS.
   Do not create
   independent nullable switches in each subsystem.
3. **Implement conversion funnels.** Add `is_null`, `box`, `unbox`, and checked-boundary helpers
   per descriptor. Every use of a lane must pass through these helpers; do not open-code a
   sentinel literal.
4. **Fix inference first.** Make total reads infer `T?` and preserve the full array element
   contract through joins, chained access, scalar null propagation, `or`, and type tests. In
   particular, never conflate `T[]` with `T?[]` to simplify a storage choice.
5. **Implement native nullable MIR lowering.** Start with `int?` indexed reads, arithmetic,
   and branch narrowing; then parameters/returns; then bool/pointer lanes and sized integers.
6. **Implement storage.** Move typed map fields and native Arrays to descriptors, including
   shape transitions/repacking, COW cloning, `ArrayNum`-to-native demotion on an admitted null
   store, and JS shaped properties.
7. **Enable LJS only after bridge tests pass.** Keep its general dynamic value path boxed.

### 9.1 Implemented first slice (2026-08-05)

- `LaneStorageDesc` resolves the full optional contract instead of deriving storage from the
  base `TypeId`. It rejects abstract, error-bearing, and `undefined`-containing unions.
- General native Arrays use fixed 64-bit words with descriptor metadata. `int?`, `bool?`, and
  `float?` use their respective lane sentinels; `i8?`…`u32?` use a widened i64 lane;
  `i64?`/`u64?` use a validated typed-`Item` word; and pointer-backed `T`/`T?` use a raw pointer
  word (`NULL` for the optional form). A null-free non-view `ArrayNum` is rebuilt as that general
  native Array before an admitted nullable store. A plain `T[]` rejects null and never widens.
- Packed Lambda map fields preserve their optional ShapeEntry contract. `int?`, `bool?`,
  `float?`, `i8?`…`u32?`, and pointer-backed optionals use their native lane storage;
  `i64?`/`u64?` retain the specified typed-Item field. Null/non-null writes stay on that shape
  rather than causing a type-change transition.
- Array admission compares the whole lane descriptor (kind, nullability, and sized/pointer
  detail), not merely whether a source is already native. Thus `int[] -> int?[]` and
  `string[] -> string?[]` give the target its own COW carrier before a nullable store; the
  source's non-null descriptor remains intact.
- Pointer-lane arrays are traced as exact raw-pointer GC edges. Numeric/bool/sized raw words are
  not traced as Items, so a lane sentinel or ordinary number cannot become a spurious root.
- `float?` reserves `0x7ff8_0000_0000_0001`; the store path canonicalizes that payload from an
  incoming numeric NaN so only an actual nullable-lane null decodes as `ItemNull`.
- Focused procedural tests cover covariance/COW preservation, null-to-value and value-to-null
  writes, out-of-bounds reads, and rejection of a non-lane dynamic map value. They also cover
  raw-pointer field and Array lanes for `decimal?`, `datetime?`, and `complex?`, including the
  `T[] -> T?[]` covariance path. Legacy large-int map and unboxed-field cases cover the int-lane
  writer/readers.

The first slice also lowers `int?`/`float?` indexed reads and nullable scalar arithmetic to their
native lane. A nullable `bool` local now keeps its `0/1/2` lane through a boxed-adapter call,
control-flow truthiness, `not`, and generic-Array insertion; it cannot be accidentally decoded as
ordinary `false`. Direct `int?`, `float?`, `bool?`, `string?`, `symbol?`, `binary?`, `decimal?`,
`datetime?`, `complex?`, and supported nullable container functions use the raw lane for declared
parameters and statically proven returns; the checked public wrapper performs the single
Item-to-lane and lane-to-Item conversion. `float?` arithmetic checks the reserved NaN null payload
before executing a hardware operation, so it cannot be canonicalized into ordinary `nan`.

The first slice intentionally leaves `f16?`/`f32?` and LambdaJS IC/property lowering for later
work. Every core pointer scalar now uses the direct raw-pointer/`NULL` ABI. The checked adapter
strips or restores the public Item representation exactly once; `complex` is already an untagged
direct-pointer Item, so its adapter is deliberately an identity conversion rather than a high-byte
tag operation.
It does not use a raw-word shortcut for pointer lanes: their base type is retained in descriptor
metadata and their GC treatment is explicit.

### 9.2 Required test matrix

| Area | Required coverage |
|---|---|
| nullable scalar representation | finite values, all poison values, null sentinel, box/unbox round trips, no collision; float NaN canonicalization |
| array reads | in-range lane read, OOB/negative null lane, N-D read, nested/chained access, `or` recovery |
| operations | arithmetic/null propagation, comparisons, truthiness, no raw arithmetic on a null lane |
| inference | `a[i]: T?`, map/optional-field reads, joins with null, flow narrowing after `is T` |
| boundaries | dynamic null into `T?` succeeds; dynamic null into `T` fails; return/parameter/closure cases |
| map storage | nullable scalar and pointer fields, null/non-null write without shape churn, widening `i8 -> i8?`, COW/repack preservation |
| arrays | boxed Array versus native Array representation; `T[]` and `T?[]` 64-bit native words; `int?[]`, `bool?[]`, pointer and sized nullable lanes; `i64?[]`/`u64?[]` typed-Item native lane |
| array variance | ordinary value assignment accepts `int[] -> int?[]`, preserves the source's non-null descriptor/backing after target null writes, rejects `int?[] -> int[]`, and rejects the covariant form for aliasing `pn var` parameters |
| `ArrayNum` transition | assert that every null encoding is forbidden in `ArrayNum`; an admitted `T?[]` null store copies native values into a native `T?` Array; a rejected `T[] = null` store leaves the original `ArrayNum` unchanged; generic Array demotion is transparent and preserves the generic contract |
| LambdaJS | JS null bridge; `undefined` never enters a native lane; `T | null | undefined` remains boxed; IC/property nullable-slot hit and miss paths |
| GC | nullable pointer local/field survives compaction when non-null and is never treated as a root when null |

Benchmarking must compare archived release binaries with interleaved runs. The initial probes are
`r7rs/sum`, `larceny/diviter`, `awfy/json`, a nullable-map field loop, and a LambdaJS
number/null property loop. The acceptance question is whether common in-bounds `int[]` reads
return to a native loop while preserving the OOB-null semantics.

---

## 10. Open issues requiring a ruling

1. **Complex and abstract types.** Confirm the initial boxed fallback for `any`, `number`,
   `integer`, `T | U`, and `T | error`, rather than inventing a universal native union lane.
2. **Optional fixed shape fields.** Decide whether an absent optional field and a present field
   with value `null` remain distinguishable at the map-observation layer. The proposed physical
   slot represents the value; field-presence metadata, if observable, must remain separate.
3. **Flow proof scope.** Start with explicit `is T` and compiler-generated bounds checks, or
   also recognize user-written `0 <= i and i < len(a)` guards in the first implementation?
   This affects optimization only, not the inferred type of an unproven read.
4. **Native `pn` write-back ABI.** Decide whether the first implementation should introduce a
   nullable native `var` ABI or retain boxed adapters for procedures while keeping their local
   representation native.
5. **Mutable `ArrayNum` views.** `subview`, `reshape`, `transpose`, and `ravel` can write
   through an aliased base buffer. A null store that needs `ArrayNum -> Array` demotion must not
   silently break that write-through relation. Defer this case: the basic implementation does
   not add nullable native-lane mutation through ArrayNum views.
6. **Vector and N-D kernels.** Broadcast, arithmetic, reduction, mask, and image kernels assume
   null-free numeric storage today. They need descriptor-aware nullable-lane kernels and explicit
   null behavior before they may accept a demoted nullable Array. Defer this case: the basic
   implementation does not extend vector/N-D kernels to nullable native Arrays.

---

## 11. Proposal summary

The language already says that total reads return null on absence and that `T?` explicitly admits
that result. The compiler should therefore represent the exact type it has inferred:

```text
int[] read      -> int? -> nullable IntLane
int[] -> int?[] assignment -> allowed COW value copy; source stays int[]
int?[] -> int[] assignment -> type error
int[] null store -> type error; no ArrayNum demotion and no int[] -> int?[] widening
bool? field     -> bool? -> 0/1/2 byte lane
string? field   -> string? -> nullable pointer
i64?            -> Item native lane (no spare raw one-word code)

ArrayNum<T> + admitted null store
                -> native Array<T?> with 64-bit native lane words

i64?[]          -> native Array<Item> using ordinary ItemNull

T | null | undefined -> boxed; undefined does not enter a nullable native lane
```

The floating lane uses a second canonical quiet-NaN payload for null, while the Item boundary
continues to expose ordinary `ItemNull`. With that encoding fixed, a shared full-type lane
descriptor lets inference, MIR, shapes, arrays, runtime conversion, and LambdaJS use one
representation policy instead of repeatedly falling back to `any`/boxed Items at the first
nullable join.
