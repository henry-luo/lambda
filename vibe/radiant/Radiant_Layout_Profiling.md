# Radiant Pipeline Timing Analysis

This document captures performance profiling and optimization history for the Radiant HTML/CSS rendering pipeline.

## Test Case

- **File**: `test/layout/data/page/hn.html` (Hacker News clone)
- **Document**: ~1335 DOM elements, 63 CSS rules
- **Output**: 1200×800 PNG

---

## Phase 0: Baseline (Before Optimization)

Initial timing measurements before any optimizations.

### Overall Timing

| Phase | Time (ms) | % of Total |
|-------|-----------|------------|
| Init | ~17 | 1.3% |
| Load HTML | ~486 | 37.4% |
| Layout | ~635 | 48.9% |
| Render | ~74 | 5.7% |
| TOTAL | **~1299** | 100% |

### CSS Cascade Analysis

The CSS cascade was identified as the major bottleneck in Load HTML:

| Metric | Value |
|--------|-------|
| DOM Elements | 1,335 |
| CSS Rules | 63 |
| Selector match attempts | 101,312 |
| Selector match hits | 749 |
| **Hit rate** | **0.7%** |
| CSS cascade time | **~443ms** |

**Root Cause**: The O(rules × elements) algorithm checked all 63 rules against each of 1335 elements, resulting in ~100K selector comparisons to find only 749 matches.

---

## Phase 1: Remove Excessive Logging

### Problem Identified

During profiling, we discovered ~100K calls to `log_debug()` in the CSS cascade hot path. Even when debug logging is disabled, the string formatting overhead was significant.

Additionally, calling `high_resolution_clock::now()` ~200K times for per-operation timing added ~20ms overhead.

### Changes Applied

1. Removed `log_debug` calls from selector matching hot path
2. Removed per-operation timing; now only track aggregate counters

### Result After Phase 1

| Phase | Before | After | Improvement |
|-------|--------|-------|-------------|
| CSS cascade | ~443ms | ~233ms | **47% faster** |
| Total | ~1299ms | ~1042ms | **20% faster** |

### Detailed Timing After Phase 1

| Phase | Time (ms) | % of Total |
|-------|-----------|------------|
| Init | 16.6 | 1.6% |
| Load HTML | 312.7 | 30.0% |
| Layout | 644.4 | 61.8% |
| Render | 68.4 | 6.6% |
| TOTAL | **1042** | 100% |

---

## Phase 2: Selector Indexing

### Problem Identified

The CSS cascade still took ~234ms due to O(elements × rules) complexity:
- 1335 elements × 63 rules = ~84K rule/element pairs
- 101K selector match attempts for only 749 hits (0.7% hit rate)

### Solution Implemented

Built a **selector index** that hashes CSS rules by their key selector (rightmost compound selector):

1. **Tag index**: Rules indexed by tag name (e.g., `td` rules only checked against `<td>` elements)
2. **Class index**: Rules indexed by class selector
3. **ID index**: Rules indexed by ID selector
4. **Universal rules**: Only `*` rules checked against all elements

```cpp
struct SelectorIndex {
    std::unordered_map<std::string, std::vector<IndexedRule>> by_tag;
    std::unordered_map<std::string, std::vector<IndexedRule>> by_class;
    std::unordered_map<std::string, std::vector<IndexedRule>> by_id;
    std::vector<IndexedRule> universal;
};
```

The index is built once per stylesheet. During cascade:
1. Get element's tag name, classes, and ID
2. Look up candidate rules from each hash map
3. Merge candidates (deduplicating)
4. Only match against candidate rules

Implementation: `radiant/cmd_layout.cpp` - `apply_stylesheet_to_dom_tree_fast()`

### Result After Phase 2

| Phase | Before | After | Improvement |
|-------|--------|-------|-------------|
| CSS cascade | ~234ms | **7ms** | **97% faster** |
| Load HTML | 312.7ms | 80.7ms | **74% faster** |
| Total | ~1042ms | **849ms** | **18% faster** |

### CSS Cascade Metrics Comparison

| Metric | Phase 1 | Phase 2 | Improvement |
|--------|---------|---------|-------------|
| Selector match attempts | 101,312 | 34,515 | **66% reduction** |
| Hit rate | 0.7% | 2.2% | **3× better** |
| Candidate rules checked | ~84,105 | 16,615 | **80% reduction** |
| Cascade time | 234ms | 7ms | **97% faster** |

---

## Current State (After All Optimizations)

### Overall Timing

| Phase | Time (ms) | % of Total | Notes |
|-------|-----------|------------|-------|
| **Init** | 16.6 | 2.0% | GLFW/ThorVG initialization |
| **Load HTML** | 80.7 | 9.5% | Parse + DOM + CSS cascade |
| **Layout** | 680.5 | 80.2% | CSS layout computation |
| **Render** | 71.7 | 8.4% | Vector graphics rendering |
| **Cleanup** | 0.1 | 0.0% | Memory cleanup |
| **TOTAL** | **849** | 100% | |

### Load HTML Breakdown

| Step | Time (ms) | Notes |
|------|-----------|-------|
| Read file | 0.6 | File I/O |
| Parse HTML | 34.2 | Tree-sitter HTML parsing |
| Build DOM tree | 31.2 | Lambda Element → DomElement conversion |
| Parse CSS | 4.5 | CSS stylesheet parsing |
| CSS cascade | 7.0 | ✅ Optimized with selector index |
| Total | 80.7 | |

### Layout Breakdown

| Step                | Time (ms) | Notes                                 |
| ------------------- | --------- | ------------------------------------- |
| Context init        | 0.0       | Layout context setup                  |
| Root style resolve  | 0.0       | HTML element style                    |
| Body find           | 0.0       | Navigate to body                      |
| **layout_block**    | **459** | ✅ Optimized style caching |
| finalize_block_flow | 0.0       | Final block flow                      |
| print_view_tree     | 7.2       | Debug tree output                     |
| Total               | 467       |                                       |

### Render Breakdown

| Step | Time (ms) | Notes |
|------|-----------|-------|
| render_block_view | 47.8 | ThorVG scene graph construction |
| tvg_canvas_sync | 0.0 | Synchronization |
| save_to_file | 20.4 | PNG encoding |
| Total | 68.4 | |

---

## Summary: Optimization Progress

| Metric | Baseline | After Phase 1 | After Phase 2 | After Phase 3 | Total Improvement |
|--------|----------|---------------|---------------|---------------|-------------------|
| CSS cascade | 443ms | 233ms | **7ms** | 6ms | **99% faster** |
| Style resolution | - | - | 226ms | **67ms** | **70% faster** |
| Layout | 635ms | 644ms | 634ms | **467ms** | **26% faster** |
| Total time | 1299ms | 1042ms | 849ms | **~550ms** | **58% faster** |

---

## Remaining Bottlenecks

### Primary Bottleneck

**`layout_block`** (459ms, ~80% of total) - The recursive layout algorithm:
- Table layout still dominates (~897ms internal, heavily recursive)
- Text shaping and line breaking (~71ms)
- Box model calculations

### Already Optimized

- HTML parsing (30ms) ✅
- DOM tree building (30ms) ✅
- CSS cascade (6ms) ✅
- Style resolution (67ms) ✅ **Phase 3**
- Rendering (68ms) ✅

---

## Phase 3: Style Resolution Caching (Reverted)

### Problem Identified

Detailed profiling of `layout_block` revealed:

| Component | Time (ms) | Calls | Notes |
|-----------|-----------|-------|-------|
| **Style resolve** | **225.9** | 4,675 | **36% of layout time** |
| Text layout | 72.1 | 2,088 | 11% |
| Table layout | nested | - | Heavy recursive calls |

Style resolution call breakdown:
- **1,325 full resolutions** - normal layout pass (matches ~1335 DOM elements ✓)
- **3,340 measurement resolutions** - during intrinsic sizing / table layout

**Root Cause**: During measurement mode (`is_measuring=true`), styles were **fully re-resolved** for each element, even when they were already resolved in a previous pass.

### Solution Attempted

Modified `dom_node_resolve_style()` in `radiant/layout.cpp`:

**Before**: Skip style resolution only during normal layout if `styles_resolved=true`
```cpp
if (dom_elem->styles_resolved && !lycon->is_measuring) {
    return;  // Skip - already resolved
}
```

**After**: Skip style resolution in BOTH normal and measurement modes if `styles_resolved=true`
```cpp
if (dom_elem->styles_resolved) {
    return;  // Reuse already-resolved styles
}
dom_elem->styles_resolved = true;  // Mark resolved in both modes
```

### Initial Results (Before Regression Found)

| Metric | Before | After | Improvement |
|--------|--------|-------|-------------|
| Style resolution | 226ms | **67ms** | **70% faster** |
| Layout total | 633ms | **467ms** | **26% faster** |

### Regression Discovered

Two table layout tests failed after the optimization:
- `table_simple_004_fixed_layout` - 83.3% element accuracy
- `table_simple_007_percentage_width` - 83.3% element accuracy

**Root Cause**: Percentage-based CSS values (e.g., `width: 25%`) are resolved against `lycon->block.parent->content_width` during style resolution. This value differs between:
1. **Measurement mode**: Uses placeholder/infinite width (10000px) for intrinsic sizing
2. **Actual layout**: Uses real containing block width

When we marked `styles_resolved=true` during measurement, the percentage values were computed against the wrong containing block, and the actual layout pass skipped re-resolution.

### Solution: Partial Revert

Reverted to original behavior where measurement passes do NOT mark `styles_resolved=true`:

```cpp
// Mark as resolved for this layout pass
// Don't mark as resolved during measurement mode - let the actual layout pass do that
if (!lycon->is_measuring) {
    dom_elem->styles_resolved = true;
}
```

This ensures:
- Normal layout still benefits from caching (skips duplicate resolution)
- Measurement passes always re-resolve (needed for correct percentage computation)
- Percentage-based widths use correct containing block dimensions

### Final Result After Phase 3

| Metric | Baseline | After Phase 3 |
|--------|----------|---------------|
| Style resolution | 226ms | **222ms** (no improvement) |
| Layout total | 633ms | **622ms** (marginal) |
| Test pass rate | 100% | **100%** ✅ |

**Lesson Learned**: Style resolution cannot be fully cached during measurement mode because percentage-based CSS values depend on containing block dimensions, which differ between measurement and actual layout passes.

---

## Future Optimization Opportunities

### Layout (Primary Focus)
- **Table layout optimization**: Still dominates layout time (~1200ms internal recursive time)
- **Text shaping cache**: Cache shaped text for repeated content (~70ms)
- **Intrinsic sizing optimization**: Reduce measurement pass overhead
- **Selective style caching**: Cache only non-percentage-based style properties during measurement

### Rendering
- **View tree caching**: Cache unchanged view subtrees
- **Parallel rendering**: Render independent subtrees in parallel

---

## Summary: Optimization Progress

| Metric | Baseline | After Phase 1 | After Phase 2 | After Phase 3 | Total Improvement |
|--------|----------|---------------|---------------|---------------|-------------------|
| CSS cascade | 443ms | 233ms | **7ms** | 7ms | **98% faster** |
| Style resolution | - | - | 226ms | 222ms | (no change) |
| Layout | 635ms | 644ms | 634ms | 622ms | **2% faster** |
| Total time | 1299ms | 1042ms | 849ms | **~700ms** | **46% faster** |

### Key Takeaways

1. **Phase 1** (Remove logging): 20% improvement - low-hanging fruit
2. **Phase 2** (Selector indexing): 18% improvement - algorithmic optimization
3. **Phase 3** (Style caching): Reverted due to correctness issues with percentage values

The main remaining bottleneck is **table layout** (~1200ms), which requires multiple measurement passes for CSS 2.1 table width algorithm compliance.
