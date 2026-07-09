# Radiant State Management — Durability Classes & State Persistence

**Status:** seed design — direction confirmed 2026-07-09 (ledger RS1–RS16; open items RSO1–RSO9)
**Context:** builds on the existing interaction-state layer (`doc/dev/radiant/RAD_17_Interaction_State.md`: `DocState`, the stateless validating FSM, the schema tables, the Mark dump) and on the page-isolate model of `Radiant_Design_Concurrency.md` (RC1–RC8). This doc adds two things RAD_17 explicitly disclaimed: a **durability contract** over the store, and a **persistence/hibernation design** — the ability to throw away a page's DOM/view tree (e.g. a hidden tab) and later reconstruct the live page from serialized state. The write model of RAD_17 (typed direct-write fields + cascade-settle validation, *not* a reactive graph) is unchanged; this doc changes what the store promises, not how it is written to.

---

## 1. The three durability classes (RS1)

Every fact the engine tracks falls into exactly one of three classes. The class is declared **in the state schema** (`state_schema.cpp`, alongside the existing `RADIANT_STATE_RULES`/`RADIANT_INVARIANTS` tables) as a persistence column per family/field — so "take a snapshot" is a schema-driven walk, not an ad-hoc dump, and the classification is auditable.

| Class | At hibernation | Belongs in the store? |
|---|---|---|
| **Durable** | serialized | yes |
| **Transient** | discarded, via a validated settle-to-neutral transition | yes |
| **Derived** (incl. scheduler bookkeeping) | dropped, recomputed/rebuilt on restore | **no** — lives with its consumer |

**Litmus test for store residency:** *if two subsystems could legitimately disagree about a value, it is state — put it in the store. If disagreement is impossible by construction because the value is recomputable, it is a cache — give it to its consumer.*

**The invariant-cost rule:** every derived value that lives in the store next to the authoritative fields it is computed from costs one invariant to police it. Existing proof: `SM_INV_SELECTION_PROJECTION_CACHE` exists *only* to keep the `CaretState`/`SelectionState` projection caches honest against the canonical `DomSelection`. Derived-in-store is always a migration in progress, never a destination.

### 1.1 Durable state — serialize (the implementor's checklist)

- Form control **dirty values**: `FormControlProp::current_value` (the live `.value`, distinct from the `value` attribute), `checked`, `selected_index`, `range_value`.
- Text-control and DOM **selection/caret**: the canonical `DomSelection`/`EditingSelection` boundary points, text-control `selection_start/end/direction`.
- **Focus** target.
- **Scroll positions**: document `scroll_x/scroll_y`, `zoom_level`, and per-view `ViewState.scroll.x/y`.
- `details`/`dialog` **open state** (today ad hoc via DOM attribute + generic `state_map`; needs a first-class home — see RSO5).
- **CSS custom-property overrides** set by script (`DomElement::css_variables` entries that deviate from stylesheet values).
- **Template reactive state**: the `template_state_map` contents ((model, template, name) → value) — serialized re-keyed by model source path (RS7).
- **Media playback position** (video/audio current time, paused/playing).
- **Visited links** (session-scoped durable).
- Shell-level companions (owned by the browsing session, serialized alongside): URL, history entry state.

Exclusions inside the durable class: **sensitive fields** — password inputs (and anything marked autocomplete-off/sensitive) are excluded from snapshots, following browser session-restore practice (RS15).

### 1.2 Transient state — discard (and *keep in the store*)

- `hover_target`, `active_target`, cursor state.
- **Drag-and-drop in flight** (`DragDropState`, `is_dragging`, drag/drop targets).
- **IME composition**: the preedit trio (`preedit_utf8/len/caret`), composition-active flags.
- **Overlay state**: `open_dropdown` + geometry, `context_menu_target` + geometry + hover index.
- Pointer-selection-in-progress and autoscroll (`EditingInteractionState`).
- Scrollbar hover/drag flags and drag-start anchors (`ViewState.scroll`).
- `value_at_focus` (the change-on-blur snapshot).
- `selectionchange` coalescing counters and the pending text-control list.
- **Undo rings** (`EditHistory`) — transient in v1; durable is a possible later upgrade (RS15).

Why transient state stays in the store (RS2):
1. It is exactly the state multiple subsystems must agree on — the CSS matcher reads `:hover`/`:active`, dispatch reads drag state, rendering reads overlays, editing reads composition. It passes the litmus test.
2. The validation boundary exists largely *for* it: the majority of the 15 `SmFamily` entries (hover, active, drag-drop, IME, dropdown, context-menu, rich-edit) are transient families with the nastiest cross-field invariants.
3. Serialization is a **filter, not a container boundary** — the snapshot walk skips transient-tagged fields for free, while the Mark dump, event-state log, WebDriver, and record-replay goldens *want* transient state included (a hover-restyle regression is a diff over transient state).
4. Discard-on-hibernate becomes an FSM transition: "settle to neutral" (cancel drag, cancel composition, close overlays) is validated by the machine instead of being a scavenger hunt across subsystem structs (RSO8).

The hot-path objection does not apply: RAD_17's store already writes transient state via typed direct fields / packed `ViewState` bits with validation deferred to cascade settle. "In the store" means store-owned memory + store writer API + schema coverage — not a subscription graph in the write path.

### 1.3 Derived state — should not be in the store at all

Caches belong to their consumers; pending-work queues belong to the scheduler. Current inventory and disposition:

- `CaretState`/`SelectionState` projections (`state_store_internal.hpp`) — **delete** once renderers read DomRange layout directly (already planned in RAD_17 §8.3); their policing invariants go with them.
- Layout geometry on `DomNode` — already outside the store; stays there. Never serialized: restore does a full layout.
- `render_map` result-node bindings — derived (rebuilt every render); acceptable in `DocState` as an explicitly-tagged binding table, never serialized, rebuilt on restore.
- `video_placements` cached geometry, `caret_blink_t`/`caret_on` — renderer/frame-clock bookkeeping; migrate out.
- Computed styles, display list, paint caches — derived; owned by their pipelines.

**Scheduler bookkeeping** (a sub-class: not recomputable, but droppable because the restore fallback is "full relayout/repaint"):
- `DirtyTracker`, `ReflowScheduler`, `needs_reflow`/`needs_repaint` — migrate to the page-loop scheduler when RO1 (concurrency doc) materializes.
- `AnimationScheduler*` — same destination.
- `EventStateLog`/`StateDumpLog` handles — diagnostics plumbing, not state.

Related cleanup: **`STATE_MODE_IMMUTABLE` (the COW/HAMT stub) is deleted, not finished** (RS16). Hibernation snapshots give coarse checkpoints; editing undo is command-based (`EditHistory`, the rich-edit transaction FSM); a copy-on-write store serves neither.

---

## 2. Snapshot mechanics (RS3, RS4)

- **Quiescent point:** snapshots are taken only at **cascade settle** with the job queue drained, at the top of the page loop. Because script + style + layout share one thread and the page is an isolate (RC1/RC2), snapshot consistency is trivial — no locks, no torn state. Preconditions enforced by the FSM: no in-flight drag/IME/rich-edit transaction (settle-to-neutral runs first, or hibernation is refused this frame).
- **One canonical format:** evolve the existing `radiant_state_dump_mark` Mark serialization into *the* snapshot format — versioned, with a tolerant reader (unknown fields dropped, missing fields defaulted). Default-suppression is kept (restore semantics: absent ⇒ default), which keeps snapshots small and diffable. One format then serves diagnostics, layout/WebDriver goldens, hibernation restore, the K29 wire (page migration between processes), and **RC7 crash recovery** — a crashed page restores from its last settled snapshot instead of only showing tab-crash UI.
- **Node identity:** never rely on monotonic `DomNode::id` reproducing across a teardown — it survives `view_pool_reset_retained` (relayout) but not re-creation. Identity across hibernation is: for Lambda pages, the model source path (RS7); for JS pages, baseline node ids assigned deterministically at parse and referenced by the delta (RS12).

---

## 3. Lambda pages — the clean part: regeneration, not serialization (RS5)

For Lambda `view`/`edit` template pages, **the DOM/view tree is derived output, not state**: DOM = f(model, template state), and template bodies are pure. Therefore the snapshot contains **zero DOM serialization**:

> snapshot = model delta (owned by the **input markup store**, separate design — RSO1) + template state (`template_state_map`) + durable interaction state (§1.1).

Resurrection = re-run the templates over (model, template state), then rebind durable interaction state. The "avoid serializing the whole DOM" goal is met absolutely here — this is the flagship advantage of the Lambda state model and should be marketed as such.

Three things become load-bearing:

### 3.1 Determinism is an invariant, not a nicety (RS6)

Restore only works if re-running templates over identical (model, state) reproduces an *identical* tree — structure and order. Pure bodies give most of this; iteration order, `apply()` dispatch order, and anything time/locale-dependent must be pinned. **Golden test from day one:** snapshot → restore → assert tree-identical. Every violation is a state-rebinding bug.

### 3.2 The identity spine is the model source path (RS7)

- `TemplateStateKey` is keyed by `Item model_item` **identity** (`lambda/template_state.h`) — identity dies with the heap, and Lambda map equality is structural. The codebase already solved this once: `render_map` records stable **source paths** (child-index arrays) precisely because Item identity was unusable (RAD_01 §7). Serialized template state is keyed by source path and re-bound to model items on restore.
- Radiant-store interaction state on generated nodes (scroll on a particular div, focus, form values) is keyed **template-instance-relative**: (template instance = (model source path, template_ref), path-within-template-output). Exact addressing scheme: RSO2.
- The input markup store is the *provider* of stable source-path identity — that contract is the critical seam between the two stores and is fixed here even though the markup store design is separate (RSO1).

### 3.3 Lifecycle: state survives hibernation; resources never do (RS8)

`tmpl_state_get_or_init` is already restore-friendly (state present ⇒ no re-init). But `init` hooks that acquire resources or start tasks must re-run at resurrection while state-init must not. Templates gain a **suspend/resume** lifecycle pair distinct from `init`/`update` (grammar: RSO3). This composes with the resource decisions R1–R5 (`vibe/Lambda_Semantics_Features.md` §3.5): open resources auto-close at hibernation (it is a scope exit); resurrection re-acquires in `resume`. One rule covers it: **state survives hibernation; resources never do.**

### 3.4 Timers and background tasks (RS9, RS10)

- **Timers: pause + relative-time resume** is the general semantic (drop is a per-timer opt-in). Timer callbacks are closures; closures are values under the K13 capture rule, so they serialize. UI-assumption ("timers are presentation-related") holds as the default policy, not a hard rule.
- **Background jobs: detachability = context ownership.** A job that must survive the page's hibernation is the analogue of a *service worker*, not a page worker:
  - Detachable tasks' closures must not capture DOM/view handles — enforced through the K13 capture analysis (DOM handles are not capturable into `start`).
  - Results return to the **page mailbox keyed by request id**, never by node pointer. Hibernated page = parked loop + retained mailbox; messages buffer (K20) and drain at resurrection.
  - Page-owned `start` tasks complete/cancel at eviction or block it; jobs that outlive eviction are owned by the shell/service context (`start process` or a service). This needs to be baked into the Lambda/Radiant embedding — it does not exist today.

### 3.5 Two hibernation levels *(proposed this review)* (RS11)

| Level | What is dropped | Serialization | Background tasks |
|---|---|---|---|
| **L1 — discard derived** | views, layout, paint caches, display lists (the memory hogs) | **none** — isolate stays alive; model + template state remain in heap | keep running untouched |
| **L2 — full eviction** | the whole isolate | full snapshot (§2) to disk | page-owned tasks end; service-owned jobs continue |

L1 covers the common hidden-tab case at near-zero cost and risk; L2 is session restore / memory-pressure eviction / migration. Trigger policy: RSO8.

---

## 4. JS pages — the messy part: best effort, deterministic (RS12–RS14)

### 4.1 DOM persistence: baseline + delta, with delta as *optimization* (RS12)

Design goal: avoid serializing the whole DOM — capture only the changed delta. Constraints that make the delta correct:

- The **baseline** (original parsed source) must be cached and re-obtainable, with node ids assigned deterministically at parse; the delta references baseline ids.
- A per-node changed *bit* alone cannot express removals, moves, or reorders. The chosen shape: **dirty-subtree flags** — serialize the minimal enclosing changed subtree wholesale, anchored by (baseline parent id, index), plus **tombstones** for removals. (The alternative, an exact mutation log, is unbounded on long-lived pages and needs compaction — RSO6.)
- Detached-but-referenced nodes (removed from the tree but held by script) are part of the script-heap graph and share the same object-id space as the DOM serialization.

**Build order:** full current-DOM→Mark serialization first as the correctness path; the subtree delta is compression layered on top. Note the pleasant asymmetry: the delta machinery is JS-only — Lambda pages never need it (§3).

### 4.2 Script state: deterministic best-effort (RS13)

A half-restored heap is worse than a predictable reload, so "best effort" is *defined*, not vague:

> restore DOM (baseline + delta) → restore durable Radiant-store state (form values, scroll, focus, selection — most of the user-visible value; this is what browser session restore does) → **re-run scripts** → offer an opt-in `pagehide`/hydrate-style API for apps that want to save custom state.

No JS heap tracing in v1. Full transparent heap serialization (Smalltalk-image / Dart-snapshot style) remains *possible* later because LambdaJS values live on the shared Item/GC heap and functions have stable (module, function-index) identity with MIR re-JIT on load — but the true root set is larger than "the globals" (module map, pending timer/promise jobs, the DOM event-listener table, host-retained callbacks), and host resources (sockets, in-flight fetches) can never serialize. It is deliberately out of scope; the offered upgrade path is **migrate the page to Lambda templates** for perfect hibernation.

### 4.3 Mixed pages downgrade (RS14)

A page containing Lambda templates *and* non-cooperative `<script>` JS is persisted at the JS tier — the clean tier's guarantee only holds if nothing outside the store mutates state. The persistence tier is an **explicit, detectable page property**, so the contract stays testable.

---

## 5. Decision ledger (RS)

- **RS1** — Three durability classes — durable / transient / derived — declared as a persistence column in the state schema; snapshotting is a schema-driven walk. *(user-confirmed)*
- **RS2** — Store residency: durable + transient live in the store; derived state lives with its consumers; scheduler bookkeeping migrates to the RO1 page scheduler. Litmus test: "could two subsystems disagree?" Every derived-in-store field costs an invariant. *(user-confirmed)*
- **RS3** — Snapshots only at cascade settle with the job queue drained; consistency is free on the single-threaded page isolate (RC1/RC2); FSM-validated settle-to-neutral precedes hibernation.
- **RS4** — One canonical, versioned Mark snapshot format, evolved from `radiant_state_dump_mark`, serving diagnostics, goldens, restore, K29 wire, and RC7 crash recovery.
- **RS5** — Lambda pages: DOM is derived — persistence is **regeneration, not serialization**; snapshot = model delta (input markup store) + template state + durable interaction state. Zero DOM serialization. *(user-confirmed)*
- **RS6** — Template re-run determinism is a tested invariant: snapshot → restore → tree-identical golden.
- **RS7** — The identity spine is the **model source path**: template state serialized by source path (fixing `TemplateStateKey`'s Item-identity keying); interaction state keyed template-instance-relative.
- **RS8** — State survives hibernation; resources never do. Templates gain suspend/resume lifecycle hooks; composes with R1–R5 auto-close.
- **RS9** — Timers pause with relative-time resume (drop is per-timer opt-in). *(user-confirmed direction: stopped-as-UI-related)*
- **RS10** — Background-job detachability = context ownership: no DOM capture in `start` closures (K13-enforced); results via page mailbox by request id; eviction-surviving jobs are shell/service-owned. *(user-confirmed direction)*
- **RS11** — Two hibernation levels: L1 discard-derived (no serialization, isolate alive, jobs run on) and L2 full eviction (snapshot). *(proposed this review)*
- **RS12** — JS DOM persistence: cached baseline + deterministic parse-time ids; dirty-subtree delta with tombstones; delta is an optimization over the full-DOM correctness path. *(user-confirmed: delta-not-whole-DOM goal)*
- **RS13** — JS script state: deterministic best-effort — restore DOM + durable store, re-run scripts, opt-in hydration API; no heap tracing in v1; Lambda migration is the upgrade path. *(user-confirmed direction)*
- **RS14** — Mixed Lambda+JS pages persist at the JS tier; persistence tier is an explicit page property.
- **RS15** — Sensitive form fields (passwords) excluded from snapshots; undo rings transient in v1.
- **RS16** — `STATE_MODE_IMMUTABLE` (COW/HAMT stub) is deleted, not completed; undo stays command-based. *(proposed this review)*

## 6. Open items (RSO)

- **RSO1 — Input markup store.** The model-change store: delta capture, the source-path identity contract (provider side of RS7), and its natural unification with edit-template undo/redo. Separate design discussion.
- **RSO2 — Template-instance-relative addressing.** Exact key format for interaction state on generated nodes: (model source path, template_ref, path-within-output) encoding, and collision rules when templates nest.
- **RSO3 — Suspend/resume hook syntax** in `view`/`edit` templates (`Reactive_UI.md` grammar extension) and their ordering guarantees relative to `init`/`update`.
- **RSO4 — Timer resume details:** relative vs absolute deadlines, clamping policy, interaction with rAF and the frame clock.
- **RSO5 — Schema persistence column + resident migrations:** implement the class tags; give `details`/`dialog` open a first-class field; unify the value/selection double-home (`FormControlProp` ↔ `ViewState.form`); collapse the triple-homed `dropdown_open`; decide the CSS-var override capture point; single-home scroll.
- **RSO6 — Delta compaction** for long-lived JS pages (squash per-subtree; bound tombstone growth).
- **RSO7 — `Serializable` constraint & migration:** validator-checked serializability of declared `state` types; snapshot schema version stamp; migration hooks on restore.
- **RSO8 — L1/L2 trigger policy** (hidden-tab timer, memory pressure, explicit shell action) and the exact settle-to-neutral set (cancel drag, cancel IME, close overlays — what else?).
- **RSO9 — Derived-state evictions:** delete the caret/selection projections (+ their invariants); move `DirtyTracker`/`ReflowScheduler`/`AnimationScheduler` into the RO1 page scheduler; relocate renderer bookkeeping (`caret_blink_t`, video placement cache).
- **RSO10 — Template-body purity (the RS6 determinism gaps).** The runtime substrate is already deterministic (seeded `math.random`, shape-order map iteration, `apply()` ties broken by `definition_order`, K19/K22 parallel determinism) — the leaks are at the language surface, and `fn` rules alone do not close them:
  1. `today()`/`justnow()` are fn-callable → a body rendering them regenerates a different tree at restore. Fix options: a stricter **template-pure** tier enforced at `build_ast` for view/edit bodies (no time, no I/O), or capture them as implicit snapshot inputs replayed at restore (extends `justnow()`'s existing per-evaluation freeze across hibernation).
  2. `input()` is fn-callable → I/O can hide inside a body, silently violating DOM = f(model, state) and failing offline at restore. Doctrine: **all I/O enters through the model**; bodies are statically I/O-free (same `build_ast` check).
  3. Anonymous templates serialize `template_ref` as `(anon)` interned-pointer names — need canonical stable names (source position / definition-order index) to rebind under RS7.
  Boundary conditions to pin, not fix: snapshots are **machine-local in v1** (libm float differences across platforms); error values rendered into trees must stay value-only (no addresses/timestamps).

## Cross-references

| This doc | Related design |
|---|---|
| Store write model, FSM, schema tables, Mark dump | `doc/dev/radiant/RAD_17_Interaction_State.md` (unchanged by this doc except the persistence contract) |
| Page isolate, quiescent loop, mailboxes, K29 wire, RC7 crash recovery, RO1 scheduler | `Radiant_Design_Concurrency.md` (RC1–RC8, RO1) + `../Lambda_Design_Concurrency.md` (K13 capture, K18 processes, K20 mailboxes, K29 wire) |
| Template state runtime | `lambda/template_state.h`, `lambda/render_map.h`, `doc/Reactive_UI.md` |
| Source-path identity precedent | RAD_01 §7 (`source_pos_bridge`, render-map source paths) |
| Resource lifecycle (R1–R5) | `../Lambda_Semantics_Features.md` §3.5 |
| Form dirty value / undo ring / IME | `doc/dev/radiant/RAD_19_Form_Controls.md` |
