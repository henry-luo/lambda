# LaTeX Document to HTML/CSS Converter Implementation Plan

## Overview

This document outlines the implementation plan for a LaTeX document structure converter in the Lambda project, inspired by the latex.js library architecture. The goal is to leverage Lambda's existing LaTeX parser to create a robust converter that transforms LaTeX document structure, typography, and formatting into high-quality HTML with accompanying CSS.

**Scope**: This implementation focuses on LaTeX document structure and text formatting, **excluding mathematical expressions** which will be handled separately in a future phase.

## Analysis of latex.js Architecture

### Key Components Analyzed

1. **Parser Architecture** (`latex-parser.pegjs`):
   - PEG.js-based parser for LaTeX syntax
   - Handles document structure, preamble, environments
   - Processes commands, arguments, and grouping
   - Document-level parsing (excluding math expressions)

2. **Generator System** (`generator.ls`, `html-generator.ls`):
   - Abstract `Generator` base class with document state management
   - `HtmlGenerator` extends base with HTML-specific output
   - Element creation functions for document constructs
   - CSS class-based styling approach

3. **CSS Framework** (`base.css`, `article.css`, `book.css`):
   - CSS custom properties for LaTeX dimensions
   - Document class-specific styling
   - Typography and layout rules
   - Page layout and spacing definitions

4. **Text Processing System** (`symbols.ls`, `latex.ltx.ls`):
   - Text formatting commands
   - Special character handling
   - Typography and spacing rules

## Implementation Strategy

### Phase 1: Core Infrastructure (Week 1-2)

#### 1.1 HTML Generator Framework
Create `lambda/format/format-latex-html.cpp` with:

```cpp
class LatexHtmlGenerator {
private:
    StringBuf* html_buffer;
    StringBuf* css_buffer;
    int depth_level;
    DocumentClass doc_class;
    ReferenceManager* ref_manager;
    
public:
    // Core generation methods
    void generate_html(Item latex_ast);
    void generate_css();
    
    // Element creation methods
    void create_element(const char* tag, const char* classes);
    void create_block_element(const char* tag, const char* classes);
    void create_inline_element(const char* tag, const char* classes);
    
    // Document structure methods
    void process_document_class(String* class_name);
    void process_preamble(Array* preamble_items);
    void process_document_body(Array* body_items);
};
```

#### 1.2 CSS Generation System
Create `lambda/format/latex-css-generator.cpp`:

```cpp
class LatexCssGenerator {
private:
    StringBuf* css_buffer;
    DocumentClass current_class;
    
public:
    // CSS generation methods
    void generate_base_css();
    void generate_document_class_css(DocumentClass class_type);
    void generate_typography_css();
    void generate_layout_css();
    
    // CSS utility methods
    void add_css_rule(const char* selector, const char* properties);
    void add_css_variable(const char* name, const char* value);
};
```

### Phase 2: Document Structure Processing (Week 3-4)

#### 2.1 Document Class Support
Implement support for standard LaTeX document classes:

- **Article**: Basic document structure
- **Book**: Chapter/section hierarchy
- **Report**: Similar to book but different formatting
- **Letter**: Letter-specific formatting

#### 2.2 Sectioning Commands
Process LaTeX sectioning hierarchy:

```cpp
void process_sectioning(Item section_item) {
    String* command = get_element_name(section_item);
    
    if (strcmp(command->chars, "chapter") == 0) {
        create_element("h1", "chapter");
    } else if (strcmp(command->chars, "section") == 0) {
        create_element("h2", "section");
    } else if (strcmp(command->chars, "subsection") == 0) {
        create_element("h3", "subsection");
    }
    // ... continue for all sectioning levels
}
```

#### 2.3 Environment Processing
Handle LaTeX text environments:

- **Lists**: `itemize`, `enumerate`, `description`
- **Figures**: `figure`, `table` (without math content)
- **Verbatim**: `verbatim`, `lstlisting`
- **Theorem**: `theorem`, `proof`, `definition` (text-only)
- **Quotes**: `quote`, `quotation`, `verse`

### Phase 3: Typography and Text Processing (Week 3-4)

#### 3.1 Font and Text Formatting
Implement LaTeX text formatting commands:

```cpp
void process_text_formatting(Item format_item) {
    String* command = get_element_name(format_item);
    
    if (strcmp(command->chars, "textbf") == 0) {
        create_inline_element("strong", "textbf");
    } else if (strcmp(command->chars, "textit") == 0) {
        create_inline_element("em", "textit");
    } else if (strcmp(command->chars, "texttt") == 0) {
        create_inline_element("code", "texttt");
    }
    // ... continue for all text formatting commands
}
```

#### 3.2 Special Characters and Symbols
Handle LaTeX special characters and symbols:

- Accented characters (á, é, ñ, etc.)
- Special symbols (§, ©, ®, etc.)
- Ligatures (fi, fl, ff, etc.)
- Diacritics and combining characters

#### 3.3 Spacing and Layout
Implement LaTeX spacing commands:

- `\quad`, `\qquad` - horizontal spacing
- `\vspace`, `\hspace` - custom spacing
- `\smallskip`, `\medskip`, `\bigskip` - vertical spacing
- `\noindent`, `\indent` - paragraph indentation

### Phase 4: Advanced Document Features (Week 5-6)

#### 4.1 Cross-References and Labels
Implement LaTeX referencing system:

```cpp
class ReferenceManager {
private:
    Map<String*, String*> labels;
    Map<String*, int> counters;
    
public:
    void register_label(String* label, String* value);
    String* resolve_reference(String* ref);
    void increment_counter(String* counter_name);
};
```

#### 4.2 Figures and Tables
Handle floating environments:

```cpp
void process_figure(Item figure_item) {
    create_block_element("figure", "latex-figure");
    
    // Process figure contents
    Array* contents = get_element_children(figure_item);
    for (int i = 0; i < contents->length; i++) {
        Item child = contents->items[i];
        if (is_includegraphics(child)) {
            process_includegraphics(child);
        } else if (is_caption(child)) {
            create_element("figcaption", "caption");
            process_item_contents(child);
        }
    }
}
```

### Phase 5: CSS Framework (Week 7-8)

#### 5.1 Base CSS Generation
Create comprehensive CSS framework:

```css
/* Base LaTeX styling */
:root {
    --latex-font-size: 10pt;
    --latex-line-height: 1.2;
    --latex-text-width: 345pt;
    --latex-margin-left: 1in;
    --latex-margin-right: 1in;
    --latex-margin-top: 1in;
    --latex-margin-bottom: 1in;
}

.latex-document {
    font-family: "Computer Modern", "Latin Modern", serif;
    font-size: var(--latex-font-size);
    line-height: var(--latex-line-height);
    max-width: var(--latex-text-width);
    margin: 0 auto;
    padding: var(--latex-margin-top) var(--latex-margin-left) 
             var(--latex-margin-bottom) var(--latex-margin-right);
}
```

#### 5.2 Typography CSS
Implement LaTeX typography rules:

```css
/* Sectioning */
.latex-document h1.chapter {
    font-size: 1.728em;
    font-weight: bold;
    margin: 2em 0 1em 0;
    page-break-before: always;
}

.latex-document h2.section {
    font-size: 1.44em;
    font-weight: bold;
    margin: 1.5em 0 0.75em 0;
}

/* Text formatting */
.textbf { font-weight: bold; }
.textit { font-style: italic; }
.texttt { 
    font-family: "Computer Modern Typewriter", "Courier New", monospace;
    font-size: 0.9em;
}
```

#### 5.3 Layout CSS
Generate document layout CSS:

```css
/* Lists */
.latex-document ul.itemize {
    list-style-type: disc;
    margin: 1em 0;
    padding-left: 2em;
}

.latex-document ol.enumerate {
    list-style-type: decimal;
    margin: 1em 0;
    padding-left: 2em;
}

/* Environments */
.latex-document .quote {
    margin: 1em 2em;
    font-style: italic;
}

.latex-document .verbatim {
    font-family: "Computer Modern Typewriter", monospace;
    white-space: pre;
    background: #f5f5f5;
    padding: 1em;
    margin: 1em 0;
}
```

## Implementation Details

### File Structure

```
lambda/format/
├── format-latex-html.cpp      # Main LaTeX to HTML converter
├── format-latex-html.h        # Header file
├── latex-css-generator.cpp    # CSS generation
├── latex-css-generator.h      # CSS generator header
├── latex-html-elements.cpp    # HTML element definitions
├── latex-reference-manager.cpp # Cross-reference handling
└── latex-document-classes.cpp  # Document class implementations
```

### Integration Points

#### 1. Parser Integration
Leverage existing LaTeX parser from `input-latex.cpp`:

```cpp
// In format-latex-html.cpp
void format_latex_to_html(StringBuf* html_buf, StringBuf* css_buf, Item latex_ast) {
    LatexHtmlGenerator generator(html_buf, css_buf);
    generator.process_document(latex_ast);
}
```

#### 2. Document Structure Integration
Focus on document structure processing:

```cpp
// Process document elements
void process_document_element(Item doc_item) {
    String* element_name = get_element_name(doc_item);
    
    if (is_sectioning_command(element_name)) {
        process_sectioning(doc_item);
    } else if (is_environment(doc_item)) {
        process_environment(doc_item);
    } else if (is_text_formatting(doc_item)) {
        process_text_formatting(doc_item);
    }
}
```

### Testing Strategy

#### Unit Tests (GTest Framework)

Create comprehensive test suite in `test/test_latex_html.cpp`:

```cpp
class LatexHtmlTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize test environment
    }
    
    void TearDown() override {
        // Cleanup
    }
};

TEST_F(LatexHtmlTest, BasicDocumentStructure) {
    const char* latex_input = R"(
        \documentclass{article}
        \begin{document}
        \section{Test Section}
        This is a test paragraph.
        \end{document}
    )";
    
    // Parse LaTeX
    Item latex_ast = parse_latex(latex_input);
    
    // Generate HTML
    StringBuf html_buf, css_buf;
    format_latex_to_html(&html_buf, &css_buf, latex_ast);
    
    // Verify output
    String* html_result = stringbuf_to_string(&html_buf);
    EXPECT_TRUE(strstr(html_result->chars, "<h2 class=\"section\">"));
    EXPECT_TRUE(strstr(html_result->chars, "Test Section"));
}

TEST_F(LatexHtmlTest, DocumentStructure) {
    const char* latex_input = R"(
        \documentclass{article}
        \title{Test Document}
        \author{Test Author}
        \begin{document}
        \maketitle
        \tableofcontents
        \section{Introduction}
        This is the introduction.
        \end{document}
    )";
    
    Item latex_ast = parse_latex(latex_input);
    StringBuf html_buf, css_buf;
    format_latex_to_html(&html_buf, &css_buf, latex_ast);
    
    String* html_result = stringbuf_to_string(&html_buf);
    EXPECT_TRUE(strstr(html_result->chars, "class=\"title\""));
    EXPECT_TRUE(strstr(html_result->chars, "class=\"author\""));
    EXPECT_TRUE(strstr(html_result->chars, "<h2 class=\"section\">"));
}

TEST_F(LatexHtmlTest, TextFormatting) {
    const char* latex_input = R"(
        \documentclass{article}
        \begin{document}
        \textbf{Bold text} and \textit{italic text} and \texttt{monospace}.
        \end{document}
    )";
    
    Item latex_ast = parse_latex(latex_input);
    StringBuf html_buf, css_buf;
    format_latex_to_html(&html_buf, &css_buf, latex_ast);
    
    String* html_result = stringbuf_to_string(&html_buf);
    EXPECT_TRUE(strstr(html_result->chars, "<strong class=\"textbf\">"));
    EXPECT_TRUE(strstr(html_result->chars, "<em class=\"textit\">"));
    EXPECT_TRUE(strstr(html_result->chars, "<code class=\"texttt\">"));
}

TEST_F(LatexHtmlTest, Lists) {
    const char* latex_input = R"(
        \documentclass{article}
        \begin{document}
        \begin{itemize}
        \item First item
        \item Second item
        \end{itemize}
        \end{document}
    )";
    
    Item latex_ast = parse_latex(latex_input);
    StringBuf html_buf, css_buf;
    format_latex_to_html(&html_buf, &css_buf, latex_ast);
    
    String* html_result = stringbuf_to_string(&html_buf);
    EXPECT_TRUE(strstr(html_result->chars, "<ul class=\"itemize\">"));
    EXPECT_TRUE(strstr(html_result->chars, "<li>First item</li>"));
}

TEST_F(LatexHtmlTest, Environments) {
    const char* latex_input = R"(
        \documentclass{article}
        \begin{document}
        \begin{theorem}
        This is a theorem.
        \end{theorem}
        \end{document}
    )";
    
    Item latex_ast = parse_latex(latex_input);
    StringBuf html_buf, css_buf;
    format_latex_to_html(&html_buf, &css_buf, latex_ast);
    
    String* html_result = stringbuf_to_string(&html_buf);
    EXPECT_TRUE(strstr(html_result->chars, "class=\"theorem\""));
}
```

## Feature Compatibility Matrix

### Core LaTeX Document Features

| Feature | latex.js Support | Lambda Implementation | Priority |
|---------|------------------|----------------------|----------|
| Document classes | ✅ article, book, report | ✅ Planned | High |
| Sectioning | ✅ Full hierarchy | ✅ Planned | High |
| Text formatting | ✅ Complete | ✅ Planned | High |
| Lists | ✅ All types | ✅ Planned | High |
| Tables | ✅ Basic support | ✅ Planned | Medium |
| Figures | ✅ Basic support | ✅ Planned | Medium |
| Cross-references | ✅ Full support | ✅ Planned | Medium |
| Verbatim | ✅ Full support | ✅ Planned | Medium |
| Title page | ✅ Full support | ✅ Planned | Medium |

### Text Processing Features

| Feature | latex.js Support | Lambda Implementation | Priority |
|---------|------------------|----------------------|----------|
| Font commands | ✅ Complete | ✅ Planned | High |
| Special characters | ✅ Full support | ✅ Planned | High |
| Spacing commands | ✅ Complete | ✅ Planned | Medium |
| Accents/Diacritics | ✅ Full support | ✅ Planned | Medium |
| Custom macros | ✅ Full support | 🔄 Future | Low |
| Packages | ✅ Limited | 🔄 Future | Low |

## Performance Considerations

### Memory Management
- Use Lambda's existing memory pool system
- Minimize string allocations during HTML generation
- Efficient CSS rule generation and deduplication

### Output Optimization
- Generate minimal, semantic HTML
- Use CSS classes instead of inline styles
- Optimize CSS for common LaTeX patterns

### Scalability
- Support for large documents (100+ pages)
- Incremental processing for real-time preview
- Memory-efficient AST traversal

## Success Metrics

### Functionality Goals
- ✅ 95%+ compatibility with common LaTeX document structures
- ✅ Accurate typography and text formatting
- ✅ Proper spacing and layout
- ✅ Cross-browser compatibility

### Performance Goals
- ✅ Process 10-page document in <1 second
- ✅ Memory usage <100MB for typical documents
- ✅ Generated HTML size <2x original LaTeX size

### Quality Goals
- ✅ 100% test coverage for core features
- ✅ Comprehensive regression test suite
- ✅ Documentation and examples
- ✅ Integration with Lambda CLI

## Future Enhancements

### Phase 6: Advanced Typography
- Microtype support (character protrusion, font expansion)
- Advanced hyphenation
- Ligature processing

### Phase 7: Interactive Features
- Clickable cross-references
- Collapsible sections
- Table of contents navigation

### Phase 8: Export Options
- PDF generation via print CSS
- EPUB export
- Standalone HTML with embedded CSS

### Phase 9: Math Integration
- Integration with existing Lambda math parser
- Mathematical expression rendering
- Math environment support

## Conclusion

This implementation plan provides a focused roadmap for creating a LaTeX document structure to HTML/CSS converter in Lambda. By **excluding mathematical expressions** from the initial scope, we can deliver a robust solution for LaTeX document formatting in a more manageable timeframe.

**Key Benefits of This Approach:**
- **Focused Scope**: Document structure and typography without math complexity
- **Faster Delivery**: 8-week timeline vs 12+ weeks with math integration
- **Solid Foundation**: Establishes core infrastructure for future math integration
- **Immediate Value**: Handles majority of LaTeX document formatting needs

**Deliverables:**
- Production-ready LaTeX document to HTML converter
- Comprehensive CSS framework for LaTeX typography
- Full test suite with GTest integration
- Documentation and usage examples

The modular design ensures that mathematical expression support can be seamlessly added in a future phase, building on the solid document structure foundation established in this implementation.
