# getComputedStyle Implementation

**Date:** 2026-04-03

---

## Overview

`getComputedStyle(elem, pseudo)` returns a lazy wrapper object. No style computation happens at creation time — properties are resolved on demand when accessed (e.g., `cs.color`, `cs.fontSize`).

---

## Architecture

### 1. Transpiler Recognition

**File:** `lambda/js/transpile_js_mir.cpp`

Both `window.getComputedStyle(elem)` and bare `getComputedStyle(elem)` calls are recognized at compile time and emit a direct call to `js_get_computed_style`.

- `jm_is_window_getComputedStyle()` — detects `window.getComputedStyle(...)` (member expression)
- Bare identifier check — detects `getComputedStyle(...)` (plain call)

### 2. Wrapper Creation — `js_get_computed_style`

**File:** `lambda/js/js_dom.cpp` (line ~209)

```
js_get_computed_style(Item elem_item, Item pseudo_item) → Item (Map wrapper)
```

Creates a lightweight **Map wrapper** with a sentinel marker:
- `wrapper->type` = `&js_computed_style_marker` (sentinel for detection)
- `wrapper->data` = `DomElement*` (the target element)
- `wrapper->data_cap` = pseudo-element type (0=none, 1=::before, 2=::after)

Pseudo-element string parsing strips leading colons, handling `"::before"`, `":before"`, and `"before"`.

Detection: `js_is_computed_style(item)` checks `Map->type == &js_computed_style_marker`.

### 3. Property Access — `js_computed_style_get_property`

**File:** `lambda/js/js_dom.cpp` (line ~241)

Called when JS reads a property on the computed style object (e.g., `cs.fontSize`).

**Steps:**

1. **Name conversion** — `js_camel_to_css_prop()` converts JS camelCase to CSS hyphenated form:
   - `fontSize` → `font-size`
   - `backgroundColor` → `background-color`
   - `cssFloat` → `float` (special case)

2. **Property ID lookup** — `css_property_id_from_name(css_prop)` → `CssPropertyId`

3. **Cascaded value retrieval** — tries `dom_element_get_specified_value(elem, prop_id)` which reads from the element's pre-cascaded `specified_style` StyleTree. For pseudo-elements, uses `dom_element_get_pseudo_element_value()`.

4. **On-demand cascade fallback** — if no cascaded value found, calls `js_match_element_property()` (see below).

5. **Special handling** — CSS `content` property: `normal` computes to `none` on `::before`/`::after`.

6. **Value serialization** — `css_value_to_string_item()` converts `CssValue*` to JS string.

### 4. On-Demand CSS Matching — `js_match_element_property`

**File:** `lambda/js/js_dom.cpp` (line ~319)

Performs a **mini-cascade** for a single element + property when the full CSS cascade hasn't run yet (JS executes before layout).

**Algorithm:**
1. Create a `SelectorMatcher` from the document pool
2. Iterate all stylesheets (`doc->stylesheets`) and their rules
3. For each style rule, test selector(s) against the element via `selector_matcher_matches()`
4. Filter by matching pseudo-element type
5. Find the requested `CssPropertyId` in the rule's declarations
6. Track the declaration with **highest specificity** (`css_specificity_compare()`)
7. Return the best-match declaration

### 5. Value Serialization — `css_value_to_string_item`

**File:** `lambda/js/js_dom.cpp` (line ~141)

| CSS Value Type | Output Format | Example |
|---|---|---|
| Keyword | enum name | `"block"`, `"normal"` |
| Length | value + unit | `"16px"`, `"1em"` |
| Percentage | value + `%` | `"50%"` |
| Number | numeric string | `"42"`, `"3.14"` |
| String | quoted | `"\"hello\""` |
| Color | rgb() | `"rgb(255, 0, 0)"` |

---

## Data Flow

```
JS: getComputedStyle(elem)
  → js_get_computed_style() → Map wrapper (lazy, no computation)

JS: cs.color
  → js_computed_style_get_property(wrapper, "color")
    → js_camel_to_css_prop("color") → "color"
    → css_property_id_from_name("color") → CSS_PROPERTY_COLOR
    → dom_element_get_specified_value(elem, CSS_PROPERTY_COLOR)
      → StyleTree lookup (inline styles, pre-cascaded values)
    → [fallback] js_match_element_property(elem, CSS_PROPERTY_COLOR, 0)
      → iterate stylesheets → selector matching → highest specificity
    → css_value_to_string_item(decl->value) → "rgb(255, 0, 0)"
```

---

## Current Limitation

**Stylesheet rules not resolved in JS-only mode:**

When the DOM is built for JS execution without a Radiant layout pass, the document's `stylesheets` array may not be populated. This means:

- **Inline styles** (`style="..."` attribute) — **work**. Parsed directly into `specified_style` during DOM construction.
- **Stylesheet rules** (`<style>` block or linked CSS) — **may not resolve**. The on-demand matcher `js_match_element_property` requires `doc->stylesheets` to be populated, which depends on the HTML parser extracting `<style>` content and the CSS parser building stylesheet objects.

Example: `#styled { color: red; }` in a `<style>` block may return `""` from `getComputedStyle` while `style="background-color: blue"` correctly returns `"blue"`.

---

## Key Files

| File | Role |
|---|---|
| `lambda/js/js_dom.cpp` | Runtime: wrapper creation, property access, on-demand matching, value serialization |
| `lambda/js/js_dom.h` | Declarations: `js_get_computed_style`, `js_computed_style_get_property`, `js_is_computed_style_item` |
| `lambda/js/transpile_js_mir.cpp` | Transpiler: recognizes `getComputedStyle()` calls, emits `js_get_computed_style` |
| `lambda/input/css/dom_element.cpp` | `dom_element_get_specified_value`, `dom_element_get_pseudo_element_value` |
| `lambda/input/css/selector_matcher.cpp` | `selector_matcher_matches` — CSS selector matching engine |
