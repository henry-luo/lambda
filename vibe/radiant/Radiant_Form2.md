# Radiant Form Support Enhancement — Phase 2 Proposal

> **Predecessor:** `Radiant_Form.md` (Phase 1 — static rendering and layout)
> **Status:** Phase 2a complete — 352/443 WPT form tests passing (79.5%)
> **Target Controls:** button, checkbox, radio, select, text input, textarea

---

## 1. Current State Analysis

### 1.1 What Exists (from Phase 1)

Phase 1 implemented static form rendering across five files:

| File | Responsibility |
|------|---------------|
| `form_control.hpp` | `FormControlProp` struct, `FormControlType` enum, intrinsic size constants |
| `resolve_htm_style.cpp` | HTML attribute parsing → `FormControlProp`, UA default styles |
| `layout_form.cpp` | Intrinsic sizing: `calc_text_input_size`, `calc_textarea_size`, `calc_button_size` |
| `render_form.cpp` | Visual rendering: 3D borders, checkmark, radio dot, select arrow, placeholder text |
| `event_sim.cpp` | Checkbox/radio toggle, select option selection |

### 1.2 WPT Form Test Results

Test suite: 443 HTML files from WPT `html/semantics/forms/`, browser references generated via Puppeteer.

| Metric | Count | % |
|--------|-------|---|
| **Total** | 443 | 100% |
| **Pass** | 352 | 79.5% |
| **Fail** | 91 | 20.5% |
| → Near-pass (elem ≥ 80%) | 13 | — |
| → Mid (50–79%) | 28 | — |
| → Low (< 50%) | 50 | — |
| **Error** (ENOENT — layout crash) | 0 | 0% |

Average element pass rate among failures: **41.1%**

#### Results by control type (name-based classification):

| Control | Pass | Total | Rate | Fail | Error |
|---------|------|-------|------|------|-------|
| other | 78 | 94 | 82% | 16 | 0 |
| select | 61 | 83 | 73% | 22 | 0 |
| text_input | 42 | 59 | 71% | 17 | 0 |
| form | 32 | 43 | 74% | 11 | 0 |
| validation | 24 | 28 | 85% | 4 | 0 |
| date | 23 | 29 | 79% | 6 | 0 |
| button | 20 | 26 | 76% | 6 | 0 |
| range | 16 | 18 | 88% | 2 | 0 |
| textarea | 14 | 17 | 82% | 3 | 0 |
| radio | 13 | 14 | 92% | 1 | 0 |
| fieldset | 10 | 10 | 100% | 0 | 0 |
| meter_progress | 8 | 9 | 88% | 1 | 0 |
| label | 5 | 6 | 83% | 1 | 0 |
| hidden | 3 | 4 | 75% | 1 | 0 |
| checkbox | 3 | 3 | 100% | 0 | 0 |

Key observations:
- **All ENOENT crashes resolved** — 0 errors (was 128). Stub handling added for `<meter>`, `<progress>`, `<output>`, `<datalist>`, `<label>`, and complex `<select>` patterns.
- **Fieldset and checkbox at 100%** — all tests pass for these controls.
- **Radio** jumped from 27% → 92%, **range** from 41% → 88%, **meter/progress** from 0% → 88% — major gains from crash fixes and UA style improvements.
- **Select** improved dramatically from 9% → 73% (was the worst, now mid-range). Remaining 22 failures involve `<optgroup>`, `<select multiple>`, and advanced dropdown behavior.
- **Text input** at 71% (was 47%) — remaining failures are border/padding details and text measurement precision.
- **91 remaining failures** are dominated by select (22), text_input (17), other (16), and form (11).

### 1.3 Gap Analysis

| Capability | Status | Details |
|------------|--------|---------|
| **Intrinsic sizing** | ✅ Implemented | Text, textarea, button, checkbox, radio, select, range, meter, progress |
| **Basic rendering** | ✅ Implemented | 3D borders, checkmark, radio dot, select arrow, meter/progress bars |
| **Attribute parsing** | ✅ Implemented | type, value, placeholder, name, size, cols, rows, disabled, readonly, checked, required |
| **Checkbox toggle** | ✅ Implemented | Via `event_sim.cpp`, pseudo-state update |
| **Radio toggle** | ✅ Implemented | Group unchecking, pseudo-state update |
| **Select option** | ✅ Implemented | Index-based selection, dropdown rendering |
| **Fieldset/legend layout** | ✅ Implemented | 100% pass rate — legend positioning and border rendering |
| **Label handling** | ✅ Implemented | 83% pass rate — layout and rendering (click-to-focus not yet wired) |
| **`<meter>` / `<progress>`** | ✅ Implemented | 88% pass rate — stub rendering with intrinsic sizing |
| **Text editing** | ❌ Missing | No caret, no cursor, no selection, no keyboard input |
| **Focus management** | ⚠️ Partial | `:focus` pseudo-state exists but no focus ring rendering, no Tab navigation for forms |
| **Label association** | ❌ Missing | `<label for="">` click-to-focus not implemented |
| **Form validation** | ❌ Missing | `:valid`/`:invalid` not dynamically computed |
| **CSS `appearance`** | ❌ Missing | Property parsed in `css_properties.cpp` L348 but no rendering effect |
| **Placeholder rendering** | ⚠️ Partial | Color set but text not always rendered (font setup issues) |

---

## 2. Proposed Enhancements

### Overview

Six workstreams targeting the six priority controls, plus cross-cutting infrastructure:

```
Phase 2a — Infrastructure: Focus, label, pseudo-states, crash fixes
Phase 2b — Button: Content-based sizing, appearance, active/hover states
Phase 2c — Checkbox & Radio: Visual accuracy, group behavior, label association
Phase 2d — Select: Dropdown layout, keyboard navigation, multi-select
Phase 2e — Text Input: Text editing engine (caret, selection, keyboard input)
Phase 2f — Textarea: Multi-line editing, scrolling, resize handle
Phase 2g — Testing: UI automation test suite for all controls
```

---

### Phase 2a — Infrastructure (Cross-Cutting)

#### 2a.1 Fix ENOENT Crashes ~~(128 tests)~~ ✅ RESOLVED

All 128 ENOENT errors have been fixed (now 0 errors). Changes made:

1. **Unsupported elements** (`<meter>`, `<progress>`, `<output>`, `<datalist>`) — added stub handling in `resolve_htm_style.cpp` with inline-block display and reasonable default sizes
2. **`<label>` crash** — fixed; label tests now at 83% pass rate (5/6)
3. **Complex `<select>` patterns** — fixed `<optgroup>` nesting, `<select multiple>`, and edge-case DOM patterns that crashed the layout engine
4. **Half-leading font metrics** — fixed `font_get_normal_lh_split()` to use proper half-leading model (ascent + leading/2), resolving many vertical alignment mismatches
5. **Replaced element strut** — removed `init_descender` from replaced element strut calculation to match browser behavior
6. **Trailing whitespace rollback** — guarded whitespace rollback to preserve inline-block replaced content dimensions

#### 2a.2 Focus Management System

Currently: `:focus` pseudo-state is defined (`PSEUDO_STATE_FOCUS`, bit 2) but no visual or behavioral system around it.

**New components:**

```
radiant/focus.hpp          — FocusManager: tracks focused element, Tab order
radiant/focus.cpp          — Implementation: focus(), blur(), tabindex logic
```

**FocusManager design:**

```cpp
struct FocusManager {
    ViewBlock* focused;          // Currently focused element
    int tab_index_mode;          // 0=natural, 1=explicit tabindex

    void focus(ViewBlock* target, RadiantState* state);
    void blur(RadiantState* state);
    void tab_next(RadiantState* state);   // Tab key handler
    void tab_prev(RadiantState* state);   // Shift+Tab
    bool is_focusable(ViewBlock* block);  // form controls, links, tabindex>=0
};
```

**Focus ring rendering** in `render_form.cpp`:
- When element has `PSEUDO_STATE_FOCUS`, draw a 2px blue outline (`#005FCC`) offset 1px from the border
- Matches browser default focus indicator styling

**Tab order:** Form controls (`<input>`, `<button>`, `<select>`, `<textarea>`) and links (`<a href>`) are focusable by default. Follow DOM order unless `tabindex` attribute overrides.

#### 2a.3 Label Association

`<label for="id">` should:
1. On click → focus the referenced form control
2. On click → toggle checkbox/radio if the target is a toggle control
3. Implicit association: `<label>` wrapping a form control → same behavior

**Implementation in `event_sim.cpp`:**
When a click lands on a `<label>`, resolve the `for` attribute to `document.getElementById(for)`, then dispatch focus + click to that element.

#### 2a.4 Dynamic Pseudo-State Computation

Currently hardcoded at parse time. Need runtime updates for:

| Pseudo-state | Trigger |
|--------------|---------|
| `:focus` / `:focus-visible` / `:focus-within` | Focus manager focus/blur |
| `:checked` | Checkbox/radio toggle (already works) |
| `:disabled` / `:enabled` | Attribute change |
| `:valid` / `:invalid` | Form validation (future) |
| `:placeholder-shown` | Text input empty + has placeholder attribute |
| `:read-only` / `:read-write` | `readonly`/`disabled` attribute presence |

After pseudo-state change, mark `needs_restyle = true` to re-resolve matched CSS rules.

---

### Phase 2b — Button Enhancement

#### Current state
- `render_button()` draws gray background with outset 3D border
- Content rendered via child flow (works for `<button>text</button>`)
- `<input type="submit/reset/button">` uses `form->value` for text

#### Improvements

1. **Active state rendering**: On mousedown, swap 3D border to inset (pressed appearance) and shift content 1px down/right. Use `PSEUDO_STATE_ACTIVE`.

2. **Hover state rendering**: On hover, lighten background slightly. Use `PSEUDO_STATE_HOVER`.

3. **Disabled rendering**: Gray out text and reduce contrast when `form->disabled`.

4. **`<input type="submit">` default text**: Already handles "Submit"/"Reset" defaults. Add "Submit Query" for `<input type="submit">` with no value (matches Chrome).

5. **Content-based sizing fix**: Current `calc_button_size()` approximates with `len * font_size * 0.55f`. Replace with actual font glyph measurement for accurate width.

**Files modified:** `render_form.cpp`, `layout_form.cpp`

---

### Phase 2c — Checkbox & Radio Enhancement

#### Current state
- Checkbox: white box, 3D inset border, ThorVG stroked checkmark when checked
- Radio: white circle, gray stroke border, filled dot when checked
- Toggle works via `event_sim.cpp`

#### Improvements

1. **Visual accuracy**: Match browser default appearance more closely:
   - Checkbox: 13×13px, 1px solid `#767676` border (not 3D), white fill, black checkmark (`✓` shape). When `:checked`, optional blue background (`#0075FF`) with white checkmark (matches modern Chrome).
   - Radio: 13×13px circle, 1px solid `#767676` border. When `:checked`, black filled circle at 40% radius.

2. **`:indeterminate` state** for checkbox: Render horizontal dash instead of checkmark. Set via JavaScript-only in browsers, but useful for test compatibility.

3. **Radio group management**: Current toggle logic in `sim_toggle_checkbox_radio()` walks the entire view tree. Optimize by:
   - Caching radio groups by `name` attribute in `RadiantState`
   - Or limiting search to the containing `<form>` element (spec-correct behavior)

4. **Label click integration**: When `<label for="checkbox-id">` is clicked, toggle the checkbox. Already described in Phase 2a.3.

5. **Focus ring**: Draw 2px blue outline when focused (via Tab navigation).

**Files modified:** `render_form.cpp`, `event_sim.cpp`

---

### Phase 2d — Select Enhancement

#### Current state
- Closed state: white box, gray border, dropdown arrow, selected option text via `render_simple_string()`
- Dropdown open: max 10 visible options, blue hover highlight, gray selected background
- Option text via DOM child traversal (`get_option_text_at_index()`)
- Select option events work

#### Issues (73% pass rate — remaining 22 failures)
- Missing: `<select multiple>`, `<optgroup>` rendering, `<select size>` attribute
- Missing: Keyboard navigation (arrow keys, type-ahead)
- Missing: `<option disabled>` rendering

#### Improvements

1. **Fix crashes** (47 ENOENT errors):
   - Handle `<select>` with no `<option>` children gracefully
   - Handle deeply nested `<optgroup>` → `<option>` structures
   - Handle `<select>` inside `<label>` or other unusual parents

2. **`<select size="N">` (listbox mode)**: When `size > 1` or `multiple` attribute, render as a scrollable list box instead of a dropdown. This is a distinct rendering mode.

3. **`<select multiple>`**: Render as listbox. Allow multiple selections via Ctrl+click or Shift+click.

4. **`<optgroup>` rendering**: Bold label for group, indented options beneath it.

5. **`<option disabled>`**: Gray text, non-selectable.

6. **Keyboard navigation** (when focused):
   - Arrow Up/Down: Change selected option
   - Enter/Space: Open/close dropdown
   - Escape: Close dropdown
   - Type-ahead: First letter jumping

7. **Dropdown positioning**: Handle edge cases where dropdown would extend below viewport — flip above if needed.

**Files modified:** `resolve_htm_style.cpp`, `render_form.cpp`, `layout_form.cpp`, `event_sim.cpp`

**New in FormControlProp:**
```cpp
int size_attr;           // <select size="N">  — visible option count for listbox mode
bool is_listbox;         // true when size > 1 or multiple
int scroll_offset;       // Scroll position for listbox mode
```

---

### Phase 2e — Text Input Editing Engine

This is the **largest and most critical missing feature**. Currently text input renders static value/placeholder only.

#### Architecture

New files:
```
radiant/text_edit.hpp     — TextEditState struct, API declarations
radiant/text_edit.cpp     — Text editing logic (caret, selection, insert/delete)
```

#### 2e.1 TextEditState

```cpp
struct TextEditState {
    // Buffer
    StrBuf buffer;              // Editable text content (from lib/strbuf.h)
    int byte_length;            // Current byte length

    // Cursor position (in Unicode codepoints, not bytes)
    int caret_pos;              // Caret position (codepoint index)
    int selection_start;        // Selection anchor (codepoint index, -1 if no selection)
    int selection_end;          // Selection end (-1 if no selection)

    // Visual state
    float caret_x;             // Caret x offset in CSS pixels (from content left)
    float scroll_offset;       // Horizontal scroll for overflow text (CSS pixels)
    bool caret_visible;        // Blink state
    int blink_counter;         // Frame counter for blink animation

    // Constraints
    int maxlength;             // Max codepoint count (-1 = unlimited)
    bool is_password;          // Mask display with dots

    // Methods
    void insert_text(const char* text);
    void delete_backward();     // Backspace
    void delete_forward();      // Delete key
    void move_left(bool shift); // Arrow left (shift = extend selection)
    void move_right(bool shift);
    void move_to_start(bool shift);  // Home / Cmd+Left
    void move_to_end(bool shift);    // End / Cmd+Right
    void select_all();
    void select_word();         // Double-click word selection

    // Clipboard
    void cut(char* out, int max_len);
    void copy(char* out, int max_len);
    void paste(const char* text);

    // Computation
    float compute_caret_x(FontProp* font, float pixel_ratio);
    void update_scroll(float visible_width);
};
```

#### 2e.2 Integration Points

**FormControlProp extension:**
```cpp
struct FormControlProp {
    // ... existing fields ...
    TextEditState* edit_state;  // Non-null when element is focused and editable
};
```

**FocusManager integration:**
- On focus, allocate `TextEditState` and initialize from `form->value`
- On blur, sync `edit_state->buffer` back to `form->value`, deallocate
- While focused, route keyboard events to `TextEditState`

**Event handling (`event_sim.cpp`):**
New event types for the UI automation framework:
```
type_text    — Type a string character by character
clear_text   — Clear the input field
set_value    — Set value directly (programmatic)
```

Keyboard event routing (in the interactive/window mode):
- Printable characters → `edit_state->insert_text()`
- Backspace → `edit_state->delete_backward()`
- Delete → `edit_state->delete_forward()`
- Left/Right arrows → `edit_state->move_left/right()`
- Home/End → `edit_state->move_to_start/end()`
- Ctrl+A / Cmd+A → `edit_state->select_all()`
- Ctrl+C / Cmd+C → `edit_state->copy()`
- Ctrl+V / Cmd+V → `edit_state->paste()`
- Ctrl+X / Cmd+X → `edit_state->cut()`

**Rendering (`render_form.cpp`):**
When `form->edit_state` is active:
1. Render text from `edit_state->buffer` (not `form->value`) with horizontal scroll offset
2. Render blinking caret as 1px wide vertical line at `caret_x` position, height = font ascender + descender
3. Render selection highlight as semi-transparent blue rectangle behind selected text
4. Clip text to content area (respect padding and border)

#### 2e.3 Caret Position Calculation

```
caret_x = sum of glyph advance widths from position 0 to caret_pos
```

Use the existing font system (`font_load_glyph()`) to measure each glyph's `advance_x`. Cache glyph advances for the current line to avoid repeated lookups.

When `caret_x > visible_width - scroll_offset`, auto-scroll right.
When `caret_x < scroll_offset`, auto-scroll left.

#### 2e.4 Password Masking

For `<input type="password">`:
- Display `●` (U+25CF BLACK CIRCLE) for each codepoint
- Use `●` glyph advance for caret positioning
- Brief delay before masking last typed character (optional, matches mobile behavior)

---

### Phase 2f — Textarea Multi-Line Editing

Extends `TextEditState` for multi-line:

#### Additional state:
```cpp
struct TextEditState {
    // ... single-line fields ...

    // Multi-line extensions
    bool multiline;
    int line_count;               // Number of logical lines
    int caret_line;               // Current line index
    int caret_col;                // Column in current line (codepoint)
    float vertical_scroll;        // Vertical scroll offset (CSS pixels)

    // Multi-line methods
    void insert_newline();
    void move_up(bool shift);
    void move_down(bool shift);
    int get_line_at_pos(int caret_pos);
    int get_col_at_pos(int caret_pos);
};
```

#### Additional behaviors:
1. **Line wrapping**: Respect `wrap` attribute (`soft`, `hard`, `off`). `soft` = visual wrap only, `hard` = insert newlines on submit, `off` = horizontal scroll.
2. **Vertical scrolling**: When content exceeds visible rows, scroll vertically. Render scrollbar indicator.
3. **Resize handle** (optional): `resize: both` CSS property — render a grip handle at bottom-right corner. Drag to resize.
4. **Arrow Up/Down navigation**: Move caret to same column on adjacent line. If line is shorter, clamp to end of line.

**Rendering:**
- Multi-line text with line breaks
- Line numbers (optional, rarely used in browsers)
- Vertical scrollbar when content overflows

---

### Phase 2g — UI Automation Test Suite

Leverage the existing event simulator framework (32 tests passing) to create form-specific interaction tests.

#### Test file structure:
```
test/layout/data/automation/form_button.html      + form_button.json
test/layout/data/automation/form_checkbox.html     + form_checkbox.json
test/layout/data/automation/form_radio.html        + form_radio.json
test/layout/data/automation/form_select.html       + form_select.json
test/layout/data/automation/form_text_input.html   + form_text_input.json
test/layout/data/automation/form_textarea.html     + form_textarea.json
test/layout/data/automation/form_focus.html        + form_focus.json
test/layout/data/automation/form_label.html        + form_label.json
```

#### Test scenarios per control:

**Button** (`form_button.json`):
```json
[
  {"type": "click", "selector": "button#submit-btn"},
  {"type": "assert_attribute", "selector": "button#submit-btn", "attribute": "data-clicked", "value": "..."},
  {"type": "click", "selector": "input[type=submit]"},
  {"type": "assert_text", "selector": "#result", "expected": "submitted"},
  {"type": "click", "selector": "button:disabled"},
  {"type": "render", "filename": "form_button_disabled"}
]
```

**Checkbox** (`form_checkbox.json`):
```json
[
  {"type": "assert_attribute", "selector": "#cb1", "attribute": "checked", "value": null},
  {"type": "click", "selector": "#cb1"},
  {"type": "assert_attribute", "selector": "#cb1", "attribute": "checked", "value": ""},
  {"type": "click", "selector": "#cb1"},
  {"type": "assert_attribute", "selector": "#cb1", "attribute": "checked", "value": null},
  {"type": "click", "selector": "label[for=cb1]"},
  {"type": "assert_attribute", "selector": "#cb1", "attribute": "checked", "value": ""},
  {"type": "render", "filename": "form_checkbox_states"}
]
```

**Radio** (`form_radio.json`):
```json
[
  {"type": "click", "selector": "#radio-a"},
  {"type": "assert_attribute", "selector": "#radio-a", "attribute": "checked", "value": ""},
  {"type": "click", "selector": "#radio-b"},
  {"type": "assert_attribute", "selector": "#radio-a", "attribute": "checked", "value": null},
  {"type": "assert_attribute", "selector": "#radio-b", "attribute": "checked", "value": ""},
  {"type": "render", "filename": "form_radio_group"}
]
```

**Select** (`form_select.json`):
```json
[
  {"type": "assert_text", "selector": "#sel1", "expected": "Option 1"},
  {"type": "select_option", "selector": "#sel1", "value": "opt2"},
  {"type": "assert_text", "selector": "#sel1", "expected": "Option 2"},
  {"type": "select_option", "selector": "#sel1", "label": "Option 3"},
  {"type": "render", "filename": "form_select_changed"}
]
```

**Text Input** (`form_text_input.json`):
```json
[
  {"type": "click", "selector": "#text1"},
  {"type": "assert_focused", "selector": "#text1"},
  {"type": "type_text", "selector": "#text1", "text": "Hello"},
  {"type": "assert_value", "selector": "#text1", "value": "Hello"},
  {"type": "type_text", "selector": "#text1", "text": " World"},
  {"type": "assert_value", "selector": "#text1", "value": "Hello World"},
  {"type": "clear_text", "selector": "#text1"},
  {"type": "assert_value", "selector": "#text1", "value": ""},
  {"type": "render", "filename": "form_text_input_typed"},
  {"type": "click", "selector": "#password1"},
  {"type": "type_text", "selector": "#password1", "text": "secret"},
  {"type": "render", "filename": "form_password_masked"}
]
```

**Textarea** (`form_textarea.json`):
```json
[
  {"type": "click", "selector": "#ta1"},
  {"type": "type_text", "selector": "#ta1", "text": "Line 1\nLine 2\nLine 3"},
  {"type": "assert_value", "selector": "#ta1", "value": "Line 1\nLine 2\nLine 3"},
  {"type": "render", "filename": "form_textarea_multiline"},
  {"type": "clear_text", "selector": "#ta1"},
  {"type": "assert_value", "selector": "#ta1", "value": ""},
  {"type": "render", "filename": "form_textarea_empty_placeholder"}
]
```

**Focus/Tab** (`form_focus.json`):
```json
[
  {"type": "click", "selector": "#input1"},
  {"type": "assert_focused", "selector": "#input1"},
  {"type": "key", "key": "Tab"},
  {"type": "assert_focused", "selector": "#input2"},
  {"type": "key", "key": "Tab"},
  {"type": "assert_focused", "selector": "#select1"},
  {"type": "key", "key": "Shift+Tab"},
  {"type": "assert_focused", "selector": "#input2"},
  {"type": "render", "filename": "form_focus_ring"}
]
```

#### New event types needed:

| Event Type | Purpose |
|------------|---------|
| `type_text` | Type text into the focused element |
| `clear_text` | Clear the focused/targeted element's text value |
| `assert_value` | Assert the current value of an input/textarea |
| `assert_focused` | Assert which element currently has focus |
| `key` | Send a keyboard key (Tab, Enter, Escape, arrow keys, Shift+Tab) |
| `set_value` | Programmatically set input value (no keystroke simulation) |

---

## 3. Implementation Plan

### Priority Order

```
✅ DONE — Phase 2a.1: Fix 128 ENOENT crashes (0 errors remaining)
P0 — Phase 2a.2: Focus management system (required by text editing)
P1 — Phase 2b: Button enhancements (76% → 90+%)
P1 — Phase 2c: Checkbox & radio (92–100%, mostly done — label click remaining)
P1 — Phase 2d: Select fixes and enhancements (73% → 85+%)
P2 — Phase 2e: Text input editing engine (core new feature)
P2 — Phase 2f: Textarea multi-line editing (extends 2e)
P2 — Phase 2a.3: Label association (enables label click tests)
P2 — Phase 2a.4: Dynamic pseudo-states
P3 — Phase 2g: UI automation tests
```

### File Change Summary

| File | Changes |
|------|---------|
| `radiant/form_control.hpp` | Add `TextEditState*`, `size_attr`, `is_listbox`, `scroll_offset` |
| `radiant/text_edit.hpp` | **New** — `TextEditState` struct |
| `radiant/text_edit.cpp` | **New** — Text editing logic |
| `radiant/focus.hpp` | **New** — `FocusManager` |
| `radiant/focus.cpp` | **New** — Focus management, Tab order |
| `radiant/resolve_htm_style.cpp` | Add meter/progress/output/datalist stubs, label handling |
| `radiant/layout_form.cpp` | Select listbox mode, improved button sizing |
| `radiant/render_form.cpp` | Focus ring, caret, selection, improved checkbox/radio/select rendering |
| `radiant/event_sim.cpp` | `type_text`, `clear_text`, `assert_value`, `assert_focused`, `key` events, label click delegation |
| `radiant/layout.hpp` | `FocusManager*` in `RadiantState` |
| `build_lambda_config.json` | Add `text_edit.cpp`, `focus.cpp` to build |

### Success Metrics

| Milestone | Target | Actual |
|-----------|--------|--------|
| After Phase 2a (crash fixes + infrastructure) | ENOENT < 20, pass > 200/447 (45%) | ✅ **0 errors, 352/443 pass (79.5%)** |
| After Phase 2b–2d (button, checkbox, radio, select) | Pass > 280/447 (63%) | ✅ Already exceeded |
| After Phase 2e–2f (text editing) | Pass > 320/447 (72%) | ✅ Already exceeded |
| After Phase 2g (all phases) | Pass > 350/447 (78%) | ✅ 352/443 (79.5%) — achieved pre-Phase 2b |
| UI automation form tests | 8 test files, all passing | Pending |

---

## 4. Technical Design Details

### 4.1 Text Editing — Byte vs Codepoint Indexing

Lambda strings are UTF-8. The `TextEditState` uses codepoint indices for caret/selection positions, but the `StrBuf` buffer stores UTF-8 bytes. Conversion functions:

```cpp
// Convert codepoint index → byte offset in UTF-8 buffer
int codepoint_to_byte(const char* buf, int codepoint_idx);

// Convert byte offset → codepoint index
int byte_to_codepoint(const char* buf, int byte_offset);
```

Use `str_utf8_decode()` from `lib/str.h` for iterating codepoints. For cursor movement, also need:

```cpp
// Get byte offset of next/previous codepoint boundary
int utf8_next(const char* buf, int byte_pos);
int utf8_prev(const char* buf, int byte_pos);

// Get word boundary positions (for Ctrl+Left/Right and double-click selection)
int utf8_word_start(const char* buf, int byte_pos);
int utf8_word_end(const char* buf, int byte_pos);
```

### 4.2 Caret Blink Animation

In interactive mode (window), caret blinks at ~530ms interval (matches browser default):

```cpp
// In render loop, after rendering text:
if (form->edit_state && form->edit_state->caret_visible) {
    int frame = state->frame_counter;
    bool show = (frame / 32) % 2 == 0;  // ~530ms at 60fps
    if (show) {
        render_caret(rdcon, form->edit_state->caret_x, ...);
    }
}
```

For UI automation (headless), caret is always visible in renders to avoid timing-dependent screenshots.

### 4.3 Focus Ring Rendering

```cpp
void render_focus_ring(RenderContext* rdcon, float x, float y, float w, float h) {
    Color focus_color = make_color(0, 95, 204);  // #005FCC
    float offset = 1.0f * rdcon->scale;
    float ring_w = 2.0f * rdcon->scale;
    // Draw outline outside the border box
    fill_rect(rdcon, x - offset - ring_w, y - offset - ring_w,
              w + 2*(offset+ring_w), ring_w, focus_color);  // top
    fill_rect(rdcon, x - offset - ring_w, y + h + offset,
              w + 2*(offset+ring_w), ring_w, focus_color);  // bottom
    fill_rect(rdcon, x - offset - ring_w, y - offset,
              ring_w, h + 2*offset, focus_color);            // left
    fill_rect(rdcon, x + w + offset, y - offset,
              ring_w, h + 2*offset, focus_color);            // right
}
```

### 4.4 Select Listbox Mode

When `<select size="N">` with N > 1, or `<select multiple>`:

```
┌─────────────────┐
│ Option 1        │  ← selected (blue highlight)
│ Option 2        │
│ Option 3        │
│ Option 4        │  ← visible, scrollable
└─────────────────┘
```

- Render as a bordered list with N visible rows
- Scroll with arrow keys or mouse wheel
- Multiple selection: Ctrl+click adds/removes, Shift+click extends range
- Layout: `intrinsic_height = option_count_visible * option_height`

### 4.5 Label Association Resolution

```cpp
// In event_sim.cpp click handler:
if (clicked_elem->tag() == HTM_TAG_LABEL) {
    const char* for_attr = clicked_elem->get_attribute("for");
    if (for_attr) {
        // Explicit association: find element by ID
        ViewBlock* target = find_element_by_id(doc, for_attr);
        if (target) {
            focus_manager->focus(target, state);
            if (sim_is_checkbox_or_radio(target)) {
                sim_toggle_checkbox_radio(target, state);
            }
        }
    } else {
        // Implicit association: find first form control descendant
        ViewBlock* target = find_first_form_descendant(clicked_elem);
        if (target) {
            focus_manager->focus(target, state);
            if (sim_is_checkbox_or_radio(target)) {
                sim_toggle_checkbox_radio(target, state);
            }
        }
    }
}
```

---

## 5. Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Text editing adds significant complexity | High | Keep TextEditState self-contained with clear API; test each operation in isolation |
| Font glyph measurement may be slow per-keystroke | Medium | Cache glyph advances per font+size; only re-measure on font change |
| 128 ENOENT crashes may have diverse root causes | ~~Medium~~ | ✅ **Resolved** — all 128 fixed via stub elements, font metrics, and defensive checks |
| Select dropdown z-ordering conflicts with other elements | Low | Already uses clip override for overlay rendering — extend pattern |
| Multi-line textarea scroll + text editing interaction | Medium | Build on single-line first, then extend incrementally |

---

## 6. Non-Goals (Out of Scope)

- Form submission (`<form action="...">` POST/GET)
- Client-side JavaScript validation
- Date/time picker controls (`<input type="date">`, etc.)
- Color picker (`<input type="color">`)
- File picker (`<input type="file">`)
- `<input type="image">` advanced behavior
- ContentEditable / rich text editing
- Drag-and-drop file upload
- Autocomplete / autofill
