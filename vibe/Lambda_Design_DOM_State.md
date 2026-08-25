# Lambda DOM State — Element Behavior Templates & the State-Store Migration

> **Status**: **accepted — committed direction** (2026-08-25). The architecture below is decided (ES14); what remains open is phasing, tuning, and the ESO implementation items — no measurement gates adoption.
> **Scope**: migrate Radiant's element interaction-state *policy* — state transitions, default actions, validation, **text-control value editing**, and (long-term) the declarative state schema — from C++ into Lambda **behavior templates** under `lambda/package/dom`, built on the shipped `view`/`edit` reactive-template design. **Initial slice: HTML form states.**
> **Companion docs**: `vibe/Lambda_Design_DOM_Pkg.md` (layering, placement policy, event-pipeline decision 2 — this proposal is the concrete design for its "state store → L" and "forms → L (Phase 3)" rows), `vibe/Reactive_UI.md`–`Reactive_UI5.md` (the template system, shipped), `vibe/radiant/Radiant_Design_State_Management.md` (RS durability classes), `doc/dev/radiant/RAD_17_Interaction_State.md`, `RAD_19_Form_Controls.md`, `RAD_15_Events_Input.md`.
> **Formal anchors**: S12.1.3 (reactive templates: body = pure `fn`, mutation only in `on` handlers), S9.1.4 (state lives in view state, never in function values), S12.2.2 (element mutation), S7.6/S7.10 (error discharge and the sys-func contract), D4.5.1v3 (the Radiant memory seam: pin, gen-check, copy-as-value), D7.2.1–D7.2.3 (script packages), D8.5.1 (MIR module cache).

---

## 1. Purpose and positioning

### 1.1 What this proposal is

Radiant's interaction state lives today in a ~11K-LOC C++ subsystem (`state_store.cpp` 8,175 + `state_machine.cpp` 1,816 + `state_schema.cpp` 926 + `state_store_internal.hpp`, plus ~1.9K lines of declarations in `event.hpp`) — comparable in size to the entire event subsystem. The state *storage* is efficient and tightly coupled to layout/paint, but the state *policy* — which events flip which states, what the default action of a click on a checkbox is, what makes an input `:valid` — is imperative C++ scattered through `event.cpp` and `text_edit.cpp`, with a declarative schema (`state_schema.cpp`) that only audits transitions in debug builds and never drives them.

This proposal moves the **policy** to Lambda. New scripts under `lambda/package/dom` declare per-element-kind behavior using the existing `view`/`edit` template design:

```lambda
view <a href: any> state visited, hover {}
on click(evt) {
    if (evt.mods == 0) { request_navigation(~.href); visited = true }
}

view <input type:'checkbox'> state checked, indeterminate {}
on click(evt) {
    if (get_state(~, 'disabled')) return
    checked = not checked
    indeterminate = false
    dispatch(~, 'change')
}
```

Radiant routes events on target elements to these handlers; the handlers own the state transitions and default actions. The C++ side keeps the mechanism: storage, CSS-restyle invalidation, hit testing, dispatch, painting.

### 1.2 Positioning against `Lambda_Design_DOM_Pkg.md`

The DOM-package proposal already made the placement rulings this design executes:

| DOM_Pkg ruling | This proposal |
| --- | --- |
| "Default actions / activation behavior (link follow, checkbox/radio toggle, submit-on-Enter …)" → **L** (decision 2) | Behavior-template `on` handlers are exactly those default actions |
| "Constraint validation logic" and "form-data-set construction/serialization" → **L (Phase 3)** | Phases F3/F4 below |
| "State store (RS1–RS16 …)" → **L** (decision 2) | §3.4 defines what "state store in Lambda" concretely means (policy + schema migrate; storage tiers stay native) |
| Event pipeline split §4.4: hot-path guard, one-call ceiling, fallback-until-registered, post-dispatch reentrancy | Adopted verbatim as the dispatch contract (§3.5) |
| "Key→editing-command mapping policy → **L** (later) … *text-insertion mechanics stay N*" | **Superseded for form controls** (user decision, 2026-08-25): text-control edit *decisions* — including the value-mutation policy itself — move to Lambda (F5/F6, ES9); only the splice/storage/geometry mechanism stays native (§5). DOM_Pkg's forms/events rows and RAD_19 §1 need a versioned amendment when F5 lands |
| Placement tests: frequency / coupling / incumbency / shape | Used per state family in §5 to decide what does *not* migrate |

One deliberate difference in sequencing: DOM_Pkg gates its Phase 3 behind Phases 0–2 (Jube module POC → Obscura-parity APIs → engine-integrated APIs) because those phases build the L3/L4 API surface for **page JS**. Behavior templates need none of that surface — they ride the **already-shipped** reactive-template infrastructure (`template_registry`, `template_state`, `render_map`, `dispatch_lambda_handler` in `radiant/event.cpp`, incremental rebuild — Reactive_UI phases 1–26, all implemented) and the already-registered `radiant` Jube module (`lambda/module/radiant/radiant_module.cpp`, statically registered in `jube_registry.cpp`). This track can therefore proceed in parallel with DOM_Pkg Phase 1, sharing only the M-level primitive additions (§3.6). Where the two tracks meet — the JS-visible event pipeline — this proposal conforms to decision 2 so nothing needs re-deciding later.

### 1.3 Why now

1. **Duplication is already live.** Native C++ implements form default actions for HTML pages (checkbox toggle at `event.cpp` `handle_event` MOUSE_UP, radio-group unchecking via `radiant_uncheck_radio_group`, text editing via `te_*`), while Lambda app templates re-implement the same behaviors in script for `.ls` pages (todo2.ls splices text per keystroke in its `on input` handler). Every new interaction feature is currently built twice.
2. **The C++ policy surface is where the bugs and gaps are.** `te_validate` is a v1 subset (`pattern` unimplemented — `TODO(F5)`, "needs lazy-compiled regex" — no `min`/`max`/`step`/`minlength`); form submission **does not exist at all** in Radiant. These are spec-shaped state machines over stable primitives — the DOM_Pkg shape test says Lambda, and Lambda has the regex, string, and serialization machinery the C++ side lacks.
3. **The schema wants to be data.** `RADIANT_STATE_RULES[]` (80 rules) and `RADIANT_INVARIANTS[]` (26 bindings) are static C tables that only run under `!NDEBUG`. They are a declarative FSM spec trapped in C — the natural end state is Lambda data in the same package as the transitions they describe.
4. **Dogfooding.** The dom package becomes the second large behavior-heavy Lambda package driving Radiant (after `lambda/package/editor`), exercising the template system, the Jube waist, and the MIR cache under real interaction load.

---

## 2. Current state (survey)

### 2.1 The C++ state subsystem — what actually exists

Corrections to RAD_17 discovered during this survey are flagged ⚠.

**Storage is four tiers, not one store** (`radiant/event.hpp`, `state_store.cpp`):

| Tier | Structure | Holds |
| --- | --- | --- |
| A — generic map | `state_map: StateKey{node*, interned name} → StateEntry{Item, version}` | long-tail pseudo-classes: `:valid`, `:invalid`, `:placeholder-shown`, `:visited`, `:selected`, `:indeterminate`, `:target`, `:focus-within`, `:focus-visible` — already `Item`-valued, low-traffic |
| B — per-view packed | `view_state_map → ViewState` (bit-packed; union of scroll payload \| form payload) + raw `DomNode::view_state_ref` cache | hover/active/focus bits; checked, selected_index, range_value, selection mirrors, current_value mirror |
| C — DocState fields | ~160 fields on the per-document `DocState` | focus/hover/active/drag targets, canonical selection, IME, dropdown & context-menu overlays, dirty/reflow queues, template maps |
| D — foreign structs | `FormControlProp` (view pool) | canonical text value (UTF-8 buffer + UTF-16 IDL offsets), undo ring, validity message, preedit |

**The state machine stores nothing and drives nothing.** FSM states are derived on demand from `DocState` fields (`sm_derive_*`, `state_schema.cpp`); the 80-rule table is consulted only *after* a mutation, in debug builds, to assert the transition was declared (`SmTransitionGuard` compiles to nothing under `NDEBUG`). Transitions themselves are ordinary imperative writers. **Form families have no transition API at all** — a checkbox click is inline code in `event.cpp` (read `:checked`, negate, `form_control_set_checked`, `sync_pseudo_state`, dispatch `change`).

**Restyle invalidation is coarse.** A pseudo-state change triggers `recascade_document_for_pseudo_state` — clear cascaded styles on the whole tree, re-apply every stylesheet — mitigated only by a stylesheet pre-scan (`document_has_hover_rules`) and a hover special case that dirty-marks the old/new ancestor chains. The CSS selector matcher reads pseudo-state through a resolver callback into DocState (`state_configure_selector_matcher` → `state_get_pseudo_state`), i.e. per selector match, per element — firmly native territory.

**Form state is triple-mirrored** (`FormControlProp` ↔ `ViewState` ↔ `DocState::sel`) with manual bidirectional sync (`form_control_sync_text_control_state` / `form_control_restore_text_control_state`), and paint/layout partially bypass the API to read `form->current_value` off the raw struct (`render_form.cpp`, `layout_form.cpp`).

**Lifetime is weak-pointer + prune.** Twenty-odd DocState fields cache raw `View*` that die on relayout, repaired by `state_store_prune_after_reflow` (called from `layout.cpp` and `event.cpp`). `typedef DomNode View` — the state store writes a cache pointer directly into the DOM node struct.

⚠ RAD_17 is stale on two points found during this survey: batching is no longer a file-static `s_in_batch` bool but a per-document `DocState::state_batch_depth` counter (its Known-Issue #3 is done), and its `file:line` references have drifted 100–450 lines throughout.

### 2.2 The reactive-template infrastructure — shipped and load-bearing

Everything below exists and is exercised by the todo2 app, the `rte_prototype.ls` editor page, and the UI-automation baseline:

- **Grammar & registry**: `view`/`edit` declarations with element/map/type/union patterns, `state k: v` declarations, `on event(evt)` handler blocks (`grammar.js` `view_stam`; `lambda/runtime/template_registry.cpp`).
- **Central template state**: `TemplateStateKey{model_item, template_ref, state_name} → Item` (`lambda/runtime/template_state.h`), **unified into Radiant's DocState** (`state_store.cpp` binds `template_state_map`/`render_map` at store creation). This is the working answer to DOM_Pkg open question Q1 — package state is document-anchored.
- **Dispatch**: `dispatch_lambda_handler` (`radiant/event.cpp`) routes ~18 event types (click, dblclick, mousedown/up/move, keydown, input, change, blur, paste, cut, selectstart, selectionchange, drag family, beforeinput) to template handlers, with `emit()`/`set_selection()` callbacks, dirty-tracked re-transform, no-op elision, and incremental DOM rebuild.
- **Route arbitration**: the editing subsystem already arbitrates per-surface between JS and Lambda handlers (`EDITING_ROUTE_DOM_SCRIPT` vs `EDITING_ROUTE_RADIANT_TEMPLATE`) through a priority-ordered, route-masked, generation-checked registry (`editing_action_registry_*`, `editing_template_handler.cpp`) — the proven pattern this proposal generalizes.
- **Runtime embedding**: the full Lambda runtime + MIR JIT is linked into Radiant; `DomDocument` retains `lambda_runtime`; Radiant already loads four `lambda/package/*` packages from C++ by generating `import pkg: lambda.package.<name>.<entry>` source and calling `run_script_mir` (`cmd_layout.cpp` math/latex/pdf, `graph_bridge.cpp` graph).

### 2.3 The gaps this proposal must close

1. **Plain-HTML anchoring (the inversion).** Template handlers today are found by reverse render-map lookup — which only knows elements *produced by* templates via `apply()`. Plain HTML documents skip Lambda dispatch entirely (guard in `dispatch_lambda_handler`: no registry ⇒ no-op). Behavior templates must attach to elements the template did **not** produce.
2. **Attribute-value matching.** `template_matches` (`template_registry.cpp`) checks tag name + attribute *count* only. `view <input type:'checkbox'>` needs attribute-value predicates and a specificity order.
3. **Bare state names.** The grammar requires `state name: value`; `state visited, hover` (engine-backed states, no initializer) does not parse today.
4. **Engine-backed state binding.** Template state writes go to `tmpl_state_set` (the template map). `checked = …` in a behavior handler must instead reach the canonical engine stores (tier A/B) so CSS `:checked` and paint see it.
5. **Default-action ordering & `prevent_default`.** Behavior handlers are UA default actions: they must run *after* JS listeners and app-template handlers, be suppressed by `defaultPrevented`, and Lambda handlers need a way to consume/cancel (today the Lambda event object has no `prevent_default`).
6. **Package loading & fallback.** No runtime package-load API exists (packages load via generated import source), and the native default actions must remain the fallback until the dom package registers a replacement per behavior class (decision 2's "fallback until registered").

---

## 3. Design

### 3.1 Element-behavior templates

A **behavior template** is a `view`/`edit` template whose pattern matches a *built-in HTML element* and whose job is state + event policy rather than DOM production. The canonical examples:

```lambda
// lambda/package/dom/form.ls

view <input type:'checkbox'> state checked, indeterminate {}
on click(evt) {
    if (get_state(~, 'disabled')) return
    checked = not checked
    indeterminate = false
    dispatch(~, 'change')
}

view <input type:'radio'> state checked {}
on click(evt) {
    if (get_state(~, 'disabled') or checked) return
    for (peer in radio_group(~)) set_state(peer, 'checked', false)
    checked = true
    dispatch(~, 'change')
}

view <input type:'text'> state value, valid, invalid, placeholder_shown {}
on beforeinput(evt) {
    // F5: the value applier — map the input type to (range, replacement)
    let ed = edit_for(~, evt)          // insertText, deleteWordBackward, insertFromPaste, …
    replace_range(~, ed.start, ed.end, ed.text)
    revalidate(~)
}
on blur(evt)    { revalidate(~) }
on keydown(evt) { if (evt.key == "Enter") submit_form(form_of(~)) }
```

- The **body is empty (`{}`) = identity view**: the element keeps its native rendering (UA chrome per RAD_19). The template contributes only state declarations and handlers. A non-empty body — a template that *renders* the control's chrome as DOM — is an explicit future direction (§6.4), not initial scope.
- **`~` binds the matched DOM element** (S16.5 element scope; the same `~`-binds-the-matched-item rule as app templates). Reads (`~.href`, `~.type`, `~.disabled`) go through the `radiant` Jube module's VMap element projection — the L2 waist from DOM_Pkg §3.1 — so behavior templates work identically on parsed-HTML documents and template-produced documents.
- Handlers are `pn`-semantics blocks per the template design; the body stays pure (S12.1.3). All effects flow through the waist primitives (§3.6).
- **`view` vs `edit`**: interaction state (checked, valid, visited) is *not* document mutation, so form behavior templates are `view` templates. `edit` behavior templates are reserved for behaviors that mutate the document itself (contenteditable — owned by the editor package; `<details>` open-attribute reflection is the first small `edit` case).

**ES1 (decision)** — Behavior templates are the same `view`/`edit` declaration form, registered in a distinct **behavior registry** and attached by *element match at dispatch time* rather than by `apply()`-time production. The reverse-render-map path and the behavior path coexist: reverse-map lookup answers "which template produced this element" (app/author templates); the behavior registry answers "which template governs this element kind" (UA templates).

### 3.2 The attachment inversion

App templates run model → DOM: `apply()` matches a template to a *model item* and records the produced element in the render map. Behavior templates run DOM → policy: they never produce the element; they lazily attach to any element (from HTML parsing, JS mutation, or template output) whose tag/attributes match.

Attachment is **implicit and lazy** — there is no per-element instantiation step, no per-element memory until first state write or first handler invocation:

1. At document setup, Radiant loads the dom package (§3.7); registration populates the behavior registry: `(tag, attr predicates) → BehaviorEntry{template, states, handlers, event mask}`.
2. On dispatch (§3.5), the target's ancestor walk consults the behavior registry (cheap: tag-indexed) for the first element with a matching entry that handles this event type.
3. State reads/writes key by **element identity**, using the same stable-node-id discipline the engine already uses for `ViewState` across relayout (RAD_01's weak-by-id binding) — never raw pointers held on the Lambda side (D4.5.1v3: pin, gen-check, copy-as-value). Behavior-template state entries participate in `state_store_prune_after_reflow` and are dropped when their element leaves the document.

**ES2 (decision)** — Behavior-template state is keyed `(element node id, template_ref, state_name)` and stored/pruned by the engine's existing state stores — *not* a parallel Lambda-side map. Local (non-engine) states reuse the template state map already unified into DocState; engine-backed states resolve to tiers A/B (§3.4).

### 3.3 Matching and specificity

`view <input type:'checkbox'>` requires attribute-**value** predicates in element patterns. Extension to the pattern grammar and `template_matches`:

- `<input>` — tag only.
- `<input type:'checkbox'>` — tag + attribute equals literal. **Landed (F0b).** Compared by *text* across symbol and string, since a parsed-HTML attribute is a string while a Lambda literal is a symbol; both spellings must match. Non-text literals (numbers, booleans) are rejected rather than matched loosely — see ESO23.
- `<a href: any>` — tag + attribute *present* (any value). **Landed (F0b).** Note the syntax differs from this doc's original sketch: the presence form is a **typed** field, not a bare name. A bare `<a href>` does not parse into the element type's shape at all (it registers identically to `<a>`), so presence is spelled `href: any`. Adding bare-name sugar is ESO24.
- `<input type: string>` — tag + attribute type pattern; presently equivalent to presence (the inner type is not yet checked — ESO23).
- Unions still compose: `view <input type:'email'> | <input type:'url'> …`.

Specificity extends the Reactive_UI §8.2 table: literal-value predicate > presence predicate > type predicate > bare tag; more predicates beat fewer; among equals, **later definition wins** (CSS-like, so an app or user package can override a dom-package behavior by re-declaring it — this is the override mechanism, mirroring the UA-stylesheet cascade).

Two matching rules specific to behavior templates:

- **Live re-match**: matching consults the element's *current* attributes at each dispatch, so `input.type = 'checkbox'` set by JS re-routes subsequent events with no invalidation machinery. (State keyed under the old template_ref is retained per the template design's state-retention rule — Reactive_UI §5A.2.)
- **Type-family defaults**: `<input>` with no `type` matches `type:'text'` templates (HTML's default), handled by the registry normalizing the effective type, mirroring `get_input_control_type` in `view.hpp`.

**ES3 (decision)** — Element patterns gain attribute presence/value predicates with CSS-like specificity and last-definition-wins override; behavior matching evaluates against live attributes at dispatch time.

### 3.4 State binding: what "migrating the state store" actually means

The honest core of this proposal, stated once: **transition policy and schema migrate to Lambda; state storage and invalidation stay native.** The CSS resolver reads pseudo-state per selector match per element; paint reads form state per frame; hover restyle walks ancestor chains per mousemove. Those are the DOM_Pkg frequency/coupling tests firing — a cross-language read on those paths is off the table (the full-document re-cascade on pseudo-state change makes this *more* true, not less, until per-element invalidation exists). What Lambda owns is the **write side**: which state changes, to what, in response to which event — per discrete event, exactly the shape test.

A behavior template's `state` clause therefore declares **bindings**, in two classes:

| Class | Declaration | Storage | Write path |
| --- | --- | --- | --- |
| **Engine-backed** | bare name from the known-state table (`checked`, `indeterminate`, `disabled`, `readonly`, `required`, `valid`, `invalid`, `placeholder_shown`, `visited`, `selected`, `dropdown_open`, `selected_index`, `range_value`, `hover`, `active`, `focus`, `focus_within`, `focus_visible`; on text controls also `value`, `selection_start`, `selection_end`, `selection_direction`) | canonical engine tiers A/B/D — same slots CSS and paint already read | assignment sugar routes to the M-level state writer (`set_state`), which performs the existing `state_set_bool` / `form_control_set_*` semantics: canonical write, mirror sync, pseudo-class restyle scheduling, cascade bookkeeping. Text-control `value`/selection writes route to the text primitives instead (§3.6): `value = s` sugars to `replace_range(~, 0, len(value), s)` |
| **Template-local** | `name: initial_value` (existing form) | template state map (already DocState-unified) | existing `tmpl_state_set` sugar |

Rules:

- Engine-backed names use snake_case in Lambda; the engine maps to hyphenated pseudo-class names (`placeholder_shown` ↔ `:placeholder-shown`). The known-state table is owned by the engine and versioned with it.
- **Hot states are read-only bindings**: `hover`, `active`, `focus`, `focus_within`, `focus_visible` are written by native transition code (frequency test; hover restyle is the "~187-failure episode" precedent in DOM_Pkg). Declaring them (`state hover`) makes them readable in the body/handlers; assigning them is a compile error initially. Cold engine states (`checked`, `valid`, `visited`, …) are read-write.
- A behavior handler writing engine state executes inside the event cascade (§3.5), so the existing settle/validation checkpoint (`radiant_state_settle`) sees Lambda-made mutations exactly as it sees native ones — the schema checker keeps auditing both during the migration.
- The declarative schema itself (`RADIANT_STATE_RULES` / `RADIANT_INVARIANTS`) migrates **later, not first**: once a family's transitions live in the dom package, its rule rows and invariant bindings can be re-expressed as Lambda data next to the handlers and fed to the (debug-only) checker, letting `state_schema.cpp` shrink family by family (§6.3).

**ES4 (decision)** — `state` in behavior templates declares bindings: bare known names bind engine-backed canonical state (hot ones read-only), `name: value` declares template-local state. Storage, mirrors, restyle invalidation, and prune stay native; Lambda owns transitions.

**One state store, one template system (unification — user clarification, 2026-08-25).** App templates already store their `state` in the StateStore: the `(model_item, template_ref, state_name) → Item` map is per-EvalContext (`lambda/runtime/template_state.cpp`) and bound into `DocState::template_state_map` at store creation (`state_store.cpp`); template bodies compile to `tmpl_state_get_or_init` in the prologue and handler writes to `tmpl_state_set` (which also marks the render map dirty) — in both the MIR transpiler and the AST interpreter. Behavior templates reuse this exact store and sugar for template-local state; the only structural difference is what `~` is — an app template's matched *model item* versus a behavior template's matched *DOM element* — and on a parsed HTML document the DOM **is** the model, so the two collapse. Key identity follows the kind: model-item identity (raw Item bits, as today) for app templates, stable node id (ES2) for behavior templates; the anonymous-`template_ref` stable-name issue (RS open ledger) applies equally to both. The payoff of sameness: the two template kinds behave identically on the same HTML output — on a `.ls` page, app templates *produce* the elements and dom-package behavior templates *attach* to those produced form controls exactly as they attach to parsed HTML, so the app script stops hand-rolling UA behavior (todo2's per-keystroke splicing) and simply consumes `input`/`change` notifications and reads values, like any web app over a browser.

**ES11 (decision)** — One template system, one state store: behavior and app templates share the template-state store (already DocState-bound), the `state`-clause semantics, the assignment sugar, and the dispatch machinery; they differ only in attachment (produced-via-`apply()` vs matched-at-dispatch) and in `~`'s identity rule. The dom package attaches to template-produced elements the same as to parsed HTML, so fully Lambda-scripted views get UA behavior from the same package. (App templates binding engine-backed states of their produced root element is a natural follow-on, deferred.)

### 3.5 Event routing: behavior handlers are default actions

Dispatch order for a discrete event on a target element, conforming to DOM_Pkg decision 2:

```
[N] hit test → target path → trusted event construction
[N] 3-phase dispatch: JS listeners (capture/target/bubble)
[N] app-template handler dispatch (reverse render-map path — author-level, existing)
      │  either layer may cancel: defaultPrevented / evt.prevent_default()
      ▼
[N] defaultPrevented? → done
[L] BEHAVIOR DISPATCH — at most one Lambda call per discrete event:
      walk target→root; first element whose behavior entry handles this
      event type wins (most specific template for that element)
[M] handler effects via primitives: set_state · dispatch · submit_form ·
    request_navigation · focus_move · …
[N] cascade settle → restyle/reflow/repaint scheduling
```

- **One-call ceiling** and **hot-path guard** adopted verbatim: the behavior registry maintains a native per-event-type bitmask; `mousemove`/`pointermove`/`scroll`/`wheel` never enter behavior dispatch unless a loaded template declares such a handler (none in the form scope).
- **Fallback until registered**: each native default-action block gains a guard — `if (behavior_registry_claims(event_class, elem)) skip native;`. Until the dom package loads (or if it fails to), native behavior is untouched. Per-class claims keep every migration step baseline-testable and bisectable (`RADIANT_DOM_PKG=0` disables the package wholesale).
- **`evt.prevent_default()`** is added to the Lambda event map for app-template handlers (closing the ordering gap between author templates and behavior templates), and behavior handlers get **`pass()`** — decline the event so the native fallback for that class still runs (mirrors `EDITING_ACTION_PASS`). Return-less completion means "claimed".
- **Reentrancy**: behavior handlers run post-dispatch in a quiescent dispatch state; anything they trigger (`dispatch(~, 'change')`, focus moves, submission) enters the pipeline as a fresh event. The handler runs inside the already-open `state_begin_event_cascade` scope so mutations batch and settle once. Generation checks (DocState `version`) guard against the handler acting on a re-targeted element, following `editing_template_handler.cpp`.
- Text-control editing keeps the WHATWG event pair with the dispatcher — native fires the cancelable `beforeinput`, native fires the non-cancelable `input` afterward — but from F5 the **behavior template is the applier** in between: its `on beforeinput(evt)` handler maps the input type to a `(range, replacement)` and commits it through `replace_range` (§3.6). Until F5 flips, the native `te_*` applier remains the per-class fallback; after it, `te_replace_byte_range*` survives only as the splice primitive under the waist.

**ES5 (decision)** — Behavior handlers implement UA default actions: post-listener, defaultPrevented-suppressed, one call per discrete event, per-class native fallback until registered, effects through waist primitives only, executed inside the event cascade.

### 3.6 Waist additions (M-level primitives)

Behavior templates call these through the `radiant` Jube module (extending its existing `attr`/`velmt_*` surface); names align with DOM_Pkg §4.3 cluster #9:

| Primitive | Semantics |
| --- | --- |
| `get_state(elem, name)` / `set_state(elem, name, value)` | canonical engine state read/write with mirror-sync + restyle scheduling (the `state_set_bool` path); `set_state` on hot names is rejected |
| `dispatch(elem, name)` / `dispatch(elem, name, data)` | fire a synthetic event into the normal pipeline (`change`, `invalid`, `submit`) as a fresh event |
| `radio_group(elem)` | peers sharing `name` within the owning form/document (wraps `form_control_uncheck_radio_group_peer`'s walk, read side) |
| `form_of(elem)` / `form_elements(form)` | form-owner resolution and member enumeration (HTML form-association rules) |
| `submit_form(form)` | run the submit pipeline: dispatch cancelable `submit` → behavior template's submission policy (F4) |
| `request_navigation(url)` | hand off to the native navigation/lifecycle layer (already ruled N) |
| `focus_move(elem)` | native focus transition (`focus_transition` writer) |
| `value_of(elem)` / selection reads | text-control value and selection as **codepoint** offsets; all UTF-8↔UTF-16↔codepoint conversion stays native (`tc_*`) |
| `replace_range(elem, start, end, text)` | **the single text-control value writer** (F5): native converts codepoint→byte offsets, splices the UTF-8 buffer in one memmove, places the caret at end-of-insertion (optional explicit caret arg), syncs mirrors, refreshes placeholder state, schedules repaint. Fires **no** events — the dispatcher owns `beforeinput`/`input`; pushes **no** history — history is Lambda's (F6) |
| `set_selection(elem, start, end, dir)` | text-control selection writer (wraps `tc_set_selection_range` semantics, codepoint offsets) |
| `offset_at_point(elem, x, y)` / `visual_line_range(elem, offset)` | layout-coupled geometry queries backing click-to-position and Up/Down visual-line caret policy — native, since wrap positions live in layout |

The exclusivity story stays clean under S9: behavior handlers never share Lambda values across elements — cross-element effects (radio peers) are `pn` calls through the waist that mutate engine-owned storage, so no `var`-aliasing arises (S9.1.3 concerns don't apply to waist calls; S9.2.4's view-state restriction is respected because engine state is never passed as a `var` argument).

**ES6 (decision)** — All behavior-handler effects go through named waist primitives; no direct DOM/state pokes from Lambda. New primitives are added to the `radiant` Jube module, not as ad-hoc sys-funcs.

### 3.7 Package layout and loading

```
lambda/package/dom/
    dom.ls        — entry: imports + registers the behavior modules
    form.ls       — F1–F5: checkbox, radio, text, select, submission, validation
    link.ls       — post-forms: <a> activation, visited
    detail.ls     — post-forms: <details>/<summary>, label forwarding
    focus.ls      — post-forms: tab-order policy (DOM_Pkg "sequential focus navigation → L")
```

- Import path: `import dom: lambda.package.dom.dom` (resolves via `LAMBDA_HOME`, same as the four packages Radiant already loads). Loading follows the existing precedent — generated import source through `run_script_mir` — until a first-class package-load API exists (ESO10).
- Load point: document setup, after the DOM exists and before first interaction; per D7.2.2 package init is a transaction barrier — a failed dom-package init rolls back cleanly and native fallbacks remain (no partial registration). Module-scope bindings are immutable (D7.2.1); all mutable state is document-anchored per ES2, which is exactly the D7.2.1-compatible home.
- Compile cost is amortized by the MIR module cache (D8.5.1); cross-document sharing of the compiled package is DOM_Pkg Q3, shared here (ESO9). The package is fixed, engine-owned source identical for every document, so **compile-once-cache-and-reuse** is the expected steady state — per-document work should reduce to instantiating registry entries against the document's context. How aggressively this is tuned follows measurement; *whether* the package loads does not (ES14).
- Headless parity: `lambda.exe layout page.html` and the event-sim harness load the same package so `:checked`-dependent layout/render tests behave identically windowed and headless; `RADIANT_DOM_PKG=0` forces native fallback in both.

**ES7 (decision)** — The dom package lives at `lambda/package/dom/` with per-area modules; document-setup loading with transactional init and whole-package disable switch; identical loading in windowed, headless, and event-sim runs.

### 3.8 Error containment

A raising behavior handler must never corrupt interaction state. The dispatch boundary adopts the S7.10 sys-func posture: the engine invokes the handler, and any surfaced error is caught at the boundary, logged (`log_error` with the template ref and event), the event cascade settles normally, and **that event falls back to the native default action for its class** (fail-open to native, matching fallback-until-registered). Repeated failure of one behavior class (threshold N per document) latches that class back to native for the session — the same sticky-fallback shape as the JS batch-cleanup flag. Handlers discharge expected errors themselves with `^`-handlers per S7.6.

**ES8 (decision)** — Handler errors are contained at the dispatch boundary: log, settle, native fallback for that event; sticky per-class fallback on repeated failure.

### 3.9 Grammar extensions

Deltas to `grammar.js` (regenerated per the standard flow) and the hand parser:

1. `state_entry: name (':' value)?` — bare names legal; semantic analysis requires bare names to resolve against the known engine-state table when the pattern is a built-in element (unknown bare name = compile error naming the table).
2. Element patterns: attribute predicates `attr`, `attr: literal`, alongside existing `attr: type`.
3. `pass()` recognized in handler bodies (library function, no grammar change); `evt.prevent_default()` likewise.

No new keywords. `apply`, `view`, `edit`, `state`, `on` are unchanged.

### 3.10 Coexistence with page JS

Restating the boundary as explicit design intent (user clarification, 2026-08-25):

- **One canonical state, two script peers.** View/form state *storage* stays in C++ (§3.4) precisely so Radiant views keep linking to it directly during rendering — `ViewState` stays arena-allocated with the `DomNode::view_state_ref` fast path, and the style resolver/paint read it as today — and so page JS keeps its existing IDL surface unchanged: `input.checked = …`, `.value = …`, `setSelectionRange`, `setCustomValidity`, `form.submit()` continue to route through the same native writers they use now (`js_dom.cpp` already drives the state API at ~200 call sites). JS writes are never proxied through Lambda, and Lambda writes are never proxied through JS; both converge on the canonical native stores, which is what keeps them coherent.
- **Lambda takes over only the UA role.** What migrates is the engine's *own* default behavior — default actions and the state transitions they imply, constraint validation, submission, and the text-edit applier (all UA behavior in the broad sense). Author-level behavior — JS listeners, inline `on*` handlers, app templates — is untouched.
- **Parallel, not exclusive.** Every interactive document runs both realms side by side: the JS realm for page scripts, the Lambda realm for UA behavior (plus app templates on `.ls` pages). "Parallel" means coexisting on the same document — execution stays single-threaded and ordered by the dispatch pipeline (§3.5): JS listeners first, the behavior template as the default action after, `defaultPrevented` as the author's veto — exactly the relationship author script has to UA behavior in a browser.

**ES10 (decision)** — Page JS and the dom package are peers over the same canonical native state: JS state manipulation continues unchanged through its IDL surface; Lambda owns only UA default behavior; both realms are active in parallel on every interactive document, sequenced by the dispatch pipeline, neither gatekeeping the other. (ESO13/ESO20 are the mechanical follow-through: the L4 adapter and IDL setters must keep hitting the same waist writers.)

### 3.11 One script runtime, explicit page kind

ES10 has a mechanical precondition the engine does not satisfy today, found while auditing implementation blockers (user direction, 2026-08-25).

**One shared runtime — no second runtime.** Everything in the Lambda template layer is scoped to the thread-bound `EvalContext`: the template registry is `context->template_registry` (`template_registry.cpp`) and the template state store is `context->template_state_store` (`template_state.cpp`). The engine already enforces one evaluator per document — `free_document` refuses teardown when the Lambda and JS runtimes carry different `EvalContext`s ("One document cannot multiplex independent evaluators on one host thread", `ui_context.cpp`), and `eval_context_init` refuses a live-thread context switch (`runtime-state.cpp`). That constraint is right and is kept. It is also already satisfiable: `DomDocument::js.runtime` **is** a Lambda `Runtime*` — `lambda-rt` is the shared Lambda + LambdaJS runtime layer — so an HTML/JS page already owns a Lambda runtime. The dom package therefore loads onto **the runtime the document already has**; it never creates one. Per-document scoping of the registry and state store then falls out for free, since one document has one context.

**Page kind becomes explicit.** Two gates currently infer a document's nature from `lambda_runtime != null`: `event_document_has_js_runtime` (`event.cpp`, `!document->lambda_runtime && document->js.mir_ctx && document->js.runtime`) and the same `!doc->lambda_runtime &&` test in the frame path. That proxy exists for a real reason — a Lambda-script page retains a Jube support capsule in `js`, so `js.mir_ctx`/`js.runtime` alone cannot distinguish "has a DOM script realm" from "has a support capsule" — but it makes "this document has Lambda code" and "this document is not a JS page" the same bit. The moment an HTML page loads the dom package, both gates would misfire and page JS would stop receiving its event realm. **A JS/HTML page can now carry Lambda code**, so the inference must be replaced by explicit markers, separating three things the single bit conflates:

| Concern | Marker | Set by |
| --- | --- | --- |
| **Provenance** — what the author wrote | `DomDocument::page_kind` ∈ {`DOM_PAGE_KIND_UNKNOWN`, `_HTML`, `_LAMBDA_SCRIPT`, `_GENERATED`} | the loader, which already knows (`.ls` → `load_lambda_script_doc`; markdown/XML/LaTeX and the math/pdf/graph bridge docs are `_GENERATED`). Before F0a this fact was computed and discarded |
| **Capability** — is there a real DOM script realm? | `DomDocument::js_has_dom_realm` | `script_runner.cpp`, when it actually establishes the realm (preamble installed) — never set by a bare Jube support capsule; cleared on JS-state teardown |
| **Which evaluator** | `dom_document_script_runtime(doc)` — the document's one shared runtime | replaces every `lambda_runtime ? … : js.runtime` ternary |

Gate rewrite: `event_document_has_js_runtime` becomes `dom_document_has_js_realm(doc)`, dropping the `!lambda_runtime` term entirely; the frame-path gate follows. This is **behavior-neutral on landing**, and F0a verified why rather than assuming it: the HTML loader passes `nullptr` as the runtime to `populate_layout_document`, so **HTML pages never set `lambda_runtime`**, while `execute_document_scripts` (the only writer of `js.mir_ctx`) runs solely on the HTML path — making the old `!lambda_runtime` term exactly redundant with `js.mir_ctx == null` for every real document. That is what let it ship ahead of any behavior template. Once in, F1 can give an HTML page Lambda code without disturbing its JS realm.

*Implementation notes (F0a, landed):* both fields sit at the **tail** of `DomDocument` — the struct carries an explicit "keep new state at the tail; native/JIT consumers depend on the offsets above" constraint, so `js_has_dom_realm` lives on the document rather than inside the `DomJsRuntime` capsule, whose growth would shift every member after it. `page_kind` is threaded through `create_layout_dom`/`create_layout_css_document` beside the existing `document_kind` log string, and a `log_debug("[page-kind] …")` line makes the otherwise write-only field observable until F1 gives it readers.

**ES12 (decision)** — One script runtime per document, shared by JS and Lambda. The dom package loads onto the document's existing runtime/`EvalContext`; no second runtime is created; the one-evaluator-per-document invariant is preserved, not relaxed. `dom_document_script_runtime(doc)` replaces the `lambda_runtime ? … : js.runtime` ternaries.

**ES13 (decision)** — Page kind and JS-realm presence become explicit fields (`page_kind`, `js_has_dom_realm`); no routing gate may infer document nature from the presence of a Lambda runtime. Landed in F0 as a behavior-neutral refactor.


### 3.12 Working with EvalContext (ESO27)

There is one `EvalContext` slot per thread: the `context` thread-local. Code that runs Lambda or JS must have the right `EvalContext` in that slot. These are the rules for putting one there.

**The calls involved.**

| Call | What it does |
| --- | --- |
| `eval_context_init(ctx)` | puts `ctx` in the slot. Succeeds if the slot is empty or already holds `ctx`. Fails if it holds a different one. |
| `eval_context_shutdown(ctx)` | empties the slot. Only the `ctx` currently in it may do this. |
| `eval_context_thread_matches(ctx)` | is `ctx` the one in the slot right now. |
| `runtime_get_eval_context(rt)` | the `EvalContext` belonging to a `Runtime`. |
| `js_runtime_state_init(ctx)` | allocates `ctx->js_state` if absent, and points `js_active_runtime_state` at it. Requires `ctx` to be in the slot. |
| `js_runtime_state_shutdown(ctx)` | clears `js_active_runtime_state`. |

`ctx->js_state` is a field on the `EvalContext`. `js_active_runtime_state` is a thread-local pointer to that same field, kept so JS code does not have to reach through `context` every time.

**EO1 — Withdrawn.** The first draft of this ruling said every `eval_context_init` should be paired with an `eval_context_shutdown` that restores whatever was bound before, via an RAII scope. That contradicts the invariant already stated in `runtime-state.h`: *"An evaluator thread acquires one context identity before execution and keeps it until teardown. Nested callbacks and guest dispatch may validate the owner, but must never replace and later restore the TLS pointer."*

Binding is not just the `context` pointer — `eval_context_init` also binds the side stack the precise GC walks for roots, so swapping identities mid-flight and restoring later is a rooting question, not a scoping convenience. The sticky binding in `EventDocumentScope` (a defaulted destructor) is therefore the rule being followed, not a bug.

Its intent is absorbed into EO4: under one-identity-per-thread, the thing that matters is that the *right* context is bound once, early. Reviving replace-and-restore would need the GC-rooting question settled first, and that is not this design's call.

**EO2 — JS state follows the EvalContext.** If you called `js_runtime_state_init(ctx)`, call `js_runtime_state_shutdown(ctx)` before `eval_context_shutdown(ctx)`. Never leave `js_active_runtime_state` set after `context` stops holding that same `ctx` — it would point at the `js_state` of an `EvalContext` that is no longer in the slot, and the next JS call reads freed or wrong memory. `js_runtime_state_init` already refuses a mismatch on the way in; the missing half is on the way out.

**EO3 — One EvalContext per document.** Reach it with `runtime_get_eval_context(doc->lambda_runtime)`. Do not create a second `Runtime` for a document that already has one. Putting it in the slot is temporary (EO1); owning it is not.

**EO4 — Create and bind at setup, not at dispatch.** `cmd_layout.cpp`'s load functions are where a document's `Runtime` is created *and* where its `EvalContext` is first bound with `eval_context_init`. Event-time code may call `eval_context_init` on the context that is already bound — it succeeds when `context == owner` and refuses a switch, so there it means "confirm I own this thread", the same as `eval_context_matches`. What event-time code must never do is call `runtime_init`, or be the first binder of an identity nothing has bound yet.

Two separate things, and only the first moves. **Creating the `EvalContext`** becomes eager: at load, for interactive documents that do not already have one (a `.ls` page and a script-bearing page get theirs from the existing paths; a script-less page is the only new case). A one-shot `layout`/`render` run dispatches no events and needs none. **Loading the dom package** stays lazy on the first discrete event, unchanged from F0b — that is the part that costs real work (parse, transpile and MIR-compile `dom.ls` + `form.ls`), and it stays off pages nobody interacts with. `runtime_init` is a `calloc` plus field setup and should be far cheaper, but that is a claim from reading the code, not a measurement; time it with the ESO9 load-cost work before enabling by default.

The reason is concrete: at event time you cannot tell whether the page will start JS later. An HTML page can have an empty slot, no `Runtime`, and `js_active_runtime_state == NULL` at the first click, and still run a script afterwards. If you created and bound an `EvalContext` at that click, the JS that starts later finds the slot held by yours and its own `js_state` unreachable. Five different checks at click time (`context`, `js_has_dom_realm`, `js_active_runtime_state`, owned-runtime-only, hot-path) each let a case through, because none of them can see the future. Document setup can: it knows whether the page has scripts.

**EO5v2 — An iframe document manages its own `Runtime` and `EvalContext`.** (Supersedes EO5, which said subdocuments must borrow — user decision 2026-08-25.) A subdocument is an ordinary document for runtime purposes: it creates its `Runtime` and `EvalContext` when it needs them (EO4: at its own setup), owns them, and releases them when the document is freed — the same `owns_script_runtime` teardown a top-level document uses. There is no borrowing and no parent/child special case.

The one thing it may not do is bind while another document's evaluation is in progress, because a thread holds one `EvalContext` at a time.

The switch is `eval_context_shutdown(current)` followed by `eval_context_init(target)`, performed at a **quiescent point** — event-dispatch entry, before any handler runs — and never inside a handler, a nested dispatch, or guest code. JS state follows the switch per EO2.

Why this is plausible rather than a violation of the one-identity rule: the side-stack regions the precise GC walks are **per-`Context`**, not per-thread (`lambda_side_stack_bind_for(Context*)`), and `side_stack.h` already describes the `_for` family as surfaces for *an inactive context* — so a context that is not currently bound retaining its own roots is an existing concept, not a new one. `interp_run_with_worker_stack` already performs `eval_context_shutdown` and hands the runner to another thread, so a clean handoff at a boundary is likewise already practiced. What `runtime-state.h` forbids is the *nested* form: "nested callbacks and guest dispatch ... must never replace and later restore the TLS pointer." A boundary switch with nothing live is a different operation.

**This is a reinterpretation and needs confirming by whoever owns the GC/rooting model.** A literal reading of "keeps it until teardown" forbids any switch. The claim here is narrower — that switching is safe when no frames are live, because each context's roots travel with it — and it should be checked, ideally with an assertion that the outgoing context has no live root frames at the switch point.

*Consequence for EO6:* if contexts can switch at boundaries, `execute_document_scripts` no longer has to reuse the parent's `EvalContext` — it can keep creating its own `Runtime` for the iframe, and the binding simply swaps at the next boundary. That would dissolve the double-free tension that currently blocks EO6, and is the cheaper route to the same outcome.

**EO6 — Later JS attaches, never competes.** When JS starts on a document that already has an `EvalContext`, call `js_runtime_state_init` on that same `ctx`; it allocates `ctx->js_state` if it is not there yet. Do not create a second `Runtime` for the JS side. This is what lets one document run both page JS and the dom package: they share the `EvalContext` and `js_state` sits on it.

**Status of the work.**

- **EO2 — already satisfied, no change needed.** Every site that ends a binding (`script_runner.cpp` ×2, `runner.cpp`, `transpile-mir.cpp`) already calls `js_runtime_state_destroy_context()` or `js_runtime_state_shutdown()` before `eval_context_shutdown()`, and the destroy path clears `js_active_runtime_state`. Verified by inspection of all six shutdown sites.
- **EO4 — implemented.** `radiant_document_ensure_evaluator()` (`event.cpp`) creates and binds a document's evaluator, called from the interactive load path in `window.cpp` — which is what separates an interactive session from a one-shot `layout`/`render` run. Dispatch-time creation is gone: `radiant_dom_package_ensure` now returns early when the document owns no evaluator. A script-less HTML page gets Lambda checkbox behavior with `RADIANT_DOM_PKG_CREATE_RUNTIME=1` (4/4).
- **EO6 — not implemented; it is what still gates the flag.** `test/html/index.html` (no scripts of its own, one iframe) still crashes with creation enabled. The parent document takes the evaluator at setup, then the iframe's content starts JS, and `execute_document_scripts` cannot bind: it creates its *own* `Runtime` and calls `eval_context_init`, which refuses because the parent's context is held. Its comment states the constraint plainly — *"The document owns this eval thread from initialization until document teardown; an ambient live context cannot be saved and restored here."*
  EO6 means that block must **reuse** the document's existing `EvalContext` instead of creating a second `Runtime`. That is not a local edit: `dom_doc->js.runtime` is retained for timers and freed by `script_runner_cleanup_js_state`, so sharing one `Runtime` with the Lambda side collides with the `owns_script_runtime` teardown added for ESO25 (double free). Land it on its own, with the JS suites as the gate, and settle single-owner teardown as part of it.
- **EO3/EO5** are stated rules; EO5 is not yet enforced in code (nothing stops a subdocument path from creating an evaluator — today none does).

Until EO6 lands, `RADIANT_DOM_PKG_CREATE_RUNTIME` stays off by default and script-less HTML pages keep native default actions.

---

## 4. Initial scope: HTML form states

Phased so each step flips one native default-action class behind its registry claim, gated by `make test-radiant-baseline` + the UI-automation suite (311 event-JSON tests) at 100% of current pass rate.

| Phase | Deliverable | Engine work | Native code retired (behind claim) |
| --- | --- | --- | --- |
| **F0a — runtime & page-kind refactor** ✅ **landed 2026-08-25** | explicit `page_kind` + `js_has_dom_realm` fields; both realm gates rewritten off `lambda_runtime`; `dom_document_script_runtime()` replacing all four runtime ternaries (§3.11, ES12/ES13) | engine-only | none — behavior-neutral by construction. Verified: Lambda baseline 2104/2104 + 3876/3876; radiant baseline failure set byte-identical to the pre-change tree (same 32 pre-existing UI-automation failures by name, same single pre-existing render regression) |
| **F0b — infrastructure** ✅ **landed 2026-08-25** | ✅ *landed*: attribute-predicate matching (value + presence, symbol/string-agnostic, literal-beats-presence specificity) and bare-state grammar, with `test/lambda/view_attr_pattern.ls` covering both; the **behavior registry** (`is_behavior` stamped by a registry-level behavior mode, excluded from `apply()` matching, matched by `template_registry_match_behavior` filtered to entries that declare the event) and the **dispatch step** (`dispatch_behavior_handler`, a second ancestor walk that runs only after no author template claimed the event — ES5 ordering — sharing one extracted `invoke_template_handler` with the author walk so the runtime-binding/reconcile path is not duplicated). Inert until a package registers; plus the **state primitives** `get_state`/`set_state` on the `radiant` Jube module, routing through `state_set_bool`/`state_get_bool` so canonical storage, mirror sync and restyle scheduling stay native — hot names (hover/active/focus/focus_within/focus_visible) are read-only per ES4, unknown names are rejected, and `set_state` **reads back** and reports whether the write actually took (a form-state write is a no-op until layout has built the control's `FormControlProp`, and a silent success would hide that). Covered by `test/lambda/radiant_state_prims.ls`; the **dom package** (`lambda/package/dom/{dom,form}.ls` — checkbox and radio activation), its **loader** (once per document, on the first event, so static layout/render runs never pay for it; `RADIANT_DOM_PKG=0` disables it), and the **fallback-until-registered guard** (`radiant_behavior_claims_event`, which the native checkbox/radio activation path now consults so exactly one of native or Lambda acts). End-to-end proof: `test/ui/dom_pkg_checkbox.json` drives real clicks on a `.ls` page and the log shows the native toggle standing down while the Lambda template drives the state, and the package's `dispatch(~, 'change')` reaches the page's own author-template `on change` handler — ES5, ES6 and ES11 in one test (6/6 assertions). Plus the **`dispatch()` primitive** (`radiant_dispatch_event_from_script`, routed through the ambient handler context so a synthetic event re-enters the pipeline as a fresh event). plus the **handler verdict protocol** (user decision 2026-08-25, ES15): a handler returns `'pass'` to decline (dispatch keeps looking) or `'prevent-default'` to suppress the remaining default actions, instead of a callable on the event object — it fits the existing `Item handler(Item, Item)` ABI. Proven differentially by `test/ui/dom_pkg_prevent_default.json`: with the author template preventing default the UA behavior never runs, so no `change` fires and the author's counter stays 0. plus the **state-dump parity harness** (`test/state_parity.sh`): it runs one scenario twice over the same page — `RADIANT_DOM_PKG=0` for the native default actions, `=1` for the package's behavior templates — and diffs the per-cascade Mark dumps. The checkbox migration is **state-equivalent**: 136 identical lines. The harness was itself validated by injecting a defect (dropping the `checked` write), which it caught with a precise diff — `'checked'` present in the native flags and absent in the package's. **F0b complete** | all of §3 | none — UI-automation failing set byte-identical to the pre-change tree throughout; the package only claims events it declares, so every untouched behavior stays native |
| **F1 — checkbox & radio** ✅ **landed 2026-08-25** | `lambda/package/dom/form.ls` owns checkbox activation (toggle + clear indeterminate) and radio activation (one-way selection **plus the group-exclusivity walk**), each gated on `disabled` and each firing `input`/`change`. A handler that declines returns `'pass'` (ES15) so the native default action stays in charge | `radio_group()` (peers sharing `name`, scoped to the owning `<form>` else the document) and `form_of()` added to the `radiant` module; the tree walk stays native, the policy loop is Lambda | native activation stands down whenever the package claims the click — verified by the absence of `uncheck_radio_group` in the trace while `.r1` still clears. Covered by `test/ui/dom_pkg_radio.json` (7/7) and `dom_pkg_checkbox.json` (6/6); both **state-equivalent to native** under `test/state_parity.sh` |
| **F2 — select (open/close half)** ✅ **landed 2026-08-25** | `form.ls` owns opening and closing the dropdown: a click on an enabled `<select>` toggles it, `'pass'` when disabled. The overlay itself — placement, sizing, painting, which option sits under the cursor, outside-click capture — stays native, and `select_open_dropdown` is shared so the geometry is computed once for both paths | `dropdown_open`, `set_dropdown_open`, `option_count`, `selected_index`, `set_selected_index`; claim guard on `handle_select_click` | native opening stands down when claimed. **Unblocked by fixing ESO28** — a pre-existing engine bug this exposed. Option *commit* stays native: the chosen index comes from overlay geometry a template cannot see, so moving it needs an event field carrying the index (ESO4). Covered by `test/ui/dom_pkg_select.json`, state-equivalent to native under the parity harness |
| **F3 — constraint validation** | full spec validation in Lambda: required/`minlength`/`maxlength`/`min`/`max`/`step`/`pattern` (Lambda regex)/email/url; writes `valid`/`invalid`/`placeholder_shown`; `checkValidity`-shaped primitive for JS layer | `input`-notification routing to behavior handlers on text controls | `te_validate` + `te_value_is_*` in `text_edit.cpp` (native seam keeps calling a validation hook that now lands in Lambda); closes RAD_19 known issues 2 & 6 |
| **F4 — submission** | greenfield: submit-on-Enter, submit-button activation, cancelable `submit` event, form-data-set construction, urlencoded/multipart serialization, `request_navigation` handoff; `invalid` event + focus-first-invalid policy | `submit_form`, serializer-support reads | none — this capability does not exist natively; it is born in Lambda (DOM_Pkg already rules both rows L) |
| **F5 — text-control value editing (the applier flip)** | `on beforeinput` in `form.ls` becomes the value applier: maps input types (`insertText`, `deleteContentBackward/Forward`, `deleteWordBackward/Forward`, `insertLineBreak`, `insertFromPaste`, `insertFromComposition`) to `(range, replacement)` using word/line scanners written as pure Lambda string functions; single-line newline policy and `maxlength` in Lambda; caret placement; `on keydown` caret-movement policy (Home/End/word-jump; Up/Down via geometry queries). The todo2.ls app path already proves per-keystroke Lambda editing works | `replace_range` / `set_selection` / geometry primitives; **collapse the `FormControlProp` ↔ `ViewState` ↔ `DocState::sel` value/selection mirrors to one canonical + one projection as part of the flip** (survey hazard §2.1, ESO22) | the applier + scanners in `text_edit.cpp`: `te_replace_byte_range*`, `te_word_*`/`te_line_*`/`te_prev/next_word_byte`, `te_select_*` |
| **F6 — clipboard, history, IME commit, change-on-blur** | paste/cut policy with sanitization in Lambda (`te_prepare_paste_replacement` logic); undo/redo ring as template-local state (bounded `{value, selection}` snapshots) with Cmd+Z/Y key policy; IME **commit content** applied through the same `beforeinput` applier (preedit session stays native, ESO21); change-on-blur decision (`value_at_focus` comparison); disabled/readonly event-gating unified | `value_at_focus` snapshot read; clipboard read/write primitives (exist) | `EditHistory` + `te_history_*`, `te_paste`, `te_ime_commit`'s splice half, `te_blur_should_dispatch_change` decision (snapshot mechanics stay native); scattered `disabled` checks in `event.cpp` |

Explicitly **in scope** for forms: state transitions, default actions, validation, submission, and text-control value editing — the edit decisions, history, clipboard policy, and IME-commit content (ES9). Explicitly **out of scope** even within forms (per §5): the native buffer/splice/mirror mechanism, caret & selection *geometry*, IME preedit sessions, selectionchange coalescing, paint.

---

## 5. What does not migrate (and why) — placement per family

| Family | Placement | Test that fires |
| --- | --- | --- |
| Text-control **edit policy** — input-type→edit mapping, word/line boundaries, paste sanitization, `maxlength`, undo/redo, change-on-blur, IME *commit content* | **→ L (F5/F6)** — user decision 2026-08-25 (ES9), superseding RAD_19 §1's stay-native rationale and DOM_Pkg's "text-insertion mechanics stay N" | shape test: per discrete input event, a spec-defined state machine over one stable splice primitive |
| Text-control **mechanism** — UTF-8 value buffer + one-memmove splice (`replace_range`), UTF-8↔UTF-16↔codepoint conversions, caret/selection geometry & paint, IME preedit session, selectionchange coalescing | **N** | coupling (buffer ↔ IDL ↔ caret paint; per-frame paint reads of `current_value`); the Stage-4B editor still owns the contenteditable side of the shared `beforeinput` seam |
| Hover/active transitions + hover restyle | **N now**; reassess only after per-element style invalidation exists | frequency (per-mousemove) — the hot-path guard exists precisely for this |
| Focus *mechanics* (focusability computation, focus events) | **N**; tab-order *policy* → L post-forms | coupling for the query; policy is per-keypress cold |
| Selection & caret (canonical selection, presentation geometry, blink) | **N** (editor package territory, RAD_18) | coupling + frequency |
| Scroll state | **N permanently** | frequency (~25 read sites across layout/render, per frame) |
| Dropdown/context-menu overlay geometry & paint | **N**; open/commit policy → L (F2) | coupling |
| Dirty/reflow tracking, prune-after-reflow, state storage tiers A–D, mirror sync | **N** — this *is* the mechanism | frequency + coupling |
| Schema rule/invariant *data* | → L per migrated family (§6.3) | shape (it's declarative data; debug-only) |
| Video placement, animation scheduler, display-list cache fields in DocState | untouched (not interaction state; separate cleanup — RAD_17 known-issue 1) | — |

Consequence for expectations: with F5/F6 in scope, the near-term C++ *deletion* covers most of `text_edit.cpp` (~900 LOC: applier, scanners, history, paste, change-on-blur, validate) on top of the activation blocks and scattered policy — order 1.5–2K LOC across F1–F6 — while most of the remaining 11K is mechanism that stays. The payoff curve is: new capability lands in Lambda instead of C++ (F3's full validation, all of F4), duplication stops (todo2-style app editing and UA text editing become one Lambda implementation), and the schema/state_schema retirement (§6.3) plus later families bend the curve further. This still matches DOM_Pkg's honesty note that existing-surface migration "nets out around half".

---

## 6. Follow-on scope (post-forms)

### 6.1 Links: `view <a href> state visited, hover {}`

The motivating example needs two things forms don't: a **history source** for `visited` (browsing_session-backed `visited(url)` primitive; RS durability: visited is *durable* class per RS1, persisted by the session store, not regeneration) and the **privacy stance** — browsers restrict `:visited` styling to color-only to block history sniffing; Radiant must decide how much of that to adopt before exposing visited-state reads to arbitrary script (ESO12). Link activation itself (`request_navigation` on unmodified click, target/rel policy) is straightforward F-phase-shaped work.

### 6.2 Focus & key policy

Tab-order (sequential focus navigation, `autofocus` processing) and key→command mapping are already ruled L in DOM_Pkg; they slot in as `focus.ls` once forms are stable, using `focus_move` + the native focusability query.

### 6.3 Schema and invariants as package data

Per migrated family, re-express its `RADIANT_STATE_RULES` rows and invariant bindings as Lambda data exported by the dom package; the debug-build checker consumes them through a read primitive. End state: `state_schema.cpp` retains only the derive functions and the guard plumbing; the FSM *specification* lives beside the transitions it constrains. Zero release-runtime cost either way (the checker compiles out), so this is purely a source-of-truth consolidation.

### 6.4 Template-rendered control chrome

A behavior template with a non-empty body rendering the control's UA chrome as real DOM (e.g. `<select>` dropdown as template-produced elements instead of `render_select_dropdown`'s hand-painted overlay) would unify form painting with normal layout/paint. Attractive, large, explicitly deferred — it needs the E0 promotion of form values to DOM text (RAD_19 known-issue 1) and a shadow-content story first.

---

## 7. Gaps and open issues (ESO ledger)

| # | Issue | Notes / proposed direction |
| --- | --- | --- |
| ESO1 | **Element identity for state keys on JS-mutated DOM** — behavior state keys by node id; JS can detach/re-insert or clone nodes. Define id stability across detach/reattach and the prune rule for moved-out-of-document elements | follow the existing ViewState weak-by-id contract; write it down as part of F0 |
| ESO2 | **Dynamic `type` mutation** — `input.type='radio'` after state exists under the checkbox template_ref; retained-state semantics are fine for template-local state, but *engine-backed* `checked` is shared across refs — spec per-type value/checkedness rules need a table | resolve in F1 with a re-match test |
| ESO3 | **`~` projection cost & mutability** — VMap element reads per handler call; and `~.attr = v` (S12.2.2) from an `edit` behavior template must route through DOM mutation primitives with restyle, not silent attr pokes | measure in F1; `edit`-template DOM writes deferred until `detail.ls` |
| ESO4 | **Event-object contract v2** — today's event map is flat and per-type ad hoc (`target_class`, `caret_pos`, `selection_start`); behavior templates need a stable, documented shape incl. `prevent_default`/`pass`, modifiers, and form-relevant fields | spec in F0 alongside the state-dump harness |
| ESO5 | **Ordering guarantee across three handler layers** (JS listeners → app templates → behavior templates) incl. `defaultPrevented` propagation from JS into the native flag the behavior step checks — and whether app-template handlers should themselves be re-based onto the listener path long-term (DOM_Pkg Q8) | F0 defines the order; Q8 unification is post-forms |
| ESO6 | **Radio-group scope correctness** — group = same `name` within form owner, else document; form-association (`form=` attribute) rules needed by `radio_group`/`form_of` don't exist natively yet | implement in the primitive (native walk) in F1 |
| ESO7 | **Per-event Lambda call budget** — dispatch+handler must stay ≪ frame budget; existing `[TIMING]` instrumentation shows handler-only cost is small vs rebuild, but behavior handlers add a call on *every* unclaimed-then-claimed discrete event | set a budget (< 1 ms policy call) and a perf gate in F1; hot-path guard keeps continuous events out |
| ESO8 | **Cascade re-entry from `dispatch()`** — synthetic `change`/`submit` from inside a handler opens a nested cascade; define depth limit and re-entrancy rules (native `active_cascade_depth` already nests) | F0; cap + log at depth N |
| ESO9 | **Package load cost — a tuning question, not a gate** (user direction, 2026-08-25). Per-document MIR compile of the dom package vs a shared compiled artifact (D8.5.1 cache; DOM_Pkg Q3). Measurement calibrates *how much* tuning is needed; it never decides whether the design ships (ES14). Expected answer: compile the package once and **cache and reuse** it across documents — the package is fixed, engine-owned source with no per-document variation, so it is an ideal cache candidate; per-document work should reduce to instantiating registry entries against the document's context | measure at the F0b load point; tune with caching/reuse as the numbers require |
| ESO10 | **First-class package-load API** — generated-import-source loading is duct tape; a `runtime_load_package()` entry with error reporting is wanted once two consumers exist | acceptable duct tape through F-phases |
| ESO11 | **Headless/CLI determinism** — layout regression suites (`make layout`) must produce identical trees with the package on/off for non-interactive pages; interactive assertions must force the package on | F0 harness decides suite defaults |
| ESO12 | **`visited` source & privacy** — history-backed state read exposed to script; adopt a color-only-styling-style restriction? Also durability: visited is RS1-durable, unlike all form state | resolve before `link.ls`; out of form scope |
| ESO13 | **JS IDL coherence** — `input.checked` / `.validity` / `setCustomValidity` from page JS must observe/affect the same canonical state the behavior templates manage; the L4 adapter (DOM_Pkg) must route these to the same waist writers | coordinate with DOM_Pkg Phase 1/2; `setCustomValidity` lands with F3 |
| ESO14 | **Schema checker coverage during migration** — while a family is Lambda-owned but the schema is still C data, the checker must keep auditing Lambda-made transitions (works via cascade settle) — but new Lambda-only behaviors (F4 submission) have no rules; write minimal new rows or accept unaudited-until-§6.3 | accept gap, tracked per phase |
| ESO15 | **`test_state_store_gtest` evolution** — unit tests currently drive native writers directly; migrated families need equivalent Lambda-driven tests plus the native-vs-Lambda dump-diff harness (§8) | F0 deliverable |
| ESO16 | ~~**Two-runtime documents**~~ — **resolved 2026-08-25 (ES12/ES13, §3.11)**, and it was smaller than first stated: no second runtime is needed, because `js.runtime` already *is* a Lambda `Runtime*` (shared `lambda-rt` layer), so the dom package loads onto the document's existing runtime and the one-evaluator-per-document invariant is preserved rather than stressed. The real issue was never startup cost or `free_document` reconciliation but the **realm gates inferring page nature from `lambda_runtime != null`** — replaced by explicit `page_kind` / `js_has_dom_realm` markers in F0a (landed). Residual open items: per-document package load cost (ESO9) and — found while building F0b — the fact that a **script-less** HTML page creates no runtime at all, so "reuse the document's runtime" needs a creation step for those pages (ESO25) | closed; see ESO9 for the cost half |
| ESO17 | **Offset unit system** — three unit systems meet at text editing: UTF-8 bytes (native buffer), UTF-16 code units (JS IDL), codepoints (Lambda strings; the existing app-template event contract already uses "char indices"). Ruling: every Lambda-facing offset is **codepoints**; native owns all conversions (`tc_utf8_to_utf16_*` family) | pin in the F0 event-contract spec (with ESO4) |
| ESO18 | **Typing-latency budget** — F5 puts one Lambda handler + one waist call on every keystroke where native today is µs-scale; the app-template path proves feasibility but its measured figures include re-render. Gate: applier handler + `replace_range` p95 under budget (proposed < 2 ms) in a dedicated `[TIMING]` lane before the F5 claim becomes default | F5 perf gate; hot-path guard is unaffected (typing is a discrete event) |
| ESO19 | **Undo-ring memory & shape** — per-edit `{value, selection}` snapshots as template-local Lambda values are GC-managed and document-anchored; long sessions in large textareas may warrant inverse-op records instead of full snapshots | start with bounded snapshots (native `EditHistory` parity, incl. dedupe); revisit on measurement |
| ESO20 | **JS `.value` writes vs Lambda-owned history/composition** — a page-JS setter mid-session must reset history and cancel composition coherently, as native `tc_set_value` does today; the L4 adapter must route `.value`/`setSelectionRange` through the same waist writers so JS and Lambda never diverge | with ESO13; lands in F5 |
| ESO21 | **IME boundary detail** — preedit session stays native, commit content is applied by the Lambda applier: who rejects a commit on readonly/disabled (today `te_ime_commit_prepare` accepts the session but rejects the commit), and does the template see a distinct `insertFromComposition` input type or plain `insertText`? | F6; proposed: native keeps the reject, Lambda applies content, distinct input type |
| ESO22 | **Mirror collapse prerequisite** — the triple mirror (`FormControlProp` ↔ `ViewState` ↔ `DocState::sel`) with manual bidirectional sync must collapse to one canonical + projections before or with F5, or Lambda-driven edits will fight `form_control_sync/restore_text_control_state` | scheduled inside F5 engine work |
| ESO23 | **Predicate value kinds beyond text** — only string/symbol literals are compared today; a numeric or boolean literal predicate (`<input tabindex:0>`) is *rejected* rather than risk a false match, and a typed field (`type: string`) checks presence without checking the inner type | extend `template_is_value_predicate` / add type-check on the `LMD_TYPE_TYPE` branch when a behavior template needs it |
| ESO24 | **Bare-name presence sugar** — `<a href>` does not parse into the element type's shape (registers identically to `<a>`), so presence must be spelled `href: any`. Sugar would live in the element-type field parser | cosmetic; not needed for forms |
| ESO25 | **Script-less pages own no runtime** — verified during F0b. **Partially resolved 2026-08-25** (user decision: lazy-on-first-event, document owns teardown). Implemented and working: on the first *discrete* event a document with no runtime creates one, loads the package, and `free_document` releases it (memtrack clean, 0 live allocations); a script-less HTML page then gets its checkbox activation from the Lambda behavior template. **Gated off by default** behind `RADIANT_DOM_PKG_CREATE_RUNTIME=1`, because creating an evaluator is not safe to decide from a point-in-time probe — see ESO27 | mechanism done; enabling by default blocked on ESO27 |
| ESO26 | **Event-sim's fallback toggle masks legitimately-prevented defaults** — `event_sim.cpp` treats "checkbox state did not change" as "the coordinate click missed" and synthesizes a toggle, skipping that only when an inline `onclick` exists. A prevented default action legitimately changes nothing, so the harness silently re-toggles and the assertion reads the simulator's action rather than the engine's. Found while testing `prevent-default`; worked around by asserting on the suppressed `change` event instead of on `checked`. Will recur across F1–F6 wherever a behavior template correctly does nothing | extend the existing exemption to cover clicks claimed by a template handler, so the heuristic stops firing in a case that is now legitimate |
| ESO27 | **Evaluator ownership needs a contract, not a probe** — creating an evaluator for a document that owns none can strand JS: an HTML page may have no runtime, no bound `context` and no live `js_active_runtime_state` at first-event time and *still* start JS afterwards, crashing in `js_observer_runtime_state` (reproduced with `test/html/index.html`). Five point-in-time guards each admitted a case. **Design landed 2026-08-25 — see §3.12 (EO1–EO6)**: ownership becomes scoped rather than sticky, derived JS state moves with the binding, documents own evaluators while threads borrow them, creation becomes a document-lifecycle event instead of a dispatch side effect, subdocuments borrow, and a later JS start attaches to the existing context instead of competing | designed; implement in the four steps listed in §3.12, then enable runtime creation by default and retire `RADIANT_DOM_PKG_CREATE_RUNTIME` |
| ESO28 | **An open dropdown outlives the `FormControlProp` it points at** — **fixed 2026-08-25**. Diagnosis by instrumentation: both halves set, settle validates clean, no close is ever called, then the same element reports `form=0x0` — the control was released while `DocState::open_dropdown` still named it. Two release paths null `elem->form` (`view_pool.cpp`'s only when `heap_allocated`, `text_control.cpp`'s unconditionally), and neither told the state store. Fixed at `form_control_prop_release`, the one point both paths funnel through, so the dropdown closes at the moment its control actually goes away — no duplicated fix and no reliance on a later prune. Prune-after-reflow was tried first and does not work: the release happens after the last prune. Pre-existing engine bug, not introduced by the migration; a behavior template merely reaches it | fixed |

---

## 8. Testing and migration safety

1. **State-dump equivalence harness (the migration oracle).** The Mark-tree cascade dump (`radiant_state_dump_emit_cascade`, diffable by design) is recorded for a scripted event sequence with the native path, then with the package claim flipped; the dumps must match modulo a whitelist. This turns each F-phase flip into a diff review rather than a leap of faith.
2. **Baselines**: `make test-radiant-baseline` and the UI-automation suite at 100% of current pass rate per phase; event-sim `assert_state_store` / snapshot assertions extended to the migrated families.
3. **Package unit tests**: `test/lambda/dom/*.ls` + expected `*.txt` per the standard convention, exercising templates headlessly against constructed elements (validation table-driven: the full HTML constraint-validation matrix as data).
4. **Kill-switch bisection**: every regression is first bisected native-vs-package via `RADIANT_DOM_PKG=0` and per-class claims before debugging inside the package.
5. **Perf gates**: interaction-latency checks per phase (decision-2 requirement); `[TIMING]` dispatch instrumentation extended with a behavior-dispatch lane.

---

## 9. Decision ledger (ES)

| # | Decision |
| --- | --- |
| ES1 | Behavior templates = `view`/`edit` declarations in a behavior registry, attached by element match at dispatch, coexisting with app templates' reverse-render-map path |
| ES2 | Behavior state keys by element node id into the engine's existing stores; engine prunes; no parallel Lambda-side store |
| ES3 | Element patterns gain attribute presence/value predicates; CSS-like specificity; last-definition-wins override; live re-match at dispatch |
| ES4 | `state` declares bindings — bare known names = engine-backed canonical state (hot names read-only), `name: value` = template-local; storage/invalidation stay native, transitions move to Lambda |
| ES5 | Behavior handlers are UA default actions: post-listener, defaultPrevented-suppressed, one call per discrete event, per-class fallback-until-registered, inside the event cascade |
| ES6 | All effects through named `radiant`-module waist primitives; no direct pokes |
| ES7 | Package at `lambda/package/dom/`; transactional document-setup load; identical windowed/headless/event-sim loading; `RADIANT_DOM_PKG=0` kill switch |
| ES8 | Handler errors contained at the boundary: log, settle, per-event native fallback, sticky per-class fallback on repeated failure |
| ES9 | **Text-control value editing moves to Lambda** (user decision, 2026-08-25): the behavior template is the `beforeinput` applier — edit decisions, word/line boundaries, paste sanitization, `maxlength`, undo/redo, change-on-blur, and IME commit content in Lambda — driving the native `replace_range` splice primitive; buffer storage, unit conversions, caret/selection geometry, IME preedit sessions, and paint stay native. Supersedes RAD_19 §1's stay-native rationale and DOM_Pkg's "text-insertion mechanics stay N" for form controls; both docs need versioned amendments when F5 lands |
| ES10 | **JS and Lambda are parallel peers over one canonical state** (user clarification, 2026-08-25): state storage stays in C++ so views link to it during rendering and JS IDL manipulation continues unchanged; Lambda owns only the UA role; both realms active on every interactive document, sequenced by the dispatch pipeline, neither gatekeeping the other (§3.10) |
| ES11 | **One template system, one state store** (user clarification, 2026-08-25): behavior and app templates share the DocState-bound template-state store, `state`-clause semantics, sugar, and dispatch; they differ only in attachment mode and `~`'s identity rule; the dom package attaches to template-produced elements the same as parsed HTML, so `.ls` app pages get UA behavior from the same package (§3.4) |
| ES12 | **One script runtime per document, shared by JS and Lambda** (user direction, 2026-08-25): the dom package loads onto the document's existing runtime/`EvalContext`; no second runtime; the one-evaluator-per-document invariant is preserved; `dom_document_script_runtime(doc)` replaces the `lambda_runtime ? … : js.runtime` ternaries (§3.11) |
| ES13 | **Explicit page kind** (user direction, 2026-08-25): `DomDocument::page_kind` (HTML / Lambda-script / generated provenance) and `js_has_dom_realm` (capability) replace every inference from `lambda_runtime != null`; both realm gates rewritten; lands in F0a as a behavior-neutral refactor so an HTML/JS page can carry Lambda code without losing its JS realm (§3.11) |
| ES14 | **The design is committed** (user direction, 2026-08-25): behavior templates in `lambda/package/dom` are the direction, not a conditional experiment. Performance measurement — package load cost (ESO9), typing latency (ESO18), per-event budget (ESO7) — calibrates *how much tuning* is required (compiled-package caching and reuse being the expected first lever); it never functions as a go/no-go on the architecture. Kill switches and per-class native fallback (ES5, ES7) remain as incremental-landing and debugging tools, not as an escape hatch from the design |
| ES15 | **Handler verdicts are return values, not callables** (user decision, 2026-08-25): a handler returns `'pass'` (decline — dispatch continues looking, and the native default action for that class stays in charge) or `'prevent-default'` (handled, and remaining default actions are suppressed, including UA behavior dispatch); any other return means handled. Chosen over an `evt.prevent_default()` callable because it fits the existing `Item handler(Item model, Item event)` ABI without putting native function values into the event map; the callable form can be revisited with the ESO4 event-contract v2 |

---

## Appendix — formal-spec anchors used

- **S12.1.3** — reactive templates: pure `fn` body, mutation only in `pn` `on` handlers. Behavior templates are the same doctrine applied to UA behavior.
- **S9.1.4 / S9.2.4** — state lives in view state (here: engine-backed slots + template-local entries), never inside function values; view-state `var`s can't be `var`-borrowed — behavior handlers respect this by mutating only through waist calls.
- **S12.2.2** — element mutation is defined on both faces; `edit`-template DOM writes route through mutation primitives (ESO3).
- **S7.6 / S7.10** — error discharge and the boundary contract backing ES8.
- **D4.5.1v3** — the Radiant seam (pin, gen-check, copy-as-value) governs every element reference crossing into Lambda (ES2, generation checks in §3.5).
- **D7.2.1–D7.2.3** — script packages: immutable module scope (state is document-anchored instead), transactional init (ES7), source distribution + derived compile caches.
- **D8.5.1** — MIR module cache amortizing package compile (ESO9).
