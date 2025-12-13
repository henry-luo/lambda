# LaTeX Grammar Optimization: Complete History

## Executive Summary

This document chronicles the complete optimization journey of the Lambda LaTeX Tree-sitter grammar, from an **unwieldy 61MB parser** to a **lean 250KB parser** - a **99.6% reduction** in parser size while maintaining full LaTeX document parsing capability.

| Metric | Original | Phase 9 | **Phase 10 (Final)** |
|--------|----------|---------|----------------------|
| Parser Size | 61 MB | 6.3 MB | **250 KB** |
| STATE_COUNT | ~10,000 | 3,169 | **~200** |
| Rule Count | ~150+ | ~80 | **~50** |
| Reduction | 0% | 90% | **99.6%** |

---

## Table of Contents

1. [Problem Analysis](#problem-analysis)
2. [Phase 1: Symbol Command Consolidation](#phase-1-symbol-command-consolidation)
3. [Phase 2: Math Environment Optimization](#phase-2-math-environment-optimization)
4. [Phase 3: Section & Delimiter Consolidation](#phase-3-section--delimiter-consolidation)
5. [Phase 4: Environment Consolidation](#phase-4-environment-consolidation)
6. [Phase 5: Regex Pattern Consolidation](#phase-5-regex-pattern-consolidation)
7. [Phase 6: Aggressive Command Consolidation](#phase-6-aggressive-command-consolidation)
8. [Phase 7: Simple & Metadata Commands](#phase-7-simple--metadata-commands)
9. [Phase 8-9: Structural Optimizations](#phase-8-9-structural-optimizations)
10. [Phase 10: Hybrid Grammar Rewrite](#phase-10-hybrid-grammar-rewrite)
11. [Final Architecture](#final-architecture)
12. [Lessons Learned](#lessons-learned)

---

## Problem Analysis

### Initial State

The original LaTeX grammar generated an **extremely large parser (61MB)** due to several anti-patterns that caused exponential state explosion in Tree-sitter's LR parser generator.

### Root Causes Identified

#### 1. Massive `choice()` Statements (PRIMARY ISSUE)

Multiple rules contained 40-60 literal string alternatives in a single `choice()`:

| Line | Rule | Items | Impact |
|------|------|-------|--------|
| 813 | `symbol_command` | 52 items | Creates 52 parser states |
| 1069 | `citation_reference` | 60 items | Creates 60 parser states |
| 1357 | `glossary_reference` | 46 items | Creates 46 parser states |
| 1423 | `acronym_reference` | 43 items | Creates 43 parser states |

**Why this is problematic:**
- Each string literal in a `choice()` creates separate parser states
- With 60 alternatives, Tree-sitter generates ~60 different state transitions
- Combined with other rules, this causes combinatorial explosion
- The parser must track all possible paths through these alternatives

#### 2. Nested `repeat()` with `prec.right()`

```javascript
_section: $ =>
  prec.right(
    choice(
      repeat1($.part),
      repeat1($.chapter),
      repeat1($.section),
      repeat1($.subsection),
      repeat1($.subsubsection),
    ),
  ),
```

This pattern creates ambiguity about when to shift vs reduce, generating many conflict resolution states.

#### 3. Deep Precedence Hierarchies

Section nesting (part → chapter → section → subsection → subsubsection) with optional content at each level creates multiplicative state space.

### Reference Comparison: JavaScript Grammar

To understand what "good" looks like, we analyzed the mature `tree-sitter-javascript` grammar:

| Metric | JavaScript | LaTeX (Original) |
|--------|------------|------------------|
| Parser Size | ~1.5MB | 61MB |
| STATE_COUNT | ~2,500 | ~10,000 |
| LARGE_STATE_COUNT | ~150 | ~1,200 |
| grammar.js | ~1,500 lines | ~2,200 lines |

Key differences:
- JavaScript uses `inline` rules for simple hidden rules
- JavaScript has flat rule structures, not deep nesting
- JavaScript uses regex patterns for similar tokens

---

## Phase 1: Symbol Command Consolidation

### Initial State
- Parser size: **61 MB**
- 227 explicit string literals across 5 major choice() statements

### Optimization Step

Refactored 5 largest choice() statements to regex patterns:

**Before (52 states):**
```javascript
symbol_command: $ =>
  field(
    'command',
    choice(
      '\\ss', '\\SS', '\\o', '\\O', '\\ae', '\\AE', // ... 52 items
    ),
  ),
```

**After (1 state):**
```javascript
symbol_command: $ =>
  seq(
    field('command', token(prec(1, /\\(ss|SS|o|O|ae|AE|oe|OE|aa|AA|l|L|i|j|dh|DH|th|TH|dag|ddag|S|P|copyright|pounds|textbackslash|LaTeX|TeX|LaTeXe|textquoteleft|textquoteright|textquotedblleft|textquotedblright|textendash|textemdash|textellipsis|dots|ldots|textbullet|textperiodcentered|textasteriskcentered|textcent|textsterling|textyen|texteuro|textdollar|textexclamdown|textquestiondown|textsection|textparagraph|textdegree|textregistered|texttrademark)/))),
  ),
```

**Rules optimized:**
1. `symbol_command` - 52 LaTeX symbol commands (\\ss, \\LaTeX, etc.)
2. `citation` - 60 citation commands (\\cite, \\parencite, \\footcite, etc.)
3. `glossary_entry_reference` - 46 glossary commands (\\gls, \\Gls, \\glspl, etc.)
4. `acronym_reference` - 43 acronym commands (\\acrshort, \\acrlong, etc.)
5. `label_reference` - 26 reference commands (\\ref, \\cref, \\pageref, etc.)

### State After
- Parser size: **30 MB**
- Reduction: **51%**
- 227 string literals → 5 regex patterns

---

## Phase 2: Math Environment Optimization

### Initial State
- Parser size: **30 MB**

### Optimization Step

Refactored 8 additional medium-sized rules:

1. `math_environment` - 27 math environment names (equation, align, gather, etc.)
2. `spacing_command` - 15 spacing commands (\\quad, \\qquad, \\,, etc.)
3. `diacritic_command` - 15 diacritic accents (\', \`, \^, etc.)
4. `escape_sequence` - 12 escape characters (\\$, \\%, \\#, etc.)
5. `operator` - 10 operator symbols (+, -, *, /, etc.)
6. `_new_command_definition` - 10 command definition commands
7. `_newer_command_definition` - 8 newer definition commands
8. `old_command_definition` - 4 old-style definitions (\\def, \\gdef, etc.)

### State After
- Parser size: **24 MB**
- Reduction: **61%**
- 101 additional literals → 8 regex patterns

---

## Phase 3: Section & Delimiter Consolidation

### Initial State
- Parser size: **24 MB**

### Optimization Step

Optimized 28 additional rules including section commands, delimiters, and includes:

| # | Rule | Description |
|---|------|-------------|
| 14 | `_part_declaration` | part/part* commands |
| 15 | `_chapter_declaration` | chapter/addchap commands |
| 16 | `_section_declaration` | section/addsec commands |
| 17 | `_subsection_declaration` | subsection commands |
| 18 | `_subsubsection_declaration` | subsubsection commands |
| 19 | `_paragraph_declaration` | paragraph commands |
| 20 | `_subparagraph_declaration` | subparagraph commands |
| 21 | `_enum_itemdeclaration` | item commands |
| 22 | `math_delimiter` | left/right/big delimiter commands (10 items) |
| 23 | `text_mode` | text/intertext/shortintertext (3 items) |
| 24 | `label_reference_range` | crefrange commands (6 items) |
| 25 | `verb_command` | verb/verb* commands |
| 26-29 | `counter_*` | Counter manipulation commands |
| 30 | `package_include` | usepackage/RequirePackage commands |
| 31 | `latex_include` | include/input/subfile commands (4 items) |
| 32 | `verbatim_include` | verbatiminput commands |
| 33 | `import_include` | import/subimport commands (6 items) |
| 34-41 | Various | Definition commands, color, tikz, hyperlinks |

### State After
- Parser size: **17 MB**
- Reduction: **72%**
- ~85 additional literals → 28 regex patterns

---

## Phase 4: Environment Consolidation

### Initial State
- Parser size: **17 MB**

### Optimization Step

Consolidated environment handling and list structures:
- Unified environment opening/closing logic
- Simplified list environment handling
- Reduced environment name matching states

### State After
- Parser size: **15 MB**
- Reduction: **75%**

---

## Phase 5: Regex Pattern Consolidation

### Initial State
- Parser size: **15 MB**

### Optimization Step

Merged similar regex patterns and consolidated overlapping rules:
- Combined citation variants into single pattern
- Merged reference command patterns
- Unified spacing command patterns

### State After
- Parser size: **14 MB**
- Reduction: **77%**

---

## Phase 6: Aggressive Command Consolidation

### Initial State
- Parser size: **14 MB**

### Optimization Step

Aggressively consolidated the `_command` rule alternatives:

**Before:** ~51 alternatives in `_command` rule
**After:** ~24 alternatives

Consolidated categories:
1. **Consolidated rules**: metadata_command, single_path_include, double_path_include, citation, counter_command, label_command, glossary_command, acronym_command, color_command, simple_command
2. **Definition commands**: new_command_definition, old_command_definition, let_command_definition, paired_delimiter_definition, environment_definition, theorem_definition
3. **Special commands**: tikz_library_import, hyperlink, changes_replaced, todo
4. **Character commands**: escape_sequence, diacritic_command, linebreak_command, verb_command
5. **Fallback**: generic_command

### State After
- Parser size: **9.4 MB**
- Reduction: **85%**

---

## Phase 7: Simple & Metadata Commands

### Initial State
- Parser size: **9.4 MB**

### Optimization Step

Final command consolidation:

1. **simple_command** - Unified spacing_command + symbol_command (2→1)
   - All no-argument commands: spacing (`\quad`, `\qquad`, `\,`, `\!`, `\;`, `\:`) + symbols (`\ss`, `\LaTeX`, `\TeX`, `\copyright`, etc.)

2. **metadata_command** - Unified title_declaration + author_declaration + caption (3→1)
   - Combined: `\title`, `\author`, `\caption`, `\thanks`, `\date`, `\abstract`

### State After
- Parser size: **7.6 MB**
- Reduction: **88%**
- 5 rules → 2 consolidated rules

---

## Phase 8-9: Structural Optimizations

### Initial State
- Parser size: **7.6 MB**
- LARGE_STATE_COUNT: 873

### Optimization Step

Applied lessons learned from JavaScript grammar comparison:

1. **Added `inline` rules** for simple hidden rules
2. **Flattened section hierarchy** - part/chapter/section became siblings instead of nested
3. **Flattened paragraph hierarchy** - removed unnecessary nesting
4. **Removed unnecessary `prec.right()` patterns** - only keep where truly needed
5. **Simplified precedence conflicts**

### State After
- Parser size: **6.3 MB**
- Reduction: **90%**
- LARGE_STATE_COUNT: **435** (50% reduction from 873!)

---

## Phase 10: Hybrid Grammar Rewrite

### Initial State

After Phase 9:
- Parser size: **6.3 MB**
- STATE_COUNT: 3,169
- Rule count: ~80
- Still complex with many specialized rules

### Key Insight: LaTeX.js Approach

Analysis of the **LaTeX.js** project (a JavaScript LaTeX-to-HTML renderer) revealed a fundamentally different approach:

> "LaTeX.js doesn't try to parse LaTeX semantically at parse time. It parses **generically** and interprets **at runtime**."

**LaTeX.js PEG.js grammar characteristics:**
- Only ~30 rules total
- Generic `command` rule: `\commandname` followed by any arguments
- Generic `environment` rule: `\begin{name}...\end{name}`
- No attempt to distinguish `\section` from `\textbf` at grammar level
- Semantic interpretation happens in JavaScript runtime code

### The Rewrite Strategy

**Philosophy shift:**
- Parse LaTeX **syntactically**, not **semantically**
- Let the converter (`input-latex-ts.cpp`) handle semantic interpretation
- Keep grammar minimal and conflict-free

### New Grammar Architecture

```javascript
// ~50 rules total, inspired by LaTeX.js

module.exports = grammar({
  name: 'latex',

  externals: $ => [
    $._verbatim_content,      // External scanner for verbatim content
    $._comment_env_content,   // External scanner for comment content  
    $.begin_document,         // \begin{document} - PRIORITY TOKEN
    $.end_document,           // \end{document} - PRIORITY TOKEN
  ],

  rules: {
    // Top-level: source file contains preamble + document
    source_file: $ => repeat($._top_level_item),

    _top_level_item: $ => choice(
      $.document,          // The main document
      $.environment,       // Preamble environments
      $.command,           // Preamble commands (\documentclass, \usepackage, etc.)
      $.curly_group,
      $.brack_group,
      $.line_comment,
      $.text,
      $.space,
    ),

    // ================================================================
    // DOCUMENT STRUCTURE
    // ================================================================

    // Document: everything between \begin{document} and \end{document}
    // Uses external scanner to ensure \begin{document} is recognized as a unit
    document: $ => seq(
      $.begin_document,    // External token - has priority over generic command
      repeat($._block),
      $.end_document,
    ),

    // Block-level content (inside document)
    _block: $ => choice(
      $.paragraph_break,   // Two or more newlines
      $.environment,       // Any environment
      $.section,           // Section commands with their content
      $.paragraph,         // Regular paragraph
    ),

    paragraph_break: $ => /\n\s*\n\s*/,

    // Sections have hierarchical content
    section: $ => prec.right(seq(
      field('command', $.section_command),
      optional(field('toc', $.brack_group)),   // Optional short title
      field('title', $.curly_group),
      repeat($._block),                         // Section content
    )),

    section_command: $ => /\\(part|chapter|section|subsection|subsubsection|paragraph|subparagraph)\*?/,

    // ================================================================
    // INLINE CONTENT
    // ================================================================

    paragraph: $ => prec.right(repeat1($._inline)),

    _inline: $ => choice(
      $.text,
      $.space,
      $.line_comment,
      $.environment,       // Environments can appear inline
      $.command,           // Generic commands
      $.curly_group,
      $.brack_group,
      $.math,
      $.ligature,
      $.control_symbol,
    ),

    text: $ => /[^\\{}$%\[\]\n~&#^_]+/,

    // ================================================================
    // COMMANDS (Generic Approach - key insight from LaTeX.js)
    // ================================================================

    // Generic command: \name followed by optional arguments
    // ALL commands use this pattern - semantic interpretation at runtime
    command: $ => prec.right(seq(
      field('name', $.command_name),
      repeat(field('arg', choice(
        $.curly_group,
        $.brack_group,
        $.star,
      ))),
    )),

    command_name: $ => /\\[@a-zA-Z]+\*?/,

    // ================================================================
    // ENVIRONMENTS (Generic Approach)
    // ================================================================

    environment: $ => $.generic_environment,

    generic_environment: $ => seq(
      field('begin', $.begin_env),
      repeat($._block),
      field('end', $.end_env),
    ),

    begin_env: $ => prec.right(seq(
      '\\begin',
      field('name', $.curly_group),
      optional(field('options', $.brack_group)),
      repeat(field('arg', $.curly_group)),
    )),

    end_env: $ => seq(
      '\\end',
      field('name', $.curly_group),
    ),

    // ================================================================
    // MATH MODE
    // ================================================================

    math: $ => choice($.inline_math, $.display_math),

    inline_math: $ => choice(
      seq('$', repeat($._math_content), '$'),
      seq('\\(', repeat($._math_content), '\\)'),
    ),

    display_math: $ => choice(
      seq('$$', repeat($._math_content), '$$'),
      seq('\\[', repeat($._math_content), '\\]'),
    ),

    _math_content: $ => choice(
      $.math_text,
      $.command,
      $.curly_group,
      $.subscript,
      $.superscript,
      /[&|]/,
    ),

    subscript: $ => seq('_', choice($.curly_group, /[a-zA-Z0-9]/)),
    superscript: $ => seq('^', choice($.curly_group, /[a-zA-Z0-9]/)),
  },
});
```

### External Scanner for Document Boundaries

**Critical Issue Solved:** Without external scanner, `\begin{document}` was matched as a generic command, not as the document boundary.

**Solution:** C external scanner (`scanner.c`) with priority detection:

```c
enum TokenType {
  VERBATIM_CONTENT,
  COMMENT_ENV_CONTENT,
  BEGIN_DOCUMENT,      // Priority token
  END_DOCUMENT,        // Priority token
};

// Scan \begin{document} - returns true if matched
static bool scan_begin_document(TSLexer *lexer) {
  if (lexer->lookahead != '\\') return false;
  lexer->advance(lexer, false);
  
  if (try_match_string(lexer, "begin{document}")) {
    lexer->mark_end(lexer);
    return true;
  }
  return false;
}

bool tree_sitter_latex_external_scanner_scan(
  void *payload, TSLexer *lexer, const bool *valid_symbols
) {
  // External tokens have priority over grammar rules
  if (valid_symbols[BEGIN_DOCUMENT]) {
    if (scan_begin_document(lexer)) {
      lexer->result_symbol = BEGIN_DOCUMENT;
      return true;
    }
  }
  // ... similar for END_DOCUMENT, VERBATIM_CONTENT, etc.
  return false;
}
```

### Converter Updates (`input-latex-ts.cpp`)

Updated node classification for new AST structure:

```cpp
static std::unordered_map<std::string, NodeClassification> node_classification = {
    // Document structure
    {"source_file", NODE_CONTAINER},
    {"document", NODE_CONTAINER},
    {"begin_document", NODE_LEAF},
    {"end_document", NODE_LEAF},
    
    // Content
    {"paragraph", NODE_CONTAINER},
    {"paragraph_break", NODE_LEAF},
    {"section", NODE_CONTAINER},
    {"section_command", NODE_LEAF},
    
    // Commands - now generic
    {"command", NODE_CONTAINER},
    {"command_name", NODE_LEAF},
    
    // Environments - now generic
    {"environment", NODE_CONTAINER},
    {"generic_environment", NODE_CONTAINER},
    {"begin_env", NODE_CONTAINER},
    {"end_env", NODE_CONTAINER},
    
    // Math
    {"math", NODE_CONTAINER},
    {"inline_math", NODE_CONTAINER},
    {"display_math", NODE_CONTAINER},
    {"subscript", NODE_CONTAINER},
    {"superscript", NODE_CONTAINER},
    
    // Terminal nodes
    {"text", NODE_TEXT},
    {"math_text", NODE_TEXT},
    {"space", NODE_WHITESPACE},
    {"line_comment", NODE_SKIP},
};
```

**Key converter changes:**
- `convert_command()` now uses `field("name")` to get command name
- `convert_environment()` now uses `begin_env`/`end_env` structure
- `convert_section()` extracts command, toc, title, and nested content
- Text nodes extracted directly when `child_count == 0`

### Issues Encountered and Solved

| Issue | Symptom | Solution |
|-------|---------|----------|
| Everything parsed as preamble | `\begin{document}` matched as generic command | External scanner with priority tokens |
| Verbatim consuming everything | Scanner matched all content as verbatim | Disabled verbatim external scanning |
| Text nodes not extracting | `convert_text_node()` expected children | Direct text extraction for terminals |
| Equation environments with errors | `^` character invalid in text pattern | Added `^` and `_` to text pattern (only special in math mode) |
| Math environment conflicts | `math_environment` vs `generic_environment` | Simplified to single `generic_environment` |

### Final State

| Metric | Before (Phase 9) | After (Phase 10) |
|--------|------------------|------------------|
| Parser Size | 6.3 MB | **250 KB** |
| STATE_COUNT | 3,169 | **~200** |
| Rule Count | ~80 | **~50** |
| Conflicts | Several | **0** |
| Parser file | 6.3MB `parser.c` | **250KB `parser.c`** |

---

## Final Architecture

### Grammar Structure Summary

```
source_file
├── _top_level_item (repeat)
│   ├── document (main content)
│   │   ├── begin_document (external token)
│   │   ├── _block (repeat)
│   │   │   ├── paragraph_break
│   │   │   ├── environment → generic_environment
│   │   │   ├── section (hierarchical)
│   │   │   └── paragraph → _inline (repeat)
│   │   └── end_document (external token)
│   ├── environment (preamble)
│   ├── command (preamble: \documentclass, \usepackage)
│   └── text, space, line_comment, curly_group, brack_group
```

### Key Design Decisions

1. **Generic commands**: No distinction between `\section` and `\textbf` at grammar level
2. **External scanner for document**: Ensures `\begin{document}` has priority
3. **Section hierarchy via repeat**: Nested content captured, hierarchy built at runtime
4. **Environment is inline-compatible**: Environments can interrupt paragraphs
5. **Math mode separate**: Explicit subscript/superscript handling

### File Locations

| File | Purpose |
|------|---------|
| `lambda/tree-sitter-latex/grammar.js` | Hybrid grammar (~300 lines) |
| `lambda/tree-sitter-latex/src/scanner.c` | External scanner for priority tokens |
| `lambda/tree-sitter-latex/src/parser.c` | Generated parser (250KB) |
| `lambda/input/input-latex-ts.cpp` | CST to Lambda tree converter |

---

## Lessons Learned

### 1. Parse Syntax, Not Semantics

The biggest insight: **don't try to encode semantic knowledge in the grammar**. Let the grammar handle syntax; let the converter/runtime handle semantics.

### 2. External Scanners for Priority

When a token must have priority over grammar rules (like `\begin{document}` vs generic `\begin`), use an external scanner.

### 3. Regex Patterns Dramatically Reduce States

Converting 60 string literals in a `choice()` to a single regex pattern: **60 states → 1 state**.

### 4. Flat is Better Than Nested

Tree-sitter's LR parser struggles with deeply nested optional structures. Flatten hierarchies and rebuild at runtime.

### 5. Reference Other Grammars

Studying `tree-sitter-javascript` and `latex.js` provided crucial insights that weren't obvious from the Tree-sitter documentation.

### 6. Incremental Optimization Has Limits

Phases 1-9 achieved 90% reduction through incremental fixes. But the final 10% required a **complete architectural rethink** (Phase 10).

---

## Verification Checklist

✅ Parser compiles successfully (0 errors, 0 warnings)  
✅ Parser size: 250KB (down from 61MB)  
✅ No conflicts in grammar  
✅ All parsing tests pass  
✅ Section commands work (`\section`, `\subsection`, `\chapter`)  
✅ Math environments work (`equation`, `align`, `gather`)  
✅ Escape sequences work (`\$`, `\%`, `\#`, `\&`)  
✅ Diacritics work (`\'e`, `` \`a ``, `\^i`)  
✅ Citations work (`\cite`, `\parencite`)  
✅ References work (`\ref`, `\cref`)  
✅ Includes work (`\include`, `\input`, `\import`)  
✅ Document structure correct (preamble vs body separation)  

---

## References

- Tree-sitter performance guide: https://tree-sitter.github.io/tree-sitter/creating-parsers#performance
- Tree-sitter regex tokens: https://tree-sitter.github.io/tree-sitter/creating-parsers#the-grammar-dsl
- LaTeX.js grammar: https://github.com/michael-brade/LaTeX.js
- tree-sitter-javascript: https://github.com/tree-sitter/tree-sitter-javascript
- tree-sitter-python: https://github.com/tree-sitter/tree-sitter-python
