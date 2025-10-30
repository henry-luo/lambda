# HTML5 Parser Refactoring Plan

## Overview

Refactor the existing `lambda/input/input-html.cpp` parser to be more HTML5 compliant while preserving its working state. This incremental approach is more practical than building from scratch.

## Current State Analysis

### Strengths of Existing Parser
- ✅ **Working and stable** - handles real-world HTML
- ✅ **Good entity support** - comprehensive HTML entity decoding
- ✅ **Error handling** - position tracking and error logging
- ✅ **Special features**:
  - Custom element validation (HTML5 feature)
  - Data attributes and ARIA support
  - Implicit tbody creation (partial HTML5 compliance)
  - Comment, DOCTYPE, and PI handling
- ✅ **Raw text element support** - script, style, textarea, title
- ✅ **Proper memory management** - uses pool allocation

### HTML5 Compliance Gaps
- ❌ No proper tree construction algorithm (insertion modes)
- ❌ Missing implicit element creation (html, head, body)
- ❌ No adoption agency algorithm for misnested tags
- ❌ No foster parenting for misplaced table content
- ❌ Limited void element handling
- ❌ No active formatting elements tracking
- ❌ Missing character reference handling in attributes

## Refactoring Strategy

### Phase 1: Extract and Modularize (2-3 days)
**Goal**: Break down the monolithic parser into manageable modules without changing behavior.

#### 1.1 Create Tokenization Module
- Extract character-level scanning into separate functions
- Create clear token types (similar to HTML5 tokenizer)
- Keep existing recursive descent structure but add token abstraction

**Files to create**:
- `input-html-scan.cpp` - Low-level scanning helpers
- `input-html-tokens.h` - Token type definitions (lightweight)

**Changes**:
- Refactor `parse_element()` to use token-based helpers
- Extract `skip_whitespace()`, `parse_attributes()`, etc. into scan module
- **No behavioral changes** - just reorganization

#### 1.2 Separate Tree Construction Logic
- Extract element insertion logic into dedicated functions
- Create clear insertion point tracking

**Files to create**:
- `input-html-tree.cpp` - Tree construction helpers

**Functions to extract**:
```cpp
// Current mixed logic → Separate tree functions
Element* html_create_element(Input*, const char* tag_name);
void html_append_child(Element* parent, Item child);
void html_insert_before(Element* parent, Item child, Item ref_child);
Element* html_get_current_node(ParserContext* ctx);
```

### Phase 2: Add HTML5 Void Element Handling (1-2 days)
**Goal**: Properly handle self-closing tags per HTML5 spec.

#### 2.1 Define Void Elements
```cpp
static const char* HTML5_VOID_ELEMENTS[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "param", "source", "track", "wbr", NULL
};
```

#### 2.2 Update Element Parsing
- Check if element is void before looking for end tag
- Ignore self-closing flag for non-void elements (HTML5 behavior)
- Proper handling in `parse_element()` around line 750

**Test cases**:
```html
<br>        <!-- No end tag needed -->
<img/>      <!-- Slash ignored in HTML5 -->
<div/>      <!-- Treated as <div> (no self-closing in HTML5) -->
```

### Phase 3: Implicit Element Creation (2-3 days)
**Goal**: Create missing html, head, and body elements per HTML5 spec.

#### 3.1 Add Parser Context
```cpp
typedef struct {
    Element* document_root;  // Always <html>
    Element* html_element;
    Element* head_element;
    Element* body_element;
    Element* current_node;
    bool in_head;
    bool in_body;
    // ... existing parse_depth, etc.
} HtmlParserContext;
```

#### 3.2 Implicit Creation Rules
- When first element encountered:
  - If not `<html>`, create implicit `<html>`
  - If not `<head>`, create implicit `<head>`
- When body content encountered:
  - If no `<body>` yet, create implicit `<body>`
- When encountering head content outside head:
  - Close head, create body if needed

**Integration points**:
- `input_read_html()` function around line 1050
- Element parsing in `parse_element()` around line 700

### Phase 4: Basic Insertion Modes (3-4 days)
**Goal**: Add simplified insertion mode tracking without full state machine.

#### 4.1 Define Lightweight Modes
```cpp
typedef enum {
    HTML_MODE_INITIAL,      // Before <html>
    HTML_MODE_BEFORE_HEAD,  // After <html>, before <head>
    HTML_MODE_IN_HEAD,      // Inside <head>
    HTML_MODE_AFTER_HEAD,   // After </head>, before <body>
    HTML_MODE_IN_BODY,      // Inside <body> (most content)
    HTML_MODE_AFTER_BODY,   // After </body>
} HtmlInsertionMode;
```

#### 4.2 Mode-Aware Parsing
- Track current mode in parser context
- Adjust element insertion based on mode
- Create implicit elements when mode transitions

**Key transitions**:
```cpp
void html_transition_mode(HtmlParserContext* ctx, HtmlInsertionMode new_mode) {
    switch (new_mode) {
        case HTML_MODE_IN_HEAD:
            if (!ctx->head_element) {
                ctx->head_element = html_create_implicit_element(ctx, "head");
            }
            break;
        case HTML_MODE_IN_BODY:
            if (ctx->in_head) {
                // Close head
                ctx->in_head = false;
            }
            if (!ctx->body_element) {
                ctx->body_element = html_create_implicit_element(ctx, "body");
            }
            break;
        // ... other cases
    }
}
```

### Phase 5: Element Stack Management (2-3 days)
**Goal**: Track open elements properly for nesting validation.

#### 5.1 Add Element Stack
```cpp
typedef struct {
    Element** elements;
    size_t length;
    size_t capacity;
    Pool* pool;
} HtmlElementStack;

void html_stack_push(HtmlElementStack* stack, Element* element);
Element* html_stack_pop(HtmlElementStack* stack);
Element* html_stack_peek(HtmlElementStack* stack);
bool html_stack_contains(HtmlElementStack* stack, const char* tag_name);
```

#### 5.2 Use Stack for Nesting
- Push elements when opened
- Pop elements when closed
- Validate nesting (optional - can log warnings)
- Use for `current_node` tracking

### Phase 6: Special Element Handling (2-3 days)
**Goal**: Improve handling of script, style, template, and formatting elements.

#### 6.1 Enhanced Raw Text Elements
- Use insertion modes for proper context
- Handle RCDATA vs RAWTEXT differences
- Template element special case

#### 6.2 Formatting Elements (Partial)
- Track common formatting elements: `<b>`, `<i>`, `<strong>`, `<em>`, etc.
- Simple reconstruction (not full adoption agency yet)

### Phase 7: Foster Parenting (Optional - 2 days)
**Goal**: Handle table misnesting per HTML5.

#### 7.1 Detect Table Context
```cpp
bool html_is_in_table_context(HtmlParserContext* ctx);
Element* html_find_foster_parent(HtmlParserContext* ctx);
```

#### 7.2 Foster Parent Insertion
- When inserting non-table content in table
- Move content before table instead

### Phase 8: Testing & Validation (2-3 days)
**Goal**: Ensure refactoring doesn't break existing functionality.

#### 8.1 Regression Tests
- All existing tests must pass
- Add new HTML5 compliance tests
- Test implicit element creation
- Test void element handling

#### 8.2 HTML5 Test Suite (Subset)
- Run simplified HTML5lib tests
- Focus on tree construction
- Document known limitations

## Implementation Guidelines

### Code Style
- Maintain existing naming conventions
- Use pool allocation consistently
- Keep error logging patterns
- Preserve position tracking

### Backward Compatibility
- Don't break existing `input_read_html()` API
- Maintain compatibility with rest of input system
- Keep existing test cases passing

### Incremental Approach
- Each phase should build on previous
- Each phase should be testable independently
- Each phase should leave codebase in working state
- Can pause at any phase if time-constrained

## Success Metrics

### Minimum Success (Phase 1-3)
- ✅ Code is modular and maintainable
- ✅ Void elements handled correctly
- ✅ Implicit html/head/body created
- ✅ All existing tests pass

### Good Success (Phase 1-5)
- ✅ Above +
- ✅ Basic insertion modes working
- ✅ Element stack for proper nesting
- ✅ Most common HTML5 patterns work

### Complete Success (Phase 1-8)
- ✅ Above +
- ✅ Foster parenting for tables
- ✅ Formatting element tracking
- ✅ Passes HTML5lib test subset

## Timeline Estimate

- **Minimum**: 5-8 days (Phases 1-3)
- **Recommended**: 9-13 days (Phases 1-5)
- **Complete**: 15-20 days (Phases 1-8)

## Next Steps

1. Review this plan with team
2. Choose target completion level (minimum/good/complete)
3. Start with Phase 1.1 (tokenization extraction)
4. Set up incremental testing framework
5. Execute phases sequentially

## Notes

- This approach preserves the working parser while gradually improving HTML5 compliance
- Each phase can be validated independently
- Can stop at any phase and still have improvements
- Much lower risk than rewriting from scratch
- Leverages existing entity handling, error reporting, and memory management
