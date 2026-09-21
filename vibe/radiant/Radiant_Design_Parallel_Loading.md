# Radiant Parallel Page Loading Design

**Status:** implemented, 2026-09-21  
**Scope:** HTTP(S) document navigation, linked CSS, fonts, images, and page
scripts from discovery through first paint.  This does not change LambdaJS or
DOM semantics.  
**Spec linkage:** D1.3v3 keeps shared host services distinct from private
guest cores.  This design applies that boundary to a shared resource service:
workers move immutable response bytes, while the page's DOM and script realm
remain private.  It operationalizes Radiant concurrency decisions RC1 and RC8
in [Radiant_Design_Concurrency.md](Radiant_Design_Concurrency.md).

## 1. Problem

The historical HTML loader has two unrelated resource paths:

1. it uses blocking HTTP for the navigation document and a legacy disk-cache
   prefetch worker pool for linked CSS and scripts;
2. after document construction it creates the curl-multi resource manager for
   CSS, images, fonts, and scripts.

The legacy prefetch waited for every CSS *and* script request before parsing
CSS.  It therefore made a slow non-render-blocking script part of the
render-blocking chain.  The two paths also had incompatible cache identities:
the early path names a DJB2 file and the manager owns SHA-256 entries and
metadata.  A later discovery could not reuse an earlier prefetch.

Large pages have a second, independent cost: CSS cascade, JS parsing/JIT, and
layout are page-thread CPU work.  More HTTP threads cannot fix repeated
intrinsic measurement or a large script bundle.

## 2. Decisions

### 2.1 One page-owned resource identity

Every loader-stage absolute subresource URL has one navigation-scoped request
identity and one completion result.  Cache hit, in-flight transfer, completed
response, and failure are states of that identity.  A subsequent consumer
attaches to the same request; it does not create a second transfer.  The
top-level HTML response remains synchronous because the `DomDocument` does
not exist until that response is parsed; its dependencies join the manager as
soon as the document is constructed.

The target implementation is the existing curl-multi scheduler and enhanced
cache.  The compatibility bridge introduced in the first implementation phase
imports a legacy-prefetched file into the enhanced cache index without copying
the response.  It removes duplicate download work while callers migrate; it is
not a second permanent cache design.

### 2.2 Critical-path priority

The scheduler orders resource classes as follows:

1. navigation document and render-blocking linked stylesheets;
2. CSS `@import` dependencies and fonts needed by those stylesheets;
3. parser-blocking script sources;
4. `defer`, module, and `async` script sources;
5. visible images and other subresources.

Priority controls request admission only.  It never changes stylesheet source
order, CSS cascade order, or script execution order.

### 2.3 Page-thread ownership

Per RC1, DOM mutation, CSS parsing/cascade, layout, and JavaScript execution
remain on the page thread.  Network workers only download and cache immutable
bytes; their completion is consumed on the page thread.  JavaScript source
fetches may run concurrently, but classic/defer/module execution remains
serial in the document realm.  This avoids cross-thread DOM access and retains
synchronous layout-query behavior.

### 2.4 First-paint boundary

First layout waits only for stylesheets that block rendering.  Images, fonts
that are not required for the chosen first layout, and non-blocking scripts
continue after first paint.  A CSS `@import` discovered while parsing a
stylesheet is enqueued immediately, but the importer is applied only in its
specified dependency and source order.

## 3. Loading model

```
navigation HTML
  -> parse DOM tokens / discover URLs
  -> queue CSS at high priority
  -> wait for required CSS; parse and enqueue @imports
  -> cascade and first layout
  -> deliver non-critical resource completions on the page loop

script source fetches overlap CSS parsing
  -> one document realm serially parses/transpiles/executes the ready tasks
```

The implementation keeps the mature synchronous CSS parser and script
executor.  It changes their wait relationship only: stylesheets are queued
and awaited first; script prefetch starts afterwards and runs while CSS is
parsed; each script source waits on its own manager identity immediately
before collection/execution.  This is behavior-preserving for source order
while removing scripts from CSS's network wait.

## 4. Phased implementation

### Phase P1 — CSS-first overlap and cache bridge

- Separate early linked-stylesheet and script URL sets.
- Complete stylesheet prefetch before the synchronous stylesheet reader runs.
- Start script prefetch asynchronously and join it only before the document
  script runner needs source bytes.
- Use monotonic wall-clock timing for each prefetch phase, never process CPU
  time.
- Let enhanced-cache lookup adopt an existing legacy cache file for the same
  URL.  The resource manager then sees the file as a cache hit instead of
  re-downloading it.

### Phase P2 — unify the loader on the resource manager

Create the resource manager as soon as a remote `DomDocument` exists.  The
HTML speculative scanner, CSS loader, script source resolver, and late DOM
discovery submit requests to that one manager and await URL identities rather
than files.  Remove the legacy prefetch API after all consumers have moved.

### Phase P3 — dependency-aware CSS and script scheduling

CSS parsing discovers `@import`, `@font-face`, and URL-bearing declarations
and queues their dependencies immediately.  The page thread applies parsed
stylesheets only in CSS order.  Script fetching follows its attributes, while
execution remains serial and follows browser scheduling rules.

### Phase P4 — CPU bottlenecks

Instrument intrinsic sizing with element count, cache-hit count, constraint
key, and inclusive/exclusive time.  Eliminate repeat subtree measurements with
generation-keyed caches before attempting general parallel layout.  Any future
fork-join style/layout pass must produce deterministic sequential-equivalent
results, as RC6 requires.

## 5. Implemented design

P1 and P2 are implemented by creating a `NetworkResourceManager` immediately
after remote HTML becomes a `DomDocument`.  It owns the enhanced cache when
the caller has not supplied one, exposes byte-copy consumers for CSS and
scripts, and retains a condition variable for those consumers to await a
single transfer.  The legacy `HttpPrefetchBatch` interface was removed.  The
enhanced cache can still adopt an older verified sidecar entry while generic
callers migrate, but no loader-stage request uses that path.

The manager is registered in the document-resource lifetime list when it is
created.  Thus every `DomDocument` destruction path, including redirect or
script navigation before a window owns the document, cancels worker activity
and destroys the manager before DOM storage disappears.  This preserves the
page-thread/private-document boundary required by D1.3v3 and RC1.

P3 is implemented as follows:

- Linked stylesheets queue at high priority and are all admitted before the
  loader waits for their bytes; direct `@import` URLs are admitted together
  before their source-order parse.
- Parser-blocking scripts consume high-priority manager bytes; defer, module,
  and async script sources consume normal-priority bytes.  Execution remains
  serial on the page thread, per RC1.
- `@font-face` sources queue at high priority during loader-stage CSS parsing.
  Their descriptors attach only after a `UiContext` exists, preventing network
  worker access to page-thread font state.
- Every parsed declaration URL, including nested function/list URLs, is made
  absolute against its stylesheet and admitted at low priority.  Background
  image decoding consumes completed manager bytes but never creates a blocking
  render-time transfer.

P4 is implemented with a per-layout `LAYOUT_PROFILE` report containing
intrinsic request/hit/miss/re-entrancy counts plus inclusive and exclusive
time.  An optimized release-profile run of the TC39 ECMAScript specification
identified repeated intrinsic measurement as the dominant CPU cost (500,003
initial requests and 237.4s cumulative inclusive time in an 81.1s wall run).
The implementation keeps the existing resolved-style and layout-generation
cache contract: a style reset or recompute invalidates the intrinsic entry.
Flex preparation no longer resets a child whose style is already current for
the same parent; its width-sensitive measurement cache still invalidates when
the container width changes.  The matching release-profile run made 396,125
requests with 221.2s cumulative inclusive time (the observed navigation plus
layout wall time was 74s; network time varies).  This is a measured lifecycle
optimization, not an unsafe general parallel-layout switch.

## 6. Observability and acceptance

Each navigation records monotonic-wall-clock spans for navigation, discovery,
each resource queue/start/finish/cache hit, CSS parse/import, script source
load/parse/transpile/execute, cascade, layout, render, and cleanup.  Reports
must distinguish first paint from process exit.

The implementation is accepted when tests prove that:

- a prefetched URL and a typed manager consumer share one cached resource;
- the nested-flex regression emits intrinsic measurement telemetry under
  `LAYOUT_PROFILE`;
- stylesheet order and script execution ordering remain unchanged through the
  Radiant baseline suite.

## 7. Non-goals

- No concurrent JavaScript execution within one document realm.
- No network worker accesses a `DomDocument`, `DomNode`, CSS engine, or GC
  value.
- No speculative request changes a page's semantic resource ordering.
- No general parallel-layout switch before the intrinsic-sizing work is
  measured and made deterministic.
