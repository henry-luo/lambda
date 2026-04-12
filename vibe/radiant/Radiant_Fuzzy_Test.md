# Radiant Layout Engine — Fuzzy Testing Proposal

## 1. Goals

- **Crash resistance**: No HTML/CSS input should cause segfaults, infinite loops, or undefined behavior in the layout engine
- **Memory safety**: No leaks, use-after-free, buffer overflows, or stack overflows during layout
- **Determinism**: Same input always produces the same layout output
- **Graceful degradation**: Malformed/adversarial inputs produce a valid (possibly empty) layout tree without crashing

## 2. Attack Surface

The Radiant layout pipeline has 5 phases, each a fuzzing target:

```
HTML string → [HTML Parser] → Element tree → [CSS Cascade] → Styled DOM
  → [Layout Algorithms] → Layout tree → [JSON Serializer] → Output
```

| Phase | Component | Risk Level | Key Concern |
|-------|-----------|------------|-------------|
| HTML Parsing | `input-html5.cpp` | Medium | Malformed tags, deep nesting, huge attributes |
| CSS Parsing | `css/css_parser.cpp` | Medium | Invalid properties, extreme values, circular imports |
| Style Resolution | `resolve_css_style.cpp` | Medium | Specificity conflicts, `!important` chains, inheritance loops |
| Layout Calculation | `layout_block/inline/flex/grid/table.cpp` | **High** | The core risk area — complex algorithms with interacting constraints |
| Serialization | View tree → JSON | Low | Large trees, deep nesting |

## 3. Fuzzing Strategy

### 3.1 Two-Tier Approach

**Tier 1 — Generative fuzzing** (structured random HTML/CSS generation):
- Generate syntactically valid HTML with randomized CSS properties
- Target specific layout modes and their interactions
- Fast iteration, broad coverage

**Tier 2 — Mutational fuzzing** (mutate existing test files):
- Start from the 5,660+ HTML test files across `test/layout/data/` (2,567 baseline, 1,052 wpt-css-text, 528 web-tmpl, 386 form, 294 wpt-css-shapes, and more)
- Apply CSS property mutations, DOM structure mutations, and value perturbations
- Targets realistic edge cases near known-good inputs

### 3.2 Oracle Strategy

Since we cannot compare against a browser in real-time, the oracle is:

1. **No crash** (exit code 0, no segfault/abort)
2. **No timeout** (layout completes within 10 seconds)
3. **No excessive memory** (RSS stays under 512MB)
4. **Valid output** (produces parseable JSON view tree)
5. **No sanitizer errors** (when built with ASan/UBSan)

## 4. Mutation Catalog

### 4.1 CSS Property Mutations (High Priority)

These target the layout algorithms directly:

| Category | Mutations | Target Module |
|----------|-----------|---------------|
| **Box Model** | `width/height`: `0`, `-1px`, `99999px`, `auto`, `100%`, `calc(100% - 99999px)` | `layout_block.cpp` |
| **Margins** | `margin`: negative values, `auto`, mixed units, all-zero | Margin collapsing in `layout_block.cpp` |
| **Display** | Random `display` changes: `block↔inline↔flex↔grid↔table↔none` | All layout modules |
| **Position** | `static↔relative↔absolute↔fixed↔sticky` with extreme offsets | `layout_block.cpp`, abs positioning |
| **Flex** | `flex-basis`: `0`, `auto`, `100%`; `flex-grow/shrink`: `0`, `9999`, `0.001` | `layout_flex.cpp`, `layout_flex_multipass.cpp` |
| **Grid** | `repeat(9999, 1fr)`, `minmax(auto, auto)`, implicit placement | `layout_grid.cpp`, `layout_grid_multipass.cpp` |
| **Table** | `colspan/rowspan`: `0`, `100`, `9999`; `border-collapse` toggle | `layout_table.cpp` |
| **Float** | `float: left/right` with `clear`, zero-width floats, overlapping | `layout_float.cpp` |
| **Overflow** | `overflow`: `hidden/scroll/auto/visible` combinations | Block layout |
| **Font/Text** | `font-size`: `0`, `0.1px`, `9999px`; `line-height`: `0`, `normal`; `text-indent`: `-9999px` | `layout_text.cpp` |
| **Writing Mode** | `direction: rtl`, `writing-mode` changes | Text and inline layout |

### 4.2 DOM Structure Mutations

| Mutation | Description | Risk |
|----------|-------------|------|
| **Deep nesting** | `<div>` nested 500–2000 levels deep | Stack overflow |
| **Wide siblings** | 10,000+ sibling elements at one level | Memory/performance |
| **Empty elements** | Inline elements with no content, zero-height blocks | Zero-height edge cases |
| **Mismatched nesting** | `<table><div><tr>`, `<flex-parent><table-child>` | Anonymous box generation |
| **Removed parents** | `<tr>` without `<table>`, `<li>` without `<ul>` | Table anonymous box fixup |
| **Text-only stress** | 100KB+ text node, single word with no break opportunities | Text layout / line breaking |
| **Mixed content** | Interleave inline/block/flex/grid children randomly | Block-inline splitting |
| **Replaced elements** | `<img>` with extreme/zero/negative dimensions | Intrinsic sizing |
| **Self-closing abuse** | `<div/>` in HTML mode, void elements with children | Parser robustness |

### 4.3 Value Edge Cases

```
Extreme lengths:   0px, 0.001px, -1px, 99999px, 1e10px, NaN, Infinity
Percentages:       0%, 100%, 200%, -50%, calc(100% + 100%)
Calc expressions:  calc(0/0), calc(1px * 99999), calc(100% - 100% + 1px)
Units mixing:      em + px + %, vw/vh on nested elements
Color values:      transparent, currentColor, invalid hex
Z-index:           -999999, 0, 999999
Opacity:           -1, 0, 0.5, 1, 2, NaN
```

## 5. Implementation Plan

### 5.1 File Structure

```
test/fuzzy/radiant/
├── radiant_fuzzer.sh           # Main orchestrator script
├── generators/
│   ├── html_gen.py             # Structured HTML/CSS generator (Tier 1)
│   └── html_mutator.py         # Mutation engine for existing tests (Tier 2)
├── corpus/
│   └── seeds/                  # Curated seed files for mutation
├── results/
│   ├── crashes/                # Crash-reproducing inputs
│   └── timeouts/               # Timeout-causing inputs
└── README.md
```

### 5.2 Phase 1 — Shell-Based Fuzzer (MVP)

Minimal infrastructure, maximum coverage. Reuses the existing `lambda.exe layout` command.

**`radiant_fuzzer.sh`** — Main loop:
```bash
#!/bin/bash
# Usage: ./test/fuzzy/radiant/radiant_fuzzer.sh --duration=600 --jobs=4

LAMBDA_EXE="./lambda.exe"
TIMEOUT=10          # seconds per layout
MAX_RSS_KB=524288   # 512MB limit
TEMP_DIR="./temp/radiant_fuzz"
CRASH_DIR="./test/fuzzy/radiant/results/crashes"
TIMEOUT_DIR="./test/fuzzy/radiant/results/timeouts"

mkdir -p "$TEMP_DIR" "$CRASH_DIR" "$TIMEOUT_DIR"

run_one() {
    local html_file="$1"
    local json_out="$TEMP_DIR/out_$$.json"

    # Run with timeout and memory limit
    timeout $TIMEOUT "$LAMBDA_EXE" layout "$html_file" -o "$json_out" 2>"$TEMP_DIR/stderr_$$.txt"
    local exit_code=$?

    if [[ $exit_code -eq 139 ]] || [[ $exit_code -eq 134 ]] || [[ $exit_code -eq 136 ]]; then
        # SIGSEGV (139), SIGABRT (134), SIGFPE (136)
        cp "$html_file" "$CRASH_DIR/crash_$(date +%s)_$$_exit${exit_code}.html"
        echo "CRASH: $html_file (exit $exit_code)"
        return 1
    elif [[ $exit_code -eq 124 ]]; then
        # timeout
        cp "$html_file" "$TIMEOUT_DIR/timeout_$(date +%s)_$$.html"
        echo "TIMEOUT: $html_file"
        return 1
    elif [[ $exit_code -ne 0 ]]; then
        # Non-zero but not crash - acceptable (graceful error)
        return 0
    fi

    # Validate output is parseable JSON
    if [[ -f "$json_out" ]]; then
        python3 -c "import json; json.load(open('$json_out'))" 2>/dev/null
        if [[ $? -ne 0 ]]; then
            cp "$html_file" "$CRASH_DIR/badjson_$(date +%s)_$$.html"
            echo "BAD JSON: $html_file"
            return 1
        fi
    fi
    return 0
}
```

### 5.3 Phase 2 — Python HTML/CSS Generator

Structured generation that targets specific layout algorithm interactions:

```python
# generators/html_gen.py — Sketch of key generation functions

class RadiantFuzzer:
    """Generates adversarial HTML/CSS for Radiant layout fuzzing."""

    DISPLAY_VALUES = ['block', 'inline', 'inline-block', 'flex', 'inline-flex',
                      'grid', 'inline-grid', 'table', 'table-row', 'table-cell',
                      'none', 'list-item', 'flow-root']

    POSITION_VALUES = ['static', 'relative', 'absolute', 'fixed', 'sticky']

    EXTREME_LENGTHS = ['0', '0.001px', '-1px', '99999px', 'auto',
                       '100%', '200%', '-50%', 'calc(100% - 99999px)',
                       'calc(0/0)', 'min-content', 'max-content', 'fit-content']

    def generate_layout_stress(self, mode: str) -> str:
        """Generate HTML targeting a specific layout mode."""
        # mode: 'flex', 'grid', 'table', 'block', 'float', 'mixed'
        ...

    def generate_deep_nesting(self, depth: int) -> str:
        """Generate deeply nested DOM structure."""
        ...

    def generate_wide_siblings(self, count: int) -> str:
        """Generate element with many siblings."""
        ...

    def generate_mixed_context(self) -> str:
        """Generate interleaved block/inline/flex/grid/table elements."""
        ...

    def mutate_css(self, html: str) -> str:
        """Randomly mutate CSS property values in an HTML string."""
        ...

    def mutate_dom(self, html: str) -> str:
        """Randomly mutate DOM structure of an HTML string."""
        ...
```

**Targeted generators** (one per high-risk module):

| Generator | Target | Key Patterns |
|-----------|--------|-------------|
| `gen_flex_stress()` | Flex layout | Nested flex, percentage basis on undefined parent, auto margins, `flex-wrap` + `align-content` combos |
| `gen_grid_stress()` | Grid layout | `repeat()` extremes, implicit tracks, spanning items, `minmax()` edge cases |
| `gen_table_stress()` | Table layout | Colspan/rowspan conflicts, nested tables, border-collapse, missing cells |
| `gen_margin_collapse()` | Block layout | Nested margins, self-collapsing blocks, clearance + margin interaction |
| `gen_float_stress()` | Float layout | Overlapping floats, float + clear + block formatting context, zero-width |
| `gen_text_stress()` | Text layout | Long unbreakable words, CJK mixed with Latin, `text-indent` + float, `line-height: 0` |
| `gen_abs_position()` | Absolute positioning | Static-positioned abs descendants, containing block edge cases |
| `gen_inline_stress()` | Inline layout | Empty inline spans with borders, `vertical-align` extremes, nested inline |

### 5.4 Phase 3 — ASan/UBSan Integration

Build Radiant with sanitizers for deeper bug detection:

```bash
# Add to Makefile or build config
make build SANITIZE=address,undefined
# Then run fuzzer against sanitized binary
LAMBDA_EXE=./build/bin/lambda_asan.exe ./test/fuzzy/radiant/radiant_fuzzer.sh
```

Sanitizers catch:
- **ASan**: heap/stack buffer overflow, use-after-free, double-free, memory leaks
- **UBSan**: integer overflow, null pointer dereference, misaligned access, division by zero

### 5.5 Phase 4 — Differential Testing (Optional)

For a subset of generated inputs, compare Radiant output against browser references:

1. Generate HTML → layout with Radiant → JSON output
2. Open same HTML in headless Chrome → extract layout via `getComputedStyle` + `getBoundingClientRect`
3. Compare layout bounds within tolerance (reuse existing `test_radiant_layout.js` comparison logic)

This reuses the existing test infrastructure but with generated inputs instead of hand-written tests.

## 6. Priority Targets

Based on code complexity and historical crash patterns:

| Priority | Module | File | Why |
|----------|--------|------|-----|
| **P0** | Flex multi-pass | `layout_flex_multipass.cpp` | 9-phase algorithm, % basis over undefined parent, grow/shrink division |
| **P0** | Margin collapsing | `layout_block.cpp` (top ~150 lines) | Nested self-collapsing + quirks mode, positive/negative chain |
| **P0** | Table layout | `layout_table.cpp` | Anonymous box generation, colspan/rowspan, border-collapse |
| **P1** | Grid track sizing | `layout_grid_multipass.cpp` | `fr` unit edges, circular auto-sizing, `repeat()` extremes |
| **P1** | Text/inline layout | `layout_text.cpp`, `layout_inline.cpp` | Float interaction, CJK metrics, zero-height inline, `text-indent` |
| **P1** | Absolute positioning | `layout_block.cpp` (`adjust_abs_descendants_y`) | Y-adjustment cascade after margin collapse |
| **P2** | CSS cascade | `resolve_css_style.cpp` | Specificity conflicts, `!important`, pseudo-elements |
| **P2** | Deep nesting | All layout modules | Stack depth limits, recursion guards |

## 7. Success Criteria

| Metric | Target |
|--------|--------|
| Unique crash inputs discovered | 0 remaining (all fixed) |
| Timeout inputs (>10s) | 0 remaining |
| Coverage of layout code paths | ≥80% line coverage |
| Fuzzing throughput | ≥50 inputs/second (non-sanitizer build) |
| Sustained fuzzing duration | 24+ hours without new crashes |

## 8. Integration with CI

```bash
# Nightly fuzzing job (Makefile target)
make fuzz-radiant duration=3600    # 1-hour nightly fuzz run

# Pre-merge quick fuzz (shorter)
make fuzz-radiant-quick duration=120  # 2-minute smoke fuzz
```

Any crash-reproducing HTML files saved to `test/fuzzy/radiant/results/crashes/` should be:
1. Minimized (manually or with delta-debugging)
2. Added as regression tests to `test/layout/data/` with corresponding reference JSON
3. Fix verified by running `make test-radiant-baseline`

## 9. Estimated Effort

| Phase | Scope | Effort |
|-------|-------|--------|
| Phase 1 | Shell fuzzer + mutation of existing tests | Small |
| Phase 2 | Python generator with targeted strategies | Medium |
| Phase 3 | ASan/UBSan build integration | Small |
| Phase 4 | Differential testing with headless Chrome | Medium-Large |

Recommended starting point: **Phase 1 + Phase 2 (flex/table/margin generators only)** — this covers the highest-risk modules with minimal infrastructure.
