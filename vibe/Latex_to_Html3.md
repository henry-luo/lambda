# LaTeX to HTML - Phase 2: Comprehensive LaTeX.js Compatibility

**Date**: November 20, 2025  
**Current Status**: 30/115 tests passing (26.1%)  
**Phase 1 Status**: ✅ Complete - 30/30 basic tests (100%)  
**Phase 2 Goal**: Achieve 80-90% compatibility with LaTeX.js test suite  
**Estimated Effort**: 20-30 hours over 4-6 weeks

---

## Executive Summary

Lambda has successfully achieved 100% pass rate (30/30 tests) on core document structure and basic formatting. We've now imported **all 115 fixtures from LaTeX.js** to establish a comprehensive test baseline. This document outlines the incremental implementation plan to achieve broad LaTeX compatibility.

**Current Test Breakdown**:
- ✅ **30 tests passing** - Core functionality (basic text, environments, formatting, sectioning)
- ❌ **85 tests failing** - Advanced features (fonts, spacing, symbols, boxes, macros, counters, labels)

**Strategic Approach**: Prioritize features by ROI (implementation effort vs. feature coverage) and skip features that are out of scope for Lambda's document processing mission.

---

## Current Test Results by Category

### ✅ Passing Categories (30 tests)

| Category | Tests | Pass Rate | Notes |
|----------|-------|-----------|-------|
| basic_text | 6/6 | 100% | Paragraphs, special chars, verbatim, dashes ✅ |
| basic_test | 2/2 | 100% | Basic functionality ✅ |
| environments | 7/7 | 100% | Lists, quotes, verbatim, center ✅ |
| formatting | 6/6 | 100% | Text styles, font sizes, alignment ✅ |
| sectioning | 4/4 | 100% | Sections, subsections, titles ✅ |
| text | 5/10 | 50% | Some advanced text features working |

### ❌ Failing Categories (85 tests)

| Category | Tests | Pass Rate | Priority | Estimated Effort |
|----------|-------|-----------|----------|------------------|
| **whitespace** | 0/21 | 0% | P1 | 8-12 hours |
| **spacing** | 0/4 | 0% | P2 | 4-6 hours |
| **fonts** | 0/8 | 0% | P2 | 6-8 hours |
| **symbols** | 0/4 | 0% | P3 | 3-4 hours |
| **text** (remaining) | 5/10 | 50% | P2 | 2-3 hours |
| **groups** | 0/3 | 0% | P3 | 2-3 hours |
| **boxes** | 0/6 | 0% | P4 | 8-10 hours |
| **label-ref** | 0/10 | 0% | Out of scope | N/A |
| **counters** | 0/2 | 0% | Out of scope | N/A |
| **macros** | 0/6 | 0% | Out of scope | N/A |
| **layout-marginpar** | 0/3 | 0% | Out of scope | N/A |
| **preamble** | 0/1 | 0% | Different approach | N/A |
| **math** | Skipped | N/A | Separate project | N/A |
| **picture** | Skipped | N/A | Out of scope | N/A |

---

## Scope Definition

### ✅ In Scope - Will Implement
Features that enhance document structure and text formatting:

1. **Whitespace Handling** (21 tests)
   - Edge cases for multiple spaces
   - Tab handling
   - Line ending normalization
   - Paragraph spacing

2. **Spacing Commands** (4 tests)
   - `\quad`, `\qquad` (em-space, 2×em-space)
   - `\enspace` (en-space)
   - `\hspace{...}` with measurements
   - `\vspace{...}` vertical spacing
   - `\negthinspace` (negative thin space)

3. **Font Declarations** (8 tests)
   - `\bfseries`, `\mdseries` (series declarations)
   - `\itshape`, `\upshape`, `\slshape` (shape declarations)
   - `\rmfamily`, `\sffamily`, `\ttfamily` (family declarations)
   - `\normalfont` (reset to normal)
   - `\em` (emphasis toggle)

4. **Text Content** (5 additional tests)
   - Paragraph indentation (`\noindent`)
   - Hyphenation hints (`\-`)
   - Text justification
   - Special punctuation

5. **Symbols** (4 tests)
   - `\char` (character by code)
   - `\symbol{...}` (symbol by hex code)
   - Predefined symbols (`\textellipsis`, etc.)
   - TeX escape sequences (`^^`, `^^^^`)

6. **Groups** (3 tests)
   - Scoping behavior
   - Group nesting
   - Local vs global changes

7. **Boxes** (6 tests) - *Lower priority*
   - `\mbox{...}` (inline box)
   - `\fbox{...}` (framed box)
   - `\framebox[width][pos]{...}`
   - `\parbox[pos][height][inner-pos]{width}{text}`
   - `minipage` environment
   - `\makebox`, `\raisebox`

### ❌ Out of Scope - Won't Implement
Features that require state management or are beyond document processing:

1. **Cross-References** (label-ref.tex - 10 tests)
   - `\label{...}`, `\ref{...}`, `\pageref{...}`
   - Requires multi-pass processing
   - State management across document

2. **Counters** (counters.tex - 2 tests)
   - `\newcounter`, `\setcounter`, `\addtocounter`, `\value{...}`
   - Requires global counter state
   - Not essential for document structure

3. **Macros** (macros.tex - 6 tests)
   - `\newcommand`, `\renewcommand`, `\def`
   - Requires command definition and expansion
   - Complex implementation

4. **Margin Notes** (layout-marginpar.tex - 3 tests)
   - `\marginpar{...}`
   - Requires page layout system
   - Not applicable to HTML flow

5. **Preamble** (preamble.tex - 1 test)
   - Document class processing
   - Lambda handles this differently

6. **Math Mode** (math.tex - skipped)
   - Separate project/library
   - Already excluded

7. **Picture Environment** (picture.tex - skipped)
   - Graphics and drawing commands
   - Not document structure

---

## Implementation Roadmap

### Phase 2.1: Whitespace Handling (Week 1-2) - 21 tests

**Priority**: P1 - Critical for robust text processing  
**Effort**: 8-12 hours  
**Impact**: +21 tests → 51/115 (44.3%)

**Objective**: Handle all edge cases for whitespace in LaTeX documents.

#### 2.1.1: Multiple Space Normalization
**File**: `lambda/input/input-latex.cpp`

**Current Behavior**: Multiple spaces might not be properly normalized.

**Implementation**:
```cpp
// In text parsing loop
static void normalize_whitespace(const char** latex) {
    // Skip any sequence of spaces/tabs/newlines
    while (**latex && isspace(**latex)) {
        (*latex)++;
    }
}

// Replace single space checks with normalize_whitespace()
```

**Tests to Pass**:
- Multiple consecutive spaces → single space
- Spaces at line start/end
- Tabs converted to spaces
- Mixed whitespace handling

#### 2.1.2: Comment Handling
**File**: `lambda/input/input-latex.cpp`

**Implementation**:
```cpp
// In parse_latex_element()
if (**latex == '%') {
    // Skip to end of line (LaTeX comment)
    while (**latex && **latex != '\n') {
        (*latex)++;
    }
    // Comment consumes the newline too
    if (**latex == '\n') (*latex)++;
    continue;
}
```

**Tests to Pass**:
- `%` comments removed from output
- Comments at end of line
- Comments with trailing spaces

#### 2.1.3: Paragraph Break Detection
**Enhancement**: Already working, but edge cases may need refinement.

**Tests to Pass**:
- Double newline → paragraph break
- Empty lines with spaces
- Multiple empty lines collapsed

**Estimated Completion**: 2 weeks (8-12 hours)  
**Risk**: Low - straightforward text processing

---

### Phase 2.2: Spacing Commands (Week 3) - 4 tests

**Priority**: P2 - Nice-to-have for typographic control  
**Effort**: 4-6 hours  
**Impact**: +4 tests → 55/115 (47.8%)

**Objective**: Implement horizontal and vertical spacing commands.

#### 2.2.1: Horizontal Spaces
**File**: `lambda/format/format-latex-html.cpp`

**Implementation**:
```cpp
// Add to process_latex_element()
else if (strcmp(cmd_name, "quad") == 0) {
    stringbuf_append_str(html_buf, " "); // em-space (U+2003)
}
else if (strcmp(cmd_name, "qquad") == 0) {
    stringbuf_append_str(html_buf, "  "); // 2× em-space
}
else if (strcmp(cmd_name, "enspace") == 0) {
    stringbuf_append_str(html_buf, " "); // en-space (U+2002)
}
else if (strcmp(cmd_name, "negthinspace") == 0) {
    stringbuf_append_str(html_buf, "<span class=\"negthinspace\"></span>");
}
```

**CSS Support**:
```css
.negthinspace {
    margin-left: -0.16667em;
}
```

#### 2.2.2: Measured Spaces
**File**: `lambda/format/format-latex-html.cpp`

**Implementation**:
```cpp
else if (strcmp(cmd_name, "hspace") == 0) {
    // Extract measurement argument
    // Convert to pixels or em units
    // Output: <span style="margin-right: Npx"></span>
}

else if (strcmp(cmd_name, "vspace") == 0) {
    // Vertical space
    // Output: <span class="vspace" style="margin-bottom: Npx"></span>
}
```

**LaTeX Units to Convert**:
- `cm` → pixels (1cm = 37.8px at 96dpi)
- `mm` → pixels (1mm = 3.78px)
- `in` → pixels (1in = 96px)
- `pt` → pixels (1pt = 1.333px)
- `em` → relative (1em = font size)
- `ex` → relative (1ex = x-height)

**Estimated Completion**: 1 week (4-6 hours)  
**Risk**: Low - mostly lookup tables

---

### Phase 2.3: Font Declarations (Week 4) - 8 tests

**Priority**: P2 - Important for text styling  
**Effort**: 6-8 hours  
**Impact**: +8 tests → 63/115 (54.8%)

**Objective**: Support declarative font changes.

#### 2.3.1: Font Series Commands
**File**: `lambda/format/format-latex-html.cpp`

**Implementation**:
```cpp
else if (strcmp(cmd_name, "bfseries") == 0) {
    // Switch to bold font
    // All following text is bold until scope ends
    // Implementation: use font stack
    push_font_series(font_context, FONT_BOLD);
}
else if (strcmp(cmd_name, "mdseries") == 0) {
    // Medium weight (normal)
    push_font_series(font_context, FONT_NORMAL);
}
```

**Font Context Stack**:
```cpp
typedef struct FontContext {
    std::vector<FontSeries> series_stack;   // bold, medium
    std::vector<FontShape> shape_stack;     // italic, upright, slant
    std::vector<FontFamily> family_stack;   // roman, sans, mono
} FontContext;

// When entering a group
void push_font_state(FontContext* ctx);

// When exiting a group
void pop_font_state(FontContext* ctx);

// Apply current font state to HTML
void apply_font_classes(StringBuf* html_buf, FontContext* ctx) {
    stringbuf_append_str(html_buf, "<span class=\"");
    if (current_series == FONT_BOLD) append("font-bold ");
    if (current_shape == FONT_ITALIC) append("font-italic ");
    // ...
    stringbuf_append_str(html_buf, "\">");
}
```

#### 2.3.2: Font Shape Commands

**Commands**:
- `\itshape` → italic
- `\slshape` → slanted (oblique)
- `\scshape` → small caps
- `\upshape` → upright (normal)

**Implementation**: Similar to series, using shape stack.

#### 2.3.3: Font Family Commands

**Commands**:
- `\rmfamily` → roman (serif)
- `\sffamily` → sans-serif
- `\ttfamily` → typewriter (monospace)

**Implementation**: Similar to series, using family stack.

#### 2.3.4: Emphasis Toggle

**File**: `lambda/format/format-latex-html.cpp`

**Implementation**:
```cpp
else if (strcmp(cmd_name, "em") == 0) {
    // Toggle emphasis
    // If currently upright → italic
    // If currently italic → upright
    toggle_emphasis(font_context);
}
```

**Estimated Completion**: 1 week (6-8 hours)  
**Risk**: Medium - requires font context tracking

---

### Phase 2.4: Text Features (Week 5) - 5 tests

**Priority**: P2 - Text quality improvements  
**Effort**: 2-3 hours  
**Impact**: +5 tests → 68/115 (59.1%)

**Objective**: Handle advanced text features.

#### 2.4.1: Paragraph Indentation
**File**: `lambda/format/format-latex-html.cpp`

**Implementation**:
```cpp
else if (strcmp(cmd_name, "noindent") == 0) {
    // Mark next paragraph as non-indented
    paragraph_state.no_indent = true;
}

// In paragraph processing
if (paragraph_state.no_indent) {
    stringbuf_append_str(html_buf, "<p class=\"noindent\">");
    paragraph_state.no_indent = false;
} else {
    stringbuf_append_str(html_buf, "<p>");
}
```

**CSS**:
```css
p { text-indent: 1.5em; }
p.noindent { text-indent: 0; }
```

#### 2.4.2: Hyphenation Hints
**File**: `lambda/input/input-latex.cpp`

**Implementation**:
```cpp
// In text parsing
if (**latex == '\\' && *(*latex + 1) == '-') {
    // Discretionary hyphen (soft hyphen)
    // HTML: &shy; (U+00AD)
    (*latex) += 2;
    append_to_text_buffer("­"); // soft hyphen
}
```

**Estimated Completion**: < 1 week (2-3 hours)  
**Risk**: Low - simple additions

---

### Phase 2.5: Symbols (Week 6) - 4 tests

**Priority**: P3 - Special character support  
**Effort**: 3-4 hours  
**Impact**: +4 tests → 72/115 (62.6%)

**Objective**: Support LaTeX symbol commands.

#### 2.5.1: Character by Code
**File**: `lambda/format/format-latex-html.cpp`

**Implementation**:
```cpp
else if (strcmp(cmd_name, "char") == 0) {
    // Extract numeric argument (decimal or hex)
    // \char98 or \char"A0
    int char_code = parse_char_code(elem);
    
    // Convert to UTF-8
    char utf8_buf[8];
    encode_utf8(char_code, utf8_buf);
    stringbuf_append_str(html_buf, utf8_buf);
}
```

#### 2.5.2: Symbol by Hex Code
**Implementation**:
```cpp
else if (strcmp(cmd_name, "symbol") == 0) {
    // \symbol{"00A9} → ©
    int char_code = parse_hex_arg(elem);
    
    char utf8_buf[8];
    encode_utf8(char_code, utf8_buf);
    stringbuf_append_str(html_buf, utf8_buf);
}
```

#### 2.5.3: TeX Escape Sequences
**File**: `lambda/input/input-latex.cpp`

**Implementation**:
```cpp
// In text parsing
if (**latex == '^' && *(*latex + 1) == '^') {
    // ^^XX notation (hex character)
    (*latex) += 2;
    char hex[3] = {**latex, *(*latex + 1), 0};
    int code = strtol(hex, NULL, 16);
    append_char_to_buffer(code);
    (*latex) += 2;
}
```

#### 2.5.4: Predefined Symbols
**Implementation**:
```cpp
// Add symbol lookup table
static struct {
    const char* name;
    const char* utf8;
} latex_symbols[] = {
    {"textellipsis", "…"},
    {"textdagger", "†"},
    {"textdaggerdbl", "‡"},
    {"textbullet", "•"},
    // ... more symbols
};

else if (strcmp(cmd_name, "text...") == 0) {
    const char* symbol = lookup_symbol(cmd_name);
    if (symbol) stringbuf_append_str(html_buf, symbol);
}
```

**Estimated Completion**: 1 week (3-4 hours)  
**Risk**: Low - lookup tables and conversions

---

### Phase 2.6: Groups (Week 7) - 3 tests

**Priority**: P3 - Scoping correctness  
**Effort**: 2-3 hours  
**Impact**: +3 tests → 75/115 (65.2%)

**Objective**: Ensure group scoping works correctly.

**Implementation**: Groups already work in parser (via AST structure), but need to verify:
1. Font state resets after group ends
2. Nested groups work correctly
3. No state leakage

**Testing Focus**:
- `{\bfseries bold text} normal text` → bold state resets
- `{\itshape {\bfseries bold italic} italic} normal` → nested states
- `test {text} more` → simple grouping

**Estimated Completion**: < 1 week (2-3 hours)  
**Risk**: Very low - mostly validation

---

### Phase 2.7: Boxes (Week 8-9) - 6 tests (Optional)

**Priority**: P4 - Advanced layout  
**Effort**: 8-10 hours  
**Impact**: +6 tests → 81/115 (70.4%)

**Objective**: Support basic box commands.

#### 2.7.1: Inline Boxes
**File**: `lambda/format/format-latex-html.cpp`

**Implementation**:
```cpp
else if (strcmp(cmd_name, "mbox") == 0) {
    // Inline box (prevent line break)
    stringbuf_append_str(html_buf, "<span class=\"mbox\">");
    process_element_content_simple(html_buf, elem, pool, depth);
    stringbuf_append_str(html_buf, "</span>");
}

else if (strcmp(cmd_name, "fbox") == 0) {
    // Framed box
    stringbuf_append_str(html_buf, "<span class=\"fbox\">");
    process_element_content_simple(html_buf, elem, pool, depth);
    stringbuf_append_str(html_buf, "</span>");
}
```

**CSS**:
```css
.mbox {
    display: inline-block;
    white-space: nowrap;
}

.fbox {
    display: inline-block;
    border: 1px solid black;
    padding: 2px;
}
```

#### 2.7.2: Paragraph Boxes
**Implementation**:
```cpp
else if (strcmp(cmd_name, "parbox") == 0) {
    // \parbox[pos][height][inner-pos]{width}{text}
    // Extract width argument
    // Create div with fixed width
    stringbuf_append_str(html_buf, "<div class=\"parbox\" style=\"width: ");
    append_width(html_buf, width_arg);
    stringbuf_append_str(html_buf, "\">");
    process_element_content(html_buf, elem, pool, depth);
    stringbuf_append_str(html_buf, "</div>");
}
```

#### 2.7.3: Minipage Environment
**Implementation**:
```cpp
else if (strcmp(env_name, "minipage") == 0) {
    // Similar to parbox but as environment
    stringbuf_append_str(html_buf, "<div class=\"minipage\">");
    process_element_content(html_buf, elem, pool, depth);
    stringbuf_append_str(html_buf, "</div>");
}
```

**Estimated Completion**: 2 weeks (8-10 hours)  
**Risk**: Medium - complex argument parsing  
**Note**: Can be deferred to Phase 3 if time constrained

---

## Expected Outcomes

### Target Pass Rates by Phase

| Phase | Focus | Tests Added | Cumulative | Pass Rate | Effort |
|-------|-------|-------------|------------|-----------|--------|
| Current | Core features | - | 30/115 | 26.1% | - |
| 2.1 | Whitespace | +21 | 51/115 | 44.3% | 8-12h |
| 2.2 | Spacing | +4 | 55/115 | 47.8% | 4-6h |
| 2.3 | Fonts | +8 | 63/115 | 54.8% | 6-8h |
| 2.4 | Text | +5 | 68/115 | 59.1% | 2-3h |
| 2.5 | Symbols | +4 | 72/115 | 62.6% | 3-4h |
| 2.6 | Groups | +3 | 75/115 | 65.2% | 2-3h |
| 2.7 | Boxes (opt) | +6 | 81/115 | 70.4% | 8-10h |

**Realistic Target**: 65-70% pass rate (75-81 tests passing)  
**Stretch Goal**: 80% pass rate with boxes implementation

### Features Not Implemented (Intentional)

**34 tests will remain failing**:
- label-ref.tex: 10 tests (cross-references)
- counters.tex: 2 tests (counter system)
- macros.tex: 6 tests (macro definitions)
- layout-marginpar.tex: 3 tests (margin notes)
- preamble.tex: 1 test (document class)
- math.tex: skipped (separate project)
- picture.tex: skipped (graphics)
- Some advanced box features: ~12 tests

**Rationale**: These features require:
- Global state management (counters, labels)
- Multi-pass processing (cross-references)
- Command definition/expansion (macros)
- Page layout system (margin notes)
- Advanced rendering (pictures)

All are beyond Lambda's core mission of document structure processing.

---

## Implementation Guidelines

### Coding Standards

1. **Incremental Development**
   - Implement one phase at a time
   - Run full test suite after each change
   - Commit working code frequently

2. **Test-Driven Approach**
   - Enable tests as features are implemented
   - Use `--gtest_filter` to focus on specific tests
   - Debug failures immediately

3. **Error Handling**
   - Graceful degradation for unsupported features
   - Log warnings for unimplemented commands
   - Don't crash on unknown LaTeX constructs

4. **Performance**
   - Keep font context stack lightweight
   - Avoid string allocations in hot paths
   - Use pool allocation for temporary buffers

5. **Code Organization**
   - Keep parser changes in `input-latex.cpp`
   - Keep formatter changes in `format-latex-html.cpp`
   - Add helper functions to separate files if needed

### Testing Strategy

```bash
# Enable specific test category
# Edit test_latex_html_fixtures.cpp and remove from skip list

# Run specific category
./test/test_latex_html_fixtures.exe --gtest_filter='*whitespace*'

# Run all tests
./test/test_latex_html_fixtures.exe

# Check progress
./test/test_latex_html_fixtures.exe 2>&1 | grep -E "(Loaded|PASSED|FAILED)"
```

### Skip List Management

**File**: `test/latex/test_latex_html_fixtures.cpp:180-195`

As features are implemented, remove tests from skip list:

```cpp
std::set<std::string> tests_to_skip_parser = {
    // Phase 2.1 - Remove after whitespace implementation
    // "whitespace test 1",
    // "whitespace test 2",
    // ...
    
    // Permanent skips (out of scope)
    "counters",
    "clear inner counters",
    "label ...",
    "macros ...",
    // ...
};
```

---

## Risk Assessment

### High Risk Items

1. **Font Context Stack**
   - **Risk**: Complex state management, potential bugs
   - **Mitigation**: Start with simple push/pop, add tests, refine
   - **Fallback**: Implement font commands as simple wrappers first

2. **Whitespace Edge Cases**
   - **Risk**: Breaking existing tests
   - **Mitigation**: Comprehensive testing, start with most common cases
   - **Fallback**: Feature flag to enable/disable new whitespace handling

### Medium Risk Items

1. **Measurement Conversions**
   - **Risk**: Incorrect conversions affect layout
   - **Mitigation**: Use well-tested conversion constants
   - **Fallback**: Support subset of units initially

2. **Box Rendering**
   - **Risk**: Complex argument parsing
   - **Mitigation**: Implement incrementally, start with simple boxes
   - **Fallback**: Skip boxes entirely (P4 priority)

### Low Risk Items

1. **Symbol Tables**
   - Straightforward lookup tables
   - Well-defined behavior

2. **Spacing Commands**
   - Simple HTML/CSS output
   - Clear specifications

---

## Success Metrics

### Phase 2 Complete When:

**Must Have**:
- ✅ 65% pass rate (75+ tests passing)
- ✅ All P1 and P2 priorities implemented
- ✅ Whitespace handling robust
- ✅ Font declarations working
- ✅ No regressions in Phase 1 tests

**Should Have**:
- ✅ 70% pass rate (81+ tests passing)
- ✅ All P3 priorities implemented
- ✅ Symbol support complete
- ✅ Groups validated

**Nice to Have**:
- ⭐ 80% pass rate (92+ tests passing)
- ⭐ Boxes partially implemented
- ⭐ Performance benchmarks documented

---

## Timeline

### Weeks 1-2: Whitespace (P1)
- **Deliverable**: 51/115 tests passing
- **Review**: Edge case testing

### Week 3: Spacing (P2)
- **Deliverable**: 55/115 tests passing
- **Review**: Typography quality

### Week 4: Fonts (P2)
- **Deliverable**: 63/115 tests passing
- **Review**: Font context correctness

### Week 5: Text Features (P2)
- **Deliverable**: 68/115 tests passing
- **Review**: Text quality

### Week 6: Symbols (P3)
- **Deliverable**: 72/115 tests passing
- **Review**: Character support

### Week 7: Groups (P3)
- **Deliverable**: 75/115 tests passing
- **Review**: Scoping validation

### Weeks 8-9: Boxes (P4, Optional)
- **Deliverable**: 81/115 tests passing
- **Review**: Layout quality

---

## Conclusion

Phase 2 will transform Lambda's LaTeX processor from "basic document structure" to "comprehensive document formatting". By focusing on high-value, achievable features and explicitly excluding complex features that are out of scope, we can reach **65-80% test compatibility** in 4-9 weeks.

**Next Steps**:
1. Review this plan with team
2. Begin Phase 2.1: Whitespace handling
3. Track progress in this document
4. Update skip list as features complete
5. Celebrate milestones! 🎉

---

**Document Status**: ✅ Complete - Ready for Implementation  
**Last Updated**: November 20, 2025  
**Author**: Lambda Development Team
