# Radiant DOM Mutation Handling Proposal

**Filed:** 2026-07-02  
**Area:** Radiant JS DOM mutation, layout reconciliation, StateStore retention  
**Related:** `vibe/radiant/Radiant_Issue4.md`, `vibe/editing/Radiant_Editor_Stage4C.md`  
**Status:** implementation in progress

**Implementation update, 2026-07-03:** ordinary DOM mutation fallback now uses
`DOM_RECONCILE_RETAINED_FULL_LAYOUT`: it releases layout-pool-owned props,
keeps the `ViewTree` shell and retained DOM/view nodes, runs
`layout_html_doc(..., true)`, and prunes/reprojects StateStore afterward.
`view_pool_destroy` remains reserved for document/root teardown. The overflow
fixture is now covered by `dom_mutation_overflow_retained_full`. Native
`DragDropState` source removal is covered by
`dom_mutation_removed_source_clears_dragdrop`.

## 1. Problem Statement

Radiant currently has the right high-level model for live documents: scripts mutate
the retained DOM tree, then Radiant reconciles layout and paint from that mutated
DOM. The current implementation, however, still has a split reconciliation model:

1. An incremental path keeps the DOM/view nodes, re-cascades affected subtrees,
   performs incremental layout, and schedules repaint.
2. A fallback path destroys `doc->view_tree`, rebuilds layout from `doc->root`,
   and clears state that may hold stale `View*` pointers.

The fallback path is the architectural problem. Under Lambda/Radiant, the view
tree is essentially the DOM tree: `View` is `typedef DomNode View`, and reflow
generally does not create replacement DOM nodes. Destroying the `ViewTree` should
therefore mean "drop the layout epoch and layout-pool props", not "forget which
document nodes own state". StateStore state should be able to re-bind to the
same DOM node after reflow, including when the fallback path is necessary.

## 2. Current End-to-End Flow

### 2.1 DOM Mutation Entry Points

JavaScript DOM APIs in `lambda/js/js_dom.cpp` mutate the live `DomNode` tree
directly. Examples:

- `appendChild`, `insertBefore`, `removeChild`, `replaceWith` update parent and
  sibling links.
- `setAttribute`, `removeAttribute`, `className`, `style`, and
  `style.setProperty` update element attributes or style state.
- `textContent`, `innerText`, and `innerHTML` replace child contents.
- text-node `data` / `nodeValue` setters update `DomText`.

Mutation helpers also maintain editing/range/focus invariants around removal.
For example, `dom_pre_remove` clears or hands off focus/selection if the removed
subtree contains the active caret or focused control.

### 2.2 Mutation Recording

After a DOM API mutates the tree, it should call
`js_dom_mutation_notify(...)`. Some structural helpers pre-record detailed
insert/remove records before the final notify, so the required contract is:
"record the mutation details, then increment the document mutation count before
the event handler reconcile runs."

`js_dom_mutation_notify(...)`:

- increments `doc->js_mutation_count` and `doc->js_mutation_sequence`;
- records a `DomJsMutationRecord` with mutation kind, target, parent, and node
  ids when available;
- marks the target subtree and ancestors `layout_dirty`;
- calls `view_state_prune_orphans(state)` to clear state for nodes that are no
  longer connected;
- requests reflow for non-paint-only changes.

Mutation records are bounded by `DOM_JS_MUTATION_RECORD_CAP`. Overflow forces
the fallback path.

Audit note: every mutating DOM API must satisfy this contract. In particular,
structural paths that call `dom_pre_remove` / `dom_post_insert` should still
perform the final `js_dom_mutation_notify(...)`; otherwise
`post_html_handler_rebuild(...)` can see `js_mutation_count == 0` and skip
reconcile even though detail records were written.

### 2.3 Event Handler Reconcile

After a JS event handler returns, `post_html_handler_rebuild(...)` in
`radiant/event.cpp` checks whether mutations occurred.

If there are no DOM mutations, it only logs handler timing.

If mutations occurred, it first tries
`post_html_handler_incremental_rebuild(...)`.

The incremental path:

1. calls `dom_js_mutation_can_incremental(...)`;
2. collects repaint roots and old bounds;
3. re-cascades affected subtrees;
4. sets `doc->incremental_layout = true` and `doc->skip_style_reset = true`;
5. calls `layout_html_doc(..., true)`;
6. clears `layout_dirty`;
7. schedules dirty-rect repaint when possible, or full repaint when selective
   repaint is not reliable.

### 2.4 Incremental Layout

`layout_html_doc(...)` uses `doc->root` as the layout source. In incremental
mode it does not recreate the view pool. `layout_block.cpp` skips clean block
subtrees when:

- `doc->incremental_layout` is true;
- the child is an element;
- `child->layout_dirty` is false;
- the child has a previous height and view type.

This is the desired direction: retained DOM/view nodes keep their identity, while
dirty regions are reflowed.

### 2.5 Full Fallback Rebuild

If the mutation is not considered incremental-safe, `post_html_handler_rebuild`
falls back to a full rebuild:

1. re-cascade CSS on the whole tree;
2. close/drop transient interaction targets;
3. destroy `doc->view_tree` with `view_pool_destroy`;
4. allocate a fresh `ViewTree`;
5. call `layout_html_doc(..., false)`;
6. walk `doc->root` and assign view/layout fields through `set_view(...)`;
7. repaint.

Important: this rebuild does not parse the original HTML again. It reconstructs
layout from the already-mutated `doc->root` DOM tree.

The unsafe part is that the fallback also clears state that could otherwise be
re-bound:

- `doc_state_clear_drag_drop(state)`;
- `hashmap_clear(state->state_map, false)`;
- animation targets are dropped;
- cursor/focus/caret view pointers are nulled or partially reprojected.

The clear is defensive against stale pointers, but it also loses valid state for
DOM nodes that still exist.

## 3. Current Fallback Triggers

`dom_js_mutation_can_incremental(...)` currently rejects:

- missing root/layout state;
- mutation record overflow;
- missing mutation records;
- `DOM_JS_MUTATION_UNKNOWN`;
- `DOM_JS_MUTATION_TREE_REPLACE`;
- mutations touching `<style>` or stylesheet-related `<link>`;
- child insert/remove when `doc->stylesheet_count > 0`.

The last case is especially broad. Most real editor documents have stylesheets,
so a local insertion such as a drop-line indicator can still fall back even
though the DOM mutation itself is local and the affected node identities are
stable.

## 4. Proposal Goals

1. DOM mutations should be handled as local changes to the retained DOM whenever
   possible.
2. The set of mutations that force fallback should be reduced aggressively.
3. Fallback should retain StateStore-owned state and re-bind it to the same DOM
   nodes after reflow.
4. Drag-and-drop state should live fully in StateStore semantics, not in a
   parallel file-static workaround.
5. Removed nodes should still cause their state to be detached. Retention applies
   to connected nodes that survive the mutation/reflow.

## 5. Design: Reduce Fallback

### 5.1 Treat Stylesheet Presence as a Cascade Scope Problem

The current `structural-css-risk` gate rejects any child insert/remove when the
document has stylesheets. That is safe but too broad.

Replace it with scoped invalidation:

- For child insert/remove, compute a conservative cascade root:
  - the parent for insertion/removal;
  - the inserted subtree for new nodes;
  - ancestors up to the nearest style containment boundary if one exists;
  - otherwise ancestors to root for selectors that may depend on sibling/index
    relationships.
- Re-cascade that scope rather than falling back solely because stylesheets
  exist.
- Only force fallback for stylesheet mutations themselves, unsupported selector
  invalidation, or record overflow.

Selectors that require wider invalidation:

- sibling combinators (`+`, `~`);
- child index selectors (`:first-child`, `:last-child`, `:nth-*`);
- ancestor-dependent states such as `:has(...)` if/when supported.

Until the selector invalidation analyzer is precise, it is acceptable to choose
a broader root, including the whole document, while still retaining the view
state and avoiding `view_pool_destroy`.

### 5.2 Split Re-cascade Scope from Layout Scope

A mutation can require full-document style recascade without requiring
view-tree destruction.

Target modes:

| Mode | CSS scope | Layout scope | ViewTree retained |
|------|-----------|--------------|-------------------|
| paint-only | mutated node | none | yes |
| local layout | mutation root + ancestors | affected subtree/ancestors | yes |
| full recascade retained layout | whole DOM | dirty nodes/whole flow | yes |
| true fallback | whole DOM | whole flow | yes, unless document identity changed |
| document rebuild | new document/root epoch | whole flow | no |

The critical change is that "full recascade" should not imply "destroy
`ViewTree`".

### 5.3 Downgrade `TREE_REPLACE`

`innerHTML`, `textContent`, and `innerText` currently record
`DOM_JS_MUTATION_TREE_REPLACE`, which forces fallback.

Most tree replacement is still local:

- old children are detached from one parent;
- new children are inserted under the same parent;
- the parent DOM node survives.

Proposal:

- replace broad `TREE_REPLACE` with a structured record:
  - `parent`;
  - removed child range or "all children";
  - inserted first/last child;
  - mutation kind `CHILD_REPLACE`;
  - bool `parent_survives = true`.
- if the parent remains connected, use retained reflow.
- detach StateStore entries for removed descendants only.
- keep parent-owned state.

Only use true fallback/document rebuild when the root document identity changes
or mutation records cannot describe the affected endpoints.

### 5.4 Handle Record Overflow Conservatively Without Discarding State

Record overflow can still force a broad layout pass. It should not force state
discard.

When records overflow:

- mark the whole DOM as dirty;
- re-cascade the whole DOM;
- run retained full layout;
- prune orphan state by checking connectedness after layout;
- avoid clearing connected-node state.

This is slower than precise incremental layout but still preserves identity and
interaction state.

## 6. Design: Retain StateStore State Across Fallback

### 6.1 State Identity Rule

StateStore state must be keyed by durable DOM identity, not by the layout epoch.
This is compatible with Lambda/Radiant because `View == DomNode` and reflow
generally updates fields on existing nodes.

Current state split:

- `view_state_map` is keyed by `(ViewId, ViewStateKind)`, which is already close
  to the desired model because ids survive reflow for retained DOM nodes.
- `state_map` is keyed by raw node pointer plus state name. In normal reflow this
  is also stable because DOM nodes survive, but fallback must not blindly clear
  it.
- transient owner fields such as focus, hover, active, drag target, dropdown,
  caret, and drag/drop contain raw `View*`/`DomElement*` pointers and need
  validation or rebind.

### 6.2 Replace Clear-All With Validate-And-Rebind

The fallback path should stop doing blanket clears for connected DOM nodes.

Replace:

```cpp
doc_state_clear_drag_drop(state);
hashmap_clear(state->state_map, false);
animation_scheduler_remove_views(state->animation_scheduler);
```

with a reconciliation step:

```text
state_store_before_full_reflow(state, doc)
layout_html_doc(...)
state_store_after_full_reflow(state, doc)
```

`before_full_reflow` should snapshot anchors that are not safe as raw pointers
through layout-pool destruction:

- focused DOM node id;
- caret DOM boundary;
- selection DOM range;
- drag/drop source DOM node id;
- drop target DOM node id/range if connected;
- active text control DOM node id;
- animation target DOM node ids and animation descriptors.

`after_full_reflow` should:

- find nodes by stable `DomNode::id` under `doc->root`;
- keep state for nodes still connected;
- drop state for removed nodes;
- refresh weak caches such as `view->view_state_ref`;
- recompute focus-within/hover/active pseudo-state mirrors;
- restore caret projection from DOM boundary;
- restore active drag/drop if source is still connected;
- drop or restart animations whose target node disappeared.

### 6.3 State Pruning Contract

`view_state_prune_orphans(state)` already moves in the right direction: it
searches the live tree and removes entries whose ids are gone.

The fallback should use this shape, not clear everything:

- prune orphan `view_state_map` entries by `ViewId`;
- prune `state_map` entries whose node pointer is no longer connected;
- prune transient owner pointers that are no longer connected;
- keep all entries whose node is still connected.

If a future rebuild ever creates replacement DOM nodes for the same logical
source, the proposal needs an additional stable source key. For the current
Lambda/Radiant model, DOM node identity is sufficient because reflow does not
create new nodes.

## 7. Design: DragDropState Retention

### 7.1 Current Check

`DragDropState` is already owned by `DocState`:

- `DocState::drag_drop` is declared in `radiant/state_store.hpp`;
- `doc_state_begin_drag_drop(...)` allocates it from the StateStore arena;
- `doc_state_clear_drag_drop(...)` zeroes the struct.

So the owner is retained, but the live drag payload is not retained across
fallback because:

- `DragDropState::source_view` is a raw `View*`;
- `DragDropState::drop_target` is a raw `View*`;
- fallback calls `doc_state_clear_drag_drop(state)`.

### 7.2 Target Shape

Extend `DragDropState` with DOM anchors:

```cpp
DomNode* source_node;
uint32_t source_node_id;
DomNode* drop_target_node;
uint32_t drop_target_node_id;
DomBoundary drop_start;
DomBoundary drop_end;
```

Keep `source_view` and `drop_target` as cached projections if useful, but treat
them as derived fields. The durable state is the DOM node id/boundary.

On `doc_state_begin_drag_drop(...)`:

- set `source_node`/`source_node_id` from `source`;
- set `source_view = source` as cache;
- preserve `pending`, `active`, coordinates, and `drag_data`.

On fallback before reflow:

- do not clear active/pending;
- snapshot source/drop ids and range boundaries;
- null only cached `View*` fields if the implementation still destroys layout
  props.

On fallback after reflow:

- find the source node by id under `doc->root`;
- if found and connected, restore `source_view`;
- if missing, clear drag/drop because the dragged source was removed;
- re-hit-test or re-resolve the drop target from pointer coordinates/range;
- continue dispatching `dragover`, `drop`, and `dragend` from StateStore state.

This lets the Stage 4C `g_jsdnd_active` / `g_jsdnd_source` workaround be
deleted once the native DragDropState survives mutation.

## 8. Proposed Implementation Phases

### Phase 1: Make Fallback Retention-Safe

Goal: keep current fallback correctness while no longer discarding connected
state.

Work:

- Add `state_store_before_full_reflow(...)` and
  `state_store_after_full_reflow(...)`.
- Audit JS DOM mutators so every path that changes the DOM increments
  `doc->js_mutation_count` before handler reconcile.
- Replace blanket `state_map` clear with connected-node pruning.
- Preserve `view_state_map` entries by `ViewId`; prune only ids absent from the
  live DOM.
- Preserve focus/caret/selection through DOM anchors.
- Preserve `DragDropState` through source DOM id and active/pending flags.
- Keep animation retention conservative: if animation targets can be rebound by
  DOM id, retain; otherwise drop only animations whose targets cannot be found.

Verification:

- `test/ui/editor4c/drag-reorder.json` should pass without `g_jsdnd_*`.
- Add a focused test where `dragover` mutates DOM and native `DragDropState`
  remains active through `mouseup`.
- Add a focus/caret test where a handler mutates a sibling subtree and caret
  remains on the focused editable host.

### Phase 2: Retained Full Layout Instead of Destroyed ViewTree

Goal: make broad recascade/reflow retain the `ViewTree` object and node identity.

Work:

- **Implemented:** add an explicit "retained full layout" mode:
  - re-cascade whole DOM;
  - release old layout-pool props and clear per-node layout pointers;
  - call `layout_html_doc(..., true)` or an equivalent full-flow retained pass;
  - report `DOM_RECONCILE_RETAINED_FULL_LAYOUT` to event_sim.
- **Implemented:** reserve `view_pool_destroy` for document/root epoch
  replacement, not ordinary DOM mutation.
- **Partially implemented:** layout props allocated from `ViewTree::pool` are
  bulk released/replaced by `view_pool_reset_retained(...)`; a broader layout
  audit is still useful for specialized props and embedded resources.

Verification:

- `dom_mutation_structural_css_retains_state`,
  `dom_mutation_stylesheet_fallback_retains_state`, and
  `dom_mutation_innerhtml_parent_retains_child_prunes` now assert
  `retained_full_layout`.
- `dom_mutation_removed_source_clears_dragdrop` verifies that removing the
  active drag source clears native `DragDropState` deliberately instead of
  leaving a stale `View*`.
- The recent editor/list/form regressions were rerun after this slice:
  `test_list_reflow`, `test_form_beforeinput_target_ranges`,
  `test_rich_text_editor`, and `test_rich_text_editor_typing`.
- Still pending: compare retained-full layout output across a broader baseline
  fixture set.

### Phase 3: Reduce Incremental Fallbacks

Goal: convert common broad-fallback mutations to retained incremental or retained
full layout.

Work:

- Replace `structural-css-risk` with scoped selector invalidation.
- Introduce `CHILD_REPLACE` records for `innerHTML`/`textContent` parent-local
  replacement.
- **Implemented:** treat record overflow as retained full layout rather than
  state-discarding fallback.
- Add mutation logging that reports:
  - mutation kinds;
  - chosen recascade scope;
  - chosen layout mode;
  - whether state was retained, pruned, or rebound.

Verification:

- Add tests for structural insert/remove in a styled document.
- Add tests for `innerHTML` replacement preserving parent state but dropping
  removed child state.
- **Implemented:** add an overflow/batch mutation test that keeps focus/state on
  connected nodes.

## 9. Test Strategy

The main regression surface is event-driven, so the primary end-to-end coverage
should live under `test/ui` and run through `event_sim`. Current event_sim
assertions are useful, but not sufficient for the new contract:

- final DOM/layout assertions prove visible behavior, but not whether state was
  retained or recreated;
- `assert_state_store` can check `DragDropState`, view state, scroll state, and
  focus-related state, but it does not currently prove same-entry rebinding;
- `assert_state_store` only checks `drag_drop_active` / `drag_drop_pending` when
  `state->drag_drop` is non-null, so fixtures must also assert
  `"drag_drop": true` today; the assertion should be tightened so expecting
  active/pending/source fails if `drag_drop` was cleared.

### 9.1 C++ StateStore Unit Tests

Add focused unit tests for the rebind/prune helpers before relying only on UI
fixtures. These tests should create a small DOM tree and DocState, then exercise:

- connected-node `view_state_map` entries survive a simulated full reflow;
- removed-subtree view state is pruned;
- `state_map` entries for connected nodes survive;
- `state_map` entries for removed nodes are pruned;
- focus/caret/selection anchors rebind when their DOM nodes survive;
- `DragDropState` keeps `pending` / `active` / source id through rebind;
- `DragDropState` clears only when the source node is removed.

This layer should not depend on rendering pixels or event dispatch. It protects
the core state-retention invariant directly.

### 9.2 EventSim Harness Enhancements

Add small, explicit assertion hooks rather than inferring behavior from logs:

- `assert_reconcile_mode`: check the last DOM-mutation reconcile mode and
  fallback reason, e.g. `incremental`, `retained_full_layout`,
  `destructive_rebuild`, `document_rebuild`.
- `snapshot_state_store`: capture state for a target selector, including
  `DomNode::id`, requested `ViewState` kind, weak-ref status, scroll/focus flags,
  and drag/drop source/target ids.
- `assert_state_store_snapshot`: compare the current state against a previous
  snapshot. This should prove that state remained bound to the same DOM id, not
  merely that a new default state exists.
- Tighten `assert_state_store` so `drag_drop_active`, `drag_drop_pending`,
  `drag_drop_source`, and `drag_drop_target` fail when `state->drag_drop` is
  absent.
- Add optional `drag_step` or paused-drag support if primitive
  `mouse_down`/`mouse_move`/`mouse_up` steps are not enough to assert mid-drag
  state after a `dragover` mutation.

The reconcile-mode assertion should read structured state stored on the document
or DocState during reconciliation, not parse `./log.txt`.

### 9.3 Test/UI Fixture Matrix

Add fixtures under `test/ui` with matching `.html` and `.json` files:

| Fixture | Purpose | Key assertions |
|---------|---------|----------------|
| `dom_mutation_structural_css_retains_state` | Styled document inserts/removes a sibling node; this used to be a broad `structural-css-risk` case. | state snapshot before/after, focus/caret/scroll retained, reconcile mode not destructive |
| `dom_mutation_stylesheet_fallback_retains_state` | Mutating a `<style>` element still needs broad recascade. | connected state retained even when layout scope is broad |
| `dom_mutation_innerhtml_parent_retains_child_prunes` | `innerHTML` / `textContent` replaces children but parent survives. | parent state retained, removed child state pruned, DOM text updated |
| `dom_mutation_overflow_retained_full` | More mutation records than `DOM_JS_MUTATION_RECORD_CAP`. | retained full layout, connected focus/state survives |
| `dom_mutation_replacechild_notifies` | Regression for structural paths that pre-record details but miss final notify. | reconcile runs, layout/text reflects replacement |
| `dom_mutation_dragover_retains_dragdrop` | During native drag, `dragover` mutates DOM. | mid-drag `drag_drop=true`, `drag_drop_active=true`, source rebound, drop/dragend completes |
| `dom_mutation_removed_source_clears_dragdrop` | Drag source is removed by handler. | implemented; drag/drop clears deliberately, no stale source pointer |

Existing `test/ui/editor4c/drag-reorder.json` should remain as a behavior
regression, but it is not enough by itself because it currently proves the
`g_jsdnd_*` workaround rather than native `DragDropState` retention.

### 9.4 Example Development Commands

Run individual fixtures while iterating:

```bash
./lambda.exe view test/ui/dom_mutation_structural_css_retains_state.html --event-file test/ui/dom_mutation_structural_css_retains_state.json --headless
```

Regression gates:

- focused StateStore unit test executable for the rebind/prune helpers;
- `make test-ui-automation` for the broader UI automation set;
- `make editor-4c-view` to keep the editor drag-reorder and editing workflows
  green.

## 10. Open Design Risks

I do not see a fundamental blocker to retaining view state because Radiant's
current model keeps DOM node identity stable across reflow. The main design risks
are implementation details:

1. Some layout-side props are allocated from `ViewTree::pool`. A retained full
   layout must either refresh those props safely or define which props are stable
   across layout passes.
2. `state_map` currently uses raw node pointers. That is okay while DOM nodes are
   retained, but any future rebuild that replaces DOM nodes will need a stable
   logical key, probably `DomNode::id` plus a source/path fallback.
3. Animation targets need a rebind story similar to focus and drag/drop. Until
   then, animation retention can be conservative.
4. Selector invalidation can be made precise later. The first safe step is to
   broaden recascade scope while still retaining the DOM/view nodes.

## 11. Desired End State

DOM mutation handling should have this invariant:

> A script DOM mutation may invalidate style, layout, paint, and removed-node
> state, but it must not discard state for connected DOM nodes.

In practical terms:

- local DOM edits mutate `doc->root` in place;
- reflow updates layout fields on retained DOM/view nodes;
- fallback means "less precise invalidation", not "throw away StateStore";
- `DragDropState` survives handler-driven DOM mutation when its source node
  still exists;
- removed nodes are pruned deliberately, not by clearing the entire store.
