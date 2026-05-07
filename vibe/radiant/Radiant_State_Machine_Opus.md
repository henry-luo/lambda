# Radiant Interactive State Machine & Event Log — Design Proposal

**Status:** Proposal by Opus 4.7

**Prompt**
Write a proposal at ./vibe/radiant/Radiant_State_Machine.md, to enhance interactive state handing under Radiant.
1. ensure UI/radiant interactive states, like focus, caret, selection, doc history/url visited, form control states are centrally stored under the StateStore;
2. model focus, caret, selection with state machine;
to ensure ensure they are always in a consistent state.
e.g. the entire doc always has only one focus, one caret position; selection if on, always has proper start and end;
3. a new event/state log, other than current log.txt.
log major events (doc load/unload, input events), major stats (like layout, render timing and memory usage), state snapshot (hit target, focus, caret, selection) after an event cascade session.
log in JSON format.

in order to make it easy to identify the elements, may need to device a way to give each element an id.
opt 1: source/char position in the source doc. 
only works for static doc.
opt 2: p.2.span.5 // path like
any other better options?

the log serve multiple purposes:
1) for debugging/diagnosing - easier to process than the text log.
2) can be used to playback and test on the doc with the same sequence of event;
3) can dev tools/scripts to monitor the stats, etc.;

Any similar design/framework from other systems that we can reference and adopt the best design and practice? doc as prior art in the proposal.

**Scope:** `radiant/state_store.{hpp,cpp}`,
`radiant/event.cpp`, `radiant/dom_range.*`, `radiant/source_pos_bridge.*`
**Related prior art in this repo:**
[Radiant_Design_State.md](Radiant_Design_State.md),
[Radiant_Design_Event.md](Radiant_Design_Event.md),
[Radiant_Design_Selection.md](Radiant_Design_Selection.md),
[Radiant_Design_Form_Input.md](Radiant_Design_Form_Input.md),
[Radiant_UI_Automation1.md](Radiant_UI_Automation1.md)

---

## 1. Motivation

Radiant today already has a centralised [`RadiantState`](../../radiant/state_store.hpp)
holding caret, selection, focus, hover, drag, dropdown, context-menu and
visited-link state. Each of these has grown organically through
feature work (selection rework, form input, drag-and-drop, IME, video
controls), and individual sub-systems still:

1. **Mutate state directly** without a well-defined transition table.
   E.g. caret position can be updated from `text_edit.cpp`,
   `dom_range.cpp`, `event.cpp`, `text_control.cpp` and
   `ime_mac.mm` independently — so it is empirically possible to land
   in inconsistent intermediate states (caret pointing at a view that
   no longer owns focus; selection collapsed in legacy struct but
   non-collapsed in `dom_selection`; two views both flagged
   `is_focused`). Phase 6 of the selection work added a
   `dom_selection_sync_depth` re-entry guard precisely because of
   this class of bug.
2. **Spread invariants across files.** "There is exactly one
   focused element", "if selection is non-collapsed then both
   anchor_view and focus_view are non-null", "caret.view is
   editable", etc. exist as comments in headers but are not
   enforceable.
3. **Are hard to debug from `log.txt`.** The current text log mixes
   layout/render trace, GC noise, and high-level user-visible
   transitions; reproducing a reported bug ("press Tab twice, click
   the link, selection vanishes") requires reading hundreds of
   unrelated lines.
4. **Are hard to replay.** There is no machine-readable record of
   *what the user did* and *what the engine decided*, so we cannot
   feed a recorded session back into `event_sim.cpp` to reproduce a
   bug or A/B-test a fix.

This proposal does three things, in increasing order of intrusiveness:

1. **Consolidate** all interactive UI state under `RadiantState`
   (finish the migration the codebase is already mid-way through).
2. **Model** focus / caret / selection as explicit finite-state
   machines with a single mutator API and runtime invariant checks.
3. **Add** a structured JSON event/state log (`session.jsonl`) that
   captures input, decisions, stats and post-cascade snapshots —
   serving debugging, replay testing, and external monitoring.

---

## 2. Goals & Non-Goals

### Goals

* Single source of truth for every interactive state mentioned in the
  user's brief: focus, caret, selection, document history / visited
  URLs, form-control state.
* Hard invariants enforced by code, not by convention:
  *at most one focus, at most one caret, selection well-formed.*
* Deterministic, replayable recording of an interaction session
  in JSON.
* Stable element identifiers usable across reloads of the *same*
  document, robust under DOM mutation when possible.
* Zero impact on release-build hot paths when logging is off
  (compile-time gated; runtime-toggle via env var or CLI flag).

### Non-Goals

* Full undo/redo for arbitrary DOM mutations — that is a separate
  document model concern; this proposal only ensures the state store
  *can* support it (immutable mode already exists).
* Network/IPC transport of the log; we write to a local file and a
  ring buffer. A devtools front-end is out of scope.
* Re-architecting the dispatcher from
  [Radiant_Design_Event.md](Radiant_Design_Event.md); the FSMs sit
  *above* the WPT-conformant dispatcher and only react to its output.

---

## 3. Prior Art

| System | What we steal | Reference |
|---|---|---|
| **Redux / Elm Architecture** | Single store, pure reducers, time-travel via immutable versions. `RadiantState.mode == STATE_MODE_IMMUTABLE` already mirrors this. | [redux.js.org](https://redux.js.org/understanding/thinking-in-redux/three-principles) |
| **Chromium `FocusController` & `FrameSelection`** | The "exactly one focused frame; exactly one focused element per frame" invariant; the explicit *anchor / extent / direction* model on selection. | `third_party/blink/renderer/core/page/focus_controller.h`, `core/editing/frame_selection.h` |
| **WHATWG DOM Selection / Range** | Anchor / focus boundary points, `selectionchange` task coalescing — already partially done (Phase 8D `selection_mutation_seq` / `selection_event_seq`). | https://www.w3.org/TR/selection-api/ |
| **xstate / Statecharts (Harel)** | Hierarchical states with explicit `entry` / `exit` actions and guards; lets us model "selecting → idle → selected" cleanly. | https://stately.ai/docs |
| **rrweb (record-and-replay-the-web)** | JSONL event stream with full mutation + input snapshots that can be played back in a sandbox; uses incremental DOM ids assigned at first observation. | https://github.com/rrweb-io/rrweb |
| **Chrome DevTools Protocol — `Tracing`, `Page.frameStartedLoading`** | Discrete, typed event categories; per-event `params`; nanosecond timestamps. We adopt the same shape. | https://chromedevtools.github.io/devtools-protocol/ |
| **OpenTelemetry trace spans** | `traceId`/`spanId` model — every input event opens a span, all derived events (layout, paint, state mutation) are children. Lets us answer "everything that happened because the user pressed Tab". | https://opentelemetry.io/docs/specs/otel/trace/ |
| **W3C ARIA / Accessible Name and Description Computation** | Stable, content-derived element identity (heading text, label) survives reflow. Useful as a *human-readable* secondary id. | https://www.w3.org/TR/accname-1.2/ |
| **Selenium / WebDriver `element-id` & Playwright locators** | Hybrid id strategy: prefer explicit `id="…"`, fall back to a structural path. Our [Radiant_UI_Automation1.md](Radiant_UI_Automation1.md) already pursues this. | https://www.w3.org/TR/webdriver/ |
| **CSS `:focus-visible` heuristic** | Distinguish keyboard vs. mouse origin of focus — already on `FocusState.from_keyboard / from_mouse`. | https://drafts.csswg.org/selectors-4/#the-focus-visible-pseudo |

The two strongest references for this proposal are **Chromium's
FrameSelection FSM** (for the state-machine layer, §5) and **rrweb +
CDP Tracing** (for the log layer, §6).

---

## 4. State Consolidation

### 4.1 Inventory

| State | Current location | Proposed home |
|---|---|---|
| Focused element (per document) | `RadiantState.focus` ✓ | unchanged |
| Caret (text cursor) | `RadiantState.caret` + `dom_selection` (collapsed) | **single** owner: `dom_selection`; `caret` becomes a derived view (cached) |
| Selection (range) | `RadiantState.selection` (legacy) + `dom_selection` | **single** owner: `dom_selection`; legacy struct retired (Phase 7 of [Radiant_Design_Selection.md](Radiant_Design_Selection.md)) |
| Hover / active / drag / cursor | `RadiantState.hover_target` etc. ✓ | unchanged |
| Form-control values & dirty flags | `FormControlProp` per-element ✓ | unchanged; **expose** an iterator so the log layer can snapshot them |
| Document history / current URL | `BrowsingSession` (radiant/browsing_session.h) | move pointer into `RadiantState.session` for one-stop access |
| Visited URLs | `RadiantState.visited_links` ✓ | unchanged |
| Scroll, zoom | `RadiantState.scroll_x/y, zoom_level` ✓ | unchanged |
| Dropdown / context-menu overlay | `RadiantState` ✓ | unchanged |
| IME composition | scattered across `ime_mac.mm`, `ime_win.cpp` | move active composition into `RadiantState.ime` (new, see §5.4) |

### 4.2 The "single mutator" rule

Each consolidated piece of state gets exactly **one writer module**.
Other code calls into that module through a public API. The writer
module is responsible for:

* enforcing invariants (§5),
* bumping `state->version`,
* invoking change callbacks,
* emitting structured log records (§6),
* dirtying the appropriate region(s).

Concretely:

```
focus.cpp        → focus_request(view, FocusCause)
                   focus_clear(FocusCause)
dom_range.cpp    → selection_set(anchor, focus, dir, SelectionCause)
                   selection_collapse_to(boundary, SelectionCause)
                   selection_clear(SelectionCause)
ime.cpp (new)    → ime_begin_composition / ime_update / ime_commit
form_control.cpp → form_set_value / form_set_checked / ...
```

Direct field access (`state->focus->current = v`) is forbidden; a
debug-build assertion via a `MutatorToken` parameter on internal
setters enforces this. (Same idea Blink uses with
`PassKey<FocusController>`.)

---

## 5. Modeling Focus / Caret / Selection as State Machines

### 5.1 Why FSMs

The three states are deeply entangled — moving focus often moves the
caret; starting a drag-select changes selection direction; pressing
Tab while a selection exists collapses it. Encoding these as ad-hoc
`if` cascades is what produced the current re-entry bugs. A small,
explicit FSM per concern, plus a coordinator that fires transitions
in a defined order, is much easier to reason about and to test.

### 5.2 Focus FSM

```
                ┌──────────┐  blur()           ┌────────────────┐
   start ─────► │ NoFocus  │ ◄──────────────── │ Focused(view)  │
                └────┬─────┘                   └────────┬───────┘
                     │ focus(view, cause)               │ focus(other, cause)
                     │  guard: view.focusable           │  → exit Focused(view) →
                     │  guard: !view.disabled           │    enter Focused(other)
                     ▼                                  ▲
                Focused(view) ─────────────────────────┘
```

States: `NoFocus`, `Focused(view)`.
Events: `focus(view, FocusCause)`, `blur()`,
`view_removed(v)` (auto-blur if `v == focus.current`).
Causes (an enum, drives `:focus-visible`):
`MOUSE`, `KEYBOARD_TAB`, `KEYBOARD_SHORTCUT`, `SCRIPT`, `INITIAL`,
`AUTOFOCUS`, `RESTORATION`.

Entry/exit actions: dispatch `focusin`/`focus`/`blur`/`focusout` per
[Radiant_Design_Event.md](Radiant_Design_Event.md), update
`focus_visible`, repaint focus ring, scroll-into-view if cause is
keyboard.

**Invariant (debug-only assert, runs at end of every event cascade):**
`focus.current == NULL || (view_is_alive(focus.current) &&
view_is_focusable(focus.current))`.

### 5.3 Selection FSM (subsumes caret)

The DOM Selection spec already gives us anchor/focus boundary points
plus an `is_collapsed` derived flag. We layer a tiny FSM on top that
captures the *user gesture*, which legacy code currently tracks via
the `is_selecting` boolean:

```
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

States:

* **None** — no caret, no range.
* **Caret(b)** — collapsed at boundary `b` (replaces `CaretState`).
* **Range(a, f, dir)** — non-collapsed; `dir ∈ {forward, backward}`.
* **Selecting(a, current)** — active drag-select; transitions to
  `Range` or `Caret` on pointer-up.

Caret blink, IME composition, and editing all read from this single
machine. The legacy `RadiantState.caret`/`selection` structs become
**read-only cached projections** computed once per cascade, retired
when call sites are migrated (mirrors the path already laid out in
[Radiant_Design_Selection.md](Radiant_Design_Selection.md)).

**Invariants:**
* `state ∈ {Caret, Range, Selecting}` ⇒ both boundaries point at
  living, text-or-element nodes in the same Document.
* `state == Range` ⇒ `anchor != focus` (else collapse to `Caret`).
* `state == Selecting` ⇒ a primary pointer button is currently down.

### 5.4 IME Composition FSM (sketch)

`Idle → Composing(text, range) → Committed → Idle`. Composition
range is held by the selection machine in `Selecting`-equivalent
substate; IME inputs are the only writers. This eliminates the
current platform-specific state in `ime_mac.mm` / `ime_win.cpp`.

### 5.5 Coordinator: ordered transitions per cascade

A single function `radiant_state_settle(state)` runs **after** every
top-level input event has finished bubbling, in this order:

1. drain pending DOM mutations,
2. resolve focus FSM (auto-blur removed views, restore focus if
   needed),
3. resolve selection FSM (clamp boundaries to surviving nodes,
   collapse if anchor==focus),
4. resolve form-control commit/blur side-effects,
5. assert invariants (debug),
6. emit `cascade_end` log record (§6),
7. run reflow/repaint.

This makes the *order* of state propagation explicit and matches the
WHATWG "perform a microtask checkpoint" pattern.

---

## 6. Structured Event & State Log

### 6.1 Format

* **Filename:** `./temp/radiant_session_<utc-ts>.jsonl` by default
  (rule #2). Override via `RADIANT_SESSION_LOG=<path>` or
  `--session-log <path>` CLI flag.
* **Encoding:** **JSON Lines** — one JSON object per line. Append-only.
  Easy to `tail -f`, easy to filter with `jq`, robust to truncation.
  (rrweb uses the same approach; CDP `Tracing.dataCollected` is a
  superset.)
* **Top-of-file metadata** is written as the first record
  (`type:"session_start"`).
* The existing `log.txt` (textual debug trace) is preserved
  unchanged; this is an *additional*, machine-readable channel.

### 6.2 Record schema

Every record has these common fields:

```jsonc
{
  "ts":      1715030400123456,    // monotonic µs since session start
  "wall":    "2026-05-06T11:20:00.123456Z",  // ISO-8601 UTC
  "seq":     12345,               // monotonically increasing
  "trace":   "ev-0007",           // input span id (see §6.3)
  "parent":  "ev-0007",           // parent span (events nest)
  "type":    "input.key" | "fsm" | "stat" | "snapshot" | ...,
  "doc":     "doc-1",             // document/frame id
  ...                             // per-type payload
}
```

### 6.3 Event categories

The brief asks for: major events, major stats, post-cascade snapshots.

| Category | `type` values | Emitted by |
|---|---|---|
| **Session lifecycle** | `session_start`, `session_end` | main loop |
| **Document lifecycle** | `doc.load_begin`, `doc.load_end`, `doc.unload`, `doc.url_change` | `browsing_session.cpp` |
| **Input events** | `input.key`, `input.pointer`, `input.wheel`, `input.ime`, `input.paste`, `input.drop` | `event.cpp` (one record per top-level OS event; opens a `trace` span that all derived records inherit) |
| **FSM transitions** | `fsm.focus`, `fsm.selection`, `fsm.ime` | the writer module of each FSM (§4.2) |
| **Mutations** | `dom.mutation` (batched per cascade) | `dom_observer` |
| **Stats** | `stat.layout`, `stat.render`, `stat.gc`, `stat.mem` (timing in µs, byte counts) | `layout.cpp`, `render.cpp`, GC, `lambda-mem.cpp` |
| **End-of-cascade snapshot** | `snapshot` — see §6.4 | `radiant_state_settle` |
| **Diagnostics** | `assert.fail`, `error` | invariant checker, error paths |

A pointer event therefore produces a tree like:

```
ev-0042 input.pointer (button=down, x=200, y=80, target=elt-17)
  ├─ fsm.focus      (NoFocus → Focused(elt-17), cause=MOUSE)
  ├─ fsm.selection  (None → Caret(elt-17, off=4))
  ├─ stat.layout    (full=false, dirty_nodes=3, dur_us=812)
  ├─ stat.render    (tiles=2, dur_us=410)
  └─ snapshot       (focus, caret, selection, hover after settle)
```

### 6.4 Snapshot record

After every cascade we emit one `snapshot`:

```jsonc
{
  "type":   "snapshot",
  "trace":  "ev-0042",
  "doc":    "doc-1",
  "focus":  { "id": "elt-17", "cause": "MOUSE", "visible": true },
  "selection": {
    "state":  "Caret",
    "anchor": { "id": "elt-17", "off": 4, "kind": "text" },
    "focus":  { "id": "elt-17", "off": 4, "kind": "text" },
    "dir":    null
  },
  "hover":  "elt-17",
  "active": null,
  "drag":   null,
  "scroll": [0, 240],
  "zoom":   1.0,
  "viewport": [1280, 800],
  "hit_target": "elt-17"
}
```

Snapshots are **delta-coded against the previous snapshot** (only
changed fields included) with a periodic full-baseline every N
cascades to bound replay cost — same trick rrweb uses.

### 6.5 Logging implementation notes

* **Hot-path discipline.** Writers append to a lock-free SPSC ring
  buffer; a dedicated writer thread serialises to disk. Records use
  pre-interned string ids.
* **Sampling.** `stat.*` records can be sampled (e.g. every 10th
  layout) when the engine is under load.
* **Disabled by default in release.** Compile-time
  `RADIANT_SESSION_LOG=1` enables; runtime env var must also be set.
  Off ⇒ no allocations, no syscalls.
* **Privacy.** `input.key` records the **key code**, not the
  resulting character, when the focused control is `type=password`
  or has `autocomplete=off`. `doc.url_change` hashes URL by default
  unless an opt-in flag is set (matches existing `VisitedLinks`
  policy).

---

## 7. Element Identification

The brief proposes two options. Below is an analysis plus a
recommended **hybrid**:

### 7.1 Comparison

| Strategy | Pros | Cons |
|---|---|---|
| **(a) Source position** (offset in source doc, already wired by [`source_pos_bridge`](../../radiant/source_pos_bridge.hpp)) | Stable across reloads of the same source; trivially mappable back to the editor; already implemented for editor integration. | Breaks for documents constructed/mutated after load (DOM diff, `innerHTML`, generated content from JS). |
| **(b) Structural path** (`p.2.span.5`, CSS-selector-like) | Human-readable; survives cosmetic CSS/style changes. | Brittle under sibling insert/delete; ambiguous for inline-anonymous boxes; expensive to recompute. |
| **(c) Pointer / numeric handle** | Cheap; trivially unique. | Useless across reloads; cannot be diffed across runs. |
| **(d) Lazy assigned monotonic id** (rrweb) | Cheap; stable for the lifetime of *this* session; works for dynamic DOM. | Not stable across reloads (different ids next time the same page loads). |
| **(e) Explicit `id="…"` / `data-rid` attribute** | Author/test-fixture controlled; perfect stability. | Requires authored markup; absent on real-world pages. |
| **(f) ARIA accessible name fingerprint** | Survives layout changes; human-readable in logs. | Not unique; not always present; locale-dependent. |

### 7.2 Recommendation — composite id

Every record uses a **two-field id**:

```jsonc
"id":        "elt-17",           // primary: lazy monotonic (d), session-scoped
"id_stable": {                   // best-available stable id for replay/diff
   "kind":     "source",         // one of: "source" | "attr" | "path" | "name"
   "value":    { "path": "main.html", "off": 482, "kind": "element" },
   "hint":     "p > span:nth-of-type(2)"   // optional, for human reading
}
```

Resolution order when picking `id_stable`:

1. **`data-rid` / `id` attribute** if present (option e).
2. **Source position** (option a) if the document was loaded from a
   recorded source and the node has not been DOM-mutated since.
3. **Structural path** (option b) otherwise — combining tag,
   `:nth-of-type`, and `::before/::after/::marker` markers for
   anonymous boxes.
4. **Accessible name fingerprint** (option f) as a *secondary hint*,
   never as the primary, attached for human debuggability.

The lazy monotonic `id` is what the rest of the log refers to (cheap,
unique within a session); `id_stable` is what *cross-session* tools
(replay harness, regression diff) match on. This mirrors the
WebDriver / Playwright "primary handle + locator" split and rrweb's
"id + serialized snapshot" approach.

### 7.3 Mutation handling

When DOM mutates, the writer module emits a `dom.mutation` record
that includes both ids of removed/added nodes, so a downstream
replay tool can keep its id table in sync.

---

## 8. Use Cases Enabled

1. **Bug triage.** A crash report attaches `radiant_session_*.jsonl`;
   maintainer greps for the offending node id and reads a clean
   per-cascade trace instead of `log.txt`.
2. **Replay testing.** A new tool (`radiant-replay`) takes the
   `input.*` records and re-feeds them through `event_sim.cpp`
   ([Radiant_UI_Automation1.md](Radiant_UI_Automation1.md) already
   builds toward this). After replay we diff the resulting `snapshot`
   stream against the recorded one — a structural regression test
   harness.
3. **Performance monitoring.** `stat.layout` / `stat.render` records
   feed `make layout-profiling` and the profiling docs already in
   `vibe/radiant/Radiant_Layout_Profiling*.md` without bespoke
   instrumentation.
4. **Determinism audit.** Re-running the same session must produce
   the same FSM transitions; any divergence is a non-determinism
   bug.

---

## 9. Phased Plan

| Phase | Scope | Done when |
|---|---|---|
| **P1** | Inventory + retire legacy `caret` / `selection` structs in favour of `dom_selection` (continues Phase 6/7 of `Radiant_Design_Selection.md`). | All callers go through `dom_selection`; legacy fields removed from `RadiantState`. |
| **P2** | Introduce `MutatorToken` + single-writer-module rule for focus, selection, IME, form value. | Compile-time poisoning of direct field writes; `make test-radiant-baseline` green. |
| **P3** | FSM coordinator (`radiant_state_settle`) + invariant assertions. | Debug-build asserts pass on baseline + WPT events corpus. |
| **P4** | JSONL session log: schema, ring buffer, writer thread, env-var gating. | New CLI flag wired; `temp/radiant_session_*.jsonl` emitted by `make layout suite=baseline`. |
| **P5** | Composite element id + `id_stable` resolver. | `id_stable` populated for ≥99% of nodes on baseline corpus. |
| **P6** | `radiant-replay` driver against `event_sim.cpp`; snapshot diff regression test. | Self-replay on baseline corpus is byte-stable. |
| **P7** | Devtool / `jq` recipes; doc updates; `Radiant_Design_State.md` superseded by this doc on the merged sections. | Documentation-only. |

Each phase is independently shippable and gated by
`make test-radiant-baseline` (rule #9 — must pass 100%).

---

## 10. Open Questions

1. **Hierarchical states (statecharts) or flat FSMs?** Current
   sketch is flat; adding "hover during selecting" or
   "ime-composing during selection" may push toward statecharts.
   Defer until P3 reveals the need.
2. **Per-iframe vs. per-document state.** Cross-realm work is
   already on the WPT skip list; for now one `RadiantState` per
   top-level browsing session, with `doc` field on log records
   distinguishing frames.
3. **Snapshot delta granularity.** Per-field deltas vs. JSON-Patch.
   JSON-Patch is more standard but slightly verbose for Radiant's
   tiny snapshot — start with per-field delta, revisit if a
   third-party tool consumes the log.
4. **Compatibility with the existing `STATE_MODE_IMMUTABLE`.** The
   FSMs must not break time-travel; transitions in immutable mode
   produce a new `RadiantState*` and the log records the version
   number, enabling step-back debugging in the future.
