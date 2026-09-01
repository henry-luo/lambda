# Lambda DOM Event Dispatch — three flows, one engine

> **Status**: **RATIFIED — implementation in progress** (2026-09-01). §2–§3 are **descriptive** — the current state of the three dispatch flows, verified against the tree at 2026-09-01. §4.1's rulings **ES22–ES29 are ratified in full (2026-09-01, user)**, §4.3's resolution design is endorsed, and ESO63/ESO67/ESO69 are resolved (ESO68 narrowed to measurement). F17–F20 are implemented; F21 remains.
> **Role**: the design home for the event **dispatch mechanism** — how an event reaches handlers across the three flows, what object the handlers receive, and how cancellation composes. Complements `vibe/Lambda_Design_DOM_Default.md`, which owns default-action **placement** (what runs after dispatch); the boundary between the two docs is the boundary between *delivering* an event and *acting* on it.
> **Scope**: propagation, handler registration and addressing, the event object, cancellation/verdict semantics, synthetic dispatch, and the unification design. **Out of scope**: which default actions exist and their status (the DOM_Default ledger), state storage and the waist (DOM_State ES-series), and the JS property/binding architecture (Jube DOM3/DOM4).
> **Companion docs**: `vibe/Lambda_Design_DOM_State.md` (ES5 pipeline, ES10 peers-over-one-state, ES15 verdicts), `vibe/Lambda_Design_DOM_Default.md` (§5.2 single-sourced activation, resolved ESO49), `vibe/Lambda_Design_DOM_Pkg.md` (placement policy), `vibe/Lambda_Jube_DOM3.md`/`DOM4.md` (declared interfaces, ordinal dispatch), `doc/dev/radiant/RAD_15_Events_Input.md`, `doc/dev/js/JS_13_Web_DOM.md` §5.
> **Formal anchors**: S12.1.3 (reactive templates: body pure `fn`, mutation only in `on` handlers), S12.2.2 (element mutation), S9.1.4 (state lives in view state), D6.2.2v2 (observable property Get + `[[Call]]`), D3.4.7/D7.4.1–D7.4.4 (host-object metadata and the VMap/Jube bridge), D4.5.1v3 (the Radiant memory seam), D8 (module/runtime ownership).
> **Ledger series**: extends the DOM-State area's `ES#` (decisions) / `ESO#` (open issues) / `F#` (migration stages) per `doc/Doc_Convention.md` §4 — no new series. Decisions **ES22–ES29** (all ratified 2026-09-01); open issues **ESO63–ESO69** (ESO63/ESO67/ESO69 resolved, ESO68 narrowed); migration stages **F17–F21**.

---

## 1. What this document is

Radiant delivers events to handlers through **three coexisting flows**:

1. **Lambda behavior templates** — UA default actions in `lambda/package/dom/` (`form.ls`, `caret.ls`, …), matched by element selector.
2. **JS DOM event handlers** — `addEventListener` listeners and `on<type>` IDL handlers, dispatched by the JS realm's own 3-phase dispatcher.
3. **Lambda author template handlers** — a `.ls` page's `view`/`edit` templates that *generate* the DOM through the render map and handle events on the elements they produced.

They already share the runtime (ES12), the canonical state store (ES10), the template registry and verdict protocol (flows 1+3), and — for submit/reset — one activation claim protocol (F4). What they do **not** share is the dispatch machinery itself: three propagation walks, two event representations, two cancellation states, and one activation behavior implemented twice. §2 documents each flow as built; §3 analyzes exactly where they diverge; §4 proposes the unified design: **one propagation engine, two tiers, three addressing modes, one event object**.

Naming note: this doc says **author tier** for the spec's "author event handling" (flows 2 and 3 — they are the same spec concept in two realms) and **UA tier** for default-action handling (flow 1 plus the surviving native fallbacks).

---

## 2. Current state: the three dispatch flows

### 2.1 Ground already won: one runtime, one state, one wrapper vocabulary

Facts the unification builds on, all landed:

- **One script runtime per document** (ES12, F0a; user direction 2026-08-25). `js.runtime` *is* a Lambda `Runtime*`; the dom package loads onto `dom_document_script_runtime(doc)`. The module-state-id collision that made JS pages defer the package was ESO34, **fixed 2026-08-26** — a JS page's package-driven state dump regenerates byte-identical to the native golden. (Stale artifact: the comment block at `radiant/event.cpp:2625` still claims the JS-realm deferral "stands"; the code below it loads into the shared runtime. F21 removes it.)
- **One canonical state** (ES10). Both realms write through the same waist writers; state storage stays in C++ so views link to it during rendering.
- **One receiver vocabulary.** Behavior handlers bind `~` to the same Jube `dom_node` branded VMap that JS holds (`radiant_dom_wrap_node`), so an element is *the same value* in both realms.
- **One claim protocol for submit/reset** (F4). Both the native pointer path and the JS `dispatchEvent` path consult `radiant_behavior_claims_event` and route to the same package policy.

### 2.2 Flow 1 — behavior templates (UA default actions)

**Registration.** `lambda/package/dom/dom.ls` (entry; imports `form.ls`, which imports `validate`/`editing`/`aria`/`ime`/`menu`/`caret`/`keymap`/`dom_edit`/`commands`/`submit`/`details`). Radiant loads it once per document, lazily, on the **first discrete event** (`radiant_dom_package_ensure`, `event.cpp:2612`): it runs `import dom: lambda.package.dom.dom` via `run_script_mir` into the document's one runtime, with `template_registry_set_behavior_mode(true)` raised for the span of the load — so "is this UA behavior" is decided by **provenance, not syntax** (`TemplateEntry.is_behavior`, `template_registry.h:60`). Behavior templates are never selected by `apply()`; they attach at dispatch time to elements they did not produce. `RADIANT_DOM_PKG=0` disables the package wholesale. A script-less page gets an evaluator created on demand, and only when the target is something the package governs — a form control, a rich editing surface, or `details > summary` (EO4/EO6 + the package-governs predicate, `event.cpp:2672`).

**Dispatch.** After the author tier and only if `!defaultPrevented`: `dispatch_behavior_handler` (`event.cpp:2872`) → `behavior_match_walk` (`:2831`) walks target→root over real DOM parents, skipping synthetic elements, and selector-matches each element's Mark source against the behavior registry for a template that declares this event name (`template_registry_match_behavior` — most-specific template per element; specificity model `TMPL_SPEC_*`). Properties:

- **One-call ceiling** — at most one Lambda call per discrete event; a `'pass'` verdict continues the walk above the matched element.
- **Hot-path guard** — `mousemove`/`pointermove`/`scroll`/`wheel`/`dragmove`/`dragover` never trigger a package load and enter dispatch only when an already-loaded template declares such a handler (`event_is_hot_path`, `:2506`).
- **Verdicts are return values** (ES15): `'pass'` declines (native fallback stays in charge); `'prevent-default'` claims and sets `evcon->default_prevented`; any other return claims.
- **Fallback-until-claimed → retirement** (ES5/ES7): each surviving native default-action block asks `radiant_behavior_claims_event` first; once a class is proven migrated its native half is *deleted* (F1b checkbox/radio, F2b select-open, F3 validation), after which `'pass'` means nothing happens (DOM_Default §5.6).
- **Effects through the waist only**: `radiant.get_state`/`set_state`/`radio_group`/`dispatch`/… — `radiant.dispatch(~, "input")` re-enters the pipeline as a fresh event (`radiant_dispatch_event_from_script`, `:2783`), and the submit path dispatches its cancelable `submit` through the **JS EventTarget** (`radiant_dispatch_submit_event_from_script`, `:2802`) so JS listeners can cancel it.
- **`init` phase** (F8/ES19): a document-order walk when controls become live seeds derived state (validity, aria mirrors) with no paint dependency.

### 2.3 Flow 2 — JS DOM event handlers

**Registration.** Listener storage is **external to the DOM structs** — a side-table in `js_dom_events.cpp` (`:363–404`): `NodeListenerEntry { key, owner_doc, node_ref, target_root, listeners }`, keyed by `DomNode*` for elements, sentinel addresses for `document`/`window`, or the object pointer for a plain `new EventTarget()`, with a pointer-key hashmap index over the entries. Each `EventListener` carries the type string, a GC-rooted callback, `capture`/`once`/`passive` flags, an optional `AbortSignal` root, and a `removed` tombstone. `on<type>` IDL handlers occupy per-element slots; inline HTML attributes compile into them via `js_dom_set_event_handler_function`.

**Dispatch.** `js_dom_dispatch_event` (JS_13 §5): validate → pre-activation → `build_path` collects target→ancestors→document→window → **CAPTURING** down, **AT_TARGET**, **BUBBLING** up (per-node: the `on<type>` handler first, then a snapshot of matching listeners so concurrent add/remove cannot perturb iteration) → teardown. Stop flags (`__stop_prop`/`__stop_imm` plus transitional thread-locals) gate phase boundaries and per-node iteration.

**Native entry.** The pipeline reaches JS through `radiant_dispatch_built_event` (`event.cpp:6111`): build a JS event via a native factory (`js_create_native_mouse_event`, …), wrap the target, call `js_dom_dispatch_event`, and return **only** `js_event_is_default_prevented(ev)`. The stage is gated by `JsDispatchScope` — a document without a live JS batch context skips it entirely, so Lambda-only pages never construct JS events.

**Synthetic bridge (F19).** A direct `dispatchEvent()` / `el.click()` on a
live DOM element enters `radiant_dispatch_synthetic_dom_event` with the same
native event record and `is_trusted = false`. The guarded recursive call runs
the shared author cascade exactly once; its uncancelled click then reaches the
same `dispatch_click_default_actions` UA stage as a trusted pointer click.
Checkbox/radio, submit/reset, link, and popover activation therefore consult
the package claim protocol once. This keeps handler-owned policy in S12.1.3
while canonical state stays at the S9.1.4/D4.5.1v3 native seam. The old embedded activation pass is deleted;
re-dispatch of the exact in-flight record remains a raw JS recursion boundary,
not a second entry into native dispatch (ES25/ES26).

### 2.4 Flow 3 — author template handlers (Lambda pages)

**Registration.** A `.ls` page's `view`/`edit` statements register at script load into the **same** EvalContext-scoped `g_template_registry` — author mode (`is_behavior == false`), with `is_edit` distinguishing edit templates, per-template `state` declarations (S9.1.4), and handlers in `TemplateHandlerEntry` lists. The link to the DOM they generate is the **render map**: when a template transforms a model, the map records *result element → (template_ref, source model item)*.

**Dispatch.** `dispatch_lambda_handler` (`event.cpp:3200`): walk target→root; for each non-synthetic element, `render_map_reverse_lookup(dom_element_render_source(elem))` answers *which template instance produced this element and from which model*; if that template declares a handler for this event name, invoke it with **the model bound to the source data item** — not the DOM wrapper. That is the defining difference from flow 1: behavior templates are *element-addressed* (the element is its own model), author templates are *model-addressed through the render map* (the handler mutates data, never the view tree). `'pass'` continues the ancestor walk; if nothing claims, the function falls through to behavior dispatch (flow 1) at its bottom.

**After-effects.** `invoke_template_handler` (`event.cpp:2309`, **shared with flow 1**): bind the document runtime's EvalContext and side stack; build the Lambda event map (`build_lambda_event_map`, `:1777` — `type`, target class/tag/coords, and per-family payloads: `option_index`, `command`/`value`, `key`+modifiers, `input_type`/`data`/`mime`/`html`, `input_intent`, IME/composition, undo history); set the emit context; call `Item handler(model, event)`; read the verdict. Then the reactive loop: an edit template whose handler advanced the MarkEditor mutation epoch marks its render-map entry dirty; dirty entries **retransform** (the template body re-runs on the mutated model), deep-equal no-op elision skips unchanged output, and `rebuild_lambda_doc_incremental` patches the DOM. Lambda pages persist by regeneration (RS-series) — dispatch and re-render are one loop.

### 2.5 The pipeline as built (trusted events)

```
[N]  hit test → target path → trusted event construction
[JS] 3-phase dispatch (capture/target/bubble) — flow 2
       gated: skipped when the document has no JS batch context
       returns ONLY `prevented`; stop-propagation state stays inside
[L]  author-template walk (target→root, reverse render map) — flow 3
       │  either layer may cancel: defaultPrevented / 'prevent-default'
       ▼
[N]  defaultPrevented? → done
[L]  behavior-template walk (target→root, selector match) — flow 1
[M]  handler effects via waist primitives only
[L]  retransform dirty render-map entries → incremental rebuild   (flow-3 docs)
[N]  cascade settle → restyle / reflow / repaint
```

Synthetic events (`dispatchEvent`, `el.click()`) enter the same author and UA
stages with `is_trusted = false`. The event's exact in-flight record is guarded
against recursive native re-entry, so nested synthetic events get their own
record and pass while the current record still completes only once (ES25).

### 2.6 Three-way comparison

| | 1. Behavior (UA) | 2. JS listeners | 3. Author templates |
|---|---|---|---|
| Spec concept | UA default action / activation behavior | author event handling | author event handling |
| Registered in | template registry, `is_behavior` (provenance-stamped at package load) | `_entries` side-table + `on<type>` IDL slots | template registry, author mode + render map |
| Addressed by | element selector (tag + attribute predicates, specificity-ranked) | target instance (pointer key) | producing template instance (reverse render map) |
| Handler receives | `~` = Jube `dom_node` wrapper + Lambda event map | JS `Event`/`MouseEvent`/… object | source model item + Lambda event map |
| Propagation | target→root walk, no phases, one-call ceiling | full capture/target/bubble with stop flags | target→root walk, no capture phase |
| Cancellation | `'prevent-default'` verdict → `evcon.default_prevented` | `preventDefault()`/stop flags on the JS event; only `prevented` crosses back | same verdict protocol as flow 1 |
| Effects | waist writes to canonical state | IDL writes to the same canonical state (ES10) | model mutation → dirty → retransform → incremental rebuild |
| Synthetic events | runs after the synthetic author tier | runs in the shared author tier | runs at its ordinary path position |
| Dispatcher entry | `dispatch_behavior_handler` (`event.cpp:2872`) | `js_dom_dispatch_event` (`js_dom_events.cpp`) | `dispatch_lambda_handler` (`event.cpp:3200`) |

### 2.7 What is already shared

Flows 1+3 share the registry (two provenance categories), `invoke_template_handler`, the verdict protocol, the event-map builder, and the retransform machinery. All three share the runtime (ES12), the canonical state (ES10), and the Jube wrapper vocabulary. The conceptual ordering is already spec-shaped: flows 2 and 3 are the author tier in two realms; flow 1 is the UA tier. The unification is therefore **not** three-into-one — it is three registration styles feeding a two-tier pipeline that currently runs on three separate engines.

---

## 3. Analysis: the disunities

### 3.1 Three propagation walks over one tree

The JS dispatcher's `build_path` (real capture/bubble phases), the author walk, and the behavior walk each traverse the same ancestor chain independently. Two observable consequences:

- **Lambda handlers have no capture phase.** Author and behavior walks are target→root only.
- **Stop-propagation does not compose.** The JS stage returns only `prevented` (`event.cpp:6173` → `:6111`); its stop flags never cross the boundary. A JS `stopPropagation()` stops JS listeners on ancestors — but the author-template handlers on those *same ancestors* still run in their own walk afterwards. Symmetrically, a Lambda `'pass'`/claim is invisible to JS. The two realms' propagation controls are two unrelated state machines.

### 3.2 Two event representations, two cancellation states

JS handlers get a full `Event` object with `eventPhase`, `currentTarget`, stop flags, and expando properties; Lambda handlers get a per-dispatch Mark map rebuilt by `build_lambda_event_map`. Cancellation is mirrored — `evcon.default_prevented` on the native side, per-event flags on the JS side — and bridged by hand at every seam (`read_prevented`, the verdict-to-flag write, the F4 bridge's `js_event_is_default_prevented`). Every new event family pays the bridging cost twice, and the two views can never be handed the *same* in-flight event.

### 3.3 Synthetic dispatch formerly bypassed the Lambda flows

Before F19, `dispatchEvent`/`el.click()` entered only the JS dispatcher, which
grew a divergent activation pass: it set a clicked radio checked but did not
perform the `form.ls` group-exclusivity walk. F19 routes direct DOM dispatch to
the native synthetic bridge, so both entries now invoke that one package
policy. `test/ui/dom_synthetic_activation.json` and
`test/ui/js_dispatch_radio_group.json` pin direct click, MouseEvent dispatch,
radio exclusivity, cancellation, and popover activation.

### 3.4 Coordination was state-diffing, not protocol

Before F19, the native pipeline decided whether the behavior template might act
by observing whether the JS realm changed checkedness (`js_did_activation`) and
by restoring state after a canceled pre-activation. F19 deletes both because
the UA tier now writes only after cancellation has settled. This removes a
per-control reconciliation protocol rather than moving it to another seam
(ES26).

### 3.5 Ordering asymmetries at the author tier

Author-template handlers are conceptually peers of JS listeners, but they run strictly **after all three JS phases** rather than interleaved per node — an ancestor's Lambda handler runs after a descendant's JS bubble listener even though both are bubble-phase author handlers. No page has been observed to depend on the difference, but it is unspecified behavior that the unified engine must pin down (ESO63).

### 3.6 Duplication inventory

The remaining duplication is the stale/comment and transitional cleanup listed
in F21. F19 removed the second activation implementation and its state-diff
reconciler; F20 removed the legacy editing-dispatch bridge and folded the
remaining trusted entry families into the shared route.

---

## 4. Proposal: one engine, two tiers, three addressing modes, one event object

The unification target, stated once: **a single propagation engine delivers every event — trusted or synthetic — through an author tier in which JS listeners and author-template handlers are peers, then a UA tier owned by the behavior registry; every participant reads and writes one native event record.** The three registration stores remain: they are addressing modes, not dispatchers.

### 4.1 Rulings (ratified 2026-09-01)

**ES22 (ratified 2026-09-01) — Two-tier dispatch model.** Dispatch order is defined by *tier*, not by realm: author tier (JS listeners + author-template handlers, one propagation pass) → cancellation settled → UA tier (behavior registry claim + surviving native fallbacks). This names the structure ES5 already implies and makes it the invariant new work is measured against.

**ES23 (ratified 2026-09-01) — One propagation engine.** One walk owns path construction (`target → ancestors → document → window`), phase sequencing, and stop state for **all** author-tier participants. At each node it consults, in order: capture-phase JS listeners (capture pass), then at-target/bubble-phase JS listeners followed by that element's author-template handler (target/bubble pass). **Ratified order** (user, 2026-09-01): `A (btn, JS) → B (outer, JS) → V.on click (outer, template) → C (window, JS)` — one event cascades once, and the author-template handler fires *alongside* that node's JS listeners as the cascade passes. Author-template handlers thereby participate **listener-like**: every governed node's handler fires at its position in the cascade, and propagation continues past it — today's first-claim-stops-the-template-walk semantics retire (the claim/suppression consequence is ESO69). `stopPropagation`/`stopImmediatePropagation` gate every participant uniformly — a stop raised in either realm stops both. Lambda participants remain target/bubble-only (capture access deferred, ESO64). The engine preserves the existing economics: the hot-path guard, the **UA tier's** one-call ceiling, and the JS-realm gate survive as engine properties — the author tier may now invoke one handler per governed node on the path (bounded by path depth, still behind the hot-path guard); a document with no JS realm constructs no JS-facing view; a document with no author templates skips reverse lookups.

**ES24 (ratified 2026-09-01) — One event record.** An in-flight event is **one native record** (type, target, path position/phase, coordinates/key/intent payload, `bubbles`/`cancelable`, `default_prevented`, stop flags, `is_trusted`). Both realms see it through the existing host-object machinery: a Jube declared interface `type event { … }` in the radiant module (the same DOM3/DOM4 mechanism that already serves `dom_node` — D3.4.7/D7.4.1–D7.4.4), so the JS `Event` object becomes a branded wrapper over the record and the Lambda handler's `evt` is the same record through the same bindings. `preventDefault()`/`stopPropagation()` become waist operations on the record; the Lambda `'prevent-default'` verdict writes the same flag (ES15 verdicts stay — they are the Lambda-native spelling, now with one storage). `build_lambda_event_map` and the dual `evcon.default_prevented`-vs-JS-flag mirroring retire. JS expando properties on events remain a wrapper concern (ESO66).

**ES25 (ratified 2026-09-01) — Synthetic dispatch enters the same engine.** `dispatchEvent()` and `el.click()` construct the native record (with `is_trusted = false`) and run the same two tiers: the full author pass — author templates and behavior claims see synthetic events, which is spec-correct and today false — then the UA tier through the behavior claim protocol. The embedded activation pass in `js_dom_events.cpp` is **deleted** once claims cover its remaining classes (checkbox/radio already have templates; popover needs one — the F4 pattern extended, closing ESO49). Trust-gated default actions consult `is_trusted` on the record where the spec requires.

**ES26 (ratified 2026-09-01) — The reconciliation hacks retire with the second implementation.** `js_did_activation` state-diffing and the cancel-revert block die when exactly one activation implementation exists. Canceled-activation restore (the spec's legacy-canceled-activation steps) becomes the UA tier's own job, expressed in the claim protocol — pre-activation state capture and restore live with the tier that owns activation, not with a diff across realms.

**ES27 (ratified 2026-09-01) — Three registration stores stay.** Per-instance listeners (side-table), per-producing-template handlers (registry + render map), and per-selector behavior (registry, provenance-stamped) are genuinely different **addressing modes** and all remain. What unifies is the dispatcher, the event object, and default-action ownership — not storage. (Rejected alternatives: registering behavior templates as listeners in the side-table erases the author/UA tier boundary that `preventDefault` semantics require; compiling author-template handlers to JS listeners loses the render-map model binding and the retransform loop, and would make one realm host the other against ES10.) How the walk queries the stores — the mask plan and the two-probe inner loop — is §4.3.

**ES28 (ratified 2026-09-01) — Flow-3's after-effects are a pipeline stage, not a dispatcher.** The retransform-on-dirty → incremental-rebuild loop runs as the settle stage of the one pipeline (alongside cascade settle), unchanged in mechanism. Dispatch unification must not perturb the reactive regeneration contract (S12.1.3): handlers mutate models/state; regeneration reconciles the DOM.

**ES29 (ratified 2026-09-01, user) — Only `'prevent-default'` suppresses the UA tier.** At the author tier, participation never suppresses default actions: that a handler fired — JS listener or author-template handler, whatever it returned — leaves the UA tier reachable. Only `preventDefault()` / the `'prevent-default'` verdict (one flag on the one event record, ES24) suppresses it. `'pass'` at the author tier becomes bookkeeping with no walk-control meaning; its walk-control meaning survives only in the UA tier, where first-claimant-wins, the one-call ceiling, and the fallback contract remain load-bearing (ES15 unchanged there). Rationale, per ratification: this makes the two realms symmetric and matches the browser model. Observable change: an author template that today swallows a UA default action merely by claiming — `dispatch_lambda_handler`'s no-fall-through on claim, e.g. a claiming click handler over a checkbox suppressing the toggle — must say `'prevent-default'` explicitly, which is the honest spelling of that intent. Resolves ESO69; ESO65's stop-vs-suppress property follows from this ruling plus ES23's shared stop state.

### 4.2 What deliberately does not change

- The **waist** (DOM_State ES5/ES6): policy still drives mechanism through named primitives; this doc adds event-record operations to the waist, nothing else.
- The **default-action ledger**: which actions exist and where each half lives stays in `Lambda_Design_DOM_Default.md`; this proposal only guarantees every entry point reaches the same tier.
- **Listener/registry semantics**: `once`/`passive`/`AbortSignal`, listener snapshots during dispatch, template specificity, and the `init` phase are untouched.
- **`stopPropagation` never suppresses default actions** (spec rule): today the separate walks conform by accident; the unified engine must conform by construction — a stopped propagation still reaches the UA tier; only `default_prevented` suppresses it (ESO65).

### 4.3 Per-node handler resolution (elaboration under ES23/ES27; endorsed 2026-09-01)

How the one walk answers "what handlers exist at this node?" without querying three stores per node: **two hashed probes per node during the cascade; the behavior store is never consulted per-node at all.**

**Per-dispatch plan — masks checked once.** When the event record is constructed, the engine consults **per-store event-type masks**, the generalization of the hot-path guard ES5 already mandates for behavior dispatch:

- *any JS listener for this type in this document?* — a type→count map maintained by `addEventListener`/`removeEventListener` (the same bookkeeping browsers keep for their passive-wheel optimizations);
- *any author template declaring this event?* — a mask the registry updates at template registration;
- *any behavior template declaring it?* — exists today as the behavior-dispatch guard.

This yields `js_live` / `author_live` booleans for the walk and a gate for the UA tier. A store whose mask misses the type costs **zero** per node — `mousemove` on a plain page degenerates to today's JS-only path or to nothing.

**Per-node — at most two O(1) probes, only for stores flagged live.**

- **JS store**: the existing pointer-key hashmap probe (`event_target_index`) → that node's listener list, filtered by type + phase; the `on<type>` IDL slot is checked at the same probe. `fire_listeners` semantics are unchanged — it is simply called from the shared walk.
- **Author store**: the existing `render_map_reverse_lookup` hashed probe on the element's render source → producing template. Refinement: precompute a per-`TemplateEntry` **event-name mask** at registration, so "does template V handle `click`?" is a bitmask test rather than a `TemplateHandlerEntry` list scan. Non-element path entries (`document`, `window`) skip this probe — templates only produce elements.

The walk's inner loop is one query interface: `participants(node, type, phase)` = probe(side-table) ∪ probe(render-map).

**UA tier — once per event, not per node.** After the cascade completes and only if `!default_prevented` (ES29), the selector-match walk runs exactly as today — target→root, most-specific template, one-call ceiling — reusing the already-built path, behind its own mask. Selector matching (attribute predicates, specificity ranking) is the expensive lookup of the three, which is precisely why it stays out of the per-node loop.

**Why not one merged per-node handler table** (the alternative ES27 rejects — staleness economics, not taste):

- *Author templates*: materializing node→handler entries would rewrite them on every retransform — flow 3 regenerates elements constantly. The render map **already is** the per-node index, maintained as a byproduct of the transform itself; reverse lookup queries an index that keeps itself correct.
- *Behavior templates*: selector matches depend on live attributes (`<input type:'checkbox'>`); a materialized per-node entry goes stale on any attribute write, while lazy once-per-event matching is self-correcting and needs no materialization.
- *JS listeners*: already per-node and already hashed.

Each store is the natural index for its addressing mode; the unification lives in the walk, not in the storage.

**Cost.** Path depth is typically 5–15; ≤2 O(1) probes per node when masks are live, zero otherwise. This discharges the design half of ESO68 — what remains there is measurement.

---

## 5. Migration stages

Each stage is independently landable and behavior-neutral-verifiable against the UI-automation `.mark` goldens, the `test/ui/dom_pkg_*.json` suites, and the JS DOM gtests; a stage that changes observable ordering ships with new pinned tests, not golden edits.

| Stage | Contents | Exit criterion |
|---|---|---|
| **F17 — one event record** | Native event record + Jube `type event { … }` declared interface; JS `Event` becomes a branded wrapper over it (constructors and native factories converge); Lambda handlers receive the same record (event-map fields preserved as declared members during migration); `preventDefault`/verdict write one flag | one `default_prevented`; `build_lambda_event_map` deleted; no behavior change in goldens |
| **F18 — one propagation walk** | The engine owns path + phases + stop state; JS listeners and author-template handlers consulted per node in ES23's ratified order via the §4.3 resolution (per-store event-type masks, JS per-type listener counts, per-template event-name masks, ≤2 hashed probes per node); author-tier suppression narrowed per ES29 — claim no longer suppresses the UA tier (audit existing claiming templates; add explicit `'prevent-default'` where that intent was real); `build_path`/`build_view_stack`/author walk merge | a JS `stopPropagation()` stops author-template handlers on ancestors (pinned test); per-node interleaving pinned per ESO63; a claiming author template over a checkbox no longer suppresses the toggle without `'prevent-default'` (pinned test) |
| **F19 — synthetic dispatch + activation single-sourcing (implemented 2026-09-01)** | `dispatchEvent`/`el.click()` route through `radiant_dispatch_synthetic_dom_event`; `form.ls` owns the popover claim; checkbox/radio/popover claims are shared by both entries; the embedded JS activation pass, `js_did_activation`, and cancel-revert block are deleted | `dom_synthetic_activation.json` proves load-time `click()`, both radio synthetic entries, cancellation, and popover visibility; DOM_Default §5.2 flips; ESO49 closes |
| **F20 — remaining entry families (implemented 2026-09-01)** | `event_sim` (WebDriver substrate), editing/IME/composition dispatchers, and `radiant.dispatch()` re-entry fold onto the engine's one entry | no dispatcher constructs events outside the record factories |
| **F21 — cleanup** | Stale comments (`event.cpp:2625`); retire transitional stop-flag thread-locals (JS_13 known-issue 7); doc updates: JS_13 §5, RAD_15, DOM_Default §2.1 pipeline | grep-clean; docs verified-against stamps updated |

Sequencing rationale: F17 before F18 because a shared walk needs a shared event to thread through it; F19 after both because deleting the JS activation pass is safe only when synthetic events can reach the behavior registry; F20/F21 are consolidation.

### F18 implementation checkpoint (2026-09-01)

The shared author cascade now runs from `js_dom_dispatch_event`: it captures the
event-type liveness masks once, invokes JS listeners at their ordinary path
positions, then invokes the producing author template at the same target/bubble
position. The template registry carries both registry and per-entry event masks;
the JS listener store maintains a type-count index through add, removal, abort,
IDL replacement, and `once` tombstoning. Both routes write the one F17 native
event record, including the Lambda snake-case projections of `event_phase` and
`current_target`. This is the D3.4.7/D7.4.1v2/D7.4.4 host-record route, rather
than a second event map.

`stopPropagation` and `stopImmediatePropagation` are read from that record by
both participant kinds; a stop ends later path positions but does not cancel the
UA tier. A retransform is settled once after author dispatch, as required by
S12.1.3; replacement nodes retain non-text control state by rekeying the
state-store record to the fresh lifecycle id (S9.1.4), while a reactive text
control rebinds from its newly rendered model rather than retaining a stale
buffer. The UA tier therefore observes the replacement target, and an author
`'handled'` click does not suppress checkbox activation under ES29.

Focused verification: `make build -j8`; the Radiant no-int-cast rule; the
template-stop, checkbox, explicit-prevent-default, radio, and Todo text UI
fixtures; JS Event gtests; and the JS click/mousedown/multi-event UI fixtures
all pass. `editable_mixed_routes` could not be assessed in this worktree: its
registered fixture source is absent while unrelated test-file deletions are
already staged. F20 subsequently moves editing/IME/composition and
event-simulator entry points onto this cascade.

### F19 implementation checkpoint (2026-09-01)

Direct DOM `dispatchEvent()` and `el.click()` now pass their F17 native event
record to `radiant_dispatch_synthetic_dom_event`. Its exact-record re-entry
guard lets the shared JS/author cascade complete without recursion, then runs
the same uncancelled-click UA stage as trusted pointer input. The one stage
retains native label association and form/link execution mechanics, while
`form.ls` owns checkbox/radio state, radio group exclusivity, submit/reset, and
popover policy through the radiant waist (S12.1.3). The added pre-layout
allocator keeps the canonical S9.1.4 `ViewState` checked bit available when
load-time script calls `click()` before layout has built a `FormControlProp`.

Focused verification: `make build`; the Radiant no-int-cast rule;
`test/ui/dom_synthetic_activation.json` (6/6); `dom_pkg_prevent_default.json`
(4/4); and `dom_pkg_radio.json` (7/7). F20 removes the remaining legacy entry
families; stale comments and transitional cleanup remain F21.

### F20 implementation checkpoint (2026-09-01)

Editing, IME/composition, automation, and package `radiant.dispatch()` now use
the record-backed author-then-UA entry in `event.cpp`; the separate
`editing_dispatch.cpp` callback bridge and its first-claim author walk are
deleted. The Lambda-only route now creates a cancelable record for trusted
event families that support cancellation, so ES24's one record retains a
`'prevent-default'` verdict through the UA boundary; non-cancelable native
families remain non-cancelable. Initial Lambda-document construction also
creates the existing element-to-DOM index, so the first reactive nested event
uses the S9.1.4 state-preserving incremental reconciliation route instead of a
destructive fallback. This keeps handler effects and regeneration in the
S12.1.3 settle stage while enforcing ES22 and ES29.

Focused verification: `make build`; `dom_synthetic_activation` (6/6),
`dom_pkg_prevent_default` (4/4), and `dom_pkg_radio` (7/7); plus the four
contenteditable/composition/cancellation automation fixtures (18/18).

---

## 6. Open issues

| ID | Issue |
|---|---|
| ESO63 | ~~**Author-tier interleaving order at a node.**~~ **Resolved — ratified 2026-09-01 (user).** Per-node interleaving in one cascade: the author-template handler fires **after that node's JS listeners** in the target/bubble pass (`A btn-JS → B outer-JS → V outer-template → C window-JS`), firing alongside JS handlers at each node level — listener-like participation, with propagation continuing past it. Rationale kept from the proposal: registration-time merging is unstable under retransform (a regenerated element would "re-register" its handler), so the rule is structural; after-JS preserves the imperative layer's first-observe/first-cancel position and is the minimal per-node deformation of today's order. F18 ships the pinned interleaving test. The claim/suppression follow-on this exposes is **ESO69**. |
| ESO64 | **Capture-phase access for Lambda handlers.** No current template needs it; deferred. If a use appears, it is an additive `on capture_*(evt)` surface on the same engine, not a new walk. |
| ESO65 | **Stop-vs-suppress conformance by construction.** The unified engine must keep the UA tier reachable after stopped propagation (only `default_prevented` suppresses). Today's accidental conformance becomes a stated engine property with a test. |
| ESO66 | **Event-record lifetime and JS expandos.** Authors attach arbitrary properties to events and re-read them across listeners; with the record native, expandos live on the wrapper. Decide the wrapper-identity rule (one wrapper per record per dispatch) and the record's pool/GC seam (D4.5.1v3). |
| ESO67 | ~~**`is_trusted` parity for synthetic activation.**~~ **Resolved — F19 / ES25.** Direct DOM events carry `is_trusted = false` into the shared author and UA stages; the current click activation set intentionally runs for both trusted and synthetic records. A future host action that is genuinely trust-gated must consult that record field at its native execution waist. |
| ESO68 | **Per-node dual-consult cost** — **narrowed 2026-09-01** by the §4.3 resolution design: per-store event-type masks make undeclared types free, the walk makes ≤2 O(1) hashed probes per node only when a mask is live, and the UA tier's selector match stays once-per-event. What remains is validation, not design: implement the JS per-type listener count map and the per-template event-name masks, then measure mask bookkeeping overhead and probe constants on listener-heavy and template-heavy documents before F18 lands. |
| ESO69 | ~~**Does an author-tier claim still suppress the UA tier?**~~ **Resolved — ratified 2026-09-01 (user) as ES29**: only `'prevent-default'`/`preventDefault()` suppresses the UA tier; author-tier participation never does; `'pass'` becomes author-tier bookkeeping. Symmetric realms, browser model. Migration note for F18: audit existing author templates that rely on claim-suppression over package-governed elements and add explicit `'prevent-default'` where that intent was real; the pinned test is a claiming template over a checkbox — the toggle must proceed. |

---

## Appendix A — source map (anchors at 2026-09-01; treat as neighborhoods)

| Where | What (this doc) |
|---|---|
| `radiant/event.cpp:2612` | `radiant_dom_package_ensure` — lazy package load, behavior-mode registration, evaluator-creation predicate |
| `radiant/event.cpp:2831` / `:2872` | `behavior_match_walk` / `dispatch_behavior_handler` — flow-1 dispatch |
| `radiant/event.cpp:3200` | `dispatch_lambda_handler` — flow-3 dispatch, reverse render-map walk, fall-through to flow 1 |
| `radiant/event.cpp:2309` | `invoke_template_handler` — shared flows 1+3: context bind, event map, verdicts, retransform loop |
| `radiant/event.cpp:1777` | `build_lambda_event_map` — the Lambda event representation (retires in F17) |
| `radiant/event.cpp:6111` / `:6173` | `radiant_dispatch_built_event` / `radiant_dispatch_mouse_event` — native→JS entry; returns `prevented` only |
| `radiant/event.cpp` `radiant_dispatch_synthetic_dom_event` / `dispatch_click_default_actions` | direct DOM synthetic bridge and one shared UA click stage; native keeps label association and execution mechanics |
| `lambda/js/js_dom_events.cpp:363–404` | listener side-table structures + hashmap index |
| `lambda/js/js_dom_events.cpp` `js_dom_dispatch_event` | direct DOM target dispatch enters the native bridge; exact-record re-entry stays on the shared author cascade |
| `lambda/runtime/template_registry.h` | `TemplateEntry`/`is_behavior`/`is_edit`, specificity, behavior mode |
| `lambda/runtime/render_map.h` | result→(template_ref, model) reverse lookup |
| `lambda/module/radiant/radiant_dom_iface.cpp` | Jube declared interfaces — the mechanism ES24 extends with `type event` |
