# Lambda Reference Implementation

**Date:** 2026-09-18
**Status:** LANDED (R1–R6); the formal specs are **not yet revised** — see
"Ratification still owed" below
**Design source:** [`vibe/Lambda_Design_Reference.md`](../Lambda_Design_Reference.md) (PTH30–PTH80)
**Normative anchors:** S1.6 (representation is invisible), S1.7 (one symbol, one
concept), S2.1.1v4 / S6.2.1v2 (`path` leaves `symbol`), S2.4.3v3 / S2.4.4
(resolution is not forcing), S5.1.4v2 (identity is data), S7.4.5 (`input`
raises), S9.1 (copy-on-write), S10.1.2 (pipe binds `~`), S16.2.2v2 / S16.2.3v2
(line-start token classes).

> The design record is PROPOSED, not ratified. This document tracks what is
> built, in what order, and what each stage deliberately leaves out.

---

## Staging

| Stage | Rulings | What lands | Status |
|---|---|---|---|
| R1 | PTH47 | `~#` → `~key`; `~word` focal-accessor family | done |
| R2 | PTH30 | `path` disjoint from `symbol`; `reference` alias; S6.2.1 band | done |
| R3 | PTH31–PTH39, PTH53v2, PTH65v2 | `#` force step, fragment sugar, longest-prefix forcing | done |
| R4 | PTH40–PTH45v2 | `&` address-of, node tables, `===` | done |
| R5 | PTH44v2, PTH76 | `temp.` provider and `temp()` constructor | done |
| R6 | PTH55–PTH80 | CRUD statements, write set, `commit`/`rollback`, `open` | done |

Each stage kept `make build` green and `make test-lambda-baseline` at its entry
state (5627/5634, seven pre-existing failures: five `test_js_script_gtest`, one
`proc_proc_markup_mutation`, one `Test262Prelim.RunnerContracts`).

Both front ends agree: `./test/c_s16_conformance.sh` 174/174,
`./test/ts_s16_conformance.sh` 157/157. Regression tests:
`test/lambda/reference_force.ls` (R1–R5) and `test/lambda/reference_crud.ls`
(R6), both verified identical on the T0 and MIR tiers.

---

## R1 — `~key` (PTH47)

`~#` is retired so `#` means force everywhere (S1.7). The accessor becomes the
fused **focal-accessor family** `~word`: `~` immediately followed by an
identifier, validated against a closed table.

- Lexer emits `LAMBDA_TOK_TILDE_KEY` for `~key`; `~~` stays `LAMBDA_TOK_PARENT`.
- An unknown `~word` is a compile error naming the accessor table, never a
  binding lookup (S12.3.7 forward compatibility).
- `~ key` with a space stays two tokens.
- `grammar.js` `current_expr` drops `~#`; the CST verifier follows.
- `~#` is NOT made a dedicated error, contrary to §8: once `#` is the force step
  it lexes as `~` then `#` and means "force the current item", which is what §1
  and §5.1 of the record actually show (`refs |> ~#`).

Migration: `test/lambda/pipe_where.ls`, `test/lambda/typed_array_pipe.ls`,
`test/std/core/operators/pipe_operator.ls`,
`test/fuzzy/lambda/corpus/valid/pipe_expressions.ls`,
`test/test_lambda_parser_poc_gtest.cpp`, `doc/Lambda_Expr_Stam.md`,
`doc/Lambda_Cheatsheet.md`, `doc/Lambda_Formal_Semantics.md`.

## R2 — the `reference` type (PTH30)

`LMD_TYPE_PATH` already ships disjoint from `LMD_TYPE_SYMBOL`; what is missing
is the surface.

- `path` registered as a type-pattern name (today: "unresolved type name
  'path', using ANY").
- `reference` registered as the alias `symbol | path` in the sys type table,
  user-first shadowable (S12.3.7).
- S6.2.1v2 total-order band `… < datetime < symbol < path < string < …`,
  bytewise within the band by canonical spelling (S2.4.2v4).
- `==` across symbol and path stays false by type.

## R3 — the force step `#` (PTH31–PTH39)

- Lexer: `#` is `LAMBDA_TOK_HASH`. Comments are `//` and `/* */`, so `#` is free.
- Parser: postfix at member/index precedence, left-associative, above `^`,
  in `parse_postfix` beside `.`; fragment sugar consumes one dotless step
  (`name`, integer, quoted symbol, `[e]`) when it abuts `#` with no whitespace.
- S16.2.2v3: `#` is a pure continuation token — no prefix role — so a line
  starting `#name` continues the chain above it.
- AST: `AstUnaryNode` with `OPERATOR_FORCE`, reusing the existing unary
  plumbing (rooting, dumps, const folding) rather than a new node shape; the
  fragment lowers to the ordinary member node over it (`p#name` ≡ `(p#).name`),
  so nothing downstream needs a second spelling. `&` is `OPERATOR_ADDRESS_OF`
  and `===` is `OPERATOR_REF_EQ` on the same principle.
- Runtime: one shared `fn_force` reached by both the interpreter and MIR Direct
  (rule 13). Provider paths go through the existing `fn_input1` dispatch
  (PTH53v2: `input(p)` ≡ `p#`); `sys.*` and `temp.*` resolve in memory and are
  total (absence is `null`, S7.1.1v3); a non-reference operand is `error()`.
- Longest-prefix forcing (PTH34) lives in the force helper: it walks the path
  from the root for the longest prefix that names a readable document, forces
  that, and navigates the remaining steps in memory, so `a.b#c.d`,
  `a.b.c.d#`, and `a.b#.c.d` are one value (S1.6).

## R4 — `&`, identity, `===` (PTH40–PTH45v2)

- `&` gains a prefix role at unary tier (`&x.y` ≡ `&(x.y)`, as in C) and moves
  to the S16.2.3v3 dual-role set: after an open-tail statement a line beginning
  `&x` is a syntax error repaired by `;`.
- `===` is a new token at the equality tier, in the S16.2.2v3 continuation set.
- Identity carrier (DO25, closed): no per-node storage. A per-evaluation
  **document context** maps a location to its head root and generation counter,
  and each document owns a **node table** keyed by container pointer giving
  `(parent, key, generation)`. `&` walks that table to the root and spells the
  path.
- `&` is total: scalars, runtime literals, and local COW copies answer `null`
  (PTH41, PTH52v3).
- `a === b` ≡ `&a != null and &a == &b` (PTH45v2); in practice pointer equality,
  sound because document nodes never move and a `commit` always creates new
  nodes. Identity-less operands compare false, never an error (S1.9).

## R5 — `temp.` documents (PTH44v2, PTH76)

- `temp` is an in-memory provider root beside `sys`.
- `temp(name, content)` creates the document immediately — outside the write
  set, like `io.mkdir` — and returns its head; it raises if the name exists.
  `temp(name)` returns the existing head, creating an empty document (root
  `{}`) if there is none.
- `temp(` and `temp.` are lexically distinct, so the call and the scheme root
  coexist (S1.7 is not at stake); `sys.temp` is unrelated.
- Documents live for the evaluation's document context (SO20).

## R6 — the three tiers (PTH55–PTH80)

Statements `put`, `del`, `commit`, `rollback` and the block form of `open` join
the barred binding names (S16.10.1); `before`, `after`, `into` are clause words
and stay bindable.

- `put target = v` upserts; `put v before t` / `put v after t` insert at a head
  node; `put v into t` adds to a container; `del t` removes. Edits comma-join
  into one statement in written order (PTH60v3).
- A CRUD statement builds a **write-only next version** (PTH61): no
  read-your-writes, value operands read the head.
- Inside `open { }` every CRUD statement across all documents forms one write
  set; outside one, each statement is a one-statement transaction that
  autocommits at once (PTH63v2).
- `commit` applies the log in program order, validates key domains then
  (S9.1.6), stamps the created nodes with the next generation, and swaps the
  head; the first rejection rolls the whole set back and raises.
- Targets are head-anchored (PTH71v2): the write set holds node pointers, and
  an anchor a later commit replaced raises at `commit` as a stale-anchor
  conflict.
- MVCC (PTH64v2): an iteration holds the version its source forced, as an
  ordinary value; `commit` never waits and never raises for a cursor.
- `open target { }` / `open v = target { }` is the bounded transaction; the
  alias is a reference with `#` implied (PTH75v3) and CRUD targets are confined
  to the opened document (PTH80). Nesting raises (PTH-O15, deferred).
- Tiers share no operators (PTH72v2): `=`/`push`/`splice` stay Tier 2 and
  error on a non-`var` root with a diagnostic naming `put`; `put`/`del`/
  `output` are Tier 3 and reject a `var` root with one naming `open`.

---

## Deliberately out of scope

- PTH-O3 (id-attribute fragment spelling) and PTH-O5 (which attribute is the
  id): the positional path is the baseline identity.
- PTH-O10 (`&` on a parameterized-input document).
- PTH-O13 (error site under deferred reading): the force checks reachability
  and raises there; content errors raise at the force too, since nothing yet
  defers.
- PTH-O15: `open` nesting stays a compile error.
- PTH-O18 (set-oriented `for … put` header): the block-bodied form covers it.
- Remote transport and the resolver/mount model (PTH16–PTH19).


---

## What the migration actually cost

§8 of the design record asked for a grep before ratification. It found more than
the two test scripts it predicted, and two collisions it did not.

**Binding-name collisions** (PTH-O8 bars `put`, `del`, `commit`, `rollback`,
`open`; renamed here):

| Site | Was | Now |
|---|---|---|
| `lambda/package/dom/editing.ls` | `pub pn commit(elem)` | `commit_change` |
| `lambda/package/dom/form.ls` | `editing.commit(~)` | `editing.commit_change(~)` |
| `test/lambda/proc/var_array_param_borrow.ls` | `pn put(…)` | `put_slot` |
| `test/lambda/proc/cow_rmw_sibling_borrow.ls` | `pn put(…)` | `put_slot` |
| `test/lambda/proc/tune26_split_string_lane.ls` | `var open` | `parts` |
| `lambda/package/math/render.ls` | `let open` | `open_brace` |
| `lambda/package/graph/graphviz/markers.ls` | `open` parameter | `is_open` |
| `test/lambda/editor/multi_node_selection.ls` | `let del` | `deleted` |

**`commit` already ships as a sys function.** `SYSFUNC_EDIT_COMMIT` /
`SYSFUNC_EDIT_COMMIT1` are the editor's `commit()` / `commit(label)`, used by
`test/lambda/edit_bridge.ls`. The design did not list this. Resolved without a
ruling change, by the rule the codebase already applies to `type` / `type(x)`:
`commit` heads the transaction statement only when the next token is not `(`,
and reads as the sys call everywhere else. It stays barred as a binding name,
exactly as `type` is.

**Data names were the large fallout.** S16.10.2 says data-name positions admit
every keyword spelling, but the predicate sets enumerate keyword tokens
explicitly, so five new keywords silently broke:

- `token_is_name_word` did not admit them, so `{open: true}`, `.open`,
  `<del …>` and an `open:` object-type field all stopped parsing — which took
  out the whole `radiant`/`dom` module interface (`open: bool`, `open: fn()`)
  and 161 tests with it.
- the `on <name>(…)` event-name position used the strict identifier predicate,
  so the DOM package's `on commit(evt)` change-on-blur hook stopped parsing.
  Widened to `token_is_name_word`: an event name is a data name.

---

## Deviations from the record, and why

- **PTH74 (the block's lexical relative base) is NOT implemented.** `\.a.b`
  inside an `open` block still resolves against the evaluation's base, not the
  opened target. The block's other two effects — the alias and the PTH80 target
  confinement — do ship.
- **PTH46 (iteration takes a value) is NOT implemented.** `for (v in path)` and
  `len(path)` still resolve the path, and `sys.os.name` still auto-resolves
  through `fn_member`. R1 says nothing forces implicitly, but §8 does not list
  this as migration fallout and removing it breaks every existing `sys.*` read.
  Left as-is deliberately; it needs a ruling.
- **PTH48 (forcing a symbol)** resolves to `null`. The resolver has no
  identity/namespace table yet, so the read is total and empty — never a
  lexical binding, a module export, or the sys-func registry (S1.8).
- **PTH-O9**: `&` prints the location only, as ruled. The generation is not
  stored per node at all — it lives in the document context's node table.
- **`put doc#items[3] = v` uses the written POSITION** (PTH70v4 says a location
  in a sequence is a position). A bare node target (`del v`, `put v = x`) and
  `before`/`after` resolve the node's position at commit instead (PTH71v2).

## Ratification still owed

The design record is PROPOSED and the formal specs are unchanged. What ships
now needs the §"Proposed spec linkage" revisions to become normative:
S2.1.1v4 / new S2.4.6 / S6.2.1v2; new S10.6 and S10.6.3; S5.1.4v3, S9.1.5v3,
D2.6.8v2 (closing SO41 and DO25, revising SO39); new S9.4 with S14.3.1v2, the
S12 effect table, and S2.4.4v2; S10.1.2v2 / S10.1.5v2; and the S16.2.2v3 /
S16.2.3v3 line-start classes. `vibe/Lambda_Design_Reference.md` also needs its
status line moved off PROPOSED once those land.
