# Lambda — Type Support Design: Enforcement First

**Status:** PROPOSAL rev 1 — complete, for discussion. Nothing here is implemented.
**Date:** 2026-07-29
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

1. **STATIC-PROVEN** — the checker proves the value's type; zero runtime cost.
2. **STATIC-REJECTED** — the checker proves a mismatch; compile error.
3. **DEFERRED** — the static type is unknowable (`any`-typed expression, parsed input data);
   a runtime check runs **at the boundary**, and a mismatch produces a diagnostic-carrying
   type-error *value* (TE-9).

What must never happen is the fourth outcome Lambda has today: the annotation silently ignored,
silently lossy, or silently corrupting (see §5.4 — all three are measured, including declared-int
bindings that print raw `String*` pointer bits).

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

**TE-1 — Annotations are contracts, not hints.** `x: T` means: past this binding, `x` *is* a
`T`, in both semantic type and runtime representation. The Go stance, not the TS stance. This
holds identically for `let`, `var`, parameters, return types, and typed fields of named map
types.

**TE-2 — Three-outcome resolution at every annotated boundary.** Each boundary is resolved as
STATIC-PROVEN, STATIC-REJECTED, or DEFERRED (runtime-checked) — never silently trusted. The
static relation used for PROVEN/REJECTED and the runtime relation used by DEFERRED checks must be
**the same relation** (one assignability definition; today there are at least three partial ones,
see §5 and TE-6).

**TE-3 — The representation invariant becomes enforceable.** The emitter's standing assumption —
*a value's static TypeId implies its runtime representation* (`Lambda_Issue_Type_Support.md` §0)
— stops being an unenforced convention: DEFERRED boundary checks are precisely what uphold it.
This is the prerequisite for the perf stage: only guaranteed types may drive representation.

**TE-4 — Runtime mismatch produces a diagnostic-carrying error value.** A failed DEFERRED check
yields an error *value* in the value lane (decided 2026-07-29 — see TE-9 for the full model),
naming the boundary, the expected type, and the actual type/value. It flows like any Lambda
value — dischargeable with `let x^err = …` / `?` / `x is error` — and a script whose
uncontained result is an error fails with that diagnostic. Never `null`, never `0`, never
pointer bits, never a silent pass-through.

**TE-5 — `any` remains the gradual gate, one-way-free.** `T → any` (boxing) is always allowed
and free. `any → T` is always DEFERRED — checked at the boundary, exactly like Go's `x.(T)`.
There is no unchecked `any → T` anywhere: not at declarations, not at call arguments, not at
returns, not at typed-container writes.

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
the front end accepted, where it should be a compile diagnostic. And after the coercion succeeds,
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
representation bridge is needed**:

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
| B4 | Re-assignment to annotated `var` | ✅ checked (`:7995`) | unchecked unbox (`:11517`) | add checked convert |
| B5 | Call arguments | ✅ checked (`:2673`) | unchecked unbox ×7 sites; missing arg → native `0` | checked convert; arity is a compile error |
| B6 | Declared return type | vacuous (ANY body defeats `:8379`) | unchecked unbox ×6 sites | per-return static check + checked convert |
| B7 | Typed container element write (`a[i] = v`) | none | `fn_array_set` silently degrades the array in place | checked write on annotated bindings |
| B8 | Typed member read (declared field type) | n/a | unchecked unbox of `fn_member` result (`:8346`) | checked (or trusted once B3 guarantees construction) |
| B9 | Parsed input → annotated binding | none | none | same as B3 — the canonical DEFERRED boundary |
| B10 | Global/module var round trip | n/a | store boxes by declared tid; load unboxes trusting it | trusted, *provided* B1 enforces the store side |
| B11 | `is` / `match` / constrained types | no operand checks | three divergent implementations (§5.1, §5.3) | one relation (TE-6) |
| B12 | The `it2*` converter family | n/a | six different silent fallback values (§5.2) | boundaries stop calling them unchecked |
| B13 | Sys-function arguments | none — registry `first_param_type` is dispatch-only metadata | per-function ad hoc: silent value, logged `ItemError`, or message-less `ItemError` (§5.5) | registry-driven static check; TE-9-quality diagnostics on runtime failure |

The pattern: **static checking exists exactly where someone once added it (B4, B5) and nowhere
else; dynamic checking exists exactly once (B2's coercion) and its result is then thrown away.**
Everything else trusts.

---

## 7. Enforcement design

### TE-6 — One assignability relation, one implementation

Adopt a single relation `assignable(S, T)` used by *all four* consumers: the static checker, the
runtime checked conversion, `fn_is`/`match`, and the validator. Its definition is the one the
static side already has — `types_compatible_with_full`'s exact-embedding numeric lattice
(`lambda_numeric_kind_exactly_embeds`) extended with shape/union/occurrence walking that
currently lives only in the validator. Consolidation targets: `numeric_type_subsumes` (fn_is)
vs `validator_numeric_type_embeds` (validator) vs `lambda_numeric_kind_exactly_embeds` (checker)
— three numeric lattices claiming to agree; and the constrained-type three-way divergence
(§5.1). The relation is **value-preserving by construction**: `INT→{INT64,INTEGER,FLOAT,DECIMAL}`,
`INTEGER→DECIMAL`, `{F32,FLOAT}→{FLOAT,DECIMAL}`, sized-int widenings — and nothing lossy.
`FLOAT→INT` is not assignable anywhere, which means the declaration ladder's `MIR_D2I` arm
(t13) is *removed*, making declarations agree with the call boundary instead of the other way
round.

Runtime-side, the relation needs a value-level variant `convertible(item, T)` for the DEFERRED
checks: a `decimal` holding an exact int *is* convertible to `int` (via
`decimal_to_int64_exact`), an inexact one is not (today: silent `INT64_MAX`). Same rule, decided
on the value instead of the static type — never lossy.

### TE-7 — Complete the static layer (fix the four root causes)

1. **Keep both types alive at declarations.** Reorder `build_assign_expr`: resolve the
   annotation first, build the initializer, run `assignable(init_type, declared_type)`, *then*
   store the declared type. Statically-known-wrong initializers become STATIC-REJECTED
   (`var s: int = "abc"` joins `x = "abc"` as E-errors). This closes TS-1.
2. **Record annotation-ness for every binding**, not just `var` — a `declared_type` that
   survives on the node (today's single overwritten slot is root cause 1; the emitter's
   whitelist re-derivation at `transpile-mir.cpp:6500-6508` is the workaround to delete).
3. **Extended types survive on declarations** the way they do on params (`full_type`
   equivalent), so union and occurrence annotations are checkable at declarations (t6).
4. **Named-shape adoption checks before it adopts**: field-by-field `assignable(literal field,
   declared field)` in `build_assign_expr:5077-5121`, with missing-required-field and (per
   TE-10's openness decision) unknown-field diagnostics. Closes the t4 corruption at the front
   door.

Plus: per-return-site checking against the declared return type (the `:8379` check is kept but
no longer the only line of defense — each `return e` / final body expression is checked where
its type is known, killing the vacuous-ANY hole for the static half); call arity becomes a
diagnostic (today's argument loop stops at the shorter list and the emitter pads `0`); TS-7's
unsupported typed-array element types (`bool[]`, `string[]`) get a compile-time diagnostic
naming the supported set.

Diagnostics stop funneling everything into E201: the dormant codes exist and get used
(`ERR_ARGUMENT_TYPE_MISMATCH=207`, `ERR_RETURN_TYPE_MISMATCH=208`, `ERR_UNDEFINED_FIELD=205`,
…, `lambda-error.h:71-99`).

### TE-8 — The checked conversion (the DEFERRED half)

One new emitter primitive, `emit_checked_unbox(mt, reg, expected_tid, site)`, replacing bare
`emit_unbox` at the **17 primary boundary sites** (§5.2 table: 7 argument, 4 declaration,
6 return). Emission shape:

```
tag = item >> 56                          // or item_type_id() for tag-0 handling
if tag == expected        → existing unbox (fast path unchanged)
elif tag == error         → short-circuit: the error value becomes the boundary's result (TE-9)
elif convertible(item, T) → convert (exact numeric embeddings only)
else                      → lambda_type_error(expected, item, site) — construct the error value
```

The runtime side adds the checked converter family (`cast2i`, `cast2l`, `cast2d`, `cast2s`, …)
for slow paths and boxed trampolines — same dispatch as `it2*` but the fall-through constructs
a diagnostic-carrying error value instead of `0`/`NaN`/`nullptr`. The `it2*` family itself is left untouched for
pre-verified internal callers; boundaries simply stop being routed through it. (`it2d`'s
`NaN`-instead-of-0.0 comment shows this exact correction was already made once, one converter at
a time; TE-8 finishes the thought at the boundary layer.)

Fast-path cost is one predictable branch on bits already in a register — the same shape as the
guards the IC machinery already emits everywhere. The perf gate in §8 holds it to noise on the
typed benchmark column.

**Placement (revised by TE-14):** the **argument** group's checks are emitted once per
function, in the boxed version's prologue — the per-call-site conversion sites listed above
become plain entry selection. The **declaration** and **return** groups stay site-local. Net:
checking code scales with the number of functions, not the number of call sites.

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
   C-ABI principle: errors as return values everywhere).

**The clean/open scheme.** For `fn a(b) int`, the declared type is the **success type**; whether
`error` joins it is *inferred*:

- **Clean case:** the compiler statically proves every boundary the result depends on — the
  call's effective type is plain `int`. Zero runtime cost, and the **native unboxed variant**
  serves exactly this case: no error path exists by construction (which also retires
  `INT64_ERROR`-style sentinels from clean lanes).
- **Open case:** some boundary is DEFERRED — the call's effective type is **`int | error`**,
  and that union propagates through downstream code. *(Revised 2026-07-29 by §10.7's firewall
  rule: this silent widening applies only to **undeclared** returns — a declared plain-`int`
  function with an open body is a compile error, resolved by containing, disclosing
  `int | error`, or imposing `int^`.)* The **boxed variant** serves this case, carrying the
  union in the tagged Item. (This refines today's crude `get_effective_type` can_raise→ANY
  rule into a precise `T | error`.)

**Error short-circuit rule.** An error value arriving at any subsequent DEFERRED boundary
becomes that boundary's result without further checking — "error in, error out", the
`GUARD_ERROR1` convention generalized. Propagation is therefore *implicit*; containment is
*explicit* and placed where the user chooses (`let x^err = …`, `?`, `x is error`). This is what
keeps the no-spill promise: nothing forces handling, and errors surface at the batch boundary
where the user aggregates results.

**Sys functions.** The normative convention (making §5.5's pattern 2 the rule): acceptive on
input types, **error value returned for invalid types** — never silent wrong values, never
message-less deaths. The future perf split mirrors user functions: per-func clean/open versions
(`len(non_error_data) int` vs `len(any) int | error`), selected by the caller's statically-known
argument types, with registry metadata driving the selection.

Note how this dissolves §5.2's structural constraint rather than fighting it: open results
travel boxed, so no native error lane is needed; native lanes exist only where clean-ness
proves no error can occur. The `RETURN_LANE_ERROR` dual-lane ABI remains the third point in the
matrix (clean args × open return: native success value + side-channel error) — reconciling the
three return shapes is an open ABI question (§10).

For validation failures at named-type boundaries (TE-10), the error value carries the
validator's path detail (`.field[3]: expected int, got string`) — which requires surfacing
`ValidationError` detail that today dies in `log_debug` (§5.3).

### TE-10 — User-defined types: the validator becomes the runtime enforcer

At a DEFERRED B3/B9 boundary (`let q: Q = <dynamic>`), emit a call to the **existing** entry
point `fn_is` already uses: `schema_validator_validate_type(ctx->validator, item,
const_type_with_tl(type_index))`. On failure → a TE-9 error value carrying the path detail. No
representation bridge is needed (§5.3: schema types and script types are the same `Type*` graph); this is an
emission change plus validator hardening:

- **Depth: deep, on first crossing** — Go `Unmarshal` semantics. The value either satisfies `Q`
  in full or the binding traps. (Witness caching to skip re-validation of already-validated
  subtrees is perf-stage work; correctness first.)
- **Openness: named map types are open by default — DECIDED 2026-07-29 (user).** Extra fields
  pass, matching the validator's current behavior, Go's unmarshal, and structural-width
  subtyping. A closed form (wiring the already-parsed `allow_unknown_fields`/`strict_mode`
  flags to actual checks) remains a possible future opt-in.
- **Constrained types (`T where …`) — DECIDED (§10.14)**: the validator is the checker and will
  evaluate constraint predicates (the MIR-inlined `is` already does); implementation
  deferrable. Until it lands, constrained checks are base-type-only as a *documented* deferral
  — distinct from today's silent, undocumented pass in `fn_is` (`:1139-1163`).
- Nominal `TypeObject` checking (today `fn_is`-only) folds into the same relation.

### TE-11 — Null policy

Today `null` passes `types_compatible`'s escape hatch statically, while `null is int` is
`false` at runtime — the two halves disagree. **Decision: plain `T` does not admit `null`;
optionality is spelled `T?`** (the occurrence form already exists in the grammar and the
validator honors it via `is_type_optional`). ANY/NULL boundary values hitting a plain-`T`
DEFERRED check trap. **DECIDED 2026-07-29 (user): ships in P0** together with the rest of the
static layer — no warn-only interim release. Migration risk is handled by the P0 baseline gate:
any code relying on null-through-annotation surfaces there and is fixed with `T?`.

### TE-12 — What enforcement deliberately does not touch

Inference stays authoritative where there is no annotation (§4.1) — enforcement never inserts
checks on inferred-only paths. TS-9 (int→float overflow) stays as specified in
`Lambda_Formal_Semantics.md` §4.1. The COW/borrow semantics of map-typed locals (TS-4/C4), the
region-producer gate (TS-6), the ANY-downgrade fast-path losses (TS-3), and the dead
direct-field-offset path (TS-5) are all perf-stage items — though note TE-10 finally gives
named map annotations a *meaning* (a validated contract), resolving TS-5's "cost without
benefit" in the benefit direction, and TE-3's guarantees are exactly what will make the TS-3/
TS-5 fast paths safe to re-enable.

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

**Acknowledgment forms (PROPOSED, elaborated 2026-07-29): must-handle = must-engage-explicitly.**
E228 guards against *unawareness*, not against deferred branching — so it is satisfied whenever
the enforcing call's result is received by a context that **textually** engages the error
possibility:

1. `^err` destructuring, or postfix-`^` propagation (today's two forms);
2. a binding, parameter, or declared return whose explicit type admits `error` — `T | error`,
   `T^` (equivalent in value positions), or `error` itself. The binding carries the outcome as
   a union, and ordinary assignability already enforces engagement-before-plain-`T` for both
   spellings — the §7.3 wrapper idiom in one step, and the batch idiom for collecting
   enforcing-call outcomes. **`any` never counts** — it admits error but acknowledges nothing;
3. a `match` with a `case error:` arm.

Bare `let x = a()` remains an error — Lambda stays stricter than Zig/Rust/Go, all of which
accept an untyped capture. The §10.7 firewall backstops the demotion path: an acknowledged
error still cannot silently escape a declared plain-`T` interface. Implementation: one added
condition at the existing E228 site (`build_ast.cpp:5197` — "declared type explicitly admits
error") plus a third suggestion in the diagnostic text.

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
engagement (textually explicit consumption handling both branches).

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
fn g(a)     { let b: T = a }   // allowed — implicit/open error-ness; a DEFERRED boundary
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
capability in play) → **static error**, as today ("'add' does not return errors").

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
`build_ast.cpp:5199`, `:9017` → `d(...)^`); the ❌/✅ duplicated example line in
`Lambda_Error_Handling.md` §"Handling System Function Errors" still needs fixing. (2) The batch
idiom for per-item failures is simply a **plain array** holding successes and errors (§10.6's
type-level framing); typed `int[]` stays clean-only, and no new `(int^)[]` spelling is needed.

### TE-14 — Two-version compilation model (decided 2026-07-29, user)

Each `fn`/`pn` compiles to (up to) two versions, and **all fn-boundary checking consolidates
into one of them**:

- **Boxed version — always `Item… → Item`, regardless of annotations.** It performs the
  argument type checks, conversions and unboxing *inside the callee*, and its Item return
  carries `T | error` when open. Every dynamically-typed call site simply boxes its args and calls
  this version — so the type-checking code is emitted **once per function, not once per call
  site**. This is what keeps TE-8 from exploding combinatorially ("otherwise too many
  combinations to check").
- **Unboxed version — assumes the annotated types.** Raw native lanes, zero checks; callable
  only from call sites whose static types are proven (TE-2 STATIC-PROVEN). Emitted when the
  user annotates params/returns — and the transpiler may *additionally* specialize one from
  usage even for `fn a(any) any` (e.g. an `a(int) int` variant): specialization is not limited
  to annotations.
- **Return types need special care:** even with a declared `int`, the *effective* return may be
  `int^` (open body). The boxed version carries the union naturally; whether an open function
  gets an unboxed variant at all is **implementation-discretionary** (§10.3, decided) — and the
  current implementation already exercises that discretion, emitting unboxed *inferred*
  variants especially for the recursive functions in the benchmarks.

Consequence for the emitter (revises TE-8's placement): call sites stop emitting argument
conversions for open calls — the three-arm ladder at `transpile_call_raw` (`:10464-10502`)
reduces to *entry selection* (proven → unboxed entry; otherwise box-and-call the boxed entry) —
and the checked conversions land in the boxed version's prologue (today's unchecked
`emit_unbox` at `:14691-14695` / `:14026-14030` becomes the checked form). Declaration,
reassignment and return boundaries keep their site-local checks.

---

## 8. Phasing

Each phase gates on `make test-lambda-baseline` at 100% plus new targeted tests (every new
`*.ls` with its `*.txt` golden, per repo rule; negative compile-error cases assert the
diagnostic text).

**P0 — Static completion (front end only, no ABI/runtime risk).** TE-7 items 1–4: declaration
reorder + scalar check (closes TS-1), extended-type survival, named-shape field checks, per-
return checks, arity diagnostic, TS-7 compile diagnostic, new error codes. **TE-11 null
strictness ships here too** (plain `T` rejects `null`; `T?` is the optional spelling — decided
2026-07-29), and the **`or`-typing narrowing rule** (`type(a or b) = (type(a) − {error, null})
| type(b)`, TE-13) so the `int(s) or 0` idiom passes the strict checker. Also delete the
emitter's annotation re-derivation whitelist in favor of the recorded declared type. *Exit
evidence:* t1/t3/t4/t6/t13/t14-family probes all become compile errors; baseline green.

**P1 — Checked-conversion infrastructure.** `lambda_type_error` constructor (emitting the
inline code-form error item on hot paths, §10.4) +
`emit_checked_unbox` with the error short-circuit arm; checks placed per TE-14 (argument
checks once per function in the boxed-version prologue, call sites reduced to entry selection;
declaration/reassignment/return sites checked locally); missing-arg
padding removed (arity is P0-static where the callee is known; an error value otherwise);
`cast2*` family for the boxed/slow paths; effective-type computation (`T | error` for open
calls) plumbed through the front end. *Exit evidence:* the dynamic halves of the catalog
(ANY-string arg, ANY return, t10, t16 field read) yield diagnostic-carrying error values; typed
benchmark column within noise of pre-P1 (the perf guardrail — enforcement must be ~free on hot
paths).

**P2 — Return honesty (closes TS-2's class).** Per-return checked conversion including boxed
returns (`emit_coerce_boxed_to_declared` gains the check for INT/FLOAT/BOOL/STRING); audit the
four ad-hoc `STRING` native-return widenings; a call expression's recorded static type is now
honest for every consumer — the declared type when clean, `T | error` when open.

**P3 — Named types at runtime (B3/B9).** TE-10: validator call emission at DEFERRED bindings;
validator hardening (constrained-type decision, error-path surfacing, openness default);
optionally the `input(url, {schema: Q})` convenience (insertion point `lambda-eval.cpp:
2981-3018`) so parse-time failures carry file context. *Exit evidence:* t16 yields an error
value carrying `.a: expected int, got string`; the §4.3 `Config` example works end-to-end.

**P4 — Container element enforcement (B7 + B2's hole).** Writes through an annotated `T[]`
binding check the element (fast paths keep their guards; the fallback arm produces an error
value instead of degrading via `convert_specialized_to_generic`); the ARRAY_NUM cross-convert path gets element
checks; the B2 raw-pointer/ANY downgrade is replaced by an honest tagged value (which is also
the TS-3 prerequisite). Unannotated arrays keep today's flexible degrade — it is inference, not
contract (TE-12).

**P5 — Relation consolidation (TE-6 refactor).** Fold the three numeric lattices and the
constrained/nominal special cases into the shared `assignable`/`convertible` module consumed by
checker, emitter, `fn_is`, and validator. Mechanical but wide; last so it lands on a
fully-enforced, fully-tested surface.

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
   vacuous-`^` warn/silent when cleanness is inferred, static error when explicit. One
   micro-item to confirm: `let x: int | error = a()` counting as E228 handling (TE-13).
3. **Return ABI for open functions' unboxed variants — DECIDED 2026-07-29 (user):
   implementation-discretionary.** Whether an open function gets an unboxed variant is not a
   language-design question; the boxed version is the semantic anchor (always present, carries
   `T^`). The current implementation already exercises the discretion — it emits unboxed
   *inferred* variants, especially for the recursive functions in the benchmarks (the TCO
   self-call native path, §5.2). Which error side-channel such a variant uses
   (`RETURN_LANE_ERROR` dual-lane or otherwise) is likewise the implementation's choice.
4. **Error payload cost — DECIDED 2026-07-29 (user): two-form error items.** Predefined system
   errors are carried as a bare **code**, inline in the Item — layout
   `[tag byte][…zeros…][16-bit error code]` — and an **error object** is constructed only when
   elaborate info is needed: `[tag byte][error-object pointer]`. The two forms are
   discriminated by payload magnitude (a sub-64K payload is a code; real pointers never live in
   the low pages). Consequences: a hot-loop boundary failure allocates **nothing** — a
   code-form item is a pure bit pattern, so it is interned by construction and its construction
   is infallible by construction (closing both §10.4 remainders). `err.code` works on both
   forms; `err.message` on a code form synthesizes from the static code table;
   `err.file`/`line`/`column`/`source` are `null` on the code form; the `error()` constructor
   always builds the object form. Accepted trade-off (TE-4 latitude): a code-only type error
   surfacing uncontained at top level reports the code/category without boundary location —
   callers wanting detail discharge near the boundary, and debug builds may always elaborate.
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
   - **Emitter win** (ties §10.3/TE-14): declared-plain-`T` functions are clean-return *by
     construction* — native return lanes never need an error path; return special-care applies
     only to undeclared/`T^`/`any` functions.
   - **No E228 spill**: inference never produces `^` at all — it produces `| error`, which
     never triggers must-handle. A function pushed open by its arithmetic discloses as
     `T | error` with zero caller impact; `^` is reserved for authors *choosing* to impose
     handling. Must-handle stays exactly where it is today (declared `^` and pn/I-O raisers,
     §7.3).
   - **Interior vs interface (settles §10.8's residual)**: binding annotations are
     *checkpoints* — `let x: int = open_call()` stays legal, produces effective `int^`, flows;
     declared returns are *contracts* — nothing undeclared escapes. Interiors flow, interfaces
     enforce.
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
8. **Declarations and reassignment — DECIDED 2026-07-29 (user): bindings are checkpoints, not
   firewalls.** No return-style enforcement at `let`/`var` — interiors flow, interfaces
   enforce (§10.7). `let b: T = a` is a **compile error** when `a`'s error possibility is
   *explicit* — either spelling, `T^` or `T | error`, equivalent in value positions — and an
   allowed DEFERRED boundary only when `a` is *implicitly* open. The deferred case's post-bind effective type
   is `T | error` (the clean `T` guarantee obtains after discharge); an open `var` local takes
   boxed storage — a native int lane cannot hold the error — an implementation consequence
   touching the MIR narrowing invariant.
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
10. **Null strictness** (TE-11) — enforce `T?`-for-nullable immediately, or one release of
   warn-only for null-through-annotation?
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
   is itself the argument against silent conversion. Lambda's rule: static reject for
   float-typed initializers including `3.0` (the literal is user-controlled — write `3`; Go's
   exact-constant nicety buys nothing here), while DEFERRED boundaries judge the value per
   TE-6 (a dynamic exact `3.0` converts, `3.5` becomes an error value — the runtime analogue
   of Swift's checked `Int(exactly:)`).
13. **t2's `var`-path `null`** — mechanism narrowed but not step-verified (§5.1); verify while
   implementing P0 so the fix isn't aimed at a ghost.
14. **Constrained types — DECIDED 2026-07-29 (user): the validator is the checker** for
   `T where …`; the implementation may be deferred to a future stage. Interim honesty: until
   predicate evaluation lands in the validator, constrained checks are base-type-only (today's
   `fn_is` behavior) — acceptable as a *documented* deferral; no compile-reject needed.
15. **`STRING` in the native-return set** — P2 audits the four ad-hoc widenings; the clean fix
   (widen `mir_is_native_scalar_value_type` or stop widening ad hoc) interacts with the perf
   stage's ABI plans; decide there, enforce honestly here.
