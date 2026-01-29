# Unified Markup Parser Design

## Overview

This document outlines the design for a unified markup parser that can handle multiple lightweight markup formats (Markdown, reStructuredText, Textile, Wiki markup, Org-mode) and output standardized Lambda nodes conforming to the schema defined in `Doc_Schema.md`.

## Design Philosophy

### Core Principles

1. **Unified Schema Output**: All parsers output Lambda nodes following the Mark Doc Schema, ensuring consistent data structures regardless of input format.

2. **Reusable Component Architecture**: Implement common parsing functions for document elements (headers, lists, tables, links, emphasis) that can be reused across formats with format-specific variations.

3. **Efficient Memory Management**: Reuse the same `StrBuf` (`input->sb`) throughout parsing to avoid temporary string creation, following the pattern established in `input-xml.cpp`.

4. **Incremental Implementation**: Build parser incrementally, starting with basic elements and adding complexity progressively with comprehensive test coverage.

5. **Format-Aware Variations**: Handle format-specific syntax variations through parameterized parsing functions and format detection.

## Architecture Overview

### Parser Structure

```
unified_markup_parser.cpp
├── Core Infrastructure
│   ├── Parser State Management
│   ├── Format Detection
│   └── Common Utilities
├── Block Element Parsers (Reusable)
│   ├── Heading Parser
│   ├── List Parser
│   ├── Table Parser
│   ├── Code Block Parser
│   ├── Quote Block Parser
│   └── Paragraph Parser
├── Inline Element Parsers (Reusable)
│   ├── Emphasis Parser
│   ├── Link Parser
│   ├── Code Span Parser
│   ├── Math Parser
│   └── Entity Parser
└── Format-Specific Adapters
    ├── Markdown Adapter
    ├── reStructuredText Adapter
    ├── Textile Adapter
    ├── Wiki Adapter
    └── Org-mode Adapter
```

### Core Data Structures

```cpp
typedef enum {
    MARKUP_MARKDOWN,
    MARKUP_RST,
    MARKUP_TEXTILE,
    MARKUP_WIKI,
    MARKUP_ORG,
    MARKUP_AUTO_DETECT
} MarkupFormat;

typedef struct {
    MarkupFormat format;
    const char* flavor;  // e.g., "github", "commonmark", "mediawiki"
    bool strict_mode;    // Strict vs. lenient parsing
} ParseConfig;

typedef struct {
    Input* input;
    ParseConfig config;
    char** lines;
    int line_count;
    int current_line;
    
    // Format-specific state
    struct {
        char list_markers[10];      // Stack of list markers
        int list_levels[10];        // Stack of list indentation levels
        int list_depth;             // Current nesting depth
        
        char table_state;           // Current table parsing state
        bool in_code_block;         // Whether we're in a code block
        char code_fence_char;       // Current code fence character
        int code_fence_length;      // Current code fence length
        
        bool in_math_block;         // Whether we're in math block
        char math_delimiter[10];    // Math block delimiter
    } state;
} MarkupParser;
```

## Component Design

### 1. Core Infrastructure

#### Parser State Management
```cpp
MarkupParser* parser_create(Input* input, ParseConfig config);
void parser_destroy(MarkupParser* parser);
void parser_reset_state(MarkupParser* parser);
```

#### Format Detection
```cpp
MarkupFormat detect_markup_format(const char* content, const char* filename);
const char* detect_markup_flavor(MarkupFormat format, const char* content);
```

### 2. Block Element Parsers (Reusable)

#### Heading Parser
```cpp
typedef struct {
    char marker;          // '#', '=', '-', etc.
    int min_markers;      // Minimum number of markers required
    int max_level;        // Maximum heading level
    bool requires_space;  // Whether space after marker is required
    bool symmetric;       // Whether closing markers required (markdown)
    bool underline;       // Whether underline style (rst)
} HeadingRules;

Item parse_heading(MarkupParser* parser, const char* line, HeadingRules* rules);
```

#### List Parser
```cpp
typedef struct {
    const char* bullet_markers;    // e.g., "*+-" for markdown
    const char* ordered_markers;   // e.g., ".)" for numbered lists
    bool indent_sensitive;         // Whether indentation determines nesting
    int indent_size;               // Standard indentation (spaces)
    bool allow_lazy_continuation;  // Lazy paragraph continuation
} ListRules;

Item parse_list_item(MarkupParser* parser, const char* line, ListRules* rules);
Item parse_list_block(MarkupParser* parser, ListRules* rules);
```

#### Table Parser
```cpp
typedef struct {
    const char* row_delimiter;     // e.g., "|" for markdown/wiki
    const char* header_separator;  // e.g., "|-|" pattern
    bool requires_header;          // Whether header row is mandatory
    bool allows_multiline_cells;   // Whether cells can span lines
    char* alignment_chars;         // Characters indicating alignment
} TableRules;

Item parse_table(MarkupParser* parser, TableRules* rules);
```

#### Code Block Parser
```cpp
typedef struct {
    const char* fence_chars;       // e.g., "`~" for markdown
    int min_fence_length;          // Minimum fence length
    bool requires_symmetric;       // Whether closing fence required
    bool supports_language;        // Whether language specification supported
    bool supports_indented;        // Whether indented code blocks supported
} CodeBlockRules;

Item parse_code_block(MarkupParser* parser, CodeBlockRules* rules);
```

### 3. Inline Element Parsers (Reusable)

#### Emphasis Parser
```cpp
typedef struct {
    const char* em_delimiters;     // e.g., "*_" for markdown
    const char* strong_delimiters; // e.g., "**__" for markdown
    bool allows_nesting;           // Whether emphasis can be nested
    bool requires_word_boundary;   // Whether word boundaries required
} EmphasisRules;

Item parse_emphasis(MarkupParser* parser, const char* text, int* pos, EmphasisRules* rules);
```

#### Link Parser
```cpp
typedef struct {
    const char* inline_pattern;    // e.g., "[text](url)" for markdown
    const char* reference_pattern; // e.g., "[text][ref]" for markdown
    const char* auto_pattern;      // e.g., "<url>" for markdown
    bool supports_titles;          // Whether link titles supported
    bool supports_attributes;      // Whether link attributes supported
} LinkRules;

Item parse_link(MarkupParser* parser, const char* text, int* pos, LinkRules* rules);
```

### 4. Format-Specific Adapters

Each format adapter provides format-specific rules to the reusable parsers:

#### Markdown Adapter
```cpp
static const HeadingRules markdown_heading_rules = {
    .marker = '#',
    .min_markers = 1,
    .max_level = 6,
    .requires_space = true,
    .symmetric = false,  // Can be, but not required
    .underline = false
};

static const ListRules markdown_list_rules = {
    .bullet_markers = "*+-",
    .ordered_markers = ".)",
    .indent_sensitive = true,
    .indent_size = 4,
    .allow_lazy_continuation = true
};
```

#### reStructuredText Adapter
```cpp
static const HeadingRules rst_heading_rules = {
    .marker = '=',  // Primary, but others supported
    .min_markers = 3,
    .max_level = 6,
    .requires_space = false,
    .symmetric = true,   // Can be over- and underlined
    .underline = true    // Primary style is underline
};

static const ListRules rst_list_rules = {
    .bullet_markers = "*+-",
    .ordered_markers = ".)",
    .indent_sensitive = true,
    .indent_size = 3,
    .allow_lazy_continuation = false
};
```

#### Textile Adapter
```cpp
static const HeadingRules textile_heading_rules = {
    .marker = 'h',  // h1. h2. etc.
    .min_markers = 1,
    .max_level = 6,
    .requires_space = false,
    .symmetric = false,
    .underline = false
};
```

#### Wiki Adapter
```cpp
static const HeadingRules wiki_heading_rules = {
    .marker = '=',
    .min_markers = 2,
    .max_level = 6,
    .requires_space = false,
    .symmetric = true,  // == Title ==
    .underline = false
};
```

#### Org-mode Adapter
```cpp
static const HeadingRules org_heading_rules = {
    .marker = '*',
    .min_markers = 1,
    .max_level = 10,  // Org supports deep nesting
    .requires_space = true,
    .symmetric = false,
    .underline = false
};
```

## Memory Management Strategy

### StrBuf Reuse Pattern

Following `input-xml.cpp`, the unified parser will reuse `input->sb` throughout:

```cpp
// Pattern for content parsing
static String* parse_content_to_string(MarkupParser* parser, const char* start, const char* end) {
    StrBuf* sb = parser->input->sb;
    strbuf_reset(sb);
    
    while (start < end) {
        // Process characters, handling escapes, entities, etc.
        strbuf_append_char(sb, *start);
        start++;
    }
    
    return strbuf_to_string(sb);
}
```

### Avoiding Temporary Allocations

1. **Line Processing**: Work directly with line pointers from `input_split_lines()`
2. **Content Accumulation**: Use `StrBuf` for building content incrementally
3. **String Creation**: Only create final strings when adding to elements
4. **Element Reuse**: Reuse element creation patterns from existing parsers

## Implementation Plan

### Phase 1: Core Infrastructure

**Files to Create:**
- `lambda/input/markup-parser.h` - Core interfaces and structures
- `lambda/input/input-markup.cpp` - Basic infrastructure

**Deliverables:**
1. Basic parser state management
2. Format detection utilities
3. Common utility functions
4. Memory management framework
5. Test framework setup

**Test Coverage:**
- Parser creation/destruction
- Format detection for each supported format
- Basic string buffer operations

### Phase 2: Block Element Parsers

**Deliverables:**
1. Heading parser (all formats)
2. Paragraph parser with line break handling
3. Code block parser (fenced and indented)
4. Horizontal rule parser
5. Basic list parser (single level)

**Test Coverage:**
- Headers: ATX (markdown), underline (rst), wiki-style, org-style
- Code blocks: fenced (markdown/rst), indented, language specification
- Horizontal rules: various marker styles
- Paragraphs: line breaks, continuations

### Phase 3: List Processing

**Deliverables:**
1. Multi-level list nesting
2. Mixed ordered/unordered lists
3. List item continuation
4. Definition lists (rst/org)
5. List tight/loose spacing

**Test Coverage:**
- Nested lists: various indentation styles
- Mixed list types within same document
- Complex list content (paragraphs, code blocks)
- Format-specific list markers

### Phase 4: Inline Element Parsers

**Deliverables:**
1. Emphasis parsing (bold, italic, combinations)
2. Link parsing (inline, reference, automatic)
3. Code spans with backtick handling
4. Basic entity/escape processing
5. Line break handling

**Test Coverage:**
- Emphasis: nested, mixed delimiters, word boundaries
- Links: various formats, with/without titles
- Code spans: nested backticks, escaping
- Entities: HTML entities, Unicode escapes

### Phase 5: Table Support

**Deliverables:**
1. Simple tables (markdown/textile style)
2. Grid tables (rst style)
3. Wiki-style tables
4. Table alignment detection
5. Multi-line cell content

**Test Coverage:**
- Table formats: pipe tables, grid tables, wiki tables
- Column alignment: left, right, center
- Complex table content: emphasis, links, code
- Malformed table handling

### Phase 6: Advanced Features

**Deliverables:**
1. Math support integration (existing math parser)
2. Footnote/citation support
3. Advanced directives (rst)
4. Template/macro support (wiki/org)
5. Metadata parsing (YAML frontmatter, org properties)

**Test Coverage:**
- Math: inline and display equations
- Citations: various citation styles
- Directives: code, figures, admonitions
- Metadata: YAML, org properties

### Phase 7: Integration and Performance

**Deliverables:**
1. Integration with existing input system
2. Performance optimization
3. Error handling and recovery
4. Comprehensive test suite
5. Documentation updates

**Test Coverage:**
- Cross-format compatibility tests
- Large document performance tests
- Error recovery scenarios
- Memory leak detection

## Algorithm Details

### List Processing Flow
1. **Detection**: `detect_block_type()` identifies list items
2. **Initialization**: `parse_list_structure()` creates appropriate list container
3. **State Management**: Tracks current depth and markers in parser state
4. **Content Parsing**: Processes immediate text and looks for nested content
5. **Recursion**: Handles nested lists by calling `parse_list_structure()` recursively
6. **Cleanup**: Pops state when exiting nested levels

### Indentation Logic
- **Base Indentation**: Establishes the reference level for list items
- **Continuation Detection**: Content indented more than base belongs to current item
- **Nesting Detection**: List items indented more than base start nested lists
- **Termination**: Content at or below base indent ends current list

## Testing Strategy

### Test Coverage Goals

1. **Format Detection**: Automatic detection of markup format
2. **Element Parsing**: All schema elements from Doc_Schema.md
3. **Cross-format Compatibility**: Same semantic output from different formats
4. **Edge Cases**: Malformed markup, nested structures, empty content
5. **Performance**: Large documents, deeply nested structures
6. **Memory**: No leaks, efficient buffer reuse

### Integration with Existing Tests

The unified parser will be tested alongside existing format-specific parsers:

```lambda
// Integration test comparing outputs
let md_unified = input('./sample.md', 'markup')
let md_specific = input('./sample.md', 'markdown')
compare_structures(md_unified, md_specific)

let rst_unified = input('./sample.rst', 'markup') 
let rst_specific = input('./sample.rst', 'rst')
compare_structures(rst_unified, rst_specific)
```

## Error Handling and Recovery

### Error Categories

1. **Syntax Errors**: Malformed markup that can't be parsed
2. **Semantic Errors**: Valid syntax but invalid document structure
3. **Format Ambiguity**: Markup that could be interpreted multiple ways
4. **Memory Errors**: Allocation failures, buffer overflows

### Recovery Strategies

1. **Graceful Degradation**: Parse what's possible, mark errors
2. **Context Preservation**: Maintain parsing state across errors
3. **Format Fallback**: Try alternative format interpretations
4. **Content Preservation**: Don't lose content due to formatting errors

## Future Extensions

### Additional Format Support

- **AsciiDoc**: Could be added with new adapter
- **Creole**: Wiki markup standard
- **BBCode**: Forum markup
- **Custom Formats**: Extensible adapter system

### Enhanced Features

- **Syntax Highlighting**: Language-aware code block processing
- **Live Editing**: Incremental parsing for editors
- **Format Conversion**: Lossless conversion between formats
- **Schema Validation**: Validate output against Doc_Schema.md

## Success Metrics

1. **Coverage**: Support all elements from Doc_Schema.md
2. **Compatibility**: Pass existing format-specific test suites
3. **Performance**: ≤20% overhead vs. format-specific parsers
4. **Memory**: Zero memory leaks, efficient buffer usage
5. **Maintainability**: Shared code reduces duplication by >60%

This design provides a solid foundation for implementing a comprehensive, efficient, and maintainable unified markup parser that will significantly reduce code duplication while providing consistent, high-quality parsing across multiple lightweight markup formats.
