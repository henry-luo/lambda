# Lambda Chart Library — Implementation Round 2

**Date:** 2025-02-19  
**Baseline:** 239/239 tests passing  
**Library size:** 12 modules, 2,348 lines of Lambda Script

---

## What Was Implemented

### 1. All 7 Data Transforms (transform.ls: 82 → 225 lines)

Previously only **filter** and **sort** were working; the remaining 5 transforms were stubs that returned data unchanged. All 7 are now fully implemented and tested.

| Transform | Element Syntax | What It Does |
|-----------|---------------|--------------|
| **filter** | `<filter field: "x", op: "==", value: "A">` | Keep rows matching a condition (==, !=, >, >=, <, <=) |
| **sort** | `<sort field: "amount", order: "ascending">` | Sort rows by a field (ascending/descending) |
| **aggregate** | `<aggregate; <group field: "cat">; <agg op: "sum", field: "amt", as: "total">>` | Group-by + aggregate (sum/count/mean/median/min/max/distinct/q1/q3/stdev/variance) |
| **calculate** | `<calculate as: "total", op: "*", field: "price", field2: "qty">` | Add computed field (+, -, *, /, copy, string, float, int) |
| **bin** | `<bin field: "amount", step: 10>` | Discretize continuous values into bins (auto or manual step) |
| **fold** | `<fold fields: ["amt", "score"]>` | Unpivot wide → long format (one row per field per original row) |
| **flatten** | `<flatten fields: ["tags"]>` | Explode array-valued field into separate rows |

Key implementation details:
- **aggregate** uses `map([k1, v1, k2, v2, ...])` (VMap) for dynamic key construction, with group-by via `unique_vals()` + string concatenation of group keys
- **sort** uses `for (d in data order by d[field]) d` instead of `sort(data, comparator)` (see Issue #14 below)
- **bin** auto-computes step via `util.nice_num(range / maxbins)` when no explicit step given
- **fold/flatten** use nested `for` comprehensions to expand rows
- **`compute_agg`** is exported as `pub fn` for external use (e.g., summary statistics)

### 2. Opacity Encoding (mark.ls, chart.ls, parse.ls, vega.ls)

Wired opacity as a visual encoding channel, matching the pattern used for color/size:

- **parse.ls:** Added `opacity_el`, `stroke_el`, `theta_el`, `text_el` channel extraction in `parse_encoding()`
- **chart.ls:** Added `opacity_ch` channel resolution, `opacity_scale` creation, passed `opacity_field`/`opacity_scale` to mark context
- **mark.ls:** Bar and point marks now compute per-datum `bar_opacity`/`pt_opacity` via `scale.scale_apply(opacity_scale, float(d[opacity_field]))`
- **vega.ls:** Added `opacity` and `stroke` to `convert_encoding()` output

### 3. VMap Value Stabilization (vmap.cpp — Engine Fix)

Fixed a critical engine bug where computed numeric values (from `sum()`, `avg()`, etc.) stored in VMap became garbage when crossing function/module boundaries.

**Files changed:** `lambda/vmap.cpp`

**What changed:**
- Added `num_values` ArrayList to `HashMapData` struct for heap-owned numeric storage
- Added `stabilize_value()` function that copies float/int64/datetime from num_stack to malloc'd heap storage
- Modified `hashmap_data_set()` to call `stabilize_value()` on every value
- Modified `hashmap_data_copy()` to stabilize values in the copy
- Modified `hashmap_data_free()` to free the heap-allocated values

### 4. Parse Fix (parse.ls)

Fixed 4 missing closing quotes on symbol literals: `'opacity` → `'opacity'`, `'theta` → `'theta'`, `'text` → `'text'`, `'stroke` → `'stroke'`.

### 5. Updated Baseline Test Expected Outputs

Regenerated expected SVG output for 3 chart tests (bar, line, scatter) to reflect the opacity attribute additions.

---

## Issues Encountered

### Resolved

| # | Issue | Root Cause | Resolution |
|---|-------|-----------|------------|
| 13 | **VMap values corrupted across module boundaries** | `fn_sum()` returns via `push_d()` onto `num_stack`. `frame_end()` resets `num_stack`, invalidating pointers. Arrays handle this via `array_set()` which copies float values into the array's own buffer space. But `hashmap_data_set()` stored raw Item pointers directly — pointers became stale after frame cleanup. | **Engine fix** in `vmap.cpp`: `stabilize_value()` copies float/int64/datetime to malloc'd heap storage tracked in `HashMapData.num_values`. |
| 14 | **`sort(data, comparator)` destroys map items** | `fn_sort2` treats the 2nd argument as a direction string ("asc"/"desc"), not as a comparator lambda. Internally calls `item_to_double()` on every item, converting maps to NaN. | **Workaround**: Use `for (d in data order by d[field]) d` syntax which transpiles to `fn_sort_by_keys` and preserves original Item types. |
| 15 | **C2MIR ternary type mismatch in `group_key_str`** | `if-else` returning `String*` (from `fn_string()`) vs `Item` (from `fn_str_join()`) — incompatible types in conditional expression. | **Workaround**: Avoided the ternary by always collecting into an array first: `let parts = for (f in fields) string(row[f]); str_join(parts, "|||")`. |
| 16 | **C2MIR implicit pointer cast with `: string` annotation** | `pub fn compute_agg(data, op: string, field)` — `: string` makes the C parameter `String*` but callers pass `Item`, causing type error. | **Workaround**: Removed `: string` annotation. Param types on pub functions must match caller conventions. |
| 17 | **Missing closing quotes on symbol literals** | `'opacity` parsed as unterminated symbol followed by `, count)` as identifier—causing 4 parse errors in parse.ls. | **Fix**: Added closing quotes: `'opacity'`, `'theta'`, `'text'`, `'stroke'`. |
| 18 | **`//` comments after element expressions** | `// comment` can be parsed as division operator when following certain expressions. | **Workaround**: Use block comments or move comments to their own line above the expression. |

### Outstanding / Known Limitations

| # | Issue | Impact | Notes |
|---|-------|--------|-------|
| 1 | **No stacking support** | Cannot render stacked bar or stacked area charts | Needs `stack` transform + cumulative offset in mark rendering |
| 2 | **No facet / concat / repeat** | Only single-view and layer compositions | Multi-view dashboards not possible yet |
| 3 | **No temporal (time) scale** | Date/time axes not supported | Data must be converted to numeric manually |
| 4 | **Gradient legend uses discrete rects** | No SVG `<linearGradient>` | Visual approximation with 20 `<rect>` elements |
| 5 | **Band/point scale O(n) lookup** | Linear scan for category index | Acceptable for <100 categories |
| 6 | **VMap display shows `[unknown type map!!]`** | `string(vmap)` doesn't produce readable output | Values are correct — only display/stringification is affected. Access fields directly: `m.total` works. |
| 7 | **Size encoding not wired** | `size` channel parsed but not connected to marks | Need to add size scale + per-datum radius in point_mark |

---

## Next Actions for the Chart Package

### High Priority

1. **Wire size encoding** — Add `size_scale` to chart context and connect to `point_mark` (radius) and `bar` (bar width). Pattern identical to opacity wiring already done.

2. **Implement stack transform** — Required for stacked bar charts and stacked area charts. Needs:
   - A `stack` transform that computes cumulative y0/y1 offsets per group
   - Mark rendering to use y0/y1 instead of 0/y for bar baseline
   - Stack ordering options (ascending, descending, none)

3. **Add stroke encoding** — `stroke_el` is parsed but not connected to marks. Wire stroke color scale similar to fill color.

4. **Temporal scale** — Add date/time scale type for time-series charts. Parse temporal data strings, compute nice time ticks (day/month/year).

### Medium Priority

5. **Facet composition** — Split data by a field and render one sub-chart per facet value in a grid layout. Needs layout computation for sub-chart positioning.

6. **Concat / repeat composition** — Arrange multiple independent chart specs horizontally or vertically.

7. **Tooltip / annotation marks** — Text labels positioned at data points, useful for highlighting specific values.

8. **SVG linearGradient** — Replace discrete-rect gradient legends with proper SVG gradient elements.

9. **VMap stringification** — Add VMap support to `fn_string` so `string(vmap)` produces `{key: value, ...}` format instead of `[unknown type map!!]`.

### Low Priority

10. **Config / theming** — Support `config` object for global defaults (fonts, colors, padding, axis style).

11. **Selection / interaction** — Brush selection, hover highlight (requires event handling infrastructure).

12. **Additional mark types** — Boxplot (composite mark), error bars, trail, geoshape.

13. **Responsive sizing** — viewBox-based scaling that adapts to container width.

14. **Data loading from URL/file** — `data: {url: "data.csv"}` support via `input()` function.

---

## Module Status Summary

| Module | Lines | Status | Changes This Round |
|--------|-------|--------|--------------------|
| `chart.ls` | 388 | ✅ | Added opacity scale + mark context fields |
| `parse.ls` | 164 | ✅ | Added opacity/stroke/theta/text channels; fixed symbol quotes |
| `mark.ls` | 337 | ✅ | Wired per-datum opacity for bar + point marks |
| `scale.ls` | 190 | ✅ | No changes |
| `axis.ls` | 189 | ✅ | No changes |
| `legend.ls` | 141 | ✅ | No changes |
| `layout.ls` | 114 | ✅ | No changes |
| `color.ls` | 103 | ✅ | No changes |
| `svg.ls` | 159 | ✅ | No changes |
| `transform.ls` | 225 | ✅ | **Fully rewritten** — all 7 transforms implemented |
| `util.ls` | 164 | ✅ | No changes |
| `vega.ls` | 176 | ✅ | Added opacity/stroke encoding conversion |

**Engine fix:** `lambda/vmap.cpp` — VMap numeric value stabilization (+37 lines)
