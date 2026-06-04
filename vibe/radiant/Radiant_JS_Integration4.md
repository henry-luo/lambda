# Radiant JS Integration Phase 4: Post-DOM Script Pipeline Proposal

**Date**: 2026-06-04
**Prerequisites**: Phase 2 load-time JS coverage and Phase 3 interactive event handler support
**Goal**: Replace the current concatenated-script load path with a browser-compatible post-DOM script loading, execution, event, and lifecycle model while preserving existing Radiant event dispatch, event simulation, and baseline behavior.

---

## 1. Executive Summary

Radiant's current HTML JS pipeline is effective for many CSS2.1 and layout tests because it:

- builds the DOM first,
- collects all `<script>` elements and body `onload` code,
- concatenates them into one synthetic source file,
- compiles that source once through the Lambda JS MIR path,
- executes it before CSS cascade/layout,
- optionally retains the JS MIR/runtime state for interactive event handler dispatch.

That design made the first JS integration tractable, and the Phase 3 event dispatch work should be preserved. However, the model is too coarse. Browsers do not concatenate all scripts into one synthetic source file; they execute distinct script units, keep one document JS realm alive, run microtask checkpoints, dispatch real lifecycle events, and apply different scheduling rules for classic, `defer`, `async`, and module scripts.

Decision for this phase: do not refactor the HTML parser and do not interleave HTML parsing with JavaScript execution. Radiant should continue to parse HTML into a complete DOM first, then transpile and run scripts if needed. This intentionally ignores parser-timing corner cases such as parser-blocking script visibility and `document.write()` during parsing until there is a stronger need for them.

The proposed Phase 4 direction is:

1. Keep the existing DOM bridge, CSSOM bridge, retained JS state, inline handler registry, event dispatch, post-handler relayout, and event simulation.
2. Introduce a script task and scheduler layer.
3. Move from "one synthetic document script" toward "one persistent document realm, many script units".
4. Preserve baseline tests by keeping the legacy pipeline as the default until each stage has coverage.
5. Use the post-DOM task pipeline to improve both correctness and performance: smaller script units, per-script caching, clearer lifecycle order, and future parallel fetch/compile where it is safe.

---

## 2. Current JS Pipeline

### 2.1 Pipeline Position

The current loader roughly does this:

```text
HTML file
  -> html5_parse()
  -> Lambda Element* tree
  -> DomElement* tree
  -> parse linked and inline stylesheets
  -> apply inline style="" attributes
  -> execute_document_scripts()
  -> re-scan dynamic/disabled <style> after JS mutations
  -> CSS cascade
  -> layout
  -> output/render
```

The important point is that load-time JS runs after the parsed DOM tree is already built, and before the normal style cascade/layout pass.

### 2.2 Script Collection

`execute_document_scripts()` in `radiant/script_runner.cpp` walks the parsed `Element*` tree using `collect_scripts_recursive()`.

For each supported classic script:

- external `<script src="...">` is resolved against the document URL,
- local script files are read from disk,
- HTTP(S) scripts are loaded through the cached HTTP helper,
- inline script text is extracted from the element children,
- body `onload` text is collected separately and appended after scripts.

Unsupported script types are skipped unless they are classic JavaScript MIME types:

- no type,
- `text/javascript`,
- `application/javascript`,
- `text/ecmascript`,
- `application/ecmascript`.

### 2.3 Synthetic Source Generation

All collected scripts are appended into one `StrBuf`.

The generated source includes:

- a browser preamble with `window`, `navigator`, `document`, timer aliases, storage stubs, observer stubs, console/performance/screen stubs, `XMLHttpRequest`, `WebSocket`, `Worker`, event target hooks, `getComputedStyle`, scroll helpers, viewport fields, and `document.defaultView`,
- each external or inline script body wrapped in a broad `try { ... } catch (...) {}`,
- synchronization snippets for `window.jQuery`, `window.$`, `jQuery`, and `$`,
- body `onload` code wrapped in `try/catch`,
- a postamble that dispatches `DOMContentLoaded` on `document` and then directly calls `window.onload()` if set.

Current shape:

```text
generated document script
  browser preamble
  try { external script 1 } catch {}
  jQuery/$ sync
  try { external script 2 } catch {}
  jQuery/$ sync
  try { inline script 1 } catch {}
  jQuery/$ sync
  try { body onload attribute code } catch {}
  document.dispatchEvent(new Event("DOMContentLoaded"))
  if (window.onload) window.onload()
```

### 2.4 Compilation and Runtime Retention

The generated source is compiled as a single unit:

```text
tree-sitter JS parse
  -> JS AST
  -> early errors
  -> MIR generation
  -> MIR_link / code generation
  -> js_main execution
  -> event loop drain
  -> optional requestAnimationFrame drain
  -> cleanup or retained runtime
```

For static `lambda layout`, Radiant can run in transient mode and release JS state after load-time mutations are complete.

For interactive documents, Radiant uses preamble mode and retains:

- `DomDocument::js_mir_ctx`,
- `DomDocument::js_preamble_state`,
- `DomDocument::js_runtime_heap`,
- `DomDocument::js_runtime_nursery`,
- `DomDocument::js_runtime_name_pool`,
- `DomDocument::js_runtime_pool`,
- `DomDocument::js_event_registry`.

This retained state supports Phase 3 inline event handler dispatch and must not be thrown away by the new design.

### 2.5 Existing Interactive Handler Pipeline

The Phase 3 pipeline is already a useful foundation:

```text
page load
  -> compile document scripts with retained preamble state
  -> collect inline handler attributes from DomElement* tree
  -> compile wrappers with transpile_js_to_mir_with_preamble()
  -> store compiled function pointers in JsEventRegistry

event time
  -> Radiant event hit testing
  -> Lambda template handler dispatch
  -> HTML inline handler dispatch
  -> JS EventTarget dispatch where implemented
  -> DOM mutations through js_dom.cpp
  -> full re-cascade / relayout / repaint request when mutations occurred
```

This work should remain. Phase 4 should improve how scripts are loaded and scheduled, not discard the working event surfaces.

---

## 3. Issues with the Current Concatenated Pipeline

### 3.1 It Is Not Parser-Blocking

In browsers, a normal classic script without `async`, `defer`, or `type=module` blocks the HTML parser:

```html
<div id="before"></div>
<script>
  // sees #before, but may not see elements after this script yet
</script>
<div id="after"></div>
```

Radiant currently builds the whole DOM first, then runs all scripts. That means scripts can see and mutate nodes that a browser script at the same location would not yet see.

This is now an explicit Phase 4 tradeoff, not a parser refactoring requirement. Radiant should keep the full-HTML-parse-first model and improve the script pipeline after DOM construction. Parser-timing corner cases are documented limitations for now.

This affects:

- DOM queries during parse,
- `document.write()`,
- scripts that intentionally insert content before later markup is parsed,
- feature-detection code that depends on parser timing,
- scripts that attach handlers before following markup exists.

### 3.2 `async`, `defer`, and Modules Are Not Modeled

The current collector treats supported classic scripts as one ordered bundle. It does not implement browser scheduling for:

- parser-blocking classic scripts,
- `defer` scripts,
- `async` scripts,
- dynamically inserted scripts,
- module scripts,
- module dependency graphs,
- `nomodule`.

This is acceptable for the original CSS2.1 target but too limited for broader page support.

### 3.3 Broad Wrappers Change JavaScript Semantics

Wrapping each script body in `try { ... } catch {}` is useful for avoiding fatal feature-detection errors, but it is not semantically neutral.

It can affect:

- top-level function declarations inside block scope,
- lexical declarations,
- `this` expectations,
- source locations and stack traces,
- early-error behavior,
- exception reporting,
- scripts that expect a top-level exception to abort that script but still be reported.

Browsers do not rewrite script source this way. A browser reports the exception, stops the current script, and continues with later script elements where appropriate.

### 3.4 Lifecycle Events Are Approximate

The current postamble dispatches `DOMContentLoaded` on `document` and directly calls `window.onload()`.

A browser-like lifecycle needs:

- document `readyState` transitions,
- `readystatechange`,
- `DOMContentLoaded`,
- resource `load` and `error` events,
- `window` `load`,
- microtask checkpoints between scripts and after events,
- correct ordering against defer/module/async scripts,
- a document-owned event loop that remains alive until the page is closed.

### 3.5 One Large MIR Module Is a Performance Bottleneck

Recent release-mode timing on JS-heavy web templates showed the bottleneck clearly. After auto-close removes the old timer wait, the slow pages are dominated by MIR code generation/link:

```text
page                                   total   JS total  parse  AST   MIR gen  MIR link  JS exec
johndoe-portfolio-resume-template      2017ms  1981ms    55ms   37ms  535ms    1296ms    13ms
mobile-app-free-one-page-template      1823ms  1767ms    42ms   32ms  466ms    1164ms    27ms
iam-html5-responsive-portfolio         1787ms  1708ms    48ms   30ms  483ms    1089ms    22ms
```

Those pages have many external scripts. Examples:

```text
iam-html5-responsive-portfolio:
  11 external scripts, 2 inline scripts, about 369 KB local JS
  jquery.js alone is about 284 KB

johndoe-portfolio-resume:
  15 external scripts, about 409 KB local JS
```

The current path compiles all of that into one synthetic module. There is no per-script cache for common libraries such as jQuery or Bootstrap, and there is no parallel script compilation for these classic scripts.

### 3.6 Error and Timeout Boundaries Are Too Coarse

The current watchdog protects the whole generated document script. If one library has a compile/runtime issue, the failure boundary is the synthetic document script, not the specific script element.

A browser-like model should have per-script source identity, per-script error reporting, and clearer recovery semantics.

---

## 4. Target Browser-Like Design

### 4.1 Preserve Existing Working Pieces

The new design should keep:

- `js_dom.cpp` DOM bridge behavior,
- CSSOM support,
- dynamic style re-scan after JS mutations,
- retained JS runtime/MIR state on `DomDocument`,
- compiled inline event handler registry,
- `dispatch_html_event_handler()`,
- JS EventTarget dispatch helpers,
- event simulation and UI automation,
- post-handler re-cascade/relayout/repaint,
- crash and timeout guards,
- `lambda layout --auto-close` semantics.

Phase 4 should change script loading and lifecycle ordering, not re-solve already working event dispatch.

### 4.2 New Core Objects

Introduce a small set of explicit pipeline objects.

```c
typedef enum ScriptKind {
    SCRIPT_KIND_CLASSIC,
    SCRIPT_KIND_MODULE
} ScriptKind;

typedef enum ScriptScheduling {
    SCRIPT_SCHEDULING_PARSER_BLOCKING,
    SCRIPT_SCHEDULING_DEFER,
    SCRIPT_SCHEDULING_ASYNC,
    SCRIPT_SCHEDULING_DYNAMIC
} ScriptScheduling;

typedef struct JsScriptTask {
    ScriptKind kind;
    ScriptScheduling scheduling;
    Url* base_url;
    char* resolved_url;
    char* source;
    size_t source_len;
    bool external;
    bool parser_inserted;
    bool already_started;
    bool force_async;
    int source_line;
    int source_column;
    DomElement* script_element;
} JsScriptTask;
```

Suggested owner objects:

```text
JsDocumentRealm
  one per DomDocument
  owns JS heap/nursery/name pool/runtime document binding
  owns window/document/global state
  owns event loop queues associated with this document

JsScriptLoader
  resolves URLs
  fetches local/HTTP script source
  records load/error state
  may fetch async/defer/module sources in parallel

JsScriptScheduler
  receives ScriptTask objects
  enforces Radiant's post-DOM classic/defer/async/module ordering
  calls compile/execute when a script is ready and due

JsScriptCompiler
  compiles one script unit
  may use parse/AST/MIR cache
  may JIT or interpret depending on policy
```

### 4.3 Document Realm

Radiant should have one JS realm per document:

```text
DomDocument
  -> JsDocumentRealm
       -> window/global object
       -> document binding
       -> active script state
       -> event loop queues
       -> microtask queue
       -> retained heap/nursery/name pool
       -> retained MIR contexts or compiled-code cache entries
```

The existing retained fields on `DomDocument` can be migrated behind this realm object over time. Initially, the realm can be a thin wrapper around the existing fields.

The important rule: script units execute in the same document realm, so `var`, global function declarations, event listeners, DOM wrappers, timers, and globals persist across scripts.

### 4.4 Post-DOM Script Ordering

The target script rules within Radiant's full-DOM-first constraint:

```text
classic script, no async/defer:
  fetch if needed
  execute in document order after HTML parsing completes
  run microtask checkpoint after each script

classic defer script:
  execute after parse completes
  preserve document order
  execute before DOMContentLoaded

classic async script:
  may fetch in parallel after collection
  execute when ready if async scheduling is enabled
  no document-order guarantee where Radiant models async behavior

module script:
  fetch module graph
  deferred by default
  strict/module scope
  execute after graph is ready
  before DOMContentLoaded for parser-inserted modules

dynamically inserted classic script:
  async by default unless force_async is cleared
```

Because Radiant still parses the full HTML first, normal classic scripts are not parser-blocking in this phase. Scripts that depend on seeing only earlier DOM nodes, or on inserting markup before later tokens are parsed, remain out of scope.

### 4.5 HTML Parser Boundary

Do not change the HTML parser for this phase. The pipeline boundary should remain:

```text
parse full HTML document
  -> build complete DOM tree
  -> collect ScriptTask objects from the DOM
  -> fetch/transpile/execute required scripts in scheduler order
  -> run microtask checkpoints at script and lifecycle boundaries
  -> dispatch DOMContentLoaded/load according to Radiant's modeled lifecycle
```

This keeps the parser stable and avoids a large parser pause/resume refactor. It does not match browser parser-blocking semantics, but those corner cases can be deferred.

### 4.6 Staged Compatibility Path

To avoid regressions, do not jump directly from concatenation to a new default executor.

Recommended sequence:

1. Add `JsScriptTask` collection while still using the legacy concatenated executor.
2. Add a feature flag for separate-script execution after full DOM build.
3. Make separate-script execution preserve existing CSS2.1 and UI automation results.
4. Add post-DOM lifecycle ordering for DOMContentLoaded/load and modeled `defer`/`async` behavior.
5. Move default behavior after `make test-radiant-baseline`, JS layout suites, and UI automation remain stable.

Possible controls:

```text
RADIANT_JS_PIPELINE=legacy
RADIANT_JS_PIPELINE=tasks-postdom
```

Default should remain `legacy` until the new path has enough coverage.

### 4.7 Parallelism

Browser rules allow parallelism in fetching, but not arbitrary parallel execution.

Safe parallelism:

- preload/fetch external scripts discovered by a speculative scanner,
- fetch `async`, `defer`, and module dependencies in parallel,
- parse/compile scripts in parallel only if the compiler product is independent of the document realm,
- compile cached external libraries once by URL/content hash.

Serial requirements:

- classic script execution mutates the document realm serially,
- classic defer execution preserves document order,
- all DOM mutations happen on the document thread/realm,
- microtask checkpoints occur at defined boundaries.

Current MIR compilation often captures runtime/transpiler pools and document state. Before parallel compile is enabled, the compiler output must be split into:

```text
reusable compile artifact
  parse tree / AST / MIR template / generated code

per-document instantiation
  global object
  document wrappers
  module/global bindings
  heap allocations
```

Until that split is safe, keep execution and compile serial inside the document realm and focus first on per-script cache and smaller modules.

### 4.8 Script Cache

A browser-like pipeline should enable a cache for external scripts:

```text
cache key:
  resolved URL
  content hash or mtime+size
  script kind: classic/module
  compile mode/options
  JS runtime version
  MIR optimization/interpreter mode
```

Cache layers can be added incrementally:

1. Source cache: already mostly present for HTTP resources.
2. Parse/AST cache: avoids tree-sitter parse and AST build.
3. MIR template cache: avoids AST-to-MIR lowering.
4. Generated-code cache: only if code can be safely rebound to a document realm.

For web templates, caching common libraries such as jQuery and Bootstrap is likely a larger win than trying to parallelize script execution.

### 4.9 Error Reporting and Watchdogs

Move from document-script error boundaries to script-task boundaries:

```text
for each ScriptTask:
  compile with source URL/line metadata
  execute with script-specific watchdog
  report error to window error handling
  continue or abort according to script kind and scheduling
```

Do not rely on injected `try/catch` wrappers for browser error semantics. Keep signal guards and watchdogs around compiler/runtime execution, but let the JS engine report exceptions.

---

## 5. Browser-Like Document Lifecycle

### 5.1 Target Load Sequence

For `lambda layout` and `lambda view`, the document lifecycle should be explicit:

```text
create DomDocument and JsDocumentRealm
readyState = "loading"

parse HTML
collect script tasks from the complete DOM

finish parsing
readyState = "interactive"
dispatch readystatechange

execute normal classic scripts in document order
execute defer scripts in order
execute parser-inserted module scripts after graph load
microtask checkpoint after each executed script

dispatch DOMContentLoaded on document
microtask checkpoint

wait for load-blocking resources that Radiant models
readyState = "complete"
dispatch readystatechange
dispatch load on window
microtask checkpoint

if auto-close:
  cancel future timers/rAF/network callbacks
  close document
else:
  keep event loop alive for user interaction
```

### 5.2 Auto-Close

The current auto-close change is directionally right: close after load/onload has run, not after a fixed 5s timer drain.

In the new lifecycle, auto-close should mean:

```text
run all work required to reach browser "load" completion
dispatch load
run microtasks created by load handlers
cancel future timers/rAF/async network callbacks
tear down the page
```

It should not fire arbitrary future `setTimeout` or `setInterval` callbacks just because they are queued.

### 5.3 Event Loop Ownership

Move timers, animation frames, promise jobs, and future network callbacks under the document realm.

Required behavior:

- microtasks flush after each script and event callback,
- timers survive across scripts and user events while the document is open,
- `requestAnimationFrame` is tied to Radiant's frame clock in view mode,
- static layout can skip future frame/timer callbacks after load when auto-close is enabled,
- document teardown cancels outstanding callbacks and releases roots.

---

## 6. HTML Event Handler Attributes

### 6.1 Recommendation

Do not interpret HTML event handler attribute strings with an ad hoc interpreter.

They should be handled by the same JavaScript engine as normal scripts, because attribute handlers need normal JS semantics:

- calls to functions defined by earlier scripts,
- access to globals on `window`,
- DOM and CSSOM bridge calls,
- closures/runtime objects where supported,
- exceptions and return values,
- `this` binding,
- `event` parameter,
- `return false` default-prevention behavior.

The implementation may choose a JIT backend or an interpreter backend, but the source should still be parsed/lowered by the real JS frontend. Manual string interpretation would become a correctness trap.

### 6.2 Current Working Choice

The current Phase 3 design compiles handler attributes into wrapper functions:

```javascript
function __evt_handler_0() { clicked(); }
function __evt_handler_1() { toggle('col2'); }
```

Those wrappers are compiled with `transpile_js_to_mir_with_preamble()` so they can see functions and globals from the retained document script preamble. The compiled function pointers are stored in `JsEventRegistry` and invoked by `dispatch_html_event_handler()`.

This is working and should be retained while Phase 4 changes the load-time script model.

### 6.3 Target Handler Semantics

The target browser-like handler model should be:

```text
HTML attribute:
  <button onclick="return clicked(event)">

compiled as:
  function __html_attr_onclick(event) {
      return clicked(event);
  }

called as:
  this = target element
  first argument = Event object
  legacy window.event = Event object during dispatch
  if return value is false: event.preventDefault()
```

Attribute handlers should be registered through the same event listener pipeline as `addEventListener()` and IDL handlers (`element.onclick = fn`) as much as possible. That lets one dispatcher handle:

- capture/bubble where supported,
- `stopPropagation()`,
- `stopImmediatePropagation()`,
- `preventDefault()`,
- default-action ordering,
- event simulation tests,
- real UI events.

### 6.4 JIT vs Interpreter

Advice:

```text
semantics:
  always parse/lower as JavaScript, not manual interpretation

current Radiant:
  keep JIT-compiled wrapper handlers because the retained preamble design works

future optimization:
  allow lazy compilation on first dispatch for large real-world pages
  use a script/handler cache keyed by handler source + document script generation
  consider MIR interpreter for tiny one-shot handlers only after interpreter mode is robust
```

Eager JIT compilation is fine for the CSS2.1 and UI automation tests because the number of handlers is small. For real pages, eager compilation of thousands of attributes can waste time. A hybrid policy is better:

```text
view/interactive page:
  compile attribute handler lazily on first event, cache afterward

layout test page with known small handler count:
  eager compile is acceptable and keeps event_sim deterministic

static lambda layout with no event simulation:
  do not compile non-load event handlers unless retained state is requested
```

The key is that both eager and lazy modes must use the same JS frontend and same event dispatch semantics.

### 6.5 Attribute Mutation

Browser-like support also needs:

- setting `onclick` attribute after load registers/replaces the handler,
- removing `onclick` unregisters it,
- assigning `element.onclick = fn` registers an IDL handler,
- assigning `null` clears it,
- dynamically inserted elements can get handlers.

This can be staged. The current registry can remain page-load-only until tests require dynamic handler mutation.

---

## 7. Proposed Implementation Plan

### Phase 4.0: Documentation and Metrics

Status: this document.

Keep collecting timing data through the existing `--timing-output` path and add per-script metrics later.

### Phase 4.1: ScriptTask Collector With Legacy Executor

Add a `JsScriptTask` collector but keep the legacy concatenate-and-compile behavior.

Purpose:

- make script metadata explicit,
- record source URLs and attributes,
- count scripts and source sizes,
- add tests without behavior changes.

Expected regression risk: very low if the executor is unchanged.

### Phase 4.2: Separate Classic Script Executor Behind Flag

Execute scripts as separate units but still after full DOM construction.

This is not full browser parser behavior, but it validates:

- persistent document realm across script units,
- per-script compile/execute boundaries,
- per-script errors,
- global sharing across scripts,
- event handler registry compatibility.

Target:

```text
RADIANT_JS_PIPELINE=tasks-postdom
```

Must pass:

- `make test-radiant-baseline`,
- current JS layout suite,
- UI automation/event_sim tests.

### Phase 4.3: Post-DOM Lifecycle and Scheduling

Implement:

- ordered defer queue,
- async readiness queue,
- DOMContentLoaded ordering,
- load-blocking behavior.

Keep module scripts separate until classic scheduling is stable.

This phase still runs after full DOM construction. Do not add parser pause/resume or token-level DOM builder hooks.

### Phase 4.4: Module Scripts

Implement module graph loading and evaluation when the JS frontend/runtime supports the required module semantics.

Do not fake module support by concatenation.

### Phase 4.5: Script Cache and Compile Policy

Add cache layers in this order:

1. source cache verification,
2. parse/AST cache for external scripts,
3. MIR template cache if safe,
4. generated-code cache only when code can be rebound to a new document realm without stale pointers.

Add compile policy:

```text
small inline script:
  compile immediately

large external script:
  use cache if available
  compile as separate unit

attribute handler:
  eager for small test pages
  lazy for large interactive pages
```

### Phase 4.6: Incremental Relayout Optimization

Keep the existing full post-handler rebuild as the correctness baseline.

Only optimize after the post-DOM script/lifecycle work is stable:

- mutation records by element and type,
- subtree re-cascade,
- subtree reflow,
- dirty-rect repaint.

---

## 8. Regression and Compatibility Plan

### 8.1 Keep Legacy as Baseline Until Proven

New script scheduling must be introduced behind a flag. Default behavior should remain legacy until all required suites pass.

### 8.2 Required Test Gates

For every implementation phase:

```bash
make test-radiant-baseline
make layout suite=js
```

For interactive changes:

```bash
make test-radiant-baseline
make test-ui-automation
```

For performance validation:

```bash
make release
./lambda.exe layout ... --timing-output temp/... --auto-close
```

Do not use debug builds for performance conclusions.

### 8.3 Tests to Add

Add targeted tests before changing defaults:

- later scripts see globals from earlier scripts,
- exception in one script does not prevent later script execution,
- `defer` order before DOMContentLoaded,
- `async` out-of-order readiness behavior,
- `DOMContentLoaded` listener through `addEventListener`,
- `window.onload` attribute/property/listener ordering,
- `onclick` attribute gets `event` and `this`,
- `return false` prevents default action,
- dynamic `element.onclick = fn`,
- dynamic `setAttribute("onclick", "...")`.

Deferred parser-interleaving tests, not required for this phase:

- parser-blocking script sees only earlier DOM,
- script-created DOM before later markup is parsed,
- `document.write()` during parse.

### 8.4 Baseline Safety Rules

No change should be made only to satisfy one failing page. Root cause must be tied to browser script/lifecycle semantics.

Avoid these shortcuts:

- hard-coded skips for specific templates,
- string rewriting beyond spec-defined wrapper compilation,
- special-casing jQuery or Bootstrap,
- suppressing exceptions globally without script-level reporting,
- changing event dispatch order to make one test pass without a spec reason.

---

## 9. Open Design Questions

### 9.1 How Much HTML Parser Refactoring Is Needed?

Decision: none for this phase.

Radiant should not change the HTML parser to interleave tokenization/DOM building with JavaScript execution. Keep the existing model:

```text
parse HTML completely
  -> build DOM
  -> collect script tasks
  -> transpile and execute scripts if needed
  -> dispatch modeled lifecycle events
  -> style/layout/render
```

This means normal classic scripts are not truly parser-blocking in Radiant. Scripts can observe the complete DOM, and `document.write()` during parser execution is not modeled. These are accepted corner-case limitations for now. The Phase 4 work should focus on separate script units, a persistent document realm, per-script errors/timing/cache, lifecycle events, and preserving event handler dispatch.

### 9.2 Can MIR Produce Realm-Independent Compile Artifacts?

Per-script caching is most valuable if an external library can be compiled once and executed in multiple document realms. Today some MIR/runtime data can reference transpiler pools, name pools, heaps, or module globals. That lifetime model must be audited before generated-code caching is enabled.

### 9.3 Should Large External Scripts Use an Interpreter?

The current release data shows `MIR_link` dominates for large browser libraries. If MIR interpreter mode becomes robust for document scripts, it may be a good static-layout policy:

```text
large library + static layout + low expected runtime:
  interpret or low-opt compile

interactive page or hot handler:
  JIT compile
```

This should be a backend policy decision, not a semantic shortcut.

### 9.4 How Should Load-Blocking Resources Be Modeled?

Browsers delay `window load` for images, stylesheets, subframes, and some script/module work. Radiant may not need to model every resource immediately, but the lifecycle should make resource readiness explicit so auto-close has a correct boundary.

---

## 10. Recommended Next Step

Start with Phase 4.1:

1. Add `JsScriptTask` metadata collection while still feeding the legacy concatenated executor.
2. Emit optional per-script timing/source-size diagnostics.
3. Add tests that assert the collector sees script attributes correctly.
4. Verify `make test-radiant-baseline` remains unchanged.

Then move to Phase 4.2 behind `RADIANT_JS_PIPELINE=tasks-postdom`. That is the first meaningful step toward separate script execution while preserving the existing working event handler dispatch and UI automation surfaces.
