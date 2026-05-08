# Radiant Interactive State Machine and Event/State Log

**Status**: Design

**Date**: 2026-05-08

**Scope**: Radiant interactive UI state, state-machine consistency, JSON event/state logging, replay, and diagnostics.

**Source proposals**:
- [Radiant_State_Machine_GPT.md](../idea/Radiant_State_Machine_GPT.md) — primary design.
- [Radiant_State_Machine_Opus.md](../idea/Radiant_State_Machine_Opus.md) — incorporated FSM details for focus, selection, IME.

**Related design docs**: `Radiant_Design_State.md`, `Radiant_Design_Event.md`, `Radiant_Design_Selection.md`, `Radiant_Design_Form_Input.md`, `Radiant_WebDriver.md`.

## Goal

Radiant already has a central `RadiantState` / `StateStore` in [radiant/state_store.hpp](../../radiant/state_store.hpp), plus event simulation and WebDriver-oriented automation paths. This design tightens that architecture so all interactive state is owned by the StateStore and mutated through explicit transition APIs.

The intended result:

- one document-level owner for focus, caret, selection, hover, active press, drag, context menu, dropdown, scroll, zoom, visited links, navigation history, and form-control state;
- state-machine transitions for focus, caret, and selection so invalid combinations cannot persist after an event cascade;
- a structured JSON event/state log, separate from `log.txt`, that supports debugging, replay, tests, and external monitoring;
- stable enough element identifiers for diagnostics and replay, including static documents and future dynamic documents.

## Design Principles

- StateStore is the single durable owner of UI state.
- Event dispatch, default actions, layout, and render may read state, but only transition APIs write major interaction state.
- Transitions validate invariants before and after mutation.
- Pseudo-state bits on `DomElement` / `View` are derived mirrors of StateStore state, not separate truth.
- DOM Selection remains the canonical document selection model; legacy `CaretState` / `SelectionState` become compatibility views.
- Logging is JSON Lines by default: append-only, streamable, and resilient if a process crashes mid-run.
- Replay logs must capture logical input and stable targets, not merely physical coordinates.
- Diagnostic logs may include runtime IDs and coordinates, but replay should prefer stable element references.
- Each document gets its own log file so iframe / multi-document sessions remain diff-friendly.

## Proposed Architecture

```text
raw platform input / event_sim / WebDriver
                |
                v
        event dispatcher (radiant/event.cpp)
                |
                v
   transition layer (radiant/state_machine.cpp) ──► JSON event/state log
                |
                v
          RadiantState (StateStore)
                |
                v
         layout / render observe state
```

Add a small state-machine layer beside `state_store.cpp`:

```cpp
// radiant/state_machine.hpp
typedef enum FocusTransitionKind { ... } FocusTransitionKind;
typedef enum SelectionTransitionKind { ... } SelectionTransitionKind;
typedef enum CaretTransitionKind { ... } CaretTransitionKind;

bool focus_transition(RadiantState* state, FocusTransitionKind kind, FocusTransitionArgs* args);
bool selection_transition(RadiantState* state, SelectionTransitionKind kind, SelectionTransitionArgs* args);
bool caret_transition(RadiantState* state, CaretTransitionKind kind, CaretTransitionArgs* args);

bool radiant_state_validate_interaction(RadiantState* state, StateValidationReport* report);
```

The existing `focus_set`, `focus_clear`, `caret_set`, `selection_start`, `selection_extend`, and text-control APIs become wrappers around this layer during migration. Call sites do not need to be rewritten all at once.

### Single-writer rule

Each consolidated piece of state gets exactly one writer module. Other code calls into that module through a public API. The writer module is responsible for:

- enforcing invariants;
- bumping `state->version`;
- invoking change callbacks;
- emitting structured log records;
- dirtying the appropriate region(s).

Direct field access (`state->focus->current = v`) is forbidden; a debug-build assertion via a `MutatorToken` parameter on internal setters enforces this (mirroring Blink's `PassKey<FocusController>` pattern).

```
focus.cpp        → focus_request(view, FocusCause)
                   focus_clear(FocusCause)
dom_range.cpp    → selection_set(anchor, focus, dir, SelectionCause)
                   selection_collapse_to(boundary, SelectionCause)
                   selection_clear(SelectionCause)
ime.cpp          → ime_begin_composition / ime_update / ime_commit
form_control.cpp → form_set_value / form_set_checked / ...
```

## StateStore Ownership

### Document-level state

Stored once per `RadiantState`:

- document lifecycle: current document id, current URL, load state, active/inactive/unloading;
- navigation: history entries, current history index, scroll restoration per history entry;
- visited links: existing `VisitedLinks`, scoped by user profile or session;
- focus: one current focus target per document, plus previous focus and focus-visible metadata;
- selection: one `DomSelection` singleton per document;
- caret: a projection of collapsed selection or active text control selection;
- input modality: last input source, user activation timestamp, composition state;
- pointer state: cursor, hover target, active/pressed target, drag/drop;
- transient UI: open dropdown, context menu, IME candidate/preedit, popover/dialog state when added;
- stats: latest layout/render timings, memory counters, dirty region counters.

### Per-element state

Stored through `state_map` keyed by element/node plus a state name:

- pseudo classes: `:hover`, `:active`, `:focus`, `:focus-within`, `:focus-visible`, `:visited`, `:checked`, `:disabled`, `:required`, `:invalid`, `:placeholder-shown`, etc.;
- form values: `value`, `dirty-value`, `checked`, `selected-index`, `selected-options`, `selection-start`, `selection-end`, `selection-direction`;
- editing state: composition text, composition range, undo group id, pending input type;
- scroll offsets for scrollable elements;
- media state where useful for dev tools: paused, current time, buffered ranges.

The `DomElement::pseudo_state` bitfield remains as a fast render/style cache, but updates flow from StateStore transition functions.

## Focus State Machine

### States

```text
NoDocument
DocumentInactive
DocumentActiveNoFocus
ElementFocused
TextControlFocused
ContentEditableFocused
SubdocumentFocused
```

`FocusState::current` remains the compact storage; the state machine treats it as one of these logical states.

### Causes (drives `:focus-visible`)

`MOUSE`, `KEYBOARD_TAB`, `KEYBOARD_SHORTCUT`, `SCRIPT`, `INITIAL`, `AUTOFOCUS`, `RESTORATION`.

### Invariants

- A document has at most one focused area.
- If focus is on a text-editable control, caret/selection state must be compatible with that control.
- If focus changes away from a text-editable control, any uncommitted form change is finalized before blur events are dispatched.
- `:focus` applies only to the focused element.
- `:focus-within` applies exactly to ancestors of the focused element.
- `:focus-visible` is derived from input modality or explicit focus options.
- Key events route to the focused DOM anchor, falling back to body/document only through a single function.
- `focus.current == NULL || (view_is_alive(focus.current) && view_is_focusable(focus.current))` — checked at end of every cascade.

### Transitions

| Transition | From | To | Notes |
|------------|------|----|-------|
| `document_load` | `NoDocument` | `DocumentActiveNoFocus` | creates document id, clears stale element state |
| `document_unload` | any | `NoDocument` | emits blur/unload cleanup, clears caret/selection targets |
| `focus_element` | active doc | `ElementFocused` | validates focusability, updates pseudo states |
| `focus_text_control` | active doc | `TextControlFocused` | initializes caret and text-control selection |
| `focus_contenteditable` | active doc | `ContentEditableFocused` | initializes DOM selection if empty |
| `focus_subdocument` | active doc | `SubdocumentFocused` | parent focus target is iframe/container, child owns inner focus |
| `blur_current` | focused | `DocumentActiveNoFocus` | clears `:focus`, may collapse/clear caret depending target |
| `tab_forward/backward` | active doc | focused/no focus | uses a single sequential focus navigation routine |
| `view_removed(v)` | any | adjusted | auto-blur if `v == focus.current` |

Focus transitions own blur/focus event ordering. Callers request a focus target; they do not manually set pseudo states or caret side effects.

## Selection and Caret State Machine

Caret and selection are modeled together because every document has one selection and a collapsed selection is the caret position.

### States

```text
SelectionEmpty
CaretCollapsed
RangeSelectedForward
RangeSelectedBackward
PointerSelecting
KeyboardExtending
ImeComposing
```

The canonical representation is `DomSelection` plus optional active text-control selection. Legacy `CaretState` and `SelectionState` remain derived projections for rendering and old call sites.

A simplified state graph for the user-gesture layer:

```text
   ┌─────────┐  collapse(b)         ┌────────────┐ extend(b)  ┌──────────┐
   │  None   │ ─────────────────►   │  Caret(b)  │ ─────────► │ Range    │
   └────▲────┘                       └────┬───────┘            │ (a→f)    │
        │ clear()                         │ collapse(b')        └────┬─────┘
        │                                 ▼                          │
        └─────────────────────────────────┴──── press_down_in_range ─┘
                                          ▲                          │
                                          │ mouse_up                 ▼
                                          └──────── Selecting(a, current) ◄─┐
                                                       │ pointer_move       │
                                                       └────────────────────┘
```

### Core invariants

- Every document has exactly one `DomSelection` object once selection is requested.
- That selection has zero or one range, matching the Selection API direction.
- A collapsed selection has equal start/end boundary points and defines the caret.
- A non-collapsed selection has valid anchor and focus points whose document roots are compatible.
- Selection endpoints are normalized into a valid range: start is before or equal to end, while direction preserves anchor/focus intent.
- A selection with `is_selecting` true must have an active pointer or keyboard extension transition in progress.
- The whole document never has two independent caret positions. Text controls expose `selectionStart` / `selectionEnd`, but StateStore tracks which text control is active.
- If the focused target is not editable, caret is hidden and text-edit selection is inactive.

### Transitions

| Transition | From | To | Notes |
|------------|------|----|-------|
| `collapse_to_boundary` | any | `CaretCollapsed` | validates node/offset, replaces selection range |
| `start_pointer_selection` | empty/caret/range | `PointerSelecting` | fires cancelable `selectstart` before mutation |
| `extend_to_boundary` | selecting/range/caret | range or caret | updates focus, preserves anchor, sets direction |
| `select_all` | active editable/doc | `RangeSelectedForward` | one canonical operation for Ctrl+A and API select all |
| `collapse_to_start/end` | range | `CaretCollapsed` | updates caret projection |
| `clear_selection` | any | `SelectionEmpty` or `CaretCollapsed` | prefer collapsed caret on user click in editable region |
| `begin_composition` | `TextControlFocused` or `ContentEditableFocused` | `ImeComposing` | pins composition range and caret |
| `update_composition` | `ImeComposing` | `ImeComposing` | validates preedit range and caret offset |
| `commit_composition` | `ImeComposing` | `CaretCollapsed` | inserts committed text and emits input/change events |
| `cancel_composition` | `ImeComposing` | previous caret/range | restores pre-composition state |

### Coupling rules

- `focus_text_control` must create or restore a caret within that control.
- `blur_current` from a text control must close IME composition first.
- `caret_set` is equivalent to `selection.collapse()` and routes through the selection machine.
- `selection_extend` never directly mutates legacy offsets; it updates `DomSelection`, then derives the legacy projection.
- `selectionchange` events are scheduled from the state-machine mutation envelope, not scattered call sites.

## IME Composition

`Idle → Composing(text, range) → Committed → Idle`. The composition range is held by the selection machine in a substate; IME inputs are the only writers. This eliminates the current platform-specific state in `ime_mac.mm` / `ime_win.cpp`.

## Form Control State

Form control rendering already stores static attributes and some mutable fields in `FormControlProp`. Mutable form state becomes StateStore-backed:

- text value, dirty value flag, selection start/end/direction;
- checkbox/radio checked state, including radio group exclusivity;
- select selected option state;
- disabled/readonly/required/validity state;
- file input selected file list;
- range value and drag state.

`FormControlProp` keeps computed/static properties: control type, intrinsic sizes, min/max/step defaults, placeholder, parsed attributes. StateStore owns user-mutated state.

Radio groups are a good example of why this matters: checking one radio button is a group transition, not a field assignment. The transition atomically unchecks the previous radio in the same group, updates pseudo states, marks dirty paint/style, and emits one event/state log snapshot.

## Navigation, URL, and Visited State

Add a `NavigationState` owned by `RadiantState`:

```cpp
typedef struct NavigationEntry {
    uint64_t entry_id;
    Str* url;
    Str* title;
    float scroll_x, scroll_y;
    uint64_t document_id;
} NavigationEntry;

typedef struct NavigationState {
    ArrayList* entries;
    int current_index;
    bool can_go_back, can_go_forward;
    uint64_t next_navigation_id;
} NavigationState;
```

Visited links remain privacy-preserving. Logs record whether a URL was classified as visited but avoid dumping the raw visited-link hash set unless an explicit debug flag is enabled.

Recommended events: `document.load_start`, `document.load_commit`, `document.dom_ready`, `document.load_complete`, `document.unload_start`, `document.unload_complete`, `navigation.history_push`, `navigation.history_replace`, `navigation.history_traverse`, `navigation.fragment_change`, `link.visited_add`.

This mirrors WebDriver BiDi's navigation event model so future tooling can map Radiant logs to automation concepts.

## Event Cascade Session

Define an event cascade session as the work triggered by one external cause:

- platform input event;
- event simulator command;
- WebDriver action tick;
- document load/unload;
- timer/task callback;
- script-initiated DOM event if Radiant exposes JS-driven interaction.

Each cascade has:

- `cascade_id`: monotonic per process;
- `cause`: `input`, `webdriver`, `event_sim`, `navigation`, `timer`, `script`, `internal`;
- raw input details where applicable;
- hit-test result before dispatch;
- dispatched DOM/UI events and default actions;
- state transitions;
- dirty/reflow/layout/render work;
- final state snapshot.

A single function `radiant_state_settle(state)` runs after every top-level input event has finished bubbling, in this order:

1. drain pending DOM mutations,
2. resolve focus FSM (auto-blur removed views, restore focus if needed),
3. resolve selection FSM (clamp boundaries to surviving nodes, collapse if anchor==focus),
4. resolve form-control commit/blur side-effects,
5. assert invariants (debug),
6. emit `state.snapshot` log record,
7. run reflow/repaint.

This is the natural boundary for consistency checks. At `cascade_end`, call `radiant_state_validate_interaction()` and log either `state.validated` or `state.invalid` with details.

## JSON Event and State Log

### File and format

Use a new log channel, separate from `log.txt`. Log files are scoped per document so concurrent sessions and iframes do not interleave:

```text
./temp/events_${pid}_${doc_name}.jsonl
```

`${doc_name}` is derived from the document's URL/path (basename, sanitized). When a document is loaded into an iframe, that subdocument writes to its own file. Per-process uniqueness is provided by `${pid}`.

JSON Lines is preferred over a single JSON array because it is append-only, streamable, and readable after crashes. A future command can convert JSONL to a single trace JSON if needed.

The log is disabled unless enabled by CLI flag or config:

```bash
./lambda.exe view page.html --event-log
./lambda.exe layout page.html --event-log
./lambda.exe run-event-sim page.html scenario.json --event-log
```

### Session start record

Per-document URL and other one-shot metadata are written once at the top of each file, so subsequent records do not need to repeat the URL:

```json
{
  "v": 1,
  "type": "session_start",
  "pid": 12345,
  "document": {
    "id": "doc-7",
    "url": "file:///project/test.html",
    "epoch": 3,
    "title": "Test Page"
  },
  "viewport": { "w": 1280, "h": 800 },
  "zoom": 1.0,
  "time": { "mono_ms": 0.0, "wall_ms": 1778123456789 },
  "build": { "version": "...", "commit": "..." }
}
```

### Event envelope

Every subsequent record uses a compact envelope (no URL):

```json
{
  "v": 1,
  "seq": 42,
  "time": { "mono_ms": 128.415, "wall_ms": 1778123456789 },
  "doc": "doc-7",
  "cascade": { "id": "cas-12", "cause": "input" },
  "type": "state.snapshot",
  "data": {}
}
```

### Required event families

| Family | Purpose |
|--------|---------|
| `document.*` | load, commit, unload, URL/title changes |
| `input.*` | raw keyboard/mouse/scroll/IME/WebDriver/event-sim input |
| `hit_test.*` | target before dispatch and coordinate mapping |
| `dom_event.*` | DOM/UI event dispatch start/end/cancel/default-prevented |
| `default_action.*` | focus, click activation, text insertion, checkbox toggle, history traversal |
| `state.transition` | focus/caret/selection/form/navigation transition result |
| `state.snapshot` | final snapshot after cascade |
| `layout.stats` | layout timing, node counts, dirty/reflow info |
| `render.stats` | render timing, draw op counts, surface memory, dirty region info |
| `memory.stats` | pool/arena/GC/memtrack summary where available |
| `replay.marker` | markers for deterministic replay and assertions |
| `logger.warning` | dropped records, buffer wrap, schema errors |

A pointer event therefore produces a tree like:

```
ev-0042 input.pointer (button=down, x=200, y=80, target=elt-17)
  ├─ fsm.focus      (NoFocus → Focused(elt-17), cause=MOUSE)
  ├─ fsm.selection  (None → Caret(elt-17, off=4))
  ├─ stat.layout    (full=false, dirty_nodes=3, dur_us=812)
  ├─ stat.render    (tiles=2, dur_us=410)
  └─ snapshot       (focus, caret, selection, hover after settle)
```

### State snapshot

At the end of each cascade, log a compact snapshot:

```json
{
  "type": "state.snapshot",
  "data": {
    "hit_target": {
      "node": { "id": 781, "stable_id": "src:124:32:button", "path": "html.1.body.3.button.0" },
      "x": 142.0,
      "y": 88.0
    },
    "focus": {
      "state": "TextControlFocused",
      "target": { "id": 912, "stable_id": "id:search", "path": "html.1.body.5.input.0" },
      "focus_visible": true,
      "from_keyboard": true
    },
    "caret": {
      "state": "CaretCollapsed",
      "target": { "id": 912, "stable_id": "id:search" },
      "offset": 4,
      "line": 0,
      "column": 4,
      "rect": { "x": 62.0, "y": 9.0, "w": 1.0, "h": 17.0 }
    },
    "selection": {
      "state": "CaretCollapsed",
      "anchor": { "node": { "id": 912 }, "offset": 4 },
      "focus": { "node": { "id": 912 }, "offset": 4 },
      "direction": "none",
      "is_collapsed": true
    },
    "document_state": {
      "scroll_x": 0.0,
      "scroll_y": 120.0,
      "history_index": 2
    }
  }
}
```

Snapshots may be delta-coded against the previous snapshot (only changed fields included) with a periodic full baseline every N cascades to bound replay cost.

### Layout, render, and memory stats

Log stats after layout/render or at cascade end if work happened:

```json
{
  "type": "layout.stats",
  "data": {
    "duration_ms": 3.42,
    "style_ms": 0.91,
    "layout_ms": 2.21,
    "nodes": 184,
    "text_runs": 53,
    "dirty_scope": "subtree",
    "reflow_requests": 2
  }
}
```

```json
{
  "type": "render.stats",
  "data": {
    "duration_ms": 1.87,
    "backend": "thorvg",
    "draw_ops": 241,
    "dirty_rects": 3,
    "surface_bytes": 3840000
  }
}
```

Memory fields use existing memtrack/pool/arena counters where available. If exact counters are not available, start with coarse data and log `"source":"unavailable"` for missing fields rather than inventing values.

### Implementation notes

- The event/state log is emitted through the existing `lib/log.c` infrastructure rather than a separate writer. A dedicated category (e.g. `event_state`) is registered in `log.conf` with its own format and `./temp/events_${pid}_${doc_name}.jsonl` output file. The category's format pattern is set to `%m%n` (message + newline only) so each call writes one bare JSON object per line, no timestamp/level/category prefix.
- A small JSON helper layer (`radiant/event_state_log.{hpp,cpp}`) builds a record into a stack/scratch buffer, then emits it via a single `clog_info(event_state_cat, "%s", json_line)` call. The helper handles escaping, numbers, and nested objects; call sites do not hand-format JSON.
- One log file per document: at document load, a per-document `log_category_t` is created (or reconfigured) so its `output` points at the document-specific file. Existing `log.txt` and other categories are unaffected.
- `stat.*` records can be sampled (e.g. every 10th layout) when the engine is under load.
- Privacy: `input.key` records the key code, not the resulting character, when the focused control is `type=password` or has `autocomplete=off`. URLs in `document.*` records may be hashed under an opt-in flag.
- Disabled by default: the category starts with `enabled = 0` and is only turned on when `--event-log` is passed. When off, the existing `log.c` early-out (`!category->enabled`) means no formatting and no syscalls.

## Element Identity

Radiant uses a multi-part element reference instead of a single ID scheme.

### Layered node identity

```json
{
  "id": 912,
  "stable_id": "id:search",
  "path": "html.1.body.5.input.0",
  "source": { "byte": 428, "length": 17 },
  "tag": "input",
  "author_id": "search",
  "classes": ["query"]
}
```

Definitions:

- `id`: monotonic per document epoch, assigned when the DOM node is created. Lives directly on `DomNode` as a `uint32_t` field. Good for exact diagnostics inside one run.
- `stable_id`: best-effort replay identity. Priority order:
  1. explicit `data-rid` / `id` attribute,
  2. durable template/render-map key,
  3. source position (when document loaded from a recorded source and node has not been DOM-mutated),
  4. path plus tag signature,
  5. accessible-name fingerprint as secondary hint only.
- `path`: structural child path from document root, including tag names and sibling indexes.
- `source`: optional parser-provided byte/length range. The document URL is recorded once in `session_start` and not repeated here.
- `author_id`: raw HTML `id` if present.

For replay, target resolution tries:

1. exact `stable_id` match;
2. same source span and tag;
3. same path and tag;
4. CSS selector fallback if logged;
5. coordinate fallback only when explicitly allowed.

This gives static documents stable replay, dynamic documents useful diagnostics, and future dev tools a clear element handle. The shape mirrors Chrome DevTools Protocol and WebDriver BiDi: separate runtime `id`, stable `stable_id`, and structural `path`.

### Mutation handling

When the DOM mutates, the writer module emits a `dom.mutation` record that includes both ids of removed/added nodes, so a downstream replay tool can keep its id table in sync.

## Replay Design

Replay consumes the same input records emitted by the event log, not a separate format. A replay runner can filter records:

- use `document.*` to load the same initial URL (read once from `session_start`);
- use `input.*` and `replay.marker` records as commands;
- use stable element refs for target resolution;
- compare final `state.snapshot`, layout stats thresholds, or explicit assertion records.

```bash
./lambda.exe replay --event-log ./temp/events_12345_test.jsonl --assert-state
```

Replay must distinguish deterministic and diagnostic fields:

- deterministic: input type, logical key, text, modifiers, target stable id, wheel delta, viewport size, document URL;
- diagnostic only: exact timestamps, pointer addresses, render duration, memory usage, absolute coordinates unless no target is available.

## Validation Checks

`radiant_state_validate_interaction()` checks:

- state has no focus target after document unload;
- focused target belongs to the active document epoch;
- only focused target has `STATE_FOCUS` true;
- `STATE_FOCUS_WITHIN` matches ancestors of focused target;
- caret target is null or editable/focused;
- caret offset is within target text length;
- selection has zero or one canonical range;
- selection endpoints belong to compatible roots;
- non-collapsed selection has ordered start/end and valid direction;
- legacy `CaretState` / `SelectionState` projection matches `DomSelection`;
- text-control active element matches focus state;
- open dropdown/context menu targets are live and visible;
- dirty flags are set when state changes imply repaint/reflow.

In debug builds, failed validation logs `state.invalid` to JSON and `log_error()` a concise text message. In release builds, invalid state is still reported to JSON if logging is enabled, but with minimal overhead.

## Implementation Plan

### Phase 1: Structured logger

- Add `radiant/event_state_log.hpp` and `radiant/event_state_log.cpp`.
- Reuse `lib/log.c`: register a dedicated `event_state` category with a `%m%n` format and a per-document output file `./temp/events_${pid}_${doc_name}.jsonl`. Use `clog_info` / `clog_debug` to emit one JSON object per line.
- Add a small JSON helper (escape, number, object/array open/close) that builds into a fixed scratch buffer, then hands the resulting string to the log lib.
- Support enabling via CLI flag (`--event-log`); default disabled (category `enabled = 0`).
- Add envelope writer, category filtering, string escaping, and dropped-record accounting (the latter via `logger.warning` records).
- Emit `session_start` once per document with URL and metadata.
- Log document load/unload, raw input, hit target, layout stats, render stats, and cascade-end state snapshot.
- Keep `log.txt` unchanged for existing human debug logs.

### Phase 2: Element identity

- Add `id` field directly on `DomNode` (assigned at creation time, monotonic per document epoch).
- Reuse source position tracking where available (no URL embedded in element refs).
- Add structural path generation using the existing render-map / source-path recorder concepts.
- Create one serializer for `ElementRef` so event sim, WebDriver, debug logs, and state logs share the same identity format.

### Phase 3: Event cascade boundary

- Add `state_begin_event_cascade()` / `state_end_event_cascade()` and `radiant_state_settle()`.
- Give all platform/event-sim/WebDriver input paths a cascade id.
- At cascade end, validate state invariants and emit one snapshot.
- Coalesce `selectionchange`, layout, render, and dirty stats under the cascade.

### Phase 4: Focus machine

- Wrap `focus_set`, `focus_clear`, and `focus_move` in focus transitions.
- Move pseudo-state updates into the transition implementation.
- Add validation for one focus target, exact `:focus-within` ancestry, and focus/caret compatibility.
- Log `state.transition` records for focus changes.

### Phase 5: Selection and caret machine

- Make `DomSelection` the only canonical mutation target.
- Convert legacy `caret_*` and `selection_*` calls into selection-machine transitions.
- Derive `CaretState` / `SelectionState` after canonical mutation.
- Centralize `selectstart` and `selectionchange` scheduling.
- Add validation for endpoint roots, offset bounds, direction, collapsed caret, and active text-control state.

### Phase 6: IME and form state migration

- Move active IME composition to `RadiantState.ime`; platform shims call only IME transitions.
- Move user-mutable form values into StateStore entries.
- Keep static/control metadata in `FormControlProp`.
- Implement radio group, checkbox, select, text input, textarea, and range transitions.
- Form transitions log old/new values in redacted mode by default for sensitive input types.

### Phase 7: Replay runner

- Add a replay command or extend event_sim to ingest `input.*` JSONL records.
- Resolve targets by layered element identity.
- Add state-snapshot assertions and layout/render threshold checks.
- Emit replay mismatch records that include expected/actual focus, caret, selection, and target ids.

Each phase is independently shippable and gated by `make test-radiant-baseline` (must remain 100% green).

## Open Questions

- Should snapshot delta granularity use per-field deltas or JSON-Patch? Start with per-field; revisit if a third-party tool consumes the log.
- Should replay logs redact text input by default? Recommended: yes for normal logs, no for explicit `--event-log-include-text` test runs.
- Should layout/render stats use the same categories as future profiler traces, so one log can later convert to Perfetto/Chrome trace format?
- Hierarchical states (statecharts) or flat FSMs? Current sketch is flat; revisit if "hover during selecting" or "ime-composing during selection" combinations grow.
- Per-iframe vs. per-document state: for now one `RadiantState` per top-level browsing session, with the `doc` field on log records distinguishing frames, and one log file per document.

## Recommended Decisions

1. Use `RadiantState` as the central StateStore; do not introduce a separate global interactive-state owner.
2. Make `DomSelection` canonical; keep `CaretState` and `SelectionState` as compatibility/render projections.
3. Add state-machine transition wrappers first, then migrate call sites incrementally.
4. Use JSON Lines under `./temp/events_${pid}_${doc_name}.jsonl`, one file per document, with `session_start` carrying the URL.
5. Use layered element identity: numeric `id` (on `DomNode`) plus `stable_id` plus `path` plus optional source span (no URL).
6. Treat replay as a first-class consumer of the log format, not a separate afterthought.
7. Borrow concepts from HTML focus, Selection API, WebDriver BiDi, and CDP tracing, but keep the implementation small and native to Radiant's existing C+ style.
