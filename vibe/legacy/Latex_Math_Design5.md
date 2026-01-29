# LaTeX Math Design 5: Extended Math Feature Support

**Date:** January 22, 2026  
**Status:** Proposal  
**Scope:** Analysis of failing extended math tests and improvement roadmap

---

## 1. Test Analysis Summary

### Current Status
- **Baseline Tests:** 18/19 passing (94.7%)
- **Extended Tests:** 0/46 passing (targeting advanced math features)

### Test Failure Categories

| Category | Tests | Key Issues |
|----------|-------|------------|
| **Accents** | Accents | Missing accent positioning, wrong fonts, missing wide accents |
| **Arrows** | Arrows | Extensible arrows not implemented (`\xrightarrow`, `\xleftarrow`) |
| **Arrays** | Array | Array/matrix layout incomplete, missing alignment |
| **Big Operators** | BigOperators | Limit placement, display vs inline sizing |
| **Binomials** | Choose | `\choose` primitive and `\binom` not fully working |
| **Delimiters** | ExtensibleDelims | Tall delimiters for matrices, extensible construction |
| **Fractions** | Fracs | Nested fractions, `\over` primitive, page overflow |
| **Negation** | Not | `\not` overlay positioning |
| **Phantoms** | Phantoms | `\phantom`, `\hphantom`, `\vphantom`, `\smash` |
| **Nested** | NestedStructures | Deeply nested fractions, radicals, matrices |
| **Over** | Over | `\over` primitive (TeX-style fractions) |
| **Scripts** | Scripts | Complex limit placement, `\mathop`, `\limits`, `\nolimits` |
| **Sampler** | Sampler | Comprehensive test (page breaks, many features) |
| **Simple** | SimpleMath | Font scaling (`cmbx12` vs `cmbx10`), section headings |

---

## 2. Priority 1: Accent System Overhaul

### Current State
The accent system handles basic accents (`\hat`, `\bar`, `\tilde`, `\dot`, `\ddot`) but has issues:

1. **Font selection:** Using wrong font for some accents (cmsy10 vs cmmi10)
2. **Positioning:** Accent not centered over base character
3. **Wide accents:** `\widehat`, `\widetilde` not stretching to content width
4. **Stacked accents:** `\hat{\bar{x}}` not properly nested

### Test Output Analysis (Accents)
```
Reference: "^ab~cde_fghi~vodxyzfabc^x+ya+ba+bx+ynz}|{..."
Output:    "^a^b~c^d^e_fg^hi~v?^xyz^abc^x+ya+ba+bx+y^..."
```
- Extra accent characters appearing (duplicated `^`)
- Missing combining characters from reference

### Proposed Implementation

#### 2.1 Accent Font Mapping Table
```cpp
struct AccentInfo {
    int32_t codepoint;      // Codepoint in font
    const char* font_name;  // cmsy10, cmmi10, cmr10, cmex10
    bool is_wide;           // Can stretch?
    float skew_correction;  // Horizontal adjustment
};

static const AccentInfo ACCENT_TABLE[] = {
    // Single-char accents (cmmi10 preferred for math)
    {"hat",   0x5E, "cmmi10", false, 0.0f},
    {"check", 0x14, "cmmi10", false, 0.0f},
    {"tilde", 0x7E, "cmmi10", false, 0.0f},
    {"acute", 0x13, "cmmi10", false, 0.0f},
    {"grave", 0x12, "cmmi10", false, 0.0f},
    {"dot",   0x5F, "cmmi10", false, 0.0f},
    {"ddot",  0x7F, "cmmi10", false, 0.0f},
    {"breve", 0x15, "cmmi10", false, 0.0f},
    {"bar",   0x16, "cmmi10", false, 0.0f},
    {"vec",   0x7E, "cmmi10", false, 0.0f},  // vector arrow
    {"mathring", 0x17, "cmmi10", false, 0.0f},
    
    // Wide accents (cmex10, extensible)
    {"widehat",   0x62, "cmex10", true, 0.0f},  // start of chain
    {"widetilde", 0x65, "cmex10", true, 0.0f},
};
```

#### 2.2 Skew Correction
Italic characters need horizontal accent adjustment. TFM files contain skew values per character:
```cpp
float get_accent_skew(TFMFont* tfm, int char_code) {
    // Read from TFM lig_kern program or use kern table
    return tfm->get_skew_char(char_code);
}
```

#### 2.3 Wide Accent Chain Walking
Similar to delimiter sizing, wide accents have size chains in cmex10:
- `\widehat`: positions 98 → 99 → 100 (increasing widths)
- `\widetilde`: positions 101 → 102 → 103

---

## 3. Priority 2: Extensible Arrow System

### Current State
Commands like `\xrightarrow{above}[below]` are parsed but not rendered as extensible arrows.

### Test Output Analysis (Arrows)
```
Reference: "word!!word  !word word !a2over!bover caover!underbover undercaover!b underc1"
Output:    "?!?^word ?a2?b?ca?b?ca?b?c"
```
- Arrow glyphs missing entirely
- Text labels not positioned

### Proposed Implementation

#### 3.1 Extensible Arrow Structure
```cpp
struct ExtensibleArrow {
    int32_t left_piece;     // Left arrow tip (cmsy10)
    int32_t right_piece;    // Right arrow tip (cmsy10)
    int32_t middle_piece;   // Repeating middle bar
    float min_width;        // Minimum arrow width
};

static const ExtensibleArrow ARROWS[] = {
    {"rightarrow",     0, 33, 0, 10.0f},  // →
    {"leftarrow",     32, 0, 0, 10.0f},   // ←
    {"leftrightarrow", 32, 33, 0, 15.0f}, // ↔
    {"Rightarrow",     0, 41, 0, 10.0f},  // ⇒
    {"Leftarrow",     40, 0, 0, 10.0f},   // ⇐
};
```

#### 3.2 Layout Algorithm
1. Typeset `above` and `below` content in scriptstyle
2. Calculate required arrow width: `max(above_width, below_width) + 2×padding`
3. Build extensible arrow by repeating middle piece
4. Stack: above → arrow → below with proper spacing

---

## 4. Priority 3: Array & Matrix Alignment

### Current State
`\begin{array}` and `\matrix` parse but produce incorrect alignment.

### Test Output Analysis (Array)
```
Reference: "AnArrayabccdefabcdDone.1"
Output:    "AnArrayabccdefbcd"
```
- Missing row separators
- Column alignment not working

### Proposed Implementation

#### 4.1 Array Layout Algorithm
```cpp
struct ArrayLayout {
    int num_cols;
    int num_rows;
    char* col_alignments;  // 'l', 'c', 'r' per column
    float* col_widths;     // Maximum width per column
    float* row_heights;    // Height per row
    float* row_depths;     // Depth per row
    float col_sep;         // \arraycolsep
    float row_sep;         // \arraystretch * baselineskip
};
```

#### 4.2 Two-Pass Layout
1. **Pass 1:** Measure all cells, track max width per column
2. **Pass 2:** Position cells with alignment, add struts

---

## 5. Priority 4: Phantom Boxes

### Current State
`\phantom`, `\hphantom`, `\vphantom` not implemented.

### Test Output Analysis (Phantoms)
```
Reference: "a++c12px=pya=b+c+d+epypyababpx+pypx+py1"
Output:    "a+b+c1+2pyx=pya=b+c+d+ep?pyababpx+pypx+pxy"
```
- Phantom content appearing instead of being invisible
- Spacing not preserved

### Proposed Implementation

```cpp
enum PhantomType { FULL, HORIZONTAL, VERTICAL, SMASH };

TexNode* typeset_phantom(MathASTNode* content, PhantomType type, MathContext& ctx) {
    TexNode* inner = typeset_node(content, ctx);
    
    TexNode* phantom = make_hbox(ctx.arena);
    switch (type) {
        case FULL:       // \phantom - full box, no rendering
            phantom->width = inner->width;
            phantom->height = inner->height;
            phantom->depth = inner->depth;
            phantom->flags |= TEX_NO_RENDER;
            break;
        case HORIZONTAL: // \hphantom - width only
            phantom->width = inner->width;
            phantom->height = 0;
            phantom->depth = 0;
            phantom->flags |= TEX_NO_RENDER;
            break;
        case VERTICAL:   // \vphantom - height/depth only
            phantom->width = 0;
            phantom->height = inner->height;
            phantom->depth = inner->depth;
            phantom->flags |= TEX_NO_RENDER;
            break;
        case SMASH:      // \smash - render but zero height
            phantom->first_child = inner;
            phantom->width = inner->width;
            phantom->height = 0;
            phantom->depth = 0;
            break;
    }
    return phantom;
}
```

---

## 6. Priority 5: `\not` Negation Overlay

### Current State
`\not` produces wrong output - should overlay a slash on the following character.

### Test Output Analysis (Not)
```
Reference: "...6=;6<;6;6;6;6;6;6;6v;..."
Output:    "...?=;?<;?;?;?;?\;?;?;??;..."
```
- `\not` producing `?` instead of negation slash
- Overlay positioning wrong

### Proposed Implementation

```cpp
TexNode* typeset_not(MathASTNode* operand, MathContext& ctx) {
    TexNode* base = typeset_node(operand, ctx);
    
    // Create negation slash overlay
    TexNode* slash = make_char_node(0x36, "cmsy10", ctx);  // negation slash
    
    // Center slash over base
    slash->x = (base->width - slash->width) / 2;
    slash->y = 0;
    
    // Create composite
    TexNode* result = make_hbox(ctx.arena);
    result->width = base->width;
    result->height = max(base->height, slash->height);
    result->depth = max(base->depth, slash->depth);
    add_child(result, base);
    add_child(result, slash);
    
    return result;
}
```

---

## 7. Priority 6: `\over` Primitive and `\choose`

### Current State
The TeX primitive `\over` (generalized fraction) not working correctly with grouping.

### Test Output Analysis (Over)
```
Reference: "1TestingOverabcdefabcdefabcdefabcdefabcdef1"
Output:    "1TestingOverabc?defabc?defabc?de?fa?bc?de?fa?bc?de?f"
```
- `\over` producing `?` characters
- Grouping with `\bgroup`/`\egroup` not working

### Proposed Implementation

#### 7.1 Generalized Fraction Primitive
```cpp
// \over, \atop, \above, \choose, \brace, \brack
struct GenFrac {
    TexNode* numerator;
    TexNode* denominator;
    float rule_thickness;    // 0 for \atop, -1 for default
    int32_t left_delim;      // 0 for none, '(' for \choose
    int32_t right_delim;     // 0 for none, ')' for \choose
};

// Special cases:
// \over      → rule=default, delims=none
// \atop      → rule=0, delims=none
// \choose    → rule=0, delims=()
// \brace     → rule=0, delims={}
// \brack     → rule=0, delims=[]
```

---

## 8. Priority 7: Complex Script Positioning

### Current State
`\mathop`, `\limits`, `\nolimits` combinations not working.

### Test Output Analysis (Scripts)
```
Reference: "fa;fa;faa;faaa,aCABCABCABCABACBACBACBACBCABCABABCABCCBA1"
Output:    "fa;fa;faa;faaa,a??B?C??C?B??B?C??C?B??B?C??C?B??B?C??C?B??C??C??C??C?"
```
- `\mathop{A}\limits_B^C` producing `?` instead of A
- Script positioning completely wrong

### Proposed Implementation

#### 8.1 Operator Limits State Machine
```cpp
enum LimitsMode { DEFAULT, LIMITS, NOLIMITS };

struct OpState {
    TexNode* nucleus;
    TexNode* subscript;
    TexNode* superscript;
    LimitsMode mode;
    bool is_big_op;  // \sum, \prod, etc.
};

// Process limits/nolimits modifiers
void process_limit_modifier(OpState& state, const char* cmd) {
    if (strcmp(cmd, "limits") == 0) state.mode = LIMITS;
    else if (strcmp(cmd, "nolimits") == 0) state.mode = NOLIMITS;
}

// Apply scripts based on mode
TexNode* apply_scripts(OpState& state, MathContext& ctx) {
    bool use_limits = (state.mode == LIMITS) ||
                      (state.mode == DEFAULT && state.is_big_op && ctx.style == DISPLAY);
    
    if (use_limits) {
        return typeset_op_limits(state.nucleus, state.subscript, state.superscript, ctx);
    } else {
        return typeset_scripts(state.nucleus, state.subscript, state.superscript, ctx);
    }
}
```

---

## 9. Font Scaling Issues

### Current State
Several tests show `cmbx12` vs `cmbx10` mismatches in headings.

### Root Cause
Section headings use 12pt bold font, but our engine defaults to 10pt.

### Proposed Fix
```cpp
// Track document base font size
struct DocumentContext {
    float base_font_size = 10.0f;  // Default
};

// Font scaling for headings
float get_heading_font_size(int level, float base) {
    switch (level) {
        case 1: return base * 1.44f;  // \Large
        case 2: return base * 1.20f;  // \large
        case 3: return base * 1.00f;  // \normalsize
        default: return base;
    }
}
```

---

## 10. Implementation Roadmap

### Phase 1: Quick Wins (1-2 days each)
1. **Phantom boxes** - Simple implementation, fixes Phantoms test
2. **`\not` overlay** - Single overlay mechanism
3. **Font scaling** - Fix heading sizes

### Phase 2: Core Improvements (3-5 days each)
4. **Accent system overhaul** - Fix font selection, positioning
5. **`\over`/`\choose` primitives** - Generalized fraction
6. **Complex scripts** - `\limits`/`\nolimits` handling

### Phase 3: Major Features (1-2 weeks each)
7. **Array layout** - Full two-pass algorithm
8. **Extensible arrows** - Arrow construction system
9. **Wide accents** - Extensible accent sizing

### Phase 4: Integration (Ongoing)
10. **Nested structure testing** - Combine all features
11. **Page breaking** - Multi-page math content

---

## 11. Test-Driven Development

For each feature:
1. Extract minimal failing test case from extended tests
2. Implement feature
3. Verify minimal test passes
4. Run full extended test suite
5. Promote passing tests to baseline

### Expected Progression
| Milestone | Baseline | Extended |
|-----------|----------|----------|
| Current   | 18/19    | 0/46     |
| Phase 1   | 19/19    | 3/46     |
| Phase 2   | 19/19    | 10/46    |
| Phase 3   | 19/19    | 25/46    |
| Phase 4   | 19/19    | 40/46    |

---

## 12. References

- TeXBook Chapter 17: More About Math (accents)
- TeXBook Chapter 18: Fine Points of Mathematics (\over, \choose)
- TeXBook Appendix G: Rules 11-18 (accent placement, generalized fractions)
- cmmi10.tfm, cmex10.tfm character tables
- amsmath.sty source for `\xrightarrow`, `\overset`
