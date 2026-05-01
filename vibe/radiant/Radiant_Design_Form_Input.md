# Radiant Design — HTML Form Text Input & Editing

> Status: Proposal
> Predecessors: [Radiant_Form.md](Radiant_Form.md), [Radiant_Form2.md](Radiant_Form2.md), [Radiant_Design_Selection.md](Radiant_Design_Selection.md), [Radiant_Design_Clipboard.md](Radiant_Design_Clipboard.md), [Radiant_UI_Automation2.md](Radiant_UI_Automation2.md)
> Scope: `<input type="text|password|search|email|url|tel|number">` and `<textarea>` — full editing model, caret, selection, mouse, keyboard, IME, clipboard, accessibility, and automation.

---

## 1. Goal

Bring Radiant's `<input>` and `<textarea>` editing experience to parity with a modern desktop browser:

- **Caret** with blinking, navigation, kept in view via auto-scroll.
- **Mouse** click-to-position, drag-to-select, double-click word, triple-click line/all.
- **Keyboard** insert / delete / Backspace / Delete / arrows / Home / End / PgUp / PgDn / Enter.
- **Modifier combinations** word-jump (Alt/Ctrl + arrow), select-extend (Shift), select-all (Cmd/Ctrl+A), undo / redo (Cmd/Ctrl+Z / Shift+Z).
- **Clipboard** Cmd/Ctrl + C / X / V wired to the OS pasteboard (already partly done — see §3.6).
- **IME / composition** — currently missing; design below.
- **Accessibility** focus ring, `:focus-visible`, ARIA value reflection, `change` event on commit.
- **Automation** drive every behavior above from `event_sim` JSON tests under [test/ui/](../../test/ui/).

This proposal supersedes the text-editing portion of [Radiant_Form2.md](Radiant_Form2.md) §2e–2f and folds in what has actually shipped in [radiant/text_control.hpp](../../radiant/text_control.hpp), [radiant/event.cpp](../../radiant/event.cpp), [radiant/render_form.cpp](../../radiant/render_form.cpp), and [radiant/clipboard.hpp](../../radiant/clipboard.hpp).

---

## 2. Current State (as of this proposal)

### 2.1 Architecture Map

| Layer | File | What it owns |
|------|------|--------------|
| DOM tagging | [lambda/input/css/dom_element.cpp](../../lambda/input/css/dom_element.cpp) | Recognizes `<input>`, `<textarea>`; sets `:checked`, `:disabled` |
| Form model | [radiant/form_control.hpp](../../radiant/form_control.hpp) | `FormControlProp`, `FormControlType`, intrinsic-size constants |
| Text-control API | [radiant/text_control.hpp](../../radiant/text_control.hpp), [text_control.cpp](../../radiant/text_control.cpp) | `tc_set_value`, `tc_set_selection_range`, UTF‑8↔UTF‑16, lazy init, focus tracking, legacy↔form sync |
| Selection / caret | [radiant/dom_range.cpp](../../radiant/dom_range.cpp), [dom_range_resolver.cpp](../../radiant/dom_range_resolver.cpp), `state->caret` / `state->selection` in [radiant/event.cpp](../../radiant/event.cpp) | Cross-view selection, glyph-precise caret |
| Layout | [radiant/layout_form.cpp](../../radiant/layout_form.cpp), [intrinsic_sizing.cpp](../../radiant/intrinsic_sizing.cpp) | Replaced-element sizing for inputs/textarea |
| Render | [radiant/render_form.cpp](../../radiant/render_form.cpp) | Border, value text, placeholder, caret, selection highlight |
| Window/events | [radiant/window.cpp](../../radiant/window.cpp), [event.cpp](../../radiant/event.cpp), [event.hpp](../../radiant/event.hpp) | GLFW callbacks, `RDT_EVENT_KEY_DOWN`, `RDT_EVENT_TEXT_INPUT`, mouse |
| Clipboard | [radiant/clipboard.hpp](../../radiant/clipboard.hpp), [clipboard.cpp](../../radiant/clipboard.cpp) | In-process canonical store + GLFW/NSPasteboard/X11/OLE bridge |
| Automation | [radiant/event_sim.cpp](../../radiant/event_sim.cpp), [event_sim.hpp](../../radiant/event_sim.hpp) | JSON-driven `click`, `type`, `key_press`, `key_combo`, `assert_value`, `assert_selection`, `assert_caret` |
| JS bridge | `lambda/js/js_dom.cpp` (see [text_control.hpp](../../radiant/text_control.hpp)) | `value`, `selectionStart/End`, `setSelectionRange`, `setRangeText`, `selectionchange` |

### 2.2 What Already Works

- **Element identity & lazy init** — `tc_is_text_control()` recognises text-like inputs and `<textarea>`; `tc_ensure_init()` populates `current_value` from HTML defaults on first access.
- **Value & selection state** — `FormControlProp::current_value` (UTF-8) plus `selection_start/end` (UTF-16 code units) with `selection_direction`. UTF-8↔UTF-16 conversion helpers exist.
- **Caret rendering & blink** — `render_form.cpp` draws a 1px caret inside the content box; selection highlight is glyph-precise and matches the caret math.
- **Mouse** — click sets caret via `selection_start()`; drag extends via `selection_extend()` with glyph-precise X→offset; cross-view selection works.
- **Keyboard navigation** — Left / Right walk UTF‑8 codepoints; Up / Down do line-aware caret movement in `<textarea>`; Home / End / Backspace / Delete; Shift+arrow extends selection.
- **Text input** — GLFW `character_callback` → `RDT_EVENT_TEXT_INPUT` (UTF-32 codepoint) → `dispatch_lambda_handler(focused, "input")`; the value buffer is appended/spliced and the caret advances.
- **Clipboard** — Cmd/Ctrl + C / X / V handled inside `<textarea>` (and partially `<input>`); bridges to `clipboard_copy_text` / `clipboard_get_text`.
- **Focus tracking** — `tc_set_active_element` / `tc_get_active_element`; `:focus` pseudo-state is wired; tab-navigation present for some control types.
- **JS observability** — `selectionchange` is queued via `tc_notify_selection_changed`; legacy ↔ form sync after every text-control mutation.
- **Automation** — `event_sim.cpp` already supports `type`, `key_press`, `key_combo`, `assert_value`, `assert_selection`, `assert_caret`, `check`, `select_option`. Existing tests prove typing works end-to-end ([test/ui/todo_text_input.json](../../test/ui/todo_text_input.json), [todo2_textarea.json](../../test/ui/todo2_textarea.json), [test_form_controls.html](../../test/ui/test_form_controls.html)).

### 2.3 Gap Analysis

| Capability | Status | Gap / Defect |
|---|---|---|
| **Caret blink** | Partial | Render path draws caret; per-frame blink animation (~530 ms) not driven from a timer. Headless render forces it visible — fine. |
| **Click positioning** | Works for ASCII; needs verification on shaped text, RTL, CJK. |
| **Drag-to-select inside one input** | Works | Confirm horizontal auto-scroll while dragging past the right edge. |
| **Drag across `<input>` into surrounding text** | Untested | Selection must clamp to the input's value region; cross-control drag should not select host DOM text. |
| **Double-click → word** | **Missing** for inputs/textarea (works for static text). Need word-segmentation using existing `utf_word_*` from `lib/`. |
| **Triple-click** | **Missing** — should select the whole logical line in `<textarea>`, the whole value in `<input>`. |
| **Click-and-drag word/line modes** | **Missing** — after double/triple click, dragging should extend by word/line. |
| **Arrow Up/Down in single-line `<input>`** | Partial — should move caret to start/end (matches browsers), currently a no-op in some paths. |
| **PgUp / PgDn in `<textarea>`** | **Missing** |
| **Cmd/Ctrl + Left/Right** (line) and **Alt/Ctrl + Left/Right** (word) | **Missing** — only character-level navigation. |
| **Cmd/Ctrl + Backspace / Delete** (delete word / line) | **Missing** |
| **Cmd/Ctrl + A** | Works in `<textarea>`; verify in `<input>`. |
| **Enter in `<input>`** | Should fire implicit form submission (`change`+`submit`); currently no-op. |
| **Enter in `<textarea>`** | Inserts newline (works). |
| **Tab navigation between focusable controls** | Partial — verify `tabindex` ordering, `Shift+Tab`, focus-trap inside dialog. |
| **`change` event on commit** | **Missing** — only `input` is dispatched. Spec: `change` fires on blur/Enter when value differs from focus-time value. |
| **`beforeinput` event** | **Missing** ([WPT beforeinput.tentative.html](../../test/layout/data/form/beforeinput.tentative.html) exists). |
| **`selectionchange`** | Wired (`tc_notify_selection_changed`) — verify dispatch under all editing paths. |
| **Undo / Redo** (Cmd/Ctrl + Z, Shift+Z) | **Missing** — no edit history. |
| **maxlength enforcement** | Stored, **not enforced** during typing or paste. |
| **type="password" masking** | Field type recognised; **glyph substitution to `●` not implemented** in render or in caret-X math. |
| **type="number" / "email" / "url" constraints** | **Not enforced** at runtime; `:invalid` not toggled. |
| **Placeholder rendering** | Field exists; render path conditional on empty value — verify across themes. |
| **`:placeholder-shown` / `:read-only` / `:read-write` / `:valid` / `:invalid`** | **Missing** dynamic pseudo-state computation. |
| **IME / composition** | **Missing** — no `compositionstart` / `compositionupdate` / `compositionend`; preedit text is not displayed. macOS `NSTextInputClient` and Win32 `WM_IME_*` not wired. |
| **Clipboard MIME** | Plain text only; `text/html` and rich content not provided for inputs (textarea fine to remain text-only). |
| **Native context menu** | **Missing** — right-click should show Cut / Copy / Paste / Select All. |
| **Spellcheck / autocorrect** | **Missing**; out of scope for v1. |
| **Drag-and-drop text** | **Missing** — drop external text onto an input. |
| **RTL / bidi caret** | **Untested** — caret movement direction in mixed-direction runs. |
| **Soft-wrap caret motion in `<textarea>`** | Partial — Up/Down across visual lines vs logical lines needs reconciliation. |
| **Vertical scroll in `<textarea>`** | Partial — caret-into-view scroll missing for tall content. |
| **Horizontal scroll in `<input>`** | Partial — long values overflow without auto-scroll. |
| **Read-only / disabled** | `readonly` blocks edits but allows selection; `disabled` blocks both — verify both paths. |
| **Autofocus** | Attribute parsed; not auto-applied at load. |
| **Form reset (`<form reset>`)** | **Missing** — should restore default value. |

---

## 3. Design

### 3.1 Edit Model — Promote `FormControlProp` to a First-Class Editor

Keep the existing fields, add an embedded editor record (kept inside `FormControlProp` rather than a side struct so the legacy/JS sync paths in `text_control.cpp` need no churn):

```cpp
// radiant/form_control.hpp  (additions)
struct EditHistoryEntry {
    char*    snapshot;        // UTF-8 value copy
    uint32_t length;          // bytes
    uint32_t sel_start_u16;
    uint32_t sel_end_u16;
    uint8_t  sel_dir;         // 0 none, 1 fwd, 2 bwd
};

struct EditHistory {
    EditHistoryEntry* ring;   // bounded ring (N=64)
    uint16_t          head;
    uint16_t          tail;
    uint16_t          cursor; // current position for redo
    uint16_t          cap;
};

struct FormControlProp {
    /* ...existing fields... */

    // Editor extensions
    EditHistory*  history;        // lazy
    char*         value_at_focus; // for `change` event diff
    uint32_t      value_at_focus_len;

    // Visual viewport (for auto-scroll)
    float         scroll_x;       // px, single-line
    float         scroll_y;       // px, multi-line
    float         caret_blink_t;  // seconds since last toggle
    bool          caret_on;

    // IME preedit (transient — not part of value)
    char*         preedit_utf8;   // owned; null when not composing
    uint32_t      preedit_len;
    uint32_t      preedit_caret;  // codepoint offset within preedit
};
```

**Rationale:** Reuse the canonical `current_value` + `selection_start/end` already shared with `js_dom.cpp`. The undo ring snapshots the *whole* value; for typical input sizes (< a few KB) memory is negligible and rollback is O(1) on the buffer side.

### 3.2 Editing Operations — `text_edit.{hpp,cpp}` (new)

A thin operations layer that all mouse / keyboard / IME / JS paths call into. Every op:

1. Pushes a history entry **iff** it changes the value (coalescing rules in §3.5).
2. Mutates `current_value` via `tc_set_value` (so the legacy-bridge stays consistent).
3. Updates selection via `tc_set_selection_range`.
4. Dispatches `beforeinput` (cancellable), then `input`, then queues `selectionchange`.
5. Schedules caret-into-view scroll on the next frame.
6. Marks the view dirty.

```cpp
// radiant/text_edit.hpp (sketch)
namespace te {
    enum class Granularity { Char, Word, Line, Paragraph, Document };
    enum class Direction   { Backward, Forward };

    bool insert_text   (DomElement*, const char* utf8, uint32_t len);
    bool delete_range  (DomElement*, Direction, Granularity);
    bool move_caret    (DomElement*, Direction, Granularity, bool extend);
    bool move_to       (DomElement*, uint32_t u16_offset, bool extend);
    bool select_all    (DomElement*);
    bool select_word_at(DomElement*, uint32_t u16_offset);
    bool select_line_at(DomElement*, uint32_t u16_offset);
    bool clear_selection_to_caret(DomElement*, bool collapse_to_start);

    bool cut   (DomElement*, RadiantState*);
    bool copy  (DomElement*, RadiantState*);
    bool paste (DomElement*, RadiantState*, const char* utf8, uint32_t len);

    bool undo  (DomElement*);
    bool redo  (DomElement*);

    // IME
    void ime_begin  (DomElement*);
    void ime_update (DomElement*, const char* preedit, uint32_t len, uint32_t caret_cp);
    void ime_commit (DomElement*, const char* committed, uint32_t len);
    void ime_cancel (DomElement*);
}
```

`event.cpp` is refactored so its existing per-key branches reduce to one-line calls into `te::*`. This eliminates today's drift between `<input>` and `<textarea>` keyboard handling.

### 3.3 Caret Positioning & Hit-Testing

Reuse the glyph-precise path already in `event.cpp`'s `MOUSE_DOWN` handler. Extract it into:

```cpp
// radiant/text_edit.cpp
uint32_t te_offset_at_x(DomElement*, float content_x);   // returns UTF-16 offset
float    te_x_at_offset (DomElement*, uint32_t u16);     // for caret_x and selection rect
void     te_caret_into_view(DomElement*);                // updates form->scroll_x / scroll_y
```

For `<textarea>`, build (and cache) a per-frame line index — array of `(byte_start, x_advance, y, height)` — derived from the existing inline layout. Up/Down then becomes:

```
desired_x = (just-vertical-moved) ? sticky_x : x_at_offset(caret)
caret = offset_at_x_on_line(line ± 1, desired_x)
```

The `sticky_x` resets on any horizontal motion, matching browser behavior.

### 3.4 Mouse Interaction Detail

| Gesture | Action |
|---|---|
| Single click | Place caret; collapse selection. |
| Click + drag | Extend selection by character. |
| Double click | Select word at point. Set `drag_mode = WORD`. |
| Triple click | `<input>`: select all. `<textarea>`: select line. Set `drag_mode = LINE`. |
| Quad click | (browsers: paragraph) — out of scope. |
| Drag after double/triple | Extend selection by `drag_mode` granularity. |
| Right click | Open native context menu (§3.10). |
| Shift + click | Extend selection from anchor to point. |
| Cmd/Ctrl + click | (no-op in inputs; reserved.) |

State lives in `RadiantState`:
```cpp
struct RadiantState {
    /* ... */
    enum class DragMode { Char, Word, Line } drag_mode;
    uint32_t    drag_anchor_u16;          // anchor offset
    DomElement* drag_target;              // text control owning the drag
    double      last_click_time;
    int         click_count;              // 1, 2, 3
    float       last_click_x, last_click_y;
};
```

A click is a "double click" when `now - last_click_time < 500ms` and within 4 px of the previous click.

### 3.5 Keyboard Bindings

Modifier abbreviations: **Mod** = Cmd on macOS, Ctrl elsewhere. **Alt** = Option on macOS.

| Key | Effect |
|---|---|
| Printable | `te::insert_text` (replacing selection). |
| Backspace | `delete_range(Backward, Char)`. With selection: delete selection. |
| Delete | `delete_range(Forward, Char)`. |
| Alt + Backspace | `delete_range(Backward, Word)`. |
| Mod + Backspace | `delete_range(Backward, Line)`. |
| Alt + Delete | `delete_range(Forward, Word)`. |
| Left / Right | `move_caret(±, Char, extend=Shift)`. |
| Alt + Left / Right | `move_caret(±, Word, …)`. |
| Mod + Left / Right (mac) / Home / End | `move_caret(±, Line, …)`. |
| Up / Down | textarea: by visual line; input: to start/end (mirrors Chrome). |
| PgUp / PgDn | textarea: by viewport-height of lines. |
| Mod + Up / Down (mac) | move to document start/end. |
| Mod + A | `select_all`. |
| Mod + C / X / V | `copy / cut / paste`. |
| Mod + Z | `undo`. Mod + Shift + Z (or Mod + Y on Win/Linux) | `redo`. |
| Enter | textarea: `insert_text("\n")`. input: dispatch implicit submit + `change`. |
| Tab | move focus (default action). With `tabindex=-1` skip. |
| Esc | cancel IME if active; else default (close dropdown / blur). |

**Coalescing for undo:** consecutive single-character `insert_text` of non-whitespace within 1.5 s are merged into one history entry. Any caret jump, selection change, deletion, or whitespace insert closes the group.

### 3.6 Clipboard

The plumbing in [radiant/clipboard.hpp](../../radiant/clipboard.hpp) is already capable. Tighten:

- Cut / Copy emit `text/plain` (UTF-8) only for inputs/textarea. Future: `text/html` from rich-text editors.
- Paste prefers `text/plain`; if the source has only `text/html`, strip tags via existing HTML parser.
- For `<input>` (single-line), strip newlines from pasted text (replace with U+0020) — matches HTML spec.
- Enforce `maxlength` after paste (truncate at char boundary).
- Fire `beforeinput` with `inputType="insertFromPaste"` (cancellable). Then `input`.

### 3.7 IME / Composition

Wire OS preedit through to `te::ime_*`:

- **macOS:** GLFW does not expose `NSTextInputClient`. We need a shim attached to `GLFWwindow`'s `NSView` that:
  - Implements `setMarkedText:selectedRange:replacementRange:` → `te::ime_update`.
  - Implements `insertText:replacementRange:` → `te::ime_commit`.
  - Returns `firstRectForCharacterRange:` from `te_x_at_offset` + caret rect (so the OS can position the candidate window).
  - File: [radiant/ime_mac.mm](../../radiant/ime_mac.mm) (new).
- **Windows:** subclass the GLFW HWND to handle `WM_IME_STARTCOMPOSITION`, `WM_IME_COMPOSITION`, `WM_IME_ENDCOMPOSITION` and drive `IMM_*` for caret rect. File: [radiant/ime_win.cpp](../../radiant/ime_win.cpp) (new).
- **Linux:** XIM/IBus via GLFW is non-trivial; a stub that forwards `RDT_EVENT_TEXT_INPUT` codepoints (current behaviour) is acceptable for v1.

**Render preedit:** when `form->preedit_len > 0`, render `value[0..caret] + preedit + value[caret..]` and underline the preedit range. The committed value isn't mutated until `ime_commit`.

`composition*` events are dispatched through the existing event bus.

### 3.8 Rendering Updates

In [radiant/render_form.cpp](../../radiant/render_form.cpp):

1. **Password masking** — when `form->control_subtype == PASSWORD`, render U+25CF for each codepoint and feed the same width to caret-X math. Provide a small "reveal last char" timer (1 s after most-recent insert) to match desktop Safari/Chrome.
2. **Placeholder** — show only when `current_value_len == 0` *and* not composing. Use the resolved `::placeholder` color or fallback `#75757580`.
3. **Caret blink** — animation timer (`form->caret_blink_t`) toggled at 530 ms; in headless rendering set `caret_on = true`.
4. **Selection rectangle** — already glyph-precise; for `<textarea>` extend per visual line.
5. **Focus ring** — 2 px `#005FCC` outline when `:focus-visible`; suppressed for mouse-only focus (set `:focus-visible` only when focus arrived via Tab / keyboard or programmatic).
6. **Auto-scroll** — call `te_caret_into_view` before draw.
7. **Read-only** — render text in normal color but suppress caret (still allow selection).
8. **Disabled** — gray text, no caret, ignore events.

### 3.9 Pseudo-State Reflection

Add a small reflector that runs after each editor mutation (and on focus / blur / attribute change):

| State | Source |
|---|---|
| `:focus`, `:focus-visible`, `:focus-within` | FocusManager |
| `:placeholder-shown` | `current_value_len == 0 && placeholder != null && !composing` |
| `:read-only` / `:read-write` | `readonly` attr / `disabled` attr presence |
| `:valid` / `:invalid` / `:in-range` / `:out-of-range` | constraint validation API (§3.11) |
| `:required` / `:optional` | `required` attr |

Set `needs_restyle = true` on transitions.

### 3.10 Native Context Menu

A new lightweight popup (reuse `<select>` dropdown infrastructure in [render_form.cpp](../../radiant/render_form.cpp)) with: Cut, Copy, Paste, Delete, Select All. Items are enabled/disabled based on selection / clipboard contents. Right-click event (already plumbed via mouse-button callback) opens it; Esc / outside click closes.

### 3.11 Constraint Validation (v1 minimum)

- `maxlength` — clamp on insert/paste/IME commit.
- `type="number"` — accept only `[0-9.eE+-]`; on blur, reject and set `:invalid`.
- `type="email"` / `"url"` — minimal pattern check on blur.
- `pattern="…"` — compile via [`radiant/re2_wrapper`](../../lambda/re2_wrapper.hpp) once at parse, evaluate on blur.

`form->custom_validity_msg` already exists; expose `setCustomValidity(msg)` from JS and `:invalid` flips when set.

### 3.12 Events Dispatched

Reused / new (all routed through the existing `dispatch_lambda_handler` machinery):

| Event | When |
|---|---|
| `beforeinput` | Before any value mutation; cancellable. `inputType` per [InputEvent spec](https://w3c.github.io/input-events/). |
| `input` | After mutation. |
| `change` | On blur or Enter when value differs from `value_at_focus`. |
| `selectionchange` | On selection mutation (already wired). |
| `compositionstart/update/end` | IME boundaries. |
| `focus` / `blur` / `focusin` / `focusout` | Existing. |
| `keydown` / `keyup` / `keypress` | Existing — must precede `beforeinput`. |
| `paste` / `copy` / `cut` | Cancellable; default = clipboard op. |
| `submit` | On Enter in single-line `<input>` if inside `<form>`. |

---

## 4. UI Automation Tests (`test/ui/`)

The existing JSON-driven runner in [event_sim.cpp](../../radiant/event_sim.cpp) already supports `type`, `key_press`, `key_combo`, `assert_value`, `assert_selection`, `assert_caret`, `click`, `select_option`, `check`, `wait`. We extend it minimally and add a focused suite.

### 4.1 Runner Additions

| New `event_sim` event | JSON shape | Maps to |
|---|---|---|
| `dblclick` | `{"type":"dblclick","target":{...}}` | two `mousedown`/`mouseup` within 500 ms; sets `click_count=2`. |
| `tripleclick` | `{"type":"tripleclick","target":{...}}` | three quick clicks. |
| `mouse_drag` | `{"type":"mouse_drag","from":{...},"to":{...},"steps":N}` | mousedown → N×mousemove → mouseup. |
| `paste_text` | `{"type":"paste_text","text":"…"}` | seed clipboard then dispatch Cmd+V to focused. |
| `copy` / `cut` | `{"type":"copy"}` / `{"type":"cut"}` | dispatch Cmd+C/X to focused; resulting clipboard readable via `assert_clipboard`. |
| `assert_clipboard` | `{"type":"assert_clipboard","equals":"…"}` | reads `clipboard_get_text`. |
| `assert_focused` | `{"type":"assert_focused","target":{...}}` | compares `tc_get_active_element`. |
| `assert_pseudo` | `{"type":"assert_pseudo","target":{...},"state":"focus-visible","present":true}` | reads pseudo-state bitmap. |
| `ime_compose` | `{"type":"ime_compose","preedit":"か","caret":1}` | drives `te::ime_update`. |
| `ime_commit` | `{"type":"ime_commit","text":"漢"}` | drives `te::ime_commit`. |

`type` and `key_press` already exist; no change.

### 4.2 Suite Layout

Place new tests under `test/ui/form/` for clarity:

```
test/ui/form/
  page_text_input.html
  page_textarea.html
  page_password.html
  page_form_with_submit.html

  input_caret_click.json          — single click positions caret at glyph boundary
  input_caret_drag.json           — drag selects, then collapse on click
  input_dblclick_word.json        — word selection
  input_tripleclick_all.json      — selects entire value
  input_keys_arrows.json          — Left/Right/Home/End, Shift extend
  input_keys_word.json            — Alt/Ctrl + arrows by word
  input_keys_line.json            — Cmd + arrows / Home / End
  input_backspace_delete.json     — char/word/line deletes
  input_select_all.json           — Cmd+A
  input_clipboard.json            — copy / cut / paste round-trip
  input_paste_strip_newlines.json — newlines collapsed in <input>
  input_maxlength.json            — typing & paste respect maxlength
  input_password_mask.json        — render snapshot + caret math
  input_undo_redo.json            — typing then Cmd+Z / Shift+Cmd+Z
  input_change_on_blur.json       — change fires only when value differs
  input_change_on_enter.json      — Enter commits, dispatches change
  input_submit_on_enter.json      — implicit form submission

  textarea_caret_vertical.json    — Up/Down sticky-X across lines
  textarea_pgup_pgdn.json
  textarea_dblclick_word.json
  textarea_tripleclick_line.json
  textarea_drag_multiline.json
  textarea_clipboard_multiline.json
  textarea_undo_redo.json
  textarea_scroll_into_view.json
  textarea_wrap_soft.json         — visual-line vs logical-line caret motion

  focus_tab_order.json            — Tab / Shift+Tab walks focusables in DOM order
  focus_visible_keyboard.json     — :focus-visible only after Tab
  focus_visible_mouse.json        — :focus-visible suppressed after mouse focus
  context_menu_basic.json         — right-click → menu items, Paste activates
  ime_compose_commit.json         — preedit shown, commit replaces
  ime_cancel.json                 — Esc cancels composition
  readonly_select_only.json       — selection works, typing blocked
  disabled_no_events.json         — neither caret nor typing
```

Each `.json` follows the existing pattern (see [test/ui/todo_text_input.json](../../test/ui/todo_text_input.json)). Render-baseline assertions reuse `{"type":"render","filename":"…"}` and the existing snapshot diff in `test/layout/`.

### 4.3 Sample Test — `input_dblclick_word.json`

```json
{
  "name": "Double-click selects a word in <input>",
  "html": "test/ui/form/page_text_input.html",
  "events": [
    {"type": "click",     "target": {"selector": "#name"}},
    {"type": "type",      "target": {"selector": "#name"}, "text": "Hello world from Lambda"},
    {"type": "wait",      "ms": 50},
    {"type": "dblclick",  "target": {"selector": "#name", "text_offset": 7}},
    {"type": "assert_selection", "is_collapsed": false},
    {"type": "assert_value",     "target": {"selector": "#name"}, "equals": "Hello world from Lambda"},
    {"type": "copy"},
    {"type": "assert_clipboard", "equals": "world"}
  ]
}
```

### 4.4 Driving the Suite

Add a `make ui-form` target that runs every `test/ui/form/*.json` through the existing runner (the same one used by `make layout`). Promote the suite to the baseline once green.

---

## 5. Phasing

| Phase | Deliverable | Success metric |
|---|---|---|
| **F1 — Refactor** | Extract `te::*` operations from `event.cpp`; add `EditHistory` skeleton; merge per-key branches. No behavior change. | All current tests still pass. |
| **F2 — Mouse parity** | `dblclick` / `tripleclick` / drag-by-word/line / right-click context menu. | New tests `input_dblclick_word.json`, `_tripleclick_all.json`, `context_menu_basic.json` pass. |
| **F3 — Keyboard parity** | Word- and line-granularity navigation & deletion; Up/Down in `<input>`; PgUp/PgDn; Enter semantics. | `input_keys_word.json`, `_keys_line.json`, `_backspace_delete.json`, `_change_on_enter.json` pass. |
| **F4 — Editor features** | Undo/redo, password masking, maxlength, auto-scroll, placeholder/`:placeholder-shown`. | `input_undo_redo.json`, `_password_mask.json`, `_maxlength.json` pass; render baselines updated. |
| **F5 — Events & validation** | `beforeinput`, `change`, constraint validation, `:valid` / `:invalid`. | `input_change_on_blur.json` and a new validity suite pass. |
| **F6 — Clipboard polish** | `paste_text`, `assert_clipboard`, `<input>` newline strip, paste maxlength clamp. | `input_clipboard.json`, `_paste_strip_newlines.json` pass. |
| **F7 — IME** | macOS `NSTextInputClient` shim + Win32 IMM; preedit render; composition events. | `ime_compose_commit.json`, `ime_cancel.json` pass on macOS & Win. |
| **F8 — Accessibility** | `:focus-visible`, ARIA reflection, focus ring polish, tab-trap utility. | `focus_visible_keyboard.json`, `focus_tab_order.json` pass. |

Each phase is independently shippable and adds test coverage in `test/ui/form/`.

---

## 6. File Touch List

| File | Action |
|---|---|
| [radiant/form_control.hpp](../../radiant/form_control.hpp) | Add `EditHistory`, `value_at_focus`, `scroll_x/y`, `caret_blink_t`, `caret_on`, `preedit_*`. |
| `radiant/text_edit.hpp` / `text_edit.cpp` | **New** — `te::*` operations, hit-testing, undo. |
| [radiant/text_control.cpp](../../radiant/text_control.cpp) | Extend `tc_set_value` to push history; honour `maxlength`. |
| [radiant/event.cpp](../../radiant/event.cpp) | Replace per-key branches with `te::*` calls; add dblclick / tripleclick / drag-mode tracking; right-click → context menu. |
| [radiant/render_form.cpp](../../radiant/render_form.cpp) | Password mask, placeholder polish, focus-visible ring, auto-scroll, IME preedit underline, context menu. |
| [radiant/clipboard.cpp](../../radiant/clipboard.cpp) | No structural change; ensure paste path passes through `te::paste`. |
| `radiant/ime_mac.mm` | **New** — `NSTextInputClient` shim. |
| `radiant/ime_win.cpp` | **New** — IMM bridge. |
| `radiant/ime_stub.cpp` | **New** — Linux/headless fallback. |
| [radiant/event_sim.cpp](../../radiant/event_sim.cpp) | Add `dblclick`, `tripleclick`, `mouse_drag`, `paste_text`, `copy`, `cut`, `assert_clipboard`, `assert_focused`, `assert_pseudo`, `ime_compose`, `ime_commit`. |
| [build_lambda_config.json](../../build_lambda_config.json) | Register new sources; per-platform IME file. |
| [test/ui/form/](../../test/ui/) | New `page_*.html` + `*.json` suite. |
| `Makefile` | Add `make ui-form`. |

---

## 7. Open Questions

1. **Undo granularity in `<textarea>`** — match Chrome (per-keystroke groups within 1.5 s) or Safari (per-token)? Default: Chrome.
2. **Linux IME** — ship XIM only, or take an IBus dependency? Default: stub for v1; revisit if user demand.
3. **Spellcheck** — defer to v2 (requires a dictionary subsystem).
4. **Drag-and-drop text into inputs** — defer to v2 (needs the larger HTML5 DnD model).
5. **`<input type="file|date|color">`** — out of scope; tracked separately under [Radiant_Form2.md](Radiant_Form2.md).
6. **Bidi caret** — schedule once `layout_text.cpp` exposes a logical→visual offset map.

---

## 8. Risks

- **History memory** — pathological pasting into `<textarea>`. Mitigation: cap snapshots at 64 entries and refuse to push when a single entry exceeds 1 MiB (just keep the previous and current as a delta).
- **IME shim binding to GLFW internals** — GLFW upgrades may break the macOS `NSView` subclass approach. Mitigation: isolate to `ime_mac.mm`, gate behind a build flag, and provide CI smoke tests using AppleScript-driven keyboard layout.
- **Event ordering with JS** — `beforeinput` must arrive *before* the value mutates, but the existing input-event path mutates first. Mitigation: route every mutation through `te::*` so the dispatch order is enforced in one place.
