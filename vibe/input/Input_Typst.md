# Typst Input Support for Lambda

## Overview

This document proposes adding **Typst** as a new lightweight markup format to Lambda's unified markup parser system. Typst is a modern markup-based typesetting system designed as a powerful, easy-to-learn alternative to LaTeX.

Rather than implementing a separate tree-sitter based parser (like `input-latex-ts.cpp`), we will integrate Typst as another format adapter within the existing `markup_parser` framework, alongside Markdown, RST, Wiki, Org, AsciiDoc, Textile, and Man.

## Rationale for Using markup_parser

Typst's markup syntax shares significant overlap with Markdown:

| Feature | Markdown | Typst |
|---------|----------|-------|
| Bold | `**bold**` | `*bold*` |
| Italic | `*italic*` / `_italic_` | `_italic_` |
| Headings | `# Heading` | `= Heading` |
| Bullet lists | `- item` | `- item` |
| Numbered lists | `1. item` | `+ item` |
| Code inline | `` `code` `` | `` `code` `` |
| Code blocks | ` ```lang ` | ` ```lang ` |
| Links | `[text](url)` | `#link("url")[text]` |
| Math inline | `$x^2$` | `$x^2$` |
| Math block | `$$...$$` | `$ ... $` (with spaces) |
| Raw/verbatim | ` ``` ` | ` ``` ` or `` ` `` |
| Comments | N/A | `// line` or `/* block */` |

The shared infrastructure of block parsers, inline parsers, and format adapters makes this approach:
- **Efficient**: Reuse 80%+ of existing parsing logic
- **Consistent**: Same element tree output as other markup formats
- **Maintainable**: Single codebase for similar formats

## Typst Syntax Summary

### Modes

Typst has three syntactical modes:

1. **Markup mode** (default) - Document content with lightweight syntax
2. **Math mode** - Mathematical formulas between `$...$`
3. **Code mode** - Scripting via `#` prefix or `{...}` blocks

### Markup Syntax

| Element | Syntax | Notes |
|---------|--------|-------|
| Paragraph | Blank line | Same as Markdown |
| Strong | `*strong*` | Single `*` (unlike Markdown's `**`) |
| Emphasis | `_emphasis_` | Same as Markdown |
| Heading | `= H1`, `== H2`, etc. | Uses `=` instead of `#` |
| Bullet list | `- item` | Same as Markdown |
| Numbered list | `+ item` | Uses `+` instead of `1.` |
| Term list | `/ Term: description` | Definition list |
| Raw text | `` `code` `` | Same as Markdown |
| Link | `https://...` or `#link(...)` | Auto-links or function |
| Label | `<intro>` | For cross-references |
| Reference | `@intro` | Cite a label |
| Line break | `\` | Backslash at end |
| Smart quotes | `'single'` `"double"` | Automatic |
| Comment | `// line` `/* block */` | C-style comments |

### Math Mode

| Element | Syntax | Notes |
|---------|--------|-------|
| Inline math | `$x^2$` | No spaces inside `$` |
| Block math | `$ x^2 $` | Spaces inside `$` |
| Subscript | `$x_1$` | Same as LaTeX |
| Superscript | `$x^2$` | Same as LaTeX |
| Fraction | `$(a+b)/c$` | Simplified syntax |

### Code Mode

Code expressions start with `#`:

```typst
#let x = 1
#if x > 0 { [positive] }
#for i in range(5) { [Item #i] }
```

## Architecture Design

### New Files

| File | Purpose |
|------|---------|
| `lambda/input/markup/format/typst_adapter.cpp` | Typst format adapter implementation |

### Modified Files

| File | Changes |
|------|---------|
| `lambda/input/markup/markup_common.hpp` | Add `Format::TYPST` enum |
| `lambda/input/markup/format_registry.cpp` | Register Typst adapter |
| `lambda/input/markup-format.h` | Add `MARKUP_TYPST` enum |
| `lambda/input/input.cpp` | Add MIME type and extension detection |

### Format Enum Changes

**markup_common.hpp:**
```cpp
enum class Format {
    MARKDOWN,
    RST,
    WIKI,
    TEXTILE,
    ORG,
    ASCIIDOC,
    MAN,
    TYPST,          // NEW
    AUTO_DETECT
};
```

**markup-format.h:**
```c
typedef enum {
    MARKUP_MARKDOWN,
    MARKUP_RST,
    MARKUP_TEXTILE,
    MARKUP_WIKI,
    MARKUP_ORG,
    MARKUP_ASCIIDOC,
    MARKUP_MAN,
    MARKUP_TYPST,       // NEW
    MARKUP_AUTO_DETECT
} MarkupFormat;
```

## TypstAdapter Implementation

### Class Structure

```cpp
// typst_adapter.cpp

class TypstAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::TYPST; }
    const char* name() const override { return "typst"; }
    
    const char* const* extensions() const override {
        static const char* exts[] = {".typ", ".typst", nullptr};
        return exts;
    }
    
    // Block detection
    HeaderInfo detectHeader(const char* line, const char* next_line) override;
    ListItemInfo detectListItem(const char* line) override;
    CodeFenceInfo detectCodeFence(const char* line) override;
    bool isCodeFenceClose(const char* line, const CodeFenceInfo& open_info) override;
    BlockquoteInfo detectBlockquote(const char* line) override;
    bool detectTable(const char* line, const char* next_line) override;
    bool detectThematicBreak(const char* line) override;
    
    // Inline detection
    const DelimiterSpec* emphasisDelimiters() const override;
    size_t emphasisDelimiterCount() const override;
    LinkInfo detectLink(const char* pos) override;
    LinkInfo detectImage(const char* pos) override;
    
    // Feature support
    bool supportsFeature(const char* feature) const override;
    const char* escapableChars() const override;
};
```

### Key Detection Methods

#### Header Detection

Typst uses `=` for headings (unlike Markdown's `#`):

```cpp
HeaderInfo TypstAdapter::detectHeader(const char* line, const char* next_line) {
    HeaderInfo info;
    const char* p = line;
    
    // Skip leading whitespace (Typst allows some indentation)
    int spaces = 0;
    while (*p == ' ' && spaces < 4) { spaces++; p++; }
    
    // Typst headings: = H1, == H2, === H3, etc.
    if (*p == '=') {
        int level = 0;
        while (*p == '=' && level < 7) { level++; p++; }
        
        // Must be followed by space or end of line
        if (level >= 1 && level <= 6 && 
            (*p == ' ' || *p == '\t' || *p == '\0' || *p == '\n')) {
            info.level = level;
            info.valid = true;
            
            // Skip whitespace after =
            while (*p == ' ' || *p == '\t') p++;
            info.text_start = p;
            
            // Find end of line
            info.text_end = p;
            while (*info.text_end && *info.text_end != '\n') {
                info.text_end++;
            }
            // Trim trailing whitespace
            while (info.text_end > info.text_start && 
                   (*(info.text_end-1) == ' ' || *(info.text_end-1) == '\t')) {
                info.text_end--;
            }
        }
    }
    
    return info;
}
```

#### List Detection

Typst uses `-` for bullets and `+` for numbered:

```cpp
ListItemInfo TypstAdapter::detectListItem(const char* line) {
    ListItemInfo info;
    const char* p = line;
    
    // Count indentation
    while (*p == ' ') { info.indent++; p++; }
    
    // Bullet list: - item
    if (*p == '-' && (*(p+1) == ' ' || *(p+1) == '\t')) {
        info.marker = '-';
        info.is_ordered = false;
        info.marker_end = p + 1;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        info.text_start = p;
        info.valid = true;
    }
    // Numbered list: + item (Typst auto-numbers)
    else if (*p == '+' && (*(p+1) == ' ' || *(p+1) == '\t')) {
        info.marker = '+';
        info.is_ordered = true;
        info.number = 1;  // Typst auto-increments
        info.marker_end = p + 1;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        info.text_start = p;
        info.valid = true;
    }
    // Term list: / Term: description
    else if (*p == '/' && *(p+1) == ' ') {
        info.marker = '/';
        info.is_ordered = false;
        info.is_definition = true;  // May need to add this field
        info.marker_end = p + 1;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        info.text_start = p;
        info.valid = true;
    }
    
    return info;
}
```

#### Emphasis Delimiters

Typst uses single `*` for bold (unlike Markdown's `**`):

```cpp
static const DelimiterSpec TYPST_EMPHASIS_DELIMITERS[] = {
    // Strong: *text*
    {"*", "*", InlineType::BOLD, 1, 1, true, true},
    // Emphasis: _text_
    {"_", "_", InlineType::ITALIC, 1, 1, true, true},
};

const DelimiterSpec* TypstAdapter::emphasisDelimiters() const {
    return TYPST_EMPHASIS_DELIMITERS;
}

size_t TypstAdapter::emphasisDelimiterCount() const {
    return sizeof(TYPST_EMPHASIS_DELIMITERS) / sizeof(TYPST_EMPHASIS_DELIMITERS[0]);
}
```

#### Code Fence Detection

Typst uses backticks like Markdown, with optional language:

```cpp
CodeFenceInfo TypstAdapter::detectCodeFence(const char* line) {
    CodeFenceInfo info;
    const char* p = line;
    
    // Skip leading whitespace
    int spaces = 0;
    while (*p == ' ' && spaces < 4) { spaces++; p++; }
    
    // Fenced code: ```lang or ```
    if (*p == '`') {
        int count = 0;
        while (*p == '`') { count++; p++; }
        
        if (count >= 3) {
            info.valid = true;
            info.fence_char = '`';
            info.fence_length = count;
            info.indent = spaces;
            
            // Parse language identifier
            while (*p == ' ' || *p == '\t') p++;
            const char* lang_start = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '`') {
                p++;
            }
            if (p > lang_start) {
                size_t len = p - lang_start;
                if (len < sizeof(info.language)) {
                    memcpy(info.language, lang_start, len);
                    info.language[len] = '\0';
                }
            }
        }
    }
    
    return info;
}
```

#### Comment Detection

Typst has C-style comments that Markdown lacks:

```cpp
bool TypstAdapter::detectComment(const char* line, CommentInfo* info) {
    const char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    
    // Single-line comment: // ...
    if (*p == '/' && *(p+1) == '/') {
        if (info) {
            info->type = COMMENT_LINE;
            info->content_start = p + 2;
        }
        return true;
    }
    
    // Block comment start: /* ...
    if (*p == '/' && *(p+1) == '*') {
        if (info) {
            info->type = COMMENT_BLOCK_START;
            info->content_start = p + 2;
        }
        return true;
    }
    
    return false;
}
```

### Feature Support

```cpp
bool TypstAdapter::supportsFeature(const char* feature) const {
    if (strcmp(feature, "math") == 0) return true;
    if (strcmp(feature, "code_expressions") == 0) return true;
    if (strcmp(feature, "labels") == 0) return true;
    if (strcmp(feature, "references") == 0) return true;
    if (strcmp(feature, "smart_quotes") == 0) return true;
    if (strcmp(feature, "definition_lists") == 0) return true;
    if (strcmp(feature, "comments") == 0) return true;
    return false;
}
```

## Special Handling

### Code Expressions (`#`)

Typst's `#` prefix for code expressions needs special handling in inline parsing:

```cpp
// In inline parser, detect #expression
if (*pos == '#' && !parser->isEscaped(text, pos)) {
    if (parser->adapter()->format() == Format::TYPST) {
        // Parse Typst code expression
        return parseTypstCodeExpression(parser, pos);
    }
}
```

Code expressions can be:
- Simple: `#x`, `#func()`
- Block: `#{ ... }`
- Content: `#[ ... ]`

For the initial implementation, we can:
1. Emit code expressions as `<code-expr>` elements
2. Store the raw expression text
3. Optionally parse nested content blocks

### Math Mode

Typst math is similar to LaTeX but with some differences:
- Inline: `$x^2$` (no spaces)
- Block: `$ x^2 $` (with spaces)
- Simplified syntax: `(a+b)/c` instead of `\frac{a+b}{c}`

The existing math handling in `markup_parser` can be extended:

```cpp
// Detect math mode
if (*pos == '$') {
    // Check for block math (has leading/trailing space inside $)
    bool is_block = false;
    if (*(pos+1) == ' ' || *(pos+1) == '\t' || *(pos+1) == '\n') {
        is_block = true;
    }
    // ... parse math content
}
```

### Labels and References

Typst uses `<label>` and `@ref` syntax:

```cpp
// Detect label: <name>
if (*pos == '<' && parser->adapter()->format() == Format::TYPST) {
    const char* end = strchr(pos + 1, '>');
    if (end && !contains_whitespace(pos + 1, end)) {
        // Create label element
        return createLabel(parser, pos + 1, end);
    }
}

// Detect reference: @name
if (*pos == '@' && parser->adapter()->format() == Format::TYPST) {
    const char* start = pos + 1;
    const char* end = start;
    while (is_identifier_char(*end)) end++;
    if (end > start) {
        return createReference(parser, start, end);
    }
}
```

## Element Mapping

| Typst Construct | Lambda Element | Attributes |
|-----------------|----------------|------------|
| `= Heading` | `<heading>` | `level` |
| `*bold*` | `<strong>` | |
| `_italic_` | `<emph>` | |
| `- item` | `<item>` in `<list>` | `marker="-"` |
| `+ item` | `<item>` in `<enum>` | |
| `/ Term: desc` | `<item>` in `<terms>` | `term` attr |
| `` `code` `` | `<code>` | |
| ` ```lang ` | `<code-block>` | `lang` |
| `$math$` | `<math>` | `display="inline"` |
| `$ math $` | `<math>` | `display="block"` |
| `#expr` | `<code-expr>` | `lang="typst"` |
| `<label>` | `<label>` | `name` |
| `@ref` | `<ref>` | `target` |
| `// comment` | `<comment>` | |
| `#link(url)[text]` | `<link>` | `url` |
| `#image(path)` | `<image>` | `src` |

## Implementation Phases

### Phase 1: Core Adapter (1 week)

1. Create `typst_adapter.cpp` with:
   - Header detection (`=` syntax)
   - List detection (`-`, `+`, `/` markers)
   - Code fence detection
   - Basic emphasis (`*` for bold, `_` for italic)

2. Register in `format_registry.cpp`

3. Add file extension detection (`.typ`, `.typst`)

4. Add MIME type (`text/typst`, `application/typst`)

### Phase 2: Inline Features (1 week)

1. Raw text / inline code (`` ` ``)
2. Math mode (`$...$` and `$ ... $`)
3. Labels (`<name>`)
4. References (`@name`)
5. Auto-links (URLs)
6. Smart quotes

### Phase 3: Code Expressions (1 week)

1. Simple expressions (`#var`, `#func()`)
2. Content blocks (`#[...]`)
3. Code blocks (`#{ ... }`)
4. Common functions: `#link()`, `#image()`, `#text()`, etc.

### Phase 4: Advanced Features (optional)

1. Term lists (`/ Term: description`)
2. Comments (`//` and `/* */`)
3. Show/set rules (parse but preserve as raw)
4. Import statements

## Testing Strategy

### Unit Tests

Create `test/test_input_typst.cpp`:

```cpp
TEST(TypstInput, Headings) {
    const char* source = "= Level 1\n== Level 2\n=== Level 3";
    Input* input = input_from_source(source, nullptr, str("typst"), nullptr);
    // Verify heading elements with correct levels
}

TEST(TypstInput, Emphasis) {
    const char* source = "*bold* and _italic_";
    // Verify strong and emph elements
}

TEST(TypstInput, Lists) {
    const char* source = "- bullet\n+ numbered\n/ Term: definition";
    // Verify list structures
}

TEST(TypstInput, Math) {
    const char* source = "Inline $x^2$ and block:\n$ y = mx + b $";
    // Verify math elements with display attribute
}

TEST(TypstInput, CodeExpressions) {
    const char* source = "#let x = 1\nValue is #x";
    // Verify code-expr elements
}
```

### Integration Tests

Create test files in `test/input/typst/`:
- `basic.typ` - Basic markup elements
- `math.typ` - Math expressions
- `lists.typ` - Various list types
- `code.typ` - Code expressions and blocks
- `document.typ` - Full document structure

## CLI Integration

```bash
# Parse Typst file
./lambda.exe convert document.typ -t html -o output.html

# Explicit format specification
./lambda.exe convert input.txt -f typst -t markdown -o output.md

# Validate Typst document
./lambda.exe validate document.typ
```

## MIME Type Registration

Add to `input.cpp`:

```cpp
// In mime_to_parser_type():
if (strcmp(mime_type, "text/typst") == 0) return "typst";
if (strcmp(mime_type, "application/typst") == 0) return "typst";

// In input_from_source():
else if (strcmp(effective_type, "typst") == 0) {
    input->root = input_markup_with_format(input, source, MARKUP_TYPST);
}
```

## Comparison with Tree-sitter Approach

| Aspect              | markup_parser Approach         | tree-sitter Approach              |
| ------------------- | ------------------------------ | --------------------------------- |
| Code reuse          | High (80%+ shared)             | Low (new parser)                  |
| Maintenance         | Single codebase                | Separate codebase                 |
| Build complexity    | None (C++ only)                | External grammar build            |
| Error recovery      | Shared infrastructure          | Grammar-specific                  |
| Code expressions    | Limited (text capture)         | Full AST available                |
| Batch performance   | Excellent (direct to elements) | Good (CST + traversal overhead)   |
| Incremental parsing | N/A (full reparse)             | Excellent (editor use case)       |
| Memory efficiency   | High (no intermediate tree)    | Moderate (full CST in memory)     |

**Performance notes:**

- **Batch processing** (Lambda's primary use case): markup_parser is likely faster because it builds Lambda elements directly without constructing an intermediate CST
- **Incremental editing** (editor integration): tree-sitter excels here, re-parsing only changed regions
- **Memory**: markup_parser allocates only final elements; tree-sitter holds entire syntax tree

The markup_parser approach is recommended for initial support, with the option to add a tree-sitter parser later for advanced code expression evaluation if needed.

## Future Enhancements

1. **Typst CLI Integration**: Call `typst` binary for compilation
2. **Code Evaluation**: Evaluate simple expressions (`#(1+2)`)
3. **Template Support**: Parse Typst templates
4. **PDF Output**: Direct Typst → PDF via CLI or library
5. **Full tree-sitter parser**: For complete AST when needed

## References

- [Typst Official Documentation](https://typst.app/docs/)
- [Typst Syntax Reference](https://typst.app/docs/reference/syntax/)
- [Typst GitHub Repository](https://github.com/typst/typst)
- [tree-sitter-typst Grammar](https://github.com/uben0/tree-sitter-typst)
