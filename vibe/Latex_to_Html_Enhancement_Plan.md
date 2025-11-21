# LaTeX to HTML Enhancement Plan
**Date**: November 21, 2025
**Status**: Analysis Complete - Ready for Implementation
**Current Test Results**: 6/75 passing (8%) in extended tests

---

## Executive Summary

After analyzing the failing tests and existing code, I've identified the root causes of failures and prioritized fixes that will have the highest impact. The current implementation has good foundation work but needs:

1. **Critical Issue**: Wrong HTML container class (`latex-document` vs `body`)
2. **Missing Features**: Font state tracking, spacing commands, and various LaTeX constructs
3. **Out of Scope**: Counters, labels/refs, macros (as documented)

**Target**: Achieve 50-60% pass rate (35-45 tests) by fixing high-impact issues
**Estimated Effort**: 15-20 hours over 2-3 weeks

---

## Test Failure Analysis

### Overall Statistics
- **Total Tests**: 75 (2 loader tests + 73 parametrized)
- **Passing**: 6 tests (8%)
- **Failing**: 69 tests (92%)
- **Skipped**: 4 tests

### Failure Categories

| Category | Tests | Priority | Impact | Implementable |
|----------|-------|----------|--------|---------------|
| **HTML Container Mismatch** | 69/69 | P0 | Critical | ✅ Yes - 1 hour |
| **Font Declarations** | 8/8 | P1 | High | ✅ Yes - 6-8 hours |
| **Spacing Commands** | 4/4 | P1 | High | ⚠️ Partial - 2-3 hours |
| **Whitespace Handling** | 21/21 | P2 | Medium | ⚠️ Partial - 4-6 hours |
| **Text Features** | 10/10 | P2 | Medium | ⚠️ Partial - 3-4 hours |
| **Groups** | 3/3 | P2 | Medium | ✅ Yes - 2 hours |
| **Boxes** | 6/6 | P3 | Low | ⚠️ Partial - 6-8 hours |
| **Symbols** | 4/4 | P3 | Low | ✅ Yes - 3-4 hours |
| **Counters** | 2/2 | ❌ | N/A | ❌ Out of scope |
| **Labels/Refs** | 10/10 | ❌ | N/A | ❌ Out of scope |
| **Macros** | 6/6 | ❌ | N/A | ❌ Out of scope |
| **Layout/Margin** | 3/3 | ❌ | N/A | ❌ Out of scope |
| **Preamble** | 1/1 | ❌ | N/A | ❌ Out of scope |

---

## Root Cause Analysis

### 1. HTML Container Class Mismatch (P0 - CRITICAL)
**Impact**: Affects ALL 69 failing tests
**Root Cause**: Formatter outputs `<div class="latex-document">` but tests expect `<div class="body">`

**Evidence from test output**:
```
Expected: <div class="body"><p>...
Actual:   <div class="latex-document"><p>...

1. Content mismatch at position 12
   Expected: b
   Actual:   l
   Context:  <div class=">>><<<body">
```

**Fix Location**: `lambda/format/format-latex-html.cpp:114`
```cpp
// Current:
stringbuf_append_str(html_buf, "<div class=\"latex-document\">\n");

// Should be:
stringbuf_append_str(html_buf, "<div class=\"body\">\n");
```

**Estimated Time**: 1 hour (includes testing)
**Impact**: Will fix the primary failure point in all 69 tests

---

### 2. Font Declaration Commands (P1 - HIGH PRIORITY)
**Impact**: 8 tests failing
**Status**: Not implemented

**Missing Commands**:
- `\bfseries` - Bold font series (declarative)
- `\mdseries` - Medium weight
- `\itshape` - Italic shape
- `\slshape` - Slanted/oblique
- `\upshape` - Upright (normal)
- `\scshape` - Small caps
- `\em` - Emphasis toggle
- Font context needs to persist across groups

**Current Issue**: These commands are parsed but not formatted:
```cpp
// lambda/format/format-latex-html.cpp
else if (strcmp(cmd_name, "bfseries") == 0) {
    process_element_content - elem or items is null  // Just skipped!
}
```

**Required Implementation**:
1. **Font Context Stack** - Track current font state through document
2. **Font State Tracking** - Series (bold/medium), Shape (italic/upright), Family (serif/sans/mono)
3. **Group Scoping** - Font changes apply until group ends
4. **HTML Generation** - Wrap affected text in `<span>` with appropriate classes

**Implementation Plan**:

```cpp
// Add to format-latex-html.cpp
typedef struct FontContext {
    std::vector<FontSeries> series_stack;   // bold, medium
    std::vector<FontShape> shape_stack;     // italic, upright, slant, sc
    std::vector<FontFamily> family_stack;   // roman, sans, mono
    std::vector<bool> em_stack;             // emphasis toggle state
} FontContext;

// Font state enums
enum FontSeries { FONT_MEDIUM, FONT_BOLD };
enum FontShape { FONT_UPRIGHT, FONT_ITALIC, FONT_SLANTED, FONT_SC };
enum FontFamily { FONT_ROMAN, FONT_SANS, FONT_MONO };

// Implement font state management
void push_font_state(FontContext* ctx);
void pop_font_state(FontContext* ctx);
void apply_font_wrapper(StringBuf* buf, FontContext* ctx, bool opening);
```

**CSS Classes Expected** (from test failures):
- `.bf` - bold
- `.it` - italic
- `.sl` - slanted
- `.up` - upright
- `.tt` - typewriter/monospace
- `.sf` - sans-serif
- `.rm` - roman/serif
- `.sc` - small-caps

**Estimated Time**: 6-8 hours
**Test Coverage**: Will fix 8 font tests

---

### 3. Spacing Commands (P1 - HIGH PRIORITY)  
**Impact**: 4 tests failing
**Status**: Partially implemented

**Current Status**:
- ✅ `\quad`, `\qquad`, `\enspace`, `\,` (thinspace), `\!` (negthinspace) - Working
- ❌ `\hspace{dim}` - Not working (outputs `<p>3cm</p>` instead of styled span)
- ❌ `\vspace{dim}` - Not working
- ❌ `\smallskip`, `\medskip`, `\bigskip` - Not implemented
- ❌ `\smallbreak`, `\medbreak`, `\bigbreak` - Not implemented

**Test Failures**:
```
Expected: <span style="margin-right:113.386px"></span>
Actual:   <p>3cm</p>
```

**Issues**:
1. `\hspace{}` argument is being formatted as paragraph instead of dimension
2. Need to implement vertical spacing commands
3. Skip commands (smallskip, etc.) not recognized

**Fix Required**:
```cpp
// In format-latex-html.cpp
else if (strcmp(cmd_name, "hspace") == 0) {
    // Extract dimension from first argument
    Element* arg = get_first_arg(elem);
    const char* dim_str = get_text_content(arg);
    double pixels = latex_dim_to_pixels(dim_str);
    
    stringbuf_append_str(html_buf, "<span style=\"margin-right:");
    // append pixels
    stringbuf_append_str(html_buf, "px\"></span>");
}

else if (strcmp(cmd_name, "vspace") == 0) {
    // Similar but margin-bottom for vertical
}
```

**Estimated Time**: 2-3 hours
**Test Coverage**: Will fix 3-4 spacing tests

---

### 4. Whitespace Handling (P2 - MEDIUM PRIORITY)
**Impact**: 21 tests failing
**Status**: Partially implemented

**Current Status**:
- ✅ Basic normalization implemented (multiple spaces → single)
- ✅ Tilde → nbsp conversion
- ❌ Comment handling (`%` to EOL) not working
- ❌ Some edge cases failing

**Test Failures Examples**:
- Comments not removed from output
- `\empty` command spacing issues
- Paragraph break detection edge cases

**Remaining Work**:
1. Implement comment stripping in parser
2. Fix `\empty{}` command handling
3. Edge case refinement

**Estimated Time**: 4-6 hours (most work already done)
**Test Coverage**: Will fix 15-18 of 21 tests

---

### 5. Text Features (P2 - MEDIUM PRIORITY)
**Impact**: 10 tests failing
**Status**: Partially working (5/10 passing)

**Missing Features**:
- Ligature control in `\texttt{}` (monospace shouldn't use ligatures)
- Alignment commands (`\raggedright`, `\centering`, etc.)
- Better dash handling (`--` vs `---`)
- Better special character handling

**Test Failure Example**:
```latex
\texttt{first test of -- and ---}

Expected: <span class="tt">first test of -- and ---</span>
Actual:   <span class="latex-texttt">first test of – and —</span>
```

**Issue**: Ligatures (-- → – and --- → —) are being applied in monospace context where they shouldn't

**Fix**:
- Add ligature flag to font context
- Disable ligatures when in `\texttt{}` or `\ttfamily` mode
- Update dash conversion logic to check font context

**Estimated Time**: 3-4 hours
**Test Coverage**: Will fix 3-5 additional text tests

---

### 6. Groups (P2 - MEDIUM PRIORITY)
**Impact**: 3 tests failing
**Status**: Parser handles groups, formatter needs scoping

**Issue**: Font and other state changes don't properly scope to groups

**Test Example**:
```latex
outer { text \bfseries what} more text

Expected: outer text <span class="bf">what</span> more text
Actual:   outer text what more text  // bfseries not applied
```

**Fix**: Requires font context stack (see Font Declarations above)

**Estimated Time**: 2 hours (mostly covered by font work)
**Test Coverage**: 3 group tests

---

### 7. Out of Scope Features (DO NOT IMPLEMENT)

Based on the project documentation, these are intentionally excluded:

#### Counters (2 tests) ❌
- `\newcounter`, `\setcounter`, `\stepcounter`, `\arabic`, `\roman`, etc.
- Requires global state management
- Complex counter hierarchy and reset logic

#### Labels & References (10 tests) ❌
- `\label{}`, `\ref{}`, `\pageref{}`
- Requires multi-pass processing
- Cross-reference resolution with numbering

#### Macros (6 tests) ❌
- `\newcommand`, `\renewcommand`, `\def`
- Requires command definition and expansion
- Macro argument processing

#### Layout/Margin (3 tests) ❌
- `\marginpar{}`
- Requires page layout system
- Not applicable to HTML flow

#### Preamble (1 test) ❌
- Document class processing
- Lambda handles differently

**Total Out of Scope**: 22 tests (will always fail)

---

## Implementation Priorities

### Phase 1: Quick Win (2-3 hours) - Target: 30% pass rate
**Goal**: Fix critical blocker affecting all tests

1. ✅ **Fix HTML Container Class** (1 hour)
   - Change `latex-document` to `body`
   - Update CSS class names to match expected
   - Test all categories

2. ✅ **Implement `\hspace{}` and `\vspace{}`** (2 hours)
   - Fix argument extraction
   - Use existing `latex_dim_to_pixels()` function
   - Add proper span generation

**Expected Result**: ~22 tests passing (30%)

---

### Phase 2: Font Support (6-8 hours) - Target: 45% pass rate
**Goal**: Implement complete font declaration system

1. **Font Context Infrastructure** (3 hours)
   - Create FontContext structure
   - Implement push/pop stack operations
   - Add font state tracking through groups

2. **Font Commands** (3 hours)
   - `\bfseries`, `\itshape`, `\slshape`, `\upshape`
   - `\rmfamily`, `\sffamily`, `\ttfamily`
   - `\em` toggle behavior

3. **HTML Generation** (2 hours)
   - Wrap font changes in spans
   - Apply correct CSS classes
   - Handle nested font changes

**Expected Result**: ~34 tests passing (45%)

---

### Phase 3: Polish (6-8 hours) - Target: 55-60% pass rate
**Goal**: Handle remaining implementable features

1. **Whitespace Edge Cases** (3 hours)
   - Comment stripping
   - `\empty` handling
   - Edge case fixes

2. **Text Features** (3 hours)
   - Ligature control in monospace
   - Better dash handling
   - Special characters

3. **Groups & Symbols** (2 hours)
   - Group scoping verification
   - Symbol commands

**Expected Result**: ~42-45 tests passing (55-60%)

---

## Detailed Implementation Steps

### Step 1: Fix HTML Container (1 hour)

**File**: `lambda/format/format-latex-html.cpp`

```cpp
// Line 114 - Change container class
stringbuf_append_str(html_buf, "<div class=\"body\">\n");

// Line 250 - Update closing
stringbuf_append_str(html_buf, "</div>\n");

// Update CSS classes throughout:
// latex-section → section or h2
// latex-textbf → bf
// latex-textit → it
// latex-texttt → tt
// latex-emph → it (or up for toggle)
// etc.
```

**Testing**: Run full test suite, should see immediate improvement

---

### Step 2: Implement Font Context System (3-4 hours)

**File**: `lambda/format/format-latex-html.cpp`

**Add structures**:
```cpp
enum FontSeries { FS_MEDIUM, FS_BOLD };
enum FontShape { FH_UPRIGHT, FH_ITALIC, FH_SLANTED, FH_SC };
enum FontFamily { FF_ROMAN, FF_SANS, FF_MONO };

typedef struct FontState {
    FontSeries series;
    FontShape shape;
    FontFamily family;
    bool emphasis;
} FontState;

typedef struct FontContext {
    Pool* pool;
    std::vector<FontState> stack;
    FontState current;
} FontContext;

// Initialize
void init_font_context(FontContext* ctx, Pool* pool) {
    ctx->pool = pool;
    ctx->current = {FS_MEDIUM, FH_UPRIGHT, FF_ROMAN, false};
    ctx->stack.clear();
}

// Push current state when entering group
void push_font_state(FontContext* ctx) {
    ctx->stack.push_back(ctx->current);
}

// Pop state when exiting group
void pop_font_state(FontContext* ctx) {
    if (!ctx->stack.empty()) {
        ctx->current = ctx->stack.back();
        ctx->stack.pop_back();
    }
}

// Apply font classes to HTML
void start_font_span(StringBuf* buf, FontContext* ctx) {
    bool needs_span = (ctx->current.series != FS_MEDIUM ||
                       ctx->current.shape != FH_UPRIGHT ||
                       ctx->current.family != FF_ROMAN);
    
    if (!needs_span) return;
    
    stringbuf_append_str(buf, "<span class=\"");
    
    if (ctx->current.series == FS_BOLD) 
        stringbuf_append_str(buf, "bf ");
    if (ctx->current.shape == FH_ITALIC) 
        stringbuf_append_str(buf, "it ");
    else if (ctx->current.shape == FH_SLANTED) 
        stringbuf_append_str(buf, "sl ");
    else if (ctx->current.shape == FH_SC) 
        stringbuf_append_str(buf, "sc ");
    if (ctx->current.family == FF_MONO) 
        stringbuf_append_str(buf, "tt ");
    else if (ctx->current.family == FF_SANS) 
        stringbuf_append_str(buf, "sf ");
    
    stringbuf_append_str(buf, "\">");
}

void end_font_span(StringBuf* buf, FontState* prev_state) {
    bool had_span = (prev_state->series != FS_MEDIUM ||
                     prev_state->shape != FH_UPRIGHT ||
                     prev_state->family != FF_ROMAN);
    
    if (had_span) {
        stringbuf_append_str(buf, "</span>");
    }
}
```

**Integrate into processing**:
```cpp
// Update format_latex_to_html() to create and pass FontContext
FontContext font_ctx;
init_font_context(&font_ctx, pool);

// Pass to all processing functions
process_latex_element_reader(html_buf, ast_reader, pool, &font_ctx, 1);
```

**Handle font commands**:
```cpp
else if (strcmp(cmd_name, "bfseries") == 0) {
    FontState prev = font_ctx->current;
    font_ctx->current.series = FS_BOLD;
    start_font_span(html_buf, font_ctx);
    // Rest of content inherits this
}

else if (strcmp(cmd_name, "itshape") == 0) {
    FontState prev = font_ctx->current;
    font_ctx->current.shape = FH_ITALIC;
    start_font_span(html_buf, font_ctx);
}

// Similar for other commands
```

---

### Step 3: Fix Spacing Commands (2 hours)

**File**: `lambda/format/format-latex-html.cpp`

```cpp
else if (strcmp(cmd_name, "hspace") == 0) {
    // Get first argument element
    if (elem->items && elem->items->length > 0) {
        Item first = elem->items->items[0];
        if (first.type == LMD_TYPE_ELEMENT) {
            Element* arg_elem = first.element;
            // Extract text from argument
            String* dim_str = extract_text_content(arg_elem);
            if (dim_str) {
                double pixels = latex_dim_to_pixels(dim_str->chars);
                
                stringbuf_append_str(html_buf, "<span style=\"margin-right:");
                char px_str[32];
                snprintf(px_str, sizeof(px_str), "%.3fpx", pixels);
                stringbuf_append_str(html_buf, px_str);
                stringbuf_append_str(html_buf, "\"></span>");
            }
        }
    }
    return;
}

else if (strcmp(cmd_name, "vspace") == 0) {
    // Similar but margin-bottom
    if (elem->items && elem->items->length > 0) {
        Item first = elem->items->items[0];
        if (first.type == LMD_TYPE_ELEMENT) {
            Element* arg_elem = first.element;
            String* dim_str = extract_text_content(arg_elem);
            if (dim_str) {
                double pixels = latex_dim_to_pixels(dim_str->chars);
                
                stringbuf_append_str(html_buf, "<span class=\"vspace\" style=\"margin-bottom:");
                char px_str[32];
                snprintf(px_str, sizeof(px_str), "%.3fpx", pixels);
                stringbuf_append_str(html_buf, px_str);
                stringbuf_append_str(html_buf, "\"></span>");
            }
        }
    }
    return;
}

// Add skip commands
else if (strcmp(cmd_name, "smallskip") == 0) {
    stringbuf_append_str(html_buf, "<span class=\"vspace-inline smallskip\"></span>");
    return;
}
else if (strcmp(cmd_name, "medskip") == 0) {
    stringbuf_append_str(html_buf, "<span class=\"vspace medskip\"></span>");
    return;
}
else if (strcmp(cmd_name, "bigskip") == 0) {
    stringbuf_append_str(html_buf, "<span class=\"vspace bigskip\"></span>");
    return;
}
```

**Helper function**:
```cpp
// Extract text content from nested elements
static String* extract_text_content(Element* elem) {
    if (!elem || !elem->items) return NULL;
    
    // Look for string items in children
    for (size_t i = 0; i < elem->items->length; i++) {
        Item item = elem->items->items[i];
        if (item.type == LMD_TYPE_STRING) {
            return item.string;
        }
        // Recursively check nested elements
        if (item.type == LMD_TYPE_ELEMENT && item.element) {
            String* nested = extract_text_content(item.element);
            if (nested) return nested;
        }
    }
    return NULL;
}
```

---

## Testing Strategy

### Incremental Testing
After each phase, run:
```bash
./test/test_latex_html_extended.exe | grep -E "(PASSED|FAILED|tests from)"
```

### Target Metrics
- **Phase 1**: 22/75 tests (30%)
- **Phase 2**: 34/75 tests (45%)  
- **Phase 3**: 42-45/75 tests (55-60%)

### Regression Prevention
- Baseline tests must always pass (25/25)
- Document any expected failures

---

## Success Criteria

### Minimum Success (Phase 1)
- ✅ HTML container class fixed
- ✅ Basic spacing commands working
- ✅ 30% overall pass rate

### Target Success (Phase 2)
- ✅ Font declarations fully working
- ✅ Font state properly scoped
- ✅ 45% overall pass rate

### Stretch Goal (Phase 3)
- ✅ All whitespace edge cases handled
- ✅ Text features polished
- ✅ 55-60% overall pass rate

---

## Risk Assessment

### High Risk
- **Font Context System**: Most complex feature, affects many tests
  - **Mitigation**: Implement incrementally, test each command
  - **Fallback**: Implement simple version without full scoping first

### Medium Risk
- **Spacing Command Arguments**: Argument extraction may be tricky
  - **Mitigation**: Debug with simple test cases first
  - **Fallback**: Skip `\hspace{}` and `\vspace{}` if needed

### Low Risk
- **HTML Container Fix**: Simple string replacement
- **Basic Command Addition**: Straightforward implementations

---

## Timeline Estimate

### Week 1 (10 hours)
- **Phase 1**: HTML container + spacing (3 hours)
- **Phase 2 Start**: Font context infrastructure (4 hours)
- **Phase 2 Continue**: Font commands (3 hours)

### Week 2 (8 hours)
- **Phase 2 Complete**: Font HTML generation + testing (5 hours)
- **Phase 3 Start**: Whitespace edge cases (3 hours)

### Week 3 (4 hours)
- **Phase 3 Complete**: Text features + final polish (4 hours)
- **Documentation**: Update status in Latex_to_Html3.md

**Total**: 22 hours over 3 weeks

---

## Next Steps

1. ✅ Review this plan with team
2. ⏳ Create branch for implementation
3. ⏳ Start with Phase 1 (HTML container fix)
4. ⏳ Test after each phase
5. ⏳ Update documentation as we progress

---

## Conclusion

This enhancement plan focuses on **implementable, high-impact features** while respecting the project's scope limitations. By fixing the HTML container issue and implementing font declarations, we can achieve 45% pass rate with reasonable effort.

The excluded features (counters, labels, macros) represent 22 tests that will always fail, which is expected and documented. Our realistic target is **42-45 passing tests out of 53 implementable tests (79-85% of in-scope tests)**.

**Status**: Ready for implementation 🚀
