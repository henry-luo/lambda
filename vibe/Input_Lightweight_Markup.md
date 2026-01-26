# Lightweight Markup Parser Design and Implementation

This document describes the design and implementation of Lambda's unified lightweight markup parser, which supports a family of similar text-based markup languages with Markdown as the primary focus.

## Table of Contents

1. [Overview](#overview)
2. [Supported Markup Languages](#supported-markup-languages)
3. [Man Page Integration](#man-page-integration)
4. [Architecture](#architecture)
5. [Code Reuse Principle](#code-reuse-principle)
6. [Mark Doc Schema Compliance](#mark-doc-schema-compliance)
7. [Implementation Analysis](#implementation-analysis)
8. [Structured Error Reporting](#structured-error-reporting)
9. [Testing Strategy](#testing-strategy)
10. [Post-Compliance Cleanup Phase](#post-compliance-cleanup-phase)
11. [Future Improvements](#future-improvements)

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

### AsciiDoc
- **Extensions**: `.adoc`, `.asciidoc`, `.asc`
- **Flavor**: `standard`
- **Features**: `= Headings`, listing blocks (`----`), admonitions (NOTE:, TIP:, etc.), tables (`|===`)

### Other Similar Markup Languages

Beyond the six currently supported, these additional lightweight markup languages share similar design patterns and could be added:

| Language | Characteristics | Potential Value |
|----------|----------------|-----------------|
| **Creole** | Wiki syntax standardization attempt | Cross-wiki compatibility |
| **BBCode** | Forum markup `[b]bold[/b]` | Legacy forum content |
| **Gemini/Gemtext** | Minimal markup for Gemini protocol | Ultra-simple documents |
| **Djot** | CommonMark successor by John MacFarlane | Modern Markdown replacement |
| **MyST** | Markdown variant for Sphinx | Technical documentation |
| **MDX** | Markdown + JSX | React documentation (already have separate parser) |
| **txt2tags** | Simple markup with multiple outputs | Basic document conversion |
| **Man pages** | Unix manual page format (`.1`-`.8`) | System documentation |

### Man Page Integration

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

#### Integration Plan

1. **Add `MARKUP_MAN` format**:
   ```cpp
   typedef enum {
       MARKUP_MARKDOWN,
       MARKUP_RST,
       MARKUP_TEXTILE,
       MARKUP_WIKI,
       MARKUP_ORG,
       MARKUP_ASCIIDOC,
       MARKUP_MAN,           // Add man page format
       MARKUP_AUTO_DETECT
   } MarkupFormat;
   ```

2. **Add detection logic**:
   ```cpp
   // File extension detection
   if (strcasecmp(ext, "1") == 0 || ... || strcasecmp(ext, "8") == 0) {
       return MARKUP_MAN;
   }
   // Content detection
   if (strncmp(content, ".TH ", 4) == 0 || strncmp(content, ".SH ", 4) == 0) {
       return MARKUP_MAN;
   }
   ```

3. **Reuse existing parsers**:
   - Headers: `.SH` → `parse_header()` with level detection
   - Bold/italic: `\fB`/`\fI` → `parse_inline_spans()` with man-specific markers
   - Lists: `.IP`/`.TP` → `parse_list_structure()`
   - Paragraphs: `.PP` → paragraph separator

4. **Man-specific additions**:
   - `.TH` title/section parsing → metadata
   - `\fB...\fR` inline escapes → inline formatting
   - `.RS`/`.RE` indent blocks → nested structure

---

## Architecture

### Core Components

```
lambda/input/
├── markup-parser.h          # Header: enums, ParseConfig, MarkupParser class
├── input-markup.cpp         # Implementation (~6200 lines)
├── input-context.hpp        # Base class: InputContext with error tracking
├── parse_error.hpp          # Error structures: SourceLocation, ParseError, ParseErrorList
└── source_tracker.hpp       # Position tracking: SourceTracker
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

| Feature | Markdown | Wiki | RST | Textile | Man |
|---------|----------|------|-----|---------|-----|
| Bold | `**text**` | `'''text'''` | `**text**` | `*text*` | `\fBtext\fR` |
| Italic | `*text*` | `''text''` | `*text*` | `_text_` | `\fItext\fR` |
| Code | `` `text` `` | `<code>` | ``` ``text`` ``` | `@text@` | - |
| Link | `[t](url)` | `[[url|t]]` | `` `t <url>`_ `` | `"t":url` | - |

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
   - AsciiDoc inline formatting: Placeholder implementation
   - Textile: Some block types not fully parsed

### Recommended Code Structure Improvements

```
lambda/input/
├── markup/
│   ├── markup-parser.hpp       # Core parser class
│   ├── markup-common.cpp       # Shared utilities
│   ├── markup-markdown.cpp     # Markdown-specific parsing
│   ├── markup-rst.cpp          # RST-specific parsing
│   ├── markup-wiki.cpp         # Wiki-specific parsing
│   ├── markup-org.cpp          # Org-mode-specific parsing
│   ├── markup-textile.cpp      # Textile-specific parsing
│   ├── markup-asciidoc.cpp     # AsciiDoc-specific parsing
│   └── emoji-map.cpp           # Emoji shortcode table
└── input-markup.cpp            # Entry point and format detection
```

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

## Future Improvements

### Short-term (Implementation Quality)

1. **Enhance Error Reporting**
   - Add structured errors for common parsing failures
   - Report location for syntax errors
   - Provide recovery hints

2. **Modularize Code**
   - Split 6200-line file into format-specific modules
   - Create shared base for block/inline parsers

3. **Add CommonMark Spec Tests**
   - Download and integrate spec.txt
   - Track compliance percentage
   - Fix failing cases

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

1. **Remove Legacy Standalone Parsers**
   - `input-org.cpp` → Migrate to unified `input-markup.cpp`
   - Verify all Org-mode features work in unified parser
   - Remove redundant code paths

2. **Unify Man Page Parser** (see [Man Page Integration](#man-page-integration))
   - `input-man.cpp` → Migrate to unified parser
   - Man pages share similar patterns: block elements, inline formatting, sections

3. **Code Deduplication Audit**
   - Identify remaining duplicated parsing logic
   - Extract common patterns to shared functions
   - Ensure single implementation for each feature

4. **Cleanup Checklist**
   - [ ] All format tests pass with unified parser
   - [ ] Legacy parser files removed
   - [ ] No duplicate implementations of common features
   - [ ] Documentation updated

### Long-term (Ecosystem)

1. **Additional Formats**
   - Djot (modern Markdown successor)
   - Creole (wiki standardization)
   - Man pages (Unix documentation)
   - Custom DSLs

2. **Two-way Conversion**
   - Parse and re-emit same format
   - Cross-format conversion
   - Preserve formatting preferences

3. **Language Server Protocol**
   - Diagnostics for editor integration
   - Completion suggestions
   - Hover information

---

## Summary

The Lambda lightweight markup parser provides a solid foundation for parsing multiple markup formats into a unified schema. Key strengths include comprehensive format support and proper schema output. Priority improvements should focus on:

1. **Testing**: Integrate CommonMark spec tests for compliance verification
2. **Error Reporting**: Leverage existing infrastructure for structured errors
3. **Code Organization**: Modularize for maintainability

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
