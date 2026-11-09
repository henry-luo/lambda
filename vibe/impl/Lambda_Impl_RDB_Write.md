# Scoped SQLite transactions and parameterized SQL

**Status:** core transaction semantics ruled; API and security details under design. **Updated:** 2026-11-03. **Purpose:** supply durable company accounting to the sibling Lambda Script accounting app. **Scope:** a single local SQLite database; no hosted database or production-readiness claim in this milestone.

## Current checkpoint

- User chose a Lambda-facing scoped SQLite transaction with parameterized SQL, automatic commit on success, rollback on error or cancellation, read-your-writes and rollback-only after a failed statement (S12.4.4v2; 2026-11-03). The user also chose the existing open-block form and an error on rollback-only exit. The accounting application and domain rules remain Lambda Script.
- `input(path, 'sqlite')` reads and closes its connection in the call; no Lambda SQL-write API exists. `lib/rdb_sqlite.c` can open read/write natively, but that is not a script API or evidence that resource scoping, transactions, authorization, type conversion or crash recovery are complete.
- S12.4.1v3–S12.4.3 specify resource acquisition, block lifetime and failure cleanup. S12.1.3v2 keeps reactive rendering pure. PTH61, PTH63v2 and PTH66v2 (no corresponding S#/D# located) specify write-only next versions and success/rollback behavior for **document update** blocks. SQL transaction behavior is ruled by S12.4.4v2 and does not override those document rules.
- `rdb_host.cpp` registers RDB connections as native-owned and refuses bound security callers. The RDB design's BLOCK ownership and statement tracking are future work. A user decision on behavior and actual scoped-resource implementation are prerequisites for connecting durable app storage.
- Implemented native SQLite transaction primitives in `lib/rdb_sqlite.c` / `lib/rdb_sqlite_tx.h`: host-registered connection, foreign keys, one-statement execution, length-aware text/blob parameters, an SQLite authorizer, read-your-writes, rollback-only tracking and close on finish. Fourteen focused tests pass, including pre-open authorization, same-thread registry teardown while a row callback is active and deferred closing across multiple holds; this is not yet a Lambda-facing API. The complete RDB suite passes 166 tests outside the sandbox, with two live-server tests skipped because their URIs are not configured.
- Outstanding: the acquisition expression, SQL operation names, nesting, resource escape, app authorization and Lambda type mapping remain open; do not infer new language rulings. A focused reproducer `temp/accounting-open-unwind-probe.ls` confirms that an error leaving an `open` block and caught by its caller rolls back in the interpreter but leaves the transaction open in MIR; the next `open` raises a nested-open error. The existing MIR open-block error path must gain scope-exit cleanup before any script can acquire this connection. Complete both interpreter and MIR integration, with on-disk cancellation, escape, GC and failure-path tests, before connecting real company data. The native connection hold only protects synchronous callback reentrancy; it is not thread synchronization, and asynchronous cancellation/teardown remains unimplemented.
- Next: complete the Lambda-facing open-block bridge and resource ownership, starting with scope cleanup on every control-flow exit; then add Lambda integration goldens and run the mandated baselines. Keep the app in sample mode until qualification.
- Baseline evidence: the first sandboxed aggregate passed 8,706/8,716; four server and four WebSocket failures require loopback, while the other two Lambda fixtures passed individually. An unsandboxed rerun passed all 152 server tests but the aggregate reported 55 failures in array-view, borrow and MIR cases amid concurrent changes to those runtime sources. See `temp/accounting-lambda-baseline.log` and `test_output/`; do not attribute or mask them without isolating the shared-checkout state. The rerun built before the later RDB lifetime fix, so it does not qualify the final source. The current RDB suite passes separately (165 passed, two live-server skips).

## Constraints and evidence

1. The requested surface is a Lambda API, not a Python service, shell invocation, linked database binary in the app, or an accounting-specific hard-coded native backend. Business validation and accounting policy stay in Lambda.
2. Existing `RdbDriver` / `RdbConn` code offers prepare/bind/step/finalize, a SQLite implementation and a host registry. Reuse these where their contracts are adequate; add generic lifecycle or driver capabilities instead of copying a static implementation. The SQLite source at `lib/sqlite/` is vendored and remains untouched.
3. Existing `open target { ... }` in `lambda/runtime/write_set.cpp` implements document write sets. It is not proof of a block-scoped database connection or SQL semantics. Per S12.4.1v3, `input()` returns an eager value and must not retain a connection; S12.4.2–S12.4.3 require deterministic scoped cleanup on failure and cancellation, with GC only a backstop. `open()` resource acquisition is starred in the formal spec; inspect actual compiler/runtime implementation before choosing syntax.
4. The current SQLite driver drops SQL after the first statement, binds text as NUL-terminated and defers BLOB results (`lib/rdb_sqlite.c`); its existing generic ABI does not carry parameter byte lengths. The proposed API cannot safely claim single-statement validation or lossless text/binary until those gaps are addressed and tested.
5. A SQLite connection must go through the host registry and an appropriate file authorization check; merely calling native `rdb_open(..., false)` would bypass required app-safety architecture. Current bound callers are refused before driver open (D7.5.2; `vibe/impl/Lambda_Impl_App_Safety.md`). Do not weaken that gate to make the demo work. OS sidecars, aliases, symlinks, replacement races, backup, quotas and cross-platform behavior need a concrete broker/capability design before enabling a confined app.
6. SQL errors, rollback failure, cancellation, busy/locked databases, process interruption, out-of-space and migration failures must be tested on real on-disk SQLite, not just simulated in-memory. Never patch vendor SQLite. Tests create all files under `./temp/`.
7. MIR lowering of document `open` in `transpile_open_statement_item` currently emits `fn_open_end` on the normal path and comments that an error return relies on enclosing evaluation teardown. The interpreter calls `fn_open_end` with an unwind flag. This is not evidence of a safe SQL resource boundary: before reusing it, test errors caught outside the scope, return/break/continue, cancellation and nested calls in both tiers and implement any missing exit-edge cleanup under S12.4.2. A database lock must be released before an outer handler runs.

## Candidate API details for review

The following is deliberately semantic pseudocode, not claimed Lambda syntax. Transaction outcomes are ruled by S12.4.4v2; the API details below remain proposals:

```text
open tx = sqlite_target(database_path) {
    execute(tx, "INSERT INTO entry(id, amount) VALUES (?1, ?2)", [id, cents])
    rows = query(tx, "SELECT id, amount FROM entry WHERE id = ?1", [id])
}
```

- The transaction has one owner, one live connection and a deterministic scope. SQL text and data parameters are separate. Each call prepares **one** statement, binds every placeholder by position and rejects missing/extra parameters and additional statements. Do not log SQL parameters or interpolate user input into identifiers or query text.
- Startup establishes foreign-key enforcement before the transaction and fails closed if configuration cannot be applied. No `ATTACH`, extensions or unmediated alternate paths may bypass authorization; establish enforcement at the SQLite/driver boundary rather than a SQL prefix denylist. Transaction control, savepoints, pragmas and multi-statement execution must not silently subvert the host transaction. Which statements are supported is a documented capability, not an implicit promise of unrestricted SQLite.
- Proposed data boundary for phase 1: `null`, `bool`, finite integer in the SQLite signed 64-bit domain, finite float, text and binary, all converted exactly or rejected; no silent coercion of decimal, datetime, out-of-range values, NaN or infinity. Result values, duplicate column names, null, large integers, binary data, invalid UTF-8 and memory/row limits need explicit rules before implementation. Accounting stores currency as finite integer cents, with overflow detection (S4.1.1–S4.1.3).
- Queries produce bounded materialized values, never an escaped native statement/cursor or an open connection hidden inside a Lambda value (S12.4.1v3). Prepare, bind, step, copy results and finalize within the call; finalize on all exits. Connection/resource handles cannot be sent to other tasks or escape an owner without the rules in S12.4.2.
- Suggested initial scope is a writable transaction covering one database and schema migrations using the same atomic boundary. Concurrent callers get SQLite's defined locking/busy outcome, never silent retries after an unknown commit outcome. Explicit backup/restore uses a consistent SQLite backup operation; copying a live WAL file alone is insufficient.
- **Ruled:** S12.4.4v2 provides read-your-writes, commit on a clean successful exit, rollback on error/cancellation, and rollback-only after any failed statement. At rollback-only exit the scope raises an error; early return, failed commit, nesting and uncertain commit outcomes remain open. Document update visibility remains governed by PTH61/PTH66v2.

## Implementation milestones and acceptance

| Step | Scope | Acceptance gate |
|---|---|---|
| R0 | Ratify API, transaction semantics and security boundary; update formal + working specs | User-decided semantics; syntax and lifecycle compatible with S12.4; no assumed unimplemented scoped-resource support |
| R1 | Host-managed scoped SQLite transaction and parameterized execute/query | On-disk commit and rollback after reopen; invalid/multiple statements and parameter arity rejected; exact type checks; statements finalized; connection closed on every success/failure/cancellation path in interpreter and MIR |
| R2 | Bound-app authorization and platform qualification | Confined caller is denied until a broker grants a specific file; no symlink/sidecar/ATTACH escape; revocation and cancellation checked; macOS, Windows and Linux evidence, no unsupported platform claim |
| R3 | Lambda accounting persistence and migrations | Atomic journal and lines, unique source IDs, closed-period and reversal guards, transactional migration, reopen, integrity checks, backup/restore and injected failures; no access to user files during tests |
| R4 | End-to-end desktop workflows | Create/open company, accounts, journal/reversal, period close and report drilldown against real DB; UI state follows committed data; tests plus accountant review |

R1 must run the Lambda engine baseline and focused RDB/runtime regression tests at its boundary. Add a `.txt` expected result with every new Lambda test. Failure injection covers begin, prepare, bind, step, commit, close, concurrent access and cancelled scopes. R3 also tests that UI previews cannot be mistaken for persisted data. Mark incomplete/unsupported cases explicitly; do not advertise GST, direct filing or InvoiceNow from this milestone.

## MIR open-block exit investigation (2026-11-03)

`temp/accounting-open-unwind-probe.ls` opens a temporary document inside a
procedure, raises, catches the error in its caller, then opens a second block.
The interpreter prints `2`; MIR rejects the second block as nested. The MIR
`transpile_open_statement_item` emits the normal `fn_open_end(false)` only
after its body. `transpile_raise`, `transpile_propagated_result`, and other
error exits can leave the function before that instruction. This is a confirmed
MIR exit-edge defect, not a SQLite failure.

The repair must identify which `open` scopes an actual transfer leaves, not
blindly close the current transaction on every error: a handler within the
same block can recover while keeping it open. It must cover direct `raise`,
postfix propagation, failed admission and binding joins, nested callees,
`return`, `break`, `continue`, inlining, task cancellation and function
fallthrough. A failure leaving the open scope must call `fn_open_end(true)`
before an enclosing handler; a successful transfer leaving it must close it
according to the resolved block-exit rules, while a transfer staying inside it
must not close it. The original error and any returned value need precise roots
across cleanup (D5.3.1–D5.3.3, S12.4.2); cleanup errors follow the applicable error
rules, with unresolved cases decided before implementation. Test the same
cases on interpreter and MIR, including an inner recovered failure, and add
matched `.ls`/`.txt` goldens. Do not implement a one-off `raise` patch: it
would leave other exits able to leak a database lock.
