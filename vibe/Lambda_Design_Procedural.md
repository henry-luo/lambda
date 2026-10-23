# Lambda Procedural Programming — Design

**Revision:** 1 (2026-10-23) — consolidation of the two procedural records into one, brought up to the current rulings.
**Status:** the procedural surface (`pn`, `var`, assignment, `while`, `break`, `continue`, `return`) is ratified and implemented on every tier (interp, jit, auto). Mutable value semantics is ratified; COW Stage 1 is landed and Stage 2 (exclusivity, the capture-assignment error, view-borrow confinement) is partial (§5, §9). The resource model is designed, not built (§7.3). Superseded designs are in Appendix S; open questions are in §9.
**Formal linkage:** effect bit S12.1.1v3, D6.1.1v2; pn-only constructs S12.1.2, S16.6.5; assignment S12.2.1, S12.2.2; mutable value semantics S9.1.1–S9.1.7, S9.2.1–S9.2.4v2, S9.3.1–S9.3.2; COW D4.4.1–D4.4.6; closures D6.2.3v2; control forms S16.6.1–S16.6.9; `return` line rule S16.2.5; script top level S16.7.1, S16.7.6; numeric widening S4.4.1; declared types S11.4.1; error channels S7.4–S7.6; resources S12.4.1v3–S12.4.3; dynamic calls S12.3.2; key domains S8.2.1v4; invalid writes S7.1.3v2; comma decomposition S16.8.7; open issues SO14.
**Spec mapping:** `Lambda_Procedural.md` §1–§5 (the 2026-01-23 proposal) → S12.1.2, S16.6.5, S12.2.1; `Lambda_Proc_Assignment.md` design principles 1–4 → S12.2.1; principle 5 and §4.3–§4.5 → S9.1.6, S9.3.2, S12.2.2; §4.6 and Phase 6 → superseded by S9.1.4 (Appendix S, S-1).
**Scope:** procedural code in Lambda — what `pn` admits, how bindings and assignment behave, the value semantics that keep procedures observably functional, where mutable state may live, and how a procedure reaches the outside world (files, documents, errors, resources) — with the reasons and the history of what was rejected or superseded. The COW machinery, nested-mutation ergonomics, document updates and concurrency each have their own record; this one links to them rather than restating them.
**ID series:** none minted. Rulings are cited by `S#`/`D#`; neighbouring areas keep their own series (C4 in `Lambda_Semantics_Formal.md`, CW# in the COW record, PTH# in the reference record, RG# in the runtime-globals record). Superseded items are numbered S-1… within Appendix S only.
**Consolidates:** `vibe/Lambda_Procedural.md` (2026-01-23, the procedural enhancement proposal: `break`/`continue`, `return`, `output()`, `while`, `var`, and future `assert`/`try`/`defer`/multiple returns) and `vibe/Lambda_Proc_Assignment.md` (2026-02-20, last amended 2026-09-14: the structural-mutation audit, BUG-1–BUG-8 and fix phases 1–7). Code-level anchors of the old C-text transpiler are not carried; the git history keeps them.
**Related:** user doc [`doc/Lambda_Procedural.md`](../doc/Lambda_Procedural.md); COW [`Lambda_Design_Runtime_COW.md`](Lambda_Design_Runtime_COW.md) (CW#); nested mutation [`Lambda_Design_Nested_Mutation.md`](Lambda_Design_Nested_Mutation.md) (CW22–CW28, SO14); document updates [`Lambda_Design_Reference.md`](Lambda_Design_Reference.md) (PTH30–PTH80) and [`doc/dev/Lambda_Document_Updates.md`](../doc/dev/Lambda_Document_Updates.md); C4 mutable value semantics [`Lambda_Semantics_Formal.md`](Lambda_Semantics_Formal.md); no global state [`Lambda_Design_Runtime_Globals.md`](Lambda_Design_Runtime_Globals.md) (RG14); concurrency [`Lambda_Design_Concurrency.md`](Lambda_Design_Concurrency.md); error handling [`Lambda_Design_Runtime_Error_Handling.md`](Lambda_Design_Runtime_Error_Handling.md); control-form syntax [`Lambda_Design_Syntax.md`](Lambda_Design_Syntax.md) §5–§6.

---

## 1. Position: locally imperative, observably functional

Lambda is a pure functional language with a declared, compiler-checked, one-bit effect system: `fn` is pure and deterministic under any schedule, `pn` may have effects, and **`fn` can never call `pn`** — statically where the callee is known, at run time where it arrives as a value (S12.1.1v3). Procedural code is not a second language beside the functional one; it is the same expression language with a small set of statements that only a `pn` body admits.

The governing principle is S9.1.1: `let` is the variable of algebra, `var` the primed variable of program logic. Every mutation is a total function of the old value (`push(b, 2)` ≡ `b′ = b ++ [2]`), so a `pn` is *locally imperative but observably functional*. Nothing a procedure does to its own `var`s can be seen through any other binding, because values never alias (S9.1.2, §5).

*Why a declared bit rather than inferred purity.* The bit is load-bearing across the runtime (D6.1.2): it is the const-folder's soundness gate, the license for stream fusion and parallelization, the guarantee that `fn` regions never suspend, and what makes the JS Promise membrane computable. An inferred bit would let a library change colour silently with its implementation; a declared one changes a caller's contract only when the declaration changes. The accepted price is that effects are not reified: a `pn` call executes; there is no held, unexecuted effect value.

Three boundaries follow from the bit and are easy to miss:

- **A script's top level is `fn` context.** Procedural code belongs in `main()`, run by `lambda run script.ls` (S12.1.1v3, S16.7.1). `lambda run` without a script opens a procedural REPL session whose entries append to one implicit procedure body (S16.7.6).
- **Element construction is `fn` context, even inside a procedure.** Attributes and content of `<elmt …>` evaluate as pure expressions; a `pn` call nested directly in an element literal is rejected statically, or refused at run time when the callee's colour is unknown (S12.1.1v3, D6.1.1v2; USER 2026-10-15). Run the procedure in a preceding statement and build the element from its bound result:

  ```lambda no-run
  // no-run: load_parts is a placeholder procedure
  pn main() {
      let parts = load_parts()
      let file = <file format: 'pdf', parts>
      file
  }
  ```

- **Effect polymorphism is spelled `function`.** A `function` declaration takes its colour from its function arguments and is checked as an `fn` body (S12.1.4v3); it is how a higher-order helper serves both worlds without being written twice.

## 2. The procedural surface

### 2.1 What only a `pn` admits

`break`, `continue`, `return`, `while`, `var` declarations and assignment are `pn`-only; using them in an `fn` is a compile error (S12.1.2). The distinction is made by semantic analysis on the effect boundary, not by grammar (S16.6.5): the reference grammar parses the statements anywhere a statement may stand, and `build_ast` rejects them outside a procedural scope with E224 — the same code as an `fn` calling a `pn`, and the code a module-level `var` draws. A `for` is a comprehension in functional context and an effect loop in procedural context — one node, two meanings, chosen by the enclosing colour.

*Why not a grammar split.* The 2026-01 proposal placed the new statements in a `proc_content` production so that only `pn` bodies could parse them. That made the grammar carry a semantic fact, duplicated every statement-bearing rule, and gave poor diagnostics (a parse error instead of "this is procedural"). Moving the check to semantic analysis (Design_Syntax §5.6) keeps one grammar and lets the error name the repair.

Procedural scopes are opened only by `fn`/`pn` bodies, `while` bodies and event handlers (S9.1.7). A procedure arrow `pn (x) => { … }` is a nested `pn` without a name; its braces are its body, so `return`, `var` and `while` belong there (S16.6.7v2).

### 2.2 Control forms

- **`while` has one node with two spellings**, like `if` and `for`: `while (c) body` or `while c { … }`; `(` immediately after `while` commits to the parenthesized spelling (S16.6.1, S16.6.2).
- **Statements require braces.** `return`, `break` and `continue` are admitted only inside a braced body; every unbraced control body — a paren-form `if`/`for` body, an `else` body, a `case T:` arm, an `=>` body — is an expression position and rejects them. `raise` is an expression and stays valid there (S16.6.6).
- **A procedural block is a statement, never an expression.** A braced block whose top level holds `return`, `break`, `continue`, `var` or an assignment is rejected in every expression position (S16.6.8v2). An `if`/`else` chain or `match` is either a value form or a control form, never a mixture (S16.6.9).
- **A procedure's body is always a braced block.** `pn name(...) { … }` is never `=>`-bodied (S16.6.7v2).

### 2.3 `break` and `continue`

`break` exits the innermost enclosing `for` or `while`; `continue` skips the rest of the body and proceeds to the next iteration. Both are statements, legal only in a braced body (S16.6.6).

Labeled loops (`outer: for …` / `break outer`) were sketched in 2026-01 as a future extension and have not been adopted; nested-loop exit is written with a flag or by moving the inner loop into a `pn` that `return`s (§9, open item).

### 2.4 `return`

`return e` exits the enclosing function with `e`; a bare `return` returns `null`. A `return` inside a `for` or `while` exits the function, not the loop. A function without an explicit `return` returns its last expression (S12.1.2, S2.5). Because `return` takes a following start-token line as its value, `return` ⏎ `42` returns 42; a bare return is `return` followed by a separator or the closing brace (S16.2.5) — the JavaScript restricted-production trap fixed by inversion rather than a special rule.

`return` is how a scoped resource escapes its block, and the escape must be visible in the declared return type (S12.4.2, §7.3).

### 2.5 `main()` and the two run modes

`lambda script.ls` evaluates the script's top level as content (S16.7.1); `lambda run script.ls` additionally calls `pn main()`. `main` is an ordinary `pn` in an otherwise functional module: it does not make the module procedural, and it does not permit module-level `var` (S9.1.7).

## 3. Bindings and assignment

### 3.1 Who may be reassigned

`let` bindings and parameters are immutable; only `var` reassigns; shadowing is allowed (S12.2.1). Assigning to a `let` or a parameter is E211, and so is a write *through* one — `arr[0] = 99` on a `let arr` — since a `let` is final for everything reachable through it (S9.1.1). Multiple declarations share one statement: `var a = 0, b = 1`. Comma decomposition binds several names from one value — `let a, b = expr` positionally, `let a, b at expr` by name — and bracket destructuring patterns (`let [a, b] = …`, `let (q, r) = …`) are rejected by design (S16.8.7).

*Why parameters are immutable.* A plain parameter is a value the caller handed over; letting the callee reassign it would make the parameter list a second, invisible set of locals and would blur the one sharing construct, the `var` parameter (§5.2). Since 2026-08-29 a callee that mutates through a plain parameter mutates its own snapshot, never the caller's value (S9.1.3, CW29); `var` is the sole write-through construct.

### 3.2 The type of a `var`

An **unannotated** `var` may change runtime type on reassignment; an **annotated** `var` constrains every assignment to its declared type (S12.2.1, S11.4.1). A mismatch is E201 (*type check at assignment*), raised by the checked boundary at run time when the value's type is not known statically. Implicit widening at an annotated position is by **exact embedding only** (S4.4.1): `int → float`, `int → int64` and every exact path into `integer`/`decimal` are admitted; `int64`/`u64 → float` and every narrowing need an explicit conversion. *Assignment never silently corrupts data* — the single sentence that the whole 2026-02 audit (Appendix A.2) existed to make true.

*Why widen silently but check annotations.* An unannotated `var` states no contract, so the compiler serves the program as written: it keeps the fast native carrier while every assignment agrees, and moves the binding to a boxed `Item` home when one does not (D2.4.1–D2.4.3). An annotation is a contract the author wrote down; honouring it is the point of writing it. The 2026-02 design admitted any numeric coercion (`int ↔ float ↔ int64 ↔ decimal`) at annotated positions; the 2026-07-20 numeric model narrowed that to exact embedding (Appendix S, S-2).

The widening decision is static and tier-shared. Its known gap: reads typed before the widening assignment keep the initializer's lane, which can leak into derived types ([LR03-36](Lambda_Issue_Ledger.md#lr03-36), PARTIAL).

### 3.3 Assignment targets

The surface has three target shapes: an identifier (`x = v`), an index (`a[i] = v`), and a member (`m.f = v`), composing into paths (`a.b[i].c = v`). There is no compound assignment (`+=`), and decomposition is a declaration form, not an assignment target.

- **A write may update or add only a member admitted by the container's existing key domain, and never changes the container kind** (S9.1.6). `var a = []; a["str"] = 1` raises (S7.1.3v2) and `a` stays an array. Changing kinds is whole-binding replacement: `var a = [1]; a = {value: 1}`. Adding a new field to a map or an open object instance is an ordinary member addition and a shape transition.
- **An invalid write raises.** Out-of-bounds, key-domain and base failures on a member or index write are hard errors through the `T^` channel, never a soft value, `null` or a silent no-op; sequence growth is explicit (`push`, `splice`) (S7.1.3v2). *Reads ask a question; writes issue a command.* The 2026-02 runtime setters logged and returned on every such failure — the silent no-op this ruling forbids.
- **Elements are mutable on both faces** (S12.2.2): `elem.attr = v` is map-field assignment; `elem[i] = v` is child assignment, which keeps content normalized (S2.6.5).
- **Storage follows the value, not the other way round.** A write of a value the container's packed representation cannot hold exactly moves the container to a representation that can — never a truncation, never a reinterpretation of bits (S9.3.2), and the change of representation is unobservable (D2.6.2). The 2026-02 audit found the opposite: an `ArrayInt` slot read back a float's tagged bits as an integer (BUG-1). The fix generalized into the construction ruling: storing a scalar and reading it back keeps its type at every magnitude.
- **Field type changes rebuild the shape.** A map or element field assigned a value of another type gets a new shape entry; shape metadata always describes the stored data (principle 5 of the 2026-02 audit; BUG-3 was a `null` field holding a container under a `null` shape).

### 3.4 `=` never writes a document

`=` is Tier 2: it changes a binding and the value that binding owns, nothing else (PTH55). A document — a file, a `temp.` store, anything reached through a reference and the force step `#` — changes only through the Tier 3 statements `put`, `del` and `output`; `doc#a.b = v` is a compile error naming `put`, and on a non-`var` root `=` is an error, never an implicit `put` (PTH72v2, PTH73). The tiers share no operators. See §7.1.

## 4. Where mutable state lives

**There is no global mutable state** (S9.1.7, RG14). A `var` at module scope is a compile error, and a module-level `let` holding a container cannot be written through. Every mutable root is owned by exactly one of:

- a **`pn` activation** — script state lives in `main`'s `var`s and reaches other procedures only as arguments;
- a **view/template instance** — template bodies are pure, mutation happens only in `on` handlers (S12.1.3);
- an **object instance** reachable from one of those — a `pn` method requires a `var` receiver (S9.1.3).

`fn` functions hold no mutable state at all, and **neither do closures** (§5.3). A spawned task cannot capture a mutable `var` (the concurrency capture rule, `doc/Lambda_Concurrency.md`); state crosses tasks by message.

*Why no globals.* A module that exposes no mutable cell can never couple two importers through hidden shared state, and an analysis never has to ask who else may have written a value. The cost — state threads through parameters — is paid once at the call site and buys local reasoning everywhere else. State that genuinely lives outside the value model (a file, a socket, a document store) is reached through effects, not bindings: *no global mutable state* is not *no global effect* (`doc/Lambda_Procedural.md` §No Global Variables).

## 5. Mutable value semantics

Binding, assignment and construction **copy, observably, for every container kind**; the implementation is copy-on-write, and sharing must be unobservable — a verifiable property, not a convention (S9.1.2). The machinery is the COW record's (D4.4.1–D4.4.6, CW#); this section states only what a procedure author sees.

### 5.1 Copies, not references

`var b = a; b[0] = 9` leaves `a` unchanged. There are no reference cells; structural `==` is the only value equality, and cycles are unconstructible (S9.1.5v2). Placing a value into a container captures it by value at every constructor and insertion point (S9.3.1) — the porting hazard is *fill-after-storing*, which leaves the stored copy empty: fill before storing, or read-modify-write. Iterating a `var` container walks the entry-time value, so mutating it inside the loop never invalidates the iteration (S9.2.3).

### 5.2 `var` parameters: the one sharing construct

`pn f(var a: T)` is an inout borrow: the callee's writes land in the caller's `var` (S9.1.3). Arguments must be `var`s; the same `var` or overlapping paths may not feed two `var` parameters (exclusivity, writer-vs-writer only); `var` parameters are invariant (`var xs: int[]` cannot be passed to `var a: any[]`, S9.2.1). A dynamic call to a `var` signature is rejected, because a value span carries no writable caller location (S12.3.2); for the same reason a `pn` method cannot be taken as a value (S12.3.3v2).

### 5.3 Closures are immutable values

Closures capture by **snapshot at creation**; assignment to a captured name — including interior mutation through it — is a compile error (S9.1.4, D6.2.3v2). State never lives inside a function value.

*Why the closure-counter pattern was withdrawn.* The 2026-02 design (Phase 6, BUG-7) gave each closure a *writable copy* of its captured `var`s, so `let counter = fn() { count = count + 1; count }` counted across calls. It was internally consistent, but it put mutable state inside a function value: two calls of an `fn`-typed value could return different results, which breaks the effect bit's central promise, and the counter's state was invisible to every analysis that reasons about `var` roots. C4 ruled it out; a counter is a `var` in the owning `pn`, or a field of an object instance behind a `var` receiver (Appendix S, S-1). A non-escaping nested `pn` used only in call position may later be allowed direct access to its parent's `var`s — designed, deferred (S9.1.4).

**Implementation lags the ruling.** Today only a captured `let` is protected (E211). A closure that assigns a captured `var` still writes its own environment slot, which is the 2026-02 writable copy. The outer scope is guarded only by E231: after calling such a closure, a read of the outer `var` without an intervening reassignment is rejected. Making the assignment itself a compile error is COW Stage 2 (§9).

### 5.4 Nested mutation and shared records

A deep update spells the path: `t.nodes[i].value = v` writes in place through the `var` root. `var h = t.nodes[i]` is a *copy* — mutating `h` does not reach `t` unless it is stored back — and the compiler binds such place copies as borrows wherever the difference is unobservable (D4.4.4v4, D4.4.5, D4.4.6). Ergonomics for deep updates are owned by the nested-mutation record (SO14, CW22–CW28).

When two owners need to see one record, the idiom is the **handle store**: one container owns each record, every other reference is a key into it, and the store travels as one `var` parameter. Identity that aliasing supplied implicitly becomes data (C4.2e; `doc/Lambda_Procedural.md` §Sharing Mutable State). The AWFY `richards` and `deltablue` ports are the worked examples.

## 6. Loops, early exit and the effect bit together

`while`, `break`, `continue` and `return` exist only where effects do, and that is deliberate rather than incidental: a pure `for` comprehension's meaning is the collection it produces, so an early exit would make its value depend on evaluation order. The functional spellings of the same intents remain available everywhere — a `where` clause or filter instead of `continue`, a first-match search instead of `break`, a `match` or `if` value form instead of a guard `return`.

## 7. Reaching the outside world

### 7.1 Files and documents: `output`, `put`, `del`

`output(v, target, options?)` writes a whole document. It is a procedure-family effect: `pn`-only inside a function, and at a script's top level it runs as its own transaction — the shell case (PTH55). Its format follows the target's extension unless an option says otherwise, `{mode: "append"}` appends, and failure **raises**, so a call is engaged with `^` or a handler (S7.4.5, S7.5.1v2). Under the reference design `output(v, p)` is exactly the whole-document `put p# = v` (PTH54v2), and the finer edits are `put`/`del` inside `open { … }` transactions (PTH60v3, PTH68v3). The file utilities are the `io.*` procedures (`io.copy`, `io.move`, `io.delete`, `io.mkdir`, …); `input`, `exists` and `format` are readers available in both colours.

The 2026-01 proposal specified `output(source, url, format?)` returning `null | error`; both the positional format symbol and the error-as-value return are superseded (Appendix S, S-3).

### 7.2 Errors

A procedure reports failure through Lambda's error channels, not through exceptions: a `T^E` return type licenses `raise`, the caller engages the raised error at the immediate expression with postfix `^`, a `^ { … }` handler, or an `error` match arm, and a `pn` handler boundary also catches system faults (S7.4.1–S7.4.6, S7.5.1v2, S7.6.1v4). The `try`/`catch` block sketched in 2026-01 was never adopted; the handler is an expression, so recovery composes with the rest of the language (Appendix S, S-4).

### 7.3 Resources and cleanup

`open()` acquires a scoped resource and is `pn`-only; a resource auto-closes at the end of its enclosing block, and ownership escapes only by `return` with the escape visible in the declared return type (S12.4.1v3, S12.4.2). There is no `defer`, `with` or `finally` keyword: auto-close is the only user-facing cleanup mechanism (S12.4.3) — *auto-close is `with` without the `with`; close is not just release, it is the last write.* The `defer` statement sketched in 2026-01 is rejected by that ruling (Appendix S, S-5). The resource model R1–R5 is designed, not built (S12.4 Appendix A footnote).

## 8. Decision summary

| Topic | Ruling | Status |
|---|---|---|
| `fn`/`pn` effect bit; top level and element construction are `fn` context | S12.1.1v3, D6.1.1v2 | Implemented; residue in the S12.1.4v3 footnote |
| `break`/`continue`/`return`/`while`/`var` are `pn`-only | S12.1.2, S16.6.5 | Implemented |
| `while` two spellings; statements need braces; procedural blocks are statements; branch homogeneity | S16.6.1–S16.6.2, S16.6.6, S16.6.8v2, S16.6.9 | Implemented (both front ends) |
| `return` takes the next start-token line | S16.2.5 | See its footnote |
| `let`/params immutable; unannotated `var` may change type; annotated `var` by exact embedding | S12.2.1, S4.4.1, S11.4.1 | Implemented; LR03-36 partial |
| Member/index writes stay in the key domain; no kind change | S9.1.6 | Core tiers conformant; editor/host access sites unaudited |
| Element attribute and child assignment | S12.2.2 | Implemented |
| Writes never corrupt a packed representation | S9.3.2, D2.6.2 | Implemented |
| Values copy; COW unobservable | S9.1.2, D4.4.1–D4.4.6 | Stage 1 landed |
| `var` parameters are the sole sharing construct; exclusivity; invariance | S9.1.3, S9.2.1 | Snapshot half unconditional; exclusivity partial |
| Closures snapshot; capture assignment is a compile error | S9.1.4, D6.2.3v2 | Captured `let` writes rejected (E211); captured `var` writes still use the writable copy, guarded by E231 |
| Comma decomposition; no bracket destructuring | S16.8.7 | Implemented |
| No global mutable state | S9.1.7 | Implemented |
| `=` is Tier 2; documents change only by `put`/`del`/`output` | PTH55, PTH72v2, PTH73 | Implemented (PTH30–PTH80) |
| Auto-close resources; no `defer` | S12.4.1v3–S12.4.3 | Designed, not built |

## 9. Open issues

- **Capture-assignment error** (S9.1.4) is not enforced. A closure assigning a captured `var` keeps the superseded writable-copy behavior (S-1), and only the E231 read-after-call guard protects the outer scope (§5.3). COW Stage 2.
- **`var a, b = expr` mutability** — `resolve_var_stam` skips decomposition declarations, so the names may not be marked mutable. Not verified at run time; probe before relying on it.
- **Exclusivity** (S9.1.3): faces 1, 3 and 4 landed (overlapping `var` arguments are E211); face 2 is unreachable behind E229; view-borrow confinement (S9.2.2) and the view-state half of S9.2.4v2 are outstanding.
- **Stale lane of early reads of a widened `var`** — [LR03-36](Lambda_Issue_Ledger.md#lr03-36).
- **A repeated literal map key** keeps both entries; a write updates the first while a read takes the last — [LR03-17](Lambda_Issue_Ledger.md#lr03-17), needs a ruling.
- **Reassigning from a place aliases it** on one tier — [LR12-34](Lambda_Issue_Ledger.md#lr12-34).
- **Nested-mutation ergonomics** — SO14, owned by the nested-mutation record.
- **Not adopted, not rejected:** labeled loops (`break outer`; the AST's label fields serve LambdaJS only), compound assignment (`+=`), an `assert` statement. Each was proposed or asked for and none has a ruling; raise them with the user before building.

## Appendix S — Superseded designs and rulings

- ~~**S-1 · Closures capture by writable copy** (Proc_Assignment §4.6, Phase 6, BUG-7 fix, 2026-02): each closure gets its own mutable `Item` slot per captured `var`; writes inside the closure persist across its calls while the outer `var` stays independent (the closure-counter pattern).~~ Replaced by **S9.1.4** / **D6.2.3v2**: closures are immutable values and capture assignment is a compile error (§5.3).
- ~~**S-2 · Annotated `var` admits any numeric coercion** (Proc_Assignment §4.1.1 rule 3: `int ↔ float ↔ int64 ↔ decimal`, and `INT ↔ INT64` interchangeable in `fn_map_set`).~~ Replaced on 2026-07-20 by exact-embedding widening, **S4.4.1** via **S12.2.1** (§3.2).
- ~~**S-3 · `output(source, url, format?): null | error`** (Procedural §3): a positional format symbol defaulting to `'mark`, extension auto-detection, and failure returned as an error value.~~ Replaced by `output(v, target, options?)`, which raises (**S7.4.5**), takes options as a map (**S17.8.1**), and is the Tier 3 whole-document `put` (**PTH54v2**, **PTH60v3**) (§7.1).
- ~~**S-4 · `try`/`catch` blocks** (Procedural §6.2, future).~~ Never adopted; error handling is the channel model of **S7.4–S7.6** (§7.2).
- ~~**S-5 · `defer` statement** (Procedural §6.3, future).~~ Rejected by **S12.4.3**: auto-close is the only cleanup mechanism (§7.3).
- ~~**S-6 · `pn`-only statements enforced by a `proc_content` grammar production** (Procedural §1–§5, grammar sketches).~~ Replaced by semantic analysis on the effect boundary, **S16.6.5** (§2.1). The grammar sketches' optional trailing `;` and `while (cond) { content }`-only form are likewise superseded by S16.1 separation and the two spellings of **S16.6.1**.
- ~~**S-7 · No field addition by assignment** (Proc_Assignment §4.4.1: "field NOT found: error (no field addition in this phase)").~~ Replaced by **S9.1.6**: a write may add a member admitted by the existing key domain, as a shape transition (§3.3).
- ~~**S-8 · Accumulating by re-`let` inside a loop** (Procedural §1 examples: `let total = total + n` inside a `for` body).~~ Never valid: an inner `let` shadows for its block only and accumulates nothing. The spelling is a `var` and assignment.
- ~~**S-9 · A plain parameter mutated by the callee writes through to the caller** (the AWFY-era behavior the 2026-02 audit inherited).~~ Retired 2026-08-29: plain parameters snapshot unconditionally and `var` is the sole write-through construct, **S9.1.3** (CW29).
- ~~**S-10 · Multiple return values by tuple unpacking** (Procedural §6.4, future: `let (q, r) = divide(17, 5)`).~~ Bracket and paren destructuring are rejected by **S16.8.7**; a procedure returns one value (an array or map when it has several results), and comma decomposition `let q, r = divide(17, 5)` takes it apart (§3.1).

## Appendix A — Implementation notes

### A.1 Current shape

Anchors are symbol names; confirm line numbers against the tree. Surveyed 2026-10-23 from source, not from a test run.

- **Front end.** The C parser handles `while` in `parse_while_statement` and `var` in `parse_var_statement`; `return`, `break` and `continue` are parsed beside them, and only `return` takes an operand. The pn-only rule (E224) lives in `build_ast.cpp`: `resolve_control_statement`, `resolve_while`, `resolve_var_stam` and `resolve_assignment`. E225–E227 are defined but unused for this rule.
- **AST.** The statement nodes are `AST_NODE_BREAK_STAM`, `CONTINUE_STAM`, `RETURN_STAM`, `VAR_STAM` and `ASSIGN_STAM`. `while` is an `AST_NODE_LOOP` with `LOOP_FORM_WHILE` (`AstWhileNode` is a typedef). `resolve_assignment` retags an index or member target as `AST_NODE_INDEX_ASSIGN_STAM` or `AST_NODE_MEMBER_ASSIGN_STAM`. Every assignment is one record, `AstAssignNode`, with `target_entry` and its COW fields (D8.2.2); `AstAssignStamNode` and `AstCompoundAssignNode` are aliases of it, where "compound" means an index or member target, not `+=`. Decomposition is `AST_NODE_DECOMPOSE` (`AstDecomposeNode`).
- **Binding facts.** `NameEntry` keeps the three 2026-02 flags `is_mutable`, `has_type_annotation` and `type_widened`, beside the COW facts (`cow_param_mutated`, `cow_owned`, `is_place_copy`, `var_root_may_share`, …). `resolve_assignment` sets `type_widened` on the first type-changing write to an unannotated `var`; later reads are typed `any`. MIR decides a native-lane `var`'s home once at its declaration (`mir_nested_control_writes_binding` with widening cascade). A `var` passed as a `var` argument is also boxed. T0 stores every binding as an `Item`.
- **Execution.** T0 runs loops in `interp.cpp` and signals `break`, `continue` and `return` through `EvalSignal`. MIR lowers through `transpile_while` and `transpile_assign_stam_core`. Annotated boundaries go through `emit_checked_boundary`, and on T0 through the matching interp check.
- **Paths and COW.** A nested path write runs through `exec_place_assign` on T0, and through `transpile_compound_assignment_item` and the `cow_place_leaf_*` helpers in MIR. Both reach `cow_path_set_impl` / `cow_path_set_inplace`. The COW primitives are `cow_mark_shared`, `cow_bind_var` and `cow_prepare_write`. The Lambda-side write wrappers (`index_assign_cow`, `array_set_cow`, `member_set_cow`, `map_set_cow`) prepare the write before calling the raw setter (D4.4.2).
- **Runtime setters.** `fn_index_set` dispatches by owner kind. A sequence write with a non-integer key raises E201. On an element, an integer key writes a child (`fn_array_set`) and a name key writes an attribute (`fn_map_set`). `fn_array_set` still converts a specialized array in place (`convert_specialized_to_generic`). A field type change in `fn_map_set` retags in place when that is safe, and otherwise moves the container onto a runtime shape-tree target (`container_retype_field`, D3.4.5). `map_rebuild_for_type_change` now serves only JS shapes. A new member is added by `map_extend_open_shape`. `Container::is_data_migrated` is still consulted for parser-built containers.
- **`var` parameters.** At the call site an argument must be a mutable `var` root (E211). Overlapping roots or view bases are E211, and the types must match exactly (E207). A plain `pn` parameter written by the callee is snapshotted (`cow_param_mutated`).
- **Closures.** `finish_capture_analysis` rejects a write to a captured `let` (E211). A write to a captured `var` is stored into the closure env (`owned_item_slot_store`). `scan_invalidated_call` raises E231 for a stale outer read. A `start` capturing a mutable `var` is E221.
- **Element purity.** `colour_walk_visit` switches to functional mode inside element and object literals, so `colour_walk_call` reports E224. Dynamic callees go through the run-time colour guard (`fn_colour_guard`).
- **Output.** `output` is registered at arity 2 and 3 (`pn_output2`, `pn_output3`; append through `pn_output_append`) in `lambda-proc.cpp`. The `io.*` procedures are in `sys_func_registry.c`. Both tiers still handle `AST_NODE_PIPE_FILE_STAM`, the old pipe-to-file statement, but `build_ast` no longer creates it, so the path looks unreachable. Retiring it is a cleanup, not a design question.
- **Fixtures.** Positive fixtures are under `test/lambda/proc/`: `proc_control`, `proc_var`, `proc_var_type_widen`, `while_swap`, `proc_array_type_convert`, `proc_map_type_change`, `proc_element_mutation`, `proc_markup_mutation`, `object_open_instance`, `proc_oob_write_error`, `proc_closure_mutation` (read capture and snapshot independence only), `var_param*`, `cow_*`, `let_finality`, `test_pipe_file` (`output`), `element_prepared_parts`. Negative fixtures are under `test/lambda/negative/semantic/`: `immutable_assignment`, `immutable_interior_assignment`, `immutable_reassign`, `mutation_in_fn`, `proc_stam_outside_pn`, `function_body_var`, `capture_mutation` (E231; no golden), `start_mutable_capture`, `element_calls_pn`, `proc_method_let_receiver`. Run-time negatives under `test/lambda/negative/runtime/` cover element-attribute `pn` calls and typed-container writes (`type_enforcement_*_write`).

### A.2 History

- **2026-01-23 — the procedural enhancement proposal** (`Lambda_Procedural.md`). Added `while`, `var`, assignment, `return` and `output()`; `break`/`continue` parsed but needed `if` statement work. Priority table: all five P0; `assert` and `try`/`catch` P2; `defer` and multiple returns P3.
- **2026-02-20 — the structural-mutation audit** (`Lambda_Proc_Assignment.md`). Found that mutation had been built ad hoc for the AWFY benchmarks and catalogued eight defects, all fixed in phases 1–7 on the then C-text transpiler:

  | Bug | Defect | Fix (then) | Where it stands now |
  |---|---|---|---|
  | BUG-1 | Specialized-array write extracted a mismatched value's tagged bits — silent garbage | Convert the array to generic in place; specialized getters fall back on `type_id` | Generalized as S9.3.2 / D2.6.2 |
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
- **2026-10-15** — element construction made `fn` context inside procedures (S12.1.1v3, D6.1.1v2).
- **2026-10-23** — this consolidation.
