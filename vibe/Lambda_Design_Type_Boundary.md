# Lambda Type Boundary — Vocabulary and Cost Model

**Status**: Reference (describes shipped behaviour)
**Version**: 1.2.0
**Date**: 2026-08-11
**Scope**: What happens when a value meets a declared type — which types
admit, what admission mutates, and the four terms used throughout the
type/shape work (**admission**, **elision**, **reification**, **erased
field**) and what each costs; plus where Lambda's mixed type system sits
against prior art and the known boundary-cost mitigations (§7–§8).
**Formal authority**: `doc/Lambda_Formal_Design.md` D3.2.1–D3.2.3 (subtype
model, validator as runtime enforcer, declared-vs-effective types),
D3.4.1–D3.4.6 (shape representation, structural identity, interning, layout
invariant, `LaneStorageDesc`); `doc/Lambda_Formal_Semantics.md` S11.4.1
(annotation is a contract).
**Related**: [Type Enforcement](./Lambda_Design_Type_Enforcement.md) (TE-17,
TE-18), [Schema Validator](./Lambda_Schema_Validator.md),
[Shape Pool](./Lambda_Shape_Pool.md)

---

## 1. The boundary

A **type boundary** is any point where a value crosses into a declared type:

```lambda
var c: Ctx = expr        // declaration boundary
f(c)                     // argument boundary, for  pn f(c: Ctx)
return c                 // return boundary, for a declared return type
```

The boundary exists because of what happens *after* it: once a value is
accepted as `Ctx`, the JIT reads its fields at **fixed byte offsets** instead
of looking them up by name. A value that does not physically match the
contract would make those reads address the wrong bytes. The boundary is what
makes the fast field read sound (D3.4.5).

Every boundary resolves one of two ways: **elided** at compile time, or
**admitted** at runtime.

---

## 2. Elision — the check the compiler deletes

**Elision** is proving at compile time that a boundary cannot fail, and
emitting no check at all. Zero runtime cost; the check is simply absent from
the generated code.

The transpiler elides when both sides name the same trusted contract, or when
the declared and expected contracts are exactly related
(`transpile-mir.cpp`, `mir_boundary_is_redundant` and the map-contract case
around the `is_trusted_contract` test).

Elision is the main reason annotated Lambda is fast, and losing it is the main
way annotated Lambda becomes slow. It is lost whenever the compiler cannot see
what is arriving — most commonly when the initializer's static type is `any`:

```lambda
pn make() any { return {a: 1.0, b: 2} }
var c: Ctx = make()      // NOT elided: `any` hides the shape → runtime admission
```

Measured: a typed record passed to a typed parameter — including deep
recursion — produces **zero** admissions. The same record arriving through an
`any`-returning constructor produces **one admission per execution**.

---

## 3. Admission — the runtime accept-and-convert

**Admission** is the runtime act of accepting a value at a boundary
(`runtime_type_admit_value`, `lambda-eval.cpp`). It is deliberately not called
"checking": admission may **convert** the value to the destination
representation. An int-tagged value at a `float` boundary is converted to the
float lane, because leaving it int-tagged would let a native float body unbox
the wrong physical lane (D2.2.2). That conversion is a COW-guarded mutation of
the value itself, not a copy returned to the caller — see §3.2.

For maps, admission has three outcomes, cheapest first:

| Outcome | Test | Cost |
|---|---|---|
| `MAP_CONTRACT_EXACT_TRUSTED` | `candidate == expected` (pointer identity) **and** `expected->is_trusted_contract` | O(1) |
| `MAP_CONTRACT_STORAGE_COMPATIBLE` | every expected field matches by name, semantics, `byte_offset`, and `LaneStorageDesc` | one field walk, then accept as-is |
| `MAP_CONTRACT_NEEDS_REIFICATION` / `INCOMPATIBLE` | semantics compatible but layout differs, or a field is unprovable | full reification (§5) |

The relation is computed by `lambda_map_contract_relation`
(`runtime/type_contract.cpp`) and memoized in a small per-heap cache
(`LAMBDA_MAP_CONTRACT_CACHE_CAPACITY`, `transpiler.hpp`).

**Identity is pointer identity, and literals do not share it.** `type Ctx =
{...}` allocates one `TypeMap` and marks it trusted (`build_ast.cpp`). A map
*literal* `{a: 1.0, b: 2}` builds its own structural `TypeMap` — same fields,
same layout, different pointer. So a literal-built value can never take the
exact path against a named contract; it falls to the field-walk relation.
(This is D3.4.2's structural-identity rule seen from the runtime side, and why
D3.4.3's *runtime* interning — still unimplemented — is the structural fix.)

### 3.1 Which types admit

Admission is not map-only. `runtime_type_admit_value` has four paths:

| Value → contract | Handler | Converts? | Walks members? |
|---|---|---|---|
| numeric → concrete numeric | `lambda_numeric_boundary_admit` | yes — retags/relanes (int → float lane) | no, **O(1)** |
| array / array_num → typed array | `runtime_type_admit_array` | yes — rebuilds via `fn_mutable_value` | **yes**, every element, recursively |
| map → typed map | `runtime_type_admit_map` | yes — reifies shape (§5) | **yes**, every field, recursively |
| union / occurrence | recurses into members | delegates | delegates |

Two asymmetries matter:

- **Numerics convert but never walk** — a numeric boundary is always O(1).
- **Elements have no admission path** (`runtime_type_admit_element` does not
  exist); they reach the array path or the validator instead.

*Erased field* (§4) is map/element vocabulary because it is about `ShapeEntry`
layout, but the underlying phenomenon — a top type making the physical layout
unprovable — applies to arrays as `T[]` versus `any[]` just as much. Maps
merely dominate the record-heavy workloads where this was measured.

### 3.2 Admission mutates, in place, under COW

The conversion is not a copy handed back to the caller — it is a **mutation of
the value itself**, guarded by COW. `runtime_type_admit_map` opens with
`cow_prepare_write(value)`:

- **uniquely owned** → returns the same pointer → everything below mutates the
  **original map in place**;
- **shared / static / immortal** → clones one level first → mutations land on
  the clone, which the caller receives through `*converted`.

Measured both ways: with a live alias, `share_marks 2, shared_copies 1,
copied_bytes 48` (the alias is protected); with no alias — cd2 —
`unique_mutations 40401, shared_copies 0` (all in place).

What gets mutated, in escalating severity:

1. **field value only** — `fn_map_set`, when the value changed but the layout
   already matches;
2. **value + physical layout + field shape** — `map_rebuild_for_type_change`
   rewrites the map's `type`, `data`, and `data_cap`, so the field's
   *type/shape* changes, not only its value;
3. **whole-map contract adoption** — on success, when the relation was proven
   and capacity suffices, the map adopts the contract's `TypeMap` pointer
   outright (`map->type = expected_map`).

Step 3 makes admission **self-healing**: the same map crossing the same
boundary 5,000 times costs **one** reification, then 4,999 cheap admissions
(`reifications 1, storage_compatible 4999, fields_visited 2`). A map is
permanently upgraded by being admitted once.

This sharpens §6's worked example. cd2 does not reify 40,401 times because
reification fails to stick — it sticks. cd2 reifies 40,401 times because it
constructs **40,401 fresh maps**, and each new instance starts un-adopted.
Self-healing rescues long-lived records; it cannot rescue per-iteration
construction, which is why runtime interning (§8, §9) is the fix: interning lets a
literal *start* with the canonical shape instead of earning it.

**Correctness note.** Mutating a value as a side effect of passing it to a
typed parameter is observable in principle — the map's `type` pointer and
physical layout change. It is sound only because COW guarantees no other
reference can observe the pre-admission state. `cow_prepare_write` is
therefore load-bearing for **correctness** here, not merely for performance
(D4.4.2).

---

## 4. Erased field — why the layout becomes unprovable

An **erased field** is one whose declared type is a top type — `any`, `map`,
`list`, `array` — rather than a concrete type (`int`, `float`, `string`, or a
named record):

```lambda
type Arr      = {l0: list, sz: int}             // l0 erased
type RbtTree  = {root: int, cnt: int, nd: map}  // nd erased
type Variable = {value: int, stay: bool, name: string}   // none erased
```

"Erased" because the specific type has been rubbed out: the contract says
*"some map lives here"*, not *"a map with this exact shape"*.

This is a **physical** problem, not only a semantic one.
`contract_storage_desc_equal` (`type_contract.cpp`) compares lane kind, byte
size, nullability, and base contract. A concrete `int` field pins an 8-byte
native lane; an erased field is a slot whose actual occupant may carry a
different width or offset than the generic contract assumes. The layouts
cannot be proven equal, so the relation degrades from storage-compatible to
needs-reification.

One erased field changes the whole record's admission cost:

| Contract | Admissions | exact | storage-compatible | reifications | fields visited |
|---|---:|---:|---:|---:|---:|
| `{a: float, b: int}` | 20,000 | 0 | **20,000** | 0 | 0 |
| `{a: float, n: map}` | 20,000 | 0 | 0 | **20,000** | 40,000 |

(Measured with `COW_EXEC_PROFILE=1 COW_EXEC_PROFILE_OUT=<file>`; identical
programs apart from the one field.)

---

## 5. Reification — making the abstract concrete

**Reification** is rebuilding a value's shape so it *physically* matches the
expected contract: `runtime_type_admit_map` walks every expected field,
recursively admits each value, and republishes the map's shape with the
contract's `byte_offset`s and storage descriptors.

It is not optional paranoia. The code states the hazard directly: a dynamic
`any` field can hold the right *value* while retaining a different packed
width/offset, and direct MIR reads use the admitted contract's layout — so
accepting a value-only match would let later fields be read at the wrong byte.

What reification costs is the **field walk** plus `cow_prepare_write` and the
root-frame setup. It does **not** normally copy: `cow_prepare_write` clones
only a shared/static/immortal root, so a uniquely-owned map is reified in
place (§3.2). (`map_admit_bytes_copied` is a *hypothetical footprint* counter
recorded before that decision — it is not evidence of memory traffic. Check
`shared_copies` / `copied_bytes` in the per-type COW table for actual copies.)

The cost is paid per admission **per instance**. A given map pays it once and
is then upgraded (§3.2 step 3), so the cost falls entirely on freshly
constructed values — and the verdict itself depends only on the (candidate
shape, expected shape) pair, which is why memoizing it, or interning shapes so
literals start canonical, are the fixes rather than removing the walk.

---

## 6. The cost model in one line

> A boundary is either **elided** (free) or **admitted** (runtime); admission
> is O(1) on pointer identity, one field walk when storage-compatible, or a
> full **reification** walk when an **erased** field makes the layouts
> unprovable.

Two independent things therefore make a program slow at its boundaries, and
they stack:

1. **Losing elision** — usually an `any`-typed initializer or argument — moves
   the work to runtime at all;
2. **Erasure in the contract** — one `any`/`map`/`list` field — makes that
   runtime work the expensive branch.

Worked example (awfy/cd2, 60,803 admissions, 1 exact hit, 66% reification):
its constructors are declared `any` (`arr_new()`, `rbt_new()`) which kills
elision, and its records carry erased fields (`l0: list`, `nd: map`) which
forces reification. jetstream/deltablue2, whose records are all concrete
scalars, takes the exact path 76,220 times instead. Neither is anomalous —
this is the ordinary consequence of the two properties above.

---

## 7. Prior art — where Lambda sits

Most "gradually typed" languages differ along three independent axes. Naming
them separately is what makes the comparison useful, because the interesting
languages disagree about which to pick:

| Axis | Options |
|---|---|
| **Enforcement** | *erased* (types vanish at runtime) vs *enforced* (checked at runtime) |
| **Representation** | *uniform* (everything boxed) vs *representation-changing* (typed values get unboxed native lanes) |
| **Identity** | *nominal* (name-based) vs *structural* (shape-based) |

Lambda picks **enforced + representation-changing + structural**. That
combination is uncommon, and it is precisely what generates the machinery in
§2–§5: enforcement creates admission, representation-change makes admission
have to *convert* rather than merely check, and structural identity is why
identity is a `TypeMap` pointer comparison that literals fail (§3).

| Language | Enforcement | Representation | Relation to Lambda |
|---|---|---|---|
| **Typed Racket** | enforced (contracts at typed/untyped module boundaries) | uniform | Closest on **enforcement**. Same "trusted inside, checked at the seam" model as Lambda's boundary admission — but a typed value is the same runtime object as an untyped one, so it has the boundary problem without the lane problem. |
| **Julia** | not contract-enforced | representation-changing | Closest on **representation**. Annotations/inference drive specialization to unboxed native code, with boxed dynamic fallback. Its community concept of **"type instability"** — one `Any` in a hot path forcing boxing — is effectively Lambda's ANY-downgrade. Differs in specializing on *observed* types rather than enforcing declared contracts. |
| **Common Lisp (SBCL)** | dial-able (`safety` decides checked vs trusted) | representation-changing (`declare type` enables unboxing) | The historical precedent for the representation axis. Its trusted/checked dial is what Lambda decides per boundary via elision (§2). |
| **Strongtalk**, **Dylan** | optional, semantics-preserving | optimization-oriented | The ancestors: optional static types layered on a dynamic language for optimization, not proofs. Strongtalk (Bracha & Griswold, OOPSLA 1993) is the canonical "optional typing that doesn't change semantics" reference. |
| **Hack (PHP)**, **Raku**, **Luau**, **Groovy** (`@CompileStatic`) | enforced | mostly uniform | Mixed models with runtime enforcement but no unboxed-lane story. |
| **C# `dynamic`** | enforced | static default | Comes from the opposite direction: static by default with a dynamic escape hatch. |
| **TypeScript**, **mypy**, **Flow**, **Erlang + Dialyzer** | **erased** | uniform | Types are a separate checking pass, gone at runtime. No admission, no reification, no lanes — which is why TypeScript cannot do what Lambda does with `int[]`, and equally why it never pays cd2's cost. |

Summary: Lambda is closest to **Typed Racket's enforcement model executed on
Julia's representation model**, with structural shapes. Nothing about its
boundary costs is idiosyncratic — they follow from that choice.

---

## 8. The gradual typing boundary cost

The §6 worked example is an instance of a named, studied phenomenon.

**The phenomenon.** In a mixed-typing language, cost concentrates neither in
typed code nor in untyped code but **at the seams between them**. Takikawa et
al., *"Is Sound Gradual Typing Dead?"* (POPL 2016), measured Typed Racket
across all typed/untyped configurations of real programs and found
order-of-magnitude slowdowns localized at boundaries. Partially typing a
program can therefore be *slower than either extreme* — the classic result
that made this a first-class research problem. (Gradual typing itself is Siek
& Taha, 2006.)

That is structurally the same finding as cd2: not slow because it is typed,
not slow because it is dynamic, but slow because typed records arrive through
`any`-returning constructors, so every crossing pays.

**Known mitigations, and where Lambda already stands on each:**

| Strategy | Idea | Lambda's position |
|---|---|---|
| **Monotonic references** (Siek et al., ESOP 2015) | On crossing, *upgrade the value in place* so later checks are cheap and no wrapper accumulates. | **Already implemented**, arrived at independently: the contract adoption `map->type = expected_map` in §3.2 step 3 is exactly this. Measured self-healing — one reification, then 4,999 cheap admissions. |
| **Transient / shallow checking** (Vitousek et al., Reticulated Python) | Check shallowly and repeatedly at each *use* instead of deeply once at the boundary. | Partially present: the validator's allocation-free fast mode is a shallow predicate; the deep reification walk is the eager form. The two coexist rather than one replacing the other. |
| **Nominal tagging / type interning** | Make the identity test a pointer compare by canonicalizing type identity. | **The main gap.** D3.4.3's runtime shape interning is unimplemented, which is why literal-built maps can never hit `EXACT_TRUSTED` (§3) and each fresh instance re-earns its contract. |
| **Memoizing the boundary verdict** | The answer depends on the (candidate, expected) shape pair, not the instance. | Partially present: the relation cache exists but is 16 entries, linearly scanned, round-robin evicted (38–48% miss). The *reification* verdict is not memoized at all. |
| **Erasure / trusting the annotation** | Drop the runtime check entirely. | Rejected by design: annotations are contracts (S11.4.1) and the JIT reads fields at fixed offsets (§1), so an unchecked value would mis-address memory. Elision (§2) is the *sound* version of this — omit the check only where it is proven redundant. |

**The design reading.** Lambda already has the two mitigations that need no new
machinery (monotonic upgrade, sound elision). What it lacks is the one that
makes those effective for **freshly constructed** values — canonical shape
identity. Self-healing upgrades a value that survives; interning is what makes
a value *start* upgraded. Programs that build records per iteration, like cd2,
can only be fixed by the latter.

A second, cheaper reading also follows: because the cost is concentrated at
seams, it is disproportionately sensitive to the *typing of constructors*. An
`any`-returning `arr_new()` converts every use site into a boundary. Declaring
constructor return types is a source-level change with no runtime machinery at
all, and would restore elision (§2) for the whole family of call sites — worth
measuring before investing in interning.

---

## 9. Open items

- **Runtime shape interning** (D3.4.3): literal-built maps do not intern, so
  they can never take the pointer-identity path against a named contract, and
  each fresh instance re-earns its contract through reification (§3.2).
  Interning would let literals start canonical, converting cd2's relation
  computations into pointer compares.
- **Relation cache**: 16 entries, linear scan, round-robin eviction; measured
  38–48% miss on record-heavy benchmarks, and each miss recomputes a shape
  walk.
- **Reification memoization**: the verdict is a property of the shape pair,
  not the instance, but is recomputed per admission.
- **Counter naming**: `map_admit_bytes_copied` measures a hypothetical
  footprint, not copies (§5); renaming it would stop the next reader drawing
  the wrong conclusion.
