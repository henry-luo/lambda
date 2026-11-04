# Lambda Local State, Control Flow and Procedures — Design

**Revision:** 3 (2026-11-02) — local imperative `fn` bodies and confined shared captures, ratified by the user.
**Status:** `fn` local control and confined shared captures are **implemented and qualified in `codex/fn-local-state`** (Lambda/input baseline **8520/8520**). The indexed escape analysis covers aliases, containers, known return/retention summaries and transitive captures. Escaping closures preserve creation-time snapshots and reject writes of fn-owned captures. Qualification, conservative proof boundaries and existing `pn`/COW debt are recorded in the implementation plan. Comprehension extensions remain SO51. The resource model remains designed, not built.
**Formal linkage:** call effects S12.1.1v3, D7.1.1; local control S12.1.2v3, S16.6.5v2; results S2.5.3v3–S2.5.4v3; local ownership S1.4v2, S9.1.7v2; inout/captures S9.1.3v2–S9.1.4v2, D7.2.3; assignment S12.2.1v3–S12.2.2; COW S9.1.2, D5.4.1–D5.4.5; optimization D7.1.2; control forms S16.6.6–S16.6.9v2; error completion S7.4.6v3; resources S12.4.1v3–S12.4.3; script top level S16.7.1, S16.7.6.
**Implementation plan:** [completed implementation record](<impl/Lambda_Impl_Function_Local_State (done).md>).
**Spec mapping:** `Lambda_Procedural.md` §1–§5 (the 2026-01-23 proposal) → S12.1.2, S16.6.5, S12.2.1; `Lambda_Proc_Assignment.md` design principles 1–4 → S12.2.1; principle 5 and §4.3–§4.5 → S9.1.6, S9.3.2, S12.2.2; §4.6 and Phase 6 → superseded by S9.1.4 (Appendix S, S-1).
**Scope:** local imperative computation in both callable colours, the continuing `fn`→`pn` prohibition, mutable-root ownership, confined procedure captures, and the procedural APIs for external effects. COW machinery, document updates and concurrency retain their own records.
**ID series:** none minted. Rulings are cited by `S#`/`D#`; neighbouring areas keep their own series (C4 in `Lambda_Semantics_Formal.md`, CW# in the COW record, PTH# in the reference record, RG# in the runtime-globals record). Superseded items are numbered S-1… within Appendix S only.
**Consolidates:** `vibe/Lambda_Procedural.md` (2026-01-23, the procedural enhancement proposal: `break`/`continue`, `return`, `output()`, `while`, `var`, and future `assert`/`try`/`defer`/multiple returns) and `vibe/Lambda_Proc_Assignment.md` (2026-02-20, last amended 2026-09-14: the structural-mutation audit, BUG-1–BUG-8 and fix phases 1–7). Code-level anchors of the old C-text transpiler are not carried; the git history keeps them.
**Related:** user doc [`doc/Lambda_Procedural.md`](../doc/Lambda_Procedural.md); COW [`Lambda_Design_Runtime_COW.md`](Lambda_Design_Runtime_COW.md) (CW#); nested mutation [`Lambda_Design_Nested_Mutation.md`](Lambda_Design_Nested_Mutation.md) (CW22–CW28, SO14); document updates [`Lambda_Design_Reference.md`](Lambda_Design_Reference.md) (PTH30–PTH80) and [`doc/dev/Lambda_Document_Updates.md`](../doc/dev/Lambda_Document_Updates.md); C4 mutable value semantics [`Lambda_Semantics_Formal.md`](Lambda_Semantics_Formal.md); no global state [`Lambda_Design_Runtime_Globals.md`](Lambda_Design_Runtime_Globals.md) (RG14); concurrency [`Lambda_Design_Concurrency.md`](Lambda_Design_Concurrency.md); error handling [`Lambda_Design_Runtime_Error_Handling.md`](Lambda_Design_Runtime_Error_Handling.md); control-form syntax [`Lambda_Design_Syntax.md`](Lambda_Design_Syntax.md) §5–§6.

---

## 1. Position: locally imperative, observably functional

Lambda is a pure functional language with a declared, compiler-checked, one-bit effect system: `fn` is pure and deterministic under any schedule, `pn` may have effects, and **`fn` can never call `pn`** — statically where the callee is known, at run time where it arrives as a value (S12.1.1v3). Both colours admit local imperative computation (S12.1.2v3). The declared colour controls effects visible across the call boundary; the use of `var` or a loop does not make a callable procedural.

The governing principles are S1.10 and S9.1.2: colour what changes the caller's contract, and preserve observable value copies. An `fn` may use local working state while its call remains functional. It cannot write a caller's value or a captured binding; assigning `var work = input` gives an observable value copy. Explicit inout arguments and confined nested-`pn` captures are separately checked sharing boundaries (§5).

The new permission is lexical, not an inference that a procedure happens to be harmless. A `pn` remains forbidden from `fn`, including a helper that only touches local data and a procedure nested in the same body. Mutating builtins declared `pn` keep that restriction; local assignment does not recolour `push`, IO, tasks or host APIs.

*Why a declared bit rather than inferred purity.* The bit is load-bearing across the runtime (D7.1.2): it is the const-folder's soundness gate, the license for stream fusion and parallelization, the guarantee that `fn` regions never suspend, and what makes the JS Promise membrane computable. An inferred bit would let a library change colour silently with its implementation; a declared one changes a caller's contract only when the declaration changes. The accepted price is that effects are not reified: a `pn` call executes; there is no held, unexecuted effect value.

Three boundaries follow from the bit and are easy to miss:

- **A script's top level is `fn` effect context, not an `fn` activation.** Top-level mutable statements remain forbidden; procedural code belongs in `main()`, run by `lambda run script.ls` (S12.1.1v3, S16.7.1). `lambda run` without a script opens a procedural REPL session whose entries append to one implicit procedure body (S16.7.6).
- **Element construction is `fn` context, even inside a procedure.** Attributes and content of `<elmt …>` evaluate as pure expressions; a `pn` call nested directly in an element literal is rejected statically, or refused at run time when the callee's colour is unknown (S12.1.1v3, D7.1.1; USER 2026-10-15). Run the procedure in a preceding statement and build the element from its bound result:

  ```lambda no-run
  // no-run: load_parts is a placeholder procedure
  pn main() {
      let parts = load_parts()
      let file = <file format: 'pdf', parts>
      file
  }
  ```

- **Effect polymorphism is spelled `function`.** A `function` declaration takes its colour from its function arguments and is checked as an `fn` body (S12.1.4v4); its local control follows S12.1.2v3, and it is how a higher-order helper serves both worlds without being written twice.

## 2. The procedural surface

### 2.1 Local control in either colour

`fn` and `pn` block bodies admit `var`, assignment, `while`, `break`, `continue` and `return` (S12.1.2v3). The same applies to an unprefixed arrow whose body is a block, without changing empty-map arrows or map-valued bodies (S16.6.7v3). There are four independent checks:

| Check | Rule |
|---|---|
| Call effect | An `fn` never calls `pn`, even a locally declared one (S12.1.1v3). |
| Write ownership | An `fn` writes only its own local `var`s; captures, ordinary parameters and `let`s are immutable (S9.1.4v2, S9.1.7v2). |
| Control target | `return` targets the nearest callable; `break`/`continue` target an eligible loop in that callable (S12.1.2v3). |
| Value/statement position | Imperative control blocks are statements; callable block bodies are the explicit admission site (S16.6.8v3). |

The grammar remains shared. Semantic analysis, rather than a duplicated `fn`/`pn` grammar, checks these permissions (S16.6.5v2). A `while` inside an `fn` inherits the pure call boundary; opening its body must never set the enclosing computation's effect to `pn`.

```lambda no-run
// no-run: design example; executable fixture is fn_local_control.ls
fn first_index(xs, target) {
    var i = 0
    while (i < len(xs)) {
        if (xs[i] == target) { return i }
        i = i + 1
    }
    return null
}
```

This changes S12.1.2's former pn-only restriction. The old grammar-split proposal and the old association of imperative syntax with effects are retained in Appendix S.

### 2.2 Control forms

- **`while` has one node with two spellings**, like `if` and `for`: `while (c) body` or `while c { … }`; `(` immediately after `while` commits to the parenthesized spelling (S16.6.1, S16.6.2).
- **Statements require braces.** `return`, `break` and `continue` are admitted only inside a braced body; every unbraced control body — a paren-form `if`/`for` body, an `else` body, a `case T:` arm, an `=>` body — is an expression position and rejects them. `raise` is an expression and stays valid there (S16.6.6).
- **An imperative control block is a statement, not a general expression.** Such a block cannot be embedded in a tuple, argument, operand or colon-form match value. Named callable bodies and anonymous callable block bodies admit local statements under S16.6.8v3; this admission does not grant `pn` call permission. An `if`/`else` chain or `match` is either a value form or a control form, never a mixture (S16.6.9).
- **A procedure's body is always a braced block.** `pn name(...) { … }` is never `=>`-bodied (S16.6.7v3).

### 2.3 `break` and `continue`

`break` exits the innermost eligible loop in the same callable; `continue` skips to that loop's next iteration. `while` is eligible in both `fn` and `pn`; existing procedural `for` loops retain their behavior. A nested callable cannot break or continue its parent's loop. Both statements require a braced body (S12.1.2v3, S16.6.6). Extending them to functional comprehensions is SO51 (§6).

Labeled loops (`outer: for …` / `break outer`) were sketched in 2026-01 as a future extension and have not been adopted; nested-loop exit is written with a flag or by moving the inner loop into a callable that `return`s (§9, open item).

### 2.4 `return` and block results

`return e` exits the nearest enclosing callable with `e`; a bare `return` returns
`null`. The target is the same from inside a loop or handler. Because `return`
takes a following start-token line as its value, `return` followed on the next
line by `42` returns 42; a bare return is `return` followed by a separator or
the closing brace (S16.2.5).

**Functional fallthrough is preserved** (S2.5.3v3). An `fn` still collects normal expression results. Local declarations, assignments and statement `while` loops contribute no result. Adding a `var` does not switch the function to last-result sequencing. `return e` discards accumulated body results and completes with `e`, subject to ordinary sequence-result normalization and the declared return contract (S2.5.7v4, S11.4.1v5).

**Normal procedural sequencing is replacement, not collection or null
filtering** (S12.1.2v3, S2.5.3v3). Statements execute in sequence; each normal
result replaces the preceding result, including `null`. A statement whose
contract produces no result leaves it unchanged. A normally completed block
yields the most recent result, or `null` if none was produced; a procedure
reaching its body's end returns that value. This rule applies to nested
procedural blocks as well as the procedure body. It does not turn procedural
blocks into general expressions (S16.6.8v3).

**No result is distinct from a `null` result** (S2.5.4v3). Declarations
contribute no result, so `pn f() { 11; let x = 22 }` returns `11`, whereas
`pn f() { 11; null }` returns `null`. No result is not a language value or
type. Other statements follow their own result contracts; this clarification
does not assign a new result to assignment or loop statements.

A handler has an ordinary normal result, regardless of its operand's `fn`/`pn`
effect (S7.6.1v9, S7.6.7v5). A final handler supplies the block's result; a
later result can replace it. Producing a fallback value in the handler body
differs from writing `return fallback`, which exits the enclosing callable.
The M1 implementation and qualification are recorded in
[TE-16.1](Lambda_Design_Type_Error_Handling.md#te-161--handler-results-follow-procedural-sequencing).

`return` is how a scoped resource escapes its block, and the escape must be visible in the declared return type (S12.4.2, §7.3).

### 2.5 `main()` and the two run modes

`lambda script.ls` evaluates the script's top level as content (S16.7.1); `lambda run script.ls` additionally calls `pn main()`. `main` is an ordinary `pn` in an otherwise functional module: it does not make the module procedural, and it does not permit module-level `var` (S9.1.7).

## 3. Bindings and assignment

### 3.1 Who may be reassigned

An `fn` may rebind or update members/indexes only through a `var` declared in that invocation. It cannot accept `var`/inout parameters or write an enclosing binding; a nested `fn` sees a snapshot even when its parent owns a mutable local (S9.1.3v2–S9.1.4v2, S9.1.7v2). The nested-`pn` exception is exactly §5.3.

`let` bindings and parameters are immutable; only `var` reassigns; shadowing is allowed (S12.2.1). Assigning to a `let` or a parameter is E211, and so is a write *through* one — `arr[0] = 99` on a `let arr` — since a `let` is final for everything reachable through it (S9.1.1). Multiple declarations share one statement: `var a = 0, b = 1`. Comma decomposition binds several names from one value — `let a, b = expr` positionally, `let a, b at expr` by name — and bracket destructuring patterns (`let [a, b] = …`, `let (q, r) = …`) are rejected by design (S16.8.7).

*Why parameters are immutable.* A plain parameter is a value the caller handed over; letting the callee reassign it would make the parameter list a second, invisible set of locals and would blur explicit inout sharing through a `var` parameter (§5.2). Since 2026-08-29 a callee that mutates through a plain parameter mutates its own snapshot, never the caller's value (S9.1.3, CW29); inout passing and confined procedure capture are the explicit write-through boundaries.

### 3.2 The type of a `var`

An **unannotated** `var` may change runtime type on reassignment; an **annotated** `var` constrains every assignment to its declared type (S12.2.1, S11.4.1). A mismatch is E201 (*type check at assignment*), raised by the checked boundary at run time when the value's type is not known statically. Implicit widening at an annotated position is by **exact embedding only** (S4.4.1): `int → float`, `int → int64` and every exact path into `integer`/`decimal` are admitted; `int64`/`u64 → float` and every narrowing need an explicit conversion. *Assignment never silently corrupts data* — the single sentence that the whole 2026-02 audit (Appendix A.2) existed to make true.

*Why widen silently but check annotations.* An unannotated `var` states no contract, so the compiler serves the program as written: it keeps the fast native carrier while every assignment agrees, and moves the binding to a boxed `Item` home when one does not (D2.3.4–D2.3.5). An annotation is a contract the author wrote down; honouring it is the point of writing it. The 2026-02 design admitted any numeric coercion (`int ↔ float ↔ int64 ↔ decimal`) at annotated positions; the 2026-07-20 numeric model narrowed that to exact embedding (Appendix S, S-2).

The widening decision is static and tier-shared. Pre-widening AST reads can retain their initial types; compact literal selection now follows widened dependencies through aliases and nested expressions and declines that stale storage proof ([LR03-36](<Lambda_Issue_Ledger (fixed).md#lr03-36>), fixed 2026-10-28; S12.2.1, D4.3.3).

### 3.3 Assignment targets

The surface has three target shapes: an identifier (`x = v`), an index (`a[i] = v`), and a member (`m.f = v`), composing into paths (`a.b[i].c = v`). There is no compound assignment (`+=`), and decomposition is a declaration form, not an assignment target.

- **A write may update or add only a member admitted by the container's existing key domain, and never changes the container kind** (S9.1.6). `var a = []; a["str"] = 1` raises (S7.1.3v2) and `a` stays an array. Changing kinds is whole-binding replacement: `var a = [1]; a = {value: 1}`. Adding a new field to a map or an open object instance is an ordinary member addition and a shape transition.
- **An invalid write raises.** Out-of-bounds, key-domain and base failures on a member or index write are hard errors through the `T^` channel, never a soft value, `null` or a silent no-op; sequence growth is explicit (`push`, `splice`) (S7.1.3v2). *Reads ask a question; writes issue a command.* The 2026-02 runtime setters logged and returned on every such failure — the silent no-op this ruling forbids.
- **Elements are mutable on both faces** (S12.2.2): `elem.attr = v` is map-field assignment; `elem[i] = v` is child assignment, which keeps content normalized (S2.6.5).
- **Storage follows the value, not the other way round.** A write of a value the container's packed representation cannot hold exactly moves the container to a representation that can — never a truncation, never a reinterpretation of bits (S9.3.2), and the change of representation is unobservable (D3.2.2). The 2026-02 audit found the opposite: an `ArrayInt` slot read back a float's tagged bits as an integer (BUG-1). The fix generalized into the construction ruling: storing a scalar and reading it back keeps its type at every magnitude.
- **Field type changes rebuild the shape.** A map or element field assigned a value of another type gets a new shape entry; shape metadata always describes the stored data (principle 5 of the 2026-02 audit; BUG-3 was a `null` field holding a container under a `null` shape).

### 3.4 `=` never writes a document

`=` is Tier 2: it changes a binding and the value that binding owns, nothing else (PTH55). A document — a file, a `temp.` store, anything reached through a reference and the force step `#` — changes only through the Tier 3 statements `put`, `del` and `output`; `doc#a.b = v` is a compile error naming `put`, and on a non-`var` root `=` is an error, never an implicit `put` (PTH72v2, PTH73). The tiers share no operators. See §7.1.

## 4. Where mutable state lives

**There is no global mutable state** (S9.1.7v2). Module-level `var` and writes through module-level `let` containers remain forbidden. Mutable roots belong to:

- an **`fn` activation**, for working state local to that call;
- a **`pn` activation**, including `main`;
- a **view/template instance**, whose persistent state is written from handlers (S12.1.3v2);
- an object reachable from such a root, under its ordinary value and receiver rules.

A confined nested `pn` can share an `fn`-owned root during that invocation (§5.3). It does not acquire a persistent mutable cell. A closure that can leave the invocation uses immutable snapshots, so returning a closure does not add a hidden state store. The ban on mutable capture by reference into a task remains (S13.1.4).

The distinction between an `fn` activation and an `fn` *effect context* matters: the module top level, an element literal and a template's persistent state do not become mutable just because local variables are admitted in functions. An ordinary pure helper called there can use its own working state.

## 5. Mutable value semantics

Binding, assignment and construction **copy, observably, for every container kind**; the implementation is copy-on-write, and sharing must be unobservable — a verifiable property, not a convention (S9.1.2). The machinery is the COW record's (D5.4.1–D5.4.5, CW#); this section states only what a procedure author sees.

### 5.1 Copies, not references

`var b = a; b[0] = 9` leaves `a` unchanged. There are no reference cells; structural `==` is the only value equality, and cycles are unconstructible (S9.1.5v2). Placing a value into a container captures it by value at every constructor and insertion point (S9.3.1) — the porting hazard is *fill-after-storing*, which leaves the stored copy empty: fill before storing, or read-modify-write. Iterating a `var` container walks the entry-time value, so mutating it inside the loop never invalidates the iteration (S9.2.3).

### 5.2 `var` parameters: explicit inout sharing

`pn f(var a: T)` is an inout borrow: the callee's writes land in the caller's `var` (S9.1.3). Arguments must be `var`s; the same `var` or overlapping paths may not feed two `var` parameters (exclusivity, writer-vs-writer only); `var` parameters are invariant (`var xs: int[]` cannot be passed to `var a: any[]`, S9.2.1). A dynamic call to a `var` signature is rejected, because a value span carries no writable caller location (S12.3.2); for the same reason a `pn` method cannot be taken as a value (S12.3.3v2).

### 5.3 Function and procedure closures

**Ratified 2026-11-02 (USER): confined sharing; escaping snapshots.** S9.1.4v2 and D7.2.3 distinguish these cases:

| Closure | Capture behavior |
|---|---|
| Nested `fn`, including an unprefixed arrow | Immutable snapshots at creation; no writes to captures. |
| A nested `pn` proven not to escape its parent `fn` | Live access to the parent's local `var`s; sibling `pn`s share the same bindings. Captured `let`s and ordinary parameters remain immutable. |
| Any closure that can escape that `fn` | Existing snapshot-at-creation rules; assignment or interior mutation through captures is a compile error. |

**Shared access means the actual parent binding.** Whole-variable reassignment, member/index updates and COW replacement all go through that binding's owner. Independently writable environment copies are not equivalent. A local value copy such as `let saved = xs` remains a value snapshot even if `xs` itself has a live procedure capture (S9.1.2).

**Escape classification precedes capture creation.** Returning a closure, retaining it outside the invocation, forwarding it through a callee that may retain it, or capturing it transitively in an escaping closure prevents a live capture. An unknown escape path is not a proof of confinement. A potentially escaping closure takes its snapshots when formed, not at the eventual return/store; a mutable captured write is rejected rather than silently changing its target. Internal representation must preserve existing no-cycle, inout-exclusivity and precise-rooting contracts (S9.1.3v2, S9.1.5v2, D5.3.2).

```lambda no-run
// no-run: design example; executable fixtures cover creation-time snapshots
fn make_reader(seed) {
    var value = seed
    let read = pn () => { return value }
    value = value + 1
    return read
}
```

Calling the returned `read` from `pn` context observes `seed`, the snapshot at its creation, not the later value. Adding `value = value + 1` inside `read` makes the escaping closure invalid. Creating a private counter that persists across calls is not part of this design.

**Capture permission does not grant call permission.** An `fn` still cannot call the nested `pn`, directly, through an alias, through `call`, or through a colour-polymorphic helper (S12.1.1v3, S12.1.4v4). With that boundary and no live-capture escape, there is no legal path that executes the shared-capture procedure during its parent `fn` invocation. Its declaration/capture eligibility can be checked, but it is not an executable imperative helper for the `fn`. That is an explicit consequence of the user's two rules, not a compiler exception to infer. Algorithmic work inside `fn` uses its own statements or nested pure helpers returning values.

**Why preserve the escaping rule.** General mutable closure cells would reopen hidden persistent state, callable-copy/equality behavior, module-held counters and optimizer reuse. Confinement avoids that expansion and preserves immutable function values at escaping boundaries. The older writable-copy counter design remains superseded (Appendix S, S-1); it must not be restored as an implementation shortcut.

### 5.4 Nested mutation and shared records

A deep update spells the path: `t.nodes[i].value = v` writes in place through the `var` root. `var h = t.nodes[i]` is a *copy* — mutating `h` does not reach `t` unless it is stored back — and the compiler binds such place copies as borrows wherever the difference is unobservable (D5.4.4, D5.4.5, D5.4.5). Ergonomics for deep updates are owned by the nested-mutation record (SO14, CW22–CW28).

When two owners need to see one record, the idiom is the **handle store**: one container owns each record, every other reference is a key into it, and the store travels as one `var` parameter. Identity that aliasing supplied implicitly becomes data (C4.2e; `doc/Lambda_Procedural.md` §Sharing Mutable State). The AWFY `richards` and `deltablue` ports are the worked examples.

## 6. Loops, early exit and the effect bit together

S12.1.2v3 separates sequential local computation from external effects. A `while` and its local state have a deterministic order; early return chooses the call's result. Neither requires permission to call a procedure. A nonterminating loop is still possible, just as nonterminating recursion was; purity is not a termination proof.

S16.6.5v2 preserves existing `for` meaning. Adding `var` to an `fn` never silently reclassifies a comprehension as a procedural loop. **SO51 remains open:** mutation of a containing local across comprehension iterations, `break`/`continue`, nonlocal `return`, partial collected results and interactions with `where`/`order by`/`limit` and parallelization. The implementation milestone covers the ratified `while` form and existing comprehensions; it must not invent these extensions while relaxing the old blanket guards.

The effect bit remains declared, checked, and suspension-free for `fn` (D7.1.2). However, an optimizer needs local read/write and control dependencies before moving an expression, folding a changing local, reusing a closure environment or parallelizing iterations. Constant evaluation needs a bounded evaluator or a safe refusal for loops it cannot evaluate; compile time must not hang on an admitted runtime loop. Interp, MIR Direct and auto promotion must agree on these facts.

## 7. Reaching the outside world

### 7.1 Files and documents: `output`, `put`, `del`

`output(v, target, options?)` writes a whole document. It is a procedure-family effect: `pn`-only inside a function, and at a script's top level it runs as its own transaction — the shell case (PTH55). Its format follows the target's extension unless an option says otherwise, `{mode: "append"}` appends, and failure **raises**, so a call is engaged with `^` or a handler (S7.4.5, S7.5.1v2). Under the reference design `output(v, p)` is exactly the whole-document `put p# = v` (PTH54v2), and the finer edits are `put`/`del` inside `open { … }` transactions (PTH60v3, PTH68v3). The file utilities are the `io.*` procedures (`io.copy`, `io.move`, `io.delete`, `io.mkdir`, …); `input`, `exists` and `format` are readers available in both colours.

The 2026-01 proposal specified `output(source, url, format?)` returning `null | error`; both the positional format symbol and the error-as-value return are superseded (Appendix S, S-3).

### 7.2 Errors

A callable reports ordinary failures as error outcomes. Its return contract must admit the error (`T^` abbreviates `T | error`; particular errors use ordinary unions); `raise` and explicit `return` can complete either colour early (S7.4.2v3, S7.4.6v3, S12.1.2v3). Caller acknowledgement follows S7.5, and handler values and control targets follow S7.6.1v9. A `pn` handler boundary also catches system faults (S7.11.2v2). The `try`/`catch` block sketched in 2026-01 was never adopted (Appendix S, S-4); the current error contract is owned by [Type and Error Handling](Lambda_Design_Type_Error_Handling.md).

### 7.3 Resources and cleanup

`open()` acquires a scoped resource and is `pn`-only; a resource auto-closes at the end of its enclosing block, and ownership escapes only by `return` with the escape visible in the declared return type (S12.4.1v3, S12.4.2). There is no `defer`, `with` or `finally` keyword: auto-close is the only user-facing cleanup mechanism (S12.4.3) — *auto-close is `with` without the `with`; close is not just release, it is the last write.* The `defer` statement sketched in 2026-01 is rejected by that ruling (Appendix S, S-5). The resource model R1–R5 is designed, not built (S12.4 Appendix A footnote).

## 8. Decision summary

| Topic | Ruling | Status |
|---|---|---|
| `fn`/`pn` effect bit; top level and element construction are `fn` context | S12.1.1v3, D7.1.1 | Implemented; residue in the S12.1.4v3 footnote |
| Local `break`/`continue`/`return`/`while`/`var` and assignment in either callable colour | S12.1.2v3, S16.6.5v2 | Qualified on interp/jit/auto; Lambda/input baseline 8520/8520 |
| Fn fallthrough collects; pn normal results replace, including null; no-result statements leave the accumulator unchanged | S2.5.3v3, S2.5.4v3, S12.1.2v3, S7.6.1v9, S7.6.7v5 | Fn local-control extension implemented; broader handler changes retain their own status |
| `while` two spellings; statements need braces; control blocks versus value blocks; branch homogeneity | S16.6.1–S16.6.2, S16.6.6, S16.6.8v3, S16.6.9v2 | Fn/arrow extension qualified; C parser 444/444, CST parser 430/430 |
| `return` takes the next start-token line | S16.2.5 | See its footnote |
| `let`/params immutable; unannotated `var` may change type; annotated `var` by exact embedding | S12.2.1, S4.4.1, S11.4.1 | Implemented; LR03-36 partial |
| Member/index writes stay in the key domain; no kind change | S9.1.6 | Core tiers conformant; editor/host access sites unaudited |
| Element attribute and child assignment | S12.2.2 | Implemented |
| Writes never corrupt a packed representation | S9.3.2, D3.2.2 | Implemented |
| Values copy; COW unobservable | S9.1.2, D5.4.1–D5.4.5 | Stage 1 landed |
| Inout parameters; exclusivity; invariance | S9.1.3v2, S9.2.1 | Snapshot half unconditional; exclusivity partial |
| Escaping/`fn` closures snapshot; confined nested `pn` may share parent `fn` locals | S9.1.4v2, D7.2.3 | Indexed confinement proofs select shared traced homes in T0/MIR; escaping fn-local captures reject writes and retain value snapshots. Unknown retention requires snapshots. Legacy pn-parent capture debt remains separate. |
| Comma decomposition; no bracket destructuring | S16.8.7, S12.2.1v3 | Mutable targets, COW ownership and capture flow implemented; [follow-up record](<impl/Lambda_Impl_Function_Local_Decomposition (done).md>) |
| No global mutable state; fn activation may own local working state | S9.1.7v2 | Global prohibition preserved; fn-local admission implemented |
| `=` is Tier 2; documents change only by `put`/`del`/`output` | PTH55, PTH72v2, PTH73 | Implemented (PTH30–PTH80) |
| Auto-close resources; no `defer` | S12.4.1v3–S12.4.3 | Designed, not built |

## 9. Open issues

- **SO51 — Comprehension extensions:** local cross-iteration mutation and early exits need a separate ruling (§6); they are outside the `while` milestone.

- **Legacy pn-parent capture modes** (S9.1.4v2, D7.2.3): the parent-fn extension has indexed confinement proofs and shared traced homes. A closure over a pn-parent `var` still has legacy writable-copy debt (S-1), and only the E231 read-after-call guard protects the outer scope (§5.3). COW Stage 2.
- **Exclusivity** (S9.1.3): faces 1, 3 and 4 landed (overlapping `var` arguments are E211); face 2 is unreachable behind E229; view-borrow confinement (S9.2.2) and the view-state half of S9.2.4v2 are outstanding.
- **A repeated literal map key** keeps both entries; a write updates the first while a read takes the last — [LR03-17](Lambda_Issue_Ledger.md#lr03-17), needs a ruling.
- **Nested-mutation ergonomics** — SO14, owned by the nested-mutation record.
- **Not adopted, not rejected:** labeled loops (`break outer`; the AST's label fields serve LambdaJS only), compound assignment (`+=`), an `assert` statement. Each was proposed or asked for and none has a ruling; raise them with the user before building.

## Appendix S — Superseded designs and rulings



- ~~**S-1 · Closures capture by writable copy** (Proc_Assignment §4.6, Phase 6, BUG-7 fix, 2026-02): each closure gets its own mutable `Item` slot per captured `var`; writes inside the closure persist across its calls while the outer `var` stays independent (the closure-counter pattern).~~ Replaced by **S9.1.4** / **D7.2.3**: closures are immutable values and capture assignment is a compile error (§5.3).
- ~~**S-2 · Annotated `var` admits any numeric coercion** (Proc_Assignment §4.1.1 rule 3: `int ↔ float ↔ int64 ↔ decimal`, and `INT ↔ INT64` interchangeable in `fn_map_set`).~~ Replaced on 2026-07-20 by exact-embedding widening, **S4.4.1** via **S12.2.1** (§3.2).
- ~~**S-3 · `output(source, url, format?): null | error`** (Procedural §3): a positional format symbol defaulting to `'mark`, extension auto-detection, and failure returned as an error value.~~ Replaced by `output(v, target, options?)`, which raises (**S7.4.5**), takes options as a map (**S17.8.1**), and is the Tier 3 whole-document `put` (**PTH54v2**, **PTH60v3**) (§7.1).
- ~~**S-4 · `try`/`catch` blocks** (Procedural §6.2, future).~~ Never adopted; error handling is the channel model of **S7.4–S7.6** (§7.2).
- ~~**S-5 · `defer` statement** (Procedural §6.3, future).~~ Rejected by **S12.4.3**: auto-close is the only cleanup mechanism (§7.3).
- ~~**S-6 · `pn`-only statements enforced by a `proc_content` grammar production** (Procedural §1–§5, grammar sketches).~~ Replaced by semantic analysis on the effect boundary, **S16.6.5** (§2.1). The grammar sketches' optional trailing `;` and `while (cond) { content }`-only form are likewise superseded by S16.1 separation and the two spellings of **S16.6.1**.
- ~~**S-7 · No field addition by assignment** (Proc_Assignment §4.4.1: "field NOT found: error (no field addition in this phase)").~~ Replaced by **S9.1.6**: a write may add a member admitted by the existing key domain, as a shape transition (§3.3).
- ~~**S-8 · Accumulating by re-`let` inside a loop** (Procedural §1 examples: `let total = total + n` inside a `for` body).~~ Never valid: an inner `let` shadows for its block only and accumulates nothing. The spelling is a `var` and assignment.
- ~~**S-9 · A plain parameter mutated by the callee writes through to the caller** (the AWFY-era behavior the 2026-02 audit inherited).~~ Retired 2026-08-29: plain parameters snapshot unconditionally and `var` is the sole write-through construct, **S9.1.3** (CW29).
- ~~**S-10 · Multiple return values by tuple unpacking** (Procedural §6.4, future: `let (q, r) = divide(17, 5)`).~~ Bracket and paren destructuring are rejected by **S16.8.7**; a procedure returns one value (an array or map when it has several results), and comma decomposition `let q, r = divide(17, 5)` takes it apart (§3.1).
- ~~**S-11 · A procedure without an explicit return returns its last expression.**~~ Refined by **S12.1.2v3** (2026-11-02): retain the most recent normal result, including null, while no-result statements leave it unchanged. The earlier shorthand did not specify that distinction or the result of a standalone handler (§2.4, TE-16.1).
- ~~**S-12 · Imperative keywords are pn-only; fn bodies and all closures contain no mutable state.**~~ Superseded 2026-11-02 by S1.4v2, S9.1.4v2, S9.1.7v2 and S12.1.2v3. The former argument coupled `while` and early exits to effects because a comprehension's result could depend on evaluation order. The replacement admits sequential local computation while retaining the comprehension question separately (SO51). The former grammar split (`proc_content`) duplicated statement-bearing productions; the shared grammar and semantic checks remain the chosen architecture.
- ~~**S-13 · Every capture is an immutable snapshot; direct up-level access for a non-escaping nested pn is deferred.**~~ Replaced by the confined shared-capture permission in S9.1.4v2. Pure and potentially escaping closures retain the blanket snapshot rule; captures outside this parent-fn exception are unchanged. The parent `fn` still cannot invoke `pn`, so this change does not grant a local-helper call exception. The former ownership list named only `pn` activations, views and objects; `fn` activations now own local working state too.

## Appendix A — Implementation notes

### A.1 Current shape

Anchors are symbol names; confirm line numbers against the tree. Local-control
and capture sections updated 2026-11-02; other entries retain the 2026-10-23 survey.

- **Front end.** Both parsers already admit local-control syntax. `resolver_function_begin` records lexical ownership independently of `NameScope::is_proc`; `resolve_control_statement`, `resolve_while`, `resolve_var_stam` and `resolve_assignment` use that ownership for S12.1.2v3. `direct_validate_write_owner` enforces fn-local writes and the SO51 boundary. Call-colour validation remains a separate pass (S12.1.1v3).
- **AST.** The statement nodes are `AST_NODE_BREAK_STAM`, `CONTINUE_STAM`, `RETURN_STAM`, `VAR_STAM` and `ASSIGN_STAM`. `while` is an `AST_NODE_LOOP` with `LOOP_FORM_WHILE` (`AstWhileNode` is a typedef). `resolve_assignment` retags an index or member target as `AST_NODE_INDEX_ASSIGN_STAM` or `AST_NODE_MEMBER_ASSIGN_STAM`. Every assignment is one record, `AstAssignNode`, with `target_entry` and its COW fields (D8.2.2); `AstAssignStamNode` and `AstCompoundAssignNode` are aliases of it, where "compound" means an index or member target, not `+=`. Decomposition is `AST_NODE_DECOMPOSE` (`AstDecomposeNode`).
- **Binding facts.** `NameEntry` keeps the three 2026-02 flags `is_mutable`, `has_type_annotation` and `type_widened`, beside the COW facts (`cow_param_mutated`, `cow_owned`, `is_place_copy`, `var_root_may_share`, …). `resolve_assignment` sets `type_widened` on the first type-changing write to an unannotated `var`; later reads are typed `any`. MIR decides a native-lane `var`'s home once at its declaration (`mir_nested_control_writes_binding` with widening cascade). A `var` passed as a `var` argument is also boxed. T0 stores every binding as an `Item`.
- **Execution.** T0 runs loops in `interp.cpp` and signals `break`, `continue` and `return` through `EvalSignal`. MIR lowers through `transpile_while` and `transpile_assign_stam_core`. Annotated boundaries go through `emit_checked_boundary`, and on T0 through the matching interp check.
- **Paths and COW.** A nested path write runs through `exec_place_assign` on T0, and through `transpile_compound_assignment_item` and the `cow_place_leaf_*` helpers in MIR. Both reach `cow_path_set_impl` / `cow_path_set_inplace`. The COW primitives are `cow_mark_shared`, `cow_bind_var` and `cow_prepare_write`. The Lambda-side write wrappers (`index_assign_cow`, `array_set_cow`, `member_set_cow`, `map_set_cow`) prepare the write before calling the raw setter (D5.4.2).
- **Runtime setters.** `fn_index_set` dispatches by owner kind. A sequence write with a non-integer key raises E201. On an element, an integer key writes a child (`fn_array_set`) and a name key writes an attribute (`fn_map_set`). `fn_array_set` still converts a specialized array in place (`convert_specialized_to_generic`). A field type change in `fn_map_set` retags in place when that is safe, and otherwise moves the container onto a runtime shape-tree target (`container_retype_field`, D3.4.5). `map_rebuild_for_type_change` now serves only JS shapes. A new member is added by `map_extend_open_shape`. `Container::is_data_migrated` is still consulted for parser-built containers.
- **`var` parameters.** At the call site an argument must be a mutable `var` root (E211). Overlapping roots or view bases are E211, and the types must match exactly (E207). A plain `pn` parameter written by the callee is snapshotted (`cow_param_mutated`).
- **Closures.** `lambda_analyze_capture_permissions` builds one indexed value-flow graph and fixed-point known-call summaries before either tier creates captures (S9.1.4v2, D7.2.3). Confined named/anonymous pn values share actual parent homes through aliases and containers; unknown retention, pure-fn capture boundaries, escaping ancestors and reference-cycle backedges require snapshots. A home is an internal one-element GC Array using existing owned Item/scalar storage. Parent writes and COW detaches publish through that home; snapshots mark captured containers shared and copy wide scalar payloads. T0/MIR environments and parent slots precisely root their homes. AUTO declines shared-home satellites; eager MIR supports them. Legacy pn-parent captured-var writes and their E231 guard remain separate debt. A `start` capturing a mutable `var` is E221.
- **Element purity.** `colour_walk_visit` switches to functional mode inside element and object literals, so `colour_walk_call` reports E224. Dynamic callees go through the run-time colour guard (`fn_colour_guard`).
- **Output.** `output` is registered at arity 2 and 3 (`pn_output2`, `pn_output3`; append through `pn_output_append`) in `lambda-proc.cpp`. The `io.*` procedures are in `sys_func_registry.c`. Both tiers still handle `AST_NODE_PIPE_FILE_STAM`, the old pipe-to-file statement, but `build_ast` no longer creates it, so the path looks unreachable. Retiring it is a cleanup, not a design question.
- **Fixtures.** Local-control and snapshot coverage is in `test/lambda/fn_local_*.ls` and `proc/fn_local_proc_snapshot.ls`; the former `mutation_in_fn` and `function_body_var` negatives are now the valid `fn_local_legacy` and `fn_local_colour_poly` cases. Ownership, capture escape, comprehension control, lexical targets and static/dynamic pn refusal have `negative/{semantic,runtime}/fn_local_*` fixtures. Existing `proc_control`, `proc_var*`, `cow_*`, `var_param*`, `let_finality` and typed-container-write cases retain their roles.

### A.2 History

- **2026-01-23 — the procedural enhancement proposal** (`Lambda_Procedural.md`). Added `while`, `var`, assignment, `return` and `output()`; `break`/`continue` parsed but needed `if` statement work. Priority table: all five P0; `assert` and `try`/`catch` P2; `defer` and multiple returns P3.
- **2026-02-20 — the structural-mutation audit** (`Lambda_Proc_Assignment.md`). Found that mutation had been built ad hoc for the AWFY benchmarks and catalogued eight defects, all fixed in phases 1–7 on the then C-text transpiler:

  | Bug | Defect | Fix (then) | Where it stands now |
  |---|---|---|---|
  | BUG-1 | Specialized-array write extracted a mismatched value's tagged bits — silent garbage | Convert the array to generic in place; specialized getters fall back on `type_id` | Generalized as S9.3.2 / D3.2.2 |
  | BUG-2 | `elem[i] = v` silently dropped | List/element cases in the array setter | S12.2.2 |
  | BUG-3 | `null` field holding a container under a `null` shape | Rebuild the shape on every type change | Principle kept (§3.3) |
  | BUG-4 | MarkEditor zeroed a field on type change | Runtime path stopped using MarkEditor | MarkEditor unchanged for input parsers |
  | BUG-5 | `let` reassignable inside a `pn` | `NameEntry::is_mutable`, E211 | S12.2.1 |
  | BUG-6 | Parameters reassignable | Same check, E211 | S12.2.1; S9.1.3 snapshot since 2026-08-29 |
  | BUG-7 | Assignment to a captured `var` wrote a nonexistent local | Writable env copy | Superseded by S9.1.4 (Appendix S, S-1) |
  | BUG-8 | `var x = 42; x = "hi"` mismatched the native C type | Static widening to `Item` (`type_widened`) | Kept under MIR Direct (§3.2) |

  The audit also routed type-changing writes on parser-built (arena) containers through a separate allocator path and a `Container::is_data_migrated` flag, because the runtime could not reach the `Input*` that owned them.
- **2026-07-20** — numeric-model amendment: annotated widening narrowed to exact embedding (S-2).
- **2026-08** — C4 ratified into S9 (mutable value semantics); COW Stage 1 landed; plain-parameter snapshots made unconditional (2026-08-29, CW29); the C-text transpiler retired (MIR Direct is the only back end); S16.6.5–S16.6.9 control-form rulings implemented in both front ends (2026-08-24).
- **2026-09** — document updates (PTH30–PTH80) put `output` in Tier 3 beside `put`/`del` (PTH-O11 resolved 2026-09-17); procedure arrow `pn (x) => { … }` (S16.6.7v2, 2026-09-26).
- **2026-10-15** — element construction made `fn` context inside procedures (S12.1.1v3, D7.1.1).
- **2026-10-23** — this consolidation.

---

## Formal Design detail (relocated 2026-11-04)

Formal Design spec 61.0.0 (2026-11-04) keeps only principles and stable
decisions. This section keeps the full text that the old rulings below had
before the rewrite, because their detail belongs to this record. Each
heading gives the old ID and the new ruling. IDs inside the text use the
new numbering. Implementation history:
[`Lambda_Impl_Formal_Design_History.md`](impl/Lambda_Impl_Formal_Design_History.md).

### ex-D6.1.2v2 → D7.1.2

**D7.1.2*** The bit is load-bearing across the runtime: it is the
const-folder's soundness gate, the stream-fusion and parallelization
license (*Polars/DuckDB must trust their UDFs; Lambda verifies*), the
reduction-legality condition, the guarantee that `fn` regions are
suspension-free (no scheduling polls in pure code), and what makes the
JS Promise membrane computable. **No reified effects**: a `pn` call
executes; there is no held, unexecuted effect value. Local mutation and
control inside `fn` do not change its call colour, but the bit alone does
not prove a local read invariant, a loop independent, or evaluation
terminating. Optimizations must preserve sequential local dependencies
and completion; non-escaping live captures may not be lifted into shared
or cached environments. Escaping captures keep the immutable-value
contract of S9.1.4v2. [U26, PD11, K19, K23, Features §3.6;
S12.1.2v3; Design_Procedural §6]

### ex-D6.2.3v3 → D7.2.3

**D7.2.3*** **Closure representation respects capture permissions.**
`fn` captures and every potentially escaping closure retain immutable
`Item` snapshots at creation; captured writes are compile errors. A
proven non-escaping nested `pn` may access the enclosing `fn`'s live local
`var` storage under S9.1.4v2. Sibling procedures refer to the same binding
home, not independent writable copies. One tier-shared analysis decides
capture mode before layout/lowering; an unknown escape is not proof of
confinement. Live storage never outlives the parent invocation and uses
precise roots (D5.3.2, D7.2.4), never a native-stack scan. Escape does not
trigger a late snapshot: snapshot-at-creation semantics must already
hold. Captures outside this confined parent-fn case keep their existing
snapshot representation. Under D1.3 this does not change LambdaJS
capture semantics.
[Design_Procedural §5.3; C4, Box_Unbox, JSI5]
