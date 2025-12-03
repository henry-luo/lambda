# LaTeX Parser Enhancement Plan

## Executive Summary

This document outlines a comprehensive plan to enhance Lambda's `input-latex.cpp` parser to become a **general-purpose LaTeX parser** that combines features from both:
1. **LaTeX.js PEG.js grammar** (1065 lines) - excels at whitespace handling, character escapes, mode tracking, runtime evaluation
2. **tree-sitter-latex grammar** (1400 lines) - excels at structural parsing, command coverage, section hierarchy, error recovery

The goal is to create a parser that can:
- Parse arbitrary LaTeX documents (not just for HTML transformation)
- Support multiple output targets (HTML, Markdown, JSON AST, validation)
- Handle advanced LaTeX features (macros, counters, references)
- Provide accurate source positions for error reporting and LSP integration

### Important Clarifications

1. **Output Construction**: All parsed output must be constructed using Lambda's **MarkBuilder** API (`lambda/mark_builder.hpp`). This provides fluent builders for elements, maps, arrays, and lists with proper arena allocation.

2. **Math Module Exclusion**: The **Math parsing module is out of scope** for this enhancement. The current math parsing implementation (`$...$`, `$$...$$`, math environments) will be kept as-is. This plan focuses on text mode parsing improvements.

3. **Test Strategy**: Leverage the **tree-sitter-latex test corpus** (`test/corpus/*.txt`) as a reference for parsing behavior and convert relevant tests to Lambda's test format.

---

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [Architecture Goals](#architecture-goals)
3. [MarkBuilder Integration](#markbuilder-integration)
4. [Refactoring Plan](#refactoring-plan)
5. [Implementation Phases](#implementation-phases)
6. [Grammar Feature Matrix](#grammar-feature-matrix)
7. [Detailed Implementation Guide](#detailed-implementation-guide)
8. [Testing Strategy](#testing-strategy)
9. [Leveraging tree-sitter-latex Test Corpus](#leveraging-tree-sitter-latex-test-corpus)
10. [Migration Path](#migration-path)

---

## Current State Analysis

### Current Parser Architecture

The current `input-latex.cpp` (1684 lines) uses a hand-written recursive descent parser with:

```
Current Structure:
├── parse_latex()                    # Entry point
├── parse_latex_element()            # Main dispatch
├── parse_latex_command()            # Command parsing (~600 lines)
├── parse_command_name()             # Command name extraction
├── parse_command_arguments()        # Argument parsing
├── parse_latex_string_content()     # String content parsing
└── Helper functions (diacritics, whitespace, etc.)
```

### Current Strengths
- ✅ Basic command parsing
- ✅ Environment handling (`\begin{}`/`\end{}`)
- ✅ Math mode (`$...$`, `$$...$$`)
- ✅ Diacritic support (combining characters)
- ✅ Counter system (full implementation)
- ✅ Whitespace normalization
- ✅ Comment handling

### Current Weaknesses
- ❌ Monolithic `parse_latex_command()` function (600+ lines with many `if` statements)
- ❌ No mode tracking (H/V/HV/P modes like PEG.js)
- ❌ Missing `\char`, `^^XX`, `^^^^XXXX` character handling
- ❌ No macro definition/expansion (`\newcommand`, `\def`)
- ❌ Limited section hierarchy (unlike tree-sitter's nested sections)
- ❌ Missing many commands from both grammars
- ❌ No source position tracking in parsed elements
- ❌ No lookahead/backtracking capabilities

---

## Architecture Goals

### 1. Modular Parser Structure

Refactor into grammar-like rule functions:

```cpp
// Each rule is a dedicated function returning Item or nullptr
class LatexParser {
    // Primitives (PEG.js style)
    Item parse_primitive();
    Item parse_char();
    Item parse_digit();
    Item parse_space();
    Item parse_ligature();      // ff, fi, fl, ---, --, ``, ''
    Item parse_ctrl_sym();      // \$, \%, \#, etc.
    Item parse_charsym();       // \char, ^^XX, ^^^^XXXX
    Item parse_diacritic();

    // Text and paragraphs
    Item parse_text();
    Item parse_paragraph();
    Item parse_line();

    // Groups and arguments
    Item parse_group();         // {...}
    Item parse_opt_group();     // [...]
    Item parse_arg_group();     // Required argument

    // Commands and macros
    Item parse_command();
    Item parse_macro();
    Item parse_macro_args();

    // Environments
    Item parse_environment();
    Item parse_begin_env();
    Item parse_end_env();

    // Sections (tree-sitter style hierarchy)
    Item parse_section();
    Item parse_subsection();
    Item parse_subsubsection();
    Item parse_paragraph_cmd();
    Item parse_subparagraph();

    // Math
    Item parse_math();
    Item parse_inline_math();
    Item parse_display_math();
    Item parse_math_environment();
};
```

### 2. Mode Tracking (from PEG.js)

```cpp
enum LatexMode {
    MODE_VERTICAL,      // V - between paragraphs
    MODE_HORIZONTAL,    // H - within paragraph
    MODE_BOTH,          // HV - works in both modes
    MODE_PARAGRAPH,     // P - paragraph-level only
    MODE_PREAMBLE,      // Before \begin{document}
    MODE_MATH,          // Inside math mode
    MODE_RESTRICTED_H,  // Restricted horizontal (in \hbox, etc.)
};

class LatexParser {
    LatexMode mode = MODE_VERTICAL;
    std::stack<LatexMode> mode_stack;

    void enter_mode(LatexMode m) { mode_stack.push(mode); mode = m; }
    void exit_mode() { mode = mode_stack.top(); mode_stack.pop(); }
    bool is_vmode() const { return mode == MODE_VERTICAL; }
    bool is_hmode() const { return mode == MODE_HORIZONTAL; }
};
```

### 3. Command Registry (from both grammars)

```cpp
// Command metadata for parsing and formatting
struct CommandSpec {
    const char* name;
    const char* arg_spec;   // PEG.js style: "V g o?" = vertical, required group, optional group
    bool is_symbol;         // No arguments, produces symbol
    bool gobbles_space;     // Consumes trailing whitespace
    LatexMode mode;         // Which mode this command works in
    // Handler function pointer for execution
};

// Comprehensive command registry combining both grammars
static const CommandSpec COMMANDS[] = {
    // Symbols (no arguments)
    {"LaTeX", "", true, true, MODE_BOTH},
    {"TeX", "", true, true, MODE_BOTH},
    {"ss", "", true, true, MODE_HORIZONTAL},
    {"ae", "", true, true, MODE_HORIZONTAL},
    // ... 100+ symbol commands from both grammars

    // Spacing commands
    {"quad", "", true, false, MODE_HORIZONTAL},
    {"qquad", "", true, false, MODE_HORIZONTAL},
    {"hspace", "s l", false, false, MODE_HORIZONTAL},  // star, length
    {"vspace", "s l", false, false, MODE_VERTICAL},

    // Font commands
    {"textbf", "H X g", false, false, MODE_HORIZONTAL},
    {"textit", "H X g", false, false, MODE_HORIZONTAL},
    {"textrm", "H X g", false, false, MODE_HORIZONTAL},
    // ... etc

    // Section commands (tree-sitter style)
    {"part", "o? g", false, false, MODE_VERTICAL},
    {"chapter", "o? g", false, false, MODE_VERTICAL},
    {"section", "o? g", false, false, MODE_VERTICAL},
    {"subsection", "o? g", false, false, MODE_VERTICAL},

    // Counter commands
    {"newcounter", "i o?", false, false, MODE_PREAMBLE},
    {"setcounter", "i n", false, false, MODE_BOTH},
    {"addtocounter", "i n", false, false, MODE_BOTH},
    {"stepcounter", "i", false, false, MODE_BOTH},

    // ... 200+ more commands
};
```

### 4. Source Position Tracking

```cpp
struct SourceSpan {
    size_t start_offset;
    size_t end_offset;
    uint32_t start_line;
    uint32_t start_col;
    uint32_t end_line;
    uint32_t end_col;
};

// Each parsed element carries source position
struct ParsedNode {
    Item item;
    SourceSpan span;
    ParsedNode* parent;
    std::vector<ParsedNode*> children;
};
```

---

## MarkBuilder Integration

All parsed output **must** be constructed using Lambda's `MarkBuilder` API. This ensures proper memory management (arena allocation) and consistent data structures.

### MarkBuilder API Overview

```cpp
#include "mark_builder.hpp"

class LatexParser {
    MarkBuilder builder_;  // Created with Input* context

public:
    LatexParser(Input* input, const char* source, size_t len)
        : builder_(input), /* ... */ {}
};
```

### Creating Primitive Items

```cpp
// String content (arena-allocated)
Item text = builder_.createStringItem("Hello, World!");
Item text_len = builder_.createStringItem(ptr, length);

// Names and symbols (pooled/interned)
Item name = builder_.createNameItem("section");
Item sym = builder_.createSymbolItem("math");

// Primitives
Item num = builder_.createInt(42);
Item flt = builder_.createFloat(3.14);
Item flag = builder_.createBool(true);
Item nil = builder_.createNull();
```

### Creating Elements (LaTeX commands → Elements)

```cpp
// For a LaTeX command like \textbf{content}
Item parse_textbf() {
    // Use element() to create an ElementBuilder
    return builder_.element("textbf")
        .put("style", "bold")           // Attribute
        .add(parse_content())           // Child content
        .final();                       // Finalize and return Item
}

// For a sectioning command like \section{title}
Item parse_section_cmd() {
    return builder_.element("section")
        .put("level", builder_.createInt(1))
        .put("starred", builder_.createBool(false))
        .add(parse_title())             // Title as child
        .final();
}
```

### Creating Maps (for metadata/options)

```cpp
// For command options like [width=0.5\textwidth, height=10cm]
Item parse_options() {
    return builder_.map()
        .put("width", parse_length())
        .put("height", parse_length())
        .final();
}
```

### Creating Arrays/Lists (for sequences)

```cpp
// For environments with multiple children
Item parse_itemize() {
    auto arr = builder_.array();
    while (peek_item()) {
        arr.add(parse_item());
    }
    return arr.final();
}

// For text content (list of inline elements)
Item parse_paragraph() {
    auto list = builder_.list();
    while (!at_paragraph_break()) {
        list.add(parse_inline());
    }
    return list.final();
}
```

### Example: Full Command Parsing

```cpp
// Parse \href{url}{text}
Item LatexParser::parse_href() {
    if (!match("\\href")) return ItemNull;

    expect('{');
    std::string url = parse_url_content();
    expect('}');

    expect('{');
    Item text = parse_content();
    expect('}');

    return builder_.element("a")
        .put("href", builder_.createStringItem(url.c_str()))
        .add(text)
        .final();
}

// Parse \begin{env}...\end{env}
Item LatexParser::parse_environment() {
    if (!match("\\begin{")) return ItemNull;

    std::string name = parse_identifier();
    expect('}');

    Item options = ItemNull;
    if (peek() == '[') {
        advance();
        options = parse_key_value_list();
        expect(']');
    }

    Item content = parse_environment_body(name);

    expect_end(name);

    auto elem = builder_.element(name.c_str());
    if (options != ItemNull) {
        elem.put("options", options);
    }
    elem.add(content);
    return elem.final();
}
```

### Memory Safety Notes

1. **Never use raw `new`/`malloc`** for data structures - always use MarkBuilder
2. **Strings are automatically arena-allocated** - no manual memory management
3. **Elements, maps, arrays are reference-counted** - safe to return/store
4. **MarkBuilder keeps Input\* reference** - ensure Input outlives parser

---

## Refactoring Plan

### Phase 0: File Organization

Create new files under `lambda/input/latex/` directory to split the monolithic parser:

```
lambda/input/
├── input-latex.cpp              # Entry point (thin wrapper, delegates to latex/)
└── latex/
    ├── latex_parser.hpp         # Main parser class declaration
    ├── latex_parser.cpp         # Main parser implementation, high-level rules
    ├── latex_primitives.cpp     # Character-level parsing (PEG.js style)
    ├── latex_commands.cpp       # Command parsing and dispatch
    ├── latex_environments.cpp   # Environment handling
    ├── latex_macros.cpp         # Macro definition and expansion
    ├── latex_sections.cpp       # Section hierarchy (tree-sitter style)
    ├── latex_registry.hpp       # Command/environment registries (header-only)
    └── latex_registry.cpp       # Registry initialization
```

The existing `input-latex.cpp` becomes a thin entry point:

```cpp
// lambda/input/input-latex.cpp (modified)
#include "latex/latex_parser.hpp"

void parse_latex(Input* input, const char* source, size_t len) {
    latex::LatexParser parser(input, source, len);
    input->root = parser.parse();
}
```

### Phase 1: Parser Class Extraction

Convert procedural code to class-based parser:

```cpp
// lambda/input/latex/latex_parser.hpp
#pragma once

namespace lambda {
namespace latex {

class LatexParser {
public:
    LatexParser(Input* input, const char* source, size_t len);

    // Main entry point
    Item parse();

private:
    // State
    Input* input_;
    InputContext ctx_;
    const char* source_;
    const char* pos_;
    const char* end_;
    LatexMode mode_;
    int depth_;

    // Group balancing (PEG.js feature)
    std::stack<int> balance_stack_;
    void start_balanced();
    bool is_balanced();
    void end_balanced();

    // Lookahead helpers
    char peek(int offset = 0) const { return pos_[offset]; }
    bool match(const char* str);
    bool match_keyword(const char* kw);

    // Parsing rules - see detailed implementation
    Item parse_document();
    Item parse_preamble();
    Item parse_body();
    // ... etc
};

} // namespace lambda
```

### Phase 2: Primitive Rules (PEG.js Inspired)

Implement character-level rules matching PEG.js grammar:

```cpp
// lambda/input/latex/latex_primitives.cpp

Item LatexParser::parse_primitive() {
    // PEG.js lines 102-125: primitive rule
    if (auto r = parse_char()) return r;
    if (auto r = parse_space()) return r;
    if (auto r = parse_hyphen()) return r;
    if (auto r = parse_digit()) return r;
    if (auto r = parse_punctuation()) return r;
    if (auto r = parse_quotes()) return r;
    if (auto r = parse_bracket()) return r;
    if (auto r = parse_nbsp()) return r;
    if (auto r = parse_ctrl_space()) return r;
    if (auto r = parse_diacritic()) return r;
    if (auto r = parse_ctrl_sym()) return r;
    if (auto r = parse_symbol()) return r;
    if (auto r = parse_charsym()) return r;
    if (auto r = parse_utf8_char()) return r;
    return ItemNull;
}

Item LatexParser::parse_ligature() {
    // PEG.js lines 986-989: ligatures
    static const char* ligatures[] = {
        "ffi", "ffl", "ff", "fi", "fl",  // Font ligatures
        "---", "--",                      // Dashes
        "``", "''", "!´", "?´",          // Quotes
        "<<", ">>",                       // Guillemets
        nullptr
    };
    for (const char** lig = ligatures; *lig; lig++) {
        if (match(*lig)) {
            return builder_.createLigature(*lig);
        }
    }
    return ItemNull;
}

Item LatexParser::parse_charsym() {
    // PEG.js lines 1021-1035: \char and ^^ notation
    if (match("\\symbol")) {
        // \symbol{num}
        expect('{');
        int code = parse_integer();
        expect('}');
        return char_from_code(code);
    }
    if (match("\\char")) {
        // \char98 (decimal), \char'77 (octal), \char"FF (hex)
        if (peek() == '\'') { advance(); return char_from_code(parse_octal()); }
        if (peek() == '"') { advance(); return char_from_code(parse_hex(2)); }
        return char_from_code(parse_integer());
    }
    if (match("^^^^")) {
        // ^^^^FFFF (4-digit hex)
        return char_from_code(parse_hex(4));
    }
    if (match("^^")) {
        // ^^FF (2-digit hex) or ^^c (charcode manipulation)
        if (isxdigit(peek()) && isxdigit(peek(1))) {
            return char_from_code(parse_hex(2));
        }
        // Single char: if code < 64, add 64; else subtract 64
        char c = advance();
        int code = (unsigned char)c;
        code = (code < 64) ? code + 64 : code - 64;
        return char_from_code(code);
    }
    return ItemNull;
}

Item LatexParser::parse_ctrl_space() {
    // PEG.js lines 946: control space
    // \<newline>, \<space>, or \ followed by space
    if (match("\\ ") || match("\\\n") || match("\\\t")) {
        return builder_.createBreakableSpace();  // ZWSP + space
    }
    return ItemNull;
}
```

### Phase 3: Section Hierarchy (tree-sitter Inspired)

Implement nested section parsing like tree-sitter-latex:

```cpp
// lambda/input/latex/latex_sections.cpp

Item LatexParser::parse_section_hierarchy() {
    // tree-sitter grammar.js lines 103-310: section hierarchy
    // Sections contain content until next section at same or higher level

    std::vector<Item> parts;
    while (!at_end()) {
        if (auto part = parse_part()) parts.push_back(part);
        else if (auto chap = parse_chapter()) parts.push_back(chap);
        else if (auto sect = parse_section()) parts.push_back(sect);
        else break;
    }
    return builder_.createSections(parts);
}

Item LatexParser::parse_section() {
    // Parse: \section[toc]{title} followed by content until next section
    if (!match("\\section")) return ItemNull;

    bool starred = match("*");

    Item toc_entry = ItemNull;
    if (peek() == '[') {
        advance(); // skip [
        toc_entry = parse_until(']');
        advance(); // skip ]
    }

    if (peek() != '{') {
        error("Expected { after \\section");
        return ItemNull;
    }
    advance();
    Item title = parse_until('}');
    advance();

    // Parse content until next section-level or higher command
    std::vector<Item> content;
    while (!at_end() && !is_section_command()) {
        // Can contain subsections
        if (auto sub = parse_subsection()) {
            content.push_back(sub);
        } else if (auto para = parse_paragraph_content()) {
            content.push_back(para);
        }
    }

    return builder_.createSection(title, toc_entry, content, starred);
}

bool LatexParser::is_section_command() {
    // Check if we're at a section-level or higher command
    const char* section_cmds[] = {
        "\\part", "\\chapter", "\\section",
        "\\end{document}", nullptr
    };
    for (const char** cmd = section_cmds; *cmd; cmd++) {
        if (lookahead(*cmd)) return true;
    }
    return false;
}
```

### Phase 4: Macro System

Implement macro definition and expansion:

```cpp
// lambda/input/latex/latex_macros.cpp

struct MacroDefinition {
    std::string name;
    int arg_count;
    std::string default_arg;  // For optional first argument
    std::string body;
    bool is_long;             // Can span paragraphs
};

class MacroRegistry {
    std::unordered_map<std::string, MacroDefinition> macros_;

public:
    void define(const MacroDefinition& macro);
    bool has(const std::string& name) const;
    const MacroDefinition* get(const std::string& name) const;
    std::string expand(const std::string& name, const std::vector<std::string>& args);
};

Item LatexParser::parse_newcommand() {
    // \newcommand{\name}[argc][default]{body}
    // \newcommand*{\name}[argc][default]{body}
    // \renewcommand, \providecommand, etc.

    bool starred = match("*");
    skip_whitespace();

    // Parse command name
    expect('{');
    expect('\\');
    std::string name = parse_identifier();
    expect('}');

    // Parse optional argument count
    int argc = 0;
    std::string default_arg;
    if (peek() == '[') {
        advance();
        argc = parse_integer();
        expect(']');

        // Parse optional default for first argument
        if (peek() == '[') {
            advance();
            default_arg = parse_balanced_until(']');
            expect(']');
        }
    }

    // Parse body
    expect('{');
    std::string body = parse_balanced_braces();

    // Register the macro
    macros_.define({name, argc, default_arg, body, !starred});

    return ItemNull;  // Definition produces no output
}

Item LatexParser::expand_macro(const std::string& name) {
    const MacroDefinition* macro = macros_.get(name);
    if (!macro) return ItemNull;

    // Parse arguments
    std::vector<std::string> args;
    for (int i = 0; i < macro->arg_count; i++) {
        if (i == 0 && !macro->default_arg.empty() && peek() != '{') {
            // Use default for optional first argument
            args.push_back(macro->default_arg);
        } else {
            expect('{');
            args.push_back(parse_balanced_braces());
        }
    }

    // Expand body with arguments
    std::string expanded = macros_.expand(name, args);

    // Parse the expanded text
    return parse_string(expanded);
}
```

### Phase 5: Environment Registry

Comprehensive environment handling:

```cpp
// lambda/input/latex/latex_environments.cpp

enum EnvType {
    ENV_GENERIC,        // Standard environment
    ENV_MATH,           // Math environments (equation, align, etc.)
    ENV_VERBATIM,       // Verbatim content (verbatim, lstlisting, etc.)
    ENV_LIST,           // List environments (itemize, enumerate, description)
    ENV_TABULAR,        // Table environments
    ENV_FIGURE,         // Float environments
    ENV_THEOREM,        // Theorem-like environments
};

struct EnvironmentSpec {
    const char* name;
    EnvType type;
    const char* arg_spec;   // Arguments after \begin{name}
    bool takes_options;     // Has [options] after name
};

static const EnvironmentSpec ENVIRONMENTS[] = {
    // Math environments (from tree-sitter grammar.js lines 605-650)
    {"math", ENV_MATH, "", false},
    {"displaymath", ENV_MATH, "", false},
    {"equation", ENV_MATH, "", false},
    {"equation*", ENV_MATH, "", false},
    {"align", ENV_MATH, "", false},
    {"align*", ENV_MATH, "", false},
    {"aligned", ENV_MATH, "", false},
    {"gather", ENV_MATH, "", false},
    {"split", ENV_MATH, "", false},
    {"multline", ENV_MATH, "", false},
    {"eqnarray", ENV_MATH, "", false},
    {"array", ENV_MATH, "cols", false},

    // Verbatim environments (from tree-sitter external scanner)
    {"verbatim", ENV_VERBATIM, "", false},
    {"lstlisting", ENV_VERBATIM, "", true},
    {"minted", ENV_VERBATIM, "g", true},  // {language}
    {"comment", ENV_VERBATIM, "", false},
    {"luacode", ENV_VERBATIM, "", false},
    {"pycode", ENV_VERBATIM, "", false},

    // List environments
    {"itemize", ENV_LIST, "", true},
    {"enumerate", ENV_LIST, "", true},
    {"description", ENV_LIST, "", true},

    // Tabular environments
    {"tabular", ENV_TABULAR, "cols", true},
    {"tabular*", ENV_TABULAR, "l cols", true},  // width, cols
    {"longtable", ENV_TABULAR, "cols", true},

    // Float environments
    {"figure", ENV_FIGURE, "", true},
    {"figure*", ENV_FIGURE, "", true},
    {"table", ENV_FIGURE, "", true},
    {"table*", ENV_FIGURE, "", true},

    // Theorem environments (defined by \newtheorem)
    // These are dynamically registered

    // Document structure
    {"document", ENV_GENERIC, "", false},
    {"abstract", ENV_GENERIC, "", false},
    {"center", ENV_GENERIC, "", false},
    {"flushleft", ENV_GENERIC, "", false},
    {"flushright", ENV_GENERIC, "", false},
    {"quote", ENV_GENERIC, "", false},
    {"quotation", ENV_GENERIC, "", false},
    {"verse", ENV_GENERIC, "", false},

    {nullptr, ENV_GENERIC, "", false}
};

Item LatexParser::parse_environment() {
    if (!match("\\begin{")) return ItemNull;

    std::string name = parse_identifier();
    bool starred = match("*");
    expect('}');

    std::string full_name = name + (starred ? "*" : "");
    const EnvironmentSpec* spec = find_environment(full_name.c_str());

    // Parse environment options/arguments
    Item options = ItemNull;
    if (spec && spec->takes_options && peek() == '[') {
        advance();
        options = parse_until(']');
        advance();
    }

    std::vector<Item> args;
    if (spec) {
        args = parse_env_args(spec->arg_spec);
    }

    // Parse content based on environment type
    Item content;
    if (spec && spec->type == ENV_VERBATIM) {
        content = parse_verbatim_content(full_name);
    } else if (spec && spec->type == ENV_MATH) {
        content = parse_math_content();
    } else if (spec && spec->type == ENV_LIST) {
        content = parse_list_content();
    } else {
        content = parse_environment_content(full_name);
    }

    // Expect closing
    if (!match("\\end{")) {
        error("Expected \\end{%s}", full_name.c_str());
    }
    std::string end_name = parse_identifier();
    if (starred) match("*");
    expect('}');

    if (end_name != name) {
        error("Mismatched environment: \\begin{%s} ... \\end{%s}",
              full_name.c_str(), end_name.c_str());
    }

    return builder_.createEnvironment(full_name, options, args, content);
}
```

---

## Implementation Phases

### Phase 0: Directory & Build Setup (Day 1)

1. Create `lambda/input/latex/` directory
2. Update `build_lambda_config.json` to include new source files:
   ```json
   {
     "sources": [
       "lambda/input/latex/latex_parser.cpp",
       "lambda/input/latex/latex_primitives.cpp",
       "lambda/input/latex/latex_commands.cpp",
       "lambda/input/latex/latex_environments.cpp",
       "lambda/input/latex/latex_macros.cpp",
       "lambda/input/latex/latex_sections.cpp",
       "lambda/input/latex/latex_registry.cpp"
     ]
   }
   ```
3. Run `make rebuild` to verify build system picks up new files

### Phase 1: Foundation (Week 1-2)
1. Create `LatexParser` class with basic state management
2. Extract primitive rules from current code
3. Implement mode tracking
4. Add source position tracking to `InputContext`

### Phase 2: Primitives & Characters (Week 3)
1. Implement PEG.js-style primitive rules
2. Add `\char`, `^^XX`, `^^^^XXXX` support
3. Implement control space `\ ` properly
4. Add ligature handling
5. Fix whitespace handling edge cases

### Phase 3: Commands & Registry (Week 4-5)
1. Create command registry with metadata
2. Refactor `parse_latex_command()` to use registry
3. Add argument parsing based on specs
4. Implement mode-aware command handling

### Phase 4: Environments (Week 6)
1. Create environment registry
2. Implement specialized environment parsers
3. Add verbatim environment support (external scanner style)
4. Handle nested environments

### Phase 5: Sections & Hierarchy (Week 7)
1. Implement tree-sitter style section parsing
2. Add paragraph/subparagraph handling
3. Handle section numbering
4. Add table of contents support

### Phase 6: Macros (Week 8)
1. Implement `\newcommand`, `\renewcommand`
2. Add `\def`, `\gdef`, `\edef` support
3. Implement macro expansion
4. Handle nested macro calls

### Phase 7: Advanced Features (Week 9-10)
1. Label/reference system
2. Citation support
3. Counter expressions
4. Length calculations
5. Error recovery

### Phase 8: Testing & Polish (Week 11-12)
1. Comprehensive test coverage
2. Performance optimization
3. Documentation
4. Migration of existing tests

---

## Grammar Feature Matrix

### Comparison: PEG.js vs tree-sitter vs Target

| Feature | PEG.js | tree-sitter | Target | Priority |
|---------|--------|-------------|--------|----------|
| **Primitives** |
| Character literals | ✅ | ✅ | ✅ | P0 |
| Ligatures (ff, fi, ---, etc.) | ✅ | ❌ | ✅ | P1 |
| Control symbols (\$, \%, etc.) | ✅ | ✅ | ✅ | P0 |
| Diacritics (\', \`, etc.) | ✅ | ❌ | ✅ | P0 |
| `\char`, `^^XX` | ✅ | ❌ | ✅ | P1 |
| UTF-8 characters | ✅ | ✅ | ✅ | P0 |
| **Whitespace** |
| Space normalization | ✅ | Basic | ✅ | P0 |
| Control space `\ ` | ✅ | ❌ | ✅ | P0 |
| Non-breaking space `~` | ✅ | ❌ | ✅ | P0 |
| Paragraph breaks | ✅ | ✅ | ✅ | P0 |
| **Mode Tracking** |
| H/V/HV/P modes | ✅ | ❌ | ✅ | P1 |
| Mode-specific commands | ✅ | ❌ | ✅ | P2 |
| Group balancing | ✅ | ✅ | ✅ | P0 |
| **Commands** |
| Generic commands | Basic | ✅ 200+ | ✅ | P0 |
| Symbol commands | ✅ | ✅ | ✅ | P0 |
| Font commands | ✅ | ✅ | ✅ | P0 |
| Spacing commands | ✅ | ✅ | ✅ | P0 |
| Counter commands | ❌ | ✅ | ✅ | P0 ✅ Done |
| Reference commands | ✅ | ✅ 30+ | ✅ | P1 |
| Citation commands | ✅ | ✅ 50+ | ✅ | P2 |
| **Environments** |
| Generic environments | ✅ | ✅ | ✅ | P0 |
| Math environments | ✅ | ✅ 20+ | Keep as-is | — Excluded |
| Verbatim environments | ✅ | ✅ | ✅ | P1 |
| List environments | ✅ | ✅ | ✅ | P0 |
| Tabular environments | ✅ | ✅ | ✅ | P1 |
| Float environments | ✅ | ✅ | ✅ | P2 |
| **Sections** |
| Section commands | Basic | ✅ Nested | ✅ | P1 |
| Hierarchy tracking | ❌ | ✅ | ✅ | P1 |
| TOC entries | ✅ | ✅ | ✅ | P2 |
| **Macros** |
| `\newcommand` | ❌ | ✅ | ✅ | P1 |
| `\def`, `\gdef` | ❌ | ✅ | ✅ | P2 |
| xparse (`\NewDocumentCommand`) | ❌ | ✅ | ✅ | P3 |
| Macro expansion | ❌ | ❌ | ✅ | P1 |
| **Math** (Excluded from this plan - keep current implementation) |
| Inline `$...$` | ✅ | ✅ | Keep as-is | — |
| Display `$$...$$` | ✅ | ✅ | Keep as-is | — |
| `\[...\]`, `\(...\)` | ✅ | ✅ | Keep as-is | — |
| Subscript/superscript | ✅ | ✅ | Keep as-is | — |
| Math delimiters | ✅ | ✅ | Keep as-is | — |
| **Error Handling** |
| Error recovery | ❌ | ✅ | ✅ | P2 |
| Source positions | Basic | ✅ | ✅ | P1 |
| Error messages | Basic | ✅ | ✅ | P1 |

**Priority Legend:**
- P0: Must have for basic functionality
- P1: Important for comprehensive parsing
- P2: Nice to have for advanced documents
- P3: Future enhancements
- — Excluded: Out of scope for this enhancement

---

## Detailed Implementation Guide

### 1. Character Handling (PEG.js lines 976-1035)

```cpp
// Characters and symbols - implement these rules from PEG.js

// char: [a-z]i (catcode 11)
Item LatexParser::parse_char() {
    if (isalpha(peek())) {
        return builder_.createChar(advance());
    }
    return ItemNull;
}

// digit: [0-9] (catcode 12)
Item LatexParser::parse_digit() {
    if (isdigit(peek())) {
        return builder_.createChar(advance());
    }
    return ItemNull;
}

// punctuation: [.,;:\*/()!?=+<>] (catcode 12)
Item LatexParser::parse_punctuation() {
    if (strchr(".,;:*/()!?=+<>", peek())) {
        return builder_.createChar(advance());
    }
    return ItemNull;
}

// quotes: [`'] (catcode 12) - smart quote conversion
Item LatexParser::parse_quotes() {
    if (peek() == '`') {
        advance();
        return builder_.createString("\u2018");  // '
    }
    if (peek() == '\'') {
        advance();
        return builder_.createString("\u2019");  // '
    }
    return ItemNull;
}

// utf8_char: any non-special UTF-8 character
Item LatexParser::parse_utf8_char() {
    // Skip special characters
    if (strchr(" \t\n\r\\{}$&#^_%~[]", peek())) {
        return ItemNull;
    }

    unsigned char c = (unsigned char)peek();
    if ((c & 0x80) == 0) {
        // ASCII
        return builder_.createChar(advance());
    }

    // Multi-byte UTF-8
    int bytes = 1;
    if ((c & 0xE0) == 0xC0) bytes = 2;
    else if ((c & 0xF0) == 0xE0) bytes = 3;
    else if ((c & 0xF8) == 0xF0) bytes = 4;

    char buf[5] = {0};
    for (int i = 0; i < bytes && !at_end(); i++) {
        buf[i] = advance();
    }
    return builder_.createString(buf);
}
```

### 2. Whitespace & Space Handling (PEG.js lines 933-968)

```cpp
// Space handling following PEG.js semantics

// skip_space: skip spaces without consuming newlines (for paragraph detection)
void LatexParser::skip_space() {
    while (peek() == ' ' || peek() == '\t' || is_comment()) {
        if (is_comment()) {
            skip_comment();
        } else {
            advance();
        }
    }
}

// skip_all_space: skip all whitespace including newlines
void LatexParser::skip_all_space() {
    while (isspace(peek()) || is_comment()) {
        if (is_comment()) {
            skip_comment();
        } else {
            advance();
        }
    }
}

// space: significant space that becomes output
Item LatexParser::parse_space() {
    // Don't produce space at paragraph breaks
    if (is_paragraph_break()) return ItemNull;

    // Skip if followed by linebreak command
    if (lookahead_linebreak()) return ItemNull;

    // Skip if followed by vertical mode command
    if (lookahead_vmode_command()) return ItemNull;

    if (peek() == ' ' || peek() == '\t' || peek() == '\n') {
        advance();
        // Collapse multiple spaces/newlines
        while (peek() == ' ' || peek() == '\t' || peek() == '\n') {
            advance();
        }
        return builder_.createBreakableSpace();  // g.brsp in PEG.js
    }
    return ItemNull;
}

// ctrl_space: \<space>, \<newline>, or just \ followed by space
Item LatexParser::parse_ctrl_space() {
    // PEG.js line 946: escape (&nl &break / nl / sp)
    if (pos_[0] == '\\' && (pos_[1] == ' ' || pos_[1] == '\n' || pos_[1] == '\t')) {
        advance(); advance();
        return builder_.createBreakableSpace();
    }
    return ItemNull;
}

// nbsp: ~ produces non-breaking space
Item LatexParser::parse_nbsp() {
    if (peek() == '~') {
        advance();
        return builder_.createNbsp();  // U+00A0
    }
    return ItemNull;
}

// break: paragraph break detection
bool LatexParser::is_paragraph_break() {
    // PEG.js lines 950-960
    const char* p = pos_;

    // Skip spaces
    while (*p == ' ' || *p == '\t') p++;

    // Must have newline
    if (*p != '\n') return false;
    p++;

    // Skip spaces and comments
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '%') {
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }

    // Must have another newline
    return *p == '\n';
}
```

### 3. Argument Parsing (PEG.js lines 225-450)

```cpp
// Argument parsing following PEG.js argument spec format

struct ArgSpec {
    char type;      // g=group, o=optional, s=star, i=identifier, l=length, n=number, etc.
    bool optional;  // Ends with ?
};

std::vector<ArgSpec> parse_arg_spec(const char* spec) {
    std::vector<ArgSpec> args;
    while (*spec) {
        ArgSpec arg;
        arg.type = *spec++;
        arg.optional = (*spec == '?');
        if (arg.optional) spec++;
        args.push_back(arg);
        if (*spec == ' ') spec++;  // Skip separator
    }
    return args;
}

std::vector<Item> LatexParser::parse_command_args(const char* spec) {
    std::vector<Item> result;
    auto args = parse_arg_spec(spec);

    for (const auto& arg : args) {
        Item parsed = ItemNull;

        switch (arg.type) {
        case 's':  // Star
            parsed = match("*") ? builder_.createBool(true) : builder_.createBool(false);
            break;

        case 'g':  // Required group {content}
            skip_space();
            if (peek() == '{') {
                advance();
                parsed = parse_balanced_content('}');
                expect('}');
            } else if (!arg.optional) {
                error("Expected {");
            }
            break;

        case 'o':  // Optional group [content]
            skip_space();
            if (peek() == '[') {
                advance();
                parsed = parse_balanced_content(']');
                expect(']');
            }
            break;

        case 'i':  // Identifier {name}
            skip_space();
            if (peek() == '{') {
                advance();
                skip_space();
                std::string id = parse_identifier();
                skip_space();
                expect('}');
                parsed = builder_.createString(id);
            } else if (!arg.optional) {
                error("Expected {identifier}");
            }
            break;

        case 'n':  // Number expression {num}
            skip_space();
            if (peek() == '{') {
                advance();
                parsed = parse_num_expr();
                expect('}');
            } else if (!arg.optional) {
                error("Expected {number}");
            }
            break;

        case 'l':  // Length {12pt}
            skip_space();
            if (peek() == '{') {
                advance();
                parsed = parse_length();
                expect('}');
            } else if (!arg.optional) {
                error("Expected {length}");
            }
            break;

        case 'h':  // Horizontal content (restricted)
            enter_mode(MODE_RESTRICTED_H);
            parsed = parse_horizontal();
            exit_mode();
            break;

        // More argument types...
        }

        result.push_back(parsed);
    }

    return result;
}
```

### 4. Expression Evaluation (PEG.js lines 482-525)

```cpp
// Numeric expression evaluation (already implemented in counter system)
// Extend for general use

// \value{counter} - get counter value
int LatexParser::parse_value() {
    if (!match("\\value")) return 0;
    expect('{');
    std::string name = parse_identifier();
    expect('}');
    return get_counter(name);
}

// \real{float} - parse float
double LatexParser::parse_real() {
    if (!match("\\real")) return 0.0;
    expect('{');
    skip_space();
    double f = parse_float();
    skip_space();
    expect('}');
    return f;
}

// num_expr: arithmetic expression
int LatexParser::parse_num_expr() {
    // Already implemented in counter system
    // term ((+|-) term)*
    int result = parse_num_term();
    while (true) {
        skip_space();
        if (match("+")) {
            skip_space();
            result += parse_num_term();
        } else if (match("-")) {
            skip_space();
            result -= parse_num_term();
        } else {
            break;
        }
    }
    return result;
}

int LatexParser::parse_num_term() {
    // factor ((* | /) factor)*
    int result = parse_num_factor();
    while (true) {
        skip_space();
        if (match("*")) {
            skip_space();
            result = (int)(result * parse_num_factor());
        } else if (match("/")) {
            skip_space();
            int divisor = parse_num_factor();
            if (divisor != 0) result = (int)(result / divisor);
        } else {
            break;
        }
    }
    return result;
}

int LatexParser::parse_num_factor() {
    skip_space();

    // Unary +/-
    if (match("-")) {
        return -parse_num_factor();
    }
    if (match("+")) {
        return parse_num_factor();
    }

    // Parentheses
    if (match("(")) {
        int result = parse_num_expr();
        expect(")");
        return result;
    }

    // \value{counter}
    if (lookahead("\\value")) {
        return parse_value();
    }

    // \real{float}
    if (lookahead("\\real")) {
        return (int)parse_real();
    }

    // Integer
    return parse_integer();
}
```

### 5. Length Parsing (PEG.js lines 348-380)

```cpp
// Length parsing for dimensions

struct Length {
    double value;
    std::string unit;  // sp, pt, px, dd, mm, pc, cc, cm, in, ex, em

    Length operator*(double factor) const { return {value * factor, unit}; }
};

Length LatexParser::parse_length() {
    skip_space();
    double value = parse_float();
    skip_space();

    std::string unit = parse_length_unit();

    // Optional plus/minus (rubber lengths)
    // plus <length> minus <length>
    skip_space();
    if (match("plus")) {
        skip_space();
        parse_float();
        parse_length_unit();
    }
    skip_space();
    if (match("minus")) {
        skip_space();
        parse_float();
        parse_length_unit();
    }

    return {value, unit};
}

std::string LatexParser::parse_length_unit() {
    static const char* units[] = {
        "sp", "pt", "px", "dd", "mm", "pc", "cc", "cm", "in", "ex", "em",
        nullptr
    };

    for (const char** u = units; *u; u++) {
        if (match_word(*u)) {
            return *u;
        }
    }

    error("Expected length unit");
    return "pt";
}
```

---

## Testing Strategy

### Unit Tests

```cpp
// test/test_latex_parser_gtest.cpp

// Primitive tests
TEST(LatexParser, CharSymbol) {
    auto result = parse_latex("\\char98");
    EXPECT_EQ(get_text(result), "b");
}

TEST(LatexParser, HexCharShort) {
    auto result = parse_latex("^^41");
    EXPECT_EQ(get_text(result), "A");
}

TEST(LatexParser, HexCharLong) {
    auto result = parse_latex("^^^^0041");
    EXPECT_EQ(get_text(result), "A");
}

TEST(LatexParser, ControlSpace) {
    auto result = parse_latex("a\\ b");
    EXPECT_EQ(get_text(result), "a\u200B b");  // ZWSP + space
}

TEST(LatexParser, Ligatures) {
    auto result = parse_latex("---");
    EXPECT_EQ(get_text(result), "—");  // em-dash

    result = parse_latex("``quoted''");
    EXPECT_EQ(get_text(result), ""quoted"");
}

// Environment tests
TEST(LatexParser, NestedEnvironments) {
    auto result = parse_latex(R"(
        \begin{center}
            \begin{tabular}{cc}
                a & b \\
            \end{tabular}
        \end{center}
    )");
    // Verify structure
}

// Macro tests
TEST(LatexParser, NewCommand) {
    auto result = parse_latex(R"(
        \newcommand{\hello}[1]{Hello, #1!}
        \hello{World}
    )");
    EXPECT_EQ(get_text(result), "Hello, World!");
}

// Section hierarchy tests
TEST(LatexParser, SectionHierarchy) {
    auto result = parse_latex(R"(
        \section{One}
        Content
        \subsection{One-A}
        More content
        \section{Two}
        Even more
    )");
    // Verify tree structure
}
```

### Fixture Tests

Expand the existing `test/latex/fixtures/` with:
- `charsym.tex` - Character and symbol tests
- `ligatures.tex` - Ligature tests
- `macros.tex` - Macro definition/expansion tests
- `sections.tex` - Section hierarchy tests
- `environments.tex` - Environment tests

### Regression Tests

Run all existing tests after each phase to ensure backwards compatibility.

---

## Leveraging tree-sitter-latex Test Corpus

The tree-sitter-latex project has an extensive test corpus at `test/corpus/*.txt` containing ~100+ test cases covering commands, environments, sections, and edge cases. We can leverage these tests to validate our parser behavior.

### tree-sitter Test Format

Each test file uses a simple format:
```
================================================================================
Test Name Description
================================================================================

\LaTeX input here

--------------------------------------------------------------------------------

(source_file
  (expected_parse_tree
    (in_s_expression_format)))
```

### Available Test Files

| File | Description | Applicable Tests |
|------|-------------|------------------|
| `text.txt` | Basic text, words, punctuation | ✅ All (no math) |
| `commands.txt` | Generic commands, arguments | ✅ All |
| `counters.txt` | Counter operations | ✅ All (already implemented) |
| `environments.txt` | Various environments | ✅ Non-math environments |
| `sections.txt` | Section hierarchy | ✅ All |
| `includes.txt` | Package/file includes | ✅ All |
| `groups.txt` | Curly group handling | ✅ All |
| `trivia.txt` | Comments, whitespace | ✅ All |
| `math.txt` | Math mode tests | ❌ Skip (out of scope) |
| `issues.txt` | Regression tests | ✅ Non-math cases |

### Conversion Strategy

1. **Manual review**: Review each test file and identify applicable tests
2. **Skip math tests**: Any test involving `$...$`, `$$...$$`, math environments
3. **Convert to GTest**: Transform applicable tests to Lambda's test format

### Conversion Example

**tree-sitter format** (`test/corpus/text.txt`):
```
================================================================================
Hello world
================================================================================

Hello World!

--------------------------------------------------------------------------------

(source_file
  (text
    (word)
    (word)))
```

**Lambda GTest format** (`test/test_latex_parser_gtest.cpp`):
```cpp
TEST(LatexParser, HelloWorld) {
    auto result = parse_latex("Hello World!");

    // Verify structure using MarkReader
    MarkReader reader(result);
    EXPECT_EQ(reader.tag(), "document");

    auto text = reader.child(0);
    EXPECT_EQ(text.tag(), "text");
    EXPECT_TRUE(text.hasChildren());
}
```

**Lambda fixture format** (`test/latex/fixtures/text_hello.tex`):
```latex
Hello World!
```
With expected output in `test/latex/fixtures/text_hello.json`:
```json
{
  "#": "document",
  "children": [
    { "#": "text", "content": "Hello World!" }
  ]
}
```

### Batch Conversion Script

Create a Python script to automate conversion:

```python
#!/usr/bin/env python3
# utils/convert_ts_latex_tests.py

import re
import sys

def parse_ts_test_file(content):
    """Parse tree-sitter test format into test cases."""
    tests = []
    pattern = r'={80}\n(.+?)\n={80}\n\n(.+?)\n\n-{80}\n\n\(source_file\n(.+?)\n\)'

    for match in re.finditer(pattern, content, re.DOTALL):
        name = match.group(1).strip()
        input_text = match.group(2).strip()
        expected_tree = match.group(3).strip()

        # Skip math tests
        if any(m in input_text for m in ['$', '\\[', '\\]', '\\(']):
            continue
        if 'math' in name.lower() or 'equation' in name.lower():
            continue

        tests.append({
            'name': name,
            'input': input_text,
            'tree': expected_tree
        })

    return tests

def generate_gtest(test):
    """Generate C++ GTest from test case."""
    safe_name = re.sub(r'[^a-zA-Z0-9]', '_', test['name'])
    return f'''
TEST(LatexParser, {safe_name}) {{
    // Input: {test['input'][:50]}...
    auto result = parse_latex(R"({test['input']})");
    EXPECT_NE(result, ItemNull);
    // TODO: Verify structure matches expected tree
}}
'''

# Usage: python convert_ts_latex_tests.py test/corpus/text.txt
```

### Recommended Test Coverage Order

1. **Phase 1**: `text.txt`, `groups.txt`, `trivia.txt` (basic primitives)
2. **Phase 2**: `commands.txt` (command parsing)
3. **Phase 3**: `counters.txt` (validate existing counter tests)
4. **Phase 4**: `sections.txt` (section hierarchy)
5. **Phase 5**: `environments.txt` (non-math environments)
6. **Phase 6**: `includes.txt` (package handling)
7. **Phase 7**: `issues.txt` (regression edge cases)

### Expected Test Count

From tree-sitter-latex corpus:
- `text.txt`: ~8 tests (all applicable)
- `commands.txt`: ~25 tests (all applicable)
- `counters.txt`: ~10 tests (all applicable)
- `environments.txt`: ~15 tests (~10 applicable, skip math)
- `sections.txt`: ~6 tests (all applicable)
- `includes.txt`: ~12 tests (all applicable)
- `groups.txt`: ~3 tests (all applicable)
- `trivia.txt`: ~4 tests (all applicable)
- `issues.txt`: ~50 tests (~35 applicable, skip math)

**Total: ~110 applicable tests** from tree-sitter corpus that can be converted.

---

## Migration Path

### Phase 1: Parallel Implementation
- Keep existing `parse_latex()` functional
- Implement new parser in parallel
- Use feature flag to switch between parsers

### Phase 2: Gradual Migration
- Start with primitives (lowest risk)
- Move to environments
- Finally, migrate command handling

### Phase 3: Validation
- Run extensive test suite
- Compare output between old and new parser
- Fix discrepancies

### Phase 4: Cutover
- Remove old parser code
- Update documentation
- Release new parser

---

## Conclusion

This plan provides a roadmap to transform the current monolithic LaTeX parser into a structured, extensible, and comprehensive parser that combines the best features from both LaTeX.js (PEG.js) and tree-sitter-latex grammars.

### Key Design Decisions

1. **MarkBuilder for Output**: All parsed output is constructed using Lambda's `MarkBuilder` API, ensuring proper memory management and consistent data structures.

2. **Math Module Excluded**: Math parsing (`$...$`, `$$...$$`, `\[...\]`, math environments) remains unchanged. This plan focuses on text mode parsing improvements.

3. **Test-Driven Development**: Leverage the tree-sitter-latex test corpus (~110 applicable tests) as a reference implementation, converting them to Lambda's test format.

### Target Features

The modular architecture will support:
- Multiple output formats (HTML, Markdown, AST)
- Error recovery and reporting
- Source position tracking
- Macro expansion (`\newcommand`, `\def`)
- Comprehensive command coverage (200+ commands from tree-sitter)
- Section hierarchy tracking
- Mode-aware parsing (H/V/HV/P modes from PEG.js)

### Implementation Timeline

| Phase | Duration | Focus |
|-------|----------|-------|
| 1-2 | 2 weeks | Foundation, primitives, mode tracking |
| 3 | 1 week | Primitives & characters (PEG.js features) |
| 4-5 | 2 weeks | Commands & registry |
| 6 | 1 week | Environments |
| 7 | 1 week | Sections & hierarchy |
| 8 | 1 week | Macros |
| 9-10 | 2 weeks | Advanced features |
| 11-12 | 2 weeks | Testing & polish |

**Total: ~12 weeks** for full implementation.

Implementation should proceed in phases, with each phase adding well-tested functionality while maintaining backwards compatibility with existing tests.
