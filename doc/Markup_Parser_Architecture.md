# Markup Parser Architecture

This document describes the modular architecture of Lambda's lightweight markup parser system, which handles parsing and conversion between various markup formats (Markdown, RST, Wiki, Textile, Org-mode, AsciiDoc, Man pages).

## Overview

The markup parser was refactored from a monolithic 6,234-line implementation into a modular, maintainable architecture with clear separation of concerns:

```
lambda/input/markup/
├── markup_common.hpp      # Shared types, enums, and constants
├── markup_parser.hpp/cpp  # Main parser class (extends InputContext)
├── format_adapter.hpp     # Format adapter interface
├── format_registry.cpp    # Format registration and detection
├── block/                 # Block-level parsers
│   ├── block_common.hpp   # Block parser interfaces
│   ├── block_header.cpp   # Header parsing (ATX, Setext, format-specific)
│   ├── block_list.cpp     # List parsing (ordered, unordered, nested)
│   ├── block_code.cpp     # Code blocks (fenced, indented)
│   ├── block_quote.cpp    # Blockquote parsing
│   ├── block_table.cpp    # Table parsing (GFM, Wiki, RST, grid)
│   ├── block_paragraph.cpp # Paragraph parsing
│   ├── block_divider.cpp  # Horizontal rules
│   ├── block_detection.cpp # Block type detection
│   └── block_utils.cpp    # Shared block utilities
├── inline/                # Inline-level parsers
│   ├── inline_common.hpp  # Inline parser interfaces
│   ├── inline_emphasis.cpp # Bold/italic (**/__, */_, etc.)
│   ├── inline_code.cpp    # Inline code (`code`)
│   ├── inline_link.cpp    # Links and references
│   ├── inline_image.cpp   # Images
│   ├── inline_math.cpp    # Math ($...$, $$...$$)
│   ├── inline_special.cpp # Special characters, escapes
│   ├── inline_wiki.cpp    # Wiki-specific ([[links]], etc.)
│   ├── inline_format_specific.cpp # Format-specific inline elements
│   └── inline_spans.cpp   # Text span management
└── format/                # Format-specific adapters
    ├── markdown_adapter.cpp  # CommonMark/GFM Markdown
    ├── rst_adapter.cpp       # reStructuredText
    ├── wiki_adapter.cpp      # MediaWiki
    ├── textile_adapter.cpp   # Textile
    ├── org_adapter.cpp       # Org-mode
    ├── asciidoc_adapter.cpp  # AsciiDoc
    └── man_adapter.cpp       # Unix man pages
```

## Core Classes

### MarkupParser (markup_parser.hpp)

The central class that coordinates parsing. Extends `InputContext` to inherit MarkBuilder, error tracking, and source tracking infrastructure.

```cpp
class MarkupParser : public InputContext {
public:
    // Parse content with auto-detection or specified format
    Item parse(const char* content, size_t length, ParseConfig config = ParseConfig());

    // Current format adapter (set during parsing)
    FormatAdapter* adapter;

    // Parser state
    ParserState state;

    // Line-based iteration
    const char* current_line;
    int current_line_number;
    bool advance_line();

    // Error reporting
    void warnUnclosed(const char* delimiter, int start_line);
    void warnInvalidSyntax(const char* element, const char* expected);
    void noteUnresolvedReference(const char* ref_name);
};
```

### FormatAdapter (format_adapter.hpp)

Abstract interface for format-specific behavior. Each format (Markdown, RST, etc.) implements its own adapter.

```cpp
class FormatAdapter {
public:
    virtual Format get_format() const = 0;
    virtual const char* get_name() const = 0;

    // Detection methods
    virtual bool detect_header(const char* line, int* level, HeaderStyle* style) = 0;
    virtual bool detect_emphasis(const char* text, EmphasisType* type, int* length) = 0;
    virtual bool detect_code_fence(const char* line, CodeFenceInfo* info) = 0;
    virtual bool detect_list_item(const char* line, ListMarkerInfo* info) = 0;
    virtual bool detect_table_row(const char* line, TableRowInfo* info) = 0;

    // Format-specific parsing
    virtual Item parse_directive(MarkupParser* parser, const char* line);  // RST
    virtual Item parse_wiki_link(MarkupParser* parser, const char* text);  // Wiki
};
```

### ParseConfig (markup_parser.hpp)

Configuration for parsing behavior:

```cpp
struct ParseConfig {
    Format format;           // Target format (or AUTO_DETECT)
    Flavor flavor;           // Format variant (e.g., GFM, CommonMark)
    bool strict_mode;        // Strict vs lenient parsing
    bool collect_metadata;   // Parse frontmatter/properties
    bool resolve_refs;       // Resolve link/footnote references
};
```

## Block Parsers (block/)

Block parsers handle document structure elements. Each returns an `Item` containing the parsed element or `ITEM_ERROR`.

### Detection Pattern

```cpp
// block_detection.cpp
BlockType detect_block_type(MarkupParser* parser, const char* line) {
    if (is_empty_line(line)) return BLOCK_BLANK;
    if (parser->adapter->detect_code_fence(line, nullptr)) return BLOCK_CODE_FENCE;
    if (parser->adapter->detect_header(line, nullptr, nullptr)) return BLOCK_HEADER;
    if (parser->adapter->detect_list_item(line, nullptr)) return BLOCK_LIST;
    if (is_blockquote_line(line)) return BLOCK_QUOTE;
    if (is_table_line(line)) return BLOCK_TABLE;
    if (is_thematic_break(line)) return BLOCK_DIVIDER;
    return BLOCK_PARAGRAPH;
}
```

### Block Parser Function Signatures

```cpp
// block_common.hpp
Item parse_header(MarkupParser* parser, const char* line);
Item parse_list_structure(MarkupParser* parser, int base_indent);
Item parse_code_block(MarkupParser* parser, const char* line);
Item parse_blockquote(MarkupParser* parser, const char* line);
Item parse_table_row(MarkupParser* parser, const char* line);
Item parse_divider(MarkupParser* parser);
Item parse_paragraph(MarkupParser* parser, const char* line);
```

### Error Handling in Block Parsers

Block parsers report errors via the parser's error infrastructure:

```cpp
// Example from block_code.cpp
if (!found_close) {
    parser->warnUnclosed("```", fence_start_line);
    // Continue parsing (recovery) rather than failing
}

// Example from block_table.cpp
if (table_element->content_length == 0) {
    parser->warnInvalidSyntax("table", "at least one row with | delimiters");
}
```

## Inline Parsers (inline/)

Inline parsers handle text-level elements within blocks. They return:
- `Item` with parsed element on success
- `ITEM_UNDEFINED` if pattern doesn't match (not an error, allows fallback)
- `ITEM_ERROR` on allocation failure

### Inline Parser Function Signatures

```cpp
// inline_common.hpp
Item parse_emphasis(MarkupParser* parser, const char* text, int* consumed);
Item parse_inline_code(MarkupParser* parser, const char* text, int* consumed);
Item parse_link(MarkupParser* parser, const char* text, int* consumed);
Item parse_image(MarkupParser* parser, const char* text, int* consumed);
Item parse_inline_math(MarkupParser* parser, const char* text, int* consumed);
Item parse_special_char(MarkupParser* parser, const char* text, int* consumed);
```

### Inline Processing Pattern

```cpp
// inline_spans.cpp
Item parse_inline_spans(MarkupParser* parser, const char* text) {
    StrBuf content;
    while (*text) {
        // Try each inline parser
        Item result;
        int consumed;

        if ((result = parse_emphasis(parser, text, &consumed)).item != ITEM_UNDEFINED.item) {
            flush_text_span(&content);
            add_child(result);
            text += consumed;
        } else if ((result = parse_inline_code(parser, text, &consumed)).item != ITEM_UNDEFINED.item) {
            // ... similar pattern
        } else {
            // Accumulate as plain text
            append_char(&content, *text++);
        }
    }
    flush_text_span(&content);
    return container;
}
```

## Format Adapters (format/)

Each format adapter implements format-specific detection and parsing rules.

### Example: Markdown Adapter

```cpp
// markdown_adapter.cpp
class MarkdownAdapter : public FormatAdapter {
public:
    Format get_format() const override { return Format::MARKDOWN; }

    bool detect_header(const char* line, int* level, HeaderStyle* style) override {
        // ATX-style: # Heading
        if (*line == '#') {
            int lvl = 0;
            while (line[lvl] == '#' && lvl < 6) lvl++;
            if (isspace(line[lvl]) || line[lvl] == '\0') {
                if (level) *level = lvl;
                if (style) *style = HEADER_ATX;
                return true;
            }
        }
        return false;
    }

    bool detect_emphasis(const char* text, EmphasisType* type, int* len) override {
        // ** or __ for bold, * or _ for italic
        if (*text == '*' || *text == '_') {
            if (text[1] == text[0]) {
                if (type) *type = EMPHASIS_BOLD;
                if (len) *len = 2;
                return true;
            }
            if (type) *type = EMPHASIS_ITALIC;
            if (len) *len = 1;
            return true;
        }
        return false;
    }
};
```

### Example: RST Adapter

```cpp
// rst_adapter.cpp
class RstAdapter : public FormatAdapter {
public:
    Format get_format() const override { return Format::RST; }

    bool detect_header(const char* line, int* level, HeaderStyle* style) override {
        // RST uses underline style (======, ------, etc.)
        // Must check if previous line was text
        if (style) *style = HEADER_UNDERLINE;
        return is_rst_underline(line);
    }

    Item parse_directive(MarkupParser* parser, const char* line) override {
        // Handle RST directives: .. directive:: argument
        if (starts_with(line, ".. ")) {
            // Parse directive name and arguments
            return create_directive_element(parser, line);
        }
        return ITEM_UNDEFINED;
    }
};
```

## Error Handling System

The parser uses a structured error system with categories and severity levels:

```cpp
// parse_error.hpp
enum class MarkupErrorCategory {
    SYNTAX,         // General syntax errors
    UNCLOSED,       // Unclosed delimiters
    REFERENCE,      // Unresolved references
    ENCODING,       // Character encoding issues
    DEPRECATED,     // Deprecated syntax warnings
    UNEXPECTED,     // Unexpected content
    LIMIT_EXCEEDED  // Size/depth limits
};

enum class ParseErrorSeverity {
    WARNING,
    ERROR,
    INFO
};
```

### Error Reporting Methods

```cpp
// markup_parser.cpp
void MarkupParser::warnUnclosed(const char* delimiter, int start_line) {
    addMarkupError(MarkupErrorCategory::UNCLOSED, ParseErrorSeverity::WARNING,
        "Unclosed '%s' starting at line %d", delimiter, start_line);
}

void MarkupParser::warnInvalidSyntax(const char* element, const char* expected) {
    addMarkupError(MarkupErrorCategory::SYNTAX, ParseErrorSeverity::WARNING,
        "Invalid %s: expected %s", element, expected);
}
```

## Format Detection

The parser can auto-detect format from file extension or content:

```cpp
// format_registry.cpp
Format detect_format_from_extension(const char* filename) {
    const char* ext = strrchr(filename, '.');
    if (!ext) return Format::MARKDOWN;  // Default

    if (strcasecmp(ext, ".md") == 0 || strcasecmp(ext, ".markdown") == 0)
        return Format::MARKDOWN;
    if (strcasecmp(ext, ".rst") == 0)
        return Format::RST;
    if (strcasecmp(ext, ".wiki") == 0)
        return Format::WIKI;
    // ... etc
}

Format detect_format_from_content(const char* content) {
    // Look for format-specific patterns
    if (strstr(content, ".. ") && strstr(content, "::"))
        return Format::RST;
    if (strstr(content, "[[") && strstr(content, "]]"))
        return Format::WIKI;
    // Default to Markdown
    return Format::MARKDOWN;
}
```

## Adding a New Format

To add support for a new markup format:

1. **Define format enum** in `markup_common.hpp`:
   ```cpp
   enum class Format {
       // ... existing formats
       NEW_FORMAT
   };
   ```

2. **Create format adapter** in `format/`:
   ```cpp
   // format/newformat_adapter.cpp
   class NewFormatAdapter : public FormatAdapter {
       // Implement all virtual methods
   };
   ```

3. **Register adapter** in `format_registry.cpp`:
   ```cpp
   static NewFormatAdapter new_format_adapter;
   adapters[Format::NEW_FORMAT] = &new_format_adapter;
   ```

4. **Add file extension mapping** in `detect_format_from_extension()`.

## HTML5 Integration for Nested HTML

Markdown documents can contain raw HTML that should pass through without markdown processing. The markup parser integrates with Lambda's HTML5 parser to build a proper DOM tree from all HTML fragments encountered during parsing.

### Architecture

```
Markdown Document
      │
      ▼
┌─────────────────────┐
│   MarkupParser      │
│                     │
│  html5_parser_ ─────┼──► Html5Parser (fragment mode)
│                     │         │
└─────────────────────┘         ▼
      │                   Accumulates all HTML
      │                         │
      ▼                         ▼
┌─────────────────────────────────────┐
│            Output Document          │
│                                     │
│  doc                                │
│  ├── body (markdown content)        │
│  │   ├── html-block (raw HTML)      │
│  │   ├── p (paragraphs)             │
│  │   │   └── raw-html (inline HTML) │
│  │   └── ...                        │
│  └── html-dom (parsed HTML5 DOM)    │
│      └── table, div, etc...         │
└─────────────────────────────────────┘
```

### Key Components

1. **`Html5Parser* html5_parser_`** - A shared HTML5 fragment parser that accumulates all HTML content from the markdown document into a single DOM tree.

2. **`getOrCreateHtml5Parser()`** - Lazily creates the HTML5 parser on first HTML encounter.

3. **`parseHtmlFragment(const char* html)`** - Feeds HTML content (block or inline) to the shared parser.

4. **`getHtmlBody()`** - Retrieves the parsed HTML body element after document parsing completes.

### HTML Block Handling

When a block-level HTML element is detected (CommonMark types 1-7), the parser:

1. Collects all lines belonging to the HTML block
2. Creates an `html-block` element with the raw content (for verbatim output)
3. Feeds the HTML to the shared HTML5 parser via `parseHtmlFragment()`

```cpp
// block_html.cpp
Item parse_html_block(MarkupParser* parser, const char* line) {
    // ... collect HTML block content into sb ...

    // Feed to HTML5 parser for DOM construction
    parser->parseHtmlFragment(sb->str->chars);

    // Also preserve raw content for output
    Element* html_elem = create_element(parser, "html-block");
    // ... add raw content as child ...
}
```

### Inline HTML Handling

Inline HTML tags (`<em>`, `</em>`, `<a href="...">`, etc.) are similarly processed:

1. The inline parser detects and extracts the HTML tag/comment/CDATA
2. Creates a `raw-html` element with the verbatim content
3. Feeds the HTML to the shared parser

### HTML5 Fragment Parsing

The HTML5 parser operates in "fragment mode" for markdown integration:

```cpp
// html5_tree_builder.cpp
Html5Parser* html5_fragment_parser_create(Pool* pool, Arena* arena, Input* input) {
    Html5Parser* parser = html5_parser_create(pool, arena, input);

    // Set up minimal document structure: #document -> html -> body
    parser->document = builder.element("#document").final().element;
    parser->html_element = builder.element("html").final().element;
    Element* body = builder.element("body").final().element;

    // Start in body mode (fragments are parsed as body content)
    parser->mode = HTML5_MODE_IN_BODY;

    return parser;
}
```

This allows:
- Parsing without requiring a complete HTML document
- Proper handling of nested structures (tables, lists, etc.)
- Application of HTML5 parsing rules (implicit tbody, etc.)

### Document Output

After parsing completes, the `html-dom` element is added to the document if any HTML was encountered:

```cpp
// block_document.cpp
Item parse_document(MarkupParser* parser) {
    // ... parse all blocks ...

    // Add HTML DOM if HTML was parsed
    Element* html_body = parser->getHtmlBody();
    if (html_body && html_body->length > 0) {
        Element* html_dom = create_element(parser, "html-dom");
        // Copy children from HTML5 body
        for (size_t i = 0; i < html_body->length; i++) {
            list_push((List*)html_dom, html_body->items[i]);
        }
        list_push((List*)doc, Item{.element = html_dom});
    }

    return Item{.element = doc};
}
```

### Benefits

- **Single DOM tree** - All HTML fragments are parsed into one coherent DOM
- **Proper nesting** - HTML5 parser handles implicit elements and nesting rules
- **Dual output** - Raw HTML preserved in `html-block`/`raw-html` for verbatim output, parsed DOM in `html-dom` for semantic processing
- **Lazy initialization** - HTML5 parser only created when HTML content is encountered

## Testing

Unit tests are in `test/test_markup_modular_gtest.cpp`:

```bash
# Run all markup tests
./test/test_markup_modular_gtest.exe

# Run specific test suite
./test/test_markup_modular_gtest.exe --gtest_filter=BlockParserTest.*
```

Test suites:
- `BlockParserTest` - Block-level parsing (headers, lists, code, tables, etc.)
- `InlineParserTest` - Inline elements (emphasis, links, code, images, etc.)
- `FormatAdapterTest` - Format detection and format-specific parsing
- `ErrorRecoveryTest` - Malformed input handling
- `MathBlockTest` - Math block parsing
- `ComplexDocumentTest` - Full document integration tests

## Performance Considerations

1. **Single pass parsing** - The parser processes input in a single forward pass
2. **Minimal allocations** - Uses MarkBuilder's arena allocation
3. **No backtracking** - Block detection is deterministic
4. **Inline state reset** - Each block's inline content is parsed independently

## Memory Management

The parser inherits `InputContext` which provides:
- `MarkBuilder` for creating Lambda data structures
- Pool allocation for string interning
- Automatic cleanup when `Input` is released
