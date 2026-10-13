# Lambda Proposal — DOM Reconciliation at the Presentation Boundary

> **Status:** design record, 2026-10-13, revised the same day to reflect the rulings. **Ruled (user, 2026-10-13):** retention is fully embraced and needs no ruling exception, because **D4.5.3v2** governs only how DOM is built from Lambda scripts (§6, §8 Q1); the template result is the presentation of record, so direct DOM edits inside a component's result are unsupported and need no tracking (§4.1, §8 Q2; Reactive UI §2.7); a kept element whose attributes changed is patched in place (§8 Q3). **Open:** phasing (§8 Q4). The current design text lives in [Reactive UI](Lambda_Design_Reactive_UI.md) §9.1, §9.4 and §10.1; this document keeps the problem, the history, the mechanics and the open options.
> **Formal linkage:** **D4.5.3v2** (DOM construction from Lambda scripts deep-copies; retention downstream is embraced), **D4.5.1v4** / **D4.5.2** (presentation lifetime and the runtime boundary), **S12.1.3** (pure template bodies), **S9.1.4** (state lives in view state).
> **Working-design linkage:** [Reactive UI](Lambda_Design_Reactive_UI.md) §5.3 (runtime identity), §9.4 (no-op elision), §9.5 (observer vs tree diff), §10.1–10.3 (incremental presentation), §18.3 (memoization, keyed lists).
> **Scope:** how an incremental template re-render turns a regenerated result into DOM. Evaluation, render-map dependency tracking and template state are unchanged.

---

## 1. Problem

Today every incremental re-render deep-copies the regenerated result into fresh DOM, building it the way **D4.5.3v2** builds DOM from Lambda scripts, even where the existing DOM could be kept. `rebuild_lambda_doc_incremental` (`radiant/cmd_layout.cpp`) then:
- splices the new subtree in place of the old one;
- migrates view state, frame requests, pointer capture, focus events, navigation requests and geomap ownership from old nodes to structurally corresponding new ones (`layout_preserve_rebuilt_owners`);
- cascades the whole new subtree;
- marks it and its ancestors layout-dirty.

Retained layout cannot help inside that subtree, because every node in it is new. Retained layout here means block shift reuse and the retained flex items of 2026-10-13 (RAD_01 §6).

The doc viewer shows the cost. Its app template owns the file tree, so any change to app state re-renders the whole ~650-row tree. Measured on 2026-10-13 (debug build, `doc_viewer_toml`):

| Interaction | What re-renders | dom_patch | layout |
|---|---|---|---|
| Pane click (tab, property toggle), after pane state moved to a stateful `document_pane` and with retained flex items | document pane only | ~1.5 ms | ~18 ms |
| Folder toggle | whole tree | ~7 ms | ~176 ms |
| File open (selection moves, pane changes) | tree + pane | ~33 ms | ~305 ms |

A file open changes two rows' `selected` class and the pane, yet it pays for a fresh copy, cascade and layout of every row. Moving the selection highlight into row-local state would fix this one app. It would not help folder toggles, and every application would have to discover and apply the same remedy.

## 2. What construction guarantees

Any change must keep these properties, which **D4.5.3v2** establishes for DOM built from Lambda scripts:

1. **One occurrence, one native subtree.** Reusing one Lambda value in several slots must produce independent native nodes and child chains. A value never shares DOM between occurrences.
2. **Lambda value semantics are untouched.** The copy happens at the DOM boundary; evaluation keeps ordinary sharing and ownership-based copy elision.
3. **The evaluator's source/result mapping holds.** Event dispatch and regeneration find the current template owner from any DOM node (`dom_element_render_source`, `element_dom_map`).
4. **Rooted callables and native handles keep their identity.**

## 3. History that constrains the design

- **2026-10-11 (`8fbe9aab82`).** The D4.5.3 copy landed with whole-result no-op elision (Reactive UI §9.4). If a regenerated result was deep-equal to the previous one, the DOM was kept and `dom_rebind_equal_render_result` rebound its source keys to the new result elements.
- **2026-10-12 (`f82b4a77b`).** Elision was replaced by an identity test. The record (`vibe/impl/Lambda_Impl_Chart_Timeline.md`): equal results with a new identity require a DOM ownership update, and only an exact same-value return keeps the existing DOM and its imperative edits. The change resolved 15 frame, slideshow and SVG fixtures and three viewer cases. `dom_rebind_equal_render_result` has had no caller since.

Two lessons follow:
- Handlers were editing kept nodes directly, so an equal result no longer proved the live DOM presented it. The 2026-10-13 ruling answers this at the contract level rather than by tracking edits: the result rules, and direct edits inside a component's result are unsupported (§4.1).
- Rebinding the element map and lifecycle backing source alone was not a complete ownership update. A reused subtree must still go through every owner hand-off and render-event step that a rebuilt one does.

## 4. Design

Reactive UI §9.4 carries these rules; they are repeated here with their mechanics. When an incremental rebuild updates the presentation of old result `R0` (live DOM `D0`) to new result `R1`, walk `R0`, `R1` and `D0` together in document order. At each position, take the first rule that applies:

1. **Equal subtree → retain.** The `R1` node is deep-equal to the `R0` node (§9.4 equality: kind, tag, attribute names and values, ordered children, content). Keep the `D0` subtree in place and rebind its source keys from `R0` to `R1` elements.
2. **Same element → patch and reconcile.** The tag is equal, but the attributes or children differ. Keep the `D0` element, which keeps its view state. Patch each changed attribute in place, so a row whose `class` changed keeps its node. Reconcile its children positionally by these same rules. Record each attribute change and each child insertion, removal or replacement as a DOM mutation, so the mutation path restyles exactly the affected nodes. *(Attribute patching decided by the user, 2026-10-13.)*
3. **Otherwise → fresh copy.** Build the node from the script as **D4.5.3v2** describes, exactly as today, and run the existing owner migration (`layout_preserve_rebuilt_owners`) for it.

Rule 3 is today's behaviour, so the design changes nothing where nothing can be reused. Positional matching is deliberate. Keyed matching (Reactive UI §18.3) can extend rule 2 later without changing rules 1 or 3.

### 4.1 Direct edits are not tracked

**Ruled 2026-10-13 (user; Reactive UI §2.7).** Inside a component presented by an author template, the template result rules. A Lambda handler or a JS script editing that result subtree directly is unsupported: the two would fight, and the next regeneration that touches an edited node presents the result. So the walk compares `R0` with `R1` only, never inspects `D0`, and keeps no "pristine" bit; the mechanism drafted here earlier (a bit cleared by every mutation entry point) is withdrawn. Interaction state (value, checkedness, selection, focus, hover, scroll) is not an edit: it lives in the state store and view state (**S9.1.4**) and survives on a retained node by construction.

A page may mix directly scripted subtrees with template subtrees when they are separate. Behavior templates for form controls' UA default actions are the sanctioned case of Lambda and JS sharing one DOM state; they regenerate nothing (§2.7, §11).

### 4.2 Ownership on a retained node

A retained node keeps every pointer-keyed owner as it is: view state, frame requests, pointer capture, focus events, navigation requests and geomap. Each node retained by rules 1–2 must also:
- rebind `element_dom_map` and the lifecycle backing source from its `R0` element to its `R1` element (`dom_rebind_equal_render_result` does this today);
- queue render events for every template instance whose result lies in the retained subtree (`radiant_queue_template_render_events`), so render handlers see the new result identity, as the 10-11 elision did for its retained root.

Validation must include the 15 fixtures that the 10-12 change repaired.

### 4.3 Cascade and layout

The reconciliation is expressed as DOM mutations, which the JS mutation path already handles incrementally (`post_html_handler_incremental_rebuild`):
- mutated subtrees are recascaded;
- structural selectors (`+`, `~`, `:nth-*`, `:has`, `:empty`) get sibling and ancestor invalidation;
- new nodes are marked dirty and their ancestors' styles reset.

Retained nodes keep their resolved style and their layout. Block shift reuse and retained flex items then reuse their geometry, so a file open would re-lay out the two re-rendered rows, the pane and the containers on their paths.

## 5. Expected effect

For the doc viewer:
- **File open:** the two rows whose `selected` class changed are patched in place by rule 2 and restyled; the other rows are retained.
- **Folder toggle:** the toggled folder's child list is reconciled; rows above and below are retained.

On retained-row and pane-only layouts measured this session, both should drop from 170–300 ms to the tens of milliseconds. The equality walk costs O(size of `R1`), which is already the cost of today's deep copy, and it replaces most of the copy, cascade and layout.

## 6. Ruling impact

None needed. The user ruled on 2026-10-13 that D4.5.3 covers how DOM is constructed from Lambda scripts and nothing downstream (layout, rendering, retention), and that retaining an existing subtree should be fully embraced. **D4.5.3v2** (spec 29.5.1) states that scope: *copy at construction, retain downstream*. Retention also keeps every property in §2, because a retained subtree is the same occurrence in the same slot, never shared with another. Reactive UI §5.3 and §10.1 carry the scope, §9.4 carries the rules with the reuse condition marked open, §9.1 the ownership update for kept nodes, §17 the superseded whole-result reading, and §18.1 item 13 the open questions.

## 7. Alternatives considered

- **Row-local state in each application.** This was applied to the doc pane on 2026-10-13. It needs no engine change, but each app must restructure, and structural changes such as folder toggles still re-render everything below the owner.
- **Template memoization** (Reactive UI §18.3). Skipping evaluation of unchanged child templates is complementary. It saves evaluation, but without retention its output is still copied afresh whenever an ancestor re-renders.
- **Whole-result elision only.** Implemented 10-11 and replaced 10-12; it never helps a partial change.
- **Whole-page virtual DOM diff.** Rejected by §9.5; reconciliation stays limited to regenerated results.

## 8. Questions for review

1. ~~Is retaining an existing occurrence consistent with the intent of D4.5.3?~~ **Decided 2026-10-13 (user): yes, and fully embraced.** D4.5.3 governs DOM construction from Lambda scripts only; **D4.5.3v2** states that scope (§6).
2. ~~Is "no imperative edit since built" the right condition for reuse?~~ **Decided 2026-10-13 (user): neither.** The template result rules; direct edits inside a component's result are unsupported and untracked (§4.1).
3. ~~Should rule 2 also patch changed attributes in place on a pristine shell, such as a row's `class`?~~ **Decided 2026-10-13 (user): yes.** Rule 2 patches them; §4.3's mutation-path invalidation restyles the patched elements.
4. **Open.** What phasing do you want? Suggested:
   - R1: equal-subtree retention (rule 1) inside changed results;
   - R2: element retention with attribute patching and positional child reconciliation (rule 2) through the mutation pipeline;
   - R3 (optional): keyed matching.

   Each phase would be gated by the differential A/B harness that validated retained flex items: every UI fixture rendered with the feature on and off, with SVG snapshots compared after each interaction.
