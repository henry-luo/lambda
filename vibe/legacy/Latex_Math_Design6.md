# LaTeX Math Typesetting Enhancement Proposal

**Date**: January 28, 2026
**Status**: Proposal
**Target**: Improve math test pass rate from 40.9% to 75%+

---

## 1. Current State Analysis

### Test Results Overview (from `make test-math`)

| Category | Score | Status |
|----------|-------|--------|
| **operators** | 79.9% | ✅ Good |
| **sqrt** | 69.6% | ✅ Good |
| **matrix** | 62.8% | ⚠️ Moderate |
| **greek** | 61.8% | ⚠️ Moderate |
| **fracs** | 59.9% | ⚠️ Moderate |
| **delims** | 59.3% | ⚠️ Moderate |
| **arrows** | 59.0% | ⚠️ Moderate |
| **scripts** | 56.2% | ⚠️ Moderate |
| **complex** | 52.4% | ⚠️ Moderate |
| **fonts** | 51.2% | ⚠️ Moderate |
| **bigops** | 38.6% | ❌ Poor |
| **not** | 33.4% | ❌ Poor |
| **choose** | 27.6% | ❌ Poor |
| **spacing** | 26.0% | ❌ Poor |
| **accents** | 22.9% | ❌ Poor |

**Weighted Average**: 40.9%
**Total Tests**: 56 (0 passed, 48 failed, 8 skipped)

### Test Framework Architecture

The test framework compares Lambda's output against authoritative references:

```
┌─────────────────────────────────────────────────────────────────┐
│                    Test Comparison Layers                        │
├─────────────────────────────────────────────────────────────────┤
│  AST Layer (50% weight)                                          │
│  └─ Lambda AST vs MathLive MathML (semantic structure)          │
│                                                                  │
│  HTML Layer (40% weight)                                         │
│  └─ Lambda HTML vs MathLive/KaTeX HTML (visual representation)  │
│                                                                  │
│  DVI Layer (10% weight)                                          │
│  └─ Lambda DVI vs pdfTeX DVI (typographic precision)            │
└─────────────────────────────────────────────────────────────────┘
```

Reference files location: `test/latex/reference/`
- `*.mathml.json` - MathLive MathML structure (authoritative for AST)
- `*.mathlive.html`, `*.katex.html` - HTML references
- `*.pdftex.dvi` - pdfTeX DVI output (authoritative for typographics)

---

## 2. Root Cause Analysis

### 2.1 AST Structure Mismatch

**Problem**: Lambda's AST uses TeX-centric branch naming that doesn't align with MathML semantics.

| Lambda AST | MathML Expected | Issue |
|------------|-----------------|-------|
| `above` | `numer` (in fractions) | Different naming |
| `below` | `denom` (in fractions) | Different naming |
| `body` | `children` array | Structural difference |
| `ACCENT` | `<mover accent="true">` | Missing accent character output |

**Example - Accent Mismatch**:

Lambda AST output (`accents_basic_0.ast.json`):
```json
{
  "type": "ACCENT",
  "body": { "type": "ORD", "codepoint": 97, "value": "a" }
}
```

Expected MathML structure (`accents_basic_0.mathml.json`):
```json
{
  "mathml": "<mover accent=\"true\"><mi>a</mi><mo>&#x005e;</mo></mover>",
  "structure": {
    "type": "root",
    "children": [
      { "tag": "mi", "content": "a" },
      { "tag": "mo", "content": "&#x005e;" }
    ]
  }
}
```

**Gap**: Lambda's ACCENT node doesn't include the accent character in the JSON output.

### 2.2 Missing Math Constructs

#### Binomial Coefficients (`\binom`, `\choose`)
- **Score**: 27.6%
- **Current**: Not implemented or producing wrong structure
- **Expected**: FRAC with `rule_thickness=0` wrapped in parentheses

#### Negation Overlay (`\not`)
- **Score**: 33.4%
- **Current**: Simple NOT node with body
- **Expected**: Composite structure with overlay semantics
- **MathML**: `<menclose notation="updiagonalstrike">` or negation slash overlay

#### Math Spacing Commands
- **Score**: 26.0%
- **Current**: SPACE nodes with `width_mu` only
- **Expected**: MathML-compatible `<mspace width="Xem"/>`

### 2.3 Incomplete Accent Implementation

**Current** (`tex_math_ast_builder.cpp:1060`):
```cpp
MathASTNode* MathASTBuilder::build_accent(TSNode node) {
    // ...
    int32_t accent_char = '^';  // default
    if (cmd_len == 3 && strncmp(cmd, "hat", 3) == 0) accent_char = '^';
    else if (cmd_len == 3 && strncmp(cmd, "bar", 3) == 0) accent_char = '-';
    // Missing: ddot, check, acute, grave, breve, wide accents
}
```

**Issues**:
1. Limited accent mapping (only hat, bar, tilde, vec, dot)
2. Accent character not exported in JSON output
3. No wide accent handling (widehat, widetilde, overline)

### 2.4 Big Operator Limits Placement

**Score**: 38.6%

**Issue**: Display mode vs inline mode not properly distinguished for limit placement:
- Display mode: `\sum_{i=0}^n` → limits above/below (sum-style)
- Display mode: `\int_a^b` → scripts to side (integral-style)
- Text mode: both use inline scripts

Current implementation has `FLAG_LIMITS` but doesn't properly check display context.

---

## 3. Enhancement Proposals

### 3.1 Enhanced AST-to-MathML Mapping Layer

**Priority**: HIGH
**Impact**: +15-20% overall score
**Files**: `lambda/tex/tex_math_ast_builder.cpp`

Create a dedicated MathML-compatible JSON export function:

```cpp
// New function alongside math_ast_to_json()
void math_ast_to_mathml_json(MathASTNode* node, StrBuf* out);
```

**Mapping rules**:

| Lambda Node | MathML Output |
|-------------|---------------|
| `FRAC` | `{"tag":"mfrac","numer":{...},"denom":{...}}` |
| `ACCENT` | `{"tag":"mover","accent":true,"base":{...},"operator":"^"}` |
| `SCRIPTS` | `{"tag":"msubsup"/"msub"/"msup",...}` |
| `SPACE` | `{"tag":"mspace","width":"1em"}` |
| `ROW` | `{"tag":"mrow","children":[...]}` |
| `DELIMITED` | `{"tag":"mrow","children":[left,content,right]}` |

### 3.2 Implement Missing Constructs

#### 3.2.1 Binomial Coefficients

**Add to** `tex_math_ast_builder.cpp`:

```cpp
// Handler for \binom{n}{k}
MathASTNode* MathASTBuilder::build_binom(TSNode node) {
    // Parse two arguments
    MathASTNode* numer = build_ts_node(ts_node_named_child(node, 0));
    MathASTNode* denom = build_ts_node(ts_node_named_child(node, 1));

    // Create fraction with zero rule thickness
    MathASTNode* frac = make_math_frac(arena, numer, denom, 0.0f);
    frac->frac.left_delim = '(';
    frac->frac.right_delim = ')';

    // Wrap in delimiters
    return make_math_delimited(arena, '(', frac, ')', true);
}

// Handler for {n \choose k}
MathASTNode* MathASTBuilder::build_choose(TSNode node) {
    // Similar to binom - extract left and right of \choose
    // Create FRAC with rule_thickness=0, wrapped in parens
}
```

**Grammar addition** (if needed in `tree-sitter-lambda/grammar.js`):
```javascript
binom: $ => seq('\\binom', $.group, $.group),
choose: $ => seq($.group, '\\choose', $.group),
```

#### 3.2.2 Enhanced Negation (`\not`)

**Current**: `tex_math_ast_builder.cpp:665` handles `\not` but output lacks overlay info

**Enhancement**:

```cpp
// Add to MathASTNode struct (tex_math_ast.hpp)
struct MathASTNode {
    // ... existing fields ...

    // For NOT nodes
    struct {
        int32_t slash_codepoint;  // Negation slash character
        bool is_overlay;          // True for overlay negation
    } negation;
};

// Enhanced JSON output
// {"type":"NOT","overlay":true,"slash":"⧸","body":{...}}
```

#### 3.2.3 Math Spacing

**Add spacing table** to `tex_math_ast_builder.cpp`:

```cpp
struct SpacingEntry {
    const char* command;
    float width_mu;        // TeX math units (1/18 em)
    const char* mathml_width;  // MathML-compatible width
};

static const SpacingEntry SPACING_TABLE[] = {
    {"quad",     18.0f, "1em"},
    {"qquad",    36.0f, "2em"},
    {",",         3.0f, "0.167em"},   // thin space
    {":",         4.0f, "0.222em"},   // medium space
    {";",         5.0f, "0.278em"},   // thick space
    {"!",        -3.0f, "-0.167em"},  // negative thin
    {"enspace",   9.0f, "0.5em"},
    {"hspace",    0.0f, nullptr},     // Variable width
    {nullptr, 0, nullptr}
};
```

**Enhanced SPACE node**:

```cpp
// Add to MathASTNode SPACE variant
struct {
    float width_mu;
    const char* mathml_width;  // Pre-computed MathML width string
} space;
```

### 3.3 Complete Accent Implementation

**Add comprehensive accent table** to `tex_math_ast_builder.cpp`:

```cpp
struct AccentEntry {
    const char* command;
    int32_t accent_codepoint;     // Unicode for <mo>
    int32_t combining_codepoint;  // Unicode combining version
    bool is_wide;                 // Wide accent (widehat, widetilde)
};

static const AccentEntry ACCENT_TABLE[] = {
    // Standard accents
    {"hat",       0x005E, 0x0302, false},  // CIRCUMFLEX ACCENT ^
    {"check",     0x02C7, 0x030C, false},  // CARON ˇ
    {"tilde",     0x007E, 0x0303, false},  // TILDE ~
    {"acute",     0x00B4, 0x0301, false},  // ACUTE ACCENT ´
    {"grave",     0x0060, 0x0300, false},  // GRAVE ACCENT `
    {"dot",       0x02D9, 0x0307, false},  // DOT ABOVE ˙
    {"ddot",      0x00A8, 0x0308, false},  // DIAERESIS ¨
    {"dddot",     0x20DB, 0x20DB, false},  // COMBINING THREE DOTS ABOVE
    {"ddddot",    0x20DC, 0x20DC, false},  // COMBINING FOUR DOTS ABOVE
    {"breve",     0x02D8, 0x0306, false},  // BREVE ˘
    {"bar",       0x00AF, 0x0304, false},  // MACRON ¯
    {"vec",       0x2192, 0x20D7, false},  // RIGHT ARROW →

    // Wide accents
    {"widehat",   0x0302, 0x0302, true},   // Wide circumflex
    {"widetilde", 0x0303, 0x0303, true},   // Wide tilde
    {"overline",  0x00AF, 0x0305, true},   // Overline ¯
    {"underline", 0x0332, 0x0332, true},   // Underline
    {"overbrace", 0x23DE, 0x23DE, true},   // Top curly bracket ⏞
    {"underbrace",0x23DF, 0x23DF, true},   // Bottom curly bracket ⏟

    {nullptr, 0, 0, false}
};
```

**Enhanced `build_accent()`**:

```cpp
MathASTNode* MathASTBuilder::build_accent(TSNode node) {
    int len;
    const char* text = node_text(node, &len);

    const char* cmd = nullptr;
    size_t cmd_len = 0;
    if (text[0] == '\\') {
        cmd = text + 1;
        const char* end = cmd;
        while (*end && *end != '{' && *end != ' ') end++;
        cmd_len = end - cmd;
    }

    // Build base content
    MathASTNode* base = nullptr;
    if (ts_node_named_child_count(node) > 0) {
        base = build_ts_node(ts_node_named_child(node, 0));
    }

    // Look up accent in table
    int32_t accent_char = '^';
    bool is_wide = false;

    for (const AccentEntry* a = ACCENT_TABLE; a->command; a++) {
        if (strlen(a->command) == cmd_len &&
            strncmp(a->command, cmd, cmd_len) == 0) {
            accent_char = a->accent_codepoint;
            is_wide = a->is_wide;
            break;
        }
    }

    MathASTNode* node = make_math_accent(arena, accent_char,
                                          arena_copy_str(cmd, cmd_len),
                                          base);
    if (is_wide) {
        node->flags |= MathASTNode::FLAG_LARGE;  // Reuse flag for wide
    }

    return node;
}
```

**Enhanced JSON output for ACCENT**:

```cpp
case MathNodeType::ACCENT:
    // Output accent character for MathML compatibility
    ::strbuf_append_str(out, ",\"accentChar\":");
    ::strbuf_append_int(out, node->accent.accent_char);
    if (node->accent.command) {
        ::strbuf_append_str(out, ",\"command\":");
        json_escape_string(node->accent.command, out);
    }
    if (node->flags & MathASTNode::FLAG_LARGE) {
        ::strbuf_append_str(out, ",\"wide\":true");
    }
    break;
```

### 3.4 Big Operator Display Mode Fix

**File**: `tex_math_ast_typeset.cpp`

**Current issue**: `FLAG_LIMITS` is set but display context not checked

**Enhancement**:

```cpp
// In typeset_scripts_node()
static TexNode* typeset_scripts_node(MathASTNode* node, MathContext& ctx) {
    MathASTNode* nucleus = node->body;

    // Check if nucleus is a big operator with limits
    bool use_limits = false;
    if (nucleus && nucleus->type == MathNodeType::OP) {
        // Use limits in display mode, unless explicitly disabled
        bool in_display = (ctx.style == MathStyle::Display ||
                          ctx.style == MathStyle::DisplayPrime);
        bool has_limits_flag = (nucleus->flags & MathASTNode::FLAG_LIMITS);
        bool has_nolimits = (nucleus->flags & MathASTNode::FLAG_NOLIMITS);

        // Operators like \sum, \prod default to limits in display
        // Operators like \int default to scripts even in display
        use_limits = in_display && has_limits_flag && !has_nolimits;
    }

    if (use_limits) {
        return typeset_limits(node, ctx);  // Above/below placement
    } else {
        return typeset_scripts_inline(node, ctx);  // Side scripts
    }
}
```

---

## 4. Implementation Roadmap

### Phase 1: Quick Wins (Week 1)
**Target**: +15-20% overall score

| Task | Impact | Effort |
|------|--------|--------|
| Add accent character to JSON output | +5% | Low |
| Add MathML-compatible spacing widths | +5% | Low |
| Implement `\binom` construct | +3% | Medium |
| Fix `\choose` handling | +2% | Medium |

### Phase 2: Core Fixes (Week 2)
**Target**: +10-15% additional

| Task | Impact | Effort |
|------|--------|--------|
| Create `math_ast_to_mathml_json()` | +8% | Medium |
| Enhance negation output | +3% | Medium |
| Complete accent table | +3% | Low |
| Fix wide accent handling | +2% | Low |

### Phase 3: Polish (Week 3)
**Target**: +5-10% additional

| Task | Impact | Effort |
|------|--------|--------|
| Big operator display mode fix | +3% | Medium |
| Delimiter matching edge cases | +2% | Medium |
| Missing Greek variants | +2% | Low |
| Update test comparators | +3% | Medium |

---

## 5. File Modification Summary

| File | Changes |
|------|---------|
| `lambda/tex/tex_math_ast.hpp` | Add `negation`, `mathml_width` fields; accent flags |
| `lambda/tex/tex_math_ast_builder.cpp` | Implement `build_binom`, `build_choose`; enhance `build_accent`, `build_space_command`; add accent/spacing tables |
| `lambda/tex/tex_math_ast_builder.cpp` (JSON) | Enhance `math_ast_to_json_impl()` for ACCENT, SPACE, NOT; add `math_ast_to_mathml_json()` |
| `lambda/tex/tex_math_ast_typeset.cpp` | Fix limits placement in display mode |
| `lambda/tree-sitter-latex-math/grammar.js` | Add `binom`, `choose` rules if needed |
| `test/latex/comparators/mathml_comparator.js` | Improve Lambda→MathML structure mapping |
| `test/latex/comparators/ast_comparator.js` | Add branch name normalization (above↔numer) |

---

## 6. Success Metrics

| Metric | Current | Target |
|--------|---------|--------|
| **Overall Weighted Score** | 40.9% | 75%+ |
| **Baseline Tests Passing** | 0/8 | 8/8 |
| **Accents** | 22.9% | 80%+ |
| **Spacing** | 26.0% | 80%+ |
| **Choose/Binom** | 27.6% | 90%+ |
| **Negation** | 33.4% | 80%+ |
| **Big Operators** | 38.6% | 70%+ |

---

## 7. Testing Strategy

### Unit Tests
Add to `test/test_tex_math_layout_gtest.cpp`:

```cpp
TEST(MathAST, BinomalCoefficient) {
    // Test \binom{n}{k} produces correct AST
}

TEST(MathAST, AccentWithCharacter) {
    // Test \hat{a} includes accent character in JSON
}

TEST(MathAST, SpacingMathML) {
    // Test \quad produces mathml_width="1em"
}
```

### Integration Tests
Run after each phase:
```bash
make test-math-group group=accents    # Phase 1 validation
make test-math-group group=spacing    # Phase 1 validation
make test-math-group group=choose     # Phase 1 validation
make test-math                        # Full regression
```

---

## 8. References

- **MathLive Atom System**: https://cortexjs.io/docs/mathlive/
- **MathML Core Spec**: https://w3c.github.io/mathml-core/
- **TeXBook Chapter 17-18**: Math typesetting rules
- **Current Implementation**: `lambda/tex/tex_math_ast.hpp`, `tex_math_ast_builder.cpp`
- **Test Framework**: `test/latex/test_math_comparison.js`
