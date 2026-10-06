# REPL — Finishing the Interpreter-Backed Session

**Date:** 2026-10-06 (rev 3 — RULED and IMPLEMENTED)
**Status:** **DECIDED 2026-10-06 (user ruling RI1–RI6, §5); Phases A–C implemented 2026-10-06 on branch `worktree-repl-interp` (§8).** The persistent T0 session (`interp_repl_session_*`) is the only REPL engine. Rulings: **S16.7.4–S16.7.6** (semantics 57.1.0) and **D8.1.1v17** (design 25.0.0).
**Design authority:** `doc/Lambda_Formal_Design.md` — D8.1.1v17 (tiers, selector, C parser, the REPL session), D7.2.1/D7.2.2 (module unit, import binding), D5.3.3 (module slab root), D4.6.2v2 (REPL shares module table + NamePool); `doc/Lambda_Formal_Semantics.md` — S16.7.1 (top level is content), S16.7.4–S16.7.6 (echo, no rebinding, session kinds), S16.2.3v3 (entry separation), S2.5.4v2 (declarations produce no item).
**Working design:** `vibe/Lambda_Design_Ast_Interpreter.md` §8 (AI20), `vibe/impl/Lambda_Impl_Ast_Interp (done).md` P4, `vibe/Lambda_Repl.md` (stale — describes the retired replay model), `vibe/idea/Clojure_REPL.md` Part 2 (ergonomics roadmap).
**Ledger:** LR01-7 and LR01-18 FIXED 2026-10-06 and moved to the fixed ledger.

---

## 0. Summary

The premise "the REPL re-evaluates the whole input on every line" is **no longer true on the shipped default**. Since the P5 selector flip (`LAMBDA_EXEC_BACKEND` unset → `AUTO`), `run_repl` (`lambda/main.cpp:1112`) opens one `InterpReplSession`, and every completed entry is parsed, built, planned and executed **as a fragment** against the retained Script, global `NameScope` and module slab (`lambda/runtime/runner.cpp:2386`). Verified on 2026-10-06: `log.txt` shows one `interp: executed script='<repl-session>' nodes=N` line per entry with `N` equal to the entry's own node count, and functions defined in the session promote to MIR satellites like any other (`satellite` lines present).

Whole-history replay survives in exactly three places, and that is what keeps the premise alive:

| Where | What |
|---|---|
| `lambda/main.cpp:1133` | `LAMBDA_EXEC_BACKEND=jit` (and `--tier=jit`) still runs the 2024 model: append to `repl_history`, `run_script_mir` the whole buffer, byte-truncate on error, prefix-diff the output. |
| `lambda/main.cpp:1137` | If `interp_repl_session_init` fails the loop silently falls back to the same replay. |
| `vibe/Lambda_Repl.md`, `doc/dev/lambda/LR_01 §6`, ledger LR01-7/LR01-18 | The documentation still describes replay as *the* REPL. |

So the work is not "move the REPL onto an interpreter" but **finish the move**: retire the replay path, fix the defects the session has today, and then use the live session (scope + slab) for the REPL services the replay model could never offer cheaply.

## 1. What the session does today (verified 2026-10-06, debug build)

Session driven over stdin; `>` is the non-tty prompt.

| Entry | Result | Assessment |
|---|---|---|
| `let x = 10` → `x * 2` | `null`, `20` | persists; echo of `null` is LR01-18 |
| `let m = {a: 1, b: [1,2,3]}` → `m.b[1]` | `2` | ok |
| `for (i in 1 to 3) i * i` | `1 4 9` | ok |
| `fn sq(n) { n * n }` → `sq(7)`; `g(1)`,`g(2)`,`g(3)` | `49`; `2 3 4` | ok; third call promotes a satellite |
| `import math` → `math.sqrt(16)` | `4` | system module ok |
| `let f = fn(a) { a + 1 }` | E100 with caret, "rolled back" | correct diagnostic (anonymous `fn` is an arrow) |
| `let x = 2` after `let x = 1` | E209 duplicate definition, rolled back | **gap G3** |
| `fn f(a) {…}` twice | E209 printed **twice**, rolled back | **gap G3** (+ duplicate print) |
| `g(1)` with `g` undefined | bare `error` | **gap G2**: no diagnostic; `jit` prints "undefined variable 'g'" |
| `1/0` | `inf` | consistent with S4 numerics |
| `import m: .temp.replmod` → `m.twice(4)` | `null` then `interp: call target is not a function (type 1)` + `error` | **gap G4**: the import is accepted, the binding is dead |
| `var v = 1`, `v = v + 1`, `p()` for `pn p` | E224 (`var`/assignment/pn call only inside `pn`) | consistent with S16.7; **gap G5** for a stateful workflow |
| `let x =` (Enter) | E100, discarded | **gap G6**: parser says INCOMPLETE but the REPL only counts brackets |
| `<div class:"c"; "t">` | E100 | correct (`,` between attributes and content) |
| runaway recursion | `error` after depth budget | recovery frame works; no Ctrl-C during an entry (**gap G7**) |

Mechanics confirmed: a rejected entry restores scope tail, const/type list lengths, source length, AST index and slab snapshot (`runner.cpp:2404`–`2515`); `clear` releases the module root and Script (`runner.cpp:2322`); the slab grows geometrically with one registered root range (D5.3.3).

### 1.1 Gap list

| ID | Gap | Site |
|---|---|---|
| **G1** | `jit` tier and init-failure fall back to whole-history replay; two REPL engines, two output models | `main.cpp:1126`–`1300` |
| **G2** | Runtime errors inside an entry print nothing but `error`; T0-unsupported entries print only "rolled back" (the reason goes to `log.txt`: `interp-repl: rejected fragment node=…`) | `main.cpp:1209`–`1225`, `runner.cpp:2466` |
| **G3** | Redefinition is E209 — **ruled correct (S16.7.5)**; the only defect is that a `fn` redefinition prints E209 twice | `build_ast` duplicate check against `root->global_vars` |
| **G4** | File imports after session start are accepted but produce a dead binding (P4 text says "deliberately rejected" — they are not rejected) | `interp_plan_repl_fragment` has no cone load; no rollback for a half-initialised import |
| **G5** | No procedural entries: `var`, assignment, `pn` calls are E224 at top level | S16.7 — needs a ruling, not a fix |
| **G6** | Completeness is bracket counting; the direct parser's `LAMBDA_PARSE_INCOMPLETE` is unused | `main-repl.cpp:72`–`158` |
| **G7** | SIGINT only observed between entries; a long-running entry cannot be interrupted | `lib/terminal_device.c:51`, no poll in `interp.cpp` |
| **G8** | Declaration-only entries echo `null` (LR01-18) | `main.cpp:1223` |
| **G9** | Per-entry snapshot copies the whole slab and registers a root range: O(bindings) per entry | `runtime-state.cpp:578` |
| **G10** | Docs/ledger describe the replay model | `vibe/Lambda_Repl.md`, LR_01 §6, LR01-7 |

## 2. Goal and scope

**Goal.** One REPL engine — the persistent T0 session — with the same semantics as a script file plus the three things a REPL needs that a file does not: rebinding a name, interrupting an entry, and inspecting the live environment. Per-entry latency flat in history length (P4's stated gate, still unmeasured on release).

**Out of scope here.** An evaluation-server protocol and editor integration (`vibe/idea/Clojure_REPL.md` Phase 2–3). §3.9 only records that nothing proposed here blocks them and that `InterpReplSession` is already the UI-free seam they need.

## 3. Design

### 3.1 One engine (G1)

- `run_repl` always uses `InterpReplSession`. The `repl_history` / `last_output` buffers, `run_script_mir(repl_history)` and the prefix-diff printer are deleted. `check_statement_completeness` keeps only the bracket fast path (§3.6).
- The tier selector keeps its meaning for *satellites*, not for the REPL driver: `auto` promotes hot session functions (today's behaviour), `interp` never promotes, `jit` promotes at the first call (threshold 1) rather than compiling a whole-history module. If `jit` must keep "eager whole-module" semantics for files, that is unaffected — the REPL simply has no whole module to compile.
- `interp_repl_session_init` failure is a startup error ("REPL unavailable: …"), not a silent fallback.
- **No Tree-sitter anywhere on the REPL path** (RI6). Verified 2026-10-06: `main-repl.cpp`, `run_repl` and the session code in `runner.cpp` reference no `ts_*`/`TSParser` symbol and do not consult `LAMBDA_PARSER`; the D8.5.1v7 carve-out that allowed the reference parser for the REPL is struck in D8.1.1v17. Completeness (§3.6) and every diagnostic come from the C parser.

### 3.2 Entry lifecycle and the diagnostics contract (G2)

`interp_repl_session_eval` returns a status, not an `Item` alone:

```
enum ReplEntryStatus { REPL_ENTRY_OK, REPL_ENTRY_INCOMPLETE, REPL_ENTRY_REJECTED, REPL_ENTRY_FAILED };
ReplEntryStatus interp_repl_session_eval(InterpReplSession*, const char* source, Item* out);
```

| Status | Meaning | Session state | What the user sees |
|---|---|---|---|
| OK | entry built and ran | published | echo (§3.7) |
| INCOMPLETE | parser returned `LAMBDA_PARSE_INCOMPLETE` | untouched | continuation prompt |
| REJECTED | parse / build / plan / T0-support failure | restored | the diagnostics, once |
| FAILED | the entry ran and produced an error Item (`^` propagation, fault, depth) | restored (slab snapshot) | the error's message, then "entry rolled back" |

Contract: **every non-OK status prints its reason on stderr exactly once**, from the session, not from the driver. Specifically:

- T0-unsupported node (`interp_scan_supported` reject): `error: <construct> is not supported in the REPL yet` naming the node kind, instead of the `log.txt`-only line. Unsupported entries are *not* compiled by any other means (§5 RI5).
- Runtime error Item: `err_print` of the error's payload (today's bare `error` comes from `print_root_item` on an error Item whose message was never surfaced). The same error the `jit` path prints as "undefined variable 'g'" must reach the user.
- The duplicate E209 print (second row of G3) is a double report from build + finalize; collapse to one.

### 3.3 Redefinition (G3) — ruled: stays an error (RI2, S16.7.5)

A later top-level `let`/`fn`/`pn`/`type` of a name already bound in the session is E209, exactly as in a file; the session top level is one scope. `clear` is the way to start over. The current behaviour is therefore correct; the one change is diagnostic hygiene: a `fn` redefinition reports E209 twice (build + finalize) and must report once (§3.2).

Rejected alternatives, recorded for the hot-reload question (AI goal g5): append-with-shadow (new slot per rebinding, earlier captures keep the old value) and late binding (overwrite the slot so existing callers see the new definition). Both were declined on 2026-10-06; late binding would in any case call satellites built against a stale contract.

### 3.4 Procedural entries (G5) — ruled (RI3, S16.7.6)

S16.7.1 makes the top level `fn` context, so a functional session cannot hold `var`, assign, or call a `pn`. Two session kinds, selected the way files already are:

| Invocation | Session kind | Entry context | Echo |
|---|---|---|---|
| `lambda` (default) | functional | top-level content, exactly as `lambda script.ls` | the entry's value (§3.7) |
| `lambda run` (no file) | procedural | each entry is a statement sequence appended to one persistent implicit `pn` body; `var` bindings live in module slots like `let` | the value of a trailing expression statement, else nothing |

A procedural session **warns at start** that entries carry out effectful changes (one line after the banner, e.g. `Procedural session: entries run with effects (file, network, process state).`), so the kind is never mistaken for the functional default.

The fragment builder gains a scope-kind flag (procedure body vs content) and the planner assigns `var` slots as it does for `pn` locals, but in `BINDING_STORAGE_MODULE`. Rollback is unchanged: the slab snapshot already covers `var` slots. No new language surface: a procedural session is "the body of `main` typed one statement at a time".

### 3.5 Imports in a session (G4)

Today a file import inside an entry passes build, gets a slab slot, and is never loaded. Two acceptable states, the first being the minimum:

1. **Reject with a message** — `error: import of a file module is not supported in a REPL entry yet; use .load` — until (2) lands. Rejection happens in the fragment scan (an `AST_NODE_IMPORT` whose target is not a system module).
2. **Cone load as a transaction** — resolve and prebuild the import cone through the ordinary module path (`module_ast_prebuild`, D8.5.1v4), run the cone's initialisation *before* the entry's slab snapshot is taken, and on failure release the cone's module states (`lambda_module_state_release`) and restore scope/source as for any rejected entry. The session already releases exact module roots on `clear`, so the release half exists.

### 3.6 Completeness via the parser (G6)

Keep `has_unclosed_brackets` as the zero-cost fast path. When brackets balance, **do not pre-check**: submit the entry; if the session returns `REPL_ENTRY_INCOMPLETE` (parser status `LAMBDA_PARSE_INCOMPLETE`, e.g. `let x =` or a trailing binary operator) show the continuation prompt and keep collecting. The parse-failure path already restores every retained structure, so a probe costs one failed parse and no state. `STMT_ERROR` and `print_repl_syntax_error` ("Syntax error.") disappear; the parser's own caret diagnostics are the syntax error report.

### 3.7 Echo (G8) — ruled (RI1, S16.7.4)

An entry whose top-level nodes are **all declarations** echoes nothing. Any other entry echoes `print_root_item` of its result, including a literal `null`. Rationale: S2.5.4v2 says declarations produce no item, so there is nothing to echo; printing `null` for `let x = 1` misreads "no item" as "the item `null`". The fragment is at hand (`parsed_root->child` list), so the test is a walk over the entry's top-level node kinds, not an output diff.

### 3.8 Interrupt (G7) — ruled (RI4, D8.1.1v17)

`terminal_device` already records SIGINT in `terminal_interrupted`. Expose a read (`terminal_interrupt_pending()` → clears and returns), and poll it in T0 at the places that are already safe points: loop back-edges, call entry, and the satellite-publication check. On a pending interrupt the walker raises a fault through the existing recovery frame (`LAMBDA_FAULT_INTERRUPTED`, new code in the fault table), which the session turns into `REPL_ENTRY_FAILED` + rollback — the same path a depth overflow takes today. Satellite (MIR) code does not poll in this round: an interrupted entry that is inside a promoted function stops at its next T0 safe point, which is the function's return. Under `interp` tier the interrupt is exact.

### 3.9 Services on the live session

Everything below reads the session's `NameScope` + slab, which replay could never offer without re-running history. Cheap once §3.1–3.3 land; listed so the phases can order them, not as part of the core change.

- `.env` — walk `root->global_vars` (latest entry per name), print name, declared/inferred type (`NameEntry::type`), and value from the slab slot.
- `.type <expr>` — build the fragment, print `fragment->type`, do not run it (rollback path, no snapshot).
- `.load <file>` — read the file, submit it as one entry (so a failing file rolls back atomically); `.save <file>` — write `script->repl_source`.
- `.time <entry>` — wrap the entry with the existing `LAMBDA_PROFILE` phase clock.
- Tab completion — candidate list = keywords ∪ system functions ∪ names in `global_vars`; `terminal_session_readline` needs a completion callback (none today).
- History file — `terminal_session` has no persistence today; `~/.lambda_history` on open/close.
- Eval server — `InterpReplSession` plus the status enum of §3.2 is the whole evaluation API an nREPL-style server needs; sessions are independent Scripts/module ids already (`clear` proves it).

### 3.10 Snapshot cost (G9) — leave as is

`lambda_module_state_snapshot` copies `var_count` Items + payloads and registers a root range per entry. At 10 000 bindings that is 160 KB of memcpy per entry — well under a millisecond — and the copy is what makes rollback trivially correct. A write-journal (record old values of slots the entry writes) is the eventual replacement if the C3 latency driver ever shows the snapshot; not before.

## 4. Verification

- **Existing gate:** `test/test_lambda_repl_gtest.cpp` drives `lambda.exe` over a pipe. Extend with one case per gap: undefined-name diagnostic (G2), unsupported-construct message (G2), redefinition of `let` and of `fn` is E209 reported exactly once (G3), file import rejected/landed (G4), procedural session `var`/assignment/`pn` call plus the start-up warning (G5), `let x =` continuation (G6), Ctrl-C during a `pn` `while true` under `--tier=interp` (G7), declaration-only entry echoes nothing (G8), and a grep-level guard that the REPL objects link no Tree-sitter symbol (RI6).
- **Tier matrix:** every REPL case runs under `auto`, `interp`, `jit` and must be byte-identical (the SI3 discipline, extended to the REPL driver, which the replay path never satisfied).
- **Latency:** `make interp-bench` C3 (history 10 / 100 / 1000) on a **release** build (rule 10); gate = per-entry median flat within noise across the three sizes. P4 claimed this gate and did not measure it.
- **GC stress:** the P4 forced-collection run (`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`) repeated after redefinition, since shadowed slots must stay rooted for old closures.

## 5. Rulings — DECIDED 2026-10-06 (USER)

| ID | Question | Ruling | Spec |
|---|---|---|---|
| **RI1** | What does a REPL entry echo? (LR01-18) | Declaration-only entries echo nothing; every other entry echoes its value, `null` included (§3.7). | S16.7.4 |
| **RI2** | Is redefinition at the session top level allowed? | **No — stays E209.** The proposed shadowing was declined; the session top level is one scope (§3.3). | S16.7.5 |
| **RI3** | May a REPL session be procedural? | `lambda` opens a functional session; `lambda run` with no file opens a procedural session (implicit persistent `pn` body) **and warns the user that entries carry out effectful changes** (§3.4). | S16.7.6 |
| **RI4** | May an entry be interrupted, and how is it reported? | SIGINT → fault through the recovery frame → entry reported failed and rolled back (§3.8). | D8.1.1v17 |
| **RI5** | What happens to an entry T0 cannot run? | Rejected with a named-construct message; never compiled as a per-entry MIR module. T0 coverage is the fix. | D8.1.1v17 |
| **RI6** | Retire whole-history replay and the reference-parser carve-out? | Yes. **Nothing on the REPL path may depend on the Tree-sitter parser.** `jit` in a session means "promote at first call"; no whole-module compile. | D8.1.1v17 (D8.5.1v7 carve-out struck) |

## 6. Phases

| Phase | Content | Gate |
|---|---|---|
| **A — one engine, honest diagnostics** | §3.1, §3.2, §3.6, §3.7 (G1, G2, G6, G8); delete replay; status enum; docs §7 | repl gtest green on three tiers; LR01-7, LR01-18 closed |
| **B — session semantics** | §3.4 procedural session + warning, §3.5 (state 1 then 2), §3.8 interrupt, single E209 report (G3, G4, G5, G7) | new gtest cases; GC stress; release C3 latency table published in this doc |
| **C — services** | §3.9: `.env`, `.type`, `.load`/`.save`, `.time`, completion, history file | manual + gtest for each command |
| **D — server** | out of scope; see `vibe/idea/Clojure_REPL.md` Phase 2 | — |

## 7. Documents to update when this lands

- `vibe/Lambda_Repl.md` — rewrite around the session model (today it documents buffer replay and Tree-sitter completeness, both gone).
- `doc/dev/lambda/LR_01_*.md` §6 — the REPL loop section describes `run_script_mir(repl_history)`.
- `vibe/Lambda_Issue_Ledger.md` — LR01-7 (replay) and LR01-18 (echo) close; `vibe/impl/Lambda_Impl_Ast_Interp (done).md` P4 text "imports are deliberately rejected" corrected.
- **Landed 2026-10-06 with the rulings:** `doc/Lambda_Formal_Design.md` D8.1.1v17 (25.0.0) — the REPL session, replay retired, the D8.5.1v7 reference-parser carve-out struck, RI4–RI6; `doc/Lambda_Formal_Semantics.md` S16.7.4–S16.7.6 (57.1.0) — echo, no rebinding, session kinds. Both carry `*` footnotes until Phases A–B land; flip them then.
- `doc/Lambda_Reference.md` / tutorial REPL samples — the website quickstart avoids `let` because of LR01-18; it can stop avoiding it.

## 8. Implementation record (2026-10-06)

| Gap | Resolution | Site |
|---|---|---|
| G1 | Replay and the init-failure fallback deleted; `jit` becomes `auto` with first-call promotion (`interp_set_func_jit_threshold_default`) | `main.cpp` `run_repl` |
| G2 | `ReplEntryStatus`; one transaction (`ReplEntryTxn`) with a single rollback; each diagnostic printed once by the session; unsupported constructs and imports named | `runner.cpp` |
| G3 | E209 kept (S16.7.5). The double report was a builder defect: predeclare and function resolution identified a declaration by start byte, so a duplicate registered twice — and in the REPL two entries' `fn f` both start at byte 0, so a duplicate was not reported at all and the entry spun. Now identified by node (`lookup_declaration_in_current_scope`) | `build_ast.cpp` |
| G4 | State 2 of §3.5: entry imports initialize their cone first; initialized modules are recorded per session | `interp.cpp` `interp_init_repl_fragment_imports` |
| G5 | Procedural session: scope `is_proc`, colour walk `module_is_proc`, top-level frame `proc_handler` | `runner.cpp`, `build_ast.cpp`, `interp.cpp` |
| G6 | Bracket fast path, then the parser's INCOMPLETE | `main-repl.cpp`, `runner.cpp` |
| G7 | SIGINT armed per entry; `LAMBDA_FAULT_INTERRUPTED` raised at call entry, every back-edge and both fast integer loops | `interp.cpp`, `main.cpp` |
| G8 | Echo unless every item is a declaration or statement | `runner.cpp` |
| §3.9 | `.env`, `.type`, `.time`, `.load`, `.save`; Tab completion through the editor's existing request/reply protocol; history file | `main.cpp`, `main-repl.cpp`, `terminal_host.cpp`, `lmd/package/io/terminal.ls` |

Defects found and fixed on the way:

- The error formatter left its buffer unterminated after a caret line on the source's last line, so diagnostics printed stray bytes (`lambda-error.cpp`).
- A grown module slab is sealed; restoring the plan count after a failed entry made the next entry fail with "sealed layout changed". The transaction keeps the grown count.

Not done, and why:

- Undefined names and some operator type errors complete with a bare error that carries no diagnostic, in files too; whether an unbound name is a compile error needs a ruling.
- Member completion after `.` needs the receiver's static shape.
- §3.10 snapshot cost was left as designed; the release C3 latency table is still to be measured.
