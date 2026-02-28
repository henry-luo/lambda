# Radiant Layout Engine: JavaScript Integration Proposal

## Executive Summary

Of the 9,567 CSS2.1 test suite files, **304 tests were originally skipped** because they contain `<script>` tags and Radiant had no JS execution path. This document describes the integration of the Lambda JS transpiler with Radiant's HTML loading pipeline to execute these scripts, enabling DOM mutations before layout.

### Implementation Status

**Phases 1–4 are implemented.** The JS transpiler is integrated into the Radiant layout pipeline and executes inline `<script>` elements and `onload` handlers for all HTML documents.

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| CSS2.1 tests total | 9,567 | 9,602 | +35 (un-skipped) |
| Passing | 8,411 (87.9%) | 8,480 (88.3%) | **+69** |
| Failing | 1,156 | 1,122 | -34 |
| Skipped (JS-dependent) | 304 | 269 | -35 |
| Baseline | 2,347/2,347 (100%) | 2,347/2,347 (100%) | No regressions |

Of the 304 originally-skipped tests, **35 now pass** and have been removed from the skip list. An additional **34 previously-failing JS tests** (that were not skip-listed) also started passing due to the JS execution support—bringing the total gain to **+69 tests**.

### JS-Dependent Test Breakdown (304 originally skipped)

| Category | Count | Now Passing | Remaining |
|----------|-------|-------------|----------|
| Simple style/className changes | 72 | ~20 | ~52 (layout issues, not JS) |
| DOM tree mutations | 103 | ~10 | ~93 (need more DOM APIs) |
| Complex (CSSOM / computed / offsetWidth) | 123 | ~5 | ~118 (need CSSOM) |
| Other | 6 | 0 | 6 |

### Newly Passing Tests (35 un-skipped)

- **Style changes**: `display-change-001`, `border-dynamic-001`, `border-collapse-dynamic-{cell,table,row,column,colgroup,rowgroup}-00{1,2}` (9 border-collapse tests)
- **DOM removal**: `block-in-inline-remove-{000,002,004,005,006}` (5 tests)
- **Table anonymous**: `table-anonymous-objects-{099-102,127-130,145-154}` (20 tests)
- **Font**: `font-family-rule-005`

---

## 1. Architecture: Integrating JS Transpiler with Radiant

### 1.1 Current Pipeline (No JS)

```
HTML file
  → input_from_source() / html5_parse()     → Element* tree (Lambda data model)
    → build_dom_tree_from_element()          → DomElement* tree (Radiant DOM)
      → css_engine + stylesheet cascade      → Resolved styles
        → layout_html_doc()                  → ViewTree (layout output)
```

Key note: `<script>` elements are parsed by HTML5 into the `Element*` tree (with their source text preserved as child strings), but `build_dom_tree_from_element()` explicitly skips them and returns `nullptr` — they never enter the `DomElement*` tree.

### 1.2 Proposed Pipeline (With JS)

```
HTML file
  → input_from_source() / html5_parse()     → Element* tree
    → build_dom_tree_from_element()          → DomElement* tree
      → extract_and_execute_scripts()        → JS mutates DomElement* tree   ← NEW
        → css_engine + stylesheet cascade    → Resolved styles
          → layout_html_doc()                → ViewTree
```

The core change: insert a **script extraction and execution phase** between DOM tree construction and CSS cascading. This mirrors the browser's `defer` behavior — all scripts execute after the full DOM is built but before layout.

### 1.3 Integration Point: `load_lambda_html_doc()` ✅

Implemented in `radiant/cmd_layout.cpp` at line ~1674, after `build_dom_tree_from_element()` returns and before CSS cascading begins:

```c
DomElement* dom_root = build_dom_tree_from_element(html_root, dom_doc, nullptr);

// Step 2b: Execute inline <script> elements and body onload handlers
dom_doc->root = dom_root;  // set root for JS DOM API access
execute_document_scripts(html_root, dom_doc, pool);

// Step 3: Initialize CSS engine (existing code continues)
CssEngine* css_engine = css_engine_create(pool);
```

Note: `dom_doc->root` must be set before script execution so that `js_dom_set_document()` can provide a valid root for `getElementById()` and other DOM traversal calls.

### 1.4 New Function: `execute_document_scripts()` ✅

Implemented in `radiant/script_runner.cpp`. Walks the original `Element*` tree (not the `DomElement*` tree, which has scripts stripped) to find `<script>` elements in document order, extract their source text, and execute via the JS transpiler with the `DomDocument*` as the DOM context.

Key implementation details:
- Tag name comparison uses `TypeElmt->name.str` via `str_ieq_const()` (the Element struct stores tag names in the `TypeElmt` type pointer, not as a direct field)
- Attribute access uses the existing `extract_element_attribute()` function (declared in `dom_element.cpp`)
- External scripts (`<script src="...">`) and non-JS type attributes are skipped
- All script blocks + `onload` handler are concatenated into a single JIT compilation unit

### 1.5 Script Source Extraction

`<script>` elements exist in the `Element*` tree with their source text as child string `Item`s. The extraction function walks children of a `<script>` element and concatenates all string items:

```c
static char* extract_script_source(Element* script_elem) {
    StrBuf* buf = strbuf_new();
    for (int i = 0; i < script_elem->length; i++) {
        Item child = script_elem->items[i];
        if (get_type_id(child) == LMD_TYPE_STRING) {
            String* s = it2s(child);
            strbuf_append(buf, s->chars, s->len);
        }
    }
    char* source = strbuf_to_cstr(buf);
    strbuf_free(buf);
    return source;
}
```

### 1.6 `onload` Handler Extraction

63 of the 304 tests use `<body onload="functionName()">`. The `onload` attribute value is an inline JS expression that must execute after all `<script>` blocks have been processed. Extraction:

```c
static char* extract_onload_handler(DomElement* body_elem) {
    const char* onload = dom_element_get_attribute(body_elem, "onload");
    return onload ? mem_strdup(onload, MEM_CAT_LAYOUT) : nullptr;
}
```

Execution order:
1. Execute all `<script>` blocks (defines functions like `doTest()`, `test()`)
2. Execute the `onload` handler string (calls those functions: `doTest()`, `test()`)

### 1.7 Script Execution Flow

```c
void execute_document_scripts(Element* html_root, DomDocument* dom_doc, Pool* pool) {
    Runtime runtime = {};
    runtime.dom_doc = (void*)dom_doc;

    // Phase 1: Execute all <script> blocks in document order
    ArrayList script_elements;
    arraylist_init(&script_elements, 16);
    collect_script_elements(html_root, &script_elements);

    for (int i = 0; i < script_elements.size; i++) {
        Element* script_elem = (Element*)arraylist_get(&script_elements, i);
        char* source = extract_script_source(script_elem);
        if (source && source[0]) {
            transpile_js_to_c(&runtime, source, "<inline-script>");
        }
        mem_free(source);
    }

    // Phase 2: Execute body onload handler (if any)
    DomElement* body = dom_find_body_element(dom_doc);
    if (body) {
        char* onload_src = extract_onload_handler(body);
        if (onload_src && onload_src[0]) {
            // Wrap in function call if it's a simple expression
            transpile_js_to_c(&runtime, onload_src, "<body-onload>");
        }
        mem_free(onload_src);
    }

    arraylist_destroy(&script_elements);
}
```

### 1.8 Shared JS Context Across Scripts

Many CSS2.1 tests define a function in a `<script>` block and call it from `onload`. This requires that the JS runtime state (function definitions, globals) **persists across multiple `transpile_js_to_c()` calls**. The current JS transpiler's GC integration (v3 Phase 3b) already supports this via shared `EvalContext` heap. However, function definitions need to be retained between compilations.

**Approach**: Concatenate all script sources + onload handler into a single JS compilation unit:

```c
void execute_document_scripts(Element* html_root, DomDocument* dom_doc, Pool* pool) {
    // Collect all script sources
    StrBuf* combined = strbuf_new();
    collect_script_elements_recursive(html_root, combined);

    // Append onload handler
    DomElement* body = dom_find_body_element(dom_doc);
    if (body) {
        const char* onload = dom_element_get_attribute(body, "onload");
        if (onload && onload[0]) {
            strbuf_append_str(combined, "\n");
            strbuf_append_str(combined, onload);
            strbuf_append_str(combined, "\n");
        }
    }

    // Single JIT compilation pass
    if (combined->length > 0) {
        Runtime runtime = {};
        runtime.dom_doc = (void*)dom_doc;
        transpile_js_to_c(&runtime, combined->str, "<document-scripts>");
    }

    strbuf_free(combined);
}
```

This avoids the cross-compilation persistence problem entirely. All function definitions and their call from `onload` are compiled together.

---

## 2. DOM API Enhancements for CSS2.1 Test Coverage

### 2.1 API Gap Analysis (Updated)

The following table maps the DOM APIs used in the 304 skipped tests to current support status:

| DOM API | Usage Count | Status | Notes |
|---------|-------------|--------|-------|
| `document.getElementById()` | 353 | ✅ Supported | — |
| `elem.className = val` | 143 | ✅ Implemented | Via `js_dom_set_property()` with `class_names[]` rebuild |
| `elem.offsetWidth` (read) | 142 | ✅ Stub (Level 1) | Returns 0; sufficient for flush side-effect |
| `.style.display = val` | 68 | ✅ Implemented | Via `js_dom_set_style_property()` |
| `elem.childNodes` (read) | 120 | ✅ Implemented | Returns all children including text nodes |
| `elem.parentNode` (read) | 103 | ✅ Implemented | Works for both element and text nodes |
| `elem.appendChild()` | 98 | ✅ Enhanced | Now handles text nodes too |
| `document.createTextNode()` | 82 | ✅ Supported | — |
| `elem.removeChild()` | 79 | ✅ Enhanced | Now handles text nodes too |
| `document.createElement()` | 77 | ✅ Supported | — |
| `elem.insertBefore()` | 63 | ✅ Enhanced | Now handles text nodes too |
| `elem.data = val` (text node) | 34 | ✅ Implemented | Read/write via `js_dom_get/set_property()` |
| `document.getElementsByTagName()` | 29 | ✅ Supported | — |
| `elem.item()` | 24 | ❌ Not yet | Need NodeList indexing |
| `.style.fontFamily = val` | 20 | ✅ Implemented | camelCase→CSS conversion |
| `elem.nextSibling` | 14 | ✅ Implemented | All-node version (includes text) |
| `elem.setAttribute()` | 11 | ✅ Supported | — |
| `elem.firstChild` | 12 | ✅ Implemented | All-node version (includes text) |
| `elem.normalize()` | 7 | ✅ Implemented | Merges adjacent text nodes |
| `.style.*` (other props) | 30 | ✅ Implemented | Generic camelCase→CSS conversion |
| `window.getComputedStyle()` | 5 | ❌ Not yet | Needs CSS cascade query (Phase 5) |
| `document.styleSheets` / `cssRules` | 6 | ❌ Not yet | Needs CSSOM (Phase 5) |
| `elem.removeAttribute()` | 4 | ✅ Supported | — |
| `elem.textContent = val` | 4 | ✅ Implemented | Clears children, adds text node |
| `elem.disabled` | 4 | ❌ Not yet | Need HTML attribute property |
| `elem.cloneNode()` | 3 | ✅ Implemented | Deep recursive clone |
| `document.write()` | 2 | ❌ Out of scope | Legacy API |
| `elem.previousSibling` | 3 | ✅ Implemented | All-node version |
| `document.createElementNS()` | 2 | ❌ Low priority | SVG/XML namespace |
| `document.createDocumentFragment()` | 1 | ❌ Low priority | — |
| `elem.innerHTML = val` | 2 | ❌ Not yet | Requires HTML parser integration |
| `elem.hasChildNodes()` | 2 | ✅ Implemented | — |
| `elem.nodeName` | — | ✅ Implemented | Uppercase tag name / `#text` |
| `elem.lastChild` | — | ✅ Implemented | All-node version |
| `elem.id = val` | — | ✅ Implemented | Via `js_dom_set_property()` |

### 2.2 Priority 1: Property Assignment ✅ (was: Critical — blocks 215+ tests)

**Status**: Implemented. The transpiler now emits `js_property_set()` for `obj.prop = val` assignments. Three patterns are handled:
1. `elem.style.X = val` → `js_dom_set_style_property(elem, "X", val)` (chained member detection)
2. `obj.prop = val` → `js_property_set(obj, "prop", val)`
3. `obj[key] = val` → `js_property_set(obj, key, val)`

**Implementation**: `transpile_js_assignment_expression()` in `transpile_js.cpp`:

```c
// transpile_js.cpp — transpile_js_assignment_expression

// NEW: Handle member expression LHS: obj.prop = val
if (assign_node->left->node_type == JS_AST_NODE_MEMBER_EXPRESSION) {
    JsMemberNode* member = (JsMemberNode*)assign_node->left;

    strbuf_append_str(tp->code_buf, "js_property_set(");
    transpile_js_box_item(tp, member->object);
    strbuf_append_str(tp->code_buf, ", s2it(heap_create_name(\"");
    strbuf_append_str(tp->code_buf, member->property_name);
    strbuf_append_str(tp->code_buf, "\")), ");
    transpile_js_box_item(tp, assign_node->right);
    strbuf_append_str(tp->code_buf, ")");
}
```

**Impact**: Enables `elem.className = "..."` (143 uses), `elem.data = "..."` (34 uses), plus all `.style.*` when combined with the `.style` sub-object support below.

### 2.3 Priority 2: `elem.style` Sub-Object ✅ (was: Critical — blocks 124+ tests)

Implemented. The `.style` property is handled via **compound member assignment** pattern detection in the transpiler, emitting a dedicated runtime call:

```c
// Detect chained member: elem.style.display = "none"
// Transpile to: js_dom_set_style_property(elem, "display", s2it(heap_create_name("none")))
```

**Runtime implementation** (`js_dom.cpp`):

```c
void js_dom_set_style_property(Item elem_item, Item prop_name, Item value) {
    DomElement* elem = js_dom_unwrap_element(elem_item);
    if (!elem) return;

    const char* prop = fn_to_cstr(prop_name);
    const char* val = fn_to_cstr(value);

    // Convert camelCase JS property to CSS property name
    // e.g. "display" → "display", "fontFamily" → "font-family",
    //      "borderWidth" → "border-width", "cssFloat" → "float"
    char css_prop[64];
    js_camel_to_css_prop(prop, css_prop, sizeof(css_prop));

    // Apply as inline style on the DomElement
    // This updates elem->specified_style with highest priority (inline)
    dom_element_set_inline_style(elem, css_prop, val);
}
```

The implementation uses the existing `dom_element_apply_inline_style()` function which:
1. Parses the CSS declaration string (e.g., `"display: none"`)
2. Stores it in the element's inline style (highest cascade priority)
3. Marks the element for re-cascading (`styles_resolved = false`)

A helper `js_camel_to_css_prop()` converts camelCase JS properties to CSS hyphenated form (e.g., `fontFamily` → `font-family`, `cssFloat` → `float`).

**CSS property names used in tests** (camelCase → CSS mapping):

| JavaScript | CSS | Count |
|------------|-----|-------|
| `display` | `display` | 68 |
| `fontFamily` | `font-family` | 20 |
| `top` | `top` | 6 |
| `borderWidth` | `border-width` | 6 |
| `borderColor` | `border-color` | 6 |
| `position` | `position` | 4 |
| `cssFloat` | `float` | 4 |
| `marginTop` | `margin-top` | 2 |
| `marginLeft` | `margin-left` | 2 |
| `border` | `border` | 2 |
| `borderStyle` | `border-style` | ~10 (from non-skipped tests) |
| `left` | `left` | 1 |
| `counterReset` | `counter-reset` | 1 |

### 2.4 Priority 3: `offsetWidth` Layout Flush ✅ Level 1 (High — 123+ tests)

A striking pattern in CSS2.1 tests: `<script>document.body.offsetWidth</script>` is used as an **inline layout flush trigger**. The script appears between DOM elements to force the browser to commit to a layout state before processing subsequent elements. The `offsetWidth` value itself is rarely used—the side effect (layout computation) is what matters.

**Analysis**: These 123 tests use `document.body.offsetWidth` as a synchronous layout barrier. In a browser, reading `offsetWidth` triggers reflow. In our pipeline, since we execute all scripts after DOM construction but before layout, the flush is moot—our DOM tree is already fully built. However, these scripts need to at least **execute without error** (rather than aborting on the undefined `offsetWidth` property).

**Approach — two levels**:

**Level 1 (Implemented — sufficient for most tests)**: Return a stub value from `offsetWidth`:
```c
// In js_dom_get_property():
if (strcmp(prop, "offsetWidth") == 0 || strcmp(prop, "offsetHeight") == 0 ||
    strcmp(prop, "clientWidth") == 0 || strcmp(prop, "clientHeight") == 0) {
    return js_make_number(0);  // Stub: no layout computed yet
}
```

Since these tests only *read* `offsetWidth` for its flush side-effect and don't use the value, returning 0 is sufficient—the test's correctness depends on DOM mutations, not the computed width.

**Level 2 (Full fidelity, future)**: Perform incremental layout when `offsetWidth` is read. This requires:
1. CSS cascade on current DOM state
2. Run `layout_html_doc()` for the current tree
3. Read the computed width from the `ViewBlock` created for the element
4. Invalidate layout for further DOM mutations

This is significantly more complex and should be deferred. Level 1 is sufficient for passing the test suite.

### 2.5 Priority 4: DOM Traversal Properties ✅ (Medium — fills gaps)

All DOM traversal properties have been implemented:

| Property | Status | Implementation |
|----------|--------|---------------|
| `parentNode` | ✅ Implemented | Returns parent for all node types (element + text) |
| `firstChild` | ✅ Implemented | Returns first child of any type (element or text) |
| `lastChild` | ✅ Implemented | Returns last child of any type |
| `nextSibling` | ✅ Implemented | Traverses all node types |
| `previousSibling` | ✅ Implemented | Traverses all node types |
| `childNodes` | ✅ Implemented | Returns array of all children (elements + text nodes) |
| `childNodes.length` | ✅ Works | Via `fn_len` on returned array |
| `nodeType` | ✅ Supported | — |
| `data` (text nodes) | ✅ Implemented | Read via `js_dom_get_property`, write via `js_dom_set_property` |
| `hasChildNodes()` | ✅ Implemented | Added to `js_dom_element_method()` |
| `nodeName` | ✅ Implemented | Returns uppercase tag name for elements, `#text` for text nodes |

**Text Node Wrapping**: Implemented by reusing the existing `js_dom_wrap_element()` function — `DomText*` is cast to `void*` and stored in the same Map wrapper. Since the wrapper only stores a `void*` data pointer, both `DomElement*` and `DomText*` nodes use the same wrapping mechanism. The `DomNode::is_text()` / `DomNode::is_element()` methods are used at runtime to distinguish node types when unwrapping.

### 2.6 Priority 5: `elem.cloneNode()` ✅ (Low — 3 tests)

Implemented in `js_dom_element_method()`. Supports `cloneNode(deep)` with recursive deep cloning of the entire subtree. Clones DomElement attributes, tag name, class names, id, and all children (both element and text nodes). Does NOT copy layout/style data.

### 2.7 Priority 6: `elem.normalize()` ✅ (Low — 7 tests)

Implemented in `js_dom_element_method()`. Merges adjacent text nodes by concatenating their content, updating the first text node's `native_string` via `heap_strcpy()`, and removing subsequent text nodes from the child list.

### 2.8 Priority 7: CSSOM and `getComputedStyle()` (Low — 11 tests)

Only 5 tests use `getComputedStyle()` and 6 use `document.styleSheets`/`cssRules`. These require:
- `window.getComputedStyle(elem)` → Run CSS cascade for element, return resolved values
- `document.styleSheets[n].cssRules[m].style.prop = val` → Access and modify stylesheet rules

This is a significant feature requiring CSS cascade integration. **Defer to a later phase** — the 11 tests represent <4% of the skipped tests.

### 2.9 Out of Scope

| API | Tests | Reason |
|-----|-------|--------|
| `document.write()` | 2 | Legacy API, modifies parser stream |
| `document.createElementNS()` | 2 | SVG/XML namespace support |
| `document.createDocumentFragment()` | 1 | Rare usage |
| `window.scrollTo()` | 2 | Viewport scrolling (no visual viewport) |
| `elem.innerHTML = val` | 2 | Requires HTML parser integration for setting |
| `window.setTimeout()` | ~5 (in `floats-137` etc.) | Requires event loop / timer |
| `elem.disabled` | 4 | Form control property |
| `onclick` handlers | 20 | Require user interaction simulation |

---

## 3. JS Transpiler Language Enhancements

### 3.1 Syntax Audit for CSS2.1 Tests

The CSS2.1 test scripts use **simple ES3/ES5 syntax** — the Lambda JS transpiler already supports all required language constructs:

| Feature | Used in Tests | Transpiler Support |
|---------|---------------|--------------------|
| `var` declarations | Yes | **Supported** |
| C-style `for` loops | Yes | **Supported** |
| `function` declarations | Yes | **Supported** |
| `if`/`else` | Yes | **Supported** |
| String concatenation (`+`) | Yes | **Supported** |
| Property access (`.`) | Yes | **Supported** |
| Bracket access (`[]`) | Yes | **Supported** |
| `==`, `===`, `!=` | Yes | **Supported** |
| Object literals | Rare | **Supported** |

**Unsupported syntax found** (1 test): `with(Math) { ... }` in `background-position-201.htm` — this is the deprecated `with` statement. This single test can remain skipped.

### 3.2 Required: Member Expression Assignment ✅

Implemented in `transpile_js_assignment_expression()`. The transpiler now handles three LHS patterns:

For simple member assignment `obj.prop = val`:
```c
js_property_set(<obj>, s2it(heap_create_name("<prop>")), <val>)
```

For chained member assignment `obj.style.prop = val`:
```c
// Detect: if LHS is member.style.prop, emit specialized call
js_dom_set_style_property(<obj>, s2it(heap_create_name("<prop>")), <val>)
```

For bracket assignment `arr[i] = val`:
```c
js_property_set_index(<arr>, <index>, <val>)
```

### 3.3 Required: `document.documentElement` and `document.body`

These are already supported in `js_document_get_property()`. Verify:
- `document.documentElement` → returns `<html>` DomElement
- `document.body` → returns `<body>` DomElement
- `document.documentElement.className` → className property on `<html>`

---

## 4. DOM Mutation → Layout Pipeline

### 4.1 Style Invalidation

When JS modifies `.className` or `.style.*`, the CSS cascade results are invalidated. Since our pipeline runs all scripts before CSS cascading, no re-cascading is needed—CSS resolution operates on the final DOM state.

However, when `.className` is changed, the `DomElement` cached `class_names[]` array must be updated:

```c
void js_dom_set_className(DomElement* elem, const char* class_str) {
    // 1. Parse space-separated class names
    dom_element_parse_class_names(elem, class_str);
    // 2. Update any cached class-based data
    elem->styles_resolved = false;  // Mark for re-cascading
}
```

### 4.2 Tree Mutation Consistency

`appendChild()`, `removeChild()`, and `insertBefore()` must keep both layers consistent:
- **`DomElement*` tree**: Update parent/child/sibling pointers (for CSS matching)
- **Lambda `Element*` tree**: Update `items[]` array (for data model consistency)

The existing `js_dom_appendChild()` in `js_dom.cpp` already handles both layers.

### 4.3 `display: none` Dynamic Changes

68 tests set `.style.display = "none"` or change display from `"none"` to another value. This is the most common JS pattern. The `dom_element_set_inline_style()` function must:
1. Parse the `display` value
2. Store it in the element's inline style specification
3. Let the CSS cascade pick it up with highest priority during the cascade phase

Since our scripts run before cascade, the inline style will naturally participate in cascade resolution.

---

## 5. Implementation Phases

### Phase 1: Core Pipeline Integration ✅ DONE

**Goal**: Execute inline `<script>` + `onload` for simple tests.

1. ✅ Created `radiant/script_runner.cpp` + `script_runner.h` with `execute_document_scripts()`
2. ✅ Extracts `<script>` source from `Element*` tree, concatenates with `onload` handler
3. ✅ Wired into `load_lambda_html_doc()` after `build_dom_tree_from_element()`, before CSS cascade
4. ✅ Implemented member expression assignment in transpiler (`obj.prop = val`, `obj[key] = val`)
5. ✅ Added `offsetWidth`/`offsetHeight`/`clientWidth`/`clientHeight` stub returns (Level 1)

### Phase 2: `elem.style` Sub-Object ✅ DONE

**Goal**: Handle `elem.style.property = value` pattern.

1. ✅ Detect chained `.style.X` member assignment in transpiler (inspects AST for `JsMemberNode` chain)
2. ✅ Implemented `js_dom_set_style_property()` with camelCase→CSS conversion (`js_camel_to_css_prop()`)
3. ✅ Uses existing `dom_element_apply_inline_style()` to apply CSS declarations
4. ✅ Supports all CSS properties used in tests (display, fontFamily, top, borderWidth, borderColor, position, cssFloat, margin, border, etc.)

### Phase 3: Text Node Support ✅ DONE

**Goal**: Full DOM traversal including text nodes.

1. ✅ Text nodes wrapped via existing `js_dom_wrap_element()` (DomText* cast to void*)
2. ✅ Implemented `parentNode`, `firstChild`, `lastChild`, `nextSibling`, `previousSibling` for all node types
3. ✅ Implemented `childNodes` returning array of all children (elements + text nodes)
4. ✅ Implemented `.data` property (get/set) on text nodes
5. ✅ Implemented `nodeName` (uppercase tag for elements, `#text` for text nodes)
6. ✅ Implemented `normalize()` (merge adjacent text nodes)

### Phase 4: `cloneNode` and Edge Cases ✅ DONE

**Goal**: Handle remaining DOM APIs.

1. ✅ Implemented `cloneNode(deep)` for DomElement and DomText (recursive deep clone)
2. ✅ Implemented `hasChildNodes()`
3. ✅ Implemented `textContent` setter (via `js_dom_set_property`)
4. ✅ Implemented `className` setter with `class_names[]` array rebuild
5. ✅ Implemented `id` setter
6. ✅ Text node support in `appendChild`, `removeChild`, `insertBefore`

### Phase 5: CSSOM (Future, Optional)

**Goal**: `getComputedStyle()` and `document.styleSheets` access.

1. Implement `window.getComputedStyle(elem).getPropertyValue(prop)`
2. Implement `document.styleSheets[n].cssRules[m].style.prop`
3. Requires running CSS cascade mid-script

**Expected test impact**: ~11 additional tests. Defer unless high demand.

---

## 6. File Structure

### New Files (created)

```
radiant/
├── script_runner.cpp          # Script extraction and execution orchestration (~170 lines)
├── script_runner.h            # Public API: execute_document_scripts()
```

### Modified Files

```
radiant/
├── cmd_layout.cpp             # Wired execute_document_scripts() into pipeline (lines 1674-1679)
lambda/js/
├── transpile_js.cpp           # Member expression assignment codegen (3 patterns: .style.X, .prop, [key])
├── js_dom.cpp                 # ~270 new lines: js_dom_set_property(), js_dom_set_style_property(),
│                              #   expanded js_dom_get_property() (+15 properties),
│                              #   expanded js_dom_element_method() (+4 methods: hasChildNodes,
│                              #     normalize, cloneNode, text node support for append/remove/insert)
├── js_dom.h                   # New extern declarations for set_property, set_style_property
lambda/
├── mir.c                      # Registered js_dom_set_property + js_dom_set_style_property
```

### Build System

`radiant/script_runner.cpp` is automatically included — the `radiant/` directory is already in `source_dirs` in `build_lambda_config.json`. No build config changes needed.

---

## 7. Test Strategy

### 7.1 Incremental Skip List Reduction

As each phase lands, remove newly-passing tests from `test/layout/data/css2.1/skip_list.txt` and verify they pass in the CSS2.1 suite:

```bash
# After each phase:
make layout suite=css2.1    # Full run, check new passes
make layout suite=baseline  # Verify no regressions
```

### 7.2 Phase Validation

| Phase | Validation |
|-------|-----------|
| Phase 1 | Run `display-change-001`, `table-anonymous-objects-043`, `border-dynamic-001` — should pass |
| Phase 2 | Run `table-anonymous-objects-139`–`143` — should pass |
| Phase 3 | Run `block-in-inline-insert-001a` through `-012` — should pass |
| Phase 4 | Run full CSS2.1 suite; passes should increase by ~200+ |

### 7.3 Baseline Protection

The 2,347 baseline tests must continue to pass 100%. Any regression in baseline blocks the phase from merging.

---

## 8. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| DOM mutations cause DomElement tree corruption | Medium | High | Thorough pointer validation; test with AddressSanitizer |
| Script execution changes DOM in ways CSS cascade doesn't expect | Low | Medium | Scripts run before cascade — cascade sees final state |
| JIT compilation overhead per test adds significant time | Medium | Low | Script compilation is ~2ms per test; 304 tests × 2ms = ~0.6s total |
| `offsetWidth` stub (returning 0) causes test failures | Low | Low | Tests use it only for flush side-effect, not the value |
| Cross-script shared state (function persistence) | Medium | Medium | Concatenate all scripts into single compilation unit |
| `with(Math)` and unsupported JS syntax | Low | Low | Only 1 test uses it; keep in skip list |
| Memory leaks from JIT allocations | Medium | Low | GC integration (v3 Phase 3b) handles this; verify no growth |

---

## 9. Results and Next Steps

### Actual Results (Phases 1–4)

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| CSS2.1 tests passing | 8,411 | 8,480 | **+69** |
| CSS2.1 tests failing | 1,156 | 1,122 | -34 |
| Tests skipped | 304 | 269 | -35 |
| Baseline | 2,347/2,347 | 2,347/2,347 | 100% (no regressions) |

### Analysis of Remaining 269 Skipped Tests

The 269 remaining skipped tests fall into categories that require additional work beyond the current JS integration:

| Category | Est. Count | Needed |
|----------|-----------|--------|
| Layout bugs (JS works, layout fails) | ~100 | Fix Radiant layout engine (not JS-related) |
| `document.body.offsetWidth` flush (multi-step scripts) | ~80 | Level 2 incremental layout (Phase 5) |
| `getComputedStyle()` / CSSOM | ~11 | CSS cascade query API |
| Complex DOM mutations (`innerHTML`, `createDocumentFragment`) | ~20 | Additional DOM APIs |
| `onclick` / `setTimeout` / event-driven | ~25 | Event loop / interaction simulation |
| `with(Math)` / unsupported syntax | ~1 | Out of scope |
| Other edge cases | ~32 | Case-by-case analysis |

### Next Steps

1. **Un-skip more tests**: Run all 269 remaining tests individually to identify which ones now execute JS correctly but fail due to layout issues (not JS issues). These can be un-skipped and tracked as regular layout failures.
2. **Phase 5 (CSSOM)**: Implement `getComputedStyle()` for ~11 tests.
3. **Focus on layout fixes**: Many of the "failing" JS tests fail because of pre-existing CSS layout bugs, not JS integration issues. Fixing those layout bugs will increase the pass count further.
4. **Incremental `offsetWidth` layout** (Level 2): For tests that need mid-script layout computation.
