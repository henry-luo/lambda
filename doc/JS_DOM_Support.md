# LambdaJS — JavaScript and DOM Support

LambdaJS is Lambda's embedded JavaScript engine. It shares the Lambda runtime's value representation, garbage collector, AST interpreter and MIR JIT, and it runs page scripts inside the Radiant viewer as well as standalone Node-style programs.

> **Status:** usable and measured, still labelled experimental. Conformance is tracked against TC39 test262 (ES2024 scope) and Web Platform Tests subsets. The figures below are the committed baselines, which the test suites regenerate; the design rulings are in [Lambda_Formal_Design.md](Lambda_Formal_Design.md) D8.4.

## Usage

```bash
lambda js script.js                    # run a .js / .mjs / .cjs file
lambda js -e "console.log(1 + 1)"      # evaluate a snippet (-p prints the result)
lambda js app.js --document page.html  # load an HTML document for DOM access
lambda ts app.ts                       # TypeScript: annotations are stripped, not checked
lambda view page.html                  # page scripts run inside the Radiant viewer
```

The `js` or `ts` subcommand is required: a bare `lambda script.js` is read as a Lambda script and fails to parse. JSX is not supported.

Other `js` options: `--input-type=module` (read a module from stdin), `--unhandled-rejections=strict|none`, `--stack-size=KB`, `--diagnose`, `--opt-level=N`. `lambda js` with no file does nothing and exits 0. See [Lambda_CLI.md](Lambda_CLI.md#js--javascript).

## Conformance

| Suite | Baseline | Where |
|---|---|---|
| TC39 test262, ES2024 scope | **40,261 passing** of 42,889 (2,628 skipped as ES2025+ or out of scope), 0 failing | `test/js262/test262_baseline.txt` |
| Node.js official tests | no current figure: the committed baseline (3,450 entries) was recorded in July 2026, before the built-in module set was reduced (see [Node.js Compatibility](#nodejs-compatibility)) | `test/node/official_baseline.txt` |
| Web Platform Tests | 9 baselines: DOM nodes, DOM ranges, CSSOM view, CSS transitions, forms, HTML reflection, input events, intersection observer, resize observer | `test/wpt/*_baseline.txt` |
| Lambda's own JS scripts | 416 script/expected-output pairs | `test/test_js_gtest.cpp` |

### What the test262 baseline covers

Everything in the ES2015–ES2024 language and standard library that is in scope passes: classes with private members, generators and async generators, `async`/`await` and Promises with a real microtask queue, ES modules and dynamic `import()`, `Proxy`/`Reflect`, `Symbol`, `WeakMap`/`WeakSet`/`WeakRef`/`FinalizationRegistry`, `BigInt`, typed arrays with `DataView`, `Atomics` and `SharedArrayBuffer`, `RegExp` including lookbehind and backreferences (a backtracking matcher sits beside the RE2 fast path), `JSON` with reviver/replacer/space, `structuredClone`, timers, and spec-compliant `delete`, property attributes and prototype semantics.

Not covered yet: `Intl` (the object exists but is not locale-aware, and the intl402 suite is not in the baseline), proper tail calls, `Temporal`, and ES2025-or-later features such as iterator helpers, which the baseline skips.

## DOM and Web Platform

DOM interface inheritance links both constructor objects and instance
prototypes, including inherited Node constants. CDATASection and
ProcessingInstruction interface lookup follows the qualified Chromium 148
profile; XML node factories and newer constructor APIs remain unqualified
(**D8.4.4 / D9.5.6v2**; [JS13-L47/L48](../vibe/jube/JS_Issue_Ledger.md#js13-l48)).

The DOM lives in `lambda/dom/` and is realm-neutral: the same nodes are driven by JavaScript (`el.querySelector(...)`) and by Lambda (`import dom`, and the `radiant.*` behaviours the `dom` package uses). The catalogue is 351 operations plus events, CSSOM, canvas, XHR/fetch, `FormData`, observers, selection and clipboard. Highlights:

- **Nodes and traversal**: the `Node`/`Element`/`Document` APIs, `querySelector[All]`, `matches`/`closest`, `cloneNode`, `insertAdjacent*`, fragments, comments, `importNode`/`adoptNode`, `createElementNS` with the namespace recorded. `hasAttributes()` observes live HTML and namespaced attributes; imported fragments initialize their custom descendants before property binding, preserving detached lifecycle behavior (**D8.4.4 / D9.5.4**, [JS13-L8/L9](../vibe/jube/JS_Issue_Ledger.md#js13-l8--missing-elementhasattributes-rejects-dynamic-lit-templates--resolved)).
- **Namespace lookup**: `Node.lookupNamespaceURI` supports captured prototype calls, HTML/SVG/XML expanded names, namespace declarations and node-kind dispatch, including fragment isolation. Four native/Chromium fixtures cover this surface; other namespace methods and complete WebIDL/XML conformance remain unqualified (**D8.4.4 / D9.5.6v2**; [JS13-L37](../vibe/jube/JS_Issue_Ledger.md#js13-l37--missing-nodelookupnamespaceuri-breaks-captured-consent-wrapper-calls--fixed)).
- **Node constants**: node-kind and document-position constants are inherited from `Node.prototype` and available on the constructor. The shared Node/CSSRule installer uses enumerable, readonly, non-configurable descriptors (**D8.4.4 / D9.5.6v2**; [JS13-L35](../vibe/jube/JS_Issue_Ledger.md#js13-l35--missing-inherited-node-constants-crash-ancestor-walks--fixed)).
- **Captured content accessors**: `Node.prototype.nodeValue` and `textContent` expose branded getters and setters. Detached text mutations retain their registered document and observer old values, including nodes owned by foreign documents. Container text replacement shares the aggregate child-list notification path (**D9.5.4 / D5.5.1**, [focused qualification](<../vibe/Lambda_Issue_Ledger (fixed).md#lr12-64>)).
- **Events**: dispatch with capture and bubbling, `addEventListener` options, `preventDefault`, input/keyboard/pointer events, `MutationObserver`, `IntersectionObserver`, `ResizeObserver`. Resource `load` propagation stops at Document; directly dispatched Window load events remain observable. Programmatic click suppresses reentry on the same element through dispatch and activation; independent targets, later clicks and direct dispatch remain available ([JS13-L38](../vibe/jube/JS_Issue_Ledger.md#js13-l38), **D8.4.4 / D9.1.3v2**).
- **Form submission events**. `SubmitEvent.submitter` and `FormDataEvent.formData` are branded readonly properties. Native activation and `requestSubmit()` share guarded validation and submit dispatch. The shared form-data event gives listeners a mutable entry list and returns a snapshot of their changes to the caller (**D6.3.3 / D7.2.2 / D9.5.6v2**). [Forms qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l12--dialog-form-submission-and-submitter-values--partial)
- **Dialog and popover visibility**. Native `ToggleEvent` exposes readonly `oldState`, `newState` and `source`. `HTMLElement.popover`, `showPopover({source})`, `hidePopover()` and `togglePopover(forceOrOptions)` share the automatic and hint stacks with button activation. `showModal()` makes the rest of the document inert for pointer targeting and focus, and the topmost modal owns interaction when dialogs are nested. `HTMLElement.inert` reflects authored inertness. Dialogs and popovers share the native top layer (**D6.3.3 / D7.2.2 / D9.1.3v2 / D9.5.6v2**). [Dialog and popover qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l13--dialog-and-popover-toggle-events--partial)
- **Declarative shadow roots**. HTML navigation, `Element.setHTMLUnsafe()`, `ShadowRoot.setHTMLUnsafe()` and `Document.parseHTMLUnsafe()` recognize `<template shadowrootmode>`. Matching `attachShadow()` preserves the declarative root's identity and clears its children. Clonable roots copy with the host. Element and root `getHTML()` include serializable or explicitly listed roots on request (**D9.1.1v4 / D9.1.3v2 / D6.3.3**). [Shadow DOM qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l2--shadowroot-apis-do-not-provide-full-composed-tree-rendering--partial)
- **Progress events**. `ProgressEvent` inherits from `Event` and exposes branded readonly `lengthComputable`, `loaded` and `total` properties. FileReader dispatches the same interface from its native tasks. XHR progress delivery remains open (**D6.3.3 / D7.2.2 / D9.1.3v2**). [Progress event qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l14--progress-event-interface-and-delivery--partial)
- **Button commands**. `HTMLButtonElement.command` and `commandForElement` share association and activation across native and scripted clicks. `CommandEvent.command` and `source` are branded readonly properties. A canceled command suppresses the popover or dialog default action (**D6.3.3 / D7.2.2 / D9.1.3v2 / D9.5.6v2**). [Button command qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l15--button-commands-and-command-events--partial)
- **Native image requests**: HTMLImageElement exposes branded loading reflection, currentSrc, complete and decode(). Decode promises wait for real raster pixels or a parsed SVG image, reject failed or superseded pending requests, and retain callbacks through precise roots. Parsed images deliver completion through the document load boundary. Snapshot preparation also polls later completions and drains their retained-realm callbacks without moving the selected virtual frame (RI-187; **D6.4.1 / D10.1.7**). Loading remains a hint; viewport-based lazy admission and full image WPT conformance are not claimed (**D8.4.4 / D6.3.3 / D5.5.2 / D9.5.6v2**). See [the focused qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l16--image-request-state-decode-promises-and-initial-completion--fixed-for-the-native-image-profile).
- **Window document binding**: `window.document` is an own, readonly, non-configurable getter returning the same proxy as bare `document`. Captured getters validate their Window receiver and accept null/undefined as the realm global (**D8.4.4 / D6.4.1**); see [the bootstrap repair](../vibe/Lambda_Issue_Ledger.md#online-window-document-accessor).
- **TreeWalker filters**: the existing `nextNode`, `firstChild` and `nextSibling` methods apply function/object filters after `whatToShow`, distinguish subtree rejection from skipping a node, and preserve traversal boundaries. Callback exceptions propagate, clear the active flag and leave `currentNode` unchanged; reentrant filtering throws `InvalidStateError`. The readonly `filter` retains callback identity. This does not qualify the other traversal methods (**D9.5.6v2 / D6.3.3**); see [LR12-55](../vibe/Lambda_Issue_Ledger.md#lr12-55).
- **NodeIterator**: main and parsed documents provide filtered bidirectional traversal with readonly cursor properties and removal adjustment for both the reference and in-flight candidate. Private state uses existing weak collections and precise realm roots (**D8.4.4 / D9.1.3v2 / D6.3.3**). Seven native/browser fixture groups qualify this path; full traversal WPT conformance remains unqualified. See [JS13-L21](../vibe/jube/JS_Issue_Ledger.md#js13-l21--nodeiterator-dependent-sanitization-fails--resolved).
- **Storage events**: synthetic `StorageEvent` construction, branded readonly payloads, nullable string/default conversion, `initStorageEvent` and ordinary dispatch are supported. Automatic cross-window storage broadcasting remains open (**D8.4.4 / D6.3.3**); see [the focused qualification](../vibe/Lambda_Issue_Ledger.md#online-storage-event).
- **Resource timing**: real fetch/XHR entries inherit `PerformanceResourceTiming` and `PerformanceEntry`, with branded getters and private metadata preserved through GC and public property shadowing (**D8.4.4 / D6.3.3**). The existing profile exposes name, entryType, startTime, duration, initiatorType, fetchStart and responseEnd; detailed network phases and size metadata remain unsupported. See [the bootstrap repair](../vibe/Lambda_Issue_Ledger.md#online-resource-timing-brand).
- **Pending readable-stream reads**: default reads wait for asynchronous pull/start completion and are settled by enqueue, close or error. Concurrent pulls serialize, original errors persist, and private request ownership survives GC (**D8.4.4 / D5.5.1 / D6.3.3**). Full byte-stream/BYOB, locking, cancellation, backpressure and piping conformance remain unqualified. [Focused qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l25--asynchronous-readable-pulls-report-eof-before-producing-data--fixed-for-pending-default-reads).
- **Fetch URL inputs**: the string arm accepts URL objects and other coercible inputs, preserves conversion failures as promise rejections, and retains complete input/resolved/redirect URLs. Fetch and XHR resolve against the live HTML base, including base mutation/removal and network bases in local documents ([base qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l34--fetch-ignores-html-base-and-xhr-retains-stale-base-urls--fixed)). Request-valued fetch remains unsupported (**D8.4.4 / D9.1.3v2 / D6.3.3**). [String-input qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l23--fetch-rejects-coercible-urls-and-truncates-long-addresses--fixed-for-string-input).
- **Live URL query binding**: URLSearchParams mutations update the global URL's search/href; replacing search/href refreshes the same parameter object. Private ownership survives GC and public property shadows. Pathname writes also rebuild coherent private state with path encoding and shared normalization while preserving query ownership ([JS13-L49](../vibe/jube/JS_Issue_Ledger.md#js13-l49)); Windows file-URL drive interoperability remains unqualified. Other component setters and full URL/IDL/WPT conformance remain unqualified (**D8.4.4 / D6.3.3 / D9.5.4**). [Focused qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l24--url-query-and-search-parameter-records-diverge--fixed-for-queryhref-binding).
- **Live Location components**: Document and Window share a separate cached Location. URL components are live, enumerable, non-configurable own accessors; captured getters validate receivers and retain their document through GC. Reads use Location's private owner; Document no longer exposes those nine fields, so source-root host traversal terminates at the Document (JS13-L50). Copying Location therefore preserves the base path for relative URL parsers. Existing navigation/history operations are reused. Full component-setter/navigation conformance, ancestorOrigins and cross-origin exotic behavior remain unqualified (**D8.4.4 / D9.5.6v2 / D6.3.3**). [Focused qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l40).
- **Virtual immediate tasks**: setImmediate retains next-turn ordering through the existing timer ID boundary without adding elapsed milliseconds. Fetch continuations can therefore render at the selected frozen frame (**D8.4.4 / D6.4.1 / D10.1.7**). [Regression qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l42).
- **Snapshot request liveness**: requests started by render checkpoints and same-time script callbacks participate in the existing finite-request drain. An earlier libuv idle result cannot retire them; the watchdog and selected virtual frame remain unchanged (**D6.4.1 / D6.4.4 / D10.1.7**). [Regression qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l41).
- **Snapshot request timing**: finite context-owned requests participate in the existing frame slices before the post-load frame is selected. Fetch/XHR-created CSS effects therefore receive the remaining interval; after-window delivery and explicit zero windows keep their selected clock. Native image-manager delivery remains separate. Seven native/MIR/Chromium controls cover this bounded profile (**D6.4.1 / D6.4.4 / D10.1.7**). [Qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l43).
- **HTTP client profile**: fetch, asynchronous XHR and synchronous XHR use the same default user agent as document and resource requests (**D8.4.4 / D9.1.3v2**). [Wire-level qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l22--script-http-requests-lose-the-shared-client-profile--resolved).
- **XHR JSON responses**: `responseType="json"` decodes UTF-8 and caches the parsed value in traced private state. Unfinished, empty and malformed responses yield null; reopening resets the value. Text modes retain their existing behavior. Other binary/document response modes remain unqualified (**D8.4.4 / D6.3.3 / D9.5.6v2**). [Focused qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l68).
- **Adjacent HTML insertion**: `insertAdjacentHTML()` uses the backed DOM transaction, activating inserted stylesheets and publishing observers/custom-element reactions. All four positions, context and document ownership, coercion/errors, inert scripts and precise GC roots have AST/MIR/Chromium controls. Fragment mutation records retain the actual added sequence through a native snapshot shared with Lambda callers. XML parsing, Trusted Types and full DOM/IDL conformance remain unqualified (**D8.4.4 / D6.3.3 / D9.5.6v2**). [Qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l54).
- **Range contextual fragments**: `createContextualFragment()` uses the live start context, preserves range bounds and owner document, and prepares returned scripts for later insertion. HTML body/table contexts, SVG integration, coercion, branding and script lifecycle have AST/MIR/Chromium controls; Lambda uses the same DOM body. XML parsing is explicitly unsupported; complete Range/parser conformance and Trusted Types remain unqualified (**D8.4.4 / D6.3.3 / D9.5.6v2**). [Qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l53).
- **Message payloads**: MessagePort and Window delivery preserve undefined separately from empty queues and constructor defaults. Node receiveMessageOnPort returns an own message property even for undefined. Queue rebuilding uses precise roots; focused AST/MIR, Chromium and Node controls cover FIFO delivery, reentry and collected object payloads (**D8.4.4 / D6.3.3 / D9.5.6v2**). [Qualification](../vibe/jube/JS_Issue_Ledger.md#js13-l52).
- **EventTarget inheritance**: Node's constructor and prototype inherit EventTarget. Node and Window event methods honor prototype and instance overrides; Document own descriptors, keys and deletion share the node expando store. Focused AST/MIR and Chromium controls cover these operations ([JS13-L39](../vibe/jube/JS_Issue_Ledger.md#js13-l39), **D8.4.4 / D9.5.4 / D9.5.6v2**).
- **Script preparation**: parser-discovered scripts, document.write and dynamically appended inline classics are implemented, including the qualified same-origin XHR reduction. Parser tasks snapshot live attributes/text once before scheduling; prepared deferred sources survive later attribute changes and removal. Head parser arrivals reach observers before preparation. Inert fragments, started clones, atomic insertion and enclosing microtask checkpoints have focused coverage. Connected classic and module external scripts use asynchronous fetch and one source per host task, preserving default-async completion order, explicit async=false ordering and load/error delivery. Modules reuse strict lexical scope, relative imports and evaluation caching; currentScript stays null. Nested source evaluation retains the enclosing callback's timer/frame boundary. Existing source caps remain in force. Body parser-arrival replay and dynamic inline modules remain open (**D9.1.3v2 / D8.4.4 / D9.5.6v2 / D10.1.7**); see [qualification and remaining limits](../vibe/Lambda_Issue_Ledger.md#online-dynamic-script-preparation). [Foreign-document retirement](../vibe/Lambda_Issue_Ledger.md#online-foreign-document-retirement) releases adopted native storage before its main document pool disappears (**D5.5.1 / D6.3.3**).
- **Classic global bindings**: the realm owns global var/function properties and lexical bindings. Task boundaries do not synthesize library aliases, export named lexical functions, or read/write unrelated author accessors. Eleven shared controls pass in AST, MIR and Chromium (**D8.4.4**); see [the loader repair](../vibe/jube/JS_Issue_Ledger.md#js13-l18--loader-rewrites-mutate-author-globals--resolved).
- **Initial geometry observation**: standalone exports obtain the first layout through the shared DOM checkpoint even when page scripts never read geometry. Host-driven commit guards and the selected virtual frame remain authoritative (**D9.1.1v4 / D9.1.3v2 / D10.1.7**); this does not claim complete observer conformance.
- **CSSOM and layout**: `element.style` get/set with camelCase mapping, `getComputedStyle` over the full cascade, stylesheet objects, `classList` and `dataset`, `getBoundingClientRect`, `offsetWidth`/`clientHeight` and `scrollTop`/`scrollLeft` computed from a lazy layout pass.

Dataset writes share `setAttribute`'s CSS dependency checks: unrelated metadata
retains geometry, while CSS-dependent values invalidate it. Mutation observers
still receive the writes and old values (**D9.5.6v2**; [LR12-60](../vibe/Lambda_Issue_Ledger.md#lr12-60)). Rewriting an identical
metadata value also retains geometry when CSS references it; callback-driven
style changes still invalidate layout ([LR12-61](../vibe/Lambda_Issue_Ledger.md#lr12-61)).
Preload/icon link metadata uses the same dependency checks; links with a
stylesheet relation or associated sheet retain stylesheet invalidation
(**D9.5.6v2**; [JS13-L29](../vibe/jube/JS_Issue_Ledger.md#js13-l29--non-stylesheet-link-metadata-forces-full-cascade-and-layout--fixed)).
CSSOM inline writes publish style attribute records with old values and
custom-element callbacks. Style-dependent selectors retain recascade; eligible
property writes use the existing retained paint path. Transform CSS-wide resets
and pending substitutions retain layout because they can remove a containing
block (RI-172, **D9.1.3v2**). Shared notifications follow **D8.4.4 / D9.5.6v2**;
[JS13-L30](../vibe/jube/JS_Issue_Ledger.md#js13-l30--cssom-writes-discard-authored-attribute-notification-and-property-identity--fixed).
SVG `stroke-dashoffset` updates retain HTML layout when selector and motion
checks permit it. Attribute records, inherited SVG values and cache epochs
remain intact; SVG repaint uses the existing renderer (**D9.1.3v2 / D9.5.6v2 /
D5.5.1**; [RI-173](../vibe/Radiant_Issue_Ledger%20%28fixed%29.md#ri-173)).
Removing an absent attribute publishes no mutation or layout invalidation;
removing a present empty attribute still delivers its observer record and
custom-element callback (**D8.4.4 / D9.5.6v2**;
[JS13-L26](../vibe/jube/JS_Issue_Ledger.md#js13-l26--removing-absent-attributes-publishes-phantom-mutations--fixed)).
- **Collection iteration**: DOM and CSSOM collection value iterators share the realm's `Array.prototype.values`, including `CSSStyleDeclaration`. The factory defers length reads, observes live collection changes and preserves branded getter errors through proxies. AUTO, AST and MIR share this behavior (**D8.4.4 / D9.5.5**); broader CSSOM and realm-crossing conformance remains open.
- **Document**: `document.cookie`, `readyState`, `title`, `URL`/`location`, `write`, `innerHTML`/`outerHTML` (parsing HTML fragments), forms and form controls, selection and ranges, clipboard.

`Document.title` has a branded setter that updates the real title node, including
foreign documents and SVG roots; reads normalize ASCII whitespace. Missing HTML
titles are created under an existing head. `createDocument` retains its requested
root namespace (**D9.1.3v2 / D9.5.6v2**;
[JS13-L27/L28](../vibe/jube/JS_Issue_Ledger.md#js13-l27--documenttitle-is-published-without-a-setter--fixed)).
Nominal Error instances expose lazy, non-enumerable captured stacks through the
existing native formatter, including subclass getters and GC during first access
(**D3.1.5 / D8.4.4 / D6.3.3**;
[JS03-L3](../vibe/jube/JS_Issue_Ledger.md#js03-l3--nominal-error-instances-omit-captured-stacks--fixed-for-lazy-stack-access)).

The design record is [Lambda_Design_DOM_API.md](../vibe/Lambda_Design_DOM_API.md).

## Node.js Compatibility

`lambda js` runs a script the way `node` would for a core subset of Node.js. The built-ins ship as native modules beside the executable (`node-core`, `node-fs`, `node-net` and `node-crypto` in the standard set).

- **Module loading**: CommonJS `require()` and ES `import` of relative and absolute files, JSON, and directories with a `package.json` `main` or an `index.js`; `module.exports`, `__filename`, `__dirname`, and the `node:` prefix. Bare package names are not resolved: there is no `node_modules` lookup, no `package.json` `exports`, and no package installer.
- **Built-in modules that load**: `fs` and `fs/promises`, `path`, `os`, `url`, `querystring`, `string_decoder`, `punycode`, `buffer`, `crypto`, `timers` and `timers/promises`, `module`, `constants`, `perf_hooks`, `v8`, `tty`, `worker_threads`, and a small `util` (`promisify`, `format`, `inspect`, `inherits`, `types`).
- **Globals**: `process` (reports `v20.0.0`), `console`, `Buffer`, timers and `setImmediate`, `performance`, `fetch`, `URL`, `TextEncoder`/`TextDecoder`, `AbortController`, `structuredClone`.
- **Not available**: `events`, `stream`, `http`, `https`, `net`, `tls`, `zlib`, `child_process`, `assert`, `readline`, `cluster`, `dns`, `vm`, `async_hooks` and `node:test`. `require()` of any of them throws "Cannot find module". An earlier, wider layer covered these; it was removed in September 2026.

Tests: `test/node/` (231 scripts).

## Performance

Benchmarks are re-run on every release under `test/benchmark/`; the current report is `Overall_Result50.md` (2026-09-30, Apple Silicon, Node.js v22.13.0, 63 benchmarks in 7 suites, median of 3 runs).

| Engine | Geometric mean vs Node.js (V8) | Note |
|---|---:|---|
| LambdaJS, pinned MIR JIT | 7.43× slower | each benchmark's self-reported workload time; faster than Node on 6 of 63 rows; QuickJS is 7.24× on the same rows |
| LambdaJS, default `auto` tier | 8.86× slower | the shipped configuration, timed end to end (start-up and compilation included; 62 of 63 rows); QuickJS is 1.26× on this measure |
| Lambda Script, MIR typed | 0.54× (faster) | the same workloads written in Lambda, self-reported workload time |

By suite, LambdaJS is level with Node on the recursion-heavy R7RS set (0.99×) and far behind on JetStream (26.7×) and the text-processing rows (23.4×). V8 wins where its optimizing compiler and generational GC matter most: class-heavy code with hot property access, allocation-heavy workloads and string processing. The six rows where LambdaJS is ahead are small recursive and numeric kernels (`tak`, `cpstak`, `sum`, `sumfp`, `sieve`) and `pidigits`. Use JavaScript on LambdaJS for compatibility with existing code and page scripts; write performance-sensitive code in Lambda Script.

## Architecture

```
JavaScript / TypeScript source
        │
        ▼
  first-party C parser (lexer + recursive-descent/Pratt)  →  unified AST
        │
        ├── AST interpreter (default tier)
        └── MIR lowering (hot functions, or JS_EXEC_BACKEND=mir) → native code
        │
        ▼
  JS runtime (lambda/js: objects, prototypes, builtins, RegExp, event loop, modules)
        │
        ▼
  DOM and web APIs (lambda/dom, shared with Lambda) → Radiant (layout, paint, events)
```

`lambda/js` is about 150K lines (131K of C++; the rest is the C parser, the builtin catalogue and generated Unicode tables) and `lambda/dom` about 34K. Values are Lambda `Item`s, so a page script and a Lambda transformation operate on literally the same objects; the compilation tiers are in [Lambda_Formal_Design.md](Lambda_Formal_Design.md) D8.4 and the value model in D2.
