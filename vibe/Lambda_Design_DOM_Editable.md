# Lambda DOM Editable — One Editing Protocol, Two Mutation Backends

**Date:** 2026-09-08
**Status:** Proposal — not yet ratified or implemented
**Scope:** unification of the model-driven Lambda rich-text editor represented
by `test/ui/rte_prototype.ls` with the package-owned UA `contenteditable`
path under `lambda/dom`; shared command and input policy; Selection/caret
ownership; source-to-DOM projection; and the Lambda↔Radiant native waist.
**Out of scope:** replacing the editor's schema, immutable Step/Transaction
model, decorations, collaboration protocol, or source-document authority;
changing the observable UA editing results fixed by WPT/Chromium; and moving
layout, hit testing, text shaping, or caret geometry into Lambda.

**Parent and companion designs:**

- [Radiant Editable Support](radiant/Radiant_Design_Editable.md), especially
  §1, §9, and §20, is authoritative for the CE1–CE3 lineage, the one editing
  gate, and full package-owned UA behavior.
- [Radiant Rich Text Editing](editing/Radiant_Rich_Text_Editing.md) is the
  source-model design; [Stage 2](editing/Radiant_Rich_Text_Editing2.md) is the
  prototype lineage.
- [Lambda DOM Event Dispatch](Lambda_Design_DOM_Dispatch.md) owns author/UA
  event ordering and cancellation.
- [RAD_18](../doc/dev/radiant/RAD_18_Editing_Selection_Ranges.md) describes the
  implemented native Selection/Range and caret substrate.

> **Formal linkage.** D7.2.5 requires all UA editing policy to remain in the
> shipped Lambda DOM behavior package and user input plus legacy commands to
> lower through one package operation model. D7.5.3 requires Lambda to reach
> Radiant only through the explicit `radiant-dom` module waist. S12.1.3 keeps
> reactive-template mutation in `on` handlers, while S12.2.2 defines element
> mutation. D5.3.3 requires continuously rooted native handoffs. D1.10 and
> D7.3.5 require executable conformance gates. This proposal refines those
> rulings and introduces no formal-spec conflict.

> **Specification linkage map.** D7.2.5 → §§1, 4–6, 8; D7.5.3 and D5.3.3 →
> §7; S12.1.3 and S12.2.2 → §§6, 8; D1.10 and D7.3.5 → §11. The proposal does
> not mint a new ledger series; the existing CE1–CE3 lineage remains at its
> parent design home.

> **Verified against the tree on 2026-09-08.** The current-state survey covers
> `test/ui/rte_prototype.ls`, `lambda/editor/mod_{editor,input_intent,
> transaction,commands,dom_bridge}.ls`, `lambda/dom/{commands,edit_context,
> edit_plan,edit_result,dom_edit,editing,caret}.ls`,
> `radiant/{event.cpp,event.hpp,editing_dom_waist.cpp,
> source_pos_bridge.cpp}`, and the `set_selection` runtime hook.

> **Parent-contract constraint.** This proposal does not alter the parent
> design's one gate, standard-`contenteditable` routing, pre-notification route
> and handler snapshot, pure `beforeinput`, exactly-one registered action,
> gate-owned post-action `input`, or UA-package fallback ownership. If a detail
> here is read otherwise, [Radiant_Design_Editable.md](radiant/Radiant_Design_Editable.md)
> §6–§11 and §20 governs.

---

## 1. Decision summary

Adopt **one package-owned editing protocol with two mutation backends**.

```text
platform input / toolbar / clipboard / drag / composition / execCommand
                              |
                     canonical EditRequest
                              |
               lambda/dom shared editing core
        descriptor + target rule + EditAction + EditResult
                         /                 \
                        /                   \
          UA DOM backend                     model backend
     live DomSelection/Range             SourceSelection/Schema
     checked DOM transaction             immutable Steps/Transaction
     UA normalization/history            editor normalization/history
                        \                   /
                         \                 /
                    explicit radiant-dom waist
                snapshot before / projection after
                              |
                 one native caret presentation
```

The diagram shows the selected **action** layer, not the whole event envelope.
Platform input reaches it only after the parent design's precursor events,
route/handler snapshot, target preparation, and uncanceled `beforeinput`.
Handlers never dispatch `beforeinput` or `input`; the common gate owns both.
Toolbar and legacy-command entry points retain their descriptor-defined event
contracts rather than blindly synthesizing the platform envelope.

“Two backends” names the two built-in Lambda implementations being unified.
It does not remove the parent design's explicitly registered JavaScript action
handler or ordinary JS editors that cancel and own an edit through standard
events. Those remain separate gate outcomes and do not import the Lambda
shared core.

The shared layer answers **what the user requested** and which cross-backend
contract applies. Each backend answers **how that request changes its own
authoritative representation**.

The design adopts these parts of the UA path:

1. one immutable descriptor registry for command aliases and `inputType`;
2. one normalized request/result protocol;
3. the browser-compatible native event, target-range, Selection/Range, IME,
   clipboard, drag, and caret substrate;
4. an explicit, generation-checked capability for every native edit; and
5. command policy above a mechanism-only native waist (D7.2.5, D7.5.3).

It adopts these parts of the model editor:

1. the Mark/source tree as the only document truth for a model-owned surface;
2. schema-aware commands producing immutable Steps and Transactions;
3. SourceSelection mapped through every Step;
4. editor-owned history, composition, decorations, and collaboration; and
5. regeneration of the rendered DOM rather than DOM-to-model reconciliation.

It rejects either representation as a universal mutation substrate. A live
DOM `Range` cannot replace a stable source path in history or collaboration;
an editor `Step` cannot reproduce browser DOM identity, live-Range adjustment,
MutationObserver records, and legacy `execCommand` normalization.

The concise answer to the selection question is therefore:

- `rte_prototype.ls` **does rely on the native DOM** for hit testing, pointer
  selection, keyboard caret movement, Selection/Range state, geometry, and
  painting;
- it **does not rely on the DOM as the edited document**: it projects native
  DOM positions to `SourceSelection`, edits `editor.doc`, maps the selection
  through model Steps, re-renders, and projects the resulting source selection
  back to the native `DomSelection`;
- ordinary UA `contenteditable` uses the same native selection/caret substrate
  but mutates the live DOM directly through the package's checked DOM backend.

### 1.1 Parent-design alignment matrix

| `Radiant_Design_Editable.md` contract | This proposal's refinement |
|---|---|
| §6/§9 one mandatory gate | native `EditingIntent` and transaction identity remain the gate record; `EditRequest` is its package action value |
| §7 standard host and existing-context route | no marker is added; `edit_mount` binds a controller only after template/render-map ownership selected the Radiant-template route |
| §7.3/§8 one snapshotted action handler | the built-in Radiant template handler is selected once; model and UA backends never compete after notification |
| §6/§9/§11 pure `beforeinput` | the prototype's model mutation moves to the registered action hook after uncanceled notification |
| §8.2 `PASS` has no fallback | unsupported model action is an unhandled/error result, never permission to invoke the UA handler |
| §9.2 structured transaction result | common package `EditResult` maps to `EditingHandleResult`, then into the gate's `EditingTransactionResult` |
| §11 source-model reconciliation | editor Steps/history remain private; returned source selection is projected only after the matching regeneration |
| §20 D7.2.5 UA ownership | the existing DOM `EditContext`/`EditPlan`, normalization, history, and conformance behavior remain the UA backend |

This matrix is a constraint on the proposal, not a competing summary of the
parent design.

## 2. Current state and the duplication to remove

### 2.1 Two working paths

| Concern | UA `contenteditable` | `rte_prototype.ls` model editor |
|---|---|---|
| Content truth | live DOM | `editor.doc` source tree |
| Selection used by edit | live `DomSelection`/`DomRange` captured in `DomEditInvocation` | `SourceSelection` copied from `evt.source_selection`/`source_pos` |
| Input mapping | `lambda/dom/commands.ls` descriptor/family dispatch | `lambda/editor/mod_input_intent.ls` `input_type` conditional |
| Mutation | `lambda/dom/edit_*` through checked DOM primitives | `lambda/editor/mod_commands.ls` → immutable Steps/Transaction |
| Selection after mutation | invocation maps and commits live DOM boundaries | transaction maps source positions, then script calls `set_selection()` |
| History | retained DOM delta/UA session | inverse model Steps and editor history |
| Rendering | mutate and invalidate live DOM | reactive template regeneration from the source tree |
| Caret geometry | Radiant | Radiant |

The common substrate is already substantial. Both paths enter the normal
`contenteditable` input pipeline, observe standard `inputType` values, use the
same native `DomSelection`, use the render map/source-position bridge, and
paint through the same caret machinery. `lambda/dom/caret.ls` already owns the
key-to-caret-operation policy for rich surfaces while native code resolves the
operation against live geometry.

### 2.2 Policy duplicated today

The remaining duplication is above that substrate:

- `lambda/dom/commands.ls` and `lambda/editor/mod_input_intent.ls` separately
  map `inputType` to editing families;
- `lambda/editor/mod_editor.ls` adds a second command-name switch for toolbar
  calls;
- input aliases, plaintext eligibility, history class, event contract, and
  supported-command facts are not one data source;
- word-boundary behavior exists in both `lambda/dom/editing.ls` and
  `lambda/editor/mod_commands.ls`, with different classifications;
- the prototype locally validates and normalizes source positions even though
  this is a reusable model/DOM adapter responsibility;
- the prototype manually repairs click selection, mirrors
  `selectionchange` back to native selection, implements mouse drag state, and
  repeats `set_selection()` after nearly every command;
- the UA path uses an explicit opaque `edit_token`; the model path sends
  selection through the ambient, thread-local `set_selection()` hook.

These differences are architectural duplication, not evidence that the
document models should merge.

### 2.3 Problems in the current prototype loop

The prototype currently runs this approximate cycle:

```text
native DomSelection
  -> event source_selection
  -> local validation/normalization
  -> edit_set_selection (clears stored marks and appends an editor event)
  -> edit_dispatch / edit_exec
  -> set_selection(editor.selection)
  -> thread-local pending selection
  -> reactive retransform
  -> source path resolved in the rebuilt DOM
  -> native selectionchange
  -> edit_set_selection again
  -> set_selection again
```

The loop works, but ownership is implicit. It can erase collapsed typing
marks on an echo, append redundant selection events, and makes correctness
depend on `set_selection()` being called inside the right handler. The custom
click and drag branches compensate for missing transaction-boundary data
rather than representing independent editor features.

## 3. Goals, non-goals, and invariants

### 3.1 Goals

1. Resolve every standard command name and platform `inputType` through one
   descriptor source under `lambda/dom`.
2. Give platform input, toolbar actions, legacy commands, clipboard, drag,
   composition, history, and replay one `EditRequest` vocabulary.
3. Share policy that is independent of DOM/source representation: aliases,
   payload interpretation, plaintext restrictions, history classes, event
   contracts, fallback text boundaries, and structured results.
4. Preserve the UA backend's exact DOM/Selection/observer behavior and the
   editor backend's schema/Step/history behavior.
5. Make DOM↔source selection projection explicit, revisioned, and free of
   feedback loops.
6. Replace the ambient `set_selection()` channel with an explicit
   `radiant-dom` capability operation (D7.5.3).
7. Reduce `rte_prototype.ls` to rendering, app state, command presentation,
   and calls into the editor/DOM adapters.

### 3.2 Non-goals

- The UA DOM and editor model do not produce byte-identical HTML or identical
  internal trees. Their normalization laws intentionally differ.
- The editor's `mod_step.ls` does not become the DOM mutation journal, and the
  DOM retained delta does not become the editor's collaboration format.
- `queryCommand*` compatibility does not become an editor API requirement.
  The descriptor schema is shared; each backend computes state from its own
  representation.
- Model-only operations such as table-row insertion or drawing manipulation
  do not become web-platform commands.
- Native layout and geometry do not move into script.
- Form-control buffers do not become rich model documents. They may consume
  the same request/text-policy modules where applicable.

### 3.3 Required invariants

1. **One action owner.** A transaction is owned by the UA DOM backend or the
   model backend, never both. A registered model-owned surface fails closed on
   an unsupported edit rather than allowing a live-DOM fallback to diverge
   from its source tree.
2. **One public notification.** Platform input dispatches one cancelable
   `beforeinput`. Cancellation invokes no action handler and emits no `input`.
   An uncanceled model-owned operation invokes the registered model action,
   creates no UA DOM mutation/history entry, then receives the common gate's
   post-action `input` when it was applied or explicitly claimed. An
   uncanceled UA operation follows D7.2.5 and the descriptor event contract.
3. **No universal coordinate fiction.** A DOM boundary and a source position
   remain distinct tagged spaces. Conversion occurs only through the render
   map and checked source-position bridge.
4. **No stale mutable capability.** The route and selected action handler are
   snapshotted before notification. A mutable DOM invocation is created only
   after author `beforeinput` has completed and the retained host, target
   ranges, selection, and mutation epoch have been validated. Notification
   never reselects a different action owner.
5. **Model edits target the event snapshot.** A model backend consumes the
   source projection of the immutable pre-`beforeinput` target ranges, not a
   locally re-derived approximation.
6. **Selection direction survives.** Anchor/focus direction, element-boundary
   positions, node selections, and collapsed positions are not reduced to an
   ordered `{start,end}` pair.
7. **Native remains mechanism-only.** No C/C++ switch on a command name or
   model schema is added (D7.2.5).
8. **Explicit rooted handoff.** Every snapshot, host handle, and completion
   value is generation-checked and continuously rooted across handler calls
   and reactive regeneration (D5.3.3).

## 4. The shared editing core under `lambda/dom`

### 4.1 Module boundary

Extract a pure common layer that neither mutates the DOM nor imports the
editor model:

| Proposed module | Responsibility | Consumers |
|---|---|---|
| `lambda/dom/edit_registry.ls` | standard command/`inputType` descriptors, aliases, policy metadata, extension merge/validation | UA commands, editor adapter, toolbar/query UI, tests |
| `lambda/dom/edit_request.ls` | normalize event, legacy command, toolbar, replay, clipboard, drag, and composition payloads | both backends |
| `lambda/dom/edit_action.ls` | lower a request plus descriptor into a representation-neutral semantic action | both backends |
| `lambda/dom/edit_result.ls` | common structured result and verdict/handler adapter | both backends and native completion |
| `lambda/dom/edit_text_policy.ls` | newline sanitation and fallback character/word/line boundary functions over strings | form controls, DOM fallback code, editor commands |

The existing `lambda/dom/commands.ls` remains the UA orchestration facade. It
imports the shared modules and the DOM-specific modules. The editor imports
only the pure shared modules; importing them must not load a document session,
touch a host object, or create package-global mutable state (D7.2.1,
D5.4.3, S9.1.7).

The existing `lambda/dom/edit_context.ls` and `edit_plan.ls` remain
DOM-backend records because they carry a live host, invocation token, mutation
epoch, DOM step kinds, and UA normalization/history facts. Calling that record
an `EditPlan` remains consistent with D7.2.5. The common record is named
`EditAction` so it does not pretend a DOM plan and an editor Transaction are
the same artifact.

### 4.2 One descriptor registry

Move the immutable registry rows out of `commands.ls`. A descriptor has a
common portion and optional backend facets:

```text
CommandDescriptor = {
    name, aliases,
    input_type, input_aliases,
    family, payload_kind,
    plaintext_rule,
    history_class,
    event_contract,
    target_rule,
    state_kind,
    dom:   { supported, planner_key, query_key, compatibility_data } | null,
    model: { supported, command_key, query_key } | null
}
```

The common fields are the only source of canonical spelling and intent
classification. Backend facets are explicit because support and state can
legitimately differ. For example, a schema may not admit lists even though
the UA DOM backend supports `insertUnorderedList`.

`queryCommandSupported` consults the `dom` facet only. Model-only extensions
are merged into an editor registry with a distinct owner and never leak into
the web API:

```text
standard registry: insertText, formatBold, insertParagraph, historyUndo, ...
editor extension:  insertTable, insertTableRow, insertCodeBlock, drawing.*, ...
```

Registry construction rejects duplicate aliases, duplicate canonical
`inputType` ownership, an unknown target rule, or a descriptor without a
backend. This is immutable package data, not mutable command state.

The first extraction must also close inventory differences instead of
preserving them invisibly. For example, the model mapper currently recognizes
`deleteWordBackward`, while the UA command registry has no equivalent row.
Every platform intent gets one explicit descriptor and an explicit per-backend
supported/unsupported disposition.

### 4.3 `EditRequest`

All entry points normalize to this shape before backend selection:

```text
EditRequest = {
    input_type,                 // canonical Input Events spelling
    command,                    // optional legacy/toolbar spelling
    origin,                     // platform | toolbar | exec-command | replay
    data, html, mime,
    composition_phase,
    clipboard,
    drag,
    requested_caret,
    plaintext_only,
    target_rule,
    event_contract,
    history_class
}
```

For platform input, `EditRequest` is the package value derived from the
parent gate's already normalized native `EditingIntent`; it does not replace
that transport/audit record or create another gate. Both records carry the
same transaction identity. Legacy commands and toolbar calls construct the
same package value through their own entry contracts.

The request contains payload and policy facts, not a `DomNode`, `DomRange`,
`SourcePos`, editor state, or native pointer. Backend coordinates are supplied
separately by the selected target capability.

Constructors are pure and entry-specific:

- `request_from_event(evt, descriptor)`;
- `request_from_command(name, value, origin)`;
- `request_from_toolbar(input_type, payload)`;
- `request_from_replay(record)`.

Clipboard HTML/text choice and `plaintext-only` filtering happen once during
normalization. A backend may perform representation-specific parsing or schema
validation afterwards, but it cannot reinterpret which payload the request
selected.

### 4.4 `EditAction`

`EditAction` is the narrow common plan:

```text
EditAction = {
    descriptor,
    family,
    payload,
    target_rule,
    history_class,
    event_contract,
    origin
}
```

It expresses semantic families such as replace, delete, composition,
paragraph, line break, format, list, indent, object insertion, selection,
clipboard, drag, and history. It contains no primitive mutation steps.

The UA adapter lowers an action to a DOM `EditPlan`; the model adapter lowers
it to a schema-aware editor command/Transaction. This is the maximum safe
unification. Sharing primitive steps would either erase model schema semantics
or smuggle DOM identity and observer semantics into the editor.

### 4.5 `EditResult`

Extend the existing structured result rather than inventing an editor-only
return protocol:

```text
EditResult = {
    supported, enabled, claimed,
    changed, selection_changed, history_recorded,
    failure,
    history_group,
    selection_after,
    selection_space,            // live-dom | source | none
    model_revision,
    api_input, api_input_type, api_input_data
}
```

For the DOM backend, the invocation owns the live post-edit selection and
`selection_after` may be null with `selection_space: live-dom`. For the model
backend, `selection_after` is a `SourceSelection` and `model_revision` names
the regenerated source state that must render before projection.

`api_input*` reports the post-action notification facts requested by the
descriptor; the handler does not dispatch that event. The common gate decides
whether to emit it after translating and validating the handler result.

`claimed` and `changed` remain independent. A compatible no-op may be
explicitly claimed; an unsupported model intent returns a structured
unclaimed failure. Neither case falls through to the UA backend because the
gate invokes only the handler selected in its route snapshot. Queries never
claim. The existing `'pass'`/`'prevent-default'` vocabulary is a boundary
adapter, not the internal result type.

At the registered-action boundary, the Lambda record maps to the parent
design's `EditingHandleResult`: a claimed no-op, mutated DOM, reconciled model,
pass, or error. The common gate then folds that handler result together with
notification and commit facts into `EditingTransactionResult`. The package
record does not replace either native envelope; it prevents the two Lambda
backends from inventing incompatible result vocabularies.

### 4.6 Shared text policy and target ranges

Move pure string policy from `lambda/dom/editing.ls` into
`edit_text_policy.ls` and make the editor reuse it:

- CRLF/CR normalization;
- single-line newline replacement;
- character classification;
- fallback word start/end;
- hard line start/end; and
- insertion-length fitting where the caller supplies a limit.

Platform-provided or native-projected target ranges take precedence over these
fallback scanners. That is essential for grapheme clusters, cross-node word
deletion, visual soft lines, bidi, atomic content, and composition. The shared
descriptor selects a named `target_rule`; Radiant resolves that rule using
generic live Selection/text/geometry mechanisms and returns immutable target
ranges. Native code does not choose a rule by command name.

The model backend consumes the source projection of those ranges. It calls
the pure scanners only for non-platform requests, tests without a mounted
surface, or a descriptor whose target rule is explicitly string-local.

## 5. Two explicit backends

### 5.1 UA DOM backend

The existing package path remains authoritative for ordinary
`contenteditable`, `designMode`, and `execCommand`:

```text
EditRequest + live DomEditInvocation
    -> EditAction
    -> DOM EditContext
    -> DOM EditPlan
    -> checked generic DOM primitives
    -> UA normalization + retained DOM delta history
    -> live Selection commit + EditResult
```

Retain:

- `DomEditInvocation` as the mutable, opaque, synchronous capability;
- live Range adjustment and direction-preserving selection mapping;
- generic range replacement, split, insert, move, wrap/unwrap, attribute/style,
  transaction rollback, retained delta, observer, and invalidation mechanisms;
- UA session/typing state and exact WPT/Chromium behavior.

Refactor:

- `commands.ls` imports `edit_registry`, `edit_request`, `edit_action`, and
  the common `edit_result`;
- DOM-only descriptor data moves into the `dom` facet;
- `edit_context.ls`, DOM family modules, `edit_plan.ls`, normalization, and UA
  history remain backend-specific;
- command-shaped native exports continue to narrow toward generic operations
  as required by D7.2.5.

### 5.2 Model backend

Add `lambda/editor/mod_dom_adapter.ls` as the only adapter between the common
protocol and editor state:

```text
EditRequest + SourceTargetSnapshot + EditorState
    -> EditAction
    -> descriptor model facet
    -> existing cmd_* function
    -> immutable Transaction/Steps
    -> state_after_intent
    -> { editor, EditResult(selection_space: source) }
```

The adapter owns:

- validation and normalization of projected source positions;
- schema capability checks;
- mapping standard families to existing `cmd_*` functions;
- model extension descriptors for tables, code blocks, drawings, and other
  non-web commands;
- composition base document/range handling;
- conversion of Transaction metadata to the common result; and
- selection-origin/revision bookkeeping.

For a mounted platform deletion, the adapter replaces the editor selection
with the projected source target range and invokes the ordinary
selection-delete command. It does not call a second model word/line scanner.
The shared string scanner is only the unmounted or explicitly string-local
fallback.

It does not mutate the DOM. The editor retains its source tree, schema,
stored marks, decorations, history, inverse Steps, and collaboration mapping.
`edit_open` initializes a monotonic `model_revision`; every Transaction that
changes `editor.doc` advances it, while a selection-only update does not. The
render binding publishes the revision represented by its current DOM. This is
the barrier that prevents a post-edit source path from being resolved against
the pre-edit projection.

`mod_input_intent.ls` becomes backend machinery for composition/history state
and command execution instead of the second canonical `input_type` switch.
`mod_editor.ls` exposes request-based entry points; compatibility constructors
such as `edit_cmd_insert_text()` may remain temporarily as thin constructors
of canonical requests.

### 5.3 Representative mappings

| Common family | UA DOM backend | Model backend |
|---|---|---|
| replace | live range replacement, typing mark application | `cmd_insert_text` / paste command → Steps |
| delete | target `DomRange` deletion and DOM normalization | schema-aware `delete_selection` / boundary join |
| paragraph | DOM block split/default paragraph separator | `cmd_insert_paragraph` / `cmd_split_block` |
| line break | insert `<br>` according to UA rules | insert schema hard-break node |
| format | DOM wrappers/styles and collapsed UA typing state | mark Steps and `stored_marks` |
| list/indent | UA DOM list normalization | schema list commands |
| history | retained DOM transaction replay | inverse editor Transactions |
| drag move | DOM source deletion + target insertion | one `cmd_move_*` Transaction |
| selection | live `DomSelection` operation | SourceSelection change, then projection |

The table is deliberately semantic. It does not assert identical primitive
steps or serialization.

## 6. Selection and caret design

### 6.1 Authority by phase

There is one visible native selection, but authority changes at explicit
transaction boundaries:

| Phase | Authoritative selection | Mirror |
|---|---|---|
| pointer/keyboard navigation | native `DomSelection` | projected editor `SourceSelection` |
| start of platform edit | immutable native target-range snapshot | source-projected target snapshot |
| model Transaction | editor `SourceSelection`, mapped through Steps | old DOM is derived/stale |
| reactive regeneration | editor post-Transaction selection | native selection waits |
| post-regeneration commit | native `DomSelection` resolved from returned source selection | editor selection is already current |
| ordinary UA edit | live native `DomSelection` throughout | no source-model authority |

This is not two competing selections. It is a handoff protocol between a live
presentation coordinate space and a durable model coordinate space.

### 6.2 Navigation

Keep `lambda/dom/caret.ls` as the single key policy. Radiant continues to
resolve named operations over live layout and to update the canonical
`DomSelection` and caret geometry. A model editor does not implement arrows,
Home/End, PageUp/PageDown, bidi movement, or visual-line geometry in source
space.

After navigation, one coalesced `selectionchange` carries:

- the source projection;
- native selection revision;
- direction and selection kind;
- origin (`pointer`, `keyboard`, `model-commit`, `dom-mutation`, or API).

`edit_accept_dom_selection(editor, evt)` adopts a newer user/native selection.
It is idempotent: an equal selection or the echo of the editor's own committed
revision changes neither editor state nor stored marks. The handler never
calls back into native selection merely because it received
`selectionchange`.

### 6.3 Pointer selection and clicks

Pointer down/move/up remain native hit-testing and Selection operations.
The editor receives the resulting source projection through
`selectionchange`. This removes `click_selection_for_doc`, the prototype's
click correction, and its manual selection-drag state.

Schema-specific atomic/node selection remains in the model adapter. The
native bridge projects the real boundary; `mod_dom_bridge.ls` may promote it
to the nearest schema-selectable source node. If that promotion changes the
visible selection, the adapter commits the normalized selection once with the
same revision, rather than echoing every ordinary pointer update.

### 6.4 Selection through edits

The UA backend keeps live Range mutation rules and commits the invocation's
anchor/focus. The model backend maps `SourceSelection` through every Step via
`sel_map`, then returns that source selection in `EditResult`.

No backend repairs selection by searching for equivalent text. Every mapping
is structural and direction-preserving. If the source path cannot resolve in
the regenerated DOM, native completion returns a structured failure and keeps
the last valid native selection; it must not silently choose a neighboring
text node.

### 6.5 Composition

Radiant remains responsible for IME transport and native composition event
ordering. The common descriptor/request classifies composition phases. The
model backend retains its current `base_doc`, `base_selection`, provisional
range, no-history updates, and final history commit. Each provisional/final
selection is returned through the same source completion protocol, avoiding a
special `set_selection()` path.

### 6.6 Drag and drop

Use standard `deleteByDrag` and `insertFromDrop` requests plus a native drag
session identity and source/target projections. For a move within one model
surface, the model adapter combines source deletion and target insertion into
one mapped Transaction and one history entry. Copy and cross-surface drops use
the clipboard/slice payload selected by the common request.

The prototype's `mousemove`/`mouseup` text-copy/delete/insert implementation
then retires. Native code continues to own pointer capture, hit testing, drag
transport, and drop geometry; neither native nor the common layer implements
schema mutation.

## 7. Alignment with the native DOM waist

### 7.1 Two capabilities, not one early mutable token

The correct `beforeinput` order requires two distinct capability phases.

#### Phase A — read-only `EditSnapshot`

After any precursor key/clipboard/composition event is accepted, the common
gate has already normalized the intent, resolved the standard editing host,
selected the route, and snapshotted exactly one registered action handler as
required by the parent design §7–§9. It then captures an immutable action
snapshot before public `beforeinput`:

```text
EditSnapshot = {
    token, surface_handle,
    document_generation, host_generation,
    source_generation, render_generation,
    mutation_epoch, selection_revision,
    dom_selection_direction,
    dom_target_ranges,
    source_selection,
    source_target_ranges,
    source_projection_status,
    plaintext_only
}
```

The common package descriptor chooses the target rule. Radiant resolves it
using live Range/text/geometry mechanisms and projects each resulting boundary
through `source_pos_bridge.cpp` where a render-map-owned model surface exists.
JavaScript observes the normal immutable `StaticRange` values; Lambda model
notifications observe the same source projections. The snapshot also retains
the route and action-handler identities even if reconciliation later replaces
the target subtree; it does not introduce a second host classifier.

The token is read-only. Public `beforeinput` handlers may inspect the event or
cancel it, but they are notifications and do not run the model adapter. After
notification, the gate validates document, retained host, generations,
mutation epoch, source root, target-range liveness, and selected handler. A
changed selection revision is recorded but does not re-run routing; whether an
operation consumes the immutable target ranges or the current live selection
is part of its descriptor and WPT/Chromium contract.
Mutation by an author listener makes the action snapshot stale: the gate logs
an error/contract result, invokes no action handler, emits no `input`, and does
not select a replacement handler. This is the fail-closed resolution of the
stale-range case identified by the parent design §8.2/§16 Q2.

#### Phase B — invoke the snapshotted action

Only if author `beforeinput` is not canceled and the snapshot is still valid
does the gate invoke the one handler selected during preparation:

1. For the Radiant-template route, pass the read-only snapshot and normalized
   action to the registered model handler. It updates the source model and
   returns `EditResult`; no mutable DOM invocation exists.
2. For the DOM/JavaScript route, create a fresh `DomEditInvocation` from the
   validated host, target ranges, direction, and epoch, then call the UA DOM
   backend.
3. Translate the package/model `EditResult` to `EditingHandleResult`, settle
   model reconciliation or DOM selection, and have the common gate emit one
   non-cancelable `input` only for an applied or explicitly claimed result.

The read-only token itself is never promoted into a mutable invocation; a new
capability is created after validation. A selected handler returning `PASS`
leaves the transaction unhandled and does not fall through to another handler,
matching the parent design §8.2. This split keeps `beforeinput` pure,
preserves the route snapshot, and keeps an explicit capability for UA
mutation.

### 7.2 Registered-action return and explicit out-of-band completion

Replace the ambient `set_selection(selection)` side channel with a declared
`radiant-dom` operation, exposed to Lambda through the existing `dom` module:

```text
dom.finish_model_edit(surface_handle, edit_result) -> CompletionResult
```

This is an internal Lambda↔Radiant mechanism operation, not a JavaScript API,
DOM method, or alternative editable-host surface.

The registered platform-input action does **not** call this operation. It
returns `EditResult` directly to the built-in Radiant template handler. The
common gate already owns the snapshot, reconciliation checkpoint, post-action
selection, `input`, logging, and final transaction result; an extra waist call
would duplicate that envelope.

`finish_model_edit` is for a toolbar or other model command outside the active
platform-input gate. It takes the persistent `EditSurfaceHandle` established
by `edit_mount`/host binding and reuses the same internal model-reconciliation
and selection-completion mechanism as the registered action path.

The operation does not apply model semantics. It:

1. validates surface ownership and expected model/source generation;
2. roots the result and source selection;
3. stages the selection while the toolbar/application handler settles;
4. waits for the S12.1.3 reactive regeneration to publish that model revision;
5. resolves source paths against the rebuilt render map/DOM;
6. updates the native `DomSelection` once, preserving direction;
7. tags the resulting selection revision as `model-commit`; and
8. requests caret/selection presentation refresh.

“Waits” here means ordering inside the synchronous handler-settle/retransform
transaction; no borrowed handle or package plan crosses an `await`.

The old `SYSPROC_SET_SELECTION`, `pn_set_selection`,
`lambda_radiant_set_selection`, `dispatch_set_selection`, and thread-local
`pending_selection` channel retire after parity. That removal is required by
D7.5.3: runtime code must not reach Radiant through a bespoke callback when a
versioned `radiant-dom` operation is the defined boundary.

### 7.3 Stable `EditSurfaceHandle`

Toolbar commands need a capability even though clicking a toolbar is not a
`beforeinput` on the editor host. Extend `edit_mount` so a model editor owns a
stable logical surface handle:

```text
EditSurfaceHandle = document + template instance + source root + host key
```

The handle is opaque and generation-checked. It is not a raw `DomElement*`.
After reactive regeneration, native code re-resolves the current host from the
template/render-map identity. Mounting binds the model controller/action thunk
for this template instance; the document-level Radiant template action handler
is still the built-in registry entry selected by the parent design. Route
selection still uses standard `contenteditable`, runtime/template ownership,
and reverse render-map lookup. The binding adds no routing attribute or second
editable-host classifier. An action-owner failure is fail-closed and never
falls through to UA mutation of the derived DOM.

Unmount invalidates the handle. A command against a stale,
foreign-document, or non-active-surface handle returns failure without moving
selection. A toolbar may act while it temporarily owns focus when the handle
still names the document's active model surface and its last selection
revision; ordinary commands cannot target an arbitrary background editor.

The editor state stores the opaque handle and last native selection revision;
it does not store a DOM wrapper or Range.

### 7.4 Waist inventory

| May cross `radiant-dom` | Must stay out of native C/C++ |
|---|---|
| open/validate edit snapshot; return StaticRange/source projections | command/alias registry |
| open/validate/close mutable DOM invocation | `inputType` → semantic family switch |
| generic checked DOM/Range operations and atomic rollback | schema command selection |
| generic target-boundary and caret-operation resolution | formatting/list/block policy |
| source boundary projection through render map | editor Step construction/mapping |
| stage/finish model selection after retransform | UA or editor history grouping policy |
| observer, invalidation, geometry, clipboard/IME/drag transport | normalization rules or unsupported-command fallback |

All Item-bearing native handoffs use `RootFrame`/`Rooted` or a persistent root
for a genuinely persistent handle; no conservative stack scan or borrowed
unrooted Item is introduced (D5.3.3).

## 8. Unified flows and prototype surface

### 8.1 Registered model action after `beforeinput`

Public `beforeinput` remains the pure, cancelable notification defined by the
parent design. The model mutation moves into the registered Radiant-template
action stage. Conceptually, the prototype becomes:

```text
// internal registered action hook; not a propagating DOM event
on editaction(action) {
    let run = edit_handle_dom_action(editor, action)
    editor = run.editor
    status = run.result.failure ?? action.input_type
    return run.result
}
```

`editaction` is an illustrative name for the internal template action hook;
the implementation may register an existing handler thunk without adding
grammar. It is not dispatched through DOM capture/bubble and does not notify
author listeners. The common gate invokes it once, only after an uncanceled
`beforeinput`.

`edit_handle_dom_action`:

1. receives the already validated immutable action snapshot;
2. constructs the common request/action;
3. selects the model facet;
4. uses projected target ranges rather than reconstructing them;
5. applies one model Transaction if supported;
6. returns the next immutable editor and common `EditResult`.

The built-in Radiant template adapter maps the returned `EditResult` to
`EDIT_HANDLE_RECONCILED_MODEL`, `CLAIMED`, `PASS`, or `ERROR`. A supported
claimed/change result triggers reconciliation, source-selection restoration,
and the common gate's one non-cancelable `input`. An unsupported result emits
no `input`; because handler selection is already final, it cannot activate the
UA backend as a fallback.

This mutation remains inside an `on` handler as required by S12.1.3. The
subsequent DOM regeneration is the template's pure projection, not a second
editing backend.

### 8.2 Selection changes

```text
on selectionchange(evt) {
    let next = edit_accept_dom_selection(editor, evt)
    if (next.changed) { editor = next.editor }
}
```

There is no `set_selection()` call. A model-commit echo is ignored by revision;
a real pointer/keyboard/API change updates the editor selection once.

### 8.3 Toolbar and command UI

Toolbar buttons carry canonical command/input descriptors rather than
prototype-specific branch strings:

```text
on rte_cmd(command) {
    if (command.kind == 'app-save') {
        save(editor.doc)
    } else {
        let run = edit_handle_request(editor,
            request_from_toolbar(command.input_type, command.payload))
        editor = run.editor
        dom.finish_model_edit(editor.surface_handle, run.result)
    }
}
```

Bold, italic, underline, lists, history, links, images, and block commands
therefore resolve through the same descriptors as platform input. Tables,
drawings, and other editor-only commands use the extension registry. Save
remains an application command because it is not an edit of the document
model.

Toolbar enabled/active state uses the descriptor's `state_kind` and model
facet query, rather than a second UI switch. The model query reads schema,
selection, and stored marks; the UA query continues to read live DOM/session
state for `queryCommand*`.

### 8.4 Expected prototype deletions

After cutover, `rte_prototype.ls` no longer owns:

- `valid_source_pos`, `normalize_source_pos`, and their selection variants;
- `event_selection_for_doc`, `click_selection_for_doc`, and click repair;
- `drag_selection`, `drag_moved`, or the manual mousemove/mouseup edit;
- direct `edit_set_selection` before every event;
- repeated `set_selection(editor.selection)` calls; or
- the large toolbar command switch.

The prototype continues to own sample-document loading, source-to-editor
conversion, rendering templates, toolbar presentation, status, and save
behavior. Reusable selection normalization belongs in `mod_dom_adapter.ls`,
not in a UI fixture.

## 9. What deliberately remains separate

| Concern | DOM backend | Model backend | Reason not to merge |
|---|---|---|---|
| position | live node + UTF-16 boundary | source path + model offset | different lifetime and identity laws |
| mutation primitive | DOM tree operation | immutable model Step | observers/live Ranges vs schema/OT mapping |
| normalization | browser DOM compatibility | schema canonicalization | different required output |
| history payload | retained DOM delta/inverse journal | inverse Steps/Transactions | different identity and replay contracts |
| selection mapping | DOM Range mutation adjustment | `step_map` over source paths | representation-specific correctness |
| formatting state | wrappers/styles + UA typing session | marks + stored marks | different state models |
| paste | HTML fragment parsing and UA sanitation | slice parse/coerce through schema | different trust and validity boundaries |
| composition | native range transport + UA DOM session | base doc/range + model transaction | same phases, different mutation state |
| geometry | native layout tree | native layout tree | shared mechanism, never a model algorithm |
| observation | MutationObserver records | editor change/decor/collab events | distinct public contracts |

Sharing stops at the first operation that must name a DOM node or a model
path. This boundary prevents the “unified” layer from becoming a union of both
engines.

## 10. Proposed migration path

This section is an implementation appendix, not a second source of behavior
requirements. Every phase keeps the existing UA conformance gates green and
adds model coverage before deleting its predecessor.

### Phase 0 — Characterize without changing behavior

- Add focused tests for current prototype typing, backward/forward/word
  deletion, replacement, selection direction, composition, paste, history,
  toolbar formatting, pointer selection, and drag move.
- Record the current descriptor and editor-intent inventories, including
  unsupported differences.
- Add event traces proving that `beforeinput` is notification-only;
  cancellation invokes neither registered action nor `input`; and an
  uncanceled model route invokes its snapshotted action once before one
  post-action `input`.

**Exit:** both paths have stable behavior evidence and every standard intent
has an explicit inventory disposition.

### Phase 1 — Extract the pure common core

- Create `edit_registry.ls`, `edit_request.ls`, `edit_action.ls`, and
  `edit_text_policy.ls` under `lambda/dom`.
- Move registry data from `commands.ls`; leave compatibility facade functions.
- Make form-control editing and the editor word-delete command consume the
  shared text policy where platform target ranges are unavailable.
- Extend `edit_result.ls` compatibly with selection space and revision fields.

**Exit:** UA output is unchanged; registry uniqueness/coverage tests pass;
duplicate word/newline policy is removed.

### Phase 2 — Add the model adapter

- Add `lambda/editor/mod_dom_adapter.ls`.
- Replace the raw `input_type` conditional in `mod_input_intent.ls` with
  descriptor/family dispatch while retaining model command functions.
- Add request-based facade/query APIs to `mod_editor.ls`.
- Convert toolbar buttons to standard descriptors plus a model extension
  registry.
- Bind the model controller to the existing document-level Radiant template
  action handler and move prototype mutation out of public `beforeinput`.

**Exit:** headless model tests and prototype toolbar/input tests pass without
the second canonical intent or command switch; the public notification is
pure and the registered model action runs once afterwards.

### Phase 3 — Add read-only native snapshots

- Introduce `EditSnapshot` beside, not inside, `DomEditInvocation`.
- Preserve the parent gate's pre-notification route and handler snapshot.
- Resolve standard DOM target ranges once for event observation and project
  them to source targets.
- Add token validation, generation/epoch failure, direction, origin, and
  selection revision to event data.
- Keep the existing mutable UA invocation timing after uncanceled
  `beforeinput`.

**Exit:** model commands consume projected target ranges; stale snapshot and
cross-document tests fail closed; UA WPT/Chromium results are unchanged.

### Phase 4 — Cut over selection completion

- Add stable model surface binding to `edit_mount`.
- Add `dom.finish_model_edit` to the declared `radiant-dom` interface.
- Return platform-input `EditResult` directly to the common gate; use
  `finish_model_edit` only for toolbar/out-of-band model commands.
- Root and stage source selection through reactive regeneration in both paths.
- Add revision/origin-aware `edit_accept_dom_selection`.
- Convert the prototype, then delete the runtime event-selection hook and
  `set_selection` sysproc after all callers are gone.

**Exit:** no Lambda editor uses an ambient native callback; a source selection
is applied exactly once after the matching regenerated model revision.

### Phase 5 — Retire interaction workarounds

- Remove prototype click repair and selection echo.
- Route pointer drag/drop through native drag sessions and standard requests.
- Combine same-editor drag move into one model Transaction/history entry.
- Remove obsolete source-selection helper copies from UI scripts.

**Exit:** pointer selection, node selection, drag copy/move, and toolbar focus
all pass without fixture-local edit logic.

### Phase 6 — Enforce the boundary

- Audit native code for command/`inputType` policy and the editor for duplicate
  standard descriptor switches.
- Add source ratchets for the retired `set_selection` ABI and duplicate
  registries.
- Update RAD_18 and the parent editable implementation notes after the design
  is ratified and shipped.

**Exit:** the ownership rules are mechanically enforced as required by D1.10.

## 11. Verification and acceptance

### 11.1 Shared-core tests

- every alias and `inputType` resolves to one descriptor;
- standard and extension registries cannot collide;
- origin-specific constructors normalize to equal actions where semantics are
  equal;
- plaintext, payload, event-contract, target-rule, and history-class facts are
  invariant across entry points;
- text sanitation and fallback word/line boundaries are table-tested with
  Unicode, CRLF, punctuation, and edge offsets;
- result/verdict adaptation never conflates claim, change, selection change,
  and history recording.

### 11.2 Backend tests

The UA backend remains gated by the applicable WPT `contenteditable`, Input
Events, Selection/Range, and pinned Chromium editing corpus required by
D7.2.5 and D7.3.5. Exact DOM, Selection, MutationObserver, event ordering,
history, and command-query results remain its authority.

The model backend remains gated by editor schema, command, Step mapping,
history, composition, paste, decorations, collaboration, drawing, and UI
automation tests. Exact source tree, SourceSelection, Transaction, and schema
results remain its authority.

### 11.3 Cross-backend differential tests

Run the same common request sequences against a simple schema/model fixture
and a plain DOM fixture. Compare only shared semantics:

- resolved descriptor, family, payload, and target rule;
- inserted/deleted/selected textual content;
- collapsed/range state and direction;
- claim/change/selection/history result flags;
- typing/composition/history group boundaries; and
- failure/unsupported disposition.

Do not compare exact DOM markup to the model tree. For structural operations,
compare an explicitly defined abstract block/list/mark projection only where
both backends advertise that capability.

### 11.4 Native-waist tests

- snapshot is immutable and read-only;
- mutation or host replacement during `beforeinput` invalidates stale use;
- cancellation invokes no action and no `input`;
- an uncanceled model edit runs only in the snapshotted action stage, and the
  gate emits at most one post-action `input`;
- a selected handler returning `PASS` never selects a fallback handler;
- mutable DOM invocation opens only after uncanceled author dispatch;
- foreign-document, wrong-host, unmounted, and old-generation capabilities
  fail without mutation;
- returned source selection is rooted across regeneration and applies only to
  its matching model revision;
- backward direction and element boundaries survive round trips;
- selection completion produces at most one `selectionchange` and an echo
  does not clear editor stored marks;
- forced-GC stress covers every Item-bearing call (D5.3.3);
- package-disabled mode does not activate a hidden native editing policy.

### 11.5 Acceptance statement

The unification is complete when:

1. the UA and editor paths share one standard registry, request, action, text
   policy, and result implementation under `lambda/dom`;
2. the only representation-specific dispatches are the two explicit backend
   adapters;
3. `rte_prototype.ls` contains no selection normalization, click repair,
   manual drag edit, repeated native-selection push, or standard command
   switch;
4. no `set_selection` runtime/Radiant callback remains;
5. model edits never mutate live DOM through the UA backend;
6. UA edits retain their exact DOM/observer/history behavior;
7. selection/caret navigation is native and common, with source projection at
   explicit boundaries; and
8. all shared, backend, differential, native-waist, WPT/Chromium, editor, and
   UI gates pass with no unexpected failures, crashes, hangs, or timeouts.

## 12. Alternatives considered

### Make the RTE use the UA DOM backend and parse DOM back into the model

Rejected. It loses schema-first validity, stable source positions, Step
mapping, deterministic history, decorations, collaboration, and format-source
identity. MutationObserver reconciliation is appropriate for third-party DOM
editors, not for a Lambda editor whose source model already exists.

### Make ordinary `contenteditable` use the editor model internally

Rejected. Browser editing must preserve observable DOM identity, live Range
rules, arbitrary author markup, MutationObserver records, `execCommand`
compatibility, and DOM-specific normalization. A hidden schema model would be
a second DOM with lossy reconciliation.

### Share one primitive Step representation

Rejected. A useful common Step would need both live DOM handles and source
paths, plus both observer inverses and schema maps. Every consumer would branch
on representation at every step. `EditAction` is the correct common level.

### Keep separate input switches and share only native Selection

Rejected. Selection is already mostly shared; the live drift is in aliases,
intent coverage, word deletion, command state, history grouping, and result
semantics. Leaving those switches preserves the root duplication.

### Keep ambient `set_selection()`

Rejected. It has no explicit document/host/revision capability, is valid only
inside a thread-local handler context, and reaches Radiant outside the
`radiant-dom` module contract. An explicit deferred completion is safer and
composable with toolbar commands (D7.5.3).

### Compute model target ranges independently

Rejected. Source-only word/line/caret logic cannot reproduce visual lines,
bidi, grapheme, atomic-node, and cross-node DOM behavior. The native gate has
the required Selection/layout facts; it should project the result rather than
force the model to approximate it.

---

This proposal preserves the essential distinction established by the two
editors: **the DOM backend is a browser UA; the model backend is a structured
editor**. The unification is the protocol and the native presentation seam,
not the authoritative document representation.
