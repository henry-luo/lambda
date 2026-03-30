# Radiant UI Automation — Phase 5+ Enhancement Proposal

This document proposes enhancements to the Radiant Event Simulator to achieve
comprehensive HTML UI interaction test coverage. It begins with a comparison
to industry-standard frameworks, identifies gaps, and proposes concrete
features to close them.

---

## Prior Art Analysis

### Industry Landscape

The dominant browser UI automation frameworks are **Selenium/WebDriver**,
**Playwright**, **Cypress**, and **Puppeteer**. Native UI frameworks have their
own test tools: **Qt Quick Test** for Qt, **XCTest** for Apple, **Espresso**
for Android. Radiant's event simulator occupies a unique niche — it's an
in-engine, declarative test tool for a custom layout/rendering engine.

### Detailed Comparison

| Dimension | **Radiant Event Sim** | **Selenium/WebDriver** | **Playwright** | **Cypress** | **Puppeteer** |
|---|---|---|---|---|---|
| **Architecture** | In-process, direct event dispatch | Out-of-process, browser protocol | Out-of-process, CDP/browser protocol | In-browser JS injection | Out-of-process, CDP |
| **Test format** | Declarative JSON | Imperative code (Java/Python/JS) | Imperative code (JS/TS/Python) | Imperative JS | Imperative JS |
| **Element targeting** | CSS selector, text search, raw coords | CSS/XPath selectors, link text | CSS, text, role, test-id locators | CSS, contains, custom commands | CSS, XPath |
| **Assertions** | Built-in event types (`assert_text`, etc.) | External libraries (JUnit, pytest) | Built-in `expect()` API | Chai/built-in `should()` | External (Jest, Mocha) |
| **Visual capture** | `render` to PNG/SVG at any step | Screenshot API | Screenshot + video recording | Screenshot + video | Screenshot API |
| **Viewport resize** | `resize` event with relayout | `set_window_size()` | `setViewportSize()` | `cy.viewport()` | `page.setViewport()` |
| **Auto-waiting** | Explicit `wait` events | Explicit/implicit waits | Auto-waiting built-in | Auto-retry on assertions | Manual waits |
| **No scripting needed** | Yes — pure JSON | No | No | No | No |
| **Runs headless** | `--no-window` flag | Headless Chrome/FF | Built-in | Headed only* | Built-in |
| **Page navigation** | Not yet supported | Full browser navigation | Full navigation + interceptors | Full navigation | Full navigation |
| **Computed styles** | Not yet exposed | `getComputedStyle()` | `toHaveCSS()` | `css()` command | `evaluate()` + DOM | 
| **Cross-element geometry** | Not yet supported | Manual JS via `getBoundingClientRect()` | `boundingBox()` method | Manual JS | `boundingBox()` method |
| **iframe interaction** | Not yet supported | `switchTo().frame()` | `frameLocator()` | `cy.iframe()` plugin | `contentFrame()` |
| **Network layer** | N/A (no network stack) | Full browser network | Interception + mocking | Stubbing | Interception |
| **Cross-browser** | Radiant engine only | Chrome, Firefox, Safari, Edge | Chromium, Firefox, WebKit | Chromium only | Chromium only |
| **Parallel execution** | Sequential | Grid/parallel drivers | Parallel workers | Parallel spec files | Manual |

\* Cypress 12+ added component testing with headless support.

### Strengths of Radiant Event Sim

1. **Zero-code, declarative** — Tests are pure JSON arrays of events and
   assertions. No programming language, no test runner boilerplate, no
   imports. Closest analogue: Selenium IDE recordings, but more structured.

2. **Engine-level fidelity** — Events go directly into `handle_event()`,
   hitting the exact same code path as real user interaction. No browser
   protocol translation layer. Analogous to Qt Quick Test for native UI.

3. **Deep internal assertions** — `assert_caret` (char offset, view type),
   `assert_selection`, `assert_scroll`, `assert_state` expose internal engine
   state that browser frameworks can't access without DOM inspection hacks.

4. **Visual regression built-in** — `render` captures a pixel-perfect
   PNG/SVG at any step. Browser frameworks need separate tools (Percy,
   Chromatic, BackstopJS).

5. **Lightweight** — No browser process, no protocol bridge, no Node.js
   runtime. A 22-test suite runs in ~50s including compilation.

### Gaps to Address

| Gap | Severity | Notes |
|-----|----------|-------|
| Cross-element geometry assertions | High | Can't compare positions/sizes of two elements |
| Computed style assertions | High | Can't verify CSS property values after resolution |
| Page navigation | Medium | No `goto(url)` — each test is a single document |
| Auto-waiting | Medium | Explicit `wait` is fragile; assertions should retry |
| Hover interaction testing | Low | `mouse_move` + `assert_state :hover` works but no style verification |
| iframe interaction | Medium | Iframes render but events don't forward into them |
| Zoom / pinch-zoom | Low | CSS `zoom` property + transforms exist but no test coverage |
| Element bounding box assertion | High | Can't assert position/size of an element |
| Tooltip / title attribute | Low | No native tooltip rendering |
| HTML5 drag-and-drop | Low | `mouse_drag` for selection only; no `dragstart`/`drop` events |

---

## Proposed Enhancements

### Phase 5a — Cross-Element and Geometry Assertions

Add assertions that verify element positions, sizes, and spatial relationships.
This is one of the highest-value additions — it enables layout regression
testing at the interaction level.

#### `assert_rect` — verify element bounding box

Assert the position and/or size of an element in CSS pixels.

```json
{"type": "assert_rect", "target": {"selector": "#sidebar"},
 "x": 0, "y": 60, "width": 250, "height": 740, "tolerance": 2}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `target` | object | — | Element to measure |
| `x`, `y` | float | — | Expected position (CSS px, document coords). Omit to skip. |
| `width`, `height` | float | — | Expected size. Omit to skip. |
| `tolerance` | float | 1 | Allowed deviation in pixels |

**Implementation**: Walk the parent chain of the target `View*` to compute
absolute position (reuse `get_element_center_abs` logic but return origin
instead of center). Read `view->width` and `view->height` directly.

#### `assert_style` — verify computed CSS property

Assert the resolved value of a CSS property on an element.

```json
{"type": "assert_style", "target": {"selector": "h1"},
 "property": "font-size", "equals": "32px"}
{"type": "assert_style", "target": {"selector": ".box"},
 "property": "background-color", "equals": "rgb(255, 0, 0)"}
{"type": "assert_style", "target": {"selector": "#hidden"},
 "property": "display", "equals": "none"}
```

| Field | Type | Description |
|-------|------|-------------|
| `target` | object | Element to query |
| `property` | string | CSS property name |
| `equals` | string | Expected resolved value (serialized) |
| `contains` | string | Substring match on serialized value |

**Supported properties (initial set):**

| Property | Source | Serialization |
|----------|--------|---------------|
| `display` | `BlockProp->display` | `"block"`, `"inline"`, `"flex"`, `"none"`, etc. |
| `visibility` | `BlockProp->visibility` | `"visible"`, `"hidden"`, `"collapse"` |
| `position` | `PositionProp->position` | `"static"`, `"relative"`, `"absolute"`, `"fixed"`, `"sticky"` |
| `font-size` | `FontProp->size_px` | `"{N}px"` |
| `font-weight` | `FontProp->weight` | `"400"`, `"700"`, etc. |
| `font-family` | `FontProp->family` | `"Arial, sans-serif"` |
| `color` | `FontProp->color` | `"rgb(R, G, B)"` or `"rgba(R, G, B, A)"` |
| `background-color` | `BackgroundProp->color` | `"rgb(R, G, B)"` |
| `text-align` | `BlockProp->text_align` | `"left"`, `"center"`, `"right"`, `"justify"` |
| `overflow` | `ScrollProp->overflow_x/y` | `"visible"`, `"hidden"`, `"scroll"`, `"auto"` |
| `opacity` | `BlockProp->opacity` | `"0.5"` |
| `z-index` | `PositionProp->z_index` | `"auto"` or integer string |
| `box-sizing` | `BlockProp->box_sizing` | `"content-box"`, `"border-box"` |
| `border-*-width` | `BorderProp->top/right/bottom/left` | `"{N}px"` |
| `margin-*` | `BlockProp->margin_*` | `"{N}px"` or `"auto"` |
| `padding-*` | `BlockProp->padding_*` | `"{N}px"` |

Additional properties can be added incrementally. The implementation reads
the resolved prop structs on `ViewBlock*` and serializes to string for
comparison.

**Implementation**: Add a `get_computed_style(View*, const char* property)`
function that dispatches on property name to read the appropriate prop struct
field and serializes the value into a `StrBuf`.

#### `assert_position` — verify relative positioning of two elements

Assert spatial relationship between two elements.

```json
{"type": "assert_position",
 "element_a": {"selector": "#header"},
 "element_b": {"selector": "#content"},
 "relation": "above", "tolerance": 2}
```

| Field | Type | Description |
|-------|------|-------------|
| `element_a` | object | First element (target spec) |
| `element_b` | object | Second element (target spec) |
| `relation` | string | `"above"`, `"below"`, `"left_of"`, `"right_of"`, `"overlaps"`, `"contains"`, `"inside"` |
| `gap` | float | Optional: assert minimum gap between elements |
| `tolerance` | float | Allowed deviation (default: 1px) |

Relation semantics (using bounding boxes A and B):

| Relation | Condition |
|----------|-----------|
| `above` | `A.bottom <= B.top + tolerance` |
| `below` | `A.top >= B.bottom - tolerance` |
| `left_of` | `A.right <= B.left + tolerance` |
| `right_of` | `A.left >= B.right - tolerance` |
| `overlaps` | Bounding boxes intersect |
| `contains` | A fully contains B |
| `inside` | A is fully inside B |

**Implementation**: Compute absolute bounding rects for both elements via
`get_element_rect_abs()`, then compare based on `relation`.

---

### Phase 5b — Page Navigation

Add a `navigate` event type that loads a new HTML document, replacing the
current page. This enables multi-page test workflows (e.g., click link →
verify landing page → go back).

#### `navigate` — load a new document

```json
{"type": "navigate", "url": "test/ui/page2.html"}
{"type": "navigate", "url": "test/layout/data/baseline/baseline_201_font_sizes.html"}
```

| Field | Type | Description |
|-------|------|-------------|
| `url` | string | Path to HTML file (relative to project root) |

**Implementation**: Call `load_html_doc()` (defined in `cmd_layout.cpp`)
with the new URL, viewport dimensions from `UiContext`, and `pixel_ratio`.
Replace `uicon->document` with the returned `DomDocument*`, then call
`layout_html_doc()` and `render_html_doc()` to complete the load.

```c
case SIM_EVENT_NAVIGATE: {
    const char* url = ev->navigate_url;
    DomDocument* new_doc = load_html_doc(NULL, (char*)url,
        uicon->viewport_width, uicon->viewport_height, uicon->pixel_ratio);
    if (!new_doc) {
        log_error("event_sim: navigate failed to load '%s'", url);
        ctx->fail_count++;
        break;
    }
    uicon->document = new_doc;
    layout_html_doc(uicon, new_doc, false);
    render_html_doc(uicon, new_doc->view_tree, NULL);
    log_info("event_sim: navigated to '%s'", url);
    break;
}
```

#### `navigate_back` — go to previous document (stretch goal)

Would require maintaining a navigation history stack:

```json
{"type": "navigate_back"}
```

This is lower priority — defer unless multi-page workflows demand it.

#### `assert_url` — verify current document URL

```json
{"type": "assert_url", "contains": "page2.html"}
```

---

### Phase 5c — Auto-Waiting on Assertions

Add an automatic retry mechanism for assertions, similar to Playwright's
`expect()` with built-in timeout. This eliminates brittle `wait` events
that guess timing.

#### Design

Add an optional `timeout` field to all `assert_*` events:

```json
{"type": "assert_text", "target": {"selector": "#result"},
 "contains": "Success", "timeout": 2000}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `timeout` | int | 0 | Maximum wait time in ms. 0 = no retry (current behaviour). |
| `interval` | int | 100 | Retry interval in ms (how often to re-check). |

When `timeout > 0`, the assertion logic becomes:

```
start = now()
while (now() - start < timeout) {
    if (assertion_passes()) return PASS;
    sleep(interval);
    // optionally: pump events / reflow if needed
}
return FAIL;
```

This applies to all assertion types: `assert_text`, `assert_visible`,
`assert_checked`, `assert_value`, `assert_focus`, `assert_state`,
`assert_scroll`, `assert_rect`, `assert_style`, `assert_position`.

**Implementation**: Wrap the existing assertion functions in a retry loop.
The loop sleeps and retries on failure. No events are pumped during the wait
(the document state is static between explicit events), so this is primarily
useful after `navigate` or `resize` events where rendering may not be
complete synchronously.

#### Global timeout default

Add a top-level `"default_timeout"` to the JSON metadata:

```json
{
  "name": "Auto-wait test",
  "html": "test/ui/page.html",
  "default_timeout": 1000,
  "events": [
    {"type": "assert_text", "target": {"selector": "#result"}, "contains": "Done"}
  ]
}
```

When `default_timeout` is set, all assertions use it unless overridden by
a per-event `timeout: 0` (which disables retry for that assertion).

---

### Phase 5d — Hover Interaction Testing

The hover mechanism already works (`mouse_move` + `assert_state :hover`), but
we lack assertions on hover-triggered style changes (e.g., background color
changes on hover). With `assert_style` from Phase 5a, hover testing becomes
fully expressive.

#### Example: hover style verification

```json
{"type": "assert_style", "target": {"selector": ".btn"},
 "property": "background-color", "equals": "rgb(200, 200, 200)"},

{"type": "mouse_move", "target": {"selector": ".btn"}},
{"type": "wait", "ms": 50},

{"type": "assert_state", "target": {"selector": ".btn"},
 "state": ":hover", "value": true},
{"type": "assert_style", "target": {"selector": ".btn"},
 "property": "background-color", "equals": "rgb(100, 149, 237)"}
```

**Implementation**: No new event types needed. `assert_style` (Phase 5a) +
existing `mouse_move` and `assert_state` compose naturally.

#### Hover test HTML

```html
<style>
  .btn { background-color: rgb(200, 200, 200); padding: 10px; }
  .btn:hover { background-color: rgb(100, 149, 237); color: white; }
</style>
<button class="btn" id="hover-btn">Hover Me</button>
```

**Prerequisite**: Confirm that pseudo-class `:hover` rule resolution triggers
a style recalculation and re-render. Check `resolve_css_style.cpp` handles
`:hover` matching when `PSEUDO_STATE_HOVER` is set on the element.

---

### Phase 5e — Focus Interaction Testing

Like hover, focus interaction is already functional (`click` to focus,
`assert_focus`, `assert_state :focus`). With `assert_style`, we can
additionally verify `:focus` and `:focus-visible` styling.

#### `assert_focus` enhancements

The existing `assert_focus` compares the element against the current focus
target. Add a negative form:

```json
{"type": "assert_focus", "target": {"selector": "#input1"}, "focused": true}
{"type": "assert_focus", "target": {"selector": "#input1"}, "focused": false}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `focused` | bool | true | If false, assert the element does NOT have focus |

#### Tab order testing

Currently, `focus_move()` (tab navigation) is marked as "not yet implemented"
in `state_store.cpp`. Once implemented:

```json
{"type": "click", "target": {"selector": "#input1"}},
{"type": "assert_focus", "target": {"selector": "#input1"}},
{"type": "key_press", "key": "tab"},
{"type": "assert_focus", "target": {"selector": "#input2"}},
{"type": "key_combo", "key": "tab", "mods_str": "shift"},
{"type": "assert_focus", "target": {"selector": "#input1"}}
```

**Dependency**: Implement `focus_move()` in `state_store.cpp` with DOM order
traversal of focusable elements (inputs, buttons, anchors, `[tabindex]`).

#### Focus-within testing

```json
{"type": "click", "target": {"selector": "#input-inside-form"}},
{"type": "assert_state", "target": {"selector": "form"},
 "state": ":focus-within", "value": true}
```

Already supported by the engine — `:focus-within` is propagated up the
ancestor chain in `focus_set()`.

---

### Phase 5f — iframe Interaction

Enable events inside `<iframe>` content. Currently, iframes render their
nested `DomDocument*` via `EmbedProp->doc`, but events don't forward
into the nested document.

#### `switch_frame` — change event target to iframe content

```json
{"type": "switch_frame", "target": {"selector": "iframe#preview"}},
{"type": "click", "target": {"selector": "#inner-button"}},
{"type": "assert_text", "target": {"selector": "#inner-result"}, "contains": "Clicked"},
{"type": "switch_frame", "target": "main"}
```

| Field | Type | Description |
|-------|------|-------------|
| `target` | object or string | CSS selector for iframe element, or `"main"` to return to parent |

**Implementation**: When `switch_frame` is executed, push the current
`DomDocument*` onto a frame stack, then set `uicon->document` to the nested
`EmbedProp->doc`. All subsequent events and assertions operate on the nested
document. `"main"` pops the stack and restores the parent document.

Event coordinates must be translated: subtract the iframe's absolute position
from mouse coordinates so they're relative to the iframe viewport.

```c
case SIM_EVENT_SWITCH_FRAME: {
    if (ev->target_selector && strcmp(ev->target_selector, "main") == 0) {
        // Pop frame stack
        if (ctx->frame_stack_depth > 0) {
            ctx->frame_stack_depth--;
            uicon->document = ctx->frame_stack[ctx->frame_stack_depth];
        }
    } else {
        View* iframe_view = find_element_by_selector(uicon->document, ev->target_selector);
        DomElement* elem = iframe_view ? ((DomElement*)iframe_view->dom_node) : NULL;
        EmbedProp* embed = elem ? get_embed_prop(iframe_view) : NULL;
        if (embed && embed->doc) {
            ctx->frame_stack[ctx->frame_stack_depth++] = uicon->document;
            uicon->document = embed->doc;
        } else {
            log_error("event_sim: switch_frame - iframe not found or has no content");
            ctx->fail_count++;
        }
    }
    break;
}
```

**Frame stack**: Add to `EventSimContext`:
```c
DomDocument* frame_stack[8];  // max nesting depth
int frame_stack_depth;
```

---

### Phase 5g — Zoom and Transform Testing

Radiant fully resolves CSS transforms (2D and 3D), `zoom`, and
`transform-origin`. Test coverage ensures hit-testing works correctly on
transformed elements.

#### Test scenarios

```json
{"type": "log", "message": "Verify click lands correctly on scaled element"},
{"type": "assert_style", "target": {"selector": ".scaled"},
 "property": "transform", "contains": "scale(2)"},
{"type": "click", "target": {"selector": ".scaled"}},
{"type": "assert_state", "target": {"selector": ".scaled"},
 "state": ":active", "value": true}
```

```json
{"type": "log", "message": "Verify transformed element bounding box"},
{"type": "assert_rect", "target": {"selector": ".rotated"},
 "width": 200, "height": 200, "tolerance": 5}
```

**No new event types needed.** `assert_style` + `assert_rect` from Phase 5a
cover transform verification. The key engineering work is ensuring hit-testing
applies inverse transforms to mouse coordinates (check that
`handle_mouse_button` in `event.cpp` accounts for CSS transforms in the
coordinate mapping).

#### CSS zoom support

```json
{"type": "assert_style", "target": {"selector": "body"},
 "property": "zoom", "equals": "1.5"}
```

**Prerequisite**: Verify `zoom` CSS property is resolved and stored. If not
already tracked, add to the style resolution pipeline.

---

### Phase 5h — Assert View at Position

Add an assertion that checks what element exists at a given coordinate. This
is the inverse of element-targeting — instead of finding where an element is,
verify what's at a known position.

#### `assert_element_at` — verify element at coordinates

```json
{"type": "assert_element_at", "x": 500, "y": 300,
 "selector": "#main-content"}
{"type": "assert_element_at", "x": 500, "y": 300,
 "tag": "div"}
{"type": "assert_element_at", "x": 100, "y": 50,
 "text_contains": "Header"}
```

| Field | Type | Description |
|-------|------|-------------|
| `x`, `y` | int | Coordinates in CSS pixels (viewport-relative) |
| `selector` | string | Expected element matches this CSS selector |
| `tag` | string | Expected element tag name |
| `text_contains` | string | Expected element contains this text |

**Implementation**: Perform hit-testing at `(x, y)` using the same
`handle_mouse_button` coordinate resolution path. Compare the hit element
against the expected selector/tag/text.

This is especially useful for:
- Verifying z-index stacking (which element is "on top" at a point)
- Testing that fixed/sticky elements remain at expected positions after scroll
- Confirming that click events will reach the intended element

---

### Phase 5i — Comprehensive HTML Interaction Coverage

The following test scenarios cover the full spectrum of HTML UI interactions
that Radiant should support. Each references the event types needed.

#### Form elements (expand existing coverage)

| Element | Interactions | Events | Assertions |
|---------|-------------|--------|------------|
| `<input type="text">` | Click to focus, type, clear | `click`, `type` | `assert_value`, `assert_focus` |
| `<input type="password">` | Same as text | `click`, `type` | `assert_value` (masked) |
| `<input type="number">` | Type, arrow keys | `click`, `type`, `key_press` | `assert_value` |
| `<input type="range">` | Drag slider thumb | `mouse_drag` | `assert_value` |
| `<textarea>` | Multi-line type, scroll | `click`, `type`, `scroll` | `assert_value`, `assert_scroll` |
| `<select>` | Open dropdown, select option | `select_option` | `assert_value` |
| `<select multiple>` | Multi-select | `select_option` (multiple) | `assert_value` |
| `<input type="checkbox">` | Toggle | `check` | `assert_checked` |
| `<input type="radio">` | Select, group exclusion | `check` | `assert_checked` (group) |
| `<button>` | Click | `click` | `assert_state :active` |
| `<form>` | Submit | `key_combo` Enter / `click` submit | `assert_url` (navigation) |

#### Interactive elements

| Element | Interactions | Events | Assertions |
|---------|-------------|--------|------------|
| `<a href>` | Click → navigate | `click`, `navigate` | `assert_url` |
| `<details>/<summary>` | Click to expand/collapse | `click` | `assert_visible`, `assert_state` |
| `<dialog>` | Open/close modal | `click` | `assert_visible`, `assert_style display` |
| `contenteditable` | Type, select, delete | `click`, `type`, `key_combo` | `assert_text` |
| `<img>` | Click (verify no crash) | `click` | `assert_caret` or `assert_target` |
| `<video>/<audio>` | Play controls | `click` | `assert_state` |
| `<canvas>` | Click on canvas area | `click` | `assert_element_at` |

#### Layout interaction scenarios

| Scenario | Description | Events | Assertions |
|----------|-------------|--------|------------|
| Scroll + click | Scroll to off-screen element, click it | `scroll`, `click` | `assert_text`, `assert_visible` |
| Resize + reflow | Resize viewport, verify wrapping changes | `resize` | `assert_rect`, `assert_style` |
| Fixed header | Scroll, verify header stays at top | `scroll` | `assert_rect y: 0`, `assert_element_at` |
| Sticky positioning | Scroll past threshold, element sticks | `scroll` | `assert_rect`, `assert_position` |
| Overflow scroll | Scroll inside overflow container | `scroll` (on container) | `assert_scroll`, `assert_visible` |
| Flex reorder | Resize causes flex wrap | `resize` | `assert_position` (order changes) |
| Grid responsive | Media query breakpoint changes layout | `resize` | `assert_rect`, `assert_style display` |
| Z-index stacking | Overlapping elements, verify top one | `click` | `assert_element_at` |
| Transform hit-test | Click on rotated/scaled element | `click` | `assert_state :active` |

#### Keyboard interaction scenarios

| Scenario | Description | Events | Assertions |
|----------|-------------|--------|------------|
| Select all | Cmd+A | `key_combo` | `assert_selection is_collapsed: false` |
| Copy/paste | Cmd+C, Cmd+V | `key_combo` | `assert_text` (after paste) |
| Undo/redo | Cmd+Z, Cmd+Shift+Z | `key_combo` | `assert_text` |
| Tab navigation | Tab through form fields | `key_press tab` | `assert_focus` (sequential) |
| Arrow keys | Navigate within text | `key_press` | `assert_caret char_offset` |
| Home/End | Jump to line start/end | `key_press` | `assert_caret` |
| Escape | Close dropdown/dialog | `key_press escape` | `assert_visible false` |
| Enter | Submit form / activate button | `key_press enter` | `assert_url` / `assert_state` |

#### Mouse interaction scenarios

| Scenario | Description | Events | Assertions |
|----------|-------------|--------|------------|
| Click | Single click on element | `click` | `assert_caret`, `assert_state :active` |
| Double-click | Select word | `dblclick` | `assert_selection is_collapsed: false` |
| Triple-click | Select paragraph (if supported) | `click` ×3 | `assert_selection` |
| Right-click | Context menu trigger | `click button: 2` | `assert_state` |
| Hover in/out | Mouse enter and leave | `mouse_move` (in/out) | `assert_state :hover` |
| Drag select | Drag to select text | `mouse_drag` | `assert_selection` |
| Scroll wheel | Scroll content | `scroll` | `assert_scroll` |
| Drag-and-drop | Drag element to target (HTML5) | `mouse_drag` | `assert_position` |

---

## Implementation Plan

### Priority ordering

| Phase | Feature | Effort | Value | Priority |
|-------|---------|--------|-------|----------|
| 5a | `assert_rect` | Small | High | **P0** — enables layout regression |
| 5a | `assert_style` | Medium | High | **P0** — enables style verification |
| 5a | `assert_position` | Small | Medium | **P1** — depends on `assert_rect` |
| 5b | `navigate` | Medium | Medium | **P1** — enables multi-page tests |
| 5c | Auto-waiting | Small | Medium | **P1** — reduces test flakiness |
| 5d | Hover style tests | None | Medium | **P1** — free once `assert_style` exists |
| 5e | Focus / tab order | Medium | Medium | **P2** — needs `focus_move()` impl |
| 5h | `assert_element_at` | Small | Medium | **P2** — inverse hit-testing |
| 5f | `switch_frame` (iframe) | Large | Low | **P3** — complex; limited use cases |
| 5g | Zoom/transform tests | None | Low | **P3** — free once `assert_rect` + `assert_style` exist |
| 5b | `navigate_back` | Small | Low | **P3** — stretch goal |

### New SimEventType values

```c
SIM_EVENT_ASSERT_RECT,        // Phase 5a
SIM_EVENT_ASSERT_STYLE,       // Phase 5a
SIM_EVENT_ASSERT_POSITION,    // Phase 5a
SIM_EVENT_NAVIGATE,           // Phase 5b
SIM_EVENT_ASSERT_URL,         // Phase 5b
SIM_EVENT_ASSERT_ELEMENT_AT,  // Phase 5h
SIM_EVENT_SWITCH_FRAME,       // Phase 5f
```

### New SimEvent fields

```c
// assert_rect
float expected_x, expected_y;     // expected position
float expected_width, expected_height;  // expected size
float rect_tolerance;             // pixel tolerance

// assert_style
char* style_property;             // CSS property name
char* style_equals;               // expected serialized value
char* style_contains;             // substring match

// assert_position
char* element_a_selector;        // first element
char* element_a_text;
char* element_b_selector;        // second element
char* element_b_text;
char* position_relation;         // "above", "below", "left_of", etc.
float position_gap;              // minimum gap

// navigate
char* navigate_url;              // file path to load

// assert_element_at
char* expected_tag;              // expected element tag
char* expected_at_selector;     // expected element matches selector
char* expected_at_text;         // expected element contains text

// auto-wait (on all assert_ types)
int assert_timeout;              // ms, 0 = no retry (default)
int assert_interval;             // retry interval ms (default 100)
```

### New EventSimContext fields

```c
// iframe support
DomDocument* frame_stack[8];
int frame_stack_depth;

// auto-wait
int default_timeout;             // from JSON "default_timeout"
```

### Files to modify

| File | Changes |
|------|---------|
| `event_sim.hpp` | Add new enum values, SimEvent fields, EventSimContext fields |
| `event_sim.cpp` | JSON parsing + execution handlers for all new types |
| `event_sim.cpp` | Add `get_element_rect_abs()`, `get_computed_style()` helpers |
| `event_sim.cpp` | Add retry loop wrapper for auto-waiting |

### Test files to create

| Phase | HTML | Event JSON | Description |
|-------|------|------------|-------------|
| 5a | `test_layout_assertions.html` | `ui_phase5a_rect.json` | `assert_rect` on positioned elements |
| 5a | `test_style_assertions.html` | `ui_phase5a_style.json` | `assert_style` on various CSS properties |
| 5a | `test_layout_assertions.html` | `ui_phase5a_position.json` | `assert_position` between elements |
| 5b | `test_nav_page1.html` + `page2.html` | `ui_phase5b_navigate.json` | Navigate between pages, assert content |
| 5c | `test_scroll.html` | `ui_phase5c_auto_wait.json` | Assert with timeout after resize |
| 5d | `test_hover_style.html` | `ui_phase5d_hover_style.json` | Hover + verify style changes |
| 5e | `test_tab_order.html` | `ui_phase5e_tab_order.json` | Tab through form fields |
| 5h | `test_stacking.html` | `ui_phase5h_element_at.json` | Verify z-index stacking |

### Acceptance criteria

1. `assert_rect` can verify element position and size with ±2px tolerance.
2. `assert_style` can read at least 15 CSS properties from resolved view.
3. `assert_position` correctly identifies spatial relationships between
   elements (above, below, left_of, right_of, overlaps, contains, inside).
4. `navigate` loads a new HTML document and all subsequent assertions
   operate on the new document.
5. Auto-waiting with `timeout: 2000` on `assert_text` retries up to 2s
   before failing.
6. Hover + `assert_style` confirms CSS `:hover` rules are applied.
7. `assert_element_at` correctly identifies the topmost element at a
   given coordinate after z-index stacking.
8. All existing 22 tests continue to pass (backward compatible).
9. Animation testing remains deferred.
