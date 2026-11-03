# Scoped SQLite transactions and parameterized SQL

**Status:** core transaction semantics ruled; API and security details under design. **Updated:** 2026-11-03. **Purpose:** supply durable company accounting to the sibling Lambda Script accounting app. **Scope:** a single local SQLite database; no hosted database or production-readiness claim in this milestone.

## Current checkpoint

- **Decisions:** S12.4.4v4 specifies parameterized scoped SQLite transactions, read-your-writes, clean-exit commit (including `return`, `break` and `continue`), return-type admission before commit, error/cancellation rollback, rollback-only after a failed statement and an error at rollback-only exit. Outside a transaction, each SQL statement autocommits. Business rules and the application remain Lambda Script.
- **Implemented slice:** `lambda.io.sqlite.execute` and `query` expose one-statement SQLite operations for unconfined callers. The owner task authorizes and pins the connection; an offload worker executes SQL with cancellation, and the owner converts bounded, copied rows before commit. See `doc/Lambda_Sys_Func.md` and the bridge section below. SQLite now builds thread-safe so workers and other connections can overlap. Opening and final commit still run on the owner thread.
- **Scoped implementation:** `sqlite.database(path)` supplies an `open` target; its task-owned handle supports read-your-writes and is invalidated at exit. SQL failures and rejected parameter conversions mark the scope rollback-only. Cancellation joins the worker before rollback; scheduler teardown joins orphaned workers before releasing their task attachments. Nested scopes, explicit commit/rollback and document writes inside the SQLite block are rejected. Bound-app database authorization remains unimplemented and fails closed. Shared-checkout qualification is in progress.
- **Scope cleanup:** the interpreter and MIR now commit successful early exits and roll back errors, return-admission failures and failed commits; `open_scope_exit` and `open_scope_unwind` pass on both tiers under forced collection. An interpreter open-body scratch-plan undercount was fixed. The aggregate baseline passed 8,720/8,720 before the later SQLite bridge and conditional-carrier fix, which still require a fresh aggregate qualification.
- **Tests:** the full thread-safe RDB suite passes 172 tests; PostgreSQL/MySQL live tests skip without configured URIs. Security tests reject bound-app SQLite calls before file creation on both tiers. Standalone, cancellation, separate-task and concurrent-query Lambda fixtures pass on both tiers under forced collection. The SQLite bridge and conditional-carrier fix are in the shared checkout; the final unsandboxed Lambda baseline passes 8,728/8,728.
- **Accounting:** `../accounting` contains the in-memory ledger, trial balance, profit-and-loss and balance-sheet UI, account activity calculations and a SQLite schema. The schema protects single-company ownership, posted account identity, balanced journals, reversals, period closing and audit records. The UI starts with sample data and can load an experimental company snapshot. A corrected conditional carrier lets the 51-case ledger golden and UI pass on the shared interpreter and JIT, including forced collection for the model. No business records, GST filing, InvoiceNow or compatibility claim is supported yet.
- **Latest qualification:** the scoped SQLite, cancellation, early-exit and security checks pass on both execution tiers; all 22 focused task-runtime tests and the full RDB suite (173 pass, 2 optional live-server skips) pass. The shared-checkout Lambda baseline passes 8,733/8,733 with `LAMBDA_TEST_MAX_CONCURRENT=1` after the URI guard (`temp/accounting-final-baseline.log`). An earlier run missed one case because `lambda.exe` was temporarily absent during a concurrent relink; that case passed separately and in the final baseline. Cross-platform and remaining failure-injection cases still require qualification.
- **Storage prototype:** `../accounting/mod_store.ls` creates a company and its schema atomically, inserts accounts and periods, posts balanced journals, creates linked reversals, closes periods, and reloads a consistent snapshot for the pure reporting model. Mutating and loading operations use the existing-only SQLite target and verify the application and schema version before touching company tables. Commands validate dates and business identifiers before SQLite can coerce their types, and reject transient memory targets for company creation. An explicit SQL comment marker separates migration statements without splitting trigger bodies at semicolons; SQLite still validates one statement per call. `tests/store_test.sh` passes 23 behavioral checks on both tiers under forced GC, including a growing journal snapshot. The adapter remains experimental; safe upgrades, backups and broader durability qualification remain unfinished.
- **Existing-only open and UI:** `sqlite.existing(path)` acquires read/write without SQLite's create flag and rejects `:memory:` and `file:` URIs; it passes native, confined-app and both-tier Lambda checks, including a missing-file case. URI rejection prevents a globally enabled SQLite URI mode from turning an existing-file open into a memory database. The UI can open an experimental company, display accounts and reports by fiscal period, and drill down from an account to its source journals; a failed open retains the previous snapshot and does not create the missing file. The company-opening, period selection, activity and sample navigation checks pass on both tiers. The UI has no editing or safe creation workflow. Bound apps remain denied under D7.5.2.
- **Creation safety:** `sqlite.database(path)` can open an existing empty SQLite file, so the prototype's create operation must not be presented as an exclusive new-file operation. SQLite documents that `SQLITE_OPEN_EXCLUSIVE` does not make `sqlite3_open_v2` fail for an existing file ([SQLite open documentation](https://sqlite.org/c3ref/open.html)). A future create-company UI needs a portable, race-resistant exclusive creation design, plus tests for existing paths, aliases, cleanup and interruption; a preflight `exists` check alone is insufficient.
- **Additional concurrency suite limitation:** the full `test_lambda_concurrency_gtest` executable fails `JubeDom4.OrdinalParityCoversDeclaredMembersAndHusks` independently (for example `broadcast_channel.name` is handled by name but not by ordinal), then later tests report a retained evaluator context and the process exits 139 (`temp/accounting-concurrency-full-final.log`). The same Jube failure reproduced in the isolated worktree before the latest SQLite change. The 22 `LambdaConcurrencyRuntime.*` cases, including the new offload/attachment shutdown test, pass independently. Investigate the Jube catalogue/guard issue before claiming that full suite passes; it is not part of the Lambda baseline target.
- **Backup design constraint:** SQLite's [`VACUUM INTO`](https://www.sqlite.org/lang_vacuum.html) produces a consistent snapshot, but permits an existing empty destination and can leave an incomplete output after interruption. It also cannot run inside the current transaction. Neither it nor a raw copy is a complete safe backup/restore workflow; implement an explicitly authorized destination, no-overwrite publication, integrity verification and interruption cleanup before exposing backup as complete.
- **Next:** qualify the scoped implementation in the shared checkout, then implement the atomic Lambda storage adapter, migrations, reopening and backup/restore before completing phase 1. The design and feature matrix record the broader Singapore and ABSS/Premier milestones.

## Constraints and evidence

1. The requested surface is a Lambda API, not a Python service, shell invocation, linked database binary in the app, or an accounting-specific hard-coded native backend. Business validation and accounting policy stay in Lambda.
2. Existing `RdbDriver` / `RdbConn` code offers prepare/bind/step/finalize, a SQLite implementation and a host registry. Reuse these where their contracts are adequate; add generic lifecycle or driver capabilities instead of copying a static implementation. The SQLite source at `lib/sqlite/` is vendored and remains untouched.
3. Existing `open target { ... }` in `lambda/runtime/write_set.cpp` implements document write sets. It is not proof of a block-scoped database connection or SQL semantics. Per S12.4.1v3, `input()` returns an eager value and must not retain a connection; S12.4.2–S12.4.3 require deterministic scoped cleanup on failure and cancellation, with GC only a backstop. `open()` resource acquisition is starred in the formal spec; inspect actual compiler/runtime implementation before choosing syntax.
4. The current SQLite driver drops SQL after the first statement, binds text as NUL-terminated and defers BLOB results (`lib/rdb_sqlite.c`); its existing generic ABI does not carry parameter byte lengths. The proposed API cannot safely claim single-statement validation or lossless text/binary until those gaps are addressed and tested.
5. A SQLite connection must go through the host registry and an appropriate file authorization check; merely calling native `rdb_open(..., false)` would bypass required app-safety architecture. Current bound callers are refused before driver open (D7.5.2; `vibe/impl/Lambda_Impl_App_Safety.md`). Do not weaken that gate to make the demo work. OS sidecars, aliases, symlinks, replacement races, backup, quotas and cross-platform behavior need a concrete broker/capability design before enabling a confined app.
6. SQL errors, rollback failure, cancellation, busy/locked databases, process interruption, out-of-space and migration failures must be tested on real on-disk SQLite, not just simulated in-memory. Never patch vendor SQLite. Tests create all files under `./temp/`.
7. MIR lowering of document `open` in `transpile_open_statement_item` currently emits `fn_open_end` on the normal path and comments that an error return relies on enclosing evaluation teardown. The interpreter calls `fn_open_end` with an unwind flag. This is not evidence of a safe SQL resource boundary: before reusing it, test errors caught outside the scope, return/break/continue, cancellation and nested calls in both tiers and implement any missing exit-edge cleanup under S12.4.2. A database lock must be released before an outer handler runs.

## Scoped API and constraints

Transaction outcomes are ruled by S12.4.4v4; `lambda.io.sqlite` supplies the target and operations:

```text
let target = sqlite.database(database_path)^
open tx = target {
    sqlite.execute(tx, "INSERT INTO entry(id, amount) VALUES (?1, ?2)", [id, cents])^
    let rows = sqlite.query(tx, "SELECT id, amount FROM entry WHERE id = ?1", [id])^
}
```

- The transaction has one owner, one live connection and a deterministic scope. SQL text and data parameters are separate. Each call prepares **one** statement, binds every placeholder by position and rejects missing/extra parameters and additional statements. Do not log SQL parameters or interpolate user input into identifiers or query text.
- Startup establishes foreign-key enforcement before the transaction and fails closed if configuration cannot be applied. No `ATTACH`, extensions or unmediated alternate paths may bypass authorization; establish enforcement at the SQLite/driver boundary rather than a SQL prefix denylist. Transaction control, savepoints, pragmas and multi-statement execution must not silently subvert the host transaction. Which statements are supported is a documented capability, not an implicit promise of unrestricted SQLite.
- Proposed data boundary for phase 1: `null`, `bool`, finite integer in the SQLite signed 64-bit domain, finite float, text and binary, all converted exactly or rejected; no silent coercion of decimal, datetime, out-of-range values, NaN or infinity. The standalone query defines these result and limit rules in `doc/Lambda_Sys_Func.md`; reuse them for scoped queries. Accounting stores currency as finite integer cents, with overflow detection (S4.1.1–S4.1.3).
- Queries produce bounded materialized values, never an escaped native statement/cursor or an open connection hidden inside a Lambda value (S12.4.1v3). Prepare, bind, step, copy results and finalize within the call; finalize on all exits. Connection/resource handles cannot be sent to other tasks or escape an owner without the rules in S12.4.2.
- Suggested initial scope is a writable transaction covering one database and schema migrations using the same atomic boundary. Concurrent callers get SQLite's defined locking/busy outcome, never silent retries after an unknown commit outcome. Explicit backup/restore uses a consistent SQLite backup operation; copying a live WAL file alone is insufficient.
- **Ruled:** S12.4.4v4 provides read-your-writes, commit on a clean successful exit, rollback on error/cancellation, and rollback-only after any failed statement. At rollback-only exit the scope raises an error; a failed commit, nesting and uncertain commit outcomes remain open. Document update visibility remains governed by PTH61; exit behavior is S12.4.4v4.

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

## Bridge investigation (2026-11-03)

The existing document `fn_open_begin` forces path targets and accepts only a
registered document node. A database cannot be smuggled through that path:
`input()` is eager (S12.4.1v3), and `open` must distinguish a database target
without changing ordinary file-document behavior. A plausible implementation is
a branded, inert SQLite target value followed by `open` acquiring its
host-registered transaction and binding a branded, scope-owned handle. The
constructor and operation names, descriptor representation and checked result
shape remain design work; the pseudocode above is not executable. Reuse the
existing VMap brand and resource-handle machinery in `io_resource.cpp`, not a
forgeable map or a copied handle implementation (D1.2v2, D5.3.3).

`input-rdb.cpp` has a static `rdb_value_to_item` helper, but its MarkBuilder
allocations are Input-arena owned and its current BLOB and length handling is
incomplete. A script-visible query result must instead own data independently
of its finalized SQLite statement and use the runtime's precise roots; first
identify or refactor a genuinely shared conversion boundary rather than copy
that helper or return borrowed cell pointers (D5.3.1–D5.3.3, S12.4.1v3).
Row names, duplicate names, integer boundaries, invalid UTF-8 and binary values
must be resolved before advertising general SQL queries.

The write-set state currently belongs to an EvalContext. Before allowing
suspension, cancellation or multiple tasks in a database `open` block, verify
that transaction state and cleanup are owned by the correct task and that
concurrent work cannot join another task's scope (S13.1.2v2, S13.1.3v2,
D7.5.2). The existing SQLite pin guards synchronous teardown only; it is not a
cross-thread cancellation protocol. `rdb_cancel`/`sqlite3_interrupt`, registry
ownership, statement finalization and asynchronous close must be designed and
tested together, without weakening the bound-app authorization gate.

## Accounting JIT regression isolation (2026-11-03)

`temp/accounting-report/jit-minimal.ls` reduces the accounting model's existing
JIT abort to a conditional whose arms call functions returning different map
shapes. The interpreter completes; the isolated debug JIT aborts before
execution with `mir-value: unavailable representation transition 1 -> 8`.
The debugger stack in `temp/accounting-report/jit-lldb.txt` leads to
`mir_emit_if_branch` joining an Item-valued branch into a raw-GC-pointer
carrier chosen by `transpile_if`. `type_to_mir` reports the same machine class
for both and does not alone establish equal semantic representations
(D2.4.1–D2.4.3, D5.2.1v5). The fix reports boxed container-call results as Item carriers and selects
conditional joins by semantic representation as well as machine register
class (D2.4.1–D2.4.3); it preserves the assertion and the app model. The
`boxed_container_branch` fixture and the app's model and UI pass both tiers;
the final aggregate baseline passes 8,728/8,728. `lambda/runtime/transpile-mir.cpp` is concurrently
changing in the shared checkout, so any validated fix must be ported surgically
and retested against the final source.

## Scope-exit lowering notes (2026-11-03)

S12.4.4v4 now commits successful early exits. To implement it,
loop lowering needs to remember the open-scope boundary at loop entry: a
`break`/`continue` within an open block must leave it active, while a jump to
an enclosing loop must close it before transfer. `LoopLabels` already tracks
an analogous task-scope base; extend its scope metadata rather than guessing
from the presence of an open block. Explicit returns have separate inline,
boxed, native and pair-forwarded paths; a return expression and its declared
return admission must finish before choosing success or error cleanup. Disable
or route tail-call/pair forwarding through cleanup where it crosses an open
scope, and root any live Item or GC pointer across the close call (D5.2.1v5,
D5.3.1–D5.3.3, S12.4.2). Failed close must propagate through the established
error channel rather than silently return a success. Validate loops wholly
inside the block, blocks inside loops, inlined callees, guarded errors and
forced collection on both execution tiers before promoting the experiment.

## Resource-carrier ownership detail (2026-11-03)

`io_resource_handle_new` provides a branded immutable VMap, but its common
`io_resource_destroy` and `io_resource_trace` deliberately do nothing: the
owning task, not the handle, must own cleanup and invalidate `host_data` on
close. `io_loopback.cpp` roots the carrier while live and invalidates it in its
owner-release callback. Reusing the VMap carrier for an SQLite transaction must
also provide deterministic block-exit closure, task ownership and cancellation;
copying the no-op carrier alone would leak a connection or permit a dangling
handle. `lambda_task_scope_leave` currently manages child tasks and has no
general block-resource destructor registry. Task-completion hooks can be a
backstop, not a replacement for block cleanup (S12.4.2–S12.4.3). An inert
path descriptor needs separately defined ownership/tracing if it stores a
Lambda string; it cannot put an untraced GC pointer in `host_data`.

## One-statement Lambda bridge experiment (2026-11-03)

An isolated `lambda.io.sqlite` prototype in `temp/accounting-rdb-worktree`
provides parameterized `execute(path, sql, params?)` and
`query(path, sql, params?)` using one-statement transactions (S12.4.4v4,
D7.2.4v4). Query returns materialized arrays of positional rows, so duplicate
column names are unambiguous and SQLite statement storage never escapes. It
copies supported finite numbers, UTF-8 text, binary and null, including wide
signed SQLite integers; unsupported values and resource limits fail closed.
SQL input is bounded to 1 MiB, path input to 32 KiB, parameters to 1,024 and
16 MiB of text/blob bytes; results are bounded to 100,000 rows, 1,024 columns,
1,000,000 cells and 16 MiB of text/blob bytes. Calls from a document `open`
block are denied instead of creating an independent transaction; an open
block in another task does not capture or block the call. Bound apps are
rejected before native acquisition (D7.5.2).

The owner task authorizes and opens the connection, pins it, then parks while
SQLite prepares, binds and steps on an offload worker. The worker copies rows
into native-owned bounded memory; the owner materializes rooted Lambda values
and commits only after the worker completes and the result is accepted.
Cancellation uses SQLite's progress handler and is joined before rollback.
Registry teardown before scheduler teardown invalidates the registry row but
the owner-held pin defers SQLite close until the worker finishes. Focused
interpreter/MIR fixtures pass under forced collection, including cancellation;
a native test synchronizes teardown with an active SQLite worker, and bound
security tests deny file creation on both tiers. Begin and final commit still
perform filesystem work on the owner thread. The bridge was promoted to the
shared checkout; scoped integration and its later qualification are recorded
in the checkpoint above. Cross-platform verification remains outstanding.

## Scoped resource checkpoint (2026-11-03)

The isolated implementation passes scoped Lambda fixtures on both tiers under
forced collection, including failed return admission, handled statement
errors, cross-task handles, cancellation and normal control transfers. Bound
apps are denied before file creation on both tiers. A scheduler teardown test
found a queued wake could resume a task while shutdown joined its worker; the
scheduler now prevents draining during teardown and releases the orphan job
before its task attachments. The 22 focused scheduler runtime tests pass.
The implementation is being qualified in the shared checkout. The current
document write set remains evaluator-owned; the task check does not make
document write sets task-isolated. The app still needs its own transactional
storage adapter and durability safeguards.
