# Runtime Constant Handling — Folding, the Const Pool, and Shared Values

**Date:** 2026-09-11 (design), 2026-09-12 (implemented)
**Status:** IMPLEMENTED for Lambda — RC-P0 … RC-P8 landed, two deliberate
residues (§10). **Designed, not built, for JavaScript** (§9).

**Formal authority:** **D6.1.1–D6.1.2** (the purity bit; it *is* the
const-folder's soundness gate), **D8.2.2** (purity is a core flag),
**D8.2.5v2** (fact placement: core runtime facts on the AST, no ID-keyed
per-node fact table), **D2.4.1–D2.4.3** (one authority per compiler fact;
carrier/representation), **DI14** (no stale address may enter cacheable MIR),
**D5.3.3** (fold frames own their rooting), **D8.1.1v2** (T0 AST interpreter)
in [`doc/Lambda_Formal_Design.md`](../doc/Lambda_Formal_Design.md). Semantics:
**S12.1** (effect system). The formal specifications win on disagreement.

**Ledger prefix:** `RC`. Rulings carrying a `v2` suffix were revised during
implementation; their superseded text and the reasoning is in Appendix C.

---

## 1. Scope

Two kinds of value reach the const pool, by two different routes:

- a **const value** — anything the source already determines: a scalar literal,
  or a container literal whose parts are all const. It is *materialized*, never
  evaluated.
- a **const expression** — a computation over const inputs. It is *folded* by
  the AST interpreter at compile time.

Both end as pool entries, so no consumer asks which produced a value. The
folder's input is expressions only.

§2–§8 describe this as built for **Lambda**. §9 carries the same storage design
to **JavaScript** with a deliberately narrower scope: JS shares the pool, the
node handle and the emission split, but not the folder — its semantics, purity
model and inertness guarantee are its own.

---

## 2. Ledger

| ID | Ruling |
|---|---|
| **RC1** | **One const-folding pass serves both tiers.** The fold is a property of the compilation unit, not of a back end. Its result is consumed by the T0 AST interpreter and by MIR lowering alike. A fold only one tier can use is a defect, not a design. |
| **RC2** | **Eligibility is bounded by the AST interpreter's evaluation capability, gated by purity.** Any *expression* T0 can evaluate is a candidate; **D6.1.2**'s purity bit decides whether it *may* fold. Not a hand-maintained node-shape whitelist. Const values (RC15) are outside this entirely — materialized, not evaluated. |
| **RC3** | **Constness is compositional over the pure fragment** (§3). |
| **RC4** | **Folded results are values, not type IDs.** Admission asks a *representation* question — is this Item self-contained, or does it need pool residency? — never a type-id whitelist. |
| **RC5v2** | **One storage home: the const pool** (§4). Every const value is a `const_list` entry; the node holds only a handle. The representation split survives at **emission**, not in storage. |
| **RC6** | **The pool is content-shared.** Equal values occupy one slot, whatever their origin. |
| **RC7** | **A pooled fold result is rehomed before its frame closes** (§6). The fold's throwaway frame owns nothing that outlives it; a value destined for the pool is copied into script-pool storage during the attempt, or the fold is abandoned. |
| **RC8** | **MIR consults the handle on every lowering path**, not only at boxing boundaries (§5.2). |
| **RC9** | **The interpreter consumes the handle directly** (§5.1), without re-entering evaluation. |
| **RC10** | **Folded type and inferred type must agree; disagreement is a defect and is reported** (§7). The producer checks once under a greppable `const-fold: RC10` prefix; the consumer does not re-derive it. It **reports and declines, never aborts** — RC14 requires inertness, and `abort()` alters program completion maximally. |
| **RC11** | **Each node folds exactly once.** The index carries a watermark; the pass resumes there and needs no reset. Stated per *node*, not per *unit*: the REPL appends to a retained index, so a unit-level skip would strand appended nodes unfolded. |
| **RC12v2** | **No per-node fact side table.** `AstNode` carries `AstConstKind const_kind` + `uint32_t const_index` in its existing padding — `sizeof(AstNode)` stays 32. `AstNodeFacts` and `AstIndex::facts` do not exist. |
| **RC13** | **Nothing pointer-backed enters cacheable MIR as a raw address** (**DI14**). Pooled values are reached through the module-state const indirection, never a baked pointer. |
| **RC14** | **Fold attempts are semantically inert.** Fuel exhaustion, a native fault, an error result, or a rejected attempt leaves the node unfolded and changes nothing observable. A compiler optimization may never alter program completion. |
| **RC15** | **Const values are pooled when built and never enter the folder.** A scalar literal, and equally a static/const container, needs no evaluation: it is materialized directly into the pool. |
| **RC16** | **Rehoming applies only to computed results** (§6). A const value is born in pool-owned storage; only a computed result is born in frame/GC storage and must move before its frame closes. A folded aggregate therefore needs no rehoming — an aggregate literal is a const value under RC15 and never reaches the folder. |
| **RC17v2** | **A `Type` carries a value only when that value is part of the type's meaning.** Literal types used for *matching* — union members, type patterns, annotations — keep their payload: the value is the type's identity, and the validator reads it with no AST node in reach. A literal in **expression** position carries no value on its type; its value is a pool entry named by the node's handle. |
| **RC18** | **Three-way separation of an expression-position constant.** *Which kind* is the `Type`. *Which value* is the per-node handle. *The value itself* is one shared pool entry. The handle is on the node, not on a const-specific type, because a folded expression is an arbitrary node kind — a `BINARY`, a `CALL`, an `IF_EXPR` — whose type stays its own inferred type. Having a constant value is a per-node fact; it is not a type. |

### 2.1 What the existing pass contributed

The pre-existing fold mechanism is sound and survives unchanged: real
interpreter semantics rather than a parallel arithmetic evaluator; a throwaway
frame so every intermediate is rooted and its side-stack extent disappears
(**D5.3.3**); a per-node fuel budget; fault containment. RC14 is that property,
named.

---

## 3. Constness (RC3)

**D6.1.2** already rules that the purity bit is the const-folder's soundness
gate; widening eligibility is what brings it into force.

A node is **const** when:

1. it is a literal; or
2. it is a pure operator applied to const operands; or
3. it is an identifier bound by an **immutable** binding whose initializer is
   const; or
4. it is a call to a **pure sys-func** with all-const arguments. A user-defined
   `fn` call is semantically const under D6.1.2 but is **out of scope** (§10,
   Appendix C.5); or
5. it is a control form (`if`, `match`) whose scrutinee and taken arms are const.

A node is **not** const when it reaches a `pn`, a mutable binding, a `var`
parameter, module-level mutable state, I/O, or any surface whose purity bit is
unset. Guest builders mark all-effectful (**D6.1.1**), so a hosted-language node
is non-const by default and never folds by accident.

Two consequences:

- **Constness is a compile-time judgement; purity is a declared fact.** The
  folder reads the bit, never infers it. Absent bit ⇒ assume effectful.
- **A const binding read is not a fold of the binding.** Folding `x` where
  `let x = 1 + 2` replaces the *read* with `3`; the binding survives.

Walks through binding initializers are depth-capped, so a self- or mutually
referential declaration declines rather than hanging.

---

## 4. Storage (RC5v2, RC12v2)

One home: the const pool. The node holds a handle into it.

```c
struct AstNode {
    AstNodeType  node_type;
    AstConstKind const_kind;   // NONE | FOLDED | POOLED
    uint32_t     const_index;
    Type        *type;         // the inferred/effective type (D8.2.5v2)
    AstNode     *next;
    SourceSpan   source_span;
};                             // 32 bytes
```

Both fields occupy padding that already sat between the 16-bit tag and the first
pointer, so `sizeof(AstNode)` is unchanged, subclass layouts are unchanged, and
the tree has no `offsetof(AstNode, …)` pins to break. Zero-initialized
allocation makes `AST_CONST_NONE` the default with no init pass.

### 4.1 Two kinds, differing only in indirection

| Kind | The slot holds | Produced by |
|---|---|---|
| `AST_CONST_FOLDED` | a pointer to a pool-allocated `Item` | the fold pass, for a computed expression |
| `AST_CONST_POOLED` | **the container pointer itself** | the materialization pass, for a const ARRAY/MAP |

`POOLED` keeps the bare-pointer convention `emit_load_const(…, MIR_T_P)` already
expected, so the existing static-collection path needed no change. `const_kind`
is what lets one handle serve both without a kind array on the pool.

### 4.2 Pass order

`lambda_const_materialize_script` runs **before** the fold, so no container ever
passes through the interpreter (RC15). It builds each const ARRAY/MAP directly
from the AST into script-pool storage — `is_static`, `is_immortal` — appends it
to `const_list`, and marks the node `POOLED`.

---

## 5. Consumers

### 5.1 Interpreter (RC9)

A node carrying a handle returns its value directly through
`const_list[const_index]` — dereferenced for `FOLDED`, taken as the pointer for
`POOLED`. No re-entry into evaluation, no recomputation per run.

Gated on node kind first: only UNARY/BINARY/IF/IDENT/CALL/ARRAY/MAP can carry a
handle, so every other evaluation leaves the path on a switch. The gate is *not*
restricted to `EvalMode::RUNTIME`: folding `a + 1` recurses into the identifier,
whose value is a published handle rather than a slab slot — the module slab is
empty at compile time. Safe because a handle is published only after passing its
RC10 agreement check.

This is the largest behavioural change here. It makes const folding a property
of the compilation unit that both tiers observe — RC1.

### 5.2 MIR (RC8)

- **Self-contained value** — read the pool slot **at compile time** and bake the
  word. The pool *address* is never emitted. The producer decodes the Item there
  and emits the consumer's native lane: a folded int as a raw `mov` on the int
  lane, a float as `dmov`, a bool as 0/1 — not a tagged word the consumer must
  unbox again.
- **Pointer-backed value** — `emit_load_const(const_index)`: load module state,
  load the `consts` pointer, load the value. Its own comment explains why the
  pointer is never baked — "the module's const image may rebind after
  interning". That indirection is what satisfies **DI14**/RC13.

**Two consultation points.** `transpile_expr_value` is the D8.2.6 core boundary
and covers generic paths. `emit_binary_value`, `emit_unary_value` and
`transpile_if` are reached *directly* by specialized callers that bypass that
boundary — the native-int declaration path in `transpile_let_stam` among them —
so the check also sits at those producers. That covers every route into them
without chasing call sites, and suffices because UNARY/BINARY/IF are exactly the
foldable node kinds (the pass skips PRIMARY outright).

`transpile_if` checks before opening its `MirFlowScope`: a folded conditional has
a settled value, so neither arm is emitted and there is no control flow to scope.

---

## 6. Lifetime and rehoming (RC7, RC16)

**Scope: computed expression results only.** Literals and const containers never
take this path — they are born in pool-owned storage.

The fold evaluates on a frame whose side-stack extent is released immediately
after the attempt. A folded `String`, `Decimal`, `Binary` or container allocated
during evaluation dies with it. Keeping such a value means copying it into
script-pool storage and registering it in the pool **before the frame closes**.

- **RC7a** — the rehome happens inside the attempt, while the value is still
  rooted. A fold that cannot rehome is abandoned under RC14.
- **RC7b** — the pool takes ownership of a copy, never a reference into
  evaluation storage.
- **RC7c** — a value whose carrier the pool cannot own is not foldable.
  Identity-bearing values must not be shared by content.

**Identity is provenance, not shape** (RC-O2): a container **loaded from input**
carries identity and must never be content-shared; a container **constructed** by
the program has none. A folded aggregate is constructed by definition, so it is
always on the poolable side.

*Implementation status:* rehoming has no reachable case yet — see §10.

---

## 7. Sharing and agreement

**RC6 — sharing.** Two interners, both scoped to the **compilation unit**, not
the `Script`: `const_list` is Script-owned and the REPL rolls a failed statement
back by truncating it, so a Script-scoped map would hold reclaimed indices.

- **Folded values** intern by their raw 8-byte word. A slot is fully described
  by its bytes: each consumer reads it through its own node's type, and const
  values are immutable, so equal words are interchangeable. An int and a double
  sharing a bit pattern may share a slot — safe precisely because the node, not
  the pool, decides how to read it.
- **String/symbol literals** intern by content, sharing one `String` and one
  slot. A symbol is a `Symbol*` carried in a `String*` slot and the two layouts
  differ, so the key is read per kind.

**RC10 — agreement.** The folded value's type must equal the node's inferred
type. The producer checks once and logs a disagreement under `const-fold: RC10`;
the consumer trusts it. A mismatch is a defect in the folder or in inference and
must surface as one, never as a quietly missed optimization.

---

## 8. Relationship to other rulings

- **D8.2.5v2 / U30v2** — satisfied. The const handle is core runtime information
  living on the AST; no ID-keyed per-node fact table exists.
- **DI14** — honoured by construction: self-contained words are baked, everything
  else is reached through the module-state indirection.
- **D6.1.2** — brought into force on this path for the first time.
- **P7 U-D** — superseded. U-D preserved the existing fold and only relocated its
  result; the fold itself was the defect.

---

## 9. JavaScript

**Status:** designed, not built. Scope is deliberately narrower than Lambda's.

### 9.1 What JS has today

| | |
|---|---|
| Const-fold pass | **None.** No `const_fold` anywhere under `lambda/js/`. `1 + 2` emits a runtime add. |
| Const pool | **Unused.** `grep const_list lambda/js/` is empty, though `JsScript : Script` inherits the field. |
| Restricted eval mode | **None.** No `EvalMode`, `mode_fuel` or `mode_rejected` in `js_interp.cpp`. |
| Purity bit | **None.** No `is_proc` equivalent for JS callables. |
| Literal emission | **Present and reasonable.** `jm_transpile_literal_value` bakes immediates and already honours a representation demand (`VALUE_REP_F64` vs boxed). |
| Node handle | **Already there.** `typedef AstNode JsAstNode`, so every JS node carries `const_kind`/`const_index` — currently always `AST_CONST_NONE`. |

Array and object literals are built at run time, every evaluation. There is no
JS equivalent of RC15 materialization.

### 9.2 The NameId constraint

`jm_box_string_literal` embeds the bytes in the MIR artifact and calls
`js_make_string_len` **at evaluation time**, with a stated reason:

> String values are not property identities. Keep their bytes in the MIR
> artifact and allocate an ordinary GC String at evaluation time so source
> text, diagnostics, and literals never consume permanent NameIds.

That reason is sound and must survive. A `NameId` is a 16-bit **append-only**
ordinal per segment — bounded at 65,535, never reclaimed — and exists to make
property identity a 32-bit compare (`js_get_name_id`). A string *value* never
participates in property lookup, so an id spent on it buys nothing and is never
returned. JS is uniquely exposed: it is string-heavy, and `eval`/`new Function`
mean the literal set is not bounded by source on disk.

**The const pool is a third option, not a violation of that.** A `const_list`
entry is neither a NameId nor a per-evaluation allocation. Pooling a JS literal
spends script-pool storage that dies with the unit, and leaves the identity
space untouched. §9.2 is the constraint to respect, not an argument against
pooling.

### 9.3 JS has no const image — this is new plumbing, not a port

Lambda reaches a pool entry through `em_load_const`: a per-module **BSS** slot
holds `const_list->data`, and the load is `bss -> consts -> consts[index*8]`.

JS has **no BSS at all** (`grep MIR_new_bss lambda/js/` is empty) and never
touches `const_list`. Its only module-level indirection is a *runtime call* —
`lambda_active_module_var_at(slot)` — built for mutable module variables and
tied to the preamble/instantiation machinery (`JsPreambleState`,
`module_var_count`, property-key prelink).

So JS-P1 needs a const image JS does not have. Two shapes, unresolved:

- **Give JS a consts BSS**, mirroring Lambda. Cheapest load (three memory ops,
  no call), but touches JS module creation and the preamble state that carries
  a compiled unit between realms.
- **Reach consts through the existing module-state call**, e.g.
  `lambda_active_module_const_at(index)`. No new module plumbing, and still a
  clear win over the status quo — the call returns an existing pointer instead
  of allocating a GC String — but a call per const load rather than a load.

Neither is the real cost. **MIR is cached**, so a const *pointer* cannot travel
with a compiled unit — only bytes can, and the pointers must be rebuilt when the
unit is linked into a realm. Lambda solves this with a per-module BSS populated
at load; JS has no equivalent.

JS does, however, already solve the identical problem for property keys:

- `jm_build_property_key_image()` serializes the keys into the compiled artifact;
- `JsPreambleState` carries them as `module_property_specs` / `_count` /
  `_bytes_size`, surviving realm cloning and the MIR cache;
- `lambda_module_state_link_property_keys()` materializes them into module state
  at execution, before any generated code runs.

**JS-P1 is that pattern, for values.** A const image with the same three phases —
build, carry, link — feeding `lambda_module_state_bind_static`, whose runtime
accessor (`lambda_module_const_at_state`) already exists. The emission change is
then one function: `jm_box_string_literal` interns by content and emits a load
instead of a `js_make_string_len` call, and all 44 call sites follow.

So the work is: (1) a value image mirroring the key image, (2) carriage through
`JsPreambleState`, (3) a link step beside the property-key link, (4) the
emission switch. Steps 1–3 are the subsystem; step 4 is a day's work once they
exist.

This is **new infrastructure inside JS's module system**, not a port. The storage
*design* transfers; the storage *mechanism* does not. It also sits in the
preamble/realm-cloning path, where a mistake is silent and cross-realm — so it
wants its own session with the key-image code as the worked example.

### 9.4 Ledger

| ID | Ruling |
|---|---|
| **RC-J1** | **The storage design is shared; the folder is not.** `AstNode`'s handle, the const pool, the two `AstConstKind` cases and the emission split (RC5v2, RC12v2, RC13, RC18) carry over unchanged. Evaluation does not: **S3.1–S3.3** truthiness is Lambda's, and JS coercion can run user code through `valueOf`/`toString`. One storage design, two folders — the same shape **D8.1.3v10** settled on for the two interpreters. |
| **RC-J2** | **Pointer-backed literal values move into the const pool; their per-evaluation construction is retired** (§9.5). Immediates are **not** pooled — `jm_box_float_const` already bakes a self-contained double and `jm_boxed_immediate_const` a bool/null, which is the correct emission and a pool load would be strictly worse. This is the same immediate/pooled split RC5v2 draws for Lambda. No literal value spends a `NameId`; property *keys* keep the NameId path untouched. |
| **RC-J3** | **Purity gating is JS-specific and must be built before any call folds.** JS has no `is_proc`. Property access can invoke a getter, most builtins can throw, and coercion can re-enter user code — so a JS purity judgement is an analysis, not a bit lookup. Until it exists, **D6.1.1**'s "guests mark all-effectful" governs and nothing calls. |
| **RC-J4** | **RC14 inertness needs a JS restricted-eval mode first.** Lambda's guarantee rests on `EvalMode::CONST`, per-node fuel, `mode_rejected` and a throwaway rooted frame. JS has none of these. A fold that can throw, suspend, or allocate unboundedly during compilation is not admissible. |
| **RC-J5** | **User-defined function calls are deferred**, for the same reasons as Lambda (Appendix C.5) and more: a JS callee can throw, await, or capture a realm. |
| **RC-J6** | **JS container literals are NOT poolable** (measured 2026-09-12). RC-O2's identity rule decides it: a Lambda constructed container has no observable identity, but a JS array/object does — `mk() === mk()` is `false` and mutating one result does not affect another. Every evaluation of `[1,2,3]` or `{a:1}` must yield a fresh object. Sharing one materialized container would break `===` and mutation isolation. **RC15 materialization therefore does not transfer to JS at all**; only *primitives* are poolable there — strings and bigints, both immutable and compared by value (`123n === 123n` is `true`). |
| **RC-J7** | **The module state owns a JS unit's const pool, not the transpiler.** Lambda's pool is safe because `Script` is registered with the `Runtime` and outlives every tier; a JS unit's `JsTranspiler` is ephemeral — `js_mir_compile_unit` destroys it (and, through `runtime_free_script`, `pool_destroy(tp->pool)` and `arraylist_free(tp->const_list)`) while its compiled code stays live in **preamble mode** (harness function objects hold code pages) and in **hot-reload batch mode** (`jm_defer_mir_cleanup`). Binding `tp->const_list->data` into `LambdaModuleState::consts` therefore publishes a pointer that is freed before its last read. So JS const bodies are `mem_calloc`'d rather than pool-allocated, and `js_mir_runtime_link_pass` calls `lambda_module_state_adopt_consts` — which copies the index array into state-owned storage and takes the entries — instead of `lambda_module_state_bind_static`. `LambdaModuleState` gains `const_count` + `consts_owned`; release frees the entries beside `property_keys`, which is the same state-owned pattern (**D5.2**). Lambda is unchanged: `bind_static` still borrows its Script-owned pool. **Constraint**: the state holds one const array, so a unit's pool is only valid while its own module state is active. The preamble harness and the tests run in *separate* states (`js_batch_reset_to`), but consecutive tests in a batch reuse `batch_test_module_state_id`, so a test's array replaces its predecessor's — the same generation-reuse the module *var* slab already has. **KNOWN DEFECT (open, merged 2026-09-12 by decision):** the constraint is violated *within a single script* by `eval`. An `eval`'d unit compiles separately but shares the caller's variable environment, so its link pass replaces the outer script's array and the outer script's later literal loads resolve against the wrong one; `lambda_module_const_at_state` has no bounds check, so a stale index reads adjacent memory and casts it to `String*`. Measured on `test/js/eval_basic.js`: `t2:hello` returns the script's filename. **Fix direction**: key the load on the compilation *unit*, not the active module state — the AST tier already does this by reaching literals through node identity (`literal->value.string_value`), which cannot alias. An integer `unit_id` assigned at transpiler creation and registered at link time (`lambda_unit_const_at(unit_id, index)`) is the relocatable stand-in for that pointer (**DI14**), plus the missing bounds check. |

### 9.5 Phases

| Phase | Content | Retires |
|---|---|---|
| **JS-P1** | **Pointer-backed const *primitives* into the pool.** (Containers were in scope until RC-J6 measured them unpoolable.) A literal whose value needs run-time construction gains a pool entry and a node handle; Materialization only — no evaluation, no folder — so RC-J3 and RC-J4 are not prerequisites. Containers are excluded by RC-J6. | `jm_box_string_literal` (44 sites, calls `js_make_string_len` per evaluation), `jm_string_literal_chars` (4), `jm_box_bigint_literal` (2, re-parses digits per evaluation). Per-evaluation array/object construction **stays** — RC-J6. |
| **JS-P2** | **Expressions and pure sys-func calls.** Requires RC-J4's restricted-eval mode and RC-J3's purity analysis first. | the runtime arithmetic behind constant JS expressions |
| — | **User function calls: deferred** (RC-J5). | — |

JS-P1 is the whole of the value: it removes a GC allocation per literal
evaluation and one runtime call per string literal, and it needs neither a
folder nor a purity analysis. JS-P2 should not start until JS-P1 has shipped and
RC-J3/RC-J4 exist.

---

## 10. Not done

| Residue | Status |
|---|---|
| **User-defined `fn` calls** (RC3.4) | **Deliberately out of scope**, ruled 2026-09-12. Rationale: Appendix C.5. |
| **Payload deletion for expression-position literals** (RC17v2) | **Blocked.** Needs proof that a `build_lit_*` result never reaches type position; failure there is silent. Appendix C.4. |
| **Rehoming** (RC7) | **No reachable case.** Eligibility cannot yet produce a pointer-backed computed value — every producible result is self-contained. Unblocks when eligibility widens further. |
| **D8.6.4v2 ratchets** | **Unmeasured** across all phases. RC-P3 should have moved MIR volume down; nothing has confirmed it. |

Open questions: **RC-O3** (which floats are inline-representable — resolved in
practice by `lambda_float_ptr_to_item`'s packing rule), **RC-O4** (should folding
a const binding read also allow eliminating the binding, or is that DCE?),
**RC-O5** (does RC6 sharing extend to the MarkPack pool of CP25?).

---

# Appendices — History

## Appendix A — What motivated this

Constant folding existed as one pass, `{"const-fold", requires INDEXED, produces
ANALYZED}`, running the real AST interpreter on a throwaway frame. That core was
right. Everything around it was too narrow, and the narrowness compounded.

- **A.1 Eligibility admitted four node shapes** — literal `PRIMARY`, `UNARY`
  NOT/NEG/POS, `BINARY` arith/cmp/logic, `IF_EXPR`. No identifier reads, no
  calls. `1 + 2 * 3` folded; `x + 1` never did, however constant `x` was.
- **A.2 Results were restricted to three type IDs** (NULL/BOOL/INT). The
  principle — a baked value must be self-contained — was right; the whitelist was
  not. Float was grouped with pointers although the self-tagged double
  representation makes the Item *be* the IEEE bits for most doubles.
- **A.3 Float was evaluated and then discarded.** A.1 admitted `LMD_TYPE_FLOAT`;
  A.2 rejected it. Every float const expression burned a frame and fuel, then was
  thrown away — on every compile, by construction.
- **A.4 The fact was consumed on two MIR paths only**, both boxing boundaries.
  `[1 + 2, 3 * 4]` folded; `let a = 1 + 2` did not — it lowered through the
  native int lane emitting `mov 1; mov 2; add`, with the fact sitting unread.
- **A.5 The producer was the interpreter, which never read its own result.** T0
  re-evaluated every constant expression on every execution.
- **A.6 No sharing.** `const_list` was a plain append at every site.
- **A.7 Type disagreement was masked.** Checked twice, silently skipped twice.
- **A.8 Literal values were recovered by re-reading source text.** `&LIT_INT` and
  `LIT_BOOL` are valueless singletons, so `ast_static_literal_item()` re-parsed
  the span. This also couples AST validity to source retention — the REPL must
  keep its source buffer alive because spans point into it. *Still true;* see §10.
- **A.9 The pass re-ran.** A retained AST begins a fresh unit with `ANALYZED`
  unseeded, so the fold repeated over settled facts.

## Appendix B — Phase record

| Phase | Content | Outcome |
|---|---|---|
| **RC-P0** | Representation-based admission, replacing the type-ID whitelist. | Landed. Floats fold. |
| **RC-P1** | Report type disagreement; delete the consumer's duplicate check. | Landed. Measured **0 firings** over the corpus before removal. |
| **RC-P2** | Fold each node once, via an index watermark. | Landed. |
| ~~**RC-P2b**~~ | Literals out of the folder; retire the span re-read. | **Withdrawn** — first half was already true (A.8 correction, Appendix C.1); second half folded into RC-P6b. |
| **RC-P3** | Consult the handle on all lowering paths; emit in the consumer's lane. | Landed, binary then unary/if. |
| **RC-P4** | Interpreter consumes its own facts. | Landed. |
| **RC-P5a** | Const containers materialized, not folded; shared by both tiers. | Landed. |
| **RC-P5** | Rehoming. | **No reachable case** — §10. |
| **RC-P6** | Eligibility widening under the purity gate. | Landed for identifiers and pure sys-func calls; user `fn` calls out of scope (C.5). |
| **RC-P6b** | RC17/RC18. | Handle half landed via RC-P8; payload deletion blocked (C.4). |
| **RC-P7** | Pool sharing. | Landed. Measured: 12 intern calls → 2 entries; 3 `"hello"` literals → 1 `String`. |
| **RC-P8** | Retire `AstNodeFacts`. | Landed. `sizeof(AstNode)` 32; forced-GC oracle clean. |

Every phase held the baseline at its 18 pre-existing failures.

## Appendix C — Reversals and corrections

### C.1 A.8 was overstated — literals never entered the folder

The original §1.8 claimed the folder pushed literals through the interpreter. It
did not: the fold loop short-circuits on `AST_NODE_PRIMARY` *before* consulting
eligibility. `interp_const_node_supported`'s `PRIMARY` arm is reached only by
recursion from a parent, validating an operand as a foldable leaf — necessary
work. The claim came from reading the predicate without checking the caller that
gates it. What is real is A.8's narrower half: literal values are recovered by
re-reading source text.

### C.2 RC5 → RC5v2, RC12 → RC12v2

RC5 specified two storage cases by representation — immediates inline in the
node's literal `Type`, everything else pooled. RC12 said the fold's result "lives
on the node's type". Neither survived RC-P8: `folded_item` is 8 bytes and would
have grown `AstNode` to 40, so folded values went to the pool as well. One
storage home, one handle, and the representation split moved to emission — which
is also what unblocked RC6 sharing, previously impossible because folded
immediates lived outside the pool entirely.

### C.3 RC11 restated per node

RC11 originally said a retained unit preseeds `ANALYZED` and skips the pass. That
is wrong: the REPL *appends* to a retained index, so a unit-level skip strands
every newly typed expression unfolded — silently losing the optimization rather
than breaking anything. A per-node watermark gives the same guarantee and stays
correct under growth.

### C.4 RC17 → RC17v2 — `Type` is not uniformly valueless

RC17 said "`Type` is valueless; every constant value lives in the const pool".
That holds for a literal *expression*, whose value is a property of the node. It
fails for a literal **type**, where the value is the type's identity.

`validate.cpp:152` matches a value against a literal type by reading the payload
off the type:

```c
TypeString* literal_type = (TypeString*)type;
... s2it(literal_type->string) ...
// Literal-union members are value singletons; a primitive TypeId check
// alone would admit every string/symbol into the union.
```

There is no AST node in that call — the validator walks a `Type*` against a
runtime `Item`, so a per-node handle cannot serve it. `parse_type_pattern.cpp`
builds value-bearing Types inside the *type* grammar, and its comment records
that a shared valueless singleton was already rejected there.

Deleting the payloads would break literal unions (`"a" | "b"`), type patterns
(`[3.14]`) and `type T = 3.14`. RC17v2 splits the two roles. The consequence for
§10: expression-position payload deletion needs proof that a `build_lit_*` result
never reaches type position, and failure there is *silent* — a literal union
would quietly admit every string rather than erroring.

### C.5 User-defined `fn` calls are out of scope

RC3.4 licenses folding any call to a pure callable with const arguments.
Implementation stops at pure sys-funcs.

A sys-func call is a *closed* evaluation: native callee, purity declared on
`SysFuncInfo`, nothing of the unit's state participating. Folding a user call
makes the folder **execute user code during compilation**:

- The fold pass runs at `depth_limit = 1` — no nested activation records. A user
  call needs a callee frame.
- Recursion becomes reachable. Fuel bounds the work, but `depth_exhausted`
  publishes a stack-overflow diagnostic as a *runtime completion*, which RC14
  forbids a fold attempt from doing.
- The callee body may read module bindings, and the slab is empty at compile
  time — the RC3.3 problem again, now inside a callee frame, extending to
  closures and captures.

Worth doing; belongs in its own phase with RC14 as the explicit constraint.

## Appendix D — Mistakes recorded

- **Interning a symbol literal through the `String` layout.** Shipped, then
  caught: 101 baseline tests failed (18 → 119). A symbol is a `Symbol*` carried
  in a `String*` slot and the layouts differ — `Symbol` has `ns` where `String`
  has its flags, so `chars` sits at a different offset. The interner hashed
  whatever followed `ns` and returned the wrong shared symbol. The surrounding
  source carries the comment "different layout from String"; it was read and the
  implication still missed. Loud only because symbols are well covered — the same
  error in a type-position literal would have been silent, which is the concrete
  argument for keeping payload deletion blocked (C.4).
- **Resolving a const initializer through `ast_static_literal_item` first.** That
  function reports what a node's *type* carries, not what it evaluates to, and
  inference hands a computed node a literal-valued type: `floor(2.7)` inherits
  its argument's literal float type, payload 2.7. Reading `let b = floor(2.7)`
  resolved to the argument — T0 gave 2, MIR gave 2.7. A published handle must
  answer first and unconditionally; only a bare literal node may answer from its
  type. Found by running T0, MIR and a `LAMBDA_CONST_FOLD=0` control together:
  the control agreeing with T0 placed the defect in the fold rather than the
  runtime. **That triangle is the standing check.**
- **A type-ID whitelist for admissible results.** Rejected by RC4. It produced
  A.2's float error: a representation question answered with a type enumeration,
  which went stale when the double representation changed beneath it.
- **Keeping the fold MIR-only.** Rejected by RC1. The interpreter produced the
  fact and could not use it — an inversion that persisted because the fact lived
  in a table only the back end read.
- **Silent skip on type disagreement.** Rejected by RC10. Masking is not
  conservatism: it turns a compiler defect into an invisible missed optimization.
- **Folding literals.** Rejected by RC15 — and the folder never did it, so the
  ruling documents an invariant rather than changing behaviour (C.1).
- **Keeping `const_index` on a `TypeConst` carried only by const AST nodes.**
  Considered while resolving RC-O1. Tidy for a literal, but a folded expression
  is any node kind, and retyping `1 + 2` as `TypeConst` would replace its
  inferred `int` type with a statement about its constness.
- **Relocating the fact without fixing the fold** — the P7 U-D plan. The analysis
  mistook a near-dead mechanism for a working one whose storage was misplaced.
- **Refusing pooled values to satisfy DI14.** Reasonable conservatism, but it
  declined the values instead of using the mechanism that already handled them
  for source literals, and the restriction propagated backwards into eligibility
  (A.3).

**Measurement note.** Three scope estimates in this ledger were inflated by
matching a field or predicate name without checking which struct or caller owned
it: the "294 name-keyed lookups" (actually 138), the "58 cast sites / 23
`double_val` readers" (`TypedItem` also has `double_val`), and A.8. A name match
is not evidence that a thing is reached.
