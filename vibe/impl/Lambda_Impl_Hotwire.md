# Hotwire compatibility and native-server HTTP tests

**Started:** 2026-10-25  
**Updated:** 2026-10-26  
**Status:** complete for the selected Stimulus/Turbo client and HTTP milestone

## Objective and authority

Add pinned, unmodified `@hotwired/stimulus` and `@hotwired/turbo` bundles under
`test/js`, readable fixture/build sources, upstream licenses, and golden tests.
Exercise real HTTP exchanges through Lambda's native `lambda/serve` server.
The production `serve` CLI and Lambda `io.http` bindings are currently stubs;
this milestone uses the native API directly from a test fixture.

Implementation follows **D8.1.3v42** (LambdaJS execution and AST/MIR paths),
**D7.4.4** (declared host interfaces), **D3.4.7** (native JavaScript records),
and **D5.3.3** (precise native roots). Fix demonstrated runtime defects at
their owning stage. Do not modify upstream packages, inject library shims,
weaken expectations, or change Test262 exclusions. No language ruling changes
are proposed.

## Proposed implementation

1. Pin the current published Stimulus/Turbo versions and build tool in a
   reproducible fixture package. Commit browser bundles and complete upstream
   licenses so running tests needs neither npm nor public network access.
2. Add Stimulus acceptance cases for controller connection/disconnection,
   targets, actions, typed values, nested scopes, dynamic DOM/attribute
   observation and cleanup. Add Turbo Stream acceptance cases for ordinary
   append/prepend/update/replace/before/after/remove actions and event hooks.
3. Build a small test fixture around Lambda's native server. Bind loopback,
   allocate an ephemeral port, expose readiness and deterministic shutdown,
   serve fixture HTML/assets, and implement page/frame/form/stream endpoints.
   Share existing server/router/response implementations; do not create an
   alternate HTTP implementation or implement all public server bindings.
4. Add HTTP integration tests using the actual bundled Turbo client: page
   navigation, matching frame extraction, request headers/body, successful
   form redirect, validation-error HTML, and stream response updates. Verify
   both the request received by Lambda's server and the resulting client DOM.
5. Investigate each failure using focused reproductions and browser reference
   behavior. Fix general DOM, JS, fetch or server lifecycle defects with
   targeted regression coverage and concise root-cause comments.

## Acceptance and verification budget

- New fixtures have explicit pass/fail output and checked-in goldens. They
  must fail on missing behavior rather than skip a case or silently time out.
- Qualify the selected cases in AST, MIR and automatic execution; stress
  focused lifetime regressions with collecting/poisoning GC where practical.
- Compare library behavior against Chromium using the same fixture server.
- During implementation run focused fixtures and affected native unit tests.
  At the milestone boundary run `make test-lambda-baseline`; if Radiant code
  changes, also run `make test-radiant-baseline`. Run the Test262 baseline
  when shared JavaScript semantics change. Repeat broad gates only after a
  relevant subsequent change or unresolved failure.
- Keep test ports isolated and processes bounded; use `temp/hotwire/` for all
  temporary files. Record exact versions, checks, failures and remaining
  limitations here before completion.

## Scope boundaries

This is a finite client-library and HTTP compatibility milestone. WebSocket
and SSE push, Rails integration, Hotwire Native bridges, Turbo morphing,
complete browser navigation, and a complete Fetch/HTML standards tail are
follow-on work unless required by the selected cases. Any acceptance case
that remains broken must be recorded honestly with its root cause.

## Progress and evidence

- Inspected native routing/response APIs and confirmed that generic `serve`
  and `io.http` entry points are placeholders.
- Added Stimulus **3.2.2**, Turbo **8.0.23**, esbuild **0.28.1**, the npm lockfile,
  unmodified generated browser bundles, and complete upstream notices. Turbo's
  omitted npm license is preserved from its matching upstream tag. An offline
  `npm ci` followed by regeneration reproduced all three bundles and the
  combined notice byte for byte. The shared builder also regenerates the
  existing React fixtures without content changes.
- `test/test_hotwire_gtest.cpp` joins `test_serve_gtest`; its standalone mode
  serves deterministic fixtures through the actual native server. Port zero
  now binds an OS-assigned port and publishes the bound port through the server
  configuration. Readiness uses an atomic file; client execution and graceful
  server shutdown are bounded. A separate test verifies two isolated listeners.
- The library suites contain **41 behavior assertions**: 11 Stimulus, 12 Turbo
  Streams, and 18 HTTP checks. The HTTP test verifies Frames and frame links,
  controller connection in fetched/streamed HTML, stream POST responses, Drive
  body/title/history updates, URL-encoded forms and submitter state, POST/303/GET,
  422 rendering, and the server's actual method/header/body audit.
- Runtime fixes found through those cases:
  - Custom-element upgrade must construct the existing native wrapper, retain
    private fields, and deliver lifecycle reactions. Realm weak state,
    construction frames and queued connection callbacks own their precise roots
    (**D3.4.7, D5.3.3, D7.4.4**). Attribute callbacks run synchronously at setter
    boundaries; deferral outlived Turbo's `src` reentrancy guard and repeatedly
    fetched the same frame. Inert templates do not upgrade until import/insert.
  - MIR private-field storage must use native host hooks rather than physical
    MVP Map storage. DOM expando presence must retain private-key identity while
    still accepting ordinary uninterned strings; the existing key-aware
    own-slot helper handles both.
  - Template-content cache publication needed roots for its key and wrapper.
    Every-allocation collection exposed the missing ownership (**D5.3.3**).
  - Range deletion must remove Mark-backed children as well as native links;
    otherwise the following append resurrects replaced frame contents.
  - The headless event loop must give I/O continuations a rendering opportunity
    before blocking on later timers; an initial-only rAF drain starved Turbo.
  - Form action/method/enctype/encoding/target setters must reflect attributes;
    Turbo reads those attributes when choosing the request method.
  - Fetch now shares the existing Request/Response BodyInit extraction,
    including URLSearchParams bytes and MIME. Native POST configuration permits
    libcurl's 303 rewrite; final Response URL/redirected state and body exclude
    the intermediate response.
  - HTML `DOMParser` must parse an inert full document rather than a body
    fragment, preserving head/title/root metadata. It uses the parser's embedded
    DOM allocation mode and the existing HTML-root extractor.
  - CLI documents lacked Window scrolling methods. Native publication now
    delegates to the existing document scroll root, shared with rendered pages;
    the duplicate rendered-page preamble implementation is removed.
- Six focused DOM regression fixtures cover custom-element lifecycle/private
  fields, repeated Range replacement, form reflection, rAF after a native task,
  full HTML parsing and Window scrolling.
- Focused qualification: **11/11 AST**, **11/11 MIR**, and **4/4 native HTTP/server**
  cases pass. The focused selection includes the three existing descriptor
  regressions; the native selection includes AST, MIR and automatic clients. Chromium 152
  agrees with all library and focused DOM goldens. Lifecycle, parser and Window
  regressions pass under both AST and MIR with `LAMBDA_GC_FORCE_EVERY=1` and
  `LAMBDA_GC_POISON_FREED=1`.
- The first Lambda baseline exposed three DOM expando descriptor failures from
  the private-key lookup change. Their root cause and fix are described above;
  all three now pass in AST and MIR after that correction, and the rerun passed
  **8,244/8,244** tests, including **566/566 JS** and **152/152 server** tests.
- Radiant qualification exposed two lifecycle-hook defects: attribute mutation
  must check for an active realm before allocating a receiver, and custom-element
  traversal must use the existing source-child/sibling helpers, excluding
  anonymous layout boxes without DOM identity (**D7.4.4**). The native paint,
  jQuery dynamic-components, and Bootstrap page reproductions now pass. Standalone
  Range targets use the existing weak-default service pattern; production uses
  the strong backing-aware DOM implementation.
- The existing Window-scroll native test assumed synchronous dispatch from the
  removed JS preamble. It now uses a genuinely scrollable parsed body, rejects
  synchronous dispatch, verifies one deferred/coalesced event and subsequent
  no-op suppression, and requires an asynchronous completion marker. The same
  script passes in Chromium and follows
  [CSSOM View §13.2](https://drafts.csswg.org/cssom-view/#scrolling-events).
  The runtime uses its pre-existing native scroll-event queue (**D7.4.4**).
- Final aggregate gates were rerun after those Radiant corrections.
  The HTTP audit restricts each frame-header
  assertion to its own request and checks that redirected HTML contains no
  intermediate response-body text.

- The final Lambda gate again passed **8,244/8,244**. Two collecting/poisoning
  stress subprocesses received SIGKILL while run concurrently with aggregate
  qualification. No JS exception or GC diagnostic was reported. Following the
  developer guide §7.4, repeated the focused three-case selection serially:
  **3/3 AST runs and 3/3 MIR runs passed** (18/18 case executions). The final
  Radiant gate passed with **zero unexpected failures**: 6,321 reported cases,
  6,176 fully passing, 143 partial cases and two skips. This includes **234/234
  native vector/paint**, **165/165 DOM UI**, **683/683 general UI**, **51/51 page
  snapshots**, and the corrected Window-scroll view test. Existing partial
  inventories/skips were not changed.
- `make test262-baseline` passed **40,261/40,261** baseline tests with zero
  regressions and its existing 2,652 exclusions. Neither the Test262 harness nor
  baseline/exclusion inventories were modified. The command built a release
  executable; final Hotwire checks also use that executable.
- Final release recheck passed **11/11 AST**, **11/11 MIR**, and **4/4 native
  HTTP/server** cases. Regenerated final bundles/notices reproduce byte for byte;
  documentation links and `git diff --check` pass.

## Reproduction and remaining scope

Build/run/regeneration commands are in
[`test/js/hotwire/README.md`](../../test/js/hotwire/README.md). Required gates:
`make test-lambda-baseline`, `make test-radiant-baseline`, and
`make test262-baseline`. HTTP tests require loopback sockets but no public
network access. Bundles and their goldens are committed; npm is only needed to
regenerate them.

This qualifies the selected behavior, not complete Hotwire/browser conformance.
The public `serve` CLI and Lambda `io.http` bindings remain stubs; the fixture
uses the native server API directly. Complete custom-element reaction stacks,
Turbo morphing, streaming Fetch bodies/policies, SSE/WebSocket push, Rails and
Hotwire Native remain follow-on scope. No vendor source, language ruling or
unrelated expected-failure inventory was changed.
