# Lambda Compiling — Native Lane Emission

- **Status:** PROPOSAL. Not started. Motivated by the v5 `int` migration, but the defect it
  fixes is older than v5 and independent of it.
- **Date:** 2026-08-04
- **Scope:** how MIR Direct (`lambda/runtime/transpile-mir.cpp`) tracks the *representation* of
  an emitted value. Not about which representation any type gets — that is
  [`Lambda_Semantics_Int_Type.md`](Lambda_Semantics_Int_Type.md) §5.
- **Related:** [`Lambda_Impl_Int_Issues.md`](Lambda_Impl_Int_Issues.md) (the bug ledger this
  proposal generalizes), `lambda/runtime/value_rep.h` (the vocabulary already exists).

---

## 1. The rule this proposal restores

> **At transpile time the AST is the single source of truth. The transpiler must never read
> back the emitted MIR to decide what an expression's type or representation is.**

MIR is the transpiler's *output*. Querying `MIR_reg_type()` to recover a fact the transpiler
itself decided is reading your own output to remember your own decision — and it is lossy,
because the register class has fewer states than the representation question has answers.

There is exactly one legitimate exception, kept explicitly in §6.

---

## 2. What is wrong today

### 2.1 The return type erases the answer

```c
static MIR_reg_t transpile_expr(MirTranspiler* mt, AstNode* node);
```

A bare `MIR_reg_t` is a register *handle*. It does not say whether that register holds a boxed
`Item`, an int lane value, a machine integer, or a double. Every consumer needs that fact, so
every consumer recovers it — and there are two recovery paths, one merely redundant and one
actively wrong.

**Path A (redundant but correct): re-derive from the AST.** Call `get_effective_type(mt, node)`
again at the call site. Correct, because the AST *is* the authority — but it means the callee
computed the answer, discarded it, and the caller recomputed it. The scale of the redundancy is
visible in the file:

| | count |
|---|---|
| `transpile_expr(mt, …)` calls | **153** |
| `get_effective_type(mt, …)` calls | **144** |

A near-1:1 ratio is the tell.

**Path B (wrong): ask MIR.** 16 sites call `MIR_reg_type(mt->ctx, reg, mt->em.func)`, of which
**12 use it to answer a semantic question**:

```c
if (MIR_reg_type(mt->ctx, reg, mt->em.func) == MIR_T_D) return reg;   // "already in the lane?"
```

Sites: `transpile-mir.cpp` 1044, 1054, 2499, 2510, 5881, 5888, 5938, 5945, 7841, 7848, 12950,
16984.

### 2.2 Why Path B worked for years, then stopped

Under v4 the mapping happened to be **injective**:

```
MIR_T_D    ⟺  scalar lane   (int and float shared it)
MIR_T_I64  ⟺  boxed Item    (mostly)
```

One bit answered the question, so the proxy was correct-by-accident. It was never a designed
invariant — nothing enforced it, nothing documented it, and no test pinned it.

v5 moved `int`'s lane to `MIR_T_I64` and **the injectivity is gone**:

| meaning | C type | MIR register type |
|---|---|---|
| boxed `Item` | `uint64_t` | `MIR_T_I64` |
| **int lane** (band integer *or* a sentinel) | `int64_t` | `MIR_T_I64` |
| **machine quantity** (index, length — always finite) | `int64_t` | `MIR_T_I64` |
| float lane | `double` | `MIR_T_D` |

Three semantically distinct things now share one register class. The proxy did not start
failing loudly — it started returning **the wrong answer silently**, which is why the v5 lane
bugs surfaced one at a time as runtime garbage instead of as compile errors.

### 2.3 The bug ledger this produced

Every one of these is the same shape — *value is in representation A, consumer assumed B,
nothing checked*:

| Symptom | Actual confusion |
|---|---|
| `1 + 2` → `inf` | 32-bit compare (`MIR_GES`) used for a 64-bit band check |
| `ndim(a)` → `5601509376` | int64 Item (tagged pointer) unboxed as a packed int lane |
| `a[0]=9` then read → `inf` | double written into i64 lane storage (`array_int_set`) |
| `[-5e-324, …]` for `[-1, …]` | lane value bit-**cast** to double instead of converted |
| `dmov: Got 'int', expected 'double'` | int lane fed to a float-lane consumer |
| shaped-field split → **+93 failures** | one writer converted, four others not |

The last row is the important one: the failure mode is *distributed*. There is no single place
to make correct, so fixes serialize — fix the store, the read is still wrong; fix the read, the
printer is still wrong.

---

## 3. The vocabulary already exists

`lambda/runtime/value_rep.h` already defines exactly the right enum, and the emitter already
uses it — for **function signatures and ABI**, via `FnReturnLaneAnalysis`,
`em_value_class_for_rep()`, and `lambda_value_rep(TypeId)`:

```c
typedef enum ValueRep {
    VALUE_REP_NONE = 0,
    VALUE_REP_ITEM,
    VALUE_REP_I64,
    VALUE_REP_U64,
    VALUE_REP_F64,
    VALUE_REP_RAW_GC_POINTER,
    VALUE_REP_RAW_NON_GC_POINTER,
} ValueRep;
```

**So this proposal does not introduce a concept. It extends an existing one from function
boundaries to expression results** — which is the only place it is currently missing, and
exactly where the bugs are.

One change to the enum is required: **`VALUE_REP_I64` currently conflates the int lane with a
machine quantity.** Under v5 those differ in a way that matters — a lane may hold
`INT64_MAX` meaning `+inf`, a machine length holds `INT64_MAX` meaning nine quintillion
elements. They must split:

```c
    VALUE_REP_INT_LANE,   // band integer, or one of the three lane sentinels (may be poison)
    VALUE_REP_MACHINE,    // index / length / count / offset — finite by contract
```

`VALUE_REP_I64` is retained as the ABI spelling for `int64`-typed values (a distinct Lambda
type), so the split is additive, not a rename.

---

## 4. The proposed design

### 4.1 Emitted values carry their representation

```c
// An emitted expression result: the register, plus what is IN it. The AST decided
// the representation; this carries that decision instead of making every consumer
// re-derive it (or, worse, ask MIR).
typedef struct Emitted {
    MIR_reg_t reg;
    ValueRep  rep;
    TypeId    tid;   // the node's semantic type; `rep` is its carrier
} Emitted;

static Emitted transpile_expr(MirTranspiler* mt, AstNode* node);
```

`tid` is included deliberately: several consumers need the *semantic* type (which boxing rule,
which contract to check) and not just the carrier, and today they re-call
`get_effective_type()` for it. Carrying both retires all 144 of those calls at expression sites.

### 4.2 One coercion function replaces the ad-hoc conversions

```c
// Move a value into the requested representation. The ONLY place representation
// changes. Identity when already there; asserts on impossible pairs.
static Emitted emit_as(MirTranspiler* mt, Emitted v, ValueRep want);
```

Every current shape becomes a call to this:

| today | becomes |
|---|---|
| `emit_int_native_lane_typed(mt, reg, tid)` | `emit_as(mt, v, VALUE_REP_INT_LANE)` |
| `emit_scalar_native_lane(mt, reg, tid)` | `emit_as(mt, v, lambda_value_rep(tid))` |
| `emit_int_lane_to_double(mt, lane)` | `emit_as(mt, v, VALUE_REP_F64)` |
| `emit_double_to_int_lane(mt, d)` | `emit_as(mt, v, VALUE_REP_INT_LANE)` |
| `mir_emit_as_double(mt, reg, tid)` | `emit_as(mt, v, VALUE_REP_F64)` |
| `emit_machine_index(mt, reg, tid)` | `emit_as(mt, v, VALUE_REP_MACHINE)` |
| `emit_box(mt, reg, tid)` | `emit_as(mt, v, VALUE_REP_ITEM)` |
| `emit_unbox(mt, reg, tid)` | `emit_as(mt, v, <target rep>)` |

That is **eight partially-overlapping helpers collapsing into one**, each of which currently
encodes its own idea of what the input was.

### 4.3 The transition matrix becomes explicit and testable

`emit_as` is a small square matrix over `ValueRep`, which means the representation contract can
be **unit-tested directly** for the first time — today it is implicit in eight helpers and
twelve `MIR_reg_type` reads.

| from → to | ITEM | INT_LANE | MACHINE | F64 |
|---|---|---|---|---|
| **ITEM** | id | unbox (tag-dispatched) | unbox + band check | unbox |
| **INT_LANE** | box (pack / poison→IEEE) | id | band check, else error | lane→double (sentinel→IEEE) |
| **MACHINE** | box (always packs) | id (finite ⊂ lane) | id | `I2D` |
| **F64** | box float | double→lane | narrow + guard | id |

Two cells are worth calling out because both were live bugs in v5:

- **ITEM → INT_LANE must dispatch on the actual tag**, not assume a packed int. `ndim()`
  declares `int` and returns an int64 Item.
- **INT_LANE → F64 must convert, never bit-cast.** The `-5e-324` failure is exactly this cell
  done as a reinterpret.

### 4.4 `MIR_reg_type` becomes a banned call, with one exception

Add a lint (`make lint`, alongside the existing `no-int-cast-radiant` rule):

> `MIR_reg_type` may not be called in `transpile-mir.cpp` except in the GC-rooting helpers.

---

## 5. Refactoring plan

Phased so the tree stays green at every step. **Do not start until the v5 defects in
`Lambda_Impl_Int_Issues.md` §1 are closed** — this refactor is mechanical, and mixing it with
live debugging makes both harder.

### Phase L1 — split the enum (no behavior change)

Add `VALUE_REP_INT_LANE` / `VALUE_REP_MACHINE`; teach `lambda_value_rep(TypeId)` to return
`VALUE_REP_INT_LANE` for `LMD_TYPE_INT`. Nothing consumes them yet. **Gate:** zero test churn.

### Phase L2 — introduce `Emitted` and `emit_as`, adapt at the boundary

Add both. Give `transpile_expr` an `Emitted`-returning twin, and make the old signature a thin
wrapper that discards the tag:

```c
static MIR_reg_t transpile_expr(MirTranspiler* mt, AstNode* node) {
    return transpile_expr_v(mt, node).reg;   // legacy shim
}
```

Nothing breaks; both APIs coexist. Implement `emit_as` by delegating to the eight existing
helpers, so behavior is bit-identical. **Gate:** zero test churn, zero MIR-emission churn
(ratchet must not move).

### Phase L3 — migrate call sites in dependency order

Convert from the leaves inward, one cluster per commit, ratchet green after each:

1. arithmetic and comparison (the densest lane traffic, already partly typed by
   `LaneReg`/`BoxedReg`)
2. indexed load/store and typed arrays (where the v5 bugs concentrated)
3. declarations, assignments, and bindings
4. calls, returns, and boundaries
5. containers, fields, control flow

At each site the pattern is the same and mechanical:

```c
-   MIR_reg_t v = transpile_expr(mt, node);
-   TypeId tid = get_effective_type(mt, node);
-   if (MIR_reg_type(mt->ctx, v, mt->em.func) != MIR_T_D) v = emit_unbox(mt, v, tid);
+   Emitted v = emit_as(mt, transpile_expr_v(mt, node), VALUE_REP_F64);
```

**Gate per cluster:** baseline green; MIR ratchet unchanged (this is a refactor — any emission
delta is a defect, not an improvement).

### Phase L4 — delete the shim and the reads

Remove the legacy `transpile_expr`, fold the eight helpers into `emit_as`, delete the 12
semantic `MIR_reg_type` reads, add the lint rule. **Gate:** the lint passes; grep shows
`MIR_reg_type` only in the rooting helpers.

### Phase L5 — extend to the guest emitters

`js_mir_*`, `transpile_py_mir.cpp`, `transpile_rb_mir.cpp` have the same shape and the same
latent bug class. Same treatment, after the Lambda side proves the design.

---

## 6. The one legitimate `MIR_reg_type` use

GC rooting (`transpile-mir.cpp` 1243, 1267, 1286, 1314):

```c
if (value && MIR_reg_type(mt->ctx, value, mt->em.func) == MIR_T_D) return;  // skip rooting
```

This asks a genuinely **machine-level** question — *can this register class physically contain
a pointer?* — and a double register cannot. The register type is the correct authority for that,
it stays correct under any representation scheme, and it must not be replaced by `ValueRep`
(a `VALUE_REP_ITEM` value in an i64 register is exactly what rooting exists for). Keep these
four, comment them as the sanctioned exception, and exempt them in the lint.

---

## 7. Cost: the "tagged return" problem

The core change is one line:

```c
- static MIR_reg_t transpile_expr(MirTranspiler* mt, AstNode* node);
+ static Emitted   transpile_expr(MirTranspiler* mt, AstNode* node);
```

Changing a return type is not local. **153 call sites** stop compiling at once, and each must be
read and converted — that is the cost, and it is why the shim in Phase L2 exists: it lets the
153 be converted in graded clusters instead of in one commit that cannot be reviewed or bisected.

Three things make the cost bounded rather than open-ended:

1. **The compiler enumerates the work.** Every unconverted site is a type error with a file and
   line. There is no searching, and no silent miss.
2. **Most sites get simpler.** The common shape is `transpile_expr` + `get_effective_type` +
   a conversion — three lines collapsing to one. Expect the file to shrink.
3. **The ratchet is a correctness oracle.** A pure refactor must not change emitted MIR. The
   existing budget tests turn "did I break it?" into a mechanical check per cluster.

The genuine risks, stated plainly:

- **Merge conflict surface.** Touching 153 sites in a 20k-line file conflicts with anything else
  in flight. Sequence it against other `transpile-mir.cpp` work; do not run it concurrently.
- **`Emitted` is 3 words, not 1.** Passed by value in registers on both SysV and AAPCS64, so no
  ABI cost — but it is no longer trivially copyable into arrays of `MIR_reg_t`, and a few sites
  that keep register arrays will need small adjustments.
- **It will find latent bugs.** Sites that "work" only because two representations happened to
  coincide will now be explicit and may be wrong. That is the point, but it means the refactor
  is not purely mechanical in the last 10%.

---

## 8. Rejected alternatives

**Keep `MIR_reg_t`, discipline the call sites.** This is the status quo. It is what produced the
v5 bug ledger. Discipline distributed over 153 sites is not a mechanism.

**Wrapper types only (`LaneReg` / `BoxedReg`).** Already landed for the int-lane API and
worth keeping — they made 16 crossings explicit and made a deliberate confusion a compile error.
But they cover one API, not the general contract; a wrapper per representation pair does not
scale, and they cannot retire `get_effective_type` re-derivation. They are the cheap 80%, not
the design.

**Make MIR carry the distinction (`MIR_T_P` for Items).** Impossible. `MIR_new_func_reg`
accepts only `I64`/`F`/`D`/`LD`, and `mir_reg_type_for_alloc()` already folds `MIR_T_P` to
`MIR_T_I64` — a "pointer register" reports `I64` too. Making it work would mean patching
vendored MIR (rule 16) for a distinction that is not a machine property.

**Annotate the AST with the carrier instead.** The AST is the source of truth for the
*semantic type*, and it should stay that. The carrier is a *lowering* decision that can differ
per occurrence (the same node boxed at one use, native at another). It belongs on the emitted
value, not on the node.

---

## 9. Open questions

- **L-Q1.** Should `Emitted` carry a *proof* flag (finite / non-null / already-checked) so the
  band and null checks can be elided by construction rather than re-derived? This is where the
  §5.3 finiteness dataflow would live. Attractive, but it turns a 3-word POD into a small
  lattice — propose deferring to a follow-up.
- **L-Q2.** Does `VALUE_REP_MACHINE` need a width (u32 index vs i64 offset), or is the existing
  `mir_binary_is_exact_u32_result` special case enough?
- **L-Q3.** `transpile_box_item()` and friends return `MIR_reg_t` today and are called from
  outside expression lowering. Do they migrate in L3, or keep a shim permanently?
