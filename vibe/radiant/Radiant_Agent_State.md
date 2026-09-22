# Radiant Agent State — Unified Persistent Browser State

**Status:** implemented — 2026-09-22. This is a continuation of the Radiant
state ledger: RS17–RS27 extend `Radiant_Design_State_Management.md`'s
RS1–RS16. The v1 SQLite state store and browser integration are landed.

**Formal-spec linkage:** no `S#` ruling covers native browser-profile data.
`D4.2.1v3` and `D4.2.3` require one explicit owner and lifetime for the
profile store; `D1.9` and `D1.10` require malformed state to fail closed and
each invariant below to have an enforcement gate. No change to either formal
specification is proposed: this is a Radiant host-service design.

**Scope:** persist browser and agent browsing history, HTTP cookies,
`localStorage`, and `sessionStorage` in one SQLite database below the
configured network-cache directory. The database is the authority for these
four classes of state; memory is only an execution cache.

## 1. Decision summary

Radiant gains a profile-owned `RadiantStateStore`, opened before the first
top-level network request and owned by the `BrowsingSession`, not by a
`DomDocument` or `NetworkResourceManager`.

```
configured network-cache directory
└── radiant_state.sqlite3       # one authoritative browser/agent state database
    ├── browsing history
    ├── cookies
    ├── local storage
    └── session storage
```

The default interactive browser uses its durable default profile. An agent
uses an explicit profile identifier through
`session_create_for_profile(...)`: reusing an identifier deliberately resumes
its authenticated state; a new identifier starts isolated. There is no
ambient shared agent state.

The existing SQLite amalgamation is already compiled into Lambda
(`lib/sqlite/sqlite3.c`; SQLite 3.45.3). `lambda/network/radiant_state_store.*`
uses that integrated module directly through `sqlite3_*`; it neither adds a
dependency nor changes vendor code. `lib/rdb_sqlite.c` remains the
document-facing relational-data adapter. It is not the right abstraction for
the state store's small transactional writes and private schema.

### RS17 — Profile state has session lifetime, not document lifetime

`BrowsingSession` owns the profile, the `RadiantStateStore`, and the one
shared `CookieJar`. A document and its resource manager borrow them. Replacing
a page therefore cannot discard session cookies, create a second jar, or reset
the history owner. This is the lifetime boundary required by `D4.2.3`.

Before this change, `BrowsingSession` owned only an in-memory `HistoryEntry[]`,
while `resource_manager_create()` created a jar at `./temp/cookies.dat` for
each document. Page replacement therefore dropped session cookies and history
was lost at process exit. `dom_batch_reset()` also cleared the current realm's
two Web Storage maps. Those ownership defects are now removed.

## 2. Identity and lifetime

### RS18 — Profile, browsing-context, origin, and session are distinct

| Identity | Lifetime | Used by |
|---|---|---|
| Profile | named durable user or agent identity | all four stores |
| Browser session | one Radiant process/session instance | session cookies and `sessionStorage` cleanup |
| Top-level browsing context | one browser window/tab or one agent browsing run | back/forward stack and `sessionStorage` partition |
| Origin | canonical `scheme`, host, and effective port | Web Storage isolation |

Each `BrowsingSession` receives an unguessable `session_id` and an unguessable
`browsing_context_id`. A profile name is configuration, not a database key by
itself: the store maps it to an internal profile row. Agents must be given a
profile identifier by their host configuration; a shared default is never
silently selected for them.

Origins are derived by the existing URL parser before a storage lookup. HTTP
and HTTPS origins use normalized scheme, host, and effective port. Opaque URLs
and `file:` do not receive durable Web Storage in v1; they retain the current
in-memory behavior only when the host explicitly enables it. Cookie matching
continues to use the existing URL, domain, path, secure, public-suffix, and
SameSite policy rather than treating an origin as a cookie key.

`localStorage` belongs to `(profile, origin)`. `sessionStorage` belongs to
`(session_id, browsing_context_id, origin)`: it survives same-context reloads
and cross-origin navigation away and back, but is not shared with another
tab/agent context and is removed when this browser session ends. Storing
session rows in SQLite while the session runs satisfies the one-store rule;
it does not change Web Storage's session lifetime into permanent storage.

## 3. SQLite store

### RS19 — One authoritative file, transactional schema, no cache eviction

The state path is `<network-cache-dir>/radiant_state.sqlite3`. The profile
bootstrap receives the network cache directory (the current browser default is
`./temp/cache`); it never uses the former `./temp/cookies.dat` path. The state
database is colocated with the network cache for profile
configuration and cleanup discovery, but is not a resource-cache entry:
`EnhancedFileCache` LRU eviction and ordinary HTTP-cache clearing must never
delete it. Browser-data clearing is an explicit, separately scoped operation.

The connection enables foreign keys, uses prepared statements for every value,
and carries a schema version in `PRAGMA user_version`. The initial mode is
`journal_mode=DELETE` and `synchronous=FULL`. SQLite may briefly create its
rollback journal during an atomic commit, but `radiant_state.sqlite3` remains
the sole durable state database; WAL is deliberately not used because it
leaves persistent `-wal` and `-shm` companions in the cache directory.

Schema setup runs in one transaction before profile data is exposed. An
unknown newer schema version, opening failure, or integrity failure logs the
precise failure and runs that profile without persistence. It must not truncate,
replace, or silently recreate a malformed database. This is the fail-closed
behavior required by `D1.9`.

The cache directory and database need user-private permissions. Cookies and
local storage are sensitive credentials; this design does not add syncing,
export, telemetry, or a script-visible path to the file.

### RS20 — Schema v1

The physical schema is deliberately narrow and relational. Timestamp columns
are UTC Unix milliseconds; boolean columns are constrained to `0` or `1`.

```sql
CREATE TABLE profiles (
    profile_id       INTEGER PRIMARY KEY,
    profile_name     TEXT NOT NULL UNIQUE,
    created_at_ms    INTEGER NOT NULL,
    last_opened_at_ms INTEGER NOT NULL
);

CREATE TABLE browsing_contexts (
    context_id       TEXT PRIMARY KEY,
    profile_id       INTEGER NOT NULL REFERENCES profiles(profile_id),
    session_id       TEXT NOT NULL,
    kind             TEXT NOT NULL,       -- interactive top-level context in v1
    current_index    INTEGER NOT NULL DEFAULT -1,
    is_open          INTEGER NOT NULL CHECK (is_open IN (0, 1)),
    created_at_ms    INTEGER NOT NULL,
    updated_at_ms    INTEGER NOT NULL
);

CREATE TABLE visits (
    visit_id         INTEGER PRIMARY KEY,
    profile_id       INTEGER NOT NULL REFERENCES profiles(profile_id),
    context_id       TEXT NOT NULL REFERENCES browsing_contexts(context_id),
    url              TEXT NOT NULL,
    title            TEXT,
    transition       TEXT NOT NULL,
    visited_at_ms    INTEGER NOT NULL
);

CREATE TABLE navigation_entries (
    context_id       TEXT NOT NULL REFERENCES browsing_contexts(context_id),
    entry_index      INTEGER NOT NULL,
    visit_id         INTEGER NOT NULL REFERENCES visits(visit_id),
    url              TEXT NOT NULL,
    title            TEXT,
    scroll_x         REAL NOT NULL DEFAULT 0,
    scroll_y         REAL NOT NULL DEFAULT 0,
    PRIMARY KEY (context_id, entry_index)
);

CREATE TABLE cookies (
    profile_id       INTEGER NOT NULL REFERENCES profiles(profile_id),
    name             TEXT NOT NULL,
    value            TEXT NOT NULL,
    domain           TEXT NOT NULL,
    path             TEXT NOT NULL,
    expires_at_ms    INTEGER,              -- NULL means a session cookie
    secure           INTEGER NOT NULL CHECK (secure IN (0, 1)),
    http_only        INTEGER NOT NULL CHECK (http_only IN (0, 1)),
    same_site        INTEGER NOT NULL,
    creation_at_ms   INTEGER NOT NULL,
    session_id       TEXT,
    PRIMARY KEY (profile_id, name, domain, path)
);

CREATE TABLE local_storage (
    profile_id       INTEGER NOT NULL REFERENCES profiles(profile_id),
    origin           TEXT NOT NULL,
    key              TEXT NOT NULL,
    value            TEXT NOT NULL,
    ordinal          INTEGER NOT NULL,
    modified_at_ms   INTEGER NOT NULL,
    PRIMARY KEY (profile_id, origin, key)
);

CREATE TABLE session_storage (
    session_id       TEXT NOT NULL,
    context_id       TEXT NOT NULL REFERENCES browsing_contexts(context_id),
    origin           TEXT NOT NULL,
    key              TEXT NOT NULL,
    value            TEXT NOT NULL,
    ordinal          INTEGER NOT NULL,
    modified_at_ms   INTEGER NOT NULL,
    PRIMARY KEY (session_id, context_id, origin, key)
);
```

Indexes cover `visits(profile_id, visited_at_ms DESC)`,
`cookies(profile_id, expires_at_ms)`, and each storage table's lookup prefix.
The implementation may add an internal `schema_meta` table only when a
migration needs metadata that `user_version` cannot express.

## 4. State behavior

### RS21 — Cookies use one profile jar and one database authority

The `CookieJar` remains the thread-safe, in-memory matching cache used by curl
workers. Its persistence backend is `RadiantStateStore`; TSV load/save methods
and the `storage_path` field are removed. At profile open it loads unexpired
persistent cookies. A fresh unguessable `session_id` means prior-session rows
are never observable; clean session close deletes the current session's cookie
rows in a transaction. Persistent cookies remain.

`Set-Cookie` handling preserves all existing fields: name, value, domain,
path, expiry, secure, HttpOnly, SameSite, and creation time. Replacing or
expiring a cookie changes both the matching cache and its database row as one
logical operation. Cookie state becomes durable before the request completion
is reported to the UI; the in-memory jar still serves redirects immediately.

`document.cookie` is routed through the same jar and store. Reads filter
HttpOnly cookies; writes use the document URL and the normal Set-Cookie parser
and policy path. The top-level loader, both curl schedulers, and Fetch/XHR
enable curl's cookie engine and import the shared jar, so redirects, network
responses, and script see one coherent cookie state rather than independent
browser and network stores.

### RS22 — Web Storage is origin-scoped and synchronous to script

The existing `JsDomStorageState` remains a GC-safe realm cache and `Storage`
object identity remains realm-local. On document/realm binding it loads the
appropriate origin rows from the state store, retaining `ordinal` so
`Storage.key(n)` has stable insertion order. `dom_batch_reset()` releases only
the cache and JS wrappers; it must not erase database-backed `localStorage` or
the active browsing context's `sessionStorage`.

`setItem`, `removeItem`, and `clear` update the database first and then the
realm cache, in a transaction that either changes both views or neither.
Values and keys remain strings. A failed write is observable as the normal
`QuotaExceededError` path; it must not silently return success, as the current
best-effort in-memory allocation path can do.

v1 enforces a 5 MiB limit per `(profile, origin)` local store and a 50 MiB
aggregate local-store limit per profile. `sessionStorage` uses the same
per-origin limit. Deletion and replacement account for the net UTF-8 byte
change inside the write transaction. The active value is not partially
modified on quota or I/O failure.

### RS23 — History records visits and restores navigation stacks

`visits` is the durable browsing history ledger. Every successful top-level
navigation appends one row, with its transition (`typed`, link, redirect,
back, forward, reload, or agent) and title once known. It is not deleted merely
because the user navigates back and then takes a new branch.

`navigation_entries` is the bounded, ordered back/forward stack for one
browsing context. It mirrors the current `BROWSE_HISTORY_MAX` of 100 entries,
including title and scroll position. A fresh navigation after Back truncates
only the forward stack, appends a new visit, and updates `current_index` in
one transaction. Page-load failure rolls the transaction back, leaving the
old page and current stack untouched.

On clean restart Radiant creates a new session and reopens the profile's most
recent browsing context as the durable back/forward stack. The host's explicit
initial URL becomes a new branch; selected historical URLs and their stored
title/scroll values remain available through the session history API. It does
not serialize live DOM, form values, JS heap, network requests, or arbitrary
`history.state`; those belong to the broader hibernation design in
`Radiant_Design_State_Management.md`, not this design.

The visit ledger is capped at 10,000 rows per profile, pruning oldest rows only
after they are no longer referenced by a navigation stack. Cookies expire by
their own expiry; Web Storage is retained until `Storage.clear()`. These limits
are fixed v1 profile limits and are independent of HTTP cache eviction.

## 5. Concurrency, error handling, and lifecycle

### RS24 — Exactly one thread owns a SQLite connection

The integrated SQLite build sets `SQLITE_THREADSAFE=0`. `RadiantStateStore`
therefore binds its connection to the browsing-session/UI thread and rejects
off-owner SQLite access. Network workers never call SQLite. A worker
may update the locked in-memory cookie jar, then enqueue a durable cookie
command; the owner drains commands before reporting completed work and before
session teardown. Synchronous JS Web Storage calls already run on that owner
thread and commit directly.

There is one connection per active profile/session, not a process-wide global.
Separate Radiant processes use separate connections and SQLite's file locking;
the store uses a bounded busy timeout and reports a write failure without
inventing a second file or a shadow in-memory durable store. State mutations
are prepared SQL with bound values, never generated SQL containing URL,
cookie, key, or value text.

Shutdown is ordered:

1. stop admitting new document/network work;
2. wait for resource workers and drain queued cookie mutations;
3. persist the active history index and scroll position;
4. remove current-session cookies and `sessionStorage` rows;
5. close the SQLite connection; and
6. release the browsing session and its profile owner.

This order makes page replacement cheap while making process exit deterministic.
It also avoids the current resource-manager teardown accidentally defining
cookie lifetime.

### RS25 — Clearing, retention, and failures are explicit

`Storage.clear()` is origin-scoped and updates the database before the realm
cache. Cookie/session cleanup is profile-scoped. Ordinary cache eviction, page
navigation, and document teardown are not browser-data clearing operations.

The store logs category counts and the database path only; it never logs cookie
values or Web Storage values. A failed state write leaves the affected operation
unchanged whenever its web API is synchronous. For queued cookie persistence,
the main-thread handoff logs a commit failure; existing request completion
semantics remain unchanged.

## 6. Migration and compatibility

### RS26 — Text persistence is retired without compatibility code

The former `./temp/cookies.dat` format is not imported or read. This keeps one
cookie authority and avoids retaining a permanent compatibility parser. New
state begins in `radiant_state.sqlite3`; a pre-existing text file is inert and
may be removed by the user. `CookieJar::storage_path` and the TSV load/save
implementation are deleted.

The initial implementation changes these ownership points together:

| Current owner | Replacement |
|---|---|
| `BrowsingSession::history` only | database-backed navigation stack plus in-memory working set |
| `NetworkResourceManager::cookie_jar` | borrowed jar owned by `BrowsingSession` profile |
| `CookieJar::storage_path` TSV persistence | `RadiantStateStore` cookie rows |
| realm-only local/session storage entries | origin-scoped state-store rows plus realm caches |

No resource response bodies or cache metadata move into SQLite. IndexedDB,
Cache Storage, Service Workers, form-state hibernation, cloud sync, encryption
at rest, and persistence of arbitrary JavaScript `history.state` are out of
scope.

## 7. Delivery and verification

### RS27 — Land the ownership boundary before clients

1. **Done:** `RadiantStateStore` and its schema gate live under
   `lambda/network/`, using the already integrated SQLite C API. Make profile
   bootstrap resolve the configured cache directory and create the store before
   top-level loading.
2. **Done:** jar ownership moved to `BrowsingSession`; the cookie table
   replaces TSV persistence; the borrowed jar reaches each resource manager and document
   cookie bridge.
3. **Done:** `localStorage` and `sessionStorage` use origin-scoped load/mutate
   helpers while preserving the current JS object/rooting contract.
4. **Done:** navigation stack, visits, title, and scroll position persist;
   explicit initial navigation creates a new branch.
5. **Done:** the legacy cookie persistence path is removed with no fallback.

Required focused gates:

- state-store open, schema-version rejection, foreign-key integrity, corrupt-file
  fail-closed behavior, and no automatic replacement;
- cookie domain/path/secure/HttpOnly/expiry behavior, persistence across a
  fresh store, session-cookie removal, and one jar shared across navigation;
- `document.cookie`, Fetch/XHR, and curl seeing the same cookie state;
- local-storage origin isolation, restart persistence, stable `key(n)` order,
  quota rollback, and explicit clearing;
- session-storage persistence across reload, isolation between browsing
  contexts, and removal at session end;
- back/forward forward-branch truncation, visit-ledger retention, scroll/title
  restore, restart reload, and failed-navigation rollback;
- queued network-worker cookie mutations keeping SQLite on the session owner;
  and
- an integration check that regular network-cache eviction cannot remove
  `radiant_state.sqlite3`.

Each gate is required by `D1.10`: the database schema, ownership boundary,
origin partitioning, session cleanup, and no-shadow-store rule are invariants,
not best-effort conventions.
