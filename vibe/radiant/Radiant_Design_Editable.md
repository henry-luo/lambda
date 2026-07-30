# Radiant Editable Support — Minimal DOM Compatibility and a Common Editing Gate

**Date:** 2026-07-29
**Status:** Implemented — first gate verified
**Scope:** `contenteditable`, Lambda/Radiant editable templates, the common
editing transaction gate, registered action handlers, and the minimum DOM
editing capabilities needed to run CodeMirror 6, ProseMirror, and Editor.js
with little or no library modification.

**Implementation boundary (2026-07-29):** The first gate is implemented and
its exact supported configurations are recorded in
`test/editable-editors/capability-manifest.json`. The public JavaScript action
registration bridge is deliberately deferred: the pinned unmodified editors
use standard DOM events and the two built-in native registrations. Editor.js
ordinary paragraph paste is an explicit exclusion because its upstream core
delegates that path to browser-native structural editing; Radiant exposes the
clipboard event but does not recreate that retired default action.

**Builds on:**

- [Radiant_Design_Content_Editable.md](../editing/Radiant_Design_Content_Editable.md)
- [Radiant_Design_Content_Editable2.md](../editing/Radiant_Design_Content_Editable2.md)
- [Radiant_Design_Content_Editable3.md](../editing/Radiant_Design_Content_Editable3.md)
- [RAD_15 — Events and Input](../../doc/dev/radiant/RAD_15_Events_Input.md)
- [RAD_18 — Editing, Selection and DOM Ranges](../../doc/dev/radiant/RAD_18_Editing_Selection_Ranges.md)
- [RAD_21 — JS Scripting Integration](../../doc/dev/radiant/RAD_21_JS_Scripting_Integration.md)

---

## 1. Decision

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

Radiant explicitly has **no implicit browser default mutation** for
`contenteditable`. Instead, the fixed single-consumer assumption is replaced
by a per-document registry of editing action handlers. Multiple handlers may
be registered, but routing selects at most one action owner for a transaction.
Other matching observers may inspect the transaction through the gate, but
they are not action handlers.

Two built-in action handlers are introduced:

1. **Radiant template handler** — preserves the current model-first path for
   Lambda/Radiant editable templates.
2. **DOM compatibility mutation handler** — an explicitly registered handler
   for direct HTML/JavaScript documents. It supplies the limited
   contenteditable DOM mutations and observer notifications proven necessary
   by pinned editor traces.

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

The design target is **web compatibility, not editor implementation**.
Radiant will not embed, port, or reproduce the internal document model,
transaction engine, command system, history, or plugin system of any of the
three editors. Real editor bundles are compatibility probes used to show that
the DOM surface is sufficient to deliver code and rich editors.

CodeMirror is the primary required end-to-end probe because its model and
input architecture are relatively small and well isolated. ProseMirror is the
required structured rich-editor probe, with the explicit light-DOM and
no-`execCommand` limitations in §13.4. Editor.js is the required block-rich
probe because it exercises a different, DOM-first Tool model.

Radiant editable templates must use this same gate and the same public DOM
surface. Template editing may retain its source-model transaction and
reconciliation internals, but it must not request a template-only Selection,
Range, input, clipboard, composition, mutation, or geometry API. A capability
not needed by a real JavaScript editor is not added merely for the template
path.

Radiant will provide **no `document.execCommand` or `queryCommand*`
support**. Formatting, link creation, block transformation, history, and
equivalent operations are owned by the JavaScript or Lambda editing script,
which updates its model or mutates through standard Range/DOM APIs. Editor.js
Tools or third-party plugins that require legacy browser editing commands are
outside the compatibility target.

The current LambdaJS document bindings expose inert `execCommand` and
`queryCommand*` functions that always report failure. Those compatibility
stubs are retired with this migration. An excluded API should be absent rather
than look present to JavaScript feature detection; Radiant does not keep a
second legacy-command dispatch surface beside the registered action handlers.

Accessibility/input-hint behavior and the DOM-compatible default actions for
`insertLineBreak` and `insertParagraph` are deferred to a future milestone.
The supported editor configurations must implement Enter, line, and block
structure in their JavaScript or Lambda editing actions.

---

## 2. Goals

1. Preserve the common editing transaction gate and its logging, audit,
   record, replay, and validation properties.
2. Make `beforeinput` a pure, cancelable pre-action notification and `input` a
   pure, non-cancelable post-action notification.
3. Permit multiple editing handlers to be registered without allowing
   duplicate mutations.
4. Route direct HTML/JavaScript documents to an explicitly registered,
   narrowly scoped DOM compatibility mutation handler.
5. Route Lambda/Radiant template documents to model-first Radiant editing.
6. Support mixed documents containing both Radiant-owned surfaces and
   third-party JavaScript editors.
7. Define and freeze a minimal viable editable DOM surface, with every
   addition justified by an upstream editor trace or focused browser probe.
8. Run real, bundled CodeMirror 6, ProseMirror, and Editor.js under Radiant
   through normal public browser APIs, without library-specific engine
   branches.
9. Support ProseMirror within the pinned light-DOM configuration and explicit
   limitations in §13.4 without adopting its schema/transform internals.
10. Add deterministic end-to-end tests for typing, deletion, editor-owned
    line/block entry, selection, clipboard, composition, history, and observer
    reconciliation.
11. Keep Radiant editable templates on the same input/event/DOM capability
    surface as JavaScript editors.
12. Remove transitional editable-routing, notification, result, and testdriver
    code after the new gate reaches its end-to-end cutover; do not leave two
    competing transaction paths.

## 3. Non-goals

- Do not embed Chromium, WebKit, or another browser engine.
- Do not embed, port, or reimplement CodeMirror, ProseMirror, or Editor.js in
  Radiant.
- Do not add editor-specific branches, editor model types, schema rules,
  transforms, commands, or history to Radiant.
- Do not let page scripts bypass the common editing gate for platform input.
- Do not require bare npm package resolution for the first compatibility
  milestone. Checked-in test bundles are sufficient.
- Do not restore a monolithic native rich-text editor. The DOM compatibility
  mutator is a registered action handler with a test-driven compatibility
  scope.
- Do not make event recording depend on the chosen editing handler.
- Do not implement `document.execCommand` or `queryCommand*`. JavaScript or
  Lambda editing actions must perform the corresponding model/DOM transaction.
- Do not implement full browser rich-`contenteditable` compatibility beyond
  the selected minimum editor contract.
- Do not support ProseMirror's Safari ShadowRoot `execCommand("indent")`
  selection fallback.
- Do not add editable-specific accessibility/input-hint behavior in the first
  milestone.
- Do not implement DOM-compatible `insertLineBreak` or `insertParagraph`
  default actions in the first milestone.

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
paste, drop, and structural editing are therefore compatible with Radiant's
no-`execCommand` decision. Keyboard clipboard events use
`ClipboardEvent.clipboardData`/`DataTransfer`; older broken-clipboard
fallbacks use temporary DOM selection.

There is one conditional core exception in `prosemirror-view`: a Safari
selection workaround for an editor mounted inside a ShadowRoot falls back to
`execCommand("indent")` to trigger `beforeinput` when
`Selection.getComposedRanges()` is unavailable. It is not reached for an
ordinary light-DOM editor. Radiant does not support this fallback; a pinned
ProseMirror probe must use light DOM and must not select Safari Shadow DOM
branches.

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
  paste remains an explicit native-default exclusion unless a Tool claims it;
- Tool code depends on ordinary DOM construction and serialization.

The stock bold, italic, and link inline Tools currently use legacy
`document.execCommand`/`queryCommandState`. That dependency is isolated from
the block editor core. The minimal Radiant milestone supports the Editor.js
core and representative block Tools without modification, while allowing a
small replacement inline Tool for formatting. Stock or third-party Tools that
depend on `execCommand` are explicitly unsupported under Radiant.

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
         -> registered DOM-compatible mutation, or
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
9. **Complete observation:** DOM mutations made by the DOM compatibility
   handler
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

#### DOM compatibility mutation handler

- Matches `EDIT_SURFACE_CONTENTEDITABLE` under the DOM compatibility route.
- Applies only the native-like DOM mutations in §10 when the preceding
  keyboard/clipboard event and `beforeinput` were not canceled.
- Updates DOM Selection, invalidates rendering, and publishes precise mutation
  records through the ordinary DOM mutation path.
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
    +--> DOM compatibility mutation
    |      + queue MutationObserver records
    |      + update DOM Selection
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

## 10. DOM Compatibility Mutation Handler

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

## 13. Minimum Editable DOM Compatibility Surface

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

## 14. Implementation Plan

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

---

## 15. Acceptance Matrix

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

## 16. Open Design Questions

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

### 17.2 Delete after the corresponding cutover

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
| inert `document.execCommand`/`queryCommand*` cases in the LambdaJS document method/property dispatch tables | delete | the APIs are outside the capability surface and resolve as absent; editor fixtures use model commands or standard Range/DOM operations |
| `test/ui/editor4b/phase3-no-native-edit.json` as a marker-era regression | replace, then delete | a new gate fixture proves route selection, one action owner, no implicit default, and structured outcome without referring to `data-script-edit` or the deleted native engine |
| `test/ui/_retired_native_editing/` | extract useful substrate assertions, then delete the obsolete archive | equivalent active tests cover retained event payloads, Selection/Range, clipboard, composition, and observer behavior; no test expects the retired browser-rich algorithms |
| stale `test/dedup/exclude.json` regions naming removed/renamed transaction helpers | delete | dedup/lint passes without the exclusions |

Deletion happens in the phase that installs the replacement, not as an
unrelated pre-cleanup patch. Temporary adapters must be private, marked with
their removal phase, and must not become a supported API.

### 17.3 Move or rename; preserve the behavior

| Current piece | Disposition |
|---|---|
| `rich_transaction_default_mutate_unscoped()` text replacement/insertion algorithm | move into `editing_dom_handler.cpp`, generalize around prepared target ranges, and extend only for the minimum actions in §13.3; do not copy it |
| `rich_transaction_default_mutate_scoped()` runtime-context guard | retain where DOM mutation notification requires the document's initialized JS/eval context, but make it an implementation detail of the DOM handler |
| rich transaction phase, target-range snapshots, selection sequence, re-entrant script-dispatch guard, and state-machine invariants | retain the invariants; migrate names from `rich_transaction_*`/`SM_FAMILY_RICH_EDIT` to route-neutral editable-transaction terminology when the new result schema lands |
| `editing_rich_find_text_descendant()` | retain the caret-placement traversal, rename it to a route-neutral helper such as `editing_find_text_descendant()`, and keep one implementation |
| rich selection snapshot, select-all, caret geometry, drag/drop range, clipboard/DataTransfer, composition, Selection/Range, and MutationObserver helpers | retain and route through the new gate or handler as appropriate; these are shared substrate, not the deleted native rich editor |
| `editing_dispatch_form_beforeinput()` / `editing_dispatch_form_input()` and native form-control value mutation/history | retain; form controls are not contenteditable and remain a separate value-backed action implementation. They may share the narrowed DOM notification adapter but are not silently rewritten by this migration |
| `had_lambda_keydown` form-control caret repair in `radiant/event.cpp` | retain until a separate form-control transaction redesign proves it unnecessary; it is not the contenteditable Lambda action path removed here |

Avoid a cosmetic repository-wide deletion of every `rich_*` identifier.
Rename a symbol only when its current name encodes a removed ownership or
transaction assumption; geometry or selection helpers whose meaning remains
accurate need no churn.

### 17.4 Logs, replay, and documentation

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
rather than being rewritten to look historically current. Active Stage 5
material that still prescribes `data-editable` must be changed to standard
`contenteditable` plus the existing template-ownership lookup.

### 17.5 Final removal gates

The cleanup is complete only when:

1. CodeMirror, ProseMirror, Editor.js, and Radiant template end-to-end suites
   pass the required matrix.
2. Form-control before/input, value, selection, IME, clipboard, and history
   regressions remain green.
3. Active source and fixtures have no references to
   `data-editable`, `EDIT_SURFACE_LAMBDA_TEMPLATE`,
   `editing_dispatch_beforeinput_ex`, `dispatch_input_without_mutation`,
   `out_lambda_handled`, or `js_dom_testdriver_rich_mutate`.
4. LambdaJS no longer exposes `execCommand` or `queryCommand*` document
   properties.
5. There is no direct contenteditable-host parser outside the canonical
   editing-host resolver.
6. New recordings contain exactly one route/action owner and can be replayed
   without consulting a legacy Boolean.
7. Build, lint, baseline Radiant tests, focused InputEvent/Selection/Range
   tests, and all editor suites pass after the obsolete code and fixtures are
   physically removed.

---

## 18. Likely Source Changes

| File | Proposed responsibility |
|---|---|
| `radiant/event.hpp` | handler/route/result contracts |
| `radiant/editing.cpp` | standard contenteditable host and context route resolution |
| `radiant/editing_dispatch.cpp` | handler snapshot, arbitration, structured result |
| `radiant/editing_intent.cpp` | intent normalization only |
| `radiant/event.cpp` | platform event ordering and built-in registrations |
| `radiant/editing_dom_handler.cpp` (new) | registered DOM compatibility mutator using shared DOM helpers |
| `radiant/editing_template_handler.cpp` (new or extracted) | Lambda template consumer adapter |
| `radiant/editing_target_range.cpp` | target-range/action parity |
| `radiant/dom_range.cpp` | shared Range mutation helpers, not handler routing |
| `radiant/dom_range_resolver.cpp` | use the canonical contenteditable-host resolver; remove custom-marker parsing |
| `radiant/state_machine.cpp`, `radiant/state_schema.cpp` | preserve transaction invariants while migrating route-neutral state and log names |
| `lambda/js/js_dom.cpp` | JavaScript handler registration bridge, automation routing, and removal of legacy command/testdriver shims |
| `lambda/js/js_dom_observers.cpp` | observer conformance fixes found by editor tests |
| `radiant/event_sim.cpp` | composition/editor assertions and replay metadata |
| `doc/dev/radiant/`, `vibe/editing/` | current-design cutover plus superseded historical-document markers |
| `test/ui/`, editor bundle fixtures | required CodeMirror, ProseMirror, Editor.js, Radiant-template, and migration regressions |

Before creating either proposed handler source, search for existing static
helpers and promote reusable functions to the appropriate module header.
Default editing operations must not be duplicated between `event.cpp` and the
DOM compatibility mutation handler. No `execCommand` implementation is added.

---

## 19. Final Architecture

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

The durable compatibility boundary is the minimum web editing contract, not
an editor library or document model. CodeMirror proves a model-first text
editor, ProseMirror proves structured model/DOM reconciliation within the
documented limitations, and Editor.js proves a DOM-first block-rich editor.
Radiant templates use the same boundary and add no private DOM editing
surface.
