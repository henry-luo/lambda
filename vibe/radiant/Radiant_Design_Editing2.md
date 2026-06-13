# Radiant Editing 2 - StateStore-driven unified editing core

**Date:** 2026-06-13
**Status:** Implementation in progress
**Layer:** Radiant interaction/editing core, above DOM Range/Selection and
below form controls, contenteditable hosts, JS DOM events, Lambda
`edit <...>` templates, and future rich text editor features.
**Scope:** Unified editing authority across contenteditable hosts, form text
controls (`<input>`/`<textarea>`), and Lambda editor templates. This is the
canonical successor to both Radiant_Design_Content_Editable.md (the rich
contenteditable strategy) and Radiant_Design_Editing.md (the form+rich
unification).
**Supersedes / updates:** [Radiant_Design_Content_Editable.md](Radiant_Design_Content_Editable.md)
for the implementation strategy. The older document remains useful for the
web-platform contract and WPT background.
**Supersedes (selection + transaction model):**
[Radiant_Design_Editing.md](Radiant_Design_Editing.md) is **phased out** as the
canonical design. Editing 2 is now the canonical editing design. Editing.md's
selection-authority model (`DocState::dom_selection` as canonical owner,
synthetic StaticRange for form fields, legacy projections kept in parallel) is
replaced by Editing 2's `EditingSelection`-canonical / facade model (§5). Editing.md
remains a historical record of already-landed E1–E7 implementation work; the
landed code stays valid and is re-described under Editing 2's authority model rather
than discarded. The durable cross-cutting decisions from Editing.md that Editing 2
adopts are listed in §4.2–§4.3, §8.4, §9, and §10.3.
**Related docs:**
[Radiant_Design_State.md](Radiant_Design_State.md),
[Radiant_Design_Selection.md](Radiant_Design_Selection.md),
[Radiant_Design_Selection2.md](Radiant_Design_Selection2.md).

---

## 1. Goal

Radiant already has working contenteditable plumbing: editing-host lookup,
DOM Range/Selection, rich `beforeinput` / `input`, clipboard, IME, drag/drop,
and partial default mutation. The next step is to make that plumbing stable
enough for richer editing features: structured logging, undo/redo history,
transaction replay, debugging, state-schema validation, and eventually richer
commands such as list editing, table editing, inline marks, and model-backed
Lambda templates.

This proposal changes the implementation authority:

1. **StateStore owns selection.** DOM Selection becomes a cached DOM-facing
   view into the StateStore selection record, not an independent source of
   truth.
2. **Contenteditable mutation is transactionized.** Every edit goes through one
   state-machine transaction path: intent, target ranges, `beforeinput`, model
   mutation, selection update, `input`, logging, history, reflow, validation.
3. **InputEvent Level 2 is first-class.** `data`, `dataTransfer`,
   `isComposing`, `inputType`, and `getTargetRanges()` are produced from the
   transaction snapshot before mutation.
4. **State schema is active, not descriptive.** The existing StateStore schema
   and state machine validate contenteditable invariants at the event-cascade
   boundary and around editing transactions.

Negative legacy API tests for `execCommand`, `queryCommand*`, and `designMode`
are deliberately low priority. Radiant should continue to reject those legacy
surfaces cleanly, but this proposal does not spend engineering budget on
expanding that test corpus.

---

## 2. Current Problem

Today rich editing works, but the authority is split:

- `DocState::dom_selection` stores DOM-range semantics.
- Legacy `state->caret` / `state->selection` still drive parts of rendering,
  event simulation, and older selection paths.
- Contenteditable mutation logic still lives in several local paths in
  `event.cpp`.
- Some operations update DOM selection first, some update StateStore projection
  first, and some require explicit sync helpers after mutation.

That split is the root cause behind recurring bugs such as stale carets after
cut, copy/cut clipboard drift, selection projection disagreement, and mutation
paths that bypass logging or history.

The target is not merely "more tests". The target is one authority and one
transaction runner.

---

## 3. Non-goals

- Do not implement browser legacy editing commands.
- Do not chase `ref/wpt/editing/run/*` execCommand quirk compatibility.
- Do not add a separate rich-editor document model in this layer.
- Do not make DOM Selection independently mutable outside StateStore.
- Do not solve Shadow DOM selection in this proposal.

Radiant may keep small compatibility stubs for feature detection, but negative
legacy tests are not an acceptance gate for this phase.

---

## 4. Architecture Overview

```text
platform event / JS command / event_sim / WebDriver
  -> EditingSurface resolution
  -> EditingIntent
  -> StateStore editing transaction begin
  -> compute StaticRange target snapshot
  -> dispatch beforeinput
  -> if not prevented: apply contenteditable mutation
  -> update canonical StateStore selection
  -> refresh DOM Selection facade/cache
  -> dispatch input
  -> append history/log records
  -> request reflow/repaint
  -> state-machine validation at cascade settle
```

The key rule:

```text
StateStore selection is canonical.
DOM Selection is a cached DOM API facade over StateStore selection.
Legacy caret/selection structs are rendering projections.
```

### 4.1 Relationship To The Existing `editing_*` Layer

This proposal is **not greenfield**. A partial editing abstraction already
exists and must be folded in, not shadowed:

- `radiant/editing.hpp` — `EditingSurface` (kind, mode, owner, view, readonly,
  disabled, `target_in_false_island`). The transaction runner consumes this
  type; it is not redefined here.
- `radiant/editing_intent.hpp` — `InputIntent`, with
  `typedef InputIntent EditingIntent`. The runner takes this intent as-is.
- `radiant/editing_geometry.hpp` — `EditingBoundary` /
  `EditingBoundaryKind` (`EDITING_BOUNDARY_DOM` / `EDITING_BOUNDARY_TEXT_CONTROL`),
  carrying surface + view for hit-test/paint. Reused for text-control offsets;
  this proposal does **not** define a second `EditingBoundary`.
- `radiant/editing_dispatch.hpp` — `editing_dispatch_beforeinput_ex()`,
  `editing_dispatch_input()`, `editing_dispatch_form_beforeinput()` already
  centralize `beforeinput`/`input` dispatch, target-range validation, and
  JS/Lambda handler routing.

`editing_run_transaction()` (§6.2) **wraps** `editing_dispatch_*` for the
beforeinput/input steps rather than reimplementing them. The runner owns the
ordering, snapshotting, selection write, and validation; the existing
dispatcher remains the mechanism for the event-emission steps inside it.

### 4.2 Editing Surface Resolution And Modes (adopted from Editing.md)

`EditingSurface` is a *resolver*, not a data owner. Two resolution entry points
exist:

- `editing_surface_from_focus()` — the active editing session.
- `editing_surface_from_target()` — hit-driven resolution (mouse-down, drop).

**Active-surface authority rule (carried over from Editing.md §4):** the active
session's surface is authoritative for focus, in-flight drag, and composition.
Per-event hit resolution is used only for pointer/drop entry and **must be
reconciled against the active surface before mutating**. This prevents the
per-event resolver and the focused session from drifting — the same
single-authority discipline this proposal applies to selection.

`EditingMode` distinguishes mutation policy now that Editing 2 folds text controls in
(Phase ED2-1 D):

```cpp
enum EditingMode {
    EDIT_MODE_RICH,
    EDIT_MODE_PLAINTEXT_ONLY,
    EDIT_MODE_SINGLE_LINE_TEXT,
    EDIT_MODE_MULTI_LINE_TEXT,
    EDIT_MODE_PASSWORD_TEXT
};
```

**The form-vs-rich fork is intrinsic, not a smell.** Form controls mutate a
local value; rich/Lambda hosts emit intents and never mutate by default. The
goal is not to remove that fork but to **consolidate it into one place** — the
transaction runner (§6.2), where exactly one branch on `EditingSurfaceKind`
replaces the same branch scattered across every key/mouse/IME path today.

### 4.3 Carry-over Concerns From Editing.md (already landed)

These cross-cutting decisions from Editing.md are adopted as-is; their
implementations have landed and remain valid under Editing 2's authority model. Editing 2
does not re-specify their mechanics, only records that they are in force:

- **One shared editing animation tick.** A single time-driven tick drives caret
  blink, selection-drag auto-scroll velocity, and the password last-character
  reveal timer. It *cannot* be driven by the event/state cascade alone (no
  events arrive while the pointer is held still), but each tick still enters
  through the cascade path so log records stay ordered. Do not reintroduce
  per-feature ad-hoc timers.
- **Selection-drag auto-scroll** for single-line input, textarea, scroll
  containers, and the document viewport, continuing while the pointer is held
  still outside the viewport.
- **Unified focus activation** via `editing_activate_surface()` /
  `editing_deactivate_surface()` from `update_focus_state()`: disabled targets
  never activate; readonly targets activate selection but reject mutation.

---

## 5. Canonical Selection Under StateStore

### 5.1 Selection Authority

The boundaries are **already single-store today**, so this is consolidation,
not new storage. `DocState::live_ranges` (a doubly-linked list of `DomRange`)
is StateStore-owned and is the one place every range boundary lives.
`DomRange.start`/`end` are inline `DomBoundary{ DomNode* node; uint32_t offset }`
with `offset` in **UTF-16 code units** for text (spec-correct). The live-range
mutation envelopes (`dom_mutation_post_insert`,
`dom_mutation_text_replace_data`, `dom_mutation_text_split/merge`) already walk
`live_ranges` and keep every boundary correct across DOM edits.

The only genuine second copy of selection state is legacy
`CaretState`/`SelectionState`. The canonical record below therefore **does not
copy boundaries** — it references the live range, so the existing envelopes keep
it correct for free:

```cpp
enum EditingSelectionKind {
    EDIT_SEL_NONE,
    EDIT_SEL_DOM_RANGE,
    EDIT_SEL_TEXT_CONTROL
};

struct EditingSelection {              // owned by DocState; the single authority
    EditingSelectionKind  kind;
    DomSelectionDirection direction;   // which end is the anchor (a bit, not a boundary copy)

    // EDIT_SEL_DOM_RANGE: boundaries live ONLY in *range (in DocState::live_ranges).
    DomRange*             range;       // selection owns one ref_count

    // EDIT_SEL_TEXT_CONTROL: no DOM range exists; element-local UTF-16 offsets.
    DomElement*           control;
    uint32_t              start_u16, end_u16;

    uint32_t              mutation_seq; // == DocState::selection_mutation_seq (reuse, not a 2nd counter)
};
```

Two rules make StateStore canonical without introducing a fourth representation:

1. **No stored anchor/focus anywhere.** `DomSelection::anchor`/`focus` are
   deleted; `anchorNode = (direction == forward ? range->start : range->end)`,
   computed on read. This *removes* the `sync_anchor_focus()` write step that
   exists today.
2. **A caret is a collapsed selection.** A contenteditable caret is `range`
   with `start == end`; a text-control caret is `start_u16 == end_u16`.
   `CaretState` becomes a pure projection of a collapsed `EditingSelection`,
   which unifies caret and selection authority.

Why a pointer to a live range and not a copied `{node, offset}` record: a
detached copy drifts on every DOM edit and would need manual fix-up after each
mutation — re-introducing the very sync problem this proposal removes. A live
range is auto-tracked by the existing envelopes.

**Revision:** reuse the existing `DocState::selection_mutation_seq` (added for
`selectionchange` coalescing) as the single revision counter. Do not add a
parallel `revision` field — two counters that must stay in lockstep are exactly
the drift this proposal exists to kill.

**The one sanctioned boundary copy** is `getTargetRanges()` / `StaticRange`
(§8): an immutable pre-mutation snapshot (`is_live == false`) that is
*deliberately* decoupled from the DOM and must not track later mutations. That
copy is correct and is the sole exception to the single-store rule.

### 5.2 DOM Selection Facade

`DocState::dom_selection` remains available for JS API compatibility:

- `window.getSelection()`
- `document.getSelection()`
- `Selection.getRangeAt()`
- `Selection.anchorNode`, `focusNode`, offsets, direction
- `Range` and `StaticRange` construction

`DomSelection` stores **no boundaries of its own**. It keeps only
`range_count`, the `ranges[0]` pointer (which IS `EditingSelection::range`),
`direction`, and `associated_doc_root`. Its scalar accessors
(`anchorNode`/`focusNode`/offsets/`isCollapsed`) read through to
`EditingSelection`:

- `getSelection().getRangeAt(0)` returns the JS wrapper for `sel.range` — the
  *same* live `DomRange`, ref-counted, never a copy. This preserves DOM `Range`
  object identity (JS holds live references; those objects must mutate with the
  DOM), which a flat "pointers into a record" facade cannot provide.
- The facade is rebuilt/refreshed when `sel.mutation_seq` advances, never
  written independently.

The public DOM Selection APIs write through the single StateStore writer:

```text
Selection.collapse()
  -> state_store_set_selection()
  -> bumps selection_mutation_seq
  -> marks legacy projection stale (lazy rebuild)
  -> queues selectionchange
```

No code should call `dom_selection_collapse()` and then separately sync
StateStore. `state_store_set_selection()` is the only mutation entry point, and
it mutates `sel.range` via the existing range boundary setters so `live_ranges`
and the mutation envelopes stay correct.

### 5.3 Legacy Projection

`CaretState` and `SelectionState` become derived projections:

- used for painting and hit-test compatibility;
- a caret is the projection of a *collapsed* `EditingSelection`
  (`start == end`), so there is no caret state independent of the selection;
- rebuilt on demand by `state_store_refresh_caret_projection()` when
  `selection_mutation_seq` has advanced (pull-based), replacing the push-based
  `legacy_sync_from_dom_selection()`;
- never written directly from rich editing code;
- never used as the source for `InputEvent.getTargetRanges()`.

Because legacy is never written independently, there is no DOM↔legacy
ping-pong, so the `dom_selection_sync_depth` re-entry guard is removed.

Note: selection painting already reads the live range directly
(`render_selection()` reads `ds->ranges[0]`), so for selection rendering this is
a no-op; the migration work is repointing **caret** painting and any hit-test
code that still reads `CaretState`/`SelectionState`.

Acceptance rule: if legacy projection disagrees with canonical StateStore
selection, validation reports `SM_INV_CARET_PROJECTION` or
`SM_INV_SELECTION_PROJECTION`.

---

## 6. Contenteditable Editing Transactions

### 6.1 Transaction Object

Create one transaction object for all contenteditable mutations:

```cpp
struct EditingTransaction {
    uint64_t id;
    EditingSurface surface;
    EditingIntent intent;
    EditingSelection selection_before;
    EditingTargetRange target_ranges[4];
    uint32_t target_range_count;
    bool beforeinput_prevented;
    bool lambda_handled;
    bool mutation_applied;
    EditingSelection selection_after;
};
```

This object is the unit for logging, history, replay, undo, debugging, and
state-machine validation.

The `target_ranges[4]` inline cap covers every current intent (most have one
range). If an intent ever needs more, the runner must `log_*` the truncation
rather than silently drop ranges — target-range completeness is an InputEvent
Level 2 contract (§8.2).

`selection_before`/`selection_after` must be **frozen snapshots**, not the live
canonical `EditingSelection`: their boundaries are captured as static
(`is_live == false`) values so the envelopes do not retroactively rewrite the
recorded before-state when the DOM mutates. This is the same sanctioned-copy
exception as `getTargetRanges()` (§5.1). The live canonical selection is read
from StateStore; these fields are its immutable transaction-time copies.

### 6.2 Transaction Runner

Add a single runner:

```cpp
bool editing_run_transaction(EventContext* evcon,
                             const EditingSurface* surface,
                             const EditingIntent* intent,
                             const EditingTransactionHooks* hooks);
```

The runner owns the order:

1. Resolve and validate the active `EditingSurface`.
2. Snapshot canonical selection from StateStore.
3. Compute target ranges from the snapshot.
4. Enter a state-machine transition scope.
5. Dispatch `beforeinput` (via `editing_dispatch_beforeinput_ex()`).
6. If prevented, commit a no-mutation transaction log and exit.
7. Apply the default mutation when the surface supports default mutation.
8. Write the canonical selection after mutation through
   `state_store_set_selection()`.
9. Refresh DOM Selection facade/cache and legacy projection (driven by the
   `selection_mutation_seq` bump from step 8).
10. Dispatch `input` (via `editing_dispatch_input()`).
11. Record mutation and history metadata.
12. Request reflow/repaint.
13. Validate state-machine invariants.

Steps 5 and 10 reuse the existing `editing_dispatch_*` functions (§4.1) rather
than reimplementing event emission. The runner adds the surrounding
transaction: snapshot, target-range freeze, single-writer selection commit,
history, and validation.

### 6.3 Default Mutation Policy

The previous contenteditable proposal said the host never mutates by default.
The implementation has since moved toward useful browser-like default mutation
for the test page and simple JS pages. This proposal makes that explicit:

- Radiant may perform default DOM mutation for basic contenteditable editing.
- Model-backed Lambda templates may intercept `beforeinput` and prevent the
  default.
- JS pages may also prevent default and own mutation themselves.

Default mutation initially covers:

- `insertText`
- `insertParagraph`
- `insertLineBreak`
- `insertFromPaste`
- `insertFromDrop`
- `insertCompositionText`
- `insertFromComposition`
- `deleteContentBackward`
- `deleteContentForward`
- `deleteByCut`
- `deleteByDrag`

Structured commands such as table insertion, list nesting, mark toggles, and
block transforms can be added later as transaction types without changing the
event pipeline.

---

## 7. State Machine And State Schema Integration

The existing state schema already has the right vocabulary:

- `SM_FAMILY_SELECTION`
- `SM_FAMILY_IME`
- `SM_FAMILY_FORM_TEXT`
- `SM_INV_EDITING_INTERACTION`
- `SM_INV_CARET_PROJECTION`
- `SM_INV_SELECTION_PROJECTION`
- `SM_INV_DOM_SELECTION`

Extend it for contenteditable transactions.

### 7.1 New Events

Add state-machine events for rich editing:

```cpp
SM_EV_EDIT_TX_BEGIN
SM_EV_EDIT_BEFOREINPUT
SM_EV_EDIT_MUTATE_DOM
SM_EV_EDIT_SET_SELECTION
SM_EV_EDIT_INPUT
SM_EV_EDIT_TX_COMMIT
SM_EV_EDIT_TX_ABORT
```

These are not DOM events. They are internal StateStore/state-machine events
used to constrain ordering and validate invariants.

### 7.2 New Invariants

Add or strengthen invariants:

| Invariant | Meaning |
|---|---|
| `SM_INV_EDITING_SURFACE` | Active editing surface exists, is focus-compatible, and is not disabled/read-only. |
| `SM_INV_EDITING_SELECTION_HOST` | Contenteditable selection endpoints are inside the active editing host unless the selection is empty. |
| `SM_INV_EDITING_FALSE_ISLAND` | Mutation target is not inside a `contenteditable="false"` island. |
| `SM_INV_EDITING_TARGET_RANGES` | Target ranges are within the active surface and match the pre-mutation selection snapshot. |
| `SM_INV_DOM_SELECTION_CACHE` | DOM Selection facade revision matches StateStore selection revision. |
| `SM_INV_LEGACY_SELECTION_PROJECTION` | Paint-facing caret/selection projection matches canonical selection. |
| `SM_INV_INPUT_EVENT_ORDER` | `beforeinput` precedes mutation; `input` follows mutation and is skipped only on prevented/no-op transactions. |

### 7.3 Validation Points

Run validation at:

- transaction begin;
- after `beforeinput`;
- after mutation and selection write;
- event cascade settle via `radiant_state_settle()`;
- event simulation assertions when requested.

In debug builds, failed invariants should log enough context to identify:

- active surface;
- selection before/after;
- intent inputType;
- target ranges;
- DOM cache revision;
- legacy projection revision.

---

## 8. Full InputEvent Level 2

InputEvent Level 2 is an acceptance target, not a best-effort add-on.

### 8.1 Required Surface

`beforeinput` and `input` must expose:

- `inputType`
- `data`
- `dataTransfer`
- `isComposing`
- `getTargetRanges()`

`beforeinput` is cancelable. `input` is not cancelable.

`getTargetRanges()` returns immutable pre-mutation `StaticRange` snapshots
computed from the transaction's `selection_before` and intent. It must not
read the DOM after mutation.

### 8.2 Target Range Rules

Target ranges are computed once:

```text
selection_before + intent -> StaticRange[]
```

Examples:

- collapsed caret + `insertText` -> collapsed range at caret
- non-collapsed selection + `insertText` -> selected range
- collapsed caret + `deleteContentBackward` -> previous character range
- collapsed caret + `deleteWordBackward` -> previous word range
- `deleteByCut` -> selected range
- `insertFromPaste` -> selected range or collapsed caret
- composition update -> current composition replacement range

The transaction runner stores the target ranges and passes the same snapshot
to JS, Lambda, logging, and mutation.

### 8.3 DataTransfer

Clipboard and drop transactions carry a structured payload:

- `text/plain`
- `text/html`
- files/items when available
- source operation metadata for drop/move

Plaintext-only hosts must filter the payload before default mutation and before
the target consumer observes a default insertion payload.

### 8.4 Composition

Composition is transaction-based:

- `compositionstart` opens composition state.
- `insertCompositionText` replaces the active composition range.
- `compositionend` commits with `insertFromComposition` or clears with
  `deleteCompositionText`.

The active composition range is part of StateStore editing interaction state,
not local text-edit scratch state.

**Composition session model (adopted from Editing.md §5.4, landed).** A
composition is one session attached to the active `EditingSurface`, owned by
StateStore (`DocState::editing.composition`), with fields:

- `surface` — the editing surface captured at `compositionstart`;
- `anchor` — the boundary where preedit begins;
- `preedit_text` / `commit` lengths — uncommitted/committed text;
- `composition_caret` — caret offset inside the preedit, copied into each
  `EditingIntent`;
- `committed` — true only after `compositionend` with non-empty text.

**Active-surface pinning:** the session pins its surface across focus and
selection-projection changes until commit/cancel, so a focus shift mid-IME does
not retarget the composition (covered by the landed `test_editing_ime_focus_change`).
Form and rich surfaces share one entry point
(`editing_controller_handle_composition()` →
`radiant_dispatch_editing_composition_event()`); the controller owns surface
resolution and intent construction. Native IME candidate-window placement reads
the shared `editing_geometry` caret-rect facade.

---

## 9. History And Logging

Once mutation is transactionized, history becomes a natural layer instead of a
patchwork.

Each committed mutation transaction records:

- transaction id;
- inputType;
- surface;
- target ranges;
- selected text/html before mutation where cheap;
- DOM mutation summary;
- selection before/after;
- composition state;
- clipboard/drop metadata when relevant.

Initial history can be simple:

- coalesce adjacent `insertText` transactions;
- separate history boundaries for Enter, paste, cut, drop, IME commit;
- do not require full document snapshotting at first;
- expose enough log data to debug editing failures.

### 9.1 Undo/Redo Ownership (adopted from Editing.md §7.2)

One command vocabulary, three storage strategies by surface:

| Surface | Undo owner | Behavior |
|---|---|---|
| `<input>` / `<textarea>` | Radiant `EditHistory` ring | `historyUndo`/`historyRedo` restore value + selection snapshots after a cancelable `beforeinput` |
| `contenteditable` | Consumer (JS/Lambda) | Radiant dispatches `beforeinput { historyUndo/historyRedo }`; the model decides |
| Lambda `edit <...>` | Lambda edit session | Bridge to `edit_undo()` / `edit_redo()` where available |

All three enter through the same transaction runner. Radiant owns local history
only for text controls; rich/Lambda hosts always receive intents.

### 9.2 Editing Log Vocabulary (adopted from Editing.md §8, landed)

The shared controller (not per-surface branches) emits first-class JSONL
records to `./temp/`:

| Record | When | Key fields |
|---|---|---|
| `editing.surface` | Surface resolved/activated | `kind`, `mode`, `owner`, `readonly`, `disabled`, `false_island` |
| `editing.intent` | Input decoded into intent | `inputType`, `data_len`, `is_composing`, `source_event` |
| `editing.beforeinput` | Beforeinput dispatched | `inputType`, `range_count`, `prevented` |
| `editing.mutation` | Mutation committed | `old_len`, `new_len`, `selection_start`, `selection_end` |
| `editing.selection` | Selection operation | `operation`, `anchor`, `focus`, `drag_mode` |
| `editing.clipboard` | Copy/Cut/Paste/Drop | `operation`, `text_len`, `html_len`, `redacted` |
| `editing.focus` | Surface activate/deactivate | `from`, `to`, `ime_active`, `focus_visible` |
| `editing.history` | Undo/redo push/restore | `action`, `depth`, `cursor`, `owned_by` |
| `editing.autoscroll` | Drag auto-scroll | `surface`, `dx`, `dy`, `velocity_x`, `velocity_y` |
| `editing.composition` | IME lifecycle | `phase`, `preedit_len`, `commit_len`, `caret` |

**Privacy rule (mandatory, carried over from Editing.md §8).** The records log
`data_len`, not `data` — but lengths, offsets, and caret positions still leak
typing structure. For `EDIT_MODE_PASSWORD_TEXT` and any consumer-marked
sensitive surface, the controller **MUST** redact: emit `kind`/`inputType` for
ordering but zero out `old_len`/`new_len`/`preedit_len`/`commit_len` and
selection offsets. JSONL in `./temp/` is easy to forget — never let plaintext or
reconstructable length sequences reach it for a password field.

### 9.3 Lambda Templates And Default History

Undo/redo for model-backed Lambda templates remains consumer-owned, but Radiant
still emits `historyUndo` / `historyRedo` intents through the same transaction
runner. For default contenteditable mutation, Radiant may maintain a DOM-edit
history ring later.

---

## 10. Plaintext-only, False Islands, And Cross-surface Boundaries

### 10.1 Plaintext-only

`contenteditable="plaintext-only"` uses the same transaction runner with a
different default mutation policy:

- paste/drop consume only `text/plain`;
- HTML markup is stripped before insertion;
- `insertParagraph` becomes `insertLineBreak` or a newline insertion;
- structural commands are rejected as no-op transactions;
- `getTargetRanges()` remains accurate.

### 10.2 False Islands

`contenteditable="false"` descendants are non-mutable islands inside a host.

Rules:

- selection may cross island boundaries when the selection model allows it;
- default mutation must not place insertion inside a false island;
- delete around an island treats it as an atomic boundary unless a later
  command explicitly supports structured deletion;
- target range validation rejects mixed or invalid mutation targets.

These rules belong in state-schema guards and invariants, not scattered local
checks.

### 10.3 Cross-surface Boundaries (adopted from Editing.md §6.2, landed)

Now that Editing 2 folds text controls into one selection authority (Phase ED2-1 D),
an embedded `<input>`/`<textarea>` inside a contenteditable host is a distinct
editing surface, and gestures across the boundary need defined behavior:

- a drag that **begins inside** a text control clamps to that control's value
  region; it never extends into surrounding host DOM text;
- a drag that **begins in a host** and crosses into an embedded control treats
  the control as an atomic, non-enterable unit (selection skips over it,
  matching browsers), unless the control is the drag origin;
- a selection that legitimately spans an editing-host boundary collapses to the
  host's own selection on mutation; the runner **rejects mutations whose target
  range straddles two surfaces**.

These are already enforced via `editing_geometry_surface_contains_target_range()`
and pinned by the landed `test_editing_mixed_selection_clamp` fixture. Under
Editing 2 they become target-range invariants (`SM_INV_EDITING_TARGET_RANGES`,
`SM_INV_EDITING_SURFACE`) rather than local checks.

---

## 11. Implementation Plan

### Phase ED2-1 - StateStore Selection Authority

The boundaries are already single-store (`DocState::live_ranges`); this phase
consolidates *authority*, not storage. Each sub-phase ships independently with
baseline tests green.

- **A. Shadow.** Add `DocState::sel` (`EditingSelection`) as a *derived view* of
  today's `DomSelection` (`sel.range = ranges[0]`, `sel.direction = …`). Change
  no writers. Add a debug invariant that `sel` agrees with `DomSelection`. This
  validates the live-range-pointer model — including the focused-but-no-visible-
  selection caret case — before any authority flips.
- **B. Collapse the copies.** Delete `DomSelection::anchor`/`focus`; compute the
  scalar accessors from `ranges[0]` + `direction`. One boundary store remains:
  the `DomRange`.
- **C. Legacy → pull-derived.** Replace `legacy_sync_from_dom_selection()` with
  `state_store_refresh_caret_projection()` (rebuild from `sel` on
  `selection_mutation_seq` change). Remove `dom_selection_sync_depth`. Repoint
  `caret_set()` / `selection_set()` writers through `state_store_set_selection()`.
- **D. Unify text controls.** Fold text-control selection into `sel`
  (`kind == EDIT_SEL_TEXT_CONTROL`), reusing the `editing_geometry`
  `EditingBoundary` for offsets. One selection authority across contenteditable
  and form controls. This replaces the synthetic-form-range approach landed
  under the phased-out [Radiant_Design_Editing.md](Radiant_Design_Editing.md) E3.
- **E. DOM facade.** Route `getSelection()` mutators and the
  `js_dom_selection.cpp` writers through `state_store_set_selection()`;
  `getRangeAt()` returns the wrapper for `sel.range`.

Order matters: writer first, then route DOM/JS/legacy through it, then delete the
sync hooks. Reuse `selection_mutation_seq` as the single revision counter and
preserve the Phase 8D `selectionchange` coalescing — the canonical writer
assumes the seq-bump and `selectionchange` queueing currently performed on the
sync path.

Exit criteria:

- caret, selection painting, event simulation, and `window.getSelection()` all
  observe the same `selection_mutation_seq`;
- no rich editing path writes legacy projection directly;
- `dom_selection_sync_depth` and `legacy_sync_from_dom_selection()` are gone;
- `selectionchange` WPT coverage does not regress.

### Phase ED2-2 - Transaction Runner

- Introduce `EditingTransaction`.
- Move rich insertion, delete, cut, paste, Enter, and drop behind
  `editing_run_transaction()`.
- Preserve existing behavior while consolidating ordering.

Exit criteria:

- all contenteditable UI tests pass through the transaction runner;
- event logs show transaction begin/commit and beforeinput/input order.

### Phase ED2-3 - State Schema Integration

- Add rich editing state-machine events.
- Add invariants for surface, selection host, target ranges, DOM cache, and
  legacy projection.
- Validate at transaction boundaries and cascade settle.

Exit criteria:

- event sim can assert state validity after each rich edit;
- stale caret/selection projection bugs become invariant failures.

### Phase ED2-4 - InputEvent Level 2 Completion

- Audit and complete `InputEvent` fields.
- Ensure `getTargetRanges()` is pre-mutation and immutable.
- Complete `dataTransfer` for paste/drop.
- Complete `isComposing` semantics.

Exit criteria:

- focused WPT Input Events Level 2 subset passes or has documented
  non-legacy skips;
- UI tests cover data, html, target ranges, composition, paste, cut, drop.

### Phase ED2-5 - Plaintext-only And False Islands

- Enforce plaintext-only mutation policy.
- Enforce false-island mutation guards.
- Add edge-case tests for selection and delete around false islands.

Exit criteria:

- plaintext-only paste/drop strips markup;
- mutations inside false islands are rejected;
- selection projection remains valid across island boundaries.

### Phase ED2-6 - History And Rich Logs

- Record transaction summaries.
- Add coalescing hooks for simple text insertion.
- Add history boundary metadata.

Exit criteria:

- logs are sufficient to replay or diagnose simple editing sessions;
- default contenteditable history can be implemented without changing the
  transaction API.

---

## 11. Implementation Progress

Last updated: 2026-06-13.

### Landed

- **ED2-1 A-D:** `DocState::sel` is the canonical selection record, with DOM
  selection and legacy caret/selection maintained as projections. Text-control
  selection is folded into the same selection authority. Legacy selection facade
  APIs route through StateStore writers for the audited rich-editing paths.
- **ED2-1 E partial:** JS Selection and rich-editing mutators have been routed
  through StateStore selection APIs where audited. Remaining work is the final
  deletion/fencing pass for legacy compatibility wrappers that are still
  callable by controller, state-machine, and event-sim paths.
- **ED2-2 substantial:** Rich insertion, deletion, paste/cut, IME, simulated
  drop, and live pointer drag/drop route through the defaultable transaction
  path. Live pointer drop stores the drop target boundary/range before commit so
  it uses the same transaction target snapshot as simulator drop.
- **ED2-2 runner-only cleanup partial:** The late rich/contenteditable key
  fallback in `event.cpp` is fenced so legacy select/copy/cut/delete projection
  writes cannot run for rich surfaces after the controller/transaction path.
- **ED2-4 substantial:** `InputEvent` now carries stable pre-mutation target
  ranges, rich paste/drop `dataTransfer`, nullable rich transfer `data`,
  composition lowering for `insertCompositionText`, and form
  `beforeinput`/target-range dispatch through the unified editing dispatcher.
- **ED2-4 residual cleanup:** The legacy text-control weak `beforeinput` bridge
  (`te_dispatch_beforeinput()` / `js_dom_queue_textcontrol_beforeinput()`) is
  retired. Legacy text-control mutation helpers only emit post-mutation `input`;
  live editing must use the unified dispatcher so cancellable `beforeinput` is
  available.
- **DataTransfer JS surface:** `DataTransferItemList.item()`,
  `DataTransferItem.getAsFile()`, `DataTransferItem.getAsString()`,
  stable `DataTransfer.files`, and stable `DataTransfer.types` are implemented
  for string/File/Blob items added through `DataTransferItemList.add()`.
- **Clipboard extraction cleanup:** Clipboard copy/cut extraction has been
  routed through canonical StateStore selection snapshots for the audited rich
  and form paths, leaving legacy fallbacks only as compatibility surface while
  projection migration finishes.

### Still Open

- **ED2-1 facade deletion:** `caret_set`, `selection_set`,
  `selection_start`, `selection_extend`, and related compatibility APIs remain
  callable. They must stay fenced as projection/compat wrappers, and new
  contenteditable code must not add direct legacy writes.
- **ED2-2 old helper retirement:** `event.cpp` still contains rich-editing
  helper code and policy branches that should continue shrinking toward
  surface resolution plus `EditingIntent` creation plus
  `editing_run_transaction()`.
- **ED2-2 commit ownership:** Reflow/repaint, selectionchange coalescing, and
  rich commit side effects are mostly centralized, but the remaining ad-hoc
  paths in `event.cpp` need one more audit before the cleanup inventory can be
  marked complete.
- **Native OS file-drop transport:** The JS `DataTransferItem` File/Blob
  surface exists, but native drop events still carry string/html payloads
  through `RdtEvent`; OS file-path/blob transport is not yet wired into rich
  drop transactions.
- **ED2-3 schema integration:** State-schema validation at every transaction
  boundary and cascade settle remains incomplete.
- **ED2-6 history/logging:** Transaction log richness and history coalescing
  remain future work.

---

## 12. Legacy Cleanup Inventory

This phase should explicitly retire or fence the legacy editing paths that make
selection and mutation authority ambiguous. "Remove" below means delete once
the replacement is landed; "fence" means keep only as a projection/compat API
that writes through the new StateStore transaction layer.

Framing: range **boundaries are already single-store** in
`DocState::live_ranges`, and `DomSelection::anchor`/`focus` are already derived.
The cleanup is therefore deleting the legacy *projection push-sync* and the
duplicate stored anchor/focus — not building a new canonical store. The
`editing_*` layer below already exists and must be folded into the runner, not
shadowed.

| Area | Current code | Cleanup action |
|---|---|---|
| Legacy canonical caret storage | `DocState::caret` in `radiant/state_store.hpp` | Fence as paint/hit-test projection only, rebuilt from a collapsed `EditingSelection`. It must not be written directly by contenteditable code. |
| Legacy canonical selection storage | `DocState::selection` in `radiant/state_store.hpp` | Fence as paint/event-sim projection only. Canonical boundaries live in the live `DomRange` (`DocState::live_ranges`) referenced by `EditingSelection`. |
| Duplicate stored anchor/focus | `DomSelection::anchor` / `DomSelection::focus` and the write half of `sync_anchor_focus()` | Remove. Compute scalar accessors from `ranges[0]` + `direction`. The live `DomRange` is the sole boundary store. |
| Bidirectional sync guard | `DocState::dom_selection_sync_depth` | Remove after DOM Selection becomes a facade/cache and there is no DOM-to-legacy ping-pong. |
| DOM Selection as independent owner | `DocState::dom_selection`, `DomSelection::ranges`, direct `dom_selection_*` mutation calls | Fence behind StateStore writers. DOM Selection APIs may exist, but their mutators must call StateStore. |
| Legacy sync hook | `legacy_sync_from_dom_selection()` in `radiant/dom_range.cpp` / `radiant/state_store.cpp` | Remove as a mutation mechanism. Replace with `state_store_refresh_selection_projection()`. |
| Legacy storage attachment | `dom_selection_attach_legacy_storage()` | Remove once `CaretState` / `SelectionState` are no longer owned or initialized through `DomSelection`. |
| Direct caret writes | `caret_set()`, `caret_clear()`, `caret_set_visible()` call sites in rich editing paths | Replace rich-editing writes with `state_store_set_editing_selection()` or transaction commit. Keep projection refresh internals private. |
| Direct selection writes | `selection_set()`, `selection_start()`, `selection_update()`, `selection_select_all()`, collapse helpers | Replace rich-editing callers with StateStore selection transactions. Keep wrappers temporarily for text controls and tests, then route them through the canonical writer. |
| Direct DOM selection writes from JS | `js_selection_collapse`, `js_selection_set_position`, `js_selection_collapse_to_start/end`, `js_selection_set_base_and_extent` in `lambda/js/js_dom_selection.cpp` | Change these to call StateStore selection APIs. They should not mutate `DomSelection` directly. |
| Rich default mutation helper | `rich_text_default_replace()` in `radiant/event.cpp` | Move logic into `editing_run_transaction()` default mutation phase, then delete the helper. |
| Rich delete selection helper | `rich_delete_dom_selection()` in `radiant/event.cpp` | Replace with transaction delete operation that uses target ranges and commits selection through StateStore. |
| Rich selection special cases | `rich_selection_single_text_range()`, `rich_selection_is_host_child_range()`, `rich_find_text_descendant()` | Delete or move into transaction target-range/mutation helpers. Avoid local fallback selection semantics in `event.cpp`. |
| Rich beforeinput wrappers | `dispatch_rich_beforeinput()`, `dispatch_rich_beforeinput_defaultable()`, `dispatch_rich_input()` in `radiant/event.cpp` | Collapse into transaction runner hooks. `event.cpp` should create an intent and call the runner. |
| Existing dispatch layer | `editing_dispatch_beforeinput_ex()`, `editing_dispatch_input()`, `editing_dispatch_form_beforeinput()` in `radiant/editing_dispatch.cpp` | Keep and reuse. The runner calls these for the beforeinput/input steps (§4.1); do not reimplement event emission inside the runner. |
| Editing surface / intent types | `EditingSurface` (`radiant/editing.hpp`), `InputIntent`/`EditingIntent` (`radiant/editing_intent.hpp`) | Reuse as-is. The runner and `EditingSelection` consume these; do not redefine. |
| Editing boundary type | `EditingBoundary` / `EditingBoundaryKind` (`radiant/editing_geometry.hpp`) | Reuse for text-control offsets. Do not introduce a second `EditingBoundary`. |
| Clipboard copy from current selection | `copy_current_selection_to_clipboard()` depending on mixed legacy/DOM selection extraction | Route through canonical StateStore selection snapshot. Clipboard HTML/plain extraction should use transaction target ranges or canonical selection. |
| Selection extraction fallback | `extract_selected_text()` / `extract_selected_html()` fallback to `state->selection` | Prefer canonical StateStore selection. Keep legacy fallback only while text-control projection migration is incomplete. |
| Text-control weak beforeinput bridge | `te_dispatch_beforeinput()` and `js_dom_queue_textcontrol_beforeinput()` | **Done.** Weak beforeinput queue removed; live text-control beforeinput is owned by `editing_dispatch_form_beforeinput()`. Legacy no-context text mutation helpers emit only post-mutation `input`. |
| Event.cpp rich policy branches | Keyboard, text-input, paste, cut, drop, IME branches that special-case rich editing and manually call mutation/sync helpers | **Partial.** Main edit paths route through transaction/controller flow; the late rich key fallback is fenced from rich surfaces. Continue reducing the remaining branch code to surface resolution plus `EditingIntent` creation plus `editing_run_transaction()`. |
| Ad-hoc reflow/repaint after rich mutation | local `doc_state_request_reflow()`, `need_repaint`, `state->needs_repaint` calls in rich mutation helpers | Move to transaction commit so every mutation invalidates layout consistently. |
| Ad-hoc selectionchange queuing | direct selection-change side effects scattered through DOM selection mutators | Queue from the StateStore canonical selection writer, using revision changes to coalesce. |
| Event simulation assumptions | assertions that read only legacy caret/selection projection | Update to optionally assert canonical selection revision, DOM facade, and projection agreement. |

The cleanup order matters:

1. Add the canonical StateStore selection writer first.
2. Route DOM Selection API writes through it.
3. Route rich default mutation through the transaction runner.
4. Make legacy caret/selection projection private to StateStore refresh code.
5. Delete sync hooks and direct rich mutation helpers.

Until the final deletion step, legacy APIs should remain available but visibly
marked as compatibility wrappers. New contenteditable code must not add new
call sites to the legacy mutation APIs.

---

## 13. Test Strategy

Priority test groups:

1. StateStore selection authority:
   - DOM Selection API writes;
   - mouse selection;
   - keyboard selection;
   - reflow after mutation;
   - legacy projection agreement.
2. Transaction order:
   - `beforeinput` prevention;
   - default mutation;
   - `input` after mutation;
   - logging and state validation.
3. InputEvent Level 2:
   - `getTargetRanges()` before mutation;
   - `data`;
   - `dataTransfer`;
   - `isComposing`;
   - cancellation.
4. Clipboard and drop:
   - plain and HTML payloads;
   - cut collapse;
   - paste over selection;
   - drop move/delete/insert pairs.
5. Plaintext-only and false islands.
6. IME composition transaction sequences (including focus shift mid-composition
   pinning the active surface).
7. Cross-surface boundaries (mixed fixture: drag clamps to origin control,
   host-originated drag skips embedded control, straddling mutation rejected).
8. Logging and privacy: `editing.*` records reconstruct an edit cascade;
   `EDIT_MODE_PASSWORD_TEXT` emits no reconstructable plaintext or length
   sequences.

Legacy negative API tests are low value for this proposal. Keep small smoke
coverage if already present, but do not make it a gating track.

---

## 14. Acceptance Criteria

- StateStore is the single source of truth for contenteditable selection.
- DOM Selection is a facade/cache over StateStore, with revision validation.
- Legacy caret/selection state is projection-only.
- All contenteditable mutations go through one transaction runner.
- The transaction runner uses state-machine scopes and state-schema
  invariants.
- Full InputEvent Level 2 payload is available for rich editing transactions.
- `beforeinput` cancellation prevents default mutation.
- `input` fires only after successful mutation or consumer-handled mutation.
- Clipboard/drop payloads preserve plain text and HTML where available.
- Plaintext-only and false-island semantics are enforced by transaction guards.
- Event logs and state validation expose enough context to debug editing bugs.
- Legacy rich editing mutation/sync helpers listed in the cleanup inventory are
  either deleted or fenced as compatibility wrappers.

---

## 15. Summary

Contenteditable 1 made Radiant editable. Editing 2 makes it reliable and
unifies it: contenteditable hosts, form text controls, and Lambda editor
templates share one editing authority.

The central move is to stop treating DOM Selection, legacy selection projection,
and rich mutation code as separate authorities. StateStore becomes the editing
state authority; DOM Selection becomes the web API facade; every edit (rich or
form) becomes a single state-machine transaction; InputEvent Level 2 becomes the
contract between platform input, JS, Lambda templates, logging, history, and
future rich editing features.
