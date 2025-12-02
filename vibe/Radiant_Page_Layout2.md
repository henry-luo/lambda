# Radiant Layout Engine - Test Results Analysis & Implementation Plan

This document analyzes the test results from `make layout suite=page` comparing Radiant's layout output against browser references, and provides an implementation plan for fixing the identified issues.

---

## Test Results Summary (December 2, 2025)

### Overall Results

| Test Case | Elements Match | Text Match | Status |
|-----------|---------------|------------|--------|
| article-layout | 3.7% | 7.9% | ❌ FAIL |
| blog-homepage | 0.0% | 0.0% | ❌ FAIL |
| cern | 5.3% | 1.3% | ❌ FAIL |
| cern_servers | 3.2% | 28.6% | ❌ FAIL |
| cnn_lite | 8.9% | 98.2% | ❌ FAIL |
| contact-form | 7.7% | 0.0% | ❌ FAIL |
| css1_test | 0.0% | 0.0% | ❌ FAIL |
| dashboard-simple | 0.0% | 0.0% | ❌ FAIL |
| documentation | Error | - | 💥 CRASH |
| error-page | 0.0% | 0.0% | ❌ FAIL |
| example | 0.0% | 0.0% | ❌ FAIL |
| footer-showcase | 2.7% | 2.4% | ❌ FAIL |
| hn | 0.0% | 0.0% | ❌ FAIL |
| html2_spec | 0.0% | 0.0% | ❌ FAIL |
| legible | 17.9% | 6.7% | ❌ FAIL |
| newsletter | 0.0% | 0.0% | ❌ FAIL |
| npr | 1.4% | 0.0% | ❌ FAIL |
| paulgraham | 0.7% | 0.0% | ❌ FAIL |
| pricing-table | 10.3% | 1.9% | ❌ FAIL |
| sample2 | 25.0% | 37.5% | ❌ FAIL |
| sample3 | 0.0% | 0.0% | ❌ FAIL |
| sample4 | 7.7% | 0.0% | ❌ FAIL |
| sample5 | 0.0% | 0.0% | ❌ FAIL |
| table-comparison | 27.3% | 6.1% | ❌ FAIL |
| zengarden | 31.3% | 7.2% | ❌ FAIL |

**Pass Rate: 0/25 (0%)**

---

## Root Cause Analysis

### Issue #1: CSS Grid `repeat(auto-fill, minmax())` Not Working
**Impact: ~40% of page failures**
**Severity: 🔴 Critical**

**Affected Pages**: blog-homepage, documentation, dashboard-simple

**Problem**: Grid items render in a single column (full width, stacked) instead of multi-column grid layout.

**Evidence** (blog-homepage.html articles):
```
CSS: grid-template-columns: repeat(auto-fill, minmax(300px, 1fr))

Expected (3-column grid):
  Article 1: (16, 586, 368×411)    ← Column 1
  Article 2: (416, 586, 368×411)   ← Column 2
  Article 3: (816, 586, 368×411)   ← Column 3

Radiant (single column):
  Article 1: (16, 486, 1168×177)   ← Full width
  Article 2: (16, 664, 1168×152)   ← Full width, below
  Article 3: (16, 816, 1168×152)   ← Full width, below
```

**Root Cause**: `repeat(auto-fill, minmax())` track sizing not computing correct column count based on available width.

**Fix Location**: `radiant/layout_grid.cpp`

---

### Issue #2: Flexbox Items Collapsing to Zero Width
**Impact: ~30% of page failures**
**Severity: 🔴 Critical**

**Affected Pages**: blog-homepage (nav), many header layouts

**Problem**: Flex items with `display: flex; gap: 2rem` have 0 width.

**Evidence** (blog-homepage.html nav):
```
CSS: nav ul { display: flex; gap: 2rem; }

Expected:
  nav ul: (975.58, 22.39, 208.42×25.59)
  li items spread with gap

Radiant:
  nav ul: (1184, 16, 0×30)    ← width: 0!
  li items: all at x=1184 with width=0
```

**Root Cause**: Flex items not receiving intrinsic width from content when `gap` is applied.

**Fix Location**: `radiant/layout_flex.cpp`

---

### Issue #3: Font Metrics / Text Width Differences
**Impact: ~80% of tests have some text width variance**
**Severity: 🟡 Medium**

**Affected Pages**: All pages with text

**Problem**: Text widths consistently 5-15% wider in Radiant than browser.

**Evidence**:
```
"CSS Zen Garden" (h1):
  Radiant: 252.5px
  Browser: 232.94px   (8.4% narrower)

"Feature Comparison" (h1):
  Radiant: 362.2px
  Browser: 302.5px    (16.5% narrower)

"Note: " (pseudo ::before):
  Radiant: 42.7px
  Browser: ~40px      (6.8% narrower)
```

**Root Cause**: Different font-family fallback chain or FreeType font metrics differ from browser.

**Fix Location**: `radiant/layout_text.cpp`, font configuration

---

### Issue #4: Table Column Width Distribution
**Impact: Table-heavy pages**
**Severity: 🟡 Medium**

**Affected Pages**: table-comparison, pricing-table

**Problem**: First column too wide, other columns proportionally incorrect.

**Evidence** (table-comparison.html):
```
Column widths (Radiant → Browser):
  Feature:      443px → 350.39px   (+92.6px too wide)
  Starter:      207px → 236.05px   (-29px too narrow)
  Professional: 293px → 327.75px   (-34.8px too narrow)
  Enterprise:   225px → 253.81px   (-28.8px too narrow)
```

**Root Cause**: Table column distribution algorithm favoring first column.

**Fix Location**: `radiant/layout_table.cpp`

---

### Issue #5: Line Height / Vertical Spacing Accumulation
**Impact: Long documents**
**Severity: 🟡 Medium**

**Affected Pages**: cnn_lite, legible, zengarden

**Problem**: Y-positions drift increasingly as document progresses. Small per-line differences (1px) accumulate.

**Evidence** (cnn_lite.html list items):
```
Y-position drift:
  Item 1:  Radiant 110  vs Browser 109   (+1px)
  Item 10: Radiant 371  vs Browser 361   (+10px)
  Item 50: Radiant 1531 vs Browser 1481  (+50px)
```

**Root Cause**: Slight difference in line-height calculation or margin collapsing.

**Fix Location**: `radiant/layout_inline.cpp`, `radiant/layout_block.cpp`

---

### Issue #6: `position: sticky` Not Implemented
**Impact: Sticky headers don't work**
**Severity: 🟢 Low (for layout testing)**

**Affected Pages**: blog-homepage (header)

**Current Behavior**: Treated as `position: static`

**Fix Location**: `radiant/layout_positioned.cpp`

---

## Implementation Plan

### Phase 1: Critical Layout Fixes (Target: 50% pass rate)

#### 1.1 Fix CSS Grid `repeat(auto-fill, minmax())`
**Estimated Effort**: 2-3 days
**Files**: `radiant/layout_grid.cpp`, `radiant/grid.hpp`

**Tasks**:
1. Implement `auto-fill` track repetition count calculation:
   ```cpp
   // Calculate number of columns that fit
   int column_count = floor((available_width + gap) / (minmax_min + gap));
   column_count = max(1, column_count);
   ```
2. Implement `minmax(min, 1fr)` track sizing
3. Test with `repeat(auto-fill, minmax(300px, 1fr))`

**Success Criteria**: blog-homepage articles render in 3-column grid

#### 1.2 Fix Flexbox Zero-Width Items
**Estimated Effort**: 1-2 days
**Files**: `radiant/layout_flex.cpp`

**Tasks**:
1. Debug why flex items collapse when `gap` is applied
2. Ensure intrinsic content size is calculated before gap distribution
3. Check `flex-shrink` behavior with gaps

**Success Criteria**: blog-homepage nav items have correct width

---

### Phase 2: Text Accuracy Improvements (Target: 70% pass rate)

#### 2.1 Calibrate Font Metrics
**Estimated Effort**: 2-3 days
**Files**: `radiant/layout_text.cpp`, `lib/font_config.c`

**Tasks**:
1. Compare FreeType metrics vs browser for common fonts
2. Investigate letter-spacing or word-spacing differences
3. Consider using browser-compatible font metrics database

#### 2.2 Fix Line Height Accumulation
**Estimated Effort**: 1 day
**Files**: `radiant/layout_inline.cpp`

**Tasks**:
1. Audit line-height calculation for all units (normal, number, px, %)
2. Compare against browser computed values
3. Verify margin collapsing between blocks

---

### Phase 3: Table Layout Improvements (Target: 80% pass rate)

#### 3.1 Table Column Distribution Algorithm
**Estimated Effort**: 2-3 days
**Files**: `radiant/layout_table.cpp`

**Tasks**:
1. Implement CSS 2.1 table column width distribution
2. Use content-based minimum/maximum column widths
3. Distribute remaining space proportionally

---

### Phase 4: Polish & Edge Cases (Target: 90% pass rate)

#### 4.1 Implement `position: sticky`
**Estimated Effort**: 2 days
**Files**: `radiant/layout_positioned.cpp`

#### 4.2 Fix Minor Positioning Issues
**Estimated Effort**: 1-2 days

---

## Test Verification Commands

```bash
# Run all page tests
make layout suite=page

# Run specific test with verbose output
node test/layout/test_radiant_layout.js -t blog-homepage.html -v

# Run grid-specific tests
make layout suite=grid

# Capture new browser references (after browser changes)
make capture-layout suite=page
```

---

## Success Metrics

| Phase | Target Pass Rate | Key Tests |
|-------|-----------------|-----------|
| Phase 1 | 50% | blog-homepage, dashboard-simple |
| Phase 2 | 70% | cnn_lite, legible, zengarden |
| Phase 3 | 80% | table-comparison, pricing-table |
| Phase 4 | 90% | All pages |

---

## Priority Queue

| Priority | Issue | Impact | Effort | ROI |
|----------|-------|--------|--------|-----|
| 🔴 P0 | Grid `repeat(auto-fill, minmax())` | 40% of failures | 2-3 days | High |
| 🔴 P0 | Flexbox zero-width items | 30% of failures | 1-2 days | High |
| 🟡 P1 | Font metrics calibration | 80% variance | 2-3 days | Medium |
| 🟡 P1 | Table column distribution | Table pages | 2-3 days | Medium |
| 🟡 P2 | Line height accumulation | Long docs | 1 day | Medium |
| 🟢 P3 | `position: sticky` | Modern layouts | 2 days | Low |

---

## Dependencies & Prerequisites

1. **Grid Fix** requires understanding of `layout_grid_container()` current implementation
2. **Flexbox Fix** may interact with recent `gap` property additions
3. **Font Metrics** requires test harness for comparing specific glyphs

---

## Appendix: Detailed Test Output Samples

### blog-homepage.html - Grid Layout Failure

```
🏗️  Comparing elements: <article> vs <article>
   Radiant: (16, 486.4, 1168×177.6)   ← Full width, wrong position
   Browser: (16, 586.83, 368×411.42)  ← Grid column 1
   ❌ LAYOUT FAIL (800.0px > 17.6px)

📁 Children of <article>: 2 child nodes
  🏗️  Comparing elements: <div> vs <svg>
     ❌ TAG MISMATCH                   ← Structure differs due to sizing
```

### cnn_lite.html - Text Accumulation Drift

```
List items Y-position progression:
  li[1]:  110 → 109  (diff: +1px)
  li[5]:  226 → 221  (diff: +5px)
  li[10]: 371 → 361  (diff: +10px)
  li[20]: 661 → 641  (diff: +20px)
  li[50]: 1531 → 1481 (diff: +50px)
```

### table-comparison.html - Column Width Issues

```
<thead><tr> columns:
  th[Feature]:      443px vs 350.39px  (diff: +92.6px, +26%)
  th[Starter]:      207px vs 236.05px  (diff: -29px, -12%)
  th[Professional]: 293px vs 327.75px  (diff: -35px, -11%)
  th[Enterprise]:   225px vs 253.81px  (diff: -29px, -11%)
```
