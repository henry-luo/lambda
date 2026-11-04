# Scoped SQLite transactions and parameterized SQL

**Status:** scoped SQLite SQL and create-only publication implemented; cross-platform qualification and backup remain open. **Updated:** 2026-11-04. **Purpose:** supply durable company accounting to the sibling Lambda Script accounting app. **Scope:** a single local SQLite database; no hosted database or production-readiness claim in this milestone.

## Current checkpoint

- **Publication durability (D7.5.2, D7.5.6):** SQLite create, clone and preimage now require the file anchor's full-sync mode; on macOS that adds `F_FULLFSYNC` for the staged image and destination directory. The same barrier applies to `io.create_file` exports. A macOS test interposes only the test process and injects full-sync failures before and after publication for both execution tiers. It checks no destination before publication and an explicit uncertain result with intact bytes afterward. This does not establish power-loss behavior on all filesystems or strengthen ordinary SQLite file-backed commits; staging recovery and cross-platform qualification remain open.
- **Existing-file commit gap (S12.4.4v4):** the SQLite driver's file-backed transactions use the default VFS. The vendored VFS normally uses `fsync`, and even when requested to use `F_FULLFSYNC` it falls back to `fsync` on failure. Merely setting SQLite's `fullfsync` pragma cannot establish a strict full-sync failure contract. A Lambda-side design must cover journal/WAL synchronization and error propagation without editing the vendored SQLite source; power-loss qualification remains open.
- **Company opening:** the accounting adapter now applies its existing posted-ledger checks to every supported schema version, rather than skipping versions 1–5. Its backup golden rejects an unbalanced journal and a mismatched reversal on open on both tiers under forced GC. The UI test confirms a rejected open retains the previously opened company; it scrolls the report into view before selecting an account. Creation, backup and upgrade UI tests pass on both tiers. The Lambda baseline passes 8,754/8,754 with normal loopback and media access (`temp/accounting-fullsync-baseline-unsandboxed.log`).
- **Referential validation (S12.4.4v4):** the SQL authorizer admits only the argument-free, read-only `foreign_key_check` pragma alongside the existing scoped rules. The accounting validator runs `PRAGMA main.foreign_key_check` in its existing snapshot before reading or validating company records. Direct pragma syntax prevents a user table named `pragma_foreign_key_check` from shadowing the check; an over-limit or failed query also rejects the operation. Native tests cover deferred violations, read-your-writes, shadowing and rejection of mutating or argument-bearing pragmas. Accounting UI checks reject orphaned journal lines and contact history while retaining the prior company. All 51 accounting goldens pass on both tiers under forced GC; two previously stale positive GST fixtures now use fully claimed purchases, while the purchase-review test still verifies that a partial claim blocks the preview. The RDB suite passes 192 tests, with two unconfigured live-server tests skipped; the Lambda baseline passes 8,755/8,755 (`temp/accounting-fk-baseline.log`).
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
- **UI setup checkpoint:** the Accounts screen can add accounts and non-overlapping fiscal periods to an opened experimental company. It reloads the snapshot after a committed change; a failed refresh disables editing until reopen and reports that the write was saved. Sample books stay read-only. `tests/ui_setup_test.py` passes on interpreter and MIR, checking categories, duplicate rejection, invalid/overlapping periods, persistence and reopening; the earlier company-opening UI test still passes. Changes are in the sibling accounting checkout and are uncommitted.
- **Journal checkpoint:** the Journals screen validates and reviews a two-line SGD entry, posts it atomically, refreshes the snapshot and links it to account activity. The parser accepts exact decimal cents without floating point; duplicate references, same-account lines and invalid input are rejected. A write failure or failed post-commit refresh disables further editing and directs the user to reopen and check before retrying. `tests/ui_journal_test.py` passes on both tiers, including cancellation, duplicate protection and injected posting failure; the ledger and amount goldens pass both tiers under forced GC. The storage and UI setup regression suites also pass. Changes remain uncommitted.
- **Reversal checkpoint:** the Journals screen can review and post a linked reversal, displaying its original reference and mirrored lines. `tests/ui_journal_test.py` passes on both tiers for invalid reversal dates, cancel, post, duplicate-reversal rejection, report drilldown, the stored mirror and audit events. Multi-line entry and the other phase-1 release gates remain incomplete.
- **Period close checkpoint:** the Accounts screen lists periods and supports review, cancellation and closing, with an explicit notice that reopening is unavailable. The journal UI test passes on both tiers for an audited close, reopening the company and rejecting a later posting into the closed period. UI mutation failures require a fresh open before retry. Changes remain uncommitted.
- **Multi-line checkpoint:** the Journals screen supports a line editor in addition to the two-line shortcut. It requires balanced entries before review, supports removing draft lines, preserves the multi-line header on review cancellation, and posts through the same transactional service. `tests/ui_multiline_test.py` passes on both tiers for unbalanced drafts, line removal, review/cancel, a three-line posting and its linked three-line reversal. The earlier journal and company-opening regressions still pass. Changes remain uncommitted.
- **Path-authorization correction:** SQLite's `RdbTarget.path` carries at most 1,023 bytes, but the driver previously truncated longer paths for authorization while passing the full name to SQLite. It also accepted `file:` URI spellings even though SQLite URI interpretation can be enabled globally. Under D7.5.2, the driver now rejects oversized or URI targets before authorization/open; Lambda validates the same limits on target construction and standalone statements. Literal names beginning `file:` can be prefixed with `./`. `sqlite_path_security.ls/.txt` passes both tiers under forced GC, and the native registry regression verifies the longest admitted path and rejects truncated/URI targets before authorization. The full RDB suite passes 174 tests with two live-server skips. The shared `make test-lambda-baseline` run had 8,717/8,734 pass: four React fixtures failed with TypeError, four photo-editor fixtures failed (three image errors, one output mismatch), one DOCX fixture timed out but passes alone, and eight loopback-related cases failed in the sandbox. A concurrent build relinked `lambda.exe` during the first focused server rerun; a subsequent unsandboxed retry passes all eight loopback cases. The four React and four photo-editor cases still fail separately, so the aggregate gate remains red. Raw logs: `temp/accounting-sqlite-path-baseline.log`, `temp/accounting-js-focused-retry.log`, `temp/accounting-photo-focused-retry.log`, `temp/accounting-loopback-server-retry.log`, and `temp/accounting-loopback-lambda-retry.log`. Do not attribute unrelated failures to SQLite without isolating them. Cross-platform checks remain outstanding.
- **Exclusive creation integrated:** `sqlite.create(path)` was prototyped in `temp/accounting-rdb-worktree` and is now in the shared checkout. It builds the database in a registered memory transaction and publishes the committed image through a held create-only file anchor, with repeated host authorization and a 64 MiB image limit. Failed statements, return admission, cancellation and existing files produce no publication. Interpreter and MIR golden tests pass under forced GC, including competing creators; four native snapshot tests and both bound-app denial tiers pass. The accounting staging copy in `temp/accounting-ui-work` can create and then open a company using a migration resolved from the document base URI; both tiers pass end-to-end, including empty/unrelated files, file and directory symlinks, paths containing spaces, posting, reopening and SQLite integrity checking. A failed directory sync is surfaced as an uncertain publication. The shared executable passes the creation and path-security goldens on both tiers under forced GC. The integrated accounting storage suite and all six UI regression groups pass on both tiers; the shared RDB suite passes 178 tests with two optional server skips, and all 12 file-anchor cases pass. The shared unsandboxed Lambda baseline passes 8,727/8,735: the same four React and four photo-editor fixtures fail; the SQLite and app-safety cases pass. Windows file anchors are unsupported, Linux is unqualified, and crash cleanup/failure injection remain open.
- **Backup prototype integrated:** `sqlite.clone(source,destination)` takes a bounded online SQLite snapshot followed by the S12.4.4v4 scoped transaction and D7.5.2/D7.5.6 create-only publication. It checks SQLite integrity and foreign keys, and stages both connections with owner pins during worker offload. The accounting app checks company identity, selected ledger invariants and the reopened ledger; the integrated backup/restore UI passes on interpreter and MIR, including preservation of existing files and snapshot isolation. Five native cases pass, including corrupt sources, uncheckpointed WAL, concurrent commits and teardown with both connection pins held; the RDB suite passes 182 with two optional server skips. Confined-app denial passes on both tiers; Lambda and accounting golden cases pass on both tiers with forced GC. All six existing UI groups pass on the isolated build, and the integrated storage suite passes. The shared Lambda baseline passes 8,728/8,736; the same four React and four photo-editor fixtures fail, while SQLite, security and server cases pass (`temp/accounting-clone-baseline.log`). Neither the prototype nor creation has been qualified on Linux; Windows anchors fail closed, in-flight cancellation and injected disk failure deserve further tests, and crash cleanup remains incomplete.
- **Next:** qualify the integrated migration and recovery paths before completing phase 1, including interruption, disk failure and supported platforms. A migration preserves a pre-migration snapshot and upgrades that same image; users must stop other writers before switching to the new company. The design and feature matrix record the broader Singapore and ABSS/Premier milestones.
- **Migration candidate, not implemented:** acquire one SQLite online snapshot, publish its pre-upgrade image to a new backup path, then execute schema changes on that same in-memory snapshot and publish the upgraded image to a second new path. The original remains untouched. Backup publication is a separate external effect: if later migration fails, the backup remains and the upgraded destination must not appear (S12.4.4v4, D7.5.6). Both destinations require their own held authorization/anchors, and uncertain publication must stop the upgrade and tell the caller to inspect the path. Prove the backup and upgraded data originate from the identical snapshot under concurrent writes, and exercise failures and cancellation at both publication boundaries. A second independent `sqlite.clone` is insufficient to prove this identity if another process changes the backup in between. SQLite documents [online backup locking](https://www.sqlite.org/c3ref/backup_finish.html) and [in-memory serialization](https://www.sqlite.org/c3ref/serialize.html). An isolated native probe `RdbTest.SqlitePreMigrationImageRemainsIndependentOfLaterWrites` confirms that a WAL-derived in-memory snapshot serialized during a read transaction reopens with the pre-change row after a later write; it does not establish crash safety or implement the operation. The probe is only in `temp/accounting-rdb-worktree`; platform qualification remains open.
- **Candidate refinement:** allow the application to validate the clone in its scope, then explicitly publish a pre-migration image before any non-read-only SQL. The runtime must enforce that ordering, treat publication failure/uncertainty as rollback-only, and distinguish a backup already published from an upgraded destination that may or may not have been published. A private clone target is required; a general file-backed transaction snapshot has different WAL and uncommitted-state considerations. This remains a design, not an implemented guarantee.
- **Isolated preimage work in progress:** `temp/accounting-rdb-worktree/lib/rdb_sqlite.c` and `rdb_sqlite_tx.h` now contain an unqualified native prototype for one preimage publication before any modifying SQL, sharing the existing bounded serialization path. The first test exposed SQLite's internal serialization pragma being blocked by the SQL authorizer; the host now temporarily removes the guard only for serialization and restores it before the publication callback. Focused tests pass for validation-before-write, rejection after write, rollback after failed/uncertain publication, and rejection of reentry during a publication callback. Existing clone/snapshot cases also pass. There is no Lambda API or accounting migration workflow yet; do not copy these changes to the shared checkout until further lifecycle, authorization, cancellation, WAL and destination-exists cases pass.
- **Preimage Lambda API is being prototyped:** isolated `lambda/runtime/io_sqlite.cpp`, `lambda/lambda.h` and `lambda/runtime/sys_func_registry.c` now expose `sqlite.preimage(tx,path)` and share the existing anchored-destination setup. The isolated build succeeds; `sqlite_preimage.ls/.txt` pass interpreter and JIT under forced GC for unchanged preimage, existing destinations, late/repeated calls, caught errors, successful preimage followed by rollback, non-clone scopes and cancellation before start. Native tests cover authorizer restoration, preimage ordering, failure/uncertainty, callback reentry, uncheckpointed WAL data and registry teardown after publication. Cancellation is checked before publication and by the destination authorization callback; failure to restore SQLite authorization closes the connection. Confined-app denial passes both tiers, and the complete isolated RDB suite passes 189 with two optional server skips; the earlier creation and clone goldens also pass both tiers under forced GC. Further test in-flight cancellation, disk errors, and full regressions before integrating; the existing documented `sqlite.clone` remains the only integrated behavior.
- **Accounting schema upgrade work in progress:** `temp/accounting-ui-work/schema/002_period_reopening.sql` and the staged `mod_store.ls` contain an experimental version-1-to-2 upgrade and immutable reasoned reopening event. `tests/migration_test.ls/.txt` pass interpreter and JIT with forced GC for preserved source/preimage, failed migration, existing backup, invalid/future versions, rejected unaudited reopen, reasoned reopening, posting after reopen, repeat close/reopen and immutable event. Existing staged storage/backup goldens and creation/backup UI tests pass. The initial run failed because SQLite reports one result column on `ALTER TABLE` adding a foreign key to a STRICT table even though the statement yields no rows. The isolated host now accepts metadata without rows, but rejects and rolls back rows returned to `execute`. SQLite invokes an internal `quick_check` while stepping the ALTER; the isolated SQL guard permits it only while stepping an authorizer-identified ALTER, and a native regression verifies direct pragma calls remain denied. These runtime changes and the migration still need security review, a UI workflow, recovery/failure cases and full regressions before integration. Temporary diagnostics were removed.
- **Staged migration UI:** `temp/accounting-ui-work/accounting.ls` now tracks the loaded schema version, presents separate new backup/upgraded paths, requires explicit reopening review and a reason for version 2, and blocks UI clicks while copying. The new full upgrade/reopen/post test and all seven earlier accounting UI groups pass both tiers. The migration golden now also verifies an already-existing upgrade destination preserves the newly created preimage and rejects replaying an older reopening event. The complete isolated RDB suite passes 190 with two optional server skips. The refreshed confined-app security target is building; staged UI and schema are not integrated into the sibling project.
- **Migration prototype integrated:** the preimage/ALTER runtime sources, native and Lambda tests, API documentation, and accounting version-2 migration/UI are now in the shared and sibling checkouts. `make build` succeeds; the integrated preimage and migration goldens pass interpreter and MIR under forced GC, and the integrated upgrade/reopen/post UI test passes both tiers. The isolated confined-app tests and all accounting UI groups pass both tiers; the RDB suite passes 190 with two optional server skips. The required shared Lambda baseline passes 8,737/8,737 (`temp/accounting-migration-baseline.log`). Production recovery, Windows, Linux, disk-failure injection and interruption qualification remain open.
- **New-company schema integrated:** new companies are created directly at schema version 2 in one create-only transaction; the version-1 creation service remains available for migration tests. Forced-GC migration tests verify successful current-schema creation, preservation of version-2 reopening events in backup, and that a failed schema transition publishes no company. The integrated creation UI and migration golden pass both tiers.
- **Activity history integrated:** the accounting app has a read-only History view using one SQLite scope (S12.4.4v4) for journal posting, period closing and version-2 reopening events. It shows reason and reference and explains that actor attribution and exact order of equal timestamps are unavailable. The integrated migration golden passes both tiers with forced GC, comparing company and backup history; the expanded integrated UI regression passes both tiers for version-1 history, version-2 events, missing-history errors and the sample state. All eight staged UI regression groups also pass both tiers.
- **Next-phase contacts integrated:** `../accounting/doc/CONTACTS_DESIGN.md` records customer/supplier scope, stable identity, immutable change snapshots, migration and acceptance cases. The sibling app now has version-3 contacts, reviewed creation and editing, search, revision-checked writes, confirmed activation changes, immutable before/after events, version-1/2-to-3 upgrades using one preimage, current-schema creation and backup comparison including contact/audit records (S12.4.4v4). Drafts survive navigation and failed-write reopening; switching companies requires saving or discarding them. Service and schema reject embedded NUL. Integrated contact, storage, migration and backup goldens pass both tiers with forced GC; integrated Contacts, migration, creation and backup UI tests pass both tiers. All nine staged UI groups and the schema test pass. Production recovery, platform qualification, credit limits, tax defaults, sales and purchases remain open.
- **GST arithmetic foundation integrated:** `../accounting/doc/GST_WORKSHEET_DESIGN.md` links current IRAS F5, rates, registration, rounding and InvoiceNow guidance. `../accounting/mod_gst.ls` is a test-only pure calculation with independently recorded values and tax, source references, explicit unsupported cases, skipped-source reporting and checked integer sums (S4.1.1–S4.1.3). `tests/gst_test.ls/.txt` pass interpreter and MIR with forced GC for ordinary mappings, signed corrections, recorded 8%/9% amounts, independent purchase claims/revenue, source validation, duplicates and overflow. It does not verify correction links or caps, integrate source documents, or provide a filing-ready worksheet; accountant review remains a release gate. IRAS already requires InvoiceNow for some voluntary registrants, and this app has no InvoiceNow integration.

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

## Candidate exclusive creation boundary

An application-level `exists` check cannot make the current
`sqlite.database(path)` exclusive. A candidate generic resource target would
run the requested creation transaction against a host-registered, private
in-memory SQLite database and publish its committed image to an absent file
only after the `open` block succeeds (S12.4.4v4). Keep migration SQL and
company initialization in Lambda. Errors, cancellation, rejected return
admission and rollback-only scopes must never publish a file.

The destination needs independent host authorization under D7.5.2: a grant
for the in-memory SQLite connection does not authorize a file. Hold and
revalidate an authorized parent anchor throughout creation, and publish using
the existing no-replacement `file_anchor_write_atomic` primitive with a
create-only expectation. It stages and syncs bytes on the destination
filesystem and distinguishes a failed publish from a published file whose
directory sync failed. The caller must never retry an uncertain result
blindly. Current file anchors deliberately fail closed on Windows, so this
candidate alone cannot meet the requested cross-platform gate; a Windows
anchor/broker implementation and qualification are required.

SQLite's [`sqlite3_serialize`](https://www.sqlite.org/c3ref/serialize.html)
can obtain the committed in-memory database image. Its no-copy probe reports
the image size even when no contiguous image is available, allowing a bounded
copy only after checking the size; the returned allocated copy must be freed
with SQLite's allocator. The accounting caller must check its company identity
and schema before publication. Exercise existing files, simultaneous creators, failed
migration, cancellation, returned errors, owner teardown, destination
replacement, authorization denial, serialization and publication failure,
and a reopen on each supported platform. Serialization creates only the
initial company image; it is not a backup mechanism for a live database.

## Candidate validated backup boundary

A possible generic clone target would acquire an existing, host-authorized
source read-only, use SQLite's [online backup API](https://www.sqlite.org/backup.html)
to copy a consistent source snapshot into a separately registered memory
connection, then bind that snapshot in an `open` transaction. The Lambda
caller could validate the accounting identity, schema and ledger before the
same create-only publication boundary writes a new destination. Restore would
also produce a new path; it would not replace the source or an existing file.

SQLite requires the backup destination to have no open transaction when
`sqlite3_backup_init` runs, so the scoped validation transaction must begin
only after the backup object is finished. Source and destination connections
must be separately authorized and pinned; workers must join before either
connection is released. Copying, cancellation, bounded memory, busy/locked
sources, concurrent commits, integrity checks and publication uncertainty need
specific tests. Do not expose a file as a validated accounting backup solely
because SQLite copied it: business/schema checks and restore reconciliation are
the application's responsibility. Attachments, platform support, interrupted
publication and cleanup remain part of the acceptance gate.

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
