# Markdown Parser Enhancement Proposal

## Executive Summary

Based on testing against the **CommonMark 0.31.2 specification** (655 test cases), the Lambda markup parser currently achieves **13.1% compliance** (86/655 tests passing). This proposal outlines a structured enhancement plan to achieve **>90% CommonMark compliance** through targeted improvements to the modular parser architecture.

## Current State Analysis

### Test Results by Category

| Priority | Section | Pass | Fail | Rate | Impact |
|----------|---------|------|------|------|--------|
| P0 | Fenced code blocks | 0 | 29 | 0.0% | High - common feature |
| P0 | Indented code blocks | 0 | 12 | 0.0% | Medium - legacy syntax |
| P0 | Backslash escapes | 1 | 12 | 7.7% | High - core mechanism |
| P1 | Block quotes | 1 | 24 | 4.0% | High - common feature |
| P1 | Links | 5 | 85 | 5.6% | Critical - most used |
| P1 | Link reference definitions | 0 | 27 | 0.0% | High - reference links |
| P1 | Emphasis and strong | 20 | 112 | 15.2% | Critical - most used |
| P2 | Setext headings | 3 | 24 | 11.1% | Medium - alternative syntax |
| P2 | ATX headings | 7 | 11 | 38.9% | High - well supported |
| P2 | Lists/List items | 11 | 63 | 14.9% | High - common feature |
| P3 | HTML blocks | 0 | 46 | 0.0% | Medium - passthrough |
| P3 | Images | 1 | 21 | 4.5% | Medium - similar to links |
| P3 | Code spans | 5 | 17 | 22.7% | Medium - inline code |

### Root Cause Analysis

From examining specific test failures, the following core issues were identified:

1. **Escape Handling** - `\` prefix not processed as escape character
2. **Trailing Delimiter Stripping** - Headers don't strip trailing `#` markers
3. **Indentation Semantics** - 4-space indent not recognized as code block
4. **Whitespace Normalization** - Excessive whitespace not trimmed
5. **Reference Resolution** - Link/image reference definitions not supported
6. **Nesting Rules** - Complex nesting (lists in quotes, etc.) incomplete
7. **HTML Passthrough** - Raw HTML blocks not preserved

## Proposed Architecture

### Phase 1: Core Escape System (P0)

**Files to modify:**
- `lambda/input/markup/inline/inline_special.cpp`
- `lambda/input/markup/markup_common.hpp`

**Changes:**

1. Add escape character map:
```cpp
// markup_common.hpp
static const char ESCAPABLE_CHARS[] =
    "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

inline bool is_escapable(char c) {
    return strchr(ESCAPABLE_CHARS, c) != nullptr;
}
```

2. Process escapes early in inline parsing:
```cpp
// inline_special.cpp
Item parse_backslash_escape(MarkupParser* parser, const char* text, int* consumed) {
    if (*text != '\\') return ITEM_UNDEFINED;

    char next = text[1];
    if (next == '\0') return ITEM_UNDEFINED;

    // Hard line break: backslash at end of line
    if (next == '\n') {
        *consumed = 2;
        return parser->builder()->createElement("br");
    }

    // Escapable punctuation
    if (is_escapable(next)) {
        *consumed = 2;
        return parser->builder()->createString(&next, 1);
    }

    return ITEM_UNDEFINED;  // Not an escape, treat as literal
}
```

**Expected improvement:** +12 tests (Backslash escapes section)

### Phase 2: Code Block Fixes (P0)

**Files to modify:**
- `lambda/input/markup/block/block_code.cpp`
- `lambda/input/markup/block/block_detection.cpp`

**Changes:**

1. Fix indented code block detection (4+ spaces):
```cpp
// block_detection.cpp
bool is_indented_code_line(const char* line) {
    int spaces = 0;
    while (*line == ' ') { spaces++; line++; }
    return spaces >= 4 && *line != '\0' && *line != '\n';
}

BlockType detect_block_type(MarkupParser* parser, const char* line) {
    // Check indented code BEFORE other block types
    if (is_indented_code_line(line) && !parser->state.in_list_item) {
        return BLOCK_INDENTED_CODE;
    }
    // ... rest of detection
}
```

2. Fix fenced code block info string parsing:
```cpp
// block_code.cpp
Item parse_fenced_code_block(MarkupParser* parser, const char* line) {
    CodeFenceInfo fence;
    if (!parser->adapter->detect_code_fence(line, &fence)) {
        return ITEM_UNDEFINED;
    }

    // Parse info string (language + optional attributes)
    const char* info_start = line + fence.fence_length;
    while (*info_start == ' ') info_start++;

    // Info string ends at newline or backtick (CommonMark rule)
    const char* info_end = info_start;
    while (*info_end && *info_end != '\n' && *info_end != '`') {
        info_end++;
    }

    // Trim trailing whitespace from info string
    while (info_end > info_start && isspace(info_end[-1])) {
        info_end--;
    }

    // ... create code element with info attribute
}
```

3. Preserve exact content in code blocks (no escape processing):
```cpp
// Code block content is literal - no inline processing
while (parser->advance_line()) {
    const char* code_line = parser->current_line;

    // Check for closing fence
    if (is_closing_fence(code_line, fence)) {
        break;
    }

    // Append raw content (no escape processing)
    content.append(code_line);
    content.append("\n");
}
```

**Expected improvement:** +41 tests (Fenced + Indented code blocks)

### Phase 3: Header Improvements (P2)

**Files to modify:**
- `lambda/input/markup/block/block_header.cpp`

**Changes:**

1. Strip trailing `#` from ATX headers:
```cpp
// block_header.cpp
Item parse_atx_header(MarkupParser* parser, const char* line) {
    int level = 0;
    const char* p = line;

    // Count leading #
    while (*p == '#' && level < 6) { level++; p++; }

    // Must be followed by space or end of line
    if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\0') {
        return ITEM_UNDEFINED;  // Not a valid header
    }

    // Skip whitespace after #
    while (*p == ' ' || *p == '\t') p++;

    // Find content end (before trailing # sequence)
    const char* content_start = p;
    const char* content_end = p + strlen(p);

    // Trim trailing whitespace
    while (content_end > content_start && isspace(content_end[-1])) {
        content_end--;
    }

    // Strip trailing # sequence (optionally preceded by spaces)
    const char* hash_scan = content_end;
    while (hash_scan > content_start && hash_scan[-1] == '#') {
        hash_scan--;
    }
    // If we found trailing #, check if preceded by space
    if (hash_scan < content_end &&
        (hash_scan == content_start || hash_scan[-1] == ' ' || hash_scan[-1] == '\t')) {
        content_end = hash_scan;
        // Trim spaces before the trailing #
        while (content_end > content_start &&
               (content_end[-1] == ' ' || content_end[-1] == '\t')) {
            content_end--;
        }
    }

    // Create header element with trimmed content
    // ...
}
```

2. Implement Setext header detection:
```cpp
// block_header.cpp
Item parse_setext_header(MarkupParser* parser, const char* text_line, const char* underline) {
    // Underline must be all = or all - (at least 1 char)
    char marker = underline[0];
    if (marker != '=' && marker != '-') return ITEM_UNDEFINED;

    const char* p = underline;
    while (*p == marker) p++;

    // Only whitespace allowed after markers
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '\n' && *p != '\0') return ITEM_UNDEFINED;

    int level = (marker == '=') ? 1 : 2;

    // Create h1 or h2 with text_line content
    // ...
}
```

**Expected improvement:** +20 tests (ATX + Setext headings)

### Phase 4: Link and Reference System (P1)

**Files to modify:**
- `lambda/input/markup/inline/inline_link.cpp`
- `lambda/input/markup/markup_parser.hpp`
- `lambda/input/markup/markup_parser.cpp`

**Changes:**

1. Add reference definition storage:
```cpp
// markup_parser.hpp
struct LinkDefinition {
    std::string label;      // Normalized label (case-insensitive)
    std::string url;
    std::string title;      // Optional
};

class MarkupParser : public InputContext {
    // ...
    std::unordered_map<std::string, LinkDefinition> link_definitions_;

public:
    void add_link_definition(const char* label, const char* url, const char* title);
    const LinkDefinition* get_link_definition(const char* label);

    // Label normalization: lowercase, collapse whitespace
    static std::string normalize_label(const char* label);
};
```

2. Parse link reference definitions as block elements:
```cpp
// block_detection.cpp - add to detect_block_type()
if (line[0] == '[') {
    // Potential link reference definition
    // [label]: url "title"
    if (looks_like_link_definition(line)) {
        return BLOCK_LINK_DEFINITION;
    }
}
```

3. Resolve reference links during inline parsing:
```cpp
// inline_link.cpp
Item parse_reference_link(MarkupParser* parser, const char* text, int* consumed) {
    // [link text][label] or [link text][] or [link text]

    // Parse [link text]
    const char* p = text + 1;
    const char* link_text_end = find_matching_bracket(p);
    if (!link_text_end) return ITEM_UNDEFINED;

    std::string link_text(p, link_text_end - p);
    p = link_text_end + 1;

    std::string label;
    if (*p == '[') {
        // [text][label] form
        const char* label_end = find_matching_bracket(p + 1);
        if (!label_end) return ITEM_UNDEFINED;
        label = std::string(p + 1, label_end - p - 1);
        p = label_end + 1;
    } else {
        // [text] form - label is same as text
        label = link_text;
    }

    // Look up reference
    const LinkDefinition* def = parser->get_link_definition(label.c_str());
    if (!def) return ITEM_UNDEFINED;  // No matching definition

    *consumed = p - text;
    return create_link_element(parser, def->url, def->title, link_text);
}
```

**Expected improvement:** +50 tests (Links + Link reference definitions)

### Phase 5: Emphasis Rules (P1)

**Files to modify:**
- `lambda/input/markup/inline/inline_emphasis.cpp`

**Changes:**

Implement CommonMark's delimiter run algorithm:

```cpp
// inline_emphasis.cpp

struct DelimiterRun {
    char character;         // * or _
    int length;             // Number of consecutive delimiters
    bool can_open;          // Can open emphasis
    bool can_close;         // Can close emphasis
    int text_position;      // Position in text
};

// Determine if delimiter run can open/close based on CommonMark rules
void classify_delimiter_run(const char* text, int pos, DelimiterRun* run) {
    char c = text[pos];
    run->character = c;

    // Count consecutive delimiters
    int len = 0;
    while (text[pos + len] == c) len++;
    run->length = len;

    // Get characters before and after the run
    char before = (pos > 0) ? text[pos - 1] : ' ';
    char after = text[pos + len];
    if (after == '\0') after = ' ';

    bool left_flanking = !isspace(after) &&
        (!ispunct(after) || isspace(before) || ispunct(before));
    bool right_flanking = !isspace(before) &&
        (!ispunct(before) || isspace(after) || ispunct(after));

    if (c == '*') {
        run->can_open = left_flanking;
        run->can_close = right_flanking;
    } else {  // '_'
        run->can_open = left_flanking &&
            (!right_flanking || ispunct(before));
        run->can_close = right_flanking &&
            (!left_flanking || ispunct(after));
    }
}

// Process emphasis using delimiter stack
Item process_emphasis(MarkupParser* parser, const char* text) {
    std::vector<DelimiterRun> stack;
    std::vector<Item> output;

    // First pass: identify all delimiter runs
    // Second pass: match openers with closers
    // Third pass: build AST with proper nesting

    // ... implementation following CommonMark algorithm
}
```

**Expected improvement:** +60 tests (Emphasis and strong emphasis)

### Phase 6: Block Quote Improvements (P1)

**Files to modify:**
- `lambda/input/markup/block/block_quote.cpp`

**Changes:**

1. Handle lazy continuation lines:
```cpp
// block_quote.cpp
Item parse_blockquote(MarkupParser* parser, const char* line) {
    // Consume > prefix
    if (*line != '>') return ITEM_UNDEFINED;
    line++;
    if (*line == ' ') line++;  // Optional space after >

    auto quote = parser->builder()->createElement("blockquote");

    while (parser->current_line) {
        const char* curr = parser->current_line;

        if (*curr == '>') {
            // Continuation with > prefix
            curr++;
            if (*curr == ' ') curr++;
            // Parse content...
        } else if (is_lazy_continuation(parser, curr)) {
            // Lazy continuation (paragraph continues without >)
            // Only valid if we're in a paragraph
            // ...
        } else if (is_empty_line(curr)) {
            // Empty line - may end quote or separate blocks
            // ...
        } else {
            // Not a blockquote line - done
            break;
        }

        parser->advance_line();
    }

    return quote;
}
```

**Expected improvement:** +15 tests (Block quotes)

### Phase 7: HTML Block Passthrough (P3)

**Files to modify:**
- `lambda/input/markup/block/block_html.cpp` (new file)
- `lambda/input/markup/block/block_common.hpp`
- `lambda/input/markup/block/block_detection.cpp`

**Changes:**

1. Detect HTML block start conditions (7 types per CommonMark):
```cpp
// block_html.cpp

enum HtmlBlockType {
    HTML_BLOCK_NONE = 0,
    HTML_BLOCK_1,  // <script>, <pre>, <style>, <textarea>
    HTML_BLOCK_2,  // <!-- comment -->
    HTML_BLOCK_3,  // <? processing instruction ?>
    HTML_BLOCK_4,  // <!DOCTYPE>
    HTML_BLOCK_5,  // <![CDATA[
    HTML_BLOCK_6,  // Block-level HTML tags
    HTML_BLOCK_7,  // Single line with inline HTML
};

HtmlBlockType detect_html_block(const char* line) {
    if (*line != '<') return HTML_BLOCK_NONE;

    // Type 1: script, pre, style, textarea
    if (strncasecmp(line, "<script", 7) == 0 ||
        strncasecmp(line, "<pre", 4) == 0 ||
        strncasecmp(line, "<style", 6) == 0 ||
        strncasecmp(line, "<textarea", 9) == 0) {
        return HTML_BLOCK_1;
    }

    // Type 2: HTML comment
    if (strncmp(line, "<!--", 4) == 0) {
        return HTML_BLOCK_2;
    }

    // ... other types
}

Item parse_html_block(MarkupParser* parser, const char* line) {
    HtmlBlockType type = detect_html_block(line);
    if (type == HTML_BLOCK_NONE) return ITEM_UNDEFINED;

    auto html_elem = parser->builder()->createElement("html_block");
    StrBuf content;
    strbuf_init(&content);

    // Accumulate raw HTML until end condition
    do {
        strbuf_append(&content, parser->current_line);
        strbuf_append(&content, "\n");

        if (html_block_ends(type, parser->current_line)) {
            break;
        }
    } while (parser->advance_line());

    // Store as raw content, no parsing
    parser->builder()->appendText(html_elem, content.str);
    return html_elem;
}
```

**Expected improvement:** +30 tests (HTML blocks + Raw HTML)

## Implementation Schedule

| Phase | Focus | Files | Est. Tests Fixed | Timeline |
|-------|-------|-------|------------------|----------|
| 1 | Escape System | 2 | +12 | 2 hours |
| 2 | Code Blocks | 2 | +41 | 3 hours |
| 3 | Headers | 1 | +20 | 2 hours |
| 4 | Links & References | 3 | +50 | 4 hours |
| 5 | Emphasis Rules | 1 | +60 | 4 hours |
| 6 | Block Quotes | 1 | +15 | 2 hours |
| 7 | HTML Passthrough | 3 | +30 | 3 hours |

**Total estimated improvement:** +228 tests → **~48% compliance** (314/655)

## Additional Phases (Future)

### Phase 8: List Refinements
- Loose vs tight list detection
- List item paragraph wrapping rules
- Start number for ordered lists

### Phase 9: Edge Cases
- Tabs vs spaces normalization
- Unicode whitespace handling
- Line ending normalization (CRLF/LF)

### Phase 10: Extensions (GFM)
- Tables (partially implemented)
- Strikethrough
- Autolinks extension
- Task lists

## Testing Strategy

1. **Incremental testing** - Run CommonMark suite after each phase
2. **Regression testing** - Ensure existing tests don't break
3. **Unit tests** - Add targeted tests for each fix in `test_markup_modular_gtest.cpp`

```bash
# Run full CommonMark suite
./test/test_commonmark_spec_gtest.exe --gtest_filter=CommonMarkSpecTest.ComprehensiveStats

# Run specific section
./test/test_commonmark_spec_gtest.exe --gtest_filter=CommonMarkSpecTest.ATXHeadings

# Run baseline tests (must stay 100%)
make test-lambda-baseline
```

## Success Criteria

| Milestone | Target Compliance | Tests Passing |
|-----------|-------------------|---------------|
| Current | 13.1% | 86/655 |
| Phase 1-3 | 25% | ~164/655 |
| Phase 4-5 | 42% | ~275/655 |
| Phase 6-7 | 48% | ~314/655 |
| Phase 8-10 | >90% | >590/655 |

## Appendix: CommonMark Spec Reference

- **Spec URL:** https://spec.commonmark.org/0.31.2/
- **Test file:** `test/markup/commonmark/spec.txt` (655 examples)
- **Test runner:** `test/markup/test_commonmark_spec_gtest.cpp`

### Key CommonMark Parsing Principles

1. **Two-phase parsing:** Block structure first, then inline content
2. **Lazy continuation:** Paragraphs in containers continue without prefix
3. **Precedence:** Earlier block types take priority
4. **Delimiter matching:** Emphasis uses stack-based algorithm
5. **Normalization:** Labels/URLs are normalized for matching
