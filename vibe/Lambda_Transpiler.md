# Lambda Transpiler — Design Record

- **Status:** CURRENT DESIGN RECORD. Consolidates and replaces the retired
  `Lambda_Transpile_*.md` set — 10 documents, archived under
  `vibe/impl/` with the `(retired)` suffix (Doc_Convention §5); see
  [Appendix C](#appendix-c--source-documents-consolidated).
- **Date:** 2026-08-28.
- **Scope:** how a Lambda AST becomes native code — the back-end ABI contract,
  the value-representation convention the emitter must uphold, how type
  knowledge is acquired and spent, the hot-path lowerings, and the compile-time
  cost structure. Covers `lambda/runtime/transpile-mir.cpp` (MIR Direct, the
  only back end) and the shared emitter helpers in
  `lambda/runtime/transpile_shared.cpp` / `transpiler.hpp`.
- **Ledger:** `DD#` — the transpiler design-decision series. **DD1–DD4 keep the
  meanings they had in `Lambda_Transpile_Map.md`** (now
  `impl/Lambda_Transpile_Map (retired).md`) (cited from
  `doc/Lambda_Formal_Design.md` D3.4 and §D3 as *"Transpile_Map DD1–DD4"*);
  DD5+ extend the series over the rest of the area.
- **Spec linkage:** D8 (compilation pipeline), D8.1 (structure), D8.3
  (dual-function compiling), D8.4 (dispatch policy), D8.5 (MIR module cache),
  D3.4 (shape and typed-map field layout), LC1 (specialization over caching).
- **Convention:** source references name symbols, not line numbers — line
  numbers drift.
- **Related:** [`Lambda_Design_Compiling.md`](Lambda_Design_Compiling.md) (LC1–LC2 — cross-cutting policy, wins on conflict),
  [`Lambda_Design_Compiling_Dual_Func.md`](Lambda_Design_Compiling_Dual_Func.md) (DF1–DF17 — the current specialization mechanism),
  [`Lambda_Design_Compiling_Return_Value.md`](Lambda_Design_Compiling_Return_Value.md) (return-convention v3),
  [`Lambda_Design_Compiling_Lane.md`](Lambda_Design_Compiling_Lane.md) (ValueRep lanes),
  [`Lambda_Design_Item_Boxing.md`](Lambda_Design_Item_Boxing.md) (Item representation),
  [`Lambda_Shape_Pool.md`](Lambda_Shape_Pool.md) (shape identity),
  [`Lambda_Design_MIR_Cache.md`](Lambda_Design_MIR_Cache.md),
  [`Lambda_Tune_Typed_Vs_C2MIR.md`](Lambda_Tune_Typed_Vs_C2MIR.md) (M1–M8 measured evidence).

---

## 0. Executive summary

Lambda compiled through **two** back ends for most of this record's history:

| Path | Pipeline | Status |
|---|---|---|
| **C2MIR** (`transpile.cpp`) | AST → C text → C2MIR → MIR → native | **REMOVED** (commit `a6d1ca0e8`, "legacy C transpiler removed"). Was frozen before that (CLAUDE.md rule 14). |
| **MIR Direct** (`transpile-mir.cpp`) | AST → MIR API → native | **The back end.** Default and only path. |

MIR Direct started as a ~330-line proof of concept, reached feature parity
(85/85 baseline) in Feb 2026, then overtook C2MIR on both axes: **2.6× faster
JIT compilation** (no ~42 KB `lambda.h` re-parse per module) and, after the
optimization work recorded here, **1.0×–10.4× faster generated code** across
the benchmark suites. The C path was retired once no benchmark still favoured
it.

Everything below that reads as a *decision* is current unless an entry in
[Appendix B](#appendix-b--superseded-and-rejected-rulings) supersedes it. The
C-path mechanisms (`_u`/`_w`/`_b` C function variants, `_store_i64`, C struct
typedef emission, `emit_struct_typedefs`) are **history** — their *rulings*
survive only where MIR Direct re-implemented them, and this document says so at
each point.

**The through-line.** Every large win in this record came from the same move:
*get a static type, then spend it*. Type knowledge (declared, or inferred from
call sites / body usage / shapes) converts a runtime-dispatched, heap-boxing
operation into a native MIR instruction sequence. Every large *bug* in this
record came from the same failure: **three places disagreeing about whether a
register holds a native value or a boxed `Item`** (DD6).

---

## 1. Architecture

### DD5 — MIR Direct is the compilation path; the emitter reuses the C runtime, never reimplements it

The transpiler emits MIR IR that **calls the same ~200 exported C runtime
functions** the C path called, resolved through `import_resolver()` in
`lambda/runtime/mir.c`. It does not re-derive `fn_add` semantics in IR.

Why direct MIR won:

1. **Compile time.** C2MIR re-parsed the embedded `lambda.h` (~42 KB, ~96 % of
   the generated C text) for *every* module. Profiling put C2MIR parse at
   46.1 % and MIR codegen at 43.6 % of JIT overhead — 89.7 % in two phases the
   transpiler could not influence. Emitting MIR directly deletes the first.
2. **Control.** Inlining decisions, custom calling conventions, speculative
   guards, and per-site lowerings are expressible in the MIR builder API and
   are not expressible through C text.
3. **Inspectability.** `MIR_output()` dumps the IR; debug builds write
   `temp/mir_dump.txt`.
4. **No C dialect games.** The C path leaned on GCC statement expressions
   `({...})` for value-producing blocks — a C2MIR-specific crutch. MIR Direct
   linearizes every expression into instructions writing a result register,
   which is the honest form of the same thing.

**Consequence (DD5.1):** every `transpile_*` function takes the AST node and
returns a `MIR_reg_t` holding the result. There is no expression-as-text
intermediate, so there is nowhere to hide an un-typed value: the register's
type is committed at emission.

### DD5.2 — The compilation pipeline is prepass-structured

`transpile_mir_ast()` runs ordered passes over the AST:

| Pass | Work |
|---|---|
| 1 | compile regex/string patterns |
| 2 | create BSS items for module-level variables |
| 3 | forward-declare all functions (**and cache inferred parameter types**) |
| 4 | define function bodies |
| 5 | emit the module main body |
| — | `MIR_link()` (resolve imports), `MIR_gen()` (native codegen) |

**Passes 3 and 4 must stay separate.** Merging them was proposed and rejected:
mutual recursion requires every sibling function to be forward-declared before
any sibling's body is transpiled (see [Appendix B](#appendix-b--superseded-and-rejected-rulings), R4).

---

## 2. The binary ABI contract

The ABI existed to let a C-compiled module and a MIR-compiled module call each
other. That cross-path requirement is gone with the C back end, but **the
contract itself is still live** — MIR Direct's own modules, the module BSS
layout, and the runtime's `fn_call*` dispatch all depend on it, and
`init_module_import()` still populates module blobs by pointer arithmetic over
computed offsets.

### DD5.3 — Type mapping: Lambda → MIR

`type_to_mir(TypeId)` is the single source of truth:

| Lambda type | MIR type | Note |
|---|---|---|
| `FLOAT` | `MIR_T_D` | the only non-I64 scalar lane |
| `INT`, `INT64`, `BOOL`, `DTIME` | `MIR_T_I64` | |
| `NULL`, `ANY`, `ERROR` | `MIR_T_I64` | carries a boxed `Item` |
| every pointer type (`STRING`, `SYMBOL`, `DECIMAL`, containers, `FUNC`, `TYPE`, `PATH`) | `MIR_T_P` → `MIR_T_I64` | MIR registers are I64; pointers ride in them |

MIR only has `MIR_T_I64 / MIR_T_F / MIR_T_D / MIR_T_LD` for registers. Pointers
are I64. `FLOAT` is therefore the only type whose lane choice can go wrong, and
in practice most representation bugs are float-shaped.

### DD5.4 — Function naming is mangled, not chosen

| Symbol kind | Pattern | Example |
|---|---|---|
| named function | `_<name><byte_offset>` | `_square42` |
| anonymous function | `_f<byte_offset>` | `_f317` |
| variable | `_<name>` | `_x` |
| imported member | `m<idx>._<name><offset>` | `m1._square42` |
| closure env | `Env_f<byte_offset>` | `Env_f42` |
| module entry points | `main`, `_init_mod_vars`, `_init_mod_consts`, `_init_mod_types` | fixed |

The byte offset of the definition site disambiguates same-named functions in
different scopes. The mangling helpers are shared through `transpiler.hpp` and
are **never** re-derived at a call site.

### DD5.5 — Signature rules

```
return:  is_closure || can_raise  →  Item (MIR_T_I64)
         otherwise                →  type_to_mir(declared/inferred return)

params:  [void* _env_ptr]                    if closure
         per param: is_closure || optional   → Item
                    otherwise                → type_to_mir(param type)
         [List* _vargs]                      if variadic
```

Closures and error-raising functions are pinned to the boxed ABI on purpose:
a closure's captured environment is an array of `Item`s, and an error return
must be able to carry a value of a type the signature does not name.

### DD5.6 — Module struct layout is positional

A module's BSS blob `m<N>` is laid out sequentially, and the order is derived
from **AST traversal order**, not from any table:

```
+0   void**       consts
+8   main_func_t  _mod_main        Item (*)(Context*)
+16  init_vars_fn _init_vars       void (*)(void*)
+24  fn_ptr       pub_func_1       ... in AST order
     <type>       pub_var_1        ... in AST order, natural alignment
```

Field access uses `MIR_new_mem_op()` with byte offsets computed at transpile
time. Any change to how public members are enumerated silently corrupts every
importer — this is the most fragile invariant in the back end.

### DD5.7 — Closures are called through the runtime, with one shape

`fn_call0..3()` / `fn_call()` extract the function and environment pointers
from the `Function` object and call `fn_ptr(env_ptr, arg1, ...)`. Every closure
body is therefore emitted as `Item f(void* _env_ptr, Item p1, ...)`. Captured
variables are `Item`-sized (8 bytes) and addressed as
`MEM(env_ptr, index * 8, I64)` — the C path's named `struct Env_*` was a
readability affordance, not a layout requirement.

---

## 3. The representation contract

### DD6 — One expression, one committed representation, agreed in three places

This is the load-bearing invariant of the whole back end.

> `transpile_expr()` returns a **native value** (`int64_t`, `double`, raw
> pointer) for an expression whose static type is concrete, and a **boxed
> `Item`** for `ANY` / `ERROR` / `NULL`. `transpile_box_item()` converts native
> → boxed at a boundary that demands it. `emit_unbox()` converts boxed →
> native after a call that returns boxed.

Three functions must agree about every expression form, and a disagreement is
always a bug:

1. **`get_effective_type()`** — what type does this expression *claim*?
2. **The emitter** (`transpile_binary`, `transpile_index`, …) — what does it
   actually *put in the register*?
3. **`transpile_box_item()`** — does the result still need boxing?

**Failure signature.** When (1) says `ANY` but (2) produced a native
`int64_t 0x1`, the value is then treated as a tagged pointer and dereferenced —
segfaults at addresses like `0x1`, `0x2`, `0x5`. When (1) says `FLOAT` but (2)
produced an I64 register, MIR's own verifier catches it at module finalization:
*"unexpected operand mode for operand #1. Got 'double', expected 'int'"*.

**The rule that follows (DD6.1):** whenever a new native fast path is added for
an operator, all three places must be updated in the same change. This was
learned the expensive way — the C path had a `binary_already_returns_item()`
predicate plus an operator-specific workaround `is_idiv_expr()` that both had
to be flipped when `%` and `//` gained native lowerings, and missing either one
produced silent double-boxing.

**The corollary that follows (DD6.2):** consumers must read
`get_effective_type(mt, node)`, **never** `node->type->type_id` directly. The
raw AST type predates inference; the effective type reflects it. A single audit
found and fixed this same mistake in **25+ locations** (index, index-assign,
array push, match scrutinee and patterns, for-collection and every
comprehension clause, decompose source, `vmap_*` arguments, dynamic-call
callee, pipe operands, raise value, query args, path segments, parent
expressions, member dispatch, unary operand).

**Where DD6 is going.** Expression results carry no first-class representation
tag today — DD6 is a discipline enforced by review, not by the type system. The
successor design is `ValueRep` extended from variables to expression results
(see `Lambda_Design_Compiling_Lane.md`; the transpiler must never read back
`MIR_reg_type` to recover a lane).

### DD6.3 — Terminal branches must stop emission

A `return` (or a TCO jump, §7) sets `mt->block_returned`. Subsequent statements
in the same straight-line block are dead and must not be transpiled: emitting
them meant boxing a dummy register of the wrong MIR type. The flag is reset at
branch merge points and saved/restored across nested function definitions so it
cannot leak between sequentially compiled functions.

---

## 4. Typed maps and objects: direct struct access

**Spec:** D3.4 (shape and typed-map field layout). Ledger IDs DD1–DD4 below are
the original `Lambda_Transpile_Map.md` design decisions, unchanged.

### The decision

A Lambda map is already a **packed C struct** at runtime: `ShapeEntry` defines
the layout and `byte_offset` is pre-computed. When the transpiler knows a map's
exact type, it emits a **direct byte-offset load/store** against `map->data`
instead of a runtime call. This replaces:

- read `v.f` — `map_get`/`fn_member`: allocate a name, tag it, O(N) `strncmp`
  scan of the shape list, type switch, box
- write `v.f = x` — `fn_map_set`: same scan plus a type check
- construction — `map_fill`: va_list walk with a per-field type switch
- method preamble/write-back — one `heap_create_name` + scan **per field**

with a pointer dereference at a compile-time constant offset.

### Eligibility (the gate)

`has_fixed_shape(TypeMap*)` in `transpile_shared.cpp` requires **all** of:

1. **Named type** — `TypeMap::struct_name` is set, i.e. the shape came from a
   `type Name = {…}` (or `type Name { … }`) declaration. Anonymous map literals
   are excluded because `fn_map_set` may rebuild their shape at runtime.
2. **Fixed shape** — every field named (no spread entries).
3. **8-byte-aligned offsets** — map literals pack by
   `type_info[type_id].byte_size` (`bool` = 1 byte), so an offset like 42 is
   reachable; MIR's instruction selector rejects unaligned memory operands
   outright (`fatal failure in matching insn: mov hr0, u64:42(hr0):pc`).
4. **Concrete field type** — not `ANY`/`NULL`/`ERROR`.

Anything else falls back to the runtime path. A separate, weaker layout-only
predicate exists for inferred literal shapes, which need the layout half of the
test without the `struct_name` half.

### DD1 — Nested typed maps are supported; the chain breaks at the first `any`

`o.pos.x` resolves step by step at transpile time: `pos` is looked up in
`Outer`'s shape → its type is `Inner` (a `TypeMap`) → `x` is looked up in
`Inner`'s shape. Each step is a known-offset dereference, so depth costs
nothing. If any intermediate field is `any` or an untyped map, the chain falls
back to `fn_member` **from that point onward**.

### DD2 — `input()` results always take the slow path

Input parsers (JSON, XML, YAML, …) build shapes dynamically from the data.
Fields may be missing, differently typed, or differently ordered than a
declared type claims. Compile-time offsets computed from the declaration would
read garbage. Provenance is tracked: variables assigned from `input()` (or
derived from one without an explicit annotation) are excluded.

*Future opening:* schema-directed parsing (`input(path, {schema: T})`) would let
the parser guarantee the declared layout, at which point the fast path becomes
sound for input data. Not implemented.

### DD3 — Writes take the fast path only when type-compatible

In procedural code, assigning a differently typed value to a field triggers
`map_rebuild_for_type_change` inside `fn_map_set` — the struct layout itself
changes, which a compile-time offset cannot follow.

| `v.x = …` | field | value | path |
|---|---|---|---|
| `42` | `int` | `int` | **fast** |
| `3.14` | `float` | `float` | **fast** |
| `3.14` | `float` | `int` | **fast** — lossless widening |
| `42` | `int` | `int64` | **fast** — same byte size |
| `"hello"` | `int` | `string` | slow — type change |
| `val` | `int` | `any` | slow — RHS unknown |

Immutable `let` bindings are always safe for fast reads: the shape never
changes after construction.

### DD4 — Layout must match exactly, and the field must be 8-byte aligned

The generated access reads the same bytes `_map_read_field` / `map_field_store`
would. The alignment requirement is not stylistic — it is MIR's (see gate 3).

### DD4.1 — Container-typed fields are stored raw and must be re-tagged on read

`map_field_store()` stores a **raw `Container*`** for container fields (NULL for
a null value), while the transpiler works in tagged `Item`s. So:

- **read:** load the raw word; `raw == 0 → ITEM_NULL`, else
  `(type_tag << 56) | raw`
- **write:** box the value, then **strip** the tag before storing the pointer

Getting this wrong is silent: `ITEM_NULL` is `0x0100000000000000`, which
reinterpreted as a pointer is non-NULL and points at nothing.

### DD4.2 — Reads in a native context are not boxed at all

Resolving a member expression's AST type to the actual field type (rather than
leaving it `ANY`) is what lets binary arithmetic take its native path. With
that in place, `p.x + p.y` on `{x: int, y: int}` lowers to a single native add,
and `v.x + v.y` on `{x: float, y: float}` costs **zero heap allocations** —
before, each float read went through a boxing helper that allocated.

The fallback path must then unbox symmetrically: when direct access is
unavailable, the `fn_member` result is unboxed to match the resolved type, so
`transpile_expr` keeps its DD6 contract regardless of which path fired.

### DD4.3 — Typed map construction is two-phase, for GC safety

Direct construction writes fields at byte offsets into `map->data`. Naively:

```c
*(void**)((char*)m->data + off) = (void*)(make_tree(depth-1) & MASK);
```

The compiler may load `m->data` into a register **before** evaluating the RHS.
If the RHS allocates and triggers a GC that compacts `m->data` from nursery to
tenured, the register holds a **stale** address and the store lands in freed
memory — silent corruption, or a crash much later.

**Rule:** evaluate *all* field values into temporaries first (GC may fire
freely there), then read `m->data` once and write every temporary. No
GC-triggering operation may occur between reading `m->data` and the stores.

A matching runtime fix belongs with it: `set_fields()` must store `nullptr`,
not `item.container`, when a container/TYPE/FUNC/PATH field receives a null.

### DD4.4 — Typed-map construction is emitted inline, allocation included

The mature form of construction emits, with no runtime call at all:

1. `heap_calloc(sizeof(Map) + byte_size, LMD_TYPE_MAP)` — one **combined**
   allocation, size a compile-time constant (DD10.4)
2. inline `Map` header stores: `type_id`, `type` pointer (a constant), `data`
   pointer (`m + sizeof(Map)`), `data_cap`
3. direct field stores at constant offsets — `DMOV` for float, `MOV` for
   int/bool, tag-stripped pointer for containers, **skipped** for null (the
   allocation is already zeroed)

`map_with_data()` / `object_with_data()` remain the runtime entry points for the
non-inlined cases.

### Not implemented

Chained member access as a *single* fused direct access (`a.b.c` resolving
without materializing the intermediate) — DD1 handles the chain, but each link
still round-trips through a pointer.

---

## 5. Acquiring type knowledge

Every optimization in §6–§7 needs a static type. Lambda gets one from three
places, in order of confidence.

### DD7 — Declared types are trusted; inferred types are evidence, and evidence has rules

#### DD7.1 — Body-usage inference (MIR Direct)

For each untyped parameter, the body is scanned for evidence:

| Flag | Set by |
|---|---|
| `INFER_INT` | used as an array index, or in a binary op with an int literal |
| `INFER_FLOAT` | in a binary op with a float literal |
| `INFER_ARITH_USE` | in an **arithmetic** operator |
| `INFER_NUMERIC_USE` | in arithmetic **or comparison** — weak |
| `INFER_FLOAT_CONTEXT` | the body contains a float literal anywhere |
| `INFER_STOP` | string concatenation, member access, or passed to a call — polymorphic, keep `ANY` |

Decision order: `STOP → ANY`; `INT` alone → `INT`; `FLOAT` (even mixed with
`INT`) → `FLOAT`; weak numeric use → `INT` **only** for a `pn` with
`INFER_ARITH_USE` and no `INFER_FLOAT_CONTEXT`; otherwise `ANY`.

Two guards, each paid for with a bug:

- **`is_proc` guard.** `pn` functions have predictable numeric usage; `fn`
  functions are often deliberately polymorphic and must not have a type forced
  on them.
- **`INFER_ARITH_USE` guard.** *Comparison is polymorphic.* `(tree.root).key ==
  key` proves nothing about `key`. Treating comparison as INT evidence made the
  native body read a `double`'s bits as an integer — the splay benchmark
  returned 0s and garbage. Only arithmetic counts.

Aliases are followed first: `let x = param` / `var x = param` are collected
transitively so evidence gathered on the alias still counts.

#### DD7.2 — Call-site inference (CSI)

Where body usage cannot decide, the *callers* often can. A pre-transpilation
pass collects every call site of each `pn`, and if all sites agree on a
concrete scalar type for a parameter position, mutates `TypeParam.type_id`
**in place**. Because parameters share their `TypeParam*` with every identifier
node referencing them, in-place mutation propagates to all uses for free.

Safety constraints, each one a fixed bug:

- **All call sites must be visible** in the same script; a function used as a
  first-class value is skipped entirely.
- **All parameters or none.** Partial inference flips `has_typed_params()` true,
  which changes the signature and return handling for a function whose
  remaining parameters are still `ANY`.
- **A parameter that ever sees a non-inferable argument is poisoned
  permanently** — a later concrete call site must not overwrite that.
- **The function's own body is skipped** when collecting sites: a recursive
  self-call passes arguments typed under the *old* (untyped) assumptions.

After mutation, `reinfer_body_types()` re-derives result types bottom-up
through the body. It must descend into **map literals, key expressions, and
named arguments** — expressions nested inside a map literal were missed at
first and kept stale `ANY` types, producing an unbox applied to an
already-native value.

It must also **not** re-type local variables. A parameter's `TypeParam*` is
shared with its uses; a local's `named->type` is not, so reassigning it
desynchronizes the declaration from every use — declared `int64_t`, used as
`Item`.

#### DD7.3 — Inference results are computed once and cached

Parameter inference walks the function body. Doing it per-parameter costs
O(params × body); doing it once for all parameters of a function costs
O(body). Doing it again in a later pass costs another O(body).

Both are fixed: `infer_param_types_batched()` gathers aliases and evidence for
every untyped parameter in a single body walk, and an `infer_cache` keyed by
`AstFuncNode*` carries the result from the forward-declare pass into the
definition pass, so **every function's parameters are inferred exactly once**.

#### DD7.4 — Some types come from constructors, not annotations

`fill(n, v)` narrows the receiving variable's type from the fill value: an int
fill yields `ARRAY_INT`, a bool fill yields `ARRAY` with `elem_type = BOOL`.
This is what starts the type-propagation chain in an untyped hot loop —
`fill(n, 0)` → variable is `ARRAY_INT` → `tape[dp]` reads native int →
`tape[dp] + 1` is a native add → `tape[dp] = v` is an inline store.

#### DD7.5 — A compile-time element type is a hint, not a fact

`fn_array_set` can convert an `ArrayInt` into a generic `Array` **in place**,
changing `Container.type_id`. So a compile-time `ARRAY(nested=INT)` does not
license an unguarded inline read.

Two sound ways to proceed, both used:

- **Runtime type check.** Load the `type_id` byte, branch to the inline read on
  match, fall back to `item_at()` otherwise.
- **Mutation analysis.** If no `arr[i] = v` targeting the variable appears
  anywhere in the enclosing function (recursing into if/while/for/match bodies
  and nested functions, for module-level variables), the element type is fixed
  and the check can be dropped. The analysis is deliberately conservative: any
  index assignment anywhere disqualifies the variable, even a conditional one
  that could not change the type.

`get_effective_type()` must **not** report `INT` for a bare `ARRAY + INT index`
on the strength of the AST's nested type alone — that shortcut caused real test
failures, and the safety comment recording why belongs at that site.

---

## 6. Spending type knowledge: the lowerings

### DD8 — Dual function versions: native body, boxed entry

A function whose parameters (declared or inferred) have native types is emitted
with **native-typed MIR parameters** — no unboxing at entry. Alongside it, a
thin **boxed wrapper** with the all-`Item` ABI unboxes each parameter, calls the
native version, and boxes the result. Dynamic dispatch (`fn_call*`), closures,
first-class references, and cross-module imports all bind to the wrapper; known
same-module calls bind to the native version.

The eliminated waste, per call of `fn add(x: int, y: int) -> int`:

```
caller: box x, box y      →  callee: unbox x, unbox y
callee: box result        →  caller: unbox result
```

Four conversions per call, on every call. Closures are excluded by
construction: their captured environment is `Item`-shaped.

**Return type inference is what makes this pay** (DD8.1). Without it, even a
"native" function boxes its return, and the caller unboxes — half the round
trip survives. The return type is taken from the declared annotation, else from
the body expression's type, restricted to `INT`/`FLOAT`/`BOOL`. `can_raise`
functions are excluded: the error branch does not produce a native value.

Return annotations on `pn` are the same mechanism reached from the source side:
`pn f(x: int) int { … }` enables the native version even when no parameter is
typed. Measured: `hashmap` gained **25 %** from return annotations alone
(~450 K calls in the inner loop); `richards`, `deltablue`, `raytrace3d` gained
1–3 %. The benefit concentrates in high-frequency calls returning scalars whose
results land in typed variables.

> **Status note.** DD8 is the ancestor of the current dual-function design,
> which has since been elaborated into DF1–DF17 in
> `Lambda_Design_Compiling_Dual_Func.md` and ratified as D8.3. Where the two
> differ, **DF/D8.3 wins**. In today's code the pair appears as
> `needs_boxed_entry` / `needs_boxed_slow_body` on the per-function native
> info, not as the `_n`/`_b` C symbols this record's early sections describe.

### DD9 — Structured returns: `Ret*` is the error ABI, not a general return convention

Returning `Item` makes "value", "null", and "error" indistinguishable without a
tag check, gives a native-returning function no way to signal failure, and
carries no error detail (`ITEM_ERROR` is a bare tag).

The family is **per return type**, 2-field, `{value, err}`:

```c
typedef struct RetInt56  { int64_t  value; LambdaError* err; } RetInt56;
typedef struct RetFloat  { double   value; LambdaError* err; } RetFloat;
typedef struct RetString { String*  value; LambdaError* err; } RetString;
typedef struct RetItem   { Item     value; LambdaError* err; } RetItem;
// … Bool, Int64, Symbol, Map, List, Elmt, Obj, Array, Range, Path
```

Three decisions inside this one:

- **Two fields, not three.** `{value, ok, err}` is 24 bytes and returns through
  a hidden pointer on x86-64. `{value, err}` is **16 bytes** and returns in
  `rax`+`rdx` (x86-64) or `x0`+`x1` (ARM64) — zero memory indirection on the
  success path. `err == NULL` already encodes success, so `ok` was redundant
  *and* could desync.
- **Per type, not one `LResult{Item, err}`.** A single boxed result would force
  a `can_raise` function to box its native value on the **success** path —
  exactly the cost the native version exists to avoid. `fn read_line() string^`
  returns `RetString{String*, err}`; the pointer is never boxed.
- **Only when `can_raise`.** A non-raising function returns its raw native type.
  `Ret*` is not a universal wrapper.

**The `.err` sentinel (DD9.1).** Lambda's `fn_error()` returns a tagged zero;
error *detail* lives in the runtime context via `set_runtime_error()`, not in
the Item. So `item_to_ri()` cannot always recover a real `LambdaError*`. It
stores the original Item in `.value` **always**, and sets `.err` to the real
error pointer when one is recoverable and to the non-null sentinel
`(LambdaError*)1` otherwise. Consequences:

- `^` propagation forwards the whole `RetItem`, it does not extract `.err`
- `let a^err` binds `_ri.value` (the error Item), not `err2it(_ri.err)`
- **`.err` may be a sentinel — never dereference it blindly, never hand it to
  `err2it()`**

**Current reach (DD9.2).** `Ret*` is live as the **runtime C ABI for
`can_raise` system functions and procedures** (`pn_send`, `pn_receive`,
`pn_wait1`, io, …) declared in `lambda.h`. It is not the general MIR Direct
user-function return convention; MIR Direct's own error returns use the
`ITEM_ERROR` sentinel path, and the current return convention is
`Lambda_Design_Compiling_Return_Value.md` (v3, companion-lane pairs). The two
coexist: `Ret*` at the C runtime boundary, lanes inside generated code.

**C/C++ dual-compilation constraint (DD9.3).** `lambda.h` compiles as C (for
the JIT runtime) **and** as C++. In C++ mode `Item` is only forward-declared in
`lambda.h`; the full struct is in `lambda.hpp`. Therefore **any inline function
in `lambda.h` taking or returning `Item` by value must sit inside
`#ifndef __cplusplus`**, with a C++ counterpart in `lambda.hpp` after the
struct. `Ret*` types with no `Item` field (`RetBool`, `RetMap`, …) and their
constructors are unconditional; `RetItem`, `ri_ok`/`ri_err`,
`item_to_ri`/`ri_to_item`, `p2it`, `err2it`/`it2err`, and the container unbox
helpers are split.

### DD9.4 — Unboxing is type-checked, never a blind cast

Container unboxing goes through `it2map`, `it2list`, `it2elmt`, `it2obj`,
`it2arr`, `it2range`, `it2path`, each of which checks the tag byte and the
`Container.type_id` and returns `NULL` on mismatch. Blind `(void*)` casts —
what the C path's early wrapper did — crash on null or type mismatch. `p2it()`
is the safe inverse: `NULL → ITEM_NULL`.

### DD10 — Hot-path lowerings

Each entry replaces a runtime call with instructions. All are gated on type
knowledge from §5.

#### DD10.1 — Array access

Tiered, most specific first:

| Object | Index | Lowering |
|---|---|---|
| `ARRAY_INT` | `INT` | inline `items[idx]`, native result |
| `ARRAY_INT` | `ANY` | `it2i()` the index, then inline read |
| `ARRAY` (nested int/bool) | `INT` or `ANY` | runtime `type_id` check → inline read; else `item_at()` |
| `ARRAY` (other) | any | `item_at()` — skips `fn_index`'s index-type dispatch |
| `ANY` | inner `INDEX_EXPR` into a typed array | unbox the inner result, `item_at()` |

Index assignment mirrors the same tiers. `fn_index()` — double dispatch on both
index type and object type — is the most expensive path and is eliminated
wherever any of the above applies.

**A type expression as index is not an index.** `arr[int]` is a child query;
`AST_NODE_TYPE` / `ARRAY_TYPE` / `UNION_TYPE` indices must bypass every fast
path.

#### DD10.2 — Boolean element specialization

An array whose elements are known `bool` yields a **native 0/1** from the
inline read (`raw & 1`), and the enclosing `and`/`or`/`not` become native MIR
instructions instead of `fn_and`/`fn_not`/`is_truthy` calls. Without this, each
boolean array element costs three runtime calls.

*Pitfall:* the AST wraps identifiers in `AST_NODE_PRIMARY`. Every variable
lookup inside these dispatch paths must unwrap it first. *Second pitfall:*
`var a: int[] = fill(3, true)` coerces to `ArrayInt`; the bool element type must
not be applied when the coercion has changed the variable's type.

#### DD10.3 — Bitwise, comparison, and string equality

- **Bitwise** — `band/bor/bxor` are `MIR_AND/OR/XOR`; `bnot` is `XOR -1` (MIR
  has no NOT); `shl/shr` are `MIR_LSH/RSH` **guarded** by a `[0, 64)` bounds
  check branching to 0.
- **Mixed INT/INT64 comparison** — native `EQ/NE/LT/LE/GT/GE` for
  `int`-vs-`int64` operands (common: `ord()` returns `INT64`, compared against
  int literals). **Comparison only** — `INT`/`INT64` *arithmetic* stays boxed
  because the two carry inconsistent representations through
  `transpile_expr()`.
- **String/symbol equality** — two-tier: inline pointer identity first (name-pool
  interning makes literals and parsed identifiers pointer-equal), then
  `fn_str_eq_ptr` / `fn_sym_eq_ptr`, which take raw pointers, skip boxing and
  type dispatch, and go straight to length + `memcmp` (plus namespace compare
  for symbols).
- **Boxed comparison returns need masking.** `fn_eq`/`fn_lt`/… return `Bool`
  (uint8_t) through an `MIR_T_I64` declaration; the upper bytes are garbage on
  some ABIs. Mask with `0xFF`.

#### DD10.4 — Allocation

- **Combined map+data allocation.** A map used to cost two GC allocations —
  the `Map` struct and the data buffer — 3.5× overhead-to-payload for a
  two-pointer node. `map_with_data()` / `object_with_data()` allocate
  `sizeof(Map) + byte_size` once and point `data` immediately after the struct.
  This is GC-safe: the combined block lives in the non-moving object zone, so
  `data` never needs compaction; `gc_data_zone_owns()` correctly reports false
  for it; and the sweep frees the whole block as one unit.
- **Inlined allocation** — see DD4.4.
- **Inlined bump path.** The bump-pointer fast path is emitted as ~8 MIR
  instructions: load `bump_cursor`, add the (constant) slot size, compare
  against `bump_end`, store the new cursor, initialize the GC header inline,
  link into `all_objects`, set `Container.is_heap`; branch to
  `heap_calloc_class()` only on exhaustion. The `gc_heap_t` pointer is loaded
  once in the function prologue and cached in a dedicated register,
  saved/restored across nested function transpilation.

#### DD10.5 — Content blocks in `pn` bodies do not build a list

When a `pn` body is a content block (declarations + statements + a trailing
expression), evaluate the declarations and statements for effect and box only
the final expression. No `list()` / `list_end()` round trip.

### DD11 — Tail calls become jumps

TCO eligibility (shared with the safety analyzer): the function is **named**,
**not a closure**, and its body contains a tail-recursive call.

Emission: a `tco_count` register initialized to 0, a label at the top of the
body, an increment and a `BLE` guard against `LAMBDA_TCO_MAX_ITERATIONS` (1 M)
branching to `lambda_stack_overflow_error`, and — at each tail call — parameter
reassignment plus `MIR_JMP` back to the label.

**Tail position propagates, it is not inherited blindly.** Condition
expressions, call arguments, and binary/unary operands are **not** tail
position (`1 + f(n)` is not a tail call). `if`/`match` branch bodies restore
the parent's tail flag before each branch. Content-block items are not tail
position; `return` statements manage their own.

**Two-phase parameter swap is mandatory (DD11.1).** `factorial(n-1, n*acc)`
computes new values from current ones. Sequential assignment (`n = n-1; acc =
n*acc`) corrupts `n` before `n*acc` is evaluated. Evaluate **all** arguments
into temporaries, then assign **all** parameters. Same for `f(b, a)`.

The guard costs nothing on non-recursive functions — the eligibility test never
fires for them.

---

## 7. Data-driven code generation

### DD12 — One registry per concept; adding a system function edits one table

Before: adding a system function meant editing `sys_funcs[]` in the AST
builder, a declaration in `lambda.h`, an entry in `mir.c`'s import table, and
often a `strcmp` chain in the transpiler — at least 3 files, often 5+ sites.
Adding a *type* meant 6+ tables. The audit that motivated this found real
inconsistencies between them.

The consolidation:

- **`sys_func_registry.h` / `.cpp`** — one `SysFuncDef` table (~120 entries)
  carrying identity, arity, type info, method eligibility, `can_raise`, the C
  function name, the C-level return/argument conventions, and the native math
  equivalent. The AST builder references it by `extern`; the transpiler reads
  `c_func_name` instead of recomputing `"fn_" + name`.
- **`CRetType` / `CArgConvention`** — the C-level ABI of each system function
  as *data* (`C_RET_ITEM`, `C_RET_INT64`, `C_RET_DOUBLE`, `C_RET_BOOL`,
  `C_RET_STRING`, `C_RET_SYMBOL`, `C_RET_DTIME`, `C_RET_TYPE_PTR`,
  `C_RET_CONTAINER`, `C_RET_RETITEM`; `C_ARG_ITEM` / `C_ARG_NATIVE`). Result
  boxing becomes a table lookup rather than a switch. `C_ARG_NATIVE` is what
  lets the six bitwise operators take raw `int64_t` and skip boxing.
- **`TypeBoxInfo` table** — `TypeId` → C type name, unbox function, box
  function, const-box function, zero value. Replaces per-type if/else chains
  in boxing, captured-variable and parameter unboxing, and zero-defaulting.
  Flat array with linear scan: `TypeId` values are not dense and the table has
  ~22 entries, so the scan is free relative to the emission it gates.
- **O(1) name lookups** — `sys_funcs` lookup by (name, arg_count) composite key
  and by name alone; `mir.c`'s ~417-entry import table and the dynamic import
  table both by hashmap. Linear scans over these ran per call expression and
  per unresolved symbol.

**Unifying `try_box_scalar()` fixed latent bugs, not just duplication.** The
nine per-type scalar cases in the boxing switch each applied a slightly
different subset of the optional-param / closure-param / captured-var checks;
`BOOL`, `DTIME`, `DECIMAL`, and `STRING` were missing the optional-param check
and would attempt native boxing on an already-`Item` value.

A `TypeNarrowEntry` table (per-operator, per-operand-type → result type +
native function) was designed but **deferred**: the hot narrowing cases (`%`,
`//`, `len`) are handled inline, and a table would add indirection without
adding coverage.

### DD12.1 — The JIT header diet has a hard floor

`lambda.h` is embedded byte-for-byte into every C2MIR module, so shrinking it
shrank JIT parse time. Moving out what the JIT never touched — `TypeKind`,
`Path`/`PathMeta` (to `lambda-path.h`), `Target`, `Name`, `get_type_name()` —
cut `lambda.h` 1,239 → 1,061 lines and the embedded payload 4,115 → 3,393 lines
(**17.5 %**).

The proposal's estimate of 56 % was wrong, and the reason is worth keeping:
**the JIT reads struct fields directly.** Generated code touches
`Function.closure_field_count/flags/ptr`, `Range.start/end`, `List.items/length`,
`Map.data`, `Element.items/length/data`, `String.chars/len`. Those definitions
cannot leave. Further reduction requires converting field access to accessor
functions — trading parse time for call overhead.

This decision is **now moot for compile time** (no C2MIR path re-parses the
header) but remains the correct account of the header's shape and of why the
container definitions live where they do.

---

## 8. Compile-time performance

Two distinct profiles, measured across 278 scripts:

| | Standalone (223) | With imports (55) |
|---|---:|---:|
| Average total | 5.20 ms | 155.85 ms |
| Dominant phase | MIR transpile 68.8 % | AST build 79.7 % |
| Worst case | 134 ms | 831 ms (28 modules) |

The import profile is an artifact of the architecture: `build_module_import()`
recursively compiled each import **during AST construction**, depth-first and
serially, so every dependency's full parse→AST→transpile→JIT cost was billed to
the importer's "AST build".

### DD13 — Imports are discovered first, then compiled by dependency level

**Phase 1 (serial, cheap):** shallow-parse each source, extract `import`
statements, resolve paths, recurse for transitive imports, deduplicate, and
build a dependency graph with topological depths. Tree-sitter parsing is
~0.3 ms per module, so discovering 28 modules costs ~8 ms.

**Phase 2 (parallel per depth level):** compile level by level from the leaves.
One module at a level → compile inline; two or more → worker threads. Below the
2-module threshold the serial path is used unchanged.

What this required, and each item is a real thread-safety hazard:

| State | Fix |
|---|---|
| `Runtime->scripts` | mutex around append and lookup |
| `TSParser` | **not thread-safe** — one per worker via `__thread tls_parser` |
| `dynamic_import_map` (MIR link) | `__thread`, per-worker |
| `func_map`, `sys_func_map` | init-once, hoisted before any threading |
| `MIR_context_t`, arenas, `MirTranspiler` | already per-compilation |
| `Script->is_loading` circular-import guard | unnecessary — Phase 1 resolves the whole graph up front |
| worker stack | 8 MB, matching the main thread (the transpiler recurses deeply) |

**The JS variant differs in one essential way (DD13.1).** ES modules must be
**executed** after compilation to produce their namespace objects, and
execution touches the shared heap, GC, and module registry. So JS splits the
level into **parallel compile** (own `JsTranspiler`, `TSParser`, `Pool`,
`NamePool`, `MIR_context_t` per thread) followed by **serial execute** on the
main thread. After precompilation, the normal loader finds every module already
registered and skips compilation entirely.

### DD13.2 — Single-module transpile cost is inference cost

The measured wins on standalone scripts came from DD7.3 (batch + cache
inference) and DD12 (hashmap dynamic imports): total 5.20 → 4.22 ms average
(**19 %**), with the transpile phase down 7.1 %. The `r7rs` suite improved
**21.5 %** — Scheme-style code has many untyped parameters, precisely what
batched inference targets.

Remaining, not done: a flat function index to avoid re-walking the AST in the
define pass (~2–3 %); MIR optimization-level tuning, which is a configuration
choice — level 0 would suit REPL and test-suite latency, level 2 suits
benchmarks.

---

## Appendix A — Implementation history

Concise chronology. Numbers are release builds on Apple Silicon unless noted;
internal `__TIMING__` (execution only, excluding JIT) where a benchmark time is
given.

### A.1 MIR Direct: from stub to parity (Feb 2026)

| Round | Outcome |
|---|---|
| 1–3 | ~330-line skeleton → ~5,500 lines. All expression forms, control flow, functions, closures, TCO, collections, pipes, spread, error handling, string patterns, constrained types, decompose, paths, procedural mode. Pass rate 0 → 76/84. |
| 4 | Import system: dynamic import table, `_init_mod_consts`/`_init_mod_types`/`_mod_main`, BSS variable loading with native types, cross-module calls, named-argument reordering, variadics. 76/84. |
| 5 | The boxing contract (DD6) made explicit and applied uniformly to all four call paths (local, system, import, dynamic). Unbox after import calls and after system functions whose C return is `ANY` but whose AST type is `FLOAT`/`INT`/`BOOL`. Literal-flag guard so an array element type propagating to a loop variable stops loading from the const pool. **82/84.** |
| 6 | **85/85.** Both remaining failures were *runtime* bugs, not transpiler bugs: (a) `list_push`'s string-merge set `prev_str->ref_cnt = 0`, freeing shared const-pool strings still referenced elsewhere — must decrement; (b) `str_to_double()` rejected values on `errno == ERANGE`, which `strtod` sets for valid **subnormals**, so `1e-308` was stored as `0.0`. The C path hid (b) by writing float literals straight into generated C. |

Two rejected detours from Round 5 are worth remembering: boxing all system
function returns inside `transpile_call` double-boxed wherever
`transpile_box_item` also boxed (14 regressions, 76→68); and a blanket skip of
re-boxing for all call results also suppressed boxing of genuine native returns.

### A.2 Correctness and performance phases (MIR2)

**Phase 1 — the four failing benchmarks.**
`pnpoly` crashed because `get_effective_type()` returned `ANY` for comparisons
between `ANY` operands, so a raw `0`/`1` was treated as a pointer — the `BOOL`
return had to move **before** the `ANY` early exit. `ray` failed MIR
verification on dead code after a `return` → `block_returned` (DD6.3).
`collatz`/`diviter` never crashed at all: untyped parameters routed every
arithmetic operation through boxed runtime calls, ~100× slower per operation —
deferred to Phase 2.

**Phase 2 — type narrowing.** DD7.1 inference plus the DD6.2 audit (25+ sites).
`brainfuck` went from timeout (>120 s) to **3.27 s** once `fill()` narrowing
(DD7.4), inline array read/write (DD10.1), and native mixed INT/INT64
comparison (DD10.3) chained together through its hot loop. MIR baseline tests
27/155 → 155/155.

Measured against C2MIR after Phase 2: `diviter` 20.4×, `array1` 19.8×,
`sum2` 11×, `collatz` 7.0×, `nqueens2` 6.3×, `sumfp2` 5.6×; `deriv`, `ray`,
`matmul`, `json_gen` at parity; `brainfuck` 0.79×, `pnpoly` 0.79×,
`base64` 0.82×. `ack2` regressed to 0.05× — extremely deep non-tail recursion,
where MIR's per-call overhead dominates. Map/struct-heavy AWFY benchmarks
(`towers2` 0.17×, `permute2` 0.20×, `havlak2`, `deltablue2`) all regressed,
which is what motivated Phase 3.

> **Debug builds lie about this.** Debug showed 50–200× speedups because the
> unoptimized boxed runtime functions cost ~50 ns/call; in release the C
> compiler brings them to ~2–5 ns and the true gap appears. Never quote a debug
> ratio.

**Phase 3 — direct field access (DD1–DD4).** `permute2` 5.0×, `towers2` 4.6×,
`deltablue2` 1.4×; `storage2` 0.84× and `cd2` 0.86× (both GC-dominated, and
two-phase construction adds a little per allocation). `gcbench2` 22 % faster
than untyped. The GC stale-`m->data` bug (DD4.3) was found here.

**Phase 4 — TCO (DD11).** `sum_to(500000, 0)` completes instead of overflowing;
no measurable cost on non-TCO benchmarks.

### A.3 The C-path optimization series (historical)

These landed in the now-removed C transpiler. Kept because the *analyses* were
correct and several conclusions carried over.

**`_store_i64` (P1).** Every scalar assignment inside a `while` loop was
emitted as a call to a trivial `*dst = val` function, purely to defeat a MIR
SSA lost-copy bug. `diviter` paid ~1 billion such calls. Two direct fixes
failed (Appendix B, R2, R3). The one that worked was **cross-dependency
analysis**: the lost-copy bug only bites variables with *circular*
cross-dependencies across iterations (the Fibonacci swap `temp=a+b; a=b;
b=temp`). Self-updating variables (`q = q+1`, `r = r-y`) have simple phi chains
that MIR handles correctly. The analysis walks the loop body, collects
(target, RHS) pairs, and marks a variable unsafe if any *other* assignment's
RHS reads it — conservatively, so a one-way dependency is also marked unsafe.
Typed `diviter` 271 ms, typed `collatz` 338 ms.

The follow-on finding mattered more than the fix: **untyped code's bottleneck
was never `_store_i64`** — it was boxed arithmetic from untyped parameters
(~5,500 ms vs ~271 ms typed). That is what motivated CSI (DD7.2).

**CSI (call-site inference).** Untyped `diviter` 5,480 → ~285 ms (**19.2×**),
untyped `collatz` 2,016 → ~439 ms (**4.6×**) — both then *faster* than Node.js,
with no source changes. Five of nine untyped AWFY benchmarks matched their
hand-typed variants exactly. The bugs fixed along the way are recorded as the
DD7.2 constraints.

**P5 — combined map+data allocation.** `gcbench` 4.7×, `gcbench2` 6.2×.

**B2 — inline bitwise.** ~8 % on untyped `collatz` (C path); ~2 % on MIR
Direct, where the value was architectural consistency rather than speed.

**D1 / D3-MIR — array access.** D1 eliminated every `fn_index` from `triangl`'s
C-path hot loop (1,416 → ~1,265 ms). D3-MIR added six inline dispatch paths with
runtime type checks to MIR Direct: `triangl` ~699 → **~441 ms**, at that point
**2.56× faster than C2MIR**. The `proc_array_type_convert` failure during D3-MIR
is the origin of DD7.5.

Geomean vs Node.js across the five focus benchmarks: **~17× → ~3.1×**.

### A.4 MIR Direct calling convention (P4 series, Mar 2026)

| Step | Result on `triangl` |
|---|---|
| Phase 3 baseline | ~441 ms |
| P4-1 dual versions (DD8) | |
| P4-3.3 native returns (DD8.1) | |
| P4-3.1 bool specialization (DD10.2) | ~290–380 ms |
| P4-3.2 inline `item_at` + mutation analysis (DD7.5) | **~222–233 ms** |

Cumulative ~47 % over the Phase 3 baseline. Against the C path at the same
commit: `array1` **10.4×**, `divrec` **8.7×**, `triangl` **6.8×**,
`puzzle` **4.1×**, `quicksort` 1.7×, `primes` 1.4×; `pnpoly` 0.89× and
`gcbench` 0.98× the only places C2MIR held on — float-heavy and GC-dominated
respectively.

P4-2 (inlining short user functions) was specified in detail — eligibility,
scope-based parameter binding, a recursion guard via an inline stack, one level
of transitive inlining — and **never implemented**. It remains the largest
designed-but-unbuilt item in this record.

### A.5 The splay campaign (Mar 2026)

A single benchmark, driven from 1,648 ms to **57 ms** release (vs Node.js
28.8 ms — 156× → **2.0×**). The sequence is a good map of where time actually
goes:

| Step | Release |
|---|---|
| baseline (untyped) | — |
| typed `SplayNode` / `SplayTree` / `RngState` | ~480–530 ms |
| correct `value` field (regression: exposed real allocation cost) | ~4,500 ms |
| direct field stores, eliminating `map_fill` | ~4,000 ms |
| typed payload maps (covers all ~768 K allocations, not just ~12 K nodes) | ~2,280 ms |
| inline `map_with_data` + skip redundant `memset` | ~2,020 ms |
| **`gc_object_zone_owns` O(slabs) → O(log N)** | **~83 ms** |
| bump-pointer nursery + JIT-inlined bump path | **~57 ms** |

**The 20× step was a GC bug, not a transpiler one.** `gc_object_zone_owns()`
iterated *every slab in every size class* on each pointer check during mark and
sweep. With ~780 K allocations the 96-byte class alone held 6,000+ slabs;
`sample` attributed **99.5 %** of runtime to that one function. The fix is a
sorted `(base, end)` range array with an O(1) min/max rejection and O(log S)
binary search.

Three transpiler-adjacent bugs surfaced here and are recorded as rulings above:
the `LMD_TYPE_ANY` 16-byte `TypedItem` vs 8-byte typed-field size mismatch that
misaligns every subsequent offset (hence DD4's exact-layout requirement and the
need to annotate the intermediate variable in a constructor); recursive type
definitions resolving to `ANY` because the name was pushed *after* the body was
built (fixed by pre-registering a placeholder before building); and the
comparison-is-not-arithmetic inference bug that became the `INFER_ARITH_USE`
guard in DD7.1.

Two allocator experiments recorded as null results: specializing the allocator
for a compile-time-known size class had **no measurable release effect**
(ThinLTO already inlines the class-index computation — it still helps debug
builds); eliminating the `all_objects` linked-list write per allocation and
skipping the `is_heap` flag store remain unstarted (~5–10 % and ~2 %).

Null-guard elimination has its infrastructure in place (a `skip_null_guard`
parameter threaded through direct field read/write) but is **disabled at every
call site**: typed variables in Lambda can legitimately hold null
(`var current: SplayNode = tree.root`), so a naive identifier check crashed.
Doing it soundly needs flow-sensitive non-null analysis, and profiling puts the
prize at only ~5 %.

### A.6 Transpile-time tuning (P1–P4, Part 3)

Batched inference, the inference cache, hashmap dynamic imports, and parallel
module precompilation for both Lambda and JS. Standalone average
5.20 → 4.22 ms; Lambda baseline 676/677 (one pre-existing UTF-8 expected-output
failure), Radiant 3671/3671; JS 679/679 with both ES-module suites passing.

### A.7 Retirement of the C path

Once MIR Direct led on compile time (2.6×) and on generated-code speed
everywhere except two GC/float-bound benchmarks, the C transpiler was frozen
(CLAUDE.md rule 14) and then deleted in commit `a6d1ca0e8`. `transpile.cpp`,
`transpile-call.cpp`, the `--c2mir` CLI flag, `_u`/`_w` variants,
`emit_struct_typedefs`, `_store_i64`, and `FN_FLAG_BOXED_RET` are gone.
`transpile_shared.cpp` and `transpiler.hpp` keep the helpers both back ends
used (`has_fixed_shape`, `resolve_field_type_id`, `find_shape_field_by_name`,
the box/unbox registry).

---

## Appendix B — Superseded and rejected rulings

Recorded so the same ground is not re-walked.

### R1 — ~~String/Symbol/Binary should be raw header pointers like containers~~ — REJECTED (measured)

The proposal: drop the high-byte type tag for `String`, `Symbol`, and `Binary`
and give each struct a `type_id` at offset 0, matching containers. Expected:
fewer boxing instructions (an OR disappears), simpler unboxing (no 56-bit
`ubfx`), uniform type dispatch, simpler GC traversal.

Implemented in full on branch `direct-string-pointer`, all 677 baseline tests
passing. **Result: 11.1 % geomean slowdown. 55 of 62 benchmarks slower, 2
faster.** Every string-heavy benchmark regressed in the opposite direction from
the hypothesis (`base64` +20.1 %, `knucleotide` +17.4 %, `levenshtein` +16.5 %,
`revcomp` +14.8 %, `json_gen` +8.6 %), and *numeric* benchmarks regressed
10–15 %.

**Root cause: type checks are far more frequent than boxing operations.** With
the tag in the Item, `_type_id` is a register read — free. Behind a pointer it
is a branch plus a memory load. The one saved OR per boxing never came close to
paying for it.

**Do not merge; do not retry.** The tagged-pointer scheme is the right design.
See `Lambda_Design_Item_Boxing.md` for the current representation. The seven
bugs fixed on that branch are all artifacts of the removed tag and have no
counterpart on master.

### R2 — ~~`volatile` replaces `_store_i64`~~ — REJECTED (C2MIR ignores it)

C2MIR *parses* `volatile` and stores the `volatile_p` flag, but never consults
it during MIR generation. Identical IR, identical lost-copy bug (Fibonacci
returned 256 instead of 55).

### R3 — ~~Pointer-through-store replaces `_store_i64`~~ — REJECTED

`*((int64_t*)&_var) = val` — C2MIR sees through the cast, recognizes a local
store, and the SSA pass optimizes it the same wrong way.

### R4 — ~~Merge forward-declare and define passes~~ — REJECTED (unsafe)

Saves one AST traversal (~5–8 % of transpile), but breaks mutual recursion: if
`A` precedes `B` in source and calls it, `B` must be forward-declared before
`A`'s body is emitted. A merged pass cannot guarantee that for siblings. The
motivating cost — redundant inference — was removed by the inference cache
(DD7.3) instead.

### R5 — ~~Vectorized arithmetic for small fixed-size vectors~~ — REJECTED (measured)

Rewriting `raytrace3d`'s 3-element vector math to use element-wise array
operators made it **12× slower than manual indexing** (1,661 ms vs 132 ms
typed). For a 3-element operation the dispatch path costs a type check, a
function call, a length extraction switch per operand, a result-type decision,
a **GC allocation** for the result array, a per-element `vector_get` switch, and
a final boxing — all to perform three additions the JIT would otherwise inline.

**Vectorized arithmetic is for large arrays in data-processing workloads, not
tight numeric loops.** The other candidates fail for a second reason:
`navier_stokes`'s `x[i] = x[i] + dt*s[i]` over 16,900 elements would become
`x = x + dt*s`, which builds a **new array** and loses the in-place mutation
the caller depends on.

### R6 — ~~The `_n` / `_b` / `_u` / `_w` C function-variant scheme~~ — SUPERSEDED

The C path's three variants (main, `_u` unboxed, `_w` wrapper) were to be
consolidated into two (`_n` native, `_b` boxed). Both schemes died with the C
back end. The *ruling* survives as DD8 and, in current form, as DF1–DF17 /
D8.3: one natively-typed body plus one boxed entry, with dynamic dispatch
bound to the boxed one.

### R7 — ~~`func_list[]` auto-generated from the system-function registry~~ — SKIPPED

Deriving `mir.c`'s import table from `SysFuncDef.func_ptr` caused symbol
resolution problems in the shared-library build
(`liblambda-input-full-cpp.dylib`). The `func_ptr` field was removed from the
registry, which stays metadata-only; `func_list[]` is maintained by hand.

### R8 — ~~Restructuring caused the ~2–3 ms JIT overhead increase~~ — DISPROVEN

A uniform ~2–3 ms JIT increase appeared after the Phase 1–4 restructuring and
was attributed to it. Phase-level profiling disproved it: parse, AST build, and
C transpile together were **6.7 %** of JIT overhead in release builds, while
C2MIR (46.1 %) and MIR codegen (43.6 %) were 89.7 %. The transpiler changes
could not have produced the delta; it came from C2MIR/codegen variance and LTO
differences.

The same profiling produced a lasting warning: **debug-build profiling is
misleading.** Debug showed AST build at 31 % of JIT overhead; release showed
2.4 %. AST build and C transpile gain ~20× from optimization, while C2MIR and
MIR codegen are unaffected.

---

## Appendix C — Source documents consolidated

All ten now live in `vibe/impl/` with the `(retired)` suffix, keeping their
detail and evidence (Doc_Convention §5).

| Retired document (`vibe/impl/`) | Contributed |
|---|---|
| [`Lambda_Transpile_Mir (retired).md`](impl/Lambda_Transpile_Mir%20(retired).md) | MIR Direct architecture, the ABI contract (§2), the feature-parity progress log (A.1) |
| [`Lambda_Transpile_MIR2 (retired).md`](impl/Lambda_Transpile_MIR2%20(retired).md) | Correctness phases 1–4, direct field access in MIR, TCO (A.2, DD11) |
| [`Lambda_Transpile_Map (retired).md`](impl/Lambda_Transpile_Map%20(retired).md) | **DD1–DD4** and §4 in full |
| [`Lambda_Transpile_Restructure (retired).md`](impl/Lambda_Transpile_Restructure%20(retired).md) | `Ret*` structs (DD9), safe unboxing (DD9.4), dual versions (R6), data-driven metadata (DD12) |
| [`Lambda_Transpile_Restructure2 (retired).md`](impl/Lambda_Transpile_Restructure2%20(retired).md) | Header diet (DD12.1), O(1) lookups, the registry, phase profiling (R8) |
| [`Lambda_Transpile_Restructure3 (retired).md`](impl/Lambda_Transpile_Restructure3%20(retired).md) | `_store_i64` analysis (A.3, R2, R3), CSI (DD7.2), array dispatch (DD10.1), combined allocation (DD10.4) |
| [`Lambda_Transpile_Restructure4 (retired).md`](impl/Lambda_Transpile_Restructure4%20(retired).md) | Dual versions in MIR (DD8), return inference (DD8.1), bool specialization (DD10.2), mutation analysis (DD7.5) |
| [`Lambda_Transpile_Restructure5 (retired).md`](impl/Lambda_Transpile_Restructure5%20(retired).md) | Splay campaign (A.5), the GC ownership bug, bump nursery, `INFER_ARITH_USE` (DD7.1), vectorized arithmetic (R5), `pn` return annotations |
| [`Lambda_Transpile_Restructure6 (retired).md`](impl/Lambda_Transpile_Restructure6%20(retired).md) | The direct-string-pointer experiment (R1) |
| [`Lambda_Transpile_Tuning (retired).md`](impl/Lambda_Transpile_Tuning%20(retired).md) | Compile-time tuning and parallel imports (§8, DD13) |

---

## Appendix D — Open items

| # | Item | Note |
|---|---|---|
| DO-T1 | Inline short user functions (P4-2) | Fully designed, never implemented. The largest remaining call-overhead win. |
| DO-T2 | Chained member access as one fused direct access | DD1 handles the chain; each link still round-trips a pointer. |
| DO-T3 | Loop-invariant hoisting of array `data`/`length`/`type_id` | ~5–10 % on iteration loops. |
| DO-T4 | Flow-sensitive non-null analysis for null-guard elimination | Infrastructure present and disabled; ~5 %. |
| DO-T5 | Speculative native arithmetic for `ANY ⊕ ANY` | Tag check + native op + boxed fallback; must handle 56-bit overflow. |
| DO-T6 | Schema-directed `input()` parsing to lift DD2 | Would make direct field access sound for input data. |
| DO-T7 | Eliminate the per-allocation `all_objects` link | ~5–10 %; sweep would iterate slabs instead. |
| DO-T8 | MIR optimization-level tuning by workload | Configuration, not code: level 0 for REPL/tests, level 2 for benchmarks. |
| DO-T9 | Extend `ValueRep` to expression results | Would make DD6 checkable rather than reviewable. See `Lambda_Design_Compiling_Lane.md`. |
| DO-T10 | MIR SSA lost-copy bug | Never reported upstream. Moot for MIR Direct (no `_store_i64`), but the underlying defect is unfixed. |
