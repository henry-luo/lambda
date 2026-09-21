# Radiant Parallel Page Loading Design

**Status:** active hardening, 2026-09-22
**Scope:** HTTP(S) document navigation, linked CSS, fonts, images, and page
scripts from discovery through first paint.  This does not change LambdaJS or
DOM semantics.  
**Spec linkage:** D1.3v3 keeps shared host services distinct from private
guest cores; D6.3.2 keeps workers share-nothing; and D4.2.2v2, D4.2.3,
D4.2.4, and D4.5.1v3 require explicit document ownership and teardown. This
design applies those rules to a shared resource service: workers move immutable
response bytes, while the page's DOM and script realm remain private. It
operationalizes Radiant concurrency decisions RC1 and RC8 in
[Radiant_Design_Concurrency.md](Radiant_Design_Concurrency.md).

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

Small interactive pages exposed a related fixed CPU cost: before a page's own
source can run, its private browser-compatibility realm must be prepared. That
setup is not proportional to the page script's byte length, and process-scoped
CPU signals can interrupt a helper thread during it. The document JS watchdog
therefore starts only after trusted realm preparation; its bounded fixed and
source-size-scaled budget covers page-owned work. If recovery nevertheless
interrupts JS or allocator ownership, the failure is sticky through outer
document teardown; teardown must abandon that batch rather than reset state
that may have been interrupted (D4.2.2v2, D4.2.3, D4.5.1v3).

### 1.1 September 2026 online-suite audit

The functional online suite is a debug-build correctness trace, not a release
benchmark. It nevertheless identifies which lifecycle span owns elapsed time.

| Representative pages | Dominant span | Consequence |
|---|---|---|
| IANA Protocols, WHATWG DOM | post-script full cascade (about 81s and 121s) | not every mutation may trigger an all-tree selector pass |
| ECMA-262, Kotlin, JetBrains, Mapbox, Deno | DOM construction or JS preparation/execution | account for CPU preparation separately from transport |
| LLVM LangRef, Elixir | intrinsic layout measurement | reuse generation-keyed results before any parallel layout design |
| Kafka, Godot, Unicode, Micronaut | cleanup after first render, often at multi-GB peak | first paint and document disposal are separate objectives |

The audit corrected an earlier interpretation of script loading. Static remote
`<script src>` URLs are discovered before CSS parsing and submitted together to
the curl-multi manager. A later source consumer waits on its existing request
identity; it does not serialize those transfers. The remaining script
bottleneck is source readiness on a miss, parsing/transpilation, ordered
execution, and retained graph size. Dynamic and module-graph discoveries still
need the same batch-admission discipline.

## 2. Decisions

### 2.1 One page-owned resource identity

Every loader-stage absolute subresource URL has one navigation-scoped request
identity and one completion result.  Cache hit, in-flight transfer, completed
response, and failure are states of that identity.  A subsequent consumer
attaches to the same request; it does not create a second transfer.  The
top-level HTML response remains synchronous because the `DomDocument` does
not exist until that response is parsed; its dependencies join the manager as
soon as the document is constructed.

The target implementation is the curl-multi scheduler and its enhanced cache.
Every persistent HTTP entry uses the enhanced cache's SHA-256 path.  There is
no compatibility cache identity or sidecar adoption path, so a loader cannot
fall back to a second cache design.

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

### 2.5 Ordered execution, parallel preparation

Classic, defer, and module evaluation mutate one document realm and remain
ordered on the page thread under RC1. Downloading immutable response bytes is
parallel today. A future bounded prebuild pool may parse immutable source or
prepare module descriptors only through share-nothing workers (D6.3.2); it
must publish no DOM, JS heap, closure, or MIR ownership across the boundary.
Evaluation, instantiation, microtask checkpoints, and browser-global sync stay
serial.

The browser preamble must not create optional module-loader globals. In
particular, it leaves `define` absent unless a page supplies an AMD loader:
otherwise a UMD bundle can select its AMD branch, register an inert definition,
and never publish the browser global expected by following classic scripts.

### 2.6 Mutation-qualified recascade

The initial cascade precedes scripts because scripts may synchronously read
computed style. After scripts, a stylesheet/CSSOM, class/attribute, structural,
or unclassified mutation conservatively re-cascades the full tree. A direct
`element.style` or `style`-attribute write has already merged an inline
declaration into its specified-style tree, whose precedence cannot be changed
by replaying stylesheet selectors. These and paint-only mutations skip the
global post-script cascade; the following layout still resolves used values.

## 3. Loading model

```
navigation HTML
  -> parse DOM tokens / discover URLs
  -> queue CSS at high priority
  -> wait for required CSS; parse and enqueue @imports
  -> cascade and first layout
  -> deliver non-critical resource completions on the page loop

script source fetches overlap CSS parsing
  -> source readiness is consumed by one document realm
  -> serially parse/transpile/execute ordered tasks

post-script mutations
  -> direct inline/paint change: retain selector result
  -> otherwise: conservative recascade

first render
  -> detach document roots and dispose its allocation context in dependency order
```

The implementation keeps the mature synchronous CSS parser and script
executor.  It changes their wait relationship only: stylesheets are queued
and awaited first; script prefetch starts afterwards and runs while CSS is
parsed; each script source waits on its own manager identity immediately
before collection/execution.  This is behavior-preserving for source order
while removing scripts from CSS's network wait.

## 4. Phased implementation

### Phase P1 — CSS-first overlap and native cache

- Separate early linked-stylesheet and script URL sets.
- Complete stylesheet prefetch before the synchronous stylesheet reader runs.
- Start script prefetch asynchronously and join it only before the document
  script runner needs source bytes.
- Use monotonic wall-clock timing for each prefetch phase, never process CPU
  time.
- Persist synchronous HTTP responses in the enhanced cache's native SHA-256
  layout, so every later consumer resolves the same URL to the same cache
  identity.

### Phase P2 — unify the loader on the resource manager

Create the resource manager as soon as a remote `DomDocument` exists.  The
HTML speculative scanner, CSS loader, script source resolver, and late DOM
discovery submit requests to that one manager and await URL identities rather
than files.  The obsolete prefetch API and its cache format are removed after
all consumers have moved.

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

### Phase P5 — qualified post-script cascade

- Distinguish direct inline-style and paint-only writes from stylesheet and
  selector-affecting DOM mutations.
- Skip the whole-tree cascade only for the former two classes; record the
  decision and mutation count in the load trace.
- Reuse the event-time mutation ledger rather than inventing a loader-only
  invalidation channel.

### Phase P6 — document disposal and retained-byte budgets

- Record first-render and cleanup spans separately, together with document
  context live/reserved bytes and script source/AST/MIR retained bytes.
- Detach timers, resource callbacks, JS roots, and shared-cache references
  before context disposal. Context teardown proceeds in dependency order under
  D4.2.2v2–D4.2.4 and D4.5.1v3; it never bulk-frees a cache shared by a live
  document.
- Bound page-local script-source and preparation retention by bytes. Release a
  transient representation as soon as no module, callback, debugging, or API
  contract needs it.

### Phase P7 — streaming construction and optional viewport work

- Profile the HTML/DOM allocation and insertion path for giant specifications
  before changing it; Vulkan currently reaches a multi-GB peak before DOM
  construction finishes.
- Consider off-viewport layout deferral only after exact scroll-extent and
  correctness contracts exist. It is not an alternative to measurement reuse.

## 5. Implemented design

P1 and P2 are implemented by creating a `NetworkResourceManager` immediately
after remote HTML becomes a `DomDocument`.  It owns the enhanced cache when
the caller has not supplied one, exposes byte-copy consumers for CSS and
scripts, and retains a condition variable for those consumers to await a
single transfer.  The obsolete `HttpPrefetchBatch` interface, its DJB2 cache
naming, and its URL sidecar files are removed.  Synchronous HTTP callers also
use the native enhanced cache, so no bridge remains outside the loader.

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
- The browser preamble does not synthesize `define.amd`. UMD source therefore
  follows its browser-global branch unless the document explicitly installs an
  AMD loader before it is evaluated.
- `@font-face` sources queue at high priority during loader-stage CSS parsing.
  Their descriptors attach only after a `UiContext` exists, preventing network
  worker access to page-thread font state.
- Every parsed declaration URL, including nested function/list URLs, is made
  absolute against its stylesheet and admitted at low priority.  Background
  image decoding consumes completed manager bytes but never creates a blocking
  render-time transfer.

Static remote script discovery submits parser-blocking classic scripts at high
priority and `defer`, module, and `async` scripts at normal priority. This is
an admission policy only: source and execution order remain unchanged.

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

P5 is implemented for the proven-safe classes. Direct `element.style`,
`cssText`, and `setProperty` writes receive an explicit inline-style mutation
kind; a load-time batch containing only those and paint-only changes bypasses
the global post-script cascade. Other bounded mutations reuse the event-time
mutation-subtree eligibility check before the first layout: local class or
attribute changes re-cascade their affected root only when stylesheet,
structural-selector, and relational-selector analysis proves that no ancestor
or sibling closure is missed. CSSOM stylesheet edits, broad structural or
relational selectors, unknown mutations, and mutation-ledger overflow retain
the full-cascade fallback. The fixtures
`test/ui/js_load_inline_style_no_recascade.html` and
`test/ui/js_load_class_mutation_subtree_recascade.html` verify both decisions.

P6 measurement is implemented for the headless layout path. Timing JSON now
records first render separately from total command completion, divides disposal
into network, state, script, view, DOM, and process-runtime slices, and records
the page context's live/reserved bytes immediately before disposal. Script
source timing separately records batch admission, wait, and read time with its
task counts. The remaining P6 retention-limit work requires per-runtime
AST/MIR ownership counters; it is deliberately not inferred from process-root
allocators.

The JS watchdog also distinguishes trusted realm preparation from page-owned
source work. Browser-preamble compilation completes before its bounded
source-size timer begins, so helper-thread preparation cannot deliver a
process-scoped profiling signal into the wrong worker. Its default user-code
allowance is ten CPU seconds, followed by the existing bounded source-size
scaling. A watchdog recovery keeps the batch unsafe marker until the caller's
document teardown has skipped global JS reset; this prevents a recovered page
from turning a controlled script failure into a post-render native crash.
Existing interactive viewport and pre-commit geometry fixtures cover the
normal startup boundary.

## 6. Observability and acceptance

Each navigation records monotonic-wall-clock spans for navigation, discovery,
each resource queue/start/finish/cache hit, CSS parse/import, script source
admission/wait/read, parse/transpile/execute, initial and post-script
recascade, handler installation, layout, render, and cleanup. Reports must
distinguish first render from process exit and report the document context's
pre-disposal live/reserved bytes. Post-script records also include mutation
count, kind mask, overflow state, and whether the conservative full recascade
was selected.

Release-profile reports additionally separate static-script admission, source
wait/read, parse/prebuild, ordered execution, initial/final cascade, layout,
render, and cleanup. Debug traces may identify a target but never establish a
performance result.

The implementation is accepted when tests prove that:

- a prefetched URL and a typed manager consumer share one cached resource;
- the nested-flex regression emits intrinsic measurement telemetry under
  `LAYOUT_PROFILE`;
- direct inline style writes preserve rendered values while skipping the global
  post-script cascade;
- a selector-affecting local class write preserves its result through the
  shared mutation-subtree recascade path;
- timing JSON exposes separate first-render, cleanup, script-transport, and
  post-script-recascade spans;
- stylesheet order and script execution ordering remain unchanged through the
  Radiant baseline suite.
- a CommonJS-aware UMD bundle reaches its global export from the following
  classic script without an implicit AMD branch.

## 7. Non-goals

- No concurrent JavaScript execution within one document realm.
- No network worker accesses a `DomDocument`, `DomNode`, CSS engine, or GC
  value.
- No speculative request changes a page's semantic resource ordering.
- No general parallel-layout switch before the intrinsic-sizing work is
  measured and made deterministic.
