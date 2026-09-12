# Runtime Constant Handling — Folding, the Const Pool, and Shared Values

**Date:** 2026-09-11
**Status:** PROPOSAL — not ratified. Supersedes the const-folding slice of
[`Lambda_Proposal_JS_Unify_P7.md`](Lambda_Proposal_JS_Unify_P7.md) U-D, which
treated the existing fold as sound and only moved where its result was stored.

**Formal authority:** **D6.1.1–D6.1.2** (the purity bit; it *is* the
const-folder's soundness gate), **D8.2.2** (purity is a core flag and the
const-folder's soundness gate), **D8.2.5v2** (fact placement: core runtime
facts on the AST, no ID-keyed per-node fact table), **D2.4.1–D2.4.3** (one
authority per compiler fact; carrier/representation), **DI14** (no stale
address may enter cacheable MIR), **D5.3.3** (fold frames own their rooting),
**D8.1.1v2** (T0 AST interpreter) in
[`doc/Lambda_Formal_Design.md`](../doc/Lambda_Formal_Design.md). Semantics:
**S12.1** (effect system). The formal specifications win on disagreement.

**Ledger prefix:** `RC`.

**Status (2026-09-12):** RC-P0 … RC-P8 landed. Two deliberate residues, each
with a recorded reason:

| Residue | Why it is open |
|---|---|
| User-defined `fn` calls (RC3.4) | Deliberately out of scope — §13. A sys-func call is a closed evaluation; a user call makes the folder execute user code at compile time. |
| RC-P6b payload deletion (RC17v2) | Blocked — §12. Needs proof that a `build_lit_*` result never reaches type position; failure there is silent. |

Superseded rulings, kept with their replacements so the reversal is auditable:
**RC5v2** (one storage home, not two), **RC12v2** (result in the pool, not on
the type), **RC17v2** (a `Type` carries a value only when the value is part of
the type's meaning), **RC-P2b** (withdrawn), **RC11** (restated per node, not
per unit).

**Owed:** the **D8.6.4v2** compiler-time and MIR-volume ratchets are unmeasured
across all phases. RC-P3 should have moved MIR volume down and nothing has
confirmed it.

---

## 1. Present state

Constant folding exists as one compiler pass — `{"const-fold", requires
INDEXED, produces ANALYZED}` ([`transpile-mir.cpp:34000`]) — implemented by
`interp_const_fold_script()` ([`interp.cpp:5294`]). It runs the real AST
interpreter in `EvalMode::CONST` on a throwaway frame with a per-node fuel
budget and a native-fault container, so a folded result cannot disagree with
what executing the code would have produced, and a fold attempt cannot alter
program completion. **That core is right and this design keeps it.**

Everything around it is too narrow, and the narrowness compounds.

### 1.1 Eligibility admits four node shapes

`interp_const_node_supported()` ([`interp.cpp:5232`]) accepts only:

- `PRIMARY` wrapping a literal type of NULL/BOOL/INT/FLOAT;
- `UNARY` with `NOT`/`NEG`/`POS`;
- `BINARY` with arithmetic, comparison, or logical operators;
- `IF_EXPR`;

recursively, over operands that are themselves eligible. **No identifier
reads. No calls.** So `1 + 2 * 3` folds; `x + 1` never folds, however
obviously constant `x` is; a pure `fn` applied to literal arguments never
folds, although **D6.1.2** exists precisely to license that.

### 1.2 Results are restricted to three type IDs

`interp_const_result_is_immediate()` ([`interp.cpp:5283`]) keeps only NULL,
BOOL and INT. Its comment gives the reason:

> fold facts deliberately carry only tagged immediate values. Float and all
> pointer-backed results may be valid during the attempt but must not survive
> its side-stack lifetime or enter cacheable MIR as a stale address (DI14).

The *principle* is correct: a folded value is baked into MIR that may be
cached and reloaded, so it must be self-contained. The *whitelist* is not.

- **Float is grouped with pointers, and is not one.** Since the self-tagged
  double representation ([`lambda.h:169`]) the Item **is** the raw IEEE bit
  pattern for the overwhelming majority of doubles — as self-contained as an
  INT. Only the boxed residue is pointer-backed.
- **Everything the const pool already holds is excluded** — string, decimal,
  datetime, binary, sized numerics — even though those are exactly the values
  the pool exists to carry, with module-state indirection and rebinding
  already solved for source literals.

### 1.3 Float is evaluated and then discarded

§1.1 admits `LMD_TYPE_FLOAT`; §1.2 rejects it. So every float constant
expression allocates a frame, burns fuel, evaluates fully, and is thrown
away — on every compile, by construction. The two predicates disagree and one
of them is wrong.

### 1.4 The fact is consumed on two MIR paths and nowhere else

`mir_emit_const_folded_item()` ([`transpile-mir.cpp:25386`]) has exactly two
callers: the generic box-an-Item path ([`:24768`]) and array element storage
([`:16134`]). Both are *boxing* boundaries.

Measured on the current build:

- `[1 + 2, 3 * 4, true and false]` folds — the MIR emits
  `mov %ra, 0x0500000000000003` (3), `mov %rb, 0x050000000000000c` (12), and a
  bool false.
- `let a = 1 + 2` does **not** fold. It lowers through the native int lane and
  emits `mov %r9, 1; mov %ra, 2; add %rc, %r9, %ra`, plus the int-range check
  and boxing sequence. The fold fact for that node exists and is ignored.

### 1.5 The producer is the interpreter, which never reads its own result

`interp.cpp` writes `flags |= CONST_FOLDED` and `folded_item`; the only
readers are in `transpile-mir.cpp`. The T0 AST interpreter therefore
**re-evaluates every constant expression on every execution**, having already
computed the answer at compile time. Const folding is currently a MIR-only
optimization produced by the interpreter for someone else's benefit.

### 1.6 No sharing

`const_list` is a plain `arraylist_append` at every construction site
([`build_ast.cpp`] 2487, 3018, 3181, 3442, 3483, 3538). There is no hashing
and no dedup: two occurrences of the same constant occupy two slots. The Const
Pool ledger (CP1–CP26,
[`Lambda_Design_Const_Pool.md`](Lambda_Design_Const_Pool.md)) covers MarkPack
serialization and file-level content hashing, not compile-time value sharing,
so this is unclaimed ground.

### 1.7 Type disagreement is masked rather than reported

Agreement between the folded value's type and the node's inferred type is
checked **twice** — producer at [`interp.cpp:5338`], consumer at
[`transpile-mir.cpp:25398`] — and **both silently skip** on mismatch.

A folded type must always agree with the inferred type. If they disagree,
either the folder or inference is defective. Declining to fold hides that
defect and reports it as nothing at all. This is a workaround standing where a
root-cause fix belongs (CLAUDE.md rule 1).

### 1.8 Literal values are recovered by re-reading source text

**Corrected 2026-09-12.** This section first claimed literals were pushed
through the folder. They are not: the fold loop short-circuits on
`node->node_type == AST_NODE_PRIMARY` *before* consulting eligibility, so no
literal is ever evaluated. `interp_const_node_supported()`'s `PRIMARY` arm is
reached only by recursion from a parent, where it validates that an operand is
a foldable leaf — necessary work, not overhead. The claim was read off the
eligibility function without checking the caller that gates it.

What is real is narrower and sits elsewhere. An in-band int literal is typed
`&LIT_INT`, a valueless singleton ([`build_ast.cpp:3896`]), and bool literals
share `LIT_BOOL`. Neither carries its value, so `ast_static_literal_item()`
recovers it by **re-reading the source span** ([`build_ast.cpp:996`–`1003`])
after its value-bearing fast path misses. Seven build-time callers depend on
this — type patterns, ranges, map keys.

Float, string, datetime, decimal and binary literals already carry values, so
only int, bool and null take the text-recovery path. Beyond the redundant
parse, it couples AST validity to source retention: the REPL must keep its
append-only source buffer alive precisely because spans point into it.

### 1.9 The pass re-runs

`pass_manager` lives on the `Transpiler`, guarded by `if (pass_count == 0)`.
A retained AST "begins a fresh unit" ([`transpile-mir.cpp:33991`]) — new
`Transpiler`, fresh manager, `FRONTEND|INDEXED` preseeded but not `ANALYZED` —
so const-fold runs again over an index that already carries its facts. The
wholesale reset at the top of the pass exists to serve that redundancy.
Folding is deterministic, so the second run provably recomputes identical
answers.

---

## 2. Design

### 2.1 Ledger

| ID | Ruling |
|---|---|
| **RC1** | **One const-folding pass serves both tiers.** The fold is a property of the compilation unit, not of a back end. Its result is consumed by the T0 AST interpreter and by MIR lowering alike. A fold that only one tier can use is a defect, not a design. |
| **RC2** | **Eligibility is bounded by the AST interpreter's evaluation capability, gated by purity.** Any *expression* the T0 interpreter can evaluate is a fold candidate; **D6.1.2**'s purity bit decides whether it *may* be folded. Not a hand-maintained node-shape whitelist. Const values (RC15) are outside this entirely — they are materialized, not evaluated. |
| **RC3** | **Constness is compositional over the pure fragment** (§3): a literal is const; a pure operator over const operands is const; an immutable binding whose initializer is const is const; a `fn` call with const arguments is const; anything reaching a `pn`, a mutable binding, or an effectful surface is not. |
| **RC4** | **Folded results are values, not type IDs.** The admission test is a *representation* question — "is this Item self-contained, or does it need pool residency?" — never a type-id whitelist. |
| **RC5v2** | **One storage case: the const pool** (§5; revised from RC5's two-case split once RC-P8 landed). Every const value — folded immediate, folded non-immediate, or materialized container — is a `const_list` entry. The node holds only a handle. The representation split survives at **emission**, not in storage: MIR reads the slot at compile time and bakes a self-contained word, or emits `emit_load_const` otherwise. |
| **RC6** | **The const pool is content-hashed and shared.** Equal constant values occupy one pool slot, whatever their origin — source literal or folded result. Identity of a pooled entry is its content hash, not its construction site. |
| **RC7** | **A pooled fold result is rehomed before its frame closes** (§4). The fold's throwaway frame owns nothing that outlives it; a value destined for the pool is copied into script-pool storage during the attempt, or the fold is abandoned. |
| **RC8** | **MIR lowering handles both cases and must consult the fact on every path** (§6.2), not only at boxing boundaries. An immediate is baked; a pooled value loads through `const_index`. |
| **RC9** | **The interpreter consumes the fact directly** (§6.1). A folded node evaluates to its stored value without re-entering evaluation. |
| **RC10** | **Folded type and inferred type must agree; disagreement is a defect and is reported** (§7). The producer checks it once, under a greppable `const-fold: RC10` prefix; the consumer does not re-derive it. It **reports and declines, never aborts**: RC14 requires a fold attempt to be semantically inert, and `abort()` alters program completion maximally. Measured 0 firings over the full `test/lambda` corpus before the consumer's duplicate check was removed. |
| **RC11** | **Each node folds exactly once.** The index carries a watermark; the pass resumes there and needs no wholesale reset. Stated per *node*, not per *unit*, because a unit can grow: the REPL appends to a retained index, so a unit-level skip would strand the appended nodes unfolded. |
| **RC12v2** | **No per-node fact side table** (revised: RC12 said the result lives *on the node's type*; as built it lives in the **pool**, named by a handle on the node). `AstNode` carries `AstConstKind const_kind` + `uint32_t const_index` in its existing padding — `sizeof(AstNode)` stays 32. `AstNodeFacts` and `AstIndex::facts` are deleted, along with `inferred_type` (duplicated `node->type`), `declared_contract` (the declaring nodes already carry `declared_type`) and `representation` (never read). |
| **RC13** | **Nothing pointer-backed enters cacheable MIR as a raw address** (**DI14**). Pooled values are reached through the module-state const indirection, never by a baked pointer. This is what makes RC5's second case safe. |
| **RC14** | **Fold attempts remain semantically inert.** Fuel exhaustion, a native fault, an error result, or a rejected attempt leaves the node unfolded and changes nothing observable. A compiler optimization may never alter program completion (retained from today's design). |
| **RC15** | **Const values are pooled when they are built and never enter the folder.** A *const value* is any value the source already determines: a scalar literal, and equally a **static/const container** — an array, map or element literal whose parts are all const. Its value needs no evaluation, so it is materialized directly into the const pool (RC6-hashed) at build time. **The folder's input is expressions only.** No const value is interpreted, and none has its value recovered by re-reading a source span — which retires the text-recovery path in `ast_static_literal_item()` for the valueless `&LIT_INT`/`LIT_BOOL` singletons. |
| **RC16** | **Rehoming applies only to computed const-expression results** (§4). A const value — scalar or container — is born in pool-owned storage and needs none; only a *computed* result is born in frame/GC storage during the fold and must be moved before its frame closes. The two paths meet at the pool, never before it. This is why a folded aggregate needs no rehoming: an aggregate literal is a const value under RC15, so it never reaches the folder at all. |
| **RC17v2** | **A `Type` carries a value only when that value is part of the type's meaning** (ratified 2026-09-12, supersedes RC17; rationale §12). Literal types used for *matching* — union members, type patterns, annotations — keep their payload: the value is the type's identity, and the validator reads it with no AST node in reach. A literal in **expression** position carries no value on its type: its value is a const-pool entry named by the node's handle, and its type may be a shared singleton. RC17's blanket "Type is valueless" generalized from the second case to the first and would have broken literal unions, type patterns and `type T = 3.14`. |
| **RC18** | **Three-way separation of a constant** (applies to expression-position constants; RC17v2 exempts type-position literals). *Which kind* is the `Type` — now a shared valueless singleton (`LIT_INT`, `LIT_FLOAT`, `LIT_STRING`, …), one per type, never per literal. *Which value* is a per-node const handle on the `AstNode`. *The value itself* is one hashed, shared const-pool entry (RC6). No literal allocates a `Type`. The handle is on the node, not on a const-specific type, because **a folded expression is an arbitrary node kind** — a `BINARY`, a `CALL`, an `IF_EXPR` — whose type stays its own inferred type. Having a constant value is a per-node fact; it is not a type. |

### 2.2 What is deliberately retained

The existing pass's *mechanism* is sound and survives unchanged: real
interpreter semantics rather than a parallel arithmetic evaluator; a throwaway
frame so every intermediate is rooted and its side-stack extent disappears
(**D5.3.3**); a per-node fuel budget; fault containment. RC14 preserves the
inertness property those provide.

---

## 3. Constness (RC3)

**D6.1.2** already rules that the purity bit is the const-folder's soundness
gate. The current folder never exercises it, because it admits no calls and no
identifier reads — the gate has nothing to gate. Widening eligibility is what
brings **D6.1.2** into force.

A node is **const** when:

1. it is a literal; or
2. it is a pure operator applied to const operands; or
3. it is an identifier bound by an **immutable** binding whose initializer is
   const; or
4. it is a call to a **pure sys-func** whose arguments are all const. A call to
   a user-defined `fn` is *semantically* const under D6.1.2 but is **out of
   scope, deferred** (ruled 2026-09-12, §13); or
5. it is a control form (`if`, `match`) whose scrutinee and taken arms are
   const.

A node is **not const** when it reaches a `pn`, a mutable binding, a `var`
parameter, module-level mutable state, I/O, or any surface whose purity bit is
unset. Guest builders mark all-effectful (**D6.1.1**), so a hosted-language
node is non-const by default and never folds by accident.

Two consequences worth stating:

- **Constness is a compile-time judgement, purity is a declared fact.** The
  folder does not infer purity; it reads the bit. Where the bit is absent it
  must assume effectful.
- **A const binding read is not a fold of the binding.** Folding `x` where
  `let x = 1 + 2` replaces the *read* with `3`; it does not remove the
  binding, which may be observed elsewhere.

---

## 4. Lifetime and rehoming (RC7, RC16)

This is the substantive new work, and the reason the current design restricted
itself to immediates.

**Scope: computed expression results only (RC16).** Literals never take this
path — under RC15 they are pooled at build time, in pool-owned storage, from a
value that is already in hand. Rehoming exists solely because a *folded* value
is produced somewhere a literal never is.

The fold evaluates on a frame whose side-stack extent is released immediately
after the attempt. A folded `String`, `Decimal`, `Binary` or container
allocated during evaluation dies with it. Keeping such a value means copying
it into script-pool storage and registering it in the const pool **before the
frame closes** — not after, and never by retaining a pointer into the frame.

Rules:

- **RC7a** — the rehome happens inside the attempt, while the value is still
  rooted. A fold that cannot rehome (allocation failure, an unsupported
  carrier) is abandoned under RC14 and the node stays unfolded.
- **RC7b** — the pool takes ownership of a copy. The evaluated value is never
  handed to the pool by reference.
- **RC7c** — a value whose carrier the pool cannot own (a live GC container, a
  closure, anything with identity semantics under **S5.4.2**/**S5.5.1**) is
  not foldable. Identity-bearing values must not be shared by content hash.

RC7c is the boundary that decides how far RC2's "whatever the interpreter can
evaluate" actually reaches. Scalars and immutable aggregates are candidates;
anything whose identity is observable is not.

---

## 5. Storage (RC5v2) — as built

**One home: the const pool.** Every const value is a `const_list` entry, and the
node holds a handle into it:

```c
struct AstNode {
    AstNodeType  node_type;
    AstConstKind const_kind;   // NONE | FOLDED | POOLED
    uint32_t     const_index;
    Type        *type;         // the inferred/effective type (D8.2.5v2)
    ...
};                             // still 32 bytes
```

Both fields occupy the six bytes of padding that already sat between the 16-bit
tag and the first pointer, so `sizeof(AstNode)` is unchanged, subclass layouts
are unchanged, and the tree has no `offsetof(AstNode, …)` pins to break.
Zero-initialized allocation makes `AST_CONST_NONE` the default with no init pass.

### 5.1 Two kinds, differing only in indirection

| Kind | Slot holds | Produced by |
|---|---|---|
| `AST_CONST_FOLDED` | a pointer to a pool-allocated `Item` | the fold pass, for a computed expression |
| `AST_CONST_POOLED` | **the container pointer itself** | the materialization pass, for a const ARRAY/MAP |

`POOLED` keeps the bare-pointer convention `emit_load_const(…, MIR_T_P)` already
expected, so the existing static-collection path needed no change. `const_kind`
is what lets one handle serve both without a kind array on the pool.

### 5.2 What did *not* happen

RC17's "a folded node receives a per-node pooled literal `Type`, and `TypeConst`
disappears" is **withdrawn** — see §12. Under **RC17v2** a literal in type
position keeps its payload, because there the value is the type's identity.
`TypeConst` and the literal `Type` subclasses remain.

The `&LIT_INT`/`LIT_BOOL` source-span re-read in `ast_static_literal_item()`
therefore also remains: retiring it needs expression-position literals to carry
handles instead of payloads, which is RC-P6b's blocked half.

What *did* land for literals is value **sharing** (§7): equal string/symbol
literals now intern to one `String` and one pool slot, without touching the
type's payload.

## 6. Consumers

### 6.1 Interpreter (RC9)

Evaluation of a node carrying a const handle returns the stored value directly —
always through `const_list[const_index]`, dereferenced for `FOLDED` and taken as
the pointer for `POOLED`. No re-entry into evaluation, no recomputation per run.

Gated twice, both cheaply: on node kind (only UNARY/BINARY/IF/IDENT/CALL/ARRAY/MAP
can carry a handle, so every other evaluation leaves the path on a switch), and
originally on `EvalMode::RUNTIME`. **That mode gate was later relaxed** — RC3.3
folding of `a + 1` recurses into the identifier, whose value is a published fact
rather than a slab slot, so CONST mode must read facts too. Safe because a fact
is published only after passing its RC10 agreement check.

This is the largest behavioural change in this design. It converts const
folding from a MIR-only optimization into a property of the compilation unit
that both tiers observe — **RC1**.

### 6.2 MIR (RC8)

Two cases, per the user-specified split:

- **Immediate** — read the pool slot **at compile time** and bake the word as a
  literal operand (`MIR_new_uint_op`). One instruction, self-contained,
  cache-safe; the pool *address* is never emitted. As built, the producer goes
  further and decodes the Item at compile time to emit the consumer's native
  lane: a folded int as a raw `mov` on the int lane, a float as `dmov`, a bool
  as 0/1 — not a tagged word the consumer must unbox again.
- **Pooled** — `emit_load_const(const_index)`: load module state, load the
  `consts` pointer from it, load the value. Three loads, and its comment
  explains why the pointer is never baked — "the module's const image may
  rebind after interning" ([`transpile-mir.cpp:6037`]). That indirection is
  what satisfies **DI14**/**RC13**.

The fact must be consulted wherever a node is lowered, not only at the two
boxing boundaries. `let a = 1 + 2` folding to an immediate in the native int
lane is the acceptance case for RC8.

**As built, two consultation points.** `transpile_expr_value` is the D8.2.6 core
boundary and covers generic paths. `emit_binary_value`, `emit_unary_value` and
`transpile_if` are reached *directly* by specialized callers that bypass that
boundary — the native-int declaration path in `transpile_let_stam` among them —
so the check sits at those producers. That covers every route into them without
chasing call sites, and it is sufficient because UNARY/BINARY/IF are exactly the
foldable node kinds (the pass skips PRIMARY outright).

---

## 7. Sharing and agreement

**RC6 — hashing.** Pool insertion is by content hash, so equal values share a
slot regardless of origin. Two source occurrences of `"abc"`, and a folded
`"ab" + "c"`, resolve to one entry. Hash identity applies only to values
without observable identity (RC7c).

**RC10 — agreement.** The folded value's type must equal the node's inferred
type. Today this is checked twice and silently skipped twice (§1.7). Under
RC10 the producer asserts it once, and the consumer trusts it. A mismatch is a
defect in the folder or in inference and must surface as one, never as a
quietly missed optimization.

---

## 8. Relationship to other rulings

- **D8.2.5v2 / U30v2** — satisfied and extended. The fold result is core
  runtime information, stays on the AST via the node's type, and needs no
  ID-keyed side table. `AstIndex::facts` retires (**RC12**), which is U-D's
  goal reached by a better route.
- **DI14** — honoured more strictly than today. Today's narrowness avoids the
  hazard by refusing the values; RC5/RC13 handle them properly by pooling them
  behind the module-state indirection.
- **D6.1.2** — brought into force for the first time on this path.
- **P7 U-D** — superseded. U-D preserved the existing fold and only relocated
  its result; the fold itself was the defect.

---

## 9. Phases

Each phase is independently landable, green on `make test-lambda-baseline` and
`test_js_gtest`, and non-positive on the **D8.6.4v2** governed-LOC delta.

| Phase | Content | Gate |
|---|---|---|
| **RC-P0** LANDED | Fix the §1.3 contradiction: reject float in eligibility *or* accept inline floats in the result test. Whichever, stop evaluating what is always discarded. | no MIR-volume growth; baseline green |
| **RC-P1** LANDED | RC10 — assert agreement, delete the duplicated silent skip. Any assertion that fires is a real defect to fix before proceeding. | baseline green with assertions armed |
| **RC-P2** LANDED | RC11 — resume the fold at a per-index watermark; delete the wholesale reset. **Not** by preseeding `ANALYZED`: the REPL *appends* to a retained index, so skipping the pass would leave every newly typed expression unfolded. A watermark gives RC11's actual guarantee — each node folds exactly once — while staying correct under growth. | REPL and retained-AST paths green |
| ~~**RC-P2b**~~ | **WITHDRAWN 2026-09-12 — folded into RC-P6b.** Its first half (remove literals from the folder) was already true, per the §1.8 correction. Its second half (retire the `&LIT_INT` span re-read) is real but is a *build-time* change to literal typing, and doing it now with per-literal value-bearing `Type`s would build exactly what RC17/RC18 then delete. The span re-read retires when literal values reach the pool — in RC-P6b. |
| **RC-P3** LANDED | RC8 — consult the fact on all lowering paths, not just boxing. `let a = 1 + 2` folds. | MIR volume drops; emission goldens re-based with attribution |
| **RC-P4** LANDED | RC9 — interpreter consumes fold facts. | T0 no longer re-evaluates constants |
| **RC-P5a** LANDED | RC15 — **const containers are materialized, not folded.** A literal ARRAY/MAP whose parts are all const becomes one pooled static container recorded as a const fact, built directly from the AST with no evaluation. `emit_static_collection_const()` already does this construction inside the MIR back end (§9.1); it becomes a consumer of the shared fact instead of a private builder, so T0 stops rebuilding the same array on every execution. | both tiers read one container; no aggregate reaches `interp_const_fold_script` |
| **RC-P5** | RC5/RC7/RC13 — pooled results with rehoming. **Blocked on RC-P6 as written (2026-09-12): there is nothing to rehome.** Eligibility admits only NULL/BOOL/INT/FLOAT literal operands, and no operator over those yields a pointer-backed value — `2 ** 100` and `9007199254740991 + 1` give floats (`1.27e30`, `inf`), `"ab" + "c"` is an error since `+` does not concatenate, and string comparison gives a bool. Every producible result is self-contained. The one reachable exception is a subnormal float, which is boxed. Rehoming needs eligibility widened first — to expressions that *compute* a pointer-backed value. Aggregate literals are **not** that case: under RC15 they are const values, materialized into the pool at build time without ever entering the folder (RC-P5a). Rehoming therefore waits on RC-P6's expression widening. | forced-GC oracle (`LAMBDA_GC_FORCE_EVERY=1`, `POISON_FREED=1`) green |
| **RC-P6** | RC2/RC3 — eligibility by interpreter capability under the purity gate. **COMPLETE as scoped (2026-09-12).** Reads of immutable const bindings fold transitively; calls reuse `interp_eval_mode_allows_sys_func`, which already encodes D6.1.2 (rejects `is_proc`/`is_async`) plus the restricted-mode set. User-defined `fn` calls are deliberately **out of scope** (§13). | baseline green; fold-rate census |
| **RC-P6b** | RC17/RC18. **Handle half landed via RC-P8** (`AstNode::const_kind`/`const_index`, `sizeof` unchanged). **Payload-deletion half is BLOCKED: RC17 is wrong as a blanket rule (2026-09-12, §12).** A literal `Type`'s payload is often *type semantics*, not a misfiled node fact — it defines which values inhabit the type. Deleting it would break literal unions, type patterns, and `type T = 3.14`. Needs RC17 restated before any of it is implementable. | — |
| **RC-P7** LANDED | RC6 — content-hashed pool sharing, in two parts. Folded values intern by their raw 8-byte word (a slot is fully described by its bytes; each consumer reads it through its own node's type, and const values are immutable, so equal words are interchangeable). String/symbol literals intern by content. Both maps live on `Transpiler`, not `Script`: `const_list` is Script-owned and the REPL rolls a failed statement back by truncating it, so a Script-scoped map would hold reclaimed indices. The earlier "blocked, const_list has no kind tags" note is superseded — RC-P8 put the kind on the *node*, which is where it was needed. | measured: 12 intern calls to 2 entries; 3 "hello" literals to 1 String |
| **RC-P8** LANDED | RC12v2 — retire `AstNodeFacts` / `AstIndex::facts`, ruled by the user: merge into `AstNode`, drop `inferred_type` (already `node->type`) and `declared_contract` (declaring nodes carry their own), no `index_id`, only `const_index`. `folded_item` would not fit the padding, so folded values moved to the pool as well — which is what unblocked RC-P7. | `sizeof(AstNode)` 32; forced-GC oracle clean; baseline 18 |

RC-P0 through RC-P2 are corrections to existing defects and carry no design
risk; all three have landed. The folder's input is already exactly the set that
needs rehoming — literals never enter it — so RC-P5 needs no literal special
case and no longer depends on a preceding literal phase. RC-P5 is where the
lifetime bugs live.

---

### 9.1 Aggregates already have a partial mechanism

`emit_static_collection_const()` ([`transpile-mir.cpp:6314`]) already builds a
pooled constant array/map for a literal ARRAY/MAP node: it constructs the
container from the AST into `script_pool`, marks it `is_static`/`is_immortal`,
appends it to `const_list`, and emits `emit_load_const(..., MIR_T_P)`.

So the aggregate half of RC-P5 is not greenfield. What it lacks is exactly what
RC1 asks for: it is built from the AST inside the MIR back end, is keyed to no
fold fact, and the interpreter cannot use it — T0 rebuilds the same array on
every execution. Routing it through the fold instead of duplicating it is the
cheaper path to RC5/RC7, and it reuses machinery already proven against the
GC's static/immortal contract.

## 10. Open questions

- **RC-O1** — ~~carrier for folded NULL/BOOL/INT~~ **RESOLVED 2026-09-11 by
  RC17/RC18**: no type carries a value, so there is no carrier to choose. The
  value is a pool entry; the node holds the handle; the type is a shared
  valueless singleton.
- **RC-O2** — ~~how far does RC7c reach for immutable aggregates?~~
  **RESOLVED 2026-09-12: immutable aggregates are foldable and poolable.** The
  identity line is *provenance*, not shape: a container **loaded from input**
  carries identity and must never be content-shared; a container **constructed**
  by the program (temporal) has none, so equal values may share one pool entry.
  A folded aggregate is constructed by definition — the folder evaluates a
  literal subtree — so it is always on the poolable side. RC7c therefore
  excludes input-loaded containers and identity-bearing values (functions,
  anything whose address is observable), not aggregates as a class.
- **RC-O3** — which floats are inline-representable, exactly? The boxed
  residue must be pooled rather than baked, so the test is a representation
  predicate that does not exist yet in this form.
- **RC-O7** — ~~RC18 places the const handle on `AstNode`; `index_id` took that
  padding instead~~ **CLOSED 2026-09-12.** The user ruled "no `index_id` in
  `AstNode`, only `const_index`". `index_id` was removed in RC-P8: with the
  handle on the node there is no facts lookup left to accelerate, so the cache
  had no remaining purpose. `ast_index_find` returns to its pointer hash for the
  callers that still need an id.
- **RC-O4** — should folding a const binding *read* also let the binding be
  eliminated when nothing else observes it, or is that a separate DCE concern?
- **RC-O5** — does RC6's content hashing extend to the MarkPack const pool of
  CP25, or is compile-time sharing a separate in-memory concern that the
  serializer collapses again on write?
- **RC-O6** — fold-rate and pool-size censuses are needed to size RC-P6 and
  RC-P7 before building them. Neither exists.

---

## 11. Rejected, and mistakes recorded

- **A type-ID whitelist for admissible results.** Rejected by RC4. It is what
  produced §1.2's float error: a representation question answered with a type
  enumeration, which then went stale when the double representation changed
  beneath it.
- **Keeping the fold MIR-only.** Rejected by RC1. The interpreter produces the
  fact and cannot use it — an inversion that persisted because the fact lived
  in a table only the back end read.
- **Silent skip on type disagreement.** Rejected by RC10. Masking is not
  conservatism: it converts a compiler defect into an invisible missed
  optimization.
- **Relocating the fact without fixing the fold** — the P7 U-D plan. Recorded
  because the analysis mistook a near-dead mechanism for a working one whose
  storage was merely misplaced. The prior analytical error in the same family
  is `Lambda_Design_Unified_AST.md` §13.6.
- **Folding literals.** Rejected by RC15 — and the folder never did it, so the
  ruling documents an invariant rather than changing behaviour (§1.8). Recorded
  because the analysis asserted the opposite: the eligibility function's
  `PRIMARY` arm was read as a fold entry point without checking that its only
  caller skips `PRIMARY` nodes outright. Third instance in this family, after
  `Lambda_Design_Unified_AST.md` §13.6 and the P7 U-A withdrawal — a predicate
  read in isolation says nothing about whether it is reached.
- **Interning a symbol literal through the `String` layout.** Shipped, then
  caught by the suite: 101 baseline tests failed (18 to 119). A symbol is a
  `Symbol*` carried in a `String*` slot and the layouts differ — `Symbol` has
  `ns` where `String` has its flags, so `chars` sits at a different offset. The
  interner hashed whatever followed `ns` and handed back the wrong shared
  symbol. The surrounding source carries the comment "different layout from
  String"; it was read and the implication still missed. Loud only because 101
  tests cover symbols — the same error inside a *type-position* literal would
  have been silent, which is the concrete reason RC-P6b's payload deletion stays
  blocked (§12).
- **Resolving a const initializer through `ast_static_literal_item` first.**
  Built and reverted during RC-P6. That function reports what a node's *type*
  carries, not what the node evaluates to, and inference hands a computed node a
  literal-valued type: `floor(2.7)` inherits its argument's literal float type,
  payload 2.7. Reading `let b = floor(2.7)` therefore resolved to the argument —
  T0 gave 2, MIR gave 2.7. A published fold fact must answer first and
  unconditionally; only a bare literal node may answer from its type. The
  divergence was found by running T0, MIR and a `LAMBDA_CONST_FOLD=0` control
  together: the control agreeing with T0 placed the defect in the fold rather
  than the runtime, and that triangle is the standing check for later phases.
- **Keeping `const_index` on a `TypeConst`, carried only by const AST nodes.**
  Considered and rejected while resolving RC-O1. It reads tidily for a literal,
  where the node *is* a constant, but it does not survive folding: a folded
  expression can be any node kind, and retyping a folded `1 + 2` as `TypeConst`
  would replace its inferred `int` type with a statement about its constness.
  Type answers *which kind of value*; the handle answers *which value*. Merging
  them costs a per-literal `Type` allocation and loses the node's real type.
- **Refusing pooled values to satisfy DI14.** Recorded rather than rejected:
  it was a reasonable conservative reading, but it declined the values instead
  of using the mechanism that already handles them for source literals, and
  the restriction then propagated backwards into eligibility (§1.3).


---

## 12. RC17 Is Wrong as a Blanket Rule

**Date:** 2026-09-12. Found while scoping RC-P6b; blocks its payload-deletion half.

RC17 says "`Type` is valueless — every constant value lives in the const pool."
That holds for a literal *expression*, whose value is a property of the node.
It does not hold for a literal **type**, where the value is the type's identity.

### 12.1 The evidence

`validate.cpp:152` matches a value against a literal type by reading the
payload off the type:

```c
if (type->is_literal && (type->type_id == LMD_TYPE_STRING || ...)) {
    TypeString* literal_type = (TypeString*)type;
    ... s2it(literal_type->string) ...
    // Literal-union members are value singletons; a primitive TypeId check
    // alone would admit every string/symbol into the union.
}
```

There is no AST node anywhere in that call: the validator walks a `Type*`
against a runtime `Item`. A per-node `const_index` cannot serve it.

`parse_type_pattern.cpp:238` builds a value-bearing `TypeFloat` inside the
*type* grammar, and its neighbouring comment records that a shared valueless
singleton was already rejected for exactly this reason — "NOT `&LIT_INT`: that
shared type makes the emitter re-parse the value".

So `"a" | "b"` is literal `TypeString`s carrying their strings, and a pattern
`[3.14]` is a literal `TypeFloat` carrying 3.14. Delete the payloads and literal
unions, type patterns and `type T = 3.14` all lose their meaning.

### 12.2 The distinction RC17 missed

The same struct serves two roles:

| Role | Who needs the value | Correct home |
|---|---|---|
| literal in **expression** position (`let x = 3.14`) | the node | const pool, via `AstNode::const_index` |
| literal in **type** position (union member, pattern, annotation) | the type | on the `Type` — it *is* the type |

RC17 saw only the first and generalized. A defensible restatement:

> **RC17v2** — a `Type` carries a value only when that value is part of the
> type's meaning: literal types used for matching (union members, patterns,
> annotations). A literal in expression position carries no value on its type;
> its value is a pool entry named by the node's handle, and its type may be a
> shared singleton.

That keeps RC18's payoff where it applies — no per-literal `Type` allocation for
expression literals — without breaking the type system. **Ratified 2026-09-12**
as RC17v2.

### 12.3 Measurement note

The initial scope of "58 cast sites" and "23 `double_val` readers" was inflated:
`TypedItem` also has `double_val`, `real`, `imag`, so `lambda-data.cpp` and
`print.cpp` matches were counted as `TypeFloat` readers when they are unrelated.
The third such miscount in this ledger (§1.8, F3, and here), each from matching a
name without checking the struct it belongs to.


---

## 13. User-Defined Function Calls Are Out of Scope

**Date:** 2026-09-12, ruled by the user. Closes RC-P6.

RC3.4 licenses folding any call to a pure callable with const arguments.
Implementation stops at **pure sys-funcs**; a call to a user-defined `fn` is
semantically const under **D6.1.2** and is deliberately left unfolded.

The two are not the same size of problem. A sys-func call is a *closed*
evaluation: the callee is a native function, its purity is a declared fact on
`SysFuncInfo`, and `interp_eval_mode_allows_sys_func` already gates it. Nothing
about the compilation unit's state participates.

Folding a user call makes the const-folder **execute user code during
compilation**, which is a different activity:

- The fold pass runs at `depth_limit = 1` — no nested activation records at
  all. A user call needs a callee frame: bind parameters, evaluate the body,
  return.
- Recursion is then reachable. Per-node fuel bounds the work, but
  `depth_exhausted` publishes a stack-overflow diagnostic as a *runtime
  completion* — which **RC14** forbids a fold attempt from doing.
- The callee body may read module bindings, and the module slab is empty at
  compile time. That is the same problem RC3.3 solved for identifiers, but now
  inside a callee frame, and extending to closures and captures.

The value is real — user functions over constants are common, and D6.1.2 exists
for exactly this. But it is the first step that lets the folder run arbitrary
user code, so it belongs in its own phase with RC14's inertness guarantee as the
explicit design constraint, not as a tail of this one.
