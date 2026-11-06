# DBench

DBench is a Lambda database workbench demo. Run it from the repository root:

```sh
./lambda.exe demo dbench
```

It opens the bundled SQLite sample automatically. Choose a table or view to
browse its rows; click a column header to sort, or use the equality filter and
page controls. Filter the navigator by object name. Object details show the
columns, primary-key positions, indexes and outgoing foreign keys reported by
the driver; a foreign-key target can be
opened from that panel. Enter one SQL statement and choose **Run SQL**; **Fetch
more** retrieves further query rows. Supply positional parameters as a JSON
array, using the driver's placeholder syntax in SQL. **History** keeps the
last 20 distinct SQL texts in memory for the current session; it does not keep
bound parameter values, and can be cleared. SQL literals remain part of the
text. Cancel closes the current connection so a new session can be opened.

**New SQL** opens another tab on the current connection target, using its own
database session and result cursor. Up to four tabs can remain open; each keeps
its SQL, parameters, loaded rows, grid position, filter controls and staged
edits. A query can continue while its tab is hidden. Closing a tab cancels its session; apply or
revert that tab's staged changes before closing it. Connection URIs are kept
only in memory while needed for these sessions. Use the arrow keys to move
between tab buttons and Enter to select one.

Select or drag across result cells with the mouse, use the arrow, Home/End or Page keys to
navigate, and hold Shift to extend the logical selection. Ctrl/Cmd+C or **Copy
selection** copies the loaded rectangle as tab-separated text, including cells
outside the visible grid. Copy is limited to 100,000 cells and 1 MiB. Both this
grid and Sheet generate only visible rows and columns with a small overscan.
**Inspect value** shows the selected cell's declared and actual value kinds,
distinguishes null, empty text and binary, and limits displayed text to 4,096
characters. Use its column controls to inspect other values in the selected row.

To connect elsewhere, choose SQLite and an existing file, or choose MySQL or
PostgreSQL and enter a connection URI. The network drivers are provided by the
`rdb-drivers` Jube module. The form defaults to TLS `verify-full`; select a
weaker mode only when appropriate for the server. Credentials in the URI are
masked and are not saved as a profile. Use database credentials restricted to
read-only access: network sessions request read-only mode, but SQL and server
permissions ultimately determine what the account can change. SQLite sessions
open the selected file read-only and deny attachment of additional files. The
network bridge and staged file publication are currently POSIX-only; MySQL and
PostgreSQL connections and exports are unavailable on Windows. This demo has
been exercised on macOS; other platform builds still need qualification.

**Profiles** opens a connection form whose non-secret details can be saved to a
new JSON file and loaded later. Enter the SQLite path or the network host, port,
database, user, TLS mode and optional CA certificate. The password field is
never saved and is cleared when a profile is loaded; enter it before connecting.
Save creates a new file and does not replace an existing profile. Host values
are entered without URI escaping; enclose IPv6 addresses in brackets. Profiles
can contain sensitive server names, user names and file paths.

**Export results** creates a new CSV or JSON file from the retained rows or
selected rectangle. A query also offers **complete** export while its result is
still active: it writes the retained prefix and streams the remaining rows from
that same result, with progress, without rerunning the statement. Cancel requests
cancellation and closes the session; check the destination if cancellation races
with publication. JSON keeps positional column descriptors and rows,
including duplicate names; binary values are base64 strings. CSV quotes text,
uses an empty unquoted field for null, and prefixes base64 binary with
`base64:`. Loaded and selection exports are limited to 100,000 cells and a
conservative 16 MiB encoded-size budget. Complete exports use bounded batches and a staged file;
no export replaces an existing file.
For a table or view, **whole table (saved values)** streams a fresh query with
the applied filter and sort, independent of the current page. It reads saved
database values and does not include staged edits. Concurrent database changes
can affect the export; the current browsing page remains available afterward.

Table pages are limited to 256 rows. The next page uses the last row's ordered
values and primary key when they can be bound safely, including non-nullable
explicit sorts. Nullable sorts, unsupported key values and older page history
use offset paging.
Concurrent updates can still affect results between pages. Query results are
fetched incrementally. A private temporary spool retains up to 1,000,000 rows
and 256 MiB while the in-memory grid window stays within 2,048 rows and 16 MiB.
Scroll back to reload evicted rows; selection, copy and bounded export use their
logical positions. The spool is scoped to the tab's result and removed on result
replacement or session close. If a private spool cannot be opened, the query
falls back to the bounded in-memory cache and reports that limitation.
For a SQLite file opened explicitly in the connection form, **Stage value edit**
accepts text, JSON scalars or hexadecimal binary. **Stage row deletion** stages
a deletion, and **Stage insert** accepts a JSON object with an explicit complete
primary key. These actions apply to ordinary tables with a complete primary key.
Review the target,
values and parameterized SQL before **Apply**; deletions can cascade or fire
triggers. **Revert** discards the drafts. For updates and deletes, Apply checks
the original row with null-safe, binary comparisons. It commits the entire
batch in one SQLite transaction.
A conflict rolls the batch back. Text edits are limited to 65,536 characters
and binary edits to 4,096 bytes; SQLite applies the target column's affinity.
The bundled sample and network databases stay read-only in this editor.
Multiple cell updates to the same original row are combined; apply or revert
them before staging a deletion of that row.

For an explicitly opened SQLite file, **Review SQL write** also offers one
parameterized statement that does not return rows, including DDL. Review the
target, selected SQL and parameters before confirming. The current result is
finished before the transaction begins; a successful write refreshes the
navigator. Cancel can stop a pending or running write and rolls it back. SQL
scripts can be saved to a new file without overwriting an existing file.

Complete query export is available while the query result is active; it stops
growing the spool before draining the rest of the cursor. Spool and file I/O
can block on storage. The TLS bridge applies a
shared deadline to TCP connection and protocol upgrades, but DNS resolution and
some driver connection operations may still block; configure the driver
connection timeout in the URI. Cancellation cannot
interrupt every in-progress connection attempt. Use `connect_timeout=15` for
PostgreSQL or `connect-timeout=15` for MySQL (seconds). See [`lambda.io.rdb`](../../../doc/Lambda_Sys_Func.md#lambdaiordb)
for the session API, result limits, security and parameter restrictions.

The shared grid's views follow S12.1.3v2; its database session is scoped to a
single task under S12.4.1v3–S12.4.3 and host authorization under D11.1.2. It is
not a transaction scope under S12.4.4v4.

The sample is generated by `_make_sample.py`. Interaction replays are
`test/ui/dbench.json` and `test/ui/dbench_virtual_drag.json`; SQL and model
golden tests are in `tests/`. Tab interaction replays are `test/ui/dbench_tabs*.json`;
run `dbench_tabs_gc.json` with `LAMBDA_EXEC_BACKEND=jit` and
`LAMBDA_GC_FORCE_EVERY=1` for the ownership check. Run
`python3 test/demo/dbench/tests/_run_tabs_edit_ui.py` for staged edits across tabs.
Run `python3 test/demo/dbench/tests/_run_spool_window_ui.py` to verify scrolling,
copying and exports across an evicted query window.
Run `python3 test/demo/dbench/tests/_run_edit_ui.py`
for the writable SQLite replay on a disposable copy, or
`python3 test/demo/dbench/tests/_run_write_ui.py` for SQL review, cancellation
and rollback. Run `python3 test/demo/dbench/tests/_run_export_ui.py` for the
loaded export replay, `_run_stream_ui.py` for complete export and cancellation,
and `_run_profiles_ui.py` for profile persistence and error handling.
`_run_live_stream_ui.py` and `_run_live_profiles_ui.py` take
`DBENCH_TEST_POSTGRESQL_URI` and `DBENCH_TEST_MYSQL_URI` for live TLS replays.
The profile replay requires server certificates valid for the supplied hosts.
`_run_pg_timeout.py`
uses a local stalled server to check PostgreSQL authentication timeout handling.
The optional fixtures in
`tools/` take `DBENCH_TEST_DRIVER` and `DBENCH_TEST_URI` and require a
disposable database with at least one indexed table. `_live_keyset.ls` also
takes `DBENCH_TEST_TABLE`: create that disposable table with columns `a`, `b`,
and `marker`, primary key `(a,b)`, and rows `(1,'a','wanted')`,
`(1,'b','wanted')`, `(2,'a','wanted')`, and `(2,'b','other')`.
