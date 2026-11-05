# DBench and shared virtual grid implementation

## Current checkpoint

Design IDs below refer to this worktree's pre-61.0.0 formal design, except
central-IO ruling D11.1.2, which is D7.5.2 in this checkout. The proposal
records the newer numbering used by the parent checkout.

- **Goal:** Implement the [DBench proposal](../Lambda_Demo_DBench.md) in this
  worktree, including a two-axis virtual grid shared by Sheet and DBench, and
  SQLite, MySQL and PostgreSQL support. S12.1.3v2 governs pure views; S12.4.4v4
  governs SQLite's lexical transaction scope. D11.1.2 governs host authorization.
- **Worktree:** `temp/dbench-worktree`, branch `codex/dbench-virtual-grid`,
  based on `64be60daa1`. The parent checkout contains unrelated work. Build
  dependencies and corpora are linked as described in Developer Guide §7.2.
- **Grid:** shared sparse axis geometry, bounded row/cell generation and logical
  selection helpers and pointer drag transport render the viewport plus overscan in both demos. Sheet has
  sparse cell/value indexes, a 100,000 × 1,000 fixture, distant reveal, editing,
  and edge-drag selection. DBench supports click, drag, shift and keyboard
  selection, reveal and bounded rectangular copy. Sheet now caches sparse
  geometry indexes until dimensions, zoom or resize previews change; equality
  checking the geometry inputs can still scale with explicit overrides. A full
  shared interaction contract remains open.
  A nested Sheet viewport state experiment broke resize and selection because
  a parent update could recreate the child; the viewport is again owned by the
  stable application state, and all eight Sheet UI fixtures pass. Frame
  scheduling no longer mutates DOM attributes merely to record a pending frame.
  The grid uses the explicit `dom.request_frame_once` operation: a 100-event
  Sheet scroll burst now delivers one measurement callback rather than 101
  queued events (five frame slots instead of 103); its measured scroll frame
  fell from 146.5 ms to 110.6 ms. Ordinary frame requests remain independent.
  The frame delivery and native provider tests cover coalescing, cancellation,
  owner identity and reentrant scheduling (D7.5.3v2). On an Apple M4 release build, a
  100,000 × 1,000 Sheet measured 208.5 ms warm callback p95 after that rollback.
  Definite fixed-width tables now skip cell intrinsic measurement when no
  max-width constraint needs it: Sheet measured 90.3 ms afterward. Sharing
  default cell styles through CSS instead of generating them for each empty
  cell reduced the same Sheet profile to 81.4 ms. Applying layout and size
  containment to the independently sized scrollport reduced it to 75.5 ms;
  reusing selection bounds measured 73.6 ms, and the coalesced scheduler
  measured 71.6 ms. Retaining a stable cell attribute shape and reusing each
  cell's address measured 71.7 ms; skipping work that empty cells cannot use
  measured 69.8 ms. All nine Sheet UI replays passed after omitting the empty
  text element from blank cells. Both grids now retain the existing overscanned
  window while it covers the visible cells, and all 39 Sheet/DBench UI replays
  passed, including window reuse and boundary coverage. The later 61.2 ms and
  40-versus-13 timing comparisons were invalid: the attempted release switch
  left the shared debug `lambda.exe` in place. A forced and verified release
  relink was used for the replacement measurements below: both Sheet and DBench
  regenerated 13 times rather than 40 on the same 40 small diagonal moves.
  Per-regeneration p95 was 48.2 ms for Sheet and 21.9 ms for DBench before
  the table-cell style fix. Later release measurements are recorded below.
  A 2,560 × 100 DBench query
  measured 29.1 ms before containment and 22.5 ms after containment and
  selection-bound reuse. Both exceed the proposal's 16 ms
  target. Result-grid caches are bounded, but the canonical render Input arena
  still grows during repeated scrolling despite retirement of detached nodes;
  overall process memory has not been shown to plateau. D4.5.3v2 requires
  independently owned view occurrences, so solving that lifetime issue needs
  precise ownership rather than reusing a shared backing value.
  In one Sheet run, the runtime pool's reported live bytes rose from about
  83 MB to 225 MB over the sampled scrolling interval, alongside the Input
  arena's rise from about 20 MB to 58 MB. That run used a conditional cell
  attribute spread; restoring a stable shape reduced the final runtime-pool
  sample to about 72 MB, but it still rose from about 36 MB during the sampled
  scrolling interval. DOM-node retirement alone does not bound the other owners;
  D4.1.4v5 permits individual release only for pool allocations with established
  ownership. Sharing compatible document-owned attribute shapes subsequently
  eliminated the measured private-shape copies and reduced, but did not stop,
  runtime-pool growth during the 240-move replay.
  The fixed-table change passed six focused layout tests (including long
  later-row content, percentages, captions, spans and collapsed borders), the
  no-int-cast Radiant lint, and the Sheet/DBench UI fixtures. The latest Radiant
  baseline passed 9,216 checks (303 partial, two skipped), including all 791
  discovered UI checks, after the attribute-root change. The Lambda baseline
  passed all 8,771 checks.
- **RDB:** MySQL server cursors and PostgreSQL single-row mode stream results.
  Native open, schema, prepare, fetch and finalize workers pin connection
  lifetime and copy bounded batches. The new task-owned `lambda.io.rdb` session
  supports read-only open, schema, one statement, incremental fetch, finish and
  cancellation; the owner controls cleanup. SQLite read-only connections now
  deny additional database attachment and state-changing PRAGMAs. The shared
  SQLite column reader preserves BLOB bytes, including empty BLOBs, rather than
  returning SQL NULL. Network
  read-only mode is requested but requires restricted database credentials.
  Connect cannot currently interrupt an in-progress driver call. The TLS bridge
  applies a shared TCP connection and upgrade deadline, but DNS and driver operations need separate
  deadlines. PostgreSQL's URI `connect_timeout` was stripped from the libpq
  connection string while configuring the bridge; forwarding is fixed and a
  stalled-authentication regression now returns within the configured timeout.
  SQL parameters containing binary or
  embedded NUL are not supported by the generic ABI.
- **DBench:** the registered demo opens a bundled SQLite sample and can connect
  to all three engines. It provides table/view browsing, quoted server-side sort,
  equality filter, 256-row paging with keyset navigation for primary-key and
  non-nullable explicit ordering and bounded cursor history, an SQL editor with selected-statement
  execution and JSON-bound parameters, and incremental query fetching.
  On demand, object details expose column types and keys, indexes and outgoing
  foreign keys, including navigation to a referenced table.
  Explicitly opened SQLite files also support staging text, JSON scalar and
  bounded binary cell values, row deletions and JSON-object inserts with
  explicit keys in keyed tables.
  Apply checks every original value and its SQLite storage type for updates and
  deletes, verifies one matching and one affected row, and uses one S12.4.4v4
  transaction for the whole batch. Conflicts roll the batch back. The sample,
  network connections and views remain read-only; updates to multiple cells
  of the same original row are combined, with bounded draft size. Loaded or
  selected results export as bounded CSV/JSON using exclusive file creation.
  An active query can also stream its retained prefix and remaining result to
  CSV/JSON, with progress and cancellation, without rerunning SQL. A table or
  view can also stream all saved values with its applied filter and sort; the
  current page is retained, and staged edits are excluded. Publication uses a
  create-only staged file.
  The navigator, SQL editor and
  result columns have mouse and keyboard resize controls. A bounded inspector
  shows the selected value and distinguishes null, empty text and binary.
  In-memory history
  retains SQL text without bound parameter values, and scripts can be saved to
  a new file. Explicitly opened SQLite databases also support a reviewed single
  parameterized SQL write or DDL statement; cancellation rolls back running
  work and successful DDL refreshes the navigator.
  Up to four SQL tabs each own a document-scoped session and result cursor.
  Inactive tabs retain their SQL, parameters, results, scroll positions, applied
  and unapplied filter controls, and staged changes. Replies carry tab and
  session generations, and closing a tab cancels its task. Query rows use a
  task-owned anonymous spool bounded at 1,000,000 rows and 256 MiB, with a
  moving in-memory window bounded at 2,048 rows and 16 MiB. The
  connection form defaults remote TLS to verify-full, allows explicit weaker modes,
  and does not persist credentials. Named profiles save an explicit allowlist of
  non-secret connection fields to a new JSON file; loading clears the transient
  password before connecting. The demo catalog and preview are integrated.
- **Verification:** before the streamed-file changes, the full Lambda baseline
  passed 8,766 of 8,766 checks. A subsequent run exhausted disk during parallel
  suites after the security runtime suite passed 98 cases; its temporary YAML
  link was restored. Worktree release objects were removed to free space and the
  baseline was restarted with one concurrent suite. After fixing a missing
  error-propagation marker in the new export golden, it passed all 8,768 checks.
  The staged-inode binding fix was added after that host was built, so the
  subsequent tab milestone rerun passed all 8,769 checks with the staged-inode
  fix. The Radiant milestone baseline subsequently passed 9,208 checks (303
  partial, two skipped), including all 785 UI cases. The latest Radiant run passed
  9,214 checks (303 partial, two skipped), including 790 UI checks, after the
  spool, layout, whole-table export, profile and frame changes. The latest Lambda
  baseline passed all 8,771 checks with the profile module and MySQL URI fix.
  The profile UI and its error paths have separate focused coverage. Shared
  grid and Sheet model goldens passed; Sheet editor, selection, resize, handle,
  virtual scroll and edge-drag UI replays passed before the last shared selection
  refactor; replay affected cases. DBench's 57-event UI replay passed 34
  assertions, including virtual range selection; its edge-drag replay also
  passes. Its model/SQL goldens and real
  SQLite parameterized paging test passed. A live session and cancellation were
  verified against disposable PostgreSQL 18 and MySQL 8.4 instances; network
  UI browsing was exercised on both. The full RDB suite passed 216 cases before
  the latest finalize and SQLite read-only tests; after those changes all 219
  cases passed with live PostgreSQL and MySQL over verified TLS after the BLOB
  fix. Focused SQLite
  read-only, metadata and transaction tests pass, as does the procedural RDB
  golden including forced precise GC. SQLite edit goldens cover binary-collation
  conflicts and atomic rollback; the 81-event edit replay passes 35 assertions
  and verifies combined text, numeric, null-to-text and binary edits and inserts,
  including into an empty table. Reviewed SQL write replays verify bound and
  selected SQL, schema refresh, rejected multiple statements, and cancellation
  both before and during execution, with database rollback checked separately.
  Tab replays cover independent cursors, background query completion, closing
  during a query, discarded stale replies, keyboard navigation, the four-tab
  limit, virtual scroll and filter restoration, and staged-edit isolation. A
  tab lifecycle replay also passes JIT with collection on every allocation.
  PostgreSQL and MySQL replays each
  opened and switched between independent sessions over verified TLS.
  The export replay checks selected values and
  exclusive publication. Complete export replays verify a 300-row JSON result,
  a 12,000-row CSV result exceeding the grid cache, a 70,000-character multibyte
  value, and cancellation without a published or staged file. Live UI replays
  also compare a 300-row complete result from each of MySQL and PostgreSQL over
  verified TLS. The 145-case file
  suite includes incremental publication, conflicts, revocation and cleanup.
  The file-broker, update-stage and admission security suites also passed 26,
  14 and 54 tests respectively after the staged-inode change;
  the procedural export test also passes JIT with collection on every allocation.
  Resize, selected-SQL and null/empty/binary/value-limit
  inspector replays pass. The durable live session, metadata and
  cancellation fixtures passed against both network engines. Composite-key
  paging with mixed ascending and descending order passed against SQLite,
  PostgreSQL and MySQL; a SQLite test inserts a row before the cursor between
  pages. SQLite attachment
  and VACUUM cannot create a secondary file. Release Sheet and DBench viewport
  and drag replays all pass. On macOS 26.3, Apple M4, 16 GiB, a release build
  replayed a 1,000,000-row SQLite table at 1280 × 800, including a distant
  256-row query, in 2.80 seconds with 10 passing assertions; this wall time
  covers the complete replay and is not a p95 frame measurement. Reported child
  maximum RSS was 310,362,112 bytes on macOS, with another UI replay running
  concurrently. On both disposable network servers, verified TLS accepted a
  trusted matching certificate and rejected an untrusted one; verify-full also
  rejected a mismatched host while MySQL verify-ca accepted the trusted chain.
  Local stalled-server tests cover the PostgreSQL SSL request and handshake and
  both stalled and slowly delivered MySQL greetings; the upgrade deadline is
  shared across partial reads.
- **Build limitation:** `make build-test` previously failed linking unrelated
  `test_source_pos_bridge_gtest` due to missing GC/security runtime symbols;
  focused executables build. The aggregate baseline needs the main-checkout YAML
  corpus linked temporarily at `test/yaml`, then removed and replaced with an
  empty directory because Git expects a submodule path. Live tests must set
  `TMPDIR` to this worktree's `temp` for their bridge sockets.
- **Next:** cover further cancellation and security cases for streamed export,
  and continue remaining workbench
  interactions. Network
  transactional writes require the reviewed contract described in proposal §8;
  do not claim SQLite semantics cover network transactions. Repeat affected UI and native tests, then run the
  applicable baseline and release performance qualification. No `make release`
  in this worktree: use `make build-release-compile`.

### Streaming export implementation checkpoint

S12.4.2/S12.4.3 make the RDB result task-owned; D11.1.2 requires the file
destination to be authorized. Implement streaming publication with a held
`FileAnchor` and a private staged inode, reusing the existing atomic writer's
create-only publication checks. The same task must own the result and writer;
abort or cancellation removes the stage, and success publishes only after the
last batch. The incremental anchored writer and task-owned session interface
are implemented; focused publication, SQLite UI and verified-TLS MySQL and
PostgreSQL UI cases pass. Further security tests and broader regression gates
remain to qualify.

The staged writer now pins its inode and checks that the staging name still
identifies it before publishing or cleaning it up. A swapped-name regression
reproduced the previous failure and now passes in the full file suite. Like existing
anchored replacement, the check is not a compare-and-swap against external
writers, as documented in `lib/file.h`. The host was rebuilt and focused local,
  forced-GC and live-network export checks passed; the Lambda aggregate passed
  after this native change.

### Independent SQL tabs

Each tab owns a document-scoped session task and result cursor under
S12.4.2/S12.4.3. Replies carry a stable tab ID plus connection and request
generations; an inactive tab updates its stored result without accessing the
active tab's DOM. Four tabs bound the retained result caches. Closing a tab
cancels its task; switching tabs preserves its cursor. Replays cover switching
during connection and query, fetching more after returning, closing during a
  query, restoring scroll position, and preserving staged changes. Synthetic
  stale replies after tab close and reconnect are discarded; a focused lifecycle
  replay passes JIT under forced collection. Connection URIs stay in memory and
  are not saved as profiles. Continue repeated-replacement and memory qualification.

### Bounded result spool — implementation design

S12.4.2/S12.4.3 require each spool to follow its owning session task and result
generation; D11.1.2 governs its host file access. The retained-grid limit must
not end an arbitrary query or require re-execution. Create a quota-limited spool
under `temp/dbench` through a held `FileAnchor`, unlink its create-only inode
after acquisition, and keep the read/write descriptor scoped to the session so
close, cancellation or process exit removes the data. Store positional rows
with explicit null, boolean, integer, float, text and binary tags; bound row,
column, payload, index and total spool sizes. Reuse `lib/endian.h` and the RDB
value conversion helpers. An append failure must leave the retained prefix
readable and mark the result incomplete; never silently skip consumed rows.

Expose bounded random-access batches from the spool and keep only a moving row
window in the UI. Grid geometry, focus, selection and copy use logical row
indices rather than cache offsets. Read requests carry tab, result and viewport
generations so stale windows cannot replace newer ones. Complete export streams
the spooled prefix and the remaining live cursor without repeating SQL. Verify
typed round trips, quota and I/O failure, arbitrary backward jumps, selection
across cache eviction, tab close and forced-GC cleanup, and a large live query on
all three drivers. File work must not lend an anchor or stream to worker threads;
account for responsiveness when adding asynchronous I/O.

## Milestones

1. M0 — shared virtual grid and Sheet migration: core implemented; shared
   interaction completeness and performance qualification remain.
2. M1 — SQLite/MySQL/PostgreSQL connection and schema explorer: connection and
   tables/views/columns, index and foreign-key details, navigator filtering and
   named non-secret profiles implemented; other platform qualification remains.
3. M2 — bounded SQL results, cancellation and export: bounded incremental
   results, independent SQL tabs, cancellation and complete active-query streaming
   export, filtered whole-table export and the bounded result spool implemented;
   further security and responsiveness qualification remain.
4. M3 — controlled writes and administration: staged SQLite scalar and binary updates,
   keyed inserts and deletions with optimistic conflict checks and reviewed SQLite
   SQL/DDL execution implemented; additional administration and a reviewed network
   write contract remain.
5. M4 — demo integration and delivery qualification: catalog and preview
   integrated; documentation and final qualification pending.

### Whole-table export checkpoint

The existing identifier-quoting, parameter-binding and ordering builder now
also emits an unpaged table query; it rejects page offsets or keyset cursors in
that mode. The owning session executes that query and reuses the bounded
create-only streaming writer. The export includes the applied filter and sort,
reads saved values rather than staged changes, and preserves the displayed page.
Cancellation closes the session as for a complete query export. The SQLite UI
replay verified 3,000 filtered rows in descending order, subsequent paging,
and a header-only empty result. Separate live TLS replays on the disposable
PostgreSQL and MySQL servers each checked 600 filtered, descending rows and
continued paging. A focused cancellation replay passed; the combined stream
runner also checks that neither the destination nor a staged file remains.

### Connection profiles checkpoint

Profiles are explicit SQLite or network records with a name, target details and
TLS choice. Save publishes a new JSON file without replacement; load validates
and retains only the allowed fields. Password input is transient and cleared on
load, so it is never written to the profile (D11.1.2). Focused tests check JSON
contents and reconnection to SQLite, PostgreSQL and MySQL; the network replays
used disposable TLS-enabled servers. Profile host fields accept bracketed IPv6
and escape zone identifiers. The MySQL driver's URI parser now handles brackets
and rejects malformed, NUL-containing or truncated targets instead of silently
changing the destination. Native target-resolution tests exercise those cases.

### UI fixture preparation checkpoint

The post-tabs Lambda baseline passed 8,769/8,769 checks. The Radiant
baseline completed with six DBench UI failures because the standard UI runner
launched replay files without the disposable SQLite databases created by their
standalone verification scripts. Use the harness's prepared-document facility
and a shared fixture preparer so the normal suite creates the same databases;
export fixtures must also start with absent output files for repeatable runs.
The remaining Radiant runners completed without other reported failures. Rerun
the affected UI cases and then the Radiant baseline after the fix.

The twelve affected standard UI replays now pass, including all six previously
failing cases. The standalone write/rollback runner also passes with the shared
fixture preparer. A fresh Radiant baseline is in progress. The native anonymous
spool's two focused tests pass for typed random reads, quota handling and cleanup.
The task-scoped fetch/read binding and its Lambda lifecycle test are being added;
they have not yet been compiled or connected to the result grid. Spool I/O is
currently synchronous and must be qualified for responsiveness before claiming
large-result support.

The fresh Radiant run's complete UI automation suite now passes 785/785;
the remaining native/layout/render runners are still executing. The new spool
binding compiles. A separate temporary executable is being built for its
integration test so the baseline continues to use the same executable throughout.

The task-scoped spool binding compiles and its Lambda integration test passes
both the default executor and JIT with forced GC. It preserves wide integers,
floats, nulls, text and blobs; rejects out-of-range and cross-task reads; keeps
the prefix after `finish`; and discards it on the next statement. The existing
UI still uses the old 2,048-row cache. Next, move query-mode grid coordinates to
logical row indices with a bounded cache window, request missing windows from
the owning session with viewport tokens, and stream exports from the spool
prefix plus the cursor. Preserve table paging and tab isolation; verify selection,
copy, scrolling and cancellation across eviction before removing the limit.

The corrected Radiant milestone baseline completed successfully: 9,208 passed,
303 partial and 2 skipped, including 785/785 UI automation checks. The baseline
used the pre-spool executable; spool binding verification used a separate build.

The moving query window is now connected to the spool. Logical selection and
copy can cross eviction; window replies carry query and viewport tokens, and a
return to cached rows invalidates an older pending window. Complete export reads
the retained prefix, seals it, and streams the rest of the same cursor; loaded
and selection exports read the spool without ending the cursor. The 26 DBench
UI replays pass. A 2,560-row SQLite replay verifies eviction, backward scrolling,
stale reply rejection, cross-window copy, selection export, and later loaded
export; its complete JSON has 3,000 verified rows. Expanded live TLS replays on
PostgreSQL and MySQL each pass 26 assertions and verify all 3,000 exported rows.
Spool I/O remains synchronous and capped; release responsiveness, broader
resource-lifetime and final aggregate regression gates remain to qualify.

The complete 2,560-row copy-and-export replay exceeded its 180-second wrapper
limit with `LAMBDA_GC_FORCE_EVERY=1`; the orphaned child was identified and
stopped. A reduced forced-GC JIT replay then crossed the cache boundary, reloaded
the first row and rejected a stale reply with 20/20 assertions; the independent
spool lifecycle golden and tab-cancellation replay also passed forced GC. The
full copy/export workload under collection on every allocation is still unqualified.
A later replay passed 34 assertions including disconnecting after eviction and
copying the surviving in-memory window.

### P0 — repeated virtual-grid scrolling

Radiant experiment: test whether a same-position, same-tag projection can
retain its native element when only its string `id` and nonvisual metadata
change. Today `collect_render_frontier` treats `id` as a replacement boundary,
so a virtual-window shift rebuilds otherwise empty rows and cells. D4.5.3v2
permits retained presentation if the DOM remains correct. Acceptance: retained
node and control identity, updated ID lookup and source routing, CSS selectors
that depend on both old and new IDs, mutation observers, focus/selection,
ordinary nonmatching replacements, and Sheet/DBench scroll, resize and edit
replays. Incremental budget: a focused projection fixture and neighboring DOM
tests, then both demo suites and the release scrolling profile. Keep the change
only if the measured benefit and correctness justify it. This does not establish
the separate retained-source memory plateau.

The ID-retention experiment passed the focused CSS, mutation-observer, source
routing and retained-input checks and 42 neighboring UI fixtures, but was
reverted: on the verified release build its large-jump Sheet callback p95 rose
from 52.4 to 107.5 ms. Profiling the small-scroll case showed 18 root layouts
for 13 transforms versus 13 layouts before. Retained ID mutations enter the DOM
mutation ledger, which the incremental renderer reconciles before it splices
the remaining changed subtrees; that creates an extra layout pass. A safe future
approach must combine those invalidations without losing earlier imperative
mutations or ID-dependent selector and observer behavior. The working renderer
keeps the prior replacement boundary until such batching is qualified.

The generated-content preparation pass resolved ordinary table cells before
their cell storage role existed, so the table formatter resolved them again.
Preparing the role for in-flow cells with a real table owner keeps the cached
style usable in both passes; other table fixups and out-of-flow cells retain
their prior path. A four-cell fixture reduced storage-role misses from five to
one, and a Sheet update eliminated 478 cell-role transitions. Focused fixed
table and dynamic width tests pass. On the same verified release profiles,
large-jump Sheet callback p95 fell from 52.4 to 40.7 ms; small-scroll
per-regeneration p95 fell from 48.2 to 41.2 ms for Sheet and from 21.9 to
16.2 ms for DBench. Both scroll fixtures passed their assertions. The 16 ms
target and the separate memory plateau remain unqualified. The full Radiant
baseline passed 9,216 checks with 303 partial and two skipped, including all
791 UI replays; the no-int-cast lint passed.

`elmt_with_type` allocates a full `DomElement` for UI render-source values
before D4.5.3v2 makes a distinct DOM occurrence. Compact source values alone
are unsafe: `MarkEditor::dom_relink_children` assumes that an inline UI edit's
parent has embedded DOM storage, and other editor paths infer node storage from
arena ownership. A representation change needs an explicit source/occurrence
contract throughout mutation and rebinding, plus nested-template and forced-GC
qualification. It would reduce each allocation, but would not establish the
memory plateau; obsolete results and shape data still need proven lifetimes.
The render map indexes descendants as well as template roots, while DOM node
records can retain a distinct backing source. An old source may also be shared
with another occurrence or retained by Lambda code. Therefore neither a DOM
retirement callback nor replacement of one render-map entry proves that source
storage is dead. A follow-up ownership design must trace those edges and make
escaped values safe under D4.5.2 and D5.3.3 before reclaiming a generation
(D5.5.2 and D6.3.3 in the 61.0.0 numbering);
deleting reverse-map entries or recycling source allocations on node retirement
alone would break identity and event routing.

Frame profiling now reports forward and reverse render-map entry counts without
creating a map for documents that have none. On the 40 large-jump Sheet replay,
the forward map stayed at one entry while reverse entries grew from 13,865 to
56,205, alongside Input arena growth from 10.9 to 40.4 MB. This identifies a
second retained structure; it does not prove those source identities can be
erased, because source-position queries, shared results and detached live nodes
can still address them. Qualify the routing and source lifetimes separately.
The nine focused render-map ownership and retransformation tests pass with the
counter, including a check that forward counts fall on retirement.

Next ownership milestone: account for render roots, nested template entries,
DOM backing-source references and values retained by scripts separately. Define
how each edge acquires and releases a source generation, including equal-result
rebinding, overlapping fragments, detached nodes and document teardown. Prove
those lifetimes with nested templates, shared results, DOM mutation and forced
GC before measuring repeated scrolls and tab replacement. Then profile the
remaining viewport layout against the recorded release fixture; reducing each
allocation is not evidence that either memory or frame-time acceptance passed.
`UiAttributeRoots` was measured separately below; eliminating its unnecessary
entries did not change the dominant pool and arena growth. Next, attribute live
runtime-pool growth to allocation sites, accounting for frees, and separately
attribute result-arena allocations to source values, content and attributes.
Use opt-in temporary instrumentation and the existing 240-move release replay;
do not recycle source storage until all escaping owners are accounted for
(D4.5.2 and D5.3.3).

### P0 — live allocation attribution

Identify which allocation sites account for the repeated Sheet scroll's
runtime-pool growth before choosing a reclamation or reuse change. Sample only
the document runtime's pool, track sampled frees as well as allocations, and
compare snapshots across the measured scroll interval; report sampling and any
untracked reallocations. Symbolize the allocation sites, reproduce the dominant
owner with a focused fixture, and preserve its ownership contract (D4.1.4v5,
D4.5.2, D5.3.3). The temporary probe must be removed after attribution. If the
remaining Input arena is a separate owner, measure it separately rather than
ascribing its growth to the pool. Verification budget: one release replay for
attribution, focused tests for any root-cause fix, and the affected baselines.

The temporary 1-in-64 live-allocation sample attributed 99.8% of the sampled
pool growth to shape entries and hash tables from field-type rebuilds. A second
probe identified repeated `globalThis.event` writes during scroll dispatch:
each change between undefined and an event object rebuilt the 360-field JS
global shape. This is one instance of a general dynamic-JS-property problem.
For packed, non-constructor ordinary JS properties, an incompatible type
change now selects the existing `any` TypedItem lane, preserving the dynamic
value contract and avoiding later rebuilds. Initial numeric writes keep the
numeric lane and its compatible integer-to-float stores (D3.4.5, D3.2.4v5,
D4.5.2). The temporary probes have been removed. In the instrumented
release-profile replay, the late-scroll runtime
pool had 3.6 MB live versus 130.7 MB before; all 252 assertions passed. The
Input arena was unchanged at 61.1 MB live at that sample, so its independent
growth and the overall memory-plateau gate remain open. The focused mixed-value,
descriptor, forced-GC and nested-event checks pass, as does the 851-test JS
MVP suite. Uninstrumented release replays pass all 252 Sheet and 62 DBench
assertions; Sheet's late-scroll runtime pool is 3.56 MB live. The aggregate
Lambda baseline passed 8,772 checks. The Radiant baseline had one failure:
`dbench_tabs_edit` passed 10 of 11 assertions; the other 9,215 checks passed
with 303 partial and two skipped. The failing assertion was not identified in
the captured baseline output. Nine standalone runs and all 39 DBench/Sheet UI
cases under four-worker scheduling subsequently passed. The full Radiant
baseline repeat passed 9,216 checks with 303 partial and two skipped, including
all 791 UI cases. The original failure's cause remains unconfirmed. The
uninstrumented Sheet run measured 109.6 ms p95 for frames over 5 ms, versus
43.0 ms in the earlier release replay. A second uninstrumented release run
passed all 252 assertions and measured 40.5 ms p95 for the same frame filter;
the 109.6 ms sample did not reproduce. The 16 ms target remains open. The
mixed-value and nested-event tests also pass with GC forced at every allocation.

### P0 — result-arena allocation attribution

Sample allocations in the document Input arena during the same 240-move Sheet
replay, accounting for retirement and separately reporting scratch-scope or
reset activity. Attribute growth by caller and allocation size, and compare it
with the arena's active-byte counter before selecting a change. Source elements,
their attributes and text, render-map entries, DOM backing edges, shared results
and script-escaped values have different lifetimes; no allocation may be reused
until every owner has released it (D4.1.4v5, D4.5.2, D5.3.3). Remove the
temporary probe after measurement. Verification budget: one release profile,
focused lifetime and routing tests for any fix, and affected baseline gates.

### P0 — attribute-root accounting

Measure the retained UI attribute-root count and value representations during
the existing Sheet and DBench scrolling fixtures. Avoid roots for self-contained
values and same-document arena containers, which a GC root cannot retain;
pointer-backed scalar homes, GC values, escaped models and foreign ownership
still require their existing lifetimes. Acceptance:
precise GC and poisoned-GC tests cover inline numbers and pointer-backed values,
including dynamic attribute replacement and distinct document arenas; the same
release fixtures quantify root and memory growth. Use focused native and demo
tests first, then the Lambda and Radiant baseline gates for any retained runtime
change (D4.5.2, D5.3.3).

The verified release Sheet replay retained at least 4,352 attribute roots after
240 moves; every sampled root was a self-contained inline float. The DBench
wide-query setup retained at least 1,280 roots, all sampled as element values
in the active document arena. A GC root does not retain or trace an arena
allocation, so `ui_prepare_element_field` now omits these two categories while
keeping the existing root entries for foreign-arena values, boxed numeric values
and GC-managed objects.
The new `ui_active_roots` frame counter observes the active script context
without creating a root vector; transferred loader roots are not included.
Both release replays passed, with zero active roots in all sampled scrolling
frames. The 240-move Sheet replay still grew Input active bytes from 6.1 to
65.3 MB and runtime-pool live bytes from 29.5 to 130.7 MB; reverse entries
still rose from 7,361 to 94,433. This removes unnecessary roots but does not
solve the allocation plateau. Six focused native tests, 41 UI fixtures, and
forced-GC JIT handler and DBench tab replays pass. The full Lambda baseline
passed all 8,771 checks, and the Radiant baseline passed 9,216 checks with
303 partial and two skipped; all 791 UI replays passed. The no-int-cast lint
also passed.

### P0 — repeated UI attribute-shape allocation

The release shape profiler reports 7,541 private shape copies after the Sheet
warmup and 30,701 after 40 further large scrolls: 23,160 extra copies and
68,240 copied entries. The copies came from runtime-key literals in the ribbon,
including SVG icons and buttons, not from the mounted grid cells. Acceptance:
repeated UI element construction shares compatible
immutable shapes without changing field order, type, namespace, content,
attribute-value lifetime or document/loader teardown; mixed attributes,
mutation and forced-GC behavior must remain correct. Check the shape profiler
and runtime-pool growth on the same replay, then run focused constructor,
render-map and Sheet/DBench tests and the affected baselines. This milestone
addresses shape growth only; the independent source-value arena plateau remains
open under D4.5.2 and D4.5.3v2.

Computed-key literals mark their shape `has_spread` before any stored spread
slot exists; the transition tree previously rejected the marker even when all
actual fields were named. Compatible shapes now share D3.4.3v5 transitions
owned by the document Input, and unnamed spread slots remain ineligible. The
marker is part of shape identity and survives D3.4.5 retyping and the deletion
fallback. Native tests cover different documents, distinct marker values, retyping, unnamed slots,
deletion and GC-retained attribute values. The focused 41 Sheet/DBench and
projection UI fixtures, four JIT fixtures including a forced-GC handler, and
dynamic-literal goldens pass. The DBench tab lifecycle also passed JIT with
collection and freed-memory poisoning forced on every allocation. On the same
verified release replay, the 40 additional scrolls
make zero private shape copies, down from 23,160. Repeated large-jump p95 was
40.4 ms versus 40.7 ms before. In the 240-move Sheet replay, runtime-pool live
bytes rose from 29.5 to 130.7 MB, versus 34.5 to 164.8 MB before; Input active
bytes still rose from 6.1 to 65.3 MB and reverse entries from 7,361 to 94,433.
DBench's 40-small-move replay passed; per-regeneration p95 measured 43.9 ms
on the first run and 18.9 ms on repeat, versus 16.2 ms in the earlier profile.
The spread between runs prevents attributing that timing difference confidently.
The shape change reduces growth but does not establish bounded memory or meet
the 16 ms target. The full Lambda baseline passed all 8,771 checks after this
change; the Radiant baseline passed 9,216 checks with 303 partial and two skipped,
including all 791 UI replays.

The shared `grid.covers` operation checks whether an unpadded viewport is still
covered by the rendered overscan, including an explicit empty-viewport case.
Sheet and DBench skip their render-state update in that case; DBench still uses
the actual measured viewport to request missing spool rows. The initial
before/after timing replay used a debug executable because the shared binary
was not relinked after the debug baseline. Its timing and allocation numbers
are invalid as release evidence. The bounded window, hidden viewport, large
jump, editing and cache-window checks pass.
Both demos also rebuild when the viewport dimensions change, so shrinking a
pane does not retain a window sized for the old viewport; focused resize tests
passed after this correction. The subsequent Radiant baseline passed 9,215
checks (303 partial, two skipped), including all 791 UI fixtures; the Lambda
baseline passed all 8,771 checks.

Replacement measurements: `make build-release-compile JOBS=4` forced the
release relink, and the Developer Guide §7.3 debug-string probe returned zero.
On macOS 26.3, Apple M4/16 GiB, the 1440×900 sparse 100,000×1,000 Sheet
replay used eight warm-ups and 40 one-row/12-pixel diagonal scrolls. The prior
window policy regenerated 40 times, p95 48.6 ms, with Input active bytes
10.8→40.1 MB and reverse entries 13,841→56,313. Window reuse regenerated
13 times, p95 48.2 ms, with Input 6.2→16.0 MB and reverse entries
7,361→21,529. These are callback timings, not GPU presentation times.

On the 1280×800 DBench 2,560-row × 100-column cached-result replay, the same
40 small moves produced 40 versus 13 regenerations; p95 was 21.0 versus
21.9 ms. Input active bytes grew 262.8→274.9 MB versus 260.9→264.9 MB;
reverse entries grew 633,499→656,977 versus 629,881→637,720. Both replays
passed their UI assertions. Separate template-profile runs directly confirmed
40 versus 13 transforms and patches for both applications. A longer 240-move
Sheet replay still grew Input
6.2→66.4 MB, reverse entries 7,361→94,433 and runtime-pool live bytes
34.5→164.8 MB, with regeneration p95 52.6 ms. Reuse reduces update frequency;
the p95 and bounded-memory gates remain open. The original large-jump replay
still required 40 transforms and measured 52.4 ms p95 on the verified release
build.

The sparse Sheet avoids formatting empty values, computing absent styles, and
checking a neighboring cell for text that cannot spill. On the same release
fixture its 40 mutating callbacks measured p95 69.8 ms; the Input arena still
reached 52.6 MB at the final sample. Focused Sheet formatting, selection and
virtual-grid replays passed. These numbers are an incremental improvement, not
the required frame-time or retained-memory acceptance.

Removing the empty text span from blank Sheet cells retains their table cell,
selection and edit controls. All nine Sheet UI replays pass, including a check
for blank-cell geometry and populated text. The subsequently reported 61.2 ms
profile accidentally used a debug executable and is withdrawn; remeasure it.
The initial Radiant rerun was stopped after a sandboxed headless 3D fixture
stalled. Rerunning with graphics access passed 9,214 checks, with 303 partial
and two skipped, including all 790 UI checks and the million-row SQLite fixture.

The live PostgreSQL/MySQL driver corpus initially exposed an obsolete TLS probe:
`or` did not handle the `input` error outcome and the compiler rejected the
probe before connecting. The harness now handles that outcome explicitly under
S7.6.1v9,
reports nonzero child exits without printing credentials, and removes its
private generated scripts after each run. With isolated temporary databases and
verified TLS certificates, the corrected corpus passed all 26 checks with no
backend skipped, including its TLS matrix and cancellation cases.
The subsequent full Lambda baseline passed all 8,771 checks; its temporary YAML
corpus link was removed and the worktree's empty submodule placeholder restored.

The release-only Sheet profile on macOS 26.3, Apple M4/16 GiB, viewport
1440×900, sparse 100,000×1,000 fixture, 8 warm-up moves and 40 diagonal
scrolls measured 40 mutating frame callbacks: p50 267.4 ms, p95 490.7 ms;
layout p95 273.5 ms across all 80 callbacks. Live DOM bytes stayed near 16–17
MiB but view-pool bytes grew from 27.8 to 72.9 MB and result Input arena bytes
from 16.0 to 59.1 MB. These are callback/commit timings, not GPU-present times;
they miss the proposed 16 ms target. The original replay is
`temp/dbench-sheet-scroll-bench.json`, profile `temp/dbench-sheet-scroll-bench.out`.

Acceptance: repeated large-sheet diagonal scroll retains only viewport cells,
preserves selection/editor/focus/resize, has bounded retained memory, and is
profiled on the same release fixture; DBench must receive the same assessment.
Initial hypothesis: changing the root template's viewport retransforms the whole
workspace into the canonical result Input and triggers broad layout. Trace the
allocation and layout owners; first isolate viewport updates to the grid template,
then measure before deciding whether shared runtime ownership needs repair.
Incremental budget: targeted Sheet scroll/edit/drag replays and release profile;
then affected DBench replays and the required Radiant/Lambda milestone baselines.

The release wide DBench query profile (2,560 fetched rows × 100 columns,
1280×800, 40 warm diagonal moves) passed 62 UI assertions but measured p95
89.8 ms for mutating viewport callbacks. Result Input usage grew from about
262 to 274 MB during those moves, after query setup. Sheet's viewport state
was then moved into its grid component: virtual-grid, drag and editor replays
passed, and the same Sheet profile improved to p95 273.9 ms. Its Input arena
and view pool still grew through 40 moves; a five-second idle did not reduce
them. Added opt-in frame-profile retirement and arena counters to distinguish
pending/reused storage from live retained allocations (D4.5.3v2).

Retirement instrumentation narrows the memory issue: during 40 Sheet moves,
retired nodes rose from 8,080 to 48,059 and arena reuse hits from 35,840 to
213,129; pending retirement bytes remained zero. Nevertheless active Input
arena bytes rose from 12.7 to 46.6 MB and view-pool bytes from 28.0 to 73.2 MB,
remaining high after idle. The DOM boundary's D4.5.3v2 occurrence copy and
separately allocated render values/payloads need an ownership audit; this is
not explained by an idle retirement backlog. Do not bypass the deep-copy rule
or recycle potentially shared source values without proving their ownership.

The pending-frame HTML attribute was a second cause of avoidable reflow: setting
and clearing it each scheduled a visual DOM mutation. Replacing that bookkeeping
with direct frame requests and ignoring unchanged windows passed the focused
scroll/drag replays and a 100-event scroll burst. On the same release profiles,
Sheet warm callback p95 fell to 97.5 ms (80 callbacks, including idle callbacks)
and DBench to 27.5 ms; both still miss the proposed 16 ms target. Sheet's active
Input arena still grew by about 34 MB over 40 moves. Profiler attribution now
shows each Sheet update spends roughly 6–8 ms in template evaluation, 11–18 ms
patching and 68–79 ms in layout, mostly the grid's table/flex work. Address the
shared allocation and retained-layout causes rather than claiming the gate passed.
