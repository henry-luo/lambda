# Lambda — Type Support Design: Enforcement First

**Status:** PROPOSAL rev 2 — complete, for discussion. Nothing here is implemented.
**Date:** 2026-07-29; revised 2026-07-30
**Scope:** making declared types *binding* — statically checked where provable, runtime-enforced
where not, never silently dropped or lossy. This document is **only about enforcement
(correctness)**. Leveraging annotations for faster code is the explicit *next* stage and is
touched here only where enforcement is its prerequisite.
**Related:** `vibe/Lambda_Issue_Type_Support.md` (TS-1…TS-9 issue ledger; evidence),
`doc/Lambda_Formal_Semantics.md` (semantic authority), `doc/Lambda_Type.md`,
`vibe/Lambda_Issues_Outstanding.md` OI-5 (MIR value-representation contract),
`doc/dev/lambda/LR_13_Schema_Validator.md` (validator design).
**Evidence basis:** all "measured" claims below were reproduced on 2026-07-29 against the current
release `lambda.exe`; repro scripts in `./temp/tsd_t*.ls`.

---

## 1. Goal and non-goals

**Goal.** A type annotation in Lambda is a **contract on the binding**, in the tradition of Go —
not a gradual-typing hint in the tradition of TypeScript. Every annotated boundary must resolve
to exactly one of three outcomes:

1. **STATIC-PROVEN** — the checker proves the value's type; zero runtime validation cost.
2. **STATIC-REJECTED** — the checker proves a mismatch; compile error.
3. **DEFERRED** — the source is genuinely dynamic (`any`-typed expression, parsed input data);
   a runtime check runs **at the boundary**. Success establishes the declared `T`; mismatch
   produces a diagnostic-carrying type-error *value* and does not establish or mutate the typed
   destination (TE-9).

What must never happen is the fourth outcome Lambda has today: the annotation silently ignored,
silently lossy, or silently corrupting (see §5.4 — all three are measured, including declared-int
bindings that print raw `String*` pointer bits).

Annotated and inferred bindings are deliberately different:

```lambda
let x: T = e   // contract boundary: after success, x is T; failure yields error before x exists
let x = e      // inference only: x receives e's effective type, which may be T | error
```

The second form is where batch-friendly error values flow without a panic or an imposed
must-handle channel. It must not be used to weaken the first form's contract.

**Non-goals of this stage:**

- Performance leverage of annotations (typed lanes, direct field offsets, two-entry call
  specialization). That is the next stage; this doc only establishes the guarantees that make it
  sound. TS-3/TS-4/TS-5/TS-6 stay in the issues ledger for that stage.
- Schema-driven typing of XML input (default XML is untyped/semi-typed; with a schema the data
  could be precisely typed — **KIV**, out of scope here).
- Flow-sensitive narrowing (`if (x is int) …` refining `x`), generics, and arity overloading
  (TS-8). Noted in §10 as open questions.

---

## 2. Prior art

### 2.1 TypeScript — gradual, erased, silent at the dynamic boundary

TS has no notion of "statically undecidable": every expression gets a type, and the not-knowable
cases are *spelled* as types with opposite policies. `any` is assignable in both directions —
passing an `any` into `f(b: number)` compiles silently, and since types are fully erased, nothing
checks at runtime either: `f(JSON.parse(s))` with `"abc"` inside computes `"abc" + 1 → "abc1"`.
`unknown` (added in TS 3.0 as the safe spelling) is the opposite: a compile error until narrowed.
A statically *known* wrong argument is always an error.

TS can afford the silent `any` hole for one structural reason: **types never affect
representation**. Every JS value is uniformly dynamic; a type lie produces a logic bug, never
corruption. Even so, the ecosystem spent a decade clawing the hole back (`unknown`,
`noImplicitAny`, typescript-eslint's `no-unsafe-argument`). The lesson is not "silence is fine";
it is "silence is only *survivable* under full erasure — and still regretted".

### 2.2 Python — same surface model, and both enforcement branches in one ecosystem

Python type hints (PEP 484) are the same gradual model: `Any` flows silently exactly like TS
`any`; `object` is the `unknown` analogue; checkers are external, optional and plural (mypy,
Pyright), and CPython itself never looks at annotations. But annotations *survive to runtime* as
data, and the ecosystem split into two branches that mark out Lambda's design space:

- **Hints as lint** (mypy over CPython): advisory, erased-in-effect, silent `Any` flow —
  affordable only because the runtime is uniformly boxed and every operation dynamically checks
  anyway. Hints never make CPython faster.
- **Hints as representation** (mypyc, Cython): the compiler uses `int` annotations to unbox into
  native ints — and is thereby **forced** to check at every typed/untyped boundary. A
  mypyc-compiled function type-checks its arguments when called from interpreted code and raises
  `TypeError` on mismatch. Nobody on this branch trusts silently, because a lie corrupts memory
  instead of producing `"abc1"`.

A third data point: CPython's own specializing interpreter (PEP 659) deliberately ignores hints
and specializes on *observed* types behind guards — annotations were judged too unreliable to
drive codegen. Lambda's inference + ICs are that mechanism; the ledger's TS-3 (an `int[]`
annotation downgrading an inferred `ARRAY_NUM` local to ANY) is Lambda accidentally proving the
same point.

### 2.3 Go — the direction Lambda wants

Go is the model this proposal follows. Types always drive representation; there is no silent
dynamic flow. The dynamic boundary is *explicit and always checked*: crossing out of
`interface{}`/`any` requires a type assertion `x.(T)` that panics (or returns `ok=false` in the
two-value form), and dynamic data enters typed structs through `json.Unmarshal`, which validates
against the destination type at the boundary and returns an error. Inside the boundary, code
trusts types completely — that trust is what the boundary check purchases.

### 2.4 The rule all three triangulate

> **An annotation may drive representation only if its boundary is enforced. An unenforced
> annotation must remain advisory — and must never override what inference already proved.**

Lambda today violates both halves: annotations drive representation (native lanes, typed shape
packing) without enforcement (§5.4), and annotations *downgrade* proven inference (TS-3's
ANY-downgrade). This document fixes the first half; the perf stage collects the winnings on the
second.

---

## 3. Lambda's model: type is binding

Decision items are numbered **TE-n** (type enforcement) and cross-reference the TS-n issue
ledger.

**TE-1 — Annotations are semantic contracts, not hints.** `x: T` means: on successful passage
through the boundary, `x` *is* a `T`. A failed check produces the boundary's error result before
the binding is established; it never stores `error` in `x`. This holds identically for `let`,
`var`, parameters, returns, and typed fields of named map types. `var` permits the value to
change; it does not weaken or replace an explicit binding type. Every later whole-value
assignment or interior update through `var x: T` must leave the new value conforming to `T`.
By contrast, an unannotated `let x = e` or `var x = e` simply infers `e`'s effective type,
including `T | error` when `e` is open; an unannotated `var` may acquire a different inferred
type/shape after a later assignment.

TE-1 does **not** assign one global physical representation to a semantic type. The boxed path
may carry `int` as an `Item`, while an explicitly selected native function variant may carry it
as a raw integer lane. Physical representation becomes an additional local invariant only where
the implementation deliberately selects one — currently native/unboxed function bodies and
explicitly typed packed map fields (TE-3).

**TE-2 — Three-outcome resolution at every annotated boundary.** Each boundary is resolved as
STATIC-PROVEN, STATIC-REJECTED, or DEFERRED (runtime-checked) — never silently trusted:

- `S <: T` under Lambda's subtype relation → STATIC-PROVEN;
- a statically known source that is not a subtype of `T` → STATIC-REJECTED (explicit unions must
  be narrowed first; partial overlap does not silently become a runtime cast);
- only a genuinely dynamic source (`any`, unresolved runtime input/shape) → DEFERRED.

The static subtype relation, runtime value match, and explicit checked conversion are related
operations with shared numeric/type primitives, but they are not falsely identified as one
operation (TE-6).

**TE-3 — Representation invariants are local and proof-backed.** Semantic `Type*`/TypeId does
not globally determine a value's carrier. Instead, every emitter operation that requires a
physical representation must be dominated by one of:

1. a statically proven boundary plus a conversion into that carrier;
2. a successful runtime check/conversion; or
3. construction directly into an explicitly typed physical slot.

Within an unboxed function version, a proven `int` parameter may therefore remain a native
integer throughout the body. Within a map layout, an explicitly typed `int` field may remain an
unboxed field slot. The boxed version and generic maps remain valid alternative carriers of the
same semantic types. Recording and exploiting these physical proofs is deliberately separated
from enforcement correctness and revisited in the implementation/performance stage.

Maps additionally maintain a strong local layout invariant: every stored field is encoded
according to the map's **current runtime `ShapeEntry`**, and the current shape describes those
bytes exactly. A store must never place a differently encoded value into an old slot while
leaving the old shape in force. If a legal write changes a field's physical type, the runtime
builds/selects a new shape and repacks the map before committing the replacement. COW may reuse
a uniquely owned container header, but that optimization is unobservable; a shared value is
detached and the replacement is installed back into the owning binding or parent path.

**TE-4 — Runtime mismatch produces a rich diagnostic-carrying error value.** A failed DEFERRED
check yields a proper error object/value (decided 2026-07-30; see TE-9), naming the boundary,
expected type, actual type/value, source location, and validator path where applicable. It flows
like any Lambda error value — dischargeable with `let x^err = …`, postfix `^`, `or`, or
`x is error` — and a script whose uncontained result is an error fails with that diagnostic.
Never `null`, never `0`, never pointer bits, never a silent pass-through. The earlier inline
code-only error form is rejected: a bare code cannot satisfy this diagnostic contract.

**TE-5 — `any` is the top type and the gradual gate.** `any` includes `error`; `T → any` is
always assignable (though boxing need not be physically cost-free). `any → T` is DEFERRED and
uses a Go-like runtime type assertion: the value must already match `T` under the same
type/subtype rules; the boundary does not perform value-dependent narrowing such as
`float(3.0) → int`. Explicit conversion functions own those conversions.

`any \ error` is the non-error top type. It is the success type produced when channel-agnostic
`^err` destructuring or postfix `^` strips errors from an `any` outcome. The schema validator's
historical `any` pattern is intentionally treated as this non-error validation pattern
(`any \ error`): validation asks whether a value is usable data, while the core language type
`any` remains the true top type. This validator-specific spelling is documented until the
validator gains a distinct surface alias.

---

## 4. Where typed data comes from

Lambda values acquire types from four sources; the enforcement design treats them differently
because their guarantees differ.

**4.1 Literals and expressions — inference, already precise.** Numeric literals, string/symbol
literals, array literals (`ARRAY_NUM` inference), map literals with their shapes. Inference-typed
values are STATIC-PROVEN material; nothing in this proposal weakens inference (and the perf stage
depends on not weakening it — §2.4).

**4.2 Input parsers — typed by the format's syntax.** Surveyed 2026-07-29:

- All numeric text routes through one shared scanner that promotes **int → int64 → decimal
  exactly** (`lambda/input/input-utils.cpp:153`; big integers become `decimal`, never lossy
  doubles). Float syntax (`1.0`, `1e3`, `-0`) becomes `float`.
- **JSON** produces exactly {null, bool, int, float, decimal, string, array, map}
  (`input-json.cpp:81`).
- **Mark** is the richest: adds symbols, `t'…'` datetime, `b'…'` binary, `n`/`m` numeric
  suffixes (`input-mark.cpp:520`).
- **TOML** produces strings/bools/ints/floats/decimals (`input-toml.cpp:518`). *Side-finding:
  TOML datetimes are currently unsupported — a `1979-05-27T07:32:00Z` value falls into the number
  path and yields `ITEM_ERROR` (`input-toml.cpp:611→343→396`). Filed as a side issue; not this
  doc's scope.*
- **XML is the untyped/semi-typed special case:** attribute values and text content are *always*
  `string` (`input-xml.cpp:184`, `:681`) — no inference. With a schema, XML data could be
  precisely typed at parse time; **KIV**, explicitly out of scope here (user decision
  2026-07-29).

**4.3 The consequence that shapes this design.** Format syntax can only ever establish
**primitives and generic containers**. No parser can establish a *user-defined* type — `type
Config = {host: string, port: int}` names a shape that JSON syntax knows nothing about.
Therefore the annotated binding over parsed data,

```lambda
let cfg: Config = input("config.json", 'json')?
```

is **the canonical DEFERRED boundary** — Lambda's `json.Unmarshal` moment — and requires runtime
validation of dynamic data against a user-defined type. That machinery already exists (§5.3);
it is just not wired to bindings.

**4.4 User annotations.** The subject of this document: today they are the *least* trustworthy
source of type information (§5.4 shows them ignored, lossy, or corrupting depending on the
boundary), which is exactly backwards for the Go direction.

---

## 5. Current implementation survey

### 5.1 Static checker map (surveyed)

**Diagnostic plumbing.** All front-end type diagnostics flow through two helpers in
`lambda/runtime/build_ast.cpp`: `record_type_error` (`:952`), which hardcodes
`ERR_TYPE_MISMATCH` — so **every type diagnostic surfaces as E201** — and
`record_semantic_error` (`:982`) with an explicit code. The error-code space already has what a
real checker needs and never uses: `ERR_ARGUMENT_TYPE_MISMATCH=207`,
`ERR_RETURN_TYPE_MISMATCH=208`, `ERR_INVALID_INDEX=213`, `ERR_INVALID_MEMBER_ACCESS=214`,
`ERR_UNDEFINED_TYPE=204`, `ERR_UNDEFINED_FIELD=205` (`lambda/runtime/lambda-error.h:71-99`) are
referenced nowhere in `build_ast.cpp`.

**The one assignability relation that exists.** `types_compatible_with_full`
(`build_ast.cpp:694-736`; `types_compatible` at `:906` is a thin wrapper):

- NULL type on either side → compatible ("unknown types are compatible").
- `ANY` param accepts everything; `ANY` arg passes into any typed param — the code comment says
  *"any arg can pass to typed param (runtime check)"*, **promising a runtime check that does not
  exist**. This comment is the whole gap in one line.
- Numeric×numeric → `lambda_numeric_kind_exactly_embeds` (`lambda/runtime/lambda-number.hpp:346-383`):
  identical kinds; sized-int widening by width/signedness; `INT → {INT64, INTEGER, FLOAT,
  DECIMAL}`; `INTEGER → DECIMAL`; `{F32, FLOAT} → {FLOAT, DECIMAL}`. **FLOAT→INT is rejected**
  (`:379-382`) — the exact-embedding lattice is value-safe by construction.
- Same `type_id`; typed-array annotation compatibility; union arms (only reachable when the
  extended type survives — see below); `number` accepts any numeric.
- It never compares map/element **shapes**, never compares function signatures, never inspects
  `TypeConstrained` predicates.

**Where checks actually run today** (complete list):

| # | Boundary | Site | Notes / escape hatches |
|---|----------|------|------------------------|
| 1 | Call args, value params | `build_ast.cpp:2673-2690` | `types_compatible_with_full` + typed-array fallback; NULL arg type skips silently; loop stops at shorter of args/params, so arity mismatch isn't type-checked here |
| 2 | Call args, `var` (inout) params | `:2660-2670` | `type_exact_match` — stricter, correctly so |
| 3 | Constant sized-int conversion `i8(x)` | `:2596-2612` | overflow check on constants only |
| 4 | Declaration, `[T]` array-arity | `:5124-5141` | only for literal-array RHS |
| 5 | Declaration, typed-array element type | `:5142-5177` | only when annotation is `TYPE_KIND_UNARY` **and** RHS is a literal array |
| 6 | Re-assignment to annotated `var` | `:7995-8027` | ANY/NULL value short-circuits; `NUM_SIZED`/`UINT64` destination accepts *any* numeric source |
| 7 | Fn body vs declared return | `:8379-8392` | **vacuous for braced bodies**: `build_content` types every content block `ANY` (`:8607`), and ANY passes — the check can only ever fire on single-expression bodies |

**Unchecked entirely:** scalar declaration initializers, map/element literal fields vs a named
map type, union annotations on declarations, index expressions, member expressions, `is`
operands. There is no separate type-checking pass over the AST; checking happens (or doesn't)
inline during construction.

**Four structural root causes**, which the design in §7 must remove rather than patch around:

1. **One type slot, wrong build order.** `AstNamedNode::type` holds *either* the annotation *or*
   the inferred type — the annotation overwrites the initializer's type (`build_ast.cpp:5073`),
   and the initializer is built at `:5065` **before** the annotation is even parsed at `:5071`.
   There is no expected-type pushdown and, after the overwrite, nothing left to compare. The
   declaration checker cannot be "added" without first keeping both types alive at the site.
2. **Annotation-ness is `var`-only metadata.** `NameEntry::has_type_annotation` is written at
   exactly one place — `build_var_stam` (`:7830`). Functional `let` bindings don't set an entry
   at all; the MIR backend *re-derives* "was annotated" by comparing types against a numeric
   whitelist (`transpile-mir.cpp:6499-6508`). Enforcement needs the fact recorded, not guessed.
3. **Extended types survive only on parameters.** `TypeParam::full_type` carries union/
   occurrence/map types across the byte-copy at `build_ast.cpp:8087-8107`; declarations have no
   equivalent, which is why union *params* are checked and union *declarations* are ignored
   (t6).
4. **Named-shape adoption replaces instead of checking.** `build_assign_expr:5077-5121` rebuilds
   the literal's `ShapeEntry` chain taking each field's `type` **from the declared shape**,
   silently discarding the literal's inferred field types — no field-by-field comparison exists.
   At runtime `set_fields` (`lambda/core/lambda-data.cpp:667`) then switches on the *declared*
   type and calls `item.get_int56()` on a boxed `String` Item (`:709`), storing tagged-pointer
   bits — the t4 corruption, end to end.

**Backend confirmations for the catalog** (from the same survey): the declaration conversion
ladder (`transpile-mir.cpp:6603-6637`) has branches for NUM_SIZED / UINT64 / INT↔FLOAT
(`MIR_D2I` at `:6618-6623` is t13's silent truncation) / DECIMAL — and **no branch for a
non-numeric source**, so a `String*` register is `MIR_MOV`'d verbatim into an INT-tracked lane
and later boxed as an int (t3's pointer bits). `fn f() int { "abc" }` returns 0 because the
ANY-typed body is unboxed via `emit_unbox(…, LMD_TYPE_INT)` → `it2i` fall-through
(`transpile-mir.cpp:14877-14888`, `lambda-data.cpp:400`). The t2 `var`-vs-`let` difference is
front-end-identical; the observed `null` most plausibly comes from statement-value emission
(`AST_NODE_VAR_STAM`'s own value is `ITEM_NULL`, `transpile-mir.cpp:12139-12147`) — mechanism
plausible but not step-verified.

**`is` and `match` (the runtime test surface):** `is` performs no static operand checking and
lowers to either an inlined constrained-type test (`transpile-mir.cpp:4592-4677` — note the MIR
inline path **does** evaluate `T where …` constraint bodies at `:4601-4627`) or a call to
`fn_is`. That creates a three-way divergence for constrained types: MIR-inlined `is` evaluates
the constraint, `fn_is`'s own constrained branch returns true without evaluating it
(`lambda-eval.cpp:1139-1163`), and the validator rejects constrained types as unsupported
(§5.3). One more reason for TE-6's single relation.

### 5.2 Emitter and runtime-converter map (surveyed)

**The native surfaces.** Params may be carried raw for `INT, FLOAT, BOOL, STRING, INT64, UINT64`
(`is_native_param_type_id`, `lambda/lambda.h:1173-1177`); returns only for `INT, INT64, UINT64,
FLOAT, BOOL` (`mir_is_native_scalar_value_type`, `transpile-mir.cpp:1485-1491`) — with `STRING`
widened *ad hoc* at four call sites (`:2507`, `:2539`, `:8348`, `:11518`). That param/return
asymmetry is TS-2's root, now localized.

**One conversion primitive, no checked variant.** Every representation change funnels through
`emit_unbox` (`transpile-mir.cpp:2112-2160`) — **83 call sites** — whose `default:` arm silently
returns the register unchanged for unknown TypeIds (`:2158-2159`). There is no checked wrapper
anywhere. The boundary-relevant sites:

| Boundary group | Emission sites (17 primary) |
|---|---|
| Call arguments | `:10475` (ANY→native, **no tag test**), `:10480` (mismatch box/unbox), `:10493` (defaults); TCO twin `:10296`, `:10299`; ABI trampoline `:14028`; boxed prologue `:14694` |
| Declaration init | ladder `:6604-6634` (`:6636` DECIMAL arm; `emit_coerce_value_to_declared` `:6605`,`:6609`); reassignment twin `:11517` |
| Returns | explicit `:11314`,`:11318`; implicit body `:14882`,`:14886`; boxed `emit_coerce_boxed_to_declared` `:11327`,`:14907` (acts only for NUM_SIZED/UINT64 — INT/FLOAT/BOOL/STRING pass unchanged, `:2056-2069`) |

Secondary trusting sites needing eventual coverage: global-var load `:2503-2508`/`:2538-2541`,
member-access unbox against declared member type `:8346-8350`, method-`self` field hoist
`:14630-14644`, index-result re-tag `:8422-8432`.

**The call boundary in detail.** Three-arm ladder (`:10464-10502`): same-type passes raw;
ANY/NULL → `emit_unbox` **with no type test**; any other static mismatch → box-then-unbox, also
unchecked. The native-variant callee prologue does *nothing* — the declared type is simply
asserted over whatever bits arrived in the lane (`:14685-14690`). And a **missing argument with
no default is padded with a literal native `0`** (`:10499-10503`).

**The declaration ladder** (`:6604-6634`) handles NUM_SIZED, UINT64, FLOAT←INT (`MIR_I2D`),
INT←FLOAT (`MIR_D2I` — t13's silent truncation), and DECIMAL (box/unbox, the recently added
coercion). **There is no `else`**: `int ← string` falls through and the raw `String*` is
`MIR_MOV`'d into the INT-tracked I64 lane (`:6669-6676`) — t3's pointer bits. `type_to_mir` maps
everything but FLOAT to `MIR_T_I64` (`:355-364`), so MIR itself cannot object (the §0
unprobeability).

**Typed arrays are the one bright spot — with two holes.** `ensure_typed_array`
(`lambda-data-runtime.cpp:2978-3127`) really does check: unsupported element types (`bool[]`,
`string[]`) log and return NULL (`:3124-3126`), and mismatched elements on the generic-`Array`
source path are rejected per element (`:3063-3070` int, `:3084-3090` float, …). The NULL is
turned into a hard fail-stop by `emit_return_item_error_if_zero`
(`transpile-mir.cpp:1763-1776`, wired at `:6589`,`:6596`,`:14731`) — **the only declared-type
boundary in the entire emitter with a real runtime failure path**, and the template for TE-8.
The holes: (1) the ARRAY_NUM→ARRAY_NUM cross-convert path (`:3018-3054`) converts blindly with
no element checks; (2) TS-7 — the unsupported-element case is a *runtime* fail on a construct
the front end accepted, where it should use checked generic-array storage rather than making
the semantic type depend on packed-carrier support. And after the coercion succeeds,
the binding is downgraded to ANY (`:6598-6599`) and — worse — the stored value is a **raw
untagged container pointer**, not a tagged Item; it survives only because `Item::type_id()`
dereferences when the tag byte is 0 (`lambda/lambda.hpp:115-129`).

**Typed element writes are contract-dropping by design.** The declared element type only ever
*selects a fast path*, never a check (`:12333-12337`, `:12527-12533`); guard failure falls back
to `fn_array_set`, not to an error. `fn_array_set` (`lambda-eval.cpp:5784-5889`) handles a
mismatched value by calling `convert_specialized_to_generic` (`:5872-5878`), which rewrites the
array **in place** to a heterogeneous `LMD_TYPE_ARRAY` (`:5776-5778`) and stores the value — no
error, no log. So `var a: int[] = …; a[0] = "abc"` silently dissolves the annotation's
representation. (t8's observed `null`: the ANY-downgraded binding holds a raw untagged pointer,
the boxed-type guard shifts it `>>56` and gets 0, so every read takes the permanent slow lane
whose out-of-bounds arm yields `ItemNull` — `:8379`, `:9184-9198`.)

**Index reads:** out-of-bounds yields `ItemNull`, never an error (`:8379`); the
`BOXED_INT` result mover re-tags whatever was loaded as INT unconditionally (`:8422-8432`);
narrowed-but-unguarded ARRAY_NUM reads are raw memory reads with no check (`:8930-8950`).

**Converter fallback semantics** (`lambda/core/lambda-data.cpp`) — the value a wrong type
silently becomes:

| Converter | Mismatch result | Note |
|---|---|---|
| `it2i` (`:368-400`) | `0`; ERROR → `0`; inexact DECIMAL → `INT64_MAX` | "callers should check type before calling" — none do |
| `it2l` (`:409-435`) | `INT64_MAX` sentinel; **no ERROR arm** | same expression yields `0` or `INT64_MAX` depending on `int` vs `int64` annotation |
| `it2d` (`:316-341`) | `NaN` — comment: *"was 0.0 — silent data corruption"* | **precedent: this fallback was already fixed once, in the poison direction** |
| `it2b` (`:343-366`) | no failure value — objects are `true` | |
| `it2u` (`:444-453`) | `0`, raw C casts | |
| `it2s` (`:455-466`) | **`nullptr` handed to JIT'd code as a native STRING lane**; ERROR → static `"<error>"` | a `len()` on that lane is the TS-2 segfault family |

**Available trap machinery** (what TE-8/TE-9 can build on): the `ITEM_ERROR` value lane and
`emit_return_item_error_if_zero` (the ensure_typed_array template); `emit_return_if_item_error`
(`:1778-1790`); the `T^E` dual-lane return ABI (`RETURN_LANE_ERROR` epilogue `:1042-1063`) —
with the **key structural constraint** that a function whose `TypeFunc::can_raise` is false has
*no error lane at all*, and `emit_function_error_return` then degrades to returning
`ITEM_ERROR`'s bit pattern in the native lane (`:984-987`); `set_runtime_error_no_trace`
(extern "C", JIT-callable, `lambda-eval.cpp:116-133`) with `ERR_TYPE_MISMATCH` already used for
runtime operand errors (`lambda-eval.cpp:336-443`); `fn_error` registered in the JIT symbol
table (`sys_func_registry.c:538`). And one near-miss: `ts_assert_type`
(`lambda/ts/ts_runtime.cpp:124-145`, registered at `sys_func_registry.c:3010`) is a runtime
type assertion **already in the symbol table** — but soft by design (logs and returns the value
unchanged, "matches TS semantics") and never emitted by the Lambda MIR path. Lambda needs its
hard sibling.

### 5.3 Validator assets — what enforcement can reuse (surveyed)

The schema validator is the big reusable asset, and the survey's headline is that **no
parallel type-graph bridge is needed**. Runtime value-layout conversion is a separate question:

- **One `Type` representation, shared.** The validator consumes the exact `Type` hierarchy the
  AST builder produces for script `type X = …` definitions — `TypeMap`/`ShapeEntry`
  (`lambda/lambda-data.hpp:321`/`:302`), `TypeArray` (`:265`), `TypeElmt`, `TypeBinary`,
  `TypeUnary`, etc. Schema `.ls` files are loaded through the *real transpiler front end*
  (`SchemaValidator::load_schema`, `lambda/validator/doc_validator.cpp:201`), and the former
  parallel `TypeSchema` model is deleted (`validator.hpp:22-26`). Named references nested inside
  a definition are already resolved to direct `Type*` pointers at AST-build time
  (`build_ast.cpp:2880-2886`).
- **A runtime-callable `(Item, Type*)` entry point exists and is already used by the language.**
  `SchemaValidator::validate_type` (`doc_validator.cpp:536`), C wrapper
  `schema_validator_validate_type` (`doc_validator.cpp:776`). The `is` operator (`fn_is`,
  `lambda/runtime/lambda-eval.cpp:1115`) already calls it from JIT'd code for shapes, arrays,
  unions and occurrence types (`lambda-eval.cpp:1294` among others), against the per-context
  validator instance (`EvalContext::validator`, `lambda-data.hpp:112`, created at
  `runner.cpp:1448`). Runtime enforcement of user-defined types is therefore an *emission*
  question, not an infrastructure question.
- **Capabilities today:** primitives (exact tag), a numeric embedding lattice
  (`validator_internal.hpp:159-229`), map shapes with required/optional fields, arrays with
  element types, tuple patterns, occurrence modifiers (`?`/`+`/`*`/counts), unions (first-match,
  flattening cap 32), intersections/exclusions, anchored regex string patterns, element types
  with attributes.
- **Gaps that matter for enforcement duty** (each becomes work in §7/§8):
  - Maps are validated **open only** — extra fields are never detected; `allow_unknown_fields`
    and `strict_mode` are parsed and stored but read by zero validation functions
    (`validate.cpp:428` iterates only the schema's `ShapeEntry` chain).
  - `TypeConstrained` (`T where …`) falls to "Unsupported type" in the validator
    (`validate.cpp:661-664`), and `fn_is` **returns true after checking only the base type** —
    the constraint body is never evaluated (`lambda-eval.cpp:1139-1163`, TODOs at `:1158,:1161`).
  - Nominal `TypeObject` checks (inheritance walk, `constraint_fn`) exist **only** in `fn_is`
    (`lambda-eval.cpp:1267-1288`), not in the validator.
  - Numeric subsumption is implemented twice — `numeric_type_subsumes` in `fn_is` vs
    `validator_numeric_type_embeds` in the validator (`validator_internal.hpp:181`) — with a
    comment *claiming* they agree. TE-6 unifies.
  - Validation failure detail is invisible at runtime: `print_validation_result` writes to
    `log_debug` only (`error_reporting.cpp:346`); `fn_is` discards the error list. Fine for a
    boolean `is`, not fine for a failed binding — TE-4 needs the error path surfaced.
  - `input()` has **no schema hook**: its options map recognizes exactly `type` and `flavor`
    (`lambda-eval.cpp:2988,:3004`); the natural insertion point for an optional `schema:` key is
    right there (`:2981-3018`).

### 5.4 Measured behavior catalog (2026-07-29, release build; repro in `temp/tsd_t*.ls`)

The same declared type `int` meets a wrong value at seven different boundaries and produces
**five different silent failure modes and two correct rejections**:

| # | Boundary | Program | Result today | Failure mode |
|---|----------|---------|--------------|--------------|
| t14 | call argument, static | `fn f(x: int) {x}` … `f(3.5)` | `error[E201] argument 1 expected int, got float` | ✅ STATIC-REJECTED |
| t15 | re-assignment, static | `var x: int = 1` … `x = "abc"` | `error[E201] cannot assign string value to var 'x' of type int` | ✅ STATIC-REJECTED |
| — | call argument, dynamic | `a(xs[1])`, arg is `any` holding `"abc"` | returns as if `b` were `0` | silent **0** (`it2i` fall-through) |
| t1 | declared return, static | `fn f() int { "abc" }` | compiles; `f()` → `0` | silent **0** |
| t11 | declared return, dynamic | `fn f() int { g() }`, g returns `any` string | `f() + 1` → `1` | silent **0** |
| t3 | `let` declaration, static | `let x: int = "abc"` | `x` → `4563730592` | **raw `String*` pointer bits as int** |
| t13 | `let` declaration, static | `let x: int = 3.5` | `x` → `3` | silent **lossy truncation** (same conversion E201-rejected at t14!) |
| t2 | `var` declaration (pn), static | `var x: int = "abc"` | `x` → `null` | silent **null** (different path than t3!) |
| t10 | `let` declaration, dynamic | `let x: int = d.a`, JSON string | `x` → `"hello"` | annotation **ignored entirely** |
| t4 | named map type, literal | `type P = {x: int, y: int}; let p: P = {x: "abc", y: 2}` | `p.x` → `4832166160` | **pointer bits through a typed field** |
| t16 | named map type, dynamic | `let q: Q = d` (JSON map, field is string) | `q.a` → `0` | no validation at bind; silent 0 at field read |
| t8 | `int[]` element write | `var a: int[] = [1,2,3]; a[0] = "abc"` | `a[0]` → `null`, clean exit | silent **null** into typed lane |
| t6 | union annotation | `let x: int \| string = 3.5` | `x` → `3.5` | annotation ignored |
| t5 | `is` operator | `[1 is int, "a" is int, 3.5 is float, null is int]` | `[true, false, true, false]` | ✅ correct — the runtime *can* check |
| t7 | `as` cast | `3.5 as int` | syntax error | no cast operator exists |
| t9 | over-range int literal | `let x: int = 9007199254740993` | `error[E108] integer literal outside compact int range` | ✅ literal-range diagnostics exist |

Reading of the table:

- Exactly **two** boundaries are enforced (call arguments, re-assignments) — both statically,
  neither dynamically.
- The **declaration** family (TS-1) is the worst offender, with *three distinct* wrong behaviors
  for the same program shape (`let` → pointer bits, `var` → null, dynamic initializer → ignored).
  The pointer-bit leaks (t3, t4) are the §0 representation corruption made user-visible: the
  annotation selected an int representation, nothing enforced it, and a `String*` was
  reinterpreted.
- The static checker and the dynamic boundary **disagree in both directions**: `f(3.5)` is
  rejected statically but `let x: int = 3.5` truncates silently; `null is int` is `false` at
  runtime while `types_compatible` has a NULL escape hatch (§5.1).
- `is` (t5) proves the enforcement primitive already works — the runtime can decide these
  questions correctly today; it is simply never asked at the boundaries that matter.

---

### 5.5 Sys-function argument conventions (surveyed 2026-07-29)

Sys-function calls receive **no static argument checking** — the E201 argument check covers user
`fn`/`pn` only (it is guarded on `LMD_TYPE_FUNC`), and the registry's `first_param_type` field
(`sys_func_registry.c:250`) exists solely to disambiguate method-style dispatch, not to check
(it is `LMD_TYPE_ANY` for most entries). `len(123)`, `abs("abc")`, `sum("abc")` all compile
clean. At runtime there is no single convention — four coexisting patterns, all measured or
read:

1. **Silent wrong value** — `fn_len` returns `0` for scalars/null (`lambda-eval.cpp:3690`,
   `default:` arm), and `INT64_ERROR` — a sentinel in a native int lane — for error input.
   `[len(123), len(3.5), len(null)]` → `[0, 0, 0]`, and the registry's declared `&TYPE_INT`
   return means that `0` flows on as a statically-valid int.
2. **`set_runtime_error(ERR_TYPE_MISMATCH, …) + return ItemError`** — the good pattern
   (`fn_join`, `lambda-eval.cpp:336-446`), but rare: ~31 `set_runtime_error` sites against 88
   `return ItemError` sites in `lambda-eval.cpp`.
3. **`log_error` + `ItemError`** — `abs("abc")` logs `abs not supported for type: 13` (a raw
   TypeId, not even a name) and the script dies with `Script execution failed`.
4. **Naked `ItemError`** — `sum("abc")` (`fn_numeric_fold`) returns an error item carrying no
   message at all; the script dies with *zero* explanation.

Because these functions are `can_raise=false`, the error item just travels the value lane until
something surfaces it; `can_raise=true` (and with it the `T^E`/E228 handling discipline) is
reserved for I/O functions (`sys_func_registry.c:523`). So sys funcs are already "errors as
values" in shape — but mostly *anonymous* errors, which is the diagnosability failure TE-9's
diagnostic-carrying error values are designed to end. B13 records the boundary; enriching the registry with per-parameter
types (it carries only `first_param_type` today) is the prerequisite for static checking here,
and is left as an open question (§10).

## 6. Boundary inventory and gap analysis

Every place a declared type meets a value, with today's status and the TE-2 target outcome.
"static" = front-end check; "dynamic" = runtime check when the static type is ANY/unknown.

| # | Boundary | Static today | Dynamic today | Target |
|---|----------|--------------|---------------|--------|
| B1 | Declaration init, scalar (`let`/`var x: T = e`) | none (TS-1) | none — ladder falls through (pointer bits) or lossy (`D2I`) | check + checked convert |
| B2 | Declaration init, typed array (`x: int[] = e`) | literal-element check only (`:5142`) | `ensure_typed_array` — real errors, but cross-convert hole + ANY/raw-pointer downgrade | keep; close hole; keep the contract on the binding |
| B3 | Declaration init, named map / union type | none | none — shape adopted unchecked (t4), or annotation ignored (t16) | field check for literals; validator for dynamic |
| B4 | Re-assignment to annotated `var` | ✅ checked (`:7995`) | unchecked unbox (`:11517`) | checked boundary before commit |
| B5 | Call arguments | ✅ checked (`:2673`) | unchecked unbox ×7 sites; missing arg → native `0` | checked boundary; arity is a compile error |
| B6 | Declared return type | vacuous (ANY body defeats `:8379`) | unchecked unbox ×6 sites | per-return static check + checked boundary |
| B7a | Typed container element write (`a[i] = v`) | none | `fn_array_set` silently degrades the array in place | check before mutation; failure leaves the array unchanged |
| B7b | Map member/index write (`m.x = v`, `m[k] = v`) | none — builder records the left side as ANY | field setter dynamically retags/reshapes without preserving an annotated root contract | annotated root: check the post-state against its binding type before commit; unannotated root: permit shape evolution; every committed map keeps an exact runtime shape |
| B8 | Typed member read (declared field type) | n/a | unchecked unbox of `fn_member` result (`:8346`) | trust only a proven typed layout; otherwise read through the value's runtime shape |
| B9 | Parsed input → annotated binding | none | none | same as B3 — the canonical DEFERRED boundary |
| B10 | Global/module var round trip | n/a | store boxes by declared tid; load unboxes trusting it | trusted, *provided* B1 enforces the store side |
| B11 | `is` / `match` / constrained types | no operand checks | three divergent implementations (§5.1, §5.3) | shared subtype/match foundation (TE-6) |
| B12 | The `it2*` converter family | n/a | six different silent fallback values (§5.2) | boundaries stop calling them unchecked |
| B13 | Sys-function arguments | none — registry `first_param_type` is dispatch-only metadata | per-function ad hoc: silent value, logged `ItemError`, or message-less `ItemError` (§5.5) | registry-driven static check; TE-9-quality diagnostics on runtime failure |

The pattern: **static checking exists exactly where someone once added it (B4, B5) and nowhere
else; dynamic checking exists exactly once (B2's coercion) and its result is then thrown away.**
Everything else trusts.

**B7b map-member assignment is a separate correctness boundary, not a variant of array
assignment.** The live builder creates `AST_NODE_MEMBER_ASSIGN_STAM`, gives its synthetic left
member `TYPE_ANY`, and performs no lookup against the root binding's declared `Type*`
(`build_ast.cpp:7916-7951`). The runtime setter already distinguishes same-physical-type stores
from type-changing stores: `fn_map_set` rebuilds a fresh `TypeMap` and repacks the data for the
latter (`lambda-eval.cpp:6798-6909,7221-7227`), while `map_set_cow` detaches shared maps
(`:6682-6693`) and the MIR assignment path installs the replacement back into the owning root
(`transpile-mir.cpp:12662-12699`). Enforcement must preserve that shape-evolution model rather
than freezing every map at its initialization shape.

There are two independent invariants:

1. **Binding invariant.** If the mutable root is annotated — `var p: Person = ...` — every
   post-state of `p`, including one produced by `p.age = v` or a nested/computed-key update,
   must conform to `Person`. `var` licenses rebinding, not type drift. A statically known
   violation is STATIC-REJECTED; a genuinely dynamic value/key is checked before commit.
2. **Map-layout invariant.** The resulting map's exact runtime shape must describe how each
   field is physically stored. A legal `int → string` field transition therefore creates or
   selects a new shape and repacks the map; it never writes string bits under an `int`
   `ShapeEntry`. The semantic binding type may be broader than that exact shape.

Those invariants produce these cases:

```lambda
type Person = {name: string, age: int}
type FlexiblePerson = {name: string, age: int | string}

var q = {name: "Ana", age: 30}
let before = q
q.age = "very old"                 // legal: q evolves to a new inferred map shape
                                    // before remains {name: "Ana", age: 30}

var p: Person = {name: "Ana", age: 30}
p.age = "very old"                 // compile error: the post-state is not Person

var f: FlexiblePerson = {name: "Ana", age: 30}
f.age = "very old"                 // legal: new exact shape, still FlexiblePerson
```

For a named literal key, the checker resolves the field through the annotated root's full
semantic type. For a computed key or dynamic value, the runtime checks the proposed post-state
against that same root contract. An unknown key is accepted only when the annotated map type is
open; the runtime extends the exact shape with a slot matching the stored value. A future
closed-map form rejects it. An unannotated root has no binding-contract check: its exact runtime
shape evolves with legal writes, and the binding's effective inferred type is updated or
honestly widened.

The operation is transactional. Evaluate and snapshot the right-hand side first, establish
that the resulting root value satisfies any annotation, then commit the COW replacement. On
mismatch, no replacement is installed, no field bytes or shape change, and the assignment
produces the rich error value. Nested writes apply the rule to the resulting root, not merely
to the final physical slot. A direct unboxed store is legal only after the semantic check and
the exact-layout proof both succeed. A uniquely owned container may be updated in place as an
unobservable COW optimization; observable semantics remain
`p′ = { *: p, field: value }`.

---

## 7. Enforcement design

### TE-6 — One subtype model, distinct operations

Use one shared type-walking/numeric foundation, but expose three deliberately distinct
operations:

1. `subtype(S, T)` — the static relation used by TE-2. It extends
   `lambda_numeric_kind_exactly_embeds` with shape/union/occurrence walking.
2. `matches(item, T)` — runtime membership used by DEFERRED checks and `is`/`match`; it asks
   whether the value's actual runtime type is a subtype of `T`.
3. `checked_convert(item, T)` — explicit conversion semantics owned by conversion functions,
   not implicitly by an annotated boundary.

This is Go-like: if an `any` holds `float(3.0)`, binding it to `int` fails the runtime type
assertion; the boundary does not inspect the magnitude and silently turn it into an int.
Value-dependent conversions remain available through explicit functions. Lossless
representation conversion after a successful subtype/match — for example, placing an `int` in
a proven `float` carrier where the numeric model says it exactly embeds — is an emitter detail,
not a fourth type relation.

The shared foundation consolidates `numeric_type_subsumes` (`fn_is`),
`validator_numeric_type_embeds` (validator), and
`lambda_numeric_kind_exactly_embeds` (checker), while keeping the validator's documented
`any`-means-`any \ error` policy at its validation boundary. `FLOAT→INT` is never a subtype,
which removes the declaration ladder's `MIR_D2I` arm (t13).

### TE-7 — Complete the static layer (fix the four root causes)

1. **Keep both types alive at declarations.** Reorder `build_assign_expr`: resolve the
   annotation first, build the initializer, run `subtype(init_type, declared_type)`, *then*
   store the declared type. Statically-known-wrong initializers become STATIC-REJECTED
   (`var s: int = "abc"` joins `x = "abc"` as E-errors). This closes TS-1.
2. **Record annotation-ness for every binding**, not just `var` — a `declared_type` that
   survives on the node (today's single overwritten slot is root cause 1; the emitter's
   whitelist re-derivation at `transpile-mir.cpp:6500-6508` is the workaround to delete).
3. **Extended types survive on declarations** the way they do on params (`full_type`
   equivalent), so union and occurrence annotations are checkable at declarations (t6).
4. **Named-shape adoption checks before it adopts**: field-by-field `subtype(literal field,
   declared field)` in `build_assign_expr:5077-5121`, with missing-required-field diagnostics.
   Open named types accept and preserve extra fields; a future closed form diagnoses them.
   Closes the t4 corruption at the front door.

Plus: per-return-site checking against the declared return type (the `:8379` check is kept but
no longer the only line of defense — each `return e` / final body expression is checked where
its type is known, killing the vacuous-ANY hole for the static half); call arity becomes a
diagnostic (today's argument loop stops at the shorter list and the emitter pads `0`).
`bool[]`, `string[]`, and other semantically valid typed arrays remain legal using checked
generic-array storage; lack of a packed `ArrayNum` carrier is an optimization limitation, not a
type-system error. Add B7b's annotated-root member-write check at the same static layer:
statically prove or reject the root's post-state, while leaving unannotated shape evolution
legal.

Diagnostics stop funneling everything into E201: the dormant codes exist and get used
(`ERR_ARGUMENT_TYPE_MISMATCH=207`, `ERR_RETURN_TYPE_MISMATCH=208`, `ERR_UNDEFINED_FIELD=205`,
…, `lambda-error.h:71-99`).

### TE-8 — The checked boundary (the DEFERRED half)

One boundary primitive, conceptually
`emit_checked_boundary(mt, reg, expected_type, site)`, replaces bare `emit_unbox` at the
boundary sites. It accepts the full `Type*`/type-list reference, not only a TypeId, so unions,
shapes, occurrences, and named types remain expressible. Emission shape:

```
actual = item_type_id(item)                 // canonical tag-0-aware query
if fast_simple_match(actual, expected) → establish proof and convert carrier if needed
elif item matches expected_type        → establish proof; preserve/normalize carrier as required
elif actual == error and T admits error → pass the error as a value
else                                   → lambda_type_error(expected, item, site)
```

The runtime slow path returns either a proof-backed value suitable for the chosen carrier or a
proper diagnostic error object; it never returns `0`/`NaN`/`nullptr` on mismatch. The `it2*`
family remains available to pre-verified internal callers, but annotated boundaries stop being
routed through it unchecked.

Fast-path cost is one predictable branch on bits already in a register — the same shape as the
guards the IC machinery already emits everywhere. The perf gate in §8 holds it to noise on the
typed benchmark column.

Correctness does not depend on where a later backend chooses to place or deduplicate the check.
A boxed-entry prologue is the likely implementation for dynamic calls, while declarations,
returns, and writes remain site-local. TE-14 records this as a performance/implementation
direction rather than a semantic prerequisite.

### TE-9 — Failed checks produce error **values** (decided 2026-07-29; supersedes rev 1's panic recommendation)

Three options existed; two are rejected by decision:

1. **No panic mode.** Lambda's semantics must stay friendly to batch processing — one bad
   record in ten thousand must not kill the run. A type failure is a per-item outcome, not a
   process-fatal event.
2. **No forced `T^E`.** `T^E` is the *user's* declared channel; conscripting it for implicit
   checks would virally impose `^err`/E228 handling on every caller — error checking spilled
   all over Lambda code.
3. **Therefore: error return values.** A failed DEFERRED check yields a diagnostic-carrying
   error value in the value lane — Lambda's existing errors-as-values model (and the Jube
   C-ABI principle: errors as return values everywhere). At an annotated destination, that
   error is the boundary operation's result; it is not stored in the declared `T`.

**Annotated and inferred outcomes stay distinct.**

- `let x: T = e` checks `e`; success establishes `x: T`, while failure produces the error
  outcome before `x` exists.
- `let x = e` establishes `x` with `e`'s inferred effective type, including `T | error`.
- A statically proven call to `fn a(b: int) int` has result `int`. A dynamically checked call
  is open at the call boundary because parameter validation can fail, so that call expression
  has effective type `int | error`; on success the callee body still sees `b: int`.
- A declared plain-`T` return remains an effect firewall: an open body is a compile error,
  resolved by containing, disclosing `T | error`, or imposing `T^`.

Boxed storage is therefore required for inferred/open outcomes, not for a successfully
established annotated `T`. A future native variant may exploit the clean proof, but the semantic
rule does not depend on that optimization.

**Error short-circuit rule.** An error value arriving at any subsequent DEFERRED boundary
becomes that boundary's result without further checking — "error in, error out", the
`GUARD_ERROR1` convention generalized. Propagation is therefore *implicit*; containment is
*explicit* and placed where the user chooses (`let x^err = …`, postfix `^`, `or`,
`x is error`). This is what keeps the no-spill promise: nothing forces handling for an inferred
`T | error`, and errors surface at the batch boundary where the user aggregates results.

**Sys functions.** The normative convention (making §5.5's pattern 2 the rule): acceptive on
input types, **error value returned for invalid types** — never silent wrong values, never
message-less deaths. The future perf split mirrors user functions: per-func clean/open versions
(`len(non_error_data) int` vs `len(any) int | error`), selected by the caller's statically-known
argument types, with registry metadata driving the selection.

Open results travel boxed, while a backend may use native lanes where cleanness and carrier
proofs establish that no error can occupy the lane. The exact entry/return ABI is deferred to
the implementation/performance stage (TE-14).

For validation failures at named-type boundaries (TE-10), the error value carries the
validator's path detail (`.field[3]: expected int, got string`) — which requires surfacing
`ValidationError` detail that today dies in `log_debug` (§5.3).

### TE-10 — User-defined types: the validator becomes the runtime enforcer

At a DEFERRED B3/B9 boundary (`let q: Q = <dynamic>`), emit a call to the **existing** entry
point `fn_is` already uses: `schema_validator_validate_type(ctx->validator, item,
const_type_with_tl(type_index))`. On failure → a TE-9 error value carrying the path detail,
with no `q` binding established.

No **type-graph** bridge is needed: schema types and script types are the same `Type*` graph.
That does not imply that a parsed map already has the declared map's packed physical layout.
Validation initially establishes the semantic named-type contract while reads continue through
the value's runtime shape. Canonicalizing/repacking a validated generic map into a declared
layout — including preservation of open extra fields — is a separate implementation/performance
decision required before direct-offset field access can use that layout.

- **Depth: deep, on first crossing.** The value either satisfies `Q` in full or the boundary
  yields a rich error without establishing the binding. (Witness caching to skip re-validation
  of already-validated subtrees is perf-stage work; correctness first.)
- **Openness: named map types are open by default — DECIDED 2026-07-29 (user).** Extra fields
  pass, matching the validator's current behavior, Go's unmarshal, and structural-width
  subtyping. A closed form (wiring the already-parsed `allow_unknown_fields`/`strict_mode`
  flags to actual checks) remains a possible future opt-in.
- **Constrained types (`T where …`) — DECIDED (§10.14)**: the validator is the checker and will
  eventually evaluate constraint predicates (the MIR-inlined `is` already does). The accepted
  interim scope is deliberately simpler: enforcement validates only the base `T`, and the
  predicate refinement remains a clearly documented validator follow-up. This does not block
  the base type-enforcement rollout.
- Nominal `TypeObject` checking (today `fn_is`-only) reuses the shared subtype/match foundation.

### TE-11 — Null policy

Today `null` passes `types_compatible`'s escape hatch statically, while `null is int` is
`false` at runtime — the two halves disagree. **Decision: plain `T` does not admit `null`;
optionality is spelled `T?`** (the occurrence form already exists in the grammar and the
validator honors it via `is_type_optional`). ANY/NULL boundary values hitting a plain-`T`
DEFERRED check yield an error and do not establish the binding. **DECIDED 2026-07-29 (user):
ships in P0** together with the rest of the
static layer — no warn-only interim release. Migration risk is handled by the P0 baseline gate:
any code relying on null-through-annotation surfaces there and is fixed with `T?`.

### TE-12 — What enforcement deliberately does not touch

Inference stays authoritative where there is no annotation (§4.1) — enforcement never inserts
checks on inferred-only paths. TS-9 (int→float overflow) stays as specified in
`Lambda_Formal_Semantics.md` §4.1. The COW/borrow semantics of map-typed locals (TS-4/C4), the
region-producer gate (TS-6), the ANY-downgrade fast-path losses (TS-3), and the dead
direct-field-offset path (TS-5) are all perf-stage items — though note TE-10 finally gives
named map annotations a *meaning* (a validated contract), resolving TS-5's "cost without
benefit" in the semantic direction. TE-3's local carrier proofs, plus an eventual layout
canonicalization decision, are what can later make the TS-3/TS-5 fast paths safe to re-enable.

### TE-13 — Unified discharge surface over the two error forms (revised 2026-07-29, user)

**Two error forms, distinguished by obligation — different types, not one type.** (This
revises the earlier draft's "one type, two provenances" identification.)

1. **`fn a() int^` — the enforcing/originating channel.** Raise-capable; callers **must**
   handle (E228). The `^` spelling is **explicit-only — inference never produces it.**
2. **`fn b() int | error` — the non-enforcing union.** Error is simply one of the possible
   values; it flows freely with no call-site obligation. **This is the form openness inference
   produces**, and it is the form the formal semantics already assigns to system-fn value
   channels (§7.3: "`T?` or `T | error`, never `T^E` on a system fn").

In **value positions** (bindings, parameters), the two spellings are **equivalent** — decided
2026-07-29: `let x: int^ = a()` and `let x: int | error = a()` mean the same thing; the binding
carries the outcome, value or error. `^` is semantically distinctive only as a *function
return* marker, where it adds the enforcing obligation and the `raise` license.

The provenance principle thereby becomes *syntactically manifest*: `^` is always a user-written
explicit fact, enforced strictly; `| error` is the inferable fact that flows. **`raise` goes
with `^` — DECIDED 2026-07-29 (user).** `raise` is licensed only by a declared `^`; in a
`T | error` function, user code constructs `error(...)` and **returns** it as an ordinary
value — `raise` there is a compile error (the existing plain-`T` raise restriction extends to
the union form). Channel ↔ verb: `^` raises, `| error` returns — exactly §7.3's "fn return
error; pn raise error" given surface spellings.

**Acknowledgment forms (DECIDED 2026-07-29 — generalization ratified): must-handle =
must-engage-explicitly, at the immediate expression.**
E228 guards against *unawareness*, not against deferred branching — so it is satisfied whenever
the enforcing call's result is received by a context that **textually** engages the error
possibility:

1. `^err` destructuring, or postfix-`^` propagation (today's two forms);
2. a binding, parameter, or declared return whose explicit type admits `error` — `T | error`,
   `T^` (equivalent in value positions), or `error` itself. The binding carries the outcome as
   a union, and ordinary assignability already enforces engagement-before-plain-`T` for both
   spellings — the §7.3 wrapper idiom in one step, and the batch idiom for collecting
   enforcing-call outcomes. The declared-return form counts **only for the call in return/tail
   position** (`return f()`, or the final expression) — see tightness below. **`any` never
   counts** — it admits error but acknowledges nothing;
3. a `match` with a `case error:` arm.

Bare `let x = a()` remains an error — Lambda stays stricter than Zig/Rust/Go, all of which
accept an untyped capture. The §10.7 firewall backstops the demotion path: an acknowledged
error still cannot silently escape a declared plain-`T` interface. Implementation: one added
condition at the existing E228 site (`build_ast.cpp:5197` — "declared type explicitly admits
error") plus a third suggestion in the diagnostic text.

**Acknowledgment tightness (DECIDED 2026-07-29, user): tight everywhere for `^` — keyed on the
form, not on fn vs pn.** The acknowledgment must be the **immediate expression surrounding the
call**; never distant, never scope-level. In particular, a declared error-admitting return does
*not* retroactively acknowledge non-tail calls:

```lambda
pn p() T | error {
    f()        // compile error if f is enforcing — discarded outcome, no acknowledgment
    g()        // same — and f's unhandled failure could be the very cause of g's
    return r   // the declared return covers only THIS expression, not the calls above
}
```

Rationale: the `^` channel is *designed* to demand explicit acknowledgment — primarily for pn
code, where an unengaged failure invalidates every subsequent command (§7.3: "commands halt on
failure"); fn may use `^` too, under the same discipline. No laxer rule for fn is needed,
because the relaxed pattern **already has its own spelling**: a callee that wants callers to
write `let a = b()` and let errors flow simply declares `T | error`. The two forms are the two
intended design patterns — the callee author chooses the caller discipline by choosing the
form:

| Pattern | Declare | Caller experience |
|---|---|---|
| Urgent — must acknowledge | `T^` | engage at the immediate expression, everywhere |
| Relaxed — value-flowing | `T \| error` | `let a = b()` is fine; the error propagates as a value |

**The soft form's contract (decided 2026-07-29, user): flow through the interior, detect at
the type boundary.** Softness is *desirable* for fn code — pipelines, batch processing,
for-loops, composed containers — because one returned error cascades like a normal value and
**does not abort** the surrounding computation. The cost, *by design*: the error is likely
embedded in the eventual result rather than surfacing upfront. The mitigation is the type
system itself — typed containers exclude error by element type (§10.6), so a type pattern
harvests embedded errors at whatever boundary the user chooses. All three legs verified live
2026-07-29:

```lambda
[123, error("m"), 456]              // → [123, error, 456] — composition holds the error
for (x in [1, "abc", 3]) int(x)     // → [1, error, 3]     — one bad element, batch continues
type IA = int[]
[1, error("m"), 3] is IA            // → false             — the type pattern detects it
[1, 2, 3] is IA                     // → true
```

So softness and enforcement are complementary halves, not a tension: errors flow freely
through the interior, and are detected exactly where the user asserts a clean type — an `is`
pattern, a `match` arm, an annotated (DEFERRED) binding, or the §10.7 firewall at a declared
interface. *Side-finding (grammar-checked 2026-07-29):* the `is` RHS is plain **expression space** — `is`
is a generic `binary_expr` table row with `operand = $._expr` (`grammar.js:76`, `:41-42`) —
which is why its type forms are restricted by design (bare identifiers, base types, `[T]`
literals; anything richer would collide with legal postfix parses: `x is int[3]` already
parses as `(x is int)[3]`). Two in-grammar precedents show type-space RHS is workable where
unambiguous: **`match` case patterns take the full `$._type_expr`** (`grammar.js:763`) —
`case int[]:` works *today* and detects embedded errors (verified:
`match(v){ case int[]: "clean" default: "has non-int" }` → `["clean", "has non-int"]`) — and
the query operator takes `$.primary_type` (`:489`). **Noted only — OUT OF SCOPE for this
proposal (user, 2026-07-29): supporting `x is (int[])` belongs to the pattern-grammar and
validator design.** Hand-off note for that effort: the parenthesized form is strictly additive
(`(int[])` is a syntax error everywhere today); mechanics would be a dedicated `is` rule with
RHS `choice($._expr, seq('(', $._type_expr, ')'))`, one GLR conflict at the paren boundary
(contents valid in both spaces, e.g. `(int)`) resolved by preferring the type parse — semantics
coincide, since the AST already resolves identifier exprs to types. Direct unparenthesized
`is int[]` should stay off the table (it would re-parse the currently-legal
`(x is int)[…]` postfix form). Until then the spellings are: a named type, or a `match` arm.

**`or`-rescue — RESOLVED 2026-07-29: no rule-bend needed; error-consuming `or` is already both
the spec and the implementation.** Specified three times over — truthiness (errors are falsy,
`Lambda_Formal_Semantics.md` §3), §7.3 ("both are falsy, so `f(x) or default` rescues both
uniformly"), and `Lambda_Error_Handling.md`'s falsy-errors section (`divide(10, x) or 0`) —
and verified live 2026-07-29: `int("abc") or 0` → `0`, `error("boom") or 5` → `5`,
`f(0) or 5` → `5` (user-fn div-by-zero), `if (int("abc"))` → falsy. The coherence argument:
`or` *is* truthy-select, and errors are falsy, so consumption follows from the definitions —
error-propagating `or` would require either error-truthy (absurd: `if` would enter) or a
special-cased `or` that breaks the identity. Two safety properties observed: **`0` is truthy**
in Lambda, so `int(s) or 0` has no JS-style zero-swallow footgun; and errors log at
origination (`runtime error [318]: boom`), so an `or`-consumed diagnostic leaves a `log.txt`
breadcrumb even though the program never sees it. `a() or 0` accordingly counts as E228
engagement (textually explicit consumption handling both branches). Pitfall documented
2026-07-29 in `Lambda_Error_Handling.md` §"Error Truthiness": bare `error` in expression
position is the **type** (truthy, not an error value) — `error or 0` → the type,
`error is error` → `false`, `error is type` → `true`; the `if`-condition lint catches the
`if (error)` form but not the `or`-operand form (lint-extension candidate).

**The `or`-typing rule (required by P0).** For the idiom to survive strict declarations, the
static type of `a or b` must narrow the falsy poison/absence members out of the left side:
`type(a or b) = (type(a) − {error, null}) | type(b)` — plain union arithmetic, no flow
analysis. Then `let n: int = int(s) or 0` types as `int` and passes the P0 checker; without
this rule the new strict declarations would reject the spec-blessed idiom.

**Operators are value-directed, not declaration-directed.** All the existing forms operate on
the error-ness of the value, so they work identically over both error forms:

| Form | Semantics (unchanged) | On `T \| error` (incl. inferred) |
|---|---|---|
| `let x^err = e` | on error: `x = null`, `err` = the error; else `err = null` | identical — the discharge point |
| `e^` (postfix) | unwrap success or return the error from the enclosing function | identical — makes the enclosing fn open (or rides its declared channel) |
| `^e` / `e is error` | boolean test | identical |
| `e or default` | errors are **falsy** → default idiom | identical — the batch one-liner: `a(xs[i]) or 0` |
| `raise` | requires a *declared* `T^` | **invalid** — a `T \| error` fn *returns* `error(...)` as a value; only `^` licenses `raise` |

Emission differs invisibly by callee kind — can_raise dual-lane reads the side channel, boxed
open results tag-test the Item — but the surface must never reveal which.

**Destructuring across mixed channels (decided 2026-07-29, user).** `^err` is **total over
error-ness and channel-agnostic**: it splits the outcome by whether the *value* is an error,
regardless of which channel delivered it — the enforcing `^E` channel, an error type inside
the value union, or an error hiding inside `any`. The two boundary cases that pin the rule:

```lambda
fn a() T | e1 ^ e2      // legal syntax (nobody would write it, but allowed):
                        //   union error e1 AND channel error e2
let b^err = a()         // b : T          — every error constituent stripped
                        // err : e1 | e2  — errors from BOTH channels land in err

fn c() any ^ e2
let b^err = c()         // b : any \ error, guaranteed non-error
                        // err : error | e2 — errors arriving via the any value or the channel
```

The general typing rule: `type(b)` = the success constituents of the source type (`T` in the
first case; `any \ error` in the second); `type(err)` = the source's error constituents ∪ the
channel's error type (∪ `null` on success). Note `any \ error` is exactly what the validator's
`any` already means — it matches everything except ERROR (§5.3) — so the checker's refinement
and validation positions agree. At runtime the split is one channel-agnostic test: `is error`
on the outcome (dual-lane callees: side-lane check plus value-tag test; boxed callees: tag
test alone). E228 applies whenever the signature carries any `^` channel, and the destructure
engages *everything* at once. **Postfix `^` behaves identically on the same mixed forms —
CONFIRMED 2026-07-29 (user):** it unwraps to the same stripped success type (`T`, or
`any \ error`) and propagates the same combined error set (`e1 | e2`), which rides the
enclosing function's error channel per the unified-channel rule. In binding position the
stripping is manifest in the type — `b` is error-free in both forms:

```lambda
let b^err = a()   // b : T (stripped); err : e1 | e2
let b = a()^      // b : T (stripped) — same type; errors propagated instead of captured
```

Postfix `^` is thus a *type-narrowing* operator in expression position: downstream of either
form, `b` is statically clean and eligible for native lanes — the two forms differ only in
where the error goes (captured locally vs propagated to the caller).

**Obligations attach to the form.** Must-handle (E228) applies to `T^`/`T^E` — the enforcing
spelling — and to pn/can_raise sys funcs; `T | error` never triggers it (the no-spill
decision). This is the simplest possible keying: no provenance metadata needed, the type
spelling *is* the obligation. Doc consequences for `Lambda_Error_Handling.md`: the `T` row of
the return-type table ("always succeeds — no error possible") holds for clean functions; an
open **undeclared** function is effectively `T | error`; a *declared* plain-`T` function with
an open body is a compile error per §10.7's firewall rule; and "raise is the **only** way to
return an error from a function" becomes `^`-channel-specific — the `| error` form returns
`error(...)` values directly.

**Explicit vs implicit error-ness (decided 2026-07-29).** Type inference tracks *where* the
error-possibility came from, and enforcement keys on that provenance:

```lambda
fn f(a: T^) { let b: T = a }   // compile error — explicit T^ must be discharged first
fn g(a)     { let b: T = a }   // DEFERRED: success binds b:T; failure exits before b exists
```

An *explicitly declared* error possibility — either spelling, `T^` or `T | error` — must be
visibly engaged before the value can enter a plain-`T` position: `let b: T = a` is a compile
error for both. **When the user is explicit, we check explicitly.** Only *implicitly*-open
values (inferred openness, `any`) cross plain-`T` boundaries as DEFERRED checks. The complete
binding rule for `x = a()` where `a` is declared `int | error` (decided 2026-07-29):

```lambda
let x: int = a()            // compile error — explicit claim contradicts a's declared type
let x: int | error = a()    // legal — x carries the outcome, value or error
let x: int^ = a()           // same as the line above — equivalent in value positions
let x = a()                 // silent — x infers int | error, takes whatever a() returns
```

This is the same provenance split as the vacuous-`^` rule below — explicit facts are enforced
strictly, inferred facts flow. Consequence worth naming: `| error`-declared sys funcs (§7.3's
`int(s)` family) reject plain-typed bindings too — `let n: int = int(s)` errors; the idioms
are `int(s) or 0`, `^err` destructuring, or the union binding.

**Propagation into a declared channel — DECIDED 2026-07-29 (user).** System and user-declared
errors ride the **same** channel: every `R^E` is operationally `R^(E | error)`. `E` constrains
*user-raised* errors; defects flow implicitly. Caller consequence, to be documented: after
`let a^err = g()`, `err` may hold a *system* error (e.g. a type defect), not only the declared
`E` — code matching on `err.code` or shape must not assume `E`.

**Vacuous discharge — DECIDED 2026-07-29 (user): split by the provenance of the cleanness.**
`e^` where `e` is provably non-error differentiates two cases: (1) the operand's
error-possibility was in play and its cleanness is an *inference* result (an open-capable
callee currently proven clean) → **warn only, or even silent** — a defensive `^` survives
distant clean↔open flips; (2) the non-error nature is *explicit* (declared plain-`T`, no open
capability in play) → **static error**, as today ("'add' does not return errors"). *(Reading
of case 2 confirmed 2026-07-29.)*

**Interaction with TE-11:** after a failed `let x^err = e`, `x` holds `null` (current spec), so
the value binding is effectively `T?` until `err` is checked. Flow narrowing stays KIV; the
discipline is documented, not enforced.

**Short-circuit refinement (for TE-8/TE-9):** an error value short-circuits a checked boundary
only where the target type does not admit it. Targets `T^`, `T | error`, `any`, and `error`
receive the error *as a value* — an error-typed parameter is the explicit opt-in to
error-transparency (spelled `T | error` for non-enforcing acceptance).

**Implementation notes.** (1) The live propagation operator is postfix `^` (verified
2026-07-29: `input(...)^` works; `input(...)?` parses as the *query* operator and does not
propagate). The two E228 diagnostic texts advertising `d(...)?` are **fixed** (2026-07-29,
`build_ast.cpp:5199`, `:9017` → `d(...)^`); the error-handling guide is updated with TE-13's
channel-agnostic discharge rules. (2) The batch
idiom for per-item failures is simply a **plain array** holding successes and errors (§10.6's
type-level framing); typed `int[]` stays clean-only, and no new `(int^)[]` spelling is needed.

### TE-14 — Boxed/unboxed entry strategy is a later implementation decision

Enforcement correctness requires a safe path for every dynamic call; it does not require this
proposal to mandate a particular specialization topology. The simplest semantic anchor is a
boxed checked entry. A later implementation/performance phase may add an unboxed entry whose
parameters and returns use native carriers and whose body relies on TE-3's local proofs:

- only STATIC-PROVEN callers may enter an unchecked native version;
- dynamic callers use a checked path, likely with argument checks consolidated in a boxed
  prologue;
- the same semantic function result is observed regardless of entry/carrier choice;
- declared typed map slots likewise use unboxed physical storage only after the declared field
  check has succeeded.

Whether every function receives both versions, whether usage creates additional
specializations, and how open returns use boxed or side-channel ABIs are deferred. They are
performance decisions, not part of the language contract.

---

## 8. Phasing

Each phase gates on `make test-lambda-baseline` and `make test262-baseline` at 100% plus new
targeted tests (every new `*.ls` with its `*.txt` golden, per repo rule; negative compile-error
cases assert the diagnostic text). Performance measurements use `make release`, never a debug
binary.

**P0 — Semantic foundation and static completion.** Establish TE-6's canonical
`subtype`/`matches` primitives and truth tables first, then TE-7 items 1–4: declaration reorder
and scalar check (closes TS-1), extended-type survival, named-shape field checks, per-return
checks, arity diagnostic, B7b annotated-root member checks, and new error codes. Semantically valid
typed arrays without packed carriers use generic storage rather than receiving a TS-7
diagnostic. **TE-11 null strictness ships here too** (plain `T` rejects `null`; `T?` is the
optional spelling), and the **`or`-typing narrowing rule**
(`type(a or b) = (type(a) − {error, null}) | type(b)`, TE-13) keeps
`let n: int = int(s) or 0` valid. Record declared/effective types explicitly; do not re-derive
annotation-ness from emitter whitelists. *Exit evidence:* t1/t3/t4/t6/t13/t14-family probes
become compile errors; the subtype/match truth-table tests and both baselines are green.

**P1 — Checked-boundary infrastructure.** Add the rich `lambda_type_error` object constructor
and `emit_checked_boundary` with a full expected `Type*`, error-preservation arm, and Go-like
runtime match. Missing-argument padding is removed (arity is P0-static where the callee is
known; an error value otherwise), and effective-type computation (`T | error` for dynamic call
or inferred-open outcomes) is plumbed through the front end. Check placement may initially
follow the safest boxed/site-local implementation; TE-14 optimization is not an exit
dependency. *Exit evidence:* dynamic wrong arguments and t10 yield rich errors; annotated
declarations never bind error, failed calls never enter the body, and both baselines are green.

**P2 — Return honesty (closes TS-2's class).** Per-return checked boundary including boxed
returns (`emit_coerce_boxed_to_declared` gains the check for INT/FLOAT/BOOL/STRING); audit the
four ad-hoc `STRING` native-return widenings; a call expression's recorded static type is now
honest for every consumer — the declared type when clean, `T | error` when open.

**P3 — Named types at runtime (B3/B9).** TE-10: validator call emission at DEFERRED bindings;
validator hardening (base-type enforcement for constrained types, error-path surfacing,
openness default); optionally the `input(url, {schema: Q})` convenience (insertion point
`lambda-eval.cpp:2981-3018`) so parse-time failures carry file context. *Exit evidence:* t16 yields an error
value carrying `.a: expected int, got string`; no binding is established; the §4.3 `Config`
example works end-to-end without assuming the parsed map has been repacked.

**P4 — Container and member-write enforcement (B7a/B7b + B2's hole).** Writes through an
annotated `T[]` check the element. A map write through an annotated root checks that the whole
post-state still conforms to the root's declared `Type*`; all failures leave the destination
unchanged. Open extra fields are allowed by an open contract, but still extend the map with an
exact runtime shape/slot matching the stored value. Legal field-type transitions rebuild and
repack the shape, with the COW replacement propagated to the root or parent. Fast-path fallback
produces an error instead of degrading a typed contract; the ARRAY_NUM cross-convert path gets
element checks; and the B2 raw-pointer/ANY downgrade is replaced by an honest tagged value.
Unannotated containers keep today's flexible type/shape evolution — inference is not a binding
contract (TE-12). *Exit evidence:* unannotated `int → string` map reshaping, annotated static
and dynamic mismatch cases, union-field shape transitions, computed keys, open extra fields,
COW snapshot isolation, and no-mutation-on-failure tests pass.

**P5 — Legacy-consumer consolidation and optimization hand-off.** Migrate remaining `fn_is`,
validator, and emitter call sites onto P0's shared subtype/match foundation; delete superseded
unchecked boundary paths. Then measure and design TE-14's boxed/unboxed entry strategy,
validated-map canonicalization, direct field offsets, and witness caching without changing
semantics.

---

## 9. Out of scope / future stages

- **Perf leverage of annotations** (the next stage, gated on this one): TS-3 ANY-downgrade fix,
  TS-5 direct field offsets, two-entry per-callee specialization (raw entry for
  statically-proven callers, checking entry for ANY callers — the Sorbet shape, JIT-specialized
  so hot paths never see the check), validation-witness caching for named types, and the
  sys-func retrofit (registry enrichment, clean/open sys-func versions, the `len`-branch fix —
  §10.9, deferred there 2026-07-29).
- **XML schema-driven typing** — KIV per user decision; §4.2 records the substrate facts.
- **Flow-sensitive narrowing** (`if (x is int)` refining `x`), **generics**, **checked-cast
  surface** (`as` / `as?` — no cast operator exists today, t7), **arity overloading** (TS-8).
- **`x is (int[])` — parenthesized types on the `is` RHS**: noted with full grammar analysis in
  TE-13's side-finding; belongs to the **pattern-grammar and validator design**, not this
  proposal (user, 2026-07-29).
- **TS-9** int→float overflow policy — owned by `Lambda_Formal_Semantics.md`.
- Side-findings filed for separate handling: TOML datetime unsupported (§4.2); `it2l` missing
  ERROR arm and the `0`-vs-`INT64_MAX` asymmetry (§5.2) — both subsumed by TE-8 at boundaries
  but the raw converters may deserve their own cleanup. (The index-OOB → `ItemNull` behavior
  flagged in §5.2 turned out to be *conformant*: `Lambda_Formal_Semantics.md` §7.1 specifies
  reads-are-total / absence-is-null; only OOB *writes* raise.)

## 10. Open questions

1. **Trap class — DECIDED 2026-07-29** (user): error-return model, no panic, no forced `T^E`
   (TE-9 records it). The decision opens sub-questions 2–9:
2. **One discharge surface for two error channels — elaborated as TE-13, revised 2026-07-29
   to the two-form model.** `T^` = enforcing (E228, raise-capable, explicit-only); `T | error`
   = non-enforcing union (what inference produces). Discharge forms (`^err`, postfix `^`,
   `^e`/`is error`, `or`-defaults) are value-directed and work identically over both.
   Residuals (a)/(b)/(c) all **DECIDED**: the obligation split is *type-directed* — attached
   to the `^` spelling itself, closing (a); `R^(E | error)` widening per the unified channel;
   vacuous-`^` warn/silent when cleanness is inferred, static error when explicit.
   **CONFIRMED 2026-07-30:** `let x: int | error = a()` counts as immediate E228 engagement.
3. **Return ABI for open/native variants — DEFERRED 2026-07-30.** The language contract is
   representation-independent. TE-14 leaves boxed/unboxed entry count, check placement, and
   open-return side channels to the implementation/performance phase, subject to boxing
   invisibility and TE-3's local proof invariant.
4. **Error payload — DECIDED 2026-07-30 (user): proper rich error object/value.** Drop the
   inline code-only form. Every type-enforcement failure constructs an error with at least
   `code`, `message`, expected type, actual type/value summary, boundary/source location, and
   validator path when applicable. Boundary failure is cold; preserving the diagnostic
   contract takes precedence over allocation avoidance.
5. **Value-domain semantics of error values — RESOLVED 2026-07-29: already fully specified in
   `Lambda_Formal_Semantics.md`.** Checked against the doc: **equality** (§5.1) — `nan` and
   `error` are the two designed poison carve-outs, never equal to anything including
   themselves (`error == error → false`, mirroring nan; so `a(b) == 5` on an errored `a` is
   `false`, and the rationale explicitly weighs the swallow-vs-spread trade: totality keeps
   set processing alive, `is error`/`case error:` arms are the classification relation).
   **Total order** (§6.2) — `error` is the maximum: `… < type < function < nan < error`
   ("null is less than everything — absence; nan and error are beyond everything — broken");
   sort is stable, `desc` is full reversal. **Comparison** (§6.1) — `<` on error operands
   taints (returns `error()`). **Dedup/grouping** (§5.6) — each error stands alone, never a
   duplicate. **Null-vs-error taxonomy** (§7.1/7.2) — reads are total, absence is `null`
   (OOB *reads* → null, so the emitter's `MIR_INDEX_OOB_ITEM_NULL` conforms to spec); OOB
   *writes* raise. One implementation-fidelity nit remains: `it2d`'s ERROR→`NaN` fallback
   *degrades* error-poison to nan-poison — distinct classes under both `is` and the total
   order — which the TE-8 short-circuit (error stays error) fixes at boundaries.
6. **Errors in containers — DECIDED 2026-07-29 (user), with corrected framing: the exclusion
   is *type-level*, not representation-level.** `[a, error(...), b]` is allowed — a plain array
   holds an error as an ordinary element, and that plain array is the batch idiom for per-item
   results. The typed `int[]` excludes error *because its element type does*: constructing or
   writing `int[]` from data containing an error yields an error result (failed element check /
   TE-8 short-circuit). `ArrayNum` is an internal optimization invisible to the user ("to the
   user, it is array") — it cannot hold an error but can hold `NaN`, which is a float value,
   not an error; the representation choice never changes semantics. Shaped-map typed fields
   follow the same type-level rule (the t4 path).
7. **Clean-ness inference mechanics — DECIDED 2026-07-29 (user, incl. the division
   consequence): declared return types are effect firewalls; bindings are not (§10.8).**
   The rule: a function with an explicitly declared plain-`T` return whose body is open
   (calls an open function, or contains an error-originating operation whose result reaches
   the return) is a **compile error** — the author picks from a three-way menu: **contain**
   the openness locally (`^err` + handling, `or` default), **disclose** it as `T | error`
   (non-enforcing — callers see it, owe nothing), or **impose** it as `T^` (enforcing —
   callers must handle). Inference itself only ever produces `| error`, never `^` (TE-13
   two-form model). `-> any` (and undeclared returns) stay
   silent: `any`/absence-of-declaration is the absence of a promise. Consequences:
   - **§10.7 mostly dissolves.** Silent clean→open cascades stop at the first declared frame
     (stability); declared-return recursion needs no fixpoint (assume the declaration, check
     the body — only undeclared recursion iterates); declared-return `pub` fns need no effect
     metadata (recommend, not require, declared returns on `pub`). A function *value* invoked
     dynamically remains per-call-site open, as before.
   - **Fixes an inversion**: explicit `open_call()^` inside a plain-`T` fn is *already* a
     compile error (`Lambda_Error_Handling.md`); implicit propagation slipping through would
     have been backwards. Zig (`catch` forced in non-`!T` fns), Swift (`do-catch` in
     non-throwing), Rust agree.
   - **Emitter consequence**: declared-plain-`T` function bodies are clean-return by
     construction. A later native variant may exploit that fact; TE-14 deliberately leaves the
     carrier/ABI choice out of the semantic phase.
   - **No E228 spill**: inference never produces `^` at all — it produces `| error`, which
     never triggers must-handle. A function pushed open by its arithmetic discloses as
     `T | error` with zero caller impact; `^` is reserved for authors *choosing* to impose
     handling. Must-handle stays exactly where it is today (declared `^` and pn/I-O raisers,
     §7.3).
   - **Interior vs interface (settles §10.8's residual)**: unannotated bindings flow —
     `let x = open_call()` infers `T | error`. Annotated bindings are contracts —
     `let x: int = dynamic_call()` checks at the boundary, establishes `x: int` on success,
     and produces an error before `x` exists on failure. Declared returns remain effect
     firewalls.
   - **Confirmed consequence**: error-*originating* operators make bodies open — notably
     division with a dynamic divisor (dynamic zero divisor returns `error()` per the formal
     semantics), so `fn avg(sum: int, count: int) int { sum / count }` errors until declared
     `int | error` (or `int^`, or contained). Literal-nonzero divisors stay clean via trivial
     value analysis.
     Joins the D2I sweep as a pre-P0 audit, and the diagnostic must state *why* the body is
     open (first cause, e.g. "call to 'g' may return error"), making the "why open?"
     diagnostic part of the error UX rather than a nice-to-have.
   - TE-9's open-case wording is revised accordingly: the silent effective-`T | error`
     applies only to **undeclared** returns; declared returns enforce.
8. **Declarations and reassignment — REVISED 2026-07-30 (user): annotations are contracts;
   inference flows.** `let x: T = e` establishes only `x: T`: a DEFERRED success binds `T`, and
   failure yields the boundary error before the binding exists. Reassignment checks before
   commit and leaves the old value unchanged on failure. `let x = e` has no such declared
   boundary and infers `e`'s effective type, including `T | error`.

   An explicitly declared error possibility (`T^` or `T | error`) still cannot enter plain
   `T` without visible discharge/narrowing; that is STATIC-REJECTED. A genuinely dynamic
   `any` source is the DEFERRED case. Consequently a successfully established annotated
   `var x: T` may use a `T` carrier; it never needs boxed storage merely to hold a failed
   check, because the error is not stored in `x`.
9. **Sys-func registry metadata for the clean/open split (extends B13) — DEFERRED 2026-07-29
   (user) to the perf-tuning stage**, where the clean/open sys-func versions are built anyway.
   The TE-9 convention (error values, never silent wrong values) is normative *now* for any new
   or touched sys-func code; only the retrofit of the existing surface rides the perf stage.
   Scope when it lands: the registry must
   record per-param types (static checking), the success return type, whether the function can
   *originate* errors (partial `abs` vs total `len`), and error-strictness on inputs.
   `Lambda_Formal_Semantics.md` §7.3 already supplies the adjudicating principle — system `fn`
   failures are values, channel `T?` or `T | error` (never `T^E` on a system fn), chosen by
   *"absence in / no answer → null; present but invalid → error()"* — and `len(123) = 0`
   conforms to **neither** branch, so the silent 0 is already condemned by spec; the remaining
   decision is only which branch it takes (present-but-invalid → `error()`, matching
   `int("abc")`, seems the natural reading). §7.3 also confirms `input`/`fetch` as pn-family
   raisers (E228 conformant) and supplies the wrapper idiom for set-oriented input.
10. **Null strictness — DECIDED 2026-07-29 (user):** enforce `T?`-for-nullable immediately in
    P0; no warn-only release.
11. **Openness default for named map types — DECIDED 2026-07-29 (user): open** (TE-10). Whether
   a closed form / `allow_unknown_fields` ever becomes user-visible syntax is left for the
   future.
12. **`let x: int = 3.5`** — P0 makes it a compile error (relation says FLOAT↛INT). Any shipped
   scripts relying on the D2I truncation need a sweep before landing. Prior art (surveyed
   2026-07-29) is near-unanimous for the error: Java ("possible lossy conversion"), C#, Rust,
   Swift, Kotlin, Dart and mypy all reject at compile time regardless of value; Go and Zig
   reject fractional constants ("constant 3.5 truncated to integer") while allowing
   exact-valued ones (`3.0`); the only allow-camp is legacy C/C++ assignment-init (silent
   truncation — regarded as a defect; C++11 bans it as "narrowing" in brace-init, and
   `-Wconversion` exists to flag it) and SQL — which **rounds** rather than truncates
   (Postgres `CAST(3.5 AS int)` = 4). That the allow-camp cannot even agree on the semantics
   is itself the argument against silent conversion. Lambda's Go-like enforcement rule:
   statically reject float-typed initializers including `3.0`, and reject an `any` value whose
   actual runtime type is float at a deferred `int` boundary regardless of magnitude. Explicit
   `int(...)` conversion owns any value-dependent conversion policy.
13. **t2's `var`-path `null`** — mechanism narrowed but not step-verified (§5.1); verify while
   implementing P0 so the fix isn't aimed at a ghost.
14. **Constrained types — CONFIRMED 2026-07-30 (user): base-only interim is accepted.** The
    validator is the eventual predicate checker for `T where …`; until that validator work
    lands, enforcement checks the base `T` only. This is a documented, deliberately narrow
    validator deferral and does not block the core enforcement phases.
15. **`STRING` in the native-return set** — P2 audits the four ad-hoc widenings; the clean fix
   (widen `mir_is_native_scalar_value_type` or stop widening ad hoc) interacts with the perf
   stage's ABI plans; decide there, enforce honestly here.
