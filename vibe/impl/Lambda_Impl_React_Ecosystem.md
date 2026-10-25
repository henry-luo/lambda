# React ecosystem compatibility tests

Date: 2026-10-25
Status: implemented and verified for the selected compatibility milestone

## Scope and authority

Add pinned, unmodified Redux Toolkit 2.13.0, React Redux 9.3.0,
Zustand 5.0.16 and React Router 8.5.0 to the ordinary `test/js` golden
harness. Reuse React 19.3.1 and the external esbuild authoring step from
`test/js/react`. Each library gets a self-contained browser bundle, readable
suite source, golden and DOM fixture where needed. Running committed tests
requires neither npm nor network access.

This follows the bundled guest-JavaScript execution boundary in
**D8.1.3v42**. Native property dispatch follows **D3.4.7/D7.4.4** and
retained callbacks and GC ownership follow **D5.3.3/D4.5.1v4/D4.5.2**.
No new language or platform ruling is proposed. Fix any demonstrated failure
in shared Lambda runtime mechanisms, never by patching upstream packages or
installing fixture-only compatibility shims.

## Acceptance cases

- **Redux Toolkit:** store/slice setup, nested Immer updates and structural
  sharing, subscription removal, middleware ordering, memoized selectors,
  entity adapter operations, asynchronous thunk fulfillment/rejection and
  cancellation.
- **React Redux:** Provider, dispatch hooks, selector equality, batched
  updates, independently provided stores, connected components and cleanup.
- **Zustand:** vanilla store merge/replace, subscriptions and unsubscribe,
  selector middleware, persistence with an explicitly supplied in-memory
  storage adapter, React hooks and selective rerendering.
- **React Router:** matching and parameters, nested routes/outlets, links,
  search parameters, push/replace/back navigation; data-router loaders,
  redirects, actions and error boundaries. Memory routing is the first
  deterministic acceptance scope; qualify browser history separately if the
  existing platform supports its required surface.

Use explicit commits, subscription completion or awaited operations rather
than arbitrary sleeps. Exercise retained callbacks after explicit GC. Shared
build options and suite helpers must be extracted before adding a third
near-identical variant. Preserve upstream license notices and a complete npm
lockfile. Add the fixtures to existing discovery, with no separate test runner
and no changes that weaken the harness.

## Implementation and verification

1. Inspect the existing React fixture/harness and official package APIs;
   pin official npm releases and document reproducible regeneration.
2. Extend its shared build and test helpers, add four focused suites and
   independent goldens, and validate the bundles in a reference browser.
3. Run all new cases on AST, MIR and AUTO; diagnose and repair shared runtime
   failures with small regressions when useful. Repeat relevant cases under
   every-allocation GC/poisoning for lifetime changes.
4. Run the complete JS golden suite. If runtime code changes, run its focused
   native tests and required Lambda baseline; if DOM/Radiant code changes,
   also run its required baseline and layout integer-cast lint. Broaden to
   Test262 when the modified mechanism warrants that gate. Verify clean
   diffs and reproducible bundles, then record exact results here.

Full framework integration, network-backed RTK Query, exhaustive Redux
middleware/API coverage and browser navigation beyond the selected cases
remain distinct follow-on scopes.

## Implementation record

- Added independent `redux_toolkit`, `react_redux`, `zustand` and
  `react_router` bundles and goldens, discovered by the existing JS GTest.
  Their 52 assertions exercise the acceptance cases above. Shared helpers,
  esbuild configuration, pinned dependencies/lock and collected upstream
  licenses live under `test/js/react/`. The React fixture uses the same
  helpers and still retains its existing golden.
- Redux initialization uses fractional base-36 formatting. MIR previously
  rejected that operation before running the suite. It now shares the ordinary
  Number formatting algorithm; binary64-to-BigInt conversion also canonicalizes
  negative zero (**S4.9.1**). `number_fractional_radix` covers both defects.
- React Redux's connected-component static hoisting called native
  Object.getOwnPropertyNames on an MVP callable. The native path cast that
  callable to the unrelated JsFunction layout and crashed. String/Symbol
  enumeration now uses the callable's property owner, following **D3.4.7**.
  Proxy results are filtered by key kind through the same helper.
  `function_own_keys_foreign` covers callable statics, descriptors and Symbols.
- React Router's search updater clones URLSearchParams. The constructor
  formerly treated iterable objects as records and silently lost their entries.
  Extracted the existing Headers WebIDL string-pair conversion and exposed it
  through the append-only Jube script API; both interfaces retain their own
  validation. URLSearchParams now supports sequences/records, duplicate entries,
  iterator failures and USVString normalization. The constructor and its inputs
  retain precise roots (**D5.3.3**). The small constructor regression covers
  independent copies, iterable pairs, records, conversion and invalid inputs.
- Data routing required a native Request constructor and Response.body.
  Added a bounded in-memory Request implementation, sharing native interface
  installation, byte extraction, MIME inference, Body consumption and cloning
  with Response. Branded records own the data; dependent signals use retained
  native bindings. Byte-backed stream pulls and Body methods share consumption
  state. `request_body` checks metadata, byte snapshots, streams, single-use
  consumption, clones/copies, signals, receivers and subclasses. This does not
  qualify the full Fetch API: streaming input, complete stream locking/tee /
  cancellation, FormData bodies, remaining RequestInit/header-guard policies
  and fetch(Request) transport integration remain follow-on work.
- Relative URL resolution assembled components without refreshing its derived
  origin. React Router consequently classified an internal redirect as external.
  Both parsing and resolution now use one origin-refresh helper. A native URL
  regression covers path/query/fragment/empty references, authority changes
  and ports.
- Forced collection exposed unpublished collection-iterator values and iterator
  result keys, plus Proxy ownKeys trap results retained across invariant checks.
  Added precise temporary roots at those publication boundaries (**D5.3.3**);
  no conservative scanning or upstream package changes are involved.
  Byte-stream reader creation/read servicing likewise roots its unpublished
  closure, controller, source and result while body pulls allocate.

The reference browser is Chromium 152. The URLSearchParams null initializer
serializes as `null=` there, matching WebIDL's string-union conversion;
the installed Node 24 instead returns an empty query. The regression follows
the browser/specification behavior. Request body override similarly disturbs
the input's non-null body while retaining its headers.

## Qualification record

- All four library goldens match Chromium 152; the four small runtime
  regressions match it too. The pre-existing React golden remains unchanged.
- The nine selected fixtures (React, four libraries and four regressions) pass
  forced AST, MIR and AUTO; the MIR sweep also includes the unchanged Promise
  expando regression. Both forced AST and MIR pass all four regressions with
  every-allocation collection and freed-memory poisoning enabled.
- `npm ci --offline` succeeds from the populated download cache. Regenerating
  all five bundles and `THIRD_PARTY_LICENSES.txt` reproduces every byte.
- All 47 native URL tests pass, including the new derived-origin regression.
- The first Lambda aggregate exposed missing Symbol enumeration for native
  Promise carriers after separating string/Symbol key filters. Restored generic
  exotic OwnPropertyKeys dispatch for the Symbol path; the existing
  `promise_expando` and dynamic object-copy tests remain unchanged.
- The full Test262 run then exposed four typed-array OwnPropertyKeys
  regressions: its metadata adapter still returned only string keys. The
  adapter now supplies the full string/Symbol list, with the existing symbol
  collector reused and its unpublished result rooted. Public name/Symbol
  APIs apply their respective filters. The reflection regression now also
  checks Uint8Array and BigInt64Array, including non-enumerable Symbol keys.
- An additional callable-census audit reports `builtin_semantic_cases: 38 > 0`.
  The same regex counts 38 in HEAD's `lambda/js/js_runtime.cpp`; no such case
  was added or changed here. Its catalog-validation errors are zero. This is
  pre-existing structural audit debt; the ratchet and harness are unchanged.
- The node-module architecture audit reports 102 existing violations in
  node_buffer.cpp, node_runtime_state.hpp, node_util.cpp and
  node_crypto_module.cpp. Each reported file is byte-identical to HEAD;
  none is part of this change. The modified node_url.cpp has no reported
  violation. The exception-helper catalog audit passes.

- The final forced-MIR sweep passes all ten selected fixtures, including React
  and the unchanged Promise expando regression. `make build-test` succeeds.
- `make test-lambda-baseline` passes **8,231/8,231**: 2,112 input tests and
  6,119 runtime tests, including all 558 JS harness tests, 847 lower-level JS
  tests, 346 forced-GC cases and the Test262 runner preflight. Neither the
  harness nor the existing regression goldens were weakened.
- The rebuilt physical React UI fixture passes all seven assertions with the
  regenerated shared bundle. No Radiant layout code changed.

- After the typed-array correction, the complete Lambda baseline passes again
  at **8,231/8,231**; the rebuilt physical React UI test again passes 7/7.
- `make test262-baseline` passes **40,261/40,261**, zero regressions, zero
  unstable/slow results and 2,652 existing configured skips. The original
  four typed-array failures are resolved; no Test262 harness, baseline or
  skip list changed. The target leaves a release executable.
- All nine selected fixtures pass again on the final release executable's
  AUTO tier. All four focused regressions also pass forced MIR with
  every-allocation GC and freed-memory poisoning on that release build.
- `git diff --check` passes. The two pre-existing structural audit failures
  above remain outside this milestone.
