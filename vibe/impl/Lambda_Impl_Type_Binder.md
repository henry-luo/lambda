# Lambda Impl Plan: Type Binder (`as T`) and Type Parameters

- **Date:** 2026-09-15
- **Status:** PLAN — nothing built. Phases TG-P0 … TG-P4 mirror the adoption
  order in the design doc §7; each phase is independently landable and gated.
  No open design item blocks any phase (TGO1 closed by TG20, 2026-09-15).
- **Design authority:** `vibe/Lambda_Design_Type_Generics.md` rev 7 (TG1–TG20,
  TGO1–TGO13). This doc implements only *ratified* items and never resolves
  an open design question in code — where an open item gates a slice, the
  interim disposition is stated and the slice stays inside it.
- **Formal authority:** S1.6, S1.7, S4.2.2 (narrowest type); D2.4.1 (the
  `Type*` graph is the contract), D3.1.1v3 (type-value kinds), D3.3.3v3
  (narrowing dies with binding; carrier certificates), D3.4.2 (structural
  shape identity), D6.2.1 (function values), D8.1.1v10 (tier admission).
  TGO8: the `S#`/`D#` entries for the binder itself are written at adoption,
  not by this plan; `doc/Doc_Convention.md` §4 makes the vibe record
  normative until then.
- **Related:** `vibe/Lambda_Design_Dual_Func_Compiling.md` (DF8 check in
  callee, DF9 boxed generic entry), `vibe/Lambda_Design_Type_Enforcement.md`
  (TE-15/17/18), `vibe/impl/Lambda_Impl_Type_Infer (done).md` (the
  `--emit-ast-dump` probe this plan reuses), `vibe/Lambda_Design_Const_Pool.md`
  (TGO12).
- **ID series:** `TG-P#` phases, `TG-P#.#` slices — extends the TG area
  series per `doc/Doc_Convention.md` §4 (no new series).

## 0. Ground truth this plan builds on (verified 2026-09-14/15)

- **`T: type` parameters parse; dependent scoping is absent.** A name in type
  position resolves in `parse_primary`
  (`lambda/runtime/parse_type_pattern.cpp:858`) through `lookup_name`; when
  the entry is an `AST_NODE_PARAM` the node takes the parameter's `TypeParam`,
  whose compact prefix is the `type` meta-type. `a: T` today therefore means
  "`a` is a type value". TG-P0 replaces this.
- **`as` is a lexer keyword** (`lambda_lexer.c:199`) used only by `for … group
  by k as alias` (`lambda_parser.c:1144`). The annotation scanner
  `parse_type_slot` (`lambda_parser.c:581`) breaks on any token it does not
  know, then hands the text to the type-pattern text parser
  (`parse_type_pattern_text_span`, `build_ast.cpp:11766`). **Both** need
  the keyword (TG-P2). `that` is already handled next to the slot in
  `parse_annotation_type_slot_value` (`lambda_parser.c:645`) and reduced by
  `direct_constrained_type` (`build_ast.cpp:8398`); `as` goes in the same
  place, after `that` (TG16).
- **Boundary machinery exists in all three places** and is the hook (DF8):
  runtime `lambda_type_check(Item, Type*, boundary)`
  (`lambda-eval.cpp:1767`) over `runtime_type_admit_value`
  (`lambda-eval.cpp:10740`); interpreter `interp_coerce_parameter_binding`
  (`interp.cpp:685`) called per parameter at frame entry (`interp.cpp:1294`);
  MIR `emit_parameter_boundary` (`transpile-mir.cpp:6104`) used by
  `mir_flow_admit_argument` at call sites (`transpile-mir.cpp:19149`, `:25560`)
  and for record returns (`:20754`). Contracts live in
  `TypeParam::contract_type` / `full_type` and `TypeFunc::return_contract`
  (`lambda-data.hpp:1012`, `:1025`).
- **`that` bounds are not enforced at runtime**: `runtime_boundary_unwrap_type`
  (`lambda-eval.cpp:1558`) strips `TYPE_KIND_CONSTRAINED` to its base and
  `fn_is` checks the base only (`lambda-eval.cpp:1836`). TG6v2 deliberately
  does **not** route bounds through this path.
- **Type kinds** are the `TypeKind` enum at `lambda/lambda.hpp:17`
  (SIMPLE, UNARY, BINARY, PATTERN, CONSTRAINED, RANGE, PARAM). Kind switches
  that must learn the new kinds: `input_schema_collect_type`
  (`lambda-eval.cpp:4120`), `runtime_boundary_unwrap_type`,
  `unwrap_simple_type_type` (`build_ast.cpp:538`), `emit_ast_dump.cpp`, the
  validator's type walker, and `type_contract.cpp`.
- **`fn_type()` is shallow** (`lambda-eval.cpp:3909`): arrays collapse to
  `array`, anonymous maps to `map`, objects report their nominal record. The
  binder oracle (TG18, S4.2.2) needs more than this; TG-P1.2 builds it once
  and `type()` is aligned to it (TGO13 residue).
- **Typed carriers carry proofs**: `ArrayRepCert` / `lambda_array_rep_proves_cert`
  (`lambda-eval.cpp:10787`) expose the leaf lane, which TG18 lets the binder
  use instead of walking elements.
- **Function types already require named parameters**
  (`parse_type_pattern.cpp:769`), so level-3 binders in `fn (...)` types are
  a suffix, not new structure. The return-annotation parser is atom +
  occurrence + set operators (`parse_type_pattern.cpp:1091`); TG17 forbids
  binders there, so it only needs the *error*.
- **Comparison operators**: lexer two-char scan at `lambda_lexer.c:621`
  (`<`/`<=`), Pratt table `lambda_parser.c:1419` (`is`/`in` share
  `LAMBDA_BP_MEMBERSHIP`), `Operator` enum `ast-core.hpp:~198`, reference
  grammar `grammar.js:112–135`. `<:` slots in beside `is` (TG-P0.1).
- **Optional parameters** are `a?: T` (marker on the name); the contract is
  already wrapped nullable by `parameter_contract_for_declared`
  (`build_ast.cpp:697`) and an omitted argument is filled with `null`
  (`doc/Lambda_Func.md` §Optional Parameters). TG18's "`a?: X as T` ≡
  `a: (X as T)?`" is therefore free: the binder sits under that wrapper.
- **Tests** are enumerated by directory (`test/test_lambda_gtest.cpp:19`),
  golden `.txt` beside each `.ls`; the T0 sweep list is
  `test/lambda/interp_p0_subset.txt` (regenerated by
  `test/interp/refresh_lists.py`); `--emit-ast-dump` asserts static types.
- **Satellite admission** (D8.1.1v10): plain `any` parameters are admitted
  to satellites and travel boxed. A binder-carrying parameter is treated
  exactly like `any` for lane purposes until TG-P4.

Standing gates for every phase: `make test-lambda-baseline` at its current
pass set (nothing new may fail; the 11 pre-existing conc/proc E221 failures
are the known baseline), `make test-radiant-baseline` untouched, every new
`.ls` fixture byte-identical under `LAMBDA_TIER=interp`, `jit` and the
default `AUTO` selector, and `--emit-ast-dump` assertions for every static
claim. Reminder from the memory ledger: `make test-lambda-baseline`
overwrites `lambda.exe` with the debug build and `make release` deletes
`test/*.exe` — rebuild before timing anything.

## 1. Scope and non-goals

**In scope (this plan):** TG2 explicit type parameters with dependent
scoping; TG3/TG5 `as T` binders at any depth inside parameter annotations
(level 1, TG10); TG6v2 direct bounds; TG7 dynamic tier fully, static tier as
propagation + bound-typed bodies; TG9 `T` in body scope; TG13 duplicate-binder
error; TG14v2 relations (they are existing `that` machinery); TG15/TG16
precedence; TG17 scope/collision/return-position/`var` rules; TG18
container/unbound rules; TG19 `<:`.

**Out of scope (later plans):** TG10 level 2 and level 3 per-value binders
in `type` bodies and schemas (need env threading through the validator and
TGO9); TG8/TG-P4 specialization keyed on bound types; TG13's solver; TGO12
const-pool serialization of binder records across the module boundary
(representation is kept relocatable now so that plan needs no rework);
TGO10 catalog operations (`T.fields`, `T.element` — note §6.0 claims
`T.element` exists but no runtime site was found; verify before relying on it).

## 2. Representation (TG-P0.3, shared by every phase)

Two new `TypeKind`s, both under `LMD_TYPE_TYPE` per D3.1.1v3:

```c
// binder SITE: `bound as name` — TG6v2 bound is a direct field, no predicate
typedef struct TypeBinder : Type {
    Type* bound;          // the whole written type at the site (TG15)
    Name* name;           // bound type name (TG12 vocabulary)
    uint16_t slot;        // index into the signature's binder env
    int type_index;
} TypeBinder;

// bound-name USE: `T` anywhere after its binder in the signature
typedef struct TypeBoundRef : Type {
    uint16_t slot;        // same env index; NO back-pointer (TGO12 relocatable)
    Type* bound;          // copied for lane decisions and diagnostics
    int type_index;
} TypeBoundRef;
```

- `TypeFunc` gains `uint16_t binder_count` and a `TypeBinder** binders`
  table (slot-ordered) so the boxed entry can size and initialise the env.
- An explicit `T: type` parameter (TG2) is registered as a binder whose
  site is the argument itself: its `TypeBinder` has `bound = &TYPE_TYPE` and
  the env slot is filled from the *value* of the argument at entry. Both
  spellings then share every downstream path (TG3 made literal).
- The bound name is registered in the function scope via
  `lambda_ast_register_name` (`build_ast.cpp:2339`) as an immutable
  `type`-typed `NameEntry` flagged `is_binder` with its slot (TG17). Reads in
  the body compile like any `type`-valued local (TG9).
- A function whose signature contains a binder or ref has, for lane purposes,
  `any` at that parameter: `mir_contract_native_scalar_type` and
  `mir_param_uses_native_lane` (`transpile-mir.cpp:3326`) answer "boxed" for
  the new kinds. This is what keeps D8.1.1v10 untouched.

Env contract (TG18): `Type* env[binder_count]`, every slot initialised to
its binder's `bound`; a binder site overwrites its slot when admission reaches
it; nothing else ever writes a slot. Unbound is therefore not a state.

## 3. Phases

### TG-P0 — `<:` operator and dependent scoping

**TG-P0.1 `<:` (TG19).**
- Lexer: `<:` two-char token `LAMBDA_TOK_SUBTYPE` at the `<` arm
  (`lambda_lexer.c:621`); tree-sitter `grammar.js` `mk('<:', 'is_in', 'left')`
  beside `is`, then `make generate-grammar` (the `lambda-cst` verifier must
  keep accepting the corpus; `compare` mode is the gate).
- Parser: Pratt entry `[LAMBDA_TOK_SUBTYPE] = LAMBDA_BP_MEMBERSHIP`
  (`lambda_parser.c:1419`); `OPERATOR_SUBTYPE` in `ast-core.hpp`; build_ast
  binary typing: both operands must be type values (reuse
  `ast_is_explicit_type_value`), result `bool`, else the existing operand
  type error.
- Runtime: `Bool fn_subtype(Item a, Item b)` in `lambda-eval.cpp` beside
  `fn_is`: unwrap both through `runtime_boundary_unwrap_type`, then
  `lambda_type_contract_semantically_compatible(a, b)`
  (`type_contract.cpp:345`) — the admission relation lifted to types, TG19 —
  plus the nominal-base walk (`TypeNominal::base`, S2.1.4) which the
  contract relation does not do today. `T <: T` must hold for every `T`;
  add a pointer-equality short-circuit first.
- Interp `eval_binary` case and MIR emission as a `fn_subtype` call (same
  pattern as `OPERATOR_IS`, `interp.cpp:947`, `transpile-mir.cpp:5658`).
- Fixture `test/lambda/type_subtype_op.ls` + `.txt`: numeric tower, unions,
  structural maps, nominal base, reflexivity, non-type operands rejected.

**TG-P0.2 Dependent scoping for `T: type` (TG2).**
- `build_param_from_parts` (`build_ast.cpp:9594`): when the declared type is
  the `type` meta-type, allocate a `TypeBinder` (bound `&TYPE_TYPE`, next
  slot), append to the function's binder table, and mark the `NameEntry`
  `is_binder`.
- `parse_primary` (`parse_type_pattern.cpp:858`): when `lookup_name` yields an
  entry flagged `is_binder`, emit an `AST_NODE_TYPE` whose type is a
  `TypeBoundRef{slot}` instead of the parameter's `TypeParam`. This is the
  line that retires the "`a: T` means `a` is a type" behaviour.
- Return contract: the same resolution applies in
  `parse_return_type_pattern`; a *binder* there is `ERR_BINDER_IN_RETURN`
  (TG17), a *ref* is fine.
- Static typing of a ref inside the body: its static type is its `bound`
  (TGO5 interim, TG18 "unbound behaves as the bound").
- Fixture `test/lambda/type_param_dependent.ls`: `fn f(T: type, a: T) T`,
  `fn Pair(T: type) type => {a: T, b: T}`, `f(int, 1)` passes, `f(int, "x")`
  fails at the boundary with the parameter's blame string.

Gate: baseline green; `--emit-ast-dump` shows `(TypeBoundRef slot 0)` on
`a`'s contract; three tiers identical.

### TG-P1 — Dynamic tier: binding at the boundary (TG7 dynamic half)

**TG-P1.1 Env-aware admission.**
- Add `Item lambda_type_check_env(Item value, Type* expected, Type** env,
  const char* boundary)`; keep `lambda_type_check` as the `env == NULL`
  wrapper so its ~20 existing call sites are untouched. Thread `env` through
  `runtime_type_admit_value` → `runtime_type_admit_array` → the validator
  entry `runtime_validate_value_against_type` (it recurses into element and
  field contracts, which is where nested binders live).
- `TYPE_KIND_BINDER` arm: admit `value` against `binder->bound` (ordinary
  path, TG6v2); on success `env[slot] = binder_narrowest_type(value, bound)`
  and return the converted value. `TYPE_KIND_BOUND_REF` arm: admit against
  `env[slot]`; on failure produce the TG4 blame:
  "`b` must have the same type as `a` (int); got float" — the binder's
  parameter name comes from the binder table, so `TypeBinder` needs the
  owning parameter's name or index recorded at build time.
- Every kind switch listed in §0 gains the two kinds (unwrap → bound;
  schema collect → bound; dump → `(binder name slot)` / `(ref slot)`).

**TG-P1.2 The oracle (TG18, TGO13 residue).**
- `Type* binder_narrowest_type(Item value, Type* bound)`: scalars per
  S4.2.2 (reuse `item_static_type_for_is`, `lambda-eval.cpp:1517`, which
  already knows the poison-merge and sized-int rules); objects → their
  nominal record; named maps → their `TypeMap`; arrays with a proven
  `ArrayRepCert` → the certified leaf lane wrapped as `T[]`; anything else →
  the value's structural type. The binder *site* decides what it binds: a
  binder at element position binds from the **first element** and later
  elements are checked (TG18); an empty container leaves the slot as its
  bound. Implement first-element-binds inside the element loop the validator
  already runs, not as a pre-pass.
- Align `fn_type()` with this function for the cases it collapses today
  (arrays, anonymous maps) **only if** the baseline goldens allow; otherwise
  expose the oracle as a system function (`narrowest_type(x)`, registry
  `sys_func_registry.c`) and record which was done. Either way TG14v2's
  `that (T <: type(~))` must agree with the binder — add a fixture that
  asserts it.

**TG-P1.3 Interpreter frame env.**
- `InterpFrame` gains `Type** binder_env` sized by `binder_count`,
  initialised from the binder table at frame entry (`interp.cpp:1280`); the
  per-parameter loop passes the env to `interp_coerce_parameter_binding`
  (`interp.cpp:685`); the return boundary uses the same env; a bound-name
  read in the body loads `env[slot]` and boxes it as a `type` Item.
- Rooting: env slots may hold heap `TypeType`s built by the oracle
  (`fn_type` uses `heap_calloc`). Give the frame a precise root range over
  the env (`RootFrame`/`Rooted`, D5, rule 15 — no conservative scanning).
  Compile-time `Type*`s are pool-immortal and need nothing.

**TG-P1.4 JIT boxed entry.**
- Binder-carrying functions take the **boxed generic entry only** (DF9) in
  this phase: `mir_function_has_binders` forces the `_b`/dynamic ABI and
  skips native-lane generation, so admission happens in the callee prologue
  exactly like the interpreter. Emit an env as a stack-allocated `MIR` local
  array registered with the function's GC root frame; per-parameter
  `emit_parameter_boundary` grows an `env` operand and calls
  `lambda_type_check_env`; the return boundary at `"function return"` does
  the same; a bound-name read loads the slot.
- Direct native edges to such functions are deferred to TG-P3/P4 where the
  static tier has the bindings; until then `mir_flow_admit_argument` at the
  call site must **not** be used for binder contracts (it has no env) — assert
  this in debug builds.

**TG-P1.5 `var` parameters (TG17).** The env is written once at entry; the
CW33 home transport and the `_b` write-back path never touch it. Fixture
`test/lambda/proc/type_binder_var_param.ls` proves a body write that changes
the parameter's type leaves `T` as bound at entry.

Gate: fixtures `type_binder_scalar.ls` (`max`-style order dependence from
TG4, `f(1, 2.5)` fails and `f(2.5, 1)` passes), `type_binder_array.ls`
(`sum`, `first`, empty array returns under the bound), `type_binder_optional.ls`
(`a?: number as T` omitted via `pick(b: 2)` and via trailing omission;
`(number as T)?` with `null`; `number? as T` with `null` binding `null`),
`type_binder_relation.ls` (TG14v2 `that (T <: U)`), each byte-identical
across tiers; `interp_p0_subset.txt` refreshed; `LAMBDA_GC_FORCE_EVERY=1` run
over the new fixtures for the env rooting.

### TG-P2 — Surface syntax: `as T` (TG3, TG5, TG15, TG16, TG17)

**TG-P2.1 First-party parser.**
- `parse_type_slot` (`lambda_parser.c:581`): after a complete type (at
  `need_atom == false`, nesting 0) accept `LAMBDA_TOK_AS` + identifier as a
  suffix and keep scanning (a following `|`/`&`/`!` after the name is an
  error per TG15 — `as` is loosest, so nothing may follow it at that level).
  Inside parentheses the balanced consumer already swallows `as name`.
- `parse_annotation_type_slot_value` (`:645`): after the optional `that`,
  accept `as IDENT` and reduce with a new
  `LAMBDA_REDUCTION_FLAG_ANNOTATION_BINDER` carrying the name token; a `that`
  after the name is `ERR_BINDER_TRAILING_THAT` (TG16).
- `build_ast.cpp:11766` `LAMBDA_REDUCE_TYPE_SLOT`: the binder flag wraps the
  reduced type in a `TypeBinder` (next slot, name registered per TG17) via a
  new `direct_binder_type`, mirroring `direct_constrained_type`.
- `parse_type_pattern.cpp`: `as IDENT` suffix accepted at the top of
  `parse_union` and inside `parse_paren_type` only — never in
  `apply_occurrence`, `parse_unary` or the set-operator loops, which is the
  precedence table of TG15 by construction. The binder name is registered
  at parse time so a *later* parameter's `parse_primary` resolves it as a
  ref (left-to-right, TG14v2). The parser must reject a name that is a base
  type (`lookup_base_type_name`), an alias in scope, or a parameter of the
  signature (`ERR_BINDER_COLLISION`); a second `as T` in one signature is
  `ERR_BINDER_DUPLICATE` with TG13's message; a ref to a name bound *later*
  is `ERR_BINDER_FORWARD_REF`.
- Return position: `parse_return_type_pattern` rejects `as` with
  `ERR_BINDER_IN_RETURN`.
- Error codes: next free values in the semantic 2xx block of
  `lambda/runtime/lambda-error.h`; each with a `test/lambda/errors/` fixture.

**TG-P2.2 Reference grammar.** `grammar.js`: `binder_type` as a suffix of
`_annotation_type` (after `constrained_type`), allowed inside `paren_type`;
`make generate-grammar`; the `lambda-cst` `compare` mode must accept every
fixture from TG-P1 rewritten in `as` syntax.

**TG-P2.3 Rewrite the TG-P1 fixtures** from the explicit `T: type` form to
`as T` where the design's examples use it, keeping both spellings covered.
`doc/Lambda_Type.md` §First-Class Types gets the binder section and the
`fn identity<T>` line (`:1195`) is replaced by the TG2/TG3 forms (TGO8's doc
half).

Gate: baseline green; `LAMBDA_PARSER=tree` reference path still parses the
corpus; every diagnostic fixture reports its code once, at the right span.

### TG-P3 — Static tier: propagation and bound-typed bodies (TG7 static half)

**TG-P3.1 Call-site propagation.** In `function_call_result_type`
(`build_ast.cpp:734`) and the argument-typing walk of
`build_call_node_from_parts` (`:9163`): for each binder, if the argument's
static type at the binder position is concrete, record it in a per-call
substitution; rewrite refs in later parameter contracts and in the return
contract through the substitution (a fresh `Type*` per call site, registered
in `type_list` — no mutation of the signature). `sum(ints)` then types as
`int` at the call site. When the argument is `any`, the ref stays its bound.

**TG-P3.2 Static mismatch.** A concrete argument at a *ref* position that
`lambda_type_contract_semantically_compatible` rejects against the
substituted type is a compile error with the TG4 blame text — the static
timing of the same contract (TG7). Order dependence is preserved verbatim
(`max(1, 2.5)` fails, `max(2.5, 1)` passes).

**TG-P3.3 Body checking.** `T` types as its bound inside the body (TGO5
interim). Nothing else: no abstract once-for-all verification yet, no
operation tables. TG20 (ratified 2026-09-15) fixes what the static tier
binds: the argument expression's **static** type, never a narrowing the
checker cannot see — so `let x: number = 1; max(x, 2.5)` substitutes
`T := number` and passes statically, while `max(1, 2.5)` binds `int` and
fails. TG-P3.1's substitution must therefore read the argument node's
established static type (`get_effective_type`, TIG1 discipline), and the
static blame text names the declaration it bound from; the dynamic blame
text names the value (TG20 diagnostics).

**TG-P3.4 Elision.** Where TG-P3.1 proved a ref's substituted type equals the
argument's static type, `mir_boundary_is_redundant` elides the callee check
(TE-17). Direct native edges to binder-carrying functions become legal only
for call sites whose substitution is complete; otherwise the boxed entry of
TG-P1.4 stays.

Gate: `--emit-ast-dump` asserts substituted result types on the fixtures;
`test_lambda_opt_gtest` gains the elision witnesses; three tiers identical.

### TG-P4 — Specialization keyed on bound types (TG8)

Deferred. Prerequisite reading before starting: the satellite cluster notes
(D8.1.1v9/v10) and the "never re-derive an admission per call" lesson cited
in TG8. Key = argument-type tuple **plus** env; boxed entry remains the
always-correct fallback; cap per function. No design gap blocks it, only
priority.

## 4. Hazards and rules of engagement

- **Do not widen `lambda_type_check`'s signature.** Add the `_env` variant;
  the existing symbol is called from the MIR emitter by name
  (`transpile-mir.cpp:5268`).
- **Kind switches fail closed.** Any switch over `TypeKind` that meets the
  new kinds without an arm must reach the `bound` — never `default: true` —
  or unions such as `string | error` in shaped fields regress the way the
  `type_field_unwrap_simple_decl` comment in `lambda-data.hpp:1155` warns.
- **Env rooting is precise** (rule 15). Heap `TypeType`s from the oracle live
  in the env; root the range, never rely on stack scanning.
- **No native lanes for binder contracts until TG-P3.4.** The
  `LAMBDA_ROOT_WITNESS` and `LAMBDA_MIR_SPECIALIZATION_PROFILE` probes are
  the way to confirm a binder-carrying function took the boxed entry.
- **Vendored code is off limits** (rule 16): the tree-sitter grammar under
  `lambda/tree-sitter-lambda/` is Lambda's own and *is* edited; the runtime
  under `lambda/tree-sitter/` is not.
- **Goldens across tiers.** Every fixture ships with its `.txt` (rule 8) and
  is run under all three tier selectors before it is called green; JIT-only
  defects hide behind T0 goldens (memory ledger, Tune27).
- **Level 2/3 stays out** until the validator takes an env; do not let a
  `type` body with `as` parse silently — reject it with a clear
  "not yet supported" diagnostic in TG-P2.1 so the syntax is reserved.

## 5. Fixture matrix

| Fixture | Phase | Proves |
|---|---|---|
| `type_subtype_op.ls` | P0.1 | TG19 relation, reflexivity, nominal base, non-type operands |
| `type_param_dependent.ls` | P0.2 | TG2 dependent scoping, `type`-returning fns |
| `type_binder_scalar.ls` | P1 | TG4 order dependence, blame text |
| `type_binder_array.ls` | P1 | TG18 first-element-binds, empty → bound |
| `type_binder_optional.ls` | P1 | TG18 `a?:`, `(X as T)?`, `X? as T` with `null` |
| `type_binder_relation.ls` | P1 | TG14v2 `that (T <: U)`, oracle agreement with `type(~)` |
| `proc/type_binder_var_param.ls` | P1.5 | TG17 bind-once under CW33 write-back |
| `errors/type_binder_*.ls` | P2 | TG13 duplicate, TG14v2 forward ref, TG16 trailing `that`, TG17 collision / return position |
| `type_binder_static.ls` (+ ast-dump) | P3 | call-site substitution, static mismatch, elision, TG20 `max(x, 2.5)` vs `max(1, 2.5)` across tiers |

## 6. Open design items carried, and what each gates

| Item | Gates | Interim in this plan |
|---|---|---|
| TGO3 elision of explicit `T: type` args | nothing | explicit always, Zig rule |
| TGO5 abstract-body admissibility | TG-P3 precision | `T` types as its bound |
| TGO6 exported-API annotation lint | nothing | convention only |
| TGO8 formal-spec entries + doc text | adoption | doc half done in TG-P2.3 |
| TGO9 level-2 re-bind under mutation | level 2 plan | out of scope |
| TGO10 catalog ops (`T.element` unverified) | nothing here | verify before use |
| TGO12 const-pool serialization | module boundary plan | slots not pointers (§2) |
