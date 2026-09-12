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
| **RC5** | **Two storage cases, by representation** (§5). Self-contained immediates (NULL, BOOL, INT, and inline-representable FLOAT) live inline in the node's literal `Type`. Everything else is rehomed into the const pool and reached by `TypeConst::const_index`. |
| **RC6** | **The const pool is content-hashed and shared.** Equal constant values occupy one pool slot, whatever their origin — source literal or folded result. Identity of a pooled entry is its content hash, not its construction site. |
| **RC7** | **A pooled fold result is rehomed before its frame closes** (§4). The fold's throwaway frame owns nothing that outlives it; a value destined for the pool is copied into script-pool storage during the attempt, or the fold is abandoned. |
| **RC8** | **MIR lowering handles both cases and must consult the fact on every path** (§6.2), not only at boxing boundaries. An immediate is baked; a pooled value loads through `const_index`. |
| **RC9** | **The interpreter consumes the fact directly** (§6.1). A folded node evaluates to its stored value without re-entering evaluation. |
| **RC10** | **Folded type and inferred type must agree; disagreement is a defect and is reported** (§7). The producer asserts it once. The consumer does not re-derive it. |
| **RC11** | **Each node folds exactly once.** The index carries a watermark; the pass resumes there and needs no wholesale reset. Stated per *node*, not per *unit*, because a unit can grow: the REPL appends to a retained index, so a unit-level skip would strand the appended nodes unfolded. |
| **RC12** | **No per-node fact side table.** The fold's result lives on the node's type under **D8.2.5v2**; `AstIndex::facts` and `AstNodeFacts` are retired. |
| **RC13** | **Nothing pointer-backed enters cacheable MIR as a raw address** (**DI14**). Pooled values are reached through the module-state const indirection, never by a baked pointer. This is what makes RC5's second case safe. |
| **RC14** | **Fold attempts remain semantically inert.** Fuel exhaustion, a native fault, an error result, or a rejected attempt leaves the node unfolded and changes nothing observable. A compiler optimization may never alter program completion (retained from today's design). |
| **RC15** | **Const values are pooled when they are built and never enter the folder.** A *const value* is any value the source already determines: a scalar literal, and equally a **static/const container** — an array, map or element literal whose parts are all const. Its value needs no evaluation, so it is materialized directly into the const pool (RC6-hashed) at build time. **The folder's input is expressions only.** No const value is interpreted, and none has its value recovered by re-reading a source span — which retires the text-recovery path in `ast_static_literal_item()` for the valueless `&LIT_INT`/`LIT_BOOL` singletons. |
| **RC16** | **Rehoming applies only to computed const-expression results** (§4). A const value — scalar or container — is born in pool-owned storage and needs none; only a *computed* result is born in frame/GC storage during the fold and must be moved before its frame closes. The two paths meet at the pool, never before it. This is why a folded aggregate needs no rehoming: an aggregate literal is a const value under RC15, so it never reaches the folder at all. |
| **RC17** | **`Type` is valueless.** A type describes a *set* of values; it never carries one. The inline payloads of `TypeFloat`, `TypeComplex`, `TypeInt64`, `TypeUint64`, `TypeNumSized`, `TypeDateTime`, `TypeDecimal`, `TypeString`/`TypeSymbol` and `TypeBinaryConst` are deleted, and `TypeConst::const_index` with them. Every constant value lives in the const pool. |
| **RC18** | **Three-way separation of a constant.** *Which kind* is the `Type` — now a shared valueless singleton (`LIT_INT`, `LIT_FLOAT`, `LIT_STRING`, …), one per type, never per literal. *Which value* is a per-node const handle on the `AstNode`. *The value itself* is one hashed, shared const-pool entry (RC6). No literal allocates a `Type`. The handle is on the node, not on a const-specific type, because **a folded expression is an arbitrary node kind** — a `BINARY`, a `CALL`, an `IF_EXPR` — whose type stays its own inferred type. Having a constant value is a per-node fact; it is not a type. |

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
4. it is a call to a pure `fn` (D6.1.1: every callable surface carries the
   bit — sys-func registry rows, Jube module signatures, the runtime-function
   catalog) whose arguments are all const; or
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

## 5. Storage (RC5)

Two origins reach this storage, and only one of them passes through the folder
(RC15/RC16): a **literal**, pooled when it is built, and a **folded expression
result**, pooled after evaluation and rehoming. Both end in the same place, so
a consumer never asks which produced a value.

Today an in-band int literal is typed `&LIT_INT` — a process-global singleton
with no payload ([`lambda-data.hpp:1141`]) — and its value is recovered by
**re-reading the source span** ([`build_ast.cpp:1003`]). RC15 retires that: a
literal carries a value-bearing type from the moment it is built. The span
re-read cannot work for a fold in any case — `1 + 2` has no span that reads
as `3`.

So a folded node always receives a **per-node pooled literal `Type`**, never a
shared singleton. This is also what **D8.2.5v2** requires: a fact attached to
a shared `TYPE_ANY`-family singleton would be read by every unrelated node
that shares it.

**RC-O1 is resolved by RC17/RC18: storage is uniform.** Every constant value —
literal or folded, immediate or not — lives in one place, the const pool. The
representation split survives only at *emission* (§6.2), not in storage.

| Concern | Home | Cardinality |
|---|---|---|
| which kind of value | `Type` — valueless shared singleton (`LIT_INT`, `LIT_FLOAT`, …) | one per type |
| which value | const handle on the `AstNode` | one per node |
| the value itself | const-pool entry, content-hashed | one per distinct value |

The question that made RC-O1 look hard — "there is no `TypeInt`, `TypeBool` or
`TypeNull`, so what carries a folded immediate?" — dissolves. Nothing carries
it in the type, because no type carries a value. The three valueless singletons
that already exist are the *model*, not the exception.

`TypeConst` disappears entirely: with no payload and no `const_index`, nothing
distinguishes it from `Type`. A folded node keeps the type it already had.

**The handle is free.** `AstNode` is `{AstNodeType node_type; Type* type;
AstNode* next; SourceSpan source_span;}` — a `uint16_t` followed by six bytes
of padding before the first pointer, for 32 bytes total. A `uint32_t` const
handle occupies that padding: `sizeof(AstNode)` is unchanged, every subclass
layout is unchanged, and there are no `offsetof(AstNode, …)` pins in the tree
to break. A reserved sentinel (index 0, or `UINT32_MAX`) means "no constant".

**This also retires the singleton-aliasing hazard** that **D8.2.5v2** warns
about. That hazard exists only if a per-node fact is attached to a possibly
shared `Type`. With the handle on the node, the situation cannot arise, and
sharing types becomes safe rather than dangerous — no literal allocates a
`Type` at all, where today every float, string, datetime and decimal literal
allocates its own.

`TypeNumSized::num_type` is the one payload that is partly *type* information
rather than value. It collapses cleanly: `type_num_sized_kind()` already falls
back to `Type::kind` for non-literals, so `kind` becomes the sole authority.

---

## 6. Consumers

### 6.1 Interpreter (RC9)

Evaluation of a node carrying a fold fact returns the stored value directly:
inline payload for an immediate, `pool[const_index]` for a pooled value. No
re-entry into evaluation, no recomputation per execution.

This is the largest behavioural change in this design. It converts const
folding from a MIR-only optimization into a property of the compilation unit
that both tiers observe — **RC1**.

### 6.2 MIR (RC8)

Two cases, per the user-specified split:

- **Immediate** — bake the tagged word as a literal operand, exactly as today
  (`MIR_new_uint_op`). One instruction, self-contained, cache-safe.
- **Pooled** — `emit_load_const(const_index)`: load module state, load the
  `consts` pointer from it, load the value. Three loads, and its comment
  explains why the pointer is never baked — "the module's const image may
  rebind after interning" ([`transpile-mir.cpp:6037`]). That indirection is
  what satisfies **DI14**/**RC13**.

The fact must be consulted wherever a node is lowered, not only at the two
boxing boundaries. `let a = 1 + 2` folding to an immediate in the native int
lane is the acceptance case for RC8.

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
| **RC-P0** | Fix the §1.3 contradiction: reject float in eligibility *or* accept inline floats in the result test. Whichever, stop evaluating what is always discarded. | no MIR-volume growth; baseline green |
| **RC-P1** | RC10 — assert agreement, delete the duplicated silent skip. Any assertion that fires is a real defect to fix before proceeding. | baseline green with assertions armed |
| **RC-P2** | RC11 — resume the fold at a per-index watermark; delete the wholesale reset. **Not** by preseeding `ANALYZED`: the REPL *appends* to a retained index, so skipping the pass would leave every newly typed expression unfolded. A watermark gives RC11's actual guarantee — each node folds exactly once — while staying correct under growth. | REPL and retained-AST paths green |
| ~~**RC-P2b**~~ | **WITHDRAWN 2026-09-12 — folded into RC-P6b.** Its first half (remove literals from the folder) was already true, per the §1.8 correction. Its second half (retire the `&LIT_INT` span re-read) is real but is a *build-time* change to literal typing, and doing it now with per-literal value-bearing `Type`s would build exactly what RC17/RC18 then delete. The span re-read retires when literal values reach the pool — in RC-P6b. |
| **RC-P3** | RC8 — consult the fact on all lowering paths, not just boxing. `let a = 1 + 2` folds. | MIR volume drops; emission goldens re-based with attribution |
| **RC-P4** | RC9 — interpreter consumes fold facts. | T0 no longer re-evaluates constants |
| **RC-P5a** | RC15 — **const containers are materialized, not folded.** A literal ARRAY/MAP whose parts are all const becomes one pooled static container recorded as a const fact, built directly from the AST with no evaluation. `emit_static_collection_const()` already does this construction inside the MIR back end (§9.1); it becomes a consumer of the shared fact instead of a private builder, so T0 stops rebuilding the same array on every execution. | both tiers read one container; no aggregate reaches `interp_const_fold_script` |
| **RC-P5** | RC5/RC7/RC13 — pooled results with rehoming. **Blocked on RC-P6 as written (2026-09-12): there is nothing to rehome.** Eligibility admits only NULL/BOOL/INT/FLOAT literal operands, and no operator over those yields a pointer-backed value — `2 ** 100` and `9007199254740991 + 1` give floats (`1.27e30`, `inf`), `"ab" + "c"` is an error since `+` does not concatenate, and string comparison gives a bool. Every producible result is self-contained. The one reachable exception is a subnormal float, which is boxed. Rehoming needs eligibility widened first — to expressions that *compute* a pointer-backed value. Aggregate literals are **not** that case: under RC15 they are const values, materialized into the pool at build time without ever entering the folder (RC-P5a). Rehoming therefore waits on RC-P6's expression widening. | forced-GC oracle (`LAMBDA_GC_FORCE_EVERY=1`, `POISON_FREED=1`) green |
| **RC-P6** | RC2/RC3 — eligibility by interpreter capability under the purity gate. | baseline green; fold-rate census |
| **RC-P6b** | RC17/RC18 — delete the inline `Type` payloads and `TypeConst::const_index`; move the handle to `AstNode`'s padding; share literal types as singletons. 58 cast sites. | `sizeof(AstNode)` unchanged; no per-literal `Type` allocation; baseline green |
| **RC-P7** | RC6 — content-hashed pool with dedup. | const-pool size census |
| **RC-P8** | RC12 — retire `AstIndex::facts` / `AstNodeFacts`. | LOC strongly negative |

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
