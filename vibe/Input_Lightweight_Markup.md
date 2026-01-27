# Lightweight Markup Parser Design and Implementation

This document describes the design and implementation of Lambda's unified lightweight markup parser, which supports a family of similar text-based markup languages with Markdown as the primary focus.

## Table of Contents

1. [Overview](#overview)
2. [Supported Markup Languages](#supported-markup-languages)
3. [Man Page Integration](#man-page-integration)
4. [Architecture](#architecture)
5. [Core Classes](#core-classes)
6. [Code Reuse Principle](#code-reuse-principle)
7. [HTML5 Integration](#html5-integration)
8. [Mark Doc Schema Compliance](#mark-doc-schema-compliance)
9. [Implementation Analysis](#implementation-analysis)
10. [Structured Error Reporting](#structured-error-reporting)
11. [Testing Strategy](#testing-strategy)
12. [Adding a New Format](#adding-a-new-format)
13. [Post-Compliance Cleanup Phase](#post-compliance-cleanup-phase)
14. [Future Improvements](#future-improvements)

---

## Overview

The lightweight markup parser (`input-markup.cpp`) provides a unified parsing framework for multiple text-based markup languages. The design philosophy emphasizes:

1. **Unified Schema Output**: All parsers produce Lambda Items conforming to the Mark Doc Schema (`doc_schema.ls`)
2. **Format Auto-Detection**: Automatic detection of markup format from content and filename
3. **Flavor-Aware Parsing**: Support for dialect variations (e.g., CommonMark, GitHub Flavored Markdown)
4. **Maximum Code Reuse**: Common features implemented once in shared functions, not duplicated per format
5. **Shared Infrastructure**: Common parsing components reused across formats
6. **Memory Efficiency**: Uses Lambda's pool allocation and `MarkBuilder` for efficient memory management

### Entry Points

```cpp
// Primary entry point (auto-detects format)
Item input_markup(Input *input, const char* content);

// Explicit format specification
Item input_markup_with_format(Input *input, const char* content, MarkupFormat format);
```

---

## Supported Markup Languages

The parser supports six lightweight markup language families:

### Primary: Markdown
- **Extensions**: `.md`, `.markdown`
- **Flavors**: `commonmark`, `github`
- **Features**: ATX headers, fenced code blocks, emphasis, links, images, tables, task lists, emoji shortcodes

### reStructuredText (RST)
- **Extensions**: `.rst`
- **Flavor**: `standard`
- **Features**: Underlined headers, directives, literal blocks, grid tables, definition lists, comments

### MediaWiki
- **Extensions**: `.wiki`
- **Flavors**: `mediawiki`, `standard`
- **Features**: `== Headings ==`, `[[Internal Links]]`, `{{Templates}}`, wiki tables, bold/italic (`'''/''`)

### Org-mode
- **Extensions**: `.org`
- **Flavor**: `standard`
- **Features**: `* Headings`, `#+BEGIN_SRC` blocks, properties drawers, TODO keywords

### Textile
- **Extensions**: `.textile`
- **Flavor**: `standard`
- **Features**: `h1.` headings, block quotes (`bq.`), pre blocks, inline formatting

### AsciiDoc ✓ (Unified)
- **Extensions**: `.adoc`, `.asciidoc`, `.asc`
- **Flavor**: `standard`
- **Status**: Fully integrated into modular parser (January 2026)
- **Features**: `= Headings`, listing blocks (`----`), admonitions (NOTE:, TIP:, WARNING:, CAUTION:, IMPORTANT:), definition lists (`term:: definition`), tables (`|===`), inline links (`link:url[text]`), inline images (`image:path[alt]`), cross-references (`<<anchor>>`), `[source,lang]` code blocks

### Man Page ✓ (Unified)

Unix man pages (`input-man.cpp`) can be unified into the markup parser. Analysis shows significant overlap with existing features:

#### Feature Mapping

| Man Page Feature | Existing Markup Equivalent | Shared Function |
|------------------|---------------------------|-----------------|
| `.SH SECTION` | `# Header` / `== Header ==` | `parse_header()` |
| `.SS Subsection` | `## Header` | `parse_header()` |
| `.PP` paragraph | Paragraph break | `parse_paragraph()` |
| `.B bold` | `**bold**` | `parse_bold_italic()` → `<strong>` |
| `.I italic` | `*italic*` | `parse_bold_italic()` → `<em>` |
| `.IP item` | `- item` | `parse_list_item()` |
| `.TP term` | Definition list | `parse_definition_list()` |
| `\fB...\fR` | Inline bold | `parse_inline_spans()` |

### Other Similar Markup Languages

Beyond the six currently supported, these additional lightweight markup languages share similar design patterns and could be added:

| Language           | Characteristics                         | Potential Value                                    |
| ------------------ | --------------------------------------- | -------------------------------------------------- |
| **Creole**         | Wiki syntax standardization attempt     | Cross-wiki compatibility                           |
| **BBCode**         | Forum markup `[b]bold[/b]`              | Legacy forum content                               |
| **Gemini/Gemtext** | Minimal markup for Gemini protocol      | Ultra-simple documents                             |
| **Djot**           | CommonMark successor by John MacFarlane | Modern Markdown replacement                        |
| **MyST**           | Markdown variant for Sphinx             | Technical documentation                            |
| **MDX**            | Markdown + JSX                          | React documentation (already have separate parser) |
| **txt2tags**       | Simple markup with multiple outputs     | Basic document conversion                          |

## Architecture

### Core Components

The modular markup parser uses a clean separation between format detection, block parsing, and inline parsing:

```
lambda/input/markup/
├── markup_common.hpp        # Shared types, Format enum, ParseContext
├── markup_parser.cpp        # Main entry point and orchestration
├── format/                  # Format-specific adapters
│   ├── format_adapter.hpp   # FormatAdapter interface
│   ├── markdown_adapter.cpp
│   ├── rst_adapter.cpp
│   ├── wiki_adapter.cpp
│   ├── org_adapter.cpp
│   ├── textile_adapter.cpp
│   ├── asciidoc_adapter.cpp # ✓ Full AsciiDoc detection
│   └── man_adapter.cpp
├── block/                   # Block-level parsers
│   ├── block_common.hpp     # Block types and shared utilities
│   ├── block_detection.cpp  # Block type detection
│   ├── block_document.cpp   # Document structure parsing
│   ├── block_header.cpp
│   ├── block_list.cpp
│   ├── block_code.cpp       # Extended for [source,lang]
│   ├── block_table.cpp      # Extended for |=== tables
│   ├── block_quote.cpp
│   └── block_asciidoc.cpp   # ✓ AsciiDoc-specific blocks
└── inline/                  # Inline-level parsers
    ├── inline_common.hpp
    ├── inline_spans.cpp
    ├── inline_emphasis.cpp
    ├── inline_links.cpp
    └── inline_format_specific.cpp  # ✓ AsciiDoc link/image/xref
```

### Legacy Components (to be migrated)

```
lambda/input/
├── input-context.hpp        # Base class: InputContext with error tracking
├── parse_error.hpp          # Error structures: SourceLocation, ParseError, ParseErrorList
└── source_tracker.hpp       # Position tracking: SourceTracker
```

---

## Core Classes

### MarkupParser

The central class that coordinates parsing. Extends `InputContext` to inherit MarkBuilder, error tracking, and source tracking.

```cpp
class MarkupParser : public InputContext {
public:
    Item parse(const char* content, size_t length, ParseConfig config = ParseConfig());
    FormatAdapter* adapter;      // Current format adapter
    ParserState state;           // Parser state
    const char* current_line;    // Line-based iteration
    int current_line_number;
    bool advance_line();

    // Error reporting
    void warnUnclosed(const char* delimiter, int start_line);
    void warnInvalidSyntax(const char* element, const char* expected);
};
```

### FormatAdapter

Abstract interface for format-specific behavior. Each format implements its own adapter.

```cpp
class FormatAdapter {
public:
    virtual Format get_format() const = 0;
    virtual const char* get_name() const = 0;
    virtual bool detect_header(const char* line, int* level, HeaderStyle* style) = 0;
    virtual bool detect_emphasis(const char* text, EmphasisType* type, int* length) = 0;
    virtual bool detect_code_fence(const char* line, CodeFenceInfo* info) = 0;
    virtual bool detect_list_item(const char* line, ListMarkerInfo* info) = 0;
    virtual bool detect_table_row(const char* line, TableRowInfo* info) = 0;
    virtual Item parse_directive(MarkupParser* parser, const char* line);  // RST
    virtual Item parse_wiki_link(MarkupParser* parser, const char* text);  // Wiki
};
```

### ParseConfig

```cpp
struct ParseConfig {
    Format format;           // Target format (or AUTO_DETECT)
    Flavor flavor;           // Format variant (e.g., GFM, CommonMark)
    bool strict_mode;        // Strict vs lenient parsing
    bool collect_metadata;   // Parse frontmatter/properties
    bool resolve_refs;       // Resolve link/footnote references
};
```

### Class Hierarchy

```cpp
InputContext (base)
├── Input* input_           // The Input being parsed (not owned)
├── MarkBuilder builder     // Lambda element construction
├── ParseErrorList errors_  // Error collection
└── SourceTracker tracker   // Source position tracking

MarkupParser : public InputContext
├── ParseConfig config      // Format, flavor, strict mode
├── char** lines           // Split source lines
├── int line_count         // Total lines
├── int current_line       // Current parsing position
└── state                  // Format-specific parsing state
```

### Format Detection Pipeline

```cpp
MarkupFormat detect_markup_format(const char* content, const char* filename)
├── 1. File extension check (highest priority)
├── 2. AsciiDoc pattern detection (= , ==, NOTE:, WARNING:, etc.)
├── 3. Org-mode pattern detection (#+TITLE:, #+BEGIN_SRC, etc.)
├── 4. RST pattern detection (.. directives, underlined headers)
├── 5. Textile pattern detection (h1., h2., etc.)
├── 6. Wiki pattern detection (== , [[, {{)
└── 7. Default: Markdown

const char* detect_markup_flavor(MarkupFormat format, const char* content)
├── MARKUP_MARKDOWN: github (if ```, ~~, task lists) else commonmark
└── MARKUP_WIKI: mediawiki (if {{, [[Category:) else standard
```

### Code Reuse Principle

**Critical Design Requirement**: Common or similar markup features across the language family MUST be implemented in shared functions, not duplicated per format.

#### Shared Parsing Functions (Current)

| Function | Shared Across | Purpose |
|----------|---------------|---------|
| `parse_header()` | All formats | ATX-style headers (`#`, `=`, `*`) |
| `parse_list_structure()` | All formats | Nested lists with various markers |
| `parse_code_block()` | MD, RST, Wiki, AsciiDoc | Fenced code blocks |
| `parse_blockquote()` | MD, RST, Wiki | Block quotes (`>`, `..`) |
| `parse_table_structure()` | MD, Wiki, AsciiDoc | Pipe-delimited tables |
| `parse_inline_spans()` | All formats | Inline formatting dispatcher |
| `parse_bold_italic()` | All formats | Emphasis with `*`, `_`, `'` |
| `parse_link()` | All formats | Links `[text](url)`, `[[wiki]]` |
| `parse_image()` | All formats | Images `![alt](src)` |

#### Format-Specific Adapters

When format differences exist, use adapter pattern with shared core:

```cpp
// Shared core function with format-specific rules
static Item parse_header(MarkupParser* parser, const char* line) {
    int level = get_header_level(parser, line);  // Format-aware level detection
    // ... shared header creation logic
}

// Format-aware helper
static int get_header_level(MarkupParser* parser, const char* line) {
    switch (parser->config.format) {
        case MARKUP_MARKDOWN: return count_hash_markers(line);
        case MARKUP_WIKI:     return count_equals_markers(line);
        case MARKUP_RST:      return detect_underline_level(parser, line);
        case MARKUP_MAN:      return parse_man_section_level(line);
        // ... all use shared level → h1-h6 mapping
    }
}
```

#### Inline Element Mapping

All formats use common inline parsing with format-specific delimiters:

| Feature | Markdown | Wiki | RST | Textile | AsciiDoc | Man |
|---------|----------|------|-----|---------|----------|-----|
| Bold | `**text**` | `'''text'''` | `**text**` | `*text*` | `*text*` | `\fBtext\fR` |
| Italic | `*text*` | `''text''` | `*text*` | `_text_` | `_text_` | `\fItext\fR` |
| Code | `` `text` `` | `<code>` | ``` ``text`` ``` | `@text@` | `` `text` `` | - |
| Link | `[t](url)` | `[[url|t]]` | `` `t <url>`_ `` | `"t":url` | `link:url[t]` | - |
| Image | `![alt](src)` | `[[File:...]]` | `.. image::` | `!src!` | `image:src[alt]` | - |
| Cross-ref | - | `[[#anchor]]` | `:ref:` | - | `<<anchor>>` | - |

**Implementation**: One `parse_emphasis()` function handles all, with format-specific delimiter detection.

### Block Element Types

```cpp
typedef enum {
    BLOCK_PARAGRAPH,
    BLOCK_HEADER,
    BLOCK_LIST_ITEM,
    BLOCK_ORDERED_LIST,
    BLOCK_UNORDERED_LIST,
    BLOCK_CODE_BLOCK,
    BLOCK_QUOTE,
    BLOCK_TABLE,
    BLOCK_MATH,
    BLOCK_DIVIDER,
    BLOCK_COMMENT,
    BLOCK_FOOTNOTE_DEF,
    BLOCK_RST_DIRECTIVE,
    BLOCK_ORG_BLOCK,
    BLOCK_YAML_FRONTMATTER,
    BLOCK_ORG_PROPERTIES,
    BLOCK_MAN_DIRECTIVE       // Man page .XX directives
} BlockType;
```

### Parsing Flow

```
input_markup(input, content)
└── MarkupParser parser(input, config)
    └── parseContent(content)
        └── parse_document(parser)
            ├── Create <doc version:"1.0">
            ├── Parse metadata (YAML frontmatter / Org properties)
            ├── Create <meta> element (if metadata exists)
            ├── Create <body> element
            └── Loop: parse_block_element(parser)
                ├── detect_block_type(parser, line)
                └── Switch on block type:
                    ├── BLOCK_HEADER → parse_header()
                    ├── BLOCK_LIST_ITEM → parse_list_structure()
                    ├── BLOCK_CODE_BLOCK → parse_code_block()
                    ├── BLOCK_QUOTE → parse_blockquote()
                    ├── BLOCK_TABLE → parse_table_structure()
                    ├── BLOCK_MATH → parse_math_block()
                    ├── BLOCK_DIVIDER → parse_divider()
                    └── BLOCK_PARAGRAPH → parse_paragraph()
```

---

## HTML5 Integration

Markdown documents can contain raw HTML that passes through without markdown processing. The parser integrates with Lambda's HTML5 parser to build a proper DOM tree from HTML fragments.

### Architecture

```
Markdown Document
      │
      ▼
┌─────────────────────┐
│   MarkupParser      │
│  html5_parser_ ─────┼──► Html5Parser (fragment mode)
└─────────────────────┘         │
      │                         ▼
      ▼                   Accumulates all HTML
┌─────────────────────────────────────┐
│            Output Document          │
│  doc                                │
│  ├── body (markdown content)        │
│  │   ├── html-block (raw HTML)      │
│  │   └── p → raw-html (inline HTML) │
│  └── html-dom (parsed HTML5 DOM)    │
│      └── table, div, etc...         │
└─────────────────────────────────────┘
```

### Key Methods

- **`getOrCreateHtml5Parser()`** - Lazily creates HTML5 parser on first HTML encounter
- **`parseHtmlFragment(const char* html)`** - Feeds HTML to shared parser
- **`getHtmlBody()`** - Retrieves parsed HTML body after document parsing

### Benefits

- **Single DOM tree** - All HTML fragments parsed into one coherent DOM
- **Proper nesting** - HTML5 parser handles implicit elements and nesting rules
- **Dual output** - Raw HTML in `html-block`/`raw-html`, parsed DOM in `html-dom`
- **Lazy initialization** - HTML5 parser only created when HTML content is encountered

---

## Mark Doc Schema Compliance

The parser outputs Lambda elements conforming to the Mark Doc Schema defined in `doc/Doc_Schema.md`.

### Document Structure

```mark
<doc version:'1.0'
  <meta
    title:"Document Title",
    author:[...],
    created:t'2025-07-25T10:30:00Z',
    ...
  >
  <body
    <h1 level:1 "Header Text">
    <p "Paragraph with " <em "emphasis"> " and " <strong "strong"> " text.">
    <ul
      <li "First item">
      <li "Second item">
    >
    <code language:python "print('hello')">
    ...
  >
>
```

### Element Mapping

| Markup Construct | Schema Element | Attributes |
|-----------------|----------------|------------|
| Headers | `<h1>` - `<h6>` | `level` (integer) |
| Paragraphs | `<p>` | - |
| Bold/Strong | `<strong>` | - |
| Italic/Emphasis | `<em>` | - |
| Strikethrough | `<s>` | - |
| Code span | `<code>` | `language` (optional) |
| Code block | `<code>` | `language` (optional) |
| Ordered list | `<ol>` | `start`, `style` |
| Unordered list | `<ul>` | - |
| List item | `<li>` | - |
| Blockquote | `<blockquote>` | - |
| Link | `<a>` | `href`, `title` |
| Image | `<img>` | `src`, `alt`, `width`, `height` |
| Table | `<table>` | - |
| Table row | `<tr>` | - |
| Table header cell | `<th>` | `align` |
| Table data cell | `<td>` | `align` |
| Horizontal rule | `<hr>` | - |
| Math block | `<math>` | `type` (inline/display) |
| Emoji | `<emoji>` | - (content is Unicode) |
| Footnote ref | `<sup>` | - |
| Citation | `<cite>` → `<citation>` | `id`, `prefix`, `suffix`, `mode` |

### Metadata Handling

The parser extracts metadata from:
- **Markdown**: YAML frontmatter (`---` delimited)
- **Org-mode**: Property lines (`#+TITLE:`, `#+AUTHOR:`, etc.)
- **AsciiDoc**: Default metadata structure

Metadata fields follow the unified schema with compatibility across formats (see `doc/Doc_Schema.md` for complete mapping).

---

## Implementation Analysis

### Current Implementation Status

#### Strengths ✓

1. **Comprehensive Format Support**: Six markup languages with format-specific logic
2. **Proper Schema Output**: Generates compliant Mark Doc Schema elements
3. **Inline Element Parsing**: Full support for emphasis, links, images, code spans
4. **Nested List Support**: Multi-level list nesting with proper indent tracking
5. **Table Parsing**: Alignment detection and multi-line cell support
6. **Math Integration**: Inline (`$...$`) and display (`$$...$$`) math with flavor detection
7. **Emoji Shortcodes**: 200+ GitHub-compatible emoji shortcode mappings
8. **Metadata Parsing**: YAML frontmatter and Org-mode properties

#### Areas for Improvement ⚠

1. **Error Reporting**: Limited use of structured error reporting (only 2 `addWarning` calls)
   ```cpp
   // Current: minimal error reporting
   if (result.item == ITEM_ERROR) {
       parser.addWarning(parser.tracker.location(), "Markup parsing returned error");
   }
   ```

2. **Code Structure**: Single 6200-line file could benefit from modularization
   - Block parsers could be in separate files
   - Format-specific code could be organized into adapter modules

3. **Memory Safety**: Some manual `malloc`/`free` patterns could use RAII wrappers
   ```cpp
   // Pattern that could be improved:
   char* content = (char*)malloc(total_len + 1);
   // ... use content ...
   free(content);
   ```

4. **CommonMark Compliance**: No formal CommonMark spec test suite integration

5. **Incomplete Format Support**:
   - RST grid tables: Basic implementation only
   - RST reference resolution: Not fully implemented
   - ~~AsciiDoc inline formatting: Placeholder implementation~~ ✓ **Completed** (January 2026)
   - Textile: Some block types not fully parsed

---

## Structured Error Reporting

### Current Error Infrastructure

Lambda provides a robust error reporting infrastructure that the markup parser should leverage more fully:

#### SourceLocation (parse_error.hpp)

```cpp
struct SourceLocation {
    size_t offset;      // Byte offset in source (0-based)
    size_t line;        // Line number (1-based)
    size_t column;      // Column number (1-based, UTF-8 aware)
};
```

#### ParseError (parse_error.hpp)

```cpp
struct ParseError {
    SourceLocation location;
    ParseErrorSeverity severity;  // ERROR, WARNING, NOTE
    std::string message;
    std::string context_line;     // Source line where error occurred
    std::string hint;             // Optional hint for fixing
};
```

#### ParseErrorList (parse_error.hpp)

```cpp
class ParseErrorList {
    bool addError(const ParseError& error);
    void addError(const SourceLocation& loc, const std::string& msg);
    void addWarning(const SourceLocation& loc, const std::string& msg);
    void addNote(const SourceLocation& loc, const std::string& msg);

    bool shouldStop() const;      // Hit error limit
    bool hasErrors() const;
    std::string formatErrors() const;
};
```

### Proposed Unified Error Structure for All Input Parsers

```cpp
// Recommended error categories for input parsers
enum class InputErrorCategory {
    SYNTAX,           // Malformed syntax
    STRUCTURE,        // Improper nesting/structure
    REFERENCE,        // Unresolved reference
    ENCODING,         // Character encoding issues
    RESOURCE,         // Missing file/resource
    LIMIT,            // Size/depth limits exceeded
    DEPRECATED,       // Deprecated syntax usage
    COMPATIBILITY     // Cross-format compatibility issues
};

struct InputParseError : public ParseError {
    InputErrorCategory category;
    std::string format;           // Which format was being parsed
    std::string element_context;  // What element was being parsed

    // Structured error data for programmatic access
    struct {
        int expected_indent;      // For indentation errors
        std::string expected;     // Expected token/pattern
        std::string actual;       // Actual token/pattern
    } details;
};
```

### Error Reporting Guidelines

1. **Always report location**: Use `tracker.location()` or maintain line/column info
2. **Provide context**: Include the source line in `context_line`
3. **Give actionable hints**: Suggest fixes when possible
4. **Categorize appropriately**: ERROR for fatal, WARNING for recoverable, NOTE for info
5. **Continue parsing**: Use recovery strategies to maximize error detection

### Example Error Messages

```
ERROR [line 15, col 8]: Unterminated code fence
  | ```python
  | def hello():
  |     print("world")
  ^-- Expected closing ``` fence

WARNING [line 42, col 1]: List indentation inconsistent
  |   - Item 1
  |  - Item 2    <- 1 space indent, expected 2
  ^-- Consider using consistent indentation

NOTE [line 5, col 10]: Wiki template not expanded
  | Hello {{user}} world
           ^-- Template expansion not supported
```

---

## Testing Strategy

### Phase 1: CommonMark Baseline (Completed)

The CommonMark specification tests have been integrated. Current baseline results:

| Section | Pass | Fail | Rate |
|---------|------|------|------|
| ATX headings | 7 | 11 | 38.9% |
| Autolinks | 7 | 12 | 36.8% |
| Backslash escapes | 1 | 12 | 7.7% |
| Blank lines | 1 | 0 | 100.0% |
| Block quotes | 1 | 24 | 4.0% |
| Code spans | 5 | 17 | 22.7% |
| Emphasis and strong emphasis | 20 | 112 | 15.2% |
| Entity and numeric character references | 3 | 14 | 17.6% |
| Fenced code blocks | 0 | 29 | 0.0% |
| HTML blocks | 0 | 46 | 0.0% |
| Hard line breaks | 4 | 11 | 26.7% |
| Images | 1 | 21 | 4.5% |
| Indented code blocks | 0 | 12 | 0.0% |
| Inlines | 0 | 1 | 0.0% |
| Link reference definitions | 0 | 27 | 0.0% |
| Links | 5 | 85 | 5.6% |
| Lists | 3 | 23 | 11.5% |
| List items | 8 | 40 | 16.7% |
| Paragraphs | 2 | 6 | 25.0% |
| Precedence | 0 | 1 | 0.0% |
| Raw HTML | 3 | 18 | 14.3% |
| Setext headings | 3 | 24 | 11.1% |
| Soft line breaks | 0 | 2 | 0.0% |
| Tabs | 1 | 10 | 9.1% |
| Textual content | 2 | 1 | 66.7% |
| Thematic breaks | 9 | 10 | 47.4% |
| **TOTAL** | **86** | **569** | **13.1%** |

**Key Findings**:
- **High compliance areas**: Blank lines (100%), Textual content (67%), Thematic breaks (47%)
- **Zero compliance areas**: Fenced code blocks, HTML blocks, Indented code, Link references
- **Major gaps**: Emphasis/strong (112 failures), Links (85 failures), HTML blocks (46 failures)

**Test Infrastructure**:
```
test/markup/
├── commonmark/
│   └── spec.txt                      # CommonMark 0.31.2 spec (655 examples)
├── commonmark_html_formatter.hpp     # CommonMark-style HTML output formatter
└── test_commonmark_spec_gtest.cpp    # Test runner with compliance reporting
```

**Run Tests**:
```bash
# Build and run
make -C build/premake config=debug_native test_commonmark_spec_gtest
./test/test_commonmark_spec_gtest.exe --gtest_filter="CommonMarkSpecTest.ComprehensiveStats"
```

### Current Test Infrastructure

The project has basic markup roundtrip tests (`test/test_markup_roundtrip_gtest.cpp`) with:
- Simple element parsing tests
- Empty content handling
- Format detection tests
- File-based comprehensive tests (currently disabled)

### Recommended Test Suites

#### 1. CommonMark Specification Tests

**Source**: https://spec.commonmark.org/
**Test Data**: https://github.com/commonmark/commonmark-spec/blob/master/spec.txt

The CommonMark spec contains ~650 test cases in a structured format:

```markdown
```````````````````````````````````` example
# foo
.
<h1>foo</h1>
````````````````````````````````````
```

**Integration Steps**:
1. Download `spec.txt` from CommonMark repository
2. Create test runner to parse example blocks
3. Compare Lambda output (formatted as HTML) against expected HTML
4. Track compliance percentage

```bash
# Recommended test file locations
test/spec/
├── commonmark/
│   ├── spec.txt              # CommonMark spec test data
│   └── gfm-spec.txt          # GitHub Flavored Markdown extension tests
```

#### 2. MediaWiki Test Suite

**Source**: MediaWiki parser tests
**Location**: https://github.com/wikimedia/mediawiki/tree/master/tests/parser

The MediaWiki project maintains parser tests in a similar format:

```
!! test
Simple paragraph
!! wikitext
hello
!! html
<p>hello</p>
!! end
```

#### 3. RST Test Suite

**Source**: docutils
**Location**: https://github.com/docutils/docutils/tree/master/docutils/test/functional

Docutils provides functional tests for reStructuredText parsing.

#### 4. Org-mode Test Suite

**Source**: org-mode
**Location**: https://git.savannah.gnu.org/cgit/emacs/org-mode.git/tree/testing

#### 5. Recommended Test Structure

```
test/
├── markup/
│   ├── commonmark/
│   │   ├── spec_runner.cpp       # CommonMark spec test runner
│   │   ├── spec.txt              # CommonMark 0.31 spec
│   │   └── gfm_spec.txt          # GFM extensions
│   ├── wiki/
│   │   ├── parser_tests.txt      # MediaWiki parser tests
│   │   └── wiki_spec_runner.cpp
│   ├── rst/
│   │   ├── functional/           # docutils functional tests
│   │   └── rst_spec_runner.cpp
│   ├── man/
│   │   ├── sample_pages/         # Sample man pages for testing
│   │   └── man_spec_runner.cpp
│   ├── fixtures/                 # Shared test fixtures
│   │   ├── headers.md
│   │   ├── lists.md
│   │   ├── tables.md
│   │   └── inline.md
│   └── test_markup_gtest.cpp     # Main spec test runner
├── input/
│   └── *.md, *.rst, *.wiki, etc.    # Existing sample files
```

### Test Categories

| Category | Description | Priority |
|----------|-------------|----------|
| **Spec Compliance** | CommonMark/GFM/Wiki spec tests | High |
| **Roundtrip** | Parse → Format → Parse consistency | High |
| **Schema Validation** | Output conforms to Doc Schema | High |
| **Error Recovery** | Parser handles malformed input | Medium |
| **Edge Cases** | Boundary conditions, Unicode, etc. | Medium |
| **Performance** | Large document parsing speed | Low |
| **Format Detection** | Correct auto-detection | Medium |

### Makefile Integration

```makefile
# Add to Makefile
test-markup-md:
	./test/test_markup_gtest.exe --gtest_filter="CommonMark.*"

test-markup-wiki:
	./test/test_markup_gtest.exe --gtest_filter="MediaWiki.*"

test-markup-rst:
	./test/test_markup_gtest.exe --gtest_filter="RST.*"

test-markup-baseline: test-markup-md test-markup-wiki test-markup-rst
```

---

## Adding a New Format

To add support for a new markup format:

1. **Define format enum** in `markup_common.hpp`:
   ```cpp
   enum class Format { /* existing... */ NEW_FORMAT };
   ```

2. **Create format adapter** in `format/newformat_adapter.cpp`:
   ```cpp
   class NewFormatAdapter : public FormatAdapter {
       Format get_format() const override { return Format::NEW_FORMAT; }
       bool detect_header(const char* line, int* level, HeaderStyle* style) override;
       bool detect_emphasis(const char* text, EmphasisType* type, int* len) override;
       // ... implement all virtual methods
   };
   ```

3. **Register adapter** in `format_registry.cpp`:
   ```cpp
   static NewFormatAdapter new_format_adapter;
   adapters[Format::NEW_FORMAT] = &new_format_adapter;
   ```

4. **Add file extension mapping** in `detect_format_from_extension()`.

5. **Add format-specific block parsers** (if needed) in `block/block_newformat.cpp`.

6. **Add format-specific inline parsers** (if needed) in `inline/inline_format_specific.cpp`.

---

## Future Improvements

### Short-term (Implementation Quality)

1. **Enhance Error Reporting**
   - Add structured errors for common parsing failures
   - Report location for syntax errors
   - Provide recovery hints

2. ~~**Modularize Code**~~ ✓ **In Progress**
   - ~~Split 6200-line file into format-specific modules~~ Modular structure implemented
   - ~~Create shared base for block/inline parsers~~ FormatAdapter pattern in place
   - [x] AsciiDoc fully modularized (January 2026)
   - [ ] Migrate remaining formats from legacy `input-markup.cpp`

3. ~~**Add CommonMark Spec Tests**~~ ✓ **Completed**
   - [x] CommonMark spec.txt integrated (662 tests)
   - [x] 100% compliance achieved
   - [x] GfmTables extension tests passing

### Medium-term (Feature Completeness)

1. **Reference Resolution**
   - Implement link reference definitions
   - Support RST substitutions
   - Handle footnote numbering

2. **Extended Syntax**
   - Definition lists (all formats)
   - Attributes/classes on elements
   - Custom containers/divs

3. **Performance Optimization**
   - Profile large document parsing
   - Optimize emoji lookup (hash table)
   - Lazy line splitting

### Post-Compliance Cleanup Phase

After passing official test suites (CommonMark, MediaWiki, RST), consolidate legacy parsers:

3. **Code Deduplication Audit**
   - Identify remaining duplicated parsing logic
   - Extract common patterns to shared functions
   - Ensure single implementation for each feature

4. **Cleanup Checklist**
   - [ ] All format tests pass with unified parser
   - [ ] No duplicate implementations of common features
   - [ ] Documentation updated

### Long-term (Ecosystem)

1. **Additional Formats**
   - Djot (modern Markdown successor)
   - Creole (wiki standardization)
   - Custom DSLs

2. **Two-way Conversion**
   - Parse and re-emit same format
   - Cross-format conversion
   - Preserve formatting preferences

3. **Language Server Protocol**
   - Diagnostics for editor integration
   - Completion suggestions
   - Hover information

### Performance & Memory Notes

- **Single pass parsing** - Forward-only processing, no backtracking
- **Arena allocation** - Uses MarkBuilder's arena for minimal allocations
- **Deterministic detection** - Block type detection is O(1)
- **Lazy HTML5 parser** - Only created when HTML content encountered
- **Memory cleanup** - Automatic cleanup when `Input` is released

---

## Summary

The Lambda lightweight markup parser provides a solid foundation for parsing multiple markup formats into a unified schema. Key strengths include comprehensive format support and proper schema output.

### Recent Achievements

- ✅ **CommonMark Compliance**: 662/662 tests passing (100%)
- ✅ **Modular Architecture**: FormatAdapter pattern with separate block/inline parsers
- ✅ **AsciiDoc Integration**: Full unification completed (January 2026)
  - Legacy `input-adoc.cpp` removed
  - Admonitions, definition lists, tables, inline links/images/cross-refs supported

### Remaining Work

1. **Format Migration**: Move remaining formats from legacy `input-markup.cpp` to modular parser
2. **Error Reporting**: Leverage existing infrastructure for structured errors
3. **Man Page Unification**: Migrate `input-man.cpp` to modular parser

The parser successfully balances flexibility (multiple formats) with consistency (unified output schema), making it suitable for document processing, transformation, and validation workflows.

---

## References

- [Mark Doc Schema](../doc/Doc_Schema.md) - Output schema specification
- [CommonMark Spec](https://spec.commonmark.org/) - Markdown standardization
- [GFM Spec](https://github.github.com/gfm/) - GitHub Flavored Markdown
- [MediaWiki Formatting](https://www.mediawiki.org/wiki/Help:Formatting) - Wiki syntax
- [reStructuredText](https://docutils.sourceforge.io/rst.html) - RST documentation
- [Org-mode Manual](https://orgmode.org/manual/) - Org syntax reference
- [Textile Reference](https://textile-lang.com/) - Textile documentation
- [AsciiDoc Manual](https://docs.asciidoctor.org/) - AsciiDoc reference
