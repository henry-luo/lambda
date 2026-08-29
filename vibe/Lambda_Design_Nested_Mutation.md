# Lambda Design: Nested Mutation — Places, Path Borrows, and the Lost-Update Rule

- **Status:** PROPOSED — rev 6, 2026-08-29. **CW24v2 ratified by designer:
  the Swift/R endpoint — CW24's error is a MIGRATION guard, retired for the
  general case once the flip migration completes; the surviving diagnostic is
  the dead-store shape only (§4.3). NM-O2 is CLOSED by the same ruling; no
  `copy()` builtin is minted.** Precedent survey in Appendix C.
- **Status history:** rev 5, 2026-08-28. CW24 and CW25 are implemented
  (worktree, flag-gated) and CW25's `E207` prerequisite is closed.
- **Status history:** rev 4, 2026-08-28. CW24 and CW25 are implemented
  (worktree, flag-gated); rev 4 records that CW25's post-call writeback step
  proved unnecessary, and that CW25 does unblock the view family.
- **Status history:** rev 3, 2026-08-28. Rev 3 records what building
  CW24 corrected: write-back deferral, and a nine- rather than four-script
  migration (§6.1).
- **Status history:** rev 2, 2026-08-28. Not ratified; no `S#` minted yet.
  Rev 2 is the simplicity pass: CW27 folded into CW22 as a corollary, and
  **CW26 (`with var`) demoted from a committed ruling to a deferred candidate**
  — the committed surface is now one diagnostic (CW24) plus conformance to
  already-ratified rules (CW25/S9.2.2), with zero new syntax.
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

Cost of flipping the flag, measured by *wrong output*: four corpus scripts —
`test/lambda/proc/proc_fill_gc_nested.ls` and
`test/benchmark/awfy/{cd2_orig,deltablue,deltablue2}.ls` — all of them that one
idiom. They do not crash or error; they compute wrong answers.

**Corrected 2026-08-28 by implementing CW24 (rev 3): the real migration is
nine scripts, not four.** The four above are where the idiom *already* breaks;
CW24 additionally rejects five that still work today —
`proc_markup_mutation`, `proc_param_typed_container_write`,
`proc_view_mutable`, `typed_map_write_child_ownership`, and `r7rs/mbrot2`.
They survive because insertion capture only marks *named* inserted values, so
a container filled with fresh values (`matrix[i] = fill(n, 0)`) still hands
back a borrowable child. They are not false positives: each binds a place and
mutates it, which S9.1.2 forbids outright, and they will break the moment
reads stop borrowing. But it means CW24 is not a four-script migration, and
the flip is a **nine**-script decision. See §6.1.

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
captured by a closure, or compared. It is legal in exactly two positions:

1. the target of a path write (§3.1),
2. a `var` argument (§3.2).

*Corollary (was CW27 in rev 1):* there is no way to obtain a place as a
first-class thing, name its root, compare two places, or build one
dynamically. A first-class place is a reference, and a reference
reintroduces every property C4 bought (total `==`, the cycle ban,
printability). Runtime-selected indirection is the handle store (C4.2e) —
data, not a place.

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
shared lowering, and it is what makes N2 — the descend-once borrow — the
answer for hot loops until A.4 items land.

Teaching consequence: the answer to "how do I update a nested value" is
"write the whole path". N2 exists for the one case where that answer is
insufficient — handing a deep location to code that will write it repeatedly
(a procedure today; a `with var` block if NM-O7 ever adopts one).

### 3.2 N2 — the path borrow

```lambda
pn bump(var r: int[]) { r[0] = r[0] + 1 }

bump(var m.rows[i])
```

**CW25 — a path borrow detaches the spine on the way in.** Precisely:

1. Evaluate the prefix, applying `cow_prepare_write` at each link and
   installing replacements — the descend half of `cow_path_set`. After this
   the whole spine down to the place is unshared and owned by the root.
2. Bind the detached leaf container as the callee's `var` parameter.

**Rev 4, 2026-08-28 — implemented, and a step fell away.** Rev 1–3 specified a
third step, "install the leaf back into its parent on return", justified by the
callee possibly *replacing* the leaf. Building it showed the step is
unnecessary: both tiers already run the borrow protocol as *detach before the
call, then let the callee mutate in place*, and `var` parameters are exactly
the case that uses the **in-place** checked setters (`is_var_param` selects
`lambda_array_set_checked_inplace`, so a typed store never needs a replacement
channel). Once the spine is detached, the leaf is unique and already installed
in its parent, so the callee's writes land in the caller's container directly.
A path borrow is therefore the ordinary root borrow one level deeper, not a new
protocol — which is why it needed one new runtime helper and two call-site
hooks rather than a writeback channel in each tier.

The old implementation detached only the *root binding*, which is exactly why
the projection case aliased instead of borrowing (§1.1).

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

**The `E207` type gap (§1.1) is closed — rev 5, 2026-08-28.** Annotated path
borrows (`pn f(var r: any[])` called as `f(m.rows)`) now compile and borrow
correctly on both tiers.

The fix is a *resolution*, not a relaxation, and that distinction is the whole
point. The exact-match rule for `var` arguments exists because a callee writes
through the borrow and must not see a mismatched representation; weakening it
for places would have been unsound. What was actually wrong is that a place's
own node type is `any` — a member/index read does not propagate its field's
declared type onto the expression (TIG1) — so the check was comparing against a
type nobody had computed. It now resolves the declared type *through the path*
before reporting, reusing `declared_compound_destination_type`, the walker the
assignment side already uses for annotated destinations. An unresolvable path,
or one whose declared type genuinely differs, still reports E207 (verified:
`var r: int[]` against a declared `any[]` field is still rejected).

Note this closes the gap for *annotated roots* only, which is what the rule
needs. The general TIG1 carrier-read propagation — making every member read
carry its field type — remains open and is unaffected. A `var` parameter demands an
exact type match, and a member read currently types as `any`, so every
*annotated* path borrow is rejected today. That is the
`get_effective_type`-vs-`node->type` carrier-read problem tracked as TIG1; N2
is blocked behind it, not behind semantics.

### 3.3 N3 — `with var`, the scoped place borrow (CW26, DEFERRED)

```lambda
with var row = m.rows[i] {
    for y in 0 to n { row[y] = f(y) }
}
```

**CW26 (deferred candidate) — `with var p = <place> { … }` borrows the place
for the block**: spine detached once at entry, leaf written back once at exit
(including early `return`/`raise`/loop exit); inside, `p` carries all of
CW22's restrictions.

**Rev 2 demotes this from the committed surface.** N3 is expressible today as
an extracted procedure with a path-borrow argument:

```lambda
pn fill_row(var r: int[], n: int) { for y in 0 to n { r[y] = f(y) } }
fill_row(var m.rows[i], n)
```

— one descend at the call, raw in-place writes inside, one writeback on
return: the same steady-state cost N3 promises, with zero new syntax, no
scope-guard machinery, no early-exit writeback rules, and no new
borrow-lifetime story (a call already is one). The block form is *not* an
exact desugaring of that call — a `with` body would read and write enclosing
locals freely, where a nested `pn` snapshots captures and C4.2a bars mutating
them — and that friction (threading needed locals as parameters) is the one
real cost of doing without it.

So the block form stays recorded here with its design intact — the borrow
scope maps to COW §11.2 by widening "call" to "call or block"; the spelling
composes `var`-as-borrow with `with`-as-scope (both words, per S10.3.1;
`with` is free in both front ends); block-exit writeback would share the
S12.4 resource-scope guard — but it ships only if the extracted-`pn`
friction proves real in migrated code. Adopting it later is additive and
breaks nothing. Pick-up trigger: NM-O7.

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
help: write the path directly              m.rows[i][y] = v
help: or pass the place as a borrow        update_row(var m.rows[i], ...)
```

This is the ruling that matters, and it is worth being explicit about why:

> **The S9.3.1 flip is gated on CW24, not on CW26.**
>
> *(rev 3: "not on CW25" was too strong — the view family needs CW25 for a
> legal spelling. See §6.1.)*

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
needs an opt-out — during the migration window, the CW29 plain-`pn` idiom or
an explicit write-back (see §4.3 and the closed NM-O2).

### 4.3 CW24v2 — the error is a migration guard, not the endpoint (RATIFIED, designer, 2026-08-29)

The Appendix C survey exposed that CW24's error has **no precedent in the
COW-value-semantics family**: Swift, R, MATLAB, and PHP arrays all accept
`var row = m.rows[i]; row.x = 9` as a deliberate private snapshot, because
binding-copies-then-mutate *is* their semantics. Lambda needs the error only
because it is migrating **from aliasing** — the same spelling historically
wrote through, so at the S9.3.1 flip its meaning silently changes. The guard
protects the transition, not the steady state.

**The ruling — two phases:**

1. **Migration phase (now, through the flip and the 88-script migration):**
   CW24 as specified in §4.2 — a mutated place copy is a compile **error**,
   full stop. Escapes: write the path directly (N1), borrow the place (N2),
   route through a plain-`pn` snapshot (CW29), or write the copy back.
2. **Steady state (after the migration completes):** the general error
   **retires**. A mutated place copy that the program subsequently
   *observes* — reads, returns, stores, or writes back — is a legitimate
   deliberate snapshot, exactly as in Swift/R. What survives is the
   **dead-store shape**: a place copy that is mutated and then never
   observed at all, which is provably useless work whatever the semantics —
   warning-class, aligned with how family-1 languages lint it.

Consequences: **NM-O2 is closed** — no `copy(x)` builtin, no `as copy` form
is minted; at steady state the binding itself is the deliberate-copy
spelling (the Hylo-style explicit marker remains a possible future sugar,
recorded in Appendix C, but nothing depends on it). The existing CW24
implementation (write-back excusal at FUNCTION_END) is phase 1 as built;
phase 2 widens the excusal from "written back" to "observed" and downgrades
the residue to a warning. **Phase 2 IMPLEMENTED with the flip, 2026-08-29**:
read/target-read counters on the place-copy entry decide "observed"; the
surviving dead-store shape logs `cow-dead-snapshot`. Two refinements
surfaced during implementation and landed with it: (a) for observed copies to
be legitimate snapshots the READ must actually copy, so **place-copy binds
mark their value** (both tiers) and the first write detaches — without this,
a fresh-literal child was never capture-marked and the "snapshot" aliased;
(b) the mark is gated on the durable **is-this-copy-ever-mutated** fact from
the CW24 walk (plus a container-capable binding type) — an unmutated place
copy stays a borrow, observationally identical to a copy (P6) and free.
Marking unconditionally broke two corpus scripts: scalar place copies
(`var mi: int = stack[d]`) deoptimized to boxed ANY (triangl2), and
read-and-return helpers (`rbt_get`'s `var v = n[NV]`) share-marked every
value they handed out, detaching the callers' borrows (cd2_orig).

---

## 5. Alternatives considered

| Option | Verdict |
|---|---|
| **Implicit borrow on projection bindings** (§4.1) | **Rejected** — makes `var h = g` and `var h = g.f` differ; unbounded borrow lifetime; contradicts C4.2e teaching. |
| **`_modify` coroutines** (Swift) / **subscript projections** (Hylo) | **Not now.** They are the general mechanism behind N2/N3 and would let *user-defined* accessors yield places. Lambda has no user-defined subscripts, so the generality buys nothing yet. Revisit if user-defined containers land. |
| **Guaranteed get-modify-put via uniqueness** (C4.4 #6 candidate 3) | **Subsumed.** This is what N1 already does under COW: the first write detaches, subsequent writes find a unique spine and mutate in place. It is a performance property, not a surface. |
| **Auto-hoisting N1 out of loops** | **Deferred, and deliberately not a semantic guarantee.** An optimizer may turn a loop of path writes into one descend plus N in-place stores when it can prove the prefix invariant; N2 (and N3, if adopted) is how a *program* states that intent without depending on an optimizer. |
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
CW26 is out of the committed sequence entirely (deferred, NM-O7); if it is
ever adopted, it rides the S12.4 resource-scope guard rather than growing its
own.

### 6.1 What implementing CW24 changed about this plan (rev 3, 2026-08-28)

Two corrections came out of building it, both worth keeping:

1. **Read-modify-write-BACK must not be flagged.** `p = w.pkts[i]` … mutate `p`
   … `w.pkts[i] = p` is the sanctioned idiom (C4.2e) and is *indistinguishable
   from the bug at the mutation site* — only the later store separates them.
   A mutation-site check therefore rejects `awfy/richards3.ls`, the model's own
   worked example. CW24 must defer to the end of the enclosing function, by
   which point a write-back has been seen. Implemented that way; `richards3`
   is clean and still passes.
2. **The migration is nine scripts, not four** (§1). Five of them work today
   and are rejected pre-emptively but correctly. One of those five,
   `proc_view_mutable`, is the more interesting case: it pins `var row = m[1]`
   as a *write-through view binding*, which **S9.2.2 already forbids** (views
   are borrows, "legal only in `var`-param position"). So CW24 turns out to
   enforce part of CW16.3 view-borrow confinement — a Stage-2 item — early and
   by side effect. That is defensible but was not intended, and it means the
   view fixtures cannot be migrated until N2/CW25 gives them a legal
   `var`-position spelling. **CW25 is therefore a prerequisite of the flip
   after all, for the view family specifically** — a change from §6's claim
   that CW24 gates it alone.

Independent of both: the A.4 tuning items (interned path keys,
skip-unchanged reinstall) are small, semantics-free, and worth doing first —
they cheapen every existing nested write in the corpus, not just the new
forms. Until they land, the hot-loop spelling is N2 (descend once per call,
raw writes inside).

Piece 1 does not depend on 2 or 3. That is the point of §4.2.

---

## 7. Open issues

- **NM-O1 — Element huge fan-out.** A one-level copy of a 10⁵-child element is
  O(width) regardless of how the update is spelled. Unchanged from COW §9.5.1
  residue / Appendix B.2 row 2; the chunked-children question is still gated on
  the editor benchmark, and this design neither helps nor worsens it.
- **NM-O2 — CLOSED 2026-08-29 by CW24v2 (§4.3).** The intentional-snapshot
  opt-out question is dissolved rather than answered: at steady state the
  binding itself is the deliberate copy (Swift/R model), so no `copy(x)`
  builtin or `as copy` form is minted. During the migration window the
  escapes are the CW29 plain-`pn` idiom
  (`pn tweak(row: Row) Row { row.x = 1; row }`) or an explicit write-back.
  Precedent survey: Appendix C.
- **NM-O3 — Should N2 ship before Stage-2 exclusivity?** A path borrow with no
  overlap check has the same unchecked writer-vs-writer residue as every
  Stage-1 borrow, but it is easier to trip (`f(var t, var t.nodes[i])` looks
  innocuous). Options: ship with the residue, or gate N2 on COW §11.3 face 3.
- **NM-O4 — Interaction with S9.2.3 snapshot iteration.** `for x in m.rows[i]`
  share-marks at the loop head. Inside a borrow over the same place (an N2
  callee body, or a `with var` block if NM-O7 adopts one), the head mark and
  the borrow's detach must not fight. Believed benign (detach precedes the
  loop), but it needs a fixture. Note CW30 (COW §11.6, 2026-08-29): the head
  mark is now compile-gated — emitted only when the loop body may write the
  collection's root — so the non-mutating case never raises this interaction.
- **NM-O5 — Where these ratify.** The formal spec's S9 currently ends at S9.3;
  the legacy "§9.5.2" references in the COW doc point at a section that
  distillation removed. On ratification these become **S9.4** (nested
  mutation), and `SO14` is struck.
- **NM-O6 — A written-once spine witness.** Appendix A.4 item 1: the
  compile-time children-shared flag is monotonic, so the shaped fast path
  never returns after a root's first COW event. A per-path emission-order
  witness — "this exact static path was descended-and-detached earlier on
  every path reaching here, with no intervening capture, call, or loop-back"
  — would let later N1 writes to the same path lower as raw shaped stores.
  It is the analysis §5's auto-hoist row deferred, in a narrower form.
  Decide only with profile evidence *after* the A.4 item-2/3 cheap fixes:
  they may make the witness unnecessary.
- **NM-O7 — Adopting `with var` (CW26).** Deferred by rev 2 (§3.3): the
  extracted-`pn` form buys the same steady state with zero new machinery.
  Pick-up trigger: migrated corpus code where threading enclosing locals
  through an extracted `pn`'s parameters (the C4.2a friction) is a repeated,
  demonstrated pain — not a hypothetical one. Adoption is additive.
- **NM-O8 — PARTLY FIXED 2026-08-28; the typed arm stays open.
  DISSOLVES under CW29 (COW §11.9, 2026-08-29):** when plain-param snapshots
  land, *staying local to the callee is the correct behavior for both arms* —
  retiring `is_proc_param` makes flat writes join the nested behavior, the
  inconsistency below disappears by construction, and the untyped-arm
  root-skip fix gets reverted (it patched toward the write-through semantics
  S9.1.3 replaces). As of 2026-08-29 the root-skip is GATED on
  `!cow_capture_enabled()` rather than deleted -- flag ON already takes the
  CW29-correct detach arm; outright retirement happens at the default flip.
  The record below is kept for the pre-CW29 state. Nested path
  writes through a plain `pn` parameter were not published to the caller, while flat member writes through the same
  parameter are (`is_proc_param` selects the in-place checked setter only on
  the flat path; the nested path takes `cow_path_set` /
  `lambda_map_path_set_checked`, which publishes a replacement into the
  callee's own binding). Both tiers agree, so this is a flat-vs-nested
  inconsistency rather than a tier mismatch.

  **The untyped arm is fixed**: `cow_path_set_inplace` skips the ROOT detach
  (children are still detached and reinstalled), selected on
  `is_var_param || is_proc_param` — the same rule the flat store already used.
  That unblocked `cd2_orig`.

  **The typed arm was tried and reverted.** `lambda_map_path_set_checked`'s
  publish runs `lambda_type_check` over the whole candidate, which *converts*
  — a `3.5` admitted into an `int` field becomes `2`. An in-place write has no
  candidate to convert, so an in-place typed variant silently skipped the
  coercion; `proc_type_numeric_structural_admission` caught it (`3.5` where the
  golden says `2`). Doing this properly means checking and converting the
  *value* against the leaf field's declared type before the raw store — i.e.
  resolving the leaf field type through the path at runtime, which is the flat
  store's decomposition applied one level down. Until then, typed nested writes
  through a parameter need the explicit read-modify-write-back spelling.

---

## Appendix A — Implementation sketch

### A.1 One lowering, three entry points

`cow_path_set(owner, path, value)` already contains the whole mechanism. Factor
it into three reusable steps:

- `descend_detach(root, path) -> leaf` — walk the prefix, `cow_prepare_write`
  each link, install each replacement into its parent. Used by all three forms.
- `install(parent, key, leaf)` — raw store, no capture (CW25).
- `write(leaf, key, value)` — the existing final store, capturing per S9.3.1.

N1 = descend_detach + write. N2 = descend_detach, bind, then install on
return. N3 (if adopted, NM-O7) = the same pair at block entry/exit.

### A.2 Where the borrow writeback goes

Today the callee-side `var` writeback publishes to the caller's *root binding*
(`interp_write_binding` in T0; the borrowed-root path in MIR). For a path
borrow it must publish to the *parent slot of the place*. The caller already
computes that parent during `descend_detach`; it needs to be retained across
the call in the same register/scratch discipline the existing borrowed-root
scratch uses.

### A.3 The CW24 check

Both tiers already have the pieces. A binding is *place-initialized* when its
initializer is a member/index expression whose `compound_root_ident` resolves;
a mutation root is the root of an `AST_NODE_INDEX_ASSIGN_STAM` /
`AST_NODE_MEMBER_ASSIGN_STAM`, a `var` argument, or a `pn` method receiver.
The check belongs in `build_ast` so one implementation serves both tiers,
alongside the existing `E211` var-argument overlap check.

Keep it linear: one pass per function body collecting the mutation-root name
set, then one membership test per place-initialized binding — O(body +
bindings). The existing walkers (`mir_nested_control_writes_name`, the
`INDEX_ASSIGN_STAM`/`CALL_EXPR` arms of `has_elem_type_invalidation`) are
per-name rescans, O(body) *each*; reuse their arms, not their driving loop.

### A.4 Steady-state cost of the shared lowering — measured 2026-08-28

The descend/detach/install machinery is asymptotically right and
constant-factor wrong. Facts, from the current MIR emission
(`mir_emit_cow_path_set`, transpile-mir.cpp) and runtime:

1. **The slow path is sticky, and `var` parameters start on it.**
   `MirVarEntry::cow_children_may_be_shared` is set in 14 places and cleared
   in none, and every `var` parameter is born with it set (plain `pn` params
   are not — they ride the in-place checked setters via `is_proc_param`). So
   every nested write through a `var` param, and every nested write after a
   root's first COW event, permanently lowers through the dynamic path helper
   instead of the T20-1d shaped guarded store. The flag is honest (the
   runtime child marks it mirrors are themselves monotonic under CW3's
   1-bit), so this cannot be fixed by clearing it; it is fixed by making the
   path helper cheap (items 2–4) or by a per-path written-once witness
   (NM-O6). Watch this row when S9.1.3 lands: plain params become
   snapshot-or-borrow, and whichever they become must not inherit this
   pessimization wholesale.
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
per-link {flag check, fn_index, raw store} with zero allocation — and N2
still beats it in loops by hoisting even that to once per call.

*Why this doesn't reopen §5's rejected alternatives:* implicit borrowing
would be faster than untuned N1, but N2 reaches the same steady state —
descend once, raw in-place writes thereafter — without the semantic damage,
and tuned N1 closes most of the rest. Nothing on the table is asymptotically
better than what CW25/CW26 already specify; first-write detach cost O(width)
per level is Stage-1 COW itself (NM-O1), not this design.

---

## Appendix B — The four blocked scripts, and their fixes

Each is the same idiom and each has a mechanical fix. These become the CW24
diagnostic's own fixtures.

| Script | Shape | Fix |
|---|---|---|
| `proc/proc_fill_gc_nested.ls` | `c1 = null16(); l0[i0] = c1; c2 = null32(); c1[i1] = c2; c2[i2] = val` | Mutate before storing (fill `c1` then insert), or write the path `l0[i0][i1][i2] = val`. The script is the *fill-after-storing* porting hazard S9.3.1 names by name. |
| `awfy/deltablue2.ls` | Constraint/variable records read out of a vector, mutated locally | **Ported to the C4.2e handle store, 2026-08-28** — passes with the flag on, both tiers, zero `E232`. See §B.1. |
| `awfy/deltablue.ls` | Same shape, untyped variant | **Ported 2026-08-28**, derived from the `deltablue2` port with the annotations stripped, so the pair still differs only in typing. |
| `awfy/cd2_orig.ls` | Voxel vectors read, appended, discarded | **Migrated 2026-08-28** once NM-O8 was fixed: trie path writes plus `var` on the eight genuinely-mutating parameters. Runtime unchanged (~40s debug, within noise of the original), so its role as the `cd2` perf control survives. |

### B.1 Migration outcome, 2026-08-28

**All nine migrated (six spelling changes, the deltablue handle-store pair, and cd2_orig after NM-O8 was fixed); goldens unchanged in every case, and each passes
with the flag both on and off.** `r7rs/mbrot2` and `proc_fill_gc_nested` became
path writes; `proc_view_mutable` became a `var`-parameter borrow (the CW25
spelling, which is what S9.2.2 says a write-through view is);
`proc_param_typed_container_write`, `typed_map_write_child_ownership` and
`proc_markup_mutation` became read-modify-write-back (C4.2e). Flag-on failures
went 9 → 3.

**The remaining three are a different kind of problem, and stopping was
deliberate:**

- **`awfy/cd2_orig`** — **completed 2026-08-28, after NM-O8 was fixed.** The
  first attempt failed because the trie rewrite alone was not enough: its
  mutating helpers take *plain* parameters, and a nested path write through a
  plain parameter did not reach the caller. Fixing NM-O8's untyped arm removed
  that obstacle; the remaining work was marking the eight parameters that
  genuinely mutate (`vec_add`, `arr_set`, six `rbt_*` helpers) as `var`, which
  is the honest spelling under S9.1.3 regardless. No cascade into callers was
  needed in the end. Correct on both tiers in both flag states
  (`collisions=4305`), and — importantly for a perf control — the migrated file
  runs within noise of the original (~40.3s vs ~39.7s, debug build), so it
  still measures what it was there to measure.
- **`awfy/deltablue`, `deltablue2`** — a constraint *graph*: one variable
  record is observed by many constraints, which is exactly the shape C4.2e says
  needs a handle store. A mechanical `var x = (c.f); x.m = v` → `c.f.m = v`
  rewrite fits only 4 of ~12 sites (the rest read the temp repeatedly), and
  even where it fits it makes each constraint mutate its *own* copy of a shared
  variable.

  **`deltablue2.ls` has since been ported (2026-08-28)** and passes with the
  flag on, on both tiers, with zero `E232`. The port follows `richards3`'s
  idiom: one `w` world owns `w.vars` and `w.cons`, every field that held a
  Variable (`out`, `v1`, `v2`, `sc`, `off`) holds a variable id, every
  constraint list and plan holds cids, planner state (`currentMark`,
  `nextCid`) moved onto the world, and `w` travels as the single `var`
  parameter. Constraint ids start at 1 so cid 0 remains the "no constraint"
  sentinel `determinedBy` already used; variable id 0 is legal, so `NO_VAR` is
  -1. Constraints were given one uniform shape rather than a per-kind shape, so
  the store stays a single map shape. The two places that mutate a variable's
  constraint list use read-modify-write-back, since binding
  `w.vars[vid].constraints` binds a copy.

  Unlike `richards2` → `richards3`, this was done **in place**: the golden is
  unchanged (`DeltaBlue: PASS`), so the port is behaviour-preserving and there
  is no reason to keep the aliasing original alongside it.

  **`deltablue.ls` followed (2026-08-28).** The two files are the typed/untyped
  pair of one program, so rather than porting it independently the ported
  `deltablue2` was taken as the source and its annotations stripped — the `Vec`
  alias, the `: Vec` parameters, the `any` returns. They now differ in exactly
  138 lines, all of them signatures, which is what makes the pair a clean
  measurement of what the annotations buy. Both pass on both tiers in both flag
  states with zero `E232`.

**Defect found during the migration (pre-existing, not caused by this work):**
a nested path write through a *plain* `pn` parameter is not published to the
caller on either tier, while a *flat* member write through the same parameter
is (`is_proc_param` selects the in-place checked setter for the flat form
only). Both tiers agree, so it is not a tier mismatch — it is an inconsistency
between the flat and nested forms, and it is what forced `cd2_orig`'s cascade.
Recorded as **NM-O8**.

**Also fixed en route:** T0's scratch planner under-budgeted the nested-path
assignment branch, which holds value, path, terminal and owner slots live
across `cow_path_set` while the estimate assumed the flat form's three. The
symptom was `interp: scratch overflow depth=5 cap=5` and a silently dropped
write. Reproduced on pristine master with a migrated script, so it predates
this work.

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

---

## Appendix C — Deliberate-copy precedent survey (2026-08-29; basis of CW24v2)

How other value-semantics and COW languages spell "give me a private mutable
copy." Two families:

### C.1 Family 1 — binding *is* the deliberate copy (COW value semantics)

| Language | Deliberate snapshot | Notes |
|---|---|---|
| **Swift** | `var row = m.rows[i]` | Structs/Array/Dictionary are COW values; assignment is semantically a copy, so mutating the copy is the *expected* meaning, never an error. No `copy()` for value types. |
| **R** | `y <- x` | Copy-on-modify; same story. |
| **MATLAB** | `y = x` | Lazy copy, COW on write. Handle classes (reference semantics) need explicit `copy()` via `matlab.mixin.Copyable`. |
| **PHP arrays** | `$b = $a` | Arrays are COW values. Objects are references and need the explicit `clone` keyword. |

Post-flip Lambda is in this family — and every member is *more permissive*
than CW24 phase 1: none errors on a mutated copy.

### C.2 Family 2 — explicit copy operator (reference or borrow semantics)

| Language | Spelling | Notes |
|---|---|---|
| **Hylo (Val)** | `x.copy()` | Closest theoretical relative to C4 — mutable value semantics where projections are *borrows*, like S9.2.2 places. Its "no implicit copies" principle makes `.copy()` the sanctioned snapshot spelling; the strongest precedent FOR minting `copy()`. |
| **Swift 5.9 ownership** | `copy x` | A literal `copy` operator forcing a copy where the compiler would borrow/move — nearest syntax precedent for an `as copy` form. |
| **Rust** | `.clone()` | Universal, explicit, idiomatic; `Rc::make_mut` is `cow_prepare_write` verbatim. |
| **Clojure** | `(transient v)` | Explicit "private mutable snapshot to batch-edit, then freeze" — the NM-O2 use case as a first-class construct; the CW29 extracted-`pn` idiom is this shape spelled as a procedure. |
| **D / Go / Python** | `.dup` / `slices.Clone` / `copy.copy()` | Explicit helpers, all mainstream. |

### C.3 The implication that became CW24v2

CW24's error has no precedent in family 1 because those languages never
migrated from aliasing — there was no historical write-through expectation
for the guard to protect. Lambda's error is therefore justified exactly as a
**transition guard** and retires with the transition (§4.3). What family-1
languages *do* accept as a permanent diagnostic is the dead-store shape — a
copy mutated and never observed — which is CW24v2's surviving form. The
Hylo-style explicit `copy()` remains available as future sugar if steady-state
experience shows the intent marker earns its keep; nothing in the ruling
forecloses it.

