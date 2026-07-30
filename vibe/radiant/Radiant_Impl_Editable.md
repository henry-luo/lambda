# Radiant Editable Support — Detailed Implementation Plan

**Date:** 2026-07-29
**Status:** Implemented — verification complete
**Design contract:** [Radiant_Design_Editable.md](Radiant_Design_Editable.md)
**Scope:** migrate `contenteditable` input to a notification/action/notification
gate, add deterministic per-document action routing, provide the minimum
DOM-compatible text/composition action needed by real editors, retain Radiant
template editing on the same gate, and prove the result with CodeMirror 6,
ProseMirror, and Editor.js end-to-end tests.

This document is an execution plan. The design contract remains authoritative
for product scope and semantics. When implementation evidence changes a
capability assumption, update the capability manifest and the design contract
before widening the native action.

---

## 1. Terminal outcome

The migration is complete when a platform, automation, or replay edit aimed at
a `contenteditable` host follows one observable path:

```text
ordinary DOM key/clipboard/composition event
  -> prepare one EditingIntent and stable transaction snapshot
  -> select at most one action handler
  -> dispatch cancelable beforeinput notification
  -> if allowed, synchronously invoke the selected action
  -> verify DOM/model/selection outcome
  -> if claimed or changed, dispatch non-cancelable input notification
  -> validate, record, invalidate, and finish the event-loop batch
```

The terminal tree has:

1. one canonical `contenteditable` host resolver;
2. one route-neutral contenteditable transaction entry;
3. one per-document action registry;
4. one cancelable `beforeinput` notification function;
5. one non-cancelable `input` notification function;
6. one narrowly scoped DOM compatibility action;
7. one Radiant template action adapter;
8. one structured transaction result used by input, audit, and replay;
9. real upstream editor tests that assert authoritative editor state, not only
   final DOM text;
10. no transitional `data-editable`, all-in-one beforeinput dispatcher,
    testdriver mutator, or inert legacy command surface.

The implementation does **not** create a browser-owned rich-editing engine.
CodeMirror, ProseMirror, Editor.js, and Radiant templates own their document
models, commands, structure, and history. Radiant supplies event delivery,
the limited uncanceled DOM fallback, and the standard DOM substrate those
editors use.

### 1.1 First-gate limits

The following stay explicit throughout implementation:

- `insertLineBreak` and `insertParagraph` return `PASS`; editor code owns
  Enter and block/line creation.
- formatting and history intents return `PASS`; editor commands own them.
- cut, paste, and drop have no general native mutation unless a required
  pinned trace proves that a reusable fallback is necessary.
- `document.execCommand` and `queryCommand*` are absent, not inert.
- accessibility, input hints, virtual-keyboard integration, spellcheck,
  autocorrect, and mobile-specific editing behavior are deferred.
- ProseMirror support is ordinary light-DOM support. Its Safari ShadowRoot
  `execCommand("indent")` fallback is excluded.
- Editor.js Tools that require `execCommand` are excluded. The probe uses
  modern Tool implementations based on public Tool APIs and Range/DOM
  mutation.
- public JavaScript action registration is not required by the three
  unmodified upstream editors. The native registry is required; the public
  bridge is a separately gated extension described in §4.10.
- Editor.js ordinary paragraph paste is explicitly excluded in the selected
  configuration: upstream delegates it to browser-native structural editing,
  while this gate intentionally provides no implicit structural default.

### 1.2 Compatibility proof, not editor implementation

The end-to-end editor suites are evidence about Radiant's public web surface.
They must not add editor-name branches to production code. If an editor test
fails, reduce the failure to a focused event, DOM, Selection/Range,
MutationObserver, clipboard, parsing, scheduling, or geometry test before
changing the engine.

---

## 2. Ground rules

1. Preserve the common editing gate for platform input, automation, recording,
   and replay. A test helper must not call an action handler directly.
2. `beforeinput` and `input` are notification functions only. Do not hide
   clipboard work, Lambda execution, mutation, reconciliation, or recursive
   event dispatch inside either function.
3. Select one action owner before `beforeinput`. Listener registration changes
   during a transaction affect the next transaction.
4. Derive the actual DOM outcome from a monotonic mutation epoch. Do not trust
   a handler-provided `dom_mutated` Boolean without verification.
5. Use existing DOM mutation, Selection/Range, observer, layout, clipboard,
   and runtime-context mechanisms. Promote reusable static helpers; never copy
   an implementation into an editor-specific path.
6. Use project `lib/` containers and C+ conventions. Do not introduce
   `std::string`, `std::vector`, `std::map`, or other `std::` containers.
7. All Radiant position and dimension variables remain `float`. Any necessary
   integer cast follows the repository's `INT_CAST_OK` rule.
8. Add a short root-cause/invariant comment at every lifecycle, re-entrancy,
   batching, mutation-observer, or platform-correlation protection point.
9. Keep form-control editing separate. This plan may share a narrowed DOM
   notification adapter, but it does not replace native `<input>`/`<textarea>`
   value mutation, selection, IME, or history.
10. Delete transitional code in the phase that installs and verifies its
    replacement. Do not create a permanent compatibility layer between the
    old and new result models.
11. Pin upstream packages exactly and keep the end-to-end tests offline.
    Public network access is never a baseline-test dependency.
12. Make unexpected unsupported intents fail the editor suite with a readable
    transaction trace. Silent fall-through is not compatibility evidence.

---

## 3. Current implementation map

The plan starts from the following source boundaries. Re-audit these symbols
at the beginning of implementation because adjacent DOM work may change them.

| Area | Current seam | Migration disposition |
|---|---|---|
| Editable classification | `radiant/editing.cpp`: `element_has_data_editable()`, `editing_surface_from_target()`, `EDIT_SURFACE_LAMBDA_TEMPLATE` | replace with one standard `contenteditable` host classifier plus a separate route snapshot |
| Transaction contract | `radiant/event.hpp`: `EditingDispatchHooks`, `EditingTransaction`, Boolean out-parameters | split notification hooks from action handlers and introduce prepared/result structs |
| All-in-one gate | `radiant/editing_dispatch.cpp`: `editing_dispatch_beforeinput_ex()`, native callback, Lambda handling, optional `input` | decompose into prepare, notify, action, verify, notify, finish |
| Event bridge | `radiant/event.cpp`: `dispatch_editing_hooks()`, rich consumer/defaultable wrappers | replace with one route-neutral transaction adapter |
| Narrow native insertion | `rich_transaction_default_mutate_unscoped()` and scoped wrapper in `radiant/event.cpp` | move, do not copy, into `radiant/editing_dom_handler.cpp` |
| Template execution | `dispatch_lambda_handler()` in `radiant/event.cpp` | extract reusable invocation/retransform machinery and wrap it as a registered template action |
| Target ranges | `radiant/editing_target_range.cpp` | retain notification substrate; prevent structural target ranges from authorizing unsupported native mutation |
| Host parsing | direct checks in `radiant/dom_range_resolver.cpp` and `radiant/event.cpp` | call the canonical editing-host resolver |
| State invariants | `rich_transaction_*` state in `event.hpp`, `state_machine.cpp`, and `state_schema.cpp` | preserve behavior; rename only ownership-specific terminology at schema cutover |
| Mutation observation | `DomJsRuntime::mutation_count`, `mutation_sequence`, `js_dom_notify_mutation*()` | add a reset-independent mutation epoch and harden shared Range mutation records |
| Testdriver edit | `JsDomTestdriverMutationArgs` and `js_dom_testdriver_rich_mutate()` in `lambda/js/js_dom.cpp` | route synthetic physical keys through public event simulation, then delete |
| Legacy commands | inert `execCommand`/`queryCommand*` entries in `lambda/js/js_dom.cpp` | remove method/property exposure and assert absence |
| Event simulator | `radiant/event_sim.cpp` `type` emits text-input events without keydown/keyup | retain legacy `type`; add browser-style physical typing for editor tests |
| Existing editor fixture | `test/js/codemirror6.min.js`, `test/ui/dom/codemirror_*` | retain as smoke coverage; add authoritative-state and operation-matrix fixtures |
| Name collision | `test/editor-js` is Lambda's `@lambda/editor-js`, not upstream Editor.js | create a distinctly named upstream editor compatibility package |

### 3.1 Current flow that is being retired

The current rich path combines responsibilities:

```text
intent
  -> dispatch_rich_* wrapper
  -> editing_run_transaction
  -> editing_dispatch_beforeinput_ex
       -> JS beforeinput
       -> Lambda beforeinput/action
       -> optional native mutate callback
       -> optional input
  -> Boolean handled/mutated/lambda_handled results
```

This shape creates four implementation hazards:

1. notification delivery can execute the Lambda editing algorithm;
2. successful dispatch can be confused with cancellation or action ownership;
3. keyboard intent dispatch can run before an editor's `keydown` keymap;
4. a handler registration or synchronous mutation can invalidate ranges that
   were prepared for a later action.

The migration must remove these causes rather than add more Boolean guards.

### 3.2 Two source-level hazards to pin before refactoring

**Mutation reset:** the existing observer record sequence is reset when
records are cleared. It cannot prove whether `beforeinput` synchronously
mutated the DOM. Add a distinct monotonic document mutation epoch before
using mutation-based contract checks.

**Structural target ranges:** `editing_target_range.cpp` contains retained
range calculation for block joins, list/table boundaries, atomic content, and
other historical native behavior. Those ranges can be useful notification
data, but they are not permission for the minimal DOM handler to perform a
structural edit. The action must independently reject ranges outside its
supported text/selection predicates.

### 3.3 Range mutation/observer gap

`dom_range_delete_contents()` and `dom_range_insert_node()` maintain DOM and
live-range state, but the current callers do not uniformly publish precise
`characterData` and `childList` observer records. The current native insertion
path compensates with a coarse tree-replace record for selected deletion.
That is insufficient for model-first editor reconciliation.

Phase 6 must correct ordinary shared Range/DOM mutation behavior or introduce
one shared mutation-aware primitive used by both JavaScript Range calls and
the DOM editing action. It must not add an editor-only observer notification
fork.

---

## 4. Target implementation architecture

This section fixes the internal contracts that later phases implement. Type
names may be adjusted to existing repository naming, but their responsibility
boundaries and result distinctions are required.

### 4.1 Per-document registry ownership

Add one `EditingActionRegistry` per `DomDocument`.

Recommended ownership:

1. add an opaque `editing_action_registry` pointer to
   `DomDocumentServices`;
2. allocate the registry as a `DomDocumentResource`;
3. register its destroy callback with `dom_document_add_resource()`;
4. destroy registrations and JavaScript roots while the retained document
   runtime is still alive;
5. clear the services pointer during destruction.

This avoids a process-global registry and avoids copying versioned registry
state through `DocState`. It also makes iframe/document teardown and editor
destruction testable at the correct owner.

The registry contains repository `ArrayList` storage, a monotonically
increasing registration ID/generation, dispatch depth, and tombstoned entries.
It must not own DOM nodes through raw, untracked pointers. Match/action calls
receive the stable prepared host reference for the current transaction.

### 4.2 Route contract

Introduce a route separate from editable-surface classification:

```cpp
enum EditingRouteKind {
    EDITING_ROUTE_NONE = 0,
    EDITING_ROUTE_DOM_SCRIPT,
    EDITING_ROUTE_RADIANT_TEMPLATE,
};
```

`EditingSurface` answers whether the target is a text control or a standard
editable host and carries host/mode information. `EditingRouteSnapshot`
answers who owns the action for this transaction.

For a `contenteditable` host:

1. resolve the nearest canonical editing host, honoring
   `contenteditable="false"` islands;
2. use the document's existing template registry and render-map reverse
   ownership to find a live editable template entry;
3. select `EDITING_ROUTE_RADIANT_TEMPLATE` only when that ownership is real
   and current;
4. otherwise select `EDITING_ROUTE_DOM_SCRIPT`.

JavaScript-created DOM inside a Lambda application has no template/render-map
ownership and therefore takes the DOM route. No attribute or document-wide
"Lambda document" shortcut is permitted.

Route resolution is read-only and occurs before event notification. The route
snapshot remains stable for the transaction even if notification code changes
the tree; detached/removed-host validation can abort the action, but must not
reroute it to another owner.

### 4.3 Action registration contract

Use an explicit status and observable outcome:

```cpp
enum EditingActionStatus {
    EDITING_ACTION_PASS = 0,
    EDITING_ACTION_CLAIMED,
    EDITING_ACTION_ERROR,
};

struct EditingActionOutcome {
    EditingActionStatus status;
    bool model_reconciled;
    bool selection_changed;
};
```

DOM mutation is deliberately absent from the trusted outcome. The gate derives
it from the document mutation epoch. A handler can report model reconciliation
because a Lambda source/model update may legitimately render to equivalent
DOM.

An internal registration contains:

```cpp
struct EditingActionRegistration {
    uint64_t registration_id;
    const char* handler_id;
    uint32_t route_mask;
    int32_t priority;
    bool (*matches)(const EditingPreparedTransaction*, void*);
    EditingActionOutcome (*handle)(EventContext*,
                                   const EditingPreparedTransaction*,
                                   void*);
    void (*destroy_user)(void*);
    void* user;
    bool tombstoned;
};
```

Implementation requirements:

- built-in registrations use stable handler IDs such as
  `radiant-template` and `dom-compat`;
- handler IDs are copied or lifetime-owned by the registry;
- priority is deterministic and documented;
- two live matching action handlers at the same winning priority are a
  configuration error, not "first array element wins";
- an explicit registration order may break priorities only if the contract
  documents it and the audit record captures it;
- handler selection snapshots one registration ID/pointer before
  `beforeinput`;
- registration/unregistration during dispatch changes the registry generation
  but not the current snapshot;
- unregister tombstones an entry while `dispatch_depth > 0` and frees it when
  the outermost dispatch finishes;
- a snapshotted handler remains callable for the current transaction unless
  its owning document is being destroyed, in which case the transaction
  aborts before action;
- handler calls are synchronous in the first implementation.

### 4.4 Prepared transaction

The preparation stage owns all data needed after arbitrary synchronous
notification code:

```cpp
struct EditingPreparedTransaction {
    uint64_t transaction_id;
    EditingSurface surface;
    EditingRouteSnapshot route;
    EditingIntent intent;
    EditingTargetRange target_ranges[4];
    uint32_t target_range_count;
    EditingSelectionSnapshot selection_before;
    DomNodeRef host_ref;
    uint64_t mutation_epoch_before_notification;
    uint64_t registry_generation;
    uint64_t selected_registration_id;
    const char* selected_handler_id;
};
```

The exact storage form can follow current `EditingIntent` ownership helpers.
The required invariants are:

- strings and transfer payloads outlive both notifications and the action;
- target ranges exposed through `InputEvent.getTargetRanges()` remain valid
  immutable snapshots;
- the action revalidates the live host and boundaries before mutation;
- the host reference detects detachment/destruction without retaining an
  invalid `View*`;
- the selection snapshot records direction and mutation sequence;
- preparation does not mutate the DOM, copy selection for cut, invoke a
  handler, or dispatch an event.

Clipboard/DataTransfer payload preparation happens before the notification so
the `InputEvent` can expose the correct data. Destructive cut behavior does
not happen in preparation.

### 4.5 Structured result

Replace Boolean out-parameters with:

```cpp
struct EditingTransactionResult {
    uint64_t transaction_id;
    EditingRouteKind route;
    bool prepared;
    bool beforeinput_dispatched;
    bool beforeinput_prevented;
    bool beforeinput_mutated_dom;
    bool contract_violation;
    bool action_selected;
    bool action_invoked;
    bool action_claimed;
    bool dom_mutated;
    bool model_reconciled;
    bool selection_changed;
    bool input_dispatched;
    bool unsupported_fallthrough;
    bool failed;
    const char* action_handler_id;
};
```

The result is copied into audit/replay state before any registry snapshot is
released. Callers decide whether to stop later default processing from
specific fields, never from "an event was dispatched."

### 4.6 Narrow notification interface

Replace the rich path's all-in-one `EditingDispatchHooks` with a notification
adapter that does only DOM event construction/dispatch:

```cpp
struct EditingNotificationHooks {
    bool (*dispatch_beforeinput)(EventContext*,
                                 const EditingPreparedTransaction*,
                                 void*);
    void (*dispatch_input)(EventContext*,
                           const EditingPreparedTransaction*,
                           void*);
    void* user;
};
```

`editing_notify_beforeinput()` returns whether the event was prevented.
`editing_notify_input()` has no cancel/result semantic. Both may report an
internal dispatch error separately from web cancellation.

The same low-level DOM `InputEvent` construction can remain reusable by form
controls, but form controls retain their own value action between the two
notifications.

### 4.7 Gate algorithm

Implement one `editing_run_contenteditable_transaction()` or equivalently
named entry with this sequence:

```text
1. Validate target and canonical editable host.
2. Allocate transaction ID.
3. Normalize intent and own payload.
4. Prepare immutable target ranges and selection snapshot.
5. Resolve and snapshot route.
6. Select and snapshot one registered action.
7. Log transaction begin and unsupported-owner state.
8. Record mutation epoch E0.
9. Dispatch cancelable beforeinput.
10. Record mutation epoch E1.
11. If E1 != E0:
      a. mark beforeinput_mutated_dom;
      b. if not canceled, mark contract_violation;
      c. stop before action;
      d. do not synthesize input.
12. If beforeinput was canceled, stop before action and input.
13. Revalidate document, host connection, route owner, and action snapshot.
14. If no action matches, mark unsupported_fallthrough and stop.
15. Invoke exactly one synchronous action.
16. Record mutation epoch E2 and derive dom_mutated = E2 != E1.
17. Verify claimed/model/DOM outcome and selection invariants.
18. If action ERROR or validation fails, mark failed and do not notify input.
19. If claimed || dom_mutated || model_reconciled:
      dispatch one non-cancelable input.
20. Record route/result/audit state.
21. Queue selectionchange and invalidation through existing mechanisms.
22. Release handler snapshot and owned intent data.
```

`beforeinput` mutation policy is intentionally strict for the first gate:

| Listener outcome | Action | Synthetic `input` |
|---|---|---|
| no mutation, not canceled | selected action runs | yes only on claim/change |
| no mutation, canceled | no action | no |
| mutation, canceled | no action; record mutation | no |
| mutation, not canceled | contract violation; no action | no |

The gate does not attempt to continue on re-resolved ranges after a mutating
listener. If real editor traces require that browser behavior later, add it as
a separately tested compatibility mode.

### 4.8 Runtime/event-loop batch

The entire `beforeinput -> action -> input` sequence must run in one retained
document dispatch batch. The current event bridge opens `JsDispatchScope`
around individual built events and performs post-handler recascade/layout and
mutation-record cleanup at scope exit. Opening independent outer scopes for
the two notifications would expose an incorrect intermediate checkpoint.

In `radiant/event.cpp`, add a central wrapper that:

1. resolves the target document/runtime;
2. opens one outer `JsDispatchScope`;
3. calls the runtime-independent gate in `editing_dispatch.cpp`;
4. permits nested `radiant_dispatch_built_event()` calls to reuse the active
   scope;
5. exits once after the post-`input` state is visible;
6. leaves MutationObserver delivery to the normal microtask checkpoint.

Required synchronous order:

```text
beforeinput listener
DOM or template action
input listener (sees post-action state)
outer dispatch scope exit
microtask checkpoint / MutationObserver delivery
queued selectionchange
```

No MutationObserver callback may run between the registered action and
`input`.

### 4.9 Monotonic mutation epoch

Add `uint64_t mutation_epoch` to the document's DOM-JS runtime state:

- increment exactly once for every accepted DOM mutation notification;
- never reset it when mutation records are delivered, cleared, or
  `takeRecords()` is called;
- initialize it at runtime/document creation;
- allow natural unsigned wrap only if equality comparison remains safe for a
  single synchronous transaction;
- expose a read-only internal helper such as
  `js_dom_mutation_epoch(DomDocument*)`;
- ensure native shared DOM mutation primitives notify through the same path.

`mutation_sequence` can retain its observer-record batching meaning.
`mutation_epoch` exists only to answer whether any DOM mutation occurred
between two synchronous gate checkpoints.

### 4.10 Public JavaScript action registration hold point

The internal registry must not be blocked on a new JavaScript global. The
unmodified required editors use standard events and the DOM compatibility
action; they do not need a Radiant registration API.

At the end of Phase 2 choose one of:

1. **Defer the public bridge** for the first editor gate. This is the
   recommended minimal implementation.
2. Add a host-scoped API under an already accepted Radiant host namespace,
   returning an explicit unregister handle.

Do not introduce a new global solely to satisfy the plan. If the bridge is
selected, it must:

- register against one host/document and route mask;
- copy callback roots with `heap_gc_root_slot_new()`;
- unregister the root and release user storage on explicit unregister or
  document destruction;
- be synchronous;
- return a structured status;
- obey dispatch snapshot/tombstone rules;
- reject a Promise return;
- never infer registration from `addEventListener("input", ...)`.

This is the only non-blocking API design decision. It does not affect
CodeMirror, ProseMirror, Editor.js, or the Radiant template action.

---

## 5. Built-in action plan

### 5.1 DOM compatibility action

Create `radiant/editing_dom_handler.cpp` with its module declaration in the
appropriate existing editing header. Register one `dom-compat` action for
`EDITING_ROUTE_DOM_SCRIPT`.

Move the body of `rich_transaction_default_mutate_unscoped()` and its runtime
scope guard out of `event.cpp`; do not retain a second copy. Refactor it around
the prepared target range and canonical live Selection.

The initial action matrix is:

| Intent | Action behavior | Unsupported boundary behavior |
|---|---|---|
| `insertText` | replace supported selected range or insert at text/element boundary; collapse Selection after inserted text | `PASS` |
| `insertReplacementText` | same shared replacement primitive with correct input type/data | `PASS` |
| `deleteContentBackward` | delete selection, or one supported text unit before a collapsed caret in one text run | `PASS` |
| `deleteContentForward` | delete selection, or one supported text unit after a collapsed caret in one text run | `PASS` |
| composition start/update | create/update provisional text range and Selection through shared primitives | `ERROR` only on invariant failure; otherwise claim/change |
| composition commit | convert provisional state to committed text without duplicate model-visible insertion | `PASS` if no active compatible composition |
| composition cancel | remove/restore provisional state and Selection | `PASS` if no active compatible composition |
| line/paragraph | no native mutation | `PASS` |
| history/format | no native mutation | `PASS` |
| cut/paste/drop | no first-gate native mutation unless Phase 0 traces prove a required reusable case | `PASS` |

Add a route-neutral predicate such as
`editing_dom_action_range_supported()` before mutating:

- both range endpoints belong to the snapshotted host;
- neither endpoint is inside a `contenteditable="false"` island;
- the host is connected to the same live document;
- selection deletion does not cross an unsupported structural boundary;
- collapsed character deletion stays in a supported text run;
- the live selection still matches the prepared transaction where required;
- `plaintext-only` drops HTML payload and uses text.

The first implementation may define a "text unit" as the same UTF-16/codepoint
unit already used consistently by the current Selection and text-control
helpers. Do not claim grapheme deletion parity without an explicit focused
test and shared utility. Structural boundary deletion remains editor-owned.

### 5.2 Shared mutation primitive

Extract one shared replace/delete implementation used by:

- the DOM compatibility action;
- JavaScript Range mutation wrappers where semantics match;
- future template DOM reconciliation only when it performs the same DOM
  operation.

Its postconditions are:

1. all live ranges remain valid;
2. the document Selection is collapsed or restored at the specified boundary;
3. character edits emit `characterData` with old value;
4. node insertion/removal emits precise `childList` records;
5. no whole-host replacement record is emitted for a local text edit;
6. DOM wrapper identity outside the changed region remains stable;
7. layout/paint invalidation follows existing shared DOM mutation rules.

If Range deletion spans multiple nodes, reduce it to a focused shared Range
test before enabling it as a DOM default. The editor gate must not use a coarse
tree-replace record as a shortcut.

### 5.3 Composition ownership

Retain the route-neutral composition state-machine invariants and move action
semantics into the DOM handler.

The prepared composition record needs:

- stable transaction/cascade ID;
- host and initial target range;
- current provisional range;
- original replaced content sufficient for cancel;
- current preedit text and caret;
- route/action owner snapshot;
- active document/focus generation.

Composition update replaces only the provisional range. Commit must produce
one committed editor-model change after observer reconciliation; cancel must
restore the pre-composition state without a post-`input` that claims a commit.
Focus change, host detachment, document destruction, or owner change must
cancel safely and release retained state.

Do not implement composition by repeatedly appending independent insertText
transactions.

### 5.4 Radiant template action

Create `radiant/editing_template_handler.cpp`, or an equivalent extraction
that keeps runtime-specific code outside the generic gate. Register one
`radiant-template` action for `EDITING_ROUTE_RADIANT_TEMPLATE`.

Extract/promote the reusable core of `dispatch_lambda_handler()`:

- locate the existing template entry through the prepared route snapshot;
- bind the current runtime/input/event context;
- invoke the editable handler;
- capture dirty/model-version state;
- retransform dirty entries;
- incrementally rebuild the changed output;
- apply pending source Selection to the DOM;
- mark layout/paint invalidation;
- restore runtime context on every exit.

Do not copy this sequence into a second handler function. General non-editing
Lambda event dispatch continues to call the shared invocation machinery where
appropriate.

For migration compatibility, the current Lambda `on beforeinput(evt)` editable
handler is invoked in the **action stage**. It is not a second notification.
The standard DOM `beforeinput` notification has already happened and may
cancel this action.

First-milestone outcome policy:

- missing editable handler: `PASS`;
- handler error: `ERROR`;
- dirty/model version or retransform/rebuild change: `CLAIMED` with
  `model_reconciled=true`;
- unchanged handler result: `PASS`;
- explicit claimed no-op: defer until a real template use case requires a
  language-level return convention.

This deliberately separates "a handler function existed" from "the edit was
handled."

### 5.5 Form controls

Keep these current responsibilities:

- `editing_dispatch_form_beforeinput()`;
- native text-control value mutation;
- text-control selection and history;
- `editing_dispatch_form_input()`;
- form-control composition;
- `had_lambda_keydown` caret repair until separately redesigned.

Only extract a shared DOM notification constructor if doing so leaves form
behavior unchanged. Add form regression tests to every gate-changing phase so
contenteditable work cannot silently change value-backed controls.

---

## 6. Platform event ordering and correlation

### 6.1 Keydown-first rule

For a focused contenteditable host:

```text
dispatch DOM keydown
  -> if canceled, suppress mapped edit and correlated text event
  -> otherwise map special editing key to EditingIntent
  -> run contenteditable gate when an intent is required
  -> dispatch keyup
```

This replaces the current ordering in which a mapped rich intent can enter the
consumer transaction before normal JavaScript `keydown`.

Callers stop later processing when:

- `keydown` was canceled;
- `beforeinput` was canceled;
- the selected action claimed;
- DOM/model state changed;
- policy consumed the key, such as focus traversal.

`beforeinput_dispatched` alone never consumes the key.

### 6.2 Printable key/text correlation

Platform printable input commonly arrives through separate key and character
callbacks. Add an input-cascade correlation ID to the platform/event path or
an equivalent per-document correlation record with these semantics:

1. keydown starts a sequence and records target, key/scancode/modifiers, and
   whether DOM `keydown` was prevented;
2. the matching text-input event consumes the pending sequence;
3. prevented keydown suppresses its character transaction;
4. text input without a key sequence, such as IME/platform injection, starts
   its own sequence;
5. keyup closes any still-pending matching sequence;
6. focus change, composition start, document change, and replay discontinuity
   invalidate stale pending state;
7. only one transaction ID is associated with one logical edit.

Prefer an explicit field on `RdtEvent`/raw replay data if it can be added
without ambiguity. If a platform supplies no ID, maintain at most one pending
printable cascade per document and make that fallback visible in logs.

### 6.3 Clipboard and drag/drop ordering

Use:

```text
paste/drop DOM event
  -> if not canceled, prepare insertFromPaste/insertFromDrop intent
  -> beforeinput
  -> selected action
  -> input on claim/change
```

Copy remains notification plus clipboard serialization, not a DOM mutation.
Cut requires the clipboard event to succeed before any deletion intent.
`ClipboardEvent.clipboardData` and `InputEvent.dataTransfer` share the same
owned payload snapshot.

The pinned editors are expected to cancel and perform their own model/Tool
transaction. Add a no-double-action assertion whenever they do.

### 6.4 Automation/replay parity

Extend `radiant/event_sim.cpp` with browser-style physical typing, without
changing the meaning of legacy `type` fixtures:

```json
{"action":"type_physical","text":"abc"}
```

Each character emits keydown, text input, and keyup through the same public
event route and correlation state as platform callbacks. Special keys continue
through `key_press`/`key_down`/`key_up` or an explicit physical key action.

Recording/replay stores the logical cascade ID and resulting transaction ID.
Replay injects the original platform event, not an action callback.

---

## 7. Audit and replay schema

At the Phase 2 cutover, bump the editable transaction schema. Do not dual-write
old ambiguous fields indefinitely.

Minimum begin record:

- schema version;
- transaction ID and input cascade ID;
- document and stable host identity;
- normalized intent/input type;
- payload metadata, with current password/redaction policy;
- route;
- registry generation;
- selected registration ID/handler ID;
- target range count and Selection snapshot;
- mutation epoch before notification.

Minimum result record:

- `beforeinput_dispatched`;
- `beforeinput_prevented`;
- `beforeinput_mutated_dom`;
- `contract_violation`;
- `action_selected`;
- `action_invoked`;
- `action_claimed`;
- `dom_mutated`;
- `model_reconciled`;
- `selection_changed`;
- `input_dispatched`;
- `unsupported_fallthrough`;
- `failed`;
- final mutation/selection generations.

Remove `lambda_handled`, `dispatch_input_without_mutation`, and Boolean
interpretation aliases after in-tree recordings migrate. Reject a pre-cutover
recording with a clear schema mismatch unless an intentional offline converter
is supplied.

`assert_editing_event` should filter and assert at least:

- transaction/cascade ID;
- route;
- handler/action owner;
- notification cancellation;
- claim;
- DOM/model mutation;
- `input` dispatch;
- unsupported fall-through;
- contract violation;
- exact ordering among correlated records.

---

## 8. Test and package layout

### 8.1 Keep upstream Editor.js distinct

`test/editor-js` is Lambda's custom `@lambda/editor-js` package. It is not the
upstream block editor and must not be renamed or reused for this gate.

Create a separate compatibility package, recommended:

```text
test/editable-editors/
  package.json
  package-lock.json
  build/
  src/
    codemirror-entry.js
    prosemirror-entry.js
    editorjs-entry.js
  fixtures/
    codemirror/
    prosemirror/
    editorjs/
    shared/
  capability-manifest.json
  README.md
```

Commit exact versions and the lockfile. `package.json` uses exact versions,
not `^` or `~`. Generated browser bundles are checked in or built by an
explicit maintenance target; normal baseline execution does not download
packages.

The existing `test/js/codemirror6.min.js` bundle currently identifies
CodeMirror 6.0.2, `@codemirror/state` 6.7.1, and
`@codemirror/view` 6.43.6. Either reuse those exact versions for the expanded
probe or intentionally repin all CodeMirror artifacts together and record the
change in the manifest.

### 8.2 Capability manifest

`capability-manifest.json` is test evidence, not a marketing list. Each row
contains:

```json
{
  "capability": "mutation.characterData.oldValue",
  "editor": "codemirror",
  "version": "@codemirror/view@6.43.6",
  "required": true,
  "status": "implemented|partial|missing|unprobed|excluded",
  "fixture": "fixtures/codemirror/typing.html",
  "focused_test": "test_wpt_input_events_gtest/...",
  "notes": ""
}
```

Rows cover the design matrix: host/focus, events, InputEvent, DOM action,
MutationObserver, Selection/Range, geometry, clipboard/drop, composition,
parsing/serialization, scheduling, and teardown. A production capability is
not added only to turn a manifest cell green; the evidence rule in the design
contract still applies.

### 8.3 Test pyramid

| Level | Purpose | Representative suites |
|---|---|---|
| L1 native unit | registry selection, route snapshot, result state machine, mutation epoch, range predicates, input correlation | new focused GTests plus existing DOM/Selection/InputEvent suites |
| L2 document JS | event order/cancellation, MutationObserver records, Range behavior, command absence, lifetime cleanup | `test/js` or document golden fixtures |
| L3 headless UI | real focus, keyboard/text correlation, mouse selection, clipboard, composition, geometry | `test/ui` event simulation |
| L4 upstream editor E2E | prove authoritative editor state and public DOM compatibility | CodeMirror, ProseMirror, upstream Editor.js, Radiant template |
| Regression | preserve forms, layout, DOM and old editor smoke behavior | Radiant baseline, DOM UI, current editor-4c suites |

No phase is complete only because an upstream editor happens to render. Every
editor-observed gap needs a focused test at L1–L3.

### 8.4 Authoritative-state probes

**CodeMirror**

- expose test-only reads of `view.state.doc.toString()` and selection;
- type and replace text;
- Backspace/Delete inside text and at an editor-owned boundary;
- Enter through CodeMirror keymap;
- copy/cut/paste;
- composition update/commit/cancel;
- undo/redo through CodeMirror history;
- mouse caret/selection and scroll/geometry;
- destroy the view and assert no further callbacks.

**ProseMirror**

- expose `view.state.doc.toJSON()` and selection;
- use a pinned schema, `baseKeymap`, and history in light DOM;
- type, select/replace, split/join/list boundary behavior through commands;
- marks/block commands through ProseMirror transactions;
- clipboard plain/HTML and DOM parser/serializer;
- composition and observer reconciliation;
- selection mapping and geometry;
- destroy the view.

Do not run the Safari ShadowRoot fallback.

**Editor.js**

- expose `editor.save()` results and current block identity;
- use core plus paragraph, heading, list, atomic/media, and one modern
  Range-based inline Tool;
- exclude stock/third-party Tools that require `execCommand`;
- type and replace inside Tool content;
- block Enter, merge/removal, navigation, paste sanitization;
- MutationObserver/change notification;
- toolbar geometry and focus;
- save/reload and destroy.

**Radiant template**

- expose authoritative source/model and projected DOM Selection;
- prove DOM `keydown -> beforeinput -> action -> input` order;
- cancel standard `beforeinput`;
- handler missing/no-op/change/error outcomes;
- source retransform/rebuild and selection projection;
- mixed document with an independent JavaScript editor host;
- replay to the same final source/model state.

### 8.5 Proposed make targets

Add targets with names that cannot be confused with Lambda's custom editor:

```text
make editable-unit
make editable-ui
make editable-editor-e2e
make test-editable

# record then replay the same mixed-route session from clean startup
./lambda.exe view test/ui/editable-mixed-routes.ls --event-file test/ui/editable-mixed-routes.json --headless --event-log --no-log
./lambda.exe replay --event-log temp/events_<pid>_editable-mixed-routes.ls.jsonl --assert-state --headless
make test-editable
```

`test-editable` aggregates focused editing tests, headless fixtures, the four
editor routes, and form-control regressions. Once stable, attach it to the
appropriate Radiant baseline; do not make package installation part of that
target.

---

## 9. Phased implementation

Each phase is intended to be reviewable and green except for explicitly
checked-in red acceptance tests. Avoid one repository-wide transaction rewrite.

## Phase 0 — Freeze baseline and add red evidence

No production behavior changes in this phase.

### 0.1 Source and symbol inventory

Record current callers/definitions for:

```text
element_has_data_editable
EDIT_SURFACE_LAMBDA_TEMPLATE
editing_dispatch_beforeinput
editing_dispatch_beforeinput_ex
dispatch_input_after
dispatch_input_without_mutation
out_lambda_handled
lambda_handled
dispatch_rich_consumer_transaction
dispatch_rich_transaction_defaultable
rich_transaction_default_mutate
js_dom_testdriver_rich_mutate
execCommand
queryCommand
```

Also inventory direct `contenteditable`/`data-editable` host parsers, the
duplicate text-descendant traversal helpers, `rich_transaction_*` state fields,
and all active/retired fixtures that assert native structural mutation.

The inventory becomes the Phase 8 deletion checklist. Do not delete substrate
solely because its current name contains `rich`.

### 0.2 Capture current transaction behavior

Add focused tests for both DOM and template routes covering:

- dispatched but not canceled;
- canceled `beforeinput`;
- Lambda handler exists and changes;
- Lambda handler exists and does nothing;
- native callback mutates;
- no owner/no mutation;
- current `input` dispatch conditions;
- listener mutates during `beforeinput`;
- host removal during `beforeinput`;
- registration-equivalent re-entrant event dispatch.

Tests must distinguish dispatched, canceled, handler found, claimed, DOM
mutated, model changed, and `input` dispatched even if current public code
cannot yet report each distinction cleanly.

### 0.3 Capture event order

For DOM and template contenteditable, record:

- printable key;
- Backspace/Delete;
- Enter;
- undo/redo shortcut;
- paste/cut/drop;
- composition start/update/end.

Trace DOM keydown/keyup, clipboard/composition events, beforeinput/input,
Selection, observer delivery, and current Lambda handler invocation.

### 0.4 Pin editor packages and traces

- preserve the current CodeMirror smoke fixtures;
- add the separate upstream editor compatibility package;
- pin ProseMirror, Editor.js, and their selected plugins/Tools;
- capture construction-time missing APIs before changing production DOM;
- capture event/mutation traces for every required acceptance row;
- mark unexpected native intent fall-through in the manifest.

Use focused Chromium comparisons where the correct checkpoint or record shape
is unclear. Store summarized expected traces in the repository; do not require
Chromium or network access for normal tests.

### 0.5 Red tests

Add expected-failing tests for:

- DOM `keydown` preceding the contenteditable transaction;
- prevented keydown suppressing correlated text input;
- one action owner;
- pure notification functions;
- monotonic mutation epoch across observer record resets;
- precise Range mutation records;
- simple backward/forward deletion;
- composition provisional DOM;
- `execCommand`/`queryCommand*` absence;
- upstream editor authoritative-state matrices.

### Phase 0 exit gate

- old baseline is green;
- red tests fail for named, expected reasons;
- capability gaps are separated into editable action versus ordinary DOM
  defects;
- package versions and supported configurations are frozen;
- no new production behavior has landed.

---

## Phase 1 — Pure notification and structured gate skeleton

### 1.1 Add mutation epoch first

In the DOM-JS runtime:

- add and initialize `mutation_epoch`;
- increment it in the single accepted mutation notification entry;
- add focused reset/`takeRecords()` tests;
- verify script mutations, native DOM mutations, and template rebuild
  notifications all advance it.

This lands before enforcing the mutating-beforeinput policy.

### 1.2 Add prepared/result structs

In `radiant/event.hpp` or a route-neutral editing header:

- add `EditingPreparedTransaction`;
- add `EditingTransactionResult`;
- add route/result name helpers for logs;
- add intent ownership cleanup helpers if current `EditingIntent` lifetime is
  insufficient;
- keep an internal transitional adapter only while callers migrate.

New tests use the structured result immediately. Do not add new callers of the
old Boolean out-parameters.

### 1.3 Extract notifications

From `editing_dispatch_beforeinput_ex()`:

- move only standard DOM `InputEvent` construction/dispatch into
  `editing_notify_beforeinput()`;
- move only standard DOM post-action construction/dispatch into
  `editing_notify_input()`;
- remove the `dispatch_input_after` behavior from the new functions;
- keep InputEvent target ranges, data, dataTransfer, composing state,
  bubbling, cancelability, and composed behavior conformant.

The old function may temporarily call the new notification function, but the
new function must never call the old one.

### 1.4 Add gate skeleton

Implement the §4.7 sequence with a temporary action adapter for current
behavior. Enforce:

- cancel stops action;
- mutating uncanceled beforeinput is a contract violation;
- one post-`input` only after a verified claim/change;
- result fields are complete on all early exits;
- cleanup happens on error, re-entrancy rejection, and detached host.

Wrap the full gate in the outer runtime dispatch scope from §4.8.

### 1.5 Preserve form controls

Add/retain focused form tests for typing, selection replacement, Backspace,
paste, IME, history, cancellation, and Lambda keydown caret repair.

### Phase 1 exit gate

- direct tests prove notification functions cannot invoke an action;
- `preventDefault()` suppresses action and post-`input`;
- beforeinput DOM mutation is detected across observer record resets;
- `input` sees post-action state and is non-cancelable;
- observer delivery remains after the synchronous sequence;
- form-control regressions are green;
- existing DOM/template paths still operate through the transitional adapter.

---

## Phase 2 — Per-document registry and one-owner arbitration

### 2.1 Registry lifecycle

Implement the document resource described in §4.1:

- create lazily or at document-runtime initialization;
- add/register/unregister/find/snapshot APIs;
- maintain generation and registration IDs;
- implement dispatch depth and tombstone reclamation;
- release user storage and JS roots at document destruction;
- make destruction idempotent.

Add unit tests for:

- deterministic priority;
- no match;
- one match;
- two equal-priority matches rejected;
- unregister before dispatch;
- self-unregister during action;
- unregister another handler during beforeinput;
- register a higher-priority handler during beforeinput;
- nested transaction;
- document destruction after registration;
- iframe/secondary document registry isolation.

### 2.2 Built-in registrations

Register `dom-compat` and `radiant-template` at document initialization or
lazy first use. Their route masks prevent both from matching one transaction.
Do not encode route selection in priority.

For this phase, action bodies may still adapt current behavior; the important
gate is registry selection and structured outcomes.

### 2.3 Snapshot before notification

Snapshot registration ID, handler ID, callback/user pointer, registry
generation, and route. Increment dispatch depth before exposing the transaction
to notification code. Release/reclaim after result logging.

If host removal or document destruction invalidates the transaction, abort
before action. If a registration is merely unregistered during notification,
the snapshotted action remains valid through tombstoned lifetime.

### 2.4 Audit schema cutover

- bump schema;
- log route/owner/result fields;
- migrate active replay fixtures;
- reject old schema clearly;
- migrate `assert_editing_event`;
- stop adding old log fields.

Actual deletion of old aliases waits until Phase 8.

### 2.5 JavaScript bridge hold

Decide whether to defer or expose public JS registration. Record the decision
in the implementation log. Deferral is acceptable and does not block the
phase.

### Phase 2 exit gate

- every contenteditable transaction snapshots at most one action;
- registry changes during callbacks are deterministic and memory-safe;
- DOM and template smoke tests run through registered built-ins;
- result/audit/replay identify the selected owner without legacy Booleans;
- document teardown leaves no registered callback or GC root.

---

## Phase 3 — Standard host classification and context routing

### 3.1 Canonical editing host

Refactor `radiant/editing.cpp` so standard `contenteditable` state is the only
rich-host marker:

- support normal true/empty and `plaintext-only` behavior already accepted by
  Radiant;
- honor inherited editability;
- stop at nested editing hosts;
- detect `contenteditable="false"` islands;
- keep text controls as a distinct surface kind.

Promote one route-neutral text-descendant traversal helper and remove the
duplicate static implementation after all callers migrate.

### 3.2 Template ownership resolver

Add a read-only resolver that:

- starts at the canonical host/target;
- uses existing runtime/template registry state;
- uses render-map reverse ownership;
- requires a live editable template entry;
- snapshots the exact entry/generation needed by the action;
- returns no template route for arbitrary JS-created descendants.

Reuse existing template lookup helpers from `dispatch_lambda_handler()` where
possible. Promote helpers instead of duplicating its ancestor walk.

### 3.3 Remove marker routing from active paths

Convert direct checks in:

- `radiant/event.cpp`;
- `radiant/dom_range_resolver.cpp`;
- caret eligibility/hit testing;
- target-range preparation;
- active test fixtures.

Do not physically remove every legacy symbol until mixed routing and old
callers are green, but no new transaction may consult `data-editable`.

### 3.4 Mixed-document tests

One retained Lambda document must contain:

- a Radiant-owned `contenteditable` template surface;
- a direct JavaScript editor-created `contenteditable`;
- a non-editable island in each;
- a nested/non-owner node whose ancestor resolves correctly.

Assert separate route/action owners, correct source/DOM outcomes, and no
cross-reconciliation.

### Phase 3 exit gate

- all active rich routing uses standard `contenteditable`;
- route is derived per host, not per document;
- mixed template and JS editors work in one document;
- caret/range resolution never enters false islands;
- no action is selected from `data-editable`.

---

## Phase 4 — Browser keyboard order and logical input cascades

### 4.1 Reorder event dispatch

In `radiant/event.cpp`:

- dispatch DOM `keydown` before mapping/running a contenteditable edit;
- preserve normal capture/target/bubble semantics;
- honor `defaultPrevented`;
- replace `break`/consume decisions based only on consumer dispatch success
  with structured-result predicates;
- retain unrelated keyboard policy such as focus traversal and form-control
  behavior.

Adopt the same order for the template route, guarded by dedicated regressions.

### 4.2 Correlate key and text events

Implement §6.2 across platform callbacks, `RdtEvent`, raw logging/replay, and
the per-document interaction state. Test:

- printable keydown + char + keyup creates one edit;
- prevented keydown suppresses char;
- repeated key;
- dead-key/text without normal key mapping;
- focus changes between key and char;
- composition interrupts pending printable input;
- replay produces identical transaction/cascade IDs or an explicit stable
  remapping.

### 4.3 Extend event simulation

Add `type_physical` and structured correlation assertions. Keep legacy `type`
unchanged so existing fixtures do not gain surprise keyboard events.

### 4.4 Editor keymap probes

Enable key-order cases for:

- CodeMirror Enter/history/selection commands;
- ProseMirror base keymap joins/splits/history;
- Editor.js block Enter/merge/removal/navigation;
- Radiant template keydown cancellation.

Assert that a canceled key produces no later `beforeinput` or DOM fallback.

### Phase 4 exit gate

- observable order is `keydown` before contenteditable transaction on both
  routes;
- cancellation suppresses the correlated edit;
- one physical printable key yields one logical edit;
- required editor keymaps receive and own structural commands;
- existing form and shortcut regressions remain green.

---

## Phase 5 — DOM-compatible text, deletion, and composition

Implement one intent family at a time. Each family lands with focused Range,
Selection, InputEvent, observer, and editor tests.

### 5.1 Move insertion/replacement

- move the current `rich_transaction_default_mutate_*` implementation into
  `editing_dom_handler.cpp`;
- replace `RichDefaultTransactionArgs` and fallback view/offset with the
  prepared range;
- use shared text/Range mutation primitives;
- support text-node and valid element-boundary insertion;
- collapse Selection after inserted text;
- preserve `plaintext-only` filtering;
- emit precise mutation records.

Delete the old body once all callers use the registered action.

### 5.2 Selected-range deletion

- validate both endpoints within one host;
- reject false-island and unsupported structural crossings;
- preserve live Range rules;
- emit the actual sequence of characterData/childList records;
- collapse Selection at the correct boundary;
- test forward/backward Selection direction.

Do not substitute a whole-host tree-replace record.

### 5.3 Collapsed backward/forward deletion

- support one text unit within a single text run;
- handle start/end offsets safely;
- return `PASS` at a structural, atomic, nested-host, or false-island boundary;
- let editor keymaps own those boundary operations;
- assert no `input` for `PASS`.

### 5.4 Deferred/unsupported actions

Add explicit result/log cases and tests:

- line break/paragraph: `PASS`;
- word/line deletion outside the supported character operation: `PASS`;
- history/format: `PASS`;
- unimplemented paste/drop/cut mutation: `PASS`.

Every required editor fixture treats an unexpected occurrence as a failure
that prints route, owner, intent, target range, and event order.

### 5.5 Composition

Implement provisional update, repeated replacement, commit, and cancel using
the state described in §5.3. Add cases for:

- collapsed start;
- selection replacement;
- multiple updates;
- caret in preedit text;
- commit;
- cancel;
- focus transfer;
- host removal;
- document destroy;
- editor redraw during composition;
- exactly one authoritative editor-model commit.

### 5.6 Invalidation

Replace coarse `force_full_reflow` assumptions with the existing DOM mutation
and layout invalidation path where possible. Verify post-action `input`
listeners read the new DOM immediately and synchronous geometry reads become
fresh through the normal committed-geometry mechanism.

### Phase 5 exit gate

- the registered DOM action handles only the declared minimum matrix;
- unsupported structural operations do not mutate or emit `input`;
- text insertion, replacement, selected deletion, simple character deletion,
  and composition have correct Selection and observer records;
- CodeMirror, ProseMirror, and Editor.js traces show no unexpected unsupported
  fall-through for required operations;
- no insertion implementation remains duplicated in `event.cpp`.

---

## Phase 6 — Shared DOM, observer, geometry, and lifecycle hardening

### 6.1 MutationObserver conformance

Harden shared DOM/Range APIs, especially:

- characterData target and old value;
- childList added/removed nodes, previous/next sibling;
- subtree observation;
- attribute observation reached by Editor.js/ProseMirror;
- record batching/order;
- `takeRecords()` and `disconnect()`;
- observer removal/editor destruction;
- mutation during notification/action/input;
- no delivery between action and post-`input`.

All fixes belong in ordinary DOM primitives or observer infrastructure, not
in an editor-name adapter.

### 6.2 Selection/Range conformance

Verify:

- live Range boundary adjustment for text replace and node insertion/removal;
- `Selection.extend()` and `setBaseAndExtent()`;
- directional Selection;
- `StaticRange` target snapshots;
- selectionchange coalescing/order;
- false-island and atomic boundaries;
- host removal/detached ranges;
- stable wrapper identity through editor redraw.

Audit structural branches in `editing_target_range.cpp`. Retain branches used
for standards-compatible target notification or current selection substrate;
delete only branches proven to exist solely for retired native mutations in
Phase 8.

### 6.3 Geometry and scheduling

Verify editor-reached:

- Range rectangles;
- element rectangles and hit testing;
- caret point mapping;
- `scrollIntoView`;
- scroll/client metrics;
- synchronous reads after action/editor redraw;
- requestAnimationFrame/timer/microtask measurement scheduling.

Use existing geometry freshness entry points. Do not introduce an
editable-only layout flush.

### 6.4 DOM parsing/serialization and clipboard

Reduce any editor failures to shared tests for:

- DOMParser/detached document fragments;
- HTML parse/serialize;
- clone/import/adopt behavior reached by the pinned package;
- plain/HTML ClipboardEvent/DataTransfer consistency;
- paste/drop cancellation and no second action.

Do not add general rich-paste normalization to the DOM action.

### 6.5 Lifecycle matrix

Test all built-in and optional registrations across:

- view/editor destroy;
- host removal during keydown/beforeinput/action/input;
- focus/blur;
- iframe or second document;
- document navigation/destroy;
- observer disconnect;
- composition cancellation;
- nested/re-entrant input.

Run leak/root assertions available in the JS runtime. Every off-heap JS
callback slot must be unrooted exactly once.

### Phase 6 exit gate

- focused observer/Selection/Range/geometry/clipboard tests are green;
- no stale geometry or intermediate observer checkpoint is visible;
- detached targets cannot receive an action;
- all registry/callback/composition resources are released;
- fixes are shared web-platform behavior, not editor branches.

---

## Phase 7 — Upstream editor and Radiant-template end-to-end gate

### 7.1 CodeMirror suite

Upgrade the existing smoke coverage to the full required matrix and assert
`EditorState` text/selection. Run through physical keyboard, mouse,
clipboard, composition, history, scrolling, and destroy paths.

### 7.2 ProseMirror suite

Run pinned light-DOM ProseMirror with schema, `baseKeymap`, and history. Assert
state JSON, selection, parsed clipboard content, model-owned structural
commands, composition, geometry, and destroy. Mark ShadowRoot Safari fallback
as excluded in the manifest rather than skipping an unexplained failure.

### 7.3 Editor.js suite

Run upstream Editor.js core and pinned compatible Tools. Assert saved block
data and Tool DOM. Include block creation/merge/removal, clipboard-event
routing, change observation, toolbar geometry, reload, and destroy. Ordinary
paragraph native-default paste is an explicit manifest exclusion, not a hidden
partial fallback. Assert legacy-command APIs are absent and excluded Tools are
not part of the supported configuration.

### 7.4 Radiant template suite

Run equivalent typing, deletion, Enter owned by Lambda action, selection,
clipboard, composition, history if template-owned, reconciliation, geometry,
and replay tests. It must request no private DOM editing API.

### 7.5 Cross-route and replay suite

- host both route kinds in one document;
- interleave focus and edits;
- record transactions;
- replay from clean startup;
- compare authoritative editor/source state, DOM, Selection, route, owner, and
  outcome records.

### 7.6 Baseline integration

Add the new make targets. Keep package rebuild/update as a maintenance command.
Once stable, include the offline E2E bundle in the Radiant baseline at an
appropriate duration tier.

### Phase 7 exit gate

Every required row in the design acceptance matrix passes for:

- CodeMirror;
- ProseMirror within documented limits;
- upstream Editor.js with supported Tools;
- Radiant template editing.

A render-only or typing-only smoke pass is not sufficient.

---

## Phase 8 — Transitional deletion and documentation cutover

Delete in dependency order after the Phase 7 gate is green.

### 8.1 Host/routing cleanup

Remove:

- `element_has_data_editable()`;
- `data-editable` route branches;
- `EDIT_SURFACE_LAMBDA_TEMPLATE` and its name/log case;
- direct marker parsing in `event.cpp` and `dom_range_resolver.cpp`;
- active fixture references to `data-editable`.

Require one canonical host parser after deletion.

### 8.2 Dispatch/result cleanup

Remove:

- unused `editing_dispatch_beforeinput()`;
- `editing_dispatch_beforeinput_ex()`;
- `dispatch_input_after`;
- rich-path Lambda/clipboard members of `EditingDispatchHooks`;
- `dispatch_editing_lambda_event()`;
- `dispatch_editing_copy_selection()` where no retained caller exists;
- the all-in-one `dispatch_editing_hooks()` factory;
- `out_lambda_handled`;
- `lambda_handled`;
- `dispatch_input_without_mutation`;
- `dispatch_rich_consumer_transaction_operation()`;
- `dispatch_rich_consumer_transaction()`;
- `dispatch_rich_transaction_defaultable()`;
- `RichDefaultTransactionArgs`.

Keep the form notification path and unrelated clipboard APIs.

### 8.3 Testdriver and command cleanup

Remove:

- `JsDomTestdriverMutationArgs`;
- `js_dom_testdriver_rich_mutate()`;
- the testdriver's hand-built `EditingTransaction`;
- inert `execCommand` and `queryCommand*` method/property table entries.

Tests assert:

```js
typeof document.execCommand === "undefined"
typeof document.queryCommandEnabled === "undefined"
typeof document.queryCommandState === "undefined"
typeof document.queryCommandSupported === "undefined"
typeof document.queryCommandValue === "undefined"
```

Use the actual binding's absence convention if it differs, but never expose a
callable false-returning stub.

### 8.4 State and helper cleanup

- rename transaction state/FSM/log symbols only where `rich` encodes the
  removed ownership model;
- preserve re-entrancy, target-range, Selection generation, and composition
  invariants;
- consolidate `editing_rich_find_text_descendant()` and
  `target_range_find_text_descendant()` into one route-neutral helper;
- audit `editing_target_range.cpp` branches by live caller and trace before
  removing structural logic;
- do not delete caret geometry, Selection/Range, clipboard/DataTransfer,
  drag/drop, or observer substrate.

### 8.5 Fixture cleanup

- replace and remove `test/ui/editor4b/phase3-no-native-edit.json`;
- extract still-valid substrate assertions from
  `test/ui/_retired_native_editing/`;
- delete the obsolete archive only after equivalent active coverage exists;
- remove stale `test/dedup/exclude.json` entries naming removed helpers;
- preserve `test/editor-js` as the distinct Lambda custom-editor project.

### 8.6 Documentation cutover

Update:

- `doc/dev/radiant/RAD_18_Editing_Selection_Ranges.md`;
- `doc/dev/radiant/RAD_19_Form_Controls.md`;
- `doc/dev/radiant/diagram/rad18_dispatch_seam.mmd`;
- active Stage 5 editing material that still prescribes `data-editable`;
- capability/support tables for editor configurations and exclusions.

Add short superseded banners to historical `vibe/editing/` records rather than
rewriting history.

### Phase 8 exit gate

These searches return no active source/fixture dependencies, except text in
historical or migration documentation explicitly describing removal:

```bash
rg -n "data-editable|EDIT_SURFACE_LAMBDA_TEMPLATE" radiant lambda/js test --glob '!test/editable-editors/build/**' --glob '!test/editable-editors/node_modules/**'
rg -n "editing_dispatch_beforeinput_ex|dispatch_input_without_mutation" radiant lambda/js test --glob '!test/editable-editors/build/**' --glob '!test/editable-editors/node_modules/**'
rg -n "out_lambda_handled|js_dom_testdriver_rich_mutate" radiant lambda/js test --glob '!test/editable-editors/build/**' --glob '!test/editable-editors/node_modules/**'
rg -n "execCommand|queryCommand" lambda/js test/editable-editors --glob '*.{c,cc,cpp,h,hpp,js,mjs,html,json,md}' --glob '!test/editable-editors/build/**' --glob '!test/editable-editors/node_modules/**'
```

There is one classifier, one gate, one registry, one notification pair, and
one structured result schema.

---

## 10. File-level change checklist

| File or area | Planned changes | Primary phase |
|---|---|---:|
| `radiant/event.hpp` | route, prepared transaction, registration, action outcome, structured result, notification hook contracts | 1–2 |
| `radiant/editing.cpp` | canonical standard contenteditable surface; route-neutral traversal; template-context route lookup helpers | 3 |
| `radiant/editing_dispatch.cpp` | preparation, action snapshot/arbitration, pure notifications, result verification, audit entry | 1–2 |
| `radiant/editing_dom_handler.cpp` | new built-in minimal DOM action, moved insertion helper, deletion/composition | 2, 5 |
| `radiant/editing_template_handler.cpp` | new/extracted template action adapter using shared Lambda invocation/retransform code | 2–3 |
| `radiant/editing_intent.cpp` | normalization only; no action or event dispatch | audit in 1 |
| `radiant/editing_target_range.cpp` | immutable notification targets; action-supported predicate separation; later structural branch audit | 1, 5, 8 |
| `radiant/dom_range.cpp` and headers | shared mutation-aware Range primitives and live-range invariants | 5–6 |
| `radiant/dom_range_resolver.cpp` | canonical host lookup; remove custom marker parser | 3 |
| `radiant/event.cpp` | outer JS dispatch batch, keydown-first ordering, gate adapter, built-in setup, removal of old wrappers | 1–5, 8 |
| `radiant/state_machine.cpp` / `state_schema.cpp` | correlation and route-neutral transaction/composition invariants; schema migration | 2, 4, 8 |
| `radiant/event_sim.cpp` / `.hpp` | physical typing, correlation, structured result/order assertions | 0, 4 |
| `lambda/js/js_dom.cpp` | monotonic mutation epoch integration as appropriate, testdriver cleanup, legacy command removal, optional JS registration bridge | 1–2, 8 |
| `lambda/js/js_dom_selection.cpp` | Range wrapper observer conformance | 6 |
| `lambda/js/js_dom_observers.cpp` | observer record/checkpoint/lifecycle conformance | 6 |
| build config / `Makefile` source lists | include new native files and test targets through `build_lambda_config.json`/generated build process | 2, 7 |
| `test/editable-editors/` | exact packages, offline bundles, capability manifest, authoritative-state probes | 0, 7 |
| `test/ui/` and focused GTests | gate/order/mutation/selection/composition/mixed-route tests | all |
| `doc/dev/radiant/`, `vibe/editing/` | current docs and historical supersession | 8 |

When adding new C+ source files, update `build_lambda_config.json` and run the
normal generator. Do not manually edit generated `.lua` build files.

---

## 11. Verification commands and gates

Run focused commands throughout instead of waiting for the final phase. Exact
GTest filters should be added as the test names land.

### 11.1 Focused build/tests

```bash
make build
make build-test
./test/test_wpt_input_events_gtest.exe
./test/test_wpt_selection_gtest.exe
./test/test_dom_range_gtest.exe
./test/test_ui_automation_gtest.exe
make dom-ui
make editor-4c-view
make editable-unit
make editable-ui
make editable-editor-e2e
```

The existing custom `editor-4c` tests remain regressions; they do not replace
the upstream editor suite.

### 11.2 Baseline and lint

```bash
make test-radiant-baseline
make lint ARGS='--rule ^no-int-cast-radiant$'
make lint
make check-radiant-dup
```

If a target name changes during implementation, update this plan and the
implementation log rather than leaving stale commands.

### 11.3 Per-phase required assertions

| Phase | Must be green before proceeding |
|---:|---|
| 0 | current Radiant baseline plus intentionally red named acceptance tests |
| 1 | notification/result/mutation-epoch tests and form controls |
| 2 | registry/re-entrancy/lifetime and new audit/replay schema |
| 3 | canonical host, false-island, mixed-route tests |
| 4 | physical input ordering/correlation and editor keymaps |
| 5 | text/delete/composition plus precise Selection/observer results |
| 6 | shared DOM/Range/MO/geometry/lifecycle conformance |
| 7 | four authoritative editor matrices and replay |
| 8 | legacy symbol audit, full baseline, lint, dedup |

---

## 12. Review checkpoints

Use explicit review checkpoints so scope does not expand silently.

### Checkpoint A — after Phase 0

Review the pinned package versions, capability manifest, browser trace
summaries, and supported Tool/plugin configurations. Any new DOM API proposal
must name its focused failing trace.

### Checkpoint B — after Phase 2

Review registry lifetime, arbitration, result semantics, and log schema.
Resolve whether the public JavaScript registration bridge is deferred. This
decision does not reopen the gate architecture.

### Checkpoint C — after Phase 4

Review keyboard/correlation ordering on platform and replay paths, especially
template compatibility and prevented printable keys.

### Checkpoint D — after each Phase 5 intent family

Review supported range boundaries and precise observer records. Do not enable
the next intent family by widening a generic "rich default."

### Checkpoint E — before Phase 8 deletion

Review the complete acceptance matrix, form regressions, fixture extraction,
and live callers of target-range/state helpers. Only then physically delete
transitional code and archives.

---

## 13. Risk register

| Risk | Failure mode | Mitigation / proving test |
|---|---|---|
| Handler result trusted over DOM reality | false claim or missed mutation produces wrong `input` | monotonic mutation epoch around notification/action |
| Listener mutates prepared range | second edit at stale boundary | strict mutate-and-not-cancel contract violation |
| Registry callback freed during dispatch | use-after-free | dispatch depth, tombstones, document resource lifetime |
| Route changes after DOM mutation | template and DOM action both run or wrong owner | pre-notification route/action snapshot plus live-owner validation |
| Keydown and char are uncorrelated | duplicate typing or canceled key still inserts | explicit input cascade state and physical typing tests |
| Structural target range authorizes native edit | accidental restoration of browser-rich engine | independent `editing_dom_action_range_supported()` |
| Coarse observer records | CM/PM parse wrong region or retain stale nodes | precise shared Range mutation tests with old values/siblings |
| Notification scope exits early | MutationObserver runs before `input` | one outer `JsDispatchScope` for full transaction |
| Template no-op treated as handled | lost input with no model change | missing/no-op returns `PASS`; explicit change detection |
| Editor DOM fixture passes while model diverges | false compatibility result | assert editor authoritative state/data and DOM Selection |
| Editor.js package name confusion | tests validate Lambda custom editor only | distinct `test/editable-editors` package and target names |
| `execCommand` stubs trigger feature detection | editor/plugin selects a broken path | remove bindings and assert absence |
| Form behavior regresses | contenteditable refactor breaks value controls | dedicated form matrix in every gate phase |
| Cleanup deletes shared substrate | Selection/geometry/clipboard regression | live-caller audit and assertion extraction before deletion |
| Package drift | passing behavior cannot be reproduced | exact versions, lockfile, checked-in offline bundle |

---

## 14. Implementation log template

Maintain this section during execution. One row represents a landed,
independently reviewable change.

| Date | Phase | Change | Tests/evidence | Follow-up |
|---|---:|---|---|---|
| 2026-07-29 | 0 | Added the distinct offline `test/editable-editors/` package with exact package versions, committed bundles, fixtures, and a capability manifest. | Focused CodeMirror, ProseMirror, and Editor.js fixtures publish editor-owned state/data. | Package rebuild stays an explicit maintenance action; baseline targets never run npm. |
| 2026-07-29 | 1–2 | Added prepared/result contracts, monotonic mutation-epoch checks, pure `editing_notify_beforeinput()`/`editing_notify_input()` calls, and the per-document `EditingActionRegistry`. | Gate fixtures cover cancellation, DOM mutation, unsupported transfer fall-through, and structured audit fields. | The public JavaScript registration bridge is deferred; no pinned editor needs it. |
| 2026-07-29 | 3–4 | Replaced marker routing with canonical `contenteditable` ownership routing and added physical key/text correlation in platform simulation and replay. | False-island, mixed-template/DOM, and canceled-physical-key fixtures pass. | Route resolution remains per host and snapshots before notification. |
| 2026-07-29 | 5–6 | Moved the narrow DOM text/delete/composition action into `editing_dom_handler.cpp`; added `radiant-template` as the separate registered action; hardened source-position conversion for exact Decimal coordinates. | DOM text/selection/composition fixtures and `SourcePosBridgeMarkBuilder.*` pass. | Structural defaults remain explicit `PASS`; no browser-owned rich engine was restored. |
| 2026-07-29 | 7 | Added offline authoritative-state E2E matrices: CodeMirror and ProseMirror selection/history/clipboard/composition/teardown; Editor.js block operations, tool data, toolbar geometry, reload, and destroy; template/DOM mixed replay. | `make test-editable`; recorded mixed-route session replayed from clean startup with 17 assertions. | Editor.js ordinary paragraph native-default paste is `excluded`, not silently emulated. |
| 2026-07-29 | 8 | Removed marker/legacy dispatch/testdriver/legacy-command surfaces, retired archive fixtures after active assertion extraction, and updated active/historical documentation plus the dispatch diagram. | Source audits, full lint, no-int-cast lint, duplicate check, diff check, and the Radiant baseline pass. | Keep only immutable upstream bundle text outside source scans. |

For each phase, record:

1. exact source symbols added/moved/deleted;
2. test names and commands run;
3. capability-manifest rows changed;
4. unexpected editor traces and their focused reproductions;
5. any design-contract update;
6. temporary adapter and its scheduled deletion phase;
7. review checkpoint decision.

---

## 15. Final completion checklist

The final validation covers full lint (including the static-module inventory
after its source regrouping), the focused no-int-cast rule, duplicate and diff
checks, the full Radiant baseline, DOM UI, WPT, editor-4C, and all editable
suites.

### Architecture

- [x] all contenteditable platform/automation/replay input enters one gate;
- [x] `beforeinput` is cancelable notification only;
- [x] `input` is non-cancelable post-action notification only;
- [x] one per-document registry snapshots at most one action;
- [x] DOM and template route from existing host ownership;
- [x] action outcome is structured and DOM mutation is epoch-verified;
- [x] notifications/action run in one synchronous runtime batch.

### Minimal DOM action

- [x] text insertion/replacement;
- [x] selected-range deletion within supported boundary;
- [x] simple backward/forward character deletion;
- [x] provisional/commit/cancel composition;
- [x] correct Selection/live Range updates;
- [x] precise MutationObserver records;
- [x] line/paragraph/history/format unsupported fall-through is explicit;
- [x] no implicit browser-rich structural engine.

### Editors

- [x] CodeMirror authoritative-state matrix;
- [x] ProseMirror light-DOM authoritative-state matrix;
- [x] upstream Editor.js supported-Tool authoritative-data matrix (with the documented native-default paste exclusion);
- [x] Radiant template source/model matrix;
- [x] mixed-route document;
- [x] record/replay parity;
- [x] clean destroy with no retained handlers/observers/roots.

### Cleanup

- [x] no active `data-editable` routing;
- [x] no `EDIT_SURFACE_LAMBDA_TEMPLATE`;
- [x] no all-in-one `editing_dispatch_beforeinput_ex`;
- [x] no ambiguous Lambda/native Boolean result plumbing;
- [x] no direct testdriver mutation callback;
- [x] no exposed `execCommand`/`queryCommand*`;
- [x] no duplicate text-descendant or insertion helper;
- [x] obsolete fixtures removed only after assertion extraction;
- [x] current Radiant docs and diagrams match the new gate;
- [x] full baseline, lint, no-int-cast, and dedup checks pass.

The implementation is not complete while both old and new transaction paths
remain callable, even if all editor tests happen to pass.
