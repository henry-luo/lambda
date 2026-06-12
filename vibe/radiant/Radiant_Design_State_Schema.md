# Radiant State Transition Schema — Data/Schema-Driven State Machine

**Status**: Proposal

**Date**: 2026-06-12

**Scope**: Refactor Radiant's interaction state machine so that transition legality and
post-transition invariants are expressed as a **declarative data table** rather than
hand-written C++ control flow. Add a single-source-of-truth schema in
`radiant/state_schema.cpp` and a debug-mode runtime checker that first covers selection/IME,
then expands family-by-family as the remaining StateStore writers are normalized behind
semantic transition events.

**Builds on**:
- [Radiant_Design_State_Machine.md](./Radiant_Design_State_Machine.md) — FSM families, settle/validate boundary, JSON event log.
- [Radiant_Design_State_Machine2.md](./Radiant_Design_State_Machine2.md) — `DocState` / `ViewState` / per-doc `StateStore`, writer-only mutation.
- [Radiant_Design_State_Store_Dump.md](./Radiant_Design_State_Store_Dump.md) — Mark-tree state dump, `assert_state_dump` UI fixtures.

### Settled decisions (2026-06-12)

These resolve the original open questions and govern the rest of this document:

1. **Event granularity:** key the schema on canonical semantic mutation events (§3.3), not
   raw `EventType`. For the first landing these are the existing selection/caret transition
   kinds plus the IME composition mutators. Other families are added only after their direct
   StateStore writers are normalized behind equivalent semantic transition events.
2. **Failure mode:** an undeclared transition, a wrong target state, an unmet action
   obligation, or an invariant failure is a **hard `assert()` + abort in debug builds**
   (and a `state.invalid` JSON record). Release builds do not run transition-edge checking;
   existing resident validation behavior remains equivalent to today.
3. **Invariant migration:** **hybrid — keep the nine `validate_*` functions running in
   parallel with the new table-driven pass during the later invariant migration step, assert
   the two agree (differential check), then delete the old functions in a dedicated cleanup
   commit** once parity is proven over the baseline suite (§8).
4. **Property/fuzz testing (§7.5):** adopted, but scheduled as a **later phase** (Step J),
   after the schema and runtime checker are in place.
5. **Mark/Lambda-authored schema (§9, last bullet):** **documentation-only future
   enhancement** in this proposal; not implemented now.
6. **First family to land:** **selection + IME only** (both already FSM-shaped and the most
   invariant-dense around carets/composition) — §8 Step C.
7. **Document-lifecycle family:** defer adding `DocState` lifecycle fields until navigation
   or document activation work needs them. Keep it in the target schema, but do not increase
   the blast radius of the first checker landing.
8. **Table layout:** a **single `RADIANT_STATE_RULES` array** for all families (one file,
   readable top-to-bottom, sectioned by comment banners); the lookup indexes it by
   `[family][event]` at startup if scan cost ever matters — §3.7, §5.3.

---

## 1. Motivation

### 1.1 Where we are today

The state-machine boundary in [radiant/state_machine.cpp](../../radiant/state_machine.cpp)
is **two-layered and imperative**:

1. **Transition functions** — `focus_transition`, `caret_transition`, `selection_transition`,
   `hover_transition`, `active_transition`, `drag_transition`
   ([state_machine.cpp:32–223](../../radiant/state_machine.cpp)). Each is a `switch` over a
   `*TransitionKind` enum that calls the relevant writer
   (`focus_set`, `selection_extend`, `doc_state_set_hover_target`, …), then calls
   `radiant_state_assert_valid()` and `radiant_state_validate_interaction()`.

2. **Invariant validators** — `radiant_state_validate_interaction()`
   ([state_machine.cpp:814](../../radiant/state_machine.cpp)) plus nine `validate_*` helpers
   (`validate_focus_invariants`, `validate_selection_invariants`,
   `validate_view_state_registry`, …). These contain **~96 distinct hand-coded
   `report_fail()` checks** — e.g. *"focused document does not have exactly one :focus target"*,
   *"collapsed DOM selection has non-none direction"*, *"view form state range value is out
   of bounds"*.

This works, but it has structural weaknesses:

- **It checks state shape, not transition legality.** The validators confirm the *resulting*
  state is internally consistent. They do **not** confirm that the specific edge
  *"from `CaretCollapsed`, on `extend_to_boundary`, you may only reach `RangeSelected*` or
  `CaretCollapsed`"* was a legal move. An illegal jump that happens to land on a
  self-consistent state passes silently.
- **The legal-transition knowledge is implicit.** It lives scattered across the `switch`
  arms, the writer bodies, and the validators. There is no single artifact you can read,
  diff, diagram, or hand to a test generator that says *"these are all the states and all the
  legal edges."*
- **Invariants are code, not data.** Adding a state or an invariant means editing a `switch`,
  editing a validator, and hoping the two stay in sync. The 96 checks are append-only and
  hard to audit for completeness.
- **No coverage signal.** Nothing tells us *"event E was dispatched in state S, but the schema
  has no rule for (S, E)"* — i.e. gaps in the model are invisible.

### 1.2 What we want

Express the entire FSM as a **transition table** keyed on the tuple the user described:

```
view/element class  +  current_state  +  event  [+ guard]  →  new_state  [+ actions + post-invariants]
```

Put the whole rule set in **`radiant/state_schema.cpp`** as a static, const,
data-only array. Then, in debug builds, on **every event routed to the StateStore**:

1. capture the **current** logical state (per family, for the affected view),
2. let the existing writer/transition code run the mutation,
3. capture the **post** logical state,
4. find the schema rule matching `(view_class, family, current_state, event, guard)`,
5. assert a rule exists (no undeclared transitions) **and** that the observed `new_state`
   equals the rule's declared `new_state`,
6. run the invariants the rule/state declares.

The 96 imperative `report_fail()` checks become **data**: a registry of named predicate
primitives plus a table binding *which predicate applies in which state*. The schema becomes
the single source of truth from which we drive validation, diagrams, replay conformance, and
test generation.

---

## 2. Goals and non-goals

### Goals

- One declarative artifact (`state_schema.cpp`) enumerating **every** state, event,
  and legal transition across **all** interaction families.
- Debug-mode transition checking that catches *both* (a) illegal transitions and (b) gaps
  where no rule covers the observed `(state, event)`.
- Refactor the existing 96 invariant checks into a data-bound invariant registry, keyed by
  the logical state they belong to, so completeness is auditable.
- Zero (or negligible) transition-check overhead in release builds — edge checking compiles
  out under `NDEBUG`, while resident validation keeps today's behavior.
- The schema is the source for generated artifacts: a Mermaid/Graphviz diagram, a coverage
  report, and replay/UI-test conformance.

### Non-goals

- **Not** a general-purpose statechart interpreter or an embedded DSL. Predicate and action
  *primitives* stay as named C++ functions; only the *binding table* (which primitive applies
  where) becomes data. (See §6 for why this is the right altitude.)
- **Not** a rewrite of the writer APIs or the `DocState`/`ViewState` ownership model. The
  schema layer sits beside the existing transition functions; writers are unchanged.
- **Not** changing release-build behavior. Transition schema checking is a debug/diagnostic
  facility; the current validation entry points keep their existing release/debug behavior.

---

## 3. The transition model

### 3.1 Orthogonal state families (parallel regions)

Radiant's interaction state is not a single FSM — it is a set of **orthogonal regions** that
evolve concurrently (the focus can change while a scroll drag is in progress). This maps
directly onto Harel statechart *parallel regions* / SCXML `<parallel>`. The schema is
partitioned by **family**:

| Family enum | Backing state in `DocState`/`ViewState` | Source |
|---|---|---|
| `SM_FAMILY_DOCUMENT` | document lifecycle (load/active/inactive/unload) | design doc; not yet a field |
| `SM_FAMILY_FOCUS` | `FocusState* focus` | [state_store_internal.hpp](../../radiant/state_store_internal.hpp) |
| `SM_FAMILY_SELECTION` | `DomSelection* dom_selection` + `SelectionState* selection` + `CaretState* caret` | state_store_internal.hpp |
| `SM_FAMILY_IME` | `EditingInteractionState.composition` | [state_store.hpp:226](../../radiant/state_store.hpp) |
| `SM_FAMILY_HOVER` | `View* hover_target` + `ViewState.flags.hovered` | state_store.hpp |
| `SM_FAMILY_ACTIVE` | `View* active_target` + `ViewState.flags.active` | state_store.hpp |
| `SM_FAMILY_DRAG_DROP` | `DragDropState* drag_drop`, `is_dragging` | state_store_internal.hpp |
| `SM_FAMILY_SCROLL` | `ViewState.data.scroll` | state_store.hpp:117 |
| `SM_FAMILY_FORM_CHECKABLE` | `ViewState.data.form.checked` (checkbox/radio) | state_store.hpp |
| `SM_FAMILY_FORM_SELECT` | `ViewState.data.form` (`selected_index`, `dropdown_open`, `hover_index`) | state_store.hpp |
| `SM_FAMILY_FORM_RANGE` | `ViewState.data.form.range_value` | state_store.hpp |
| `SM_FAMILY_FORM_TEXT` | `ViewState.data.form` (`selection_*`, `current_value`) | state_store.hpp |
| `SM_FAMILY_DROPDOWN` | `open_dropdown` + geometry (doc-level overlay) | state_store.hpp:262 |
| `SM_FAMILY_CONTEXT_MENU` | `context_menu_target` + `context_menu_hover` | state_store.hpp |

Each family is an independent FSM with its own logical-state enum. A single raw input event
(e.g. `MOUSE_DOWN`) fans out into several family transitions; each is checked against its own
family's rules.

### 3.2 Logical states per family

These enumerate the **logical** state (a small enum), derived on demand from the concrete
`DocState`/`ViewState` fields by a `derive_state()` function (§5.2). They consolidate the
states already sketched in `Radiant_Design_State_Machine.md` with what the code actually
represents.

```cpp
// radiant/state_schema.hpp

enum DocLifecycleState   { DOC_NONE, DOC_LOADING, DOC_ACTIVE, DOC_INACTIVE, DOC_UNLOADING };

enum FocusFsmState       { FOCUS_NO_DOCUMENT, FOCUS_DOC_INACTIVE, FOCUS_DOC_ACTIVE_NONE,
                           FOCUS_ELEMENT, FOCUS_TEXT_CONTROL, FOCUS_CONTENTEDITABLE,
                           FOCUS_SUBDOCUMENT };

enum SelectionFsmState   { SEL_EMPTY, SEL_CARET_COLLAPSED, SEL_RANGE_FORWARD, SEL_RANGE_BACKWARD,
                           SEL_POINTER_SELECTING, SEL_KEYBOARD_EXTENDING };

enum ImeFsmState         { IME_IDLE, IME_COMPOSING, IME_COMMITTED /*transient*/ };

enum HoverFsmState       { HOVER_NONE, HOVER_TARGET };
enum ActiveFsmState      { ACTIVE_NONE, ACTIVE_PRESSED };

enum DragFsmState        { DRAG_IDLE, DRAG_PENDING, DRAG_ACTIVE, DRAG_OVER_TARGET };

enum ScrollFsmState      { SCROLL_IDLE, SCROLL_BAR_HOVER, SCROLL_BAR_DRAGGING };

enum CheckableFsmState   { CHK_UNCHECKED, CHK_CHECKED, CHK_INDETERMINATE };
enum SelectFsmState      { SELCTL_CLOSED, SELCTL_OPEN };
enum DropdownFsmState    { DD_CLOSED, DD_OPEN };
enum ContextMenuFsmState { CM_CLOSED, CM_OPEN };

// Modifier sub-states (orthogonal, not in the primary enum): disabled / readonly / required.
// Modeled as guards (§3.5), not as states, to avoid combinatorial state explosion.

#define SM_STATE_ANY  (-1)   // wildcard "from" state
#define SM_STATE_SAME (-2)   // "to" state == from state (self-loop, no change)
```

### 3.3 Event vocabulary

Today there are three different mutation surfaces:

1. raw `EventType` values ([event.hpp:8](../../radiant/event.hpp):
   `RDT_EVENT_MOUSE_DOWN`, `RDT_EVENT_KEY_DOWN`, …),
2. the existing per-family transition APIs (`focus_transition`, `selection_transition`,
   `drag_transition`, …), and
3. direct StateStore writers such as `form_control_set_*`, `scroll_state_*`,
   `doc_state_open_dropdown`, and `doc_state_open_context_menu`.

The schema keys on **canonical semantic mutation events**, not raw hardware events. For the
first landing this vocabulary is intentionally small and backed by existing choke points:
selection/caret transitions plus the IME composition mutators. The larger enum below is the
target end-state after direct writer surfaces are normalized behind equivalent semantic
events.

```cpp
enum SmEvent {
    // focus
    SM_EV_FOCUS_ELEMENT, SM_EV_FOCUS_TEXT_CONTROL, SM_EV_FOCUS_CONTENTEDITABLE,
    SM_EV_FOCUS_SUBDOCUMENT, SM_EV_BLUR_CURRENT, SM_EV_FOCUS_MOVE_FWD, SM_EV_FOCUS_MOVE_BACK,
    SM_EV_VIEW_REMOVED,
    // selection / caret
    SM_EV_COLLAPSE_TO_BOUNDARY, SM_EV_START_POINTER_SELECTION, SM_EV_END_POINTER_SELECTION,
    SM_EV_EXTEND_TO_BOUNDARY, SM_EV_EXTEND_TO_VIEW, SM_EV_SET_BASE_AND_EXTENT,
    SM_EV_SELECT_ALL, SM_EV_COLLAPSE_TO_START, SM_EV_COLLAPSE_TO_END, SM_EV_CLEAR_SELECTION,
    // ime
    SM_EV_COMPOSITION_START, SM_EV_COMPOSITION_UPDATE, SM_EV_COMPOSITION_COMMIT,
    SM_EV_COMPOSITION_CANCEL,
    // hover / active
    SM_EV_HOVER_SET, SM_EV_HOVER_CLEAR, SM_EV_ACTIVE_SET, SM_EV_ACTIVE_CLEAR,
    // drag/drop
    SM_EV_DRAG_SET_STATE, SM_EV_DRAG_BEGIN_DROP, SM_EV_DRAG_UPDATE_MOTION,
    SM_EV_DRAG_SET_DROP_ACTIVE, SM_EV_DRAG_SET_DROP_TARGET, SM_EV_DRAG_CLEAR_DROP,
    // scroll
    SM_EV_SCROLL_SET_POSITION, SM_EV_SCROLL_SET_MAX, SM_EV_SCROLLBAR_HOVER,
    SM_EV_SCROLLBAR_BEGIN_DRAG, SM_EV_SCROLLBAR_CLEAR_DRAG,
    // form
    SM_EV_FORM_SET_CHECKED, SM_EV_FORM_SET_VALUE, SM_EV_FORM_SET_SELECTION,
    SM_EV_FORM_SET_SELECTED_INDEX, SM_EV_FORM_SET_RANGE_VALUE, SM_EV_FORM_SET_HOVER_INDEX,
    SM_EV_FORM_SET_DISABLED, SM_EV_FORM_SET_READONLY, SM_EV_FORM_SET_REQUIRED,
    // dropdown / context menu
    SM_EV_DROPDOWN_OPEN, SM_EV_DROPDOWN_CLOSE,
    SM_EV_CONTEXT_MENU_OPEN, SM_EV_CONTEXT_MENU_CLOSE, SM_EV_CONTEXT_MENU_HOVER,
    // document lifecycle
    SM_EV_DOC_LOAD, SM_EV_DOC_COMMIT, SM_EV_DOC_UNLOAD,
    SM_EV__COUNT
};
```

Migration is therefore **not** purely mechanical for every family. Each existing `switch` arm
does map cleanly to a semantic event, but several families currently bypass transition
functions entirely. Those families must first get thin transition wrappers or writer-level
schema hooks so the checker can distinguish "this event intentionally mutated state" from
"this helper was called during layout/style initialization."

### 3.4 View/element classes

The `view_class` discriminator lets one event resolve to different rules depending on the
target. Derived from the `DomElement` tag/type via the existing `tc_is_text_control()`,
`form_control_*` helpers.

```cpp
enum SmViewClass {
    SM_VC_ANY,            // matches any view
    SM_VC_FOCUSABLE,      // tabindex / natively focusable
    SM_VC_TEXT_CONTROL,   // input[type=text], textarea, contenteditable
    SM_VC_CHECKBOX, SM_VC_RADIO, SM_VC_SELECT, SM_VC_RANGE, SM_VC_FILE,
    SM_VC_SCROLLABLE, SM_VC_LINK, SM_VC_DOCUMENT
};
```

### 3.5 Guards and actions

A pure `(from, event) → to` table cannot express conditional transitions
(e.g. `extend_to_boundary` collapses to a caret iff anchor==focus, otherwise forms a range;
a disabled control ignores `set_checked`). Two mechanisms, both **referenced by id** so the
table stays data:

- **Guards** — named boolean predicates, e.g. `GUARD_NOT_DISABLED`,
  `GUARD_ANCHOR_EQUALS_FOCUS`, `GUARD_HAS_RADIO_GROUP`. Multiple rules may share the same
  `(from, event)` and are disambiguated by guard; the first whose guard passes wins (ordered,
  like a CSS cascade or a `cond` clause).
- **Actions** — a set of *obligations* the transition must have performed, e.g.
  `ACT_EMIT_BLUR`, `ACT_UNCHECK_PRIOR_RADIO`, `ACT_CLOSE_IME`, `ACT_CLEAR_CARET`. In debug
  mode the checker can verify side-effect obligations were met (e.g. *"a radio check must have
  unchecked exactly one prior radio in the group"*).

Guards and action/effect observers are the **only** code in the schema layer; everything else
is data. Action verification is staged after the first transition checker lands because many
actions are not visible in final state alone. The implementation should add a small
`SmObservedEffects` recorder to the transition context and let writers mark observed effects
as they perform them.

### 3.6 The rule struct

```cpp
// radiant/state_schema.hpp

typedef struct StateTransitionRule {
    SmFamily      family;        // which region
    SmViewClass   view_class;    // SM_VC_ANY or a specific control kind
    int           from_state;    // family logical state, or SM_STATE_ANY
    SmEvent       event;         // canonical event
    SmGuardId     guard;         // SM_GUARD_NONE or a named predicate
    const int*    to_states;     // allowed resulting states; may include SM_STATE_SAME
    uint8_t       to_state_count;
    uint32_t      actions;       // observed side-effect obligations (ACT_*), staged in later
    const SmInvariantId* invariants;  // post-state invariants to assert
    uint16_t      invariant_count;
    const char*   name;          // diagnostic label, e.g. "focus.mouse_into_text_control"
} StateTransitionRule;
```

The `to_states` list is deliberately more general than a single `to_state`. Some real edges
resolve based on document order or selection collapse after the writer runs, and splitting
those into many guard rows would either duplicate complex predicates or obscure the transition
being modeled. `SM_STATE_SAME` remains a shorthand for "the derived post-state must equal the
captured pre-state."

### 3.7 Example rule rows (`state_schema.cpp`)

```cpp
// Illustrative shorthand. The real C++ file uses small static arrays for
// to_states/invariants because the rule struct stores pointer + count.
#define RULE(fam, vc, from, ev, guard, to_list, acts, inv_list, label) ...
#define TO(...) ...
#define INVS(...) ...

static const StateTransitionRule RADIANT_STATE_RULES[] = {

  // ---- FIRST LANDING: SELECTION / CARET ---------------------------------------
  RULE(SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_COLLAPSE_TO_BOUNDARY,
    SM_GUARD_NONE, TO(SEL_CARET_COLLAPSED), 0,
    INVS(INV_SEL_ONE_RANGE, INV_SEL_COLLAPSED_EQ), "sel.collapse_to_caret"),
  RULE(SM_FAMILY_SELECTION, SM_VC_ANY, SEL_CARET_COLLAPSED, SM_EV_EXTEND_TO_BOUNDARY,
    SM_GUARD_ANCHOR_EQUALS_FOCUS, TO(SEL_CARET_COLLAPSED), 0,
    INVS(INV_SEL_COLLAPSED_EQ), "sel.extend_noop"),
  RULE(SM_FAMILY_SELECTION, SM_VC_ANY, SEL_CARET_COLLAPSED, SM_EV_EXTEND_TO_BOUNDARY,
    SM_GUARD_FOCUS_AFTER_ANCHOR, TO(SEL_RANGE_FORWARD), 0,
    INVS(INV_SEL_DIR_FORWARD), "sel.extend_forward"),
  RULE(SM_FAMILY_SELECTION, SM_VC_ANY, SEL_CARET_COLLAPSED, SM_EV_EXTEND_TO_BOUNDARY,
    SM_GUARD_FOCUS_BEFORE_ANCHOR, TO(SEL_RANGE_BACKWARD), 0,
    INVS(INV_SEL_DIR_BACKWARD), "sel.extend_backward"),
  RULE(SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_START_POINTER_SELECTION,
    SM_GUARD_NONE, TO(SEL_POINTER_SELECTING), ACT_FIRE_SELECTSTART,
    INVS(INV_SEL_ACTIVE_HAS_ANCHOR), "sel.start_pointer"),
  RULE(SM_FAMILY_SELECTION, SM_VC_ANY, SEL_POINTER_SELECTING, SM_EV_END_POINTER_SELECTION,
    SM_GUARD_NONE, TO(SEL_CARET_COLLAPSED, SEL_RANGE_FORWARD, SEL_RANGE_BACKWARD), 0,
    INVS(INV_SEL_ONE_RANGE), "sel.end_pointer"),
  RULE(SM_FAMILY_SELECTION, SM_VC_ANY, SM_STATE_ANY, SM_EV_CLEAR_SELECTION,
    SM_GUARD_NONE, TO(SEL_EMPTY), 0, INVS(INV_SEL_EMPTY_NO_STALE), "sel.clear"),

  // ---- FIRST LANDING: IME ------------------------------------------------------
  RULE(SM_FAMILY_IME, SM_VC_TEXT_CONTROL, IME_IDLE, SM_EV_COMPOSITION_START,
    SM_GUARD_FOCUS_IS_EDITABLE, TO(IME_COMPOSING), ACT_PIN_COMPOSITION_RANGE,
    INVS(INV_IME_HAS_SURFACE), "ime.begin"),
  RULE(SM_FAMILY_IME, SM_VC_TEXT_CONTROL, IME_COMPOSING, SM_EV_COMPOSITION_UPDATE,
    SM_GUARD_NONE, TO(IME_COMPOSING), 0,
    INVS(INV_IME_PREEDIT_CARET_BOUNDS), "ime.update"),
  RULE(SM_FAMILY_IME, SM_VC_TEXT_CONTROL, IME_COMPOSING, SM_EV_COMPOSITION_COMMIT,
    SM_GUARD_NONE, TO(IME_IDLE), ACT_EMIT_INPUT | ACT_EMIT_CHANGE,
    INVS(INV_IME_NO_SURFACE), "ime.commit"),
  RULE(SM_FAMILY_IME, SM_VC_TEXT_CONTROL, IME_COMPOSING, SM_EV_COMPOSITION_CANCEL,
    SM_GUARD_NONE, TO(IME_IDLE), ACT_RESTORE_PRE_COMPOSITION,
    INVS(INV_IME_NO_SURFACE), "ime.cancel"),

  // ---- LATER FAMILIES AFTER TRANSITION API NORMALIZATION ----------------------
  // focus, hover/active, drag/drop, scroll, form controls, dropdown,
  // context menu, and document lifecycle are populated in later slices.
};
```

This array is **the** specification. It is exhaustively enumerable, diffable in code review,
and the source for the diagram and coverage tooling in §7.

---

## 4. Invariants as data

The 96 `report_fail()` checks in `state_machine.cpp` are refactored into a two-part design:

1. **Predicate primitives** — each invariant becomes a named `static bool`/`void` predicate
   that takes `(DocState*, const SmCheckContext*, StateValidationReport*)`. These are the same
   bodies that exist today; only their *binding* changes. Each gets a stable `INV_*` id and a
   message string in one table:

```cpp
// radiant/state_schema.cpp
typedef struct InvariantDef {
    SmInvariantId id;          // INV_ONE_FOCUS, INV_SEL_DIR_FORWARD, ...
    SmFamily      family;
    int           applies_in_state;   // SM_STATE_ANY or a specific logical state
    SmInvariantFn predicate;          // named C++ primitive
    const char*   message;            // "focused document does not have exactly one :focus target"
} InvariantDef;

static const InvariantDef RADIANT_INVARIANTS[] = {
  { INV_ONE_FOCUS,        SM_FAMILY_FOCUS,     FOCUS_ELEMENT,  inv_one_focus_target,
    "focused document does not have exactly one :focus target" },
  { INV_FOCUS_WITHIN_CHAIN, SM_FAMILY_FOCUS,   SM_STATE_ANY,   inv_focus_within_ancestry,
    ":focus-within ancestry is inconsistent" },
  { INV_SEL_DIR_FORWARD,  SM_FAMILY_SELECTION, SEL_RANGE_FORWARD, inv_forward_direction,
    "forward DOM selection direction is inconsistent" },
  // ... all ~96, each one row ...
};
```

2. **Two evaluation modes**, both reading the same table:
   - **State-resident invariants** (Moore-style): `radiant_state_validate_interaction()`
     becomes a loop over `RADIANT_INVARIANTS`, running each predicate whose
     `applies_in_state` matches the current derived state of its family (or is `ANY`). This
     *replaces* the nine hand-written `validate_*` functions with one table-driven loop.
   - **Transition-targeted invariants** (Mealy-style): after a transition, only the
     invariant ids listed on the matched rule are checked. This is cheaper and pinpoints
     *which* transition produced an inconsistency. Use an id list rather than a `uint32_t`
     bitmask so the design can represent all current checks and keep growing without mask
     exhaustion.

The win: completeness becomes auditable. You can ask *"which invariants apply in
`SEL_RANGE_BACKWARD`?"* and get a list, instead of grepping a 700-line function. Adding a
state forces you to decide which invariants bind to it.

> **Honest caveat (see §6):** the predicate *bodies* remain C++. We are data-driving the
> **rule set** — the mapping of state/event → next-state, guards, actions, and which
> invariants apply where — not the arbitrary logic inside each predicate. That is the correct
> altitude for a C+ codebase without an embedded interpreter, and it is exactly how Blink,
> Chromium gesture recognition, and table-driven parsers are structured.

---

## 5. Debug-mode runtime checker

### 5.1 Wrapping the transition boundary

The existing selection/caret transition functions form the first reliable choke point. IME
composition should be wrapped at its current mutator boundary. Later families are added only
after their direct StateStore writers have equivalent semantic transition wrappers.

The checker must distinguish attempted transitions from committed transitions. Several
current transition functions return `false` before mutating state when arguments are missing
or a move cannot be applied. Those must not be reported as undeclared state-machine edges.
Use an explicit commit flag:

```cpp
// debug-only; compiles to nothing under NDEBUG
struct SmTransitionScope {
    DocState* st; SmFamily fam; SmEvent ev; SmViewClass vc; View* target;
    int from_state;
    bool committed;
    SmTransitionScope(DocState* s, SmFamily f, SmEvent e, View* v)
      : st(s), fam(f), ev(e), vc(sm_classify_view(v)), target(v), committed(false) {
        from_state = sm_derive_state(s, f, v);          // capture BEFORE mutation
    }
    void commit() { committed = true; }
    ~SmTransitionScope() {
        if (!committed) return;                          // ignored failed/no-op attempt
        int to_state = sm_derive_state(st, fam, target); // capture AFTER mutation
        sm_check_transition(st, fam, vc, from_state, ev, to_state, target);
    }
};
```

Usage in `selection_transition` is intentionally after argument validation and commits only
after mutation plus projection sync:

```cpp
bool selection_transition(DocState* state,
                          SelectionTransitionKind kind,
                          SelectionTransitionArgs* args) {
    if (!state) return false;

    SmTransitionScope _sm(state, SM_FAMILY_SELECTION,
                          selection_kind_to_event(kind),
                          args ? args->target : nullptr);
    transition_enter(state);
    switch (kind) { /* unchanged */ }
    transition_leave(state);
    state_machine_sync_selection_projection(state);
    editing_interaction_sync_projection(state);
    _sm.commit();
    return radiant_state_validate_interaction(state, NULL);
}
```

### 5.2 `sm_derive_state` — concrete → logical

Eventually, one function per family maps the concrete `DocState`/`ViewState` fields to the
logical enum. The first slice implements only selection and IME; later families get derive
functions in the same patch that adds their rules. These functions centralize logic that
today is duplicated across validators. Example:

```cpp
static int sm_derive_focus_state(DocState* s, View* /*v*/) {
    if (!s || !s->focus) return FOCUS_NO_DOCUMENT;
    View* cur = s->focus->current;
    if (!cur) return FOCUS_DOC_ACTIVE_NONE;
    DomElement* el = cur->is_element() ? lam::dom_require_element(cur) : nullptr;
    if (el && tc_is_text_control(el))  return FOCUS_TEXT_CONTROL;
    if (el && dom_is_contenteditable(el)) return FOCUS_CONTENTEDITABLE;
    return FOCUS_ELEMENT;
}
```

### 5.3 `sm_check_transition` — the heart

```cpp
static void sm_check_transition(DocState* st, SmFamily fam, SmViewClass vc,
                                int from, SmEvent ev, int to, View* target) {
#ifndef NDEBUG
    const StateTransitionRule* rule = sm_find_rule(fam, vc, from, ev, st, target);
    if (!rule) {
        // GAP: an event reached the store with no schema rule for (state, event).
        sm_report_undeclared(st, fam, vc, from, ev, to);   // log + assert
        return;
    }
    if (!sm_rule_allows_to_state(rule, from, to)) {
        sm_report_wrong_target(st, rule, from, to);  // log + assert
    }
    sm_check_actions(st, rule, target);          // later stage; reads SmObservedEffects
    sm_run_invariants(st, fam, rule->invariants, rule->invariant_count);
#endif
}
```

`sm_find_rule` scans `RADIANT_STATE_RULES` for the first row where `family`, `view_class`
(with `SM_VC_ANY` wildcard), `from_state` (with `SM_STATE_ANY` wildcard), and `event` match
**and** the row's `guard` predicate passes. Ordering gives guard precedence (like CSS
specificity / `cond`). The scan is linear but the table is small (low hundreds of rows) and
only runs in debug builds; if it ever matters, build a `[family][event]` index once at
startup.

### 5.4 Outcomes

- **Undeclared transition** → either a real illegal move *or* a hole in the schema. Both are
  bugs we want surfaced. Logged as `state.invalid` with `reason:"undeclared_transition"` to
  the JSON event log ([event_state_log.hpp](../../radiant/event_state_log.hpp)) and
  `assert()`-ed in debug.
- **Wrong target state** → the writer produced a state the schema forbids for this edge.
- **Missing action** → later stage; e.g. a radio check that did not uncheck its prior sibling
  after `SmObservedEffects` is wired to the relevant writers.
- **Invariant failure** → same as today, but the message and binding came from the table.

All reported failures route through the existing `state.invalid` JSON record and
`log_error()` path described in `Radiant_Design_State_Machine.md`, so tooling and tests
already understand them.

### 5.5 Integration with the settle boundary and state dump

- `radiant_state_settle()` ([state_machine.cpp:1177](../../radiant/state_machine.cpp))
  continues to run the *state-resident* invariant pass at cascade end. It remains the old
  imperative pass until the later invariant-table migration proves parity.
- The schema-version is emitted into the `session_start` record so replay/log consumers can
  detect FSM changes.
- The Mark state dump (`Radiant_Design_State_Store_Dump.md`) can annotate each `<doc>` /
  element with its derived per-family logical state (e.g. `focus_fsm:"TextControlFocused"`),
  making the dump self-describing against the schema. A new
  `assert_state_dump`-style fixture can additionally assert the **transition trace** matched
  the schema across a scenario.

---

## 6. Why "schema-driven," realistically

A note to set expectations, because "data-driven state machine" can mean very different
things:

- **What is data:** the set of states, events, view classes, the legal edges between them,
  guard/action/invariant *bindings*, diagnostic names, and messages. This is the part that
  changes often, must be audited for completeness, and benefits from being one diffable
  artifact.
- **What stays code:** the guard predicates, action implementations, invariant predicate
  bodies, and the writers. These are referenced from the table by id.

This split is deliberate and matches mature systems (Blink `FocusController`, Chromium
`ui::GestureRecognizer`'s transition tables, table-driven LR parsers, Erlang `gen_statem`).
Pushing the predicate bodies themselves into data would require an embedded expression
interpreter — large, slow, and against the project's C+ convention and the "no clever
indirection" grain of the codebase. If a future need arises (e.g. authoring rules without
recompiling), the table could be loaded from a Lambda/Mark file at startup — and since Lambda
*is* the project's data language, that is a natural extension (see §9), but it is explicitly
out of scope here.

---

## 7. Generated artifacts (single source of truth)

Because the schema is one structured table, several things fall out for free, each a small
offline tool or a `make` target:

1. **Diagram** — emit Graphviz/Mermaid per family: `make state-machine-diagram` walks
   `RADIANT_STATE_RULES` and prints `from --event[guard]--> to`. Keeps docs honest because the
   picture is generated, never hand-drawn.
2. **Coverage matrix** — for each family, a `state × event` grid marking covered / ignored /
   undeclared cells. Surfaces gaps (an event with no rule in a given state) *before* runtime
   hits them. This is the FSM analog of `make check-int-cast` / `make check-state-store`.
3. **Reachability / dead-state check** — static pass over the table: every non-initial state
   must be reachable by some edge, and every state must have an exit (or be explicitly
   terminal). Catches typos and orphaned states.
4. **Replay conformance** — the replay runner in `Radiant_Design_State_Machine.md` §"Replay"
   can assert that a recorded `input.*`/`state.transition` trace only contains edges present
   in the schema of the recorded version.
5. **Property/fuzz testing** — feed `event_sim` random legal event sequences; after each
   event the schema checker must pass. This is cheap, high-yield, and directly reuses the
   existing `event_sim` + `assert_state_store` machinery.

Recommend wiring (2) and (3) into a `make check-state-machine` target gated in CI, mirroring
the existing `make check-state-store` guard.

---

## 8. Migration plan

Each step is independently shippable and gated by `make test-radiant-baseline` staying 100%
green, matching the cadence of the prior phases.

- **Step A — schema skeleton.** Add `state_schema.hpp` (enums, rule/invariant structs, ids)
  and `state_schema.cpp` (empty tables + lookup helpers). No behavior change.
- **Step B — first derive functions.** Implement `sm_derive_state` only for
  `SM_FAMILY_SELECTION` and `SM_FAMILY_IME`. Unit-test those against focused `DocState`
  fixtures. Do not derive every family yet; deriving unused families increases surface area
  before the checker can prove value.
- **Step C — first vertical checker slice.** Populate `RADIANT_STATE_RULES` for selection +
  IME, add committed `SmTransitionScope` checking to `caret_transition`,
  `selection_transition`, and the IME composition path, and keep action checking disabled
  except for effects already observable from final state. Fix undeclared-transition findings
  as either real bugs or missing rows.
- **Step D — early coverage tooling for implemented families.** Add
  `make check-state-machine` for the families currently in the table. The check accepts
  partial global coverage but requires total coverage inside families marked complete.
  Mermaid/Graphviz diagram generation is deferred to a later step; the diagram remains useful
  once the schema grows beyond the first complete family set.
- **Step E — transition API normalization.** Add thin semantic transition wrappers, or
  writer-level schema hooks, for families that currently mutate directly through StateStore
  writers: form controls, scroll, dropdown, context menu, and any remaining hover/active/focus
  bypasses. Current implementation uses writer-level hooks for scroll, form controls,
  dropdown, and context menu so existing callers share the same semantic transition boundary.
- **Step F — expand one family at a time.** After normalization, populate focus, hover/active,
  drag/drop, scroll, form controls, dropdown, and context menu. Current coverage marks 13
  families complete in `make check-state-machine`: focus, selection, IME, hover, active,
  drag/drop, scroll, checkable, select, range, text, dropdown, and context menu.
- **Step G — invariant table (hybrid migration).** Move the ~96 `report_fail()` checks into
  `RADIANT_INVARIANTS` as predicate primitives + bindings, and add the table-driven pass to
  `radiant_state_validate_interaction()`. **Crucially, do not delete the nine `validate_*`
  functions yet** — run both the old imperative validators and the new table loop in debug
  builds, and add a **differential assertion**: on every validation, the old and new passes
  must produce the *same* pass/fail verdict (and ideally the same first failure message). A
  small `make` test or a debug-only cross-check enforces this over the baseline suite. Once
  parity is demonstrated, **delete the nine `validate_*` functions in a dedicated cleanup
  commit**, leaving the table as the single source of truth. This keeps the migration
  behavior-preserving by construction and trivially reversible, then lands the clean
  end-state. Current implementation has the first hybrid slice: 14 invariant bindings run
  through the table-driven pass, debug builds assert parity against the legacy pass, and
  `make check-state-machine` validates the binding table. (See "Replace vs. keep" rationale
  below.)
- **Step H — action/effect obligations.** Add observed action/effect recording and wire only
  the writers whose effects are needed by table rules. Current implementation has the first
  narrow slice: `SmTransitionScope` records observed action flags, nested transition scopes
  restore correctly, `make check-state-machine` validates action enum references in
  `RADIANT_STATE_RULES`, and `form_checkable.set_checked` requires the centralized
  `ViewState.data.form.checked` write (`SM_ACT_WRITE_CHECKED`) before the transition can
  commit. The second slice adds `form_checkable.uncheck_radio_group`, a dedicated semantic
  event for unchecking the previously selected radio in a same-name group, and requires both
  the checked-state write and `SM_ACT_UNCHECK_RADIO_GROUP`. The third slice adds
  `form_text.replace_text` for browser-style text-control replacement and `form_text.history`
  for undo/redo, requiring both `SM_ACT_DISPATCH_BEFOREINPUT` and `SM_ACT_DISPATCH_INPUT`
  around committed mutations. The fourth slice adds `selection.ui_start_pointer`, a UI-level
  pointer-selection event that requires `SM_ACT_DISPATCH_SELECTSTART` while leaving the
  lower-level `selection.start_pointer` event available for programmatic selection writes.
  The fifth slice adds UI-level focus events that require `SM_ACT_DISPATCH_BLUR` and, when a
  text control's focus-time value changed, `SM_ACT_DISPATCH_CHANGE`. Future Step H work should
  add action obligations only when new effectful transitions are introduced.
- **Step I — document lifecycle when needed.** Add the `SM_FAMILY_DOCUMENT` backing field only
  when navigation/document activation work needs it. Concretely, add a `DocLifecycleState
  lifecycle;` field plus `doc_state_set_lifecycle()`, then populate
  `SM_EV_DOC_LOAD`/`SM_EV_DOC_COMMIT`/`SM_EV_DOC_UNLOAD`. Keep this out of the first checker
  landing.
- **Step J — property/fuzz conformance (later phase).** Extend `event_sim` to drive random
  *legal* event sequences and assert schema conformance after every event (the hard-assert
  checker fires on any undeclared edge or invariant break). Also assert conformance over
  recorded replay traces. Add a small set of `test/ui/` fixtures. Scheduled after the schema
  and runtime checker are stable, per the §"Settled decisions".

### Replace vs. keep the `validate_*` functions — rationale

| | Replace (big-bang) | Keep / parallel (chosen, hybrid) |
|---|---|---|
| Source of truth | One immediately | Two during the window, then one |
| Behavior preservation | Manual review only; silent coverage loss if a predicate body drifts during extraction | By construction — old checks keep firing; differential assert proves equivalence |
| Review risk | Large, risky diff; hard rollback | Incremental; trivially reversible |
| Cost | Smallest end-state code | Temporary duplication + double debug cost; requires a disciplined delete step |

**Decision: hybrid, but later than the first transition checker.** Run both, assert agreement
(differential), then delete the nine functions in a dedicated commit once parity holds over
`make test-radiant-baseline`. This captures "keep"'s safety net and "replace"'s clean
end-state without blocking the initial selection/IME edge checker.

---

## 9. Good practices adopted from comparable systems

These informed the design above; collecting them so reviewers can weigh each.

- **Harel statecharts / W3C SCXML — parallel regions + guards + entry/exit actions.** The
  orthogonal-family partition (§3.1) is exactly SCXML `<parallel>`; guards are `cond`;
  ACT_* obligations are `onentry`/`onexit` actions. Standard, well-understood vocabulary.
- **XState (statecharts as data).** Confirms the "machine config is a plain data structure,
  guards/actions are named references into code" altitude (§6) — and that a single config can
  drive execution, visualization, and test-path generation. We mirror this without the
  interpreter.
- **Erlang/OTP `gen_statem` — totality and postponed events.** Every (state, event) pair is
  handled explicitly; unhandled events are a deliberate choice, not a silent gap. Drives the
  coverage matrix (§7.2) and the explicit `SM_STATE_SAME` "ignored" rows.
- **Chromium/Blink — table-driven transitions + `PassKey` writer tokens.** Validates keeping
  predicate bodies in C++ behind ids; the `MutatorToken`/`PassKey` pattern is already cited in
  `Radiant_Design_State_Machine.md` for writer enforcement and composes with this schema.
- **Mealy vs Moore distinction.** We use **both** deliberately: state-resident invariants
  (Moore) for "must always hold in state S," and transition-targeted invariants/actions
  (Mealy) for "this edge must do X." Being explicit about which is which prevents the common
  bug of checking the wrong thing at the wrong time.
- **TLA+/model-checking mindset (lightweight).** We don't run a model checker, but the
  reachability/dead-state pass (§7.3) and the coverage matrix give a cheap subset of its
  value: prove no state is orphaned and no event silently no-ops.
- **Decision tables / DO-178C traceability.** Each rule has a stable `name`; that name can be
  cited in commit messages, test fixtures, and bug reports, giving requirement→rule→test
  traceability.
- **Property-based testing (QuickCheck-style).** Random legal event sequences through
  `event_sim` with a per-event schema assertion (§7.5) finds transition bugs far faster than
  example-based tests alone, and reuses infrastructure that already exists. **Adopted as a
  later phase (Step J).**
- **Single source of truth / generate-don't-write.** Diagram, coverage, docs, and replay
  conformance all derive from `RADIANT_STATE_RULES`, so they cannot drift from the
  implementation — the same philosophy as grammar→`parser.c` and
  `build_lambda_config.json`→Lua in this repo.
- **Eventual native authoring in Mark/Lambda.** Long-term, the table could be authored as a
  Mark document and validated by a Lambda schema (the project already has a schema validator,
  `doc/Lambda_Validator_Guide.md`), making the FSM a first-class, re-parseable Lambda value —
  symmetric with the Mark state dump in `Radiant_Design_State_Store_Dump.md`. Out of scope
  now, but the struct layout in §3.6 is intentionally flat so it maps cleanly to Mark later.

---

## 10. Decisions and remaining open questions

### Resolved (see "Settled decisions" at the top)

1. **Event granularity** — ✅ canonical semantic mutation events, with selection/IME first
   and direct StateStore writers normalized later (§3.3, §8 Step E).
2. **Failure mode** — ✅ hard `assert()` + abort in debug, `state.invalid` JSON, release
   transition-edge checking compiled out while resident validation keeps current behavior
   (§5.3–5.4).
3. **Invariant migration** — ✅ hybrid keep-in-parallel + differential check, then delete the
   nine `validate_*` functions during the later invariant-table migration (§8 Step G and
   "Replace vs. keep").
4. **Property/fuzz testing** — ✅ adopted as a later phase (§8 Step J).
5. **Mark/Lambda-authored schema** — ✅ documentation-only future enhancement (§9).

### Resolved (round 2)

6. **Scope of first landing** — ✅ selection + IME only (§8 Step C).
7. **Document-lifecycle family** — ✅ defer the missing `DocState` lifecycle field until
   navigation/document activation work needs it (§8 Step I).
8. **Table layout** — ✅ a single `RADIANT_STATE_RULES` array, sectioned by comment banners,
   indexed by `[family][event]` at startup only if scan cost matters (§3.7, §5.3).
9. **Diagram/coverage tooling timing** — ✅ add coverage for implemented families immediately
   after the first checker slice, with diagrams in the same step or directly after (§8 Step D).
