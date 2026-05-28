# Radiant Design - Unified Editing

**Status:** Implementation in progress
**Date:** 2026-05-28
**Layer:** Interaction/editing core, above DOM Range/Selection and below form controls, contenteditable hosts, JS DOM events, and Lambda `edit <...>` templates.
**Related docs:** [Radiant_Design_Form_Input.md](Radiant_Design_Form_Input.md), [Radiant_Design_Content_Editable.md](Radiant_Design_Content_Editable.md), [Radiant_Design_State.md](Radiant_Design_State.md), [Radiant_Design_Event.md](Radiant_Design_Event.md), [Radiant_Design_Selection.md](Radiant_Design_Selection.md), [Radiant_ContentEditable_WPT_Status.md](Radiant_ContentEditable_WPT_Status.md)

---

## 1. Goal

Radiant now has two editing stacks that are close cousins but not the same
system:

- **Form text controls** (`<input>` and `<textarea>`) use `text_control.*`,
  `text_edit.*`, `FormControlProp`, StateStore projections, and weak JS queue
  hooks for `beforeinput` / `input` / `composition*`.
- **Contenteditable hosts** use `editing_host.*`, DOM Range/Selection, the
  `InputIntent` table in `event.cpp`, `dispatch_rich_beforeinput`, and the
  native JS `InputEvent` bridge.

The objective is to unify these into one editing layer with one event routing
model, one focus/caret/selection/scroll model, one state/history model, and
one event/state logging vocabulary.

The end state should let `event.cpp` answer a single question for every mouse,
keyboard, text-input, IME, clipboard, and drag/drop event:

> "Is the current target an editing surface, and if so, which editing
> operation should the shared editor perform or dispatch?"

---

## 2. Current State Verified In Code

| Area | Form text controls | Contenteditable |
|---|---|---|
| Surface detection | `tc_is_text_control()` / `tc_get_or_create_form()` in `text_control.*` | `editing_host_lookup()` in `editing_host.*` |
| Canonical selection | `DocState::dom_selection` is the intended canonical object, but legacy `state->caret` / `state->selection` (`state_store.hpp:247-248`) still run **in parallel**; the projection migration is in progress, not complete | DOM Range/Selection directly |
| Value/model mutation | `te_replace_byte_range()`, `tc_set_value()`, `FormControlProp::current_value` | No default DOM mutation; JS/Lambda consumer owns the model |
| Input intent | Mostly direct calls from `event.cpp` to `te_*` helpers; weak `te_dispatch_beforeinput()` lacks full `InputEvent` payload | `InputIntent` -> `inputType`, `dispatch_rich_beforeinput()`, JS `InputEvent` with `getTargetRanges()` |
| Focus | `focus_set()` syncs text-control state and fires focus events from `update_focus_state()` | Editing hosts are implicitly focusable via `is_view_focusable()` |
| Undo/redo | Per-control `EditHistory` ring exists in `text_edit.*` | Consumer-owned; Radiant emits `historyUndo` / `historyRedo` intents |
| Composition | `te_ime_*` for text controls; platform shims in progress | macOS rich composition lowers to `InputIntent` in `event.cpp` |
| Logging | `event_state_log` emits raw input and state transitions; selection/focus transitions are logged | Same log infrastructure, but rich beforeinput operations are not first-class log records |

The intended structural win — `DocState::dom_selection` as the single
canonical selection object — is **partially landed**. The object exists
(`state_store.hpp:258`) and sync infrastructure is in place
(`dom_selection_sync_depth`, the phase 6-8 coalescing machinery), but the
legacy `state->caret` / `state->selection` fields still hold independent
state rather than being pure projections. The unification must build on DOM
Selection rather than introduce another selection type — **but it cannot
assume the projection migration is finished.** Completing that migration is
promoted to a prerequisite phase (E0, §11).

> **Note on sibling docs.** The gap analysis in
> [Radiant_Design_Form_Input.md](Radiant_Design_Form_Input.md) §2.3 predates
> this work and is now stale: it lists undo, IME, and `beforeinput` as
> "Missing", but all three exist in code today (`form_control.hpp:216`
> EditHistory ring, `text_edit.hpp:181-189` `te_ime_*`,
> `te_dispatch_beforeinput`). Where the two docs disagree on what exists,
> this table is authoritative. This proposal supersedes that gap analysis.

---

## 3. Problems To Solve

1. **Duplicated event decoding.** Keyboard, text-input, composition, paste, cut,
   drag/drop, and selection gestures are decoded partly in form-specific code
   and partly in contenteditable code.
2. **Two InputEvent shapes.** Contenteditable has `InputIntent` and a real JS
   `InputEvent` factory with `data`, `dataTransfer`, `isComposing`,
   `inputType`, and `getTargetRanges()`. Form controls mostly queue generic
   weak hooks with too little payload.
3. **Focus and selection are unified underneath but not at the API boundary.**
   `focus_set()` and DOM Selection do the right kind of work, but callers still
   need to know whether they are editing a form value or a host subtree.
4. **Scrolling is not a shared editing concern.** Document scrolling, control
   scrolling, textarea visual-line scrolling, and selection-drag auto-scroll are
   scattered.
5. **Undo/redo semantics differ.** Form controls need browser-like value
   history; contenteditable needs model-owned history while still emitting
   `historyUndo` / `historyRedo` consistently.
6. **Logging lacks an editing vocabulary.** We log raw input and some state
   transitions, but not "editing intent", "beforeinput prevented",
   "selection drag auto-scrolled", or "undo restored snapshot" in one schema.

---

## 4. Design Overview

Introduce a small shared layer:

```cpp
// radiant/editing.hpp
enum EditingSurfaceKind {
    EDIT_SURFACE_NONE,
    EDIT_SURFACE_TEXT_CONTROL,
    EDIT_SURFACE_CONTENTEDITABLE,
    EDIT_SURFACE_LAMBDA_TEMPLATE
};

enum EditingMode {
    EDIT_MODE_RICH,
    EDIT_MODE_PLAINTEXT_ONLY,
    EDIT_MODE_SINGLE_LINE_TEXT,
    EDIT_MODE_MULTI_LINE_TEXT,
    EDIT_MODE_PASSWORD_TEXT
};

struct EditingSurface {
    EditingSurfaceKind kind;
    EditingMode mode;
    DomElement* owner;          // input/textarea or editing host
    View* view;                 // focused / hit target view
    bool readonly;
    bool disabled;
    bool target_in_false_island;
};

bool editing_surface_from_target(View* target, EditingSurface* out);
bool editing_surface_from_focus(DocState* state, EditingSurface* out);
```

`EditingSurface` is a resolver, not a new owner of data. It points to the
existing owners:

- `FormControlProp` remains the value/state owner for form text controls.
- DOM Range/Selection remains the selection owner for contenteditable.
- Lambda `edit <...>` templates remain model owners for rich documents.

**Resolution and caching.** `EditingSurface` is a transient view recomputed
per event, not stored state. To avoid drift, define one authority rule:
`EditingInteractionState::active_surface` (§7.1) is authoritative for the
*active* editing session (focus, in-flight drag, composition); the per-event
`editing_surface_from_target()` is used only for hit-driven resolution
(mouse-down, drop) and must be reconciled against the active surface before
mutating. `editing_host_lookup()` already caches per layout-tick and is
invalidated by attribute mutation; reuse that cache rather than adding a
second one.

**Lambda-template scope.** `EDIT_SURFACE_LAMBDA_TEMPLATE` is **out of scope
for E1-E7** and is listed here only to reserve the enum slot and signature
shape. The controller treats it identically to `EDIT_SURFACE_CONTENTEDITABLE`
(dispatch intents, no default mutation) until a dedicated phase defines its
undo bridge and target-range computation. See §14 Non-Goals. This keeps every
controller signature stable without committing to half-specified behavior.

Then add one controller that all event paths call:

```cpp
// radiant/editing_controller.hpp
bool editing_handle_mouse_down(EventContext*, const EditingSurface*, const MouseButtonEvent*);
bool editing_handle_mouse_move(EventContext*, const EditingSurface*, const MousePositionEvent*);
bool editing_handle_mouse_up  (EventContext*, const EditingSurface*, const MouseButtonEvent*);
bool editing_handle_key_down  (EventContext*, const EditingSurface*, const KeyEvent*);
bool editing_handle_text_input(EventContext*, const EditingSurface*, const TextInputEvent*);
bool editing_handle_composition(EventContext*, const EditingSurface*, const CompositionEvent*);
bool editing_handle_paste(EventContext*, const EditingSurface*, const ClipboardPayload*);
bool editing_handle_drop (EventContext*, const EditingSurface*, const DragDropPayload*);
```

`event.cpp` remains the platform event router and DOM event dispatcher, but it
stops owning editing policy. It does hit-testing, builds an `EditingSurface`,
then asks the controller to consume the event.

> **On "eliminating branches".** The goal is *not* to remove the
> form-vs-contenteditable fork — that fork is intrinsic to the ownership
> difference (forms mutate a local value; rich hosts emit intents and never
> mutate by default). The goal is to **consolidate the fork into one place**:
> the transaction runner in §7.3. After unification there is exactly one
> branch on `EditingSurfaceKind`, in the runner, instead of the same branch
> scattered across every key/mouse/IME path in `event.cpp`. The acceptance
> criteria (§13) are worded accordingly.

---

## 5. Unified Input Intent And InputEvent

### 5.1 One Intent Type

Move the current `InputIntent` enum and `inputType` mapping out of
`event.cpp` into `editing_intent.{hpp,cpp}`. Extend it so both form controls
and contenteditable use the same payload:

```cpp
struct EditingIntent {
    InputIntentType type;
    const char* data;
    const char* html_data;
    bool is_composing;
    uint32_t composition_caret;
    int key;
    int mods;
    DataTransferPayload* transfer;
};
```

The `format*` and `selectAll` exclusion policy from the contenteditable doc
stays intact for Radiant-synthesized `InputEvent`s.

**Composition fields must be designed now, not in E7.** `is_composing` and
`composition_caret` are frozen into this struct in E2, but the composition
*transaction model* (one `insertCompositionText` transaction per IME session,
preedit lifecycle, ordering of `compositionstart/update/end` relative to
`beforeinput`/`input`) is not implemented until E7. These two paths are
genuinely different today — forms use `te_ime_*` (`text_edit.hpp:181-189`),
contenteditable lowers composition to `InputIntent` in `event.cpp`. If the
transaction shape is left undesigned until E7, this struct will almost
certainly need rework. **E2 must include a paper sketch of the composition
transaction** (fields, event order, session boundaries) sufficient to prove
the struct above is the right shape — even though the implementation lands in
E7. See §11 E2 exit criterion.

### 5.2 One Dispatcher

Create:

```cpp
bool editing_dispatch_beforeinput(EventContext* evcon,
                                  const EditingSurface* surface,
                                  const EditingIntent* intent,
                                  EditingTargetRange* ranges,
                                  uint32_t range_count);

void editing_dispatch_input(EventContext* evcon,
                            const EditingSurface* surface,
                            const EditingIntent* intent);
```

For **contenteditable**, the dispatcher keeps the current behavior:

1. Build JS `InputEvent`.
2. Dispatch `beforeinput`.
3. Dispatch Lambda template handler.
4. If not prevented, dispatch `input`.
5. Do not mutate DOM by default.

For **form text controls**, the dispatcher changes the order to the browser
contract:

1. Compute target ranges from the current form selection.
2. Dispatch JS `beforeinput` with the full InputEvent payload.
3. If prevented, abort mutation.
4. Mutate `FormControlProp::current_value`.
5. Sync selection projection and queue `selectionchange`.
6. Dispatch JS `input` and Lambda `input`.

This replaces the current form-control weak queue path, where
`te_dispatch_beforeinput()` lacks the full `InputEvent` surface.

**Performance note.** The form path now builds an `InputEvent` and computes
target ranges on every keystroke, where the current weak path does neither.
This is almost certainly negligible (the keystroke path already walks glyphs
for caret rendering), but it is new per-keystroke work; the contenteditable
doc flagged the same cost for `getTargetRanges()`. Carry a micro-benchmark in
E3 so a regression is caught rather than assumed away.

### 5.3 Target Ranges

Use one helper:

```cpp
uint32_t editing_compute_target_ranges(DocState* state,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       EditingTargetRange* out,
                                       uint32_t cap);
```

- For contenteditable, reuse the current `compute_target_ranges()` logic.
- For text controls, produce StaticRange-like snapshots over the anonymous
  text-control text node / projection boundary already used by DOM Selection.
- History, select-all, and replacement cases return an empty range when the
  spec says the target is not predictable.

> **Prerequisite to verify in E0/E3.** The text-control bullet assumes a
> concrete anonymous text node exists in Radiant's tree for a form value, and
> that `dom_selection` already references it. If `dom_selection` over a form
> field does **not** resolve to a real node today (likely, given the parallel
> legacy fields noted in §2), then `getTargetRanges()` for forms requires
> synthesizing a node — a non-trivial rabbit hole. Confirm the node exists
> before committing the form `getTargetRanges()` story in E3; this is one more
> reason the E0 projection migration gates this work. See Open Question 1.

### 5.4 Composition Transaction Sketch

E2 freezes the intent payload shape for IME even though E7 owns the full
implementation. A composition session is modeled as one active editing
transaction attached to the active `EditingSurface`.

Session fields:

- `surface`: the focused editing surface captured at `compositionstart`.
- `anchor`: the DOM Selection/form-selection boundary where preedit begins.
- `preedit_text`: latest uncommitted text, stored outside the intent.
- `composition_caret`: caret offset inside `preedit_text`, copied into each
  `EditingIntent`.
- `committed`: true only after `compositionend` with non-empty committed text.

Event order:

1. Platform `compositionstart` resolves the active surface, opens the session,
   dispatches `compositionstart`, then emits an
   `INPUT_INTENT_COMPOSITION_START` record for logging. No mutation occurs.
2. Platform `compositionupdate` dispatches `compositionupdate`, then emits
   `beforeinput/input` with `insertCompositionText`, `data = preedit_text`,
   `is_composing = true`, and `composition_caret` set to the platform caret.
   Text controls replace the previous preedit span; contenteditable consumers
   receive the same intent and own their model update.
3. Platform `compositionend` dispatches `compositionend`, then emits either
   `insertFromComposition` when committed text is non-empty or
   `deleteCompositionText` when the session is canceled. `is_composing` is
   false for this final intent.
4. The session closes after the final `beforeinput/input` cascade completes.

This proves the E2 payload is sufficient: `data`, `is_composing`, and
`composition_caret` describe every cross-surface IME event, while the mutable
preedit span and ownership rules stay in the controller/session state rather
than in each transient intent.

---

## 6. Unified Focus, Caret, Selection, And Scrolling

### 6.1 Focus

`focus_set()` remains the only focus state mutator. Add an editing activation
hook called from `update_focus_state()` after the focus events are dispatched:

```cpp
void editing_activate_surface(EventContext* evcon, const EditingSurface* surface);
void editing_deactivate_surface(EventContext* evcon, const EditingSurface* surface);
```

Activation does the mode-specific work:

- Text control: `tc_ensure_init()`, capture `value_at_focus`, sync DOM
  Selection from `FormControlProp::selection_*`, activate platform IME.
- Contenteditable: ensure host selection is valid, forward `inputmode` /
  `enterkeyhint`, activate platform IME.
- Disabled targets never activate; readonly targets activate selection but
  reject mutations.

`Document.activeElement` continues to reflect the focused element. For clicks
inside a contenteditable descendant, focus remains on the editing host.

### 6.2 Selection

Keep DOM Selection as the single canonical representation. Add an editing
selection facade that hides whether the boundary is in a text control value or
DOM text:

```cpp
bool editing_selection_collapse(DocState*, const EditingSurface*, EditingBoundary);
bool editing_selection_extend  (DocState*, const EditingSurface*, EditingBoundary);
bool editing_selection_select_all(DocState*, const EditingSurface*);
bool editing_selection_delete(DocState*, const EditingSurface*, EditingIntent*);
```

For form controls this writes DOM Selection first, then mirrors into
`FormControlProp::selection_start/end/direction`. For contenteditable it writes
DOM Selection only.

**Cross-surface boundary policy.** The unified controller is the right — and
only — place to define what happens when a gesture crosses a surface
boundary. The form-input doc flags this as untested
([Radiant_Design_Form_Input.md](Radiant_Design_Form_Input.md) §2.3,
"cross-control drag should not select host DOM text"). Define explicitly:

- A drag that begins inside a text control **clamps** to that control's value
  region; it never extends into surrounding host DOM text.
- A drag that begins in a contenteditable host and crosses into an embedded
  `<input>`/`<textarea>` treats the control as an atomic, non-enterable unit
  (selection skips over it, matching browsers), unless the control is the drag
  origin.
- A selection that legitimately spans across an editing-host boundary collapses
  to the host's own selection on mutation; the controller rejects mutations
  whose target range straddles two surfaces.

This belongs in the §12 Selection test group and must run against the
input/textarea/contenteditable fixtures plus a mixed fixture.

### 6.3 Caret And Hit Testing

Extract the glyph-positioning pieces from `event.cpp`, `dom_range_resolver.cpp`,
and `render_form.cpp` into an editing geometry module:

```cpp
bool editing_hit_test_text(EventContext*, const EditingSurface*, float x, float y,
                           EditingBoundary* out);
bool editing_caret_rect(DocState*, const EditingSurface*, EditingBoundary,
                        Rect* out_doc_rect);
```

The renderer remains surface-specific: form controls draw value/placeholder and
contenteditable draws DOM content. But hit testing and caret rect calculation
should share the same `EditingBoundary`.

### 6.4 Auto Scroll

Add an `EditingScrollState` under `DocState`:

```cpp
struct EditingScrollState {
    bool active;
    EditingSurface surface;
    float pointer_x;
    float pointer_y;
    float velocity_x;
    float velocity_y;
    uint64_t last_tick_ms;
};
```

Rules:

- During mouse selection dragging, if the pointer leaves the visible editing
  viewport, start auto-scroll.
- For `<input>`, scroll horizontally inside the control.
- For `<textarea>`, scroll horizontally and vertically inside the control.
- For contenteditable, scroll the nearest scroll container, falling back to the
  document viewport.
- Each scroll tick extends selection to the current pointer boundary after the
  scroll has applied.
- Log every start/stop and throttled movement as `editing.autoscroll`.

This is the missing bridge between selection dragging and scroll state.

**Timer ownership (resolving Open Question 4).** Auto-scroll *cannot* be driven
by the event/state cascade alone: while the mouse is held still outside the
viewport, no new input events arrive, yet scrolling must keep ticking. This
requires a real time-driven tick — the **same** requirement as caret blink
(`form_control.hpp` `caret_blink_t`, form doc §3.8). Rather than two ad-hoc
timers, introduce **one shared editing animation tick** that drives caret
blink, auto-scroll velocity integration, and the IME reveal-last-char timer.
Each tick still enters through the event/state cascade path so log records stay
ordered (the original Q4 concern is preserved). This shared tick is a
deliverable of E5, not a later afterthought — see §11.

---

## 7. Unified State Store And Undo/Redo

### 7.1 Editing State In DocState

Add one editing interaction record to `DocState`:

```cpp
struct EditingInteractionState {
    EditingSurface active_surface;
    bool has_active_surface;

    bool pointer_selecting;
    enum DragMode { DRAG_CHAR, DRAG_WORD, DRAG_LINE } drag_mode;
    EditingBoundary drag_anchor;

    bool composing;
    EditingScrollState autoscroll;
};
```

This replaces scattered fields such as text-selection drag flags over time. The
old fields can remain as projections while call sites migrate.

### 7.2 Undo/Redo

Use one command vocabulary, but two storage strategies:

| Surface | Undo owner | Behavior |
|---|---|---|
| `<input>` / `<textarea>` | Radiant `EditHistory` ring | `historyUndo` / `historyRedo` restore value + selection snapshots |
| `contenteditable` | Consumer | Radiant dispatches `beforeinput { historyUndo/historyRedo }`; JS/Lambda model decides |
| Lambda `edit <...>` | Lambda edit session | Bridge `historyUndo/historyRedo` to `edit_undo()` / `edit_redo()` where available |

The controller exposes:

```cpp
bool editing_undo(EventContext*, const EditingSurface*);
bool editing_redo(EventContext*, const EditingSurface*);
```

For text controls it mutates local history after a cancellable `beforeinput`.
For rich hosts it only dispatches the intent.

### 7.3 Mutation Transactions

All value-changing form operations run through a transaction:

```cpp
struct EditingTransaction {
    EditingSurface surface;
    EditingIntent intent;
    EditingTargetRange ranges[2];
    uint32_t range_count;
    bool beforeinput_prevented;
};
```

Transaction steps:

1. Snapshot target ranges.
2. Dispatch `beforeinput`.
3. If canceled, log and stop.
4. Push history when the surface owns local history.
5. Apply mutation.
6. Sync DOM Selection and projections.
7. Run validation / pseudo-state reflection.
8. Dispatch `input`.
9. Request caret-into-view / repaint.
10. Log commit.

---

## 8. Unified Event Logging

Extend `event_state_log` with editing-specific records. Keep JSONL output in
`./temp/`, matching the existing logging rule.

| Record type | When | Key fields |
|---|---|---|
| `editing.surface` | Surface resolved or activated | `kind`, `mode`, `owner`, `readonly`, `disabled`, `false_island` |
| `editing.intent` | Key/mouse/text/IME decoded into intent | `inputType`, `data_len`, `is_composing`, `source_event` |
| `editing.beforeinput` | Beforeinput dispatched | `inputType`, `range_count`, `prevented` |
| `editing.mutation` | Local mutation committed | `old_len`, `new_len`, `selection_start`, `selection_end` |
| `editing.selection` | Selection operation | `operation`, `anchor`, `focus`, `drag_mode` |
| `editing.focus` | Surface activation/deactivation | `from`, `to`, `ime_active`, `focus_visible` |
| `editing.history` | Undo/redo push/restore | `action`, `depth`, `cursor`, `owned_by` |
| `editing.autoscroll` | Drag selection auto-scroll | `surface`, `dx`, `dy`, `velocity_x`, `velocity_y` |
| `editing.composition` | IME lifecycle | `phase`, `preedit_len`, `commit_len`, `caret` |

These records should be emitted from the shared controller, not from every
surface-specific branch. That keeps debugging form input and contenteditable
sessions comparable.

**Privacy rule for password and sensitive surfaces.** The records above
deliberately log `data_len` rather than `data` — but `editing.mutation`,
`editing.selection`, and `editing.composition` still carry lengths, offsets,
and caret positions that leak typing structure. For
`EDIT_MODE_PASSWORD_TEXT` (and any field the consumer marks sensitive), the
controller MUST redact: emit the record `kind`/`inputType` for ordering but
zero out `old_len`/`new_len`/`preedit_len`/`commit_len` and selection offsets.
JSONL in `./temp/` is easy to forget about; never let plaintext or
reconstructable length sequences reach it for a password field.

---

## 9. Event Flow

### 9.1 Keyboard Insert Into `<input>`

1. `RDT_EVENT_KEY_DOWN` fires `keydown`.
2. Controller resolves focused `EditingSurface` as single-line text.
3. `RDT_EVENT_TEXT_INPUT` decodes to `insertText`.
4. Compute target range from current text-control selection.
5. Dispatch cancellable `beforeinput`.
6. If not canceled, mutate `FormControlProp::current_value`.
7. Sync DOM Selection, StateStore projection, and `selectionStart/End`.
8. Dispatch `input`.
9. Scroll caret into view.
10. Log `editing.intent`, `editing.beforeinput`, `editing.mutation`.

### 9.2 Keyboard Insert Into `contenteditable`

1. Same `keydown` / `text_input` sequence.
2. Controller resolves focused host as rich or plaintext-only.
3. Dispatch `beforeinput` with the same `insertText` intent.
4. If not canceled, dispatch `input`.
5. No default DOM mutation.
6. JS/Lambda consumer applies model mutation and reconciles DOM.

### 9.3 Selection Drag With Auto Scroll

1. Mouse down resolves an editing surface and collapses/starts selection.
2. Mouse move extends by char/word/line mode.
3. Pointer leaves the visible editing viewport.
4. `EditingScrollState` starts a timer-driven autoscroll.
5. Each tick scrolls the owning viewport and recomputes the focus boundary.
6. Mouse up stops autoscroll and finalizes the selection.

---

## 10. File Plan

| File | Action |
|---|---|
| `radiant/editing.hpp` | New shared `EditingSurface`, mode, boundary, transaction types |
| `radiant/editing.cpp` | Surface resolver over text controls, contenteditable, and Lambda `data-editable` |
| `radiant/editing_intent.hpp/.cpp` | Move `InputIntent`, key/text/clipboard/drop decoding, `inputType` mapping out of `event.cpp` |
| `radiant/editing_controller.hpp/.cpp` | Mouse/key/text/IME/clipboard/drop handlers and transaction runner |
| `radiant/editing_geometry.hpp/.cpp` | Hit testing, caret rects, target ranges, caret-into-view helpers |
| `radiant/text_edit.hpp/.cpp` | Keep form value operations; make them implementation helpers called by the controller |
| `radiant/editing_host.hpp/.cpp` | Keep contenteditable lookup; expose through `editing_surface_from_target()` |
| `radiant/event.cpp` | Shrink to event routing, DOM event dispatch, and controller calls |
| `radiant/state_store.hpp/.cpp` | Add `EditingInteractionState`; migrate drag/autoscroll flags |
| `radiant/event_state_log.*` | Add editing record helpers |
| `lambda/js/js_dom_events.*` | Reuse existing native `InputEvent` factory for both forms and rich hosts |
| `lambda/js/js_dom.cpp` | Text-control `beforeinput` queue should use the shared dispatcher payload |
| `radiant/render_form.cpp` | Read shared caret/scroll geometry; keep form-specific drawing |
| `test/ui/` | Add shared editing tests that run the same gesture against input, textarea, and contenteditable |
| `test/wpt/` | Add contenteditable/form-input WPT runner follow-ups once the shared dispatcher lands |

---

## 11. Phasing

| Phase | Scope | Exit criterion |
|---|---|---|
| **E0 - Selection Projection (prerequisite)** | Finish the migration so legacy `state->caret` / `state->selection` (`state_store.hpp:247-248`) become true projections of `DocState::dom_selection`, with no independent state. Verify a form value resolves to a concrete node reachable from `dom_selection` (needed for form target ranges, §5.3). | No code path writes `state->caret`/`state->selection` directly except through the `dom_selection` projection; form-field selection round-trips through `dom_selection`; existing form + contenteditable smoke tests stay green |
| **E1 - Surface Resolver** | Add `EditingSurface`; route text controls, contenteditable, and `data-editable` through it without behavior changes. Establish the active-surface authority rule (§4). | Existing form and contenteditable smoke tests still pass |
| **E2 - Shared Intent** | Move `InputIntent` and key/text decoding from `event.cpp`; make form controls and rich hosts use the same intent table. **Produce a paper sketch of the composition transaction model** sufficient to freeze the `EditingIntent` composition fields (§5.1). | `insertText`, delete, paste, drop, history intents produce identical `inputType` strings across surfaces; composition-transaction sketch reviewed and `EditingIntent` shape signed off |
| **E3 - Shared InputEvent** | Replace form weak `beforeinput`/`input` queue with full `InputEvent` dispatch and target ranges. Confirm form `getTargetRanges()` node assumption from E0. | Form `beforeinput` can be canceled; `input` fires only after mutation; per-keystroke micro-benchmark shows no regression vs the weak path |
| **E4 - Selection And Geometry** | Add shared boundaries, caret rects, and hit testing; keep DOM Selection canonical. Implement the cross-surface boundary/clamping policy (§6.2). | Click, drag, double-click, triple-click work across input, textarea, and contenteditable; cross-surface drag clamps correctly on a mixed fixture |
| **E5 - Auto Scroll** | Implement selection-drag autoscroll for text controls, scroll containers, and document viewport. Introduce the **one shared editing animation tick** (caret blink + autoscroll + IME reveal timer, §6.4). | UI tests cover drag past edge in input, textarea, contenteditable; autoscroll continues while pointer is held still outside the viewport |
| **E6 - State And History** | Add `EditingInteractionState`; route undo/redo through controller | Form undo restores local snapshots; contenteditable emits history intents |
| **E7 - IME Unification** | Implement the composition transaction sketched in E2: platform IME calls one composition controller for both form and rich surfaces | `insertCompositionText` and `composition*` order is consistent; matches the E2 sketch with no `EditingIntent` rework |
| **E8 - Logging And WPT** | Add editing log records (including the §8 password redaction rule); wire WPT/status docs for form/input-events/contenteditable | Event log can reconstruct an edit cascade; password fields emit no reconstructable lengths; Tier-A runners start filling status tables |

**Dependencies:** E0 gates E3 and E4 (both rest on a true canonical
selection). E2's composition sketch gates E7. The shared animation tick (E5)
is also consumed by caret blink, so E5 can be pulled earlier if form caret
blink work lands first.

---

## 12. Tests

Add paired UI tests that perform the same operation on three fixtures:

- `test/ui/editing/input.html`
- `test/ui/editing/textarea.html`
- `test/ui/editing/contenteditable.html`
- `test/ui/editing/mixed.html` — contenteditable host with an embedded
  `<input>`/`<textarea>`, for the cross-surface boundary cases (§6.2)

> **Same gesture, surface-specific expectation.** A paired test drives the
> *same* gesture against all fixtures, but the asserted outcome legitimately
> differs by surface: forms mutate their value, while contenteditable/Lambda
> dispatch intents and mutate nothing by default. The harness must express
> the expected outcome per surface — a divergent result is correct, not a
> regression. Keep this explicit so the fork (which is intrinsic, per §4) is
> not mistaken for a bug.

Test groups:

| Group | Assertions |
|---|---|
| Typing | `beforeinput` order, `input` order, surface-specific value/model effect |
| Cancellation | `preventDefault()` blocks form mutation and signals rich consumer |
| Selection | click, drag, Shift+click, double-click word, triple-click line/all |
| Cross-surface | drag clamps to control; drag from host skips embedded control; straddling mutation rejected (mixed fixture) |
| Keyboard navigation | char, word, line, document granularity |
| Clipboard | copy/cut/paste, `dataTransfer`, plaintext-only filtering |
| Drop | `insertFromDrop`, `deleteByDrag`, target ranges |
| Undo/redo | form snapshot restore, rich history intent dispatch |
| IME | composition start/update/end and `insertCompositionText` |
| Auto scroll | drag selection past viewport/control edge; tick continues while pointer held still |
| Logging | JSONL contains `editing.intent`, `editing.beforeinput`, mutation/selection records; password fields redacted |

For WPT:

- Reuse the planned contenteditable runner from
  [Radiant_ContentEditable_WPT_Status.md](Radiant_ContentEditable_WPT_Status.md).
- Add form/input-events cases after E3, especially `beforeinput`, `input`,
  `selectionchange`, and text-control focus.
- Keep `editing/run/*` as a non-gating legacy gauge.

---

## 13. Acceptance Criteria

- `event.cpp` no longer scatters editing policy across per-key/mouse/IME
  branches; it routes to the editing controller. The intrinsic
  form-vs-rich fork is **consolidated into the single transaction runner**
  (§7.3) rather than eliminated — there is exactly one branch on
  `EditingSurfaceKind`, in one place.
- One `InputIntent` / `EditingIntent` table covers form controls and rich hosts.
- Both form controls and contenteditable dispatch the same JS `InputEvent`
  surface: `data`, `dataTransfer`, `inputType`, `isComposing`,
  `getTargetRanges()`.
- DOM Selection is canonical for caret/selection; legacy `state->caret` /
  `state->selection` and form `selectionStart/End` are **pure projections**
  with no independent state (verified by E0).
- Selection-drag autoscroll works for single-line input, textarea, and
  contenteditable inside scroll containers, including while the pointer is held
  still outside the viewport (shared animation tick).
- Cross-surface gestures behave per §6.2: drags clamp to their origin control,
  embedded controls are atomic to host-originated drags, and straddling
  mutations are rejected.
- Undo/redo enters through the same controller. Form controls own local history;
  rich hosts receive history intents.
- Event/state logs can reconstruct a full editing cascade from raw input to
  intent to beforeinput to mutation/selection/scroll/focus result; password
  fields emit no reconstructable plaintext or length sequences.
- Existing `make test-radiant-baseline` remains green; new editing UI tests are
  added before WPT gating.

---

## 14. Non-Goals

- Implementing `document.execCommand` or browser legacy editing quirks.
- Making contenteditable mutate DOM by default.
- Replacing DOM Range/Selection with a separate editor selection model.
- Solving Shadow DOM editing boundaries in this phase.
- Full spellcheck/autocorrect. Those can later become producers of
  `insertReplacementText`.
- **Full `EDIT_SURFACE_LAMBDA_TEMPLATE` support in E1-E7.** The enum slot and
  signatures are reserved (§4) and the controller treats it as
  contenteditable-equivalent until a dedicated later phase defines its
  `edit_undo()`/`edit_redo()` bridge and target-range computation. No
  Lambda-template-specific behavior is committed in this proposal.

---

## 15. Open Questions

1. **Form target ranges:** Should text-control `getTargetRanges()` expose
   anonymous text-control nodes, host elements with offsets, or a Radiant
   StaticRange wrapper that maps to `selectionStart/End`? Default proposal:
   expose the same projected text node used by DOM Selection — **but this is
   blocked on verifying that node actually exists in the tree (E0/§5.3).** If
   it does not, fall back to a synthetic StaticRange wrapper over
   `selectionStart/End` and document the divergence from real DOM nodes.
2. **Undo coalescing:** Keep current form snapshot ring as-is first, then add
   browser-like coalescing after unification. Default proposal: do not change
   coalescing in E1-E3.
3. **Plaintext-only filtering:** Should filtering happen in the dispatcher or
   in per-surface mutation code? Default proposal: dispatcher normalizes the
   intent payload before `beforeinput`.
4. **Autoscroll timer owner:** *Resolved (§6.4).* A pure event/state cascade
   cannot tick while the pointer is held still, so autoscroll, caret blink, and
   the IME reveal timer share **one editing animation tick** that still enters
   through the cascade path to keep logs ordered. Delivered in E5.

---

## 16. Implementation Notes

- E1 surface resolution is landed in `editing.{hpp,cpp}` and `event.cpp`
  now resolves rich/data-editable hosts through `EditingSurface`.
- E2 shared intent extraction is landed in `editing_intent.{hpp,cpp}`.
- E3 target-range computation has been extracted to
  `editing_target_range.{hpp,cpp}` for rich/contenteditable surfaces. Form
  text controls intentionally return no target ranges until E0 proves a real
  form-value DOM boundary exists; this avoids inventing synthetic ranges while
  Selection2 live-range/removal work is still active.
- The rich-host `beforeinput`/`input` policy has been extracted to
  `editing_dispatch.{hpp,cpp}`. `event.cpp` still owns the concrete JS/Lambda
  dispatch bridges, but now adapts them through `EditingDispatchHooks`; form
  text controls remain on their existing mutation path until E3 can safely
  provide full form `InputEvent` target ranges.
- Form typing and delete mutations now enter the same dispatch surface through
  `dispatch_form_text_replace()`: build an `EditingIntent`, dispatch form
  `beforeinput`, perform `te_replace_byte_range_no_events()`, then dispatch
  form `input`. This keeps the text-control value store and history logic in
  `text_edit.cpp`, but removes the old direct event/mutation coupling for the
  keyboard/text-input paths.
- Keyboard paste now uses the same dispatch surface via
  `dispatch_form_text_paste()`. Paste sanitization and maxlength clamping stay
  in `text_edit.cpp` through `te_prepare_paste_replacement()`, so behavior is
  preserved while the event order is unified.
- Form IME commit now has the same split: `text_edit.cpp` owns preedit cleanup,
  selection-to-byte-range calculation, and `compositionend`, while
  `radiant_dispatch_form_text_ime_commit()` in `event.cpp` routes the committed
  text through the shared `beforeinput`/mutation/`input` transaction.
- Native form context-menu Cut/Delete/Paste now uses edit hooks supplied by
  `event.cpp`, so menu ownership/rendering stays in `context_menu.cpp` while
  mutations enter the same unified form replacement/paste transaction.

---

## 17. Summary

Radiant already has the hard pieces: DOM Selection, StateStore focus and
selection transitions, form value helpers, contenteditable host lookup, real
`InputEvent` construction, clipboard transport, IME hooks, and JSONL event
logging. The missing piece is a small editing interface that makes those pieces
meet in one place.

The proposed `EditingSurface` + `EditingController` layer gives Radiant one
editing contract for form controls, contenteditable hosts, and Lambda editor
templates while preserving the correct ownership differences: form controls
mutate local values; contenteditable and Lambda editors consume intents and own
their document model.
