# LaTeX to HTML Formatter Rewrite - Comprehensive Proposal

**Date**: December 11, 2025  
**Objective**: Rewrite LaTeX-to-HTML formatter from scratch with literal translation from latex.js  
**Target**: Integration with tree-sitter based LaTeX parser (`input-latex-ts.cpp`)

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Architecture Analysis](#architecture-analysis)
3. [HtmlWriter Design](#htmlwriter-design)
4. [File-by-File Translation Plan](#file-by-file-translation-plan)
5. [Implementation Roadmap](#implementation-roadmap)
6. [Testing Strategy](#testing-strategy)

---

## Executive Summary

### Goals

1. **Literal Translation**: Translate latex.js formatting logic file-by-file, function-by-function for easy comparison
2. **Tree-sitter Integration**: Work with existing `input-latex-ts.cpp` parser output (Lambda Element tree)
3. **Dual Output Modes**: Support both text-mode (HTML string) and node-mode (Lambda tree) via HtmlWriter
4. **Memory Efficiency**: Use `strbuf` (not `stringbuf`) for string building
5. **Design Excellence**: Incorporate proven patterns from `format-latex-html.cpp`

### Key Innovation: HtmlWriter Abstraction

```cpp
// Unified interface for two output modes
class HtmlWriter {
public:
    virtual void writeText(const char* text) = 0;
    virtual void openTag(const char* tag, const char* classes = nullptr) = 0;
    virtual void closeTag(const char* tag) = 0;
    virtual void writeElement(const char* tag, const char* content, const char* classes = nullptr) = 0;
    // ... more methods
};

// Text mode: generates HTML strings
class TextHtmlWriter : public HtmlWriter { /* uses strbuf */ };

// Node mode: generates Lambda Element tree
class NodeHtmlWriter : public HtmlWriter { /* uses MarkBuilder */ };
```

---

## Architecture Analysis

### latex.js Architecture Overview

**Source Repository**: https://github.com/michael-brade/LaTeX.js

**Core Components**:

1. **Parser** (`latex-parser.pegjs`): PEG.js parser → AST
2. **Generator Base** (`generator.ls`): Abstract generator with state management
3. **HtmlGenerator** (`html-generator.ls`): HTML-specific output generation
4. **Macros/Commands** (`latex.ltx.ls`): LaTeX command implementations (~1400 lines)
5. **Document Classes** (`documentclasses/*.ls`): article, book, report
6. **Packages** (`packages/*.ls`): Additional LaTeX packages
7. **Symbols** (`symbols.ls`): Unicode symbol mappings, diacritics, ligatures
8. **CSS Stylesheets** (`css/*.css`): Document styling (base.css, article.css, etc.)

**Data Flow**:
```
LaTeX Source 
  → PEG Parser 
  → AST 
  → Generator.macro(command_name, args) 
  → HtmlGenerator.create(element_type) 
  → DOM Tree 
  → HTML Document
```

**Key Design Patterns**:
- **State Stack**: Generator maintains stack of scopes (groups, environments)
- **Counter System**: Global counters (chapter, section, figure, etc.) with parent-child relationships
- **Label/Reference**: Two-pass resolution or deferred reference filling
- **Font Context**: Stack-based font state management
- **Length System**: LaTeX dimensions (pt, em, cm, etc.) → CSS values
- **Macro Expansion**: User-defined macros with parameter substitution

### Current Lambda Architecture

**Source Files**:
- Parser: `lambda/input/input-latex-ts.cpp` (tree-sitter based, ~500 lines)
- Formatter: `lambda/format/format-latex-html.cpp` (~6300 lines)

**Data Flow**:
```
LaTeX Source 
  → Tree-sitter Parser 
  → CST (Concrete Syntax Tree) 
  → input-latex-ts.cpp 
  → Lambda Element Tree 
  → format-latex-html.cpp 
  → HTML String + CSS String
```

**Lambda Element Tree Structure** (from parser output):
```cpp
Element("latex_document")           // Root
├─ Element("textbf")                 // Commands become Elements
│  └─ String("Hello")                //   with arguments as children
├─ Symbol("quad")                    // Leaf commands become Symbols
├─ Element("itemize")                // Environments become Elements
│  └─ Element("item")                //   with nested structure
│     └─ String("First item")
└─ String("Regular text")            // Plain text
```

**Key Insight**: Lambda parser already produces a structured Element tree similar to latex.js AST, making translation straightforward.

---

## HtmlWriter Design

### Requirements

1. **Dual Output Modes**:
   - **Text Mode**: Generate HTML as string (for CLI, file output)
   - **Node Mode**: Generate HTML as Lambda Element tree (for further processing, testing)

2. **API Uniformity**: Same code generates both outputs via polymorphism

3. **Memory Efficiency**: Use `strbuf` for text mode (stack-allocated, reusable buffer)

### Interface Definition

```cpp
// lambda/format/html_writer.hpp

#ifndef HTML_WRITER_HPP
#define HTML_WRITER_HPP

#include "../../lib/strbuf.h"
#include "../mark_builder.hpp"

namespace lambda {

// Abstract base class for HTML generation
class HtmlWriter {
public:
    virtual ~HtmlWriter() = default;
    
    // Text output
    virtual void writeText(const char* text, size_t len = 0) = 0;
    virtual void writeRawHtml(const char* html) = 0;
    
    // Element creation
    virtual void openTag(const char* tag, const char* classes = nullptr, 
                         const char* id = nullptr, const char* style = nullptr) = 0;
    virtual void closeTag(const char* tag) = 0;
    virtual void writeSelfClosingTag(const char* tag, const char* classes = nullptr, 
                                     const char* attrs = nullptr) = 0;
    
    // Convenience methods
    virtual void writeElement(const char* tag, const char* content, 
                             const char* classes = nullptr) = 0;
    virtual void writeAttribute(const char* name, const char* value) = 0;
    
    // Indentation control (for text mode pretty-printing)
    virtual void indent() = 0;
    virtual void unindent() = 0;
    virtual void newline() = 0;
    
    // Output retrieval
    virtual Item getResult() = 0;  // For node mode: returns Element tree
    virtual const char* getHtml() = 0;  // For text mode: returns HTML string
};

// Text mode implementation: generates HTML strings
class TextHtmlWriter : public HtmlWriter {
private:
    StrBuf* buf_;
    int indent_level_;
    bool pretty_print_;
    Pool* pool_;
    
public:
    TextHtmlWriter(Pool* pool, bool pretty_print = false);
    ~TextHtmlWriter() override;
    
    void writeText(const char* text, size_t len = 0) override;
    void writeRawHtml(const char* html) override;
    void openTag(const char* tag, const char* classes = nullptr, 
                const char* id = nullptr, const char* style = nullptr) override;
    void closeTag(const char* tag) override;
    void writeSelfClosingTag(const char* tag, const char* classes = nullptr,
                            const char* attrs = nullptr) override;
    void writeElement(const char* tag, const char* content, 
                     const char* classes = nullptr) override;
    void writeAttribute(const char* name, const char* value) override;
    
    void indent() override;
    void unindent() override;
    void newline() override;
    
    Item getResult() override;  // Returns String
    const char* getHtml() override;
    
private:
    void appendIndent();
    void escapeHtml(const char* text, size_t len);
};

// Node mode implementation: generates Lambda Element tree
class NodeHtmlWriter : public HtmlWriter {
private:
    MarkBuilder* builder_;
    std::vector<ElementBuilder> stack_;  // Stack of open elements
    Pool* pool_;
    
public:
    NodeHtmlWriter(Pool* pool);
    ~NodeHtmlWriter() override;
    
    void writeText(const char* text, size_t len = 0) override;
    void writeRawHtml(const char* html) override;  // Parse HTML → Elements
    void openTag(const char* tag, const char* classes = nullptr,
                const char* id = nullptr, const char* style = nullptr) override;
    void closeTag(const char* tag) override;
    void writeSelfClosingTag(const char* tag, const char* classes = nullptr,
                            const char* attrs = nullptr) override;
    void writeElement(const char* tag, const char* content,
                     const char* classes = nullptr) override;
    void writeAttribute(const char* name, const char* value) override;
    
    void indent() override { /* no-op */ }
    void unindent() override { /* no-op */ }
    void newline() override { /* no-op */ }
    
    Item getResult() override;  // Returns root Element
    const char* getHtml() override { return nullptr; }  // Not applicable
    
private:
    ElementBuilder& current() { return stack_.back(); }
};

} // namespace lambda

#endif // HTML_WRITER_HPP
```

### Usage Example

```cpp
// Text mode example
Pool* pool = pool_create();
TextHtmlWriter writer(pool, true /* pretty print */);

writer.openTag("div", "container");
  writer.openTag("h1", nullptr, "title");
    writer.writeText("Hello World");
  writer.closeTag("h1");
  writer.openTag("p");
    writer.writeText("This is a paragraph.");
  writer.closeTag("p");
writer.closeTag("div");

const char* html = writer.getHtml();
// Output: <div class="container">\n  <h1 id="title">Hello World</h1>\n  <p>This is a paragraph.</p>\n</div>

// Node mode example
NodeHtmlWriter node_writer(pool);
node_writer.openTag("div", "container");
  node_writer.writeText("Content");
node_writer.closeTag("div");

Item result = node_writer.getResult();
// Result: Element("div", {String("Content")}, attributes={"class": "container"})
```

---

## File-by-File Translation Plan

### Overview

Translate latex.js files systematically, preserving structure and logic:

| latex.js File | Lambda C++ File | Lines | Purpose | Priority |
|---------------|----------------|-------|---------|----------|
| `generator.ls` | `latex_generator.cpp` | ~600 | Base generator with state | P0 - Foundation |
| `html-generator.ls` | `html_generator.cpp` | ~500 | HTML-specific output | P0 - Foundation |
| `latex.ltx.ls` | `latex_macros.cpp` | ~1400 | LaTeX command handlers | P1 - Core |
| `symbols.ls` | `latex_symbols.cpp` | ~300 | Symbol/ligature tables | P1 - Core |
| `documentclasses/article.ls` | `docclass_article.cpp` | ~200 | Article document class | P2 - Classes |
| `documentclasses/book.ls` | `docclass_book.cpp` | ~100 | Book document class | P2 - Classes |
| `packages/*.ls` | `packages/*.cpp` | ~500 | LaTeX packages | P3 - Extensions |

**Total Estimated Lines**: ~3600 lines of C++ (vs. ~3100 LiveScript in latex.js)

### P0: Foundation (Week 1-2)

#### File: `latex_generator.cpp` (translate `generator.ls`)

**Responsibilities**:
- Document state management (title, author, date, counters, labels)
- Group/scope stack for font contexts, lengths, alignments
- Counter system: newCounter, stepCounter, setCounter, formatCounter
- Label/reference system: setLabel, getLabel, ref
- Length system: newLength, setLength, Length class
- State stack: enterGroup, exitGroup

**Key Classes/Structures**:

```cpp
// lambda/format/latex_generator.hpp

class LatexGenerator {
protected:
    Pool* pool_;
    HtmlWriter* writer_;
    
    // Document state
    std::string document_class_;
    std::string document_title_;
    std::string document_author_;
    std::string document_date_;
    
    // Counter system
    std::map<std::string, Counter> counters_;
    std::map<std::string, std::vector<std::string>> counter_resets_;  // parent → children
    
    // Label/reference system
    std::map<std::string, LabelInfo> labels_;
    LabelInfo current_label_;
    int label_id_counter_;
    
    // Length system
    std::map<std::string, Length> lengths_;
    
    // State stack (for groups)
    struct GroupState {
        FontContext font;
        AlignmentMode alignment;
        std::map<std::string, Length> local_lengths;
    };
    std::vector<GroupState> group_stack_;
    
public:
    LatexGenerator(Pool* pool, HtmlWriter* writer);
    virtual ~LatexGenerator() = default;
    
    // Counter operations
    void newCounter(const std::string& name, const std::string& parent = "");
    void stepCounter(const std::string& name);
    void setCounter(const std::string& name, int value);
    void addToCounter(const std::string& name, int delta);
    int getCounter(const std::string& name) const;
    std::string formatCounter(const std::string& name, const std::string& format) const;
    
    // Label/reference operations
    void setLabel(const std::string& name);
    LabelInfo getLabel(const std::string& name) const;
    void setCurrentLabel(const std::string& anchor, const std::string& text);
    std::string generateAnchorId(const std::string& prefix = "ref");
    
    // Length operations
    void newLength(const std::string& name, const Length& value = Length::zero());
    void setLength(const std::string& name, const Length& value);
    Length getLength(const std::string& name) const;
    
    // Group/scope operations
    void enterGroup();
    void exitGroup();
    FontContext& currentFont();
    AlignmentMode currentAlignment() const;
    
    // Document structure
    virtual void startSection(const std::string& level, bool starred, 
                             const std::string& toc_title, const std::string& title);
    virtual void startList();
    virtual void endList();
    
    // Utility methods
    std::string macro(const std::string& counter_name) const;  // Get formatted counter
    bool isBlockLevel(const char* tag) const;
};
```

**Translation Notes**:
- `generator.ls` lines 1-44: Class initialization, document state
- `generator.ls` lines 110-220: Counter system (newCounter, stepCounter, etc.)
- `generator.ls` lines 222-280: Label/reference system
- `generator.ls` lines 282-390: Length system with CSS unit conversion
- `generator.ls` lines 392-450: Group/scope stack management

#### File: `html_generator.cpp` (translate `html-generator.ls`)

**Responsibilities**:
- HTML element creation (via HtmlWriter)
- Character/text processing (ligatures, entities, escaping)
- Math rendering delegation (KaTeX equivalent)
- CSS class management
- DOM fragment generation

**Key Methods**:

```cpp
// lambda/format/html_generator.hpp

class HtmlGenerator : public LatexGenerator {
private:
    // Character tokens
    static const char* SP;      // ' '
    static const char* BRSP;    // '\u200B '
    static const char* NBSP;    // '\u00A0'
    static const char* VISP;    // '\u2423'
    static const char* ZWNJ;    // '\u200C'
    static const char* SHY;     // '\u00AD'
    
    bool hyphenate_;
    
public:
    HtmlGenerator(Pool* pool, HtmlWriter* writer, bool hyphenate = true);
    
    // Element creation (translates to HtmlWriter calls)
    void createInline(const char* content, const char* classes = nullptr);
    void createBlock(const char* content, const char* classes = nullptr);
    void createParagraph(const char* content = nullptr);
    void createTitle(const char* content);
    void createSection(const std::string& level, const char* content, const char* id = nullptr);
    void createList(const char* type, const char* classes = nullptr);
    void createListItem(const char* content, const char* classes = nullptr);
    
    // Text processing
    std::string processText(const char* text);  // Apply ligatures, normalize whitespace
    std::string applyLigatures(const char* text);
    std::string escapeDiacritics(const char* cmd, const char* base);
    
    // Math rendering
    void renderMath(const char* math, bool display_mode);
    
    // CSS generation
    std::string generateCSS(const std::string& document_class);
    
    // Output
    Item getResult();  // Delegates to HtmlWriter
};
```

**Translation Notes**:
- `html-generator.ls` lines 29-90: HTML element type definitions → C++ constants
- `html-generator.ls` lines 134-167: Constructor, options → C++ initialization
- `html-generator.ls` lines 228-268: Document generation (htmlDocument, stylesAndScripts)
- `html-generator.ls` lines 300-380: Text processing (character, ligatures, diacritics)
- `html-generator.ls` lines 439-473: Spacing and line breaks

### P1: Core Commands (Week 3-4)

#### File: `latex_macros.cpp` (translate `latex.ltx.ls`)

**Responsibilities**:
- Implement ~200 LaTeX commands
- Command argument parsing and validation
- Macro expansion logic
- Environment handling

**Structure**:

```cpp
// lambda/format/latex_macros.hpp

// Command handler signature
typedef void (*CommandHandler)(HtmlGenerator* gen, Element* elem, RenderContext* ctx);

// Command registry
struct CommandInfo {
    CommandHandler handler;
    CommandCategory category;
    const char* arg_signature;  // e.g., "HV g" = horizontal/vertical mode, required group
    const char* description;
};

// Global command registry
extern std::map<std::string, CommandInfo> g_latex_commands;

// Initialize command registry (called once at startup)
void init_latex_commands();

// Macro implementations (one function per command)
void handle_textbf(HtmlGenerator* gen, Element* elem, RenderContext* ctx);
void handle_textit(HtmlGenerator* gen, Element* elem, RenderContext* ctx);
void handle_section(HtmlGenerator* gen, Element* elem, RenderContext* ctx);
void handle_item(HtmlGenerator* gen, Element* elem, RenderContext* ctx);
// ... ~200 more handlers

// Environment handlers
void handle_itemize(HtmlGenerator* gen, Element* elem, RenderContext* ctx);
void handle_enumerate(HtmlGenerator* gen, Element* elem, RenderContext* ctx);
void handle_center(HtmlGenerator* gen, Element* elem, RenderContext* ctx);
// ... ~30 more environment handlers
```

**Translation Strategy**:
1. **Group by Category**: Text formatting, sectioning, lists, boxes, math, etc.
2. **One-to-One Mapping**: Each latex.js macro → one C++ function
3. **Preserve Logic**: Copy control flow, calculations exactly
4. **Comment References**: Add `// latex.ltx.ls:123-145` to track source

**Example Translation**:

```livescript
# latex.ltx.ls lines 235-240
\textbf : (txt) -> [ @g.create @g.inline, txt, "bf" ]
```

↓ translates to ↓

```cpp
// latex_macros.cpp - translate latex.ltx.ls:235-240
void handle_textbf(HtmlGenerator* gen, Element* elem, RenderContext* ctx) {
    // Extract argument text
    Item arg = extract_first_argument(elem);
    if (arg.item == ITEM_NULL) {
        log_error("\\textbf: missing argument");
        return;
    }
    
    // Create inline span with "bf" class
    gen->writer()->openTag("span", "bf");
    process_content(gen, arg, ctx);  // Recursively process argument
    gen->writer()->closeTag("span");
}
```

**File Organization**:

```
lambda/format/latex_macros.cpp        # ~1400 lines
  ├─ Section 1: Text formatting (textbf, textit, emph, etc.)
  ├─ Section 2: Sectioning (chapter, section, subsection, etc.)
  ├─ Section 3: Lists (itemize, enumerate, description, item)
  ├─ Section 4: Boxes (mbox, fbox, parbox, minipage)
  ├─ Section 5: Spacing (hspace, vspace, quad, qquad)
  ├─ Section 6: References (label, ref, pageref)
  ├─ Section 7: Counters (newcounter, stepcounter, etc.)
  ├─ Section 8: Fonts (tiny, small, normalsize, large, etc.)
  ├─ Section 9: Alignment (centering, raggedright, flushleft)
  └─ Section 10: Special (today, TeX, LaTeX, verb, etc.)
```

#### File: `latex_symbols.cpp` (translate `symbols.ls`)

**Responsibilities**:
- Unicode symbol mappings
- Ligature tables
- Diacritic combining characters

**Structure**:

```cpp
// lambda/format/latex_symbols.hpp

// Symbol lookup: \alpha → α
const char* lookup_symbol(const char* symbol_name);

// Ligature lookup: "ffi" → "ﬃ"
const char* lookup_ligature(const char* text, size_t* consumed_len);

// Diacritic lookup: \'{e} → é
const char* lookup_diacritic(char accent, char base);

// Symbol tables (exported from symbols.ls)
extern const struct SymbolEntry {
    const char* name;
    const char* unicode;
} g_symbol_table[];

extern const struct LigatureEntry {
    const char* seq;
    const char* replacement;
    size_t len;
    bool only_non_tt;  // Disable in typewriter font
} g_ligature_table[];

extern const struct DiacriticEntry {
    char accent;          // e.g., '\''
    const char* combining;  // e.g., "\xCC\x81" (combining acute)
    const char* standalone; // e.g., "\xC2\xB4" (standalone acute)
} g_diacritic_table[];
```

**Translation Notes**:
- `symbols.ls` lines 1-200: Unicode symbol definitions → C++ table initialization
- `symbols.ls` lines 201-250: Ligatures → `g_ligature_table`
- `symbols.ls` lines 251-300: Diacritics → `g_diacritic_table`

### P2: Document Classes (Week 5)

#### Files: `docclass_article.cpp`, `docclass_book.cpp`

**Responsibilities**:
- Document-class-specific sectioning hierarchy
- Counter initialization (chapter, section, figure, etc.)
- CSS stylesheet selection
- Page layout parameters

**Structure**:

```cpp
// lambda/format/docclass_article.hpp

class ArticleDocClass {
public:
    static void init(LatexGenerator* gen);
    static std::string getCSS();
    static int getSectionLevel(const std::string& section_type);
};

// Translate documentclasses/article.ls
```

### P3: Packages (Week 6)

LaTeX packages: `graphicx`, `hyperref`, `color`, etc.

**Structure**:

```cpp
// lambda/format/packages/package_graphicx.cpp
void init_package_graphicx(LatexGenerator* gen);
void handle_includegraphics(HtmlGenerator* gen, Element* elem, RenderContext* ctx);
```

---

## Main Formatter Entry Point

### File: `format_latex_html_v2.cpp` (new file)

This is the main entry point that orchestrates the entire formatting pipeline.

```cpp
// lambda/format/format_latex_html_v2.hpp

#ifndef FORMAT_LATEX_HTML_V2_HPP
#define FORMAT_LATEX_HTML_V2_HPP

#include "../lambda-data.hpp"
#include "../../lib/strbuf.h"
#include "../../lib/mempool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Main API: Generate HTML from LaTeX Element tree
// Output modes:
//   - text_mode=true:  Returns String with HTML text in html_result
//   - text_mode=false: Returns Element tree in html_result
void format_latex_html_v2(
    Item* html_result,          // Output: String or Element
    Item* css_result,           // Output: String with CSS (optional, can be NULL)
    Item latex_tree,            // Input: Element tree from parser
    Pool* pool,
    bool text_mode,             // true=text output, false=node output
    bool pretty_print           // true=indent HTML (text mode only)
);

#ifdef __cplusplus
}
#endif

#endif // FORMAT_LATEX_HTML_V2_HPP
```

**Implementation**:

```cpp
// lambda/format/format_latex_html_v2.cpp

#include "format_latex_html_v2.hpp"
#include "html_writer.hpp"
#include "html_generator.hpp"
#include "latex_macros.hpp"

extern "C" {

void format_latex_html_v2(
    Item* html_result,
    Item* css_result,
    Item latex_tree,
    Pool* pool,
    bool text_mode,
    bool pretty_print)
{
    // Validate input
    if (get_type_id(latex_tree) != LMD_TYPE_ELEMENT) {
        log_error("format_latex_html_v2: input must be Element");
        *html_result = ITEM_ERROR;
        return;
    }
    
    // Create HtmlWriter (text or node mode)
    HtmlWriter* writer;
    if (text_mode) {
        writer = new TextHtmlWriter(pool, pretty_print);
    } else {
        writer = new NodeHtmlWriter(pool);
    }
    
    // Create HtmlGenerator
    HtmlGenerator gen(pool, writer);
    
    // Initialize command registry (once)
    static bool commands_initialized = false;
    if (!commands_initialized) {
        init_latex_commands();
        commands_initialized = true;
    }
    
    // Process LaTeX Element tree
    RenderContext ctx(&gen);
    Element* root = (Element*)latex_tree.pointer;
    
    // Start HTML document
    writer->openTag("html");
    writer->openTag("head");
    
    // Generate CSS
    if (css_result) {
        std::string css = gen.generateCSS("article");
        MarkBuilder builder(pool);
        *css_result = s2it(builder.createString(css.c_str(), css.length()));
        
        writer->openTag("style");
        writer->writeText(css.c_str());
        writer->closeTag("style");
    }
    
    writer->closeTag("head");
    writer->openTag("body", "body");
    
    // Process document content
    process_element_tree(&gen, root, &ctx);
    
    writer->closeTag("body");
    writer->closeTag("html");
    
    // Extract result
    *html_result = writer->getResult();
    
    delete writer;
}

} // extern "C"
```

---

## Implementation Roadmap

### Phase 0: Setup (Week 1)

**Tasks**:
1. Create directory structure:
   ```
   lambda/format/
   ├─ html_writer.hpp
   ├─ html_writer.cpp
   ├─ latex_generator.hpp
   ├─ latex_generator.cpp
   ├─ html_generator.hpp
   ├─ html_generator.cpp
   ├─ latex_macros.hpp
   ├─ latex_macros.cpp
   ├─ latex_symbols.hpp
   ├─ latex_symbols.cpp
   ├─ format_latex_html_v2.hpp
   └─ format_latex_html_v2.cpp
   ```

2. Implement `HtmlWriter` base class and both implementations
3. Set up build system integration (update `build_lambda_config.json`)
4. Create initial test harness

**Deliverable**: Compiling skeleton with empty implementations

### Phase 1: Foundation (Week 2-3)

**Tasks**:
1. Translate `generator.ls` → `latex_generator.cpp`
   - Counter system
   - Label/reference system
   - Length system
   - Group stack

2. Translate `html-generator.ls` → `html_generator.cpp`
   - Element creation methods
   - Text processing (ligatures, entities)
   - CSS generation

3. Create basic command registry infrastructure

**Deliverable**: Foundation classes with basic text output working

**Test**: `format_latex_html_v2(s2it("Hello \\textbf{World}"))` produces `<p>Hello <span class="bf">World</span></p>`

### Phase 2: Core Commands (Week 4-6)

**Tasks**:
1. Translate text formatting commands (~40 commands):
   - Font commands: `\textbf`, `\textit`, `\texttt`, `\textrm`, etc.
   - Font declarations: `\bfseries`, `\itshape`, `\ttfamily`, etc.
   - Font sizes: `\tiny`, `\small`, `\normalsize`, `\large`, `\Huge`, etc.

2. Translate sectioning commands (~15 commands):
   - `\chapter`, `\section`, `\subsection`, `\subsubsection`
   - `\paragraph`, `\subparagraph`
   - `\part`

3. Translate list environments (~10 commands):
   - `itemize`, `enumerate`, `description`
   - `\item` with optional argument

4. Translate spacing commands (~20 commands):
   - Horizontal: `\,`, `\quad`, `\qquad`, `\hspace{}`
   - Vertical: `\vspace{}`, `\smallskip`, `\medskip`, `\bigskip`
   - Line breaks: `\\`, `\newline`, `\linebreak`

**Deliverable**: ~85 commands implemented, core document structure working

**Test Suite**: Port 30 baseline tests from `test_latex_html_baseline.exe`

### Phase 3: Advanced Commands (Week 7-8)

**Tasks**:
1. Translate box commands (~15 commands):
   - `\mbox`, `\fbox`, `\framebox`, `\parbox`
   - `\makebox`, `\raisebox`
   - `minipage` environment

2. Translate alignment commands (~10 commands):
   - `\centering`, `\raggedright`, `\raggedleft`
   - `center`, `flushleft`, `flushright` environments

3. Translate counter commands (~15 commands):
   - `\newcounter`, `\setcounter`, `\stepcounter`, `\addtocounter`
   - `\arabic`, `\roman`, `\Roman`, `\alph`, `\Alph`
   - `\refstepcounter`, `\value`

4. Translate reference commands (~10 commands):
   - `\label`, `\ref`, `\pageref`
   - `\nameref` (if needed)

**Deliverable**: ~50 more commands, advanced formatting working

**Test Suite**: Port 50 extended tests

### Phase 4: Document Classes & Packages (Week 9-10)

**Tasks**:
1. Translate `article.ls` → `docclass_article.cpp`
2. Translate `book.ls` → `docclass_book.cpp`
3. Implement essential packages:
   - `hyperref`: Link generation
   - `graphicx`: Image inclusion
   - `color`: Color support

**Deliverable**: Full document class support

**Test Suite**: Document-level tests with preamble

### Phase 5: Polish & Optimization (Week 11-12)

**Tasks**:
1. Performance profiling and optimization
2. Memory leak detection and fixes
3. Edge case handling
4. Documentation
5. CSS refinement

**Deliverable**: Production-ready formatter

---

## Testing Strategy

### Test Levels

1. **Unit Tests**: Individual command handlers
   ```cpp
   TEST(LatexMacros, TextBold) {
       Pool* pool = pool_create();
       TextHtmlWriter writer(pool);
       HtmlGenerator gen(pool, &writer);
       
       Element* elem = create_element("textbf", {s2it("Hello")});
       handle_textbf(&gen, elem, nullptr);
       
       EXPECT_STREQ(writer.getHtml(), "<span class=\"bf\">Hello</span>");
   }
   ```

2. **Integration Tests**: Full document processing
   ```cpp
   TEST(LatexFormatter, SimpleDocument) {
       const char* latex = "\\section{Title}\\n\\nHello \\textbf{World}.";
       Item result;
       format_latex_html_v2(&result, nullptr, parse_latex(latex), pool, true, false);
       // Verify HTML structure
   }
   ```

3. **Comparison Tests**: latex.js output vs Lambda output
   - Use existing test fixtures from latex.js repo
   - Parse same LaTeX with both systems
   - Compare HTML output (normalize whitespace, element order)

### Test Files

```
test/
├─ test_html_writer_gtest.cpp        # HtmlWriter unit tests
├─ test_latex_generator_gtest.cpp    # LatexGenerator unit tests
├─ test_latex_macros_gtest.cpp       # Individual command tests
├─ test_format_latex_html_v2_gtest.cpp  # End-to-end tests
└─ fixtures/
   └─ latex.js/                      # Imported test cases from latex.js
```

### Verification Process

1. **Text Mode**: Compare HTML strings (after normalization)
2. **Node Mode**: Compare Element tree structure
3. **CSS**: Verify stylesheet generation
4. **Regression**: Ensure all existing baseline tests still pass

---

## Migration Strategy

### Coexistence Period

During development, both formatters will coexist:

```cpp
// Old formatter (existing)
void format_latex_to_html(StringBuf* html, StringBuf* css, Item latex_ast, Pool* pool);

// New formatter (v2)
void format_latex_html_v2(Item* html, Item* css, Item latex_tree, Pool* pool, 
                          bool text_mode, bool pretty_print);
```

### Switchover Plan

1. **Week 1-8**: Develop v2 in parallel, no changes to v1
2. **Week 9**: Add CLI flag `--formatter=v2` to test new formatter
3. **Week 10**: Run both formatters in test suite, compare outputs
4. **Week 11**: Fix discrepancies, achieve 100% compatibility
5. **Week 12**: Make v2 default, deprecate v1

### Rollback Plan

If critical issues arise, keep v1 as fallback:
```cpp
void format_latex_to_html(StringBuf* html, StringBuf* css, Item latex_ast, Pool* pool) {
    // Wrapper that calls either v1 or v2 based on compile flag
#ifdef USE_FORMATTER_V2
    Item html_item, css_item;
    format_latex_html_v2(&html_item, &css_item, latex_ast, pool, true, false);
    // Convert Item to StringBuf
#else
    // Original v1 implementation
#endif
}
```

---

## Design Considerations from format-latex-html.cpp

### Good Patterns to Preserve

1. **Command Registry**: Dispatch table with handler functions
   ```cpp
   std::map<std::string, CommandInfo> command_registry;
   ```
   ✅ Keep this pattern, maps well to latex.js macro system

2. **RenderContext**: Centralized state container
   ```cpp
   struct RenderContext {
       Pool* pool;
       FontContext font;
       ParagraphState paragraph;
       std::map<std::string, Counter> counters;
       std::map<std::string, LabelInfo> labels;
   };
   ```
   ✅ Excellent pattern, aligns with latex.js Generator state

3. **Recursive Element Processing**:
   ```cpp
   void process_element(StringBuf* buf, Element* elem, RenderContext* ctx) {
       for (size_t i = 0; i < elem->child_count; i++) {
           process_item(buf, elem->children[i], ctx);
       }
   }
   ```
   ✅ Keep, but adapt to use HtmlWriter instead of StringBuf

4. **Font Context Stack**:
   ```cpp
   void enter_group(RenderContext* ctx) {
       ctx->font_stack.push(ctx->current_font);
   }
   ```
   ✅ Keep, matches latex.js group handling

### Patterns to Improve

1. **Monolithic File**: format-latex-html.cpp is 6300 lines
   ❌ Split into multiple files (generator, macros, symbols)

2. **Mixed C/C++**: Awkward C++ in C-style code
   ✅ Use pure C++ with proper classes and STL

3. **StringBuf Direct Manipulation**: Lots of `stringbuf_append_str`
   ✅ Abstract via HtmlWriter interface

4. **Hard-coded HTML**: Strings scattered throughout code
   ✅ Centralize in HtmlWriter methods

---

## Performance Considerations

### Memory Efficiency

1. **strbuf Reuse**: TextHtmlWriter reuses single StrBuf
   ```cpp
   class TextHtmlWriter {
       StrBuf* buf_;  // Single buffer, reset between uses
   };
   ```

2. **Pool Allocation**: All Elements/Strings use pool allocator
   ```cpp
   NodeHtmlWriter::NodeHtmlWriter(Pool* pool) : pool_(pool) {
       builder_ = new MarkBuilder(pool);  // Uses pool internally
   }
   ```

3. **Minimal Copying**: Pass by reference, avoid string duplication

### Speed Optimization

1. **Static Tables**: Symbol/ligature lookups via `static const` arrays
2. **Command Registry**: O(1) lookup via `std::unordered_map`
3. **Lazy CSS Generation**: Generate stylesheet once, reuse

---

## Appendix: latex.js Command Coverage

### Commands to Translate (Priority 1)

**Text Formatting** (40 commands):
- `\textbf`, `\textit`, `\texttt`, `\textrm`, `\textsf`
- `\textmd`, `\textup`, `\textsl`, `\textsc`
- `\emph`, `\underline`, `\sout`
- `\bfseries`, `\itshape`, `\ttfamily`, `\rmfamily`, `\sffamily`
- `\mdseries`, `\upshape`, `\slshape`, `\scshape`
- `\normalfont`, `\em`
- `\tiny`, `\scriptsize`, `\footnotesize`, `\small`
- `\normalsize`, `\large`, `\Large`, `\LARGE`, `\huge`, `\Huge`

**Sectioning** (15 commands):
- `\part`, `\chapter`, `\section`, `\subsection`, `\subsubsection`
- `\paragraph`, `\subparagraph`
- `\appendix`, `\tableofcontents`

**Lists** (10 commands):
- `itemize`, `enumerate`, `description`
- `\item`

**Spacing** (20 commands):
- `\,`, `\!`, `\:`, `\;`, `\ `, `~`
- `\quad`, `\qquad`, `\enspace`, `\hspace`
- `\vspace`, `\smallskip`, `\medskip`, `\bigskip`
- `\\`, `\newline`, `\linebreak`, `\pagebreak`

**Boxes** (15 commands):
- `\mbox`, `\fbox`, `\framebox`, `\makebox`, `\parbox`
- `\raisebox`, `\llap`, `\rlap`, `\hphantom`, `\vphantom`, `\phantom`
- `minipage`

**Total Priority 1**: ~100 commands

### CSS Stylesheets to Port

1. **base.css**: Core LaTeX typography (~400 lines)
2. **article.css**: Article document class (~200 lines)
3. **book.css**: Book document class (~150 lines)
4. **logos.css**: TeX/LaTeX logo styling (~50 lines)

**Total CSS**: ~800 lines → translate to C++ string literals

---

## Success Criteria

### Functional Requirements

✅ **P0 - Foundation**:
- [ ] HtmlWriter produces valid HTML in both text and node modes
- [ ] LatexGenerator manages counters, labels, fonts correctly
- [ ] Basic text output works (plain text, bold, italic)

✅ **P1 - Core Commands**:
- [ ] 80+ LaTeX commands implemented
- [ ] All baseline tests pass (30/30)
- [ ] Document structure (sections, paragraphs) correct

✅ **P2 - Advanced Features**:
- [ ] Box commands working
- [ ] Counter/reference system functional
- [ ] Extended tests pass (50/50)

✅ **P3 - Production Ready**:
- [ ] Document classes (article, book) complete
- [ ] CSS generation matches latex.js output
- [ ] 95%+ compatibility with latex.js test suite

### Quality Requirements

- **Code Quality**: Pass clang-tidy, AddressSanitizer clean
- **Performance**: Process 1000-line document in <100ms
- **Memory**: No leaks, efficient pool usage
- **Documentation**: Every function documented with latex.js reference

---

## Conclusion

This proposal outlines a comprehensive, systematic approach to rewriting the LaTeX-to-HTML formatter from scratch using literal translation from latex.js. The dual-mode HtmlWriter design provides flexibility for both string and tree output, while maintaining compatibility with the existing tree-sitter based parser.

**Key Innovations**:
1. **HtmlWriter abstraction**: Unified interface for text/node output
2. **File-by-file translation**: Preserves latex.js structure for easy verification
3. **Modular design**: Separate files for generator, macros, symbols, document classes
4. **Incremental migration**: Coexist with v1, gradual switchover

**Timeline**: 12 weeks for full implementation
**Risk**: Low - latex.js is proven, translation is mechanical
**Benefit**: High - maintainable, extensible, compatible with latex.js ecosystem

---

**Document Version**: 1.0  
**Last Updated**: December 11, 2025  
**Author**: Development Team  
**Status**: Proposal - Ready for Review
