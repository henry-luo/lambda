# Lambda Design: Nested Mutation — Places, Path Borrows, and the Lost-Update Rule

- **Status:** PROPOSED — rev 1, 2026-08-28. Not ratified; no `S#` minted yet.
- **Owns:** `SO14` (nested-mutation ergonomics) — the open issue that had no
  owner document. Also closes out `C4.4 #6` and
  [COW Appendix B.2](Lambda_Design_Runtime_COW.md) row 1, which
  [was promoted to blocking](Lambda_Design_Runtime_COW.md) on 2026-08-28.
- **ID series:** extends the COW area's `CW#` series (CW22–CW28); the series
  home is `Lambda_Design_Runtime_COW.md`, which carries a forward pointer.
- **Semantic authority:** `doc/Lambda_Formal_Semantics.md` §S9. This document
  may not change what a conforming program observes; it decides *how a program
  spells a deep update* and *what the compiler must say when the spelling is
  wrong*. Directly governed by **S9.1.2** (binding copies), **S9.1.3** (`var`
  is the sole sharing construct), **S9.2.2** (write-through views are borrows,
  never values; a mutable borrow un-shares first), **S9.2.3** (snapshot
  iteration), **S9.3.1** (insertion captures by value), and **S10.4.3** (no
  parent pointers, no observable identity).
- **Scope:** the surface and rules. Implementation sketch is Appendix A;
  worked migrations are Appendix B. It does *not* re-open exclusivity
  (COW §11.3), which already reserves the path-prefix face this design needs.

---

## 1. Why this is now blocking

S9.3.1 (insertion captures by value) is implemented on both tiers behind
`LAMBDA_COW_CAPTURE`, default off — see
[LR12-9](Lambda_Issue_Ledger.md#lr12-9). It cannot become the default, and the
reason is not performance and not missing syntax. It is this:

> **Insertion capture and borrowing reads are individually sound and jointly
> lossy.** Once insertion captures, a slot holds a value with a second
> observer. Element and field *reads* still hand back that value unchanged
> (the open half of C4.1). So `c = owner[i]` … `c[j] = v` detaches `c` on the
> first write and the update never reaches `owner`. Nothing reports it.

Measured cost of flipping the flag today: exactly four corpus scripts —
`test/lambda/proc/proc_fill_gc_nested.ls` and
`test/benchmark/awfy/{cd2_orig,deltablue,deltablue2}.ls` — all of them that one
idiom. They do not crash or error; they compute wrong answers.

This is the shape C4.4 #6 predicted in the abstract ("deep in-place update must
not degrade into *read a copy, mutate it, discard it*"). What was not predicted
is that the degradation is **silent**, and that silence — not verbosity — is
what makes the flip unsafe.

### 1.1 What already works, measured 2026-08-28

Probes under `temp/cow_probe/`, both tiers, both flag states:

| Shape | Status |
|---|---|
| Path write `a.b[i].c = v` | **Works**, O(depth) spine, no subtree copy. `cow_path_set` detaches each link and installs replacements (COW Stage 1, C0/C3). |
| Path borrow `f(var a.b[i])` | **Parses and resolves** — `compound_root_ident` already walks a path to its root. But it *aliases*: `pathborrow5.ls` shows a write through `m.rows` reaching the original `rows` binding, with capture **on**. |
| Typed path borrow `f(var m.rows)` where `rows: any[]` | **Rejected** — `error[E207]`, because a member read's static type comes back `any` rather than the field's declared type. A type-propagation gap, not a design one. |
| `with` as a keyword | **Free** in both the reference grammar and the C parser. |
| An explicit `copy(x)` builtin | **Does not exist** (only `io.copy` for files). |

Two conclusions. First, the *hard* part — O(depth) spine detachment with
writeback — is already built and shipping; this design mostly decides who may
call it and how it is spelled. Second, the path-borrow alias above is a
standing violation of **S9.2.2** ("creating a mutable borrow over shared
storage un-shares first"): the caller detaches the *root binding* only, never
the projected place. That ruling is already ratified — the borrow half of this
design is conformance work, not new semantics.

---

## 2. The concept: a place is a borrow, never a value

**CW22 — Places.** A *place* is a location named by a root binding plus a
static path: `m`, `m.rows`, `m.rows[i]`, `t.nodes[i].value`. A place is **not a
value**. It cannot be bound by `let`, returned, stored into a container,
captured by a closure, or compared. It is legal in exactly three positions:

1. the target of a path write (§3.1),
2. a `var` argument (§3.2),
3. the head of a `with var` block (§3.3).

This is not a new concept — it is **S9.2.2 generalized from slices to paths**.
That ruling already says write-through views are "borrows, never values —
legal only in `var`-param position, exclusivity-checked, non-escaping", and
that creating one over shared storage "un-shares first". A mutable view over
`a[2..5]` and a mutable borrow of `a.b[i]` are the same object under different
spellings; giving them one rule is the point.

It also keeps **S10.4.3**: places carry no parent pointers and add no
observable identity. A place exists during a call or a block and is gone
after; it is compiler bookkeeping, exactly as S10.4.3 permits lineage to be
"a navigation path, cursor, or zipper".

**CW27 — No place values, no place arithmetic.** There is no way to obtain a
place as a first-class thing, name its root, compare two places, or build one
dynamically. If a program needs runtime-selected indirection, it needs the
handle store (C4.2e), which is data, not a place. This is what keeps S9.1.5
(structural `==` is the only equality) and the cycle ban intact: a first-class
place is a reference, and a reference reintroduces every property C4 bought.

---

## 3. The three spellings

### 3.1 N1 — the path write (primary, already correct)

```lambda
m.rows[i][j] = v
```

**CW28 — the path write is the primary spelling and needs no new surface.** It
already detaches only the spine (O(depth)), never a subtree, and installs each
replacement into its parent. Every other form in this design is an
optimization or an ergonomic shorthand for it, and all of them lower through
the same descend/detach/install machinery (Appendix A).

*Asymptotics vs. constants:* the O(depth) claim is about copies. The emitted
steady-state descend has a poor constant factor today — including a heap
allocation per member key per write — measured and itemized in Appendix A.4.
None of it changes this ruling; all of it is mechanical tuning of the one
shared lowering, and it is what makes N3, not N1, the answer for hot loops
until A.4 items land.

Teaching consequence: the answer to "how do I update a nested value" is
"write the whole path". The other two forms exist for the cases where that
answer is insufficient — passing a deep location to a procedure (N2), and
repeating writes to one deep location (N3).

### 3.2 N2 — the path borrow

```lambda
pn bump(var r: int[]) { r[0] = r[0] + 1 }

bump(var m.rows[i])
```

**CW25 — a path borrow detaches the spine on the way in and writes the leaf
back on the way out.** Precisely:

1. Evaluate the prefix, applying `cow_prepare_write` at each link and
   installing replacements — the descend half of `cow_path_set`. After this
   the whole spine down to the place is unshared and owned by the root.
2. Bind the leaf container as the callee's `var` parameter.
3. On return, install the (possibly replaced) leaf back into its parent slot.

Step 3 is required because the callee's own writes may replace the leaf — a
typed store can rebuild an array's representation, and a shared leaf detaches.
The current implementation writes back only to the *root binding*, which is
exactly why the projection case aliases instead of borrowing (§1.1).

**The write-back is a raw store, not a capturing one.** Installing the leaf
back into its parent looks like an insertion, but S9.3.1 must not re-fire: the
value returning home is the same logical value, not a second observer. Marking
it would make the next write to that slot detach a copy for no reason. This is
the same trap already recorded for `cow_unmark_shape_children` in
`lambda_map_set_checked_impl`, and it has the same answer — the writeback path
uses `cow_path_set_raw`.

Exclusivity needs nothing new: COW §11.3 face 3 already reserves the
**path-prefix** relation (`x` conflicts with `x.f`; `x.a` and `x.b` do not).
Until Stage 2 lands that check, N2 carries the same unchecked-overlap caveat
as every other borrow (COW §10, Stage 1 risk 5).

**Prerequisite:** the `E207` type gap in §1.1. A `var` parameter demands an
exact type match, and a member read currently types as `any`, so every
*annotated* path borrow is rejected today. That is the
`get_effective_type`-vs-`node->type` carrier-read problem tracked as TIG1; N2
is blocked behind it, not behind semantics.

### 3.3 N3 — `with var`, the scoped place borrow

```lambda
with var row = m.rows[i] {
    for y in 0 to n { row[y] = f(y) }
}
```

**CW26 — `with var p = <place> { … }` borrows the place for the block.** The
spine is detached once at block entry and the leaf written back once at block
exit — including on early `return`, `raise`, and loop exit. Inside the block
`p` is a borrow with all of CW22's restrictions: it cannot escape, be returned,
be stored, or be captured.

Why a block rather than a bare binding: **a block is a borrow scope in exactly
the way a call is**, so COW §11.2's structural argument — "borrows live only
for the duration of a call, and the runtime is single-threaded, therefore a
second live writer has exactly two possible sources" — carries over verbatim
with "call" widened to "call or `with` block". No second lifetime notion is
introduced, and the exclusivity design does not have to be re-derived.

Why the spelling: `var` already means *borrow* in parameter position, so
`with var` composes two meanings the language has rather than inventing a
third; `with` marks the scope that bounds it; and both are words, per S10.3.1's
preference for keyword operators over sigils. `with` is unused today in both
front ends.

Block-exit writeback is the same hook the resource model needs (S12.4, R1–R5
scoped `open()`/auto-close). They should share one scope-guard mechanism rather
than each growing its own.

---

## 4. The read-side ruling — the part that actually unblocks the flip

### 4.1 The binding stays a copy

**CW23 — `var b = <place>` copies. There is no implicit borrow.** S9.1.2 is
unchanged and S9.1.3 keeps `var` (parameter, and now `with var`) as the sole
sharing construct.

The tempting alternative is to make `var row = m.rows[i]` *mean* a borrow,
which would make every one of the four blocked scripts correct with no edit.
It is rejected, for three reasons:

1. It would make `var h = g` and `var h = g.f` mean different things — copy
   versus borrow — distinguished only by whether the initializer happens to be
   a projection. The canonical C4.1 fixture (`let g = [4,5,6]; var h = g;
   h[0] = 77` must leave `g` alone) sits one character away from the borrow
   case.
2. It reintroduces unbounded aliasing: the binding's lifetime is the enclosing
   scope, not a call, so an implicitly borrowed name could be returned or
   stored, and CW22's non-escaping property would need a whole-scope escape
   analysis to recover.
3. It is the reference-semantics answer wearing value-semantics clothes. The
   model's position (C4.2e) is that when two places must see one mutable
   thing, the thing gets an owner and everyone else holds a key. Making the
   common projection implicitly aliasing contradicts that at the exact point
   where the teaching matters most.

### 4.2 The diagnostic is the deliverable

**CW24 — a lost-update through a place copy is a compile error.** When a
binding is initialized from a place expression rooted at a mutable binding, and
that binding is later the root of a mutation — an index or member assignment, a
`var` argument, or a `pn` method receiver — the compiler must reject it:

```
error[E2xx]: writes through `row` do not reach `m.rows[i]`
  |
  |     var row = m.rows[i]
  |               --------- `row` is a copy taken here (S9.1.2)
  |     row[y] = v
  |     ^^^^^^^^^^ this updates the copy; `m` is unchanged
  |
help: write the path directly           m.rows[i][y] = v
help: or borrow the place for a block   with var row = m.rows[i] { ... }
```

This is the ruling that matters, and it is worth being explicit about why:

> **The S9.3.1 flip is gated on CW24 alone, not on CW25 or CW26.**

The four blocked scripts are dangerous because they silently compute wrong
answers. A compile error converts them into four mechanical, located fixes. The
ergonomic forms remove a verbosity tax; the diagnostic removes a correctness
trap. Only the second one blocks the flip. This decouples a small, tractable
piece of work from a much larger one, and it is the main scheduling result of
this design (see §6).

The shape is statically decidable with information both tiers already build:
`compound_root_ident` gives the place's root, and the existing mutation-target
walkers (`mir_nested_control_writes_name`, and the `INDEX_ASSIGN_STAM` /
`CALL_EXPR` arms of `has_elem_type_invalidation`) already find the mutation
sites. No new analysis framework is required — see Appendix A.3.

**Error, not warning.** A warning preserves the silent-wrong-answer failure
mode for anyone not reading warnings, which is the entire problem. The cost of
strictness is a program that genuinely wants a mutable local snapshot, which
needs an opt-out — see NM-O2.

---

## 5. Alternatives considered

| Option | Verdict |
|---|---|
| **Implicit borrow on projection bindings** (§4.1) | **Rejected** — makes `var h = g` and `var h = g.f` differ; unbounded borrow lifetime; contradicts C4.2e teaching. |
| **`_modify` coroutines** (Swift) / **subscript projections** (Hylo) | **Not now.** They are the general mechanism behind N2/N3 and would let *user-defined* accessors yield places. Lambda has no user-defined subscripts, so the generality buys nothing yet. Revisit if user-defined containers land. |
| **Guaranteed get-modify-put via uniqueness** (C4.4 #6 candidate 3) | **Subsumed.** This is what N1 already does under COW: the first write detaches, subsequent writes find a unique spine and mutate in place. It is a performance property, not a surface. |
| **Auto-hoisting N1 out of loops** | **Deferred, and deliberately not a semantic guarantee.** An optimizer may turn a loop of path writes into one descend plus N in-place stores when it can prove the prefix invariant; N3 is how a *program* states that intent without depending on an optimizer. |
| **A first-class place/reference value** | **Rejected** — CW27. It is a reference; it costs the cycle ban, total `==`, and printability (C4.2e point 2). |
| **Leave reads aliasing and never flip S9.3.1** | **Rejected** — leaves the model permanently half-aliasing, which LR12-9 already argues is harder to reason about than either endpoint. |

---

## 6. Sequencing

The design splits into three independently shippable pieces, in this order:

1. **CW24, the diagnostic.** Small, self-contained, no new syntax, no runtime
   change. **Ships with — and gates — the `LAMBDA_COW_CAPTURE` default flip.**
   The four blocked scripts become compile errors, each with a mechanical fix
   (Appendix B). This is the whole of the flip's safety story.
2. **CW25, path borrows.** Conformance work against the already-ratified
   S9.2.2 ("un-shares first"), plus the `E207`/TIG1 type-propagation fix that
   currently rejects every annotated path borrow. Removes the "extract a
   procedure and pass the root" workaround.
3. **CW26, `with var`.** New syntax in both front ends plus a scope guard;
   the largest piece. Best done alongside the S12.4 resource-scope work, which
   needs the same block-exit hook. Its urgency is set by Appendix A.4: until
   the steady-state descend is tuned, N1 in a hot loop allocates per write, so
   CW26 is also the *performance* spelling, not only the ergonomic one.

Independent of all three: the A.4 tuning items (interned path keys,
skip-unchanged reinstall) are small, semantics-free, and worth doing first —
they cheapen every existing nested write in the corpus, not just the new
forms.

Piece 1 does not depend on 2 or 3. That is the point of §4.2.

---

## 7. Open issues

- **NM-O1 — Element huge fan-out.** A one-level copy of a 10⁵-child element is
  O(width) regardless of how the update is spelled. Unchanged from COW §9.5.1
  residue / Appendix B.2 row 2; the chunked-children question is still gated on
  the editor benchmark, and this design neither helps nor worsens it.
- **NM-O2 — The intentional-snapshot opt-out.** CW24 rejects a mutated place
  copy, but a program may genuinely want a private mutable snapshot. There is
  no `copy(x)` builtin today (only `io.copy`). Options: mint one; accept `let`
  (immutable, so it does not answer the mutable-snapshot case); or an explicit
  `var row = m.rows[i] as copy` form. Needs a decision before CW24 ships.
- **NM-O3 — Should N2 ship before Stage-2 exclusivity?** A path borrow with no
  overlap check has the same unchecked writer-vs-writer residue as every
  Stage-1 borrow, but it is easier to trip (`f(var t, var t.nodes[i])` looks
  innocuous). Options: ship with the residue, or gate N2 on COW §11.3 face 3.
- **NM-O4 — Interaction with S9.2.3 snapshot iteration.** `for x in m.rows[i]`
  share-marks at the loop head. Inside a `with var` block over the same place,
  the head mark and the block's detach must not fight. Believed benign
  (detach precedes the loop), but it needs a fixture.
- **NM-O5 — Where these ratify.** The formal spec's S9 currently ends at S9.3;
  the legacy "§9.5.2" references in the COW doc point at a section that
  distillation removed. On ratification these become **S9.4** (nested
  mutation), and `SO14` is struck.

---

## Appendix A — Implementation sketch

### A.1 One lowering, three entry points

`cow_path_set(owner, path, value)` already contains the whole mechanism. Factor
it into three reusable steps:

- `descend_detach(root, path) -> leaf` — walk the prefix, `cow_prepare_write`
  each link, install each replacement into its parent. Used by all three forms.
- `install(parent, key, leaf)` — raw store, no capture (CW25).
- `write(leaf, key, value)` — the existing final store, capturing per S9.3.1.

N1 = descend_detach + write. N2 = descend_detach, bind, then install on return.
N3 = descend_detach at block entry, install at block exit.

### A.2 Where the borrow writeback goes

Today the callee-side `var` writeback publishes to the caller's *root binding*
(`interp_write_binding` in T0; the borrowed-root path in MIR). For a path
borrow it must publish to the *parent slot of the place*. The caller already
computes that parent during `descend_detach`; it needs to be retained across
the call in the same register/scratch discipline the existing borrowed-root
scratch uses.

### A.3 The CW24 check — keep it linear

Implement as one pass per function body: collect the mutation-root name set
(index/member assigns, `var` arguments, `pn` receivers) into a small set,
then check each place-initialized binding against it — O(body + bindings),
not the O(bindings × body) a per-binding rescan would cost. The existing
walkers (`mir_nested_control_writes_name`, `has_elem_type_invalidation`) are
per-name rescans; reuse their *arms*, not their driving loop.

### A.4 Steady-state cost of the shared lowering — measured 2026-08-28

The descend/detach/install machinery is asymptotically right and
constant-factor wrong. Facts, from the current MIR emission
(`mir_emit_cow_path_set`, transpile-mir.cpp) and runtime:

1. **The slow path is sticky, and parameters start on it.**
   `MirVarEntry::cow_children_may_be_shared` is set in 14 places and cleared
   in none, and every `var`/`pn` parameter is born with it set. So *every*
   nested write through a parameter — the dominant shape in the graph
   benchmarks — permanently lowers through the dynamic path helper instead of
   the T20-1d shaped guarded store. The flag is honest (the runtime child
   marks it mirrors are themselves monotonic under CW3's 1-bit), so this
   cannot be fixed by clearing it; it is fixed by making the path helper
   cheap (items 2–4) or by a per-path written-once witness (NM-O6).
2. **A heap allocation per member key per write.** `mir_emit_cow_path_key`
   calls `heap_create_symbol` for each member segment on every execution of
   the write — and `heap_create_symbol` does not intern (lambda-mem.cpp: a
   fresh GC `Symbol` per call). Meanwhile the plain member-store fallback
   already shows the zero-cost spelling: box the *interned name-pool string*
   as an `LMD_TYPE_STRING` immediate (transpile-mir.cpp `store_key`,
   "identifier keys already live in the script name pool").
   `runtime_named_map_field` accepts STRING and SYMBOL keys alike, so this is
   a drop-in substitution: **no allocation, no call, one MOV**.
3. **Unconditional reinstall per link.** The descend calls
   `cow_path_set_raw(parent, key, child)` even when `cow_prepare_write`
   returned the child unchanged — in the steady (already-unique) state that
   is a dynamic shape-lookup store per link per write, all waste. One pointer
   compare (`detached == child`) skips it. The same compare belongs in the
   CW25/CW26 leaf writeback.
4. **Dynamic child lookup per link.** `fn_index` resolves each segment by
   name/index at runtime. For a root under a fixed shape contract the offsets
   are known at compile time; a shaped descend (byte-offset loads, the same
   trick as the shaped literal fast path) removes the lookup. This is the
   largest remaining item and the only one needing new emission logic.

Ladder order: 2 and 3 are afternoon-sized and semantics-free; 4 is real work;
NM-O6 is a design question. With 2+3 done, the steady-state N1 write is
per-link {flag check, fn_index, raw store} with zero allocation — and N3
still beats it in loops by hoisting even that.

*Why this doesn't reopen §5's rejected alternatives:* implicit borrowing
would be faster than untuned N1, but N3 reaches the same steady state —
descend once, raw in-place writes thereafter — without the semantic damage,
and tuned N1 closes most of the rest. Nothing on the table is asymptotically
better than what CW25/CW26 already specify; first-write detach cost O(width)
per level is Stage-1 COW itself (NM-O1), not this design.

Both tiers already have the pieces. Per function body: for each binding whose
initializer's root is a place (`compound_root_ident` returns non-null and the
initializer is a member/index expression), record it; then scan the body for a
mutation whose root is that binding — `AST_NODE_INDEX_ASSIGN_STAM`,
`AST_NODE_MEMBER_ASSIGN_STAM`, a `var` argument position, or a `pn` method
receiver. The mutation-site walkers exist (`mir_nested_control_writes_name`
plus the `INDEX_ASSIGN_STAM`/`CALL_EXPR` arms of
`has_elem_type_invalidation`); they are name-keyed over an AST subtree, which
is the shape this check needs. The check belongs in `build_ast` so one
implementation serves both tiers, alongside the existing `E211` var-argument
overlap check.

---

## Appendix B — The four blocked scripts, and their fixes

Each is the same idiom and each has a mechanical fix. These become the CW24
diagnostic's own fixtures.

| Script | Shape | Fix |
|---|---|---|
| `proc/proc_fill_gc_nested.ls` | `c1 = null16(); l0[i0] = c1; c2 = null32(); c1[i1] = c2; c2[i2] = val` | Mutate before storing (fill `c1` then insert), or write the path `l0[i0][i1][i2] = val`. The script is the *fill-after-storing* porting hazard S9.3.1 names by name. |
| `awfy/deltablue.ls`, `deltablue2.ls` | Constraint/variable records read out of a vector, mutated locally | Handle store (C4.2e), as `richards3.ls` does — or path writes through the owning vector. |
| `awfy/cd2_orig.ls` | Voxel vectors read, appended, discarded | Read-modify-write; note `cd2_orig` is a **perf control** for the `cd2` comparison, so whether it is migrated or retired is a benchmarking decision, not a correctness one. |

`awfy/richards3.ls` is the already-landed proof that the target idiom works: it
passes with `LAMBDA_COW_CAPTURE=1` today.

---

*Cross-refs:* series home and Stage-1/Stage-2 structure
[`Lambda_Design_Runtime_COW.md`](Lambda_Design_Runtime_COW.md) (CW1–CW21,
§11.2/§11.3 exclusivity, Appendix B.2); semantics record
[`Lambda_Semantics_Formal.md`](Lambda_Semantics_Formal.md) (C4.1 bug catalog,
C4.2e handle store, C4.4 #6); status and evidence
[LR12-9](Lambda_Issue_Ledger.md#lr12-9); spec
[`doc/Lambda_Formal_Semantics.md`](../doc/Lambda_Formal_Semantics.md)
(S9.1.2, S9.1.3, S9.2.2, S9.2.3, S9.3.1, S10.4.3, SO14).
