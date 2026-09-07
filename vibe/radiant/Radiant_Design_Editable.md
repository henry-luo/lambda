# Radiant Editable Support — Common Gate and Full UA `contenteditable`

**Date:** 2026-07-29 · **Revised:** 2026-09-07
**Status:** Phase 1 and Phase 9 implemented and verified for the applicable
UA conformance scope; the separate unified form/history expansion remains a
proposal.
**Scope:** `contenteditable`, Lambda/Radiant editable templates, the common
editing transaction gate, registered action handlers, full UA editing
behavior, `execCommand`/`queryCommand*`, `designMode`, and conformance against
the applicable WPT and Chromium editing suites.

> **Current ruling — D7.2.5.** Phase 9 (§20) supersedes Phase 1's “minimal DOM
> compatibility, no default UA rich editing, no `execCommand`” boundary.
> Radiant's common gate remains, but all UA editing policy is implemented in
> the shipped Lambda DOM package. Native C/C++ supplies platform events and
> generic DOM/Selection/Range mechanisms only; it does not own editing
> commands, normalization, or history. Sections explicitly labeled Phase 1
> retain the implemented foundation and historical reasoning, while §20 is
> authoritative wherever their capability limits conflict.

**Phase-1 implementation boundary (2026-07-29):** The first gate is
implemented and its exact supported configurations are recorded in
`test/editable-editors/capability-manifest.json`. The public JavaScript action
registration bridge is deliberately deferred: the pinned unmodified editors
use standard DOM events and the two built-in native registrations. Editor.js
ordinary paragraph paste is an explicit exclusion because its upstream core
delegates that path to browser-native structural editing; Radiant exposes the
clipboard event but does not recreate that retired default action.

**Phase-9 implementation (2026-09-07):**
`lambda/package/dom` now owns the shared descriptor/context/plan/result path,
default actions, structural normalization, retained history, `designMode`,
the legacy command/query surface, and the conformance runners. Native code
retains generic checked DOM/Selection/Range/clipboard mechanisms only; no C++
rich-edit engine was restored. The implementation and release evidence are in
[Radiant_Impl_Editable2.md](Radiant_Impl_Editable2.md) and
[Radiant_Editable_UA6_Report.md](Radiant_Editable_UA6_Report.md).

**Edit-history design:**
[Radiant_Design_Edit_History.md](Radiant_Design_Edit_History.md) defines the
detailed proposed history architecture: Lambda-element storage, independent
surface timelines, the `EditStep` contract, atomic native-waist application,
grouping/coalescing, retention, pruning, and unified form-control history. It
remains a proposal until its D7.2.5v2 ratification steps are completed; §20.7
below is the current Phase-9 baseline in the meantime.

**Historical lineage:** §17.2 consolidates the removed CE1–CE3 design records:
the editable-host foundation, the later native-legacy-editor pivot, and the
Chromium structural-corpus program.

**Builds on:**

- [RAD_15 — Events and Input](../../doc/dev/radiant/RAD_15_Events_Input.md)
- [RAD_18 — Editing, Selection and DOM Ranges](../../doc/dev/radiant/RAD_18_Editing_Selection_Ranges.md)
- [RAD_21 — JS Scripting Integration](../../doc/dev/radiant/RAD_21_JS_Scripting_Integration.md)

---

## 1. Decision — Phase 1 gate and retained invariants

Radiant will retain one mandatory editing gate for every editable surface.
Platform keyboard, text, composition, clipboard, drag/drop, automation, and
replay input must enter this gate as a normalized `EditingIntent`. The gate is
the stable interception point for:

- event logging and audit;
- deterministic recording and playback;
- target-range and selection snapshots;
- cancellation and policy checks;
- handler routing;
- mutation, layout, and paint invalidation;
- post-transaction validation.

The existing **consumer transaction** remains, with a narrower contract. Every
edit that is not already consumed by a preceding keyboard/clipboard event
enters three explicit stages:

1. dispatch one cancelable `beforeinput` notification;
2. if it was not canceled, route the intent to one registered action handler;
3. if that handler applied or explicitly claimed an edit, dispatch one
   non-cancelable `input` notification.

`beforeinput` and `input` are notifications, not edit algorithms. They do not
select an action owner, copy clipboard data, mutate the DOM or source model,
reconcile a template, or dispatch each other. Range/payload preparation,
logging, routing, action dispatch, and post-transaction validation remain
separate responsibilities of the common gate.

Radiant C/C++ has **no command-specific editing policy** for `contenteditable`.
The fixed single-consumer assumption is replaced by a per-document registry of
editing action handlers. Multiple handlers may be registered, but routing
selects at most one action owner for a transaction. Other matching observers
may inspect the transaction through the gate, but they are not action
handlers. Under Phase 9, the Lambda DOM package is the built-in UA action
owner whenever author or model-owned code has not canceled or claimed the
edit (D7.2.5).

Two built-in action handlers are introduced:

1. **Radiant template handler** — preserves the current model-first path for
   Lambda/Radiant editable templates.
2. **Lambda DOM UA handler** — a package-owned handler for direct
   HTML/JavaScript documents. Phase 1 supplied a limited text/composition
   action; Phase 9 expands it to full `contenteditable` default behavior and
   the legacy command/query surface (§20).

Both routes use only the standard `contenteditable` attribute. Existing
document runtime/template registry and render-map ownership identify a Radiant
template context; the absence of that ownership identifies the DOM/JavaScript
route. No `data-editable`, routing attribute, or new document provenance flag
is introduced.

JavaScript integrations may also register an action handler through a Radiant
host API. This is distinct from ordinary `addEventListener` registration.
Unmodified CodeMirror, ProseMirror, and Editor.js do not use such a host API;
they use standard DOM events, Selection/Range, DOM mutation APIs, and
`MutationObserver`.

This is an extension of the present architecture, not a second editing path.
Both routes run inside `editing_run_transaction`.

The design target is **web-platform editing compatibility, not embedding a
third-party editor implementation**.
Radiant will not embed, port, or reproduce the internal document model,
transaction engine, command system, history, or plugin system of any of the
three editors. Real editor bundles are compatibility probes used to show that
the DOM surface is sufficient to deliver code and rich editors. The UA engine
is nonetheless a real browser-style editing implementation for plain
`contenteditable`; its operation model and history live in the shipped Lambda
DOM behavior package, not inside CodeMirror, ProseMirror, or Editor.js.

CodeMirror is the primary required end-to-end probe because its model and
input architecture are relatively small and well isolated. ProseMirror is the
required structured rich-editor probe, including its legacy fallback paths.
Editor.js is the required block-rich probe because it exercises a different,
DOM-first Tool model and consumes several legacy commands.

Radiant editable templates must use this same gate and the same public DOM
surface. Template editing may retain its source-model transaction and
reconciliation internals, but it must not request a template-only Selection,
Range, input, clipboard, composition, mutation, or geometry API. A capability
not needed by a real JavaScript editor is not added merely for the template
path.

Radiant will provide `document.execCommand`, `queryCommandSupported`,
`queryCommandEnabled`, `queryCommandState`, `queryCommandIndeterm`,
`queryCommandValue`, and `designMode` through thin host bindings whose policy
and implementation live in the Lambda DOM behavior package (D7.2.5).
Formatting, links, block
transformation, history, clipboard commands, and the equivalent keyboard or
`beforeinput` intents share one package command registry and operation model.

Native bindings may marshal arguments, retain the live document/selection,
and invoke the package behavior, but they must contain no command table or
editing algorithm. `insertLineBreak`, `insertParagraph`, and other structural
defaults are Phase-9 package responsibilities rather than permanent editor
requirements.

---

## 2. Goals

1. Preserve the common editing transaction gate and its logging, audit,
   record, replay, and validation properties.
2. Make `beforeinput` a pure, cancelable pre-action notification and `input` a
   pure, non-cancelable post-action notification.
3. Permit multiple editing handlers to be registered without allowing
   duplicate mutations.
4. Route direct HTML/JavaScript documents to the package-owned full UA
   `contenteditable` handler.
5. Route Lambda/Radiant template documents to model-first Radiant editing.
6. Support mixed documents containing both Radiant-owned surfaces and
   third-party JavaScript editors.
7. Define a full editable-UA surface, with behavior fixed by the applicable
   WPT and Chromium editing suites rather than editor-specific branches.
8. Run real, bundled CodeMirror 6, ProseMirror, and Editor.js under Radiant
   through normal public browser APIs, without library-specific engine
   branches.
9. Support ProseMirror, Editor.js, and other legacy-command consumers without
   adopting their private model/transform internals.
10. Add deterministic end-to-end tests for typing, deletion, line/block
    editing, selection, formatting, clipboard, composition, UA history,
    legacy commands, query state, and observer reconciliation.
11. Keep Radiant editable templates on the same input/event/DOM capability
    surface as JavaScript editors.
12. Remove transitional editable-routing, notification, result, and testdriver
    code after the new gate reaches its end-to-end cutover; do not leave two
    competing transaction paths.
13. Make the applicable automated WPT contenteditable/editing suites and the
    pinned Chromium editing corpus green conformance gates (D1.10, D7.3.5).

## 3. Non-goals

- Do not embed Chromium, WebKit, or another browser engine.
- Do not embed, port, or reimplement CodeMirror, ProseMirror, or Editor.js in
  Radiant.
- Do not add editor-specific branches, editor model types, schema rules,
  transforms, commands, or history to Radiant.
- Do not let page scripts bypass the common editing gate for platform input.
- Do not require bare npm package resolution for the first compatibility
  milestone. Checked-in test bundles are sufficient.
- Do not restore a monolithic native rich-text editor. The full UA engine is a
  Lambda package over generic native DOM mechanisms (D7.2.5, D7.5.3).
- Do not make event recording depend on the chosen editing handler.
- Do not encode command names, list/table policy, normalization, or editing
  history in C/C++.
- Do not add test-name, editor-name, or harness-mode behavior branches.
- Do not patch WPT or Chromium vendor tests. Harness adaptation and documented
  platform exclusions live on the Lambda side.
- Do not treat manual, pixel-only, spellchecking, OS-service, or unsupported
  platform tests as implemented UA behavior; classify them explicitly rather
  than silently skipping them.

---

## 4. Prior Art: Three Editing Models

The three editors are not candidate Radiant implementations. They are prior
art that exercises three materially different ownership models. The useful
common denominator is their browser-facing contract, not their internal data
structure.

| Editor | Authoritative editing state | How ordinary typing enters the model | Structural editing |
|---|---|---|---|
| CodeMirror 6 | Immutable flat text, selection, and extension state | browser mutates a controlled `contenteditable`; DOM changes are observed and converted to a state transaction | keymaps and commands dispatch transactions |
| ProseMirror | Immutable schema-constrained tree, selection, and plugin state | native editable-DOM changes are observed, parsed, and converted to transactions | commands and transform steps update the tree |
| Editor.js | Ordered block instances with Tool-owned live DOM; clean block data is extracted on save | browser normally edits a Tool's `contenteditable`; mutation observation marks the block changed | core `keydown` logic and Tool callbacks split, merge, remove, or convert blocks |

### 4.1 CodeMirror 6: model-first flat text

CodeMirror keeps an immutable `EditorState` containing a flat text document,
selection, and extension state. Updates are explicit transactions. The
`EditorView` projects that state into DOM and dispatches state transactions;
the DOM is not the durable document model.

Its input path is deliberately mixed:

- keymaps and extension DOM handlers may intercept `keydown`, clipboard, drop,
  and other events and dispatch model transactions directly;
- ordinary text and composition can be allowed to change the controlled
  `contenteditable`;
- `MutationObserver` plus the DOM selection lets the view translate that
  temporary DOM change into a state transaction and redraw a canonical view;
- `beforeinput` is used selectively rather than as the editor's universal
  transaction API.

This is the primary architectural precedent for a small model-first Radiant
editor: immutable state, explicit transactions, one dispatch point, extension
state, and an incremental view. It is not precedent for representing rich
documents as flat text.

References:

- [CodeMirror system guide](https://codemirror.net/docs/guide/)
- [CodeMirror input handling](https://github.com/codemirror/view/blob/6.43.6/src/input.ts)
- [CodeMirror DOM observer](https://github.com/codemirror/view/blob/6.43.6/src/domobserver.ts)

### 4.2 ProseMirror: model-first schema tree

ProseMirror keeps an immutable schema-constrained document tree. Transactions
contain transform steps, selection updates, and metadata; plugins add state
and event behavior. The view renders the tree and uses a DOM observer to parse
allowed native editable-DOM changes back into document transactions.

Its browser-facing input pattern is similar to CodeMirror—standard DOM
events, editable DOM, Selection/Range, mutation observation, clipboard, and
geometry—but its internal correctness contract is much broader: arbitrary
nested schemas, node and mark validity, slices, step mapping, structural
selection, history, and collaboration-oriented transforms.

Radiant does not need those internal abstractions to host ProseMirror. A
ProseMirror failure is relevant when it demonstrates a missing standard DOM
capability within the selected supported configuration. Radiant must not grow
a ProseMirror-like tree or transform engine to make the library pass.

ProseMirror does not use browser `document.execCommand` for normal editing.
Its exported "commands" are functions that construct and dispatch ProseMirror
transactions. Marks, block transforms, history, ordinary selection changes,
paste, drop, and structural editing therefore remain editor-owned even after
Phase 9 adds the full UA command surface. Keyboard clipboard events use
`ClipboardEvent.clipboardData`/`DataTransfer`; older broken-clipboard
fallbacks use temporary DOM selection.

There is one conditional core exception in `prosemirror-view`: a Safari
selection workaround for an editor mounted inside a ShadowRoot falls back to
`execCommand("indent")` to trigger `beforeinput` when
`Selection.getComposedRanges()` is unavailable. It is not reached for an
ordinary light-DOM editor. Phase 9 admits this as part of the command corpus;
the same package command registry must handle it without a ProseMirror- or
ShadowRoot-specific engine branch.

References:

- [ProseMirror guide](https://prosemirror.net/docs/guide/)
- [ProseMirror input handling](https://github.com/ProseMirror/prosemirror-view/blob/master/src/input.ts)
- [ProseMirror DOM observer](https://github.com/ProseMirror/prosemirror-view/blob/master/src/domobserver.ts)
- [Current ProseMirror view repository](https://code.haverbeke.berlin/prosemirror/prosemirror-view)

### 4.3 Editor.js: DOM-first Tool blocks

Editor.js is block-oriented and more DOM-first. Each Tool renders one element,
often a `contenteditable`, owns its UI, and later extracts block data with
`save(blockContent)`. The core watches Tool DOM changes, dispatches lifecycle
notifications, and handles block-level Enter, boundary Backspace/Delete,
paste, navigation, and merging. Ordinary inline text editing is largely
delegated to native `contenteditable`.

This model is useful because it tests a different boundary:

- live editable DOM is meaningful state for the duration of editing;
- `MutationObserver` must report accurate subtree changes;
- selection must survive block insertion, removal, merge, and focus changes;
- paste events must expose their plain and HTML payloads; ordinary paragraph
  paste falls through to the Phase-9 UA package unless a Tool claims it;
- Tool code depends on ordinary DOM construction and serialization.

The stock bold, italic, and link inline Tools currently use legacy
`document.execCommand`/`queryCommandState`. That dependency is isolated from
the block editor core. Phase 9 supports the stock Tools through the public
legacy command/query surface, making them direct conformance probes for the
package-owned UA engine.

References:

- [Editor.js Tools API](https://editorjs.io/tools-api/)
- [Editor.js saved-data format](https://editorjs.io/saving-data/)
- [Editor.js block event handling](https://github.com/codex-team/editor.js/blob/next/src/components/modules/blockEvents.ts)
- [Editor.js mutation observer](https://github.com/codex-team/editor.js/blob/next/src/components/modules/modificationsObserver.ts)
- [Editor.js paste handling](https://github.com/codex-team/editor.js/blob/next/src/components/modules/paste.ts)
- [Editor.js stock bold Tool](https://github.com/codex-team/editor.js/blob/next/src/components/inline-tools/inline-tool-bold.ts)

### 4.4 Prior-art conclusions

The editors disagree about document shape, transaction representation, and
whether live DOM is temporary or durable. Radiant therefore standardizes none
of those choices. It standardizes only the shared browser-facing envelope:

```text
DOM keyboard / clipboard / composition event
    -> cancelable beforeinput notification where applicable
    -> explicit selected action
         -> editor-owned JS transaction, or
         -> Lambda DOM UA default action, or
         -> Radiant source-model transaction
    -> non-cancelable input notification after a claimed/change result
    -> Selection + MutationObserver + layout checkpoints
```

The three probes have distinct value:

1. CodeMirror proves that a model-owned text editor can reconcile controlled
   native input.
2. Editor.js proves that a block/Tool editor can rely on live editable DOM.
3. ProseMirror proves structured model/DOM reconciliation within the pinned
   supported configuration without becoming the Radiant design template.

---

## 5. Current Behavior

### 5.1 Existing common pieces

The current implementation already has most of the common substrate:

| Capability | Current location |
|---|---|
| Editing surface classification | `radiant/editing.cpp` |
| JS/Radiant context ownership | document Lambda runtime/template registry and render-map reverse lookup in `radiant/event.cpp` |
| Input intent taxonomy and key mapping | `radiant/editing_intent.cpp` |
| Transaction state machine | `radiant/editing_dispatch.cpp` |
| JS `InputEvent` construction and dispatch | `radiant/event.cpp`, `lambda/js/js_dom_events.cpp` |
| Lambda template event dispatch/reconciliation | `radiant/event.cpp` |
| DOM Range, Selection, target ranges | `radiant/dom_range*.cpp`, `radiant/editing_target_range.cpp` |
| MutationObserver delivery | `lambda/js/js_dom_observers.cpp` |
| Clipboard/DataTransfer | `radiant/clipboard.cpp`, `lambda/js/js_clipboard.cpp` |
| Composition events and intents | `radiant/event.cpp`, platform IME adapters |
| Event simulation and replay | `radiant/event_sim.cpp` |

`EditingDispatchHooks` currently contains one JS input-event callback, one
Lambda event callback, and one clipboard callback. `dispatch_editing_hooks()`
installs all three as a fixed set for every transaction.

### 5.2 Current rich-key transaction

For a focused `contenteditable` or legacy `data-editable` surface, a mapped key
such as Backspace, Delete, Enter, paste, or history is converted to an
`EditingIntent` and sent to `dispatch_rich_consumer_transaction`.

`data-editable` is current implementation behavior, not part of this proposal.
The redesigned surface uses `contenteditable` for both JavaScript and Radiant
template editors and removes the custom editable marker.

The transaction:

1. resolves the editing surface;
2. snapshots target ranges and selection;
3. dispatches `beforeinput`;
4. invokes the JavaScript DOM event path;
5. invokes the Lambda template event path;
6. dispatches `input` even when there was no DOM mutation;
7. commits the editing transaction and logging state.

The consumer transaction has no mutation callback. For mapped rich-edit keys,
a successful transaction currently stops the later ordinary JavaScript
`keydown` path.

Printable text is a narrow exception. `RDT_EVENT_TEXT_INPUT` uses
`dispatch_rich_transaction_defaultable`, whose current native mutation can
replace a selection and insert text into a DOM text position. It publishes
MutationObserver records and moves the DOM selection. It does not provide the
complete structural behavior required for deletion, paragraph insertion, and
all composition cases.

### 5.3 What the current `beforeinput` handler does

`editing_dispatch_beforeinput_ex` is nominally a dispatcher, but currently
spans notification, gate, and action responsibilities. Today it:

1. rejects invalid or mixed-surface target ranges;
2. prepares clipboard state for `deleteByCut`;
3. constructs and dispatches a cancelable JavaScript `InputEvent` named
   `beforeinput`, including `inputType`, `data`, `dataTransfer`,
   `isComposing`, and a pre-mutation `StaticRange[]`;
4. records whether JavaScript called `preventDefault()`;
5. looks up and invokes a Lambda template `beforeinput` handler by reverse
   mapping the target DOM node to its source template;
6. records whether the Lambda handler was found and ran;
7. optionally dispatches the non-cancelable `input` event for legacy
   event-only callers;
8. returns whether the rich editing dispatch was valid.

When called by `editing_run_transaction`, native mutation is allowed only when
JavaScript did not prevent the event, no Lambda handler claimed it, and the
transaction supplied a `mutate` callback.

The important limitation is that the current return value means "the
transaction was dispatched", not "an edit occurred". The consumer path then
treats this as handled and may stop later keyboard dispatch even when no
handler changed the document.

### 5.4 Required decomposition

`editing_dispatch_beforeinput_ex` currently combines notification, payload
preparation, action routing, template execution, reconciliation, optional
post-notification, and outcome reporting. The redesign splits those concerns:

| Stage | Responsibility |
|---|---|
| Gate preparation | validate surface/ranges, snapshot selection, prepare clipboard/DataTransfer payload, select the action handler, record the transaction |
| `editing_notify_beforeinput` | construct and dispatch the cancelable pre-action notification; report only dispatch/cancellation facts |
| `editing_dispatch_registered_handler` | invoke exactly one selected JavaScript, Radiant-template, or DOM-compatibility action handler |
| `editing_notify_input` | construct and dispatch the non-cancelable post-action notification after an applied or explicitly claimed edit |
| Gate commit | selection/layout checkpoints, outcome validation, audit/record commit |

The current Lambda template work performed from the `beforeinput` callback
moves into the Radiant template action handler. Clipboard side effects move to
gate preparation or the selected action handler. The legacy option that makes
`beforeinput` dispatch `input` is removed.

---

## 6. Required Invariants

The enhanced design must protect these invariants:

1. **One gate:** every platform or replay edit intent enters
   `editing_run_transaction`.
2. **Pure notifications:** notification functions do not select handlers,
   apply edits, reconcile templates, or invoke each other.
3. **One `beforeinput`:** an eligible transaction emits at most one
   cancelable pre-action notification.
4. **Cancellation stops routing:** when `beforeinput` is canceled, the gate
   invokes no action handler and emits no `input`.
5. **One action owner:** at most one handler may apply or own the edit.
6. **No false handling:** merely dispatching `beforeinput` does not mean that
   the key or edit was handled.
7. **No implicit contenteditable mutation:** core dispatch does not apply a
   browser default; any DOM edit is owned by a registered action handler.
8. **Selection is canonical:** mutation and reconciliation must update the
   canonical DOM or source-model selection before commit.
9. **Complete observation:** DOM mutations made by the Lambda DOM UA handler
   publish the same mutation records as equivalent DOM API calls.
10. **Stable replay:** handler selection and transaction outcome are logged so
   playback does not silently select a different editing policy.
11. **Re-entrancy safety:** handler registration changes made during an event
   affect the next transaction, not the current handler snapshot.
12. **One post-notification:** non-cancelable `input` is emitted at most once,
    and only after an edit was applied or explicitly claimed.

---

## 7. Editing Context and Routing

### 7.1 One editable-host marker

Every rich editing host uses the standard `contenteditable` attribute,
regardless of whether its action is owned by JavaScript or a Radiant template.
The redesign introduces no `data-editable`, `data-radiant-editing`, or other
public routing attribute.

`EditingHost::from_node()` remains the single resolver for the editable host
and `contenteditable="false"` islands. Handler ownership is a separate
question resolved from existing document/template context.

Illustrative internal route:

```cpp
enum EditingRouteKind {
    EDIT_ROUTE_NONE = 0,
    EDIT_ROUTE_DOM_SCRIPT,
    EDIT_ROUTE_RADIANT_TEMPLATE
};
```

This enum is internal transaction metadata, not a DOM API or attribute value.
The redesigned classifier uses `EDIT_SURFACE_CONTENTEDITABLE` for both routes;
the legacy `EDIT_SURFACE_LAMBDA_TEMPLATE` distinction moves into route/action
metadata rather than remaining a separate editable-host kind.

### 7.2 Existing-context routing

Radiant already has enough ownership information to distinguish the two
contexts:

- plain HTML/JavaScript documents have no Lambda template runtime/registry
  ownership for the target;
- Lambda/Radiant output has document runtime state plus render-map reverse
  lookup from result DOM to source item and template;
- JavaScript-created editor DOM has no template result ownership even when it
  is mounted in a Lambda-generated application.

The gate resolves a `contenteditable` host as follows:

1. resolve the standard editable host and retain it;
2. use the existing runtime/template registry and render-map reverse lookup to
   determine whether the target belongs to a live Radiant template editing
   context;
3. select the Radiant template route when that ownership exists;
4. otherwise select the DOM/JavaScript compatibility route;
5. select the matching registered action handler within that route.

No URL suffix, script-listener inference, custom attribute, or new document
provenance field participates in routing. A mixed Lambda application can host
CodeMirror, ProseMirror, or Editor.js in JavaScript-created DOM while other
`contenteditable` template results remain model-owned.

### 7.3 Route snapshot

The gate resolves the route once, before `keydown`/`beforeinput` dispatch, and
stores it in the transaction log:

```text
contenteditable host
    + existing document/template ownership context
    + render-map reverse lookup
    + registered handler match
    -> transaction handler snapshot
```

Reconciliation may replace the target subtree during the transaction. The
snapshot therefore retains the stable editing host and handler IDs, not a
borrowed leaf-only decision.

---

## 8. Action Handler Registry

### 8.1 Registry ownership

The registry is owned by `DomDocument` or a document resource and is destroyed
with the document. Implementation must use project containers such as
`ArrayList`; it must not introduce `std::vector` or another `std::` container.

Built-in handlers are registered when the document is initialized. Optional
native extensions and page JavaScript may register additional action handlers
through internal and host APIs respectively. Notification listeners registered
with `addEventListener("beforeinput", ...)` or `addEventListener("input", ...)`
remain ordinary DOM listeners; they are not entries in this registry.

### 8.2 Handler contract

Illustrative API:

```cpp
enum EditingHandleResult {
    EDIT_HANDLE_PASS,
    EDIT_HANDLE_CLAIMED,
    EDIT_HANDLE_MUTATED_DOM,
    EDIT_HANDLE_RECONCILED_MODEL,
    EDIT_HANDLE_ERROR
};

struct EditingActionHandler {
    const char* id;
    int priority;
    uint32_t route_mask;
    uint32_t surface_mask;

    bool (*matches)(const EditingHandlerContext* context, void* user);
    EditingHandleResult (*handle)(EditingHandlerContext* context, void* user);
    void* user;
};
```

This is a shape proposal, not a requirement to copy the exact declarations.
The important contract is:

- matching handlers are snapshotted and ordered deterministically;
- the highest-priority action handler whose route, surface, intent, and
  `matches` callback agree is selected during gate preparation;
- equal-priority action matches are a configuration error rather than an
  order-dependent choice;
- the selected handler is not invoked until the common `beforeinput`
  notification completes without cancellation;
- a selected handler returning `PASS` leaves the transaction unhandled; the
  gate does not fall through to another action handler;
- a second mutation claim is an invariant violation;
- `ERROR` aborts the transaction and is logged;
- asynchronous ownership is not permitted in the first implementation.

The notification APIs have separate, deliberately small results:

```cpp
struct EditingNotificationResult {
    bool dispatched;
    bool cancelable;
    bool prevented;
};

EditingNotificationResult editing_notify_beforeinput(
    EditingHandlerContext* context);
bool editing_notify_input(EditingHandlerContext* context);
```

`editing_notify_beforeinput` receives an already prepared immutable intent,
target-range snapshot, composition state, and clipboard payload. It may invoke
ordinary event listeners, which are page code and may independently mutate
the document, but the notification routine itself performs no editing and
does not treat listener existence as an action claim. The gate detects a
synchronous listener mutation through the mutation epoch, records it, and
must not run the registered action handler on stale ranges; the policy for
that exceptional case is an open question (§16).

Both notification functions target the retained editing host and use the
standard DOM propagation path. The post-notification carries the same
`inputType`, data/DataTransfer, and composition identity as the transaction,
but is non-cancelable and observes the post-action selection/DOM state.
MutationObserver callbacks remain deferred to the normal microtask checkpoint.

The JavaScript host registration API may take this conceptual shape:

```js
const registration = Radiant.editing.registerInputHandler(host, {
  intents: ["insertText", "deleteContentBackward"],
  handle(intent) {
    // Apply a synchronous model or DOM transaction and return its outcome.
    return {handled: true, modelChanged: true}
  }
})

registration.unregister()
```

The final API needs a non-forgeable transaction-scoped result and lifecycle
rules. It is an integration option for custom applications, not a requirement
placed on CodeMirror or ProseMirror.

### 8.3 Built-in registrations

#### Lambda DOM UA handler

- Matches `EDIT_SURFACE_CONTENTEDITABLE` under the DOM/JavaScript route.
- Invokes the shipped Lambda DOM package default action when the preceding
  keyboard/clipboard event and `beforeinput` were not canceled. Phase 1 used
  the limited action in §10; Phase 9 uses the operation model in §20.
- Applies policy only in the package, then updates DOM Selection, invalidates
  rendering, and publishes precise mutation records through generic native
  DOM mechanisms.
- Does not dispatch `beforeinput` or `input`; those notifications belong to the
  common gate.

#### Radiant template handler

- Matches the standard contenteditable surface under the Radiant template
  route.
- Invokes the registered Lambda/Radiant edit action using the prepared intent.
- Treats successful source-model update and DOM reconciliation as
  `EDIT_HANDLE_RECONCILED_MODEL`.
- Reports a real model change or an explicit claimed no-op to the gate.
- Preserves source-position mapping and pending source selection restoration.
- Does not dispatch `beforeinput` or `input`.

#### JavaScript registered action handler

- Is installed explicitly by page/application code for a host.
- Receives a normalized, immutable intent after `beforeinput`.
- Applies its synchronous JS-owned model or DOM action and reports a structured
  outcome.
- Does not replace normal DOM event listeners and is not automatically
  installed merely because listeners exist.

Audit, logging, recording, and playback are gate observers, not action
handlers. The existing event-state logger can implement the first gate
observer. Playback injects at the intent gate and verifies the recorded route
and action owner.

---

## 9. Transaction Flow

### 9.1 Common flow

```text
platform / automation / replay input
    |
    v
normalize EditingIntent + assign transaction/cascade ID
    |
    v
resolve EditingSurface + handler route + handler snapshot
    |
    v
dispatch pre-edit keyboard/clipboard/composition DOM event where applicable
    |
    | canceled or handled by script
    +-------------------------------> commit observation-only result
    |
    v
prepare target ranges, payload, selection snapshot, and audit record
    |
    v
consumer transaction: notify beforeinput once
    |
    | preventDefault
    +-------------------------------> skip action, commit canceled result
    |
    v
invoke exactly one registered action handler
    |
    +--> Lambda DOM UA package action
    |      + apply plan through generic DOM primitives
    |      + queue MutationObserver records
    |      + update DOM Selection and UA history
    |      + invalidate layout/paint
    |
    +--> source-model transaction
           + reconcile DOM
           + restore projected selection
    |
    +--> JavaScript registered action
           + synchronously update its model/DOM
           + report a structured result
    |
    v
notify input once only for an applied/claimed edit
    |
    v
queue selectionchange + run microtask/layout checkpoints
    |
    v
validate invariants + commit audit record
```

The gate remains responsible for the transaction envelope and both
notifications. Handlers implement only action policy. In particular, there is
no hidden core "browser default mutation" between the two notifications.

### 9.2 Outcome model

`editing_run_transaction` should return a structured outcome rather than one
ambiguous Boolean:

```cpp
struct EditingTransactionResult {
    bool dispatched;
    bool beforeinput_dispatched;
    bool beforeinput_prevented;
    bool key_event_prevented;
    bool handler_invoked;
    bool claimed;
    bool dom_mutated;
    bool model_reconciled;
    bool input_dispatched;
    bool selection_changed;
    const char* action_handler_id;
};
```

Callers break out of later event handling only when:

- a pre-edit DOM event was canceled;
- `beforeinput` was canceled;
- an action handler claimed the intent;
- a DOM or model mutation occurred; or
- policy explicitly consumes the key, such as focus traversal.

`dispatched == true` by itself is not sufficient.

---

## 10. Phase-1 DOM Compatibility Mutation Handler

> This section records the limited first-gate handler. Its transaction
> ordering and DOM invariants remain; its intentionally narrow operation set
> is superseded by the full Lambda-package UA engine in §20 (D7.2.5).

The web-platform baseline is
[Input Events Level 2](https://www.w3.org/TR/input-events-2/): a user agent
notifies `beforeinput`, updates an editable DOM if the event was not canceled,
and then notifies `input`. Radiant preserves that observable sequence but
replaces the user agent's implicit update with an explicit registered action.

### 10.1 Keyboard ordering

For DOM-compatible contenteditable, mapped edit keys must no longer bypass
ordinary JavaScript keyboard dispatch.

Required ordering:

```text
keydown
  -> if not prevented, beforeinput
  -> if not prevented, registered DOM compatibility mutation
  -> input
  -> queued selectionchange / observer checkpoint
keyup
```

CodeMirror and ProseMirror install `keydown` handlers for keymaps, history,
structural commands, selection motion, and platform workarounds. Editor.js
installs per-block `keydown` handlers for block creation, merge, removal, and
navigation. If any such handler calls `preventDefault()`, Radiant must not
invoke the registered DOM compatibility mutator for that key.

Printable input may arrive as separate platform key and text-input events. The
gate must correlate them by cascade ID so it does not dispatch duplicate
edits.

### 10.2 Minimum mutation coverage for the pinned editors

The DOM handler should extract and reuse existing DOM mutation helpers before
adding new ones. No alternate editor-only DOM implementation should be
created.

CodeMirror and ProseMirror are model-driven editors, while Editor.js explicitly
handles most block structure in keydown handlers. Their keymaps, block
handlers, and clipboard/drop handlers consume many structural operations
themselves. The initial DOM mutation handler therefore implements only the
fallback operations that reach native contenteditable in the pinned desktop
configurations:

| Intent family | Required handler behavior |
|---|---|
| Text | `insertText`, selection replacement, and `insertReplacementText` when an editor/platform trace reaches it |
| Delete | selected range and character backward/forward inside a text run when the editor did not cancel `keydown` |
| Composition | provisional `insertCompositionText`, replacement, commit, and cancel |
| Line/paragraph | `insertLineBreak` and `insertParagraph` are deferred; return `PASS` with no `input` |

Every action must:

- honor `contenteditable="false"` islands;
- constrain mutations to one editing host;
- apply `plaintext-only` filtering;
- preserve live Range invariants;
- update the DOM selection;
- publish complete mutation records;
- invalidate style/layout/paint at the correct granularity;
- produce target ranges that describe the same content the action changes.

Structural behavior should be measured against a pinned Chromium baseline.
For the first editor gate:

- CodeMirror keymaps own Enter, many deletion commands, and history.
- ProseMirror `baseKeymap`/history own Enter, block joins/splits, boundary
  deletion, and history.
- Editor.js core owns normal block Enter and boundary merge/removal. The pinned
  configuration excludes Tools that delegate Enter through
  `enableLineBreaks`; a Tool must handle its own multiline behavior.
- the three editors' clipboard/drop handlers normally update their model or
  Tool DOM and call `preventDefault()`.
- an operation that reaches the DOM handler outside the supported table
  returns `PASS`, logs the unsupported intent, and emits no `input`.

The end-to-end suite must prove that no required pinned-editor path reaches an
unsupported mutation. This is intentionally narrower than a general browser
rich-text editor. Broader contenteditable actions can be added later from
traces and conformance tests, without changing the notification/handler
architecture.

### 10.3 MutationObserver and event-loop timing

CodeMirror and ProseMirror reconcile native editable DOM changes through
`MutationObserver`; Editor.js uses mutation observation to identify changed
Tool blocks. The DOM handler must use the ordinary
`js_dom_notify_mutation*` path; direct private mutation without observer
records is invalid.

For an action owned by this handler, the synchronous sequence is:

```text
beforeinput notification -> registered mutation and selection update
                         -> input notification
```

Mutation records are delivered at the host's normal microtask checkpoint.
Tests must verify record type, target, old value, subtree behavior, batching,
and delivery ordering rather than relying only on final text.

For a text-node edit it must publish `characterData` with the old value. When
the operation inserts or removes a text node, it must publish `childList`.
The observer configurations reached by the three editors include subtree
child-list, character-data, old-value, and attribute cases, so ordinary DOM API
mutations must remain visible through those paths.

The handler must not rebuild the entire editing host for a simple text edit.
Third-party editors retain node references and use mutation records to locate
the changed region.

### 10.4 Selection and geometry

The existing Range/Selection implementation is retained. Compatibility tests
must cover the APIs used by the editor probes:

- `document.getSelection()` and `window.getSelection()`;
- `Range.getClientRects()` / `getBoundingClientRect()`;
- `Selection.extend()` / `setBaseAndExtent()`;
- `document.elementFromPoint()`;
- caret hit-testing;
- `scrollIntoView`, element scrolling, client/scroll geometry;
- selection preservation after synchronous DOM reconciliation;
- stable DOM wrapper identity.

Synchronous geometry reads after a mutation must trigger or observe the
correct layout checkpoint. Stale rectangles are a correctness issue for
editor cursor motion and scrolling, not merely a rendering delay.

### 10.5 Clipboard and drag/drop

The browser event must be dispatched before the input transaction:

```text
paste ClipboardEvent
  -> beforeinput insertFromPaste
  -> selected registered action, if any
  -> input
```

A canceled `paste` event prevents the later transaction. A canceled
`beforeinput` prevents handler dispatch. `ClipboardEvent.clipboardData` and
`InputEvent.dataTransfer` must refer to consistent payloads.

In the pinned CodeMirror and ProseMirror configurations, copy/cut/paste and
drop listeners generally apply library model transactions and prevent the
browser action. Editor.js core delegates ordinary paragraph text paste to the
browser-native structural default; that default is deliberately unavailable in
this gate, so the selected Editor.js Tool configuration documents it as an
exclusion instead of claiming a partial fallback. A future
simple-contenteditable clipboard mutator may be registered separately; it is
not required for the initial editor gate.

### 10.6 Composition and IME

The DOM handler must expose a browser-compatible composition stream and actual
provisional DOM state. Event-only composition is insufficient because
model-first editors reconcile composing DOM and DOM-first Tools must observe
the text the user is composing.

Required cases:

- composition start, repeated update, commit, and cancel;
- replacement of a selected range;
- caret movement inside preedit text where the platform supports it;
- focus change or host removal while composing;
- exactly one committed editor-model change;
- correct `isComposing`, `data`, target ranges, and mutation records.

Platform event ordering varies in edge cases. The accepted sequence must be
captured from the supported platform/browser baseline and represented in the
headless simulation format.

---

## 11. Radiant Template Handler

The Radiant handler keeps the current model-first behavior:

```text
common gate maps selection/target ranges and notifies beforeinput
  -> if not canceled, invoke registered Radiant template action
       -> mutate source model
       -> retransform/reconcile changed template output
       -> project source selection back to DOM
  -> common gate notifies input after a claimed/change result
  -> commit
```

The enhancement is contractual:

- it is registered explicitly rather than hard-wired beside the DOM
  dispatcher;
- the current template action is removed from
  `editing_dispatch_beforeinput_ex` and runs only in the action stage;
- it reports whether it actually claimed or changed the model;
- a missing Lambda handler returns `PASS`, not handled;
- a no-op handler reports whether the no-op is deliberately claimed;
- a Lambda/Radiant pre-action notification, if retained, is cancel-only and
  distinct from the template action registration;
- DOM event delivery can remain enabled as a notification without making the
  DOM compatibility mutator the action owner;
- template reconciliation and DOM compatibility mutation cannot both run
  for one transaction.

This preserves the present model-first editor architecture while removing
mutation and reconciliation from the `beforeinput` notification seam.

The template handler is a consumer of the same compatibility substrate:

- it receives the same normalized keyboard, clipboard, composition, and input
  intent data;
- standard DOM listeners see the same `beforeinput`/`input` objects and event
  ordering as they do on a JavaScript-owned host;
- it represents selections through the same DOM Selection/Range and
  `StaticRange` contracts;
- its reconciled DOM changes use the shared mutation APIs and observer
  notification path;
- its selection projection uses the shared caret geometry and scrolling
  capabilities.

Source mapping, template re-evaluation, and source-model history remain private
Radiant behavior, not new DOM APIs. If a template feature cannot be expressed
with the frozen minimum surface, the template action or reconciliation design
must adapt first. The DOM surface expands only under the evidence rule in
§13.5.

When the template editor itself needs new state or extension machinery, it
should prefer the CodeMirror precedent—model-authoritative state, explicit
transactions, one dispatch point, mapped selections, and incremental DOM
projection. That is an internal editor architecture choice and must remain
separate from the public editable DOM contract.

---

## 12. Logging, Audit, Recording, and Playback

The common gate records enough information to reproduce and diagnose an edit:

```json
{
  "type": "editing.transaction",
  "transaction_id": 42,
  "source": "platform|automation|replay",
  "surface": "contenteditable|text_control",
  "route": "dom_script|radiant_template",
  "intent": "deleteContentBackward",
  "action_handlers": ["editing.dom"],
  "gate_observers": ["editing.audit"],
  "action_owner": "editing.dom",
  "keydown_prevented": false,
  "beforeinput_prevented": false,
  "claimed": true,
  "dom_mutated": true,
  "model_reconciled": false,
  "input_dispatched": true,
  "selection_before": {},
  "selection_after": {},
  "target_ranges": [],
  "mutation_summary": {}
}
```

Recording should store semantic intent and platform key data. Playback should
inject through the same public gate, not call a handler's mutation callback
directly.

Playback modes:

1. **Behavioral replay:** resolve handlers from the current document and
   compare the outcome with the recorded route/action owner.
2. **Strict replay:** require the same handler IDs and reject a mismatch.
3. **Audit-only replay:** dispatch no mutation, but validate routing and event
   construction.

This design keeps audit and replay independent of whether the page uses a
Lambda editor, CodeMirror, ProseMirror, or a simple contenteditable element.

---

## 13. Phase-1 Minimum Editable DOM Compatibility Surface

> This section records the first-gate compatibility boundary. Phase 9 removes
> its command, structural-default, history, paste, and ShadowRoot exclusions;
> the current target and gates are §20.5–§20.10 (D7.2.5).

### 13.1 Boundary: a DOM capability set, not an editor API

The minimum viable editable DOM surface is the delta on top of Radiant's
ordinary DOM implementation that real editors require. It is not a new
`RadiantEditor` JavaScript abstraction and it contains no editor document
model.

None of the three editors registers one universal input callback with the
browser. They compose ordinary `addEventListener` handlers, native editable
behavior, Selection/Range, DOM mutation, and observers:

- CodeMirror installs listeners on `contentDOM`, handles commands through
  keymaps, and observes allowed native DOM edits.
- ProseMirror installs listeners on `view.dom`, lets commands handle structural
  operations, and observes/parses allowed native DOM edits.
- Editor.js installs per-block keyboard handlers, lets the browser edit Tool
  content, and observes the Tool subtree for changes.

Consequently, library-specific host adapters are neither necessary nor useful
as the compatibility proof. The target contract is:

```text
ordinary DOM events with cancellation
    + explicit DOM-compatible text/composition action when not canceled
    + live Selection/Range
    + precise MutationObserver records
    + clipboard/DataTransfer
    + synchronous geometry and normal event-loop checkpoints
```

### 13.2 Required capability matrix

The following table is the proposed minimum. "Required" means that the API and
its observable behavior are part of the first compatibility gate. It does not
mean that every browser edge case for that API is in scope.

| Capability group | Minimum observable contract | Principal probes | Disposition |
|---|---|---|---|
| Editable host and focus | `contenteditable`, `isContentEditable`, nested `contenteditable=false`, focus/blur, `activeElement`, tab focus, stable connected node identity | all three | required |
| Event dispatch | capture/target/bubble ordering, listener options, `preventDefault`, `defaultPrevented`, keyboard modifier/key data, focus and selection events | all three | required |
| Input notifications | cancelable `beforeinput`; `inputType`, `data`, `dataTransfer`, `isComposing`, `getTargetRanges()`; non-cancelable post-action `input` | all three plus Radiant template | required |
| DOM-compatible action | text insertion/replacement, selected-range deletion, simple backward/forward deletion, provisional/committed composition, Selection update | all three | required |
| DOM construction and mutation | stable Element/Text/DocumentFragment wrappers; insert/remove/replace; text/attribute mutation; cloning; `textContent`, `innerHTML`, containment and traversal | all three, especially Editor.js | ordinary DOM prerequisite; probe before adding |
| Mutation observation | subtree `childList`, `characterData`, and `attributes`; old values; batching; `takeRecords()`/`disconnect()`; microtask delivery after synchronous mutation | all three | required |
| Selection and Range | `getSelection`, live anchor/focus, directional selection, `createRange`, boundary mutation rules, collapse/extend/setBaseAndExtent, range insertion/deletion, `selectionchange`, `StaticRange` | all three | required |
| Geometry and hit testing | Range/element rectangles, caret hit testing, `elementFromPoint`, scrolling metrics, `scrollIntoView`, synchronous layout freshness | CodeMirror and ProseMirror; Editor.js toolbar | required where reached |
| Clipboard and drag/drop | copy/cut/paste/drop ordering, `ClipboardEvent.clipboardData`, `DataTransfer`, plain/HTML payloads, cancellation, consistent `InputEvent.dataTransfer` | all three | required |
| Composition/IME | composition start/update/end, provisional DOM and selection, `isComposing`, commit/cancel, host removal/focus-change safety | CodeMirror and ProseMirror; editable Editor.js Tools | required |
| DOM parsing/serialization | `DOMParser`, detached documents/fragments, HTML parsing and serialization sufficient for clipboard and Tool save/load | ProseMirror and Editor.js | ordinary DOM prerequisite; required when traced |
| Scheduling/lifecycle | microtasks, observer checkpoints, timers/animation frames used by view measurement, teardown without retained callbacks | all three | required |

The editable-specific implementation work is therefore concentrated in event
ordering, the explicit DOM-compatible action, Selection mutation, composition,
and their observer/layout effects. General DOM calls in this matrix should
reuse and harden the existing LambdaJS DOM implementation rather than create
editing-only variants.

#### Current Radiant delta

The proposal does not presume that every item in the matrix is missing.
Radiant already exposes most of the general DOM, event, Selection/Range,
clipboard/DataTransfer, and MutationObserver surface. The known first-gate
delta is:

| Work item | Nature of change |
|---|---|
| Split `editing_dispatch_beforeinput_ex` | refactor notification, routing, action, reconciliation, and post-notification into the explicit stages in §5.4 |
| Add deterministic handler registry/context routing | new internal editing capability; the public DOM event API is unchanged |
| Restore normal `keydown` delivery before rich consumer dispatch | event-ordering correction needed by editor keymaps and Editor.js block handlers |
| Complete the DOM-compatible action | extend current insertion/replacement with simple deletion and real provisional/committed composition |
| Correlate platform key and text cascades | prevent duplicate edits and notifications |
| Harden mutation/selection checkpoints | conformance work so native and script DOM mutations produce precise records, live ranges, `selectionchange`, and fresh geometry |
| Add general-DOM gaps found by editor probes | only standard DOM parsing/serialization, lifecycle, or geometry semantics demonstrated missing by a pinned trace |

`execCommand`, a ProseMirror-like model, general rich structural defaults,
host-native rich history, and `EditContext` are not items in this delta.

### 13.3 Minimum DOM-compatible action set

The registered DOM compatibility handler supplies only operations that a
pinned editor actually leaves to native `contenteditable`:

| Intent | First-gate behavior |
|---|---|
| `insertText`, `insertReplacementText` | replace the target selection/range with text and collapse Selection |
| `deleteContentBackward`, `deleteContentForward` | delete a selected range or one valid text unit within the host |
| composition insert/update/commit/cancel | maintain provisional DOM, target range, composition state, and final Selection |
| `insertLineBreak`, `insertParagraph` | deferred; editor scripts/keymaps must own Enter and multiline structure |
| cut/paste/drop default mutation | add only for an uncanceled required trace; editor-owned handlers normally consume these events |
| history and rich formatting | never supplied by the first DOM handler; editor/model commands own them |

Each supported action must produce the same externally visible bundle:

1. mutate through shared DOM primitives;
2. maintain live Range and Selection invariants;
3. queue precise mutation records;
4. invalidate layout/paint;
5. expose the updated state to the post-action `input` listener;
6. deliver observers at the normal microtask checkpoint.

An unsupported intent returns `PASS`, emits no synthetic `input`, and is
recorded as an unsupported fall-through. The test suite treats an unexpected
fall-through from a required editor path as a missing-capability failure.

### 13.4 Explicit exclusions

The following are not part of the first minimum:

- `EditContext`; Radiant must omit it so CodeMirror selects its established
  `contenteditable` path rather than detecting a partial implementation.
- any `document.execCommand` or `queryCommand*` semantics;
- general browser rich-paste cleanup and arbitrary structural
  `contenteditable` mutation;
- browser-owned rich-text history;
- `insertLineBreak` and `insertParagraph` DOM-compatible default actions;
- accessibility and input-hint work, including editor-specific ARIA behavior,
  spellcheck, autocapitalization, `inputmode`, grammar services, and virtual
  keyboard integration;
- mobile-only browser quirks unless selected as a later platform target.

These deferred capabilities may already have generic DOM attribute reflection;
they are not acceptance requirements and receive no editable-specific behavior
in this milestone.

The no-`execCommand` rule is unconditional. The equivalent edit must be
performed by JavaScript or Lambda using its own model transaction or standard
Range/DOM mutation, followed by the ordinary observable selection and mutation
effects. `document.execCommand` and `queryCommand*` are absent from the
LambdaJS document binding; feature detection must not mistake inert stubs for
a supported command surface.

Editor.js's stock inline formatting Tools are a known legacy-command
dependency and are therefore unsupported. The first gate uses replacement
Tools implemented through Editor.js's public Tool API and standard Range/DOM
operations; this is fixture configuration, not a fork of Editor.js.

ProseMirror normal light-DOM editing requires no exception: its editing
commands dispatch ProseMirror transactions and its normal clipboard path uses
`ClipboardEvent.clipboardData`/`DataTransfer`. Its Safari ShadowRoot selection
fallback calls `execCommand("indent")`; that configuration is unsupported.
Applications or third-party plugins that independently call `execCommand`
remain unsupported.

ProseMirror compatibility without legacy commands is expected to be:

| ProseMirror capability | `execCommand` dependency | Actual Radiant dependency |
|---|---|---|
| construct/render schema documents and NodeViews | none | ordinary DOM construction, attributes, stable node identity |
| typing, deletion, Enter, joins/splits | none | keyboard events plus native-like editable DOM changes and observer reconciliation where commands do not consume the event |
| marks, headings, lists, commands, input rules | none | ProseMirror transactions, keydown/keypress delivery, DOM redraw |
| history and collaboration | none | ProseMirror state/transactions and normal scheduling |
| selection, node selection, cursor movement in light DOM | none | live Selection/Range, DOM-position mapping, geometry, hit testing |
| Safari selection inside ShadowRoot without `getComposedRanges()` | conditional `execCommand("indent")` fallback | unsupported |
| IME composition | none | composition events, provisional DOM, precise MutationObserver timing |
| keyboard copy/cut/paste | none on the normal path | working `ClipboardEvent.clipboardData`/`DataTransfer`, DOM parsing/serialization |
| drag/drop | none | DragEvent/DataTransfer, geometry, selection |
| application toolbar copy/paste implemented with `execCommand` | application-dependent | unsupported; use Clipboard API or application-owned serialization |
| third-party plugin that calls `execCommand` | plugin-dependent | unsupported |

Thus the absence of `execCommand` should not reduce normal light-DOM
ProseMirror editor functionality. It excludes Safari's legacy ShadowRoot
selection fallback plus application/plugin calls. Other substantial
ProseMirror gaps under Radiant are expected to come from its demanding DOM
reconciliation and browser-compatibility surface, not from legacy formatting
commands.

### 13.5 Evidence rule for adding APIs

A new public DOM editing capability is added only when all of these are true:

1. a pinned unmodified editor core, representative public plugin, or focused
   browser comparison reaches the missing capability;
2. it is a standard DOM/web-platform primitive rather than an editor-specific
   concept;
3. its required observable semantics can be stated and tested independently
   of the editor;
4. it can be implemented through shared DOM/event/layout infrastructure.

Priority is:

1. capability required by a pinned CodeMirror, ProseMirror, or Editor.js core
   path in its supported configuration;
2. capability shared by two or more probes;
3. platform-specific behavior selected for a later target.

A Radiant template request alone does not justify a new public DOM API.
Templates must adapt their action and reconciliation implementation to the
surface already justified by the JavaScript-editor probes.

Before attributing an editor failure to editable behavior, add a focused probe
for stable wrapper identity, event cancellation/order, observer
records/timing, Selection mutation, geometry freshness, detached DOM parsing,
or clipboard payloads. This separates a general LambdaJS DOM defect from a
missing editing action.

### 13.6 Packaging and allowed integration

The compatibility suites use committed browser bundles with exact upstream
versions and reproducible build commands:

- CodeMirror 6: retain the current IIFE bundle: `codemirror` 6.0.2,
  `@codemirror/state` 6.7.1, and `@codemirror/view` 6.43.6.
- Editor.js: add a pinned core bundle plus representative paragraph, heading,
  list, and one atomic/media Tool.
- ProseMirror: add one pinned bundle with a minimal normal schema, base keymap,
  commands, and history, mounted in ordinary light DOM.

Bare npm resolution is a separate LambdaJS concern and is not part of the
editable DOM proof.

Allowed test integration consists of normal editor construction,
configuration, public plugins/Tools, and bundling. Patching editor input,
observer, selection, or view source is not allowed. Replacing Editor.js legacy
inline Tools through its public Tool API is the only planned "very little
modification" case; it must be recorded in the fixture manifest.

### 13.7 Verification probes

CodeMirror is the required model-first text probe. Existing tests already
cover construction, state/DOM synchronization, typing, arrow navigation,
select-all replacement, and paste. Extend them with deletion, Enter, word
movement/deletion, composition, mouse selection, cut/copy, history, geometry,
focus, and destruction.

Editor.js is the required DOM-first block-rich probe. A representative
configuration must cover:

- construction and block Tool rendering;
- typing and selection replacement inside a block;
- Enter to create/split a block;
- boundary Backspace/Delete merge/removal;
- block navigation and focus;
- plain and HTML paste through Tool substitution;
- composition;
- block change notification and `save()` output;
- a Range/DOM-based inline formatting Tool;
- destruction without retained observers/listeners.

ProseMirror is the required structured model-first probe. Its pinned light-DOM
configuration must cover:

- construction and rendering of paragraphs, headings, marks, lists, and one
  atomic NodeView;
- typing, selection replacement, and simple deletion;
- editor-owned Enter, block split/join, and boundary deletion through the base
  keymap/commands;
- mark toggling and input rules;
- plain and HTML paste, copy/cut, and drag/drop;
- composition update/commit/cancel;
- history undo/redo;
- mouse placement, node/text selection, geometry, and scrolling;
- destruction without retained observers/listeners.

Safari ShadowRoot selection and any application/plugin `execCommand` path are
excluded. A limitation outside that boundary must be recorded explicitly; it
does not silently reduce the required light-DOM operation matrix.

Assertions must inspect the editor's authoritative state or saved data as well
as the rendered DOM and Selection. A final DOM-text assertion alone does not
prove observer-to-model reconciliation.

### 13.8 Why this is sufficient for a rich editor

Rich-editor semantics do not have to be browser defaults. Paragraph/list/table
rules, marks, schema validation, commands, plugin state, history, and
collaboration can remain entirely in JavaScript or a Radiant source model.
What the host must provide is a trustworthy input surface, selection and
geometry, DOM construction/mutation, clipboard/composition payloads, and
observable timing.

CodeMirror demonstrates that the surface can drive an immutable
transaction-owned text editor. ProseMirror demonstrates structured rich
model/DOM reconciliation. Editor.js demonstrates block-rich Tool DOM and clean
data serialization. Together, the three required suites prove that Radiant is
sufficient to deliver rich editors without implementing a browser rich-text
engine.

---

## 14. Phase-1 Implementation Plan

### Phase 0 — Pin the current baseline

- Keep the existing CodeMirror tests green.
- Add the versioned capability manifest from §13.2 and mark each row
  implemented, partial, or unprobed.
- Add focused transaction tests that demonstrate the current distinction
  between "beforeinput dispatched", "handler claimed", and "DOM mutated".
- Record the current behavior for Backspace, Delete, Enter, paste,
  composition, and history on both route kinds.
- Bundle Editor.js and capture API/event/mutation traces before adding editable
  APIs.
- Bundle ProseMirror and capture its construction/API trace before expanding
  the operation matrix.
- Update detailed design documentation where it describes a native-rich path
  that is no longer present.

**Exit:** current behavior is represented by tests, editor-required capability
gaps are separated from general DOM defects, and no Boolean return is being
interpreted ambiguously in new code.

### Phase 1 — Separate notifications from actions

- Add `EditingTransactionResult`.
- Extract `editing_notify_beforeinput` as a cancelable notification-only
  operation.
- Extract `editing_notify_input` as a non-cancelable notification-only
  operation.
- Move range validation, payload preparation, clipboard work, logging,
  template execution, reconciliation, and action routing out of
  `editing_dispatch_beforeinput_ex`.
- Remove the legacy path that dispatches `input` from inside `beforeinput`.

**Exit:** tests prove `beforeinput` cannot invoke a registered action,
`preventDefault()` suppresses the action stage, and `input` is emitted only
after a claimed or changed result.

### Phase 2 — Structured outcome and action registry

- Add per-document registry and deterministic handler snapshots.
- Register the Radiant template action and DOM compatibility mutation action.
- Add the lifetime-safe JavaScript host registration bridge if it is selected
  for the first milestone.
- Keep existing routing behavior initially.
- Assert one action owner and reject double claims.

**Exit:** existing Radiant template and CodeMirror tests pass through the
registry with no behavior change.

### Phase 3 — Existing-context route selection

- Use `contenteditable` as the only rich editable-host marker.
- Remove `data-editable` classification from the redesigned path.
- Resolve Radiant ownership through the existing runtime/template registry and
  render-map reverse lookup.
- Route all other `contenteditable` hosts to DOM/JavaScript compatibility.
- Add mixed-document tests with JavaScript-created editor DOM inside a Lambda
  application.
- Log route and action owner.

**Exit:** one document can host one Radiant template editor and one
DOM-compatible contenteditable without cross-routing.

### Phase 4 — Browser keyboard ordering

- Dispatch DOM `keydown` before generating a DOM-compatible edit intent.
- Honor `keydown.preventDefault()`.
- Continue to the consumer transaction only when an editing action is still
  required.
- Remove the `break` based solely on `beforeinput` dispatch success.
- Preserve or intentionally migrate Radiant-template ordering with dedicated
  regressions.

**Exit:** CodeMirror and ProseMirror keymaps plus Editor.js block handlers
receive Backspace, Delete, Enter, history, and formatting shortcuts.

### Phase 5 — Pinned-editor DOM compatibility mutations

- Promote the existing insertion helper into the DOM handler.
- Complete text insertion, selection replacement, and simple character
  deletion.
- Add provisional and committed composition mutation.
- Verify that structural, clipboard/drop, and history operations are consumed
  by the pinned editor keymaps/listeners; make unsupported fall-through a test
  failure.
- Publish precise mutation records and selection changes through shared DOM
  APIs.

Implement this in small intent families. Each family must add a root-cause or
invariant comment at its integration point and pass the focused WPT/Chromium
slice before proceeding.

**Exit:** editor-visible DOM behavior is sufficient for the required
CodeMirror, ProseMirror, and Editor.js operation matrices within the explicit
limitations.

### Phase 6 — Observer, layout, and lifecycle hardening

- Verify event/microtask/mutation ordering.
- Make synchronous editor geometry reads fresh.
- Validate observer disconnect and handler unregister on editor destruction.
- Verify focus changes, iframe documents, detached subtrees, and host removal
  during dispatch/composition.

**Exit:** no stale cursor geometry, duplicate observer delivery, detached-view
transaction target, or retained editor resources in the end-to-end suite.

### Phase 7 — End-to-end editor gate

- Add `make` targets or GTest suites for CodeMirror, ProseMirror, and
  Editor.js.
- Pin bundle versions and test manifests.
- Run headless event simulation and model/DOM assertions.
- Include the suites in the appropriate baseline once stable.

**Exit:** CodeMirror, ProseMirror, Editor.js, and Radiant template editing pass
the acceptance matrix in §15 from clean startup to destroy.

### Phase 8 — Retire the transitional paths

- Execute the code, test, log-schema, and documentation cleanup in §17.
- Remove compatibility aliases only after all in-tree callers use the new
  notification, registry, and structured-result contracts.
- Extract still-valid Selection, Range, event-ordering, and observer assertions
  from retired native-editing fixtures before deleting obsolete fixtures.
- Run the legacy-symbol audit and the full editable/editor regression gates.

**Exit:** there is one contenteditable classifier, one consumer-transaction
entry, one action registry, and one notification pair. No active source,
fixture, or replay schema depends on the transitional dispatch path.

### Phase 9 — Full package-owned UA editing

Phase 9 supersedes the first gate's intentionally limited default action and
legacy-command exclusions. The detailed design, work breakdown, and exit gate
are in §20 (D7.2.5).

---

## 15. Phase-1 Acceptance Matrix

`R` is required for the first gate; `N/A` means the editor does not own that
capability in the selected configuration.

| Capability | CodeMirror | Editor.js | ProseMirror | Radiant template | Gate evidence |
|---|:---:|:---:|:---:|:---:|---|
| Construct/render | R | R | R | R | ready marker + initial model/data and DOM |
| Printable typing | R | R | R | R | authoritative state/data + DOM + selection |
| Backspace/Delete | R | R | R | R | within text and editor-owned boundary case |
| Editor-owned Enter/line or block creation | R | R | R | R | authoritative structure/text + DOM |
| DOM key handlers receive `keydown` | R | R | R | R | handler trace + prevented-default outcome |
| `beforeinput` cancellation | R | R | R | R | no action and no post-`input` |
| Selection replacement | R | R | R | R | forward and backward range |
| Mouse caret/selection | R | R | R | R | model/data and DOM selection parity |
| Copy/cut/paste | R | R | R | R | plain and HTML where applicable; no duplicate action |
| MutationObserver | R | R | R | R | precise records + model/Tool/template reconciliation |
| IME composition | R | R | R | R | update, commit, cancel |
| Editor-owned undo/redo | R | N/A | R | R | model and DOM restored without host double edit |
| Scrolling/geometry | R | R | R | R | cursor/toolbar visible + fresh rect |
| Focus/blur | R | R | R | R | `activeElement` and event order |
| Save/serialize | N/A | R | R | R | Tool data or source model matches DOM |
| Destroy/cleanup | R | R | R | R | no callbacks or retained registrations |
| Audit/replay | R | R | R | R | same route, intent, owner, final state |

CodeMirror, ProseMirror, and Editor.js are considered supported only when
their required end-to-end rows pass within the documented limitations.
Library construction or a single typing smoke test is not sufficient.

---

## 16. Phase-1 Open Design Questions

> Phase 9 resolves Q6, Q7, and the `execCommand` limitation in Q10: the Lambda
> DOM package owns full structural defaults, UA history, and the legacy
> command/query surface. The other questions remain applicable unless §20
> states otherwise.

Routing is settled by §7: `contenteditable` is the only host marker and
existing document/template ownership context selects the route. ProseMirror
support, deferred accessibility/input hints, and deferred
`insertLineBreak`/`insertParagraph` are also settled decisions rather than
open questions.

### Q1. Which event domains receive the pure notifications?

**Recommendation:** every contenteditable transaction emits the standard DOM
`InputEvent` notifications. If Lambda templates also need a language-level
notification, expose a distinct cancel-only Lambda observer callback; do not
reuse the Lambda edit action as the notification.

Action handlers never receive `beforeinput` by registry dispatch. They receive
the normalized intent only in the later action stage. Audit/log/record
observers see the gate directly.

### Q2. What happens if a `beforeinput` listener mutates synchronously without
canceling?

Browsers permit arbitrary listener code even though the event is conceptually
a notification. Continuing with the prepared ranges could then apply a second
edit to stale content.

**Recommendation:** record the mutation epoch around notification. For the
first implementation, require a mutating listener to also call
`preventDefault()`; if it does not, stop before handler dispatch, report a
contract violation, and emit no synthetic `input`. A later browser-parity mode
may re-resolve the live selection and continue if editor traces require it.

### Q3. What is the JavaScript action-registration API?

Unmodified CodeMirror, ProseMirror, and Editor.js do not need this API, but
custom JavaScript model editors may prefer direct intent handling.

**Recommendation:** expose a host-scoped registration returning an explicit
unregister handle. Keep it synchronous, snapshot registrations per
transaction, and return a structured claimed/DOM/model outcome. Do not infer
an action handler from `addEventListener("input", ...)`.

### Q4. Does a no-op template handler claim the transaction?

**Recommendation:** not by default. The handler must return or emit an explicit
claim. "Handler function existed" and "edit handled" are different facts.

### Q5. Must the Radiant-template path adopt browser `keydown` ordering?

**Recommendation:** yes eventually, because a single observable order is easier
to audit and reason about. Migrate behind focused editor regressions because
existing template handlers may rely on the current intent-first order.

### Q6. How much native structural editing is required?

**Recommendation:** implement the smallest reusable DOM compatibility action
set that passes the pinned CodeMirror, ProseMirror, and Editor.js paths plus
focused WPT and Chromium slices. `insertLineBreak` and `insertParagraph`
remain deferred, so the pinned editor configurations must consume structural
Enter themselves. Keep unsupported commands explicit. Do not restore an
unbounded browser-quirk project as a prerequisite.

### Q7. Who owns undo for DOM-compatible contenteditable?

CodeMirror and ProseMirror normally own their model history and cancel the
relevant key event. Editor.js history depends on its selected core/plugin
configuration. Simple contenteditable expects host history.

**Recommendation:** first guarantee keydown delivery and editor-owned history.
Add host-native history as a separate handler capability for simple
contenteditable, with no double recording when an editor prevents the key.

### Q8. Are asynchronous action handlers allowed?

**Recommendation:** no for the first implementation. `beforeinput`, model
reconciliation, selection repair, and the registered DOM action remain
synchronous. An asynchronous extension needs an explicit retained transaction
token and detached-target rules.

### Q9. What observer checkpoint is the compatibility contract?

**Recommendation:** pin it with focused Chromium comparisons and LambdaJS
microtask tests. The required synchronous order is
`beforeinput notification -> registered mutation -> input notification`;
MutationObserver delivery follows the normal microtask checkpoint.

### Q10. Which editor configurations are pinned?

The minimum DOM mutator depends on the installed keymaps and history plugins.
A bare ProseMirror view and a view configured with `baseKeymap` do not delegate
the same structural keys to native editing.

**Recommendation:** pin CodeMirror `basicSetup` plus the test extensions;
Editor.js core plus paragraph, heading, list, atomic/media, and one modern
Range-based inline Tool, excluding `enableLineBreaks` Tools; and ProseMirror
`baseKeymap` plus history in ordinary light DOM. Record exact package versions
and make any unexpected unsupported intent in a required suite a failing
trace. Supporting materially different configurations is a separate matrix
entry; Safari ShadowRoot selection is explicitly excluded by the
no-`execCommand` decision.

### Q11. Should bare npm imports be part of the editor milestone?

**Recommendation:** no. Use pinned bundles for the end-to-end gate. Track npm
package resolution separately so editing correctness is not coupled to module
loader work.

---

## 17. Legacy Retirement and Cleanup

### 17.1 Audit result

The monolithic native rich-edit engine is not a remaining migration target:
`editing_rich_transaction.{hpp,cpp}` has already been deleted. Its useful
Layer-A substrate—Selection/Range, target-range calculation, geometry,
clipboard, intent normalization, and interaction validation—still has live
callers and remains necessary.

The current tree does contain transitional code from the subsequent
script-owned migration. It encodes Lambda templates as a second editable
surface, combines notifications and actions in one dispatcher, and retains
consumer/default/testdriver adapters that predate the proposed registry.
These pieces should be retired deliberately rather than kept as a parallel
compatibility path.

### 17.2 CE1–CE3 history (consolidated)

The former `Radiant_Design_Content_Editable*.md` records were consolidated
here and then removed. They capture useful context for the present decision,
but none remains an implementation authority.

| Record | Historical decision | What the current design retains / rejects |
|---|---|---|
| **CE1** — *Radiant `contenteditable`* (2026-05-19) | Defined the DOM editable-host layer shared by Lambda `edit <…>` templates and JavaScript: standard `contenteditable` modes, focus, `beforeinput` / `input`, composition, clipboard, drag/drop, `inputmode`, `enterkeyhint`, and a curated WPT baseline. It deliberately left source or DOM mutation to the editor consumer and rejected `execCommand`, `queryCommand*`, and `designMode`. | Retain the standard-host, Selection/Range, event-payload, and platform-input requirements. Refine the original consumer dispatch into this document's one editing gate and registered action-owner model. The earlier `data-editable` routing discussion is retired. |
| **CE2** — *execCommand, Chrome corpus, WPT baseline* (2026-06-15) | Reversed CE1's legacy-API decision. It proposed a built-in native default-action engine for `execCommand` / `queryCommand*` / `designMode`, plus history and a Chromium `editing/` corpus as the primary compatibility program. Its intended event envelope remained `beforeinput` → mutation → `input`. | Phase 1 rejected both the native engine and the legacy API. Phase 9 now restores the full UA and command/query goals while retaining the crucial ownership correction: algorithms live in the Lambda DOM behavior package; native Radiant supplies only the common gate and generic DOM mechanisms (D7.2.5). |
| **CE3** — *structural Chrome editing corpus plan* (2026-06-18) | Turned CE2's imported Chromium corpus into a broad structural conformance roadmap: a faithful legacy harness, sample-tree selection serialization, a general edit-operation planner, DOM normalization, and capability-by-capability promotion. | The old native conformance lane remains retired. Phase 9 reuses the pinned Chromium corpus as a package-UA conformance gate, never as justification for restoring C++ editing policy. Already-derived `test/editor-js/test/tier_f_chromium` fixtures remain a distinct editor-model suite. |

**Resulting direction.** CE1 identified the durable shared substrate. CE2 and
CE3 identified the compatibility surface but assigned too much editing policy
to native Radiant. Phase 1 corrected that ownership and proved the common gate;
Phase 9 completes the UA surface in Lambda. The design therefore keeps one
gate, sends `beforeinput` and `input` as notifications around one chosen action
owner, and uses standard `contenteditable` plus existing template ownership to
select the model-first template or package-owned UA route (D7.2.5).

### 17.3 Delete after the corresponding cutover

| Existing code or artifact | Cleanup | Required replacement/deletion gate |
|---|---|---|
| `element_has_data_editable()`, the `data-editable` branch in `editing_surface_from_target()`, `EDIT_SURFACE_LAMBDA_TEMPLATE`, and its surface-name/log case | delete | all template editor hosts use `contenteditable`; route ownership is resolved through the runtime/template registry and render-map lookup; mixed-route tests pass |
| direct `data-editable` checks in `radiant/event.cpp` and `radiant/dom_range_resolver.cpp` | delete and consolidate | caret eligibility, hit testing, and range traversal call the canonical `EditingHost`/`editing_surface_from_target()` resolver, including `contenteditable="false"` islands |
| unused wrapper `editing_dispatch_beforeinput()` and overloaded `editing_dispatch_beforeinput_ex()` | delete | all contenteditable callers use `editing_notify_beforeinput()`, the action stage, and `editing_notify_input()` explicitly |
| `dispatch_input_after` and the branch that lets `beforeinput` dispatch `input` | delete | post-notification exists only in the gate commit path |
| fixed Lambda and clipboard members of `EditingDispatchHooks`, plus `dispatch_editing_lambda_event()`, `dispatch_editing_copy_selection()`, and the all-in-one `dispatch_editing_hooks()` factory | remove from the rich/contenteditable path | Lambda action is a registered handler; clipboard preparation is a gate/action responsibility; the DOM event adapter remains available through a narrower notification interface used by contenteditable and form controls |
| `out_lambda_handled`, `lambda_handled`, and `dispatch_input_without_mutation` result/plumbing fields and their log keys | delete | `EditingTransactionResult` records notification, cancellation, selected owner, claim, DOM mutation, model reconciliation, and post-notification separately |
| `dispatch_rich_consumer_transaction_operation()`, `dispatch_rich_consumer_transaction()`, and `dispatch_rich_transaction_defaultable()` | delete after call-site conversion | keyboard, text, composition, clipboard, drag/drop, automation, and replay use one contenteditable consumer-gate entry that snapshots the registered action |
| `RichDefaultTransactionArgs` and its unused fallback view/offset plumbing | delete | the selected target range and canonical DOM Selection are the mutation inputs |
| `JsDomTestdriverMutationArgs`, inert `js_dom_testdriver_rich_mutate()`, and the testdriver's hand-built `EditingTransaction` in `lambda/js/js_dom.cpp` | delete | synthetic keys enter the same platform/automation event path as end-to-end editor input; the testdriver no longer pretends to provide a native edit callback |
| native `document.execCommand`/`queryCommand*` command tables or mutation bodies | replace with thin package dispatch | public bindings remain, but all command lookup, enabled/state/value decisions, mutation, normalization, and history belong to the Lambda DOM behavior package (D7.2.5) |
| `test/ui/editor4b/phase3-no-native-edit.json` as a marker-era regression | replace, then delete | a new gate fixture proves route selection, one action owner, no implicit default, and structured outcome without referring to `data-script-edit` or the deleted native engine |
| `test/ui/_retired_native_editing/` | already deleted with the native-engine retirement; do not recreate it | active tests cover retained event payloads, Selection/Range, clipboard, composition, and observer behavior; no test expects the retired browser-rich algorithms |
| old `test/editing` CE3 runner wiring | keep retired | Phase 9 may expose the same pinned, read-only Chromium corpus through new package-UA runner wiring, but it must not restore the native structural-editing test path; derived `test/editor-js/test/tier_f_chromium` fixtures remain a distinct editor-model suite |
| stale `test/dedup/exclude.json` regions naming removed/renamed transaction helpers | delete | dedup/lint passes without the exclusions |

Deletion happens in the phase that installs the replacement, not as an
unrelated pre-cleanup patch. Temporary adapters must be private, marked with
their removal phase, and must not become a supported API.

### 17.4 Move or rename; preserve the behavior

| Current piece | Disposition |
|---|---|
| `rich_transaction_default_mutate_unscoped()` text replacement/insertion algorithm | decompose into generic checked DOM operations in the native waist and package-owned policy in `lambda/package/dom/dom_edit.ls`; extend the policy only in the package and do not copy it |
| `rich_transaction_default_mutate_scoped()` runtime-context guard | retain where DOM mutation notification requires the document's initialized JS/eval context, but keep it inside the generic native mutation waist rather than a command/default-action handler |
| rich transaction phase, target-range snapshots, selection sequence, re-entrant script-dispatch guard, and state-machine invariants | retain the invariants; migrate names from `rich_transaction_*`/`SM_FAMILY_RICH_EDIT` to route-neutral editable-transaction terminology when the new result schema lands |
| `editing_rich_find_text_descendant()` | retain the caret-placement traversal, rename it to a route-neutral helper such as `editing_find_text_descendant()`, and keep one implementation |
| rich selection snapshot, select-all, caret geometry, drag/drop range, clipboard/DataTransfer, composition, Selection/Range, and MutationObserver helpers | retain and route through the new gate or handler as appropriate; these are shared substrate, not the deleted native rich editor |
| `editing_dispatch_form_beforeinput()` / `editing_dispatch_form_input()` and native form-control value mutation/history | retain; form controls are not contenteditable and remain a separate value-backed action implementation. They may share the narrowed DOM notification adapter but are not silently rewritten by this migration |
| `had_lambda_keydown` form-control caret repair in `radiant/event.cpp` | retain until a separate form-control transaction redesign proves it unnecessary; it is not the contenteditable Lambda action path removed here |

Avoid a cosmetic repository-wide deletion of every `rich_*` identifier.
Rename a symbol only when its current name encodes a removed ownership or
transaction assumption; geometry or selection helpers whose meaning remains
accurate need no churn.

### 17.5 Logs, replay, and documentation

The current audit records `lambda_handled`,
`dispatch_input_without_mutation`, and `rich_transaction_*` state. Those
fields cannot express the new one-owner result and must not be dual-written
indefinitely.

- bump the editing transaction/recording schema at the structured-result
  cutover;
- emit `route`, selected handler snapshot, `action_owner`, `claimed`,
  `dom_mutated`, `model_reconciled`, and notification outcomes from §12;
- treat pre-cutover recordings as an explicitly versioned legacy format;
  reject incompatible replay with a clear schema error unless an offline
  converter is intentionally supplied;
- remove old field aliases after the last in-tree fixture is migrated.

Update the current developer documentation
`RAD_18_Editing_Selection_Ranges.md`,
`RAD_19_Form_Controls.md`, and
`diagram/rad18_dispatch_seam.mmd` at cutover. Older `vibe/editing/` phase and
design records should receive a short “superseded by this proposal” banner
rather than being rewritten to look historically current. The CE1–CE3 history
is consolidated in §17.2 and its separate records are removed. Active Stage 5
material that still prescribes `data-editable` must be changed to standard
`contenteditable` plus the existing template-ownership lookup.

### 17.6 Final removal gates

The cleanup is complete only when:

1. CodeMirror, ProseMirror, Editor.js, and Radiant template end-to-end suites
   pass the required matrix.
2. Form-control before/input, value, selection, IME, clipboard, and history
   regressions remain green.
3. Active source and fixtures have no references to
   `data-editable`, `EDIT_SURFACE_LAMBDA_TEMPLATE`,
   `editing_dispatch_beforeinput_ex`, `dispatch_input_without_mutation`,
   `out_lambda_handled`, or `js_dom_testdriver_rich_mutate`.
4. LambdaJS exposes `execCommand` and `queryCommand*` only as thin dispatch
   bindings; no command name or edit algorithm is implemented in C/C++.
5. There is no direct contenteditable-host parser outside the canonical
   editing-host resolver.
6. New recordings contain exactly one route/action owner and can be replayed
   without consulting a legacy Boolean.
7. Build, lint, baseline Radiant tests, focused InputEvent/Selection/Range
   tests, and all editor suites pass after the obsolete code and fixtures are
   physically removed.

---

## 18. Phase-1 Source Plan

| File | Proposed responsibility |
|---|---|
| `radiant/event.hpp` | handler/route/result contracts |
| `radiant/editing.cpp` | standard contenteditable host and context route resolution |
| `radiant/editing_dispatch.cpp` | handler snapshot, arbitration, structured result |
| `radiant/editing_intent.cpp` | intent normalization only |
| `radiant/event.cpp` | platform event ordering and built-in registrations |
| `lambda/package/dom/dom_edit.ls` | package-owned uncanceled default action |
| `lambda/package/dom/commands.ls` | package-owned legacy command registry and shared command execution |
| `radiant/editing_dom_waist.cpp` | checked generic DOM/Selection/Range primitives; no command policy |
| `radiant/editing_template_handler.cpp` (new or extracted) | Lambda template consumer adapter |
| `radiant/editing_target_range.cpp` | target-range/action parity |
| `radiant/dom_range.cpp` | shared Range mutation helpers, not handler routing |
| `radiant/dom_range_resolver.cpp` | use the canonical contenteditable-host resolver; remove custom-marker parsing |
| `radiant/state_machine.cpp`, `radiant/state_schema.cpp` | preserve transaction invariants while migrating route-neutral state and log names |
| `lambda/js/js_dom.cpp` | JavaScript handler registration bridge, automation routing, and thin `execCommand`/`queryCommand*` package dispatch |
| `lambda/js/js_dom_observers.cpp` | observer conformance fixes found by editor tests |
| `radiant/event_sim.cpp` | composition/editor assertions and replay metadata |
| `doc/dev/radiant/`, `vibe/editing/` | current-design cutover plus superseded historical-document markers |
| `test/ui/`, editor bundle fixtures | required CodeMirror, ProseMirror, Editor.js, Radiant-template, and migration regressions |

Before creating a native primitive, search for existing static helpers and
promote reusable functions to the appropriate module header. Default editing
operations must not be duplicated between `event.cpp`, the DOM waist, and the
Lambda package. Command behavior is added only in the Lambda DOM behavior
package (D7.2.5).

---

## 19. Phase-1 Architecture (retained substrate)

```text
                        Radiant common editing gate
                 logging / audit / recording / playback
                                  |
              ordinary key / clipboard / composition event
                     |
                     +-- JS editor handles + cancels
                     |      -> editor-owned model/DOM transaction
                     |      -> observe and commit; no host action
                     |
             prepare intent / ranges / payload
                    + action-handler snapshot
                                  |
                 cancelable beforeinput notification
                                  |
                        canceled? stop action
                                  |
                       invoke snapshotted action
               /                  |                     \
     Radiant template      DOM compatibility      registered JS action
      source update           text/composition      custom model/DOM
      reconciliation          Range mutation        transaction
      selection project       observer records
               \                  |                     /
                   non-cancelable input notification
                                  |
                          selectionchange
                              |
                 validation / layout / paint / commit
```

This remains the transaction substrate, not the Phase-9 capability ceiling.
CodeMirror proves a model-first text editor, ProseMirror proves structured
model/DOM reconciliation, and Editor.js proves a DOM-first block-rich editor.
Radiant templates use the same public surface and add no private DOM editing
API. The package-owned UA action in §20 replaces the limited “DOM
compatibility” box without changing the single-owner gate around it.

---

## 20. Phase 9 — Full UA `contenteditable` in the Lambda DOM package

### 20.1 Decision and conformance meaning

Phase 9 implements a browser-compatible user-agent editing action for plain
`contenteditable` and `designMode`. The action is a shipped behavior of the
Lambda DOM package, not native Radiant policy and not an application editor.
This is the binding design for D7.2.5 and follows the package/runtime boundary
in D7.2.1–D7.2.3 and the explicit Lambda-to-Radiant contract in D7.5.3.

“Full UA support” has a conformance meaning rather than a promise to copy one
browser's internals:

1. all applicable automated tests in the pinned WPT contenteditable, editing,
   input-events, and Selection slices pass;
2. all applicable automated tests in the pinned Chromium editing corpus pass;
3. the public legacy editing surface—`execCommand`, every `queryCommand*`
   method, and `designMode`—is implemented wherever that corpus exercises it;
4. platform input and programmatic commands use the same package operation
   model and produce compatible DOM, Selection, history, event, and observer
   effects;
5. exclusions are limited to genuinely manual, pixel-only, OS-service, or
   platform-unavailable cases and are named with a reason in a reviewed
   manifest.

WPT is authoritative when the suites disagree. Chromium expectations define
the compatibility result when WPT leaves legacy editing behavior unspecified.
A conflict must be recorded once in the manifest and resolved at the design
level; no runtime branch may inspect a test name, corpus, URL, or harness mode.
Passing through harness-side mutation, command emulation, expected-result
rewrites, or vendor patches is not conformance.

This UA action is the fallback owner. An author editor that cancels the
relevant `keydown`, clipboard/drop event, or `beforeinput`, or a registered
model handler that claims the edit, continues to own its transaction. The UA
package must not perform a second mutation or create a second history entry.

### 20.2 Ownership boundary

| Layer | Owns | Must not own |
|---|---|---|
| Radiant C/C++ | platform key/text/IME/clipboard/drop transport; editable-host and route resolution; the common transaction gate; DOM event propagation; Selection/Range/StaticRange mechanics; MutationObserver plumbing; geometry, layout invalidation, and checked generic DOM mutations | command-name tables, formatting/list/block rules, typing style, normalization policy, UA editing history, or per-command result construction |
| Lambda DOM behavior package | default-action selection; edit planning; structural and inline rules; typing state; normalization; clipboard/drop insertion policy; history; `designMode`; command registry; `execCommand` and `queryCommand*` semantics | platform widgets, layout algorithms, raw native object lifetimes, or editor-library models |
| Page/editor code | first refusal through ordinary cancelable events; its own schema/model transactions, plugins, and history when it claims an action | bypassing the common platform-input gate or causing the UA package to record the same edit |

Native code may validate and retain handles, translate UTF-16 boundaries,
marshal package arguments/results, and expose a generic tree operation that
cannot safely be expressed in Lambda. Such a primitive describes *how* to
perform a checked DOM operation; it must not decide *which* operation an
editing command requires. A performance primitive is acceptable only when it
is parameterized, independently tested, and semantically usable outside one
named command.

The current `radiant/editing_dom_waist.cpp` is therefore a transition
inventory. Its generic range replacement, split, wrap/unwrap, selection, and
observation mechanisms may remain. Any branch that chooses behavior from a
command or `inputType` moves into `lambda/package/dom/`; command-shaped native
exports are narrowed or replaced as soon as their package caller can compose
the equivalent generic operations. The policy must not be copied while it is
moved.

Package state is per document and is reached through the document's package
context, as required by D7.2.1. Mutable command state, typing marks, or history
must not be stored in process-global or module-global state.

### 20.3 One gate, two public entry points

Platform editing and `execCommand` enter through different web APIs but join
before policy is selected:

```text
platform key / text / composition / clipboard / drop
    -> ordinary precursor event
    -> common Radiant editing gate
    -> cancelable beforeinput, where the platform action requires it
    -> Lambda DOM package command registry + operation planner
                                      |
document.execCommand(name, ui, value) |
    -> thin Document binding          |
    -> canonical package command -----+
                                      |
                             validated EditPlan
                                      |
                         generic DOM/Selection waist
                                      |
                    normalization + history transaction
                                      |
                 input / selectionchange / observer checkpoint
```

Both routes must resolve the same host, live selection, command descriptor,
enabled predicate, planner, normalization passes, and history transaction.
Keyboard formatting cannot diverge from `execCommand("bold")`; paste invoked
through a command cannot use a different structural inserter from an
uncanceled paste event.

The shared model does not imply identical event envelopes for every entry
point. Legacy commands have command-specific observable event behavior.
The command descriptor states whether and how the command dispatches
`beforeinput`/`input`, and the implementation follows WPT first and Chromium
where WPT is silent. The gate must not blindly synthesize both events for
every `execCommand` call.

Re-entrant `execCommand` during an editing notification is handled by one
explicit package rule and tested. It may be rejected or nested only where the
compatibility corpus requires nesting; it may never partially apply over an
uncommitted plan.

### 20.4 Package operation model

The package introduces one internal vocabulary used by default actions and
legacy commands. Exact record syntax may follow Lambda package conventions,
but the concepts are required:

- **`EditContext`** — document, editing host, live Selection direction,
  immutable target-range snapshot, `contenteditable` mode, `designMode`
  state, command/value or input intent, clipboard payload, composition state,
  typing state, and history grouping metadata;
- **`CommandDescriptor`** — canonical name and aliases, support/enabled
  predicates, state/value queries, event contract, and planner;
- **`EditPlan`** — an ordered, validated description of mutations, selection
  mapping, normalization scope, and history behavior;
- **primitive steps** — replace or delete range, split text/element, insert,
  remove, move, wrap, unwrap, set/remove attribute or style, merge compatible
  nodes, and set Selection;
- **`EditResult`** — supported, enabled, claimed, changed, selection-changed,
  history-recorded, and failure reason without conflating event dispatch with
  mutation.

Planning is side-effect free. Before application, the package validates that
all referenced nodes are live, belong to one document and one editing host,
do not cross a `contenteditable="false"` boundary, and remain compatible with
the snapshotted mutation epoch. Application is one synchronous transaction.
Failure before apply changes nothing; failure during apply must roll back or
abort at a generic atomic boundary rather than leave a half-normalized tree.

Each primitive returns enough position mapping to project the original
anchor/focus and any retained ranges through the change. A command must not
repair Selection by rediscovering equivalent text after mutation. Direction,
element-boundary positions, atomic nodes, empty blocks, and disconnected
ranges are preserved according to the conformance result.

Normalization is an explicit final pass over the smallest affected roots. It
does not serialize and rebuild the host. It removes only redundant structure
created or exposed by the operation, merges only compatible nodes, preserves
attributes and node identity outside the affected range, and is idempotent.
The same initial DOM plus context must yield the same plan and normalized DOM.

### 20.5 Required UA behavior

The Phase-9 engine covers the behavior families below. The detailed expected
DOM and Selection values come from the conformance manifests, not from ad hoc
examples in this document.

| Family | Required behavior |
|---|---|
| Host state | inherited `contenteditable`, `true`, empty-string, `false`, and `plaintext-only`; nested hosts and false islands; focus/blur/tab behavior; `isContentEditable`; host removal; whole-document `designMode` |
| Text and IME | text/replacement insertion, grapheme-safe backward/forward deletion, word/line deletion where exposed, composition start/update/replace/commit/cancel, typing style on a collapsed caret, and plaintext filtering |
| Selection | forward/backward ranges, caret and element-boundary positions, select-all, extend/modify behavior reached by tests, Selection/Range mutation rules, atomic content, empty hosts/blocks, and cross-node editing constrained to one host |
| Blocks | paragraph and line-break insertion, split/join at starts/ends/middles, headings, block quotes, empty-block behavior, whitespace and `<br>` placeholders |
| Lists and indentation | ordered/unordered list creation and removal, list-item split/join, nested list indent/outdent, partial selections, and conversion at block boundaries |
| Inline formatting | bold, italic, underline, strike-through, subscript, superscript, font name/size, foreground/background color, `styleWithCSS`, `removeFormat`, collapsed typing state, mixed-state selections, and canonical wrapper/style normalization |
| Block formatting | `formatBlock`, justify left/right/center/full, indent/outdent, and multi-block selection behavior |
| Objects and markup | create/unlink links, insert image, horizontal rule, HTML, and text; delete or select atomic nodes without corrupting adjacent structure |
| Clipboard and drag/drop | copy/cut/paste and delete/insert-by-drag; plain/HTML payload selection; sanitization or parsing policy fixed by platform contract; cancellation; source deletion for moves; Selection placement |
| History | undo/redo, typing/composition coalescing, transaction boundaries for commands and clipboard/drop, redo invalidation, and isolation per document/editing host as required by compatibility behavior; the exact proposed storage, step, grouping, and retention contract is in [Radiant_Design_Edit_History.md](Radiant_Design_Edit_History.md) |
| Observability | correct `keydown`/clipboard/drop, `beforeinput`, `input`, `selectionchange`, and composition ordering; `getTargetRanges()`; precise MutationObserver records and microtask delivery; synchronous geometry freshness |

Form controls remain value-backed native controls governed by their existing
transaction/history design. Phase 9 may share event or clipboard mechanisms
with them, but it must not route `<input>` or `<textarea>` mutations through
the DOM-tree UA package.

### 20.6 `execCommand`, query APIs, and `designMode`

`lambda/package/dom/commands.ls` becomes a descriptor registry rather than a
growing conditional. Command names and aliases are canonicalized
case-insensitively once. A descriptor supplies:

- whether the command is supported by this build;
- whether it is enabled in the current document, host, and Selection;
- how state, indeterminate state, and value are derived from the live DOM and
  collapsed typing state;
- how the command lowers to an `EditPlan`;
- its event and history behavior.

The required public contract is:

- `execCommand(command, showUI, value)` executes synchronously and returns the
  compatibility Boolean for the actual supported/enabled/result state;
- `queryCommandSupported` consults the registry without requiring a mutable
  selection and does not claim unsupported commands;
- `queryCommandEnabled`, `queryCommandState`, `queryCommandIndeterm`, and
  `queryCommandValue` read the same descriptor and current context without
  mutating the DOM, Selection, history, or typing state;
- query state is computed from live DOM plus per-document typing state, never
  from an uninvalidated cache;
- `showUI` is accepted for compatibility but triggers no native UI unless a
  conformance requirement explicitly defines one;
- clipboard commands obey the platform's trusted-user-activation and
  permission rules and return the compatible failure value when disallowed;
- `designMode="on"` establishes the document editing host and participates in
  the same focus, Selection, commands, default actions, history, and
  `designMode="off"` teardown rules as ordinary hosts.

The complete command inventory is generated or declared from one package
registry and checked by a package test. Native switch statements and a second
query table are forbidden. Adding support means adding one descriptor plus
its planner/tests, not editing separate keyboard, execution, and query paths.

The first registry baseline is mechanically derived from
`ref/wpt/editing/data` and the pinned Chrome/Chromium corpus. It includes:

- inline commands `bold`, `italic`, `underline`, `strikeThrough`, `subscript`,
  `superscript`, `fontName`, `fontSize`, `foreColor`, `backColor`,
  `hiliteColor`, and `removeFormat`;
- block commands `formatBlock`, every `justify*` form, `indent`, `outdent`,
  `insertOrderedList`, and `insertUnorderedList`;
- insertion and deletion commands `createLink`, `unlink`, `insertImage`,
  `insertHorizontalRule`, `insertHTML`, `insertText`, `insertLineBreak`,
  `insertParagraph`, `delete`, and `forwardDelete`;
- `selectAll`, `copy`, `cut`, `paste`, `undo`, and `redo`;
- compatibility settings and aliases including `styleWithCSS`, `useCSS`, and
  `defaultParagraphSeparator`.

Any additional command reached by an applicable automated pinned test is
required and receives a descriptor. Chrome-only platform/editor commands such
as `transpose`, `yank`, movement/selection commands, and paste variants must
have an explicit supported or platform-unavailable disposition; they must not
silently disappear through a generic unknown-command path.

### 20.7 History, normalization, and observation

The separate proposed history expansion is
[Radiant_Design_Edit_History.md](Radiant_Design_Edit_History.md). It specifies
the physical package-state slot, tagged Lambda schemas, `EditStep`/delta
contract, independent per-surface timelines, atomic Radiant DOM-waist API,
coalescing rules, and the 256-entry-per-timeline/16-MiB-per-document pruning
policy. It also proposes bringing `<input>` and `<textarea>` into the same
package-owned architecture. That expansion is not required by, and does not
replace, the implemented `contenteditable`/`designMode` history in D7.2.5;
the existing form-control boundary in §20.5 remains the current ruling.

UA history is a per-document package service. Every applied `EditPlan` records
one reversible transaction containing primitive inverses or an equivalently
precise retained delta, Selection before/after, typing state, and grouping
metadata. It must:

- coalesce compatible adjacent typing and composition updates without merging
  across explicit commands, focus/host changes, clipboard/drop, or author
  mutations where the browser baseline creates a boundary;
- clear redo after a new committed edit;
- make undo/redo use the same checked primitives, selection mapping,
  normalization, invalidation, and observation path as forward edits;
- avoid a history record for a prevented, declined, unsupported, or unchanged
  operation unless the compatibility contract explicitly treats it as a
  state change;
- avoid recording an editor-owned mutation when page code canceled or claimed
  the UA action.

MutationObserver fidelity is judged at the primitive-operation boundary. The
package must not manufacture observer records or suppress records to match a
final string. DOM mutation primitives queue the real `childList`,
`characterData`, and `attributes` records in operation order; delivery remains
at the normal microtask checkpoint. `input` observes the committed DOM and
Selection, while observer callbacks follow the platform ordering established
by WPT/Chromium.

Normalization belongs to the same history transaction. Undo restores the
pre-operation observable structure, not merely equivalent `innerHTML`, and
redo reapplies the same normalized result without depending on node addresses
that have gone stale.

### 20.8 Native waist and package loading requirements

The Lambda-to-Radiant waist is explicit and reviewable under D7.5.3. Its API
is grouped by mechanism—range validation, tree mutation, selection mapping,
mutation epoch, invalidation—not by command. Every exported primitive must:

1. validate document and editing-host ownership;
2. retain/root every Lambda or DOM object across allocation and callback
   boundaries;
3. preserve live Range and Selection invariants;
4. queue ordinary observer records and rendering invalidation;
5. return structured failure without applying command fallback policy.

The package is loaded as a normal shipped script package under D7.2.1–D7.2.3.
Initialization failure is a document/runtime error and cannot silently fall
back to native editing policy. Package dispatch is synchronous for Phase 9;
no plan or borrowed DOM handle survives an `await`.

“Lambda DOM behavior package” is an ownership label, not a new import spelling.
Its sources currently live under `lambda/package/dom`, while `import dom`
already names the built-in mechanism module. When the old `lambda.package.*`
paths are migrated, the behavior package must receive a distinct,
non-colliding package path; it must not shadow or dual-resolve the built-in
module (D7.2.4).

The native gate remains responsible for logging and replay. Logs identify the
package command/intent, descriptor version, selected action owner, plan/result
summary, history group, and pre/post Selection without serializing private
package implementation state. Replay re-enters through the public gate or
command dispatch, never by invoking mutation primitives directly.

### 20.9 Delivery plan

#### UA-0 — Freeze corpora and expose honest runners

- pin WPT and Chromium revisions and record them in manifests;
- classify applicable automated, manual/pixel, OS-service, and
  platform-unavailable cases with reviewed reasons;
- reintroduce the Chromium corpus as read-only test data for a new package-UA
  runner, not as the retired native CE3 implementation lane;
- inventory semantic branches in `radiant/editing_dom_waist.cpp`,
  `lambda/dom/dom.cpp`, and the WPT harness shim; move implementation behavior
  out of harness code before counting results;
- record the current package command/default-action coverage and failures.

**Exit:** every selected test has one visible disposition, runners execute
product behavior, and baseline counts are reproducible without modifying the
vendor corpora.

#### UA-1 — Establish the operation and descriptor cores

- add per-document `EditContext`, command descriptors, `EditPlan`, structured
  results, transaction application, position mapping, and history storage;
- make the current input and `execCommand` subset use those cores;
- split remaining command policy from generic native mechanics;
- add atomicity, stale-plan, false-island, cross-document, rooting, and
  re-entrancy tests.

**Exit:** input and command entry points demonstrably share one plan/applier,
and native code contains no command-name decision.

#### UA-2 — Complete text, composition, host, and Selection behavior

- finish host inheritance, nested hosts, `plaintext-only`, and `designMode`;
- complete cross-node text replacement/deletion, grapheme/word boundaries,
  element-boundary carets, false islands, and atomic nodes;
- complete composition update/commit/cancel and typing-state foundations;
- close applicable WPT contenteditable, input-events, and Selection failures.

**Exit:** the non-rich foundation passes its selected WPT slices and has no
native fallback action.

#### UA-3 — Complete structural insertion and deletion

- implement paragraph/line-break rules, block splitting/joining, placeholders,
  headings, quotes, lists, indentation, tables, and boundary deletion;
- implement minimal affected-root normalization and Selection mapping;
- promote Chromium structural groups only after focused unit and WPT slices
  pass.

**Exit:** structural Chromium groups are green with stable DOM identity and
observer records.

#### UA-4 — Complete formatting, objects, and queries

- implement all inline and block families in §20.5;
- add collapsed typing state and mixed-selection state/value calculation;
- implement links, images, horizontal rules, HTML/text insertion, and
  `removeFormat`;
- complete every `queryCommand*` method from the same registry.

**Exit:** command execution and query matrices pass for collapsed, uniform,
mixed, multi-block, disabled, and unsupported contexts.

#### UA-5 — Complete clipboard, drag/drop, and history

- implement rich/plain copy, cut, paste, and drag/drop through shared plans;
- enforce trusted activation/permission behavior at the platform boundary;
- implement coalescing, undo/redo, redo invalidation, selection restoration,
  author-mutation boundaries, and editor-cancellation isolation, using the
  ratified contract from
  [Radiant_Design_Edit_History.md](Radiant_Design_Edit_History.md).

**Exit:** clipboard/drop and history WPT/Chromium groups are green, including
event and observer ordering.

#### UA-6 — Close conformance and cut over

- make every applicable automated WPT selection green;
- make every applicable automated pinned Chromium editing case green;
- keep CodeMirror, ProseMirror, Editor.js, Radiant template, and form-control
  regressions green;
- remove superseded native policy, harness emulation, duplicate command
  tables, transitional command-shaped waist calls, and obsolete exclusions;
- promote stable runners into the required baseline/CI configuration through
  `build_lambda_config.json` and normal build generation.

**Exit:** §20.11 is satisfied from a clean build with no behavior supplied by
the test harness.

### 20.10 Conformance program

The conformance program has three complementary layers:

| Gate | Source | Purpose |
|---|---|---|
| Focused package tests | `lambda/package/dom` plus purpose-built JS/UI fixtures | planner, descriptors, queries, normalization, history, error atomicity, and native-waist contracts |
| WPT | pinned `ref/wpt/contenteditable/`, applicable `ref/wpt/editing/`, `input-events/`, and Selection tests | standards-facing host, event, command, Selection/Range, and observable DOM behavior |
| Chromium compatibility | pinned Chromium `editing/` corpus through a Lambda-side runner/manifest | legacy command DOM/Selection results and browser behavior where WPT is silent |

The previously retired native runner names may be reused only if their new
scope and linkage make the package ownership unambiguous; preferred suite
names are `test_wpt_contenteditable_gtest` and
`test_chromium_contenteditable_gtest`. Runner code adapts harness APIs,
selects manifest entries, and reports results. It must not mutate the editing
host, synthesize command results, normalize output, or maintain expected UA
state on the product's behalf.

Each manifest records upstream revision, source path, test kind, required
features, disposition, and—only for an exclusion—a stable reason category.
Failure baselines are temporary progress reports, not acceptance criteria.
Promotion is monotonic by behavior family: a promoted group may not be hidden
later behind an exclusion because another group is difficult.

The final automated gate permits zero unexpected failures, crashes, hangs, or
timeouts. A platform-unavailable case is counted separately and must have a
focused Lambda-side test for every product behavior that can be exercised
without that external service. D7.3.5 requires these gates for the shipped
package; package unit tests alone are insufficient.

### 20.11 Phase-9 exit criteria

Phase 9 is complete only when all of the following hold:

1. all applicable automated pinned WPT contenteditable/editing, input-events,
   and Selection cases pass with the reviewed exclusion manifest;
2. all applicable automated pinned Chromium editing cases pass with the same
   no-emulation rule;
3. `execCommand`, all five `queryCommand*` methods, and `designMode` satisfy
   their command/state/event/Selection/history matrices;
4. platform defaults and legacy commands share the package descriptor,
   planner, normalization, and history implementation;
5. no C/C++ file contains a command-name table, list/block/formatting rule,
   UA normalization policy, or contenteditable history algorithm;
6. no WPT/Chromium harness code implements product editing behavior or patches
   vendor expectations;
7. package state is isolated per document and passes teardown, re-entrancy,
   stale-handle, and GC-rooting tests;
8. CodeMirror, ProseMirror, Editor.js, Radiant template editing, form controls,
   logging/replay, and mutation/selection/layout regressions remain green;
9. the active implementation and developer docs agree with D7.2.5 and this
   section, and superseded Phase-1 exclusions are no longer presented as
   current limitations.

### 20.12 Principal risks and controls

| Risk | Control |
|---|---|
| Chromium quirks distort package architecture | WPT wins conflicts; descriptors and generic plans isolate compatibility rules; every divergence is manifest-reviewed |
| Native waist grows back into an editor | mechanism-only API review, no command strings, one package registry, and the C/C++ ownership exit audit |
| Exact DOM matching causes whole-host rebuilds | affected-root plans, position maps, node-identity assertions, and precise observer tests |
| UA history double-records editor/model changes | cancellation/claim gate precedes package apply; history begins only for the chosen UA owner |
| Re-entrancy or allocation invalidates DOM handles | synchronous plans, mutation-epoch validation, per-document state, and precise rooting across every waist call |
| Harness support is mistaken for product support | harness mutation/emulation audit in UA-0 and no-emulation CI assertions |
| Corpus size hides regressions | capability-family promotion, focused package tests before corpus promotion, deterministic seeds, and zero unexpected crash/hang policy |
