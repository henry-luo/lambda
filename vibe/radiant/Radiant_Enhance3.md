# Radiant Enhancement Plan 3: Test Debugging & Code Organization

## 1. Layout Test Debugging Improvements

### A. Element-Level Diff in Single-Test Verbose Output

When `make layout test=X` fails, the output shows overall pass/fail percentages but forces manual mapping between Radiant's JSON tree and the reference JSON tree. A **side-by-side property diff per failing element** (like `compare-layout` provides) should be the default in single-test verbose mode, not a separate tool invocation.

### B. Resolved CSS Property Dump (`--dump-styles`)

The hardest debugging scenario: "the layout numbers are wrong, but is it a style resolution bug or a layout algorithm bug?" A `--dump-styles` flag on the layout command that prints the **resolved CSS property values** for each element (before layout) would instantly separate these two classes of bugs. This is essentially a computed-style dump — cheap to implement since `resolve_css_styles()` already stores everything on the view structs.

Usage: `make layout test=X dump=1`

### C. Failure Categorization by CSS Property

When running a whole suite (`make layout suite=wpt-css-lists`), failures are listed test-by-test. A summary at the end would help prioritize which layout subsystem to attack first:

```
Failure breakdown:
  x offset:  23 tests (marker positioning)
  width:     12 tests (intrinsic sizing)
  height:     5 tests (block height)
```

This avoids manually reading each test's diff to spot patterns.

### D. HTML Visual Diff Output

Generate an HTML file that overlays Radiant layout boxes on browser reference boxes — green for matching, red for mismatched — so differences are visible at a glance. The data is already present in both JSONs; it's just a rendering step. Especially useful for complex tests with many nested elements.

### E. Reference Capture Verification

After the `word-spacing-003` experience (browser captured a wrong reference because Ahem didn't load), a `make capture-layout test=X verify=1` that captures, then immediately feeds the result back to the same browser for a round-trip sanity check, would catch bad references early.

### F. Log Correlation with Element Selectors

The `log.txt` file has detailed trace output from layout, but there's no easy way to correlate a line in `log.txt` with a specific element in the test output. Adding the element's CSS selector path (e.g., `body > div.ws > span:nth-child(2)`) to key log lines would make `grep` much more useful.

### G. HTML Source Line Tracking (IMPLEMENTED)

**Problem**: Layout debug logs identify elements by tag name alone (`div`, `span`, `img`), making it hard to correlate log messages with specific elements in the source HTML when the document contains many elements of the same type.

**Solution**: Track the source line number of each HTML element from parsing through to layout logging, so log messages display `tag:line` (e.g., `div:42`, `img:105`, `p:300`) instead of just the tag name.

**Architecture**:

1. **Parse-time line counting** (`html5_tokenizer.cpp`): An incremental scanner `html5_update_line_count()` counts `\n` characters as the tokenizer advances. When a start-tag token is created (in `HTML5_TOK_TAG_OPEN` state), the current line number is stamped on the token's `source_line` field. Total work is O(n) across the document.

2. **Element attribute storage** (`html5_parser.cpp`): When `track_source_lines` is enabled, `html5_create_element_for_token()` stores `__source_line` as an integer attribute on the Lambda `Element` via `ElementBuilder::attr()`.

3. **DOM propagation** (`dom_element.cpp`): `build_dom_tree_from_element()` reads the `__source_line` attribute from each Element and copies it to `DomNode::source_line`.

4. **Log formatting** (`dom_node.cpp`): `DomNode::source_loc()` returns `"tag:line"` if `source_line > 0`, else falls back to `node_name()`. Uses a ring of 4 static buffers so multiple `source_loc()` calls work in a single log statement.

5. **Layout integration**: Key `log_debug` calls in `layout_block.cpp`, `layout_flex.cpp`, `layout_table.cpp`, `layout_inline.cpp`, `layout.cpp`, and `resolve_css_style.cpp` use `source_loc()` instead of `node_name()`.

**API**:

```c
// New parse options struct
typedef struct Html5ParseOptions {
    bool track_source_lines;  // default: false
} Html5ParseOptions;

// Extended parse function
Element* html5_parse_ex(Input* input, const char* source, Html5ParseOptions* opts);

// DomNode additions
struct DomNode {
    int source_line;                  // 0 = not tracked
    const char* source_loc() const;   // returns "tag:line" or node_name()
};
```

**Activation**: Source line tracking is enabled automatically when `--debug` is passed to `lambda layout` (single-test mode). The flag flows through `layout_single_file()` → `load_lambda_html_doc()` → `html5_parse_ex()`.

**Example log output** (with `--debug`):
```
layout block div:12
layout block div:25
layout inline span:30
FLEX START div:42 ...
layout block img:105
```

---

## 2. Code Organization Assessment

### Current State

The Radiant codebase is **~96K lines** across `.cpp`/`.hpp` files. It is well-modularized by layout mode, with consistent naming and CSS spec references in comments.

Top files by size:

| File | Lines | Purpose |
|------|------:|---------|
| `resolve_css_style.cpp` | 10,437 | CSS property resolution, unit conversion, style cascade |
| `layout_table.cpp` | 8,901 | Table layout algorithm |
| `layout_block.cpp` | 6,937 | Block formatting context + pseudo-elements + counters |
| `layout_flex.cpp` | 6,004 | Flexbox layout algorithm |
| `cmd_layout.cpp` | 4,353 | Layout command entry point |
| `intrinsic_sizing.cpp` | 3,219 | Min/max content width/height calculation |
| `layout_flex_multipass.cpp` | 2,508 | Flex multipass algorithm |
| `view_pool.cpp` | 2,368 | View node allocation |
| `render.cpp` | 2,358 | Rendering pipeline |
| `layout.cpp` | 2,025 | Layout orchestration, vertical alignment |

### Strengths

- **Clean separation by layout mode**: `layout_block.cpp`, `layout_flex.cpp`, `layout_table.cpp`, `layout_grid.cpp`, `layout_inline.cpp` — each handles one formatting context, matching CSS spec organization.
- **Consistent naming**: `snake_case` everywhere; `layout_*`, `resolve_*`, `intrinsic_*` prefixes are predictable.
- **CSS spec references**: Comments reference specific CSS spec sections (e.g., CSS Logical Properties URL in `resolve_css_property`).

### Refactoring Recommendations

#### A. Split `resolve_css_style.cpp` (HIGH Priority)

At 10,437 lines, this is the biggest file and handles too many concerns:
- CSS variable resolution (`lookup_css_variable`)
- Color parsing (`resolve_color_component`)
- Font size resolution (`resolve_font_size`)
- Length/unit resolution (`resolve_length_value`)
- Spacing resolution (margin/padding/border with inherit)
- Grid track parsing (`parse_grid_track_list`, `parse_repeat_function`, etc.)
- The giant `resolve_css_property()` switch statement (~7,800 lines)

**Suggested split** — the `resolve_css_property()` switch already has comment groups that map to natural file boundaries:

| New File | Content | Est. Lines |
|----------|---------|----------:|
| `resolve_css_typography.cpp` | font, line-height, text-align, text-decoration, vertical-align, word/letter-spacing | ~1,500 |
| `resolve_css_box.cpp` | width/height/min/max, margin, padding, border, overflow, display | ~2,500 |
| `resolve_css_grid.cpp` | grid-template-*, grid track parsing helpers, grid placement | ~1,500 |
| `resolve_css_flex.cpp` | flex-direction, flex-wrap, flex-grow/shrink/basis, align/justify | ~800 |
| `resolve_css_style.cpp` | Core dispatch table, variable resolution, unit conversion, color, shorthand expansion | ~4,000 |

Each group would be a function called from the main switch, keeping the dispatch table thin.

#### B. Extract Pseudo-Element Code from `layout_block.cpp` (MEDIUM Priority)

`layout_block.cpp` contains ~600 lines of pseudo-element and counter logic that is conceptually distinct from block formatting:
- `create_pseudo_element()` (~125 lines)
- `layout_pseudo_element()` (~80 lines)
- `apply_pseudo_counter_ops()` (~150 lines)
- `extract_counter_spec_from_style()` (~65 lines)
- `create_first_letter_pseudo()` and helpers (~180 lines)

These could move to a `layout_pseudo.cpp` file.

#### C. Move Grid Header Implementations to `.cpp` (LOW Priority)

`grid_sizing_algorithm.hpp` (1,860 lines) and `grid_enhanced_adapter.hpp` (1,287 lines) are large headers with template/inline-heavy code. Moving implementations to `.cpp` files would reduce compile times and improve navigation.

#### D. Wrap Timing Globals in a Profiler Struct (LOW Priority)

Global timing variables (e.g., `g_layout_table_height` in `layout_block.cpp`) are scattered across files. Wrapping them in a toggleable profiling struct would be cleaner.

### What Does NOT Need Refactoring

- **`layout_flex.cpp`** (6K) and **`layout_table.cpp`** (8.9K) — these are large because table layout and flexbox *are* inherently complex algorithms. Splitting them artificially would make the code harder to follow.
- **`intrinsic_sizing.cpp`** (3.2K) — well-scoped to its purpose.
- **`layout_inline.cpp`** (1.2K) — appropriately sized.

### Priority Summary

| Priority | Action | Impact |
|----------|--------|--------|
| **High** | Split `resolve_css_style.cpp` into 4-5 files | Biggest readability win, easier to locate property handling |
| **Medium** | Extract pseudo-element code from `layout_block.cpp` | ~600 lines of distinct logic separated |
| **Low** | Move grid header implementations to `.cpp` | Compile time improvement |
| **Low** | Wrap timing globals in a profiler struct | Code cleanliness |
