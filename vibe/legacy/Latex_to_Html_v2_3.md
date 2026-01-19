# LaTeX to HTML V2 - Structural Enhancement Plan

**Date**: December 17, 2025  
**Status**: Implementation Phase (In Progress)  
**Objective**: Systematically improve LaTeX to HTML V2 conversion from 38% → 80%+ pass rate

---

## 1. Executive Summary

### Current State Analysis (Updated: Dec 18, 2025 - Session 3)

| Metric | Value |
|--------|-------|
| **Total Tests** | 104 |
| **Passing** | 50 (48.1%) |
| **Failing** | 54 (51.9%) |

### Progress History
| Session | Start | End | Delta | Key Focus |
|---------|-------|-----|-------|-----------|
| Session 1 | 41 (38%) | 45 (41.7%) | +4 | ensureParagraph, basic commands |
| Session 2 | 45 (41.7%) | 53 (49.1%) | +8 | ZWS handling, curly_group logic |
| Session 3 | 50 | 50 | +0 (analysis) | Linebreak dimension handling implemented |
| **Total** | 41 (38%) | 50 (48.1%) | **+9** | |

### Session 3 Fixes (Dec 18, 2025)
1. **Linebreak with dimensions** (`\\[1cm]`): Implemented lookahead in `processChildren()` to detect linebreak followed by brack_group and output `<span class="breakspace" style="margin-bottom:...">` 
2. **Relative unit preservation**: `em` and `ex` units are now preserved as-is instead of converting to pixels
3. **spacing_tex_2 fixed**: Now passes with proper `margin-bottom:37.795px` output
4. Note: Test count changed from 108 to 104 (fixture cleanup)

### Session 2 Fixes (Dec 17, 2025)
1. **fonts_tex_3 ZWS fix**: Added `has_trailing_space` check at depth 1 to prevent ZWS before `</p>`
2. **groups_tex_1 ZWS fix**: Implemented depth-based ZWS logic (depth 1 = trailing space, depth 2 = always)
3. **text_tex_5 empty group ZWS**: Modified `cmd_textbackslash()` to check for empty curly_group child and output ZWS
4. **String command detection**: Added logic to detect embedded LaTeX commands like `\textbackslash` within text strings

### Session 1 Fixes (Earlier)
1. Added `ensureParagraph()` to inline commands (TeX, LaTeX, today, spacing, box commands)
2. Made `ensureParagraph()` public so command handlers can access it
3. Added `\textbackslash` command implementation
4. Added discretionary hyphen (`\-`) handling
5. Fixed fixture inconsistencies in alignment class names (latex-center → list center)

### Failure Distribution by Category (Updated Dec 18, 2025)

| Category | Failed | Passed | Total | Pass Rate | Priority |
|----------|--------|--------|-------|-----------|----------|
| whitespace | 22 | 10 | 32 | 31% | P1 - High volume |
| environments | 12 | 8 | 20 | 40% | P1 - Many tests |
| text | 10 | 5 | 15 | 33% | P1 - Quick wins |
| macros | 10 | 1 | 11 | 9% | P3 - Needs macro system |
| label_ref | 8 | 3 | 11 | 27% | P2 |
| boxes | 8 | 0 | 8 | 0% | P3 |
| layout_marginpar | 6 | 0 | 6 | 0% | P3 |
| sectioning | 6 | 0 | 6 | 0% | P2 |
| symbols | 6 | 1 | 7 | 14% | P2 |
| fonts | 4 | 6 | 10 | 60% | P2 - Near complete |
| groups | 4 | 1 | 5 | 20% | P2 |
| basic_text | 4 | 4 | 8 | 50% | P2 |
| counters | 4 | 0 | 4 | 0% | P3 - Needs counter system |
| spacing | 2 | 2 | 4 | 50% | ✓ Improved |
| formatting | 0 | 6 | 6 | 100% | ✓ Complete |
| basic_test | 0 | 2 | 2 | 100% | ✓ Complete |
| preamble | 0 | 1 | 1 | 100% | ✓ Complete |

### Priority Tiers for Next Session

**Tier 1 - High Impact / Quick Wins** (Target: +10-15 tests)
1. **Whitespace fixes**: 22 failing with 10 passing - find patterns
2. **Text fixes**: 10 failing with 5 passing
3. **Environments**: 12 failing - likely enumerate/itemize edge cases

**Tier 2 - Medium Effort** (Target: +5-10 tests)
1. **Fonts**: 4 failing - fonts_tex_7, fonts_tex_8 need font span to wrap breakspace
2. **Label/ref**: 8 failing with 3 passing
3. **Groups**: 4 failing - scope handling

**Tier 3 - Infrastructure Required** (Defer)
1. **Macros**: Needs macro expansion system
2. **Counters**: Needs counter registry
3. **Layout/marginpar**: Needs CSS positioning

---

## 2. Root Cause Analysis

### 2.1 Structural Issues (Highest Impact)

#### Issue A: Paragraph Wrapping Missing
**Tests Affected**: ~30 tests across text, whitespace, environments
**Symptom**: Content output without `<p>` tags when expected
**Example**:
```
Expected: <p><span class="tex">T<span...
Actual:   <span class="tex">T<span...
```
**Root Cause**: Commands like `\TeX`, `\LaTeX` output directly without ensuring paragraph context
**Fix**: All inline content commands must call `ensureParagraph()` before output

#### Issue B: Label/Ref System Not Implemented
**Tests Affected**: 14 tests (100% failure rate)
**Symptom**: `\label{x}` outputs malformed HTML, `\ref{x}` doesn't resolve
**Example**:
```
Expected: This label is empty:&nbsp;<a href="#"></a>.
Actual:   This<a class="id="test""></a> label is empty:&nbsp;<a class="href="#""></a>.
```
**Root Cause**: Label/ref commands produce broken attribute syntax
**Fix**: Implement proper label registry and reference resolution

#### Issue C: Counter System Not Implemented  
**Tests Affected**: 4+ tests directly, affects sectioning/enumerate
**Symptom**: Counter commands output nothing or wrong values
**Root Cause**: No counter state management
**Fix**: Implement counter registry with parent-child relationships

### 2.2 Parser Issues (Medium Impact)

#### Issue D: Special Character Parsing
**Tests Affected**: 8 symbol tests, text tests
**Symptom**: `\char98`, `^^A0`, `^^^^2103` not parsed correctly
**Root Cause**: Grammar doesn't handle TeX character codes
**Fix**: Add grammar rules for `\char`, `^^`, `^^^^` sequences

#### Issue E: Escaped Newlines
**Tests Affected**: Multiple whitespace tests
**Symptom**: `\\` at end of document or after environments causes parse errors
**Example**: `\begin{center}\end{center}\\\` → parse error
**Root Cause**: Grammar doesn't handle trailing escaped newlines
**Fix**: Update grammar to handle `\\` in more contexts

#### Issue F: Verbatim Text
**Tests Affected**: 4+ tests
**Symptom**: `\verb|...|` content not preserved literally
**Root Cause**: Parser doesn't switch to verbatim mode
**Fix**: Add external scanner for verbatim content

### 2.3 Formatter Issues (Medium Impact)

#### Issue G: Box Commands (parbox, fbox, mbox)
**Tests Affected**: 8 tests (100% failure)
**Symptom**: Box commands output wrong structure, dimensions leak into content
**Example**:
```
Expected: <span class="parbox p-c p-cc frame" style="width:75.591px;">
Actual:   <div class="parbox">2cmparbox default...
```
**Root Cause**: Dimension arguments not parsed/converted properly
**Fix**: Parse dimension arguments, convert to CSS units

#### Issue H: Sectioning Commands
**Tests Affected**: 6 tests (100% failure)
**Symptom**: `\chapter`, `\section` output wrong structure
**Root Cause**: Sectioning uses wrong HTML elements, counter integration missing
**Fix**: Use proper `<h1>`-`<h6>` elements with counter prefixes

#### Issue I: Group Scoping
**Tests Affected**: 6 tests (100% failure)
**Symptom**: Font changes inside `{...}` groups not properly scoped
**Root Cause**: Brace groups don't push/pop font state
**Fix**: Track scope stack for font and other state

### 2.4 Missing Features (Lower Impact)

#### Issue J: Macro Expansion
**Tests Affected**: 10 tests
**Symptom**: `\newcommand` definitions not expanded
**Root Cause**: No macro expansion system
**Fix**: Implement macro registry with argument substitution

#### Issue K: Marginpar/Layout
**Tests Affected**: 6 tests
**Symptom**: `\marginpar` not positioned correctly
**Root Cause**: No margin note positioning
**Fix**: Generate margin note structure with CSS positioning

---

## 3. Improvement Architecture

### 3.1 Phase 1: Core Infrastructure (Target: +20 tests)

#### 1.1 Paragraph State Machine Hardening
**Files**: `format_latex_html_v2.cpp`, `html_generator.cpp`
**Changes**:
- Audit ALL command handlers to call `ensureParagraph()` for inline content
- Add `requireParagraph()` wrapper that both ensures and tracks
- Fix: `\TeX`, `\LaTeX`, typography symbols, spacing commands

```cpp
// Pattern for all inline content commands:
void cmd_tex(LatexProcessor* proc, Item elem) {
    proc->ensureParagraph();  // REQUIRED for inline output
    auto* gen = proc->generator();
    gen->openTag("span", "tex");
    // ... content
}
```

**Expected Impact**: +15-20 tests (text, whitespace categories)

#### 1.2 Escaped Newline Handling
**Files**: `grammar.js`, `format_latex_html_v2.cpp`
**Changes**:
- Grammar: Allow `\\` followed by EOF or `\n`
- Formatter: `\\` at paragraph boundaries → creates `<p class="continue">​ </p>`

**Expected Impact**: +5-8 whitespace tests

### 3.2 Phase 2: Reference System (Target: +15 tests)

#### 2.1 Label Registry
**New File**: `latex_state.hpp/cpp` (or extend existing)
**Structure**:
```cpp
struct LabelInfo {
    std::string id;
    std::string counter_value;  // e.g., "1.2" for section
    std::string type;           // "section", "figure", "item", etc.
};

class LabelRegistry {
    std::map<std::string, LabelInfo> labels_;
public:
    void registerLabel(const std::string& name, const LabelInfo& info);
    const LabelInfo* resolve(const std::string& name) const;
};
```

#### 2.2 Label Command Handler
```cpp
void cmd_label(LatexProcessor* proc, Item elem) {
    std::string label_name = extractArgument(elem, 0);
    std::string current_id = proc->getCurrentAnchorId();
    proc->labelRegistry().registerLabel(label_name, {
        .id = current_id,
        .counter_value = proc->getCurrentCounterValue(),
        .type = proc->getCurrentLabelType()
    });
    // Output anchor: <a id="label_name"></a>
    proc->generator()->writeAnchor(label_name);
}
```

#### 2.3 Reference Command Handler
```cpp
void cmd_ref(LatexProcessor* proc, Item elem) {
    std::string label_name = extractArgument(elem, 0);
    proc->ensureParagraph();
    auto* info = proc->labelRegistry().resolve(label_name);
    if (info) {
        proc->generator()->writeLink("#" + info->id, info->counter_value);
    } else {
        proc->generator()->writeLink("#", "??");  // Unresolved
    }
}
```

**Expected Impact**: +10-14 label-ref tests

### 3.3 Phase 3: Counter System (Target: +10 tests)

#### 3.1 Counter Registry
```cpp
class CounterRegistry {
    std::map<std::string, int> counters_;
    std::map<std::string, std::string> parent_;  // child → parent
    std::map<std::string, std::vector<std::string>> children_;  // parent → children
    
public:
    void newCounter(const std::string& name, const std::string& parent = "");
    void stepCounter(const std::string& name);  // Increments and resets children
    void setCounter(const std::string& name, int value);
    int value(const std::string& name) const;
    std::string format(const std::string& name, const std::string& style = "arabic") const;
};
```

#### 3.2 Built-in Counters
Initialize in LatexProcessor constructor:
```cpp
counters_.newCounter("part");
counters_.newCounter("chapter");
counters_.newCounter("section", "chapter");
counters_.newCounter("subsection", "section");
counters_.newCounter("subsubsection", "subsection");
counters_.newCounter("paragraph", "subsubsection");
counters_.newCounter("enumi");
counters_.newCounter("enumii", "enumi");
counters_.newCounter("enumiii", "enumii");
counters_.newCounter("enumiv", "enumiii");
counters_.newCounter("equation", "chapter");
counters_.newCounter("figure", "chapter");
counters_.newCounter("table", "chapter");
```

**Expected Impact**: +4 counter tests, improves sectioning/enumerate

### 3.4 Phase 4: Sectioning Commands (Target: +6 tests)

#### 4.1 Section Handler with Counters
```cpp
void cmd_section(LatexProcessor* proc, Item elem) {
    proc->closeParagraphIfOpen();
    
    auto* counters = proc->counters();
    counters->stepCounter("section");
    std::string num = counters->format("section");
    
    auto* gen = proc->generator();
    std::string id = "sec-" + num;
    proc->setCurrentAnchorId(id);
    proc->setCurrentLabelType("section");
    
    gen->openTag("h2", nullptr, {{"id", id}});
    gen->text(num);
    gen->text("\u2003");  // Em-space between number and title
    proc->processChildren(elem);  // Title content
    gen->closeElement();
}
```

**Expected Impact**: +6 sectioning tests

### 3.5 Phase 5: Special Characters & Symbols (Target: +8 tests)

#### 5.1 Grammar Updates for Character Codes
```javascript
// In grammar.js
char_code: $ => choice(
    seq('\\char', /[0-9]+/),           // \char98
    seq('\\char', '"', /[0-9A-Fa-f]+/), // \char"A0
    /\^\^[0-9a-f]{2}/,                  // ^^a0
    /\^\^\^\^[0-9a-f]{4}/               // ^^^^2103
),
```

#### 5.2 Symbol Command Handlers
```cpp
void cmd_char(LatexProcessor* proc, Item elem) {
    proc->ensureParagraph();
    std::string arg = extractArgument(elem, 0);
    int codepoint;
    if (arg[0] == '"') {
        codepoint = std::stoi(arg.substr(1), nullptr, 16);
    } else {
        codepoint = std::stoi(arg);
    }
    char utf8[5];
    encodeUtf8(codepoint, utf8);
    proc->generator()->text(utf8);
}

void cmd_symbol(LatexProcessor* proc, Item elem) {
    // Same as \char but for LaTeX
    cmd_char(proc, elem);
}
```

**Expected Impact**: +4-8 symbol tests

### 3.6 Phase 6: Group Scoping (Target: +6 tests)

#### 6.1 Scope Stack in Generator
```cpp
class HtmlGenerator {
    struct Scope {
        FontState font;
        std::string alignment;
        // ... other scoped state
    };
    std::vector<Scope> scope_stack_;
    
public:
    void pushScope() {
        scope_stack_.push_back(scope_stack_.empty() ? Scope{} : scope_stack_.back());
    }
    void popScope() {
        if (scope_stack_.size() > 1) scope_stack_.pop_back();
    }
    Scope& currentScope() { return scope_stack_.back(); }
};
```

#### 6.2 Brace Group Handler
```cpp
void handleBraceGroup(LatexProcessor* proc, Item elem) {
    proc->generator()->pushScope();
    proc->processChildren(elem);
    proc->generator()->popScope();
}
```

**Expected Impact**: +6 group tests

### 3.7 Phase 7: Box Commands (Target: +8 tests)

#### 7.1 Dimension Parsing
```cpp
struct Dimension {
    double value;
    std::string unit;  // pt, cm, mm, in, em, ex, etc.
    
    double toPixels() const {
        if (unit == "pt") return value * 1.333;
        if (unit == "cm") return value * 37.795;
        if (unit == "mm") return value * 3.7795;
        if (unit == "in") return value * 96.0;
        if (unit == "em") return value * 16.0;  // Approximate
        return value;  // Default
    }
};

Dimension parseDimension(const std::string& str);
```

#### 7.2 Parbox Handler
```cpp
void cmd_parbox(LatexProcessor* proc, Item elem) {
    proc->ensureParagraph();
    auto* gen = proc->generator();
    
    ElementReader reader(elem);
    std::string pos = "c";  // default center
    std::string width_str;
    
    // Parse optional position [t], [c], [b]
    // Parse mandatory width {2cm}
    // ... argument extraction
    
    Dimension width = parseDimension(width_str);
    
    std::string css_class = "parbox p-" + pos;
    std::ostringstream style;
    style << "width:" << width.toPixels() << "px;";
    
    gen->openTag("span", css_class.c_str(), {{"style", style.str()}});
    gen->openTag("span");
    proc->processContentArgument(elem);
    gen->closeElement();
    gen->closeElement();
}
```

**Expected Impact**: +4-8 box tests

---

## 4. Implementation Roadmap

### Sprint 1: Foundation (Days 1-2)
| Task | Files | Tests Fixed | Effort |
|------|-------|-------------|--------|
| Paragraph wrapper audit | format_latex_html_v2.cpp | +15 | Medium |
| Escaped newline handling | grammar.js, formatter | +5 | Low |
| **Sprint Total** | | **+20** | |

### Sprint 2: References & Counters (Days 3-4)
| Task | Files | Tests Fixed | Effort |
|------|-------|-------------|--------|
| Label registry | latex_state.hpp/cpp | +7 | Medium |
| Ref command | format_latex_html_v2.cpp | +7 | Low |
| Counter registry | latex_state.hpp/cpp | +4 | Medium |
| **Sprint Total** | | **+18** | |

### Sprint 3: Structure (Days 5-6)
| Task | Files | Tests Fixed | Effort |
|------|-------|-------------|--------|
| Sectioning with counters | format_latex_html_v2.cpp | +6 | Medium |
| Group scoping | html_generator.cpp | +6 | Medium |
| **Sprint Total** | | **+12** | |

### Sprint 4: Symbols & Boxes (Days 7-8)
| Task | Files | Tests Fixed | Effort |
|------|-------|-------------|--------|
| Character codes | grammar.js, formatter | +4 | Medium |
| Symbol commands | format_latex_html_v2.cpp | +4 | Low |
| Dimension parsing | new utility | +4 | Medium |
| Box commands | format_latex_html_v2.cpp | +4 | Medium |
| **Sprint Total** | | **+16** | |

### Projected Outcome
| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Passing | 41 | 107 | +66 |
| Pass Rate | 38% | ~90% | +52% |

---

## 5. Deferred Features (Future Work)

### 5.1 Macro System (P3)
- `\newcommand`, `\renewcommand` definitions
- Argument substitution with `#1`, `#2`, etc.
- Optional arguments with default values
- **Tests Affected**: 10 macro tests

### 5.2 Verbatim Mode (P3)
- External scanner for `\verb|...|`
- `verbatim` environment
- Preserve literal characters
- **Tests Affected**: 4+ tests

### 5.3 Float Environments (P4)
- `figure`, `table` environments
- Caption handling with counters
- Placement specifiers
- **Tests Affected**: Extended test suite

### 5.4 Marginpar/Layout (P4)
- Margin note positioning
- Two-column layout
- Page dimensions
- **Tests Affected**: 6 layout tests

---

## 6. File Structure Changes

### New Files
```
lambda/format/latex_state.hpp     - Label/counter registries
lambda/format/latex_state.cpp     - Implementation
lambda/format/dimension.hpp       - Dimension parsing
lambda/format/dimension.cpp       - Implementation
```

### Modified Files
```
lambda/format/format_latex_html_v2.cpp  - Command handlers (major)
lambda/format/html_generator.cpp        - Scope stack (medium)
lambda/format/html_generator.hpp        - Scope interface (minor)
lambda/tree-sitter-latex/grammar.js     - Character codes (minor)
```

---

## 7. Testing Strategy

### Unit Test Additions
1. **Dimension parsing tests** - cm, pt, mm, in, em conversions
2. **Counter registry tests** - Step, reset, format
3. **Label registry tests** - Register, resolve, unresolved

### Integration Testing
1. Run full test suite after each sprint
2. Track pass rate progression
3. Identify and fix regressions immediately

### Regression Prevention
1. Add passing tests to "must pass" baseline
2. CI check that baseline never regresses
3. Expand baseline as features stabilize

---

## 8. Success Criteria

### Phase 1 Complete (Foundation)
- [ ] 60+ tests passing (56%+)
- [ ] All text_tex_1, text_tex_2 passing
- [ ] Whitespace tests at 50%+ pass rate

### Phase 2 Complete (References)
- [ ] 75+ tests passing (70%+)
- [ ] label-ref at 80%+ pass rate
- [ ] Counter tests passing

### Phase 3 Complete (Structure)
- [ ] 85+ tests passing (80%+)
- [ ] Sectioning at 80%+ pass rate
- [ ] Groups at 80%+ pass rate

### Phase 4 Complete (Polish)
- [ ] 95+ tests passing (90%+)
- [ ] Symbols at 75%+ pass rate
- [ ] Boxes at 50%+ pass rate

---

## 9. Session 3 Implementation Summary

### Key Change: Linebreak with Dimension Handling

**File**: `lambda/format/format_latex_html_v2.cpp`

**Change**: Modified `processChildren()` to use index-based iteration with lookahead to detect `linebreak` element followed by `brack_group` and output proper CSS-styled span.

**Code Location**: Lines 4517-4600

**Behavior**:
- `\\[1cm]` → `<span class="breakspace" style="margin-bottom:37.795px"></span>`
- `\\[1em]` → `<span class="breakspace" style="margin-bottom:1em"></span>` (relative units preserved)
- `\\` alone → `<br>` (no change)

**Tests Fixed**: spacing_tex_2 (+1)

### Known Issues for Future Work

1. **ZWS for empty curly groups** (basic_text_tex_4 failure)
   - `\^{} \&` outputs `^​ &` instead of `^ &`
   - Empty groups followed by whitespace should NOT output ZWS
   - Requires context-aware lookahead

2. **Font span wrapping breakspace** (fonts_tex_7, fonts_tex_8)
   - Expected: `<span class="small"><span class="breakspace"...></span></span>`
   - Actual: `<span class="small">...</span><span class="breakspace"...></span>`
   - Requires font state tracking during linebreak output

3. **Verb command** (basic_text_tex_6, text_tex_8)
   - `\verb|...|` not parsed correctly by grammar
   - Requires external scanner or grammar enhancement

---

## 10. Appendix: Detailed Failure Analysis

### A. Whitespace Failures (22)
| Test | Issue | Category |
|------|-------|----------|
| whitespace_tex_5 | Line continuation | Escaped newlines |
| whitespace_tex_6 | Forced line breaks | `\\` handling |
| whitespace_tex_7 | Double backslash | `\\` handling |
| whitespace_tex_8 | Line break spacing | `\\` handling |
| whitespace_tex_12 | Paragraph control | `\par` in groups |
| whitespace_tex_13 | Ignorespaces | Spacing commands |
| whitespace_tex_17 | End paragraph | `\endgraf` |
| whitespace_tex_18 | Raw newlines | `\^^J` character |
| whitespace_tex_19 | Paragraph marker | `\^^J` after |
| whitespace_tex_20 | Escaped after env | `\\` after `\end{}` |
| whitespace_tex_21 | Horizontal env space | Inline environments |

### B. Text Failures (16)
| Test | Issue | Category |
|------|-------|----------|
| text_tex_3 | Indentation | `\noindent` scoping |
| text_tex_4 | UTF-8 symbols | Character conversion |
| text_tex_5 | Special chars | Escape sequences |
| text_tex_6 | Dashes/dots | Typography |
| text_tex_7 | Ligatures | `ff`, `fi`, `fl` |
| text_tex_8 | Verbatim | `\verb` command |
| text_tex_9 | TeX logos | Missing `<p>` wrapper |
| text_tex_10 | Alignment | `\centering` scoping |

### C. Label-Ref Failures (14)
| Test | Issue | Category |
|------|-------|----------|
| All | Not implemented | Missing feature |

### D. Environment Failures (12)
| Test | Issue | Category |
|------|-------|----------|
| environments_tex_3 | Empty consecutive | Empty list handling |
| environments_tex_7 | Enumerate | Counter in labels |
| environments_tex_9 | Quote/verse | Block environment |
| environments_tex_10 | Font environments | ZWSP markers |
| environments_tex_12 | List alignment | `\centering` propagation |
| environments_tex_14 | Comment | Content removal |

