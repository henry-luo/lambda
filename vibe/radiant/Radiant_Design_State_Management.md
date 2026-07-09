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

### 3.5 The purity lattice and the evaluation session (RS17–RS19)

> Language-level decisions recorded here because hibernation surfaced them; they should graduate to the Lambda semantics set (`../Lambda_Semantics_Features.md` / the formal-semantics ADR) as the normative home.

**The three-tier effect lattice: `pure fn ⊂ fn ⊂ pn` (RS17).**
- **`pure fn`** — totally referentially transparent. Inferred automatically; a user may also declare `pure fn` and the compiler *enforces* it ("color contracts, infer mechanics").
- **`fn`** — pure *given* (model, state, env): may additionally read the frozen **evaluation-session environment** — `today()`, `justnow()`, `input()`. Reads of `~` (model) and template state are defined as *parameters*, not effects, so they never taint inference.
- **`pn`** — full effects. Genuinely non-repeatable builtins (`clock()` is monotonic — every call differs) live here, never in the env-reader class.

**The evaluation session is a transaction, not a tab lifetime (RS18).** The session window is one evaluation pass — an event cascade / retransform-render pass, on the scale of a DB transaction or a browser event handler (seconds at most). Within the window the engine holds all page inputs constant (transaction isolation): two `today()` calls in one pass always agree; no torn trees. Env changes — synced input updates, time advancing — queue as messages and apply **between passes only** (the same loop-top discipline as K3); the UI then reacts, retransforms, and re-renders with the new env. Hibernation restore needs no special case: **restore is simply the next pass with a fresh env** — the non-pure parts re-evaluate by construction.

**Conservative inference (RS19).** Anything not provably pure is non-pure — higher-order and dynamic call sites default to non-pure rather than requiring effect polymorphism in v1. The direction is safe: over-tainting costs extra re-transform, never a wrong rebinding. `pure fn` annotations at boundaries recover precision where it matters.

**The seven consumers of the purity bit.** Only correctness needs #1; the rest ride the same inference:

1. **Caching / retransform** — the primary consumer. In UI mode, each template instance additionally tracks an **env-cell read-set** (which of today / justnow / each `input()` target it read), recorded in `render_map` like the dirty flags, so an input sync doesn't re-transform time-only parts and vice versa; `justnow()` readers legitimately re-run every pass (RS20).
2. **Parallelism** — `pure fn` is safely fork-joinable (the K15/K19 safety predicate for free); env-reader `fn`s are *also* parallel-safe within a pass because the env is frozen (an RS18 transaction-isolation dividend); only `pn` is excluded.
3. **Transpiler optimizations** — constant folding, CSE, hoisting, memoization, DCE for pure calls; env reads are CSE-able/hoistable *within a pass* (two `today()` calls are one).
4. **Scheduler abortability** — a pure/frozen-env retransform can be abandoned mid-pass on frame-budget overrun and re-run next frame with no cleanup; a useful primitive for the RO1 page scheduler.
5. **Record-replay / event sourcing** — see RS22: with the env journaled per pass, every frame is reproducible.
6. **API contracts** — `pure` appears in function types (`pure <: fn` subtyping), the validator, and native-module signatures; the one consumer that is cost rather than benefit (RSO10.3).
7. **Logging** — the lattice forces the previously-flagged decision, now resolved: `log_*` is **observationally pure** — logs are derived diagnostic output, exempt from the lattice (RS21).

**Event-sourced page state (RS22).** The state design is event sourcing: **page ≡ input docs + event journal + view states**, where durable view state is a *checkpoint of the fold* over the event history — a materialized view that lets replay start from the last snapshot instead of t=0, and lets the journal be truncated behind it. For frame-by-frame reproducibility the journal must capture *all* nondeterminism entering the page: input events (mouse, keyboard, hw), per-pass env deltas (time ticks, input-doc syncs), and mailbox arrivals (background-task results, in loop-arrival order — K20e's per-sender FIFO makes this stable), plus engine/script version stamps (replay is only guaranteed against the same code). The single-threaded page loop (RC1/RC2) means there is exactly one total order of events — journaling it is trivial, which is another dividend of the same-thread decision. Seeds already exist: the RAD_15 event JSONL log and the per-cascade state dump.

**Retention & compaction — the practical form of RS22 (RS24).** In theory, input markups + full event history = all states; in practice a long editor session accumulates enormous event volume, most of it doc-neutral (mouse move, selection, scroll), and reconstructing a late state by replaying an hour of events is unacceptably slow. So the persistent state is three parts:

> **input markups + view-state snapshots + the current interval of event history.**

Periodically the engine **folds** the oldest events into snapshots and truncates them (the DB checkpoint-and-WAL-truncate pattern; video keyframes vs inter-frames). Accepted loss: frames *between* snapshots are no longer reconstructible once their events are gone. Design specifics:

- **Two-track fold with different retention.** Doc-mutating events fold into **markup history** (deltas, owned by the input markup store); view-only events fold into **view-state snapshots**. Undo rides the markup-delta track, not the event journal — undo depth is decoupled from journal retention (users expect undo far beyond the current event interval; the view journal truncates aggressively).
- **The durability classes are the compression function.** View snapshots are concise precisely because of RS1: 10,000 mousemoves fold to one hover state, which is transient and folds to *nothing*; scroll folds to one offset, selection to one range. Snapshot = the durable-class filter applied to the folded state — RS1 designed for hibernation turns out to be the fold.
- **Snapshot cadence = quiescent boundaries** (idle, save, hibernation — the RS3 cascade-settle hook), not a raw timer, so snapshots land at meaningful settled states.
- **Verified compaction:** before truncating an interval, replay it from the previous snapshot and assert it reproduces the new one — the RS6 determinism golden running continuously as a side effect of compaction (debug builds at minimum). A divergence is caught the moment it exists, not at a distant restore.

**Time-travel debugging (RS23).** Event sourcing makes a time-travel debugger a product feature, not a research project: the user scrolls to any frame and inspects, logs, and evaluates there. Frame addressing = nearest checkpoint + deterministic replay forward (passes are transaction-scale, so replay distance is short and cheap). The effect lattice is the debugger's safety system — the part no competitor has *in the language*:
- **`pure` eval at a frame** — always safe, side-effect-free inspection: run any expression against frame N's (model, state) values.
- **`fn` eval at a frame** — safe with that frame's *frozen env*: `today()` answers what it answered then. Historical evaluation is semantically exact, not approximated.
- **`pn` eval at a frame** — cannot run inside the timeline; it **forks a branch**: a new timeline from frame N's checkpoint. What-if debugging (replay the session with a different input doc, a patched handler) is the same operation.
- **Retroactive logging** — add `log_*` statements and replay: logs from the past, guaranteed identical to what a live run would have printed (RS21: logs are derived output).
The differentiator is the lattice, not the journal — every time-travel system can record events; what kills them is paused-world evaluation, and Lambda answers it in the type system. Time-travel granularity follows RS24's retention structure: frame-exact scrubbing within the current event interval; snapshot-hopping beyond it.
Precedent calibration: rr/Replay.io record at syscall level with heavy overhead — Lambda records at event level (tiny journals) because determinism is a language property, not an instrumentation trick; Elm's famous time-travel debugger decayed because effects/subscriptions broke replay — Lambda's lattice makes replay-safety statically known; Redux DevTools time-travels only the reducer state — Lambda reproduces the *screen* at frame N, because the DOM is derived (RS5) and layout is deterministic (RC6). Scrubbing cost model: RS18's transaction-scale passes make checkpoint-to-frame replay milliseconds of work — no process snapshots or fork-per-checkpoint tricks. Implementation distance is short — the debugger is productizing infrastructure that exists for testing: the `event_sim` replay harness + JSONL event log (RAD_15), the per-cascade Mark state dump (frame checkpoints), the display-list golden machinery (RO8, pixel-exact frames), and the REPL (the eval-at-frame engine).

### 3.6 Two hibernation levels *(proposed this review)* (RS11)

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
- **RS17** — Three-tier effect lattice `pure fn ⊂ fn ⊂ pn`: pure = referentially transparent (inferred; `pure fn` declaration compiler-enforced); fn = pure given (model, state, env) — may read the frozen session env (`today()`/`justnow()`/`input()`); pn = full effects. Model/state reads are parameters, not effects. *(user-confirmed)*
- **RS18** — Evaluation session = one pass (event cascade / retransform-render), DB-transaction scale — **not** a tab lifetime. Engine holds all inputs constant within the window; env changes queue and apply between passes; `today()`/`justnow()` refresh per pass; restore = next pass with fresh env. *(user-confirmed)*
- **RS19** — Conservative purity inference: not provably pure ⇒ non-pure (higher-order/dynamic call sites default non-pure). Over-tainting costs re-transform, never wrong rebinding; `pure fn` recovers precision. *(user-confirmed)*
- **RS20** — Env-cell read-sets tracked per template instance in UI mode (in `render_map`, like dirty flags): re-transform only readers of changed cells. *(user-confirmed)*
- **RS21** — `log_*` is observationally pure — logs are derived diagnostic output, exempt from the effect lattice. Resolves the "logging inside fn" pre-decision flagged in the Unbundled Monad note. *(user-confirmed)*
- **RS22** — Event-sourced page state: page ≡ input docs + event journal + view states; durable view state = checkpoint of the fold (enables journal truncation); journal captures all nondeterminism (input events, per-pass env deltas, mailbox arrivals in loop order) + version stamps ⇒ frame-by-frame reproducibility on the single-ordered page loop. *(user-confirmed)*
- **RS23** — Time-travel debugging as a product feature riding RS22: scroll to any frame = checkpoint + short deterministic replay; eval-at-frame safety is decided by the effect lattice (`pure` = free inspection, `fn` = exact historical eval under the frozen env, `pn` = forks a what-if branch timeline); retroactive logging via RS21. Granularity gradient per RS24: frame-exact within the current interval, snapshot-hopping beyond. *(user-confirmed)*
- **RS24** — Retention & compaction: persistent state = **input markups + view-state snapshots + current interval of event history**; old events fold into snapshots and truncate (accepted loss: inter-snapshot frames). Two-track fold (doc events → markup history / view events → snapshots) with decoupled retention — undo rides the markup track; RS1 durability classes are the compression function (transient folds to nothing); snapshot at quiescent boundaries; verified compaction replays the interval before truncating (continuous RS6 self-test). *(user-confirmed)*

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
- **RSO10 — Purity-lattice implementation (residuals of the RS6 determinism gaps, mechanism decided in RS17–RS19).** The runtime substrate is already deterministic (seeded `math.random`, shape-order map iteration, `apply()` ties broken by `definition_order`, K19/K22 parallel determinism); remaining work:
  1. **Closed builtin classification audit**: every sys-func gets a class (pure / env-reader / pn); unclassified defaults to non-pure. `clock()` stays pn (monotonic); audit locale/timezone/env-var/platform readers.
  2. **Per-session `input()` memo**: keyed by (canonical target, format), reset per pass. Does not exist — `./temp/cache` is a cross-session HTTP *disk* cache with the opposite semantics (persists across sessions, re-reads local files within one). Decide failure caching: is a `T^E` from `input()` the frozen env value for the pass, or retried next pass?
  3. **Effect bit in function types**: `pure <: fn` subtyping where fn values cross boundaries — function-type syntax, validator, Jube native-module signatures (`vibe/Lambda_Native_Module_Design.md` fn/pn-prefixed sigs gain the sub-color).
  4. **Structural vs value taint**: non-pure values flowing only into text/attributes leave structure stable (rebinding safe, re-render only); values reaching control flow (conditions, iterated collections) are the structural-identity risk. Compiler distinguishes by consumption site — shrinks the risky region.
  5. Anonymous templates serialize `template_ref` as `(anon)` interned-pointer names — need canonical stable names (source position / definition-order index) to rebind under RS7.
  (Former items — env-cell read-sets and logging-in-fn — are decided: RS20, RS21.)
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
