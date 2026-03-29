# Radiant UI Automation — Enhanced Event Simulator

## Problem

The current event simulator (`event_sim.cpp`) supports basic mouse/key events and a few caret/selection assertions. This is insufficient for automated UI interaction testing of HTML pages under Radiant. Key gaps:

1. **No text input** — `RDT_EVENT_TEXT_INPUT` exists in the engine but the simulator can't generate it. Can't type into form fields.
2. **No high-level actions** — clicking requires manual `mouse_down` + `mouse_up` pairs. No double-click, no "type text into field".
3. **No element targeting by selector** — only raw coordinates or `target_text` string search. Can't target `#submit-btn` or `.nav-link`.
4. **Limited assertions** — only caret view_type/char_offset, selection collapsed, and target view_type. Can't verify: text content, form values, focus, hover/active state, scroll position, visibility, element attributes.
5. **No form interaction** — no checkbox toggle, dropdown selection, radio button, or text field fill.

## Design Goals

- **Configuration-driven** — pure JSON, no scripting. Every test is a list of events and assertions.
- **Minimal** — small number of orthogonal primitives rather than many overlapping commands.
- **Backward compatible** — all existing event types continue to work unchanged.
- **Composable** — high-level actions (click, type, select) are sugar that expand to primitive events internally.

## Proposed Changes

### 1. Element Targeting — `target` field

Replace ad-hoc `target_text` with a unified `target` field available on all mouse and action events. The `target` field supports three modes:

| Mode | Syntax | Example |
|------|--------|---------|
| Coordinates | `{"x": 100, "y": 200}` | Absolute pixel position |
| Text search | `{"text": "Click here"}` | Find first visible text match |
| CSS selector | `{"selector": "#login-btn"}` | Query DOM by CSS selector |

The `x`/`y` coordinates remain top-level for backward compatibility. The `target` field takes precedence when present.

```json
{"type": "click", "target": {"selector": "#submit-btn"}}
{"type": "click", "target": {"text": "Sign In"}}
{"type": "click", "x": 200, "y": 300}
```

**Implementation**: Add `find_element_by_selector(DomDocument*, const char*)` using Radiant's existing CSS selector matching. Returns the center point of the element's border box.

### 2. New Action Events

These are high-level actions that expand to primitive events internally.

#### `click` — single click

Combines `mouse_down` + `mouse_up` at the same position. Avoids the most common boilerplate.

```json
{"type": "click", "target": {"selector": "a.nav-link"}, "button": 0}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `target` | object | — | Element target (see above) |
| `x`, `y` | int | — | Fallback coordinates |
| `button` | int | 0 | Mouse button (0=left) |
| `mods_str` | string | — | Modifier keys |

#### `dblclick` — double click

Generates two rapid `click` sequences with `clicks=2` on the second.

```json
{"type": "dblclick", "target": {"text": "Select this word"}}
```

#### `type` — text input

Types a string character-by-character via `RDT_EVENT_TEXT_INPUT` events. Automatically clicks the target first to focus it.

```json
{"type": "type", "target": {"selector": "input[name='email']"}, "text": "user@example.com"}
```

| Field | Type | Description |
|-------|------|-------------|
| `target` | object | Element to type into |
| `text` | string | Characters to type |
| `clear` | bool | If true, select-all and delete before typing (default: false) |

**Implementation**: For each character in `text`, generate a `TextInputEvent` with the Unicode codepoint. If `clear` is true, first send `Cmd+A` then `Delete`.

#### `select_option` — dropdown selection

Clicks on a `<select>` element, then clicks the matching `<option>`.

```json
{"type": "select_option", "target": {"selector": "select#country"}, "value": "US"}
{"type": "select_option", "target": {"selector": "select#country"}, "label": "United States"}
```

| Field | Type | Description |
|-------|------|-------------|
| `target` | object | The `<select>` element |
| `value` | string | Match option by `value` attribute |
| `label` | string | Match option by visible text |

#### `check` — toggle checkbox/radio

Clicks a checkbox or radio button. Optionally asserts the desired checked state.

```json
{"type": "check", "target": {"selector": "input#agree"}, "checked": true}
```

| Field | Type | Description |
|-------|------|-------------|
| `target` | object | Checkbox or radio element |
| `checked` | bool | Desired state. Only clicks if current state differs. |

#### `focus` — move focus to element

Dispatches a click on the element to give it focus, or directly sets focus.

```json
{"type": "focus", "target": {"selector": "textarea#notes"}}
```

### 3. New Assertion Events

Assertions verify state after interactions. All assertion names start with `assert_`.

#### `assert_text` — verify visible text content

Check that an element contains (or exactly matches) expected text.

```json
{"type": "assert_text", "target": {"selector": ".result"}, "contains": "Success"}
{"type": "assert_text", "target": {"selector": "h1"}, "equals": "Welcome"}
```

| Field | Type | Description |
|-------|------|-------------|
| `target` | object | Element to check |
| `contains` | string | Text must contain this substring |
| `equals` | string | Text must exactly equal this |

#### `assert_value` — verify form field value

Check the current value of an input, textarea, or select element.

```json
{"type": "assert_value", "target": {"selector": "input#email"}, "equals": "user@example.com"}
```

#### `assert_checked` — verify checkbox/radio state

```json
{"type": "assert_checked", "target": {"selector": "input#agree"}, "checked": true}
```

#### `assert_focus` — verify focused element

```json
{"type": "assert_focus", "target": {"selector": "input#email"}}
```

Passes if the currently focused element matches the target.

#### `assert_visible` — verify element visibility

```json
{"type": "assert_visible", "target": {"selector": ".modal"}, "visible": true}
```

Checks that the element has non-zero dimensions and is not `display: none` or `visibility: hidden`.

#### `assert_state` — verify arbitrary UI state

Check hover, active, or any state tracked by `RadiantState`.

```json
{"type": "assert_state", "target": {"selector": "button"}, "state": ":hover", "value": true}
{"type": "assert_state", "target": {"selector": "button"}, "state": ":active", "value": true}
```

#### `assert_scroll` — verify scroll position

```json
{"type": "assert_scroll", "x": 0, "y": 500, "tolerance": 10}
```

| Field | Type | Description |
|-------|------|-------------|
| `x`, `y` | float | Expected scroll position |
| `tolerance` | float | Allowed deviation in pixels (default: 1) |

#### `assert_caret` / `assert_selection` / `assert_target`

Unchanged from current implementation.

### 4. `wait` Enhancement

The existing `wait` event already supports arbitrary durations:

```json
{"type": "wait", "ms": 5000}
```

This sleeps for 5 seconds. No change needed — this already works.

### 5. Test Metadata and Reporting

Add optional metadata at the top level of the JSON file:

```json
{
  "name": "Login form test",
  "description": "Verify login form fills and submits correctly",
  "viewport": {"width": 1024, "height": 768},
  "events": [...]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Test name (shown in results) |
| `description` | string | Optional description |
| `viewport` | object | Set viewport size before running (width, height) |

**Exit code**: When simulation completes, exit with code 0 if all assertions pass, 1 if any fail. This enables CI integration.

## Implementation Plan

### Phase 1: Core targeting + click/type (high value)

Files to modify:

| File | Changes |
|------|---------|
| `event_sim.hpp` | Add new `SimEventType` values. Add `target` fields to `SimEvent`. |
| `event_sim.cpp` | Add CSS selector resolution (`find_element_by_selector`). Add `click`, `dblclick`, `type` event parsing and execution. Add `text_input` simulation. |

New event types to add to `SimEventType` enum:
```
SIM_EVENT_CLICK,
SIM_EVENT_DBLCLICK,
SIM_EVENT_TYPE,
SIM_EVENT_SELECT_OPTION,
SIM_EVENT_CHECK,
SIM_EVENT_FOCUS,
SIM_EVENT_ASSERT_TEXT,
SIM_EVENT_ASSERT_VALUE,
SIM_EVENT_ASSERT_CHECKED,
SIM_EVENT_ASSERT_FOCUS,
SIM_EVENT_ASSERT_VISIBLE,
SIM_EVENT_ASSERT_STATE,
SIM_EVENT_ASSERT_SCROLL,
```

New `SimEvent` fields:
```c
// Target resolution (unified)
char* target_selector;    // CSS selector string
char* target_text;        // text content search (existing)

// Type action
char* input_text;         // text to type
bool clear_first;         // select-all + delete before typing

// Select option
char* option_value;       // match by value attribute
char* option_label;       // match by visible text

// Checkbox
bool expected_checked;    // desired checked state

// Assert text
char* assert_contains;    // substring match
char* assert_equals;      // exact match

// Assert state
char* state_name;         // e.g. ":hover", ":active"
bool expected_bool_value; // expected boolean value

// Assert scroll
float expected_scroll_x, expected_scroll_y;
float scroll_tolerance;
```

### Phase 2: Assertions

Add `assert_text`, `assert_value`, `assert_checked`, `assert_focus`, `assert_visible`, `assert_state`, `assert_scroll`.

Each reads the relevant state from `RadiantState` / DOM and compares against expected values.

### Phase 3: Form controls + metadata

Add `select_option`, `check`, `focus`, viewport configuration, and exit code reporting.

## Complete Example: Login Form Test

```json
{
  "name": "Login form test",
  "events": [
    {"type": "log", "message": "=== Login form test ==="},
    {"type": "wait", "ms": 200},

    {"type": "type",
     "target": {"selector": "input[name='username']"},
     "text": "admin"},

    {"type": "type",
     "target": {"selector": "input[name='password']"},
     "text": "secret123"},

    {"type": "assert_value",
     "target": {"selector": "input[name='username']"},
     "equals": "admin"},

    {"type": "check",
     "target": {"selector": "input#remember"},
     "checked": true},

    {"type": "assert_checked",
     "target": {"selector": "input#remember"},
     "checked": true},

    {"type": "click",
     "target": {"selector": "button[type='submit']"}},

    {"type": "wait", "ms": 500},

    {"type": "assert_text",
     "target": {"selector": ".welcome-msg"},
     "contains": "Welcome, admin"},

    {"type": "log", "message": "=== Login test complete ==="}
  ]
}
```

## Complete Example: Text Selection Test

```json
{
  "name": "Text selection test",
  "events": [
    {"type": "wait", "ms": 200},

    {"type": "dblclick", "target": {"text": "Hello"}},
    {"type": "assert_selection", "is_collapsed": false},
    {"type": "assert_caret", "view_type": 1},

    {"type": "click", "target": {"text": "World"}},
    {"type": "assert_selection", "is_collapsed": true},

    {"type": "render", "file": "./temp/selection_test.png"}
  ]
}
```

## Complete Example: Link Navigation Test

```json
{
  "name": "Link click test",
  "events": [
    {"type": "wait", "ms": 200},

    {"type": "click", "target": {"selector": "a#about-link"}},
    {"type": "wait", "ms": 100},
    {"type": "assert_state", "target": {"selector": "a#about-link"}, "state": ":visited", "value": true},

    {"type": "click", "target": {"text": "Contact Us"}},
    {"type": "wait", "ms": 100},
    {"type": "assert_scroll", "y": 800, "tolerance": 50}
  ]
}
```

## Summary of All Event Types

### Action Events

| Event | Purpose | Key Fields |
|-------|---------|------------|
| `wait` | Pause execution | `ms` |
| `click` | **NEW** Single click | `target`, `button`, `mods_str` |
| `dblclick` | **NEW** Double click | `target` |
| `type` | **NEW** Type text into element | `target`, `text`, `clear` |
| `select_option` | **NEW** Select dropdown option | `target`, `value` or `label` |
| `check` | **NEW** Toggle checkbox/radio | `target`, `checked` |
| `focus` | **NEW** Focus an element | `target` |
| `mouse_move` | Move cursor | `x`, `y`, `target` |
| `mouse_down` | Press mouse button | `x`, `y`, `target`, `button`, `mods_str` |
| `mouse_up` | Release mouse button | `x`, `y`, `button` |
| `mouse_drag` | Drag between positions | `from_x/y`, `to_x/y` |
| `key_press` | Press + release key | `key`, `mods_str` |
| `key_down` | Press key | `key` |
| `key_up` | Release key | `key` |
| `key_combo` | Key with modifiers | `key`, `mods_str` |
| `scroll` | Scroll at position | `x`, `y`, `dx`, `dy` |

### Assertion Events

| Event | Purpose | Key Fields |
|-------|---------|------------|
| `assert_caret` | Verify caret state | `view_type`, `char_offset` |
| `assert_selection` | Verify selection | `is_collapsed` |
| `assert_target` | Verify target view | `view_type` |
| `assert_text` | **NEW** Verify element text | `target`, `contains` or `equals` |
| `assert_value` | **NEW** Verify form value | `target`, `equals` |
| `assert_checked` | **NEW** Verify checkbox state | `target`, `checked` |
| `assert_focus` | **NEW** Verify focus | `target` |
| `assert_visible` | **NEW** Verify visibility | `target`, `visible` |
| `assert_state` | **NEW** Verify UI state | `target`, `state`, `value` |
| `assert_scroll` | **NEW** Verify scroll position | `x`, `y`, `tolerance` |

### Utility Events

| Event | Purpose | Key Fields |
|-------|---------|------------|
| `log` | Print message to stderr | `message` |
| `render` | Capture to PNG/SVG | `file` |
| `dump_caret` | Dump caret state | `file` |

---

## Detailed Phased Testing Plan

This plan progressively grows the UI automation test coverage in four phases,
starting from the most foundational interactions and working toward composited,
drag-based interactions. Each phase builds on the reliability established by
the previous one.

> **Important**: The Radiant event system and hit-testing are not yet fully
> robust. Mouse coordinates computed from the view tree may not always match
> what the layout engine places on screen, especially for inline, table, or
> flex elements. Tests in early phases should be forgiving and should verify
> only what the engine demonstrably supports.

---

### Baseline-First Strategy

**HTML files must be layout-verified before being used for UI interaction
tests.** If the layout is wrong, there is no point testing UI interaction on
top of it — incorrect element positions will produce misleading results.

**Rule**: Use HTML files from `test/layout/data/baseline/` as the primary
source for UI automation tests. These files are already verified against a
real browser (Chrome) via the layout regression suite (`make
test-radiant-baseline`). Their expected element positions are recorded in
`test/layout/reference/*.json`.

If you need to write a new HTML file for a UI scenario not covered by the
baseline suite, keep it minimal and run it through the layout comparison tool
(`make layout test=<name>`) to confirm it matches browser output before using
it in UI tests.

#### Coordinate derivation

Element coordinates used in JSON event files are derived from the
corresponding reference JSON in `test/layout/reference/`. The reference JSON
stores the browser-recorded absolute CSS-pixel positions. The viewport is
**1200 × 800 CSS pixels** (set in `radiant/ui_context.cpp`).

To extract positions from a reference JSON, use the helper script:

```sh
python3 temp/al2.py test/layout/reference/baseline_809_text_align.json
```

#### Current test mapping

| Phase | HTML (from baseline/) | Event JSON (test/ui/) | Key assertions |
|-------|-----------------------|-----------------------|----------------|
| 1a | `baseline_201_font_sizes.html` | `ui_phase1a_font_sizes.json` | `assert_visible`, `assert_text` on font-size paragraphs — no mouse |
| 1b | `baseline_809_text_align.html` | `ui_phase1b_text_align.json` | `click` by text, `assert_caret` on block paragraphs |
| 2a | `baseline_502_text_wrapping.html` | `ui_phase2a_text_wrapping.json` | `click` on wrapped text, `assert_caret` |
| 2b | `baseline_503_styled_spans.html` | `ui_phase2b_styled_spans.json` | `assert_visible` on inline spans, `click` by selector and text |
| 3a | `baseline_809_text_align.html` | `ui_phase3a_drag_select.json` | `mouse_drag`, `assert_selection is_collapsed: false` |

#### How UI tests specify their HTML target

Every UI test JSON file must contain a top-level `"html"` field with the path
to its HTML file (relative to the project root). The GTest runner
(`test/test_ui_automation_gtest.cpp`) reads this field with
`extract_html_from_json()` to determine which page to load.

```json
{
  "name": "Phase 1b: text align click test",
  "html": "test/layout/data/baseline/baseline_809_text_align.html",
  "events": [...]
}
```

The HTML path can point anywhere in the repo:
- Baseline layout files: `"html": "test/layout/data/baseline/baseline_809_text_align.html"`
- Co-located test HTML: `"html": "test/ui/test_click_elements.html"`

JSON files without a valid `"html"` field (or whose HTML file doesn't exist)
are skipped by the GTest runner.

#### Running the tests

```sh
make build-test                                      # compile test executable
make test-ui-automation                              # run all test/ui/*.json tests
./test/test_ui_automation_gtest.exe --gtest_filter=UIAutomation.phase1a*
```

---

### Phase 1 — Mouse Clicking and Hover Events

**Goal**: Verify that single clicks land on the correct elements and that hover
state changes are reflected in the view tree.

**Prerequisites / known limitations**

- Hit-testing (`handle_mouse_button`) walks the block view tree. Inline boxes,
  text runs inside flex items, and elements inside table cells may not be
  hit-tested correctly until the view traversal in `lambda-eval.cpp` is
  up-to-date with all layout types.
- `get_element_center_abs()` in `event_sim.cpp` walks the parent chain of
  `View*` nodes. The result is correct only when every ancestor correctly
  stores its own `x`/`y` offset relative to its parent. Verify this for block,
  flex, and grid containers before relying on selector-based clicking.
- Hover state (`doc->state->hover_target`) is set by `handle_mouse_move`.
  `:hover` CSS pseudo-class styling is applied only if
  `selector_matcher_matches` checks `hover_target`. Confirm this path works
  before writing hover assertions.

**Test files**

| File | Description |
|------|-------------|
| `test/ui/ui_phase1a_font_sizes.json` | No mouse — pure `assert_visible` + `assert_text` on `baseline_201_font_sizes.html` |
| `test/ui/ui_phase1b_text_align.json` | `click` by text, `assert_caret` on `baseline_809_text_align.html` |
| `test/ui/test_click_elements.html` + `.json` | Click on `<a>` and `<button>` elements by CSS selector (pending layout verification) |
| `test/ui/test_hover.html` + `.json` | Mouse-move over boxes; assert hover target (pending layout verification) |

**Event types to exercise**

```json
{"type": "mouse_move", "target": {"selector": ".box"}}
{"type": "click",      "target": {"selector": "#btn-primary"}}
{"type": "click",      "target": {"text":     "Click me"}}
{"type": "assert_visible", "target": {"selector": ".box:hover"}, "visible": true}
{"type": "render",     "file": "./temp/phase1_hover.png"}
```

**Acceptance criteria**

1. Clicking a `<button>` by CSS selector lands the mouse within the button's
   layout bounding box (verify via `render` + visual inspection).
2. Clicking a text string moves the caret to a text view (`assert_caret
   view_type: 1`).
3. Mouse-moving over a `.box` sets `hover_target` to the expected element
   (`assert_state` once implemented, or `assert_visible` via `:hover` styling).
4. All 5 assertions in `test_click_text.json` and `test_click_elements.json`
   pass with exit code 0.

**Known failure modes to debug**

- If `assert_caret` fails: check that `find_text_position_recursive` walks the
  correct view tree for the layout type used.
- If click lands outside element: add `dump_caret` and `render` events to
  compare computed center vs. actual element position.
- If hover styling doesn't change: confirm `:hover` pseudo-class is evaluated
  during re-render after `RDT_EVENT_MOUSE_MOVE`.

---

### Phase 2 — Basic Text Events

**Goal**: Verify text input, selection (single-click caret, double-click word),
and keyboard-driven editing.

**Prerequisites / known limitations**

- `RDT_EVENT_TEXT_INPUT` is dispatched to the focused element. Focus must
  already be set to a text-editable view. Editable content areas in Radiant
  use the caret system — verify that `input[type=text]` or `[contenteditable]`
  elements receive focus before text events.
- Double-click word selection relies on `handle_mouse_button` receiving
  `clicks=2`. Confirm that word-boundary detection in the caret code is
  implemented before asserting `is_collapsed: false`.
- The `type` action sends one `TextInputEvent` per byte of ASCII text. For
  multi-byte UTF-8, the current loop in `event_sim.cpp` sends individual bytes,
  which is incorrect. Add proper UTF-8 decoding loop when testing non-ASCII
  text.
- `clear_first` uses `Cmd+A`/`Ctrl+A` then `Delete`. This only works if the
  focused element supports select-all. Test with `[contenteditable]` divs
  first before form `<input>` elements.

**Test files**

| File | Description |
|------|-------------|
| `test/ui/ui_phase2a_text_wrapping.json` | Click on wrapped text in `baseline_502_text_wrapping.html`; assert caret |
| `test/ui/ui_phase2b_styled_spans.json` | Inline span assertions + click in `baseline_503_styled_spans.html` |
| `test/ui/test_text_selection.html` + `.json` | Three paragraphs; click sets caret, dblclick selects word (pending layout verification) |
| `test/ui/test_keyboard_nav.html` + `.json` | Arrow keys, Home/End inside text; assert_caret offset (pending layout verification) |

**Event types to exercise**

```json
{"type": "click",           "target": {"text": "Hello World"}}
{"type": "assert_caret",    "view_type": 1}
{"type": "assert_selection","is_collapsed": true}

{"type": "dblclick",        "target": {"text": "Hello"}}
{"type": "assert_selection","is_collapsed": false}

{"type": "type",            "target": {"selector": "#editor"}, "text": "test input"}
{"type": "assert_text",     "target": {"selector": "#editor"}, "contains": "test input"}

{"type": "key_press",       "key": "home"}
{"type": "assert_caret",    "char_offset": 0}
```

**Acceptance criteria**

1. Single click in a paragraph positions the caret (`assert_caret view_type: 1
   char_offset: N` matches expected).
2. Double-click on a word creates a non-collapsed selection.
3. `type` into a `[contenteditable]` element produces visible text that
   `assert_text` can verify.
4. Arrow key navigation (`left`, `right`, `home`, `end`) moves the caret
   as expected by `assert_caret char_offset`.
5. `clear_first: true` replaces existing text without leaving residuals.

**Known failure modes to debug**

- If text doesn't appear after `type`: check that the element has focus
  (`assert_focus`) and that `handle_event` routes `RDT_EVENT_TEXT_INPUT` to the
  caret system.
- If `assert_text` returns empty: check `sim_extract_text` — it walks
  `DomText` nodes; verify those nodes are attached after edits.
- If `char_offset` assertion fails after `home`: confirm the caret code handles
  `GLFW_KEY_HOME` and resets offset to 0.

---

### Phase 3 — Mouse Dragging Events

**Goal**: Verify that click-and-drag between two positions creates a text
selection or moves a draggable element.

**Prerequisites / known limitations**

- `SIM_EVENT_MOUSE_DRAG` emits `mouse_down` at `(from_x, from_y)`, then five
  interpolated `mouse_move` steps, then `mouse_up` at `(to_x, to_y)`. This
  is sufficient for text selection drags but may not suffice for UI drag-and-
  drop if the engine requires pointer-capture or specific event timing.
- Text selection via drag requires that `handle_mouse_move` extends the
  selection when `button 0` is held. Verify the drag selection logic in
  `lambda-eval.cpp` (or wherever selection extension is implemented) before
  writing drag-selection assertions.
- Hit-test robustness (Phase 1) must be solid before drag tests will be
  reliable, since both endpoints are resolved through the same mechanism.
- Drag-and-drop of DOM elements is not yet in scope — that requires
  HTML5 drag events which are not yet wired to `RdtEvent`.

**Test files**

| File | Description |
|------|-------------|
| `test/ui/ui_phase3a_drag_select.json` | Drag across `.left` and `.center` paragraphs in `baseline_809_text_align.html`; assert non-collapsed selection. Coordinates from `test/layout/reference/baseline_809_text_align.json`. |
| `test/ui/test_drag_scroll.html` + `.json` | Drag scrollbar thumb; assert scroll position changes (pending layout verification) |

**Event types to exercise**

```json
{
  "type":   "mouse_drag",
  "from_x": 50, "from_y": 100,
  "to_x":   200, "to_y": 100,
  "button": 0
}
{"type": "assert_selection", "is_collapsed": false}
{"type": "render",           "file": "./temp/phase3_drag.png"}
```

With selector-based endpoints (once absolute coordinate mapping is reliable):

```json
{
  "type":          "mouse_drag",
  "target":        {"selector": "#word-start"},
  "to_target":     {"selector": "#word-end"},
  "button":        0
}
```

> The `to_target` field is a **future extension** — the current `mouse_drag`
> implementation requires raw `to_x`/`to_y` coordinates. Add selector-based
> drag endpoints in a follow-up.

**Acceptance criteria**

1. Dragging horizontally across a text paragraph creates a non-collapsed
   selection (`assert_selection is_collapsed: false`).
2. The selection anchor is at the drag start position and focus at drag end
   (verifiable via `dump_caret`).
3. A rendered PNG (`render`) visually shows the selected text with highlight.
4. `assert_scroll` after a scroll-bar drag shows a changed `scroll_y`.

**Known failure modes to debug**

- If selection stays collapsed after drag: confirm `handle_mouse_move` checks
  for button-down state and extends selection accordingly.
- If drag end position lands outside text: the five-step interpolation may
  overshoot or undershoot; consider increasing interpolation steps or adding
  a final `mouse_move` at exactly `(to_x, to_y)` before `mouse_up`.
- If `from_x`/`to_x` coordinates were wrong: add `dump_caret` at the
  start of the drag JSON to verify caret placement before dragging.

---

### Phase 4 — Advanced Interactions (Future)

These are planned but not yet scoped for immediate implementation. They depend
on the engine capabilities being extended beyond the current state.

#### 4a. Form Controls

- `<input type="text">` — requires form input focus, cursor rendering, and
  value tracking in `RadiantState`.
- `<input type="checkbox">` — requires toggle logic + visual checked state.
- `<select>` dropdown — requires popup overlay rendering.

Event types needed: `type`, `check`, `select_option`, `assert_value`,
`assert_checked`.

#### 4b. Scroll + Layout Interaction

- Scroll to bring off-screen elements into view, then interact with them.
- Requires `assert_visible` to correctly handle elements that are in the DOM
  but clipped by overflow.
- Sticky / fixed-position elements need special handling in hit-testing.

#### 4c. Keyboard Shortcuts

- `Cmd+A` (select all), `Cmd+C`/`Cmd+V` (copy/paste), `Cmd+Z` (undo).
- Requires clipboard integration and undo history in `RadiantState`.

#### 4d. Accessibility Tree Assertions

- Assert ARIA roles and labels via `assert_attribute`.
- Requires the DOM to expose `role`, `aria-label`, etc. through `DomElement`.

#### 4e. Animation and Timing

- CSS transitions triggered by hover/click.
- Requires the event loop to process frames between events and expose
  `assert_style` for computed property values.

---

## GTest Integration

The GTest runner at `test/test_ui_automation_gtest.cpp` auto-discovers all
`*.json` files in `test/ui/` and reads each file's `"html"` field to resolve
the HTML target. Each discovered test runs via:

```sh
./lambda.exe view <html> --event-file <json> --no-window
```

**Running all UI automation tests:**

```sh
make test-ui-automation
```

**Running a single test:**

```sh
./test/test_ui_automation_gtest.exe --gtest_filter=UIAutomation.*click*
```

**Exit codes:**
- `0` — All assertions in the JSON file passed.
- `1` — One or more assertions failed.

**Stderr output format** (parsed by the GTest runner):

```
========================================
 EVENT SIMULATION RESULTS
 Test: <name from JSON "name" field>
========================================
 Events executed: N
 Assertions: P passed, F failed
 Result: PASS / FAIL
========================================
```

---

## Debugging Failing Tests

1. **Enable log output**: remove `--no-log` from the command (or check
   `./log.txt` which is always written).
2. **Add a `render` event** just before the failing assertion to capture a
   PNG snapshot of the view at that point.
3. **Add a `dump_caret` event** to output caret/selection/focus state to a
   text file.
4. **Use raw coordinates** to bypass selector resolution: replace
   `"target": {"selector": "..."}` with `"x": N, "y": M` to confirm the
   event lands where you think it should.
5. **Run with debugger**:
   ```sh
   lldb -o "run" -o "bt" -o "quit" ./lambda.exe -- view test/ui/test_foo.html --event-file test/ui/test_foo.json
   ```

