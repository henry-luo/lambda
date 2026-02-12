# Lambda Input Parser Enhancement - Round 2

**Date**: November 18, 2025
**Status**: Proposal
**Priority**: High - Foundational improvements for parser consistency

---

## Executive Summary

This document proposes Round 2 enhancements to Lambda's input parsing system, building on Round 1 (InputContext introduction). The focus is on **API consolidation**, **context unification**, and **eliminating legacy C functions** to create a clean, consistent parser architecture across all 40+ format parsers.

**Key Goals**:
1. ✅ Make `map_put()` static - migrate all usage to `MarkBuilder`
2. ✅ Remove `input_add_attribute_to_element()` and migrate to `MarkBuilder::attr()`
3. ✅ Make `input_create_element()` static - migrate to `MarkBuilder::element()`
4. ✅ Unify `HtmlParserContext` with `InputContext`
5. ✅ Unify `MarkupParser` with `InputContext` (consolidate `ParseConfig`)
6. 🎯 Establish consistent patterns across all parsers

**Estimated Impact**: ~40 parser files, ~2,000 lines of migrations
**Timeline**: 2-3 weeks for full implementation

---

## Current State Analysis

### 1. Legacy API Usage Breakdown

**`map_put()` Usage** (100+ call sites):
- ❌ Directly called by parsers: `input-eml.cpp` (8), `input-ini.cpp` (3), `input-ics.cpp` (46), `input-pdf.cpp` (80), `input-vcf.cpp` (15), `input-toml.cpp` (4), `input-rtf.cpp` (5), `input-mark.cpp` (1), `input-prop.cpp` (1)
- ✅ Already using MarkBuilder: `input-json.cpp`, `input-xml.cpp`, `input-yaml.cpp`, `input-csv.cpp`, `input-html.cpp`

**`input_create_element()` Usage** (52 call sites):
- Files using it: `input-css.cpp` (8), `input-html.cpp` (2), `input-jsx.cpp` (3), `input-html-context.cpp` (4), `input_sysinfo.cpp` (11), `input-mark.cpp` (1), `input-mdx.cpp` (3), `input-graph.cpp` (4), `input_dir.cpp` (1)
- ✅ Some already migrating to ElementBuilder

**`input_add_attribute_to_element()` / `input_add_attribute_item_to_element()` Usage** (100+ call sites):
- Heavy users: `input-css.cpp` (20+), `input-jsx.cpp` (9), `input_sysinfo.cpp` (20+), `input-mark.cpp` (1), `input-mdx.cpp` (2), `input_dir.cpp` (5), `input-graph.cpp` (10+)
- Many use `#define add_attribute_to_element input_add_attribute_to_element` macro pattern

### 2. Context Architecture Issues

**Current Context Types**:

```cpp
// 1. InputContext (Round 1 - modern, unified)
class InputContext {
    Input* input_;
    MarkBuilder builder_;
    ParseErrorList errors_;
    SourceTracker* tracker_;
};

// 2. HtmlParserContext (specialized, C struct)
typedef struct HtmlParserContext {
    Input* input;                        // ❌ Redundant with InputContext
    Element* html_element;
    Element* head_element;
    Element* body_element;
    Element* current_node;
    HtmlInsertionMode insertion_mode;
    HtmlElementStack* open_elements;
    HtmlFormattingList* active_formatting;
    // ... HTML5 state machine data
} HtmlParserContext;

// 3. MarkupParser (specialized, C struct)
typedef struct MarkupParser {
    Input* input;                        // ❌ Redundant with InputContext
    ParseConfig config;
    char** lines;
    int line_count;
    int current_line;
    struct {
        char list_markers[10];
        int list_levels[10];
        int list_depth;
        bool in_code_block;
        // ... parser state
    } state;
} MarkupParser;

// 4. ParseConfig (part of MarkupParser)
typedef struct {
    MarkupFormat format;
    const char* flavor;
    bool strict_mode;
} ParseConfig;
```

**Problems**:
- 🔴 **Duplication**: All three contexts store `Input*`
- 🔴 **No composition**: Can't use InputContext features (errors, tracking) in specialized parsers
- 🔴 **Inconsistent APIs**: Different ways to create elements/maps across contexts
- 🔴 **Memory management confusion**: Mix of pool, arena, malloc/free
- 🔴 **No builder integration**: HtmlParserContext/MarkupParser can't use MarkBuilder easily

### 3. Current MarkBuilder API

**Available** (already in Round 1):
```cpp
class MarkBuilder {
    String* createString(const char* str);
    ElementBuilder element(const char* tag_name);
    MapBuilder map();
    ArrayBuilder array();
    Item createMap();
    Item createArray();
    Item createStringItem(const char* str);
    // ... primitive creators
};

class ElementBuilder {
    ElementBuilder& attr(const char* key, Item value);
    ElementBuilder& attr(const char* key, const char* value);
    ElementBuilder& child(Item item);
    ElementBuilder& text(const char* text);
    Item final();
};

class MapBuilder {
    MapBuilder& put(const char* key, Item value);
    MapBuilder& put(const char* key, const char* value);
    Item final();
};
```

**Missing** (needed for full migration):
- ❌ No `ElementBuilder::attr()` overload for `String*` keys (parsers already have String*)
- ❌ No `elmt_put()` equivalent in ElementBuilder (needed for HTML attribute setting)
- ❌ No direct element child append in builder

---

## Proposed Architecture

### Phase 1: Context Hierarchy & Unification

#### 1.1 Unified Context Base

```cpp
namespace lambda {

/**
 * InputContext - Base context for all parsers
 * Provides: Input, MarkBuilder, error tracking, optional source tracking
 */
class InputContext {
private:
    Input* input_;
    MarkBuilder builder_;
    ParseErrorList errors_;
    SourceTracker* tracker_;
    bool owns_tracker_;

public:
    // Existing constructors
    explicit InputContext(Input* input);
    InputContext(Input* input, const char* source, size_t len);

    // Accessors
    Input* input() { return input_; }
    MarkBuilder& builder() { return builder_; }
    ParseErrorList& errors() { return errors_; }
    SourceTracker* tracker() { return tracker_; }

    // Error reporting
    void addError(const SourceLocation& loc, const std::string& message);
    void addWarning(const SourceLocation& loc, const std::string& message);
    // ... etc
};

/**
 * ParseConfig - Unified configuration for all parsers
 * Replaces MarkupParser::ParseConfig and can be extended for other formats
 */
struct ParseConfig {
    // Format identification
    const char* format_name;     // "markdown", "html", "css", etc.
    const char* flavor;          // "github", "commonmark", "html5", etc.

    // General options
    bool strict_mode;            // Strict vs. lenient parsing
    bool preserve_whitespace;    // Keep all whitespace
    bool track_positions;        // Enable source location tracking

    // Format-specific flags (can be extended)
    union {
        struct {
            bool auto_close_tags;
            bool allow_custom_elements;
        } html;

        struct {
            bool gfm_extensions;
            bool math_support;
        } markdown;
    };

    // Default constructor
    ParseConfig()
        : format_name(nullptr)
        , flavor(nullptr)
        , strict_mode(false)
        , preserve_whitespace(false)
        , track_positions(true)
    {}
};

} // namespace lambda
```

#### 1.2 Specialized Context Extensions

**HtmlInputContext** - Extends InputContext with HTML5 state machine:

```cpp
namespace lambda {

/**
 * HtmlInputContext - HTML parser context extending InputContext
 * Adds HTML5 parsing state machine while reusing InputContext infrastructure
 */
class HtmlInputContext : public InputContext {
private:
    // HTML5 document structure
    Element* html_element_;
    Element* head_element_;
    Element* body_element_;
    Element* current_node_;

    // HTML5 parsing state
    HtmlInsertionMode insertion_mode_;
    HtmlElementStack* open_elements_;
    HtmlFormattingList* active_formatting_;

    // Explicit tag flags
    bool has_explicit_html_;
    bool has_explicit_head_;
    bool has_explicit_body_;
    bool in_head_;
    bool head_closed_;
    bool in_body_;

public:
    /**
     * Constructor - initializes InputContext + HTML state
     */
    explicit HtmlInputContext(Input* input, const char* html_source = nullptr, size_t len = 0)
        : InputContext(input, html_source, len)
        , html_element_(nullptr)
        , head_element_(nullptr)
        , body_element_(nullptr)
        , current_node_(nullptr)
        , insertion_mode_(HTML_MODE_INITIAL)
        , open_elements_(nullptr)
        , active_formatting_(nullptr)
        , has_explicit_html_(false)
        , has_explicit_head_(false)
        , has_explicit_body_(false)
        , in_head_(false)
        , head_closed_(false)
        , in_body_(false)
    {
        // Initialize HTML5 state machine
        open_elements_ = html_stack_create(input->pool);
        active_formatting_ = html_formatting_create(input->pool);
    }

    ~HtmlInputContext() {
        if (open_elements_) html_stack_destroy(open_elements_);
        if (active_formatting_) html_formatting_destroy(active_formatting_);
    }

    // HTML5-specific methods
    Element* ensureHtml();
    Element* ensureHead();
    Element* ensureBody();
    Element* getInsertionPoint(const char* tag_name);
    void transitionMode(const char* tag_name, bool is_closing);
    void closeHead();

    // Stack management
    void pushElement(Element* element);
    Element* popElement();
    Element* currentElement() const;

    // Formatting elements
    void pushFormatting(Element* element);
    void removeFormatting(Element* element);
    void reconstructFormatting(Element* parent);

    // Accessors
    HtmlInsertionMode mode() const { return insertion_mode_; }
    void setMode(HtmlInsertionMode mode) { insertion_mode_ = mode; }

    Element* htmlElement() const { return html_element_; }
    Element* headElement() const { return head_element_; }
    Element* bodyElement() const { return body_element_; }
    Element* currentNode() const { return current_node_; }
};

} // namespace lambda
```

**MarkupInputContext** - Extends InputContext with markup parsing state:

```cpp
namespace lambda {

/**
 * MarkupInputContext - Markup parser context extending InputContext
 * Supports Markdown, reStructuredText, Textile, Wiki, Org-mode, AsciiDoc
 */
class MarkupInputContext : public InputContext {
private:
    ParseConfig config_;

    // Line-based parsing state
    char** lines_;
    int line_count_;
    int current_line_;

    // Parsing state
    struct {
        char list_markers[10];
        int list_levels[10];
        int list_depth;

        char table_state;
        bool in_code_block;
        char code_fence_char;
        int code_fence_length;

        bool in_math_block;
        char math_delimiter[10];

        int header_level;
        bool in_quote_block;
        int quote_depth;
        bool in_table;
        int table_columns;
    } state_;

public:
    /**
     * Constructor with configuration
     */
    MarkupInputContext(Input* input, const ParseConfig& config, const char* source)
        : InputContext(input, source, strlen(source))
        , config_(config)
        , lines_(nullptr)
        , line_count_(0)
        , current_line_(0)
    {
        resetState();
    }

    ~MarkupInputContext() {
        if (lines_) {
            input_free_lines(lines_, line_count_);
        }
    }

    // Line access
    void splitLines(const char* content);
    const char* currentLine() const;
    const char* peekLine(int offset = 1) const;
    bool hasMoreLines() const;
    void advanceLine();

    // State management
    void resetState();

    // Configuration
    const ParseConfig& config() const { return config_; }
    MarkupFormat format() const { return (MarkupFormat)0; } // TODO: add to config

    // State accessors
    bool inCodeBlock() const { return state_.in_code_block; }
    void setInCodeBlock(bool val) { state_.in_code_block = val; }

    bool inMathBlock() const { return state_.in_math_block; }
    void setInMathBlock(bool val) { state_.in_math_block = val; }

    int listDepth() const { return state_.list_depth; }
    void pushListLevel(char marker, int indent);
    void popListLevel();
};

} // namespace lambda
```

### Phase 2: MarkBuilder API Extensions

#### 2.1 Enhanced ElementBuilder

```cpp
class ElementBuilder {
public:
    // ... existing methods ...

    // NEW: Accept String* keys directly (avoid re-creating strings)
    ElementBuilder& attr(String* key, Item value);
    ElementBuilder& attr(String* key, const char* value);
    ElementBuilder& attr(String* key, int64_t value);
    ElementBuilder& attr(String* key, double value);
    ElementBuilder& attr(String* key, bool value);

    // NEW: Append child directly to existing element (for HTML parser)
    ElementBuilder& appendChild(Item child);

    // NEW: Get underlying element pointer (for HTML context manipulation)
    Element* element() { return elmt_; }
};
```

#### 2.2 Internal Helper - Replace map_put()

Make `map_put()` static in `input.cpp` and create internal helper:

```cpp
// In input.cpp - make static
static void map_put(Map* mp, String* key, Item value, Input *input) {
    // ... existing implementation ...
}

// In MarkBuilder - internal use only
void MarkBuilder::putToMap(Map* map, String* key, Item value) {
    map_put(map, key, value, input_);
}

// In MapBuilder
MapBuilder& MapBuilder::put(String* key, Item value) {
    builder_->putToMap(map_, key, value);
    return *this;
}
```

#### 2.3 Internal Helper - Replace elmt_put()

```cpp
// In input.cpp - keep as is (used internally)
void elmt_put(Element* elmt, String* key, Item value, Pool* pool) {
    // ... existing implementation ...
}

// In MarkBuilder - internal use only
void MarkBuilder::putToElement(Element* elmt, String* key, Item value) {
    elmt_put(elmt, key, value, pool_);
}

// In ElementBuilder
ElementBuilder& ElementBuilder::attr(String* key, Item value) {
    builder_->putToElement(elmt_, key, value);
    return *this;
}
```

---

## Migration Strategy

### Phase 1: Make Legacy Functions Static (Week 1)

**Files to modify**: `input.cpp`, `input.hpp`

#### Step 1.1: Make `map_put()` static

```cpp
// input.cpp - change to static
static void map_put(Map* mp, String* key, Item value, Input *input) {
    // ... existing implementation unchanged ...
}

// Remove from input.hpp
// void map_put(Map* mp, String* key, Item value, Input *input);  ❌ REMOVE
```

#### Step 1.2: Make `input_create_element()` static

```cpp
// input.cpp - change to static
static Element* input_create_element(Input *input, const char* tag_name) {
    // ... existing implementation unchanged ...
}

// Remove from input.hpp
// Element* input_create_element(Input *input, const char* tag_name);  ❌ REMOVE
```

#### Step 1.3: Remove attribute functions

```cpp
// input.cpp - DELETE these functions
// void input_add_attribute_to_element(Input *input, Element* element, const char* attr_name, const char* attr_value) { ... }
// void input_add_attribute_item_to_element(Input *input, Element* element, const char* attr_name, Item attr_value) { ... }

// Remove from input.hpp
// void input_add_attribute_to_element(...);  ❌ REMOVE
// void input_add_attribute_item_to_element(...);  ❌ REMOVE
```

### Phase 2: Migrate Parsers to MarkBuilder (Week 1-2)

**Priority order** (migrate in this sequence):

1. ✅ **Simple data formats** (low complexity, high impact):
   - `input-ini.cpp` (3 `map_put` calls)
   - `input-prop.cpp` (1 `map_put` call)
   - `input-mark.cpp` (1 `map_put`, 1 element, 1 attribute)

2. ✅ **Medium structured formats**:
   - `input-eml.cpp` (8 `map_put` calls)
   - `input-vcf.cpp` (15 `map_put` calls)
   - `input-toml.cpp` (4 `map_put` calls)
   - `input-rtf.cpp` (5 `map_put` calls)

3. 🟡 **Complex calendar/structured formats**:
   - `input-ics.cpp` (46 `map_put` calls) - largest map_put user
   - `input-vcf.cpp` structured value parsing

4. 🟡 **Document/visual formats**:
   - `input-css.cpp` (8 `input_create_element`, 20+ `input_add_attribute`)
   - `input-jsx.cpp` (3 `input_create_element`, 9 `input_add_attribute`)
   - `input-mdx.cpp` (3 `input_create_element`, 2 `input_add_attribute`)

5. 🔴 **PDF format** (largest user):
   - `input-pdf.cpp` (80 `map_put` calls) - needs careful refactoring

6. 🔴 **System/directory formats**:
   - `input_sysinfo.cpp` (11 `input_create_element`, 20+ `input_add_attribute`)
   - `input_dir.cpp` (1 `input_create_element`, 5 `input_add_attribute`)
   - `input-graph.cpp` (4 `input_create_element`, 10+ `input_add_attribute`)

#### Migration Pattern Example

**Before** (`input-ini.cpp`):
```cpp
void parse_ini(Input* input, const char* ini_string) {
    Map* root_map = map_pooled(input->pool);
    Map* section_map = map_pooled(input->pool);

    String* key = input_create_string(input, "key_name");
    Item value = {.item = s2it(input_create_string(input, "value"))};

    map_put(section_map, key, value, input);  // ❌ OLD
    map_put(root_map, section_name, {.item = (uint64_t)section_map}, input);  // ❌ OLD

    input->root = {.item = (uint64_t)root_map};
}
```

**After**:
```cpp
void parse_ini(Input* input, const char* ini_string) {
    InputContext ctx(input);
    MarkBuilder& b = ctx.builder;

    MapBuilder section = b.map();
    section.put("key_name", "value");  // ✅ NEW - direct string

    Item root = b.map()
        .put("section_name", section.final())  // ✅ NEW - fluent chaining
        .final();

    input->root = root;
}
```

**Before** (`input-css.cpp`):
```cpp
Element* stylesheet = input_create_element(ctx.input(), "stylesheet");  // ❌ OLD
input_add_attribute_item_to_element(ctx.input(), stylesheet, "rules", rules_item);  // ❌ OLD
```

**After**:
```cpp
Item stylesheet = ctx.builder.element("stylesheet")  // ✅ NEW
    .attr("rules", rules_item)  // ✅ NEW
    .final();
```

### Phase 3: Context Unification (Week 2)

#### Step 3.1: Extract HtmlInputContext

**File**: `lambda/input/html_input_context.hpp`, `html_input_context.cpp`

1. Create new `HtmlInputContext` class extending `InputContext`
2. Move all `HtmlParserContext` fields to private members
3. Keep all HTML5 state machine functions as methods
4. Add convenience accessors

**Migration**:
```cpp
// Before
HtmlParserContext* ctx = html_context_create(input);
Element* parent = html_context_ensure_body(ctx);
html_context_destroy(ctx);

// After
HtmlInputContext ctx(input, html_source, html_len);
Element* parent = ctx.ensureBody();
// Auto-cleanup via RAII
```

#### Step 3.2: Extract MarkupInputContext

**File**: `lambda/input/markup_input_context.hpp`, `markup_input_context.cpp`

1. Create new `MarkupInputContext` class extending `InputContext`
2. Move all `MarkupParser` fields to private members
3. Add `ParseConfig` as member
4. Keep parsing state and line management

**Migration**:
```cpp
// Before
ParseConfig config = {MARKUP_MARKDOWN, "github", false};
MarkupParser* parser = parser_create(input, config);
// ... parsing ...
parser_destroy(parser);

// After
ParseConfig config = {.format_name = "markdown", .flavor = "github", .strict_mode = false};
MarkupInputContext ctx(input, config, content);
// ... parsing ...
// Auto-cleanup via RAII
```

### Phase 4: Update Parser Call Sites (Week 2-3)

**Files to update**:

1. `input-html.cpp` - Use `HtmlInputContext` instead of `HtmlParserContext`
2. `input-markup.cpp` - Use `MarkupInputContext` instead of `MarkupParser`
3. All markup format parsers - Markdown, RST, Textile, Wiki, Org, AsciiDoc

**Pattern**:
```cpp
// Before
void parse_html_impl(Input* input, const char* html_string) {
    HtmlParserContext* context = html_context_create(input);
    InputContext ctx(input, html_string, strlen(html_string));
    // Two separate contexts!
    // ...
    html_context_destroy(context);
}

// After
void parse_html_impl(Input* input, const char* html_string) {
    HtmlInputContext ctx(input, html_string, strlen(html_string));
    // Single unified context with both features!
    // Use ctx.builder for creating elements
    // Use ctx.ensureBody() for HTML5 state machine
    // Use ctx.addError() for error reporting
}
```

---

## Implementation Checklist

### Week 1: Foundation & Simple Migrations

- [ ] **Day 1-2**: Make legacy functions static
  - [ ] Make `map_put()` static in `input.cpp`
  - [ ] Make `input_create_element()` static
  - [ ] Remove `input_add_attribute_to_element()`
  - [ ] Remove `input_add_attribute_item_to_element()`
  - [ ] Update `input.hpp` exports
  - [ ] Verify build succeeds (expecting link errors)

- [ ] **Day 3-4**: Extend MarkBuilder API
  - [ ] Add `ElementBuilder::attr(String*, Item)` overload
  - [ ] Add `ElementBuilder::attr(String*, const char*)` overload
  - [ ] Add `ElementBuilder::appendChild(Item)` method
  - [ ] Add `ElementBuilder::element()` accessor
  - [ ] Add `MapBuilder::put(String*, Item)` overload
  - [ ] Add internal `MarkBuilder::putToMap()` helper
  - [ ] Add internal `MarkBuilder::putToElement()` helper
  - [ ] Write unit tests for new APIs

- [ ] **Day 5**: Migrate simple parsers
  - [ ] `input-ini.cpp` (3 map_put calls)
  - [ ] `input-prop.cpp` (1 map_put call)
  - [ ] `input-mark.cpp` (1 map_put, 1 element, 1 attr)
  - [ ] Test all three parsers
  - [ ] Update documentation

### Week 2: Medium & Complex Migrations

- [ ] **Day 1-2**: Migrate medium parsers
  - [ ] `input-eml.cpp` (8 map_put)
  - [ ] `input-toml.cpp` (4 map_put)
  - [ ] `input-rtf.cpp` (5 map_put)
  - [ ] Test parsers

- [ ] **Day 3**: Migrate structured formats
  - [ ] `input-vcf.cpp` (15 map_put)
  - [ ] `input-ics.cpp` (46 map_put) - largest migration
  - [ ] Test calendar formats

- [ ] **Day 4-5**: Migrate document formats
  - [ ] `input-css.cpp` (8 elements, 20+ attrs)
  - [ ] `input-jsx.cpp` (3 elements, 9 attrs)
  - [ ] `input-mdx.cpp` (3 elements, 2 attrs)
  - [ ] `input-graph.cpp` (4 elements, 10+ attrs)
  - [ ] Test visual formats

### Week 3: Context Unification & PDF

- [ ] **Day 1-2**: Create unified contexts
  - [ ] Implement `HtmlInputContext` class
  - [ ] Implement `MarkupInputContext` class
  - [ ] Create `ParseConfig` unified struct
  - [ ] Write unit tests for contexts

- [ ] **Day 3**: Migrate HTML parser
  - [ ] Update `input-html.cpp` to use `HtmlInputContext`
  - [ ] Update `input-html-context.cpp` helper functions
  - [ ] Test HTML parsing thoroughly

- [ ] **Day 4**: Migrate markup parsers
  - [ ] Update `input-markup.cpp` to use `MarkupInputContext`
  - [ ] Update format-specific parsers (Markdown, RST, etc.)
  - [ ] Test all markup formats

- [ ] **Day 5**: Migrate PDF & system parsers
  - [ ] `input-pdf.cpp` (80 map_put - largest)
  - [ ] `input_sysinfo.cpp` (11 elements, 20+ attrs)
  - [ ] `input_dir.cpp` (1 element, 5 attrs)
  - [ ] Comprehensive testing

---

## Testing Strategy

### Unit Tests

Create `test/test_parser_context.cpp`:

```cpp
TEST(ParserContext, InputContextBasic) {
    Input* input = Input::create(pool);
    InputContext ctx(input);

    Item map = ctx.builder.map()
        .put("key", "value")
        .final();

    EXPECT_EQ(get_type_id(map), LMD_TYPE_MAP);
}

TEST(ParserContext, HtmlContextStateManagement) {
    Input* input = Input::create(pool);
    HtmlInputContext ctx(input);

    Element* body = ctx.ensureBody();
    EXPECT_NE(body, nullptr);
    EXPECT_EQ(ctx.htmlElement(), ctx.bodyElement()->parent);
}

TEST(ParserContext, MarkupContextLineManagement) {
    Input* input = Input::create(pool);
    ParseConfig config = {.format_name = "markdown"};
    MarkupInputContext ctx(input, config, "# Header\nText");

    ctx.splitLines(ctx.tracker->content());
    EXPECT_EQ(ctx.line_count, 2);
    EXPECT_STREQ(ctx.currentLine(), "# Header");
}
```

### Integration Tests

For each migrated parser:
1. Verify existing test suite passes
2. Add error tracking tests
3. Add position tracking tests
4. Test complex nested structures

### Regression Testing

```bash
# Run full test suite after each phase
make test

# Run specific parser tests
./build/test/test_input_json
./build/test/test_input_html
./build/test/test_input_css
```

---

## Benefits & Expected Outcomes

### Code Quality Improvements

1. **Consistency**: All parsers use same API patterns
2. **Safety**: Type-safe fluent API vs. raw C functions
3. **Maintainability**: Single source of truth for element/map creation
4. **Debugging**: Error tracking built into context
5. **Performance**: No change (same underlying allocators)

### API Simplification

**Before** (3 different ways to create elements):
```cpp
// Way 1: Legacy C function
Element* e1 = input_create_element(input, "div");
input_add_attribute_to_element(input, e1, "class", "content");

// Way 2: MarkBuilder (manual)
Element* e2 = elmt_pooled(input->pool);
// ... manual type setup ...

// Way 3: ElementBuilder (partial)
Item e3 = builder.element("div").final();
// ... but can't add attributes after creation
```

**After** (1 consistent way):
```cpp
// Single fluent API
Item e = ctx.builder.element("div")
    .attr("class", "content")
    .child(text_item)
    .final();
```

### Reduced Code Duplication

- **~500 lines** of string utility duplication → centralized in MarkBuilder
- **~200 lines** of element creation boilerplate → eliminated
- **~300 lines** of manual type setup → hidden in ElementBuilder

### Error Reporting Enhancement

**Before**:
```cpp
// No error context
if (parse_failed) {
    log_error("Parse failed");
    return NULL_ITEM;
}
```

**After**:
```cpp
// Rich error tracking
if (parse_failed) {
    ctx.addError("Expected closing tag");  // With line/column!
    ctx.addNote("Opening tag was here");
    return NULL_ITEM;
}
```

---

## Risk Mitigation

### Potential Issues

1. **Build breakage**: Making functions static will cause immediate link errors
   - ✅ Mitigation: Migrate in small batches, test frequently

2. **Performance regression**: Additional abstraction layers
   - ✅ Mitigation: MarkBuilder is stack-allocated RAII, zero overhead

3. **API confusion during transition**: Mix of old/new patterns
   - ✅ Mitigation: Clear migration order, update docs

4. **Memory leaks**: Changing allocation patterns
   - ✅ Mitigation: No allocation changes, only API wrappers

### Rollback Plan

If issues arise:
1. Revert `input.cpp`/`input.hpp` changes (restore exports)
2. Keep migrated parsers (they work with both APIs)
3. Complete migration in next round

---

## Future Enhancements (Round 3+)

After Round 2 is complete:

1. **Macro elimination**: Remove all `#define add_attribute_to_element` style macros
2. **C++ conversion**: Convert remaining C parsers to C++
3. **Template specialization**: Type-safe builders for specific formats
4. **Streaming parsers**: Large file support without loading entire content
5. **Parallel parsing**: Multi-threaded parser for large documents

---

## Appendix: File Impact Summary

### High Impact (>20 changes)
- `input-ics.cpp` - 46 map_put calls
- `input-pdf.cpp` - 80 map_put calls
- `input-css.cpp` - 8 elements + 20 attributes
- `input_sysinfo.cpp` - 11 elements + 20 attributes

### Medium Impact (5-20 changes)
- `input-vcf.cpp` - 15 map_put calls
- `input-eml.cpp` - 8 map_put calls
- `input-jsx.cpp` - 3 elements + 9 attributes
- `input-graph.cpp` - 4 elements + 10 attributes
- `input_dir.cpp` - 1 element + 5 attributes
- `input-toml.cpp` - 4 map_put calls
- `input-rtf.cpp` - 5 map_put calls

### Low Impact (<5 changes)
- `input-ini.cpp` - 3 map_put calls
- `input-mdx.cpp` - 3 elements + 2 attributes
- `input-prop.cpp` - 1 map_put call
- `input-mark.cpp` - 1 map_put + 1 element + 1 attribute

### Context Refactoring
- `input-html.cpp` - HtmlInputContext migration
- `input-html-context.cpp` - Convert to HtmlInputContext methods
- `input-markup.cpp` - MarkupInputContext migration
- `markup-parser.h` - Update to use unified ParseConfig

---

## Conclusion

Round 2 parser enhancements establish a **clean, unified architecture** across all Lambda parsers. By making legacy C functions static and migrating to MarkBuilder/InputContext APIs, we achieve:

✅ **Consistency**: Single API pattern for all parsers
✅ **Safety**: Type-safe fluent APIs
✅ **Maintainability**: Centralized element/map creation
✅ **Error tracking**: Built-in position tracking and error collection
✅ **Extensibility**: Easy to add new parsers following established patterns

The migration path is well-defined, testable, and reversible. Each phase builds on the previous, allowing for incremental delivery and validation.

**Status**: Ready for implementation
**Next Step**: Begin Week 1 - Foundation & Simple Migrations
