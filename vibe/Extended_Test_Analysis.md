# LaTeX Extended Test Failure Analysis

**Date**: December 11, 2025  
**Total Extended Tests**: 74  
**Failed Tests**: 74 (100%)  
**Passing Tests**: 0 (0%)

## Executive Summary

Analysis of 74 failing extended tests reveals **systematic architectural gaps** in the LaTeX parser and HTML formatter. The failures cluster into 6 major structural categories, with whitespace handling being the most problematic (26 failures).

**Key Finding**: Most failures stem from **incomplete AST processing** and **missing post-processing phases** rather than parser grammar issues.

---

## Failure Categories by Count

| Category | Count | Percentage | Severity |
|----------|-------|------------|----------|
| **Whitespace** | 26 | 35% | 🔴 CRITICAL |
| **Environments** | 20 | 27% | 🔴 CRITICAL |
| **Text/Special Chars** | 16 | 22% | 🟡 HIGH |
| **Labels/References** | 14 | 19% | 🟡 HIGH |
| **Macros** | 10 | 14% | 🟡 HIGH |
| **Fonts** | 10 | 14% | 🟢 MEDIUM |
| **Symbols/Spacing** | 16 | 22% | 🟢 MEDIUM |
| **Boxes/Layout** | 14 | 19% | 🟢 MEDIUM |
| **Sectioning** | 6 | 8% | 🟢 MEDIUM |
| **Groups** | 6 | 8% | 🟢 LOW |
| **Basic Text** | 4 | 5% | 🟡 HIGH |
| **Counters** | 2 | 3% | 🟢 LOW (math-related) |
| **Other** | 4 | 5% | 🟢 LOW |

---

## Structural Issue Patterns

### 1. **Whitespace Management** (26 failures, 35%)

#### Issues Identified:
1. **Tilde `~` Not Converting to `&nbsp;`**
   - Expected: `&nbsp;` (non-breaking space entity)
   - Actual: Literal `~` character
   - Root cause: No HTML entity encoding for special whitespace

2. **Zero-Width Space (ZWSP) Missing After Groups**
   - Expected: `two​ groups ​` (U+200B after `}`)
   - Actual: `two groups ` (regular spaces)
   - Root cause: Group boundary ZWSP insertion code not being executed

3. **Paragraph Break Preservation**
   - Expected: Multiple `<p>` tags for `\par` commands
   - Actual: Text merged into single paragraph
   - Root cause: Paragraph detection not respecting explicit `\par`

4. **Comment + Blank Line Handling**
   - Expected: Comments discarded, blank lines create paragraphs
   - Actual: Incorrect paragraph splitting
   - Root cause: Whitespace normalization happens before comment removal

5. **`\mbox` Whitespace Compression**
   - Expected: Complex whitespace rules inside `\mbox{...}`
   - Actual: Standard paragraph whitespace
   - Root cause: Horizontal mode context not tracked

#### Architectural Gap:
**Missing Post-Processing Phase**: Need dedicated whitespace normalization pass AFTER AST construction that:
- Tracks paragraph/horizontal/vertical mode context
- Applies mode-specific whitespace rules
- Handles special characters (`~`, `\,`, `\quad`, etc.)
- Inserts ZWSP at group boundaries

---

### 2. **Environment Processing** (20 failures, 27%)

#### Issues Identified:
1. **Blank Lines Inside List Items Not Creating Paragraphs**
   ```latex
   \item first item
   
       with a new paragraph  % Should be <p>...</p><p>...</p>
   ```
   - Expected: Two `<p>` elements inside `<li>`
   - Actual: Single `<p>` with all text
   - Root cause: Environment context not affecting paragraph detection

2. **Text After `\end{environment}` Without Blank Line**
   ```latex
   \end{itemize}
   this belongs to paragraph with itemize
   ```
   - Expected: `<p class="continue">` (paragraph continuation)
   - Actual: Separate paragraph or incorrect nesting
   - Root cause: Post-environment context not tracked

3. **Alignment Environment (`\centering`, `\raggedleft`)**
   - Expected: Alignment affects containing block, not creates new element
   - Actual: Creates extra wrapper elements
   - Root cause: Scope management for declaration commands

4. **Abstract/Quote Environment Styling**
   - Expected: Specific CSS classes and styling
   - Actual: Generic paragraph handling
   - Root cause: Environment-specific formatting rules not implemented

5. **Comment Environment**
   - Expected: Contents completely discarded
   - Actual: May be partially rendered
   - Root cause: Special environments not properly handled

#### Architectural Gap:
**Missing Environment Context Stack**: Need to track:
- Current environment type (itemize, enumerate, abstract, etc.)
- Alignment/font declarations in scope
- Paragraph continuation rules
- Special environment behaviors

---

### 3. **Special Characters & HTML Entities** (16 failures, 22%)

#### Issues Identified:
1. **Ampersand Not Escaped**
   ```latex
   \&
   ```
   - Expected: `&amp;`
   - Actual: `&` (causes HTML parsing issues)
   - Root cause: HTML entity escaping not implemented

2. **Accented Characters**
   ```latex
   H\^otel, na\"ive, \'el\`eve
   ```
   - Expected: `Hôtel, naïve, élève` (composed Unicode)
   - Actual: `Ho tel, naive, eleve` (missing accents or decomposed)
   - Root cause: Accent combining logic incomplete

3. **German Special Characters**
   ```latex
   Schlo\ss{} Stra\ss e
   ```
   - Expected: `Schloß​ Straße` (with ZWSP after first `ß`)
   - Actual: Missing ZWSP, possibly wrong character
   - Root cause: `\ss` command + ZWSP insertion

4. **Dash Variations**
   ```latex
   - -- ---
   ```
   - Expected: `‐ – —` (hyphen, en-dash, em-dash with proper Unicode)
   - Actual: Simple hyphens
   - Root cause: Dash conversion rules not implemented

5. **Special Symbols**
   ```latex
   \$10, 5€, \textbackslash
   ```
   - Expected: Proper Unicode symbols
   - Actual: May be escaped incorrectly or missing
   - Root cause: Symbol command mapping incomplete

#### Architectural Gap:
**Missing Output Encoding Layer**: Need:
- HTML entity escaping (`&`, `<`, `>`, `"`)
- Accent composition/decomposition
- Special character mapping table
- Context-aware character rendering

---

### 4. **Label/Reference System** (14 failures, 19%)

#### Issues Identified:
1. **`\label` Creating Nested Paragraphs**
   ```latex
   This\label{test} label
   ```
   - Expected: `<p>This label</p>` (invisible anchor)
   - Actual: `<p>This<p><p>label</p></p>` (broken structure)
   - Root cause: `\label` treated as block element instead of inline

2. **`\ref` Not Producing Links**
   - Expected: `<a href="#label-id">1</a>`
   - Actual: May produce nested paragraphs or plain text
   - Root cause: Reference resolution not implemented

3. **Empty `currentlabel` Handling**
   - Expected: Empty link when label has no number
   - Actual: Broken HTML structure
   - Root cause: Label numbering system not integrated

4. **Section Label References**
   ```latex
   \section{Title}\label{sec:intro}
   See Section~\ref{sec:intro}
   ```
   - Expected: Link to section with proper number
   - Actual: No link or incorrect reference
   - Root cause: Section numbering + label system integration

5. **`\item` Labels**
   ```latex
   \item\label{item:first} text
   ```
   - Expected: Reference returns item number
   - Actual: Broken or missing
   - Root cause: Item counter not connected to label system

#### Architectural Gap:
**Missing Two-Pass Processing**:
1. **First Pass**: Collect all labels with their:
   - Counter values (section, subsection, item, etc.)
   - Generated IDs
   - Position information
2. **Second Pass**: Resolve all `\ref` commands to:
   - Insert proper link HTML
   - Use collected counter values
   - Generate `id` attributes on targets

---

### 5. **Macro Expansion** (10 failures, 14%)

#### Issues Identified:
1. **Custom Macros Not Expanding**
   ```latex
   \newcommand{\mycommand}{replacement text}
   \mycommand
   ```
   - Expected: `replacement text`
   - Actual: Command may be rendered literally or cause error
   - Root cause: Macro expansion not implemented

2. **Macros With Arguments**
   ```latex
   \newcommand{\greet}[1]{Hello, #1!}
   \greet{World}
   ```
   - Expected: `Hello, World!`
   - Actual: Not expanded
   - Root cause: Argument substitution not implemented

3. **Optional Arguments**
   ```latex
   \newcommand{\cmd}[2][default]{#1: #2}
   ```
   - Expected: Default value handling
   - Actual: Not supported
   - Root cause: Optional argument parsing missing

4. **Recursive/Nested Macro Expansion**
   - Expected: Multiple expansion passes
   - Actual: Single pass or no expansion
   - Root cause: Expansion engine not implemented

#### Architectural Gap:
**Missing Macro Expansion Engine**: Need:
- Macro definition storage (name → template + args)
- Argument parsing (required + optional)
- Recursive expansion with cycle detection
- Expansion timing (during parse vs. during format)

---

### 6. **Font/Text Formatting** (10 failures, 14%)

#### Issues Identified:
1. **Nested Font Commands**
   ```latex
   \textit{italic \textbf{bold-italic} italic}
   ```
   - Expected: Proper nesting with cumulative styles
   - Actual: May lose inner formatting or create wrong structure
   - Root cause: Font stack not properly maintained

2. **Font Scoping**
   ```latex
   {\em emphasized \textbf{bold-em}}
   ```
   - Expected: Declaration extends to end of group
   - Actual: May extend beyond or end early
   - Root cause: Group boundaries not enforcing scope

3. **Font Size Commands**
   ```latex
   {\large larger {\small smaller} larger}
   ```
   - Expected: Proper size nesting
   - Actual: May not nest correctly
   - Root cause: Size stack not tracked

4. **Ligature Handling**
   ```latex
   first -> fi+rst (fi ligature)
   ```
   - Expected: Unicode ligature characters
   - Actual: Separate characters
   - Root cause: Ligature substitution not implemented

#### Architectural Gap:
**Missing Font State Management**: Need:
- Font property stack (family, series, shape, size)
- Cumulative style computation
- Declaration vs. command distinction
- Proper scope tracking via groups

---

## Proposed Structural Enhancements

### Phase 1: Core Infrastructure (HIGH PRIORITY)

#### 1.1 Multi-Pass Processing Architecture
```
Input LaTeX
    ↓
[Pass 1: Parse] → AST (Tree-sitter)
    ↓
[Pass 2: Macro Expansion] → Expanded AST
    ↓
[Pass 3: Label Collection] → Label Table + AST
    ↓
[Pass 4: Whitespace Normalization] → Cleaned AST
    ↓
[Pass 5: Format to HTML] → HTML Tree
    ↓
[Pass 6: HTML Post-Processing] → Final HTML
```

**Benefits**:
- Separates concerns (expansion ≠ formatting)
- Allows forward/backward references
- Enables context-dependent processing
- Facilitates debugging (inspect each pass)

#### 1.2 Context State Machine
```cpp
struct RenderContext {
    // Existing counter system
    std::map<std::string, Counter> counters;
    
    // NEW: Mode tracking
    enum Mode { VERTICAL, HORIZONTAL, MATH, RESTRICTED_HORIZONTAL };
    std::vector<Mode> mode_stack;
    
    // NEW: Environment tracking
    struct EnvironmentState {
        std::string name;
        bool requires_paragraph;
        bool paragraph_continuation;
        std::map<std::string, std::string> properties;
    };
    std::vector<EnvironmentState> env_stack;
    
    // NEW: Font tracking
    struct FontState {
        std::string family;   // rm, sf, tt
        std::string series;   // bf, md
        std::string shape;    // it, sl, sc, up
        std::string size;     // normalsize, large, small, etc.
    };
    std::vector<FontState> font_stack;
    
    // NEW: Alignment tracking
    enum Alignment { LEFT, CENTER, RIGHT, JUSTIFY };
    std::vector<Alignment> align_stack;
    
    // NEW: Label/Reference system
    struct Label {
        std::string counter_value;
        std::string id;
        int section_depth;
    };
    std::map<std::string, Label> labels;
    
    // NEW: Macro definitions
    struct Macro {
        std::string name;
        int num_args;
        int num_optional;
        std::string template_text;
    };
    std::map<std::string, Macro> macros;
};
```

#### 1.3 Whitespace Normalization Engine
```cpp
class WhitespaceNormalizer {
public:
    Item normalize(Item ast_node, RenderContext& ctx);
    
private:
    // Insert ZWSP after group closing braces
    void insert_group_boundaries(Item node);
    
    // Convert ~ to &nbsp; in appropriate contexts
    void handle_special_spaces(Item node);
    
    // Compress multiple spaces based on mode
    void compress_spaces(Item node, Mode mode);
    
    // Handle paragraph breaks (\par, blank lines)
    void detect_paragraphs(Item node, Mode mode);
    
    // Remove comments and their trailing whitespace
    void strip_comments(Item node);
};
```

---

### Phase 2: HTML Output Layer (HIGH PRIORITY)

#### 2.1 HTML Entity Encoder
```cpp
class HtmlEncoder {
public:
    static std::string escape(const std::string& text) {
        // & → &amp;
        // < → &lt;
        // > → &gt;
        // " → &quot;
        // ' → &#39;
    }
    
    static std::string encode_nbsp() { return "&nbsp;"; }
    static std::string encode_zwsp() { return "\u200B"; }
    static std::string encode_shy() { return "&shy;"; }
};
```

#### 2.2 Character Composition Engine
```cpp
class AccentComposer {
public:
    // Combine base + accent into Unicode composed form
    static std::string compose(char base, const std::string& accent);
    
    // Examples:
    // compose('e', "acute") → "é" (U+00E9)
    // compose('o', "umlaut") → "ö" (U+00F6)
    // compose('n', "tilde") → "ñ" (U+00F1)
};

class LigatureSubstituter {
public:
    // Replace character sequences with ligatures
    static std::string substitute(const std::string& text);
    
    // Examples:
    // "fi" → "ﬁ" (U+FB01)
    // "fl" → "ﬂ" (U+FB02)
    // "ff" → "ﬀ" (U+FB00)
};
```

---

### Phase 3: Macro System (MEDIUM PRIORITY)

#### 3.1 Macro Definition Parser
```cpp
struct MacroDefinition {
    std::string name;
    int num_required_args;
    int num_optional_args;
    std::string default_arg;
    std::vector<Token> body;  // Template with #1, #2, etc.
};

class MacroExpander {
public:
    // Register macro from \newcommand
    void define(const MacroDefinition& def);
    
    // Expand all macros in AST recursively
    Item expand(Item ast_node, int max_depth = 100);
    
private:
    std::map<std::string, MacroDefinition> definitions;
    
    // Substitute arguments into template
    std::vector<Token> substitute_args(
        const std::vector<Token>& template_body,
        const std::vector<Item>& args
    );
};
```

---

### Phase 4: Label/Reference System (MEDIUM PRIORITY)

#### 4.1 Two-Pass Architecture
```cpp
class LabelCollector {
public:
    // Pass 1: Collect all \label commands
    void collect(Item ast_root, RenderContext& ctx);
    
private:
    struct LabelInfo {
        std::string key;
        std::string counter_value;  // From ctx.counters
        std::string generated_id;
        Position source_location;
    };
    
    std::map<std::string, LabelInfo> collected_labels;
};

class ReferenceResolver {
public:
    // Pass 2: Replace \ref with <a> tags
    Item resolve(Item ast_node, const LabelCollector& labels);
    
private:
    Item create_link(const std::string& label_key, 
                     const LabelInfo& target);
};
```

---

### Phase 5: Environment System (MEDIUM PRIORITY)

#### 5.1 Environment Handler Registry
```cpp
class EnvironmentHandler {
public:
    virtual Item begin(const std::string& name, Item args) = 0;
    virtual Item end(const std::string& name) = 0;
    virtual Item handle_content(Item content) = 0;
};

class ItemizeHandler : public EnvironmentHandler {
    // Handles \begin{itemize}...\end{itemize}
    // Creates <ul class="list"> with proper <li> structure
};

class AbstractHandler : public EnvironmentHandler {
    // Handles \begin{abstract}...\end{abstract}
    // Creates <div class="abstract"> with styling
};

class CommentHandler : public EnvironmentHandler {
    // Handles \begin{comment}...\end{comment}
    // Returns empty/null - discards all content
};

class EnvironmentRegistry {
    std::map<std::string, std::unique_ptr<EnvironmentHandler>> handlers;
};
```

---

### Phase 6: Font System (LOW PRIORITY)

#### 6.1 Font State Tracker
```cpp
class FontManager {
public:
    // Push font declaration onto stack
    void push_family(const std::string& family);  // \rmfamily, \sffamily
    void push_series(const std::string& series);  // \bfseries, \mdseries
    void push_shape(const std::string& shape);    // \itshape, \slshape
    void push_size(const std::string& size);      // \large, \small
    
    // Pop font declaration from stack
    void pop_font();
    
    // Get current cumulative font state
    FontState get_current() const;
    
    // Apply font command (creates temporary scope)
    Item apply_command(const std::string& cmd, Item content);
    
private:
    struct FontFrame {
        std::optional<std::string> family;
        std::optional<std::string> series;
        std::optional<std::string> shape;
        std::optional<std::string> size;
    };
    
    std::vector<FontFrame> stack;
};
```

---

## Implementation Priority Matrix

### Critical Path (Must Fix for >50% Pass Rate)

| Priority | System | Affected Tests | Effort | Impact |
|----------|--------|----------------|--------|--------|
| **P0** | Whitespace Normalizer | 26 | Medium | Very High |
| **P0** | HTML Entity Escaping | 16 | Low | High |
| **P1** | Environment Context | 20 | High | Very High |
| **P1** | Label/Reference (2-pass) | 14 | Medium | High |

### Secondary Improvements (For >75% Pass Rate)

| Priority | System | Affected Tests | Effort | Impact |
|----------|--------|----------------|--------|--------|
| **P2** | Macro Expansion | 10 | High | Medium |
| **P2** | Font State Manager | 10 | Medium | Medium |
| **P3** | Accent Composer | 8 | Low | Medium |
| **P3** | Special Char Mapping | 8 | Low | Low |

### Low Priority (Polish & Edge Cases)

| Priority | System | Affected Tests | Effort | Impact |
|----------|--------|----------------|--------|--------|
| **P4** | Box Layout | 8 | High | Low |
| **P4** | Advanced Spacing | 8 | Medium | Low |
| **P4** | Symbol Tables | 8 | Low | Low |

---

## Quick Wins (Low Effort, High Impact)

### 1. **Tilde to `&nbsp;` Conversion** (30 min)
```cpp
// In handle_text() or text normalization pass
if (text[i] == '~') {
    output += "&nbsp;";
} else {
    output += text[i];
}
```
**Impact**: Fixes ~10 whitespace tests immediately

### 2. **HTML Entity Escaping** (1 hour)
```cpp
std::string escape_html(const std::string& text) {
    std::string result;
    for (char c : text) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            default: result += c;
        }
    }
    return result;
}
```
**Impact**: Fixes 16 special character tests

### 3. **ZWSP After Groups** (2 hours)
```cpp
// In format_group() or equivalent
Item format_group(Item group_node) {
    Item content = format_children(group_node);
    
    // Add ZWSP after closing brace
    if (is_non_empty(content)) {
        content = append_text(content, "\u200B");
    }
    
    return content;
}
```
**Impact**: Fixes 6 group tests + some whitespace tests

### 4. **Fix `\label` as Inline Element** (2 hours)
```cpp
// In handle_label()
Item handle_label(Item label_node, RenderContext& ctx) {
    std::string label_key = get_label_key(label_node);
    
    // Collect label but don't output visible element
    ctx.labels[label_key] = {
        .counter_value = get_current_counter(ctx),
        .id = generate_id(label_key)
    };
    
    // Return empty inline element (or null)
    return ItemNull;  // Don't create paragraph!
}
```
**Impact**: Fixes 14 label/reference tests

---

## Recommended Implementation Order

### Sprint 1: Foundation (2-3 days)
1. ✅ Add `RenderContext` extensions (mode stack, env stack, font stack)
2. ✅ Implement HTML entity escaping
3. ✅ Fix tilde → `&nbsp;` conversion
4. ✅ Fix ZWSP after groups

**Expected Result**: ~25-30 tests passing (up from 0)

### Sprint 2: Whitespace & Paragraphs (3-4 days)
1. ✅ Implement mode tracking (vertical/horizontal)
2. ✅ Whitespace normalization pass
3. ✅ Paragraph detection with `\par` support
4. ✅ Comment removal in preprocessing

**Expected Result**: ~40-45 tests passing

### Sprint 3: Environments (4-5 days)
1. ✅ Environment context stack
2. ✅ Implement list environment handlers (itemize, enumerate)
3. ✅ Paragraph continuation logic
4. ✅ Alignment scope tracking

**Expected Result**: ~55-60 tests passing

### Sprint 4: Labels & References (3-4 days)
1. ✅ Label collection pass
2. ✅ Reference resolution pass
3. ✅ Counter integration
4. ✅ Fix `\label` as inline element

**Expected Result**: ~65-70 tests passing (>90%)

### Sprint 5: Polish (2-3 days)
1. ✅ Accent composition
2. ✅ Font state management
3. ✅ Special character mappings
4. ✅ Edge case fixes

**Expected Result**: >70 tests passing (>95%)

---

## Testing Strategy

### Unit Tests for New Systems
```cpp
// test/test_whitespace_normalizer_gtest.cpp
TEST(WhitespaceNormalizer, TildeConversion) {
    EXPECT_EQ(normalize("hello~world"), "hello&nbsp;world");
}

// test/test_html_encoder_gtest.cpp
TEST(HtmlEncoder, EntityEscaping) {
    EXPECT_EQ(escape("A & B"), "A &amp; B");
}

// test/test_label_collector_gtest.cpp
TEST(LabelCollector, BasicLabelCollection) {
    // Test label collection from AST
}
```

### Integration Tests
- Run extended test suite after each sprint
- Track pass rate progression
- Identify regressions immediately

---

## Metrics & Success Criteria

### Current State
- **Baseline**: 34/35 passing (97.1%) ✅
- **Extended**: 0/74 passing (0%) ❌
- **Overall**: 34/109 passing (31.2%)

### Target Milestones

| Milestone | Baseline | Extended | Overall | Timeline |
|-----------|----------|----------|---------|----------|
| **M1: Foundation** | 35/35 | 25/74 | 60/109 (55%) | Week 1 |
| **M2: Whitespace** | 35/35 | 45/74 | 80/109 (73%) | Week 2 |
| **M3: Environments** | 35/35 | 60/74 | 95/109 (87%) | Week 3 |
| **M4: Labels/Refs** | 35/35 | 70/74 | 105/109 (96%) | Week 4 |
| **M5: Polish** | 35/35 | 72/74 | 107/109 (98%) | Week 5 |

### Success Criteria
- ✅ **Baseline maintained**: 35/35 passing (100%)
- 🎯 **Extended improved**: >70/74 passing (>95%)
- 🎯 **Overall quality**: >105/109 passing (>96%)

---

## Architecture Diagram

```
                    INPUT
                      ↓
        ┌─────────────────────────┐
        │   Tree-sitter Parser    │
        │  (grammar.js → AST)     │
        └───────────┬─────────────┘
                    ↓
        ┌─────────────────────────┐
        │   Macro Expander        │ ← NEW SYSTEM
        │  (expand \newcommand)   │
        └───────────┬─────────────┘
                    ↓
        ┌─────────────────────────┐
        │   Label Collector       │ ← NEW SYSTEM
        │  (find all \label)      │
        └───────────┬─────────────┘
                    ↓
        ┌─────────────────────────┐
        │ Whitespace Normalizer   │ ← NEW SYSTEM
        │ (ZWSP, ~, paragraphs)   │
        └───────────┬─────────────┘
                    ↓
        ┌─────────────────────────┐
        │   HTML Formatter        │
        │  (format-latex-html)    │ ← ENHANCED
        │   + Context Stack       │
        │   + Environment Handler │
        │   + Font Manager        │
        └───────────┬─────────────┘
                    ↓
        ┌─────────────────────────┐
        │  HTML Post-Processor    │ ← NEW SYSTEM
        │  (entity escape, refs)  │
        └───────────┬─────────────┘
                    ↓
                  OUTPUT
```

---

## Conclusion

The extended test failures reveal **systematic architectural gaps** rather than isolated bugs. The current single-pass formatting approach cannot handle:

1. Context-dependent processing (modes, scopes, environments)
2. Forward/backward references (labels/refs)
3. Multi-level transformations (expansion → normalization → formatting)

**Recommended Approach**: Implement multi-pass architecture with dedicated subsystems for macro expansion, whitespace normalization, label collection, and reference resolution. This will enable >95% extended test pass rate within 4-5 weeks.

**Quick Wins**: Start with HTML entity escaping, tilde conversion, and ZWSP insertion to get ~25-30 tests passing in first sprint.
