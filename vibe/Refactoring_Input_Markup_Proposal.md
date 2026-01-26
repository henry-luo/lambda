# Proposal: Refactoring input-markup.cpp

This document provides a detailed implementation plan for refactoring the unified lightweight markup parser (`input-markup.cpp`) to improve modularity, code reuse, error handling, and support for additional formats.

---

## Executive Summary

| Aspect | Current State | Target State |
|--------|---------------|--------------|
| **File Size** | 6,234 lines (single file) | ~8-12 files (~500-800 lines each) |
| **Formats** | 6 in unified, 2 standalone (man, org) | 8 in unified parser |
| **Error Handling** | 2 warning calls | Comprehensive structured errors |
| **Code Duplication** | Significant across format-specific code | Shared function table pattern |
| **CommonMark Compliance** | 13.1% baseline | Target 80%+ |

**Estimated Effort**: 4-6 weeks for complete refactoring

---

## Table of Contents

1. [Current State Analysis](#current-state-analysis)
2. [Modularization Strategy](#modularization-strategy)
3. [Shared Function Architecture](#shared-function-architecture)
4. [Error Handling Framework](#error-handling-framework)
5. [Man Page Integration](#man-page-integration)
6. [Org-mode Integration](#org-mode-integration)
7. [Additional Enhancements](#additional-enhancements)
8. [Implementation Phases](#implementation-phases)
9. [Testing Strategy](#testing-strategy)
10. [Risk Mitigation](#risk-mitigation)

---

## 1. Current State Analysis

### 1.1 File Structure

```
lambda/input/
├── input-markup.cpp      # 6,234 lines - monolithic parser
├── markup-parser.h       # 110 lines - header with MarkupParser class
├── input-org.cpp         # 2,104 lines - standalone Org parser (duplicate)
├── input-man.cpp         # 455 lines - standalone Man parser (to unify)
├── input-context.hpp     # Base class with error tracking
├── parse_error.hpp       # Error infrastructure (underutilized)
└── source_tracker.hpp    # Position tracking
```

### 1.2 Current Function Distribution

Counting functions by category in `input-markup.cpp`:

| Category | Count | Lines (est.) |
|----------|-------|--------------|
| Block parsing (parse_header, parse_list, etc.) | 15 | ~800 |
| Inline parsing (parse_bold_italic, parse_link, etc.) | 12 | ~600 |
| MediaWiki-specific | 10 | ~450 |
| RST-specific | 12 | ~500 |
| AsciiDoc-specific | 8 | ~350 |
| Textile-specific | 10 | ~400 |
| Org-mode-specific | 4 | ~200 |
| Table parsing | 8 | ~500 |
| Math parsing | 5 | ~300 |
| Detection/utility | 20 | ~600 |
| Emoji shortcodes | 1 | ~500 |
| **Total** | ~105 | ~5,200 |

### 1.3 Key Problems Identified

1. **Monolithic Structure**: All 6,234 lines in one file, hard to navigate and maintain
2. **Scattered Format Logic**: Format-specific code mixed with shared code
3. **Switch Statement Sprawl**: Many `if (parser->config.format == MARKUP_XXX)` scattered throughout
4. **Minimal Error Reporting**: Only 2 `addWarning()` calls in entire file
5. **Duplicate Standalone Parsers**: `input-org.cpp` (2,104 lines) duplicates unified parser features
6. **Inconsistent APIs**: Different patterns for different formats

---

## 2. Modularization Strategy

### 2.1 Proposed File Structure

```
lambda/input/
├── markup/                           # NEW: Markup parser module
│   ├── markup_parser.hpp             # MarkupParser class definition
│   ├── markup_parser.cpp             # Core parser implementation
│   ├── markup_common.hpp             # Shared types, utilities, forward decls
│   ├── markup_common.cpp             # Shared utility implementations
│   ├── markup_detection.cpp          # Format and flavor detection
│   │
│   ├── block/                        # Block-level parsers
│   │   ├── block_common.hpp          # Shared block parsing interface
│   │   ├── block_header.cpp          # Header parsing (all formats)
│   │   ├── block_list.cpp            # List parsing (all formats)
│   │   ├── block_code.cpp            # Code block parsing
│   │   ├── block_quote.cpp           # Blockquote parsing
│   │   ├── block_table.cpp           # Table parsing (all formats)
│   │   ├── block_math.cpp            # Math block parsing
│   │   └── block_divider.cpp         # Horizontal rules, dividers
│   │
│   ├── inline/                       # Inline-level parsers
│   │   ├── inline_common.hpp         # Shared inline parsing interface
│   │   ├── inline_emphasis.cpp       # Bold, italic, strikethrough
│   │   ├── inline_code.cpp           # Code spans
│   │   ├── inline_link.cpp           # Links (all formats)
│   │   ├── inline_image.cpp          # Images
│   │   ├── inline_math.cpp           # Inline math
│   │   └── inline_special.cpp        # Emoji, super/subscript, etc.
│   │
│   ├── format/                       # Format-specific adapters
│   │   ├── format_adapter.hpp        # Adapter interface
│   │   ├── markdown_adapter.cpp      # Markdown-specific rules
│   │   ├── rst_adapter.cpp           # RST-specific rules
│   │   ├── wiki_adapter.cpp          # MediaWiki-specific rules
│   │   ├── asciidoc_adapter.cpp      # AsciiDoc-specific rules
│   │   ├── textile_adapter.cpp       # Textile-specific rules
│   │   ├── org_adapter.cpp           # Org-mode rules (migrated)
│   │   └── man_adapter.cpp           # Man page rules (migrated)
│   │
│   ├── emoji_map.cpp                 # Emoji shortcode table
│   └── metadata_parser.cpp           # YAML, Org properties, etc.
│
├── input-markup.cpp                  # Entry point (thin wrapper, ~200 lines)
└── (deprecated)
    ├── input-org.cpp                 # TO BE REMOVED after migration
    └── input-man.cpp                 # TO BE REMOVED after migration
```

### 2.2 Header Structure

**markup_common.hpp** - Shared definitions:
```cpp
#pragma once
#include "../lambda-data.hpp"
#include "input-context.hpp"
#include "parse_error.hpp"

namespace lambda::markup {

// Forward declarations
class MarkupParser;
class FormatAdapter;

// Block types (unified across formats)
enum class BlockType {
    PARAGRAPH,
    HEADER,
    LIST_ITEM,
    ORDERED_LIST,
    UNORDERED_LIST,
    CODE_BLOCK,
    QUOTE,
    TABLE,
    MATH,
    DIVIDER,
    COMMENT,
    FOOTNOTE_DEF,
    DIRECTIVE,      // RST/Org directives, Man .XX commands
    METADATA,       // Frontmatter, properties
    DEFINITION_LIST
};

// Inline element types
enum class InlineType {
    TEXT,
    BOLD,
    ITALIC,
    CODE,
    LINK,
    IMAGE,
    MATH,
    STRIKETHROUGH,
    SUPERSCRIPT,
    SUBSCRIPT,
    EMOJI,
    FOOTNOTE_REF,
    CITATION,
    TEMPLATE        // Wiki templates, Man escapes
};

// Format identifiers
enum class Format {
    MARKDOWN,
    RST,
    WIKI,
    TEXTILE,
    ORG,
    ASCIIDOC,
    MAN,            // NEW
    AUTO_DETECT
};

// Delimiter specification for inline parsing
struct DelimiterSpec {
    const char* open;
    const char* close;
    InlineType type;
    bool nestable;
};

// Header detection result
struct HeaderInfo {
    int level;              // 1-6
    const char* text_start; // Where text content begins
    const char* text_end;   // Where text content ends (before trailing markers)
    bool valid;
};

// List item detection result
struct ListItemInfo {
    char marker;           // '-', '*', '#', etc.
    int indent;            // Indentation level
    int number;            // For ordered lists (0 for unordered)
    const char* text_start;
    bool is_ordered;
    bool valid;
};

// Error categories for markup parsing
enum class MarkupErrorCategory {
    SYNTAX,           // Malformed syntax
    STRUCTURE,        // Improper nesting
    REFERENCE,        // Unresolved link/footnote
    ENCODING,         // Character encoding issues
    UNCLOSED,         // Unclosed delimiter
    UNEXPECTED,       // Unexpected token
    DEPRECATED,       // Deprecated syntax
    LIMIT_EXCEEDED    // Nesting depth, etc.
};

} // namespace lambda::markup
```

### 2.3 Module Dependency Graph

```
                    ┌─────────────────┐
                    │ input-markup.cpp │  (entry point)
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │  markup_parser  │  (core orchestration)
                    └────────┬────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
┌───────▼───────┐   ┌────────▼────────┐  ┌───────▼───────┐
│ block parsers │   │ inline parsers  │  │format adapters│
└───────┬───────┘   └────────┬────────┘  └───────┬───────┘
        │                    │                   │
        └────────────────────┼───────────────────┘
                             │
                    ┌────────▼────────┐
                    │  markup_common  │  (shared utilities)
                    └────────┬────────┘
                             │
                    ┌────────▼────────┐
                    │  InputContext   │  (base class)
                    └─────────────────┘
```

---

## 3. Shared Function Architecture

### 3.1 Format Adapter Interface

The key to code reuse is the **Format Adapter** pattern - each format provides its detection rules, but delegates actual parsing to shared functions.

**format_adapter.hpp**:
```cpp
#pragma once
#include "markup_common.hpp"

namespace lambda::markup {

/**
 * FormatAdapter - Abstract interface for format-specific behavior
 *
 * Each format implements this to provide its detection rules and delimiters.
 * Actual parsing is done by shared functions using these rules.
 */
class FormatAdapter {
public:
    virtual ~FormatAdapter() = default;

    // --- Format identification ---
    virtual Format format() const = 0;
    virtual const char* name() const = 0;

    // --- Block detection ---
    // Return HeaderInfo if line is a header, or invalid HeaderInfo if not
    virtual HeaderInfo detectHeader(const char* line, const char* next_line) = 0;

    // Return ListItemInfo if line is a list item
    virtual ListItemInfo detectListItem(const char* line) = 0;

    // Check if line starts a code block, returns fence info
    virtual bool detectCodeFence(const char* line, char* fence_char, int* fence_len, const char** lang) = 0;

    // Check if line is a blockquote
    virtual bool detectBlockquote(const char* line, const char** content_start) = 0;

    // Check if line is a table row/start
    virtual bool detectTable(const char* line) = 0;

    // Check if line is a thematic break
    virtual bool detectThematicBreak(const char* line) = 0;

    // --- Inline delimiters ---
    // Get emphasis delimiters for this format
    virtual const DelimiterSpec* emphasisDelimiters() const = 0;
    virtual size_t emphasisDelimiterCount() const = 0;

    // Get link syntax pattern
    virtual bool detectLink(const char* pos, const char** text_start, const char** text_end,
                           const char** url_start, const char** url_end) = 0;

    // Get image syntax pattern
    virtual bool detectImage(const char* pos, const char** alt_start, const char** alt_end,
                            const char** src_start, const char** src_end) = 0;

    // --- Format-specific features ---
    virtual bool supportsFeature(const char* feature) const { return false; }

    // --- Metadata ---
    virtual bool detectMetadata(const char* content) = 0;

protected:
    FormatAdapter() = default;
};

/**
 * FormatRegistry - Factory for format adapters
 */
class FormatRegistry {
public:
    static FormatAdapter* getAdapter(Format format);
    static FormatAdapter* detectAdapter(const char* content, const char* filename);
};

} // namespace lambda::markup
```

### 3.2 Shared Block Parsers

**block_header.cpp** - Single implementation for all formats:
```cpp
#include "block_common.hpp"

namespace lambda::markup {

/**
 * parse_header - Unified header parsing for all formats
 *
 * Uses the FormatAdapter to detect header syntax, then creates
 * the h1-h6 element uniformly.
 */
Item parse_header(MarkupParser* parser, const char* line) {
    FormatAdapter* adapter = parser->adapter();

    // Get next line for formats that use underline-style headers (RST, Setext)
    const char* next_line = nullptr;
    if (parser->current_line + 1 < parser->line_count) {
        next_line = parser->lines[parser->current_line + 1];
    }

    HeaderInfo info = adapter->detectHeader(line, next_line);
    if (!info.valid) {
        return parse_paragraph(parser, line);  // Fallback
    }

    // Create h1-h6 element (same for all formats)
    char tag_name[4];
    snprintf(tag_name, sizeof(tag_name), "h%d", info.level);
    Element* header = parser->builder.element(tag_name).final().element;
    if (!header) {
        parser->addError(parser->location(), "Failed to create header element",
                        MarkupErrorCategory::STRUCTURE);
        return Item{.item = ITEM_ERROR};
    }

    // Add level attribute
    char level_str[4];
    snprintf(level_str, sizeof(level_str), "%d", info.level);
    parser->addAttribute(header, "level", level_str);

    // Extract header text
    std::string text = extractText(info.text_start, info.text_end);

    // Parse inline content
    Item content = parse_inline_spans(parser, text.c_str());
    if (content.item != ITEM_ERROR) {
        list_push((List*)header, content);
        increment_element_content_length(header);
    }

    // Advance line(s)
    parser->current_line++;
    if (info.uses_underline) {
        parser->current_line++;  // Skip underline too
    }

    return Item{.item = (uint64_t)header};
}

} // namespace lambda::markup
```

### 3.3 Format Adapter Example

**markdown_adapter.cpp**:
```cpp
#include "format_adapter.hpp"

namespace lambda::markup {

class MarkdownAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::MARKDOWN; }
    const char* name() const override { return "markdown"; }

    HeaderInfo detectHeader(const char* line, const char* next_line) override {
        HeaderInfo info = {0, nullptr, nullptr, false};

        // ATX-style headers: # Header
        if (*line == '#') {
            int level = 0;
            const char* p = line;
            while (*p == '#' && level < 7) { level++; p++; }

            if (level >= 1 && level <= 6 && (*p == ' ' || *p == '\t' || *p == '\0')) {
                info.level = level;
                info.valid = true;

                // Skip whitespace after #
                while (*p == ' ' || *p == '\t') p++;
                info.text_start = p;

                // Find end (before trailing # markers)
                info.text_end = p + strlen(p);
                // Strip trailing # and whitespace
                while (info.text_end > info.text_start &&
                       (*(info.text_end-1) == '#' || *(info.text_end-1) == ' ')) {
                    info.text_end--;
                }
            }
        }

        // Setext-style headers: underline with === or ---
        if (!info.valid && next_line) {
            const char* ul = next_line;
            while (*ul == ' ' || *ul == '\t') ul++;

            bool is_h1 = (*ul == '=' && strlen(ul) >= 3);
            bool is_h2 = (*ul == '-' && strlen(ul) >= 3);

            if (is_h1 || is_h2) {
                // Verify it's all the same character
                char ch = *ul;
                bool valid_ul = true;
                for (const char* p = ul; *p && *p != '\n'; p++) {
                    if (*p != ch && *p != ' ' && *p != '\t') {
                        valid_ul = false;
                        break;
                    }
                }

                if (valid_ul) {
                    info.level = is_h1 ? 1 : 2;
                    info.text_start = line;
                    info.text_end = line + strlen(line);
                    info.uses_underline = true;
                    info.valid = true;
                }
            }
        }

        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info = {0, 0, 0, nullptr, false, false};

        const char* p = line;

        // Count leading whitespace for indent
        while (*p == ' ') { info.indent++; p++; }
        if (*p == '\t') { info.indent += 4; p++; }

        // Check for unordered list markers: -, *, +
        if ((*p == '-' || *p == '*' || *p == '+') &&
            (*(p+1) == ' ' || *(p+1) == '\t')) {
            info.marker = *p;
            info.is_ordered = false;
            info.text_start = p + 2;
            info.valid = true;
        }

        // Check for ordered list markers: 1. or 1)
        if (!info.valid && isdigit(*p)) {
            const char* num_start = p;
            while (isdigit(*p)) p++;
            if ((*p == '.' || *p == ')') && (*(p+1) == ' ' || *(p+1) == '\t')) {
                info.marker = *p;
                info.number = atoi(num_start);
                info.is_ordered = true;
                info.text_start = p + 2;
                info.valid = true;
            }
        }

        return info;
    }

    // Emphasis delimiters for Markdown
    static constexpr DelimiterSpec md_emphasis[] = {
        {"**", "**", InlineType::BOLD, true},
        {"__", "__", InlineType::BOLD, true},
        {"*", "*", InlineType::ITALIC, true},
        {"_", "_", InlineType::ITALIC, true},
        {"~~", "~~", InlineType::STRIKETHROUGH, false},
        {"`", "`", InlineType::CODE, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return md_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(md_emphasis)/sizeof(md_emphasis[0]); }

    // ... other methods
};

// Register the adapter
static MarkdownAdapter markdown_adapter_instance;

} // namespace lambda::markup
```

### 3.4 Shared Inline Parsing

**inline_emphasis.cpp** - Single implementation using delimiter specs:
```cpp
#include "inline_common.hpp"

namespace lambda::markup {

/**
 * parse_emphasis - Unified emphasis parsing using delimiter specs
 *
 * Works for all formats by using the FormatAdapter's delimiter table.
 */
Item parse_emphasis(MarkupParser* parser, const char** text) {
    FormatAdapter* adapter = parser->adapter();
    const DelimiterSpec* delims = adapter->emphasisDelimiters();
    size_t delim_count = adapter->emphasisDelimiterCount();

    const char* pos = *text;

    // Try each delimiter
    for (size_t i = 0; i < delim_count; i++) {
        const DelimiterSpec& spec = delims[i];
        size_t open_len = strlen(spec.open);

        if (strncmp(pos, spec.open, open_len) != 0) continue;

        // Find closing delimiter
        const char* content_start = pos + open_len;
        const char* closing = find_closing_delimiter(content_start, spec.close);

        if (!closing) {
            // No closing found - not an error, just not this delimiter
            continue;
        }

        // Create element based on type
        const char* tag = inline_type_to_tag(spec.type);
        Element* elem = parser->builder.element(tag).final().element;
        if (!elem) {
            parser->addError(parser->location(),
                           "Failed to create emphasis element",
                           MarkupErrorCategory::STRUCTURE);
            return Item{.item = ITEM_ERROR};
        }

        // Parse nested content if allowed
        size_t content_len = closing - content_start;
        std::string content(content_start, content_len);

        if (spec.nestable) {
            Item nested = parse_inline_spans(parser, content.c_str());
            if (nested.item != ITEM_ERROR) {
                list_push((List*)elem, nested);
                increment_element_content_length(elem);
            }
        } else {
            // Just add as text (e.g., code spans)
            String* str = parser->builder.createString(content.c_str());
            list_push((List*)elem, Item{.item = s2it(str)});
            increment_element_content_length(elem);
        }

        // Advance position past closing delimiter
        *text = closing + strlen(spec.close);

        return Item{.item = (uint64_t)elem};
    }

    return Item{.item = ITEM_NULL};  // No match
}

const char* inline_type_to_tag(InlineType type) {
    switch (type) {
        case InlineType::BOLD: return "strong";
        case InlineType::ITALIC: return "em";
        case InlineType::CODE: return "code";
        case InlineType::STRIKETHROUGH: return "s";
        case InlineType::SUPERSCRIPT: return "sup";
        case InlineType::SUBSCRIPT: return "sub";
        default: return "span";
    }
}

} // namespace lambda::markup
```

---

## 4. Error Handling Framework

### 4.1 Current State

Only 2 error reports in 6,234 lines:
```cpp
parser.addWarning(parser.tracker.location(), "Markup parsing returned error");
parser.addWarning(parser.tracker.location(), "Markup parsing with explicit format returned error");
```

### 4.2 Proposed Error Categories and Messages

**Error Taxonomy**:

| Category | Example Situations | Severity |
|----------|-------------------|----------|
| `UNCLOSED` | Missing closing delimiter (`**bold` without `**`) | WARNING |
| `SYNTAX` | Invalid list marker, malformed link | WARNING |
| `STRUCTURE` | Improperly nested elements | WARNING |
| `REFERENCE` | Unresolved link/footnote reference | WARNING |
| `LIMIT_EXCEEDED` | List nesting > 10, header level > 6 | WARNING |
| `ENCODING` | Invalid UTF-8 sequence | ERROR |
| `UNEXPECTED` | Unexpected token/character | NOTE |
| `DEPRECATED` | Old syntax style | NOTE |

### 4.3 Error Helper Functions

**markup_parser.cpp** - Enhanced error methods:
```cpp
namespace lambda::markup {

void MarkupParser::addMarkupError(MarkupErrorCategory category,
                                   const std::string& message,
                                   const std::string& hint) {
    // Get current source location
    SourceLocation loc = tracker.location();

    // Get context line
    std::string context;
    if (current_line < line_count && lines[current_line]) {
        context = lines[current_line];
    }

    // Determine severity based on category
    ParseErrorSeverity severity;
    switch (category) {
        case MarkupErrorCategory::ENCODING:
            severity = ParseErrorSeverity::ERROR;
            break;
        case MarkupErrorCategory::UNEXPECTED:
        case MarkupErrorCategory::DEPRECATED:
            severity = ParseErrorSeverity::NOTE;
            break;
        default:
            severity = ParseErrorSeverity::WARNING;
    }

    // Add structured error
    ParseError error(loc, severity, message, context, hint);
    errors_.addError(error);

    // Log for debugging
    log_debug("markup: [%s] %s at line %zu: %s",
              category_name(category), severity_name(severity),
              loc.line, message.c_str());
}

// Convenience wrappers
void MarkupParser::warnUnclosed(const char* delimiter, size_t start_line) {
    addMarkupError(MarkupErrorCategory::UNCLOSED,
                  std::string("Unclosed ") + delimiter + " (opened at line " +
                      std::to_string(start_line) + ")",
                  std::string("Add closing ") + delimiter);
}

void MarkupParser::warnInvalidSyntax(const char* construct, const char* expected) {
    addMarkupError(MarkupErrorCategory::SYNTAX,
                  std::string("Invalid ") + construct + " syntax",
                  std::string("Expected: ") + expected);
}

void MarkupParser::noteUnresolvedReference(const char* ref_type, const char* ref_id) {
    addMarkupError(MarkupErrorCategory::REFERENCE,
                  std::string("Unresolved ") + ref_type + " reference: " + ref_id,
                  "Define the reference or check spelling");
}

} // namespace lambda::markup
```

### 4.4 Integration Points

Add error reporting throughout parsing:

```cpp
// In parse_code_block():
if (!found_closing_fence) {
    parser->warnUnclosed("code fence", start_line);
    // Continue with what we have
}

// In parse_link():
if (!validate_url(url)) {
    parser->addMarkupError(MarkupErrorCategory::SYNTAX,
                          "Invalid URL in link: " + std::string(url),
                          "URLs should start with http://, https://, or /");
}

// In parse_emphasis():
if (nesting_depth > MAX_NESTING) {
    parser->addMarkupError(MarkupErrorCategory::LIMIT_EXCEEDED,
                          "Emphasis nesting too deep (max " +
                              std::to_string(MAX_NESTING) + ")",
                          "Simplify nested formatting");
}
```

---

## 5. Man Page Integration

### 5.1 Current `input-man.cpp` Analysis

| Component | Lines | Unified Equivalent |
|-----------|-------|-------------------|
| Section parsing (`.SH`, `.SS`) | ~50 | `parse_header()` |
| Paragraph (`.PP`, `.LP`) | ~30 | Paragraph break detection |
| Bold/italic (`.B`, `.I`) | ~40 | `parse_emphasis()` |
| Lists (`.IP`, `.TP`) | ~80 | `parse_list()` |
| Inline escapes (`\fB`, `\fI`) | ~60 | `parse_inline_spans()` |
| Utilities | ~50 | Common utilities |
| **Total** | 455 | ~310 can be shared |

### 5.2 Man Adapter Implementation

**man_adapter.cpp**:
```cpp
#include "format_adapter.hpp"

namespace lambda::markup {

class ManAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::MAN; }
    const char* name() const override { return "man"; }

    HeaderInfo detectHeader(const char* line, const char* /*next_line*/) override {
        HeaderInfo info = {0, nullptr, nullptr, false};

        // .SH - Section header (h1)
        if (strncmp(line, ".SH", 3) == 0) {
            info.level = 1;
            info.text_start = line + 3;
            while (*info.text_start == ' ') info.text_start++;
            info.text_end = info.text_start + strlen(info.text_start);
            info.valid = true;
        }
        // .SS - Subsection (h2)
        else if (strncmp(line, ".SS", 3) == 0) {
            info.level = 2;
            info.text_start = line + 3;
            while (*info.text_start == ' ') info.text_start++;
            info.text_end = info.text_start + strlen(info.text_start);
            info.valid = true;
        }

        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info = {0, 0, 0, nullptr, false, false};

        // .IP - Indented paragraph (list item)
        if (strncmp(line, ".IP", 3) == 0) {
            info.marker = '-';
            info.text_start = line + 3;
            while (*info.text_start == ' ') info.text_start++;
            info.valid = true;
        }
        // .TP - Tagged paragraph (definition list)
        else if (strncmp(line, ".TP", 3) == 0) {
            info.marker = ':';  // Definition list marker
            info.text_start = line + 3;
            info.valid = true;
        }

        return info;
    }

    bool detectBlockquote(const char* line, const char** content_start) override {
        // .RS - Start indent block
        if (strncmp(line, ".RS", 3) == 0) {
            *content_start = line + 3;
            return true;
        }
        return false;
    }

    bool detectThematicBreak(const char* line) override {
        // Man pages don't have horizontal rules
        return false;
    }

    // Man page inline escapes
    static constexpr DelimiterSpec man_inline[] = {
        {"\\fB", "\\fR", InlineType::BOLD, false},
        {"\\fB", "\\fP", InlineType::BOLD, false},
        {"\\fI", "\\fR", InlineType::ITALIC, false},
        {"\\fI", "\\fP", InlineType::ITALIC, false},
    };

    const DelimiterSpec* emphasisDelimiters() const override { return man_inline; }
    size_t emphasisDelimiterCount() const override { return sizeof(man_inline)/sizeof(man_inline[0]); }

    // Man-specific: detect .XX directives
    bool isDirective(const char* line, const char** directive, const char** args) {
        if (line[0] != '.') return false;

        *directive = line + 1;
        const char* space = strchr(*directive, ' ');
        if (space) {
            *args = space + 1;
        } else {
            *args = "";
        }
        return true;
    }
};

static ManAdapter man_adapter_instance;

} // namespace lambda::markup
```

### 5.3 Man-Specific Block Handler

```cpp
// In block_directive.cpp (new file for directive-style blocks)

Item parse_man_directive(MarkupParser* parser, const char* line) {
    ManAdapter* adapter = static_cast<ManAdapter*>(parser->adapter());

    const char* directive;
    const char* args;
    if (!adapter->isDirective(line, &directive, &args)) {
        return parse_paragraph(parser, line);
    }

    // Handle common directives via shared parsers
    if (strncmp(directive, "SH", 2) == 0 || strncmp(directive, "SS", 2) == 0) {
        return parse_header(parser, line);  // Shared!
    }
    if (strncmp(directive, "IP", 2) == 0 || strncmp(directive, "TP", 2) == 0) {
        return parse_list(parser);  // Shared!
    }
    if (strncmp(directive, "B ", 2) == 0) {
        // .B text - bold whole line
        return parse_formatted_line(parser, args, "strong");
    }
    if (strncmp(directive, "I ", 2) == 0) {
        // .I text - italic whole line
        return parse_formatted_line(parser, args, "em");
    }
    if (strncmp(directive, "PP", 2) == 0 || strncmp(directive, "LP", 2) == 0) {
        parser->current_line++;
        return Item{.item = ITEM_NULL};  // Paragraph break
    }
    if (strncmp(directive, "RS", 2) == 0) {
        return parse_indent_block(parser);  // Uses shared blockquote logic
    }

    // Unknown directive - skip or treat as text
    parser->addMarkupError(MarkupErrorCategory::UNEXPECTED,
                          std::string("Unknown man directive: .") + directive);
    parser->current_line++;
    return Item{.item = ITEM_NULL};
}
```

---

## 6. Org-mode Integration

### 6.1 Current `input-org.cpp` Analysis

| Component | Lines | Unified Equivalent |
|-----------|-------|-------------------|
| Headline parsing (`* Header`) | ~100 | `parse_header()` |
| Plain lists (`-`, `+`, numbers) | ~150 | `parse_list()` |
| Emphasis (`*bold*`, `/italic/`) | ~100 | `parse_emphasis()` |
| Source blocks (`#+BEGIN_SRC`) | ~150 | `parse_code_block()` |
| Properties drawer | ~80 | `parse_metadata()` |
| Math (`$...$`, `\[...\]`) | ~100 | `parse_math()` |
| Links (`[[url][text]]`) | ~80 | `parse_link()` |
| Utilities | ~200 | Common utilities |
| Duplicate code | ~1,000+ | To be removed |
| **Total** | 2,104 | ~800 unique |

### 6.2 Org Adapter Implementation

**org_adapter.cpp**:
```cpp
#include "format_adapter.hpp"

namespace lambda::markup {

class OrgAdapter : public FormatAdapter {
public:
    Format format() const override { return Format::ORG; }
    const char* name() const override { return "org"; }

    HeaderInfo detectHeader(const char* line, const char* /*next_line*/) override {
        HeaderInfo info = {0, nullptr, nullptr, false};

        // Org headlines: * Header, ** Header, etc.
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;  // Skip leading whitespace

        if (*p == '*') {
            int level = 0;
            while (*p == '*') { level++; p++; }

            if (*p == ' ' || *p == '\t') {  // Must have space after stars
                info.level = std::min(level, 6);
                info.text_start = p + 1;
                info.text_end = info.text_start + strlen(info.text_start);

                // Strip TODO keywords if present
                const char* todo_end = skip_todo_keyword(info.text_start);
                if (todo_end) info.text_start = todo_end;

                info.valid = true;
            }
        }

        return info;
    }

    ListItemInfo detectListItem(const char* line) override {
        ListItemInfo info = {0, 0, 0, nullptr, false, false};

        const char* p = line;
        while (*p == ' ') { info.indent++; p++; }

        // Unordered: - or +
        if ((*p == '-' || *p == '+') && *(p+1) == ' ') {
            info.marker = *p;
            info.text_start = p + 2;
            info.valid = true;
        }
        // Ordered: 1. or 1)
        else if (isdigit(*p)) {
            const char* num_start = p;
            while (isdigit(*p)) p++;
            if ((*p == '.' || *p == ')') && *(p+1) == ' ') {
                info.number = atoi(num_start);
                info.marker = *p;
                info.is_ordered = true;
                info.text_start = p + 2;
                info.valid = true;
            }
        }

        return info;
    }

    bool detectCodeFence(const char* line, char* fence_char, int* fence_len,
                        const char** lang) override {
        // #+BEGIN_SRC language
        if (strncasecmp(line, "#+BEGIN_SRC", 11) == 0) {
            *fence_char = '#';  // Special marker
            *fence_len = 11;
            const char* lang_start = line + 11;
            while (*lang_start == ' ') lang_start++;
            *lang = lang_start;
            return true;
        }
        return false;
    }

    bool isCodeFenceEnd(const char* line) {
        return strncasecmp(line, "#+END_SRC", 9) == 0;
    }

    // Org emphasis delimiters
    static constexpr DelimiterSpec org_emphasis[] = {
        {"*", "*", InlineType::BOLD, false},
        {"/", "/", InlineType::ITALIC, false},
        {"=", "=", InlineType::CODE, false},
        {"~", "~", InlineType::CODE, false},
        {"+", "+", InlineType::STRIKETHROUGH, false},
        {"_", "_", InlineType::ITALIC, false},  // Underline in Org
    };

    const DelimiterSpec* emphasisDelimiters() const override { return org_emphasis; }
    size_t emphasisDelimiterCount() const override { return sizeof(org_emphasis)/sizeof(org_emphasis[0]); }

    // Org links: [[url][description]] or [[url]]
    bool detectLink(const char* pos, const char** text_start, const char** text_end,
                   const char** url_start, const char** url_end) override {
        if (strncmp(pos, "[[", 2) != 0) return false;

        *url_start = pos + 2;

        // Find ][  or ]]
        const char* bracket = strstr(*url_start, "][");
        if (bracket) {
            *url_end = bracket;
            *text_start = bracket + 2;
            *text_end = strstr(*text_start, "]]");
            return *text_end != nullptr;
        }

        // No description - [[url]]
        *url_end = strstr(*url_start, "]]");
        if (*url_end) {
            *text_start = *url_start;
            *text_end = *url_end;
            return true;
        }

        return false;
    }

private:
    static const char* skip_todo_keyword(const char* text) {
        static const char* keywords[] = {"TODO ", "DONE ", "NEXT ", "WAIT ", nullptr};
        for (int i = 0; keywords[i]; i++) {
            size_t len = strlen(keywords[i]);
            if (strncmp(text, keywords[i], len) == 0) {
                return text + len;
            }
        }
        return nullptr;
    }
};

static OrgAdapter org_adapter_instance;

} // namespace lambda::markup
```

### 6.3 Migration Steps for Org

1. **Create `org_adapter.cpp`** with detection rules
2. **Update `detect_markup_format()`** to use unified detection
3. **Test existing Org files** against unified parser
4. **Fix discrepancies** (if any)
5. **Remove `input-org.cpp`** after all tests pass

---

## 7. Additional Enhancements

### 7.1 Performance Optimizations

| Enhancement | Current | Proposed | Benefit |
|-------------|---------|----------|---------|
| Emoji lookup | Linear search in ~200 entries | Hash table | O(1) vs O(n) |
| Line splitting | Eager allocation | Lazy/streaming | Memory reduction |
| String comparison | `strcmp` loops | String interning | Faster matching |
| Delimiter matching | Nested loops | State machine | Fewer comparisons |

**Emoji Hash Table**:
```cpp
// emoji_map.cpp - Use hashmap instead of linear search
#include "../../lib/hashmap.h"

static Hashmap* emoji_map = nullptr;

void init_emoji_map() {
    emoji_map = hashmap_create(512);

    // Populate with shortcodes
    hashmap_set(emoji_map, "smile", "😄");
    hashmap_set(emoji_map, "heart", "❤️");
    // ... etc.
}

const char* lookup_emoji(const char* shortcode) {
    return (const char*)hashmap_get(emoji_map, shortcode);
}
```

### 7.2 CommonMark Compliance Improvements

Based on the 13.1% baseline, priority fixes:

| Issue | Examples Failed | Fix |
|-------|-----------------|-----|
| Fenced code blocks | 29 | Fix fence detection and closing |
| HTML blocks | 46 | Add HTML block detection/passthrough |
| Indented code blocks | 12 | Detect 4-space indent as code |
| Link reference definitions | 27 | Implement reference resolution |
| Backslash escapes | 12 | Handle `\*`, `\[`, etc. properly |
| Emphasis edge cases | 112 | Fix delimiter matching rules |

### 7.3 Reference Resolution System

**reference_resolver.cpp**:
```cpp
namespace lambda::markup {

/**
 * ReferenceResolver - Manages link/footnote reference definitions
 *
 * First pass: collect all reference definitions
 * Second pass: resolve references to definitions
 */
class ReferenceResolver {
public:
    // Add a link reference definition: [id]: url "title"
    void addLinkReference(const std::string& id, const std::string& url,
                         const std::string& title = "");

    // Add a footnote definition: [^id]: content
    void addFootnoteDefinition(const std::string& id, Item content);

    // Resolve a link reference: [text][id] or [id]
    bool resolveLinkReference(const std::string& id,
                             std::string* url, std::string* title);

    // Resolve a footnote reference: [^id]
    bool resolveFootnoteReference(const std::string& id, Item* content);

    // Report unresolved references
    std::vector<std::string> unresolvedLinkRefs() const;
    std::vector<std::string> unresolvedFootnoteRefs() const;

private:
    struct LinkRef {
        std::string url;
        std::string title;
    };

    Hashmap* link_refs_;
    Hashmap* footnote_refs_;
    std::vector<std::string> link_uses_;
    std::vector<std::string> footnote_uses_;
};

} // namespace lambda::markup
```

### 7.4 Two-Pass Parsing for References

```cpp
Item MarkupParser::parseContent(const char* content) {
    // First pass: collect reference definitions
    ReferenceResolver resolver;
    first_pass_collect_references(content, &resolver);

    // Second pass: actual parsing with reference resolution
    resolver_ = &resolver;
    Item result = parse_document();

    // Report unresolved references as warnings
    for (const auto& ref : resolver.unresolvedLinkRefs()) {
        addMarkupError(MarkupErrorCategory::REFERENCE,
                      "Unresolved link reference: [" + ref + "]");
    }

    return result;
}
```

### 7.5 Streaming API for Large Documents

```cpp
namespace lambda::markup {

/**
 * StreamingParser - Parse large documents without loading entirely in memory
 */
class StreamingParser {
public:
    StreamingParser(Input* input, FormatAdapter* adapter);

    // Parse next block element
    Item nextBlock();

    // Check if more content
    bool hasMore() const;

    // Current position
    size_t currentLine() const;
    size_t bytesProcessed() const;

private:
    // Read lines on-demand from input stream
    bool readNextChunk();

    std::deque<std::string> line_buffer_;
    size_t chunk_size_ = 1000;  // Lines per chunk
};

} // namespace lambda::markup
```

---

## 8. Implementation Phases

### Phase 1: Infrastructure (Week 1)

**Goal**: Create module structure and shared interfaces without changing behavior

1. Create `lambda/input/markup/` directory structure
2. Create `markup_common.hpp` with shared types
3. Create `format_adapter.hpp` interface
4. Create stub adapters for all formats
5. Move `MarkupParser` class to `markup_parser.cpp`
6. Update build system (`build_lambda_config.json`)

**Deliverables**:
- New directory structure
- All headers created
- Build passes
- No behavior changes

### Phase 2: Block Parser Extraction (Week 2)

**Goal**: Extract block parsers to separate files

1. Create `block/block_common.hpp`
2. Extract `parse_header()` → `block_header.cpp`
3. Extract `parse_list*()` → `block_list.cpp`
4. Extract `parse_code_block()` → `block_code.cpp`
5. Extract `parse_blockquote()` → `block_quote.cpp`
6. Extract `parse_table*()` → `block_table.cpp`
7. Extract `parse_math_block()` → `block_math.cpp`
8. Extract `parse_divider()` → `block_divider.cpp`

**Deliverables**:
- 7 new block parser files
- All existing tests pass
- ~2000 lines removed from main file

### Phase 3: Inline Parser Extraction (Week 2-3)

**Goal**: Extract inline parsers to separate files

1. Create `inline/inline_common.hpp`
2. Extract `parse_bold_italic()` → `inline_emphasis.cpp`
3. Extract `parse_code_span()` → `inline_code.cpp`
4. Extract `parse_link()` → `inline_link.cpp`
5. Extract `parse_image()` → `inline_image.cpp`
6. Extract math inline → `inline_math.cpp`
7. Extract special (emoji, etc.) → `inline_special.cpp`

**Deliverables**:
- 6 new inline parser files
- ~1500 lines removed from main file

### Phase 4: Format Adapter Implementation (Week 3-4)

**Goal**: Implement format-specific adapters

1. Implement `MarkdownAdapter` (fully featured)
2. Implement `RstAdapter`
3. Implement `WikiAdapter`
4. Implement `AsciidocAdapter`
5. Implement `TextileAdapter`
6. Refactor block/inline parsers to use adapters
7. Remove format-specific `if` statements

**Deliverables**:
- 5 adapter files
- Shared parsers use adapter interface
- Significant code deduplication

### Phase 5: Man and Org Integration (Week 4-5)

**Goal**: Migrate standalone parsers to unified system

1. Implement `ManAdapter`
2. Implement `OrgAdapter`
3. Test with existing Man/Org files
4. Fix any discrepancies
5. Remove `input-man.cpp`
6. Remove `input-org.cpp`

**Deliverables**:
- Man and Org work through unified parser
- 2,559 lines of duplicate code removed
- 2 adapters added (~400 lines)

### Phase 6: Error Handling Enhancement (Week 5)

**Goal**: Add comprehensive error reporting

1. Define error categories and messages
2. Add `addMarkupError()` helper methods
3. Add error reporting to block parsers
4. Add error reporting to inline parsers
5. Update tests to check error output
6. Document error messages

**Deliverables**:
- 50+ strategic error reporting points
- Structured error output
- Error documentation

### Phase 7: Performance and Polish (Week 6)

**Goal**: Optimize and finalize

1. Implement emoji hash table
2. Add reference resolution (two-pass)
3. Profile and optimize hot paths
4. Run full CommonMark spec tests
5. Fix CommonMark compliance issues
6. Update documentation

**Deliverables**:
- Performance improvements
- Improved CommonMark compliance (target 50%+)
- Final documentation

---

## 9. Testing Strategy

### 9.1 Regression Test Matrix

| Test Category | Before Refactor | After Each Phase | Final |
|---------------|-----------------|------------------|-------|
| CommonMark spec | Baseline (13.1%) | No regression | 50%+ |
| Roundtrip tests | Pass | Pass | Pass |
| Format detection | Pass | Pass | Pass |
| Org-mode samples | Pass (via input-org) | Pass (unified) | Pass |
| Man page samples | Pass (via input-man) | Pass (unified) | Pass |
| Error reporting | N/A | New tests | Full coverage |

### 9.2 Test Commands

```bash
# Run all markup tests
make test-markup-md

# Run CommonMark compliance
./test/test_commonmark_spec_gtest.exe --gtest_filter="CommonMarkSpecTest.ComprehensiveStats"

# Run format-specific tests
./test/test_markup_roundtrip_gtest.exe --gtest_filter="*Markdown*"
./test/test_markup_roundtrip_gtest.exe --gtest_filter="*Org*"
./test/test_markup_roundtrip_gtest.exe --gtest_filter="*Man*"

# Run error reporting tests
./test/test_markup_errors_gtest.exe
```

### 9.3 New Test Files Needed

```
test/markup/
├── test_markup_adapters_gtest.cpp    # Test format detection
├── test_markup_errors_gtest.cpp       # Test error reporting
├── test_markup_man_gtest.cpp          # Man page specific tests
├── test_markup_org_gtest.cpp          # Org-mode specific tests
└── fixtures/
    ├── man/                           # Sample man pages
    └── org/                           # Sample org files
```

---

## 10. Risk Mitigation

### 10.1 Identified Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Regression in existing formats | Medium | High | Comprehensive test suite before refactor |
| Man/Org edge cases missed | Medium | Medium | Collect comprehensive test files |
| Build system complexity | Low | Medium | Incremental changes, test each phase |
| Performance regression | Low | Medium | Profile before/after benchmarks |
| Schedule slip | Medium | Medium | Prioritize phases, can defer phase 7 |

### 10.2 Rollback Strategy

Each phase should be a separate branch/PR:

```
main
 └── feature/markup-refactor
      ├── phase1-infrastructure
      ├── phase2-block-parsers
      ├── phase3-inline-parsers
      ├── phase4-adapters
      ├── phase5-man-org
      ├── phase6-errors
      └── phase7-performance
```

If any phase introduces critical issues:
1. Revert that phase's branch
2. Analyze the issue
3. Re-implement with fix
4. Continue

### 10.3 Success Criteria

| Criterion | Threshold |
|-----------|-----------|
| All existing tests pass | 100% |
| CommonMark compliance | ≥50% (up from 13.1%) |
| Man/Org files parse correctly | 100% |
| Main file size reduction | ≥70% (from 6,234 to <1,900 lines) |
| Error reporting coverage | ≥50 strategic points |
| No performance regression | Within 10% of current |

---

## 11. Summary

This refactoring will transform the monolithic `input-markup.cpp` into a modular, maintainable system with:

1. **8 format adapters** providing format-specific rules
2. **Shared block parsers** (~7 files) used by all formats
3. **Shared inline parsers** (~6 files) used by all formats
4. **Unified Man/Org support** replacing standalone parsers
5. **Comprehensive error reporting** with structured messages
6. **Improved CommonMark compliance** targeting 50%+

The result will be a cleaner, more maintainable codebase that's easier to extend with new formats and fix for edge cases.

---

## Appendix A: File Line Count Projection

| File | Estimated Lines |
|------|----------------|
| `markup_parser.hpp` | 150 |
| `markup_parser.cpp` | 300 |
| `markup_common.hpp` | 200 |
| `markup_common.cpp` | 150 |
| `markup_detection.cpp` | 200 |
| `format_adapter.hpp` | 150 |
| Block parsers (7 files) | 1,200 |
| Inline parsers (6 files) | 900 |
| Format adapters (7 files) | 1,400 |
| `emoji_map.cpp` | 500 |
| `metadata_parser.cpp` | 200 |
| `reference_resolver.cpp` | 200 |
| `input-markup.cpp` (entry) | 150 |
| **Total** | ~5,700 |

This represents a slight increase in total lines, but massive improvement in organization and maintainability.

## Appendix B: Build Configuration Changes

Add to `build_lambda_config.json`:

```json
{
    "name": "markup-module",
    "source_dirs": [
        "lambda/input/markup",
        "lambda/input/markup/block",
        "lambda/input/markup/inline",
        "lambda/input/markup/format"
    ]
}
```

## Appendix C: Header Dependencies

```
markup_common.hpp
    └── lambda-data.hpp
    └── input-context.hpp
    └── parse_error.hpp

format_adapter.hpp
    └── markup_common.hpp

markup_parser.hpp
    └── format_adapter.hpp

block_common.hpp
    └── markup_parser.hpp

inline_common.hpp
    └── markup_parser.hpp

*_adapter.cpp
    └── format_adapter.hpp

block_*.cpp
    └── block_common.hpp

inline_*.cpp
    └── inline_common.hpp
```
