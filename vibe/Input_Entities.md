# HTML/XML Entity and Emoji Shortcode Support Enhancement Plan

## Overview

This document outlines a plan to structurally enhance Lambda/Radiant to properly support HTML/XML entities and emoji shortcodes using a unified Symbol-based representation. Currently:
- HTML entities like `&copy;` are decoded to UTF-8 at parse time (losing original representation)
- Markdown emoji shortcodes like `:smile:` are resolved to UTF-8 emoji and wrapped in `<emoji>` elements

This enhancement will unify both mechanisms to store named references as Lambda Symbols, enabling roundtrip compatibility and consistent rendering.

## Current State Analysis

### 1. HTML Parser (`lambda/input/input-html-scan.cpp`)

The current HTML parser handles entities in `html_parse_string_content()`:
- **ASCII escapes** (`&lt;`, `&gt;`, `&amp;`, `&quot;`, `&apos;`) are correctly decoded to their character equivalents
- **Numeric character references** (`&#65;`, `&#x41;`) are correctly decoded to UTF-8
- **Named entities** (80+ including `&copy;`, `&nbsp;`, `&reg;`, etc.) are decoded to their UTF-8 character equivalents
- **Unknown entities** are preserved as-is (e.g., `&unknownEntity;` stays as literal text)

**Key File**: `lambda/input/input-html-scan.cpp` (lines 26-164)
- `html_entities[]` table maps ~80 named entities to their UTF-8 values
- `find_html_entity()` looks up entity names
- `html_parse_string_content()` handles entity decoding

### 2. XML Parser (`lambda/input/input-xml.cpp`)

The XML parser has a simpler entity resolution in `parse_string_content()`:
- **Only 5 predefined entities** are handled: `lt`, `gt`, `amp`, `quot`, `nbsp`
- **Numeric character references** are supported
- **Other entities** are not resolved

**Key Function**: `resolve_entity()` (lines 17-27)

### 3. Markdown Parser (`lambda/input/input-markup.cpp`)

The markdown parser handles emoji shortcodes in `parse_emoji_shortcode()`:
- **Emoji shortcodes** (`:smile:`, `:heart:`, `:rocket:`, etc.) are resolved to UTF-8 emoji characters
- A comprehensive lookup table `emoji_map[]` (lines 2893-3447) maps ~400+ shortcodes to emoji
- Resolved emoji is wrapped in an `<emoji>` element with the UTF-8 character as content

**Current Flow**:
```
Input: "Hello :smile: World"
Output: List[
    String("Hello "),
    Element<emoji>[String("😄")],
    String(" World")
]
```

**Key Files**:
- `lambda/input/input-markup.cpp` (lines 2893-3510)
- `emoji_map[]` static table with ~400+ emoji mappings
- `parse_emoji_shortcode()` function

**Current Limitation**: The original shortcode name (e.g., `smile`) is lost after parsing. Only the resolved UTF-8 emoji is stored, preventing roundtrip back to `:smile:`.

### 4. HTML/XML Formatters

**HTML Formatter** (`lambda/format/format-html.cpp`):
- Uses `format_html_string_safe()` to escape `<`, `>`, `&`, `"` characters
- Does not currently handle Symbol types
- No special handling for entity symbols

**XML Formatter** (`lambda/format/format-xml.cpp`):
- `format_xml_string()` escapes special characters
- Attempts to preserve existing entities (checks for `&...;` pattern)
- Does not handle Symbol types

### 5. Markdown Formatter

**Markdown Formatter** (`lambda/format/format-markdown.cpp`):
- Currently outputs `<emoji>` elements as-is or resolves to Unicode
- No mechanism to output `:shortcode:` format for roundtrip

### 6. DOM Text Nodes (`lambda/input/css/dom_node.hpp`, `dom_element.cpp`)

**DomText Structure**:
```cpp
struct DomText : public DomNode {
    const char* text;            // Text content
    size_t length;               // Text length
    String* native_string;       // Pointer to backing Lambda String
    TextRect *rect;              // Layout rectangles
    FontProp *font;              // Font for this text
};
```

**Current Limitation**: DomText only stores `String*` (text), not `Symbol*` (entity/emoji references). Text nodes cannot represent unresolved entities or emoji shortcodes.

### 7. Radiant Text Rendering

**Text Wrapping** (`radiant/text_wrapping.cpp`):
- Gets text content via `DomText::text` pointer
- Passes text directly to font rendering
- No entity/emoji expansion at render time

---

## Proposed Enhancement

### Design Goals

1. **ASCII escapes** (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`) → Convert to literal characters at parse time (current behavior, maintain)
2. **Numeric character references** (`&#65;`, `&#x41;`) → Convert to UTF-8 at parse time (current behavior, maintain)
3. **Named HTML/XML entities** (`&copy;`, `&nbsp;`, `&reg;`, etc.) → Store as Lambda Symbol for deferred resolution
4. **Emoji shortcodes** (`:smile:`, `:heart:`, `:rocket:`, etc.) → Store as Lambda Symbol for deferred resolution
5. **Roundtrip compatibility** → Formatters should output `&copy;` or `:smile:` when the source used those formats
6. **Radiant rendering** → Resolve symbols to glyphs at render time

### Unified Symbol-Based Representation

Both HTML entities and emoji shortcodes will use the same Lambda Symbol mechanism:

| Source Format | Stored As | Formatter Output | Rendered As |
|--------------|-----------|------------------|-------------|
| `&copy;` | `Symbol("copy")` | `&copy;` | © |
| `&nbsp;` | `Symbol("nbsp")` | `&nbsp;` | (non-breaking space) |
| `:smile:` | `Symbol("smile")` | `:smile:` | 😄 |
| `:heart:` | `Symbol("heart")` | `:heart:` | ❤️ |

**Key Insight**: Both entity names (`copy`) and emoji shortcodes (`smile`) are identifiers that map to Unicode codepoints. Using Symbol for both provides:
- Unified data model
- Consistent roundtrip behavior
- Single resolution mechanism in Radiant

---

## Implementation Plan

### Phase 1: Parser Enhancements

#### 1.1 Create Entity Resolution Module

**New File**: `lambda/input/html_entities.h` / `html_entities.cpp`

```cpp
// Entity resolution result
typedef enum {
    ENTITY_ASCII,     // ASCII escape (convert inline)
    ENTITY_UNICODE,   // Unicode character (convert to UTF-8)
    ENTITY_SYMBOL,    // Named entity (store as symbol)
    ENTITY_UNKNOWN    // Unknown (preserve as text)
} EntityType;

typedef struct {
    EntityType type;
    union {
        char ascii_char;          // For ENTITY_ASCII
        uint32_t codepoint;       // For ENTITY_UNICODE
        const char* symbol_name;  // For ENTITY_SYMBOL (points to entity_name input)
    };
    size_t entity_length;        // Length of entity reference including & and ;
} EntityResult;

// Resolve HTML entity by name
EntityResult resolve_html_entity(const char* entity_name, size_t len);

// Check if character needs entity encoding in output
bool char_needs_entity_encoding(char c);
```

**Entity Classification**:
- ASCII escapes (`lt`, `gt`, `amp`, `quot`, `apos`): `ENTITY_ASCII`
- All other named entities (`copy`, `nbsp`, `reg`, etc.): `ENTITY_SYMBOL`
- Numeric references: `ENTITY_UNICODE`
- Unknown: `ENTITY_UNKNOWN`

#### 1.2 Modify HTML Parser (`input-html-scan.cpp`)

**Changes to `html_parse_string_content()`**:

```cpp
// Current: appends decoded string to StringBuf
// New: returns a List of mixed String/Symbol items

// New function signature:
List* html_parse_text_content_with_entities(
    MarkBuilder& builder,
    const char **html,
    char end_char
);
```

**Algorithm**:
1. Accumulate regular text in StringBuf
2. When encountering `&`:
   - If accumulated text exists, flush to String item and add to list
   - Parse entity reference
   - If ASCII escape → append decoded char to current StringBuf
   - If Symbol entity → create Symbol item and add to list
   - If numeric → decode to UTF-8 and append to current StringBuf
   - If unknown → preserve literal text `&entityname;`
3. Return list of mixed String/Symbol items (or single String if no symbols)

**Example Parse Result**:
```
Input: "Hello &copy; 2024 &amp; beyond"
Output: List[
    String("Hello "),
    Symbol("copy"),
    String(" 2024 & beyond")
]
```

#### 1.3 Modify XML Parser (`input-xml.cpp`)

Apply same changes to `parse_string_content()` in XML parser, but with XML entity set (fewer named entities).

#### 1.4 Modify Markdown Parser (`input-markup.cpp`)

**Changes to `parse_emoji_shortcode()`**:

Current behavior creates an `<emoji>` element with resolved UTF-8 emoji. New behavior:

```cpp
// Current: creates <emoji "😄">
// New: creates Symbol("smile") directly as inline content

static Item parse_emoji_shortcode(MarkupParser* parser, const char** text) {
    // ... parse :shortcode: ...

    // Look up shortcode in emoji_map[]
    const char* emoji_char = lookup_emoji(shortcode);

    if (!emoji_char) {
        // Unknown shortcode - preserve as text
        return (Item){.item = ITEM_ERROR};
    }

    // NEW: Create Symbol instead of <emoji> element
    String* symbol = create_symbol(parser, shortcode_name);  // e.g., "smile"
    return (Item){.item = sym2it(symbol)};  // Return as Symbol type
}
```

**Example Parse Result**:
```
Input: "Hello :smile: World"
Output: List[
    String("Hello "),
    Symbol("smile"),
    String(" World")
]
```

**Note**: The `<emoji>` element wrapper is no longer needed. Symbols are stored inline as children, just like HTML entities.

#### 1.5 Update Element Builder

Modify `html_append_child()` or equivalent to handle:
- Single String → append as before
- List of String/Symbol → append each item as child
- Symbol → append directly as child

### Phase 2: Data Model Enhancements

#### 2.1 Extend DomText to Support Mixed Content

**Option A: TypeId Field in DomText** (Preferred)

Modify `DomText` structure to track content type:

```cpp
struct DomText : public DomNode {
    // Existing fields
    const char* text;
    size_t length;
    String* native_string;

    // New field for mixed content support
    TypeId content_type;    // LMD_TYPE_STRING or LMD_TYPE_SYMBOL

    // For symbol content (alternative to native_string)
    union {
        String* native_string;  // When content_type == LMD_TYPE_STRING
        String* symbol;         // When content_type == LMD_TYPE_SYMBOL
    } source;

    // Layout fields (unchanged)
    TextRect *rect;
    FontProp *font;
};
```

**Option B: Multiple DomText Nodes per Element**

Instead of mixing String/Symbol in one DomText, create separate DomText nodes for each content segment. This is simpler but creates more nodes.

**Recommendation**: Use Option A for cleaner representation.

#### 2.2 Create DomSymbol Node Type (Alternative)

If Option A becomes complex, create a dedicated `DomSymbol` node:

```cpp
struct DomSymbol : public DomNode {
    String* symbol_name;     // Entity name (e.g., "copy")
    uint32_t codepoint;      // Resolved Unicode codepoint (cached)
    bool resolved;           // Whether codepoint is valid
};
```

### Phase 3: Formatter Enhancements

#### 3.1 HTML Formatter (`format-html.cpp`)

Add handling for Symbol items in `format_item_reader()`:

```cpp
else if (item.isSymbol()) {  // Need to add isSymbol() to ItemReader
    String* sym = item.asSymbol();
    if (sym && sym->chars) {
        // Output as HTML entity reference
        stringbuf_append_char(ctx.output(), '&');
        stringbuf_append_str(ctx.output(), sym->chars);
        stringbuf_append_char(ctx.output(), ';');
    }
}
```

#### 3.2 XML Formatter (`format-xml.cpp`)

Add similar Symbol handling to `format_item_reader()`.

#### 3.3 Markdown Formatter (`format-markdown.cpp`)

Add handling for Symbol items to output emoji shortcodes:

```cpp
else if (item.isSymbol()) {
    String* sym = item.asSymbol();
    if (sym && sym->chars) {
        // Check if this is an emoji shortcode (exists in emoji_map)
        if (is_emoji_shortcode(sym->chars)) {
            // Output as :shortcode:
            stringbuf_append_char(ctx.output(), ':');
            stringbuf_append_str(ctx.output(), sym->chars);
            stringbuf_append_char(ctx.output(), ':');
        } else {
            // Assume HTML entity, resolve to Unicode for markdown
            const char* utf8 = resolve_entity_to_utf8(sym->chars);
            if (utf8) {
                stringbuf_append_str(ctx.output(), utf8);
            }
        }
    }
}
```

**Note**: Markdown formatter needs to distinguish between emoji shortcodes and HTML entities since markdown uses different syntax for each.

#### 3.4 Add Symbol Support to ItemReader

**File**: `lambda/mark_reader.hpp`

```cpp
class ItemReader {
    // ... existing methods ...

    bool isSymbol() const;
    String* asSymbol() const;
};
```

**File**: `lambda/mark_reader.cpp`

```cpp
bool ItemReader::isSymbol() const {
    return cached_type_ == LMD_TYPE_SYMBOL;
}

String* ItemReader::asSymbol() const {
    if (cached_type_ != LMD_TYPE_SYMBOL) return nullptr;
    return (String*)item_.pointer;
}
```

### Phase 4: Radiant Rendering Enhancements

#### 4.1 Unified Symbol Resolution

**New File**: `radiant/symbol_resolver.cpp`

```cpp
// Unified resolution table for both HTML entities and emoji shortcodes
// Merges html_entities[] and emoji_map[] into single lookup

// Symbol types for different resolution behavior
typedef enum {
    SYMBOL_HTML_ENTITY,   // e.g., "copy" → ©
    SYMBOL_EMOJI,         // e.g., "smile" → 😄
    SYMBOL_UNKNOWN        // Not found in either table
} SymbolType;

typedef struct {
    SymbolType type;
    uint32_t codepoint;      // Primary codepoint (or first codepoint for emoji sequences)
    const char* utf8;        // Pre-computed UTF-8 string
} SymbolResolution;

// Resolve any symbol (entity or emoji) to its Unicode representation
SymbolResolution resolve_symbol(const char* symbol_name);

// Convenience function returning UTF-8 string
const char* resolve_symbol_to_utf8(const char* symbol_name, Arena* arena);
```

**Resolution Priority**:
1. Check emoji_map[] first (since emoji shortcodes are more specific)
2. Fall back to html_entities[] table
3. Return SYMBOL_UNKNOWN if not found

#### 4.2 Modify DOM Tree Building

**File**: `lambda/input/css/dom_element.cpp`

In `build_dom_tree_from_element()`, handle Symbol children:

```cpp
} else if (child_type == LMD_TYPE_SYMBOL) {
    // Entity symbol - create DomText with symbol type
    String* symbol_str = (String*)child_item.pointer;
    if (symbol_str && symbol_str->len > 0) {
        DomText* text_node = dom_text_create_symbol(symbol_str, dom_elem);
        if (text_node) {
            // ... append to DOM tree ...
        }
    }
}
```

#### 4.3 Modify Text Rendering

**File**: `radiant/text_wrapping.cpp`

When processing text content, check for symbol type:

```cpp
void wrap_text_in_layout_context(LayoutContext* lycon, void* text_node, int max_width) {
    DomNode* node = (DomNode*)text_node;
    DomText* text = node->as_text();

    const char* content;
    if (text->content_type == LMD_TYPE_SYMBOL) {
        // Resolve symbol to Unicode character
        content = resolve_entity_to_utf8(text->symbol->chars, lycon->arena);
    } else {
        content = text->text;
    }

    // Continue with text wrapping using resolved content
    // ...
}
```

### Phase 5: Testing

#### 5.1 Parser Unit Tests

**File**: `test/test_html_gtest.cpp`

Add/modify tests:

```cpp
TEST_F(HtmlParserTest, EntityParseAsSymbol) {
    Item result = parseHtml("<p>&copy;</p>");
    Element* p = result.element;
    ASSERT_NE(p, nullptr);

    // First child should be a Symbol with name "copy"
    Item child = p->items[0];
    EXPECT_EQ(get_type_id(child), LMD_TYPE_SYMBOL);
    String* sym = (String*)child.pointer;
    EXPECT_STREQ(sym->chars, "copy");
}

TEST_F(HtmlParserTest, EntityMixedContent) {
    Item result = parseHtml("<p>Hello &copy; World</p>");
    // Should have: String("Hello "), Symbol("copy"), String(" World")
    // ...
}

TEST_F(HtmlParserTest, EntityAsciiEscape) {
    Item result = parseHtml("<p>&lt; &amp; &gt;</p>");
    std::string text = getTextContent(result);
    // ASCII escapes should be decoded inline
    EXPECT_EQ(text, "< & >");
}
```

#### 5.2 Formatter Unit Tests

**File**: `test/test_html_roundtrip_gtest.cpp`

```cpp
TEST_F(HtmlRoundtripTest, EntityRoundtrip) {
    const char* html = "<p>&copy; 2024</p>";
    Item parsed = parseHtml(html);
    String* formatted = format_html(pool, parsed);

    // Should contain &copy; in output
    EXPECT_NE(strstr(formatted->chars, "&copy;"), nullptr);
}
```

#### 5.3 Markdown Parser Unit Tests

**File**: `test/test_markdown_gtest.cpp`

```cpp
TEST_F(MarkdownParserTest, EmojiParseAsSymbol) {
    Item result = parseMarkdown("Hello :smile: World");
    // Should contain Symbol("smile") as child
    // ...
}

TEST_F(MarkdownParserTest, EmojiMixedContent) {
    Item result = parseMarkdown("I :heart: Lambda :rocket:");
    // Should have: String("I "), Symbol("heart"), String(" Lambda "), Symbol("rocket")
    // ...
}

TEST_F(MarkdownParserTest, UnknownEmojiPreserved) {
    Item result = parseMarkdown("Hello :unknown_emoji: World");
    // Unknown shortcode should be preserved as literal text ":unknown_emoji:"
    // ...
}
```

#### 5.4 Markdown Formatter Unit Tests

**File**: `test/test_markdown_roundtrip_gtest.cpp`

```cpp
TEST_F(MarkdownRoundtripTest, EmojiRoundtrip) {
    const char* md = "Hello :smile: World";
    Item parsed = parseMarkdown(md);
    String* formatted = format_markdown(pool, parsed);

    // Should contain :smile: in output
    EXPECT_NE(strstr(formatted->chars, ":smile:"), nullptr);
}
```

#### 5.5 Radiant Rendering Tests

**File**: `test/test_layout_entities.cpp` (new)

```cpp
TEST_F(LayoutTest, EntityRendersCopyright) {
    // Load HTML with &copy; entity
    // Perform layout
    // Verify text content contains © character
}

TEST_F(LayoutTest, EmojiRendersSmile) {
    // Load markdown with :smile: emoji
    // Perform layout
    // Verify text content contains 😄 character
}
```

---

## File Change Summary

### New Files
1. `lambda/input/html_entities.h` - Entity resolution API
2. `lambda/input/html_entities.cpp` - Entity resolution implementation
3. `radiant/symbol_resolver.cpp` - Unified symbol resolution (entities + emoji)

### Modified Files

#### Parser Layer
1. `lambda/input/input-html-scan.cpp` - Parse entities as symbols
2. `lambda/input/input-html.cpp` - Handle mixed String/Symbol children
3. `lambda/input/input-xml.cpp` - Same entity handling for XML
4. `lambda/input/input-markup.cpp` - Parse emoji shortcodes as symbols (refactor `parse_emoji_shortcode()`)

#### Data Model
5. `lambda/input/css/dom_node.hpp` - Add content_type to DomText
6. `lambda/input/css/dom_element.cpp` - Handle Symbol children in DOM building

#### Formatter Layer
7. `lambda/format/format-html.cpp` - Output symbols as entity references
8. `lambda/format/format-xml.cpp` - Output symbols as entity references
9. `lambda/format/format-markdown.cpp` - Output emoji symbols as `:shortcode:`
10. `lambda/mark_reader.hpp` - Add isSymbol(), asSymbol()
11. `lambda/mark_reader.cpp` - Implement symbol accessors

#### Radiant Layer
12. `radiant/text_wrapping.cpp` - Resolve entity/emoji symbols for rendering
13. `radiant/layout_inline.cpp` - Handle symbol nodes in inline layout

#### Tests
14. `test/test_html_gtest.cpp` - Entity parsing tests
15. `test/test_html_roundtrip_gtest.cpp` - Entity roundtrip tests
16. `test/test_markdown_gtest.cpp` - Emoji parsing tests
17. `test/test_markdown_roundtrip_gtest.cpp` - Emoji roundtrip tests
18. `test/test_layout_entities.cpp` - Rendering tests for both entities and emoji

---

## Migration Strategy

### Backward Compatibility

The change should be backward compatible because:
1. Existing code that only handles String children will still work (most cases)
2. ASCII escapes continue to be decoded inline
3. Numeric references continue to be decoded inline
4. Only named entities and emoji shortcodes change behavior

### Gradual Rollout

1. **Phase 1**: Implement parser changes with feature flag (can toggle between old/new behavior)
2. **Phase 2**: Enable new behavior, fix any failing tests
3. **Phase 3**: Implement formatter changes
4. **Phase 4**: Implement Radiant rendering changes
5. **Phase 5**: Remove feature flag, update documentation

### Breaking Change: `<emoji>` Element Removal

The current `<emoji>` element wrapper will be deprecated. Migration path:
- Old: `<emoji "😄">` (element with UTF-8 content)
- New: `Symbol("smile")` (inline symbol)

Code that specifically looks for `<emoji>` elements will need updating.

---

## Open Questions

1. **Entity Table Size**: Should we include all 2000+ HTML5 named character references, or stick with the current ~80 most common?

2. **Symbol Pooling**: Entity/emoji symbols are short and repeated. Should we ensure they're pooled via `createSymbol()` for memory efficiency?

3. **Error Handling**: What to do with malformed entities (e.g., `&copy` without semicolon)? Current behavior preserves them as text.

4. **XML Entities**: Should XML parser support the same entity set as HTML, or only the XML predefined entities?

5. **Emoji vs Entity Ambiguity**: Some names exist in both tables (e.g., hypothetically `heart`). Resolution priority: emoji first, then HTML entity.

6. **`<emoji>` Element Deprecation**: Should we keep backward compatibility for old `<emoji>` elements, or cleanly remove them?

---

## Appendix A: HTML5 Named Character References

The HTML5 spec defines 2,231 named character references. Key categories:

- **ISO 8859-1 symbols** (latin-1): `&copy;`, `&reg;`, `&deg;`, etc.
- **Mathematical symbols**: `&plusmn;`, `&times;`, `&divide;`, etc.
- **Greek letters**: `&alpha;`, `&beta;`, `&gamma;`, etc.
- **Currency symbols**: `&euro;`, `&pound;`, `&yen;`, etc.
- **Typography**: `&mdash;`, `&ndash;`, `&hellip;`, etc.
- **Arrows**: `&rarr;`, `&larr;`, `&uarr;`, etc.

For initial implementation, recommend supporting the ~80 most common entities (already in `html_entities[]`), with option to expand later.

## Appendix B: GitHub Emoji Shortcodes

The current `emoji_map[]` in `input-markup.cpp` contains ~400+ emoji shortcodes. Key categories:

- **Smileys & Emotion**: `:smile:`, `:joy:`, `:heart_eyes:`, etc.
- **People & Body**: `:wave:`, `:thumbsup:`, `:clap:`, etc.
- **Animals & Nature**: `:dog:`, `:cat:`, `:unicorn:`, etc.
- **Food & Drink**: `:apple:`, `:coffee:`, `:pizza:`, etc.
- **Objects**: `:rocket:`, `:computer:`, `:book:`, etc.
- **Symbols**: `:heart:`, `:star:`, `:checkmark:`, etc.

The emoji table is already comprehensive for GitHub-flavored markdown compatibility.

---

## References

- [HTML5 Named Character References](https://html.spec.whatwg.org/multipage/named-characters.html)
- [GitHub Emoji Cheat Sheet](https://github.com/ikatyang/emoji-cheat-sheet)
- Current HTML entity implementation: `lambda/input/input-html-scan.cpp`
- Current emoji implementation: `lambda/input/input-markup.cpp`
- Lambda data model: `lambda/lambda-data.hpp`
- DOM structure: `lambda/input/css/dom_node.hpp`
