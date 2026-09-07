# Radiant Edit History — Unified UA History for Editable DOM Surfaces

**Date:** 2026-09-07
**Status:** Proposal — awaiting ratification; not yet implemented
**Parent design:**
[Radiant_Design_Editable.md](Radiant_Design_Editable.md), especially §20
**Implementation-plan impact:**
[Radiant_Impl_Editable2.md](Radiant_Impl_Editable2.md) must replace its
`EditPlan` vocabulary and history sections when this proposal is ratified.
**Scope:** user-agent editing history for rich/plain `contenteditable`,
`designMode`, `<input>` text controls, and `<textarea>`; Lambda-node storage,
native Radiant DOM-waist application, grouping, retention, and lifecycle.

> **Formal linkage.** D7.2.5 assigns `contenteditable` history and editing
> policy to the shipped Lambda DOM behavior package. D7.2.1 requires package
> state to live in per-context storage; D7.5.3 requires an explicit
> Lambda↔Radiant waist; D5.3.3 requires precise native roots; D1.10 requires
> executable enforcement. S9.1.4 and S9.1.7 forbid captured or module-global
> mutable history state. This proposal preserves those rulings except for the
> form-control ownership expansion described below.

> **Conflict notice.** The current parent design §20.5 keeps `<input>` and
> `<textarea>` on their existing native `EditHistory` ring. This proposal
> replaces that decision: all UA editing surfaces use one package-owned,
> Lambda-node history architecture. Ratification therefore requires an
> in-place D7.2.5v2 revision, the formal-document version bump required by
> `doc/Doc_Convention.md`, and matching revisions to the parent design and
> implementation plan. Until then, the existing formal and parent rulings
> remain authoritative.

---

## 1. Decision summary

The browser-UA history model is one **document-owned history service** with
one **independent linear timeline per editing surface**.

1. All durable history state is represented by tagged Lambda elements,
   rooted from one per-document package-state slot.
2. A `contenteditable` editing host, a `designMode` document, an `<input>` text
   control, and a `<textarea>` each own an independent timeline.
3. The focused/command-target surface selects the timeline for undo or redo.
   Focus changes never combine timelines and do not make undo jump to a
   different surface.
4. One logical UA edit is an immutable `<edit-step>`. This name replaces
   `EditPlan` in the D7.2.5 design. A step contains ordered primitive
   operations and their precise inverses.
5. One `<edit-history-entry>` contains one or more coalesced edit steps plus
   the state before and after the group.
6. Lambda chooses the edit, creates the forward and inverse operations,
   groups it, and constructs the next history root. The native `radiant-dom`
   waist validates and atomically applies the step while swapping the rooted
   history value.
7. Each timeline retains at most **256 entries**. All timelines in one
   document collectively retain at most **16 MiB** (`16,777,216` bytes),
   measured by the exact rule in §9.
8. The existing native `EditHistory` snapshot ring for `<input>` and
   `<textarea>` is retired after parity. Form value and selection remain
   native mechanisms; their history policy and history values move to the
   Lambda package.

The design is an in-memory undo service. It is not browsing-session history,
durable file history, crash recovery, or a collaboration log.

---

## 2. Goals and non-goals

### 2.1 Goals

- Give ordinary UA-edited rich DOM and plain-text controls one history
  architecture without conflating their storage models.
- Preserve exact DOM structure, retained node identity where observable,
  Selection direction, form-control selection, and collapsed typing state.
- Make input intents and `execCommand("undo"|"redo")` use the same timeline
  and application path.
- Make history a Lambda value that can be inspected, validated, measured,
  logged by summary, and tested without teaching native code command policy.
- Keep a failed edit, failed history-state update, or failed undo atomic.
- Bound both history depth and retained memory deterministically.
- Prevent UA history from recording an edit owned by CodeMirror,
  ProseMirror, Editor.js, a Radiant model editor, or page script.
- Meet the WPT and pinned Chromium gates required by D7.2.5 and D1.10.

### 2.2 Non-goals

- This does not merge `window.history` with editing history.
- This does not replace the private history model of an author editor that
  cancels or claims the UA action.
- This does not make separate editing surfaces share one undo order.
- This does not serialize edit history into HTML, a Mark document, session
  storage, local storage, or a file.
- This does not introduce branching history. Redo is a linear suffix and is
  discarded by a new committed forward edit.
- This does not make native C/C++ interpret commands, block/list structure,
  normalization policy, or grouping policy.
- This does not require the Lambda rich-editor model's existing
  `mod_step.ls` representation to become the live-DOM representation. The two
  share the step/inversion architecture, but one addresses immutable model
  paths and the other addresses live DOM/control handles.

---

## 3. Overall architecture

```text
platform input or execCommand
        |
        v
common Radiant gate: resolve surface, dispatch cancellation
        |
        v
Lambda DOM behavior package
  descriptor -> context -> <edit-step forward + inverse>
        |
        +---- read <edit-history> from document package slot
        +---- coalesce/append, enforce 256-entry + 16-MiB limits
        +---- construct next immutable <edit-history>
        |
        v
radiant-dom atomic edit waist
  validate step + slot revision + DOM epoch
  apply primitive operations using a private rollback journal
  update DOM/control Selection and queue ordinary observations
  replace the rooted package-state value
  commit, or roll everything back
        |
        v
input / selectionchange / MutationObserver / layout checkpoint
```

### 3.1 Ownership

| Concern | Owner |
|---|---|
| command and input-intent history class | Lambda DOM behavior package |
| surface/timeline selection | Lambda package, from native-resolved facts |
| forward and inverse `<edit-step>` construction | Lambda package |
| grouping, coalescing, cursor movement, redo invalidation | Lambda package |
| entry/depth/byte pruning | Lambda package |
| persistent package slot and GC root | generic native document state mechanism |
| live handle validation and DOM epoch | native Radiant DOM mechanism |
| atomic DOM/control mutation and rollback | native Radiant DOM waist |
| live Range/Selection adjustment | native DOM/Selection mechanism |
| MutationObserver records and invalidation | native DOM mechanism |
| author-editor history | the editor that canceled or claimed the edit |

This is the D7.2.5 boundary: policy is Lambda; checked mutation mechanics are
native. The native layer can reject an invalid operation but cannot select a
different edit or manufacture a fallback history entry.

### 3.2 Independent timelines

There is exactly one timeline for each effective editing surface:

- the root editing host for a `contenteditable` selection;
- the document surface while `designMode` is `on`;
- each eligible `<input>` element;
- each `<textarea>` element.

Editable descendants share their root host's timeline. A nested editing host
has a separate timeline. `contenteditable="false"` descendants create no
timeline. A control's timeline belongs to the DOM element, not its transient
`FormControlProp` or layout `View`.

The focused surface handles keyboard undo/redo. `execCommand("undo")` and
`execCommand("redo")` use the command context's effective surface. If there is
no eligible current surface, they return the compatible failure value and do
not fall back to the most recently used timeline.

An operation that legitimately touches more than one surface—for example a
UA-owned move between editing hosts—is stored once on the initiating
surface's timeline. Its entry lists every participant. It is never duplicated
into two timelines, because two independently undoable copies could apply the
same mutation twice.

### 3.3 Lifecycle

The history service is created lazily on the first recordable edit or history
query and lives until document teardown. Replacing `<body>` does not destroy
the physical package slot. Surface pruning rules in §10 decide whether an
individual timeline survives a DOM change.

The service is confined to the document's owning `EvalContext`. It cannot be
read or written under another runtime context. No history node or borrowed
DOM handle survives an asynchronous suspension; D7.2.5 editing dispatch stays
synchronous.

---

## 4. Exact physical storage

### 4.1 The native slot

`StateStore` gains one generic package-state registry:

```text
StateStore.package_state_slots : HashMap<(module_id, slot_name), PackageStateSlot*>

PackageStateSlot = {
  module_id:     uint32,
  slot_name:     NameId,
  value:         Item,
  revision:      uint64,
  owner_context: EvalContext*,
  rooted:        bool
}
```

The history package opens the slot named `edit-history`. The calling module's
instantiated `module_id` is implicit; package code cannot impersonate another
module by passing its name. The physical key is therefore:

```text
(active Lambda DOM behavior package module_id, NameId("edit-history"))
```

The slot contains exactly one value: `null` before initialization, then the
root `<edit-history>` element. Native metadata contains no timeline, command,
selection, entry, or grouping field.

Each `PackageStateSlot` is an individually allocated, stable-address object
owned by the document pool. The `HashMap` stores only its pointer; rehashing
must never move a registered `value.item` root slot.

On slot creation, native code registers `&slot.value.item` against
`slot.owner_context` using the precise persistent-root API required by
D5.3.3. Replacing the value writes through that already registered slot and
increments `revision`; it never unregisters/re-registers around an update.
Document teardown unregisters the root under the same owner context before
freeing the slot registry.

This registry belongs on `StateStore`, not a layout `View`, a
`FormControlProp`, a process global, or a module global:

- `StateStore` already has document lifetime and the owning semantic context;
- it survives relayout and form-prop reconstruction;
- the registered `Item` keeps the complete Lambda history graph live;
- teardown has one deterministic root-release point.

### 4.2 Generic package-state API

The built-in `import dom` mechanism exposes these package-private operations
to a shipped behavior package:

| Operation | Exact contract |
|---|---|
| `dom.package_state_open(document, slot_name)` | Return an opaque slot handle scoped to the calling module and document; create a rooted `null` slot at revision `0` if absent. |
| `dom.package_state_read(slot)` | Return `<package-state revision:R value:V>` without cloning `V`. The result and `V` are caller-rooted borrows for the synchronous call. |
| `dom.package_state_compare_replace(slot, expected_revision, value)` | Replace the value only if the slot and revision are live; return `<package-state-write ok:true revision:R2>` or a stable failure. Used for state-only changes with no DOM mutation. |
| `dom.package_state_drop(slot, expected_revision)` | Replace with `null`, increment the revision, and invalidate the handle during document teardown/reset. |
| `dom.retained_size(value)` | Return the exact retained-byte charge defined in §9 without modifying the value. |

These are generic package mechanisms. None contains `history`, `undo`,
`command`, or `inputType` policy beyond the package-chosen slot name.

The behavior package itself exposes no history object to page JavaScript. Its
private `edit_history.ls` module has this exact functional surface:

| Function | Result |
|---|---|
| `history_open(document)` | `<history-session slot:S slot_revision:R state:<edit-history>>`; initialize schema 1 when the slot is null. |
| `history_record(session, step)` | `<history-transition step:step next_state:H entry_id:E group_id:G>` after grouping and pruning. |
| `history_undo(session, surface)` | One composed history-origin step plus the proposed cursor-decremented root, or null when disabled. |
| `history_redo(session, surface)` | One composed history-origin step plus the proposed cursor-incremented root, or null when disabled. |
| `history_barrier(session, surface, reason, current)` | A new root with `active_group_id:null`, updated current state/external epoch, and any required redo invalidation. |
| `history_forget_surface(session, surface, reason)` | A new root without that timeline; also clear any owning timeline whose entries name the surface as a participant, making all affected retained handles unreachable. |
| `history_can_undo(state, surface)` / `history_can_redo(state, surface)` | Pure Boolean cursor queries. |
| `history_validate(state)` | Pure schema/invariant validation used by tests and the native boundary. |

`<history-session>` and `<history-transition>` are ephemeral Lambda elements.
Only `<edit-history>` and its descendants are stored in the persistent slot.

### 4.3 Atomic edit-and-state API

DOM/control mutation and the package root must commit together. The waist
therefore exposes one atomic operation:

```text
dom.edit_apply_step(
  step: <edit-step>,
  slot: PackageStateSlotHandle,
  expected_slot_revision: uint64,
  next_state: <edit-history>
) -> <edit-step-result>
```

Its ordering is binding:

1. retain/root all arguments;
2. validate the slot owner and revision;
3. validate the step schema, surface, participants, live handles,
   preconditions, expected DOM mutation epoch, and package-provided root;
4. construct a private native rollback journal;
5. apply `forward_ops` in order;
6. apply the step's final selection/control-selection state;
7. queue normal Range adjustment, observer records, and invalidation without
   delivering callbacks yet;
8. replace the already rooted slot value with `next_state` and increment its
   revision;
9. commit the native journal and publish the result.

If any operation through step 7 fails, native code reverses its private
journal, restores Selection and pending observation/invalidation queues, and
does not replace the package slot. Validation failure changes nothing. The
slot assignment in step 8 is allocation-free and cannot fail after validation.

The durable `inverse_ops` supplied in the Lambda step are not native's private
rollback journal. The former power a later user-visible undo; the latter exist
only to make the current call atomic.

For schema 1, root validation includes `revision = old.revision + 1`, an
unchanged `document_id`, `step.id = old.next_step_id`, and
`next_state.next_step_id = step.id + 1`, plus the invariants and cached byte
total in §§5, 9, and 14. The native boundary validates these structural facts;
it does not choose entries, grouping, cursor movement, or pruning victims.

---

## 5. Canonical Lambda-node schema

All durable state uses the following tagged Lambda elements. Tag names and
field spellings are part of schema version 1. Arrays preserve order. Symbols
are used for closed enums; strings are used for browser-provided data.

### 5.1 History root

```text
<edit-history
  schema: 1
  revision: 0
  document_id: 7
  total_bytes: 0
  next_step_id: 1
  next_entry_id: 1
  next_group_id: 1
  next_activity_seq: 1
  active_surface_key: null
  timelines: []
>
```

`revision` mirrors the package's logical revision and advances with every
successful slot update. It need not equal the native slot revision, but a
successful replacement increments both by one. `document_id` is stable for
the document lifetime. `total_bytes` is the cached §9 charge of this root with
that scalar normalized to zero while measuring. The post-commit DOM epoch is
returned by native code rather than guessed in the immutable proposed root.

`next_step_id` supplies a document-unique ID to every successfully applied UA
or history-origin step, including an applied step that cannot itself be
retained. A failed candidate may reuse its uncommitted ID. `next_activity_seq`
supplies one document-wide monotonic sequence number for each successful
recorded forward edit, undo, or redo. A new or coalesced entry receives that
number as `commit_seq`, the affected timeline receives it as `last_used_seq`,
and the root then increments it. State-only selection refreshes and
failed/no-history edits do not consume an activity sequence number.

`active_surface_key` is null when no eligible surface is current; otherwise
it is the key resolved by the common focus/command-target gate. A focus or
effective-host transition updates this field with a state-only slot
replacement and closes the old and new timelines' active groups without
combining or traversing their entries.

### 5.2 Surface identity

```text
<edit-surface
  key: "document-id/node-id/kind"
  kind: 'contenteditable | 'design-mode | 'text-input | 'textarea
  owner: OpaqueDomNodeHandle
  document_id: 7
  node_id: 42
  host_mode: 'rich | 'plaintext | 'control
>
```

`owner` is a precisely rooted, native-validated DOM handle containing a
`DomNodeRef`; it is not a raw pointer exposed to Lambda. Stable `document_id`
plus monotonic `node_id` makes the textual key diagnostic and collision-free
for the document lifetime; the key never changes when the DOM mutation epoch
advances. Native identity comes from `owner`, not from reparsing the key. A
`designMode` surface uses the `Document`'s stable root handle and the
`'design-mode` discriminator, so it cannot collide with an ordinary
`contenteditable` host on the same element.

### 5.3 Timeline

```text
<edit-timeline
  surface: <edit-surface ...>
  entries: []
  cursor: 0
  current: <edit-state ...>
  active_group_id: null
  last_used_seq: 0
  external_epoch: 0
  total_bytes: 0
>
```

`entries` are chronological. `cursor` is the number of entries currently
applied and satisfies `0 <= cursor <= len(entries)`:

- undo applies `entries[cursor - 1]` and decrements `cursor`;
- redo applies `entries[cursor]` and increments `cursor`;
- a new forward edit deletes `entries[cursor..]`, appends/coalesces its entry,
  and sets `cursor = len(entries)`.

This cursor form is canonical. Separate mutable undo and redo stacks are not
stored because they duplicate ordering state and complicate safe pruning.
`active_group_id` is either the `group_id` of `entries[cursor - 1]` or null.
`last_used_seq` advances whenever the timeline commits, undoes, or redoes and
provides the deterministic pruning tie-break. `external_epoch` is the most
recent non-UA mutation epoch observed for this surface; changing it closes
the active group. `current` is the package's current typing/composition state
and its last committed/observed selection snapshot. Native live Selection is
still authoritative when constructing a new `EditContext`; a selection-only
change refreshes `current` with a state-only slot update and creates no entry.
`total_bytes` is the cached retained charge of this timeline, measured with
all cached byte fields normalized as in §9.2; document pruning uses the
identity-aware root total rather than summing timeline totals.

### 5.4 History entry

```text
<edit-history-entry
  id: 19
  group_id: 8
  history_class: 'typing
  coalesce_key: "insertText/plain"
  started_ms: 1200
  updated_ms: 1432
  commit_seq: 31
  sensitive: false
  primary_surface_key: "7/42/contenteditable"
  participant_keys: ["7/42/contenteditable"]
  steps: [<edit-step ...>]
  before: <edit-state ...>
  after: <edit-state ...>
  charged_bytes: 4096
>
```

`steps` are stored once, in forward order. Each step already contains both
`forward_ops` and `inverse_ops`; storing separate redo and undo step arrays
would duplicate the same deltas and retention handles. Coalescing appends the
new step, keeps `before` from the first step, and replaces `after` with the
last step's result.

`started_ms` and `updated_ms` come from the common gate's monotonic event
clock, including deterministic replay time. Wall-clock time is never read by
the package.

### 5.5 State snapshots

```text
<edit-state
  selection: <dom-selection ...> | <control-selection ...> | null
  typing: <edit-typing-state ...> | null
  composition_id: null
>

<dom-selection
  anchor: <dom-boundary ...>
  focus: <dom-boundary ...>
  direction: 'forward | 'backward | 'none
>

<dom-boundary
  node: OpaqueDomNodeHandle
  offset_kind: 'utf16 | 'child
  offset: 0
  affinity: 'before | 'after
>

<control-selection
  control: OpaqueDomNodeHandle
  start_u16: 0
  end_u16: 0
  direction: 'forward | 'backward | 'none
>

<edit-typing-state
  marks: []
  style_with_css: false
  paragraph_separator: "div"
  host_epoch: 0
>
```

DOM text offsets and form-control offsets are UTF-16. Element boundary offsets
are child indices. Conversions happen only at explicit package/native edges;
an unlabelled integer offset is invalid.

The state snapshot contains only editing state that undo/redo restores. Focus,
scroll position, hover, clipboard permission, and layout geometry are not
history state unless a pinned conformance case requires one to become an
explicit versioned field.

---

## 6. `<edit-step>` contract — replacing `EditPlan`

### 6.1 Meaning

`EditStep` is the one complete, atomic, user-visible edit formerly called
`EditPlan` in the Phase-9 design. A step is not one low-level tree write. It is
an immutable Lambda element containing the ordered primitive operations,
their inverses, selection/state transition, history classification, and
validation facts for one command or input intent.

The rename produces this vocabulary:

```text
EditInvocation -> EditContext -> EditStep -> EditStepResult
                                      |
                                      +-> [EditOp...]
```

`EditOp` is the low-level mutation unit. `EditTransaction` means the native
atomic application of one step. `EditHistoryEntry` means one or more steps
coalesced into one undo unit. The name `EditPlan` is removed from this design,
package modules, waist APIs, tests, and logs when the proposal is ratified.

### 6.2 Exact step schema

```text
<edit-step
  schema: 1
  id: 55
  origin: 'platform | 'exec-command | 'automation | 'replay | 'history
  intent: "insertText"
  command: null
  surface: <edit-surface ...>
  participants: [<edit-surface ...>]
  expected_dom_epoch: 91
  history_class: 'typing
  coalesce_key: "insertText/plain"
  barrier_before: false
  barrier_after: false
  sensitive: false
  forward_ops: [<edit-op ...>]
  inverse_ops: [<edit-op ...>]
  before: <edit-state ...>
  after: <edit-state ...>
  event_contract: 'input-default
  changed: true
>
```

Required invariants:

1. `surface` is the primary participant and appears exactly once in
   `participants`.
2. Every handle belongs to the same document and an allowed participant.
3. `forward_ops` are valid against `before`; `inverse_ops`, applied in their
   stored order after the forward operations, restore `before`.
4. `before.selection` equals the live selection captured after author
   cancellation and before package planning.
5. `after` is derived by deterministic position mapping, not by searching for
   similar text after mutation.
6. A changed step has at least one state or mutation difference. An unchanged
   step is never added to history unless its command descriptor explicitly
   classifies the state-only change as recordable.
7. History-origin steps bind `expected_dom_epoch` to the current epoch when
   selected from an entry. Stored steps do not bypass live validation.
8. The complete step is constructed and rooted before the first native write.

### 6.3 Exact primitive operation set

Schema version 1 has five operation tags:

| Tag | Required fields | Meaning |
|---|---|---|
| `<replace-text>` | `node`, `from_u16`, `to_u16`, `text`, `expect` | Replace UTF-16 data in one DOM Text node. |
| `<splice-children>` | `parent`, `index`, `delete_count`, `insert`, `expect` | Delete a consecutive child slice and insert retained-node handles at that index. |
| `<move-node>` | `node`, `parent`, `index`, `expect_parent`, `expect_index` | Reparent/reorder one existing node while retaining its identity. |
| `<set-attribute>` | `element`, `namespace`, `name`, `present`, `value`, `expect_present`, `expect_value` | Set or remove one attribute with an explicit old-value precondition. |
| `<replace-control-text>` | `control`, `from_u16`, `to_u16`, `text`, `expect` | Replace a UTF-16 slice in the canonical value of an eligible text input or textarea. |

Their canonical Lambda spellings are:

```text
<replace-text
  node:N from_u16:F to_u16:T text:S expect:E>

<splice-children
  parent:P index:I delete_count:C
  insert:[OpaqueRetainedDomNodeHandle...] expect:[OpaqueDomNodeHandle...]>

<move-node
  node:N parent:P index:I
  expect_parent:EP expect_index:EI>

<set-attribute
  element:E namespace:NS name:N present:B value:V
  expect_present:EB expect_value:EV>

<replace-control-text
  control:C from_u16:F to_u16:T text:S expect:E>
```

`F`, `T`, `I`, `C`, and `EI` are non-negative integers; text offsets are
UTF-16 units. `S` and text `expect` values are strings. `NS` is null or a
namespace string. Attribute `value`/`expect_value` are null exactly when the
corresponding presence Boolean is false. The splice `expect` list contains
the exact child identities expected in the deleted slice and its length must
equal `delete_count`. No omitted field receives a context-sensitive default.

`expect` fields make stale or externally modified data fail before mutation.
The package expands split, join, wrap, unwrap, formatting, list, block, and
normalization rules into these primitives. Native C/C++ receives no
`<wrap-range>`, `<toggle-bold>`, `<insert-paragraph>`, or other policy-shaped
operation.

Every forward operation has a mechanically paired inverse:

- `<replace-text>` and `<replace-control-text>` retain the replaced string;
- `<splice-children>` retains the removed nodes and reverses inserted nodes;
- `<move-node>` retains the old parent and index;
- `<set-attribute>` retains exact presence and value, distinguishing absent
  from the empty string;
- the step-level `before.selection` and `after.selection` fields are the
  inverse and forward selection states; selection is applied once after the
  content operations rather than duplicated as an `EditOp`.

An operation address uses an ordinary opaque DOM handle. A node that must
survive while detached for a later inverse or redo uses an opaque
history-retained handle. In particular, every node in a `<splice-children>`
`insert` payload is history-retained, as is a stored Selection endpoint when
its node can be detached by the same entry. Both wrappers validate a
`DomNodeRef`; only the retained form owns a lifecycle pin and incurs the DOM
retention charge in §9.2. The schema validator rejects an address-only handle
where a retained handle is required.

The package constructs the inverse from the same immutable DOM/control
snapshot used for planning. The native waist independently journals actual
pre-state for rollback and rejects a forward precondition mismatch. This
separation catches a bad inverse in round-trip tests without relying on it for
current-call safety.

### 6.4 Result schema

```text
<edit-step-result
  schema: 1
  status: 'applied | 'no-change | 'rejected | 'failed
  changed: true
  selection_changed: true
  history_recorded: true
  slot_revision: 18
  dom_epoch_before: 91
  dom_epoch_after: 96
  step_id: 55
  entry_id: 19
  group_id: 8
  history_failure: null
  failure: null
>
```

Stable failures include `stale-slot`, `stale-dom`, `dead-surface`,
`cross-document`, `cross-surface`, `false-island`, `bad-schema`,
`precondition`, `allocation`, and `teardown`. A failure never causes a native
policy fallback. `failure` describes failure of the atomic edit itself;
`history_failure` is null unless an applied edit could not be retained under
the history policy. In that case `status` remains `'applied` and
`history_recorded` is false.

---

## 7. Forward edit, undo, and redo

### 7.1 Forward edit

For a recordable forward edit, the package:

1. resolves the primary surface and reads its timeline;
2. constructs the forward and inverse `<edit-step>` from one `EditContext`,
   assigning the candidate `next_step_id`;
3. discards the timeline's redo suffix `entries[cursor..]`;
4. closes or extends the active group using §8;
5. constructs the proposed next `<edit-history>` value, setting
   `timeline.current = step.after`; when an entry is retained, the proposal
   also assigns `next_activity_seq` to the entry and `last_used_seq` and then
   advances that counter;
6. enforces §9 limits and writes the resulting cached byte totals;
7. calls `dom.edit_apply_step` with the step, slot revision, and proposed root;
8. publishes `input` and later observer/selection checkpoints only after a
   successful result.

A prevented, declined, unsupported, failed, editor-owned, or ordinary no-op
edit does not create an entry. It may clear `active_group_id` through a
state-only package-slot compare/replace when §8 defines a boundary.

### 7.2 Undo

Undo targets `entries[cursor - 1]`. The package composes that entry's
`steps` into one transient history-origin `<edit-step>`. Its `forward_ops`
concatenate each stored step's `inverse_ops` while traversing `steps` in
reverse; its `inverse_ops` concatenate each stored step's `forward_ops` in
forward order; and its state transition is entry `after` to entry `before`.
It binds that single step to the current DOM epoch, proposes `cursor - 1`,
sets `current = entry.before`, closes `active_group_id`, advances
`last_used_seq` from `next_activity_seq`, increments that counter, and calls
`dom.edit_apply_step` once. The entry remains in place, so a coalesced group
is atomic rather than a sequence of separately committed undos.

On success, the native waist restores DOM/control state and the entry's
`before` state, then atomically commits the decremented cursor. Undo does not
create another entry and does not change the §9 retention charge.

### 7.3 Redo

Redo targets `entries[cursor]`. The package likewise composes `steps`
into one transient history-origin `<edit-step>`: `forward_ops` concatenate
the stored steps' `forward_ops` in forward order, and `inverse_ops`
concatenate their `inverse_ops` in reverse step order. Its state transition
is entry `before` to entry `after`. The proposed timeline advances the cursor,
sets `current = entry.after`, closes `active_group_id`, and advances
`last_used_seq` and `next_activity_seq` by the same rule as undo. Redo does not
create an entry or change the §9 retention charge.

### 7.4 Validation failure during history traversal

If the selected entry no longer validates because of untracked DOM/control
mutation, undo/redo returns `false`, clears the affected surface timeline in a
state-only compare/replace, and emits one diagnostic. It does not skip the
invalid entry and apply an older one; doing so would cross an unknown state.

Page/editor mutations that cancel or claim a UA transaction are never added
to UA history. A mutation inside a surface outside the UA waist updates
`external_epoch`, clears `active_group_id` and that surface's redo suffix, and
is offered to the package at the mutation checkpoint. If it changes a
value/node named by a retained precondition, the package clears the timeline
at that checkpoint. It also clears any other timeline containing an entry
whose `participant_keys` names the affected surface; this handles the rare
multi-surface entry without leaving a stale copy. A race that escapes the
checkpoint is caught by native step validation and follows the first
paragraph. Timelines with no affected participant are unchanged.

---

## 8. Grouping and coalescing

### 8.1 One entry is one undo unit

Every non-coalesced step creates one entry and one `group_id`. Coalescing adds
a step to the current entry without changing the entry count or `group_id`.
`next_group_id` and `next_entry_id` are monotonic within the document and are
never reused after pruning.

### 8.2 General coalescing predicate

A new step can join the entry immediately before `cursor` only when all of
these are true:

1. `cursor == len(entries)`; no redo suffix exists;
2. the previous entry's `group_id` equals the timeline's
   `active_group_id`;
3. the primary surface and participant list are identical;
4. both history classes are coalescible according to §8.3;
5. `coalesce_key` values are equal;
6. previous `after.selection` structurally equals new `before.selection`;
7. typing state is compatible for the class;
8. neither step declares `barrier_after`/`barrier_before`;
9. no focus, effective-host, selection-navigation, clipboard/drop, command,
   external-mutation, or composition boundary occurred;
10. for timed classes, `new.started_ms - previous.updated_ms <= 500`.

The 500-ms window uses the gate's monotonic/replay clock. A negative timestamp
delta, missing timestamp, or replay discontinuity forces a boundary.

### 8.3 Class rules

| History class | Coalescing rule |
|---|---|
| `'typing` | Adjacent platform `insertText` replacements with the same typing marks, same insertion direction, collapsed contiguous selection, and equal `coalesce_key`; 500-ms window. |
| `'delete-backward` | Consecutive collapsed backward deletions with the same granularity and contiguous mapped boundary; 500-ms window. |
| `'delete-forward` | Consecutive collapsed forward deletions with the same granularity and contiguous mapped boundary; 500-ms window. |
| `'composition` | Every update from one `composition_id` forms one entry regardless of the 500-ms window; commit/cancel closes it. A canceled composition whose net state equals `before` removes the entry. |
| `'structural` | Never coalesces. Paragraph, line break, list, indent/outdent, and block operations are one entry per invocation. |
| `'format` | Never coalesces. Each explicit formatting command is independently undoable. |
| `'clipboard` | Never coalesces. One cut, paste, or drop transaction is one entry even if it contains many operations. |
| `'control-replacement` | Platform autocomplete/replacement is one isolated entry. Ordinary control typing uses `'typing`. |
| `'selection-only` / `'none` | Not recorded. It closes the active group when produced by explicit navigation or command policy. |

Programmatic `execCommand` calls are explicit command boundaries even when
the command is `insertText`. Platform text generated by one physical key/text
cascade is not treated as a programmatic command.

### 8.4 Mandatory barriers

The active group closes before or after:

- undo or redo;
- an explicit `execCommand`;
- pointer or keyboard selection navigation;
- focus or effective editing-host change;
- `contenteditable`/`designMode` mode change;
- clipboard or drag/drop activity;
- a structural or formatting edit;
- composition start and composition end;
- a form reset or programmatic control-value replacement;
- an author mutation inside the surface;
- a rejected stale step, document lifecycle transition, or replay boundary.

An unrelated edit on a different surface does not alter the entries of this
timeline, but it closes this timeline's active group. Returning later therefore
starts a fresh undo unit rather than coalescing across a focus excursion.

---

## 9. Capacity and byte accounting

### 9.1 Limits

- **Per timeline:** at most 256 `<edit-history-entry>` elements total,
  including both the applied prefix and redo suffix.
- **Per document:** the rooted `<edit-history>` graph and reserved
  history-retained DOM storage together may charge at most 16 MiB, exactly
  `16 * 1024 * 1024 = 16,777,216` bytes.

The limits are compile-time product constants for this phase:

```text
DOM_EDIT_HISTORY_MAX_ENTRIES_PER_TIMELINE = 256
DOM_EDIT_HISTORY_MAX_BYTES_PER_DOCUMENT   = 16777216
DOM_EDIT_HISTORY_COALESCE_WINDOW_MS       = 500
```

They are not web-visible preferences and page script cannot raise them.

### 9.2 Exact retained-byte charge

`dom.retained_size(value)` performs a generic identity-aware traversal and
returns:

```text
lambda_bytes
  = actual allocated bytes of every GC-managed Lambda container, element,
    array, string, binary, wrapper, and scalar home reachable from value,
    counted once per identity within this traversal

retained_dom_bytes
  = actual primary allocation plus owned payload bytes for every node/subtree
    reached through an opaque history-retained handle, counted once per
    DomNodeRef identity, whether that node is currently attached or detached

total = lambda_bytes + retained_dom_bytes
```

An ordinary address handle charges only its wrapper, so the surface-owner
handle does not charge the whole live editing host. A history-retained handle
reserves the full payload even while attached because a later undo can detach
it while redo must preserve its identity. This makes the charge invariant
under undo/redo attachment changes and prevents cursor movement from exceeding
the cap. Shared Lambda allocations and retained DOM nodes are counted once
across the complete history root. The walker uses project `HashMap` identity
sets, not `std::` containers, and is a general retained-value mechanism rather
than a history-schema parser.

To avoid self-reference, every cached `total_bytes` and `charged_bytes` scalar
is treated as zero during measurement; its fixed scalar home still counts.
The package measures each entry for `charged_bytes`, each timeline for its
`total_bytes`, then the complete proposed root for the root `total_bytes`.
Native commit verifies the cached root total before swapping the slot.

### 9.3 Pruning algorithm

Pruning runs before every forward history commit in this exact order:

1. Delete the active timeline's redo suffix.
2. Coalesce or append the new entry.
3. While that timeline has more than 256 entries, delete its oldest applied
   prefix entry and decrement `cursor`.
4. Measure the complete proposed history root.
5. While it exceeds 16 MiB, choose the globally smallest `commit_seq` among
   the oldest applied prefix entries of all timelines, delete it, decrement
   that timeline's cursor, remove an empty inactive timeline, and remeasure.
6. If no applied-prefix candidate remains, discard the complete redo suffix
   of the least-recently-used timeline, chosen by `last_used_seq` then surface
   key, and remeasure.
7. Prefer the newly committed entry over all older entries. If the new entry
   alone still causes the root to exceed 16 MiB after every other entry is
   removed, commit the user edit without a history entry, clear the initiating
   timeline, return `history_recorded:false` with failure reason
   `history_failure:'entry-too-large'`, and retain other
   empty/noncontributing timelines only if needed for active state.

Dropping an entry always drops a complete group; no individual step is removed
from a coalesced entry. There is never a history hole. Pruning changes what can
be undone but never mutates the current document/control value.

Undo and redo only move state/cursor metadata, so they neither allocate a new
entry nor trigger capacity eviction; the reserved-retention rule keeps their
charge stable. Every state-only root replacement, schema migration, or
diagnostic state rewrite remeasures the proposed root and applies the same
16-MiB pruning algorithm before replacement.

---

## 10. Surface-specific policy and pruning

### 10.1 `contenteditable`

The timeline key is the effective root editing host. Rich and
`plaintext-only` modes use the same schema; their command descriptors produce
different steps. Disabling or changing the element so it is no longer the
same effective host closes and prunes its timeline at the mutation checkpoint.
Changing the same host between rich and `plaintext-only` mode also clears its
timeline, because earlier structural operations are not valid under the new
host policy.

Removing and reinserting the same host within one synchronous atomic DOM
transaction preserves its timeline if it is connected with the same effective
host identity at commit. A host still detached at the checkpoint is pruned.

### 10.2 `designMode`

The document surface owns one timeline distinct from any element-host
timeline. Turning `designMode` off closes its active group but retains the
timeline while the same document remains live. Turning it back on can resume
undo/redo, but never coalesces with the old active group. Replacing the
document element or body clears this timeline because stored structural
preconditions no longer describe the editing root.

### 10.3 `<input>` and `<textarea>`

Eligible controls use `<replace-control-text>` and `<control-selection>` but
otherwise follow the same entry, cursor, grouping, capacity, and atomic
application rules.

For schema 1, an “`<input>` text control” means a missing/empty type or one of
`text`, `password`, `email`, `url`, `search`, `tel`, or `number`. `<textarea>`
is the other eligible control kind. `hidden`, `checkbox`, `radio`, `button`,
`submit`, `reset`, `image`, `range`, `file`, `date`, `time`, `month`, `week`,
`datetime-local`, and `color` do not acquire timelines merely because an
incomplete native renderer temporarily represents them with a text widget.

- ordinary user typing/deletion and composition are recorded;
- cut, paste, drop, autocomplete/replacement, and accepted UA actions are
  isolated according to §8;
- setting `.value`, form reset, or changing an `<input>`'s normalized type
  clears that control's timeline and closes its group; setting the type
  attribute to another spelling with the same normalized type does not;
- `readonly` or `disabled` closes the group but does not by itself clear
  entries; undo remains disabled until the control is eligible again;
- removing the control prunes the timeline at the detach checkpoint;
- `maxlength` and input sanitization run on the forward edit only. Undo
  restores a previously accepted value without re-sanitizing it.

The canonical value remains native form-control state. History stores only
the Lambda steps/deltas and before/after editing state. Native C++ no longer
stores value snapshots in an `EditHistoryEntry` ring.

Password-control entries set `sensitive:true` on their step and entry. Their
payloads remain in the in-memory rooted Lambda graph so undo works, but logs,
state dumps, replay summaries, and diagnostics emit only a redacted marker
and lengths, never text or a reusable content hash. Pruning or teardown makes
the payload unreachable and releases native retention promptly; schema 1 does
not promise secure erasure of shared GC pages.

### 10.4 Author/editor ownership

The UA history service records only a step applied by the UA package. If page
code or a registered model editor cancels or claims the action, the package
does not record the resulting DOM/control mutation. CodeMirror, ProseMirror,
Editor.js, and Radiant model editors retain their own timelines.

This rule prevents one Cmd+Z from first invoking an editor's undo and then
also applying a stale UA entry for the same user gesture.

---

## 11. Events, observation, and selection

An undo or redo is an ordinary editing transaction with input type
`historyUndo` or `historyRedo`. Its descriptor determines the precise event
envelope required by WPT/Chromium, but the ordering invariant is:

1. the precursor key/command event and cancelable notification run before
   mutation where required;
2. a canceled history action does not move the cursor or timeline cursor;
3. native applies the step and history-root replacement atomically;
4. `input` sees the committed DOM/control value and restored Selection;
5. `selectionchange` and MutationObserver records use the ordinary platform
   scheduling rules;
6. layout and geometry queries after the synchronous command see the restored
   state.

DOM undo/redo uses the same live Range adjustment and MutationObserver
machinery as a forward edit. Form-control undo/redo uses the same value,
selection, validation, pseudo-state, and `selectionchange` mechanisms as a
forward control edit. Neither path fabricates observer records from the
history entry.

Normalization operations are already present in the stored forward/inverse
steps. Undo restores pre-operation observable structure; redo restores the
same normalized result. The package never substitutes equivalent
`innerHTML` for exact operations.

---

## 12. Re-entrancy, failure, and safety

- One document may have only one open `dom.edit_apply_step` transaction.
- A re-entrant edit requested while native application is open is rejected as
  `reentrant-apply`; it cannot see or extend a half-committed root.
- Re-entrant `execCommand` during author notification follows the command
  descriptor compatibility rule before an outer native transaction begins.
- Every slot value, step, next-state root, DOM wrapper, and returned result is
  continuously rooted across native allocation/callback boundaries under
  D5.3.3.
- History may retain detached nodes only through validated handles and
  history-specific lifecycle pins. Dropping the last reachable entry releases
  those pins; document teardown clears them deterministically even before the
  next GC.
- A slot revision mismatch or DOM epoch mismatch is a normal stale-operation
  rejection, not permission to retry with guessed state.
- Package initialization failure leaves no native contenteditable or control
  history fallback after cutover. Author events still dispatch, but no UA
  history mutation occurs.
- Logs carry document/surface key, step/entry/group IDs, class, cursor change,
  byte totals, and result category. They do not serialize private entry
  payloads or user text by default.

---

## 13. Query and command integration

The shared immutable command registry contains descriptors for `undo` and
`redo`. `queryCommandSupported` reads descriptor presence.
`queryCommandEnabled("undo")` is true only when the current eligible timeline
has `cursor > 0`; redo is enabled only when `cursor < len(entries)`.

Queries read the current rooted `<edit-history>` and current surface without
changing the DOM, cursor, active group ID, slot revision, or byte totals.
Keyboard `historyUndo`/`historyRedo`, input-event intents, and
`execCommand("undo"|"redo")` select the same descriptor and functions.

Form controls use the same internal descriptors even though the legacy
`execCommand` surface may not expose every control operation. There is no
second C++ key-history switch and no separate form history cursor.

---

## 14. Invariants and conformance gates

The implementation must enforce these invariants:

1. every stored state root is an `<edit-history schema:1>` Lambda element;
2. every durable edit and history entry is a Lambda element, never a native
   `EditHistoryEntry` or serialized host snapshot;
3. timeline keys are unique within one history root;
4. `cursor` is always within the entry array;
5. entry and group IDs are monotonic and unique per document;
6. applying every `redo_step` followed by every `undo_step` restores the exact
   before DOM/control state and Selection;
7. undo followed by redo restores the exact after state;
8. a failed application changes neither content nor history root;
9. no timeline exceeds 256 entries;
10. the complete root charge never exceeds 16,777,216 bytes;
11. pruning removes complete entries only;
12. an editor-owned/canceled mutation produces no UA entry;
13. histories do not cross documents or effective editing surfaces;
14. no history root or retained handle survives document teardown.

Required test layers:

- pure Lambda schema, cursor, grouping, coalescing, capacity, and pruning
  tests, each with an expected `.txt` result;
- native package-slot root/revision/teardown and retained-size tests;
- direct generic EditOp atomicity, rollback, stale-handle, and precise-rooting
  tests independent of command names;
- `<input>` and `<textarea>` parity tests covering typing, deletion,
  composition, maxlength, reset, `.value`, selection direction, capacity, and
  prop/view churn;
- contenteditable/designMode tests covering text, structure, formatting,
  clipboard/drop, composition, normalization, selection, detached hosts, and
  cross-host isolation;
- WPT and Chromium history families with no harness-side mutation;
- CodeMirror, ProseMirror, Editor.js, and Radiant editor tests proving no
  duplicate UA history;
- GC stress at both limits and repeated document/iframe teardown.

D1.10 requires the schema validator, source ownership audit, entry-count
assertion, retained-byte assertion, and no-native-`EditHistory` audit to be
executable gates rather than review prose.

---

## 15. Alternatives considered

### 15.1 Native per-control or per-host rings

Rejected. They duplicate command/grouping policy, cannot naturally hold rich
DOM inverses as Lambda values, and contradict D7.2.5 for contenteditable. The
current form-control ring also ties one surface family to snapshot storage
while rich DOM needs precise operations.

### 15.2 One document-wide undo timeline

Rejected. Focused surfaces are independent editing contexts. A document-wide
stack would let undo in one input unexpectedly mutate another host and would
make author-editor isolation ambiguous. One document service with per-surface
timelines shares infrastructure without sharing user-visible order.

### 15.3 Whole-value or `innerHTML` snapshots

Rejected as the canonical representation. They are easy to implement but
destroy node identity, overproduce observer records, lose exact Selection
mapping, scale poorly for large hosts/textareas, and obscure which primitive
failed. Precise deltas keep local edits local.

### 15.4 Let Lambda mutate without a native transaction

Rejected. A multi-operation rich edit must update live Ranges, native DOM
lifetimes, form state, observers, Selection, layout invalidation, and the
history root atomically. Those mechanisms already straddle the native DOM
boundary. D7.5.3 calls for one explicit waist, not best-effort rollback in
package script.

### 15.5 Store separate undo/redo arrays

Rejected for the canonical schema. A chronological entry array plus cursor
represents the same linear history with one ordering source, makes redo
invalidation a suffix deletion, and makes dependency-safe pruning explicit.

### 15.6 Reuse `lambda/package/editor/mod_step.ls` unchanged

Rejected. Its paths address an immutable editor model, while UA history must
address live DOM nodes, element-boundary offsets, form-control values, and
native observation. The architecture—immutable invertible steps, selection
mapping, pure history updates—is reused, but the schemas remain distinct and
must not be silently accepted by the wrong applier.

---

## 16. Ratification and migration consequences

Ratifying this proposal requires one coordinated documentation change:

1. revise D7.2.5 in place to D7.2.5v2 so package ownership explicitly covers
   UA history for contenteditable, `designMode`, text inputs, and textareas;
2. bump `doc/Lambda_Formal_Design.md` according to
   `doc/Doc_Convention.md` because an existing ruling changes meaning;
3. revise parent design §20.4/§20.5/§20.7 to use `EditStep`, this physical
   slot, and unified form-control history;
4. revise `Radiant_Impl_Editable2.md`: rename `edit_plan.ls` to
   `edit_step.ls`, replace every `EditPlan` reference, and incorporate the
   slot/schema/capacity migration;
5. supersede the native-ring sections of
   `Radiant_Design_Form_Input.md` and the editing integration records without
   deleting their historical account.

The code migration then has four bounded outcomes:

- land the generic rooted package-state registry and retained-size mechanism;
- land `<edit-step>` validation and atomic `dom.edit_apply_step`;
- move contenteditable history/grouping into the Lambda DOM package;
- migrate `<input>`/`<textarea>` to `<replace-control-text>`, then delete
  `EditHistory`, `EditHistoryEntry`, `te_history_*`, the `ViewState` history
  pointer, and native history cursors only after parity gates pass.

No formal document or current parent ruling is changed merely by creating
this proposal. Ratification is the explicit point at which the coordinated
updates above become required.
